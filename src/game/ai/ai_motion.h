#pragma once

// A unit's trajectory (UnitMotion, Unit::motion) and its parts: the points, the waypoints and the cubic
// segments, sampled every tick for the unit's position, facing and speed. The order it follows (MotionOrder /
// PathOrder) is in ai_map.h; ai_motion.h has the rest of the motion code. Kept apart so unit.h can hold the
// motion as a member (ai_map.h includes unit.h).

#include <cstddef>
#include <cstdint>
#include "lib/rect.h"
#include "lib/vecmath.h"

#include <vector>

class ByteStream;
class World;
struct MotionOrder;
struct PathOrder;

// A point of the trajectory.
struct MotionPoint {
    Vector2 position;   // half-cell units
    Vector2 direction;  // unit direction of travel
    float speed = 0.0f;  // half-cells per tick
    Point cell;         // path cell the point belongs to (-1, -1 none)
};
static_assert(sizeof(MotionPoint) == 0x1C);   // saved as its 28 bytes
constexpr bool NoPadding(const MotionPoint*) {
    return lib::FieldsFill<&MotionPoint::position, &MotionPoint::direction, &MotionPoint::speed, &MotionPoint::cell>();
}

// A corner of the path the trajectory is built from: `cells` path cells lead to it.
struct MotionWaypoint {
    Vector2 position;
    int cells = 0;
};
static_assert(sizeof(MotionWaypoint) == 0x0C);   // saved as its 12 bytes
constexpr bool NoPadding(const MotionWaypoint*) {
    return lib::FieldsFill<&MotionWaypoint::position, &MotionWaypoint::cells>();
}

// c0 + c1 u + c2 u^2 + c3 u^3: one coordinate of a trajectory segment.
struct MotionCubic {
    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;

    // Hermite fit: value p0 / slope d0 at u = 0, value p1 / slope d1 at u = length.
    void Fit(float p0, float d0, float p1, float d1, float length);
    // The value at u.
    float Value(float u);
    // The slope at u.
    float Slope(float u);
};

// What UnitMotion::Sample finds at one time.
struct MotionSample {
    bool alive = true;      // false when the order is complete
    bool turning = false;   // the unit only turns
    Vector2 position = {0.0f, 0.0f};    // in half-cells
    Vector3 direction = {0.0f, 0.0f, 0.0f};
    float speed = 0.1f;                 // 0.1 if unchanged
    Point cell = {-1, -1};              // the path cell reached (-1, -1 if none)
};

struct UnitMotion {
    // Samples the motion of `order` at world time `time`. `world` resolves object ids for tracking orders
    // (its ObjectWithId).
    MotionSample Sample(World* world, MotionOrder* order, float time);
    // Sample for a path order; `time` is relative to the order's start. `start`: the position while the
    // unit only turns (the order's).
    MotionSample SamplePath(float time, Vector2 start);
    // Rebuilds the trajectory for `order` (a PathOrder with its steps).
    void Build(PathOrder* order);
    // Empties the trajectory. Returns 0.
    int Clear();
    // Stores the path cells of the steps in the trajectory points.
    void AssignCells(PathOrder* order);
    // Splits every waypoint segment into points, one per path cell.
    void Subdivide();
    // Adds the points of the segment from `a` to `b` covering `cells` path cells.
    void SubdivideSegment(const MotionWaypoint* from, const MotionWaypoint* to, int cells, float startOffset,
                          float endOffset, int* stepIndex);
    // Slows the points down where the turn to the next one takes longer than the way.
    void LimitSpeeds(float turnRate);
    // The points (count, then 0x1C bytes each), the waypoints (0xC each), the step speeds (4 each), then
    // +0x3C..+0x4C as dwords. Loading sizes each array to the count read and clears `segment` (-1) first.
    void LoadState(ByteStream* stream);
    void SaveState(ByteStream* stream);

    std::vector<MotionPoint> points;
    std::vector<MotionWaypoint> waypoints;
    std::vector<float> stepSpeeds;            // per step: AIPath step speed (1/512 units)
    float speedScale = 0.0f;
    float baseTurnRate = 0.0f;              // radians per tick
    float turnRate = 0.0f;
    float startAngle = 0.0f;
    float turn = 0.0f;                      // turn to make before moving, in [-pi, pi]
    Vector2 segmentFrom, segmentFromDirection;  // current segment: its first and last point
    Vector2 segmentTo, segmentToDirection;
    float length = 0.0f;
    float speed = 0.0f;
    MotionCubic curveX;
    MotionCubic curveY;
    float segmentStart = 0.0f;              // time the current segment starts / ends
    float segmentEnd = 0.0f;
    int segment = 0;                        // index of its first point, -1 none
};

// A unit's motion: the order its controller gives (MotionOrder, the header of AIPath; ai_map.h) and
// the trajectory the unit follows (UnitMotion, Unit + 0x27C; ai/ai_motion.h), sampled every tick
// for the unit's position, facing and speed. Names provisional.

#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"

// The order (MotionOrder, the first 0x20 bytes of PathOrder) is in ai_map.h.
inline MotionOrder* OrderOf(PathOrder* path) { return &path->motionOrder; }
