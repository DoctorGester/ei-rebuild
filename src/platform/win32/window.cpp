// The game window and its events on Windows (platform/window.h). Through game.exe's import slots: the harness hooks
// the window's creation and showing and the message calls (its input arrives as posted messages, its frame steps run
// in PeekMessageA), and the image routes the class, the messages and the default procedure to the wide calls. The
// screen mapping and the monitor cover never went through a slot and call the system directly.
#include "platform/window.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {
namespace {

EventHandler g_handler = nullptr;
// The registered class, which stands for its name in FindWindowA and CreateWindowExA.
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
    event.native = {window, id, first, second};
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

NativeWindow FindClassWindow(const char* title) { return gameapi::FindWindowA(MAKEINTRESOURCEA(g_classAtom), title); }

std::uint32_t WindowProcess(NativeWindow window) {
    DWORD process = 0;
    gameapi::GetWindowThreadProcessId(static_cast<HWND>(window), &process);
    return process;
}

void BringToFront(NativeWindow window) {
    HWND other = static_cast<HWND>(window);
    const DWORD foregroundThread = gameapi::GetWindowThreadProcessId(gameapi::GetForegroundWindow(), nullptr);
    const DWORD otherThread = gameapi::GetWindowThreadProcessId(other, nullptr);
    if (otherThread == foregroundThread && !gameapi::IsIconic(other)) return;
    HWND popup = gameapi::GetLastActivePopup(other);
    if (gameapi::IsIconic(popup)) gameapi::ShowWindow(popup, SW_RESTORE);
    gameapi::BringWindowToTop(popup);
    gameapi::SetForegroundWindow(popup);
}

NativeInstance ProgramInstance() { return GetModuleHandleA(nullptr); }

NativeWindow CreateGameWindow(NativeInstance instance, const char* title, int width, int height) {
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
    const NativeMessage& m = event.native;
    return gameapi::DefWindowProcA(static_cast<HWND>(m.window), m.id, m.first, m.second);
}

void DispatchEvent(const Event& event) {
    MSG message{};
    message.hwnd = static_cast<HWND>(event.native.window);
    message.message = event.native.id;
    message.wParam = event.native.first;
    message.lParam = event.native.second;
    gameapi::TranslateMessage(&message);
    gameapi::DispatchMessageA(&message);
}

void PostQuit(int code) { gameapi::PostQuitMessage(code); }

} // namespace platform
