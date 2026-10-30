// Application helpers around WinMain: window procedure, input dispatcher,
// message pump and the start of the first map. See app.h.
#include "app.h"
#include "console.h"
#include "player_client.h"
#include "world_glue.h"
#include "sound.h"
#include "ui_cursor.h"
#include "dx_errors.h"
#include "game_strings.h"
#include "present_window.h"
#include "asset_text.h"

#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "platform/platform.h"
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

std::intptr_t OnActivate(platform::Activation activation, bool minimized) {
    if (minimized) {
        g_activationChanged = 1;
        g_active = 0;
        return 1;
    }
    auto* sounds = g_clientWorld.sounds;
    if (activation == platform::Activation::Inactive) {
        g_active = 0;
        g_activationChanged = 1;
        g_cursor.ShowImage(0);
        if (DirectInputMouse* mouse = g_mouse) mouse->SetActive(false);
        if (sounds) sounds->PauseAll();
        platform::Minimize(g_mainWindow);
        return 1;
    }
    if (activation == platform::Activation::Unknown) return 1;
    g_active = 1;
    g_activationChanged = 1;
    platform::Restore(g_mainWindow);
    if (DirectInputMouse* mouse = g_mouse) mouse->SetActive(true);
    TextureDevice* device = g_sceneRenderer.device;
    if (device && device->fullScreen) {
        g_sceneRenderer.RestoreSurfaces();
        device = g_sceneRenderer.device;
        if (device) {
            // ResetCursors after the device was restored. The context is the mouse.
            ResetCursors(options::g_options.fpsIndependentCursor != 0, g_mouse, 0);
        }
    }
    g_cursor.ShowImage(1);
    if (sounds) sounds->ResumeAll();
    g_interface.ResetInput();
    return 1;
}

std::intptr_t OnSystemCommand(const platform::Event& event) {
    if (event.command == platform::SystemCommand::TaskSwitch) {
        console::PrintLine("switch task");
    } else if (event.command == platform::SystemCommand::ScreenSaver) {
        return 0;
    }
    return platform::DefaultHandling(event);
}

std::intptr_t Handled() {
    g_interface.CloseAllPending();
    return 1;
}

// The interface takes an event's position and wheel turn as the 16-bit halves of Windows' message data, unsigned (a
// turn towards the player as 65536 less its size).
int Unsigned16(int value) { return value & 0xFFFF; }

// The window is a Unicode one, so a typed character comes as a UTF-16 unit, and a character
// past U+FFFF comes as two (a surrogate pair). The typed character as a code point, or -1 while the first
// half of a pair waits for the second (a half that comes alone is dropped).
unsigned g_highSurrogate = 0;
int TypedCharacter(int unit) {
    const unsigned u = static_cast<unsigned>(unit & 0xFFFF);
    const unsigned high = g_highSurrogate;
    g_highSurrogate = 0;
    if (u >= 0xD800 && u <= 0xDBFF) {
        g_highSurrogate = u;
        return -1;
    }
    if (u >= 0xDC00 && u <= 0xDFFF)
        return high ? static_cast<int>(0x10000 + ((high - 0xD800) << 10) + (u - 0xDC00)) : -1;
    return static_cast<int>(u);
}


void MenuOpen(void* flow) { static_cast<GameFlow*>(flow)->ProcessRequest(); }

int g_fatalErrorCount = /*007A9B9C*/ {};

const char* CodeReportFormat() { return "%s (0x%.8X) %s"; }

} // namespace

std::intptr_t HandleWindowEvent(const platform::Event& event) {
    switch (event.type) {
    case EventType::Paint:
        platform::MarkPainted(event.native.window);
        return 1;
    case EventType::Destroy:
        if (g_quitAllowed != 0) platform::PostQuit(0);
        return 1;
    case EventType::Activate:
        return OnActivate(event.activation, event.minimized);
    case EventType::SetCursor:
        platform::SetCursor(nullptr);
        return 1;
    case EventType::SystemCommand:
        return OnSystemCommand(event);
    case EventType::AutoPlay:
        return 1;
    default:
        return platform::DefaultHandling(event);
    }
}

std::intptr_t DispatchInput(const platform::Event& received) {
    // The switch-over with the frame scaled into the window: the mouse's coordinates as the frame's
    // (present_window.h; unchanged otherwise).
    platform::Event event = received;
    present_window::MouseToGame(&event);
    Interface3D* input = &g_interface;
    input->CloseAllPending();
    const int x = Unsigned16(event.x);
    const int y = Unsigned16(event.y);
    switch (event.type) {
    case EventType::KeyDown:
        input->OnKeyDown(event.key, event.repeat, event.keyFlags);
        return Handled();
    case EventType::KeyUp:
        input->OnKeyUp(event.key, event.repeat, event.keyFlags);
        return Handled();
    case EventType::Character: {
        const int character = TypedCharacter(event.key);
        if (character < 0) return Handled();
        input->OnChar(character, event.repeat, event.keyFlags);
        return Handled();
    }
    case EventType::SystemKeyDown:
        if (event.key == platform::kKeyF4 && (event.keyFlags & platform::kKeyAltDown)) {
            if (g_quitAllowed != 0) platform::PostQuit(0);
            return Handled();
        }
        input->OnKeyDown(event.key, event.repeat, event.keyFlags);
        return Handled();
    case EventType::SystemKeyUp:
        // The key and its message data (the flags over the repeat count), as the original printed them.
        console::EndLine(console::g_consoleOut << static_cast<unsigned>(event.key) << ':'
                                               << static_cast<long>((static_cast<unsigned>(event.keyFlags) << 16) |
                                                                    static_cast<unsigned>(event.repeat)));
        if (event.key == platform::kKeyTab && (event.keyFlags & platform::kKeyAltDown)) {   // Alt+Tab: as if the window was deactivated
            OnActivate(platform::Activation::Inactive, platform::IsMinimized(g_mainWindow));
            return Handled();
        }
        input->OnKeyUp(event.key, event.repeat, event.keyFlags);
        return Handled();
    case EventType::MouseMove:
        input->OnMouseMove(event.buttons, x, y);
        return Handled();
    case EventType::LeftDown:
        input->OnLeftDown(event.buttons, x, y);
        return Handled();
    case EventType::LeftUp:
        input->OnLeftUp(event.buttons, x, y);
        return Handled();
    case EventType::LeftDouble:
        input->OnLeftDouble(event.buttons, x, y);
        return Handled();
    case EventType::RightDown:
        input->OnRightDown(event.buttons, x, y);
        return Handled();
    case EventType::RightUp:
        input->OnRightUp(event.buttons, x, y);
        return Handled();
    case EventType::RightDouble:
        input->OnRightDouble(event.buttons, x, y);
        return Handled();
    case EventType::Wheel:
        input->OnWheel(event.buttons, Unsigned16(event.wheelDelta), x, y);
        return Handled();
    default:
        return HandleWindowEvent(event);
    }
}

void PumpMessages() {
    platform::Event event;
    while (platform::TakeEvent(&event)) HandleWindowEvent(event);
}

int StartFirstMap() {
    const auto& cmdMap = g_commandLineMap;    // map on the command line
    const auto& cmdMode = g_commandLineMob;   // empty: main menu
    const auto& cmdScript = g_commandLineScript;
    std::string map;
    map = cmdMap.size() != 0 ? cmdMap : options::g_options.defaultMapName;
    if (str::Compare(cmdMode,
                        game::kEmptyText) == 0) {
        g_gameFlow.CloseSessions();
        MenuOpen(&g_gameFlow);
        return 1;
    }
    WorldServer* world = &g_serverWorld;
    if (!world->NewZone(map, cmdMode, std::nullopt))
        return game::FatalError("Can't load map \"%s\".\n", map);
    world->SetGameResult(1);
    WorldClient* client = &g_clientWorld;
    client->SetPlayer(g_serverWorld.worldHandle.Get());
    client->LocalView()->sendParty = 0;
    g_serverWorld.sessionKind = 7;
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
    platform::ShowMessageBox(nullptr, text, platform::ExePath().c_str(), platform::MessageButtons::Ok,
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
    char text[0x400];
    char report[0x400];
    std::vsnprintf(text, sizeof(text), format, args);
    std::snprintf(report, sizeof(report), CodeReportFormat(), text, result, DirectXErrorText(result));
    ShowMessageBox(report);
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
