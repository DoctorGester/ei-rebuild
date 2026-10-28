// CInterface3D::CreateScreen: the screens by id. See ui.h and docs/notes/ui.md.
// The constructors of screens 1, 2, 3 and 7 are inlined here (the container base, their members' constructors
// and vtables); the others' constructors are called as functions.
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

#include "render/render_state.h"
#include "lib/crt.h"
#include <cstdint>
#include <new>
#include <string>
#include "game_data.h"
#include "world_glue.h"
#include "game_singletons.h"

#include <cstring>
#include <iterator>
#include <list>
#include <memory>

// The screens CreateScreen builds inline (screens 1 and 3; 2 and 7 have only the container's).
CBattleScreen::CBattleScreen(CInterface3D* ui) : CUiContainer(ui) {
    itemUseKey_.ConstructItemUseKey();
}

CBriefingScreen::CBriefingScreen(CInterface3D* ui) : CUiContainer(ui) {}

void CInterface3D::CreateScreen(int id) {
    CUiWindow* screen = nullptr;
    switch (id) {
    case 0:
        return;
    case 1:   // the in-game screen
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
    case 8:   // the titles
        screen = new CCreditsScreen(this, false, "Crdt");
        break;
    case 9:
        screen = new CExchangeScreen(this);
        break;
    default:   // 6 and above 9: the original activates the id itself as a window pointer (a crash), here the
               // fatal error of a missing screen
        break;
    }
    Activate(screen);
}

// --- PushWindow ----------------------------------------------------------------------------------

namespace {

CCursor* Cursor() { return TheCursor(); }

// The window stack's insert when it is full (vector<CUiWindow*>'s, plain elements).

void SetRenderState(CTextureDevice* device, unsigned long state, unsigned long value) {
    uidevice::SetRenderState(device, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), static_cast<std::uint32_t>(value));
}

// The end both PushWindow and Activate share: the window pushed on the stack, the render states
// the interface draws with, the window created, the messages pumped, the mouse released.
void OpenOnTop(CInterface3D* ui, CUiWindow* window) {
    ui->stack_.push_back(window);
    static const unsigned long kStates[5][2] = {{0x1B, 1}, {0x0F, 1}, {0x13, 5}, {0x14, 6}, {0x18, 0x80}};
    for (const auto& state : kStates) {
        SetRenderState(ui->renderer_, state[0], state[1]);
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
    if (stack_.size() == 1) {   // one window open
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
    if (!stack_.empty()) stack_.back()->Input()->Deactivate();
    if (!stack_.empty()) {
        stack_.back()->Show(0);
    }
    if (!window) game::FatalError("Cannot activate NULL interface screen\n");
    OpenOnTop(this, window);
}

// --- Activate ------------------------------------------------------------------------------------

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
    while (!stack_.empty()) {
        stack_.back()->Input()->Deactivate();
        ScreenOf(stack_.back())->CloseScreen();
        delete stack_.back();
        stack_.pop_back();
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

// --- the message window --------------------------------------------------------------------------

void CInterface3D::ShowMessage(int force) {
    bool forced = static_cast<std::uint8_t>(force) != 0;
    if (!forced) {
        if (messages_.empty()) return;
        std::string text = messages_.front();
        messages_.pop_front();
        messageText_ = text;
        messageFlag_ = 0;
    }
    if (messageText_.size() == 0) return;
    if (options::TheOptions()->showTutorial_ == 0 && !forced) return;
    PushWindow(new CHelpWindow(this, true, messageText_.c_str(), messageFlag_), 1, 0);
}

// --- the help window -----------------------------------------------------------------------------

void CInterface3D::OpenHelp(int flag) {
    if (stack_.empty()) return;
    CUiWindow* active = stack_.back();
    const char* topic = ScreenOf(active)->HelpTopic();
    if (!topic) return;
    int seen = -1;
    // The seen-topics map (find; operator[], lower_bound an insert at that
    // hint).
    auto found = helpTopics_.find(std::string(topic));
    if (found != helpTopics_.end()) seen = found->second;
    else helpTopics_[std::string(topic)] = 0;
    bool forced = static_cast<std::uint8_t>(flag) != 0;
    if (options::TheOptions()->showTutorial_ != 0) {
        if (!forced && seen >= 0) return;
    } else if (!forced) {
        return;
    }
    PushWindow(new CHelpWindow(this, false, topic, seen), 1, 0);
}

// --- Screens 8 and 9: constructors and screen 8's destructor body -----------------------------------

CExchangeScreen::CExchangeScreen(CInterface3D* ui) : CItemScreen(ui) {}

CCreditsScreen::CCreditsScreen(CInterface3D* ui, bool fromGame, const char* name) : CUiContainer(ui) {
    new (archive_.Get()) CMappedResource();
    name_ = name;
    player_.Construct();
    fromGame_ = fromGame;
}

CCreditsScreen::~CCreditsScreen() {
    player_.Destruct();
    archive_.Get()->CMappedResource::Destroy();
}
