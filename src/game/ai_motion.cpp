// A unit's motion: the order header of AIPath and the trajectory the unit follows. The trajectory is rebuilt
// when a path is advanced (Build) and sampled every tick by the unit's movement (Sample).
#include "ai_motion.h"

#include "ai_map.h"
#include "core_streams.h"
#include "map_object.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
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

void FaceAngle(float angle, MotionSample* sample) {
    sample->direction.x = std::cos(angle);
    sample->direction.y = std::sin(angle);
}

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
    float length = std::sqrt(LengthSquared2D(dy, dx));
    float angle = length > 0.0f ? std::atan2(dy / length, dx / length) : 0.0f;
    float x = kHalfCellScale * position.x;
    float y = kHalfCellScale * position.y;
    EI_NAN_AUDIT(order->angle, angle, x, order->x, order->y, y);
    bool same = order->kind == 0 && order->angle == angle && x == order->x && order->y == y;
    order->x = x;
    order->angle = angle;
    order->kind = 0;
    order->y = y;
    order->startTime = time;
    steps.clear();
    return !same;
}

void PathOrder::SetTurn(const Vector3& direction, std::uint32_t rateBits) {
    MotionOrder* order = OrderOf(this);
    order->kind = 1;
    float angle = std::atan2(direction.y, direction.x);
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
    float angle = std::atan2(direction.y, direction.x);
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

Vector3 PathOrder::Destination() {
    const MotionOrder* order = OrderOf(this);
    float scale = 0.5f;     // half a cell
    if (order->kind != 2) return {scale * order->x, scale * order->y, 0.0f};
    return {scale * AsFloat(order->arg5), scale * AsFloat(order->arg6), 0.0f};
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
    half += half;
    float cubic = ((d1 - d0) - half) * inverse * inverse;
    c3 = cubic;
    float scaled = cubic * length * length;
    scaled += scaled;
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

MotionSample UnitMotion::Sample(World* world, MotionOrder* order, float time) {
    MotionSample sample;
    float start = static_cast<float>(order->startTime);
    time = time >= start ? time : start;
    sample.x = order->x;
    sample.y = order->y;

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
        FaceAngle(order->angle, &sample);
        return sample;
    case 1: {
        sample.turning = true;
        bool final;
        float angle = TurnAngle(this, time - start, &final);
        sample.direction.x = std::cos(angle);
        sample.direction.y = std::sin(angle);
        sample.alive = !final;
        return sample;
    }
    case 2:
        return SamplePath(time - start, sample.x, sample.y);
    case 3: {
        MapObject* object = world->ObjectWithId(static_cast<int>(order->arg5));
        if (!object) {
            FaceAngle(order->angle, &sample);
            return sample;
        }
        int from = static_cast<int>(order->arg6);
        Vector3 target = static_cast<float>(from) >= time ? object->PositionAt(from) : object->AimPoint();
        float tx = kHalfCellScale * target.x;
        float ty = kHalfCellScale * target.y;
        int to = static_cast<int>(order->arg7);
        if (static_cast<float>(to) > time && to != order->startTime) {
            float angleTo = std::atan2(ty - order->y, tx - order->x) - order->angle;
            const float twoPi = kPi + kPi;
            while (angleTo > kPi) angleTo = angleTo - twoPi;
            const float negPi = -kPi;
            EI_NAN_AUDIT(angleTo);
            while (angleTo < negPi) angleTo = angleTo + twoPi;   // ends on a NaN (the original's did not)
            // Over the turn's tick count, an int.
            float angle = ((time - start) * angleTo / static_cast<float>(to - order->startTime)) +
                          order->angle;
            sample.direction.x = std::cos(angle);
            sample.direction.y = std::sin(angle);
            return sample;
        }
        sample.direction.x = tx - order->x;
        float dy = ty - order->y;
        sample.direction.y = dy;
        float norm = std::sqrt(dy * dy + sample.direction.x * sample.direction.x);
        EI_NAN_AUDIT(norm);
        if (norm == 0.0f) return sample;
        float inverse = 1.0f / norm;
        sample.direction *= inverse;
        return sample;
    }
    case 4: {
        bool final;
        float angle = TurnAngle(this, time - start, &final);
        sample.direction.x = std::cos(angle);
        sample.direction.y = std::sin(angle);
        sample.turning = !final;
        return sample;
    }
    default:
        return sample;
    }
}

MotionSample UnitMotion::SamplePath(float time, float startX, float startY) {
    MotionSample sample;
    sample.x = startX;
    sample.y = startY;
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
        sample.turning = true;
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
            sample.direction.x = std::cos(angle);
            sample.direction.y = std::sin(angle);
            return sample;
        }
        end = turnTime;
        index = 0;
    }

    if (!found) {
        sample.turning = false;
        float saved = segmentStart;
        for (; index < std::ssize(points) - 1; ++index) {
            segmentStart = end;
            const MotionPoint& from = points[index];
            const MotionPoint& to = points[index + 1];
            std::memcpy(&fromX, &from.x, 4 * sizeof(float));     // x, y, dirX, dirY
            std::memcpy(&toX, &to.x, 4 * sizeof(float));
            float dx = fromX - toX;
            float dy = fromY - toY;
            float distance = std::sqrt(LengthSquared2D(dy, dx));
            length = distance;
            speed = from.speed;
            end += distance / speed;
            EI_NAN_AUDIT(end);
            if (time <= end) {
                found = true;
                break;
            }
        }
        if (!found) {
            segmentStart = saved;
            const MotionPoint& last = points[std::ssize(points) - 1];
            sample.alive = false;
            sample.speed = 0.0f;
            sample.x = last.x;
            sample.y = last.y;
            sample.direction = {last.dirX, last.dirY, 0.0f};
            sample.cellX = last.cellX;
            sample.cellY = last.cellY;
            return sample;
        }
        curveX.Fit(fromX, fromDirX, toX, toDirX, length);
        curveY.Fit(fromY, fromDirY, toY, toDirY, length);
        segmentEnd = end;
        segment = index;
    }

    sample.turning = false;
    sample.speed = speed;
    float u = (time - segmentStart) * speed;
    sample.x = curveX.Value(u);
    sample.direction.x = curveX.Slope(u);
    sample.y = curveY.Value(u);
    sample.direction.y = curveY.Slope(u);
    sample.cellX = points[segment + 1].cellX;
    sample.cellY = points[segment + 1].cellY;
    return sample;
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
        float s = std::sin(angle);
        float c = std::cos(angle);
        EI_NAN_AUDIT(c, s);
        if (c == 0.0f && s == 0.0f) {
            turn = 0.0f;
            return;
        }
        EI_NAN_AUDIT(rate);
        if (rate != 0.0f) turnRate = rate;
        else turnRate = baseTurnRate;
        NormalizeTurn(this, std::atan2(s, c) - startAngle);
        return;
    }
    case 4: {
        float angle = AsFloat(order->arg5);
        float s = std::sin(angle);
        float c = std::cos(angle);
        EI_NAN_AUDIT(c, s);
        if (c == 0.0f && s == 0.0f) {
            turn = 0.0f;
            return;
        }
        turnRate = baseTurnRate;
        NormalizeTurn(this, std::atan2(s, c) - startAngle);
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

    // A path with no steps has no trajectory point to face.
    if (points.empty()) {
        turn = 0.0f;
        AssignCells(path);
        return;
    }
    // Face the first point: no turn when it is within ~25 degrees of the current facing.
    MotionPoint* p = points.data();
    if (std::cos(std::atan2(p->dirY, p->dirX) - startAngle) > 0.9f) {
        turn = 0.0f;
        p->dirX = std::cos(startAngle);
        points.data()->dirY = std::sin(startAngle);
        AssignCells(path);
        return;
    }
    EI_NAN_AUDIT(p->dirX, p->dirY);
    if (p->dirX == 0.0f && p->dirY == 0.0f) {
        turn = 0.0f;
        AssignCells(path);
        return;
    }
    float angle = std::atan2(p->dirY, p->dirX);
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

void UnitMotion::SubdivideSegment(const MotionWaypoint* from, const MotionWaypoint* to, int cells, float startOffset,
                                  float endOffset, int* stepIndex) {
    float dx = from->x - to->x;
    float dy = from->y - to->y;
    float distance = std::sqrt(LengthSquared2D(dy, dx));
    EI_NAN_AUDIT(distance);
    if (distance == 0.0f) distance = kMinLength;
    MotionPoint p;
    p.cellX = -1;
    p.cellY = -1;
    p.dirX = (to->x - from->x) / distance;
    p.dirY = (to->y - from->y) / distance;

    EI_NAN_AUDIT(startOffset, endOffset);
    if (startOffset != 0.0f && endOffset != 0.0f && cells == 1) {
        p.x = from->x;
        p.y = from->y;
        int size = lib::Count(points);
        p.speed = stepSpeeds[*stepIndex] * speedScale * kSpeedUnit;
        points.resize(size + 1);
        points[size] = p;
        size = lib::Count(points);
        p.x = to->x;
        p.y = to->y;
        points.resize(size + 1);
        points[size] = p;
        ++*stepIndex;
        return;
    }

    float inverse = 1.0f / (static_cast<float>(cells) + startOffset - endOffset);
    for (int k = 0; k < cells; ++k) {
        float f = (static_cast<float>(k) + startOffset + 0.5f) * inverse;
        float g = 1.0f - f;
        p.x = f * to->x + g * from->x;
        p.y = g * from->y + f * to->y;
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
        float angle = AbsBits(std::atan2(p[0].dirY, p[0].dirX) - std::atan2(p[1].dirY, p[1].dirX));
        while (angle > twoPi) angle = angle - twoPi;
        if (angle > kPi) angle = twoPi - angle;
        float distance = std::sqrt(LengthSquared2D(p[0].x - p[1].x, p[0].y - p[1].y));
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
