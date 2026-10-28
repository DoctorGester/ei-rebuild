// Two CUnit virtuals giving points of the unit: the position predicted along its
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

bool IsDead(CUnit* unit) { return unit->IsDead() != 0; }
void CopyPosition(Vector3* out, const CUnit* unit) { std::memcpy(out, &unit->localPosition_, sizeof(Vector3)); }

} // namespace

// A dead unit gives its action point; otherwise a unit following a path order (kind 2) without flag
// 0x8000000 gives its motion's position at `time`, and any other its position.
static Vector3* PredictPosition(CUnit* unit, Vector3* out, int time) {
    // The controller's motion virtual: its motion (the order in its first 0x20 bytes).
    PathOrder* path = unit->Controller()->Path();
    if (IsDead(unit)) {
        unit->AimPoint(out);
        return out;
    }
    MotionOrder* order = &path->motionOrder;
    if (order->kind == kOrderPath && !(unit->flags_ & 0x8000000)) {
        float toCells = 2.0f;
        Vector3 position = {unit->localPosition_.x, unit->localPosition_.y, unit->localPosition_.z};
        position.x = toCells * position.x;
        position.y = toCells * position.y;
        bool turning;
        Vector3 direction;
        float speed;
        int cellX, cellY;
        unit->motion.Sample(ObjectWorld(unit), order, static_cast<float>(time), &turning,
                               &position.x, &direction.x, &speed, &cellX, &cellY);
        float toWorld = 0.5f;
        out->x = toWorld * position.x;
        out->y = toWorld * position.y;
        std::memcpy(&out->z, &position.z, 4);
        return out;
    }
    CopyPosition(out, unit);
    return out;
}

// Returns `out` (the callers use it). For a dead unit, the point the unit's current animation acts on: an
// animation entry of kind 0x1C0000 (bits 0x3C0000 of its key) offsets the position along the orientation by
// its direction bits (0x1C00000 of its flags): 0x400000 back (-Y), 0x800000 forward (+Y) by above[1],
// 0xC00000 right (+X), 0x1000000 left (-X) by above[0]. The point is kept only where the AI map lets size
// class 3 stand (on the server); otherwise, and for a living unit, the position.
static Vector3* ActionPoint(CUnit* unit, Vector3* out) {
    if (!IsDead(unit)) {
        CopyPosition(out, unit);
        return out;
    }
    auto index = static_cast<std::uint8_t>(unit->animationEntry);
    Vector3 offset = {0.0f, 0.0f, 0.0f};
    const FigureAnimation* entry = unit->AnimationAt(index);
    if (!entry) {
        game::DebugLog(kNoEntry, static_cast<unsigned>(index), unit->name);
    } else if ((entry->key & 0x3C0000) == 0x1C0000) {
        std::uint32_t side = entry->flags & 0x1C00000;
        Vector3 axis;
        if (side == 0x800000) {
            unit->localRotation_.AxisY(&axis);
            float reach = unit->bounds.boxMax.y;
            offset = axis * reach;
        } else if (side == 0x400000) {
            unit->localRotation_.AxisY(&axis);
            float reach = unit->bounds.boxMax.y;
            offset = -axis * reach;
        } else if (side == 0xC00000) {
            unit->localRotation_.AxisX(&axis);
            float reach = unit->bounds.boxMax.x;
            offset = axis * reach;
        } else if (side == 0x1000000) {
            unit->localRotation_.AxisX(&axis);
            float reach = unit->bounds.boxMax.x;
            offset = -axis * reach;
        }
    }
    // The world's IsServer; the server's AI map.
    auto* world = unit->ServerWorld();
    if (world->IsServer()) {
        Vector3 point = {offset.x + unit->localPosition_.x, offset.y + unit->localPosition_.y, offset.z + unit->localPosition_.z};
        if (!world->aiMap_.IsPassableForClass(3, &point)) {
            CopyPosition(out, unit);
            return out;
        }
    }
    out->x = offset.x + unit->localPosition_.x;
    out->y = offset.y + unit->localPosition_.y;
    out->z = offset.z + unit->localPosition_.z;
    return out;
}

Vector3* CUnit::PositionAt(Vector3* out, int time) { return PredictPosition(this, out, time); }
Vector3* CUnit::AimPoint(Vector3* out) { return ActionPoint(this, out); }
