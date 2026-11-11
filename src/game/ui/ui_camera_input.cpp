// The battle camera's input (CameraInput): key release, right-button turning, deactivation and the wheel
// (CameraInput, ui/ui_camera_input.h; the key press is in ui_camera_input.cpp). See docs/notes/ui.md,
// section 3.
#include "game/ui/ui_camera_input.h"

#include "engine/scene/camera.h"
#include "game/ui/camera_trace.h"
#include "game/app/singletons.h"
#include "lib/bit_flags.h"
#include "engine/data/options.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/screen_layout.h"
#include "game/app/key_bindings.h"
#include "lib/vecmath.h"
#include "platform/platform.h"

#include <cstdint>

namespace {

// A camera motion's speed with the mouse: its power times its speed.
float MouseSpeed(const options::CameraMotionOptions& motion) { return motion.powerMouse * motion.speed; }
// The wheel's zoom steps per unit, kWheelUnits a notch (read from the exe, rule 9).
constexpr float kWheelStep = -0.008333334f;

bool ModifierHeld() { return g_interface.ctrlDown || g_interface.altDown; }

// Layout units to a part of the screen and the rates' scales
constexpr float kPerWidth = 0.0025f;
constexpr float kPerHeight = 0.0033333334f;

bool Differs(float a, float b) {
    return a != b;
}

enum class Edge : unsigned { Left = 1, Right = 0x10, Top = 0x100, Bottom = 0x1000 };
constexpr bool EnableBitFlags(Edge) { return true; }

// The arrow cursor of the screen edges the mouse is at.
CursorShape EdgeCursor(Edge edges) {
    switch (lib::Bits(edges)) {
    case lib::Bits(Edge::Left): return CursorShape::ScrollLeft;
    case lib::Bits(Edge::Right): return CursorShape::ScrollRight;
    case lib::Bits(Edge::Top): return CursorShape::ScrollUp;
    case lib::Bits(Edge::Left | Edge::Top): return CursorShape::ScrollUpLeft;
    case lib::Bits(Edge::Right | Edge::Top): return CursorShape::ScrollUpRight;
    case lib::Bits(Edge::Bottom): return CursorShape::ScrollDown;
    case lib::Bits(Edge::Left | Edge::Bottom): return CursorShape::ScrollDownLeft;
    case lib::Bits(Edge::Right | Edge::Bottom): return CursorShape::ScrollDownRight;
    default: return CursorShape::Default;
    }
}

} // namespace

void CameraInput::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::RightDown: case UiEventKind::RightDouble: RightDown(event); break;
    case UiEventKind::RightUp: RightUp(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::KeyUp: KeyUp(event); break;
    case UiEventKind::Wheel: Wheel(event); break;
    default: break;
    }
}

void CameraInput::KeyUp(UiEvent& event) {
    switch (g_keyActions[event.scan]) {
    case 5:
    case 6:
        x = 0.0f;
        event.handled = true;
        break;
    case 7:
    case 8:
        y = 0.0f;
        event.handled = true;
        break;
    case 9:
    case 10:
        z = 0.0f;
        event.handled = true;
        break;
    default:
        break;
    }
    if (!event.handled) return;
    Camera* camera = &g_camera;
    if (!ModifierHeld()) {
        float left = -x;
        float speed = KeyboardSpeed(options::g_options.cameraScroll);
        Vector3 direction = {y * speed, left * speed, 0.0f};
        camera->Pan(direction);
        camera->SetTurnRate(0.0f);
        camera->SetZoomTarget(KeyboardSpeed(options::g_options.cameraZoom) * z);
        camera->SetTiltRate(0.0f);
        return;
    }
    Vector3 still = {0.0f, 0.0f, 0.0f};
    camera->Pan(still);
    camera->SetTurnRate(KeyboardSpeed(options::g_options.cameraRotate) * y);
    camera->SetZoomTarget(KeyboardSpeed(options::g_options.cameraZoom) * z);
    camera->SetTiltRate(KeyboardSpeed(options::g_options.cameraPitch) * x);
}

void CameraInput::RightDown(UiEvent& event) {
    pressScreen = event.at;
    pressLayout = screen_layout::FromScreen(event.at);
    turning = true;
    g_cursor.SetHeld(1);
    event.handled = true;
}

void CameraInput::RightUp(UiEvent& event) {
    turning = false;
    Camera* camera = &g_camera;
    camera->SetTurnRate(0.0f);
    camera->SetTiltRate(0.0f);
    Vector3 still = {0.0f, 0.0f, 0.0f};
    camera->Pan(still);
    const Point position = g_cursor.position;
    g_cursor.SetHeld(0);
    platform::SetCursorPosition(position);
    event.handled = true;
}

void CameraInput::MouseMove(UiEvent& event) {
    Camera* camera = &g_camera;
    if (turning) {
        // The mouse's speed as a 60 Hz frame's motion, so that the turn and tilt rates go with the speed at any
        // frame rate (they went with each frame's motion, and so with the frame's length).
        const Point taken = g_cursor.TakeDrag();
        Point point = screen_layout::FromScreen(event.at);
        // The drag in layout units, kept as a fraction: a slow drag of a count or two still moves the camera.
        const float dragX = static_cast<float>(taken.x) * screen_layout::kLayoutPerCount;
        const float dragY = static_cast<float>(taken.y) * screen_layout::kLayoutPerCount;
        float across = dragX * kPerWidth;
        float along = dragY * kPerHeight;
        if (!ModifierHeld()) {
            // The turn and tilt speeds times the mouse direction factors.
            const float turnSpeed = MouseSpeed(options::g_options.cameraRotate) * options::g_options.cameraFlipX;
            camera->SetTurnRate(turnSpeed * across * 10.0f);
            float width = dragX < 0.0f ? -dragX : dragX;
            float height = dragY < 0.0f ? -dragY : dragY;
            // At rest the camera tilts only when the mouse moves more up or down than sideways.
            const float zero = 0.0f;
            if (Differs(camera->tiltTarget, zero) || Differs(camera->tiltRate, zero) || height > width) {
                const float tiltSpeed = MouseSpeed(options::g_options.cameraPitch) * options::g_options.cameraFlipY;
                camera->SetTiltRate(tiltSpeed * along * 500.0f);
            } else {
                camera->SetTiltRate(0.0f);
            }
        } else {
            float speed = MouseSpeed(options::g_options.cameraScroll);
            Vector3 direction{across * speed, along * speed, 0.0f};
            camera->Pan(direction);
        }
        camera_trace::Drag(taken, dragX, dragY);
        g_cursor.SetShape(CursorShape::Camera);
        event.handled = true;
        pressLayout = point;
        return;
    }
    // The width of the screen edges that pan (0: off).
    int margin = static_cast<const volatile int&>(options::g_options.cameraBorderScrollArea);
    if (margin > 0) {
        // The edges of the picture: the screen's, or the letterbox bars' inner ones.
        const Rect picture = g_interface.PictureRect();
        Edge edges = {};
        Vector3 direction{0.0f, 0.0f, 0.0f};
        if (event.at.x < picture.left + margin) {
            direction.x = -1.0f;
            edges = Edge::Left;
        } else if (event.at.x >= picture.right - margin) {
            direction.x = 1.0f;
            edges = Edge::Right;
        }
        if (event.at.y < picture.top + margin) {
            direction.y = -1.0f;
            edges |= Edge::Top;
        } else if (event.at.y >= picture.bottom - margin) {
            direction.y = 1.0f;
            edges |= Edge::Bottom;
        }
        if (!ModifierHeld()) {
            // The keys' pan goes on (KeyUp sets the same).
            float left = -x;
            float speed = KeyboardSpeed(options::g_options.cameraScroll);
            float forward = y * speed;
            left *= speed;
            direction.x = forward + direction.x;
            direction.y = left + direction.y;
        }
        g_cursor.SetShape(EdgeCursor(edges));
        camera->Pan(direction);
        if (edges != Edge{}) event.handled = true;
    }
    pressLayout = event.at;
}

void CameraInput::Deactivate() {
    turning = false;
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    g_cursor.SetHeld(0);
    g_camera.StopMotion();
}

void CameraInput::Wheel(UiEvent& event) {
    g_camera.ZoomBy(MouseSpeed(options::g_options.cameraZoom) * (event.delta * kWheelUnits) * kWheelStep);
}

// --- Ui camera keys ----------------------------------------------------------------------------------

// The battle camera's key press. See docs/notes/ui.md, section 3.
// Keys are mapped to characters through the scan-code: characters 5-10 set a
// direction (+0x20 x, +0x24 y, +0x28 z to +-1) and 13-16 restore camera views 0-3 (saved with Ctrl
// or Alt held: interface +0x7C / +0x7E). While a direction is held, the camera pans (or with
// a modifier turns and tilts) at the option speeds.

void CameraInput::KeyDown(UiEvent& event) {
    int character = g_keyActions[event.scan];
    switch (character) {
    case 5: x = 1.0f; event.handled = true; break;
    case 6: x = -1.0f; event.handled = true; break;
    case 7: y = -1.0f; event.handled = true; break;
    case 8: y = 1.0f; event.handled = true; break;
    case 9: z = -1.0f; event.handled = true; break;
    case 10: z = 1.0f; event.handled = true; break;
    case 13:
    case 14:
    case 15:
    case 16: {
        unsigned slot = static_cast<unsigned>(character - 13);
        if (ModifierHeld())
            g_camera.SaveView(slot);
        else
            g_camera.RestoreView(slot);
        event.handled = true;
        return;
    }
    default:
        break;
    }
    if (!event.handled) return;
    Vector3 direction = {0.0f, 0.0f, 0.0f};
    if (!ModifierHeld()) {
        // Pan along (y, -x) at the pan speed; no turning or tilting.
        float left = -x;
        float speed = KeyboardSpeed(options::g_options.cameraScroll);
        direction.x = y * speed;
        direction.y = left * speed;
        g_camera.Pan(direction);
        g_camera.SetTurnRate(0.0f);
        g_camera.SetZoomTarget(KeyboardSpeed(options::g_options.cameraZoom) * z);
        g_camera.SetTiltRate(0.0f);
        return;
    }
    // With a modifier: no panning; turn by y, tilt by x (times 50).
    g_camera.Pan(direction);
    g_camera.SetTurnRate(KeyboardSpeed(options::g_options.cameraRotate) * y);
    g_camera.SetZoomTarget(KeyboardSpeed(options::g_options.cameraZoom) * z);
    g_camera.SetTiltRate(KeyboardSpeed(options::g_options.cameraPitch) * x * 50.0f);
}
