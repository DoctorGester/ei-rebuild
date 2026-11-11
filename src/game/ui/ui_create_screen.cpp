// Interface3D::CreateScreen: the screens by id. See ui.h and docs/notes/ui.md.
#include "game/app/app.h"

#include "game/app/movies.h"
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "engine/data/options.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/ui/ui_hud.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_help_window.h"
#include "game/ui/ui_briefing.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_credits.h"
#include "game/ui/ui_screens.h"
#include "game/ui/ui_view_screen.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <new>
#include <string>

void Interface3D::CreateScreen(int id) {
    std::unique_ptr<UiContainer> screen;
    switch (id) {
    case 0:
        return;
    case 1:   // the in-game screen
        screen = std::make_unique<BattleScreen>();
        break;
    case 2:   // the main menu
        screen = std::make_unique<MainMenu>();
        break;
    case 3:   // the base / briefing screen
        screen = std::make_unique<BriefingScreen>();
        break;
    case 4:
        screen = std::make_unique<GlobalMap>();
        break;
    case 5:
        screen = std::make_unique<CampPages>(false);
        break;
    case 7:
        screen = std::make_unique<ViewScreen>();
        break;
    case 8:   // the titles
        screen = std::make_unique<CreditsScreen>(false, "Crdt");
        break;
    case 9:
        screen = std::make_unique<ExchangeScreen>();
        break;
    default:   // 6 and above 9: the original activates the id itself as a window pointer (a crash), here the
               // fatal exit of a missing screen
        break;
    }
    Activate(std::move(screen));
}

// --- PushWindow ----------------------------------------------------------------------------------

namespace {

// The end both PushWindow and Activate share: the window pushed on the stack, the render state the
// interface draws with (blending by source alpha, the alpha test against 0x80), the window created, the messages
// pumped, the mouse released.
void OpenOnTop(std::unique_ptr<UiContainer> window) {
    UiContainer* opened = window.get();
    g_interface.stack.push_back(std::move(window));
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.alphaTest = true;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.alphaReference = 0x80;
    app::g_interfaceHasScreen = 1;
    opened->CreateScreen();
    app::PumpMessages();
    g_interface.mouseCapture = nullptr;
    g_cursor.SetShape(CursorShape::Default);
}
} // namespace

void Interface3D::PushWindow(std::unique_ptr<UiContainer> window, WindowBackdrop keep) {
    g_cursor.SetShape(CursorShape::Wait);
    if (keep == WindowBackdrop::Screen) {
        backdropMode = Backdrop::TakeSecond;
        backdropWindows = stack.size();
        app::FrameStep(false);
    }
    if (stack.size() == 1) {    // one window open
        serverFlag = static_cast<std::uint8_t>(g_serverWorld.paused);
        if (!IsMultiplayerClient()) g_serverWorld.SetServerPaused(1);
        if (keep == WindowBackdrop::FirstWindow) {
            backdropMode = Backdrop::TakeFirst;
            app::FrameStep(false);
        }
    }
    ResetInput();
    if (!window) game::FatalExit("Cannot activate NULL interface screen\n");
    OpenOnTop(std::move(window));
}

// --- Activate ------------------------------------------------------------------------------------

void Interface3D::Activate(std::unique_ptr<UiContainer> window) {
    CloseAllPending();
    g_cursor.SetShape(CursorShape::Wait);
    backdrop.reset();
    secondBackdrop.reset();
    backdropMode = Backdrop::None;
    serverFlag = 0;
    g_serverWorld.SetServerPaused(0);
    CloseAll();
    if (!window) game::FatalExit("Cannot activate NULL interface screen\n");
    OpenOnTop(std::move(window));
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
    PushWindow(std::make_unique<HelpWindow>(true, messageText, messageFlag), WindowBackdrop::Screen);
}

// --- the help window -----------------------------------------------------------------------------

void Interface3D::OpenHelp(bool forced) {
    if (stack.empty()) return;
    const char* topic = stack.back()->HelpTopic();
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
    PushWindow(std::make_unique<HelpWindow>(false, topic, seen), WindowBackdrop::Screen);
}

// --- The message box ----------------------------------------------------------------------------------

MessageWindow::MessageWindow(std::string_view titleKey, std::string_view textKey, MessageQuestion newQuestion,
                             MessageButtons newButtons, char* editBuffer)
    : question(newQuestion), buttons(newButtons) {
    std::string line;
    TextManager* texts = g_clientWorld.texts.get();
    line = texts->GetString(titleKey);
    title = line;
    if (buttons == MessageButtons::OkCancelEdit) {
        editText = editBuffer;    // the caller's buffer: OK copies the answer into it
    } else {
        line = texts->GetString(textKey);
        text = line;
    }
}
