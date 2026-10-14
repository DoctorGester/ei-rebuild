// CInterface3D's per-frame update (game.exe 005F57C0). See docs/notes/ui.md ("Interface frame").
#include "ui.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "originals_engine.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_core.h"

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
int CursorAt::SetShape(int a2) { return CursorSetShapeOriginal((void*)this, nullptr, a2); }
CursorAt* Cursor() { return reinterpret_cast<CursorAt*>(0x007C7BD8); }

struct SessionAt {
    bool IsMultiplayerClient();      // 0057F440 (007AFE20)
    void Disconnect();               // 0057ECA0
    void LeaveGame();                // 00581660
};
bool SessionAt::IsMultiplayerClient() { return IsNetworkClientOriginal(); }
void SessionAt::Disconnect() { GameDisconnectOriginal(this); }
void SessionAt::LeaveGame() { FlowCloseSessionsOriginal(this); }
SessionAt* Session() { return reinterpret_cast<SessionAt*>(0x007AFE20); }

struct GameStateAt {
    void SetServerFlag(bool flag);   // 00578D10 (007AFEA4): the world server 007AFF60 +0x6C
};
void GameStateAt::SetServerFlag(bool f) { SetServerFlagOriginal(f); }

struct SurfaceAt {
    void Freeze(int save);           // 005FD7A0 saves the surface's contents
};
void SurfaceAt::Freeze(int save) { SurfaceSwapContentsOriginal(this, nullptr, save); }

// 00634650: the message box (0x180 bytes): owner, caption and text ids, the value it leaves
// pending, two flags.
struct MessageBoxAt {
    void* Construct(CInterface3D* ui, const char* caption, const char* text, std::uint32_t value, int a, int b);
};
void* MessageBoxAt::Construct(CInterface3D* a2, const char* a3, const char* a4, std::uint32_t a5, int a6, int a7) { return MessageBoxConstructOriginal((void*)this, nullptr, a2, a3, a4, (int)a5, a6, a7); }

// 00458220: dispatches the window messages waiting in the queue.

void ReleaseBackdrop(void* surface) {
    if (!surface) return;
    SurfaceReleaseContentsOriginal(static_cast<CUiSurface*>(surface));
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
        PumpMessagesOriginal();
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
        reinterpret_cast<InterfaceOriginals*>(this)->ResetInput();
        PumpMessagesOriginal();
        *reinterpret_cast<bool*>(0x00784750) = false;
    }
    if (f.queuedWindow) {
        reinterpret_cast<InterfaceOriginals*>(this)->Activate(f.queuedWindow);
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
        InterfacePushWindowOriginal(this, nullptr, box, 1, 1);
    } else if (stackBegin_ != stackEnd_) {
        CallSlot<void>(stackEnd_[-1], 24);
    }
    CallSlot<void>(f.root, 24);
}
// ---- String events (005F8070) ------------------------------------------------------------------

namespace {

// 00461360 (ECX text). A number from the text (its characters' upper-case codes mixed).
// The tutorial queue (+0x84, an STLport list of CStrings): end() into `out` (005F97D0) and
// insert(position, value) returning the new node into `out` (005F97E0).
__declspec(naked) void __fastcall QueueEnd(void*, void*, void** /*out*/) EI_ORIGINAL(0x005F97D0)
__declspec(naked) void** __fastcall QueueInsert(void*, void*, void** /*out*/, void* /*position*/, const mfc::CString*)
    EI_ORIGINAL(0x005F97E0)
// Rebirth's game over message: the menu object at 007AFEA5 (00578920 with 1, then 00578940), the
// message window (0x180 bytes, constructor 00634650: interface, title, text, 0x20000, 3, 1) and
// the interface's ShowWindow (005F7350: window, 1, 1).
void __fastcall MenuSet578920(void*, void*, int a2) { MenuSetOriginal(a2); }
void __fastcall MenuCall578940(void*, void*) { MenuCallOriginal(); }
void* __fastcall MessageWindowConstruct(void* a0, void*, CInterface3D* a2, const char* a3, const char* a4, int a5, int a6, int a7) { return MessageBoxConstructOriginal(a0, nullptr, a2, a3, a4, a5, a6, a7); }

constexpr int kSlotShowBriefing = 34;   // top window (+0x88): (text)
constexpr int kSlotEndOfGame = 31;      // top window (+0x7C): (1)
constexpr int kSlotSay = 32;            // top window (+0x80): (number, 0x2A say / 0x2B say_block, text)
int Compare(const mfc::CString& word, std::uintptr_t text) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(word)),
                           reinterpret_cast<const unsigned char*>(text));
}

} // namespace

void CInterface3D::OnStringEvent(const char* event) {
    if (!event) return;
    console::CStringArray words;
    mfc::CString unused;
    mfc::CString text(event);
    SplitWords(text, &words, ' ');
    words.data[0].MakeUpper();
    // Rebirth's NewStringEvents (context hook at 005F80EF): "gameover [title [text]]" plays
    // buttons\gameover.wav and shows a message window (the texts default to the keys game_over and
    // game_over_msg).
    if (ei::rebirth::Enabled(ei::rebirth::Patch::NewStringEvents) && std::strcmp(words.data[0], "gameover") == 0) {
        void* menu = reinterpret_cast<void*>(0x007AFEA5);
        MenuSet578920(menu, nullptr, 1);
        MenuCall578940(menu, nullptr);
        PlaySound("buttons\\gameover.wav", 0);
        const char* title = words.size > 1 ? static_cast<const char*>(words.data[1]) : "game_over";
        const char* message = words.size > 2 ? static_cast<const char*>(words.data[2]) : "game_over_msg";
        void* memory = gamecrt::operator_new(0x180);
        void* window = memory ? MessageWindowConstruct(memory, nullptr, this, title, message, 0x20000, 3, 1) : nullptr;
        InterfacePushWindowOriginal(this, nullptr, window, 1, 1);
        return;
    }
    auto top = [this]() { return stackEnd_[-1]; };
    if (Compare(words.data[0], 0x007930E8) == 0 || Compare(words.data[0], 0x007930DC) == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size < 2) return;
        int number = words.size > 2 ? TextKeyOf(words.data[2]) : 0;
        int kind = Compare(words.data[0], 0x007930DC) != 0 ? 0x2A : 0x2B;
        if (stackBegin_ != stackEnd_)
            ei::VCall<void>(top(), kSlotSay, number, kind, static_cast<const char*>(words.data[1]));
    } else if (Compare(words.data[0], 0x007930D0) == 0) {   // "tutorial" name: queued
        if (words.size < 2) return;
        void* queue = reinterpret_cast<std::uint8_t*>(this) + 0x84;
        void* end;
        QueueEnd(queue, nullptr, &end);
        void* out;
        QueueInsert(queue, nullptr, &out, end, &words.data[1]);
    } else if (Compare(words.data[0], 0x007930C4) == 0) {   // "briefing" name
        if (words.size < 2 || stackBegin_ == stackEnd_) return;
        ei::VCall<void>(top(), kSlotShowBriefing, static_cast<const char*>(words.data[1]));
    } else if (Compare(words.data[0], 0x00787704) == 0) {   // "autosave"
        reinterpret_cast<std::uint8_t*>(this)[0x81] = 1;
    } else if (Compare(words.data[0], 0x00790664) == 0) {   // "endofgame"
        if (stackBegin_ != stackEnd_) ei::VCall<void>(top(), kSlotEndOfGame, 1);
    }
}

EI_REPLACE_METHOD(0x005F57C0, CInterface3D::Update);
EI_REPLACE_METHOD(0x005F8070, CInterface3D::OnStringEvent);
EI_REBIRTH_IMPLEMENTS(0x005F8070, ei::rebirth::Patch::NewStringEvents);
