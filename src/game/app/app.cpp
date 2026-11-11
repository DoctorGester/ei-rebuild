// Application helpers around WinMain: the window's events, the input dispatcher, the event pump, the start of the
// first map, the cursor's confinement, resolution changes and the start folders. See app.h.
#include "game/app/app.h"

#include "game/app/game_flow.h"
#include "game/app/options.h"
#include "game/app/present_window.h"
#include "game/app/singletons.h"
#include "game/app/tool_hooks.h"
#include "game/core/asset_text.h"
#include "game/scene/device.h"
#include "game/scene/scene_renderer.h"
#include "game/players/player_client.h"
#include "game/ui/screen_layout.h"
#include "game/script/script.h"
#include "game/sound/sound.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace app {
namespace {

using platform::EventType;
using platform::MouseButton;

void OnActivate(bool active, bool minimized) {
    if (minimized) {
        g_active = 0;
        UpdateCursorConfinement();
        return;
    }
    auto* sounds = g_clientWorld.sounds.get();
    if (!active) {
        g_active = 0;
        g_cursor.ShowImage(0);
        if (sounds) sounds->PauseAll();
        UpdateCursorConfinement();
        return;
    }
    g_active = 1;
    TextureDevice* device = g_sceneRenderer.device;
    if (device && device->fullScreen) {
        g_sceneRenderer.RestoreSurfaces();
        if (g_sceneRenderer.device) ResetCursors(0);   // after the device was restored
    }
    g_cursor.ShowImage(1);
    if (sounds) sounds->ResumeAll();
    g_interface.ResetInput();
    UpdateCursorConfinement();
}

void Handled() { g_interface.CloseAllPending(); }

// The events the interface takes: keys, text, the mouse's moves, its left and right buttons and a wheel's turn.
bool IsInput(const platform::Event& event) {
    switch (event.type) {
    case EventType::KeyDown:
    case EventType::KeyUp:
    case EventType::Text:
    case EventType::MouseMove:
    case EventType::Wheel: return true;
    case EventType::MouseDown:
    case EventType::MouseUp: return event.button == MouseButton::Left || event.button == MouseButton::Right;
    default: return false;
    }
}

void MenuOpen(void* flow) { static_cast<GameFlow*>(flow)->ProcessRequest(); }

// Alt+F4 and Alt+Tab never get here: the backend makes Alt+F4 the close request, the system's switcher takes Alt+Tab
// and the backend reports the focus it took.
void OnKey(const platform::Event& event) {
    if (event.type == EventType::KeyDown) g_interface.OnKeyDown(event.key, event.scan, event.repeat);
    else g_interface.OnKeyUp(event.key, event.scan);
}

// Each character of the text to the interface, with the scan code of the key that typed it (the interface drops the
// console key's).
void OnText(const platform::Event& event) {
    const std::string& text = event.text;
    for (std::size_t at = 0; at < text.size();) {
        unsigned character = 0;
        at += static_cast<std::size_t>(text::DecodeUtf8(text.data() + at, text.size() - at, &character));
        if (character == 0xFFFFFFFFu) continue;
        g_interface.OnChar(static_cast<int>(character), event.scan);
        Handled();
    }
}

void OnMouse(const platform::Event& event, Point at) {
    // Every second press of a quick series is a double click.
    const bool twice = event.clicks % 2 == 0;
    const bool left = event.button == MouseButton::Left;
    switch (event.type) {
    case EventType::MouseMove: g_interface.OnMouse({.kind = UiEventKind::MouseMove, .at = at}); break;
    case EventType::MouseDown:
        if (left) g_interface.OnMouse({.kind = twice ? UiEventKind::LeftDouble : UiEventKind::LeftDown, .at = at});
        else g_interface.OnMouse({.kind = twice ? UiEventKind::RightDouble : UiEventKind::RightDown, .at = at});
        break;
    case EventType::MouseUp:
        g_interface.OnMouse({.kind = left ? UiEventKind::LeftUp : UiEventKind::RightUp, .at = at});
        break;
    case EventType::Wheel: g_interface.OnMouse({.kind = UiEventKind::Wheel, .at = at, .delta = event.wheel}); break;
    default: break;
    }
}

constexpr int kLargestSide = 16384;   // the renderer's frame target's (render::GameTargetSize)

bool g_confined = false;   // what the window was last asked

int g_requestedWidth = 0, g_requestedHeight = 0;   // RequestResolution's, taken by the next pass (0: none)

} // namespace

void HandleWindowEvent(const platform::Event& event) {
    switch (event.type) {
    case EventType::CloseRequested:
        platform::PostQuit(0);
        break;
    case EventType::Focus: OnActivate(event.active, event.minimized); break;
    default: break;
    }
}

void DispatchInput(const platform::Event& received) {
    if (!IsInput(received)) {
        HandleWindowEvent(received);
        return;
    }
    // The mouse's coordinates as the frame's (present_window.h).
    platform::Event event = received;
    const bool onFrame = present_window::MouseToGame(&event);
    g_interface.CloseAllPending();
    // A press outside the picture (the bars of a window of another shape, the letterbox bars) is not passed on; the
    // release still is, and moves arrive on the picture's nearest pixel.
    const Point at{event.x, event.y};
    if (event.type == EventType::MouseDown) {
        const Rect picture = g_interface.PictureRect();
        if (!onFrame || at.x < picture.left || at.x >= picture.right || at.y < picture.top || at.y >= picture.bottom) {
            ei::Log("input: a press outside the picture ignored (at %d, %d of the window)", received.x, received.y);
            Handled();
            return;
        }
    }
    switch (event.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: OnKey(event); break;
    case EventType::Text: OnText(event); return;
    default: OnMouse(event, at); break;
    }
    Handled();
}

void PumpMessages() {
    while (const std::optional<platform::Event> event = platform::TakeEvent()) {
        if (event->type == EventType::Quit) {   // left for the main loop, which ends on it
            platform::PostQuit(event->quitCode);
            return;
        }
        HandleWindowEvent(*event);
    }
}

void StartFirstMap() {
    if (g_commandLineMob.empty()) {
        g_gameFlow.CloseSessions();
        if (g_toolScene.newGame) g_gameFlow.RequestState3();
        MenuOpen(&g_gameFlow);
        return;
    }
    const std::string map = g_commandLineMap.empty() ? std::string(options::kDefaultMapName) : g_commandLineMap;
    WorldServer* world = &g_serverWorld;
    if (!world->NewZone(map, g_commandLineMob, std::nullopt)) game::FatalExit("Can't load map \"%s\".\n", map);
    world->SetGameState(GameState::Playing);
    WorldClient* client = &g_clientWorld;
    client->SetPlayer(g_serverWorld.worldHandle.Get());
    client->LocalView()->sendParty = 0;
    g_serverWorld.zoneMode = ZoneMode::Script;
    if (g_commandLineScript.empty()) return;
    Script* script = TheMissionScript();
    if (script->LoadFile(g_dataFolder + "Scripts\\" + g_commandLineScript)) return;
    ScriptErrorInfo error;
    script->GetError(&error);
    game::FatalExit("%s, line %i", error.text, error.line);
}

// --- The cursor confined to the window -----------------------------------------------------------

// app.h's UpdateCursorConfinement. docs/notes/platform.md, "The cursor".
void UpdateCursorConfinement() {
    const bool active = g_active != 0;
    const bool confined = active && g_interface.EdgePanLive();
    // Focus lost while confined: the cursor goes where it likes, and an edge pan the last motion started must not go
    // on for it.
    if (!active && g_confined) g_interface.ResetInput();
    if (confined == g_confined) return;
    g_confined = confined;
    platform::ConfineCursor(g_mainWindow, confined);
    ei::Log("app: cursor %s the window", confined ? "confined to" : "released from");
}

// --- Resolution changes --------------------------------------------------------------------------

// The rendering resolution changed while the game runs (app.h's SetResolution): a window resized by the player or
// the system, or a resolution chosen in the options. docs/notes/renderer.md, "Resolution changes".
bool SetResolution(int width, int height) {
    TextureDevice* device = g_sceneRenderer.device;
    if (!device || width < 1 || height < 1 || width > kLargestSide || height > kLargestSide) return false;
    const Rect& screen = device->screen;
    if (screen.right == width && screen.bottom == height) return false;
    ei::Log("Resolution: %dx%d (was %dx%d)", width, height, screen.right, screen.bottom);
    const screen_layout::Layout before = screen_layout::Get();
    screen_layout::Configure(width, height);
    g_sceneRenderer.ResizeScreen(width, height);
    g_cursor.bounds = device->screen;
    options::g_options.resolutionX = width;
    options::g_options.resolutionY = height;
    if (g_interface.renderer) g_interface.Resize(before);
    return true;
}

void RequestResolution(int width, int height) {
    g_requestedWidth = width;
    g_requestedHeight = height;
}

void ChooseResolution(int width, int height) {
    RequestResolution(width, height);
    present_window::SizeClient(g_sceneRenderer.device, width, height);
}

void ChooseFullScreen(bool fullScreen) {
    options::g_options.fullScreen = fullScreen ? 1 : 0;
    present_window::SetFullScreen(g_sceneRenderer.device, fullScreen);
}

void TakeResolutionChange() {
    if (!g_requestedWidth) return;
    SetResolution(g_requestedWidth, g_requestedHeight);
    g_requestedWidth = g_requestedHeight = 0;
}

// ---- Fatal errors and warnings --------------------------------------------------------

void ShowMessageBox(std::string_view text) {
    platform::CursorShape cursor = platform::CurrentCursor();
    platform::ShowMessageBox(g_mainWindow, text, std::string_view(g_windowTitle), platform::MessageButtons::Ok, platform::MessageIcon::Error);
    platform::SetCursor(cursor);
}

void ShowLibraryMessageBox(std::string_view text) {
    platform::ShowMessageBox(nullptr, text, platform::ProgramPath(), platform::MessageButtons::Ok,
                             platform::MessageIcon::Warning);
}

} // namespace app

void game::FatalExit(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("fatal: %s", text);
    app::ShowMessageBox(text);
    std::exit(-1);
}

void game::Warning(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("warning: %s", text);
}

// --- The start folders ---------------------------------------------------------------------------

std::string g_startFolder;
std::string g_textFolder;
std::string g_dataFolder;
