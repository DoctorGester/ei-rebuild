// The battle camera's input side: key release, right-button turning, deactivation and the wheel
// (CameraInput, ui_camera_keys.h; the key press is in ui_camera_keys.cpp). See docs/notes/ui.md,
// section 2 ("").
#include "ui_camera_keys.h"
#include "options.h"

#include "camera.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ui_widescreen.h"

#include "lib/nan_audit.h"
#include "platform/cursor.h"
#include "lib/vecmath.h"
#include "game_singletons.h"

namespace {

// A camera motion's speed with the mouse: its power times its speed.
float MouseSpeed(const options::CameraMotionOptions& motion) { return motion.powerMouse * motion.speed; }
// The wheel's steps per delta unit (read from the exe, rule 9).
constexpr float kWheelStep = -0.008333334f;

bool ModifierHeld(const Interface3D* ui) { return ui->ctrlDown || ui->altDown; }

// Layout units to a part of the screen and the rates' scales
constexpr float kPerWidth = 0.0025f;
constexpr float kPerHeight = 0.0033333334f;

bool Differs(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a != b;
}

constexpr int kDragCursor = 0x16;
enum Edge : unsigned { kLeft = 1, kRight = 0x10, kTop = 0x100, kBottom = 0x1000 };

// The arrow cursor of the screen edges the mouse is at.
int EdgeCursor(unsigned edges) {
    switch (edges) {
    case kLeft: return 11;
    case kRight: return 7;
    case kTop: return 5;
    case kLeft | kTop: return 12;
    case kRight | kTop: return 6;
    case kBottom: return 9;
    case kLeft | kBottom: return 10;
    case kRight | kBottom: return 8;
    default: return 0;
    }
}

constexpr int kScanCodeMask = 0x1FF;

} // namespace


void CameraInput::KeyUp(int, int, int flags) {
    switch (g_keyActions[flags & kScanCodeMask]) {
    case 5:
    case 6:
        x = 0.0f;
        handled = true;
        break;
    case 7:
    case 8:
        y = 0.0f;
        handled = true;
        break;
    case 9:
    case 10:
        z = 0.0f;
        handled = true;
        break;
    default:
        break;
    }
    if (!handled) return;
    Camera* camera = &g_camera;
    if (!ModifierHeld(ui)) {
        float left = static_cast<float>(-x);
        float speed = KeyboardSpeed(options::g_options.cameraScroll);
        Vector3 direction = {static_cast<float>(y * speed), static_cast<float>(left * speed), 0.0f};
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

void CameraInput::RightDown(int, int atX, int atY) {
    pressScreen = Point{atX, atY};
    pressLayout = Point{atX, atY};
    turning = true;
    ui->ScalePointFromScreenEntry(&pressLayout);
    g_cursor.motion.y = 0;
    g_cursor.motion.x = 0;
    g_cursor.SetHeld(1);
    handled = true;
}

void CameraInput::RightUp(int, int, int) {
    turning = false;
    Camera* camera = &g_camera;
    camera->SetTurnRate(0.0f);
    camera->SetTiltRate(0.0f);
    Vector3 still = {0.0f, 0.0f, 0.0f};
    camera->Pan(still);
    int positionX = g_cursor.position.x;
    int positionY = g_cursor.position.y;
    g_cursor.motion.y = 0;
    g_cursor.motion.x = 0;
    g_cursor.SetHeld(0);
    platform::SetCursorPosition({positionX, positionY});
    handled = true;
}

void CameraInput::MouseMove(int, int atX, int atY) {
    Camera* camera = &g_camera;
    if (turning) {
        Point point{atX, atY};
        Point drag{g_cursor.motion.x, g_cursor.motion.y};
        ui->ScalePointFromScreenEntry(&point);
        // Rebirth's WideScreen scales the drag without the margin (addon's entry).
        ui->CallScalePointFromScreenNoMargin(&drag);
        float across = static_cast<float>(drag.x) * kPerWidth;
        float along = static_cast<float>(static_cast<float>(drag.y) * kPerHeight);
        if (!ModifierHeld(ui)) {
            // The turn and tilt speeds times the mouse direction factors.
            const float turnSpeed = MouseSpeed(options::g_options.cameraRotate) * options::g_options.cameraFlipX;
            camera->SetTurnRate(turnSpeed * across * 10.0f);
            float width = static_cast<float>(drag.x < 0 ? -drag.x : drag.x);
            float height = static_cast<float>(drag.y < 0 ? -drag.y : drag.y);
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
            Vector3 direction{static_cast<float>(across * speed), static_cast<float>(along * speed), 0.0f};
            camera->Pan(direction);
        }
        g_cursor.motion.y = 0;
        g_cursor.motion.x = 0;
        g_cursor.SetShape(kDragCursor);
        handled = true;
        pressLayout = point;
        return;
    }
    // The width of the screen edges that pan (0: off).
    int margin = static_cast<const volatile int&>(options::g_options.cameraBorderScrollArea);
    if (margin > 0) {
        unsigned edges = 0;
        Vector3 direction{0.0f, 0.0f, 0.0f};
        if (atX < margin) {
            direction.x = -1.0f;
            edges = kLeft;
        } else if (atX >= ui->width - margin) {
            direction.x = 1.0f;
            edges = kRight;
        }
        if (atY < margin) {
            direction.y = -1.0f;
            edges |= kTop;
        } else if (atY >= ui->height - margin) {
            direction.y = 1.0f;
            edges |= kBottom;
        }
        if (!ModifierHeld(ui)) {
            // The keys' pan goes on (KeyUp sets the same).
            float left = static_cast<float>(-x);
            float speed = KeyboardSpeed(options::g_options.cameraScroll);
            float forward = y * speed;
            left = static_cast<float>(left * speed);
            direction.x = static_cast<float>(forward + direction.x);
            direction.y = static_cast<float>(left + direction.y);
        }
        g_cursor.SetShape(EdgeCursor(edges));
        camera->Pan(direction);
        if (edges) handled = true;
    }
    pressLayout = Point{atX, atY};
}

void CameraInput::Deactivate() {
    turning = false;
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    g_cursor.SetHeld(0);
    g_cursor.motion.y = 0;
    g_cursor.motion.x = 0;
    g_camera.StopMotion();
}

void CameraInput::Wheel(int, int delta, int, int) {
    int steps = static_cast<short>(delta);
    g_camera.ZoomBy(MouseSpeed(options::g_options.cameraZoom) * static_cast<float>(steps) * kWheelStep);
}

