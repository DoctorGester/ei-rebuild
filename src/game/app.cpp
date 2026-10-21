// Application helpers around WinMain: window procedure (00457D40), input dispatcher (00457970),
// message pump (00458220) and the start of the first map (00456140). See app.h.
//
// Callees patched by Rebirth (0057C700, 00595480, FatalError 00480080) are reached through their
// original addresses.
#include "device.h"   // the old device's calls
#include "app.h"
#include "world_glue.h"
#include "sound.h"
#include "ui_cursor.h"
#include "dx_errors.h"
#include "game_strings.h"
#include "present_window.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_script.h"
#include "options.h"
#include "originals_world.h"
#include "originals_ui.h"
#include "script.h"
#include "texture.h"            // the renderer's device object
#include "ui.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <ddraw.h>

#include <cstdarg>
#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace app {
namespace {


HWND MainWindow() { return ei::GameData<HWND>(0x0079BC28); }
bool QuitOnClose() { return ei::GameData<std::uint8_t>(0x00784751) != 0; }   // Alt+F4 / WM_DESTROY quit

// The interface (007C7D50) takes the input: pending windows closed before and after each message
// (005F5AB0), then the message with (wParam, x, y) (005F76A0-005F7BE0; ui_interface_input.cpp).

// The console's ostream (007C55A8).
libcrt::Ostream* Console() { return ei::GamePointer<libcrt::Ostream>(0x007C55A8); }

// Objects told about activation changes: the cursor (007C7BD8), the display device
// (RenderDeviceObject), the scene renderer, the mouse (pointer at 0079BA08) and the sound manager
// (pointer at 007AF784).
CDirectInputMouse* TheMouse() { return Mouse(); }

std::uint8_t& ActiveFlag() { return ei::GameData<std::uint8_t>(0x0079BC5C); }
std::uint8_t& ActivationSeen() { return ei::GameData<std::uint8_t>(0x0079BC5D); }

LRESULT OnActivate(WPARAM wParam) {
    if ((wParam >> 16) != 0) {   // minimised
        ActivationSeen() = 1;
        ActiveFlag() = 0;
        return 1;
    }
    auto* sounds = TheSoundManager();
    unsigned state = wParam & 0xFFFF;
    if (state == 0) {   // WA_INACTIVE
        ActiveFlag() = 0;
        ActivationSeen() = 1;
        TheCursor()->ShowImage(0);
        CTextureDevice* device = TheRenderDevice();
        if (device && (device->flags_ & 1)) device->RestoreDisplayMode();
        if (CDirectInputMouse* mouse = TheMouse()) mouse->SetActive(0);
        if (sounds) sounds->PauseAll();
        gameapi::ShowWindow(MainWindow(), SW_MINIMIZE);
        return 1;
    }
    if (state > 2) return 1;
    ActiveFlag() = 1;
    ActivationSeen() = 1;
    gameapi::ShowWindow(MainWindow(), SW_RESTORE);
    if (CDirectInputMouse* mouse = TheMouse()) mouse->SetActive(1);
    CTextureDevice* device = TheRenderDevice();
    if (device && (device->flags_ & 1)) {
        SceneRenderer()->RestoreSurfaces();
        device = TheRenderDevice();
        if (device) {
            // 005DAE70 (ResetCursors) after the device was restored. The original pushes the last
            // argument from EAX after loading only AL, so its upper bytes are those of the device
            // pointer; the value below rebuilds them. The context is the mouse.
            std::uint32_t flags = (reinterpret_cast<std::uintptr_t>(device) & 0xFFFFFF00u) |
                                  (ei::GameData<std::uint8_t>(0x00784756) == 0 ? 1u : 0u);
            ResetCursors(options::TheOptions()->fpsIndependentCursor_ != 0, static_cast<int>(reinterpret_cast<std::intptr_t>(TheMouse())), 0,
                         static_cast<int>(flags));
        }
    }
    TheCursor()->ShowImage(1);
    if (sounds) sounds->ResumeAll();
    TheInterface()->ResetInput();
    return 1;
}

LRESULT OnSysCommand(HWND window, WPARAM wParam, LPARAM lParam) {
    unsigned command = wParam & 0xFFF0;
    if (command == SC_HOTKEY || command == SC_NEXTWINDOW || command == SC_PREVWINDOW) {
        Console()->PutText("switch task")->PutChar('\n')->Apply(libcrt::FlushManipulator);
    } else if (command == SC_SCREENSAVE || command == SC_MONITORPOWER) {
        return 0;
    }
    return gameapi::DefWindowProcA(window, WM_SYSCOMMAND, wParam, lParam);
}

// The "QueryCancelAutoPlay" message id, registered on first use (function-local static of the
// original: guard bit 0 of 0079BA04, id 0079BA00).
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
    TheInterface()->CloseAllPending();
    return 1;
}

int Low(LPARAM v) { return static_cast<int>(v & 0xFFFF); }
int HighSigned(LPARAM v) { return static_cast<int>(v) >> 16; }              // SAR (keyboard)
int HighUnsigned(LPARAM v) { return static_cast<int>(static_cast<unsigned>(v) >> 16); }   // SHR (mouse)

bool AltDown(LPARAM lParam) { return (lParam & 0x20000000) != 0; }


void __fastcall MenuOpen(void* flow) { FlowProcessRequestOriginal(flow); }

int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

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
        if (QuitOnClose()) gameapi::PostQuitMessage(0);
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
EI_REPLACE(0x00457D40, WindowProc);

LRESULT __fastcall DispatchInput(UINT message, WPARAM wParam, LPARAM lParam) {
    // The switch-over with the frame scaled into the window: the mouse's coordinates as the frame's
    // (present_window.h; unchanged otherwise). The game's own wheel message (0xCC54) is not mapped.
    lParam = present_window::MouseToGame(message, lParam);
    CInterface3D* input = TheInterface();
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
        input->OnChar(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4 && AltDown(lParam)) {
            if (QuitOnClose()) gameapi::PostQuitMessage(0);
            return Handled();
        }
        input->OnKeyDown(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_SYSKEYUP:
        // (006FC14E, "%u", for the int; 006FC91C, "%ld", for lParam.)
        Console()->PutUnsigned(static_cast<unsigned>(w))->PutChar(':')->PutLong(static_cast<long>(lParam))->PutChar('\n')
            ->Apply(libcrt::FlushManipulator);
        if (wParam == VK_TAB && AltDown(lParam)) {   // Alt+Tab: as if the window was deactivated
            HWND window = MainWindow();
            BOOL iconic = gameapi::IsIconic(window);
            WindowProc(MainWindow(), WM_ACTIVATE, static_cast<WPARAM>(iconic) << 16, 0);
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
    case 0xCC54:   // the game's own wheel message: wParam is the delta
        input->OnWheel(0, w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    }
    return WindowProc(MainWindow(), message, wParam, lParam);
}

void __cdecl PumpMessages() {
    MSG msg;
    while (gameapi::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        WindowProc(MainWindow(), msg.message, msg.wParam, msg.lParam);
}

int __cdecl StartFirstMap() {
    const auto& cmdMap = CommandLineMap();    // map on the command line
    const auto& cmdMode = CommandLineMob();   // empty: main menu
    const auto& cmdScript = CommandLineScript();
    mfc::CString map;
    map = Length(cmdMap) != 0 ? cmdMap : options::TheOptions()->defaultMapName_;
    if (gamecrt::mbscmp(static_cast<const char*>(cmdMode),
                        game::EmptyText()) == 0) {
        TheGameFlow()->CloseSessions();
        MenuOpen(TheGameFlow());
        return 1;
    }
    void* world = TheServerWorld();
    if (!WorldNewZoneOriginal(world, nullptr, map, cmdMode, (const char*)0))
        return game::FatalError("Can't load map \"%s\".\n", static_cast<const char*>(map));
    static_cast<CWorldServer*>(world)->SetGameResult(1);
    void* client = TheClientWorld();
    static_cast<CWorldClient*>(client)->SetPlayer(ei::GameData<std::uint8_t*>(0x007AFFBC));
    static_cast<CWorldClient*>(client)->LocalView()[0x85] = 0;
    ei::GameData<int>(0x007C2CC4) = 7;
    if (Length(cmdScript) == 0) return 1;
    void* script = TheMissionScript();
    BOOL loaded;
    {
        mfc::CString root(TheDataFolder());
        mfc::CString dir = root + "Scripts\\";
        mfc::CString path = dir + cmdScript;
        loaded = static_cast<CScript*>(script)->LoadFile(path);
    }
    if (loaded) return 1;
    ScriptErrorInfo error;   // GetError constructs the text over the empty one
    static_cast<CScript*>(script)->GetError(&error);
    return game::FatalError("%s, line %i", static_cast<const char*>(error.text), error.line);
}

// ---- FatalError (00480080) ---------------------------------------------------------------------

namespace {
const char* FlipFailedText() { return "Can't flip to GDI surface.\n"; }
const char* WindowTitle() { return ei::GamePointer<const char>(0x0079BB24); }      // the game window's title (a buffer)
} // namespace

// 004560D0 (A-E2).
int __fastcall ShowMessageBox(const char* text, int flags) {
    HCURSOR cursor = gameapi::GetCursor();
    CTextureDevice* device = TheRenderDevice();
    if (device && (device->flags_ & 1)) {
        long result = device::FlipToGDISurface(static_cast<IDirectDraw7*>(device->ddraw_));
        device->lastLoadResult_ = result;
        if (result != 0) FatalErrorCode(result, FlipFailedText());
    }
    int answer = gameapi::MessageBoxA(MainWindow(), text, WindowTitle(), static_cast<UINT>(flags));
    gameapi::SetCursor(cursor);
    return answer;
}

namespace {

constexpr int kErrorBoxFlags = 0x10;   // MB_ICONHAND
using ErrorHook = void(__fastcall*)(const char* text);
void* g_fatalErrorHook = /*007A9BA0*/ {nullptr};   // set by the console (null here)
int g_fatalErrorCount = /*007A9B9C*/ {};

} // namespace

// Formats the error (wvsprintfA, 1 KB), shows it in a message box, hands it to the error hook and
// exits the game (exit(-1); the error counter at 007A9B9C reaches 1). Rebirth's
// DisableExitOnCrashReport replaces the message box with a line in addon.log (not reproduced) and
// removes the exit, so the game carries on. Returns 0.
int __cdecl FatalError(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    gameapi::wvsprintfA(text, format, args);
    va_end(args);
    bool carryOn = ei::rebirth::Enabled(ei::rebirth::Patch::DisableExitOnCrashReport);
    if (!carryOn) ShowMessageBox(text, kErrorBoxFlags);
    if (auto hook = reinterpret_cast<ErrorHook>(g_fatalErrorHook)) hook(text);
    if (++g_fatalErrorCount >= 1 && !carryOn) libcrt::Exit(-1);
    return 0;
}
EI_REPLACE(0x00480080, FatalError);

// ---- FatalErrorCode (004800E0) -----------------------------------------------------------------

namespace {
const char* CodeReportFormat() { return "%s (0x%.8X) %s"; }
} // namespace

int FatalErrorCodeV(long result, const char* format, va_list args) {
    char text[0x400];
    char report[0x400];
    gameapi::wvsprintfA(text, format, args);
    gamecrt::sprintf(report, CodeReportFormat(), text, result, DirectXErrorText(result));
    ShowMessageBox(report, kErrorBoxFlags);
    if (auto hook = reinterpret_cast<ErrorHook>(g_fatalErrorHook)) hook(report);
    if (++g_fatalErrorCount >= 1) libcrt::Exit(-1);
    return 0;
}

int __cdecl FatalErrorCode(long result, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int value = FatalErrorCodeV(result, format, args);
    va_end(args);
    return value;
}

// ---- The operator new handler (00455A30) -------------------------------------------------------

namespace {
const char* MemoryDepletedText() { return "Memory depleted"; }
} // namespace

// 00455A30 (A-E2). The body is the message box 004560D0's, inlined.
int __cdecl OutOfMemory(std::size_t) {
    ShowMessageBox(MemoryDepletedText(), kErrorBoxFlags);
    return 0;
}
EI_REPLACE(0x00455A30, OutOfMemory);

} // namespace app
