// CInterface3D::CreateScreen (005F5BA0): the screens by id. See ui.h and docs/notes/ui.md.
//
// The original inlines the constructors of screens 1, 2, 3 and 7 (the container base 00600B20,
// their members' constructors and vtables) and calls the others' constructors; ours does the same
// through the original addresses.
#include "app.h"
#include "bink_video.h"
#include "options.h"
#include "world.h"
#include "ui_camp.h"
#include "ui_briefing.h"
#include "ui_screens.h"
#include "ui_view_screen.h"

#include "ui.h"
#include "ui_cursor.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game_data.h"
#include "world_glue.h"
#include "library_trees.h"
#include "game_singletons.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {

// An empty STLport list whose nodes are `Node` (next, prev, then the element): its sentinel from the
// node pool, linked to itself (the list constructors 005F90E0 / 005F9300).
template <typename Node>
Node* EmptyList() {
    auto* head = stlp::allocate_as<Node>();
    head->next = head;
    head->prev = head;
    return head;
}

} // namespace

// The screens CreateScreen builds inline (screens 1 and 3; 2 and 7 have only the container's).
CBattleScreen::CBattleScreen(CInterface3D* ui) : CUiContainer(ui) {
    mfc::impl::ConstructHeader<mfc::impl::UnitPtrArrayClass>(&party_);
    mfc::impl::ConstructHeader<mfc::impl::IntArrayClass>(&heroes_);
    personTexts_.Construct();          // 005F8F30
    clientParty_.Construct();          // 005F6080
    itemUseKey_.ConstructItemUseKey();  // 005F5F90
}

CBriefingScreen::CBriefingScreen(CInterface3D* ui) : CUiContainer(ui) {
    mfc::impl::ConstructHeader<mfc::impl::UnitPtrArrayClass>(&party_);
    movers_ = EmptyList<BriefingMover>();
    personTexts_.Construct();           // 005F8F30
    shown_ = EmptyList<BriefingShownItem>();
    people_.ConstructEmpty();           // 005F8F10
    speakers_ = nullptr;                // the speakers' vector (005F9540): no storage
    speakersEnd_ = nullptr;
    speakersCapacity_ = nullptr;
}

void CInterface3D::CreateScreen(int id) {
    CUiWindow* screen = nullptr;
    switch (id) {
    case 0:
        return;
    case 1:   // the in-game screen (ui_camp.h)
        screen = new CBattleScreen(this);
        break;
    case 2:   // the main menu
        screen = new CMainMenu(this);
        break;
    case 3:   // the base / briefing screen
        screen = new CBriefingScreen(this);
        break;
    case 4:
        screen = new CGlobalMap(this);
        break;
    case 5:
        screen = new CCampPages(this, false);
        break;
    case 7:
        screen = new CViewScreen(this);
        break;
    case 8:   // the titles (CCreditsScreen, ui_screens.h)
        screen = new CCreditsScreen(this, false, "Crdt");
        break;
    case 9:
        screen = new CExchangeScreen(this);
        break;
    default:   // 6 and above 9: the id itself
        screen = reinterpret_cast<CUiWindow*>(static_cast<std::uintptr_t>(static_cast<unsigned>(id)));
        break;
    }
    Activate(screen);
}

// --- 005F7350: PushWindow --------------------------------------------------------------------------

namespace {

CCursor* Cursor() { return TheCursor(); }   // 005E5200 SetShape (RET 4)

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
    app::g_interfaceHasScreen = 1;
    ScreenOf(window)->CreateScreen();
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
        stackEnd_[-1]->Show(0);
    }
    if (!window) game::FatalError("Cannot activate NULL interface screen\n");
    OpenOnTop(this, window);
}

// --- 005F71C0: Activate ----------------------------------------------------------------------------

namespace {
void DeleteBackdrop(CUiSurface* surface) {
    if (!surface) return;
    delete surface;
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
        ScreenOf(stackEnd_[-1])->CloseScreen();
        delete stackEnd_[-1];
        --stackEnd_;
    }
    ResetInput();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;
    if (!window) {
        game::FatalError("Cannot activate NULL interface screen\n");
        return;
    }
    OpenOnTop(this, window);
}

EI_REWRITTEN(0x005F71C0, CInterface3D::Activate);

EI_REWRITTEN(0x005F7350, CInterface3D::PushWindow);

EI_REWRITTEN(0x005F5BA0, CInterface3D::CreateScreen);

// --- 005F8A80: the message window ------------------------------------------------------------------

void CInterface3D::ShowMessage(int force) {
    bool forced = static_cast<std::uint8_t>(force) != 0;
    if (!forced) {
        using Node = stlp::list<mfc::CString>::Node;
        Node* header = messages_.head;
        Node* node = header->next;
        if (node == header) return;
        mfc::CString text(node->data);
        messages_.Erase(node);   // 005F8E50
        messageText_ = text;
        messageFlag_ = 0;
    }
    if (messageText_.GetLength() == 0) return;
    if (options::TheOptions()->showTutorial_ == 0 && !forced) return;
    PushWindow(new CHelpWindow(this, true, messageText_, messageFlag_), 1, 0);
}

EI_REWRITTEN(0x005F8A80, CInterface3D::ShowMessage);

// --- 005F86F0: the help window -----------------------------------------------------------------------

void CInterface3D::OpenHelp(int flag) {
    if (stackBegin_ == stackEnd_) return;
    CUiWindow* active = stackEnd_[-1];
    const char* topic = ScreenOf(active)->HelpTopic();
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
    if (options::TheOptions()->showTutorial_ != 0) {
        if (!forced && seen >= 0) return;
    } else if (!forced) {
        return;
    }
    PushWindow(new CHelpWindow(this, false, topic, seen), 1, 0);
}

EI_REWRITTEN(0x005F86F0, CInterface3D::OpenHelp);

// --- Screens 8 and 9: constructors and screen 8's destructor body -----------------------------------

namespace {

// Members (thiscall): screen 9's two maps are item caches (lib_trees::ItemCache: 005F9F80 with two
// allocators, 005F98F0); screen 8's player at +0x1D8 (005DB340 / 005DB350).
using ItemCache = lib_trees::ItemCache;

} // namespace

// 005F6F30. Screen 9's constructor.
CExchangeScreen::CExchangeScreen(CInterface3D* ui) : CItemScreen(ui) {
    // The original passes a stack byte and its argument slot as the allocators.
    std::uint8_t junk = 0;
    CInterface3D* slot = ui;
    static_cast<ItemCache*>(&models_.map)->ConstructWith(&junk, &slot);
    models_.limit = 50;
    ItemCache::Construct(&textures_.map);
    textures_.limit = 50;
}
EI_REWRITTEN_SPECIAL(0x005F6F30, CExchangeScreen::CExchangeScreen);

// 005F6D50. Screen 8's constructor: the list at +0x1C8 gets its header node (its allocator byte,
// the flag's low byte, is overwritten), +0x1D4 the text, +0x1F4 the flag.
CCreditsScreen::CCreditsScreen(CInterface3D* ui, bool fromGame, const char* name) : CUiContainer(ui) {
    archive_->Construct();
    lines_.Construct();   // the header from the node pool, linked to itself
    name_ = name;
    player_.Construct();
    fromGame_ = fromGame;
}
EI_REWRITTEN_SPECIAL(0x005F6D50, CCreditsScreen::CCreditsScreen);

// 005F6E40. Screen 8's destructor body: its members, the list (each line's two CStrings, style
// then text) back to the node allocator, then the container base.
CCreditsScreen::~CCreditsScreen() {
    player_.Destruct();
    using Node = stlp::list<TitleLine>::Node;
    for (Node* node = lines_.head->next; node != lines_.head;) {
        Node* next = node->next;
        node->data.style.~CString();
        node->data.text.~CString();
        stlp::deallocate(node, sizeof(Node));
        node = next;
    }
    lines_.head->next = lines_.head;
    lines_.head->prev = lines_.head;
    stlp::deallocate(lines_.head, sizeof(Node));
    archive_.Get()->CMappedResource::Destroy();
}
EI_REWRITTEN_SPECIAL(0x005F6E40, CCreditsScreen::~CCreditsScreen);
EI_REWRITTEN_SPECIAL(0x005F6E20, CCreditsScreen::~CCreditsScreen);   // deleting
EI_REWRITTEN_SPECIAL(0x005FC0F0, CCreditsScreen::~CCreditsScreen);   // the input side's
