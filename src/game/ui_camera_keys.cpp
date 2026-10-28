// The battle camera's key input (an input side, constructed; its slot
// 9 is). See docs/notes/ui.md ("Camera keys").
// Keys are mapped to characters through the scan-code: characters 5-10 set a
// direction (+0x20 x, +0x24 y, +0x28 z to +-1) and 13-16 restore camera views 0-3 (saved with Ctrl
// or Alt held: interface +0x7C / +0x7E). While a direction is held, the camera pans (or with
// a modifier turns and tilts) at the option speeds.
#include "ui_camera_keys.h"
#include "options.h"

#include "ui.h"

#include "camera.h"

#include "game_singletons.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }
float GameFloat(const float& setting) { return static_cast<const volatile float&>(setting); }

bool ModifierHeld(const CInterface3D* ui) { return ui->ctrlDown_ || ui->altDown_; }

std::uint32_t Bits(float f) {
    std::uint32_t b;
    b = std::bit_cast<std::uint32_t>(f);
    return b;
}

} // namespace

void CCameraInput::OnKeyDown(int, int, int flags) {
    int character = g_keyActions[flags & 0x1FF];
    switch (character) {
    case 5: x_ = 1.0f; handled_ = true; break;
    case 6: x_ = -1.0f; handled_ = true; break;
    case 7: y_ = -1.0f; handled_ = true; break;
    case 8: y_ = 1.0f; handled_ = true; break;
    case 9: z_ = -1.0f; handled_ = true; break;
    case 10: z_ = 1.0f; handled_ = true; break;
    case 13:
    case 14:
    case 15:
    case 16: {
        unsigned slot = static_cast<unsigned>(character - 13);
        if (ModifierHeld(interface_))
            g_camera->SaveView(slot);
        else
            g_camera->RestoreView(slot);
        handled_ = true;
        return;
    }
    default:
        break;
    }
    if (!handled_) return;
    Vector3 direction = {0.0f, 0.0f, 0.0f};
    if (!ModifierHeld(interface_)) {
        // Pan along (y, -x) at the pan speed; no turning or tilting.
        float left = -x_;
        float speed = static_cast<float>(D(GameFloat(options::g_options->cameraScroll_.powerKeyboard)) * D(GameFloat(options::g_options->cameraScroll_.speed)));
        direction.x = static_cast<float>(D(y_) * D(speed));
        direction.y = static_cast<float>(D(left) * D(speed));
        g_camera->Pan(&direction);
        g_camera->SetTurnRate(0.0f);
        g_camera->SetZoomTarget(Bits(static_cast<float>(D(GameFloat(options::g_options->cameraZoom_.powerKeyboard)) * D(GameFloat(options::g_options->cameraZoom_.speed)) * D(z_))));
        g_camera->SetTiltRate(0.0f);
        return;
    }
    // With a modifier: no panning; turn by y, tilt by x (times 50).
    g_camera->Pan(&direction);
    g_camera->SetTurnRate(static_cast<float>(D(GameFloat(options::g_options->cameraRotate_.powerKeyboard)) * D(GameFloat(options::g_options->cameraRotate_.speed)) * D(y_)));
    g_camera->SetZoomTarget(Bits(static_cast<float>(D(GameFloat(options::g_options->cameraZoom_.powerKeyboard)) * D(GameFloat(options::g_options->cameraZoom_.speed)) * D(z_))));
    g_camera->SetTiltRate(
        static_cast<float>(D(GameFloat(options::g_options->cameraPitch_.powerKeyboard)) * D(GameFloat(options::g_options->cameraPitch_.speed)) * D(x_) * D(50.0f)));
}
