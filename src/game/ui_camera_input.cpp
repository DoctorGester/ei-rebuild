// The battle camera's input side: key release, right-button turning, deactivation and the wheel
// (CCameraInput, ui_camera_keys.h; the key press is in ui_camera_keys.cpp). See docs/notes/ui.md,
// section 2 ("005EADD0").
#include "ui_camera_keys.h"

#include "camera.h"
#include "ui.h"
#include "ui_cursor.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "originals_ui.h"

#include <cstring>

namespace {

// The camera speed options (floats in the game's data; the camera keys multiply two of them).
float Option(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
float PanSpeed() { return Option(0x0079BDC0) * Option(0x0079BDB8); }
float TurnSpeed() { return Option(0x0079BDCC) * Option(0x0079BDC4); }
float TiltSpeed() { return Option(0x0079BDD8) * Option(0x0079BDD0); }
float ClimbSpeed() { return Option(0x0079BDE4) * Option(0x0079BDDC); }
float ZoomSpeed() { return Option(0x0079BDE0) * Option(0x0079BDDC); }
// 00740240: the wheel's steps per delta unit (read from the exe, rule 9).
float WheelStep() { return Option(0x00740240); }

// The original keeps these products in float stack slots: rounded through a volatile store.
float StoreFloat(float value) {
    volatile float stored = value;
    return stored;
}

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool ModifierHeld(const CInterface3D* ui) { return ui->ctrlDown_ || ui->altDown_; }

constexpr int kScanCodeMask = 0x1FF;

void LetCursorGo() {
    MouseDragY() = 0;
    MouseDragX() = 0;
    CursorSetHeldOriginal(GameCursor(), nullptr, 0);
}

} // namespace

void CCameraInput::OnKeyUp(int, int, int flags) {
    switch (KeyActions()[flags & kScanCodeMask]) {
    case 5:
    case 6:
        x_ = 0.0f;
        handled_ = true;
        break;
    case 7:
    case 8:
        y_ = 0.0f;
        handled_ = true;
        break;
    case 9:
    case 10:
        z_ = 0.0f;
        handled_ = true;
        break;
    default:
        break;
    }
    if (!handled_) return;
    CCamera* camera = camera::TheCamera();
    if (!ModifierHeld(interface_)) {
        float left = StoreFloat(-x_);
        float speed = StoreFloat(PanSpeed());
        float direction[3] = {StoreFloat(y_ * speed), StoreFloat(left * speed), 0.0f};
        camera->Pan(direction);
        camera->SetTurnRate(0.0f);
        camera->SetUnknownD0(Bits(ClimbSpeed() * z_));
        camera->SetTiltRate(0.0f);
        return;
    }
    float still[3] = {0.0f, 0.0f, 0.0f};
    camera->Pan(still);
    camera->SetTurnRate(TurnSpeed() * y_);
    camera->SetUnknownD0(Bits(ClimbSpeed() * z_));
    camera->SetTiltRate(TiltSpeed() * x_);
}

void CCameraInput::OnRightDown(int, int x, int y) {
    pressScreen_ = POINT{x, y};
    pressLayout_ = POINT{x, y};
    turning_ = true;
    interface_->CallScalePointFromScreen(&pressLayout_);
    MouseDragY() = 0;
    MouseDragX() = 0;
    CursorSetHeldOriginal(GameCursor(), nullptr, 1);
    handled_ = true;
}

void CCameraInput::OnRightUp(int, int, int) {
    turning_ = false;
    CCamera* camera = camera::TheCamera();
    camera->SetTurnRate(0.0f);
    camera->SetTiltRate(0.0f);
    float still[3] = {0.0f, 0.0f, 0.0f};
    camera->Pan(still);
    int x = MouseScreenX();
    int y = MouseScreenY();
    LetCursorGo();
    gameapi::SetCursorPos(x, y);
    handled_ = true;
}

void CCameraInput::OnDeactivate() {
    turning_ = false;
    x_ = 0.0f;
    y_ = 0.0f;
    z_ = 0.0f;
    CursorSetHeldOriginal(GameCursor(), nullptr, 0);
    MouseDragY() = 0;
    MouseDragX() = 0;
    camera::TheCamera()->StopMotion();
}

BOOL CCameraInput::OnWheel(int, int delta, int, int) {
    int steps = static_cast<short>(delta);
    camera::TheCamera()->ZoomBy(ZoomSpeed() * static_cast<float>(steps) * WheelStep());
    return TRUE;
}

namespace {
void __fastcall CameraInputKeyUp(CCameraInput* self, void*, int key, int repeat, int flags) {
    self->OnKeyUp(key, repeat, flags);
}
void __fastcall CameraInputRightDown(CCameraInput* self, void*, int buttons, int x, int y) {
    self->OnRightDown(buttons, x, y);
}
void __fastcall CameraInputRightUp(CCameraInput* self, void*, int buttons, int x, int y) {
    self->OnRightUp(buttons, x, y);
}
void __fastcall CameraInputDeactivate(CCameraInput* self) { self->OnDeactivate(); }
BOOL __fastcall CameraInputWheel(CCameraInput* self, void*, int keys, int delta, int x, int y) {
    return self->OnWheel(keys, delta, x, y);
}
} // namespace
EI_REPLACE(0x005EB090, CameraInputKeyUp);
EI_REPLACE(0x005EAD00, CameraInputRightDown);
EI_REPLACE(0x005EAD50, CameraInputRightUp);
EI_REPLACE(0x005EB580, CameraInputDeactivate);
EI_REPLACE(0x005EB540, CameraInputWheel);
