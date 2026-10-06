// Checks for the battle camera's input side (src/game/ui_camera_input.cpp).
//
// Side by side: the handlers write only the input object's fields, the camera's motion fields, the
// mouse-drag motion (007C7C60) and the cursor's held byte (and put the cursor back with
// SetCursorPos, harmless twice). All are copied, the original runs, its result is copied and the
// old bytes put back, ours runs; the copies are compared dword by dword, floats within 4 ULPs (the
// rewrite uses plain float maths). Scenarios: the battle screen's key releases (camera, ui_hud
// keys: OnKeyUp), the escape menu opening over it (ui_screens: OnDeactivate), right clicks on the
// view (ui_battle_commands' `clickunit ... r`: OnRightDown / OnRightUp) and ui_hud's wheel
// (OnWheel, when no panel takes it).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_camera_keys.h"
#include "../../src/game/ui_cursor.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uintptr_t kCamera = 0x0079B2A8;
constexpr std::size_t kCameraDwords = 0x1E0 / 4;   // up to the end of the saved views
constexpr std::size_t kInputDwords = sizeof(CCameraInput) / 4;

struct State {
    std::uint32_t camera[kCameraDwords - 1];
    std::uint32_t input[kInputDwords - 1];
    int dragX;
    int dragY;
    std::uint32_t held;

    void Read(const CCameraInput* object) {
        std::memcpy(camera, reinterpret_cast<const std::uint8_t*>(kCamera) + 4, sizeof(camera));
        std::memcpy(input, reinterpret_cast<const std::uint8_t*>(object) + 4, sizeof(input));
        dragX = MouseDragX();
        dragY = MouseDragY();
        held = reinterpret_cast<const std::uint8_t*>(GameCursor())[0x90];
    }
    void Write(CCameraInput* object) const {
        std::memcpy(reinterpret_cast<std::uint8_t*>(kCamera) + 4, camera, sizeof(camera));
        std::memcpy(reinterpret_cast<std::uint8_t*>(object) + 4, input, sizeof(input));
        MouseDragX() = dragX;
        MouseDragY() = dragY;
        reinterpret_cast<std::uint8_t*>(GameCursor())[0x90] = static_cast<std::uint8_t>(held);
    }
};

bool Near(std::uint32_t a, std::uint32_t b) {
    if (a == b) return true;
    float fa, fb;
    std::memcpy(&fa, &a, 4);
    std::memcpy(&fb, &b, 4);
    return ei::verify::NearlyEqual(fa, fb, 4, 1e-6f);
}

// The first dword that differs beyond float rounding (camera offsets from 4, the input's from
// 0x1004, 0x2000 the drag motion and the cursor), or -1.
int FirstDifference(const State& a, const State& b) {
    for (std::size_t i = 0; i < kCameraDwords - 1; ++i)
        if (!Near(a.camera[i], b.camera[i])) return static_cast<int>(4 + i * 4);
    for (std::size_t i = 0; i < kInputDwords - 1; ++i)
        if (!Near(a.input[i], b.input[i])) return 0x1004 + static_cast<int>(i * 4);
    if (a.dragX != b.dragX || a.dragY != b.dragY || a.held != b.held) return 0x2000;
    return -1;
}

template <typename Original, typename Ours>
void SideBySide(const char* name, CCameraInput* self, int detail, Original original, Ours ours) {
    static State before, expected, actual;
    before.Read(self);
    original();
    expected.Read(self);
    before.Write(self);
    ours();
    actual.Read(self);
    int at = FirstDifference(expected, actual);
    ei::verify::Check(name, true, at == -1, "%X: first difference at %X (0x1000+: input, 0x2000: cursor)", detail, at);
}

using EventFn = void(__fastcall*)(CCameraInput*, void*, int, int, int);
void* s_keyUp;
void* s_rightDown;
void* s_rightUp;
void* s_deactivate;
void* s_wheel;

} // namespace

static void __fastcall KeyUpVerify(CCameraInput* self, void*, int key, int repeat, int flags) {
    SideBySide(
        "CCameraInput::OnKeyUp", self, flags & 0x1FF,
        [&] { reinterpret_cast<EventFn>(s_keyUp)(self, nullptr, key, repeat, flags); },
        [&] { self->OnKeyUp(key, repeat, flags); });
}
// 005EB090: MOV EAX,[ESP+0xC]; SUB ESP,0x10 (4 + 3 bytes).
EI_VERIFY(0x005EB090, KeyUpVerify, 7, s_keyUp);

static void __fastcall RightDownVerify(CCameraInput* self, void*, int buttons, int x, int y) {
    SideBySide(
        "CCameraInput::OnRightDown", self, x,
        [&] { reinterpret_cast<EventFn>(s_rightDown)(self, nullptr, buttons, x, y); },
        [&] { self->OnRightDown(buttons, x, y); });
}
// 005EAD00: MOV EDX,[ESP+0xC]; PUSH ESI (4 + 1 bytes).
EI_VERIFY(0x005EAD00, RightDownVerify, 5, s_rightDown);

static void __fastcall RightUpVerify(CCameraInput* self, void*, int buttons, int x, int y) {
    SideBySide(
        "CCameraInput::OnRightUp", self, x,
        [&] { reinterpret_cast<EventFn>(s_rightUp)(self, nullptr, buttons, x, y); },
        [&] { self->OnRightUp(buttons, x, y); });
}
// 005EAD50: SUB ESP,8; PUSH EBX; PUSH ESI (3 + 1 + 1 bytes).
EI_VERIFY(0x005EAD50, RightUpVerify, 5, s_rightUp);

static void __fastcall DeactivateVerify(CCameraInput* self) {
    SideBySide(
        "CCameraInput::OnDeactivate", self, 0,
        [&] { reinterpret_cast<void(__fastcall*)(CCameraInput*)>(s_deactivate)(self); },
        [&] { self->OnDeactivate(); });
}
// 005EB580: PUSH EBX; XOR EBX,EBX; MOV byte ptr [ECX+0x1C],BL (1 + 2 + 3 bytes).
EI_VERIFY(0x005EB580, DeactivateVerify, 6, s_deactivate);

static BOOL __fastcall WheelVerify(CCameraInput* self, void*, int keys, int delta, int x, int y) {
    using WheelFn = BOOL(__fastcall*)(CCameraInput*, void*, int, int, int, int);
    BOOL expected = 0;
    BOOL actual = 0;
    SideBySide(
        "CCameraInput::OnWheel", self, delta,
        [&] { expected = reinterpret_cast<WheelFn>(s_wheel)(self, nullptr, keys, delta, x, y); },
        [&] { actual = self->OnWheel(keys, delta, x, y); });
    ei::verify::Check("CCameraInput::OnWheel", expected, actual, "result");
    return actual;
}
// 005EB540: MOVSX EAX,word ptr [ESP+8] (5 bytes).
EI_VERIFY(0x005EB540, WheelVerify, 5, s_wheel);
