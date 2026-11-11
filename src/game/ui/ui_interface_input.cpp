// The interface's input dispatch (Interface3D): the input dispatcher closes the
// windows when asked, then hands each mouse event here (on to the
// window that took the mouse or the top window) and each typed character and key release
// (on to the console, the chat line or the top window). See ui.h and
// docs/notes/ui.md ("Container input dispatch").
#include "game/ui/ui.h"
#include "engine/scene/device.h"
#include "game/ui/ui_chat_overlay.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_message_box.h"

#include "game/app/app.h"
#include "game/console/console.h"
#include "engine/core/core_maths.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"
#include "game/net/net_session.h"
#include "engine/data/text_manager.h"
#include "game/sound/sound.h"
#include "engine/core/text_split.h"
#include "game/ui/ui_briefing.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_screens.h"
#include "game/ui/screen_layout.h"
#include "game/world/world.h"
#include "game/terrain/terrain.h"
#include "platform/log.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "platform/platform.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

UiWindow* Interface3D::MouseTarget() const {
    if (mouseCapture) return mouseCapture;
    return stack.empty() ? nullptr : stack.back().get();
}

namespace {
constexpr int kScanConsoleKey = 0x29;   // the key under Esc (the console's)

} // namespace

void Interface3D::OnMouse(UiEvent event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: doubleClick = false; break;
    case UiEventKind::LeftDouble: doubleClick = true; break;
    case UiEventKind::RightDown:
    case UiEventKind::RightDouble: rightDown = true; break;
    case UiEventKind::RightUp: rightDown = false; break;
    default: break;
    }
    if (UiWindow* target = MouseTarget()) target->OnEvent(event);
    if (event.kind == UiEventKind::LeftUp) doubleClick = false;
}

// --- Keys ------------------------------------------------------------------

void Interface3D::OnChar(int key, int scan) {
    if (scan == kScanConsoleKey) return;
    if (console->shown) {
        console->OnChar(key, scan);
        return;
    }
    UiEvent event{.kind = UiEventKind::Char, .scan = scan, .character = key};
    if (IsMultiplayerClient() && root->chatOpen) {
        root->OnEvent(event);
        return;
    }
    if (!stack.empty()) stack.back()->OnEvent(event);
}

void Interface3D::OnKeyUp(platform::Key key, int scan) {
    if (key == platform::Key::Shift)
        shiftDown = 0;
    else if (key == platform::Key::Control)
        ctrlDown = false;
    else if (key == platform::Key::Alt)
        altDown = false;
    if (console->shown) {
        console->OnKeyUp(key, scan);
        return;
    }
    UiEvent event{.kind = UiEventKind::KeyUp, .key = key, .scan = scan};
    if (!stack.empty()) stack.back()->OnEvent(event);
}

void console::ConsoleWindow::OnKeyUp(platform::Key key, int) {
    if (key == platform::Key::Shift)
        shift = false;
    else if (key == platform::Key::Control)
        ctrl = false;
}

// --- Every window closed --------------------------------------------------

void Interface3D::PopWindow() {
    stack.back()->Deactivate();
    stack.back()->CloseScreen();
    stack.pop_back();
}

void Interface3D::ResetInput() {
    ctrlDown = false;
    altDown = false;
    shiftDown = 0;
    rightDown = false;
    doubleClick = false;
    if (!stack.empty()) stack.back()->Deactivate();
}

void Interface3D::CloseAll() {
    g_cursor.SetShape(CursorShape::Wait);
    while (!stack.empty()) PopWindow();
    ResetInput();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;
}

void Interface3D::CloseAllPending() {
    if (!closeAll) return;
    closeAll = false;
    CloseAll();
}

// --- Ui interface frame ------------------------------------------------------------------------------

// Interface3D's per-frame update. See docs/notes/ui.md ("Interface base").

namespace {

// The value a window leaves pending that ends the game session (the message boxes' "yes").
constexpr std::uint32_t kLeaveGame = 0x990001;

} // namespace

void Interface3D::Update() {
    // A pending value closes the top window: it is cancelled, deactivated and deleted, the
    // backdrop changes when asked to, the last window closing drops the backdrop and hands the
    // server flag to the world server (single player), and the window below gets the value.
    while (pendingFlag) {
        pendingFlag = false;
        g_cursor.SetShape(CursorShape::Wait);
        ResetInput();
        if (!stack.empty()) PopWindow();
        app::PumpMessages();
        if (backdropMode == Backdrop::Both) {
            backdrop = std::move(secondBackdrop);
            backdropMode = backdrop ? Backdrop::First : Backdrop::None;
        }
        if (std::ssize(stack) == 1) {
            backdrop.reset();
            screenCopy.reset();
            backdropMode = Backdrop::None;
            if (!IsMultiplayerClient()) g_serverWorld.SetServerPaused(serverFlag);
            serverFlag = 0;
        }
        if (!stack.empty()) {
            stack.back()->MessageResult(pendingValue);
        }
        if (pendingValue == kLeaveGame) {
            closeAll = true;
            g_gameFlow.DropSessions();
            g_gameFlow.CloseSessions();
        }
        mouseCapture = nullptr;
        g_cursor.SetShape(CursorShape::Default);
    }
    if (pollTop && !stack.empty()) pollTop = !stack.back()->ReadyToStopPolling();
    CloseAllPending();
    mouse = g_cursor.position;
    if (disconnected && IsMultiplayerClient() && net::g_gameClient->joinState != net::ClientBase::JoinState::Joined) {
        disconnected = false;
        auto box = std::make_unique<MessageWindow>("disconnect", "disconnect_msg", MessageQuestion::Disconnect, MessageButtons::Ok);
        if (!disconnectReason.empty()) box->text = std::exchange(disconnectReason, std::string());
        ei::Log("interface: the network client is not joined (state %u): the disconnect box: %s",   // not the game's
                static_cast<unsigned>(net::g_gameClient->joinState), box->text);
        PushWindow(std::move(box), WindowBackdrop::FirstWindow);
    } else if (!stack.empty()) {
        stack.back()->Update();
    }
    root->Update();
}
// ---- String events ------------------------------------------------------------------

void Interface3D::OnStringEvent(std::string_view event) {
    std::vector<std::string> words;
    std::string text(event);
    words = text::SplitWords(text, ' ');
    str::MakeLower(words[0]);
    // "gameover [title [text]]" plays buttons\gameover.wav and shows a message window (the texts default to the keys
    // game_over and game_over_msg; Rebirth's NewStringEvents).
    if (str::Compare(words[0], "gameover") == 0) {
        g_clientWorld.sounds->StopAll(true);
        g_clientWorld.terrain->StopAmbience();
        PlayInterfaceSound("buttons\\gameover.wav");
        const std::string_view title = words.size() > 1 ? std::string_view(words[1]) : "game_over";
        const std::string_view message = words.size() > 2 ? std::string_view(words[2]) : "game_over_msg";
        PushWindow(std::make_unique<MessageWindow>(title, message, MessageQuestion::Second, MessageButtons::OkCancel), WindowBackdrop::FirstWindow);
        return;
    }
    if (str::Compare(words[0], "say") == 0 || str::Compare(words[0], "say_block") == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size() < 2) return;
        int number = words.size() > 2 ? TextKeyFromName(words[2]) : 0;
        AckKind kind = str::Compare(words[0], "say_block") != 0 ? AckKind::Scenario : AckKind::ScenarioTalking;
        if (BattleScreen* screen = OnTop(battle)) screen->Speech(number, kind, words[1]);
    } else if (str::Compare(words[0], "tutorial") == 0) {   // queued
        if (words.size() < 2) return;
        messages.push_back(words[1]);
    } else if (str::Compare(words[0], "briefing") == 0) {
        if (words.size() < 2) return;
        if (BriefingScreen* screen = OnTop(briefing)) screen->ShowBriefing(words[1]);
    } else if (str::Compare(words[0], "autosave") == 0) {
        pollTop = true;
    } else if (str::Compare(words[0], "endofgame") == 0) {
        if (!stack.empty()) stack.back()->GameOver(true);
    }
}

// --- Ui interface glue -------------------------------------------------------------------------------

// The interface manager's small services to the rest of the game (Interface3D): interface sounds,
// the console window settings, the frame rate surface and quest variable news for
// the top window. See ui.h and docs/notes/ui.md, section 3.

namespace {
constexpr int kInterfacePriority = 1000;
constexpr int kLayoutWidth = 800;
// The frame rate's surface: (730, 10)-(800, 28).
constexpr int kFpsLeft = 730, kFpsTop = 10, kFpsRight = 800, kFpsBottom = 28;
constexpr int kQuestPart = 2;   // "q.<zone>.<quest>": the quest's name
// The status line: a surface over (0, 280)-(800, 320), the text in green in its top 40 pixels.
constexpr int kStatusTop = 280, kStatusBottom = 320;
constexpr int kStatusFont = 2, kStatusTextHeight = 40;
constexpr std::uint32_t kStatusColour = 0xFF00;

// A unit's speech without an extra argument.
constexpr std::string_view kNoExtra;

} // namespace

int Interface3D::PlayInterfaceSound(std::string_view name, bool loop) {
    return g_clientWorld.sounds->Play2D(name, kInterfacePriority, kFullVolume, 0, {.loop = loop});
}

void Interface3D::StopSound(int handle) {
    if (handle != kNoSound) g_clientWorld.sounds->StopSound(handle);
}

void Interface3D::ConsoleAddText(std::string_view text) {
    if (console) console->AddText(text);
}

void Interface3D::SetConsoleRect(const Rect& rect) {
    if (console) console->SetRect(rect);
}

void Interface3D::SetConsoleHistory(int lines) {
    if (console) console->SetHistory(lines);
}

void Interface3D::ShowFps(bool on) {
    if (!on) {
        fpsSurface.reset();
        return;
    }
    if (fpsSurface) return;
    fpsSurface = std::make_unique<UiSurface>(kFpsLeft, kFpsTop, kFpsRight, kFpsBottom, SurfaceBlend::Keyed, true);
    fpsText.clear();
    fpsSurface->PaintWith([this](UiSurface& out) { PaintFps(out); });
}

void Interface3D::OnQuestVariable(std::string_view name, float value) {
    if (BattleScreen* screen = OnTop(battle)) screen->QuestVariable(name, value);
    std::vector<std::string> parts;
    if (value != 1.0f) return;
    parts = text::SplitWords(name, '.');
    if (parts.size() > kQuestPart) savedText = parts[kQuestPart];
}

// --- News for the top window -------------------------------------

void Interface3D::OnItemsPicked(const std::vector<Item*>& items, int money, bool crippled) {
    if (BattleScreen* screen = OnTop(battle)) screen->ItemsPicked(items, money, crippled);
}

void Interface3D::OnUnitJoined(Unit* unit) {
    if (!stack.empty()) stack.back()->UnitJoined(unit);
}

void Interface3D::OnUnitLeft(Unit* unit) {
    if (!stack.empty()) stack.back()->UnitLeft(unit);
}

void Interface3D::OnGameLost() {
    if (!stack.empty()) stack.back()->GameOver(false);
}

void Interface3D::OnUnitSpeech(int id, AckKind kind) {
    if (BattleScreen* screen = OnTop(battle)) screen->Speech(id, kind, kNoExtra);
}

void Interface3D::OnPlayersChanged(int added) {
    if (BriefingScreen* screen = OnTop(briefing)) screen->PlayersChanged(added);
}

void Interface3D::OnChatLine(std::string_view text) {
    root->AddLine(text);
}

void Interface3D::OnExchangeDone(Party* partyB, Party* partyA, Party* partyC) {
    if (ExchangeScreen* screen = OnTop(exchange)) screen->ExchangeDone(partyB, partyA, partyC);
}

// The bottom window, not the top one (a save is made from the battle screen under the save window).
void Interface3D::SaveWindowState() {
    if (!stack.empty()) stack[0]->SaveState();
}

void Interface3D::ShowStatusText(std::string_view text) {
    statusSurface = std::make_unique<UiSurface>(0, kStatusTop, kLayoutWidth, kStatusBottom, SurfaceBlend::Keyed, true);
    statusSurface->PaintWith([text](UiSurface& out) {
        Rect rect = {0, 0, kLayoutWidth, kStatusTextHeight};
        out.DrawTextIn(text, rect, TextStyle{kStatusFont, kStatusColour, render::text::Format::Center});    // Rebirth-patched
    });
    app::FrameStep(true);
    statusSurface.reset();
}

// --- The interface manager and its chat keys ---------------------------------------------------------

Interface3D g_interface;

void Interface3D::ChatKeyEnter() {
    if (!root->chatOpen) root->SetOpen(true);
}

void Interface3D::ChatKeyBackspace() {
    if (!root->chatOpen) root->ClearLines();
}

// --- Ui interface draw -------------------------------------------------------------------------------

// The interface's per-frame draw (Interface3D::Draw, called by the main loop between the
// scene's BeginScene and EndScene) and the screen fade drawn over it (ScreenFade::Draw).
// See docs/notes/ui.md, section 3, and docs/notes/renderer.md, section 2.

namespace {

constexpr float kOpaque = 255.0f;

Rect ScreenOf(const SceneRenderer* renderer) {
    Rect screen;
    screen = renderer->device->screen;
    return screen;
}

// --- The overlays ---

constexpr std::uint32_t kFpsPeriod = 400;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFpsFont = 1;

} // namespace

int ScreenFade::LetterboxBar(int height) {
    constexpr float kLetterboxShare = 0.217f;   // the bars' share of the screen height
    return static_cast<int>(height * kLetterboxShare);
}

void ScreenFade::Draw(SceneRenderer* renderer) {
    Rect screen = ScreenOf(renderer);
    if (letterbox) {
        const int width = screen.Width();
        const int height = screen.Height();
        const int bar = LetterboxBar(height);
        render::g_renderState.depthTest = false;
        renderer->DrawRectangle(0, 0, width, bar, -1, 0);
        renderer->DrawRectangle(0, height - bar, width, height, -1, 0);
        render::g_renderState.depthTest = true;
    }
    if (!active) return;
    // A step towards 255 (rate > 0) or 0 (rate < 0), by the milliseconds since the last one.
    if ((alpha < kOpaque && rate > 0.0f) || (alpha > 0.0f && rate < 0.0f)) {
        const std::uint32_t now = platform::FrameClock();
        alpha = (now - lastTime) * rate + alpha;
        lastTime = platform::FrameClock();
    }
    if (alpha > kOpaque) {
        alpha = kOpaque;
    } else if (alpha < 0.0f) {
        active = false;
        return;
    }
    render::g_renderState.depthTest = false;
    screen = ScreenOf(renderer);
    renderer->DrawRectangle(0, 0, screen.Width(), screen.Height(), static_cast<int>(alpha), 0);
    render::g_renderState.depthTest = true;
}

void ScreenFade::Start(int ms) {
    rate = kOpaque / ms;
    alpha = ms > 0 ? 0.0f : kOpaque;
    lastTime = platform::FrameClock();
    active = true;
}

void Interface3D::Draw() {
    CloseAllPending();
    if ((backdropMode == Backdrop::First || backdropMode == Backdrop::Both || backdropMode == Backdrop::TakeSecond) && backdrop) backdrop->Blit();
    if (!stack.empty()) stack.back()->DrawSprites(&g_sceneRenderer);
    fade->Draw(&g_sceneRenderer);
}

// The frame rate, once measured: "FPS:" at the left, the rate at the right.
void Interface3D::PaintFps(UiSurface& surface) const {
    if (fpsText.empty()) return;
    Rect rect{0, 0, 60, 18};
    surface.DrawTextIn("FPS:", rect, TextStyle{kFpsFont, kWhite});
    surface.DrawTextIn(fpsText, rect, TextStyle{kFpsFont, kWhite, render::text::Format::Right});
}

void Interface3D::DrawOverlays() {
    if (!stack.empty()) {
        stack.back()->DrawSurfaces();
        stack.back()->DrawTips();
    }
    // (The original calls an empty function on the fade here; nothing to do.)
    if (statusSurface) statusSurface->Blit();

    if (backdropMode == Backdrop::TakeFirst) {
        // A window opens over the screen: the screen greyed behind it.
        // The backdrop is the whole window in pixels, unscaled (Rebirth's WideScreen; the original's 800 x 600 layout
        // rectangle scaled is the same on a 4:3 window).
        if (!backdrop) backdrop = std::make_unique<UiSurface>(0, 0, width, height, SurfaceBlend::Opaque, false);
        backdrop->CaptureScreen();
        if (!screenCopy) screenCopy = std::make_unique<UiSurface>(0, 0, width, height, SurfaceBlend::Opaque, false);
        screenCopy->CaptureScreen();
        backdrop->Greyscale();
        backdropMode = Backdrop::First;
    } else if (backdropMode == Backdrop::TakeSecond) {
        // A window opens over a window: the screen as it is, the old backdrop put aside.
        secondBackdrop = std::move(backdrop);
        backdrop = std::make_unique<UiSurface>(0, 0, width, height, SurfaceBlend::Opaque, false);
        backdrop->CaptureScreen();
        backdropMode = Backdrop::Both;
    }
    if (IsMultiplayerClient() && std::ssize(stack) == 1) root->DrawSurfaces();

    ++fpsFrames;
    const std::uint32_t now = platform::FrameClock();
    const std::uint32_t elapsed = now - fpsTime;
    if (elapsed > kFpsPeriod) {
        if (fpsSurface) {
            const float perSecond = fpsFrames * 1000.0f / static_cast<int>(elapsed);
            fpsText = str::Format("%.1f", perSecond);
        }
        fpsTime = now;
        fpsFrames = 0;
    }
    if (fpsSurface) fpsSurface->Blit();
    if (console && console->shown) console->Blit();
}
