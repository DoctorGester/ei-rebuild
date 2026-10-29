// The battle camera's key input (an input side, constructed; its slot
// 9 is). See docs/notes/ui.md ("Camera keys").
// Keys are mapped to characters through the scan-code: characters 5-10 set a
// direction (+0x20 x, +0x24 y, +0x28 z to +-1) and 13-16 restore camera views 0-3 (saved with Ctrl
// or Alt held: interface +0x7C / +0x7E). While a direction is held, the camera pans (or with
// a modifier turns and tilts) at the option speeds.
#include "ui_camera_keys.h"
#include "options.h"

#include "ui.h"
#include "ui_float.h"

#include "camera.h"

#include "game_singletons.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

float GameFloat(const float& setting) { return static_cast<const volatile float&>(setting); }

// A camera motion's keyboard power times its speed, in double: the key handler multiplies the direction
// in with it and rounds to float once. (ui_camera_input.cpp's KeyboardSpeed rounds power * speed to float
// first, which gives different results: keep both.)
double KeyboardSpeedProduct(const options::CameraMotionOptions& motion) {
    return D(GameFloat(motion.powerKeyboard)) * D(GameFloat(motion.speed));
}

bool ModifierHeld(const Interface3D* ui) { return ui->ctrlDown || ui->altDown; }

} // namespace

void CameraInput::KeyDown(int, int, int flags) {
    int character = g_keyActions[flags & 0x1FF];
    switch (character) {
    case 5: x = 1.0f; handled = true; break;
    case 6: x = -1.0f; handled = true; break;
    case 7: y = -1.0f; handled = true; break;
    case 8: y = 1.0f; handled = true; break;
    case 9: z = -1.0f; handled = true; break;
    case 10: z = 1.0f; handled = true; break;
    case 13:
    case 14:
    case 15:
    case 16: {
        unsigned slot = static_cast<unsigned>(character - 13);
        if (ModifierHeld(ui))
            g_camera.SaveView(slot);
        else
            g_camera.RestoreView(slot);
        handled = true;
        return;
    }
    default:
        break;
    }
    if (!handled) return;
    Vector3 direction = {0.0f, 0.0f, 0.0f};
    if (!ModifierHeld(ui)) {
        // Pan along (y, -x) at the pan speed; no turning or tilting.
        float left = -x;
        float speed = static_cast<float>(KeyboardSpeedProduct(options::g_options.cameraScroll));
        direction.x = static_cast<float>(D(y) * D(speed));
        direction.y = static_cast<float>(D(left) * D(speed));
        g_camera.Pan(direction);
        g_camera.SetTurnRate(0.0f);
        g_camera.SetZoomTarget(static_cast<float>(KeyboardSpeedProduct(options::g_options.cameraZoom) * D(z)));
        g_camera.SetTiltRate(0.0f);
        return;
    }
    // With a modifier: no panning; turn by y, tilt by x (times 50).
    g_camera.Pan(direction);
    g_camera.SetTurnRate(static_cast<float>(KeyboardSpeedProduct(options::g_options.cameraRotate) * D(y)));
    g_camera.SetZoomTarget(static_cast<float>(KeyboardSpeedProduct(options::g_options.cameraZoom) * D(z)));
    g_camera.SetTiltRate(
        static_cast<float>(KeyboardSpeedProduct(options::g_options.cameraPitch) * D(x) * D(50.0f)));
}
