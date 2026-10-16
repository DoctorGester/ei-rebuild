// A unit's path told to stand where the unit is (005B1DE0, 005B1F10, 005B2050, 005B2190; thiscall
// on the AIPath): the remaining cells are dropped, the order reset, the target becomes the unit's
// cell (position * 1 / (0073FD18 * 0.5)) with the flags {reached, -, -, 1}, the excluded regions
// emptied, and the order starts at the unit's position facing the direction its orientation
// quaternion (+0x34) gives, negated (computed on the FPU in the original's order). Then the
// variants add a turn (005C7C00), a tracked object (005C7C20) or a turn with an end (005C7C40);
// 005B2190 starts at the order's own start time. See ai_map.h (AIPath) and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"
#include "map_object.h"
#include "originals_ai.h"
#include "lib/vecmath.h"

#include "ei/hook.h"

#include <cstdint>
#include "originals_library.h"

namespace {

// The facing direction from the quaternion {w, x, y, z} (the unit's +0x34): x = -2(yx - zw) (rounded
// once), y = -((ww - ((zz + xx) + yy)) + 2yy) and z = -2(yz + xw), the last two rounded to floats
// before the negation, as the original.
void FacingFromQuaternion(const float* q, float* out) {
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double across = y * x - z * w;
    const double squares = LengthSquaredUnrounded<Sum3::ZXY, double>(x, y, z);
    const float up = static_cast<float>((w * w - squares) + (y * y + y * y));
    const double along = Dot2DUnrounded<double>(z, x, y, w);
    const float forward = static_cast<float>(along + along);
    out[0] = static_cast<float>(-(across + across));
    out[1] = -up;
    out[2] = -forward;
}

// The order's follow-ups through their original addresses (ai_motion.cpp), the arguments as the
// caller's dwords.
__declspec(naked) void __fastcall OrderSetTurn(AIPath*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x005C7C00)
__declspec(naked) void __fastcall OrderSetTrack(AIPath*, void*, std::uint32_t, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x005C7C20)
__declspec(naked) void __fastcall OrderSetTurnUntil(AIPath*, void*, std::uint32_t, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x005C7C40)

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The common part; `time` is read after the reset (005B2190 passes the path's own +0x10).
void StandAt(AIPath* path, const int* time, std::uint8_t* unit) {
    path->cells.RemoveAll();
    path->Reset();
    *reinterpret_cast<std::uint32_t*>(&path->targetX) = 0;
    *reinterpret_cast<std::uint32_t*>(&path->targetY) = 0;
    path->reached = true;
    path->failed = false;
    path->flag6E = false;
    path->flag6F = true;
    PointArraySetSizeOriginal(&path->excludedBlocks, nullptr, 0, -1);
    path->smoothing = 0;
    path->reached = true;
    double scale = static_cast<double>(GameFloat(0x0073B84C)) /
                   (static_cast<double>(GameFloat(0x0073FD18)) * GameFloat(0x0073B86C));
    const CMapObject* object = reinterpret_cast<const CMapObject*>(unit);
    path->targetX = static_cast<float>(scale * object->position.x);
    path->targetY = static_cast<float>(scale * object->position.y);
    Vector3 facing;
    FacingFromQuaternion(&object->orientation.w, &facing.x);
    AIPathStartOriginal(path, nullptr, &object->position, &facing, *time);
}

} // namespace

// 005B1DE0 (RET 0x10).
void AIPath::StandAndTurn(int time, std::uint8_t* unit, std::uint32_t direction, std::uint32_t rate) {
    StandAt(this, &time, unit);
    OrderSetTurn(this, nullptr, direction, rate);
}

// 005B1F10 (RET 0x14).
void AIPath::StandAndTrack(int time, std::uint8_t* unit, std::uint32_t id, std::uint32_t from, std::uint32_t to) {
    StandAt(this, &time, unit);
    OrderSetTrack(this, nullptr, id, from, to);
}

// 005B2050 (RET 0x14).
void AIPath::StandAndTurnUntil(int time, std::uint8_t* unit, std::uint32_t direction, std::uint32_t a,
                               std::uint32_t b) {
    StandAt(this, &time, unit);
    OrderSetTurnUntil(this, nullptr, direction, a, b);
}

// 005B2190 (RET 4).
void AIPath::StandStill(std::uint8_t* unit) {
    StandAt(this, &motionOrder.startTime, unit);
}

EI_REPLACE_METHOD(0x005B1DE0, AIPath::StandAndTurn);
EI_REPLACE_METHOD(0x005B1F10, AIPath::StandAndTrack);
EI_REPLACE_METHOD(0x005B2050, AIPath::StandAndTurnUntil);
EI_REPLACE_METHOD(0x005B2190, AIPath::StandStill);
