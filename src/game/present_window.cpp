// The game window in the switch-over without DirectDraw (present_window.h).
#include "present_window.h"

#include "device.h"
#include "texture.h"

#include "../hook/log.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>

namespace present_window {
namespace {

HWND Window() {
    CTextureDevice* device = RenderDeviceObject();
    return device ? device->window_ : nullptr;
}

// The frame does not fill the client area one to one: the game's coordinates need mapping.
bool Scaled() {
    int fw = 0, fh = 0;
    render::Rect placed{};
    if (!render::FrameInWindow(&fw, &fh, &placed)) return false;
    return placed.left != 0 || placed.top != 0 || placed.right != fw || placed.bottom != fh;
}

// The window's client point as a frame pixel.
POINT ClientToFrame(POINT p) {
    int x = p.x, y = p.y;
    render::WindowToFrame(&x, &y);
    return POINT{x, y};
}

decltype(&::GetCursorPos) g_getCursorPos = nullptr;
decltype(&::SetCursorPos) g_setCursorPos = nullptr;

// The game's GetCursorPos: the cursor's screen position as a point of the frame (the game's screen).
BOOL WINAPI FrameCursorPos(LPPOINT point) {
    if (!g_getCursorPos(point)) return FALSE;
    HWND window = Window();
    if (!window || !Scaled()) return TRUE;
    POINT p = *point;
    ScreenToClient(window, &p);
    *point = ClientToFrame(p);
    return TRUE;
}

// The game's SetCursorPos: a point of the frame to the screen.
BOOL WINAPI FrameSetCursorPos(int x, int y) {
    HWND window = Window();
    if (window && Scaled()) {
        render::FrameToWindow(&x, &y);
        POINT p{x, y};
        ClientToScreen(window, &p);
        x = p.x;
        y = p.y;
    }
    return g_setCursorPos(x, y);
}

template <typename F>
void ReplaceImport(F& slot, F hook, F* previous) {
    DWORD old = 0;
    VirtualProtect(&slot, sizeof(F), PAGE_READWRITE, &old);
    *previous = slot;
    slot = hook;
    VirtualProtect(&slot, sizeof(F), old, &old);
}

void MapCursorImports() {
    static bool done = false;
    if (done) return;
    done = true;
    // Whatever the slots hold now (the harness's own cursor in its runs) stays underneath.
    ReplaceImport(gameapi::GetCursorPos, &FrameCursorPos, &g_getCursorPos);
    ReplaceImport(gameapi::SetCursorPos, &FrameSetCursorPos, &g_setCursorPos);
}

// Full screen: the window over its monitor (the frame is scaled into it, aspect kept).
void CoverMonitor(HWND window) {
    if (IsIconic(window)) return;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) return;
    RECT now{};
    if (!GetWindowRect(window, &now) || EqualRect(&now, &info.rcMonitor)) return;
    const RECT& m = info.rcMonitor;
    ei::Log("present: full screen without a display mode change: the window (%ld,%ld)-(%ld,%ld) covers the monitor "
            "(%ld,%ld)-(%ld,%ld)",
            now.left, now.top, now.right, now.bottom, m.left, m.top, m.right, m.bottom);
    SetWindowPos(window, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

} // namespace

void Update(CTextureDevice* device) {
    if (!device || !render::PresentsItself()) return;
    MapCursorImports();
    if ((device->flags_ & 1) && device->window_) CoverMonitor(device->window_);
}

LPARAM MouseToGame(UINT message, LPARAM lParam) {
    bool client = message >= WM_MOUSEMOVE && message <= WM_MBUTTONDBLCLK;
    if (!(client || message == WM_MOUSEWHEEL) || !Scaled()) return lParam;
    POINT p{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
    if (message == WM_MOUSEWHEEL) {
        HWND window = Window();
        if (!window) return lParam;
        ScreenToClient(window, &p);
        POINT f = ClientToFrame(p);
        // Back to "screen" coordinates of a window at 0, 0: the frame's.
        return MAKELPARAM(static_cast<WORD>(f.x), static_cast<WORD>(f.y));
    }
    POINT f = ClientToFrame(p);
    return MAKELPARAM(static_cast<WORD>(f.x), static_cast<WORD>(f.y));
}

} // namespace present_window
