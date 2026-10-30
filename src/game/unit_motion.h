#pragma once

// A unit's trajectory (UnitMotion, CUnit::motion) and its parts: the points, the waypoints and the cubic
// segments, sampled every tick for the unit's position, facing and speed. The order it follows (MotionOrder /
// PathOrder) is in ai_map.h; ai_motion.h has the rest of the motion code. Kept apart so unit.h can hold the
// motion as a member (ai_map.h includes unit.h).

#include <cstddef>
#include <cstdint>
#include <vector>

class ByteStream;
class CWorld;
struct MotionOrder;
struct PathOrder;

// A point of the trajectory.
struct MotionPoint {
    float x, y;         // half-cell units
    float dirX, dirY;   // unit direction of travel
    float speed;        // half-cells per tick
    int cellX, cellY;   // path cell the point belongs to (-1 none)
};
static_assert(sizeof(MotionPoint) == 0x1C);

// A corner of the path the trajectory is built from: `cells` path cells lead to it.
struct MotionWaypoint {
    float x, y;
    int cells;
};
static_assert(sizeof(MotionWaypoint) == 0x0C);

// c0 + c1 u + c2 u^2 + c3 u^3: one coordinate of a trajectory segment.
struct MotionCubic {
    float c0, c1, c2, c3;

    // Hermite fit: value p0 / slope d0 at u = 0, value p1 / slope d1 at u = length.
    void Fit(float p0, float d0, float p1, float d1, float length);
    // The value at u.
    float Value(float u);
    // The slope at u.
    float Slope(float u);
};

struct UnitMotion {
    // Samples the motion of `order` at world time `time`: position (x, y) in half-cells,
    // direction (x, y, 0), speed (0.1 if unchanged) and the path cell reached (-1 if none).
    // `turning` is set while the unit only turns. Returns false when the order is complete.
    // `world` resolves object ids for tracking orders (its ObjectWithId).
    bool Sample(CWorld* world, MotionOrder* order, float time, bool* turning, float* position, float* direction,
                float* speed, int* cellX, int* cellY);
    // Sample for a path order; `time` is relative to the order's start.
    bool SamplePath(float time, bool* turning, float* position, float* direction, float* speed, int* cellX,
                    int* cellY);
    // Rebuilds the trajectory for `order` (a PathOrder with its steps).
    void Build(PathOrder* order);
    // Empties the trajectory. Returns 0.
    int Clear();
    // Stores the path cells of the steps in the trajectory points.
    void AssignCells(PathOrder* order);
    // Splits every waypoint segment into points, one per path cell.
    void Subdivide();
    // Adds the points of the segment from `a` to `b` covering `cells` path cells.
    void SubdivideSegment(const MotionWaypoint* a, const MotionWaypoint* b, int cells, float startOffset,
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
    float speedScale;
    float baseTurnRate;                     // radians per tick
    float turnRate;
    float startAngle;
    float turn;                             // turn to make before moving, in [-pi, pi]
    float fromX, fromY;                     // current segment
    float fromDirX, fromDirY;
    float toX, toY;
    float toDirX, toDirY;
    float length;
    float speed;
    MotionCubic curveX;
    MotionCubic curveY;
    float segmentStart;                     // time the current segment starts / ends
    float segmentEnd;
    int segment;                            // index of its first point, -1 none
};
