// Two CUnit virtual slots giving points of the unit (vtable 0073EA8C, shared by CFakeUnit
// 0073BFE8): 52 the position predicted along its path (0052FCC0) and 53 the point its current
// animation acts on (0052FDF0). Registered as free functions (thiscall as fastcall; rule 4). See
// docs/notes/units.md ("Unit points").
#include "ai_motion.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_core.h"
#include "originals_units.h"

#include <cstdint>
#include <cstring>

namespace {

// The unit's world [00455950]; the motion's sample [005C7430 on +0x27C: world, order, time,
// turning, position (half-cells), direction, speed, cell x, cell y]; the animation entry of an
// animation index [00518F80 (index)]; the orientation's axes [0040D940 X, 0040F560 Y (out)]; the AI
// map's passability for a size class [005BA790 (class, point)]; the debug message [0044C470].
__declspec(naked) float* __fastcall AxisX(const void*, void*, float*) EI_ORIGINAL(0x0040D940)
float* __fastcall AxisY(const void* a0, void*, float* a2) { return QuaternionAxisY(a0, nullptr, a2); }
__declspec(naked) bool __fastcall IsPassableForClass(void*, void*, int, const float*) EI_ORIGINAL(0x005BA790)

constexpr int kOrderPath = 2;
constexpr float kCellScale = 1.0f;            // 0073E9B8 / 0073EBF0
constexpr float kHalf = 0.5f;                 // 0073B86C
const char* const kNoEntry = reinterpret_cast<const char*>(0x0078F918);   // "Can't find animation entry index..."

// The unit's virtual slot 55 (IsDead: +0x274).
bool IsDead(CUnit* unit) { return ObjectSlots(unit)->IsDead() != 0; }
void CopyPosition(float* out, const CUnit* unit) { std::memcpy(out, &unit->positionX, 12); }

} // namespace

// 0052FCC0 (slot 52, RET 8). A dead unit (slot 55) gives its action point (slot 53); otherwise a
// unit following a path order (kind 2) without flag 0x8000000 gives its motion's position at
// `time`, and any other its position.
float* __fastcall UnitPredictPosition(std::uint8_t* object, void*, float* out, int time) {
    auto* unit = reinterpret_cast<CUnit*>(object);
    // The controller's slot 16: its motion (the order in its first 0x20 bytes).
    AIPath* path = static_cast<CUnitController*>(unit->controller)->Path();
    if (IsDead(unit)) {
        ObjectSlots(unit)->AimPoint(reinterpret_cast<Vector3*>(out));
        return out;
    }
    auto* order = reinterpret_cast<MotionOrder*>(path);
    if (order->kind == kOrderPath && !(unit->flags_ & 0x8000000)) {
        float toCells = 1.0f / (kCellScale * kHalf);
        float position[3] = {unit->positionX, unit->positionY, unit->positionZ};
        position[0] = toCells * position[0];
        position[1] = toCells * position[1];
        bool turning;
        Vector3 direction;
        float speed;
        int cellX, cellY;
        MotionSample(unit->motion, nullptr, ObjectWorld(unit), order, static_cast<float>(time), &turning, position,
                     &direction.x, &speed, &cellX, &cellY);
        float toWorld = kCellScale * kHalf;
        out[0] = toWorld * position[0];
        out[1] = toWorld * position[1];
        std::memcpy(&out[2], &position[2], 4);
        return out;
    }
    CopyPosition(out, unit);
    return out;
}

// 0052FDF0 (slot 53, RET 4; returns `out` in EAX, which the callers use). For a dead unit (slot
// 55), the point the unit's current animation (+0x254) acts on: an animation entry of kind
// 0x1C0000 (bits 0x3C0000 of its key) offsets the position along the orientation by its direction
// bits (0x1C00000 of its flags): 0x400000 back (-Y), 0x800000 forward (+Y) by above[1], 0xC00000
// right (+X), 0x1000000 left (-X) by above[0]. The point is kept only where the AI map lets size
// class 3 stand (on the server); otherwise, and for a living unit, the position.
float* __fastcall UnitActionPoint(std::uint8_t* object, void*, float* out) {
    auto* unit = reinterpret_cast<CUnit*>(object);
    if (!IsDead(unit)) {
        CopyPosition(out, unit);
        return out;
    }
    auto index = static_cast<std::uint8_t>(unit->unknown_254);
    Vector3 offset = {0.0f, 0.0f, 0.0f};
    auto* entry = reinterpret_cast<const FigureAnimation*>(ObjectAnimationEntryOriginal(unit, nullptr, index));
    if (!entry) {
        DebugMessageOriginal(kNoEntry, static_cast<unsigned>(index), unit->prototypeName);
    } else if ((entry->key & 0x3C0000) == 0x1C0000) {
        std::uint32_t side = entry->flags & 0x1C00000;
        Vector3 axis;
        if (side == 0x800000) {
            AxisY(&unit->orientation, nullptr, &axis.x);
            float reach = unit->above.y;
            offset = axis * reach;
        } else if (side == 0x400000) {
            AxisY(&unit->orientation, nullptr, &axis.x);
            float reach = unit->above.y;
            offset = -axis * reach;
        } else if (side == 0xC00000) {
            AxisX(&unit->orientation, nullptr, &axis.x);
            float reach = unit->above.x;
            offset = axis * reach;
        } else if (side == 0x1000000) {
            AxisX(&unit->orientation, nullptr, &axis.x);
            float reach = unit->above.x;
            offset = -axis * reach;
        }
    }
    // The world's slot 26 (IsServer); the server's AI map.
    auto* world = static_cast<CWorldServer*>(ObjectWorld(unit));
    if (world->IsServer()) {
        float point[3] = {offset.x + unit->positionX, offset.y + unit->positionY, offset.z + unit->positionZ};
        if (!IsPassableForClass(world->AIMap(), nullptr, 3, point)) {
            CopyPosition(out, unit);
            return out;
        }
    }
    out[0] = offset.x + unit->positionX;
    out[1] = offset.y + unit->positionY;
    out[2] = offset.z + unit->positionZ;
    return out;
}

EI_REPLACE(0x0052FCC0, UnitPredictPosition);
EI_REPLACE(0x0052FDF0, UnitActionPoint);
