#pragma once

// A unit's motion: the order its controller gives (the header of AIPath) and the trajectory the
// unit follows (UnitMotion, CUnit + 0x27C), sampled every tick for the unit's position, facing and
// speed. Names provisional; layouts static_asserted below.

#include "ai_map.h"

#include <cstddef>
#include <cstdint>

// The arrays are mfc::CArray (lib/mfc.h).

// The order (MotionOrder, the first 0x20 bytes of AIPath) is in ai_map.h.
inline MotionOrder* OrderOf(AIPath* path) { return &path->motionOrder; }

// A point of the trajectory (0x1C bytes).
struct MotionPoint {
    float x, y;         // half-cell units
    float dirX, dirY;   // unit direction of travel
    float speed;        // half-cells per tick
    int cellX, cellY;   // path cell the point belongs to (-1 none)
};
static_assert(sizeof(MotionPoint) == 0x1C);

// A corner of the path the trajectory is built from (0x0C bytes): `cells` path cells lead to it.
struct MotionWaypoint {
    float x, y;
    int cells;
};
static_assert(sizeof(MotionWaypoint) == 0x0C);
// 005C6E30 (RET 8; ai_motion.cpp, A-W14). The constructor: the corner (x, y) (the floats moved as
// dwords), no cells. Returns the waypoint. UnitMotion::Build builds its corners in place.
MotionWaypoint* __fastcall MotionWaypointConstruct(MotionWaypoint* self, void*, std::uint32_t xBits, std::uint32_t yBits);

// c0 + c1 u + c2 u^2 + c3 u^3: one coordinate of a trajectory segment.
struct MotionCubic {
    float c0, c1, c2, c3;

    // 005C73B0. Hermite fit: value p0 / slope d0 at u = 0, value p1 / slope d1 at u = length.
    void Fit(float p0, float d0, float p1, float d1, float length);
    // 004E5660 (forwarder). The value at u, unrounded (ST0).
    double Value(float u);
    // 005C7410. The slope at u, unrounded (ST0).
    double Slope(float u);
};
static_assert(sizeof(MotionCubic) == 0x10);

struct UnitMotion {
    // 005C7430. Samples the motion of `order` at world time `time`: position (x, y) in half-cells,
    // direction (x, y, 0), speed (0.1 if unchanged) and the path cell reached (-1 if none).
    // `turning` is set while the unit only turns. Returns false when the order is complete.
    // `world` resolves object ids for tracking orders (its slot 0xA0/4).
    bool Sample(void* world, MotionOrder* order, float time, bool* turning, float* position, float* direction,
                float* speed, int* cellX, int* cellY);
    // 005C6FC0. Sample for a path order; `time` is relative to the order's start.
    bool SamplePath(float time, bool* turning, float* position, float* direction, float* speed, int* cellX,
                    int* cellY);
    // 005C67D0. Rebuilds the trajectory for `order` (an AIPath with its steps).
    void Build(AIPath* order);
    // 005C6270. Empties the trajectory. Returns 0 (EAX, as the original).
    int Clear();
    // 005C6750. Stores the path cells of the steps in the trajectory points.
    void AssignCells(AIPath* order);
    // 005C66C0. Splits every waypoint segment into points, one per path cell.
    void Subdivide();
    // 005C6370. Adds the points of the segment from `a` to `b` covering `cells` path cells.
    void SubdivideSegment(const MotionWaypoint* a, const MotionWaypoint* b, int cells, float startOffset,
                          float endOffset, int* stepIndex);
    // 005C6290. Slows the points down where the turn to the next one takes longer than the way.
    void LimitSpeeds(float turnRate);
    // 005C7860 / 005C79B0 (RET 4; the unit's save, unit_save.cpp). The points (count, then 0x1C
    // bytes each), the waypoints (0xC each), the step speeds (4 each), then +0x3C..+0x4C as dwords.
    // Loading sizes each array to the count read and clears `segment` (-1) first.
    void LoadState(CStream* stream);
    void SaveState(CStream* stream);

    mfc::CArray<MotionPoint> points;          // +0x00
    mfc::CArray<MotionWaypoint> waypoints;    // +0x14
    mfc::CArray<float> stepSpeeds;            // +0x28 per step: AIPath step speed (1/512 units)
    float speedScale;                       // +0x3C
    float baseTurnRate;                     // +0x40 radians per tick
    float turnRate;                         // +0x44
    float startAngle;                       // +0x48
    float turn;                             // +0x4C turn to make before moving, in [-pi, pi]
    float fromX, fromY;                     // +0x50 current segment
    float fromDirX, fromDirY;               // +0x58
    float toX, toY;                         // +0x60
    float toDirX, toDirY;                   // +0x68
    float length;                           // +0x70
    float speed;                            // +0x74
    MotionCubic curveX;                     // +0x78
    MotionCubic curveY;                     // +0x88
    float segmentStart;                     // +0x98 time the current segment starts / ends
    float segmentEnd;                       // +0x9C
    int segment;                            // +0xA0 index of its first point, -1 none
};
static_assert(offsetof(UnitMotion, speedScale) == 0x3C);
static_assert(offsetof(UnitMotion, fromX) == 0x50);
static_assert(offsetof(UnitMotion, curveX) == 0x78);
static_assert(offsetof(UnitMotion, segment) == 0xA0);
static_assert(sizeof(UnitMotion) == 0xA4);
