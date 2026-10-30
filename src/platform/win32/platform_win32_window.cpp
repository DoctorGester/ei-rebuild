// The window-procedure path of the platform layer (platform/platform.h; docs/notes/platform.md, "SDL3 and the
// harness"): the clocks, the message box, the threads, the window and its events, the cursor, the mouse and the
// clipboard on Windows, through game.exe's import slots, which the harness drives (its virtual clocks, its posted
// window messages and the frame steps in PeekMessageA, its own mouse). Built into the harness's builds (verify,
// verify-x64, asan) in place of src/platform/sdl/platform_sdl.cpp; the other areas are platform_win32.cpp's.
#include "platform/platform.h"
#include "platform/win32/gameapi.h"
#include "platform/win32/platform_win32.h"

#include <windows.h>

#define DIRECTINPUT_VERSION 0x0700
#include <cstdint>
#include <cstring>
#include <dinput.h>
#include <memory>
#include <process.h>
#include <string>

// ==== Clocks =========================================================================================================

// Through game.exe's import slots: the harness's clocks replace them.

namespace platform {

std::uint32_t Milliseconds() { return gameapi::timeGetTime(); }

std::uint32_t TickCount() { return gameapi::GetTickCount(); }

void Sleep(std::uint32_t ms) { gameapi::Sleep(ms); }

DateTime LocalTime() {
    SYSTEMTIME local;
    gameapi::GetLocalTime(&local);
    return {local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond};
}

} // namespace platform

// ==== The message box ================================================================================================

// Through game.exe's import slot, which the image routes to the UTF-8 call.

namespace platform {

MessageAnswer ShowMessageBox(void* owner, const char* text, const char* caption, MessageButtons buttons,
                             MessageIcon icon) {
    const UINT style = (buttons == MessageButtons::OkCancel ? MB_OKCANCEL : MB_OK) |
                       (icon == MessageIcon::Error ? MB_ICONHAND : MB_ICONEXCLAMATION);
    const int answer = gameapi::MessageBoxA(static_cast<HWND>(owner), text, caption, style);
    return answer == IDCANCEL ? MessageAnswer::Cancel : MessageAnswer::Ok;
}

} // namespace platform

// ==== Threads ========================================================================================================

// Through game.exe's import slots, as the game called them.

namespace platform {

bool Thread::Start(ThreadProcedure procedure, void* parameter, int priority) {
    Close();
    unsigned id = 0;
    HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, procedure, parameter, CREATE_SUSPENDED, &id));
    if (!thread) return false;
    gameapi::SetThreadPriority(thread, priority);
    gameapi::ResumeThread(thread);
    handle = thread;
    return true;
}

void Thread::Close() {
    if (handle) gameapi::CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

void Signal::Create() {
    Close();
    handle = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

void Signal::Set() { gameapi::SetEvent(static_cast<HANDLE>(handle)); }

void Signal::Wait() { gameapi::WaitForSingleObject(static_cast<HANDLE>(handle), INFINITE); }

void Signal::Close() {
    if (handle) gameapi::CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

} // namespace platform

// ==== The window and its events ======================================================================================

// Through game.exe's import slots: the harness hooks the window's creation and showing and the message calls (its
// input arrives as posted messages, its frame steps run in PeekMessageA), and the image routes the class, the
// messages and the default procedure to the wide calls. The screen mapping and the monitor cover never went through a
// slot and call the system directly.

namespace platform {
namespace {

EventHandler g_handler = nullptr;
// The registered class, which stands for its name in CreateWindowExA.
ATOM g_classAtom = 0;

// The "QueryCancelAutoPlay" message's id, registered the first time a message is not one of the others.
UINT g_autoPlayMessage = 0;
bool g_autoPlayRegistered = false;
UINT AutoPlayMessage() {
    if (!g_autoPlayRegistered) {
        g_autoPlayRegistered = true;
        g_autoPlayMessage = gameapi::RegisterWindowMessageA("QueryCancelAutoPlay");
    }
    return g_autoPlayMessage;
}

Activation ActivationOf(WPARAM state) {
    switch (state & 0xFFFF) {
    case WA_INACTIVE: return Activation::Inactive;
    case WA_ACTIVE: return Activation::Active;
    case WA_CLICKACTIVE: return Activation::ClickActive;
    }
    return Activation::Unknown;
}

SystemCommand SystemCommandOf(WPARAM command) {
    switch (command & 0xFFF0) {
    case SC_HOTKEY:
    case SC_NEXTWINDOW:
    case SC_PREVWINDOW: return SystemCommand::TaskSwitch;
    case SC_SCREENSAVE:
    case SC_MONITORPOWER: return SystemCommand::ScreenSaver;
    }
    return SystemCommand::Other;
}

void SetKey(Event& event, EventType type, WPARAM key, LPARAM data) {
    event.type = type;
    event.key = static_cast<int>(key);
    event.repeat = static_cast<int>(data & 0xFFFF);
    event.keyFlags = static_cast<int>(data) >> 16;
}

void SetMouse(Event& event, EventType type, WPARAM buttons, LPARAM at) {
    event.type = type;
    event.buttons = static_cast<int>(buttons);
    event.x = static_cast<short>(LOWORD(at));
    event.y = static_cast<short>(HIWORD(at));
}

Event Translate(HWND window, UINT id, WPARAM first, LPARAM second) {
    Event event;
    event.window = window;
    event.native = {id, first, second};
    switch (id) {
    case WM_KEYDOWN: SetKey(event, EventType::KeyDown, first, second); break;
    case WM_KEYUP: SetKey(event, EventType::KeyUp, first, second); break;
    case WM_SYSKEYDOWN: SetKey(event, EventType::SystemKeyDown, first, second); break;
    case WM_SYSKEYUP: SetKey(event, EventType::SystemKeyUp, first, second); break;
    case WM_CHAR: SetKey(event, EventType::Character, first & 0xFFFF, second); break;
    case WM_MOUSEMOVE: SetMouse(event, EventType::MouseMove, first, second); break;
    case WM_LBUTTONDOWN: SetMouse(event, EventType::LeftDown, first, second); break;
    case WM_LBUTTONUP: SetMouse(event, EventType::LeftUp, first, second); break;
    case WM_LBUTTONDBLCLK: SetMouse(event, EventType::LeftDouble, first, second); break;
    case WM_RBUTTONDOWN: SetMouse(event, EventType::RightDown, first, second); break;
    case WM_RBUTTONUP: SetMouse(event, EventType::RightUp, first, second); break;
    case WM_RBUTTONDBLCLK: SetMouse(event, EventType::RightDouble, first, second); break;
    case WM_MOUSEWHEEL:
        SetMouse(event, EventType::Wheel, LOWORD(first), second);
        event.wheelDelta = static_cast<short>(HIWORD(first));
        break;
    case WM_PAINT: event.type = EventType::Paint; break;
    case WM_DESTROY: event.type = EventType::Destroy; break;
    case WM_ACTIVATE:
        event.type = EventType::Activate;
        event.activation = ActivationOf(first);
        event.minimized = HIWORD(first) != 0;
        break;
    case WM_SETCURSOR: event.type = EventType::SetCursor; break;
    case WM_SYSCOMMAND:
        event.type = EventType::SystemCommand;
        event.command = SystemCommandOf(first);
        break;
    case WM_QUIT:
        event.type = EventType::Quit;
        event.quitCode = static_cast<int>(first);
        break;
    default:
        if (id != 0 && id == AutoPlayMessage()) event.type = EventType::AutoPlay;
        break;
    }
    return event;
}

Event Translate(const MSG& message) { return Translate(message.hwnd, message.message, message.wParam, message.lParam); }

LRESULT CALLBACK WindowProcedure(HWND window, UINT id, WPARAM first, LPARAM second) {
    return g_handler(Translate(window, id, first, second));
}

RECT ToRECT(const Rect& r) { return RECT{r.left, r.top, r.right, r.bottom}; }
Rect FromRECT(const RECT& r) {
    return Rect{static_cast<int>(r.left), static_cast<int>(r.top), static_cast<int>(r.right), static_cast<int>(r.bottom)};
}

} // namespace

// ---- The window --------------------------------------------------------------------------------------------------

void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler) {
    g_handler = handler;
    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = static_cast<HINSTANCE>(instance);
    windowClass.lpszClassName = className;
    g_classAtom = gameapi::RegisterClassExA(&windowClass);
}

NativeWindow CreateGameWindow(NativeInstance instance, const char* title, int& width, int& height, bool) {
    return gameapi::CreateWindowExA(0, MAKEINTRESOURCEA(g_classAtom), title, WS_POPUP | WS_CLIPSIBLINGS, 0, 0, width,
                                    height, nullptr, nullptr, static_cast<HINSTANCE>(instance), nullptr);
}

void ShowGameWindow(NativeWindow window, int show) {
    gameapi::ShowWindow(static_cast<HWND>(window), show);
    gameapi::UpdateWindow(static_cast<HWND>(window));
}

bool IsMinimized(NativeWindow window) { return gameapi::IsIconic(static_cast<HWND>(window)) != FALSE; }

void Minimize(NativeWindow window) { gameapi::ShowWindow(static_cast<HWND>(window), SW_MINIMIZE); }

void Restore(NativeWindow window) { gameapi::ShowWindow(static_cast<HWND>(window), SW_RESTORE); }

void MarkPainted(NativeWindow window) {
    RECT rect;
    if (!gameapi::GetUpdateRect(static_cast<HWND>(window), &rect, FALSE)) return;
    PAINTSTRUCT paint;
    gameapi::BeginPaint(static_cast<HWND>(window), &paint);
    gameapi::EndPaint(static_cast<HWND>(window), &paint);
}

void RepaintDesktop() { gameapi::InvalidateRect(nullptr, nullptr, TRUE); }

void ScreenToClient(NativeWindow window, Point* point) {
    POINT p{point->x, point->y};
    ::ScreenToClient(static_cast<HWND>(window), &p);
    *point = Point{static_cast<int>(p.x), static_cast<int>(p.y)};
}

void ClientToScreen(NativeWindow window, Point* point) {
    POINT p{point->x, point->y};
    ::ClientToScreen(static_cast<HWND>(window), &p);
    *point = Point{static_cast<int>(p.x), static_cast<int>(p.y)};
}

bool CoverMonitor(NativeWindow window, Rect* was, Rect* monitor) {
    HWND handle = static_cast<HWND>(window);
    if (::IsIconic(handle)) return false;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &info)) return false;
    RECT now{};
    if (!GetWindowRect(handle, &now) || EqualRect(&now, &info.rcMonitor)) return false;
    *was = FromRECT(now);
    *monitor = FromRECT(info.rcMonitor);
    const RECT m = ToRECT(*monitor);
    SetWindowPos(handle, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    return true;
}

// ---- Events ------------------------------------------------------------------------------------------------------

bool EventWaits() {
    MSG message;
    return gameapi::PeekMessageA(&message, nullptr, 0, 0, PM_NOREMOVE) != FALSE;
}

bool NextEvent(Event* event) {
    MSG message;
    if (!gameapi::GetMessageA(&message, nullptr, 0, 0)) {
        *event = Translate(message);
        return false;
    }
    gameapi::TranslateMessage(&message);
    *event = Translate(message);
    return true;
}

bool TakeEvent(Event* event) {
    MSG message;
    if (!gameapi::PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) return false;
    *event = Translate(message);
    return true;
}

std::intptr_t DefaultHandling(const Event& event) {
    const NativeEvent& m = event.native;
    return gameapi::DefWindowProcA(static_cast<HWND>(event.window), m.id, m.first, m.second);
}

void DispatchEvent(const Event& event) {
    MSG message{};
    message.hwnd = static_cast<HWND>(event.window);
    message.message = event.native.id;
    message.wParam = event.native.first;
    message.lParam = event.native.second;
    gameapi::TranslateMessage(&message);
    gameapi::DispatchMessageA(&message);
}

void PostQuit(int code) { gameapi::PostQuitMessage(code); }

} // namespace platform

// ==== The cursor =====================================================================================================

// Through game.exe's import slots: the harness's own mouse answers GetCursorPos and keeps SetCursorPos from moving the
// user's cursor.

namespace platform {
namespace {

PointMap g_toGame = nullptr;
PointMap g_toScreen = nullptr;

} // namespace

CursorShape CurrentCursor() { return gameapi::GetCursor(); }

void SetCursor(CursorShape shape) { gameapi::SetCursor(static_cast<HCURSOR>(shape)); }

void MapCursorPoints(PointMap toGame, PointMap toScreen) {
    g_toGame = toGame;
    g_toScreen = toScreen;
}

bool CursorPosition(Point* at) {
    POINT p;
    if (!gameapi::GetCursorPos(&p)) return false;
    Point point{static_cast<int>(p.x), static_cast<int>(p.y)};
    if (g_toGame) g_toGame(&point);
    *at = point;
    return true;
}

void SetCursorPosition(Point at) {
    if (g_toScreen) g_toScreen(&at);
    gameapi::SetCursorPos(at.x, at.y);
}

} // namespace platform

// ==== The mouse ======================================================================================================

// DirectInput 7 of the system's dinput.dll.

namespace {

// The game's own copies of the interface ids, the system mouse's id and c_dfDIMouse's axes.
constexpr GUID kGuidSysMouse = /*00744DD0*/ {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidZAxis = /*00744E60*/ {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidYAxis = /*00744E70*/ {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidXAxis = /*00744E80*/ {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr IID kIidDirectInputDevice7 = /*00744EA0*/ {0x57D7C6BC, 0x2356, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};
constexpr IID kIidDirectInput7 = /*00744F10*/ {0x9A4CB684, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};

constexpr DWORD kInputVersion = 0x0700;

// DirectInputCreateEx of the system's dinput.dll (System32: Rebirth's dinput.dll in the game folder is a proxy that
// loads addon.dll, which patches game.exe's code), loaded on the first call; null when it is missing. The SDK has
// no import library of DirectInput 7's dinput.dll.
decltype(&::DirectInputCreateEx) SystemDirectInputCreate() {
    static const auto create = [] {
        HMODULE dinput = LoadLibraryExW(L"dinput.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return dinput ? reinterpret_cast<decltype(&::DirectInputCreateEx)>(GetProcAddress(dinput, "DirectInputCreateEx"))
                      : nullptr;
    }();
    return create;
}

// The mouse data format the device gets (dinput.lib's c_dfDIMouse, as a DIDATAFORMAT), our own copy.
const DIDATAFORMAT* MouseDataFormat() {
    constexpr DWORD kOptional = 0x80000000;   // DIDFT_OPTIONAL (DirectInput 8's headers only)
    static DIOBJECTDATAFORMAT objects[] = {
        {&kGuidXAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&kGuidYAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&kGuidZAxis, 8, DIDFT_AXIS | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 14, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 15, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
    };
    static const DIDATAFORMAT format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 16,
                                        sizeof(objects) / sizeof(objects[0]), objects};
    return &format;
}

// A counted pointer taking `value` (the game's smart pointer assignment, then the temporary's
// release, which is not null-checked).
template <typename T>
void Adopt(T*& slot, T* value) {
    T* previous = slot;
    slot = value;
    if (value) value->AddRef();
    if (previous) previous->Release();
    slot->Release();
}

template <typename T>
void Drop(T*& slot) {
    T* held = slot;
    slot = nullptr;
    if (held) held->Release();
}

} // namespace

void (*g_directInputObserver)(IDirectInput7A* input) = nullptr;

namespace platform {

struct MouseDevice::Device {
    IDirectInput7A* input = nullptr;          // counted
    IDirectInputDevice7A* mouse = nullptr;    // counted
};

MouseDevice::MouseDevice() : device(std::make_unique<Device>()) {}

int MouseDevice::Open(NativeWindow window, NativeInstance instance, MouseReport report) {
    IDirectInput7A*& input = device->input;
    IDirectInputDevice7A*& mouse = device->mouse;
    void* newInput = nullptr;
    const auto create = SystemDirectInputCreate();
    HRESULT result = create ? create(static_cast<HINSTANCE>(instance), kInputVersion, kIidDirectInput7, &newInput, nullptr)
                            : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    if (result < 0) return report(MouseStep::Create, result);
    if (g_directInputObserver) g_directInputObserver(static_cast<IDirectInput7A*>(newInput));
    Adopt(input, static_cast<IDirectInput7A*>(newInput));

    void* newDevice = nullptr;
    result = input->CreateDeviceEx(kGuidSysMouse, kIidDirectInputDevice7, &newDevice, nullptr);
    if (result < 0) return report(MouseStep::Device, result);
    Adopt(mouse, static_cast<IDirectInputDevice7A*>(newDevice));

    result = mouse->SetDataFormat(MouseDataFormat());
    if (result < 0) return report(MouseStep::Format, result);
    result = mouse->SetCooperativeLevel(static_cast<HWND>(window), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (result < 0) return report(MouseStep::Cooperation, result);
    if (mouse) {
        result = mouse->Acquire();
        if (result < 0) report(MouseStep::Acquire, result);
    }
    return 1;
}

void MouseDevice::Close() {
    if (device->mouse) device->mouse->Unacquire();
    Drop(device->mouse);
    Drop(device->input);
}

MouseDevice::~MouseDevice() { Close(); }

int MouseDevice::SetActive(bool active, MouseReport report) {
    IDirectInputDevice7A* mouse = device->mouse;
    if (!mouse) return 0;
    if (active) {
        HRESULT result = mouse->Acquire();
        if (result < 0) return report(MouseStep::Acquire, result);
    } else {
        mouse->Unacquire();
    }
    return 1;
}

void MouseDevice::Read(std::int32_t state[4]) {
    static_assert(sizeof(std::int32_t[4]) == sizeof(DIMOUSESTATE));
    if (device->mouse->GetDeviceState(sizeof(DIMOUSESTATE), state) < 0) std::memset(state, 0, sizeof(DIMOUSESTATE));
}

} // namespace platform

// ==== The clipboard ==================================================================================================

// Unicode text. Direct calls (never through game.exe's slots: the clipboard came with Rebirth's console).

namespace platform {

void CopyText(std::string_view text) {
    const std::wstring wide = ::text::Wide(text);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
    if (!memory) return;
    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(locked, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
}

std::string ClipboardText() {
    std::wstring wide;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                wide = text;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    return ::text::Narrow(wide);
}

} // namespace platform
