// CInterface3D::CreateScreen (005F5BA0): the screens by id. See ui.h and docs/notes/ui.md.
//
// The original inlines the constructors of screens 1, 2, 3 and 7 (the container base 00600B20,
// their members' constructors and vtables) and calls the others' constructors; ours does the same
// through the original addresses.
#include "ui_create_screen.h"

#include "ui.h"
#include "ui_cursor.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

void SetDword(void* p, int offset, std::uint32_t value) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &value, 4); }
std::uint32_t EmptyString() { return *reinterpret_cast<const std::uint32_t*>(0x00796E48); }   // afxEmptyString

__declspec(naked) void __fastcall ConstructIslandMap(void*, void*, CInterface3D*) EI_ORIGINAL(0x005F67A0)      // id 4
__declspec(naked) void __fastcall ConstructScreen5(void*, void*, CInterface3D*, int) EI_ORIGINAL(0x005F6240)    // id 5
__declspec(naked) void __fastcall ConstructScreen8(void*, void*, CInterface3D*, int, const void*) EI_ORIGINAL(0x005F6D50)
__declspec(naked) void __fastcall ConstructScreen9(void*, void*, CInterface3D*) EI_ORIGINAL(0x005F6F30)

// Member constructors (thiscall on the member).
__declspec(naked) void __fastcall Member5C1760(void*) EI_ORIGINAL(0x005C1760)
__declspec(naked) void __fastcall Member5F8F30(void*) EI_ORIGINAL(0x005F8F30)
__declspec(naked) void __fastcall Member5F6080(void*) EI_ORIGINAL(0x005F6080)
__declspec(naked) void __fastcall Member5F5F90(void*) EI_ORIGINAL(0x005F5F90)
   // CStringArray
__declspec(naked) void __fastcall Member5FAF80(void*) EI_ORIGINAL(0x005FAF80)
// STLport containers taking an allocator (by address; empty).
__declspec(naked) void __fastcall Member5F90E0(void*, void*, const void*) EI_ORIGINAL(0x005F90E0)
__declspec(naked) void __fastcall Member5F9300(void*, void*, const void*) EI_ORIGINAL(0x005F9300)
__declspec(naked) void __fastcall Member5F9540(void*, void*, const void*) EI_ORIGINAL(0x005F9540)

// The window's two vtables, the window's (+0x00) first, then its input side's (+0x94).
void** Vtable(std::uintptr_t address) { return reinterpret_cast<void**>(address); }
void SetVtables(void* window, void** vtable, void** inputVtable) {
    auto* w = static_cast<CUiWindow*>(window);
    w->vtable_ = vtable;
    w->Input()->vtable_ = inputVtable;
}

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
        ConstructContainerAt(screen, nullptr, this);
        SetDword(screen, 0x168, 0x007402AC);
        SetDword(screen, 0x16C, 0);
        SetDword(screen, 0x178, 0);
        SetDword(screen, 0x174, 0);
        SetDword(screen, 0x170, 0);
        Member5C1760(ei::OffsetPtr(screen, 0x1DC));
        Member5F8F30(ei::OffsetPtr(screen, 0x1F0));
        Member5F6080(ei::OffsetPtr(screen, 0x204));
        Member5F5F90(ei::OffsetPtr(screen, 0x220));
        SetVtables(screen, Vtable(0x00740764), Vtable(0x00740730));
        break;
    }
    case 2:
        screen = gamecrt::operator_new(0x17C);
        if (!screen) break;
        ConstructContainerAt(screen, nullptr, this);
        SetVtables(screen, Vtable(0x00740688), Vtable(0x00740654));
        break;
    case 3: {   // the base / briefing screen
        screen = gamecrt::operator_new(0x2A4);
        if (!screen) break;
        ConstructContainerAt(screen, nullptr, this);
        StringArrayConstruct(ei::OffsetPtr(screen, 0x170));
        SetDword(screen, 0x184, EmptyString());
        Member5FAF80(ei::OffsetPtr(screen, 0x190));
        Member5F90E0(ei::OffsetPtr(screen, 0x240), nullptr, &allocators[0]);
        SetDword(screen, 0x248, EmptyString());
        Member5F8F30(ei::OffsetPtr(screen, 0x26C));
        Member5F9300(ei::OffsetPtr(screen, 0x278), nullptr, &allocators[1]);
        IdVectorConstructOriginal(ei::OffsetPtr(screen, 0x284), nullptr, &allocators[2]);
        Member5F9540(ei::OffsetPtr(screen, 0x294), nullptr, &allocators[3]);
        SetVtables(screen, Vtable(0x007405AC), Vtable(0x00740578));
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
        ConstructContainerAt(screen, nullptr, this);
        SetVtables(screen, Vtable(0x00740840), Vtable(0x0074080C));
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
    reinterpret_cast<InterfaceOriginals*>(this)->Activate(static_cast<CUiWindow*>(screen));
}

// --- 005F7350: PushWindow --------------------------------------------------------------------------

namespace {

CCursor* Cursor() { return GameCursor(); }   // 005E5200 SetShape (RET 4)

// 00458220: the message pump.
// 0057F440 (on 007AFE20): a multiplayer client session; returns AL.
// The game state's server flag (007AFEA4; 00578D00 reads it, 00578D10 sets it; neither reads ECX).

// 005FA9A0 (thiscall on the window stack, RET 0xC): the vector's insert when it is full.
__declspec(naked) void __fastcall StackInsertOverflow(void*, void*, CUiWindow** /*at*/, CUiWindow* const* /*value*/,
                                                      int /*count*/) EI_ORIGINAL(0x005FA9A0)

// IDirect3DDevice7::SetRenderState (COM, slot 20).
void SetRenderState(UiD3DDevice* device, unsigned long state, unsigned long value) {
    device->SetRenderState(state, value);
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
        SetRenderState(ui->renderer_->device_, state[0], state[1]);
    }
    *reinterpret_cast<std::uint8_t*>(0x00784750) = 1;
    using CreateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<CreateFn>(window->vtable_[25])(window);
    PumpMessagesOriginal();
    ui->mouseCapture_ = nullptr;
    Cursor()->SetShape(0);
}

} // namespace

void CInterface3D::PushWindow(CUiWindow* window, int a, int b) {
    Cursor()->SetShape(3);
    bool first = static_cast<std::uint8_t>(a) != 0;
    bool second = static_cast<std::uint8_t>(b) != 0;
    if (first && !second) {
        backdropMode_ = 3;
        FrameStepOriginal(0, 1);
    }
    // One window open (the byte difference with its low two bits cleared is 4).
    std::uint32_t bytes = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stackEnd_) -
                                                     reinterpret_cast<std::uintptr_t>(stackBegin_));
    if ((bytes & ~3u) == 4) {
        serverFlag_ = static_cast<std::uint8_t>(ServerFlagOriginal());
        if (!IsNetworkClientOriginal()) SetServerFlagOriginal(1);
        if (first && second) {
            backdropMode_ = 1;
            FrameStepOriginal(0, 1);
        }
    }
    ctrlDown_ = false;
    altDown_ = false;
    shiftDown_ = 0;
    rightDown_ = false;
    doubleClick_ = false;
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Deactivate();
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
void DeleteBackdrop(CUiSurface* surface) {
    if (!surface) return;
    surface->ReleaseContents();
    gamecrt::operator_delete(surface);
}
} // namespace

void CInterface3D::Activate(CUiWindow* window) {
    if (closeAll_) {
        closeAll_ = false;
        InterfaceCloseAllOriginal(this);
    }
    Cursor()->SetShape(3);
    CUiSurface* backdrop = backdrop_;
    DeleteBackdrop(backdrop);
    backdrop = secondBackdrop_;
    backdrop_ = nullptr;
    DeleteBackdrop(backdrop);
    secondBackdrop_ = nullptr;
    backdropMode_ = 0;
    serverFlag_ = 0;
    SetServerFlagOriginal(0);
    Cursor()->SetShape(3);
    while (stackBegin_ != stackEnd_) {
        stackEnd_[-1]->Input()->Deactivate();
        using VoidFn = void(__thiscall*)(CUiWindow*);
        CUiWindow* top = stackEnd_[-1];
        reinterpret_cast<VoidFn>(top->vtable_[26])(top);
        top = stackEnd_[-1];
        if (top) {
            using DeleteFn = void(__thiscall*)(CUiWindow*, int);
            reinterpret_cast<DeleteFn>(top->vtable_[0])(top, 1);
        }
        --stackEnd_;
    }
    reinterpret_cast<InterfaceOriginals*>(this)->ResetInput();
    PumpMessagesOriginal();
    *reinterpret_cast<std::uint8_t*>(0x00784750) = 0;
    if (!window) {
        game::FatalError(reinterpret_cast<const char*>(0x0079308C));
        return;
    }
    OpenOnTop(this, window);
}

EI_REPLACE_METHOD(0x005F71C0, CInterface3D::Activate);

EI_REPLACE_METHOD(0x005F7350, CInterface3D::PushWindow);

EI_REPLACE_METHOD(0x005F5BA0, CInterface3D::CreateScreen);

// --- 005F8A80: the message window ------------------------------------------------------------------

namespace {
// The message queue (a std::list of CStrings at +0x84; thiscall): 005F8E50 erases a node into
// the returned iterator (RET 8).
__declspec(naked) void __fastcall MessageListErase(void*, void*, void* /*out*/, void* /*node*/) EI_ORIGINAL(0x005F8E50)
__declspec(naked) void __fastcall ConstructListHolder(void*, void*, const void*, const void*) EI_ORIGINAL(0x005FA340)
inline void* __fastcall AllocateNodes(std::size_t a0) { return NodeAllocateOriginal(a0); }

// A list header node of 12 bytes, linked to itself.
void NewListHeader(stlp::list<CUiMesh*>* list) {
    using Node = stlp::list<CUiMesh*>::Node;
    auto* node = static_cast<Node*>(AllocateNodes(0xC));
    list->head = node;
    node->next = node;
    node->prev = node;
}
} // namespace

void CInterface3D::ShowMessage(int force) {
    bool forced = static_cast<std::uint8_t>(force) != 0;
    if (!forced) {
        using Node = stlp::list<mfc::CString>::Node;
        Node* header = messages_.head;
        Node* node = header->next;
        if (node == header) return;
        alignas(4) std::uint8_t storage[sizeof(mfc::CString)];
        auto* text = reinterpret_cast<mfc::CString*>(storage);
        CStringCopyConstructOriginal(text, nullptr, &node->data);
        void* next;
        MessageListErase(&messages_, nullptr, &next, node);
        messageText_ = *text;
        messageFlag_ = 0;
        text->~CString();
    }
    if (messageText_.GetLength() == 0) return;
    if (*reinterpret_cast<const int*>(0x0079BE78) == 0 && !forced) return;
    void* memory = gamecrt::operator_new(0x194);
    CUiWindow* window = nullptr;
    if (memory) {
        int value = messageFlag_;
        ConstructContainerAt(memory, nullptr, this);
        auto* win = static_cast<CHelpWindow*>(memory);
        win->message_ = true;
        CStringConstructTextOriginal(&win->text_, nullptr, messageText_);
        SetDword(&win->title_, 0, EmptyString());
        StringArrayConstruct(&win->lines_);
        win->page_ = value;
        // The original passes a stack slot and a zeroed local; the holder keeps the zero.
        std::uint32_t junk = 0, zero = 0;
        ConstructListHolder(&win->pulsing_, nullptr, &junk, &zero);
        NewListHeader(&win->pulsing_);
        *reinterpret_cast<std::uint8_t*>(&win->pictures_) = 0;   // the allocator byte, overwritten
        win->pictures_.head = nullptr;
        NewListHeader(&win->pictures_);
        SetVtables(win, Vtable(0x00740E28), Vtable(0x00740DF4));
        window = win;
    }
    PushWindow(window, 1, 0);
}

EI_REPLACE_METHOD(0x005F8A80, CInterface3D::ShowMessage);

// --- 005F86F0: the help window -----------------------------------------------------------------------

namespace {
// The seen-topics map at +0x98 (STLport map<CString, int>; thiscall): find (005FAEA0) and
// lower_bound (005FAF30) into an iterator (RET 8); insert with a hint is HelpTopicsInsertOriginal.
using TopicNode = stlp::map<mfc::CString, int>::Node;
__declspec(naked) void __fastcall TopicsFind(void*, void*, TopicNode** /*out*/, const void* /*key*/) EI_ORIGINAL(0x005FAEA0)
__declspec(naked) void __fastcall TopicsLowerBound(void*, void*, TopicNode** /*out*/, const void* /*key*/)
    EI_ORIGINAL(0x005FAF30)
// The help window's list member at +0x188 (005F9A00, from an allocator; RET 4).
__declspec(naked) void __fastcall ConstructHelpList(void*, void*, const void*) EI_ORIGINAL(0x005F9A00)
// _mbscmp (the game's CRT).
int __cdecl MbsCompare(const char* a0, const char* a1) { return GameMbscmp(a0, a1); }
} // namespace

void CInterface3D::OpenHelp(int flag) {
    if (stackBegin_ == stackEnd_) return;
    CUiWindow* active = stackEnd_[-1];
    using TopicFn = const char*(__thiscall*)(CUiWindow*);
    const char* topic = reinterpret_cast<TopicFn>(active->vtable_[41])(active);
    if (!topic) return;
    int seen = -1;
    stlp::map<mfc::CString, int>* topics = &helpTopics_;
    TopicNode* found;
    {
        mfc::CString key(topic);
        TopicsFind(topics, nullptr, &found, &key);
    }
    if (found != topics->end()) {
        seen = found->second;
    } else {
        mfc::CString key(topic);
        TopicNode* at;
        TopicsLowerBound(topics, nullptr, &at, &key);
        if (at == topics->end() || MbsCompare(key, at->first) < 0) {
            // A new entry {topic, 0} before `at`.
            struct Pair {
                std::uint8_t key[sizeof(mfc::CString)];
                int value;
            } pair;
            CStringCopyConstructOriginal(pair.key, nullptr, &key);
            pair.value = 0;
            void* inserted;
            HelpTopicsInsertOriginal(topics, nullptr, &inserted, at, &pair);
            at = static_cast<TopicNode*>(inserted);
            reinterpret_cast<mfc::CString*>(pair.key)->~CString();
        }
        at->second = 0;
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
        ConstructContainerAt(memory, nullptr, this);
        auto* win = static_cast<CHelpWindow*>(memory);
        win->message_ = false;
        CStringConstructTextOriginal(&win->text_, nullptr, topic);
        SetDword(&win->title_, 0, EmptyString());
        StringArrayConstruct(&win->lines_);
        win->page_ = seen;
        // The original passes its argument slot as the list's allocator.
        int slot = flag;
        ConstructHelpList(&win->pulsing_, nullptr, &slot);
        std::uint32_t junk = 0, zero = 0;
        ConstructListHolder(&win->pictures_, nullptr, &junk, &zero);
        NewListHeader(&win->pictures_);
        SetVtables(win, Vtable(0x00740E28), Vtable(0x00740DF4));
        window = win;
    }
    PushWindow(window, 1, 0);
}

EI_REPLACE_METHOD(0x005F86F0, CInterface3D::OpenHelp);

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
__declspec(naked) void __fastcall Screen8Member1D8Destruct(void*) EI_ORIGINAL(0x005DB350)

std::uint8_t* Bytes(void* p) { return static_cast<std::uint8_t*>(p); }

} // namespace

// 005F6F30 (thiscall, RET 4). Screen 9's constructor (vtables 00740D18 / 00740CE4).
void* __fastcall Screen9Construct(void* self, void*, CInterface3D* ui) {
    ConstructContainerAt(self, nullptr, ui);
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
    SetVtables(self, Vtable(0x00740D18), Vtable(0x00740CE4));
    return self;
}
EI_REPLACE(0x005F6F30, Screen9Construct);

// 005F6D50 (thiscall, RET 0xC). Screen 8's constructor (vtables 00740C3C / 00740C08): the list
// at +0x1C8 gets its header node, +0x1D4 the text, +0x1F4 the flag's low byte.
void* __fastcall Screen8Construct(void* self, void*, CInterface3D* ui, int flag, const char* text) {
    ConstructContainerAt(self, nullptr, ui);
    MappedResourceConstructOriginal(Bytes(self) + 0x170);
    std::uint8_t* list = Bytes(self) + 0x1C8;
    list[0] = static_cast<std::uint8_t>(flag);   // the allocator byte, overwritten
    SetDword(list, 0, 0);
    void* header = AllocateNode18();
    SetDword(list, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));
    SetDword(header, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));   // next
    SetDword(header, 4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));   // prev
    CStringConstructTextOriginal(Bytes(self) + 0x1D4, nullptr, text);
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
    CStringDestroy(Bytes(self) + 0x1D4);
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
        CStringDestroy(node + 0x14);
        CStringDestroy(node + 0x10);
        if (node) FreeNode18(node);
        node = next;
    }
    std::uint8_t* header = headerOf();
    SetDword(header, 0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));
    header = headerOf();
    SetDword(header, 4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header)));
    header = headerOf();
    if (header) FreeNode18(header);
    MappedResourceDestroyOriginal(reinterpret_cast<CMappedResource*>(Bytes(self) + 0x170));
    DestroyContainerBody(self);
}
EI_REPLACE(0x005F6E40, Screen8Destroy);
