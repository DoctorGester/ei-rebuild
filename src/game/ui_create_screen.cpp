// CInterface3D::CreateScreen (005F5BA0): the screens by id. See ui.h and docs/notes/ui.md.
//
// The original inlines the constructors of screens 1, 2, 3 and 7 (the container base 00600B20,
// their members' constructors and vtables) and calls the others' constructors; ours does the same
// through the original addresses.
#include "app.h"
#include "world.h"
#include "ui_camp.h"
#include "ui_briefing.h"
#include "ui_create_screen.h"

#include "ui.h"
#include "ui_cursor.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "game_data.h"
#include "world_glue.h"

#include <cstdint>
#include <cstring>

namespace {

void SetDword(void* p, int offset, std::uint32_t value) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &value, 4); }
std::uint32_t EmptyString() { return *reinterpret_cast<const std::uint32_t*>(0x00796E48); }   // afxEmptyString

// An empty STLport list whose nodes are `Node` (next, prev, then the element): its sentinel from the
// node pool, linked to itself (the list constructors 005F90E0 / 005F9300).
template <typename Node>
Node* EmptyList() {
    auto* head = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    head->next = head;
    head->prev = head;
    return head;
}

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
    switch (id) {
    case 0:
        return;
    case 1: {   // the in-game screen (ui_camp.h)
        screen = gamecrt::operator_new(0x230);
        if (!screen) break;
        ConstructContainerAt(screen, nullptr, this);
        auto* battle = static_cast<CBattleScreen*>(screen);
        mfc::impl::ConstructHeader<mfc::impl::UnitPtrArrayClass>(&battle->party_);
        mfc::impl::ConstructHeader<mfc::impl::IntArrayClass>(&battle->heroes_);
        battle->personTexts_.Construct();          // 005F8F30
        battle->clientParty_.Construct();          // 005F6080
        battle->itemUseKey_.ConstructItemUseKey();  // 005F5F90
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
        auto* briefing = static_cast<CBriefingScreen*>(screen);
        StringArrayConstruct(&briefing->topics_);
        new (&briefing->topic_) mfc::CString();
        mfc::impl::ConstructHeader<mfc::impl::UnitPtrArrayClass>(&briefing->party_);
        briefing->movers_ = EmptyList<BriefingMover>();
        new (&briefing->dialogueText_) mfc::CString();
        briefing->personTexts_.Construct();           // 005F8F30
        briefing->shown_ = EmptyList<BriefingShownItem>();
        briefing->people_.ConstructEmpty();           // 005F8F10
        briefing->speakers_ = nullptr;                // the speakers' vector (005F9540): no storage
        briefing->speakersEnd_ = nullptr;
        briefing->speakersCapacity_ = nullptr;
        SetVtables(screen, Vtable(0x007405AC), Vtable(0x00740578));
        break;
    }
    case 4:
        screen = gamecrt::operator_new(0x294);
        if (screen) static_cast<CGlobalMap*>(screen)->Construct(this);
        break;
    case 5:
        screen = gamecrt::operator_new(0x260);
        if (screen) static_cast<CCampPages*>(screen)->Construct(this, 0);
        break;
    case 7:
        screen = gamecrt::operator_new(0x168);
        if (!screen) break;
        ConstructContainerAt(screen, nullptr, this);
        SetVtables(screen, Vtable(0x00740840), Vtable(0x0074080C));
        break;
    case 8:
        screen = gamecrt::operator_new(0x1FC);
        if (screen) Screen8Construct(screen, nullptr, this, 0, reinterpret_cast<const char*>(0x00793084));
        break;
    case 9:
        screen = gamecrt::operator_new(0x1B8);
        if (screen) Screen9Construct(screen, nullptr, this);
        break;
    default:   // 6 and above 9: the id itself
        screen = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<unsigned>(id)));
        break;
    }
    Activate(static_cast<CUiWindow*>(screen));
}

// --- 005F7350: PushWindow --------------------------------------------------------------------------

namespace {

CCursor* Cursor() { return GameCursor(); }   // 005E5200 SetShape (RET 4)

// 00458220: the message pump.
// 0057F440 (on 007AFE20): a multiplayer client session; returns AL.
// The game state's server flag (007AFEA4; 00578D00 reads it, 00578D10 sets it; neither reads ECX).

// 005FA9A0: the window stack's insert when it is full (vector<CUiWindow*>'s, plain elements).

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
        reinterpret_cast<stlp::vector<CUiWindow*>*>(&ui->stackBegin_)
            ->InsertOverflow<stlp::PlainElement<CUiWindow*>>(ui->stackEnd_, window, 1);
    }
    static const unsigned long kStates[5][2] = {{0x1B, 1}, {0x0F, 1}, {0x13, 5}, {0x14, 6}, {0x18, 0x80}};
    for (const auto& state : kStates) {
        SetRenderState(ui->renderer_->device_, state[0], state[1]);
    }
    *reinterpret_cast<std::uint8_t*>(0x00784750) = 1;
    using CreateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<CreateFn>(window->vtable_[25])(window);
    app::PumpMessages();
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
        app::FrameStep(false, true);
    }
    // One window open (the byte difference with its low two bits cleared is 4).
    std::uint32_t bytes = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stackEnd_) -
                                                     reinterpret_cast<std::uintptr_t>(stackBegin_));
    if ((bytes & ~3u) == 4) {
        serverFlag_ = static_cast<std::uint8_t>(GameServerPaused());
        if (!IsMultiplayerClient()) GameSetServerPaused(1);
        if (first && second) {
            backdropMode_ = 1;
            app::FrameStep(false, true);
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
        CloseAll();
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
    GameSetServerPaused(0);
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
    ResetInput();
    app::PumpMessages();
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
        messages_.Erase(node);   // 005F8E50
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
        win->pulsing_.Construct();    // the allocator proxy (005FA340), then a sentinel
        win->pictures_.Construct();
        SetVtables(win, Vtable(0x00740E28), Vtable(0x00740DF4));
        window = win;
    }
    PushWindow(window, 1, 0);
}

EI_REPLACE_METHOD(0x005F8A80, CInterface3D::ShowMessage);

// --- 005F86F0: the help window -----------------------------------------------------------------------

void CInterface3D::OpenHelp(int flag) {
    if (stackBegin_ == stackEnd_) return;
    CUiWindow* active = stackEnd_[-1];
    using TopicFn = const char*(__thiscall*)(CUiWindow*);
    const char* topic = reinterpret_cast<TopicFn>(active->vtable_[41])(active);
    if (!topic) return;
    int seen = -1;
    // The seen-topics map (find 005FAEA0; operator[], lower_bound 005FAF30 and an insert at that
    // hint, inlined by the original).
    decltype(helpTopics_)::Node* found;
    {
        mfc::CString key(topic);
        found = helpTopics_.find(key);
    }
    if (found != helpTopics_.end()) {
        seen = found->second;
    } else {
        mfc::CString key(topic);
        helpTopics_.Subscript(key) = 0;
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
        win->pulsing_.Construct();    // 005F9A00
        win->pictures_.Construct();   // the allocator proxy (005FA340), then a sentinel
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

void* AllocateNode18() {
    void* node = *FreeList18();
    if (node) {
        *FreeList18() = *static_cast<void**>(node);
        return node;
    }
    return libcrt::NodeRefill(0x18);
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
