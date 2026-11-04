// Application helpers around WinMain: the window's events, the input dispatcher, the event pump and the start of the
// first map. See app.h.
#include "app.h"
#include "console.h"
#include "player_client.h"
#include "sound.h"
#include "ui_cursor.h"
#include "dx_errors.h"
#include "game_strings.h"
#include "present_window.h"
#include "asset_text.h"
#include "platform/log.h"

#include "lib/rebirth.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include "options.h"
#include "script.h"
#include "texture.h"
#include "ui.h"
#include "world.h"
#include "game_singletons.h"

#include <cstdio>
#include <cstdlib>


namespace app {
namespace {

using platform::EventType;
using platform::MouseButton;

void OnActivate(bool active, bool minimized) {
    if (minimized) {
        g_activationChanged = 1;
        g_active = 0;
        UpdateCursorConfinement();
        return;
    }
    auto* sounds = g_clientWorld.sounds;
    if (!active) {
        g_active = 0;
        g_activationChanged = 1;
        g_cursor.ShowImage(0);
        if (sounds) sounds->PauseAll();
        UpdateCursorConfinement();
        return;
    }
    g_active = 1;
    g_activationChanged = 1;
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

// The keys Windows called system keys (Alt held without Ctrl, or F10): the original printed their releases to the
// console and took Alt+F4 and Alt+Tab from them.
bool SystemKey(const platform::Event& event) {
    const bool alt = (event.modifiers & platform::kModifierAlt) != 0;
    return (alt && !(event.modifiers & platform::kModifierControl)) || event.key == platform::kKeyF10;
}

// The wheel's turn as the interface takes it: 120 a notch, positive away from the player.
int WheelTurn(const platform::Event& event) { return static_cast<int>(std::lround(event.wheel * 120.0f)); }

// A released system key's message data as the original printed it: the repeat count 1, the scan code at bit 16 (the
// extended bit 24), Alt held bit 29, the key down before and released bits 30 and 31.
std::int32_t ReleaseData(const platform::Event& event) {
    constexpr std::uint32_t kRepeatCount = 1, kAlt = 0x20000000u, kReleased = 0xC0000000u;
    const std::uint32_t alt = (event.modifiers & platform::kModifierAlt) ? kAlt : 0;
    return static_cast<std::int32_t>(kReleased | alt | (static_cast<std::uint32_t>(event.scan) << 16) | kRepeatCount);
}

// The events the interface takes: keys, text, the mouse's moves, its left and right buttons and a wheel's turn.
bool IsInput(const platform::Event& event) {
    switch (event.type) {
    case EventType::KeyDown:
    case EventType::KeyUp:
    case EventType::Text:
    case EventType::MouseMove: return true;
    case EventType::MouseDown:
    case EventType::MouseUp: return event.button == MouseButton::Left || event.button == MouseButton::Right;
    case EventType::Wheel: return WheelTurn(event) != 0;
    default: return false;
    }
}

void MenuOpen(void* flow) { static_cast<GameFlow*>(flow)->ProcessRequest(); }

int g_fatalErrorCount = /*007A9B9C*/ {};

const char* CodeReportFormat() { return "%s (0x%.8X) %s"; }

void OnKey(Interface3D* input, const platform::Event& event) {
    if (event.type == EventType::KeyDown) {
        if (event.key == platform::kKeyF4 && SystemKey(event)) {   // Alt+F4
            if (g_quitAllowed != 0) platform::PostQuit(0);
            return;
        }
        input->OnKeyDown(event.key, event.scan, event.repeat);
        return;
    }
    if (SystemKey(event)) {
        // The key and its message data, signed (a released key's are negative), wherever long is wider: F10's
        // "121:-1069285375".
        console::EndLine(console::g_consoleOut << static_cast<unsigned>(event.key) << ':'
                                               << static_cast<long>(ReleaseData(event)));
        if (event.key == platform::kKeyTab && (event.modifiers & platform::kModifierAlt)) {   // Alt+Tab: as if the window was deactivated
            OnActivate(false, platform::IsMinimized(g_mainWindow));
            return;
        }
    }
    input->OnKeyUp(event.key, event.scan);
}

// Each character of the text to the interface, with the scan code of the key that typed it (the interface drops the
// console key's).
void OnText(Interface3D* input, const platform::Event& event) {
    const std::string& text = event.text;
    for (std::size_t at = 0; at < text.size();) {
        unsigned character = 0;
        at += static_cast<std::size_t>(text::DecodeUtf8(text.data() + at, text.size() - at, &character));
        if (character == 0xFFFFFFFFu) continue;
        input->OnChar(static_cast<int>(character), event.scan);
        Handled();
    }
}

void OnMouse(Interface3D* input, const platform::Event& event, Point at) {
    // Every second press of a quick series is a double click.
    const bool twice = event.clicks % 2 == 0;
    const bool left = event.button == MouseButton::Left;
    switch (event.type) {
    case EventType::MouseMove: input->OnMouseMove(at); break;
    case EventType::MouseDown:
        if (left && twice) input->OnLeftDouble(at);
        else if (left) input->OnLeftDown(at);
        else if (twice) input->OnRightDouble(at);
        else input->OnRightDown(at);
        break;
    case EventType::MouseUp:
        if (left) input->OnLeftUp(at);
        else input->OnRightUp(at);
        break;
    case EventType::Wheel: input->OnWheel(WheelTurn(event), at); break;
    default: break;
    }
}
} // namespace

void HandleWindowEvent(const platform::Event& event) {
    switch (event.type) {
    case EventType::CloseRequested:
        if (g_quitAllowed != 0) platform::PostQuit(0);
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
    Interface3D* input = &g_interface;
    input->CloseAllPending();
    // A press outside the picture (the bars of a window of another shape, the letterbox bars) is not passed on; the
    // release still is, and moves arrive on the picture's nearest pixel.
    const Point at{event.x, event.y};
    if (event.type == EventType::MouseDown) {
        const Rect picture = input->PictureRect();
        if (!onFrame || at.x < picture.left || at.x >= picture.right || at.y < picture.top || at.y >= picture.bottom) {
            ei::Log("input: a press outside the picture ignored (at %d, %d of the window)", received.x, received.y);
            Handled();
            return;
        }
    }
    switch (event.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: OnKey(input, event); break;
    case EventType::Text: OnText(input, event); return;
    default: OnMouse(input, event, at); break;
    }
    Handled();
}

void PumpMessages() {
    platform::Event event;
    while (platform::TakeEvent(&event)) {
        if (event.type == EventType::Quit) {   // left for the main loop, which ends on it
            platform::PostQuit(event.quitCode);
            return;
        }
        HandleWindowEvent(event);
    }
}

int StartFirstMap() {
    const auto& cmdMap = g_commandLineMap;    // map on the command line
    const auto& cmdMode = g_commandLineMob;   // empty: main menu
    const auto& cmdScript = g_commandLineScript;
    std::string map;
    map = cmdMap.size() != 0 ? cmdMap : options::kDefaultMapName;
    if (str::Compare(cmdMode,
                        game::kEmptyText) == 0) {
        g_gameFlow.CloseSessions();
        MenuOpen(&g_gameFlow);
        return 1;
    }
    WorldServer* world = &g_serverWorld;
    if (!world->NewZone(map, cmdMode, std::nullopt))
        return game::FatalError("Can't load map \"%s\".\n", map);
    world->SetGameState(GameState::Playing);
    WorldClient* client = &g_clientWorld;
    client->SetPlayer(g_serverWorld.worldHandle.Get());
    client->LocalView()->sendParty = 0;
    g_serverWorld.zoneMode = ZoneMode::Script;
    if (cmdScript.size() == 0) return 1;
    Script* script = TheMissionScript();
    bool loaded;
    {
        std::string root = g_dataFolder;
        std::string dir = root + "Scripts\\";
        std::string path = dir + cmdScript;
        loaded = script->LoadFile(path);
    }
    if (loaded) return 1;
    ScriptErrorInfo error;
    script->GetError(&error);
    return game::FatalError("%s, line %i", error.text, error.line);
}

// ---- FatalError ---------------------------------------------------------------------

void ShowMessageBox(const char* text) {
    platform::CursorShape cursor = platform::CurrentCursor();
    platform::ShowMessageBox(g_mainWindow, text, g_windowTitle, platform::MessageButtons::Ok, platform::MessageIcon::Error);
    platform::SetCursor(cursor);
}

void ShowLibraryMessageBox(const char* text) {
    platform::ShowMessageBox(nullptr, text, platform::ProgramPath().c_str(), platform::MessageButtons::Ok,
                             platform::MessageIcon::Warning);
}

} // namespace app

// Formats the error (1 KB), shows it in a message box and exits the
// game (exit(-1); the error counter reaches 1). Rebirth's DisableExitOnCrashReport replaces the message box
// with a line in addon.log (not reproduced) and removes the exit, so the game carries on. Returns 0.
int game::FatalError(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    bool carryOn = ei::rebirth::Enabled(ei::rebirth::Patch::DisableExitOnCrashReport);
    if (!carryOn) app::ShowMessageBox(text);
    if (++app::g_fatalErrorCount >= 1 && !carryOn) std::exit(-1);
    return 0;
}

namespace app {

// ---- FatalErrorCode -----------------------------------------------------------------

int FatalErrorCodeV(long result, const char* format, va_list args) {
    const std::string text = str::FormatV(format, args);
    const std::string report = str::Format(CodeReportFormat(), text.c_str(), result, DirectXErrorText(result));
    ShowMessageBox(report.c_str());
    if (++g_fatalErrorCount >= 1) std::exit(-1);
    return 0;
}

int FatalErrorCode(long result, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int value = FatalErrorCodeV(result, format, args);
    va_end(args);
    return value;
}

} // namespace app
