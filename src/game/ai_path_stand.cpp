// A unit's path told to stand where the unit is (four variants): the remaining cells are dropped, the order
// reset, the target becomes the unit's cell (position * cell scale) with the flags {reached, -, -, 1}, the
// excluded regions emptied, and the order starts at the unit's position facing the direction its orientation
// quaternion gives, negated (computed on the FPU in the original's order). Then the variants add a turn, a
// tracked object or a turn with an end; one starts at the order's own start time. See ai_map.h (AIPath) and
// docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"
#include "map_object.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace {

// The facing direction from the quaternion {w, x, y, z} of the unit: x = -2(yx - zw) (rounded once), y =
// -((ww - ((zz + xx) + yy)) + 2yy) and z = -2(yz + xw), the last two rounded to floats before the negation, as
// the original.
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

// The common part; `time` is read after the reset (one variant passes the path's own start time).
void StandAt(AIPath* path, const int* time, const CMapObject* object) {
    path->cells.clear();
    path->Reset();
    path->targetX = 0.0f;
    path->targetY = 0.0f;
    path->reached = true;
    path->failed = false;
    path->mayExcludeRegions = false;
    path->firstSearch = true;
    path->excludedBlocks.clear();
    path->smoothing = 0;
    path->reached = true;
    double scale = 1.0 /
                   (0.5f);
    path->targetX = static_cast<float>(scale * object->localPosition.x);
    path->targetY = static_cast<float>(scale * object->localPosition.y);
    Vector3 facing;
    FacingFromQuaternion(&object->localRotation.w, &facing.x);
    path->Start(object->localPosition, facing, *time);
}

} // namespace

void AIPath::StandAndTurn(int time, const CMapObject* unit, const Vector3& direction, std::uint32_t rateBits) {
    StandAt(this, &time, unit);
    SetTurn(direction, rateBits);
}

void AIPath::StandAndTrack(int time, const CMapObject* unit, int id, int from, int to) {
    StandAt(this, &time, unit);
    SetTrack(id, from, to);
}

void AIPath::StandAndTurnUntil(int time, const CMapObject* unit, const Vector3& direction, int fromTime, int toTime) {
    StandAt(this, &time, unit);
    SetTurnUntil(direction, fromTime, toTime);
}

void AIPath::StandStill(const CMapObject* unit) {
    StandAt(this, &motionOrder.startTime, unit);
}
