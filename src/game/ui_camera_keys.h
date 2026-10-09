#pragma once

// The battle camera's input (ui_camera_keys.cpp, ui_camera_input.cpp).

#include <cstddef>
#include <cstdint>

#include <windows.h>

class CInterface3D;

// The battle camera's input side (vtable 00740208, 0x2C bytes, constructed by 005EACB0; one of the
// battle screen's extra input sides). Keys move the camera along x_ / y_ / z_ (set by the key-down,
// cleared by the key-up); the right button turns the view while held.
class CCameraInput {
public:
    static void** Vtable() { return reinterpret_cast<void**>(std::uintptr_t(0x00740208)); }
    // 005EACB0 (RET 4; ui_camera_input.cpp). Constructor: vtable 00740208, the interface, not
    // turning, the axes 0 (handled_ and the press points left). Returns this.
    CCameraInput* Construct(CInterface3D* ui);
    // 005EADD0 (slot 9, RET 0xC; ui_camera_keys.cpp).
    void OnKeyDown(int key, int repeat, int flags);
    // 005EB090 (slot 11, RET 0xC; ui_camera_input.cpp). A direction key released (actions 5-10 of
    // KeyActions) clears its axis and marks the key used; then, when used, the camera's motion is
    // set again from the axes: without Ctrl / Alt it pans along (y_, -x_) at the pan speed
    // (0079BDC0 * 0079BDB8, stored as float), turns and tilts not, and climbs at 0079BDE4 * 0079BDDC
    // * z_; with one it does not pan, turns at 0079BDCC * 0079BDC4 * y_, climbs, and tilts at
    // 0079BDD8 * 0079BDD0 * x_.
    void OnKeyUp(int key, int repeat, int flags);
    // 005EAD00 (slot 5, RET 0xC). The right button went down: the point kept (screen and layout
    // (005F7E00)), turning_, the mouse-drag motion cleared, the cursor held; the button used.
    void OnRightDown(int buttons, int x, int y);
    // 005EAD50 (slot 6, RET 0xC). Released: turning_ cleared, the camera stops turning, tilting
    // and panning, the motion cleared, the cursor let go and put back at the mouse position; used.
    void OnRightUp(int buttons, int x, int y);
    // 005EB580 (slot 1). The input loses the screen: no turning, the axes cleared, the cursor let go,
    // the motion cleared and the camera stopped (0040F430).
    void OnDeactivate();
    // 005EB540 (slot 12, RET 0x10, returns 1). The wheel zooms the camera by 0079BDE0 * 0079BDDC *
    // (the delta's low word) * 00740240 steps.
    BOOL OnWheel(int keys, int delta, int x, int y);

    void** vtable_;
    CInterface3D* interface_;        // +0x04
    bool handled_;                   // +0x08 the input used the event
    std::uint8_t unknown_09[3];
    POINT pressScreen_;              // +0x0C where the right button went down (screen)
    POINT pressLayout_;              // +0x14 the same in layout coordinates
    bool turning_;                   // +0x1C the right button is held
    std::uint8_t unknown_1D[3];
    float x_;                        // +0x20 right (+1) / left (-1)
    float y_;                        // +0x24 forward / back
    float z_;                        // +0x28 up / down
};
static_assert(offsetof(CCameraInput, pressLayout_) == 0x14);
static_assert(offsetof(CCameraInput, x_) == 0x20);
static_assert(sizeof(CCameraInput) == 0x2C);

// 005EADD0 (thiscall on the input object, RET 0xC; slot 9 of vtable 00740208): see
// ui_camera_keys.cpp.
void __fastcall CameraKeysOnKeyDown(void* self, void* unused, int key, int repeat, int flags);
