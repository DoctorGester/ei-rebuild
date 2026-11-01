// The harness's input is the game's only input (harness_input.h).
#include "harness_input.h"

#include "harness_platform.h"

#include <windowsx.h>

namespace ei::verify::input {
namespace {

using platform::Event;
using platform::EventType;

void SetKey(Event& event, EventType type, WPARAM key, LPARAM data) {
    event.type = type;
    event.key = static_cast<int>(key);
    event.repeat = static_cast<int>(data & 0xFFFF);
    event.keyFlags = static_cast<int>(data) >> 16;
}

void SetMouse(Event& event, EventType type, WPARAM buttons, LPARAM at) {
    event.type = type;
    event.buttons = static_cast<int>(buttons);
    event.x = GET_X_LPARAM(at);
    event.y = GET_Y_LPARAM(at);
}

} // namespace

void Post(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_ACTIVATE) {
        harnessplatform::Activate(LOWORD(wParam) != WA_INACTIVE);
        return;
    }
    Event event;
    switch (message) {
    case WM_KEYDOWN: SetKey(event, EventType::KeyDown, wParam, lParam); break;
    case WM_KEYUP: SetKey(event, EventType::KeyUp, wParam, lParam); break;
    case WM_SYSKEYDOWN: SetKey(event, EventType::SystemKeyDown, wParam, lParam); break;
    case WM_SYSKEYUP: SetKey(event, EventType::SystemKeyUp, wParam, lParam); break;
    case WM_CHAR: SetKey(event, EventType::Character, wParam & 0xFFFF, lParam); break;
    case WM_MOUSEMOVE: SetMouse(event, EventType::MouseMove, wParam, lParam); break;
    case WM_LBUTTONDOWN: SetMouse(event, EventType::LeftDown, wParam, lParam); break;
    case WM_LBUTTONUP: SetMouse(event, EventType::LeftUp, wParam, lParam); break;
    case WM_LBUTTONDBLCLK: SetMouse(event, EventType::LeftDouble, wParam, lParam); break;
    case WM_RBUTTONDOWN: SetMouse(event, EventType::RightDown, wParam, lParam); break;
    case WM_RBUTTONUP: SetMouse(event, EventType::RightUp, wParam, lParam); break;
    case WM_RBUTTONDBLCLK: SetMouse(event, EventType::RightDouble, wParam, lParam); break;
    case WM_MOUSEWHEEL:
        SetMouse(event, EventType::Wheel, LOWORD(wParam), lParam);
        event.wheelDelta = static_cast<short>(HIWORD(wParam));
        break;
    default: return;
    }
    harnessplatform::Queue(event);
}

void AddMotion(int dx, int dy) { harnessplatform::AddMotion(dx, dy); }

void Report(FILE* f) {
    const unsigned left = harnessplatform::RealInputLeftOut();
    if (!left)
        std::fprintf(f, "\nMOUSE: the harness's only (the real keyboard and mouse never reached the game window)\n");
    else
        std::fprintf(f, "\nMOUSE: the harness's only; the real keyboard and mouse left out: %u events\n", left);
}

} // namespace ei::verify::input
