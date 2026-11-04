#pragma once

// The battle camera's input (ui_camera_input.cpp, ui_camera_input.cpp).

#include "options.h"
#include "ui.h"
#include "lib/rect.h"

// A camera motion's speed with the keys: its keyboard power times its speed.
inline float KeyboardSpeed(const options::CameraMotionOptions& motion) { return motion.powerKeyboard * motion.speed; }

// The battle camera's input side (one of the battle screen's extra input sides). Keys move the camera along
// x / y / z (set by the key-down, cleared by the key-up); the right button turns the view while held.
class CameraInput final : public UiInputSide {
public:
    explicit CameraInput(Interface3D* ui) : UiInputSide(ui) {}

    // A direction key (actions 5-10 of g_keyActions) sets its axis; actions 13-16 restore camera view 0-3,
    // or save it with Ctrl / Alt. Then, when used, the camera's motion is set from the axes as KeyUp does.
    void KeyDown(int key, int scan, bool repeat) override;
    // A direction key released clears its axis and marks the key used; then, when used, the camera's motion
    // is set again from the axes: without Ctrl / Alt it pans along (y, -x) at the pan speed (the speed
    // options multiplied, stored as float), turns and tilts not, and climbs at the climb speed * z; with one
    // it does not pan, turns at the turn speed * y, climbs, and tilts at the tilt speed * x.
    void KeyUp(int key, int scan) override;
    // The right button went down: the point kept in screen and layout coordinates, turning set, the cursor held
    // (its motion so far dropped); the button used.
    void RightDown(Point point) override;
    // Released: turning cleared, the camera stops turning, tilting and panning, the cursor let go and put back
    // at the mouse position; used.
    void RightUp(Point point) override;
    // Rebirth-patched (WideScreen: the drag scaled without the margin). While turning: the cursor's drag (its
    // speed as a 60 Hz frame's motion, Cursor::TakeDrag; in layout units) turns the camera at the turn speed *
    // (dx / 400) * 10 and tilts it at the tilt speed * (dy / 300) * 500 (not tilting when it is at rest and
    // |dy| <= |dx|), or with Ctrl / Alt pans it along (dx / 400, dy / 300) at the pan speed. The cursor takes
    // shape 0x16, the event is used and the layout point kept. Otherwise, when the edge margin is positive: the
    // cursor within the margin of an edge of the picture (Interface3D::PictureRect) pans that way (with the keys'
    // pan added unless Ctrl / Alt is held) under the edge's arrow cursor, the event used when at an edge; the
    // point is kept.
    void MouseMove(Point point) override;
    // The input loses the screen: no turning, the axes cleared, the cursor let go and the camera stopped.
    void Deactivate() override;
    // The wheel zooms the camera by the zoom speed * (the delta's low word) * the step size.
    void Wheel(int delta, Point point) override;

    Point pressScreen;               // where the right button went down (screen)
    Point pressLayout;               // the same in layout coordinates
    bool turning = false;            // the right button is held
    float x = 0.0f;                  // right (+1) / left (-1)
    float y = 0.0f;                  // forward / back
    float z = 0.0f;                  // up / down
};
