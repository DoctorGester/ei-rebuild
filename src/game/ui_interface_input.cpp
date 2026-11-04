// The interface's input dispatch (Interface3D): the input dispatcher closes the
// windows when asked, then hands each mouse message here (on to the
// input side that took the mouse or the top window's) and each typed character and key release
// (on to the console, the chat line or the top window). See ui.h and
// docs/notes/ui.md ("Container input dispatch").
#include "ui.h"

#include "app.h"
#include "console.h"
#include "core_maths.h"
#include "game_singletons.h"
#include "model.h"
#include "net_classes.h"
#include "object_tables.h"
#include "sound.h"
#include "text_split.h"
#include "texture.h"
#include "ui_cursor.h"
#include "screen_layout.h"
#include "world.h"
#include "world_glue.h"
#include "../platform/log.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "platform/platform.h"
#include "render/render_state.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

UiInputSide* Interface3D::MouseTarget() const {
    if (mouseCapture) return mouseCapture;
    if (stack.begin() != stack.end()) return stack.end()[-1]->Input();
    return nullptr;
}

namespace {
constexpr int kScanConsoleKey = 0x29;   // the key under Esc (the console's)
constexpr int kCursorWait = 3;

} // namespace

void Interface3D::OnLeftDown(Point point) {
    doubleClick = false;
    if (UiInputSide* target = MouseTarget()) target->LeftDown(point);
}

void Interface3D::OnLeftUp(Point point) {
    if (UiInputSide* target = MouseTarget()) target->LeftUp(point);
    doubleClick = false;
}

void Interface3D::OnMouseMove(Point point) {
    if (UiInputSide* target = MouseTarget()) target->MouseMove(point);
}

void Interface3D::OnRightDown(Point point) {
    rightDown = true;
    if (UiInputSide* target = MouseTarget()) target->RightDown(point);
}

void Interface3D::OnRightUp(Point point) {
    rightDown = false;
    if (UiInputSide* target = MouseTarget()) target->RightUp(point);
}

void Interface3D::OnLeftDouble(Point point) {
    doubleClick = true;
    if (UiInputSide* target = MouseTarget()) target->LeftDouble(point);
}

void Interface3D::OnRightDouble(Point point) {
    rightDown = true;
    if (UiInputSide* target = MouseTarget()) target->RightDouble(point);
}

void Interface3D::OnWheel(int delta, Point point) {
    if (UiInputSide* target = MouseTarget()) target->Wheel(delta, point);
}

// --- Keys ------------------------------------------------------------------

void Interface3D::OnChar(int key, int scan) {
    if (scan == kScanConsoleKey) return;
    if (console->shown) {
        console->OnChar(key, scan);
        return;
    }
    if (IsMultiplayerClient() && root->chatOpen) {
        root->Input()->Char(key, scan);
        return;
    }
    if (!stack.empty()) stack.back()->Input()->Char(key, scan);
}

void Interface3D::OnKeyUp(int key, int scan) {
    if (key == platform::kKeyShift)
        shiftDown = 0;
    else if (key == platform::kKeyControl)
        ctrlDown = false;
    else if (key == platform::kKeyMenu)
        altDown = false;
    if (console->shown) {
        console->OnKeyUp(key, scan);
        return;
    }
    if (!stack.empty()) stack.back()->Input()->KeyUp(key, scan);
}

void console::ConsoleWindow::OnKeyUp(int key, int) {
    if (key == platform::kKeyShift)
        shift = false;
    else if (key == platform::kKeyControl)
        ctrl = false;
}

// --- Every window closed --------------------------------------------------

void Interface3D::CloseAll() {
    g_cursor.SetShape(kCursorWait);
    while (!stack.empty()) {
        stack.back()->Input()->Deactivate();
        stack.back()->CloseScreen();
        delete stack.back();
        stack.pop_back();
    }
    ctrlDown = false;
    altDown = false;
    shiftDown = 0;
    rightDown = false;
    doubleClick = false;
    // No top window to deactivate here (the original deactivates it, but none can exist any more).
    if (!stack.empty()) stack.back()->Input()->Deactivate();
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
        g_cursor.SetShape(3);
        ctrlDown = false;
        altDown = false;
        shiftDown = 0;
        rightDown = false;
        doubleClick = false;
        if (!stack.empty()) stack.back()->Input()->Deactivate();
        if (!stack.empty()) {
            stack.back()->CloseScreen();
            delete stack.back();
            stack.pop_back();
        }
        app::PumpMessages();
        if (backdropMode == 4) {
            delete backdrop;
            UiSurface* next = secondBackdrop;
            secondBackdrop = nullptr;
            backdrop = next;
            backdropMode = next ? 2 : 0;
        }
        if (std::ssize(stack) == 1) {
            delete backdrop;
            backdrop = nullptr;
            delete screenCopy;
            screenCopy = nullptr;
            backdropMode = 0;
            if (!IsMultiplayerClient()) GameSetServerPaused(serverFlag);
            serverFlag = 0;
        }
        if (!stack.empty()) {
            stack.back()->MessageResult(static_cast<int>(pendingValue));
        }
        if (pendingValue == kLeaveGame) {
            closeAll = true;
            g_gameFlow.DropSessions();
            g_gameFlow.CloseSessions();
        }
        mouseCapture = nullptr;
        g_cursor.SetShape(0);
    }
    if (pollTop && !stack.empty()) pollTop = !stack.back()->ReadyToStopPolling();
    if (closeAll) {
        closeAll = false;
        g_cursor.SetShape(3);
        while (!stack.empty()) {
            stack.back()->Input()->Deactivate();
            stack.back()->CloseScreen();
            delete stack.back();
            stack.pop_back();
        }
        ResetInput();
        app::PumpMessages();
        app::g_interfaceHasScreen = 0;
    }
    if (queuedWindow) {
        Activate(queuedWindow);
        queuedWindow = nullptr;
    }
    mouse = g_cursor.position;
    if (disconnected && IsMultiplayerClient() && net::g_gameClient->joinState != 2) {
        disconnected = false;
        ei::Log("interface: the network client is not joined (state %u): the disconnect box",   // not the game's
                static_cast<unsigned>(net::g_gameClient->joinState));
        MessageWindow* box = new MessageWindow(this, "disconnect", "disconnect_msg", 0x990000, 1, 1);
        PushWindow(box, true, true);
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
        GameStopSounds(1);
        GameStopAmbience();
        PlayInterfaceSound("buttons\\gameover.wav");
        const std::string_view title = words.size() > 1 ? std::string_view(words[1]) : "game_over";
        const char* message = words.size() > 2 ? words[2].c_str() : "game_over_msg";
        MessageWindow* window = new MessageWindow(this, title, message, 0x20000, 3, 1);
        PushWindow(window, true, true);
        return;
    }
    auto top = [this]() { return stack.back(); };
    if (str::Compare(words[0], "say") == 0 || str::Compare(words[0], "say_block") == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size() < 2) return;
        int number = words.size() > 2 ? TextKeyFromName(words[2]) : 0;
        AckKind kind = str::Compare(words[0], "say_block") != 0 ? AckKind::Scenario : AckKind::ScenarioTalking;
        if (!stack.empty())
            top()->Speech(number, kind, const_cast<char*>(words[1].c_str()));
    } else if (str::Compare(words[0], "tutorial") == 0) {   // queued
        if (words.size() < 2) return;
        messages.push_back(words[1]);
    } else if (str::Compare(words[0], "briefing") == 0) {
        if (words.size() < 2 || stack.empty()) return;
        top()->ShowBriefing(words[1]);
    } else if (str::Compare(words[0], "autosave") == 0) {
        pollTop = true;
    } else if (str::Compare(words[0], "endofgame") == 0) {
        if (!stack.empty()) top()->GameOver(true);
    }
}

// --- Ui interface glue -------------------------------------------------------------------------------

// The interface manager's small services to the rest of the game (Interface3D): interface sounds,
// the console window settings, the frame rate surface and quest variable news for
// the top window. See ui.h and docs/notes/ui.md, section 3.

namespace {
constexpr int kInterfacePriority = 1000;
constexpr int kFullVolume = 100;
constexpr int kNoSound = -1;
constexpr int kLayoutWidth = 800;
// The frame rate's surface: (730, 10)-(800, 28).
constexpr int kFpsLeft = 730, kFpsTop = 10, kFpsRight = 800, kFpsBottom = 28;
constexpr int kQuestPart = 2;   // "q.<zone>.<quest>": the quest's name
// The status line: a surface over (0, 280)-(800, 320), the text in green in its top 40 pixels.
constexpr int kStatusTop = 280, kStatusBottom = 320;
constexpr int kStatusFont = 2, kStatusTextHeight = 40;
constexpr std::uint32_t kStatusColour = 0xFF00;

// A unit's speech without an extra argument.
constexpr char* kNoExtra = nullptr;

} // namespace

int Interface3D::PlayInterfaceSound(std::string_view name, bool loop) {
    return g_clientWorld.sounds->Play2D(name, kInterfacePriority, kFullVolume, 0, loop, 0, 0, 0);
}

void Interface3D::StopSound(int handle) {
    if (handle != kNoSound) g_clientWorld.sounds->StopSound(handle);
}

void Interface3D::ConsoleAddText(std::string_view text) {
    if (console) console->AddText(text);
}

void Interface3D::SetConsoleRect(const Rect* rect) {
    if (console) console->SetRect(rect);
}

void Interface3D::SetConsoleHistory(int lines) {
    if (console) console->SetHistory(lines);
}

void Interface3D::ShowFps(bool on) {
    if (!on) {
        if (fpsSurface) {
            delete fpsSurface;
        }
        fpsSurface = nullptr;
        return;
    }
    if (fpsSurface) return;
    fpsSurface = new UiSurface(this, kFpsLeft, kFpsTop, kFpsRight, kFpsBottom, SurfaceBlend::Keyed, true);
    fpsText.clear();
    fpsSurface->PaintWith([this](UiSurface& out) { PaintFps(out); });
}

void Interface3D::OnQuestVariable(std::string_view name, float value) {
    if (!stack.empty()) stack.back()->QuestVariable(name, value);
    std::vector<std::string> parts;
    if (value != 1.0f) return;
    parts = text::SplitWords(name, '.');
    if (parts.size() > kQuestPart) savedText = parts[kQuestPart];
}

// --- News for the top window -------------------------------------

void Interface3D::OnItemsPicked(const std::vector<Item*>& items, int money, bool crippled) {
    if (!stack.empty()) stack.back()->ItemsPicked(items, money, crippled);
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
    if (!stack.empty()) stack.back()->Speech(id, kind, kNoExtra);
}

void Interface3D::OnPlayersChanged(int added) {
    if (!stack.empty()) stack.back()->PlayersChanged(added);
}

void Interface3D::OnChatLine(const char* text) {
    root->AddLine(text);
}

void Interface3D::OnExchangeDone(Party* partyB, Party* partyA, Party* partyC) {
    if (!stack.empty()) stack.back()->ExchangeDone(partyB, partyA, partyC);
}

// The bottom window, not the top one (a save is made from the battle screen under the save window).
void Interface3D::SaveWindowState() {
    if (!stack.empty()) stack[0]->SaveState();
}

void Interface3D::ShowStatusText(std::string_view text) {
    statusSurface = new UiSurface(this, 0, kStatusTop, kLayoutWidth, kStatusBottom, SurfaceBlend::Keyed, true);
    statusSurface->PaintWith([text](UiSurface& out) {
        Rect rect = {0, 0, kLayoutWidth, kStatusTextHeight};
        out.DrawTextIn(text, rect, TextStyle{kStatusFont, kStatusColour, render::text::kCenter});    // Rebirth-patched
    });
    app::FrameStep(true, true);
    if (statusSurface) {
        delete statusSurface;
    }
    statusSurface = nullptr;
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

// A new surface for the screen's picture, as the interface's backdrop: the whole window in pixels, unscaled
// (Rebirth's WideScreen; the original's 800 x 600 layout rectangle scaled is the same on a 4:3 window).
void MakeBackdrop(Interface3D* ui) {
    ui->backdrop = new UiSurface(ui, 0, 0, ui->width, ui->height, SurfaceBlend::Opaque, false);
}

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
        const std::uint32_t now = platform::Milliseconds();
        alpha = (now - lastTime) * rate + alpha;
        lastTime = platform::Milliseconds();
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
    lastTime = platform::Milliseconds();
    active = true;
}

void Interface3D::Draw() {
    CloseAllPending();
    if ((backdropMode == 2 || backdropMode == 4 || backdropMode == 3) && backdrop) backdrop->Blit();
    if (!stack.empty()) stack.back()->Draw(&g_sceneRenderer);
    fade->Draw(&g_sceneRenderer);
}

// The frame rate, once measured: "FPS:" at the left, the rate at the right.
void Interface3D::PaintFps(UiSurface& surface) const {
    if (fpsText.empty()) return;
    Rect rect{0, 0, 60, 18};
    surface.DrawTextIn("FPS:", rect, TextStyle{kFpsFont, kWhite});
    surface.DrawTextIn(fpsText, rect, TextStyle{kFpsFont, kWhite, render::text::kRight});
}

void Interface3D::DrawOverlays() {
    if (!stack.empty()) {
        stack.back()->Redraw();        // its surfaces blit themselves
        stack.back()->RedrawTips();    // its tooltips follow the mouse and redraw
    }
    // (The original calls an empty function on the fade here; nothing to do.)
    if (statusSurface) statusSurface->Blit();

    if (backdropMode == 1) {
        // A window opens over the screen: the screen greyed behind it.
        if (!backdrop) MakeBackdrop(this);
        backdrop->CaptureScreen();
        if (!screenCopy) screenCopy = new UiSurface(this, 0, 0, width, height, SurfaceBlend::Opaque, false);
        screenCopy->CaptureScreen();
        backdrop->Greyscale();
        backdropMode = 2;
    } else if (backdropMode == 3) {
        // A window opens over a window: the screen as it is, the old backdrop put aside.
        secondBackdrop = backdrop;
        MakeBackdrop(this);
        backdrop->CaptureScreen();
        backdropMode = 4;
    }
    if (IsMultiplayerClient() && std::ssize(stack) == 1) root->Redraw();

    ++fpsFrames;
    const std::uint32_t now = platform::Milliseconds();
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
