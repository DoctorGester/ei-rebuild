// CInterface3D's per-frame update (game.exe 005F57C0). See docs/notes/ui.md ("Interface frame").
#include "ui.h"

#include "lib/crt.h"

#include "ei/hook.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {

// Members of CInterface3D only the frame update uses (offsets into the object; ui.h keeps them
// opaque).
struct InterfaceFields {
    std::uint8_t unknown_00[0x0C];
    void* mouseCapture;              // +0x0C
    std::uint8_t unknown_10[0x1C - 0x10];
    CUiWindow* root;                 // +0x1C updated last every frame (slot 24)
    std::uint8_t unknown_20[0x40 - 0x20];
    CUiWindow* queuedWindow;         // +0x40 activated by the next frame (005F71C0)
    bool closeAll;                   // +0x44 close every window (set by result 0x990001)
    std::uint8_t unknown_45[0x48 - 0x45];
    std::uint32_t pendingValue;      // +0x48
    bool pendingFlag;                // +0x4C
    std::uint8_t unknown_4D[0x50 - 0x4D];
    int backdropState;               // +0x50 4: switch to the next backdrop
    void* backdrop;                  // +0x54 CUiSurface behind the windows
    void* nextBackdrop;              // +0x58
    void* backdropPixels;            // +0x5C
    bool serverFlag;                 // +0x60 handed to the world server (+0x6C) when the last window closes
    std::uint8_t unknown_61[0x68 - 0x61];
    int mouseX;                      // +0x68
    int mouseY;                      // +0x6C
    std::uint8_t unknown_70[0x7C - 0x70];
    bool keys[5];                    // +0x7C..+0x80 modifier keys
    bool pollTop;                    // +0x81 asks the top window (slot 35) every frame until it agrees
    std::uint8_t unknown_82[0x88 - 0x82];
    bool disconnected;               // +0x88 a network client lost the server
};
static_assert(offsetof(InterfaceFields, root) == 0x1C && offsetof(InterfaceFields, queuedWindow) == 0x40 &&
              offsetof(InterfaceFields, pendingValue) == 0x48 && offsetof(InterfaceFields, backdropState) == 0x50 &&
              offsetof(InterfaceFields, serverFlag) == 0x60 && offsetof(InterfaceFields, mouseX) == 0x68 &&
              offsetof(InterfaceFields, keys) == 0x7C && offsetof(InterfaceFields, pollTop) == 0x81 &&
              offsetof(InterfaceFields, disconnected) == 0x88);

// The value a window leaves pending that ends the game session (the message boxes' "yes").
constexpr std::uint32_t kLeaveGame = 0x990001;

template <typename R, typename... A>
R CallSlot(void* object, int slot, A... args) {
    using Fn = R(__thiscall*)(void*, A...);
    return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot])(object, args...);
}

// Window slots: main vtable 0 deleting destructor, 19 show children, 24 update, 26 deactivate,
// 27 pending value, 35 ready to stop polling; input side (window + 0x94) slot 1 cancel input.
void CancelInput(CUiWindow* w) { CallSlot<void>(w->InputSide(), 1); }

// Not rewritten.
struct CursorAt {
    int SetShape(int shape);         // 005E5200 (the mouse cursor 007C7BD8)
};
__declspec(naked) int CursorAt::SetShape(int) EI_ORIGINAL(0x005E5200)
CursorAt* Cursor() { return reinterpret_cast<CursorAt*>(0x007C7BD8); }

struct SessionAt {
    bool IsMultiplayerClient();      // 0057F440 (007AFE20)
    void Disconnect();               // 0057ECA0
    void LeaveGame();                // 00581660
};
__declspec(naked) bool SessionAt::IsMultiplayerClient() EI_ORIGINAL(0x0057F440)
__declspec(naked) void SessionAt::Disconnect() EI_ORIGINAL(0x0057ECA0)
__declspec(naked) void SessionAt::LeaveGame() EI_ORIGINAL(0x00581660)
SessionAt* Session() { return reinterpret_cast<SessionAt*>(0x007AFE20); }

struct GameStateAt {
    void SetServerFlag(bool flag);   // 00578D10 (007AFEA4): the world server 007AFF60 +0x6C
};
__declspec(naked) void GameStateAt::SetServerFlag(bool) EI_ORIGINAL(0x00578D10)

struct SurfaceAt {
    void ReleaseContents();          // 005FD120
    void Freeze(int save);           // 005FD7A0 saves the surface's contents
};
__declspec(naked) void SurfaceAt::ReleaseContents() EI_ORIGINAL(0x005FD120)
__declspec(naked) void SurfaceAt::Freeze(int) EI_ORIGINAL(0x005FD7A0)

struct InterfaceAt : CInterface3D {
    void Activate(CUiWindow* window);                       // 005F71C0
    void Push(void* window, int a, int b);                  // 005F7350
    void AfterCloseAll();                                   // 005F8C50
};
__declspec(naked) void InterfaceAt::Activate(CUiWindow*) EI_ORIGINAL(0x005F71C0)
__declspec(naked) void InterfaceAt::Push(void*, int, int) EI_ORIGINAL(0x005F7350)
__declspec(naked) void InterfaceAt::AfterCloseAll() EI_ORIGINAL(0x005F8C50)

// 00634650: the message box (0x180 bytes): owner, caption and text ids, the value it leaves
// pending, two flags.
struct MessageBoxAt {
    void* Construct(CInterface3D* ui, const char* caption, const char* text, std::uint32_t value, int a, int b);
};
__declspec(naked) void* MessageBoxAt::Construct(CInterface3D*, const char*, const char*, std::uint32_t, int, int)
    EI_ORIGINAL(0x00634650)

// 00458220: dispatches the window messages waiting in the queue.
__declspec(naked) void __cdecl PumpMessages() EI_ORIGINAL(0x00458220)

void ReleaseBackdrop(void* surface) {
    if (!surface) return;
    static_cast<SurfaceAt*>(surface)->ReleaseContents();
    gamecrt::operator_delete(surface);
}

} // namespace

void CInterface3D::Update() {
    InterfaceFields& f = *reinterpret_cast<InterfaceFields*>(this);
    // A pending value closes the top window: it is cancelled, deactivated and deleted, the
    // backdrop changes when asked to, the last window closing drops the backdrop and hands +0x60
    // to the world server (single player), and the window below gets the value.
    while (f.pendingFlag) {
        f.pendingFlag = false;
        Cursor()->SetShape(3);
        f.keys[0] = false;
        f.keys[2] = false;
        f.keys[1] = false;
        f.keys[4] = false;
        f.keys[3] = false;
        if (stackBegin_ != stackEnd_) CancelInput(stackEnd_[-1]);
        if (stackBegin_ != stackEnd_) {
            CallSlot<void>(stackEnd_[-1], 26);
            if (CUiWindow* top = stackEnd_[-1]) CallSlot<void>(top, 0, 1);
            --stackEnd_;
        }
        PumpMessages();
        if (f.backdropState == 4) {
            ReleaseBackdrop(f.backdrop);
            void* next = f.nextBackdrop;
            f.nextBackdrop = nullptr;
            f.backdrop = next;
            f.backdropState = next ? 2 : 0;
            if (next) static_cast<SurfaceAt*>(next)->Freeze(1);
        }
        if (stackEnd_ - stackBegin_ == 1) {
            ReleaseBackdrop(f.backdrop);
            f.backdrop = nullptr;
            if (f.backdropPixels) gamecrt::operator_delete(f.backdropPixels);
            f.backdropPixels = nullptr;
            f.backdropState = 0;
            if (!Session()->IsMultiplayerClient())
                reinterpret_cast<GameStateAt*>(0x007AFEA4)->SetServerFlag(f.serverFlag);
            f.serverFlag = false;
        }
        if (stackBegin_ != stackEnd_) {
            CallSlot<void>(stackEnd_[-1], 19, 1);
            CallSlot<void>(stackEnd_[-1], 27, f.pendingValue);
        }
        if (f.pendingValue == kLeaveGame) {
            f.closeAll = true;
            Session()->Disconnect();
            Session()->LeaveGame();
        }
        f.mouseCapture = nullptr;
        Cursor()->SetShape(0);
    }
    if (f.pollTop && stackBegin_ != stackEnd_) f.pollTop = !CallSlot<bool>(stackEnd_[-1], 35);
    if (f.closeAll) {
        f.closeAll = false;
        Cursor()->SetShape(3);
        while (stackBegin_ != stackEnd_) {
            CancelInput(stackEnd_[-1]);
            CallSlot<void>(stackEnd_[-1], 26);
            if (CUiWindow* top = stackEnd_[-1]) CallSlot<void>(top, 0, 1);
            --stackEnd_;
        }
        static_cast<InterfaceAt*>(this)->AfterCloseAll();
        PumpMessages();
        *reinterpret_cast<bool*>(0x00784750) = false;
    }
    if (f.queuedWindow) {
        static_cast<InterfaceAt*>(this)->Activate(f.queuedWindow);
        f.queuedWindow = nullptr;
    }
    f.mouseX = *reinterpret_cast<int*>(0x007C7C50);
    f.mouseY = *reinterpret_cast<int*>(0x007C7C54);
    if (f.disconnected && Session()->IsMultiplayerClient() &&
        *reinterpret_cast<int*>(*reinterpret_cast<std::uint8_t**>(0x0079B5D8) + 0x2598) != 2) {
        f.disconnected = false;
        void* memory = gamecrt::operator_new(0x180);
        void* box = memory ? static_cast<MessageBoxAt*>(memory)->Construct(
                                 this, reinterpret_cast<const char*>(0x0079242C),
                                 reinterpret_cast<const char*>(0x00793074), 0x990000, 1, 1)
                           : nullptr;
        static_cast<InterfaceAt*>(this)->Push(box, 1, 1);
    } else if (stackBegin_ != stackEnd_) {
        CallSlot<void>(stackEnd_[-1], 24);
    }
    CallSlot<void>(f.root, 24);
}
EI_REPLACE_METHOD(0x005F57C0, CInterface3D::Update);
