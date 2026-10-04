// CInterface3D::CreateScreen (005F5BA0): the screens by id. See ui.h and docs/notes/ui.md.
//
// The original inlines the constructors of screens 1, 2, 3 and 7 (the container base 00600B20,
// their members' constructors and vtables) and calls the others' constructors; ours does the same
// through the original addresses.
#include "ui_create_screen.h"

#include "ui.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

void SetDword(void* p, int offset, std::uint32_t value) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &value, 4); }
std::uint32_t EmptyString() { return *reinterpret_cast<const std::uint32_t*>(0x00796E48); }   // afxEmptyString

__declspec(naked) void __fastcall ConstructContainer(void*, void*, CInterface3D*) EI_ORIGINAL(0x00600B20)
__declspec(naked) void __fastcall ConstructIslandMap(void*, void*, CInterface3D*) EI_ORIGINAL(0x005F67A0)      // id 4
__declspec(naked) void __fastcall ConstructScreen5(void*, void*, CInterface3D*, int) EI_ORIGINAL(0x005F6240)    // id 5
__declspec(naked) void __fastcall ConstructScreen8(void*, void*, CInterface3D*, int, const void*) EI_ORIGINAL(0x005F6D50)
__declspec(naked) void __fastcall ConstructScreen9(void*, void*, CInterface3D*) EI_ORIGINAL(0x005F6F30)

// Member constructors (thiscall on the member).
__declspec(naked) void __fastcall Member5C1760(void*) EI_ORIGINAL(0x005C1760)
__declspec(naked) void __fastcall Member5F8F30(void*) EI_ORIGINAL(0x005F8F30)
__declspec(naked) void __fastcall Member5F6080(void*) EI_ORIGINAL(0x005F6080)
__declspec(naked) void __fastcall Member5F5F90(void*) EI_ORIGINAL(0x005F5F90)
__declspec(naked) void __fastcall StringArrayConstruct(void*) EI_ORIGINAL(0x006FE925)   // CStringArray
__declspec(naked) void __fastcall Member5FAF80(void*) EI_ORIGINAL(0x005FAF80)
// STLport containers taking an allocator (by address; empty).
__declspec(naked) void __fastcall Member5F90E0(void*, void*, const void*) EI_ORIGINAL(0x005F90E0)
__declspec(naked) void __fastcall Member5F9300(void*, void*, const void*) EI_ORIGINAL(0x005F9300)
__declspec(naked) void __fastcall Member5F8F10(void*, void*, const void*) EI_ORIGINAL(0x005F8F10)
__declspec(naked) void __fastcall Member5F9540(void*, void*, const void*) EI_ORIGINAL(0x005F9540)

struct InterfaceAt {
    void Activate(CUiWindow* window);   // 005F71C0
};
__declspec(naked) void InterfaceAt::Activate(CUiWindow*) EI_ORIGINAL(0x005F71C0)

std::uint8_t* At(void* p, int offset) { return static_cast<std::uint8_t*>(p) + offset; }

} // namespace

void CInterface3D::CreateScreen(int id) {
    void* screen = nullptr;
    char allocators[4] = {};
    switch (id) {
    case 0:
        return;
    case 1: {   // the in-game screen (ui_camp.h)
        screen = gamecrt::operator_new(0x230);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        SetDword(screen, 0x168, 0x007402AC);
        SetDword(screen, 0x16C, 0);
        SetDword(screen, 0x178, 0);
        SetDword(screen, 0x174, 0);
        SetDword(screen, 0x170, 0);
        Member5C1760(At(screen, 0x1DC));
        Member5F8F30(At(screen, 0x1F0));
        Member5F6080(At(screen, 0x204));
        Member5F5F90(At(screen, 0x220));
        SetDword(screen, 0x00, 0x00740764);
        SetDword(screen, 0x94, 0x00740730);
        break;
    }
    case 2:
        screen = gamecrt::operator_new(0x17C);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        SetDword(screen, 0x00, 0x00740688);
        SetDword(screen, 0x94, 0x00740654);
        break;
    case 3: {   // the base / briefing screen
        screen = gamecrt::operator_new(0x2A4);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        StringArrayConstruct(At(screen, 0x170));
        SetDword(screen, 0x184, EmptyString());
        Member5FAF80(At(screen, 0x190));
        Member5F90E0(At(screen, 0x240), nullptr, &allocators[0]);
        SetDword(screen, 0x248, EmptyString());
        Member5F8F30(At(screen, 0x26C));
        Member5F9300(At(screen, 0x278), nullptr, &allocators[1]);
        Member5F8F10(At(screen, 0x284), nullptr, &allocators[2]);
        Member5F9540(At(screen, 0x294), nullptr, &allocators[3]);
        SetDword(screen, 0x00, 0x007405AC);
        SetDword(screen, 0x94, 0x00740578);
        break;
    }
    case 4:
        screen = gamecrt::operator_new(0x294);
        if (screen) ConstructIslandMap(screen, nullptr, this);
        break;
    case 5:
        screen = gamecrt::operator_new(0x260);
        if (screen) ConstructScreen5(screen, nullptr, this, 0);
        break;
    case 7:
        screen = gamecrt::operator_new(0x168);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        SetDword(screen, 0x00, 0x00740840);
        SetDword(screen, 0x94, 0x0074080C);
        break;
    case 8:
        screen = gamecrt::operator_new(0x1FC);
        if (screen) ConstructScreen8(screen, nullptr, this, 0, reinterpret_cast<const void*>(0x00793084));
        break;
    case 9:
        screen = gamecrt::operator_new(0x1B8);
        if (screen) ConstructScreen9(screen, nullptr, this);
        break;
    default:   // 6 and above 9: the id itself
        screen = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<unsigned>(id)));
        break;
    }
    reinterpret_cast<InterfaceAt*>(this)->Activate(static_cast<CUiWindow*>(screen));
}

// --- 005F7350: PushWindow --------------------------------------------------------------------------

namespace {

struct CursorAt {
    int SetShape(int shape);   // 005E5200 (on 007C7BD8)
};
__declspec(naked) int CursorAt::SetShape(int) EI_ORIGINAL(0x005E5200)
CursorAt* Cursor() { return reinterpret_cast<CursorAt*>(0x007C7BD8); }

// 00458010 (__fastcall with two byte arguments).
__declspec(naked) void __fastcall GameState458010(char, char) EI_ORIGINAL(0x00458010)
// 00458220: the message pump.
__declspec(naked) void __cdecl PumpMessages() EI_ORIGINAL(0x00458220)
// 0057F440 (on 007AFE20): a multiplayer client session; returns AL.
__declspec(naked) int __fastcall IsClientAt(void*) EI_ORIGINAL(0x0057F440)

struct GlueAt {
    char ServerFlag();               // 00578D00 (on 007AFEA4)
    void SetServerFlag(char flag);   // 00578D10
};
__declspec(naked) char GlueAt::ServerFlag() EI_ORIGINAL(0x00578D00)
__declspec(naked) void GlueAt::SetServerFlag(char) EI_ORIGINAL(0x00578D10)
GlueAt* Glue() { return reinterpret_cast<GlueAt*>(0x007AFEA4); }

// 005FA9A0 (thiscall on the window stack, RET 0xC): the vector's insert when it is full.
__declspec(naked) void __fastcall StackInsertOverflow(void*, void*, CUiWindow** /*at*/, CUiWindow* const* /*value*/,
                                                      int /*count*/) EI_ORIGINAL(0x005FA9A0)

// IDirect3DDevice7::SetRenderState (COM, slot 20).
void SetRenderState(void* device, unsigned long state, unsigned long value) {
    using Fn = long(__stdcall*)(void*, unsigned long, unsigned long);
    reinterpret_cast<Fn>((*static_cast<void***>(device))[20])(device, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), static_cast<std::uint32_t>(value));
}

// The end both PushWindow and Activate share: the window pushed on the stack, the render states
// the interface draws with, the window created (slot 25), the messages pumped, the mouse released.
void OpenOnTop(CInterface3D* ui, CUiWindow* window) {
    if (ui->stackEnd_ != ui->stackCapacity_) {
        if (ui->stackEnd_) *ui->stackEnd_ = window;
        ++ui->stackEnd_;
    } else {
        StackInsertOverflow(&ui->stackBegin_, nullptr, ui->stackEnd_, &window, 1);
    }
    static const unsigned long kStates[5][2] = {{0x1B, 1}, {0x0F, 1}, {0x13, 5}, {0x14, 6}, {0x18, 0x80}};
    for (const auto& state : kStates) {
        void* device;
        std::memcpy(&device, reinterpret_cast<std::uint8_t*>(ui->renderer_) + 0x54, 4);
        SetRenderState(device, state[0], state[1]);
    }
    *reinterpret_cast<std::uint8_t*>(0x00784750) = 1;
    using CreateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<CreateFn>(window->vtable_[25])(window);
    PumpMessages();
    ui->mouseCapture_ = nullptr;
    Cursor()->SetShape(0);
}

} // namespace

void CInterface3D::PushWindow(CUiWindow* window, int a, int b) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    Cursor()->SetShape(3);
    bool first = static_cast<std::uint8_t>(a) != 0;
    bool second = static_cast<std::uint8_t>(b) != 0;
    if (first && !second) {
        SetDword(self, 0x50, 3);
        GameState458010(0, 1);
    }
    // One window open (the byte difference with its low two bits cleared is 4).
    std::uint32_t bytes = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stackEnd_) -
                                                     reinterpret_cast<std::uintptr_t>(stackBegin_));
    if ((bytes & ~3u) == 4) {
        self[0x60] = static_cast<std::uint8_t>(Glue()->ServerFlag());
        if ((IsClientAt(reinterpret_cast<void*>(0x007AFE20)) & 0xFF) == 0) Glue()->SetServerFlag(1);
        if (first && second) {
            SetDword(self, 0x50, 1);
            GameState458010(0, 1);
        }
    }
    self[0x7C] = 0;
    self[0x7E] = 0;
    self[0x7D] = 0;
    self[0x80] = 0;
    self[0x7F] = 0;
    if (stackBegin_ != stackEnd_) {
        void* input = reinterpret_cast<std::uint8_t*>(stackEnd_[-1]) + 0x94;
        using Fn = void(__thiscall*)(void*);
        reinterpret_cast<Fn>((*static_cast<void***>(input))[1])(input);
    }
    if (stackBegin_ != stackEnd_) {
        CUiWindow* top = stackEnd_[-1];
        using Fn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<Fn>(top->vtable_[19])(top, 0);
    }
    if (!window) game::FatalError(reinterpret_cast<const char*>(0x0079308C));
    OpenOnTop(this, window);
}

// --- 005F71C0: Activate ----------------------------------------------------------------------------

namespace {
struct InterfaceSteps {
    void CloseAllPending();   // 005F74F0
    void AfterCloseAll();     // 005F8C50
};
__declspec(naked) void InterfaceSteps::CloseAllPending() EI_ORIGINAL(0x005F74F0)
__declspec(naked) void InterfaceSteps::AfterCloseAll() EI_ORIGINAL(0x005F8C50)

void DeleteBackdrop(CUiSurface* surface) {
    if (!surface) return;
    surface->ReleaseContents();
    gamecrt::operator_delete(surface);
}
} // namespace

void CInterface3D::Activate(CUiWindow* window) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    auto* steps = reinterpret_cast<InterfaceSteps*>(this);
    if (self[0x44] != 0) {
        self[0x44] = 0;
        steps->CloseAllPending();
    }
    Cursor()->SetShape(3);
    CUiSurface* backdrop;
    std::memcpy(&backdrop, self + 0x54, 4);
    DeleteBackdrop(backdrop);
    std::memcpy(&backdrop, self + 0x58, 4);
    SetDword(self, 0x54, 0);
    DeleteBackdrop(backdrop);
    SetDword(self, 0x58, 0);
    SetDword(self, 0x50, 0);
    self[0x60] = 0;
    Glue()->SetServerFlag(0);
    Cursor()->SetShape(3);
    while (stackBegin_ != stackEnd_) {
        void* input = reinterpret_cast<std::uint8_t*>(stackEnd_[-1]) + 0x94;
        using VoidFn = void(__thiscall*)(void*);
        reinterpret_cast<VoidFn>((*static_cast<void***>(input))[1])(input);
        CUiWindow* top = stackEnd_[-1];
        reinterpret_cast<VoidFn>(top->vtable_[26])(top);
        top = stackEnd_[-1];
        if (top) {
            using DeleteFn = void(__thiscall*)(CUiWindow*, int);
            reinterpret_cast<DeleteFn>(top->vtable_[0])(top, 1);
        }
        --stackEnd_;
    }
    steps->AfterCloseAll();
    PumpMessages();
    *reinterpret_cast<std::uint8_t*>(0x00784750) = 0;
    if (!window) {
        game::FatalError(reinterpret_cast<const char*>(0x0079308C));
        return;
    }
    OpenOnTop(this, window);
}

namespace {
void __fastcall InterfaceActivate(CInterface3D* self, void*, CUiWindow* window) { self->Activate(window); }
} // namespace
EI_REPLACE(0x005F71C0, InterfaceActivate);

namespace {
void __fastcall InterfacePushWindow(CInterface3D* self, void*, CUiWindow* window, int a, int b) {
    self->PushWindow(window, a, b);
}
} // namespace
EI_REPLACE(0x005F7350, InterfacePushWindow);

namespace {
void __fastcall InterfaceCreateScreen(CInterface3D* self, void*, int id) { self->CreateScreen(id); }
} // namespace
EI_REPLACE(0x005F5BA0, InterfaceCreateScreen);

// --- 005F8A80: the message window ------------------------------------------------------------------

namespace {
// The message queue (a std::list of CStrings at +0x84; thiscall): 005F8E50 erases a node into
// the returned iterator (RET 8).
__declspec(naked) void __fastcall MessageListErase(void*, void*, void* /*out*/, void* /*node*/) EI_ORIGINAL(0x005F8E50)
__declspec(naked) void __fastcall CopyCString(void*, void*, const void*) EI_ORIGINAL(0x007006E5)
__declspec(naked) void __fastcall ConstructCString(void*, void*, const char*) EI_ORIGINAL(0x007009DE)
__declspec(naked) void __fastcall ConstructStringArrayAt(void*) EI_ORIGINAL(0x006FE925)
__declspec(naked) void __fastcall ConstructListHolder(void*, void*, const void*, const void*) EI_ORIGINAL(0x005FA340)
__declspec(naked) void* __fastcall AllocateNodes(std::size_t) EI_ORIGINAL(0x00405950)

// A list header node of 12 bytes, linked to itself.
void NewListHeader(std::uint8_t* at) {
    void* node = AllocateNodes(0xC);
    SetDword(at, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node)));
    SetDword(node, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node)));
    SetDword(node, 4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node)));
}
} // namespace

void CInterface3D::ShowMessage(int force) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    bool forced = static_cast<std::uint8_t>(force) != 0;
    if (!forced) {
        std::uint8_t* header;
        std::memcpy(&header, self + 0x84, 4);
        std::uint8_t* node;
        std::memcpy(&node, header, 4);
        if (node == header) return;
        alignas(4) std::uint8_t storage[sizeof(mfc::CString)];
        auto* text = reinterpret_cast<mfc::CString*>(storage);
        CopyCString(text, nullptr, node + 8);
        void* next;
        MessageListErase(self + 0x84, nullptr, &next, node);
        *reinterpret_cast<mfc::CString*>(self + 0xA4) = *text;
        SetDword(self, 0xA8, 0);
        text->~CString();
    }
    const char* message;
    std::memcpy(&message, self + 0xA4, 4);
    if (*reinterpret_cast<const int*>(message - 8) == 0) return;
    if (*reinterpret_cast<const int*>(0x0079BE78) == 0 && !forced) return;
    void* memory = gamecrt::operator_new(0x194);
    CUiWindow* window = nullptr;
    if (memory) {
        std::uint32_t value;
        std::memcpy(&value, self + 0xA8, 4);
        ConstructContainer(memory, nullptr, this);
        auto* win = static_cast<std::uint8_t*>(memory);
        win[0x164] = 1;
        std::memcpy(&message, self + 0xA4, 4);
        ConstructCString(win + 0x168, nullptr, message);
        SetDword(win, 0x16C, *reinterpret_cast<const std::uint32_t*>(0x00796E48));   // empty CString
        ConstructStringArrayAt(win + 0x170);
        SetDword(win, 0x184, value);
        // The original passes a stack slot and a zeroed local; the holder keeps the zero.
        std::uint32_t junk = 0, zero = 0;
        ConstructListHolder(win + 0x188, nullptr, &junk, &zero);
        NewListHeader(win + 0x188);
        win[0x18C] = 0;   // the allocator byte, overwritten
        SetDword(win, 0x18C, 0);
        NewListHeader(win + 0x18C);
        SetDword(win, 0x00, 0x00740E28);
        SetDword(win, 0x94, 0x00740DF4);
        window = reinterpret_cast<CUiWindow*>(memory);
    }
    PushWindow(window, 1, 0);
}

namespace {
void __fastcall InterfaceShowMessage(CInterface3D* self, void*, int force) { self->ShowMessage(force); }
} // namespace
EI_REPLACE(0x005F8A80, InterfaceShowMessage);

// --- 005F86F0: the help window -----------------------------------------------------------------------

namespace {
// The seen-topics map at +0x98 (STLport map<CString, int>; thiscall): find (005FAEA0) and
// lower_bound (005FAF30) into an iterator (RET 8), insert with a hint (005F9810, RET 0xC).
__declspec(naked) void __fastcall TopicsFind(void*, void*, void** /*out*/, const void* /*key*/) EI_ORIGINAL(0x005FAEA0)
__declspec(naked) void __fastcall TopicsLowerBound(void*, void*, void** /*out*/, const void* /*key*/)
    EI_ORIGINAL(0x005FAF30)
__declspec(naked) void __fastcall TopicsInsert(void*, void*, void** /*out*/, void* /*hint*/, const void* /*pair*/)
    EI_ORIGINAL(0x005F9810)
// The help window's list member at +0x188 (005F9A00, from an allocator; RET 4).
__declspec(naked) void __fastcall ConstructHelpList(void*, void*, const void*) EI_ORIGINAL(0x005F9A00)
// _mbscmp (the game's CRT).
__declspec(naked) int __cdecl MbsCompare(const char*, const char*) EI_ORIGINAL(0x006E848B)
} // namespace

void CInterface3D::OpenHelp(int flag) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    if (stackBegin_ == stackEnd_) return;
    CUiWindow* active = stackEnd_[-1];
    using TopicFn = const char*(__thiscall*)(CUiWindow*);
    const char* topic = reinterpret_cast<TopicFn>(active->vtable_[41])(active);
    if (!topic) return;
    int seen = -1;
    void* topics = self + 0x98;
    void* header;
    std::memcpy(&header, topics, 4);
    void* found;
    {
        mfc::CString key(topic);
        TopicsFind(topics, nullptr, &found, &key);
    }
    std::memcpy(&header, topics, 4);
    if (found != header) {
        seen = *reinterpret_cast<const int*>(static_cast<std::uint8_t*>(found) + 0x14);
    } else {
        mfc::CString key(topic);
        void* at;
        TopicsLowerBound(topics, nullptr, &at, &key);
        std::memcpy(&header, topics, 4);
        if (at == header ||
            MbsCompare(key, *reinterpret_cast<const char* const*>(static_cast<std::uint8_t*>(at) + 0x10)) < 0) {
            // A new entry {topic, 0} before `at`.
            struct Pair {
                std::uint8_t key[sizeof(mfc::CString)];
                int value;
            } pair;
            CopyCString(pair.key, nullptr, &key);
            pair.value = 0;
            void* inserted;
            TopicsInsert(topics, nullptr, &inserted, at, &pair);
            at = inserted;
            reinterpret_cast<mfc::CString*>(pair.key)->~CString();
        }
        *reinterpret_cast<int*>(static_cast<std::uint8_t*>(at) + 0x14) = 0;
    }
    bool forced = static_cast<std::uint8_t>(flag) != 0;
    if (*reinterpret_cast<const int*>(0x0079BE78) != 0) {
        if (!forced && seen >= 0) return;
    } else if (!forced) {
        return;
    }
    void* memory = gamecrt::operator_new(0x194);
    CUiWindow* window = nullptr;
    if (memory) {
        ConstructContainer(memory, nullptr, this);
        auto* win = static_cast<std::uint8_t*>(memory);
        win[0x164] = 0;
        ConstructCString(win + 0x168, nullptr, topic);
        SetDword(win, 0x16C, *reinterpret_cast<const std::uint32_t*>(0x00796E48));   // empty CString
        ConstructStringArrayAt(win + 0x170);
        SetDword(win, 0x184, static_cast<std::uint32_t>(seen));
        // The original passes its argument slot as the list's allocator.
        int slot = flag;
        ConstructHelpList(win + 0x188, nullptr, &slot);
        std::uint32_t junk = 0, zero = 0;
        ConstructListHolder(win + 0x18C, nullptr, &junk, &zero);
        NewListHeader(win + 0x18C);
        SetDword(win, 0x00, 0x00740E28);
        SetDword(win, 0x94, 0x00740DF4);
        window = reinterpret_cast<CUiWindow*>(memory);
    }
    PushWindow(window, 1, 0);
}

namespace {
void __fastcall InterfaceOpenHelp(CInterface3D* self, void*, int flag) { self->OpenHelp(flag); }
} // namespace
EI_REPLACE(0x005F86F0, InterfaceOpenHelp);

// --- Screens 8 and 9: constructors and screen 8's destructor body -----------------------------------

namespace {

// STLport's node allocator: the free list of 0x18-byte nodes (0079B22C) and the refill
// (004060E0, __fastcall with the size).
void** FreeList18() { return reinterpret_cast<void**>(0x0079B22C); }
__declspec(naked) void* __fastcall RefillNodes(std::size_t) EI_ORIGINAL(0x004060E0)

void* AllocateNode18() {
    void* node = *FreeList18();
    if (node) {
        *FreeList18() = *static_cast<void**>(node);
        return node;
    }
    return RefillNodes(0x18);
}
void FreeNode18(void* node) {
    *static_cast<void**>(node) = *FreeList18();
    *FreeList18() = node;
}

// Members (thiscall): screen 9's two maps (005F9F80 with two allocators, RET 8; 005F98F0);
// screen 8's members at +0x170 (00671280 / 006714A0) and +0x1D8 (005DB340 / 005DB350).
__declspec(naked) void __fastcall Screen9FirstMap(void*, void*, const void*, const void*) EI_ORIGINAL(0x005F9F80)
__declspec(naked) void __fastcall Screen9SecondMap(void*) EI_ORIGINAL(0x005F98F0)
__declspec(naked) void __fastcall Screen8Member170(void*) EI_ORIGINAL(0x00671280)
__declspec(naked) void __fastcall Screen8Member170Destruct(void*) EI_ORIGINAL(0x006714A0)
__declspec(naked) void __fastcall Screen8Member1D8(void*) EI_ORIGINAL(0x005DB340)
__declspec(naked) void __fastcall Screen8Member1D8Destruct(void*) EI_ORIGINAL(0x005DB350)
__declspec(naked) void __fastcall ConstructCStringFrom(void*, void*, const char*) EI_ORIGINAL(0x007009DE)
__declspec(naked) void __fastcall DestructCString(void*) EI_ORIGINAL(0x00700970)
__declspec(naked) void __fastcall DestroyContainerBody(void*) EI_ORIGINAL(0x00600BA0)

std::uint8_t* Bytes(void* p) { return static_cast<std::uint8_t*>(p); }

} // namespace

// 005F6F30 (thiscall, RET 4). Screen 9's constructor (vtables 00740D18 / 00740CE4).
void* __fastcall Screen9Construct(void* self, void*, CInterface3D* ui) {
    ConstructContainer(self, nullptr, ui);
    SetDword(self, 0x168, 0);
    // The original passes a stack byte and its argument slot as the allocators.
    std::uint8_t junk = 0;
    CInterface3D* slot = ui;
    Screen9FirstMap(Bytes(self) + 0x170, nullptr, &junk, &slot);
    SetDword(self, 0x17C, 50);
    Screen9SecondMap(Bytes(self) + 0x180);
    SetDword(self, 0x18C, 50);
    SetDword(self, 0x1AC, 0);
    SetDword(self, 0x1B0, 0);
    Bytes(self)[0x1B4] = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(slot));
    SetDword(self, 0x1B4, 0);
    SetDword(self, 0x00, 0x00740D18);
    SetDword(self, 0x94, 0x00740CE4);
    return self;
}
EI_REPLACE(0x005F6F30, Screen9Construct);

// 005F6D50 (thiscall, RET 0xC). Screen 8's constructor (vtables 00740C3C / 00740C08): the list
// at +0x1C8 gets its header node, +0x1D4 the text, +0x1F4 the flag's low byte.
void* __fastcall Screen8Construct(void* self, void*, CInterface3D* ui, int flag, const char* text) {
    ConstructContainer(self, nullptr, ui);
    Screen8Member170(Bytes(self) + 0x170);
    std::uint8_t* list = Bytes(self) + 0x1C8;
    list[0] = static_cast<std::uint8_t>(flag);   // the allocator byte, overwritten
    SetDword(list, 0, 0);
    void* header = AllocateNode18();
    SetDword(list, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));
    SetDword(header, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));   // next
    SetDword(header, 4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));   // prev
    ConstructCStringFrom(Bytes(self) + 0x1D4, nullptr, text);
    Screen8Member1D8(Bytes(self) + 0x1D8);
    SetDword(self, 0x00, 0x00740C3C);
    Bytes(self)[0x1F4] = static_cast<std::uint8_t>(flag);
    SetDword(self, 0x94, 0x00740C08);
    return self;
}
EI_REPLACE(0x005F6D50, Screen8Construct);

// 005F6E40 (thiscall). Screen 8's destructor body: its members, the list (entries with two
// CStrings at +0x10 / +0x14) back to the node allocator, then the container base.
void __fastcall Screen8Destroy(void* self) {
    Screen8Member1D8Destruct(Bytes(self) + 0x1D8);
    DestructCString(Bytes(self) + 0x1D4);
    std::uint8_t* list = Bytes(self) + 0x1C8;
    auto headerOf = [list] {
        std::uint8_t* h;
        std::memcpy(&h, list, 4);
        return h;
    };
    std::uint8_t* node;
    std::memcpy(&node, headerOf(), 4);
    while (node != headerOf()) {
        std::uint8_t* next;
        std::memcpy(&next, node, 4);
        DestructCString(node + 0x14);
        DestructCString(node + 0x10);
        if (node) FreeNode18(node);
        node = next;
    }
    std::uint8_t* header = headerOf();
    SetDword(header, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));
    header = headerOf();
    SetDword(header, 4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));
    header = headerOf();
    if (header) FreeNode18(header);
    Screen8Member170Destruct(Bytes(self) + 0x170);
    DestroyContainerBody(self);
}
EI_REPLACE(0x005F6E40, Screen8Destroy);
