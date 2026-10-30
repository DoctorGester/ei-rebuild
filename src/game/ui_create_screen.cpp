// Interface3D::CreateScreen: the screens by id. See ui.h and docs/notes/ui.md.
// The constructors of screens 1, 2, 3 and 7 are inlined here (the container base, their members' constructors
// and vtables); the others' constructors are called as functions.
#include "app.h"

#include "bink_video.h"
#include "console.h"
#include "game_singletons.h"
#include "options.h"
#include "text_manager.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "ui_view_screen.h"
#include "world.h"
#include "world_glue.h"
#include "lib/crt.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <new>
#include <string>

// The screens CreateScreen builds inline (screens 1 and 3; 2 and 7 have only the container's).
BattleScreen::BattleScreen(Interface3D* ui) : UiContainer(ui) {}

BriefingScreen::BriefingScreen(Interface3D* ui) : UiContainer(ui) {}

void Interface3D::CreateScreen(int id) {
    UiWindow* screen = nullptr;
    switch (id) {
    case 0:
        return;
    case 1:   // the in-game screen
        screen = new BattleScreen(this);
        break;
    case 2:   // the main menu
        screen = new MainMenu(this);
        break;
    case 3:   // the base / briefing screen
        screen = new BriefingScreen(this);
        break;
    case 4:
        screen = new GlobalMap(this);
        break;
    case 5:
        screen = new CampPages(this, false);
        break;
    case 7:
        screen = new ViewScreen(this);
        break;
    case 8:   // the titles
        screen = new CreditsScreen(this, false, "Crdt");
        break;
    case 9:
        screen = new ExchangeScreen(this);
        break;
    default:   // 6 and above 9: the original activates the id itself as a window pointer (a crash), here the
               // fatal error of a missing screen
        break;
    }
    Activate(screen);
}

// --- PushWindow ----------------------------------------------------------------------------------

namespace {

// The end both PushWindow and Activate share: the window pushed on the stack, the render state the
// interface draws with (blending by source alpha, the alpha test against 0x80), the window created, the messages
// pumped, the mouse released.
void OpenOnTop(Interface3D* ui, UiWindow* window) {
    ui->stack.push_back(window);
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.alphaTest = true;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.alphaReference = 0x80;
    app::g_interfaceHasScreen = 1;
    ScreenOf(window)->CreateScreen();
    app::PumpMessages();
    ui->mouseCapture = nullptr;
    g_cursor.SetShape(0);
}
} // namespace

void Interface3D::PushWindow(UiWindow* window, bool behind, bool pauseFrame) {
    g_cursor.SetShape(3);
    if (behind && !pauseFrame) {
        backdropMode = 3;
        app::FrameStep(false, true);
    }
    if (stack.size() == 1) {    // one window open
        serverFlag = static_cast<std::uint8_t>(g_serverWorld.paused);
        if (!IsMultiplayerClient()) GameSetServerPaused(1);
        if (behind && pauseFrame) {
            backdropMode = 1;
            app::FrameStep(false, true);
        }
    }
    ctrlDown = false;
    altDown = false;
    shiftDown = 0;
    rightDown = false;
    doubleClick = false;
    if (!stack.empty()) stack.back()->Input()->Deactivate();
    if (!stack.empty()) {
        stack.back()->Show(false);
    }
    if (!window) game::FatalError("Cannot activate NULL interface screen\n");
    OpenOnTop(this, window);
}

// --- Activate ------------------------------------------------------------------------------------

void Interface3D::Activate(UiWindow* window) {
    if (closeAll) {
        closeAll = false;
        CloseAll();
    }
    g_cursor.SetShape(3);
    delete backdrop;
    backdrop = nullptr;
    delete secondBackdrop;
    secondBackdrop = nullptr;
    backdropMode = 0;
    serverFlag = 0;
    GameSetServerPaused(0);
    g_cursor.SetShape(3);
    while (!stack.empty()) {
        stack.back()->Input()->Deactivate();
        ScreenOf(stack.back())->CloseScreen();
        delete stack.back();
        stack.pop_back();
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

void Interface3D::ShowMessage(bool forced) {
    if (!forced) {
        if (messages.empty()) return;
        std::string text = messages.front();
        messages.pop_front();
        messageText = text;
        messageFlag = 0;
    }
    if (messageText.size() == 0) return;
    if (options::g_options.showTutorial == 0 && !forced) return;
    PushWindow(new HelpWindow(this, true, messageText, messageFlag), true, false);
}

// --- the help window -----------------------------------------------------------------------------

void Interface3D::OpenHelp(bool forced) {
    if (stack.empty()) return;
    UiWindow* active = stack.back();
    const char* topic = ScreenOf(active)->HelpTopic();
    if (!topic) return;
    int seen = -1;
    // The seen-topics map (find; operator[], lower_bound an insert at that
    // hint).
    auto found = helpTopics.find(std::string(topic));
    if (found != helpTopics.end()) seen = found->second;
    else helpTopics[std::string(topic)] = 0;
    if (options::g_options.showTutorial != 0) {
        if (!forced && seen >= 0) return;
    } else if (!forced) {
        return;
    }
    PushWindow(new HelpWindow(this, false, topic, seen), true, false);
}

// --- Screens 8 and 9: constructors -------------------------------------------------------------------

ExchangeScreen::ExchangeScreen(Interface3D* ui) : ItemScreen(ui) {}

CreditsScreen::CreditsScreen(Interface3D* ui, bool newFromGame, const char* newName)
    : UiContainer(ui), name(newName), fromGame(newFromGame) {}

// --- Ui screen constructors --------------------------------------------------------------------------

// Constructors of interface windows: the message box, the objectives window
// and the battle screen's portraits panel. Their Create / Build
// does the rest. See ui.h and docs/notes/ui.md.

namespace {

constexpr std::uint32_t kEditField = 4;   // the message box's flag: an edit field instead of the text

} // namespace

// --- The message box ----------------------------------------------------------------------------------

MessageWindow::MessageWindow(Interface3D* ui, std::string_view titleKey, const char* textKey, std::uint32_t newResult,
                         std::uint32_t newFlags, std::uint32_t newFormat)
    : UiContainer(ui), result(newResult), flags(newFlags), format(newFormat) {
    std::string line;
    TextManager* texts = g_clientWorld.texts;
    line = texts->GetString(titleKey);
    title = line;
    if (flags & kEditField) {
        editText = const_cast<char*>(textKey);    // the caller's buffer: OK copies the answer into it
    } else {
        editText = nullptr;
        line = texts->GetString(textKey);
        text = line;
    }
}

// --- The objectives window ----------------------------------------------------------------------------

// --- The portraits panel ------------------------------------------------------------------------------

HudPortraits::HudPortraits(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {
    rect = Rect{250, 440, 550, 600};
    count = 0;
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);
}
