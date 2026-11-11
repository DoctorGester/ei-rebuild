#pragma once

// The battle camera's input (ui_camera_input.cpp, ui_camera_input.cpp).

#include "game/app/options.h"
#include "game/ui/ui.h"
#include "lib/rect.h"

// A camera motion's speed with the keys: its keyboard power times its speed.
inline float KeyboardSpeed(const options::CameraMotionOptions& motion) { return motion.powerKeyboard * motion.speed; }

// The battle camera's input (one of the battle screen's extra inputs). Keys move the camera along
// x / y / z (set by the key-down, cleared by the key-up); the right button turns the view while held.
class CameraInput final : public UiInput {
public:
    void OnEvent(UiEvent& event) override;
    // A direction key (actions 5-10 of g_keyActions) sets its axis; actions 13-16 restore camera view 0-3,
    // or save it with Ctrl / Alt. Then, when used, the camera's motion is set from the axes as KeyUp does.
    void KeyDown(UiEvent& event);
    // A direction key released clears its axis and marks the key used; then, when used, the camera's motion
    // is set again from the axes: without Ctrl / Alt it pans along (y, -x) at the pan speed (the speed
    // options multiplied, stored as float), turns and tilts not, and climbs at the climb speed * z; with one
    // it does not pan, turns at the turn speed * y, climbs, and tilts at the tilt speed * x.
    void KeyUp(UiEvent& event);
    // The right button went down: the point kept in screen and layout coordinates, turning set, the cursor held
    // (its motion so far dropped); the button used.
    void RightDown(UiEvent& event);
    // Released: turning cleared, the camera stops turning, tilting and panning, the cursor let go and put back
    // at the mouse position; used.
    void RightUp(UiEvent& event);
    // Rebirth-patched (WideScreen: the drag scaled without the margin). While turning: the cursor's drag (its
    // speed as a 60 Hz frame's motion, Cursor::TakeDrag; in layout units) turns the camera at the turn speed *
    // (dx / 400) * 10 and tilts it at the tilt speed * (dy / 300) * 500 (not tilting when it is at rest and
    // |dy| <= |dx|), or with Ctrl / Alt pans it along (dx / 400, dy / 300) at the pan speed. The cursor takes
    // shape 0x16, the event is used and the layout point kept. Otherwise, when the edge margin is positive: the
    // cursor within the margin of an edge of the picture (Interface3D::PictureRect) pans that way (with the keys'
    // pan added unless Ctrl / Alt is held) under the edge's arrow cursor, the event used when at an edge; the
    // point is kept.
    void MouseMove(UiEvent& event);
    // The input loses the screen: no turning, the axes cleared, the cursor let go and the camera stopped.
    void Deactivate() override;
    // Whether the mouse pans the camera at the screen's edges: the border scroll area is above 0.
    bool PansAtEdges() const override { return options::g_options.cameraBorderScrollArea > 0; }
    // The wheel zooms the camera by the zoom speed * (the delta's low word) * the step size.
    void Wheel(UiEvent& event);

    Point pressScreen;               // where the right button went down (screen)
    Point pressLayout;               // the same in layout coordinates
    bool turning = false;            // the right button is held
    float x = 0.0f;                  // right (+1) / left (-1)
    float y = 0.0f;                  // forward / back
    float z = 0.0f;                  // up / down
};
