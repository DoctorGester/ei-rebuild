// A unit's motion: the order header of AIPath and the trajectory the unit follows. The trajectory is rebuilt
// when a path is advanced (Build) and sampled every tick by the unit's movement (Sample). Float maths in the
// original's order (its FPU registers held floats at the main thread's 24-bit precision); FSIN, FCOS and FPATAN's
// 64-bit results through lib/pc24.h.
#include "ai_motion.h"

#include "ai_map.h"
#include "core_streams.h"
#include "map_object.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"


#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>

namespace {

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

constexpr float kHalfCellScale = 2.0f;      // 1 / (half a cell)
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
    direction[0] = pc24::Round(pc24::Cos(angle));
    direction[1] = pc24::Round(pc24::Sin(angle));
}

// FCOS's 64-bit result above a double constant, compared unrounded.
bool Above(pc24::Unrounded value, double limit) { return value.hi > limit || (value.hi == limit && value.lo > 0.0); }

// Turning in place: the facing after `elapsed` of turning `turn` from `start` at `rate`; `final`
// when the turn takes no longer than that. Used by the order kinds 1 and 4.
float TurnAngle(const UnitMotion* m, float elapsed, bool* final) {
    float ratio = AbsBits(m->turn / m->turnRate);
    EI_NAN_AUDIT(ratio, elapsed);
    if (ratio < elapsed) {
        *final = true;
        return m->startAngle + m->turn;
    }
    *final = false;
    EI_NAN_AUDIT(m->turn);
    float sign = m->turn < 0.0f ? kMinusOne : (m->turn > 0.0f ? kOne : kZero);
    return sign * elapsed * m->turnRate + m->startAngle;
}

// Brings `turn` into [-pi, pi], a turn (2 pi as the float sum kPi + kPi) at a time.
void NormalizeTurn(UnitMotion* m, float turn) {
    m->turn = turn;
    const float twoPi = kPi + kPi;
    if (turn > kPi) {
        float v;
        do {
            v = m->turn - twoPi;
            m->turn = v;
        } while (v > kPi);
    }
    const float negPi = -kPi;
    if (negPi > m->turn) {
        float v;
        do {
            v = twoPi + m->turn;
            m->turn = v;
            EI_NAN_AUDIT(v);
        } while (v < negPi);   // ends on a NaN (the original's did not)
    }
}

MotionWaypoint Waypoint(float x, float y) { return {x, y, 0}; }

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

bool PathOrder::Start(const Vector3& position, const Vector3& direction, int time) {
    MotionOrder* order = OrderOf(this);
    float dx = direction.x;
    float dy = direction.y;
    float length = std::sqrt(LengthSquared2DUnrounded(dy, dx));
    // FPATAN's 64-bit result: compared with the stored angle unrounded.
    pc24::Unrounded angle = length > 0.0f ? pc24::Atan2(dy / length, dx / length) : pc24::Unrounded{};
    float x = kHalfCellScale * position.x;
    float y = kHalfCellScale * position.y;
    EI_NAN_AUDIT(order->angle, angle.hi, x, order->x, order->y, y);
    bool same = order->kind == 0 && order->angle == angle.hi && angle.lo == 0.0 && x == order->x && order->y == y;
    order->x = x;
    order->angle = pc24::Round(angle);
    order->kind = 0;
    order->y = y;
    order->startTime = time;
    steps.clear();
    return !same;
}

void PathOrder::SetTurn(const Vector3& direction, std::uint32_t rateBits) {
    MotionOrder* order = OrderOf(this);
    order->kind = 1;
    float angle = pc24::Round(pc24::Atan2(direction.y, direction.x));
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

void PathOrder::SetTurnUntil(const Vector3& direction, int fromTime, int toTime) {
    MotionOrder* order = OrderOf(this);
    order->kind = 4;
    order->arg7 = static_cast<std::uint32_t>(toTime);
    float angle = pc24::Round(pc24::Atan2(direction.y, direction.x));
    order->arg6 = static_cast<std::uint32_t>(fromTime);
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
    for (auto& node : steps) node.flags = speed;
    return 0;
}

Vector3* PathOrder::Destination(Vector3* out) {
    const MotionOrder* order = OrderOf(this);
    float scale = 0.5f;     // half a cell
    if (order->kind != 2) {
        out->x = scale * order->x;
        out->z = 0.0f;
        out->y = scale * order->y;
    } else {
        out->x = scale * AsFloat(order->arg5);
        out->z = 0.0f;
        out->y = scale * AsFloat(order->arg6);
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

// A copy of CubicSegment::Evaluate: Horner's scheme.
float MotionCubic::Value(float u) { return ((u * c3 + c2) * u + c1) * u + c0; }

void MotionCubic::Fit(float p0, float d0, float p1, float d1, float length) {
    float inverse = 1.0f / length;
    c0 = p0;
    c1 = d0;
    float half = ((p1 - p0) - d0 * length) * inverse;
    half = half + half;
    float cubic = ((d1 - d0) - half) * inverse * inverse;
    c3 = cubic;
    float scaled = cubic * length * length;
    scaled = scaled + scaled;
    c2 = (half - scaled) * inverse * 0.5f;
}

float MotionCubic::Slope(float u) {
    float c2x2 = c2 + c2;
    return (u * c3 * 3.0f + c2x2) * u + c1;
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
    // The start tick, an int: compared exactly; the elapsed time is the difference rounded once.
    double start = order->startTime;
    time = time >= start ? time : static_cast<float>(start);
    position[0] = order->x;
    position[1] = order->y;

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
        FaceAngle(order->angle, direction);
        return true;
    case 1: {
        *turning = true;
        bool final;
        float angle = TurnAngle(this, pc24::Sub(time, start), &final);
        direction[0] = pc24::Round(pc24::Cos(angle));
        direction[1] = pc24::Round(pc24::Sin(angle));
        return !final;
    }
    case 2:
        return SamplePath(pc24::Sub(time, start), turning, position, direction, speedOut, cellX, cellY);
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
        float tx = kHalfCellScale * target.x;
        float ty = kHalfCellScale * target.y;
        int to = static_cast<int>(order->arg7);
        if (static_cast<double>(to) > time && to != order->startTime) {
            float angleTo = pc24::Sub(pc24::Atan2(ty - order->y, tx - order->x), order->angle);
            const float twoPi = kPi + kPi;
            while (angleTo > kPi) angleTo = angleTo - twoPi;
            const float negPi = -kPi;
            EI_NAN_AUDIT(angleTo);
            while (angleTo < negPi) angleTo = angleTo + twoPi;   // ends on a NaN (the original's did not)
            // Over the turn's tick count, an int.
            float angle = pc24::Div(pc24::Sub(time, start) * angleTo, static_cast<double>(to - order->startTime)) +
                          order->angle;
            direction[0] = pc24::Round(pc24::Cos(angle));
            direction[1] = pc24::Round(pc24::Sin(angle));
            return true;
        }
        direction[0] = tx - order->x;
        float dy = ty - order->y;
        direction[1] = dy;
        float norm = std::sqrt(dy * dy + direction[0] * direction[0]);
        EI_NAN_AUDIT(norm);
        if (norm == 0.0f) return true;
        float inverse = 1.0f / norm;
        direction[0] = inverse * direction[0];
        direction[1] = inverse * direction[1];
        direction[2] = inverse * direction[2];
        return true;
    }
    case 4: {
        bool final;
        float angle = TurnAngle(this, pc24::Sub(time, start), &final);
        direction[0] = pc24::Round(pc24::Cos(angle));
        direction[1] = pc24::Round(pc24::Sin(angle));
        *turning = !final;
        return true;
    }
    default:
        return true;
    }
}

bool UnitMotion::SamplePath(float time, bool* turning, float* position, float* direction, float* speedOut,
                            int* cellX, int* cellY) {
    float end = 0.0f;
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
        float turnTime = AbsBits(turn / baseTurnRate);
        EI_NAN_AUDIT(time, turnTime);
        if (time <= turnTime) {
            float ratio = AbsBits(turn / turnRate);
            float angle;
            EI_NAN_AUDIT(ratio);
            if (ratio < time) {
                angle = startAngle + turn;
            } else {
                EI_NAN_AUDIT(turn);
                float sign = turn < 0.0f ? kMinusOne : (turn > 0.0f ? kOne : kZero);
                angle = sign * turnRate * time + startAngle;
            }
            direction[0] = pc24::Round(pc24::Cos(angle));
            direction[1] = pc24::Round(pc24::Sin(angle));
            return true;
        }
        end = turnTime;
        index = 0;
    }

    if (!found) {
        *turning = false;
        float saved = segmentStart;
        for (; index < std::ssize(points) - 1; ++index) {
            segmentStart = end;
            const MotionPoint& from = points[index];
            const MotionPoint& to = points[index + 1];
            std::memcpy(&fromX, &from.x, 4 * sizeof(float));     // x, y, dirX, dirY
            std::memcpy(&toX, &to.x, 4 * sizeof(float));
            float dx = fromX - toX;
            float dy = fromY - toY;
            float distance = std::sqrt(LengthSquared2DUnrounded(dy, dx));
            length = distance;
            speed = from.speed;
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
            position[0] = last.x;
            position[1] = last.y;
            direction[0] = last.dirX;
            direction[2] = 0.0f;
            direction[1] = last.dirY;
            *cellX = last.cellX;
            *cellY = last.cellY;
            return false;
        }
        curveX.Fit(fromX, fromDirX, toX, toDirX, length);
        curveY.Fit(fromY, fromDirY, toY, toDirY, length);
        segmentEnd = end;
        segment = index;
    }

    *turning = false;
    *speedOut = speed;
    float u = (time - segmentStart) * speed;
    position[0] = curveX.Value(u);
    direction[0] = curveX.Slope(u);
    position[1] = curveY.Value(u);
    direction[1] = curveY.Slope(u);
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
    startAngle = order->angle;

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
    case 3:
        return;
    case 1: {
        float rate = AsFloat(order->arg6);
        float angle = AsFloat(order->arg5);
        float s = pc24::Round(pc24::Sin(angle));
        pc24::Unrounded c = pc24::Cos(angle);   // compared with 0 unrounded
        float cf = pc24::Round(c);
        EI_NAN_AUDIT(c.hi, s);
        if (c.hi == 0.0 && s == 0.0f) {
            turn = 0.0f;
            return;
        }
        EI_NAN_AUDIT(rate);
        if (rate != 0.0f) turnRate = rate;
        else turnRate = baseTurnRate;
        NormalizeTurn(this, pc24::Sub(pc24::Atan2(s, cf), startAngle));
        return;
    }
    case 4: {
        // FPATAN of FSIN's and FCOS's results stored as doubles (hi + lo: the 53-bit rounding).
        float angle = AsFloat(order->arg5);
        pc24::Unrounded sine = pc24::Sin(angle);
        pc24::Unrounded cosine = pc24::Cos(angle);
        double s = sine.hi + sine.lo;
        double c = cosine.hi + cosine.lo;
        EI_NAN_AUDIT(c, s);
        if (c == 0.0 && s == 0.0) {
            turn = 0.0f;
            return;
        }
        turnRate = baseTurnRate;
        NormalizeTurn(this, pc24::Sub(pc24::Atan2(s, c), startAngle));
        return;
    }
    default:
        break;
    }

    // Kind 2 (and anything else): waypoints at the start, at each corner of the steps and at the
    // target, then one trajectory point per path cell.
    MotionWaypoint first;
    first.x = order->x;
    first.y = order->y;
    first.cells = 0;
    lib::SetAtGrow(waypoints, lib::Count(waypoints), first);
    int previous = 0;
    for (auto node = path->steps.begin(); node != path->steps.end();) {
        auto next = std::next(node);
        ++waypoints[std::ssize(waypoints) - 1].cells;
        stepSpeeds.push_back(static_cast<float>(node->flags));
        if (next == path->steps.end()) break;
        auto afterNext = std::next(next);
        const PathCell& cell = *node;
        float cx = cell.x, cy = cell.y;
        int dir = Direction(cell, *next);
        MotionWaypoint w;
        switch (dir) {
        case 2:
            w = Waypoint(cx, cy);
            lib::SetAtGrow(waypoints, lib::Count(waypoints), w);
            break;
        case 4:
            w = Waypoint(cx, cy + 1.0f);
            lib::SetAtGrow(waypoints, lib::Count(waypoints), w);
            break;
        case 6:
            w = Waypoint(cx + 1.0f, cy + 1.0f);
            lib::SetAtGrow(waypoints, lib::Count(waypoints), w);
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
                lib::SetAtGrow(waypoints, lib::Count(waypoints), w);
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
    if (Above(pc24::Cos(pc24::Sub(pc24::Atan2(p->dirY, p->dirX), startAngle)), 0.9)) {
        turn = 0.0f;
        p->dirX = pc24::Round(pc24::Cos(startAngle));
        points.data()->dirY = pc24::Round(pc24::Sin(startAngle));
        AssignCells(path);
        return;
    }
    EI_NAN_AUDIT(p->dirX, p->dirY);
    if (p->dirX == 0.0f && p->dirY == 0.0f) {
        turn = 0.0f;
        AssignCells(path);
        return;
    }
    pc24::Unrounded angle = pc24::Atan2(p->dirY, p->dirX);
    turnRate = baseTurnRate;
    NormalizeTurn(this, pc24::Sub(angle, startAngle));
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
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float distance = std::sqrt(LengthSquared2DUnrounded(dy, dx));
    EI_NAN_AUDIT(distance);
    if (distance == 0.0f) distance = kMinLength;
    MotionPoint p;
    p.cellX = -1;
    p.cellY = -1;
    p.dirX = (b->x - a->x) / distance;
    p.dirY = (b->y - a->y) / distance;

    EI_NAN_AUDIT(startOffset, endOffset);
    if (startOffset != 0.0f && endOffset != 0.0f && cells == 1) {
        p.x = a->x;
        p.y = a->y;
        int size = lib::Count(points);
        p.speed = stepSpeeds[*stepIndex] * speedScale * kSpeedUnit;
        points.resize(size + 1);
        points[size] = p;
        size = lib::Count(points);
        p.x = b->x;
        p.y = b->y;
        points.resize(size + 1);
        points[size] = p;
        ++*stepIndex;
        return;
    }

    float inverse = 1.0f / (static_cast<float>(cells) + startOffset - endOffset);
    for (int k = 0; k < cells; ++k) {
        float f = (static_cast<float>(k) + startOffset + 0.5f) * inverse;
        float g = 1.0f - f;
        p.x = f * b->x + g * a->x;
        p.y = g * a->y + f * b->y;
        p.speed = stepSpeeds[*stepIndex] * speedScale * kSpeedUnit;
        int size = lib::Count(points);
        points.resize(size + 1);
        points[size] = p;
        ++*stepIndex;
    }
}

void UnitMotion::LimitSpeeds(float rate) {
    const float twoPi = kPi + kPi;
    for (int i = 0; i < std::ssize(points) - 1; ++i) {
        MotionPoint* p = &points[i];
        // The first angle waits out the second FPATAN call stored as a double (rounded to 53 bits).
        pc24::Unrounded a1 = pc24::Atan2(p[0].dirY, p[0].dirX);
        float angle = AbsBits(pc24::Sub(a1.hi + a1.lo, pc24::Atan2(p[1].dirY, p[1].dirX)));
        while (angle > twoPi) angle = angle - twoPi;
        if (angle > kPi) angle = twoPi - angle;
        float dx = p[0].x - p[1].x;
        float dy = p[0].y - p[1].y;
        float distance = std::sqrt(LengthSquared2DUnrounded(dy, dx));
        float turnTime = angle / rate;
        float wayTime = distance / p[0].speed;
        if (turnTime > wayTime) p[0].speed = distance / turnTime;
    }
}

// ---- The unit's save ------------------------------------------------------

void UnitMotion::LoadState(ByteStream* stream) {
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

void UnitMotion::SaveState(ByteStream* stream) {
    int count = lib::Count(points);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(points); ++i) stream->WriteValue(points[i]);
    count = lib::Count(waypoints);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(waypoints); ++i) stream->WriteValue(waypoints[i]);
    count = lib::Count(stepSpeeds);
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
