// Two Unit virtuals giving points of the unit: the position predicted along its
// path and the point its current animation acts on. See docs/notes/units.md ("Unit points").
#include "ai_map.h"
#include "ai_motion.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kOrderPath = 2;
const char* const kNoEntry = "Can't find animation entry index\n %i\n for \"%s\".\n";

void CopyPosition(Vector3* out, const Unit* unit) { *out = unit->localPosition; }

} // namespace

// A dead unit gives its action point; otherwise a unit following a path order (kind 2) without flag
// 0x8000000 gives its motion's position at `time`, and any other its position.
static Vector3* PredictPosition(Unit* unit, Vector3* out, int time) {
    // The controller's motion virtual: its motion (the order in its first 0x20 bytes).
    PathOrder* path = unit->Controller()->Path();
    if (unit->IsDead()) {
        unit->AimPoint(out);
        return out;
    }
    MotionOrder* order = &path->motionOrder;
    if (order->kind == kOrderPath && !(unit->flags & MapObjectFlag::Planned)) {
        float toCells = 2.0f;
        Vector3 position = {unit->localPosition.x, unit->localPosition.y, unit->localPosition.z};
        position.x = toCells * position.x;
        position.y = toCells * position.y;
        bool turning;
        Vector3 direction;
        float speed;
        int cellX, cellY;
        unit->motion.Sample(unit->OwningWorld(), order, static_cast<float>(time), &turning,
                               &position.x, &direction.x, &speed, &cellX, &cellY);
        float toWorld = 0.5f;
        out->x = toWorld * position.x;
        out->y = toWorld * position.y;
        out->z = position.z;
        return out;
    }
    CopyPosition(out, unit);
    return out;
}

// Returns `out` (the callers use it). For a dead unit, the point the unit's current animation acts on: a death
// animation offsets the position along the orientation away from its BlowSide: Back -Y, Front +Y by the
// box's max Y, MinusX +X, PlusX -X by its max X. The point is kept only where the AI map lets size
// class 3 stand (on the server); otherwise, and for a living unit, the position.
static Vector3* ActionPoint(Unit* unit, Vector3* out) {
    if (!unit->IsDead()) {
        CopyPosition(out, unit);
        return out;
    }
    auto index = static_cast<std::uint8_t>(unit->animationEntry);
    Vector3 offset = {0.0f, 0.0f, 0.0f};
    const FigureAnimation* entry = unit->AnimationAt(index);
    if (!entry) {
        game::DebugLog(kNoEntry, static_cast<unsigned>(index), unit->name);
    } else if (entry->Kind() == AnimationKind::Death) {
        BlowSide side = entry->Side();
        Vector3 axis;
        if (side == BlowSide::Front) {
            unit->localRotation.AxisY(&axis);
            float reach = unit->bounds.boxMax.y;
            offset = axis * reach;
        } else if (side == BlowSide::Back) {
            unit->localRotation.AxisY(&axis);
            float reach = unit->bounds.boxMax.y;
            offset = -axis * reach;
        } else if (side == BlowSide::MinusX) {
            unit->localRotation.AxisX(&axis);
            float reach = unit->bounds.boxMax.x;
            offset = axis * reach;
        } else if (side == BlowSide::PlusX) {
            unit->localRotation.AxisX(&axis);
            float reach = unit->bounds.boxMax.x;
            offset = -axis * reach;
        }
    }
    // The world's IsServer; the server's AI map.
    auto* world = unit->ServerWorld();
    if (world->IsServer()) {
        Vector3 point = {offset.x + unit->localPosition.x, offset.y + unit->localPosition.y, offset.z + unit->localPosition.z};
        if (!world->aiMap.IsPassableForClass(3, point)) {
            CopyPosition(out, unit);
            return out;
        }
    }
    out->x = offset.x + unit->localPosition.x;
    out->y = offset.y + unit->localPosition.y;
    out->z = offset.z + unit->localPosition.z;
    return out;
}

Vector3* Unit::PositionAt(Vector3* out, int time) { return PredictPosition(this, out, time); }
Vector3* Unit::AimPoint(Vector3* out) { return ActionPoint(this, out); }
