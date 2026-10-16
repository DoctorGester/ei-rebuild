// Application helpers around WinMain: window procedure (00457D40), input dispatcher (00457970),
// message pump (00458220) and the start of the first map (00456140). See app.h.
//
// Callees patched by Rebirth (0057C700, 00595480, FatalError 00480080) are reached through their
// original addresses.
#include "device.h"   // the old device's calls
#include "app.h"
#include "dx_errors.h"
#include "game_strings.h"
#include "present_window.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_world.h"
#include "originals_ui.h"
#include "script.h"
#include "texture.h"            // the renderer's device object
#include "world.h"

#include <ddraw.h>

#include <cstdarg>
#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace app {
namespace {

template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

HWND MainWindow() { return Global<HWND>(0x0079BC28); }
bool QuitOnClose() { return Global<std::uint8_t>(0x00784751) != 0; }   // Alt+F4 / WM_DESTROY quit

// The interface's input handler (007C7D50). Coordinates are passed as (wParam, x, y).
struct Input {
    void Update();                                   // 005F5AB0, called before and after each message
    void LButtonDown(int keys, int x, int y);        // 005F76A0
    void LButtonUp(int keys, int x, int y);          // 005F76E0
    void LButtonDblClk(int keys, int x, int y);      // 005F7800
    void RButtonDown(int keys, int x, int y);        // 005F7780
    void RButtonUp(int keys, int x, int y);          // 005F77C0
    void RButtonDblClk(int keys, int x, int y);      // 005F7840
    void MouseMove(int keys, int x, int y);          // 005F7740
    void Wheel(int keys, int delta, int x, int y);   // 005F7880
    void KeyDown(int key, int repeat, int flags);    // 005F78C0
    void Char(int key, int repeat, int flags);       // 005F7B40
    void KeyUp(int key, int repeat, int flags);      // 005F7BE0
};
__declspec(naked) void Input::Update() EI_ORIGINAL(0x005F5AB0)
__declspec(naked) void Input::LButtonDown(int, int, int) EI_ORIGINAL(0x005F76A0)
__declspec(naked) void Input::LButtonUp(int, int, int) EI_ORIGINAL(0x005F76E0)
__declspec(naked) void Input::LButtonDblClk(int, int, int) EI_ORIGINAL(0x005F7800)
__declspec(naked) void Input::RButtonDown(int, int, int) EI_ORIGINAL(0x005F7780)
__declspec(naked) void Input::RButtonUp(int, int, int) EI_ORIGINAL(0x005F77C0)
__declspec(naked) void Input::RButtonDblClk(int, int, int) EI_ORIGINAL(0x005F7840)
__declspec(naked) void Input::MouseMove(int, int, int) EI_ORIGINAL(0x005F7740)
__declspec(naked) void Input::Wheel(int, int, int, int) EI_ORIGINAL(0x005F7880)
__declspec(naked) void Input::KeyDown(int, int, int) EI_ORIGINAL(0x005F78C0)
__declspec(naked) void Input::Char(int, int, int) EI_ORIGINAL(0x005F7B40)
__declspec(naked) void Input::KeyUp(int, int, int) EI_ORIGINAL(0x005F7BE0)
Input* TheInput() { return reinterpret_cast<Input*>(0x007C7D50); }

// The console's ostream (007C55A8).
struct Ostream {
    Ostream* PutInt(int value);              // 006FC14E
    Ostream* PutLong(unsigned long value);   // 006FC91C
    Ostream* PutChar(char c);                // 006FC0B0
    Ostream* PutString(const char* text);    // 006FC31D
    Ostream* Manipulate(void* manipulator);  // 00434FE0
};
Ostream* Ostream::PutInt(int a0) { return static_cast<Ostream*>(OstreamPutUnsignedOriginal(this, nullptr, static_cast<unsigned>(a0))); }
Ostream* Ostream::PutLong(unsigned long a0) { return static_cast<Ostream*>(OstreamPutSignedLongOriginal(this, nullptr, static_cast<long>(a0))); }
Ostream* Ostream::PutChar(char c) { return static_cast<Ostream*>(OstreamPutChar(this, nullptr, static_cast<char>(c))); }
Ostream* Ostream::PutString(const char* text) { return static_cast<Ostream*>(OstreamPutString(this, nullptr, text)); }
Ostream* Ostream::Manipulate(void* manipulator) { return static_cast<Ostream*>(OstreamManipulate(this, nullptr, manipulator)); }
Ostream* Console() { return reinterpret_cast<Ostream*>(0x007C55A8); }
void* const kFlush = reinterpret_cast<void*>(0x00435000);

// Objects told about activation changes.
struct Renderer {          // 007C7BD8
    void SetActive(int active);   // 005E5270
};
void Renderer::SetActive(int a0) { CursorShowOriginal(this, nullptr, a0); }
struct Device {            // pointer at 007AF080 (flag byte +0x20 bit 0: full screen)
    void Deactivate();     // 004EEC60
};
__declspec(naked) void Device::Deactivate() EI_ORIGINAL(0x004EEC60)
struct DeviceHolder {      // 007AF060
    void Restore();        // 00504780
};
__declspec(naked) void DeviceHolder::Restore() EI_ORIGINAL(0x00504780)
struct Sound {             // pointer at 0079BA08
    void SetActive(int active);   // 0065EC70
};
__declspec(naked) void Sound::SetActive(int) EI_ORIGINAL(0x0065EC70)
struct Network {           // pointer at 007AF784
    void Pause();          // 006B7320
    void Resume();         // 006B7380
};
__declspec(naked) void Network::Pause() EI_ORIGINAL(0x006B7320)
__declspec(naked) void Network::Resume() EI_ORIGINAL(0x006B7380)
// 005DAE70 (__fastcall): resets the interface after the device was restored. The original pushes
// the second stack argument from EAX after loading only AL, so its upper bytes are those of the
// device pointer; the value below rebuilds them.
__declspec(naked) void __fastcall ResetInterface(bool, void*, int, std::uint32_t)
    EI_ORIGINAL(0x005DAE70)

std::uint8_t& ActiveFlag() { return Global<std::uint8_t>(0x0079BC5C); }
std::uint8_t& ActivationSeen() { return Global<std::uint8_t>(0x0079BC5D); }

LRESULT OnActivate(WPARAM wParam) {
    if ((wParam >> 16) != 0) {   // minimised
        ActivationSeen() = 1;
        ActiveFlag() = 0;
        return 1;
    }
    auto* network = Global<Network*>(0x007AF784);
    unsigned state = wParam & 0xFFFF;
    auto* renderer = reinterpret_cast<Renderer*>(0x007C7BD8);
    if (state == 0) {   // WA_INACTIVE
        ActiveFlag() = 0;
        ActivationSeen() = 1;
        renderer->SetActive(0);
        auto* device = Global<std::uint8_t*>(0x007AF080);
        if (device && (device[0x20] & 1)) reinterpret_cast<Device*>(device)->Deactivate();
        if (auto* sound = Global<Sound*>(0x0079BA08)) sound->SetActive(0);
        if (network) network->Pause();
        gameapi::ShowWindow(MainWindow(), SW_MINIMIZE);
        return 1;
    }
    if (state > 2) return 1;
    ActiveFlag() = 1;
    ActivationSeen() = 1;
    gameapi::ShowWindow(MainWindow(), SW_RESTORE);
    if (auto* sound = Global<Sound*>(0x0079BA08)) sound->SetActive(1);
    auto* device = Global<std::uint8_t*>(0x007AF080);
    if (device && (device[0x20] & 1)) {
        reinterpret_cast<DeviceHolder*>(0x007AF060)->Restore();
        device = Global<std::uint8_t*>(0x007AF080);
        if (device) {
            std::uint32_t value = (reinterpret_cast<std::uintptr_t>(device) & 0xFFFFFF00u) |
                                  (Global<std::uint8_t>(0x00784756) == 0 ? 1u : 0u);
            ResetInterface(Global<int>(0x0079BCF0) != 0, Global<void*>(0x0079BA08), 0, value);
        }
    }
    renderer->SetActive(1);
    if (network) network->Resume();
    reinterpret_cast<InterfaceOriginals*>(TheInput())->ResetInput();
    return 1;
}

LRESULT OnSysCommand(HWND window, WPARAM wParam, LPARAM lParam) {
    unsigned command = wParam & 0xFFF0;
    if (command == SC_HOTKEY || command == SC_NEXTWINDOW || command == SC_PREVWINDOW) {
        Console()->PutString(reinterpret_cast<const char*>(0x007849E8))->PutChar('\n')->Manipulate(kFlush);   // "switch task"
    } else if (command == SC_SCREENSAVE || command == SC_MONITORPOWER) {
        return 0;
    }
    return gameapi::DefWindowProcA(window, WM_SYSCOMMAND, wParam, lParam);
}

// The "QueryCancelAutoPlay" message id, registered on first use (function-local static of the
// original: guard bit 0 of 0079BA04, id 0079BA00).
UINT AutoPlayMessage() {
    auto& guard = Global<std::uint8_t>(0x0079BA04);
    if (!(guard & 1)) {
        guard |= 1;
        Global<UINT>(0x0079BA00) = gameapi::RegisterWindowMessageA(reinterpret_cast<const char*>(0x007847B8));
    }
    return Global<UINT>(0x0079BA00);
}

LRESULT Handled() {
    TheInput()->Update();
    return 1;
}

int Low(LPARAM v) { return static_cast<int>(v & 0xFFFF); }
int HighSigned(LPARAM v) { return static_cast<int>(v) >> 16; }              // SAR (keyboard)
int HighUnsigned(LPARAM v) { return static_cast<int>(static_cast<unsigned>(v) >> 16); }   // SHR (mouse)

bool AltDown(LPARAM lParam) { return (lParam & 0x20000000) != 0; }

// The script loader of the mission script (007C27D4).
// 00456360. Copies the script's error text, code and line.
struct ScriptError {
    char* text;   // CString
    int code;
    int line;
};
__declspec(naked) ScriptError* __fastcall GetScriptError(void*, void*, ScriptError*) EI_ORIGINAL(0x00456360)

void __fastcall MenuOpen(void* flow) { FlowProcessRequestOriginal(flow); }
int __fastcall WorldLoadMap(void* a0, void*, const char* a2, const char* a3, int a4) { return WorldNewZoneOriginal(a0, nullptr, a2, a3, (const char*)a4); }

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
    Input* input = TheInput();
    input->Update();
    int w = static_cast<int>(wParam);
    switch (message) {
    case WM_KEYDOWN:
        input->KeyDown(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_KEYUP:
        input->KeyUp(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_CHAR:
        input->Char(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4 && AltDown(lParam)) {
            if (QuitOnClose()) gameapi::PostQuitMessage(0);
            return Handled();
        }
        input->KeyDown(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_SYSKEYUP:
        Console()->PutInt(w)->PutChar(':')->PutLong(static_cast<unsigned long>(lParam))->PutChar('\n')->Manipulate(kFlush);
        if (wParam == VK_TAB && AltDown(lParam)) {   // Alt+Tab: as if the window was deactivated
            HWND window = MainWindow();
            BOOL iconic = gameapi::IsIconic(window);
            WindowProc(MainWindow(), WM_ACTIVATE, static_cast<WPARAM>(iconic) << 16, 0);
            return Handled();
        }
        input->KeyUp(w, Low(lParam), HighSigned(lParam));
        return Handled();
    case WM_MOUSEMOVE:
        input->MouseMove(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_LBUTTONDOWN:
        input->LButtonDown(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_LBUTTONUP:
        input->LButtonUp(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_LBUTTONDBLCLK:
        input->LButtonDblClk(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_RBUTTONDOWN:
        input->RButtonDown(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_RBUTTONUP:
        input->RButtonUp(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_RBUTTONDBLCLK:
        input->RButtonDblClk(w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    case WM_MOUSEWHEEL:
        input->Wheel(Low(static_cast<LPARAM>(wParam)), HighUnsigned(static_cast<LPARAM>(wParam)), Low(lParam), HighUnsigned(lParam));
        return Handled();
    case 0xCC54:   // the game's own wheel message: wParam is the delta
        input->Wheel(0, w, Low(lParam), HighUnsigned(lParam));
        return Handled();
    }
    return WindowProc(MainWindow(), message, wParam, lParam);
}
EI_REPLACE(0x00457970, DispatchInput);

void __cdecl PumpMessages() {
    MSG msg;
    while (gameapi::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        WindowProc(MainWindow(), msg.message, msg.wParam, msg.lParam);
}
EI_REPLACE(0x00458220, PumpMessages);

int __cdecl StartFirstMap() {
    const auto& cmdMap = Global<mfc::CString>(0x0079BA10);    // map on the command line
    const auto& cmdMode = Global<mfc::CString>(0x0079BA14);   // empty: main menu
    const auto& cmdScript = Global<mfc::CString>(0x0079BA18);
    mfc::CString map;
    map = Length(cmdMap) != 0 ? cmdMap : Global<mfc::CString>(0x0079BEC8);   // options' DefaultMapName
    if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(cmdMode)),
                        reinterpret_cast<const unsigned char*>(game::EmptyText())) == 0) {
        static_cast<GameFlow*>(reinterpret_cast<void*>(0x007AFE20))->CloseSessions();
        MenuOpen(reinterpret_cast<void*>(0x007AFE20));
        return 1;
    }
    void* world = reinterpret_cast<void*>(0x007AFF60);
    if (!WorldLoadMap(world, nullptr, map, cmdMode, 0))
        return game::FatalError(reinterpret_cast<const char*>(0x007847E4), static_cast<const char*>(map));   // "Can't load map '%s'"
    static_cast<CWorldServer*>(world)->SetGameResult(1);
    void* client = reinterpret_cast<void*>(0x007AF5B8);
    ClientSetPlayer(client, nullptr, Global<int>(0x007AFFBC));
    static_cast<CWorldClient*>(client)->LocalView()[0x85] = 0;
    Global<int>(0x007C2CC4) = 7;
    if (Length(cmdScript) == 0) return 1;
    void* script = reinterpret_cast<void*>(0x007C27D4);
    BOOL loaded;
    {
        mfc::CString root(Global<mfc::CString>(0x007AF460));
        mfc::CString dir = root + reinterpret_cast<const char*>(0x007847D8);   // "Scripts\"
        mfc::CString path = dir + cmdScript;
        loaded = static_cast<CScript*>(script)->LoadFile(path);
    }
    if (loaded) return 1;
    ScriptError error;
    GetScriptError(script, nullptr, &error);
    int result = game::FatalError(reinterpret_cast<const char*>(0x007847CC), error.text, error.line);   // "%s, line %i"
    reinterpret_cast<mfc::CString*>(&error.text)->~CString();
    return result;
}
EI_REPLACE(0x00456140, StartFirstMap);

// ---- FatalError (00480080) ---------------------------------------------------------------------

namespace {
const char* FlipFailedText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078476C)); }   // "Can't flip to GDI surface.\n"
const char* WindowTitle() { return reinterpret_cast<const char*>(std::uintptr_t(0x0079BB24)); }      // the game window's title (a buffer)
} // namespace

// 004560D0 (A-E2).
int __fastcall ShowMessageBox(const char* text, int flags) {
    HCURSOR cursor = gameapi::GetCursor();
    CTextureDevice* device = RenderDeviceObject();
    if (device && (device->flags_ & 1)) {
        long result = device::FlipToGDISurface(static_cast<IDirectDraw7*>(device->ddraw_));
        device->lastLoadResult_ = result;
        if (result != 0) FatalErrorCode(result, FlipFailedText());
    }
    int answer = gameapi::MessageBoxA(MainWindow(), text, WindowTitle(), static_cast<UINT>(flags));
    gameapi::SetCursor(cursor);
    return answer;
}
EI_REPLACE(0x004560D0, ShowMessageBox);

namespace {

constexpr int kErrorBoxFlags = 0x10;   // MB_ICONHAND
using ErrorHook = void(__fastcall*)(const char* text);
ErrorHook& FatalErrorHook() { return Global<ErrorHook>(0x007A9BA0); }   // set by the console (null here)
int& FatalErrorCount() { return Global<int>(0x007A9B9C); }

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
    if (ErrorHook hook = FatalErrorHook()) hook(text);
    if (++FatalErrorCount() >= 1 && !carryOn) libcrt::Exit(-1);
    return 0;
}
EI_REPLACE(0x00480080, FatalError);
EI_REBIRTH_IMPLEMENTS(0x00480080, ei::rebirth::Patch::DisableExitOnCrashReport);

// ---- FatalErrorCode (004800E0) -----------------------------------------------------------------

namespace {
const char* CodeReportFormat() { return reinterpret_cast<const char*>(std::uintptr_t(0x00787EE4)); }   // "%s (0x%.8X) %s"
} // namespace

int FatalErrorCodeV(long result, const char* format, va_list args) {
    char text[0x400];
    char report[0x400];
    gameapi::wvsprintfA(text, format, args);
    gamecrt::sprintf(report, CodeReportFormat(), text, result, DirectXErrorText(result));
    ShowMessageBox(report, kErrorBoxFlags);
    if (ErrorHook hook = FatalErrorHook()) hook(report);
    if (++FatalErrorCount() >= 1) libcrt::Exit(-1);
    return 0;
}

int __cdecl FatalErrorCode(long result, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int value = FatalErrorCodeV(result, format, args);
    va_end(args);
    return value;
}
EI_REPLACE(0x004800E0, FatalErrorCode);

// ---- The operator new handler (00455A30) -------------------------------------------------------

namespace {
const char* MemoryDepletedText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078475C)); }   // "Memory depleted"
} // namespace

// 00455A30 (A-E2). The body is the message box 004560D0's, inlined.
int __cdecl OutOfMemory(std::size_t) {
    ShowMessageBox(MemoryDepletedText(), kErrorBoxFlags);
    return 0;
}
EI_REPLACE(0x00455A30, OutOfMemory);

} // namespace app
