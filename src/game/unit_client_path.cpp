// A party unit's client controller (CUnitClientSpecific, class 0x3D) placing its unit on the
// motion it follows (00535850). See docs/notes/units.md ("Client path placement").
//
// Float maths is plain C++ (rewrite-guide rule 7): atan2, exp, cos and sin come from the C++
// library instead of FPATAN / F2XM1 / FCOS / FSIN, so a heading may rarely differ in its last bit
// (the two angles are rounded to float like the original's, which removed the common 1-ULP shift).
#include "core_transform.h"
#include "map_object_figures.h"
#include "unit.h"
#include "unit_client_models.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_core.h"
#include "originals_units.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// The controller's world [00455950]; the motion's sample [005C7430 on unit +0x27C: world, order,
// time, turning, position (half-cells), direction, speed, cell x, cell y]; the unit's ground height
// [00530280 (x, y)], its turn toward a direction [00401F00 (direction, flag)], its heading from a
// direction [00402220], its animation speed factor [005327D0 (animation)]; the orientation's
// facing axis [0040F560 on the quaternion (out)].
inline double __fastcall UnitGroundHeight(void* a0, void*, float a2, float a3) { return static_cast<CUnit*>((void*)a0)->GroundHeight(a2, a3); }
Vector3* __fastcall FacingAxis(const Quaternion* q, void*, Vector3* out) { return QuaternionAxisY(q, nullptr, out); }

constexpr float kCellScale = 1.0f;            // 0073EBF0
constexpr float kHalf = 0.5f;                 // 0073B86C
constexpr float kHalfTurn = 3.1416f;          // 0073CF80
constexpr float kDecay = -0.3f;               // 0073CDFC
constexpr float kAnimationRate = 15.0f;       // 0073C518
float FullTurn() { return *reinterpret_cast<const float*>(0x007AF330); }   // set at start-up

} // namespace

// 00535850 (RET 8; slot 41 passes (+0x34, 0); 00535B30 the tick's time and rate). Samples the
// motion at `time`, moves the unit there (on the ground, +0x320 above), snaps its facing once when
// +0x76 is set, then turns it: at once with unit flag 0x10000000, else blended toward the motion's
// direction with weight exp(-0.3 rate) on the old heading when `rate` > 0; finally the animation
// speed factor +0x178 from the motion's speed and the figure's current animation.
void __fastcall ClientPlaceOnPath(std::uint8_t* controller, void*, float time, float rate) {
    auto* self = reinterpret_cast<CUnitClientSpecific*>(controller);
    CUnit* unit = self->unit;
    if (unit->flags_ & 0x4000000) return;
    float toCells = 1.0f / (kCellScale * kHalf);
    Vector3 position = {unit->position.x, unit->position.y, unit->position.z};
    position.x = toCells * position.x;
    position.y = toCells * position.y;
    Vector3 direction;
    float speed;
    int cellX, cellY;
    bool turning;
    void* world = ObjectWorld(controller);
    MotionSample(unit->motion, nullptr, world, self->motionOrder, time, &turning, &position, &direction, &speed, &cellX,
                 &cellY);
    float toWorld = kCellScale * kHalf;
    position.x = toWorld * position.x;
    position.y = toWorld * position.y;
    position.z = static_cast<float>(UnitGroundHeight(unit, nullptr, position.x, position.y) + unit->heightOffset);
    static_cast<CWorld*>(ObjectWorld(controller))->MoveObject(unit, &position.x);   // the world's slot 41
    if (self->snapHeading) {
        self->snapHeading = 0;
        reinterpret_cast<CTransformObject*>(unit)->TurnUpTowards(direction, 0);
    }
    if (unit->flags_ & 0x10000000) {
        reinterpret_cast<CTransformObject*>(unit)->TurnUpTowards(direction, 1);
    } else if (!x87::IsNaN(rate) && rate > 0.0f) {   // FCOMP + TEST AH,0x41: only above zero
        Vector3 axis;
        FacingAxis(&unit->orientation, nullptr, &axis);
        // The original stores both angles as floats (FSTP). Left unrounded they shift the heading by
        // a float ULP in about one call in ten, and at a heading of +-pi (where 00402220 jumps from
        // one quaternion sign to the other) that flipped the orientation's sign once in the ui_hud run.
        float current = static_cast<float>(x87::fpatan(-static_cast<double>(axis.y), -static_cast<double>(axis.x)));
        float target = static_cast<float>(x87::fpatan(static_cast<double>(direction.y), static_cast<double>(direction.x)));
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
        double weight = x87::fexp(static_cast<double>(rate * kDecay));
        double heading = (1.0 - weight) * target + weight * current;
        direction.x = static_cast<float>(x87::fcos(heading));
        direction.y = static_cast<float>(x87::fsin(heading));
        direction.z = 0.0f;
        TransformSetHeading(reinterpret_cast<CTransformObject*>(unit), nullptr, &direction.x);
    }

    // The animation the figure plays (its animation state, CObject3D +0x98).
    const FigureAnimation* animation = unit->Object3D()->animation->current;
    if (!animation) {
        unit->animationSpeed = 1.0f;
        return;
    }
    float length;
    std::memcpy(&length, &animation->stride, sizeof(length));
    // FCOMP + TEST AH,0x40: equal to 0 (or unordered).
    if (x87::IsNaN(length) || length == 0.0f) {
        unit->animationSpeed = static_cast<float>(unit->ActionSpeed(animation));
        return;
    }
    // The database the animation is in: its index (read as a dword) back to the first one.
    int index;
    std::memcpy(&index, &animation->index, sizeof(index));
    const auto* database = reinterpret_cast<const AnimationDatabase*>(
        reinterpret_cast<const char*>(animation - index) - offsetof(AnimationDatabase, animations));
    float frames = 1.0f;
    float span = database->length;
    if (!(x87::IsNaN(span) || span == 0.0f))
        frames = ((database->end - database->start) * unit->scale.z + database->start) / span;
    double moved = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(direction.x, direction.y, direction.z));
    double numerator = moved * speed * kAnimationRate;
    double denominator = (1.0f / (kCellScale * kHalf)) * (static_cast<double>(frames) * length);
    unit->animationSpeed = static_cast<float>(numerator / denominator);
}

EI_REPLACE(0x00535850, ClientPlaceOnPath);

// 00535B20 (slot 41 MotionChanged; A-U12): the unit placed on its new path at the time it was last
// advanced to, without turning.
void __fastcall ClientMotionChanged(CUnitClientSpecific* self) {
    ClientPlaceOnPath(reinterpret_cast<std::uint8_t*>(self), nullptr, self->lastTime, 0.0f);
}
EI_REPLACE(0x00535B20, ClientMotionChanged);
