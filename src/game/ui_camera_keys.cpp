// The battle camera's key input (an input side, vtable 00740208, constructed by 005EACB0; its slot
// 9 is 005EADD0). See docs/notes/ui.md ("Camera keys").
//
// Keys are mapped to characters through the scan-code table 0079BF70: characters 5-10 set a
// direction (+0x20 x, +0x24 y, +0x28 z to +-1) and 13-16 restore camera views 0-3 (saved with Ctrl
// or Alt held: interface +0x7C / +0x7E). While a direction is held (+0x8), the camera pans (or with
// a modifier turns and tilts) at the option speeds 0079BDB8..0079BDE4.
#include "ui_camera_keys.h"

#include "ui.h"

#include "core_transform.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
CCamera* Camera() { return reinterpret_cast<CCamera*>(0x0079B2A8); }

struct CameraKeys {
    void* vtable;
    CInterface3D* ui;        // +0x04
    bool moving;             // +0x08 a direction is held
    std::uint8_t unknown_09[0x20 - 0x09];
    float x;                 // +0x20 right (+1) / left (-1)
    float y;                 // +0x24 forward / back
    float z;                 // +0x28 up / down

    void OnKeyDown(int key, int repeat, int flags);
};
static_assert(offsetof(CameraKeys, x) == 0x20);

bool ModifierHeld(const CInterface3D* ui) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(ui);
    return bytes[0x7C] != 0 || bytes[0x7E] != 0;   // Ctrl or Alt
}

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void CameraKeys::OnKeyDown(int, int, int flags) {
    int character = static_cast<signed char>(reinterpret_cast<const std::uint8_t*>(0x0079BF70)[flags & 0x1FF]);
    switch (character) {
    case 5: x = 1.0f; moving = true; break;
    case 6: x = -1.0f; moving = true; break;
    case 7: y = -1.0f; moving = true; break;
    case 8: y = 1.0f; moving = true; break;
    case 9: z = -1.0f; moving = true; break;
    case 10: z = 1.0f; moving = true; break;
    case 13:
    case 14:
    case 15:
    case 16: {
        unsigned slot = static_cast<unsigned>(character - 13);
        if (ModifierHeld(ui))
            Camera()->SaveView(slot);
        else
            Camera()->RestoreView(slot);
        moving = true;
        return;
    }
    default:
        break;
    }
    if (!moving) return;
    float direction[3] = {0.0f, 0.0f, 0.0f};
    if (!ModifierHeld(ui)) {
        // Pan along (y, -x) at the pan speed; no turning or tilting.
        float left = -x;
        float speed = Store(D(GameFloat(0x0079BDC0)) * D(GameFloat(0x0079BDB8)));
        direction[0] = Store(D(y) * D(speed));
        direction[1] = Store(D(left) * D(speed));
        Camera()->Pan(direction);
        Camera()->SetTurnRate(0.0f);
        Camera()->SetUnknownD0(Bits(Store(D(GameFloat(0x0079BDE4)) * D(GameFloat(0x0079BDDC)) * D(z))));
        Camera()->SetTiltRate(0.0f);
        return;
    }
    // With a modifier: no panning; turn by y, tilt by x (times 50).
    Camera()->Pan(direction);
    Camera()->SetTurnRate(Store(D(GameFloat(0x0079BDCC)) * D(GameFloat(0x0079BDC4)) * D(y)));
    Camera()->SetUnknownD0(Bits(Store(D(GameFloat(0x0079BDE4)) * D(GameFloat(0x0079BDDC)) * D(z))));
    Camera()->SetTiltRate(
        Store(D(GameFloat(0x0079BDD8)) * D(GameFloat(0x0079BDD0)) * D(x) * D(GameFloat(0x0074023C))));
}

} // namespace

void __fastcall CameraKeysOnKeyDown(void* self, void*, int key, int repeat, int flags) {
    static_cast<CameraKeys*>(self)->OnKeyDown(key, repeat, flags);
}
EI_REPLACE(0x005EADD0, CameraKeysOnKeyDown);
