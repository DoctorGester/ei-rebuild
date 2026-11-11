// A unit's motion: the order header of AIPath and the trajectory the unit follows. The trajectory is rebuilt
// when a path is advanced (Build) and sampled every tick by the unit's movement (Sample).
#include "game/ai/ai_motion.h"

#include "game/ai/ai_map.h"
#include "engine/core/core_streams.h"
#include "game/objects/map_object.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <bit>
#include <concepts>
#include <cstdint>

namespace {

float AsFloat(std::uint32_t bits) {
    float f;
    f = std::bit_cast<float>(bits);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float AsFloat(T) = delete;


// The direction of a turn: -1, 0 or 1. The product with it keeps NaN and -0 (the compiler does not fold 0 * x).
float TurnSign(float turn) {
    return turn < 0.0f ? -1.0f : (turn > 0.0f ? 1.0f : 0.0f);
}

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
    sample->direction.x = lib::Cos(angle);
    sample->direction.y = lib::Sin(angle);
}

// Turning in place: the facing after `elapsed` of turning `turn` from `start` at `rate`; `final`
// when the turn takes no longer than that. Used by the order kinds 1 and 4.
float TurnAngle(const UnitMotion* m, float elapsed, bool* final) {
    float ratio = AbsBits(m->turn / m->turnRate);
    if (ratio < elapsed) {
        *final = true;
        return m->startAngle + m->turn;
    }
    *final = false;
    float sign = TurnSign(m->turn);
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
        } while (v < negPi);   // ends on a NaN (the original's did not)
    }
}

MotionWaypoint Waypoint(float x, float y) { return {{x, y}, 0}; }

} // namespace

// --- The order (AIPath header) --------------------------------------------------------------------

int PathOrder::Reset() {
    MotionOrder* order = OrderOf(this);
    order->kind = 0;
    order->position = {0.0f, 0.0f};
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
    float angle = length > 0.0f ? lib::Atan2(dy / length, dx / length) : 0.0f;
    const Vector2 at = kHalfCellScale * XY(position);
    bool same = order->kind == 0 && order->angle == angle && at == order->position;
    order->position = at;
    order->angle = angle;
    order->kind = 0;
    order->startTime = time;
    steps.clear();
    return !same;
}

void PathOrder::SetTurn(const Vector3& direction, std::uint32_t rateBits) {
    MotionOrder* order = OrderOf(this);
    order->kind = 1;
    float angle = lib::Atan2(direction.y, direction.x);
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
    float angle = lib::Atan2(direction.y, direction.x);
    order->arg6 = static_cast<std::uint32_t>(fromTime);
    order->arg5 = std::bit_cast<std::uint32_t>(angle);
}

void PathOrder::SetTarget(Vector2 target) {
    MotionOrder* order = OrderOf(this);
    order->kind = 2;
    order->arg5 = std::bit_cast<std::uint32_t>(target.x);
    order->arg6 = std::bit_cast<std::uint32_t>(target.y);
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
    if (order->kind != 2) return WithZ(scale * order->position, 0.0f);
    return {scale * AsFloat(order->arg5), scale * AsFloat(order->arg6), 0.0f};
}

void AIPath::Clear() {
    cells.clear();
    Reset();
    target = {0.0f, 0.0f};
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
    sample.position = order->position;

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
        FaceAngle(order->angle, &sample);
        return sample;
    case 1: {
        sample.turning = true;
        bool final;
        FaceAngle(TurnAngle(this, time - start, &final), &sample);
        sample.alive = !final;
        return sample;
    }
    case 2:
        return SamplePath(time - start, sample.position);
    case 3: {
        MapObject* object = world->ObjectWithId(static_cast<int>(order->arg5));
        if (!object) {
            FaceAngle(order->angle, &sample);
            return sample;
        }
        int from = static_cast<int>(order->arg6);
        Vector3 target = static_cast<float>(from) >= time ? object->PositionAt(from) : object->AimPoint();
        const Vector2 toTarget = kHalfCellScale * XY(target) - order->position;
        int to = static_cast<int>(order->arg7);
        if (static_cast<float>(to) > time && to != order->startTime) {
            float angleTo = lib::Atan2(toTarget.y, toTarget.x) - order->angle;
            const float twoPi = kPi + kPi;
            while (angleTo > kPi) angleTo = angleTo - twoPi;
            const float negPi = -kPi;
            while (angleTo < negPi) angleTo = angleTo + twoPi;   // ends on a NaN (the original's did not)
            // Over the turn's tick count, an int.
            FaceAngle(((time - start) * angleTo / static_cast<float>(to - order->startTime)) + order->angle, &sample);
            return sample;
        }
        sample.direction = WithZ(toTarget, 0.0f);
        float norm = std::sqrt(LengthSquared2D(toTarget.y, toTarget.x));
        if (norm == 0.0f) return sample;
        float inverse = 1.0f / norm;
        sample.direction *= inverse;
        return sample;
    }
    case 4: {
        bool final;
        FaceAngle(TurnAngle(this, time - start, &final), &sample);
        sample.turning = !final;
        return sample;
    }
    default:
        return sample;
    }
}

MotionSample UnitMotion::SamplePath(float time, Vector2 start) {
    MotionSample sample;
    sample.position = start;
    float end = 0.0f;
    int index = 0;
    bool found = false;
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
        if (time <= turnTime) {
            float ratio = AbsBits(turn / turnRate);
            float angle;
            if (ratio < time) {
                angle = startAngle + turn;
            } else {
                float sign = TurnSign(turn);
                angle = sign * turnRate * time + startAngle;
            }
            FaceAngle(angle, &sample);
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
            const MotionPoint& first = points[index];
            const MotionPoint& next = points[index + 1];
            segmentFrom = first.position;
            segmentFromDirection = first.direction;
            segmentTo = next.position;
            segmentToDirection = next.direction;
            float distance = Length2D(segmentFrom - segmentTo);
            length = distance;
            speed = first.speed;
            end += distance / speed;
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
            sample.position = last.position;
            sample.direction = WithZ(last.direction, 0.0f);
            sample.cell = last.cell;
            return sample;
        }
        curveX.Fit(segmentFrom.x, segmentFromDirection.x, segmentTo.x, segmentToDirection.x, length);
        curveY.Fit(segmentFrom.y, segmentFromDirection.y, segmentTo.y, segmentToDirection.y, length);
        segmentEnd = end;
        segment = index;
    }

    sample.turning = false;
    sample.speed = speed;
    float u = (time - segmentStart) * speed;
    sample.position = {curveX.Value(u), curveY.Value(u)};
    sample.direction.x = curveX.Slope(u);
    sample.direction.y = curveY.Slope(u);
    sample.cell = points[segment + 1].cell;
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
        float s = lib::Sin(angle);
        float c = lib::Cos(angle);
        if (c == 0.0f && s == 0.0f) {
            turn = 0.0f;
            return;
        }
        if (rate != 0.0f) turnRate = rate;
        else turnRate = baseTurnRate;
        NormalizeTurn(this, lib::Atan2(s, c) - startAngle);
        return;
    }
    case 4: {
        float angle = AsFloat(order->arg5);
        float s = lib::Sin(angle);
        float c = lib::Cos(angle);
        if (c == 0.0f && s == 0.0f) {
            turn = 0.0f;
            return;
        }
        turnRate = baseTurnRate;
        NormalizeTurn(this, lib::Atan2(s, c) - startAngle);
        return;
    }
    default:
        break;
    }

    // Kind 2 (and anything else): waypoints at the start, at each corner of the steps and at the
    // target, then one trajectory point per path cell.
    lib::SetAtGrow(waypoints, lib::Count(waypoints), MotionWaypoint{order->position, 0});
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

    waypoints.push_back(Waypoint(std::bit_cast<float>(order->arg5), std::bit_cast<float>(order->arg6)));
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
    if (lib::Cos(lib::Atan2(p->direction.y, p->direction.x) - startAngle) > 0.9f) {
        turn = 0.0f;
        p->direction = {lib::Cos(startAngle), lib::Sin(startAngle)};
        AssignCells(path);
        return;
    }
    if (p->direction == Vector2{0.0f, 0.0f}) {
        turn = 0.0f;
        AssignCells(path);
        return;
    }
    float angle = lib::Atan2(p->direction.y, p->direction.x);
    turnRate = baseTurnRate;
    NormalizeTurn(this, angle - startAngle);
    AssignCells(path);
}

void UnitMotion::AssignCells(PathOrder* path) {
    auto head = path->steps.begin();
    int i = 0;
    for (auto node = head; node != path->steps.end(); ++node) {
        if (i >= std::ssize(points)) break;
        points[i].cell = {node->x, node->y};
        ++i;
    }
    if (std::ssize(path->steps) == 1) points[std::ssize(points) - 1].cell = {head->x, head->y};
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
    const Vector2 a = from->position, b = to->position;
    float distance = Length2D(a - b);
    if (distance == 0.0f) distance = kMinLength;
    MotionPoint p;
    p.cell = {-1, -1};
    p.direction = (b - a) / distance;

    if (startOffset != 0.0f && endOffset != 0.0f && cells == 1) {
        p.position = a;
        int size = lib::Count(points);
        p.speed = stepSpeeds[*stepIndex] * speedScale * kSpeedUnit;
        points.resize(size + 1);
        points[size] = p;
        size = lib::Count(points);
        p.position = b;
        points.resize(size + 1);
        points[size] = p;
        ++*stepIndex;
        return;
    }

    float inverse = 1.0f / (static_cast<float>(cells) + startOffset - endOffset);
    for (int k = 0; k < cells; ++k) {
        float f = (static_cast<float>(k) + startOffset + 0.5f) * inverse;
        float g = 1.0f - f;
        p.position = f * b + g * a;
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
        float angle = AbsBits(lib::Atan2(p[0].direction.y, p[0].direction.x) - lib::Atan2(p[1].direction.y, p[1].direction.x));
        while (angle > twoPi) angle = angle - twoPi;
        if (angle > kPi) angle = twoPi - angle;
        float distance = Length2D(p[0].position - p[1].position);
        float turnTime = angle / rate;
        float wayTime = distance / p[0].speed;
        if (turnTime > wayTime) p[0].speed = distance / turnTime;
    }
}

// ---- The unit's save ------------------------------------------------------

void UnitMotion::LoadState(ByteStream* stream) {
    segment = -1;
    points.resize(stream->ReadCount());
    for (int i = 0; i < std::ssize(points); ++i) stream->ReadValue(points[i]);
    waypoints.resize(stream->ReadCount());
    for (int i = 0; i < std::ssize(waypoints); ++i) stream->ReadValue(waypoints[i]);
    stepSpeeds.resize(stream->ReadCount());
    for (float& step : stepSpeeds) stream->ReadValue(step);
    stream->ReadValue(speedScale);
    stream->ReadValue(baseTurnRate);
    stream->ReadValue(turnRate);
    stream->ReadValue(startAngle);
    stream->ReadValue(turn);
}

void UnitMotion::SaveState(ByteStream* stream) {
    stream->WriteCount(points.size());
    for (int i = 0; i < std::ssize(points); ++i) stream->WriteValue(points[i]);
    stream->WriteCount(waypoints.size());
    for (int i = 0; i < std::ssize(waypoints); ++i) stream->WriteValue(waypoints[i]);
    stream->WriteCount(stepSpeeds.size());
    for (const float& step : stepSpeeds) stream->WriteValue(step);
    // Copied bit for bit.
    const float* const fields[] = {&speedScale, &baseTurnRate, &turnRate, &startAngle, &turn};
    for (const float* field : fields) {
        std::uint32_t value;
        value = std::bit_cast<std::uint32_t>(*field);
        stream->WriteValue(value);
    }
}
