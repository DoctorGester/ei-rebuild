#pragma once

// The battle camera's input (ui_camera_keys.cpp, ui_camera_input.cpp).

#include <cstddef>
#include <cstdint>

#include <windows.h>
#include "game_data.h"
#include "ui.h"

class CInterface3D;

// The battle camera's input side (0x2C bytes; one of the battle screen's extra input
// sides). Keys move the camera along x_ / y_ / z_ (set by the key-down, cleared by the key-up); the right button
// turns the view while held.
class CCameraInput final : public UiInputSide {
public:
    // The interface, not turning, the axes 0 (handled_ and the press points
    // left).
    explicit CCameraInput(CInterface3D* ui);
    // The deleting destructor: the base side's table back.
    ~CCameraInput() override;
    void Deactivate() override { OnDeactivate(); }                                         // 1
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }       // 4
    int RightDown(int b, int x, int y) override { return OnRightDown(b, x, y), 0; }       // 5
    int RightUp(int b, int x, int y) override { return OnRightUp(b, x, y), 0; }           // 6
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }           // 9
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }               // 11
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }        // 12

    void OnKeyDown(int key, int repeat, int flags);
    // A direction key released (actions 5-10 of
    // KeyActions) clears its axis and marks the key used; then, when used, the camera's motion is
    // set again from the axes: without Ctrl / Alt it pans along (y_, -x_) at the pan speed
    // (the speed constants multiplied, stored as float), turns and tilts not, and climbs at the climb speed
    // * z_; with one it does not pan, turns at the turn speed * y_, climbs, and tilts at the
    // tilt speed * x_.
    void OnKeyUp(int key, int repeat, int flags);
    // The right button went down: the point kept screen and layout
    // turning_, the mouse-drag motion cleared, the cursor held; the button used.
    void OnRightDown(int buttons, int x, int y);
    // Released: turning_ cleared, the camera stops turning, tilting
    // and panning, the motion cleared, the cursor let go and put back at the mouse position; used.
    void OnRightUp(int buttons, int x, int y);
    // (Rebirth-patched, reproducing WideScreen: the drag scaled without the margin.) The mouse moved.
    // While turning: the drag motion
    // (in layout units) turns the camera at the turn speed * (dx / 400) * 10 and
    // tilts it at the tilt speed * (dy / 300) * 500 (not tilting when it is at rest
    // and |dy| <= |dx|), or with Ctrl / Alt pans it along (dx / 400, dy / 300) at the pan speed.
    // The motion is cleared, the cursor takes shape 0x16, the event is used and the
    // layout point kept. Otherwise, when the edge margin is positive: the cursor within
    // the margin of a screen edge pans that way (with the keys' pan added unless Ctrl / Alt is
    // held) under the edge's arrow cursor, the event used when at an edge; the point is kept.
    void OnMouseMove(int buttons, int x, int y);
    // The input loses the screen: no turning, the axes cleared, the cursor let go,
    // the motion cleared and the camera stopped.
    void OnDeactivate();
    // The wheel zooms the camera by the zoom speed *
    // (the delta's low word) * the step count.
    BOOL OnWheel(int keys, int delta, int x, int y);

    POINT pressScreen_;              // +0x0C where the right button went down (screen)
    POINT pressLayout_;              // +0x14 the same in layout coordinates
    bool turning_;                   // +0x1C the right button is held
    std::uint8_t unused_1D[3];
    float x_;                        // +0x20 right (+1) / left (-1)
    float y_;                        // +0x24 forward / back
    float z_;                        // +0x28 up / down
};

// The key press handler is in ui_camera_keys.cpp.
