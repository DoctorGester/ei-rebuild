// The battle camera's input side: key release, right-button turning, deactivation and the wheel
// (CCameraInput, ui_camera_keys.h; the key press is in ui_camera_keys.cpp). See docs/notes/ui.md,
// section 2 ("005EADD0").
#include "ui_camera_keys.h"

#include "camera.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "lib/vecmath.h"
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

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool ModifierHeld(const CInterface3D* ui) { return ui->ctrlDown_ || ui->altDown_; }

// The mouse options: the drag's pan speed, its turn and tilt speeds (with the mouse factors
// 0079BD64 / 0079BD68) and the width of the screen edges that pan (0 = off).
float DragPanSpeed() { return Option(0x0079BDBC) * Option(0x0079BDB8); }
float DragTurnSpeed() { return Option(0x0079BDC8) * Option(0x0079BDC4) * Option(0x0079BD64); }
float DragTiltSpeed() { return Option(0x0079BDD4) * Option(0x0079BDD0) * Option(0x0079BD68); }
int EdgeMargin() { return *reinterpret_cast<const volatile int*>(0x0079BDAC); }
// Layout units to a part of the screen (0074015C 1 / 400, 00740204 1 / 300) and the rates' scales
// (0073BAAC 10, 0073BAB4 500).
float PerWidth() { return Option(0x0074015C); }
float PerHeight() { return Option(0x00740204); }

// FCOMP; TEST AH,0x40: unordered counts as equal.
bool Differs(float a, float b) { return a != b && !x87::IsNaN(a) && !x87::IsNaN(b); }

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

void LetCursorGo() {
    MouseDragY() = 0;
    MouseDragX() = 0;
    GameCursor()->SetHeld(0);
}

} // namespace

CCameraInput* CCameraInput::Construct(CInterface3D* ui) {
    interface_ = ui;
    vtable_ = Vtable();
    turning_ = false;
    y_ = 0.0f;
    x_ = 0.0f;
    z_ = 0.0f;
    return this;
}
EI_REPLACE_METHOD(0x005EACB0, CCameraInput::Construct);

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
        float left = static_cast<float>(-x_);
        float speed = static_cast<float>(PanSpeed());
        Vector3 direction = {static_cast<float>(y_ * speed), static_cast<float>(left * speed), 0.0f};
        camera->Pan(&direction);
        camera->SetTurnRate(0.0f);
        camera->SetZoomTarget(Bits(ClimbSpeed() * z_));
        camera->SetTiltRate(0.0f);
        return;
    }
    Vector3 still = {0.0f, 0.0f, 0.0f};
    camera->Pan(&still);
    camera->SetTurnRate(TurnSpeed() * y_);
    camera->SetZoomTarget(Bits(ClimbSpeed() * z_));
    camera->SetTiltRate(TiltSpeed() * x_);
}

void CCameraInput::OnRightDown(int, int x, int y) {
    pressScreen_ = POINT{x, y};
    pressLayout_ = POINT{x, y};
    turning_ = true;
    interface_->CallScalePointFromScreen(&pressLayout_);
    MouseDragY() = 0;
    MouseDragX() = 0;
    GameCursor()->SetHeld(1);
    handled_ = true;
}

void CCameraInput::OnRightUp(int, int, int) {
    turning_ = false;
    CCamera* camera = camera::TheCamera();
    camera->SetTurnRate(0.0f);
    camera->SetTiltRate(0.0f);
    Vector3 still = {0.0f, 0.0f, 0.0f};
    camera->Pan(&still);
    int x = MouseScreenX();
    int y = MouseScreenY();
    LetCursorGo();
    gameapi::SetCursorPos(x, y);
    handled_ = true;
}

void CCameraInput::OnMouseMove(int, int x, int y) {
    CCamera* camera = camera::TheCamera();
    if (turning_) {
        POINT point{x, y};
        POINT drag{MouseDragX(), MouseDragY()};
        interface_->CallScalePointFromScreen(&point);
        // Rebirth's WideScreen scales the drag without the margin (005EB249: addon's entry 005F7DFE).
        interface_->CallScalePointFromScreenNoMargin(&drag);
        float across = static_cast<float>(drag.x) * PerWidth();
        float along = static_cast<float>(static_cast<float>(drag.y) * PerHeight());
        if (!ModifierHeld(interface_)) {
            camera->SetTurnRate(DragTurnSpeed() * across * Option(0x0073BAAC));
            float width = static_cast<float>(drag.x < 0 ? -drag.x : drag.x);
            float height = static_cast<float>(drag.y < 0 ? -drag.y : drag.y);
            // At rest the camera tilts only when the mouse moves more up or down than sideways.
            const float zero = Option(0x0073B858);
            if (Differs(camera->tiltTarget_, zero) || Differs(camera->tiltRate_, zero) || height > width)
                camera->SetTiltRate(DragTiltSpeed() * along * Option(0x0073BAB4));
            else
                camera->SetTiltRate(0.0f);
        } else {
            float speed = static_cast<float>(DragPanSpeed());
            Vector3 direction{static_cast<float>(across * speed), static_cast<float>(along * speed), 0.0f};
            camera->Pan(&direction);
        }
        MouseDragY() = 0;
        MouseDragX() = 0;
        GameCursor()->SetShape(kDragCursor);
        handled_ = true;
        pressLayout_ = point;
        return;
    }
    int margin = EdgeMargin();
    if (margin > 0) {
        unsigned edges = 0;
        Vector3 direction{0.0f, 0.0f, 0.0f};
        if (x < margin) {
            direction.x = -1.0f;
            edges = kLeft;
        } else if (x >= interface_->width_ - margin) {
            direction.x = 1.0f;
            edges = kRight;
        }
        if (y < margin) {
            direction.y = -1.0f;
            edges |= kTop;
        } else if (y >= interface_->height_ - margin) {
            direction.y = 1.0f;
            edges |= kBottom;
        }
        if (!ModifierHeld(interface_)) {
            // The keys' pan goes on (OnKeyUp sets the same).
            float left = static_cast<float>(-x_);
            float speed = static_cast<float>(PanSpeed());
            float forward = y_ * speed;
            left = static_cast<float>(left * speed);
            direction.x = static_cast<float>(forward + direction.x);
            direction.y = static_cast<float>(left + direction.y);
        }
        GameCursor()->SetShape(EdgeCursor(edges));
        camera->Pan(&direction);
        if (edges) handled_ = true;
    }
    pressLayout_ = POINT{x, y};
}

void CCameraInput::OnDeactivate() {
    turning_ = false;
    x_ = 0.0f;
    y_ = 0.0f;
    z_ = 0.0f;
    GameCursor()->SetHeld(0);
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
void __fastcall CameraInputDeactivate(CCameraInput* self) { self->OnDeactivate(); }
} // namespace
EI_REPLACE_METHOD(0x005EB090, CCameraInput::OnKeyUp);
EI_REPLACE_METHOD(0x005EAD00, CCameraInput::OnRightDown);
EI_REPLACE_METHOD(0x005EAD50, CCameraInput::OnRightUp);
EI_REPLACE_METHOD(0x005EB210, CCameraInput::OnMouseMove);
EI_REPLACE(0x005EB580, CameraInputDeactivate);
EI_REPLACE_METHOD(0x005EB540, CCameraInput::OnWheel);
