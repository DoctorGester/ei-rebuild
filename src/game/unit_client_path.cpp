// A party unit's client controller (CUnitClientSpecific, class 0x3D) placing its unit on the
// motion it follows (00535850). See docs/notes/units.md ("Client path placement").
//
// Float maths is plain C++ (rewrite-guide rule 7): atan2, exp, cos and sin come from the C++
// library instead of FPATAN / F2XM1 / FCOS / FSIN, so a heading may rarely differ in its last bit
// (the two angles are rounded to float like the original's, which removed the common 1-ULP shift).
#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cmath>
#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// The controller's world [00455950]; the motion's sample [005C7430 on unit +0x27C: world, order,
// time, turning, position (half-cells), direction, speed, cell x, cell y]; the unit's ground height
// [00530280 (x, y)], its turn toward a direction [00401F00 (direction, flag)], its heading from a
// direction [00402220], its animation speed factor [005327D0 (animation)]; the orientation's
// facing axis [0040F560 on the quaternion (out)].
__declspec(naked) std::uint8_t* __fastcall ObjectWorld(void*) EI_ORIGINAL(0x00455950)
__declspec(naked) bool __fastcall MotionSample(void*, void*, void*, void*, float, bool*, float*, float*, float*, int*,
                                               int*) EI_ORIGINAL(0x005C7430)
__declspec(naked) double __fastcall UnitGroundHeight(void*, void*, float, float) EI_ORIGINAL(0x00530280)
__declspec(naked) void __fastcall UnitTurnTowards(void*, void*, const float*, int) EI_ORIGINAL(0x00401F00)
__declspec(naked) void __fastcall UnitFaceDirection(void*, void*, const float*) EI_ORIGINAL(0x00402220)
__declspec(naked) double __fastcall UnitAnimationSpeed(void*, void*, const void*) EI_ORIGINAL(0x005327D0)
__declspec(naked) float* __fastcall FacingAxis(const void*, void*, float*) EI_ORIGINAL(0x0040F560)

constexpr int kSlotMoveObject = 0xA4 / 4;     // world: (object, position)
constexpr float kCellScale = 1.0f;            // 0073EBF0
constexpr float kHalf = 0.5f;                 // 0073B86C
constexpr float kHalfTurn = 3.1416f;          // 0073CF80
constexpr float kDecay = -0.3f;               // 0073CDFC
constexpr float kAnimationRate = 15.0f;       // 0073C518
float FullTurn() { return *reinterpret_cast<const float*>(0x007AF330); }   // set at start-up

// Rounds an x87 intermediate to float, as the original's FSTP into a float temporary does. Under
// /fp:fast a cast to float alone leaves the FPATAN result unrounded in the register.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

} // namespace

// 00535850 (RET 8; slot 41 passes (+0x34, 0); 00535B30 the tick's time and rate). Samples the
// motion at `time`, moves the unit there (on the ground, +0x320 above), snaps its facing once when
// +0x76 is set, then turns it: at once with unit flag 0x10000000, else blended toward the motion's
// direction with weight exp(-0.3 rate) on the old heading when `rate` > 0; finally the animation
// speed factor +0x178 from the motion's speed and the figure's current animation.
void __fastcall ClientPlaceOnPath(std::uint8_t* controller, void*, float time, float rate) {
    std::uint8_t* unit = At<std::uint8_t*>(controller, 0x1C);
    if (At<std::uint32_t>(unit, 8) & 0x4000000) return;
    float toCells = 1.0f / (kCellScale * kHalf);
    float position[3] = {At<float>(unit, 0x1C), At<float>(unit, 0x20), At<float>(unit, 0x24)};
    position[0] = toCells * position[0];
    position[1] = toCells * position[1];
    float direction[3];
    float speed;
    int cellX, cellY;
    bool turning;
    std::uint8_t* world = ObjectWorld(controller);
    MotionSample(unit + 0x27C, nullptr, world, controller + 0x38, time, &turning, position, direction, &speed, &cellX,
                 &cellY);
    float toWorld = kCellScale * kHalf;
    position[0] = toWorld * position[0];
    position[1] = toWorld * position[1];
    position[2] = static_cast<float>(UnitGroundHeight(unit, nullptr, position[0], position[1]) + At<float>(unit, 0x320));
    ei::VCall<void>(ObjectWorld(controller), kSlotMoveObject, unit, position);
    if (At<std::uint8_t>(controller, 0x76)) {
        At<std::uint8_t>(controller, 0x76) = 0;
        UnitTurnTowards(unit, nullptr, direction, 0);
    }
    if (At<std::uint32_t>(unit, 8) & 0x10000000) {
        UnitTurnTowards(unit, nullptr, direction, 1);
    } else if (!x87::IsNaN(rate) && rate > 0.0f) {   // FCOMP + TEST AH,0x41: only above zero
        float axis[3];
        FacingAxis(unit + 0x34, nullptr, axis);
        // The original stores both angles as floats (FSTP). Left unrounded they shift the heading by
        // a float ULP in about one call in ten, and at a heading of +-pi (where 00402220 jumps from
        // one quaternion sign to the other) that flipped the orientation's sign once in the ui_hud run.
        float current = Store(std::atan2(-static_cast<double>(axis[1]), -static_cast<double>(axis[0])));
        float target = Store(std::atan2(static_cast<double>(direction[1]), static_cast<double>(direction[0])));
        for (;;) {
            float gap = target - current;
            gap = gap < 0 ? -gap : gap;
            if (x87::IsNaN(gap) || gap <= kHalfTurn) break;
            // Bring the smaller angle up by a full turn until they are within half a turn.
            if (!x87::IsNaN(target) && !x87::IsNaN(current) && target > current)
                current = FullTurn() + current;
            else
                target = FullTurn() + target;
        }
        double weight = std::exp(static_cast<double>(rate * kDecay));
        double heading = (1.0 - weight) * target + weight * current;
        direction[0] = static_cast<float>(std::cos(heading));
        direction[1] = static_cast<float>(std::sin(heading));
        direction[2] = 0.0f;
        UnitFaceDirection(unit, nullptr, direction);
    }

    void* animation = At<void*>(At<void*>(unit, 0x98), 0x40);
    if (!animation) {
        At<float>(unit, 0x178) = 1.0f;
        return;
    }
    float length = At<float>(animation, 0x24);
    // FCOMP + TEST AH,0x40: equal to 0 (or unordered).
    if (x87::IsNaN(length) || length == 0.0f) {
        At<float>(unit, 0x178) = static_cast<float>(UnitAnimationSpeed(unit, nullptr, animation));
        return;
    }
    std::uint8_t* entry = static_cast<std::uint8_t*>(animation) - At<int>(animation, 0x10) * 0x58 - 0x2C;
    float frames = 1.0f;
    float span = At<float>(entry, 0x24);
    if (!(x87::IsNaN(span) || span == 0.0f))
        frames = ((At<float>(entry, 0x28) - At<float>(entry, 0x20)) * At<float>(unit, 0xCC) + At<float>(entry, 0x20)) / span;
    double moved = std::sqrt((static_cast<double>(direction[2]) * direction[2] + static_cast<double>(direction[1]) * direction[1]) +
                             static_cast<double>(direction[0]) * direction[0]);
    double numerator = moved * speed * kAnimationRate;
    double denominator = (1.0f / (kCellScale * kHalf)) * (static_cast<double>(frames) * length);
    At<float>(unit, 0x178) = static_cast<float>(numerator / denominator);
}

EI_REPLACE(0x00535850, ClientPlaceOnPath);
