// A unit's motion: the order header of AIPath and the trajectory the unit follows. The trajectory is rebuilt
// when a path is advanced (Build) and sampled every tick by the unit's movement (Sample). Floating point
// follows the original's x87 code: `double` for values the original keeps on the FPU stack,
// static_cast<float> where it stores to a float.
#include "ai_motion.h"
#include "core_streams.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include "map_object.h"
#include "world.h"
#include "ai_map.h"

#include <concepts>

namespace {

float AbsBits(float f) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(f);
    bits &= 0x7FFFFFFF;
    f = std::bit_cast<float>(bits);
    return f;
}

float AsFloat(std::uint32_t bits) {
    float f;
    f = std::bit_cast<float>(bits);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float AsFloat(T) = delete;


// The turn direction factors (-1, 0, 1). Read through volatile so the compiler cannot fold 0 * x to 0 (the
// multiplication keeps NaN and -0).
const volatile float kMinusOne = -1.0f;
const volatile float kZero = 0.0f;
const volatile float kOne = 1.0f;

constexpr float kPi = 3.14159274f;
constexpr double kHalfCellScale = 2.0;      // 1 / (half a cell)
constexpr float kSpeedUnit = 0.001953125f;  // 1/512
constexpr float kMinLength = 9.99999975e-06f;

// The direction of a step to a neighbouring cell (ai_map.h's DirectionSteps: 1..8, 0 for the cell itself).
int Direction(const PathCell& from, const PathCell& to) {
    return DirectionSteps()[(to.y - from.y) * 3 + to.x - from.x];
}
int DirectionDelta(int a, int b) {
    int d = a - b;
    if (d < 0) d = -d;
    if (d > 4) d = 8 - d;
    return d;
}

void FaceAngle(float angle, float* direction) {
    direction[0] = static_cast<float>(x87::fcos(angle));
    direction[1] = static_cast<float>(x87::fsin(angle));
}

// Turning in place: the facing after `elapsed` of turning `turn` from `start` at `rate`; `final`
// when the turn takes no longer than that. Used by the order kinds 1 and 4.
double TurnAngle(const UnitMotion* m, float elapsed, bool* final) {
    float ratio = AbsBits(static_cast<float>(static_cast<double>(m->turn) / m->turnRate));
    EI_NAN_AUDIT(ratio, elapsed);
    if (ratio < elapsed) {
        *final = true;
        return static_cast<double>(m->startAngle) + m->turn;
    }
    *final = false;
    EI_NAN_AUDIT(m->turn);
    float sign = m->turn < 0.0f ? kMinusOne : (m->turn > 0.0f ? kOne : kZero);
    return static_cast<double>(sign) * elapsed * m->turnRate + m->startAngle;
}

// Brings `turn` (stored as float) into [-pi, pi]: the first comparison is on the unrounded difference, the
// later ones on each unrounded step.
void NormalizeTurn(UnitMotion* m, double turn) {
    m->turn = static_cast<float>(turn);
    const double twoPi = static_cast<double>(kPi) + kPi;
    if (turn > kPi) {
        double v;
        do {
            v = static_cast<double>(m->turn) - twoPi;
            m->turn = static_cast<float>(v);
        } while (v > kPi);
    }
    const float negPi = -kPi;
    if (negPi > m->turn) {
        double v;
        do {
            v = twoPi + m->turn;
            m->turn = static_cast<float>(v);
            EI_NAN_AUDIT(v);
        } while (v < negPi);   // ends on a NaN (the original's did not)
    }
}

MotionWaypoint Waypoint(double x, double y) { return {static_cast<float>(x), static_cast<float>(y), 0}; }

} // namespace

// --- The order (AIPath header) --------------------------------------------------------------------

int PathOrder::Reset() {
    MotionOrder* order = OrderOf(this);
    order->kind = 0;
    order->x = 0.0f;
    order->y = 0.0f;
    order->arg5 = 0;
    order->arg6 = 0;
    order->arg7 = 0;
    order->angle = 0.0f;
    order->startTime = 0;
    steps.clear();
    return 0;
}

bool PathOrder::Start(const Vector3* position, const Vector3* direction, int time) {
    MotionOrder* order = OrderOf(this);
    double dx = direction->x;
    float dy = direction->y;
    double length = std::sqrt(LengthSquared2DUnrounded<double>(dy, dx));
    double angle = length > 0.0 ? x87::fpatan(dy / length, dx / length) : 0.0;
    double x = kHalfCellScale * position->x;
    float y = static_cast<float>(kHalfCellScale * position->y);
    EI_NAN_AUDIT(order->angle, angle, x, order->x, order->y, y);
    bool same = order->kind == 0 && order->angle == angle && x == order->x && order->y == y;
    order->x = static_cast<float>(x);
    order->angle = static_cast<float>(angle);
    order->kind = 0;
    order->y = y;
    order->startTime = time;
    steps.clear();
    return !same;
}

void PathOrder::SetTurn(const Vector3* direction, std::uint32_t rateBits) {
    MotionOrder* order = OrderOf(this);
    order->kind = 1;
    float angle = static_cast<float>(x87::fpatan(direction->y, direction->x));
    order->arg6 = rateBits;
    order->arg5 = std::bit_cast<std::uint32_t>(angle);
}

void PathOrder::SetTrack(int id, int from, int to) {
    MotionOrder* order = OrderOf(this);
    order->arg5 = static_cast<std::uint32_t>(id);
    order->kind = 3;
    order->arg6 = static_cast<std::uint32_t>(from);
    order->arg7 = static_cast<std::uint32_t>(to);
}

void PathOrder::SetTurnUntil(const Vector3* direction, int arg6, int arg7) {
    MotionOrder* order = OrderOf(this);
    order->kind = 4;
    order->arg7 = static_cast<std::uint32_t>(arg7);
    float angle = static_cast<float>(x87::fpatan(direction->y, direction->x));
    order->arg6 = static_cast<std::uint32_t>(arg6);
    order->arg5 = std::bit_cast<std::uint32_t>(angle);
}

void PathOrder::SetTarget(float x, float y) {
    MotionOrder* order = OrderOf(this);
    order->kind = 2;
    order->arg5 = std::bit_cast<std::uint32_t>(x);
    order->arg6 = std::bit_cast<std::uint32_t>(y);
}

void PathOrder::ForgetObject(int id) {
    MotionOrder* order = OrderOf(this);
    if (order->kind == 3 && order->arg5 == static_cast<std::uint32_t>(id)) order->arg5 = 0xFFFFFFFF;
}

void PathOrder::ShiftTime(int delta) {
    MotionOrder* order = OrderOf(this);
    order->startTime += delta;
    if (order->kind == 3) {
        order->arg6 += static_cast<std::uint32_t>(delta);
        order->arg7 += static_cast<std::uint32_t>(delta);
    }
}

int PathOrder::SetStepSpeeds(std::uint16_t speed) {
    for (auto node = steps.begin(); node != steps.end(); ++node) node->flags = speed;
    return 0;
}

Vector3* PathOrder::Destination(Vector3* out) {
    const MotionOrder* order = OrderOf(this);
    double scale = 0.5;     // half a cell
    if (order->kind != 2) {
        out->x = static_cast<float>(scale * order->x);
        out->z = 0.0f;
        out->y = static_cast<float>(scale * order->y);
    } else {
        out->x = static_cast<float>(scale * AsFloat(order->arg5));
        out->z = 0.0f;
        out->y = static_cast<float>(scale * AsFloat(order->arg6));
    }
    return out;
}

void AIPath::Clear() {
    cells.clear();
    Reset();
    targetX = 0.0f;
    targetY = 0.0f;
    reached = true;
    failed = false;
    mayExcludeRegions = false;
    firstSearch = true;
    lib::FreeStorage(excludedBlocks);
    smoothing = 0;
}

// --- The trajectory --------------------------------------------------------------------------------

// A copy of CubicSegment::Evaluate: Horner's scheme, unrounded.
double MotionCubic::Value(float u) { return ((u * c3 + c2) * u + c1) * u + c0; }

void MotionCubic::Fit(float p0, float d0, float p1, float d1, float length) {
    double inverse = 1.0f / length;
    x87::CopyBits(c0, p0);
    x87::CopyBits(c1, d0);
    double half = ((static_cast<double>(p1) - p0) - static_cast<double>(d0) * length) * inverse;
    half = half + half;
    double cubic = ((static_cast<double>(d1) - d0) - half) * inverse * inverse;
    c3 = static_cast<float>(cubic);
    double scaled = cubic * length * length;
    scaled = scaled + scaled;
    c2 = static_cast<float>((half - scaled) * inverse * 0.5f);
}

double MotionCubic::Slope(float u) {
    double c2x2 = static_cast<double>(c2) + c2;
    return (static_cast<double>(u) * c3 * 3.0f + c2x2) * u + c1;
}

int UnitMotion::Clear() {
    segment = -1;
    points.clear();
    waypoints.clear();
    stepSpeeds.clear();
    return 0;
}

bool UnitMotion::Sample(CWorld* world, MotionOrder* order, float time, bool* turning, float* position,
                        float* direction, float* speedOut, int* cellX, int* cellY) {
    *cellX = -1;
    *cellY = -1;
    *speedOut = 0.1f;
    direction[2] = 0.0f;
    *turning = false;
    double start = order->startTime;
    time = time >= start ? time : static_cast<float>(start);
    x87::CopyBits(position[0], order->x);
    x87::CopyBits(position[1], order->y);

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
        FaceAngle(order->angle, direction);
        return true;
    case 1: {
        *turning = true;
        bool final;
        double angle = TurnAngle(this, static_cast<float>(time - start), &final);
        direction[0] = static_cast<float>(x87::fcos(angle));
        direction[1] = static_cast<float>(x87::fsin(angle));
        return !final;
    }
    case 2:
        return SamplePath(static_cast<float>(time - start), turning, position, direction, speedOut, cellX, cellY);
    case 3: {
        CMapObject* object = world->ObjectWithId(static_cast<int>(order->arg5));
        if (!object) {
            FaceAngle(order->angle, direction);
            return true;
        }
        int from = static_cast<int>(order->arg6);
        Vector3 buffer;
        const Vector3* where;
        if (static_cast<double>(from) >= time) where = object->PositionAt(&buffer, from);
        else where = object->AimPoint(&buffer);
        Vector3 target;
        target = *where;
        float tx = static_cast<float>(kHalfCellScale * target.x);
        double ty = kHalfCellScale * target.y;
        int to = static_cast<int>(order->arg7);
        if (static_cast<double>(to) > time && to != order->startTime) {
            double angleTo = x87::fpatan(ty - order->y, static_cast<double>(tx) - order->x) - order->angle;
            const double twoPi = static_cast<double>(kPi) + kPi;
            while (angleTo > kPi) angleTo = angleTo - twoPi;
            const float negPi = -kPi;
            EI_NAN_AUDIT(angleTo);
            while (angleTo < negPi) angleTo = angleTo + twoPi;   // ends on a NaN (the original's did not)
            double angle = (time - start) * angleTo / static_cast<double>(to - order->startTime) + order->angle;
            direction[0] = static_cast<float>(x87::fcos(angle));
            direction[1] = static_cast<float>(x87::fsin(angle));
            return true;
        }
        direction[0] = static_cast<float>(static_cast<double>(tx) - order->x);
        double dy = ty - order->y;
        direction[1] = static_cast<float>(dy);
        double norm = std::sqrt(dy * dy + static_cast<double>(direction[0]) * direction[0]);
        EI_NAN_AUDIT(norm);
        if (norm == 0.0) return true;
        double inverse = 1.0f / norm;
        direction[0] = static_cast<float>(inverse * direction[0]);
        direction[1] = static_cast<float>(inverse * direction[1]);
        direction[2] = static_cast<float>(inverse * direction[2]);
        return true;
    }
    case 4: {
        bool final;
        double angle = TurnAngle(this, static_cast<float>(time - start), &final);
        direction[0] = static_cast<float>(x87::fcos(angle));
        direction[1] = static_cast<float>(x87::fsin(angle));
        *turning = !final;
        return true;
    }
    default:
        return true;
    }
}

bool UnitMotion::SamplePath(float time, bool* turning, float* position, float* direction, float* speedOut,
                            int* cellX, int* cellY) {
    double end = 0.0;
    int index = 0;
    bool found = false;
    EI_NAN_AUDIT(time, segmentStart, segmentEnd);
    if (segment != -1 && time >= segmentStart && time <= segmentEnd) {
        found = true;   // still in the cached segment
        index = segment;
    } else if (segment != -1 && segmentStart <= time) {
        end = segmentStart;     // later: walk on from the cached segment
        index = segment;
    } else {
        *turning = true;
        turnRate = baseTurnRate;
        float turnTime = AbsBits(static_cast<float>(static_cast<double>(turn) / baseTurnRate));
        EI_NAN_AUDIT(time, turnTime);
        if (time <= turnTime) {
            float ratio = AbsBits(static_cast<float>(static_cast<double>(turn) / turnRate));
            double angle;
            EI_NAN_AUDIT(ratio);
            if (ratio < time) {
                angle = static_cast<double>(startAngle) + turn;
            } else {
                EI_NAN_AUDIT(turn);
                float sign = turn < 0.0f ? kMinusOne : (turn > 0.0f ? kOne : kZero);
                angle = static_cast<double>(sign) * turnRate * time + startAngle;
            }
            direction[0] = static_cast<float>(x87::fcos(angle));
            direction[1] = static_cast<float>(x87::fsin(angle));
            return true;
        }
        end = turnTime;
        index = 0;
    }

    if (!found) {
        *turning = false;
        float saved = segmentStart;
        for (; index < std::ssize(points) - 1; ++index) {
            segmentStart = static_cast<float>(end);
            const MotionPoint& from = points[index];
            const MotionPoint& to = points[index + 1];
            std::memcpy(&fromX, &from.x, 4 * sizeof(float));     // x, y, dirX, dirY
            std::memcpy(&toX, &to.x, 4 * sizeof(float));
            double dx = static_cast<double>(fromX) - toX;
            double dy = static_cast<double>(fromY) - toY;
            double distance = std::sqrt(LengthSquared2DUnrounded(dy, dx));
            length = static_cast<float>(distance);
            x87::CopyBits(speed, from.speed);
            end = end + distance / speed;
            EI_NAN_AUDIT(end);
            if (time <= end) {
                found = true;
                break;
            }
        }
        if (!found) {
            segmentStart = saved;
            *speedOut = 0.0f;
            const MotionPoint& last = points[std::ssize(points) - 1];
            x87::CopyBits(position[0], last.x);
            x87::CopyBits(position[1], last.y);
            x87::CopyBits(direction[0], last.dirX);
            direction[2] = 0.0f;
            x87::CopyBits(direction[1], last.dirY);
            *cellX = last.cellX;
            *cellY = last.cellY;
            return false;
        }
        curveX.Fit(fromX, fromDirX, toX, toDirX, length);
        curveY.Fit(fromY, fromDirY, toY, toDirY, length);
        segmentEnd = static_cast<float>(end);
        segment = index;
    }

    *turning = false;
    x87::CopyBits(*speedOut, speed);
    float u = static_cast<float>((time - static_cast<double>(segmentStart)) * speed);
    position[0] = static_cast<float>(curveX.Value(u));
    direction[0] = static_cast<float>(curveX.Slope(u));
    position[1] = static_cast<float>(curveY.Value(u));
    direction[1] = static_cast<float>(curveY.Slope(u));
    *cellX = points[segment + 1].cellX;
    *cellY = points[segment + 1].cellY;
    return true;
}

void UnitMotion::Build(PathOrder* path) {
    MotionOrder* order = OrderOf(path);
    segment = -1;
    points.clear();
    waypoints.clear();
    stepSpeeds.clear();
    x87::CopyBits(startAngle, order->angle);

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
    case 3:
        return;
    case 1: {
        float rate = AsFloat(order->arg6);
        float angle = AsFloat(order->arg5);
        float s = static_cast<float>(x87::fsin(angle));
        double c = x87::fcos(angle);
        float cf = static_cast<float>(c);
        EI_NAN_AUDIT(c, s);
        if (c == 0.0 && s == 0.0) {
            turn = 0.0f;
            return;
        }
        EI_NAN_AUDIT(rate);
        if (rate != 0.0f) turnRate = rate;
        else turnRate = baseTurnRate;
        NormalizeTurn(this, x87::fpatan(s, cf) - startAngle);
        return;
    }
    case 4: {
        float angle = AsFloat(order->arg5);
        double s = x87::fsin(angle);
        double c = x87::fcos(angle);
        EI_NAN_AUDIT(c, s);
        if (c == 0.0 && s == 0.0) {
            turn = 0.0f;
            return;
        }
        turnRate = baseTurnRate;
        NormalizeTurn(this, x87::fpatan(s, c) - startAngle);
        return;
    }
    default:
        break;
    }

    // Kind 2 (and anything else): waypoints at the start, at each corner of the steps and at the
    // target, then one trajectory point per path cell.
    MotionWaypoint first;
    x87::CopyBits(first.x, order->x);
    x87::CopyBits(first.y, order->y);
    first.cells = 0;
    lib::SetAtGrow(waypoints, std::ssize(waypoints), first);
    int previous = 0;
    for (auto node = path->steps.begin(); node != path->steps.end();) {
        auto next = std::next(node);
        ++waypoints[std::ssize(waypoints) - 1].cells;
        stepSpeeds.push_back(static_cast<float>(static_cast<double>(node->flags)));
        if (next == path->steps.end()) break;
        auto afterNext = std::next(next);
        const PathCell& cell = *node;
        double cx = cell.x, cy = cell.y;
        int dir = Direction(cell, *next);
        MotionWaypoint w;
        switch (dir) {
        case 2:
            w = Waypoint(cx, cy);
            lib::SetAtGrow(waypoints, std::ssize(waypoints), w);
            break;
        case 4:
            w = Waypoint(cx, cy + 1.0f);
            lib::SetAtGrow(waypoints, std::ssize(waypoints), w);
            break;
        case 6:
            w = Waypoint(cx + 1.0f, cy + 1.0f);
            lib::SetAtGrow(waypoints, std::ssize(waypoints), w);
            break;
        case 8:
            w = Waypoint(cx + 1.0f, cy);
            waypoints.push_back(w);
            break;
        default:
            break;
        }

        bool corner = previous != 0 && DirectionDelta(dir, previous) > 1;
        if (!corner && afterNext != path->steps.end()) corner = DirectionDelta(dir, Direction(*next, *afterNext)) > 1;
        if (corner) {
            switch (dir) {
            case 1:
                w = Waypoint(cx + 0.5f, cy);
                waypoints.push_back(w);
                break;
            case 3:
                w = Waypoint(cx, cy + 0.5f);
                lib::SetAtGrow(waypoints, std::ssize(waypoints), w);
                break;
            case 5:
                w = Waypoint(cx + 0.5f, cy + 1.0f);
                waypoints.push_back(w);
                break;
            case 7:
                w = Waypoint(cx + 1.0f, cy + 0.5f);
                waypoints.push_back(w);
                break;
            default:
                break;
            }
        }
        previous = dir;
        node = next;
    }

    MotionWaypoint last;
    last.x = std::bit_cast<float>(order->arg5);
    last.y = std::bit_cast<float>(order->arg6);
    last.cells = 0;
    waypoints.push_back(last);
    Subdivide();
    LimitSpeeds(baseTurnRate);

    // Face the first point: no turn when it is within ~25 degrees of the current facing.
    MotionPoint* p = points.data();
    if (x87::fcos(x87::fpatan(p->dirY, p->dirX) - startAngle) > 0.9) {
        turn = 0.0f;
        p->dirX = static_cast<float>(x87::fcos(startAngle));
        points.data()->dirY = static_cast<float>(x87::fsin(startAngle));
        AssignCells(path);
        return;
    }
    EI_NAN_AUDIT(p->dirX, p->dirY);
    if (p->dirX == 0.0f && p->dirY == 0.0f) {
        turn = 0.0f;
        AssignCells(path);
        return;
    }
    double angle = x87::fpatan(p->dirY, p->dirX);
    turnRate = baseTurnRate;
    NormalizeTurn(this, angle - startAngle);
    AssignCells(path);
}

void UnitMotion::AssignCells(PathOrder* path) {
    auto head = path->steps.begin();
    int i = 0;
    for (auto node = head; node != path->steps.end(); ++node) {
        if (i >= std::ssize(points)) break;
        points[i].cellX = node->x;
        points[i].cellY = node->y;
        ++i;
    }
    if (std::ssize(path->steps) == 1) {
        MotionPoint& last = points[std::ssize(points) - 1];
        last.cellX = head->x;
        last.cellY = head->y;
    }
}

void UnitMotion::Subdivide() {
    int stepIndex = 0;
    for (int i = 0; i < std::ssize(waypoints) - 1; ++i) {
        float startOffset = i == 0 ? -0.5f : 0.0f;
        float endOffset = i == std::ssize(waypoints) - 2 ? 0.5f : 0.0f;
        MotionWaypoint* w = &waypoints[i];
        SubdivideSegment(w, w + 1, w->cells, startOffset, endOffset, &stepIndex);
    }
}

void UnitMotion::SubdivideSegment(const MotionWaypoint* a, const MotionWaypoint* b, int cells, float startOffset,
                                  float endOffset, int* stepIndex) {
    double dx = static_cast<double>(a->x) - b->x;
    double dy = static_cast<double>(a->y) - b->y;
    double distance = std::sqrt(LengthSquared2DUnrounded(dy, dx));
    EI_NAN_AUDIT(distance);
    if (distance == 0.0) distance = kMinLength;
    MotionPoint p;
    p.cellX = -1;
    p.cellY = -1;
    p.dirX = static_cast<float>((static_cast<double>(b->x) - a->x) / distance);
    p.dirY = static_cast<float>((static_cast<double>(b->y) - a->y) / distance);

    EI_NAN_AUDIT(startOffset, endOffset);
    if (startOffset != 0.0f && endOffset != 0.0f && cells == 1) {
        x87::CopyBits(p.x, a->x);
        x87::CopyBits(p.y, a->y);
        int size = std::ssize(points);
        p.speed = static_cast<float>(static_cast<double>(stepSpeeds[*stepIndex]) * speedScale * kSpeedUnit);
        points.resize(size + 1);
        points[size] = p;
        size = std::ssize(points);
        x87::CopyBits(p.x, b->x);
        x87::CopyBits(p.y, b->y);
        points.resize(size + 1);
        points[size] = p;
        ++*stepIndex;
        return;
    }

    float inverse = static_cast<float>(1.0f / (static_cast<double>(cells) + startOffset - endOffset));
    for (int k = 0; k < cells; ++k) {
        double f = (static_cast<double>(k) + startOffset + 0.5f) * inverse;
        double g = 1.0f - f;
        p.x = static_cast<float>(f * b->x + g * a->x);
        p.y = static_cast<float>(g * a->y + f * b->y);
        p.speed = static_cast<float>(static_cast<double>(stepSpeeds[*stepIndex]) * speedScale * kSpeedUnit);
        int size = std::ssize(points);
        points.resize(size + 1);
        points[size] = p;
        ++*stepIndex;
    }
}

void UnitMotion::LimitSpeeds(float rate) {
    const double twoPi = static_cast<double>(kPi) + kPi;
    for (int i = 0; i < std::ssize(points) - 1; ++i) {
        MotionPoint* p = &points[i];
        double a1 = x87::fpatan(p[0].dirY, p[0].dirX);
        double a2 = x87::fpatan(p[1].dirY, p[1].dirX);
        double angle = AbsBits(static_cast<float>(a1 - a2));
        while (angle > twoPi) angle = angle - twoPi;
        if (angle > kPi) angle = twoPi - angle;
        double dx = static_cast<double>(p[0].x) - p[1].x;
        double dy = static_cast<double>(p[0].y) - p[1].y;
        float distance = static_cast<float>(std::sqrt(LengthSquared2DUnrounded(dy, dx)));
        double turnTime = angle / rate;
        double wayTime = static_cast<double>(distance) / p[0].speed;
        if (turnTime > wayTime) p[0].speed = static_cast<float>(distance / turnTime);
    }
}

// ---- The unit's save ------------------------------------------------------

void UnitMotion::LoadState(CStream* stream) {
    segment = -1;
    // Each count is read into its argument slot: a failed read leaves the stream pointer.
    int count = 0;
    stream->ReadValue(count);
    points.resize(count);
    for (int i = 0; i < std::ssize(points); ++i) stream->ReadValue(points[i]);
    stream->ReadValue(count);
    waypoints.resize(count);
    for (int i = 0; i < std::ssize(waypoints); ++i) stream->ReadValue(waypoints[i]);
    stream->ReadValue(count);
    stepSpeeds.resize(count);
    for (float& step : stepSpeeds) stream->ReadValue(step);
    stream->ReadValue(speedScale);
    stream->ReadValue(baseTurnRate);
    stream->ReadValue(turnRate);
    stream->ReadValue(startAngle);
    stream->ReadValue(turn);
}

void UnitMotion::SaveState(CStream* stream) {
    int count = std::ssize(points);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(points); ++i) stream->WriteValue(points[i]);
    count = std::ssize(waypoints);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(waypoints); ++i) stream->WriteValue(waypoints[i]);
    count = std::ssize(stepSpeeds);
    stream->WriteValue(count);
    for (const float& step : stepSpeeds) stream->WriteValue(step);
    // Copied bit for bit.
    const float* const fields[] = {&speedScale, &baseTurnRate, &turnRate, &startAngle, &turn};
    for (const float* field : fields) {
        std::uint32_t value;
        value = std::bit_cast<std::uint32_t>(*field);
        stream->WriteValue(value);
    }
}
