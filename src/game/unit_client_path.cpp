// A party unit's client controller (UnitClientSpecific, class 0x3D) placing its unit on the
// motion it follows. See docs/notes/units.md ("Client path placement").
// Float maths as the main thread's 24-bit x87 rounds it: atan2, exp, cos and sin through lib/pc24.h
// (FPATAN, VC6's inline exp, FCOS, FSIN).
#include "ai_motion.h"
#include "core_transform.h"
#include "map_object_figures.h"
#include "unit.h"
#include "unit_client_models.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kHalfTurn = 3.1416f;
constexpr float kDecay = -0.3f;
constexpr float kAnimationRate = 15.0f;
constexpr float kFullTurn = /*007AF330*/ 6.2831855f;   // a static initialiser: pi + pi

} // namespace

// Samples the motion at `time`, moves the unit there (on the ground, heightOffset above), snaps its facing
// once when snapHeading is set, then turns it: at once with MapObjectFlag::NoCasting, else blended toward the
// motion's direction with weight exp(-0.3 rate) on the old heading when `rate` > 0; finally animationSpeed
// from the motion's speed and the figure's current animation.
static void PlaceOnPathOf(UnitClientSpecific* self, float time, float rate) {
    Unit* unit = self->unit;
    if (unit->flags & MapObjectFlag::Preview) return;
    float toCells = 2.0f;
    Vector3 position = {unit->localPosition.x, unit->localPosition.y, unit->localPosition.z};
    position.x = toCells * position.x;
    position.y = toCells * position.y;
    Vector3 direction;
    float speed;
    int cellX, cellY;
    bool turning;
    World* world = self->OwningWorld();
    unit->motion.Sample(world, OrderOf(&self->motion), time, &turning, &position.x,
                           &direction.x, &speed, &cellX, &cellY);
    float toWorld = 0.5f;
    position.x = toWorld * position.x;
    position.y = toWorld * position.y;
    position.z = pc24::Add(unit->GroundHeight(position.x, position.y), unit->heightOffset);
    self->OwningWorld()->MoveObject(unit, &position.x);
    if (self->snapHeading) {
        self->snapHeading = 0;
        unit->TurnUpTowards(direction, 0);
    }
    if (unit->flags & MapObjectFlag::NoCasting) {
        unit->TurnUpTowards(direction, 1);
    } else if (rate > 0.0f) {   // only above zero (NaN not)
        Vector3 axis;
        unit->localRotation.AxisY(&axis);
        // Both angles are stored as floats. Left unrounded they shift the heading by a float ULP in about one
        // call in ten, and at a heading of +-pi (where jumps from one quaternion sign to the other) that
        // flipped the orientation's sign once in the ui_hud run.
        float current = pc24::Round(pc24::Atan2(-axis.y, -axis.x));
        float target = pc24::Round(pc24::Atan2(direction.y, direction.x));
        for (;;) {
            float gap = target - current;
            gap = gap < 0 ? -gap : gap;
            if (!(gap > kHalfTurn)) break;   // (a NaN gap ends the loop too)
            // Bring the smaller angle up by a full turn until they are within half a turn.
            if (target > current)
                current = kFullTurn + current;
            else
                target = kFullTurn + target;
        }
        float weight = pc24::Exp(rate * kDecay);
        float heading = (1.0f - weight) * target + weight * current;
        direction.x = pc24::Round(pc24::Cos(heading));
        direction.y = pc24::Round(pc24::Sin(heading));
        direction.z = 0.0f;
        unit->SetHeading(&direction.x);
    }

    // The animation the figure plays.
    const FigureAnimation* animation = unit->AsObject3D()->animation->current;
    if (!animation) {
        unit->animationSpeed = 1.0f;
        return;
    }
    float length;
    length = animation->stride;
    EI_NAN_AUDIT(length);
    if (length == 0.0f) {
        unit->animationSpeed = static_cast<float>(unit->ActionSpeed(animation));
        return;
    }
    // The database the animation is in: its index back to the first one.
    int index;
    std::memcpy(&index, &animation->index, sizeof(index));
    const auto* database = reinterpret_cast<const AnimationDatabase*>(
        reinterpret_cast<const char*>(animation - index) - offsetof(AnimationDatabase, animations));
    float frames = 1.0f;
    float span = database->length;
    EI_NAN_AUDIT(span);
    if (span != 0.0f)
        frames = ((database->end - database->start) * unit->scale.z + database->start) / span;
    float moved = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(direction.x, direction.y, direction.z));
    float numerator = moved * speed * kAnimationRate;
    float denominator = 2.0f * (frames * length);
    unit->animationSpeed = numerator / denominator;
}

void UnitClientSpecific::PlaceOnPath(float time, float rate) { PlaceOnPathOf(this, time, rate); }

void UnitClientSpecific::MotionChanged() { PlaceOnPath(lastTime, 0.0f); }
