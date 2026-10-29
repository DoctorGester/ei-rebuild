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
#include "lib/gameapi.h"
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include "options.h"
#include "script.h"
#include "texture.h"
#include "ui.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdio>
#include <cstdlib>


namespace app {
namespace {





LRESULT OnActivate(WPARAM wParam) {
    if ((wParam >> 16) != 0) {   // minimised
        g_activationChanged = 1;
        g_active = 0;
        return 1;
    }
    auto* sounds = g_clientWorld->sounds_;
    unsigned state = wParam & 0xFFFF;
    if (state == 0) {   // WA_INACTIVE
        g_active = 0;
        g_activationChanged = 1;
        g_cursor->ShowImage(0);
        if (CDirectInputMouse* mouse = g_mouse) mouse->SetActive(0);
        if (sounds) sounds->PauseAll();
        gameapi::ShowWindow(g_mainWindow, SW_MINIMIZE);
        return 1;
    }
    if (state > 2) return 1;
    g_active = 1;
    g_activationChanged = 1;
    gameapi::ShowWindow(g_mainWindow, SW_RESTORE);
    if (CDirectInputMouse* mouse = g_mouse) mouse->SetActive(1);
    CTextureDevice* device = g_sceneRenderer->device_;
    if (device && device->fullScreen_) {
        g_sceneRenderer->RestoreSurfaces();
        device = g_sceneRenderer->device_;
        if (device) {
            // ResetCursors after the device was restored. The context is the mouse.
            ResetCursors(options::g_options->fpsIndependentCursor_ != 0, g_mouse, 0);
        }
    }
    g_cursor->ShowImage(1);
    if (sounds) sounds->ResumeAll();
    g_interface->ResetInput();
    return 1;
}

LRESULT OnSysCommand(HWND window, WPARAM wParam, LPARAM lParam) {
    unsigned command = wParam & 0xFFF0;
    if (command == SC_HOTKEY || command == SC_NEXTWINDOW || command == SC_PREVWINDOW) {
        console::PrintLine("switch task");
    } else if (command == SC_SCREENSAVE || command == SC_MONITORPOWER) {
        return 0;
    }
    return gameapi::DefWindowProcA(window, WM_SYSCOMMAND, wParam, lParam);
}

// The "QueryCancelAutoPlay" message id, registered on first use (a function-local static).
std::uint8_t g_autoPlayGuard = /*0079BA04*/ {};
std::uint32_t g_autoPlayMessage = /*0079BA00*/ {};
UINT AutoPlayMessage() {
    if (!(g_autoPlayGuard & 1)) {
        g_autoPlayGuard |= 1;
        g_autoPlayMessage = gameapi::RegisterWindowMessageA("QueryCancelAutoPlay");
    }
    return g_autoPlayMessage;
}

LRESULT Handled() {
    g_interface->CloseAllPending();
    return 1;
}

int Low(LPARAM v) { return static_cast<int>(v & 0xFFFF); }
int HighSigned(LPARAM v) { return static_cast<int>(v) >> 16; }              // keyboard
int HighUnsigned(LPARAM v) { return static_cast<int>(static_cast<unsigned>(v) >> 16); }   // mouse

bool AltDown(LPARAM lParam) { return (lParam & 0x20000000) != 0; }

// The window is a Unicode one, so WM_CHAR carries a UTF-16 unit, and a character
// past U+FFFF comes as two (a surrogate pair). The typed character as a code point, or -1 while the first
// half of a pair waits for the second (a half that comes alone is dropped).
unsigned g_highSurrogate = 0;
int TypedCharacter(WPARAM unit) {
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

constexpr int kErrorBoxFlags = 0x10;   // MB_ICONHAND
// The game's own wheel message (a fixed number in the registered messages' range; nothing here sends it): wParam is
// the delta.
constexpr UINT kGameWheelMessage = 0xCC54;
int g_fatalErrorCount = /*007A9B9C*/ {};

const char* CodeReportFormat() { return "%s (0x%.8X) %s"; }

} // namespace

LRESULT __stdcall WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        RECT rect;
        if (!gameapi::GetUpdateRect(window, &rect, FALSE)) return 1;
        PAINTSTRUCT paint;
        gameapi::BeginPaint(window, &paint);
        gameapi::EndPaint(window, &paint);
        return 1;
    }
    case WM_DESTROY:
        if (g_quitAllowed != 0) gameapi::PostQuitMessage(0);
        return 1;
    case WM_ACTIVATE:
        return OnActivate(wParam);
    case WM_SETCURSOR:
        gameapi::SetCursor(nullptr);
        return 1;
    case WM_SYSCOMMAND:
        return OnSysCommand(window, wParam, lParam);
    }
    UINT autoPlay = AutoPlayMessage();
    if (message != 0 && message == autoPlay) return 1;
    return gameapi::DefWindowProcA(window, message, wParam, lParam);
}

LRESULT DispatchInput(UINT message, WPARAM wParam, LPARAM lParam) {
    // The switch-over with the frame scaled into the window: the mouse's coordinates as the frame's
    // (present_window.h; unchanged otherwise). The game's own wheel message (kGameWheelMessage) is not mapped.
    lParam = present_window::MouseToGame(message, lParam);
    CInterface3D* input = &g_interface.Get();
    input->CloseAllPending();
    int w = static_cast<int>(wParam);
    switch (message) {
    case WM_KEYDOWN:
        input->OnKeyDown(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_KEYUP:
        input->OnKeyUp(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_CHAR:
        w = TypedCharacter(wParam);
        if (w < 0) return Handled();
        input->OnChar(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4 && AltDown(lParam)) {
            if (g_quitAllowed != 0) gameapi::PostQuitMessage(0);
            return Handled();
        }
        input->OnKeyDown(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_SYSKEYUP:
        console::EndLine(console::g_consoleOut << static_cast<unsigned>(w) << ':' << static_cast<long>(lParam));
        if (wParam == VK_TAB && AltDown(lParam)) {   // Alt+Tab: as if the window was deactivated
            HWND window = g_mainWindow;
            BOOL iconic = gameapi::IsIconic(window);
            WindowProc(g_mainWindow, WM_ACTIVATE, static_cast<WPARAM>(iconic) << 16, 0);
            return Handled();
        }
        input->OnKeyUp(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_MOUSEMOVE:
        input->OnMouseMove(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_LBUTTONDOWN:
        input->OnLeftDown(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_LBUTTONUP:
        input->OnLeftUp(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_LBUTTONDBLCLK:
        input->OnLeftDouble(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_RBUTTONDOWN:
        input->OnRightDown(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_RBUTTONUP:
        input->OnRightUp(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_RBUTTONDBLCLK:
        input->OnRightDouble(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_MOUSEWHEEL:
        input->OnWheel(Low(static_cast<LPARAM>(wParam)), HighUnsigned(static_cast<LPARAM>(wParam)), Low(lParam), HighUnsigned(lParam));
        return Handled();
    case kGameWheelMessage:
        input->OnWheel(0, w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    }
    return WindowProc(g_mainWindow, message, wParam, lParam);
}

void PumpMessages() {
    MSG msg;
    while (gameapi::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        WindowProc(g_mainWindow, msg.message, msg.wParam, msg.lParam);
}

int StartFirstMap() {
    const auto& cmdMap = g_commandLineMap;    // map on the command line
    const auto& cmdMode = g_commandLineMob;   // empty: main menu
    const auto& cmdScript = g_commandLineScript;
    std::string map;
    map = cmdMap.size() != 0 ? cmdMap : options::g_options->defaultMapName_;
    if (str::Compare(cmdMode,
                        game::kEmptyText) == 0) {
        g_gameFlow->CloseSessions();
        MenuOpen(&g_gameFlow.Get());
        return 1;
    }
    CWorldServer* world = &g_serverWorld.Get();
    if (!world->NewZone(map, cmdMode, std::nullopt))
        return game::FatalError("Can't load map \"%s\".\n", map);
    world->SetGameResult(1);
    CWorldClient* client = &g_clientWorld.Get();
    client->SetPlayer(g_serverWorld->worldHandle_.Get());
    client->LocalView()->sendParty_ = 0;
    g_serverWorld->sessionKind_ = 7;
    if (cmdScript.size() == 0) return 1;
    CScript* script = TheMissionScript();
    BOOL loaded;
    {
        std::string root = *g_dataFolder;
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

int ShowMessageBox(const char* text, int flags) {
    HCURSOR cursor = gameapi::GetCursor();
    // The image's MessageBoxA takes UTF-8.
    int answer = gameapi::MessageBoxA(g_mainWindow, text, g_windowTitle, static_cast<UINT>(flags));
    gameapi::SetCursor(cursor);
    return answer;
}

int ShowLibraryMessageBox(const char* text) {
    char title[MAX_PATH];
    gameapi::GetModuleFileNameA(nullptr, title, MAX_PATH);
    return gameapi::MessageBoxA(nullptr, text, title, MB_OK | MB_ICONEXCLAMATION);
}

} // namespace app

// Formats the error (wvsprintfA, 1 KB), shows it in a message box and exits the
// game (exit(-1); the error counter reaches 1). Rebirth's DisableExitOnCrashReport replaces the message box
// with a line in addon.log (not reproduced) and removes the exit, so the game carries on. Returns 0.
int game::FatalError(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    gameapi::wvsprintfA(text, format, args);
    va_end(args);
    bool carryOn = ei::rebirth::Enabled(ei::rebirth::Patch::DisableExitOnCrashReport);
    if (!carryOn) app::ShowMessageBox(text, app::kErrorBoxFlags);
    if (++app::g_fatalErrorCount >= 1 && !carryOn) std::exit(-1);
    return 0;
}

namespace app {

// ---- FatalErrorCode -----------------------------------------------------------------

int FatalErrorCodeV(long result, const char* format, va_list args) {
    char text[0x400];
    char report[0x400];
    gameapi::wvsprintfA(text, format, args);
    std::snprintf(report, sizeof(report), CodeReportFormat(), text, result, DirectXErrorText(result));
    ShowMessageBox(report, kErrorBoxFlags);
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
