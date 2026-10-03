// A unit's motion: the order header of AIPath (005C7B00-005C7DA9, 005B23A0) and the trajectory at
// CUnit + 0x27C that the unit follows (005C6270-005C7843). The trajectory is rebuilt when a path is
// advanced (Build) and sampled every tick by the unit's movement (Sample).
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, StoreFloat where it stores to a float.
#include "ai_motion.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>

namespace {
// The game's CArray template instantiations for the trajectory arrays.
__declspec(naked) int __fastcall WaypointsAdd(GameArray<MotionWaypoint>*, void*, const MotionWaypoint*)
    EI_ORIGINAL(0x005C8400)
__declspec(naked) void __fastcall WaypointsSetAtGrow(GameArray<MotionWaypoint>*, void*, int, const MotionWaypoint*)
    EI_ORIGINAL(0x005C87F0)
__declspec(naked) void __fastcall StepSpeedsSetAtGrow(GameArray<float>*, void*, int, float) EI_ORIGINAL(0x005C8950)
__declspec(naked) void __fastcall PointsSetSize(GameArray<MotionPoint>*, void*, int, int) EI_ORIGINAL(0x005C8560)

// FPATAN: atan2(y, x), unrounded.
__declspec(naked) double __cdecl Fpatan(double, double) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm fpatan
    __asm ret
}

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// x87 FCOM followed by TEST AH,0x40: "equal", which unordered (NaN) operands also are.
bool Equal(double a, double b) { return !(a < b) && !(a > b); }

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

float AsFloat(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void CopyBits(float* dst, const void* src) { std::memcpy(dst, src, sizeof(float)); }

// The turn direction factors the original loads from memory (-1, 0, 1). Read through volatile so
// the compiler cannot fold 0 * x to 0 (the original multiplies, which keeps NaN and -0).
const volatile float kMinusOne = -1.0f;
const volatile float kZero = 0.0f;
const volatile float kOne = 1.0f;

constexpr float kPi = 3.14159274f;          // 0073FE44
constexpr double kHalfCellScale = 2.0;      // 1 / (cell size 1.0 * 0.5), computed by the original
constexpr float kSpeedUnit = 0.001953125f;  // 0073CC90: 1/512
constexpr float kMinLength = 9.99999975e-06f; // 0073CDF0

// Dir[(dy * 3 + dx)] for neighbouring cells: 1..8 counter-clockwise from (0, -1); 0 for the cell
// itself. The table at 00791988 is indexed from its middle.
const int* DirectionTable() { return reinterpret_cast<const int*>(0x00791988); }
int Direction(const PathCell& from, const PathCell& to) {
    return DirectionTable()[(to.y - from.y) * 3 + to.x - from.x];
}
// Difference between two directions in 45-degree steps (0..4).
int DirectionDelta(int a, int b) {
    int d = a - b;
    if (d < 0) d = -d;
    if (d > 4) d = 8 - d;
    return d;
}

void ClearList(PathCellList& list) {
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(list.blocks));
    list.blocks = nullptr;
}

void FaceAngle(float angle, float* direction) {
    direction[0] = Store(x87::fcos(angle));
    direction[1] = Store(x87::fsin(angle));
}

// Turning in place: the facing after `elapsed` of turning `turn` from `start` at `rate`; `final`
// when the turn takes no longer than that. Used by the order kinds 1 and 4.
double TurnAngle(const UnitMotion* m, float elapsed, bool* final) {
    float ratio = AbsBits(Store(static_cast<double>(m->turn) / m->turnRate));
    if (!(ratio >= elapsed)) {
        *final = true;
        return static_cast<double>(m->startAngle) + m->turn;
    }
    *final = false;
    float sign = m->turn < 0.0f || x87::IsNaN(m->turn) ? kMinusOne : (m->turn > 0.0f ? kOne : kZero);
    return static_cast<double>(sign) * elapsed * m->turnRate + m->startAngle;
}
} // namespace

// --- The order (AIPath header) --------------------------------------------------------------------

int AIPath::Reset() {
    MotionOrder* order = OrderOf(this);
    order->kind = 0;
    order->x = 0.0f;
    order->y = 0.0f;
    order->arg5 = 0;
    order->arg6 = 0;
    order->arg7 = 0;
    order->angle = 0.0f;
    order->startTime = 0;
    ClearList(steps);
    return 0;
}
EI_REPLACE_METHOD(0x005C7D50, AIPath::Reset);

bool AIPath::Start(const Vector3* position, const Vector3* direction, int time) {
    MotionOrder* order = OrderOf(this);
    double dx = direction->x;
    float dy = direction->y;
    double length = x87::fsqrt(static_cast<double>(dy) * dy + dx * dx);
    double angle = length > 0.0 ? Fpatan(dy / length, dx / length) : 0.0;
    double x = kHalfCellScale * position->x;
    float y = Store(kHalfCellScale * position->y);
    bool same = order->kind == 0 && Equal(order->angle, angle) && Equal(x, order->x) && Equal(order->y, y);
    order->x = Store(x);
    order->angle = Store(angle);
    order->kind = 0;
    order->y = y;
    order->startTime = time;
    ClearList(steps);
    return !same;
}
EI_REPLACE_METHOD(0x005C7B00, AIPath::Start);

void AIPath::SetTurn(const Vector3* direction, float rate) {
    MotionOrder* order = OrderOf(this);
    order->kind = 1;
    float angle = Store(Fpatan(direction->y, direction->x));
    std::memcpy(&order->arg6, &rate, sizeof(rate));
    std::memcpy(&order->arg5, &angle, sizeof(angle));
}
EI_REPLACE_METHOD(0x005C7C00, AIPath::SetTurn);

void AIPath::SetTrack(int id, int from, int to) {
    MotionOrder* order = OrderOf(this);
    order->arg5 = static_cast<std::uint32_t>(id);
    order->kind = 3;
    order->arg6 = static_cast<std::uint32_t>(from);
    order->arg7 = static_cast<std::uint32_t>(to);
}
EI_REPLACE_METHOD(0x005C7C20, AIPath::SetTrack);

void AIPath::SetTurnUntil(const Vector3* direction, int arg6, int arg7) {
    MotionOrder* order = OrderOf(this);
    order->kind = 4;
    order->arg7 = static_cast<std::uint32_t>(arg7);
    float angle = Store(Fpatan(direction->y, direction->x));
    order->arg6 = static_cast<std::uint32_t>(arg6);
    std::memcpy(&order->arg5, &angle, sizeof(angle));
}
EI_REPLACE_METHOD(0x005C7C40, AIPath::SetTurnUntil);

void AIPath::SetTarget(float x, float y) {
    MotionOrder* order = OrderOf(this);
    order->kind = 2;
    std::memcpy(&order->arg5, &x, sizeof(x));
    std::memcpy(&order->arg6, &y, sizeof(y));
}
EI_REPLACE_METHOD(0x005C7C70, AIPath::SetTarget);

void AIPath::ForgetObject(int id) {
    MotionOrder* order = OrderOf(this);
    if (order->kind == 3 && order->arg5 == static_cast<std::uint32_t>(id)) order->arg5 = 0xFFFFFFFF;
}
EI_REPLACE_METHOD(0x005C7C90, AIPath::ForgetObject);

void AIPath::ShiftTime(int delta) {
    MotionOrder* order = OrderOf(this);
    order->startTime += delta;
    if (order->kind == 3) {
        order->arg6 += static_cast<std::uint32_t>(delta);
        order->arg7 += static_cast<std::uint32_t>(delta);
    }
}
EI_REPLACE_METHOD(0x005C7CB0, AIPath::ShiftTime);

int AIPath::SetStepSpeeds(std::uint16_t speed) {
    for (PathCellList::Node* node = steps.head; node; node = node->next) node->data.flags = speed;
    return 0;
}
EI_REPLACE_METHOD(0x005C7CE0, AIPath::SetStepSpeeds);

Vector3* AIPath::Destination(Vector3* out) {
    const MotionOrder* order = OrderOf(this);
    double scale = 1.0f * 0.5f;     // cell size * 0.5
    if (order->kind != 2) {
        out->x = Store(scale * order->x);
        out->z = 0.0f;
        out->y = Store(scale * order->y);
    } else {
        out->x = Store(scale * AsFloat(order->arg5));
        out->z = 0.0f;
        out->y = Store(scale * AsFloat(order->arg6));
    }
    return out;
}
EI_REPLACE_METHOD(0x005C7D00, AIPath::Destination);

void AIPath::Clear() {
    ClearList(cells);
    Reset();
    targetX = 0.0f;
    targetY = 0.0f;
    reached = true;
    failed = false;
    flag6E = false;
    flag6F = true;
    void*& buffer = *reinterpret_cast<void**>(unknown_70 + 4);     // +0x74
    if (buffer) {
        gamecrt::operator_delete(buffer);
        buffer = nullptr;
    }
    *reinterpret_cast<std::uint32_t*>(unknown_70 + 0xC) = 0;        // +0x7C
    *reinterpret_cast<std::uint32_t*>(unknown_70 + 8) = 0;          // +0x78
    smoothing = 0;
}
EI_REPLACE_METHOD(0x005B23A0, AIPath::Clear);

// --- The trajectory --------------------------------------------------------------------------------

__declspec(naked) double MotionCubic::Value(float) EI_ORIGINAL(0x004E5660)

void MotionCubic::Fit(float p0, float d0, float p1, float d1, float length) {
    double inverse = 1.0f / length;
    CopyBits(&c0, &p0);
    CopyBits(&c1, &d0);
    double half = ((static_cast<double>(p1) - p0) - static_cast<double>(d0) * length) * inverse;
    half = half + half;
    double cubic = ((static_cast<double>(d1) - d0) - half) * inverse * inverse;
    c3 = Store(cubic);
    double scaled = cubic * length * length;
    scaled = scaled + scaled;
    c2 = Store((half - scaled) * inverse * 0.5f);
}
EI_REPLACE_METHOD(0x005C73B0, MotionCubic::Fit);

double MotionCubic::Slope(float u) {
    double c2x2 = static_cast<double>(c2) + c2;
    return (static_cast<double>(u) * c3 * 3.0f + c2x2) * u + c1;
}
EI_REPLACE_METHOD(0x005C7410, MotionCubic::Slope);

int UnitMotion::Clear() {
    segment = -1;
    points.size = 0;
    waypoints.size = 0;
    stepSpeeds.size = 0;
    return 0;
}
EI_REPLACE_METHOD(0x005C6270, UnitMotion::Clear);

bool UnitMotion::Sample(void* world, MotionOrder* order, float time, bool* turning, float* position,
                        float* direction, float* speedOut, int* cellX, int* cellY) {
    *cellX = -1;
    *cellY = -1;
    const std::uint32_t kDefaultSpeed = 0x3DCCCCCD;    // 0.1f
    std::memcpy(speedOut, &kDefaultSpeed, sizeof(kDefaultSpeed));
    direction[2] = 0.0f;
    *turning = false;
    double start = order->startTime;
    if (!(time >= start)) time = Store(start);
    CopyBits(&position[0], &order->x);
    CopyBits(&position[1], &order->y);

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
        FaceAngle(order->angle, direction);
        return true;
    case 1: {
        *turning = true;
        bool final;
        double angle = TurnAngle(this, Store(time - start), &final);
        direction[0] = Store(x87::fcos(angle));
        direction[1] = Store(x87::fsin(angle));
        return !final;
    }
    case 2:
        return SamplePath(Store(time - start), turning, position, direction, speedOut, cellX, cellY);
    case 3: {
        void* object = ei::VCall<void*>(world, 0xA0 / 4, static_cast<int>(order->arg5));
        if (!object) {
            FaceAngle(order->angle, direction);
            return true;
        }
        int from = static_cast<int>(order->arg6);
        float buffer[3];
        const float* where;
        if (static_cast<double>(from) >= time) where = ei::VCall<const float*>(object, 0xD0 / 4, buffer, from);
        else where = ei::VCall<const float*>(object, 0xD4 / 4, buffer);
        float target[3];
        std::memcpy(target, where, sizeof(target));
        float tx = Store(kHalfCellScale * target[0]);
        double ty = kHalfCellScale * target[1];
        int to = static_cast<int>(order->arg7);
        if (static_cast<double>(to) > time && to != order->startTime) {
            // Still turning towards it: interpolate the facing over [start, to].
            double angleTo = Fpatan(ty - order->y, static_cast<double>(tx) - order->x) - order->angle;
            const double twoPi = static_cast<double>(kPi) + kPi;
            while (angleTo > kPi) angleTo = angleTo - twoPi;
            const float negPi = -kPi;
            while (!(angleTo >= negPi)) angleTo = angleTo + twoPi;
            double angle = (time - start) * angleTo / static_cast<double>(to - order->startTime) + order->angle;
            direction[0] = Store(x87::fcos(angle));
            direction[1] = Store(x87::fsin(angle));
            return true;
        }
        direction[0] = Store(static_cast<double>(tx) - order->x);
        double dy = ty - order->y;
        direction[1] = Store(dy);
        double norm = x87::fsqrt(dy * dy + static_cast<double>(direction[0]) * direction[0]);
        if (Equal(norm, 0.0)) return true;
        double inverse = 1.0f / norm;
        direction[0] = Store(inverse * direction[0]);
        direction[1] = Store(inverse * direction[1]);
        direction[2] = Store(inverse * direction[2]);
        return true;
    }
    case 4: {
        bool final;
        double angle = TurnAngle(this, Store(time - start), &final);
        direction[0] = Store(x87::fcos(angle));
        direction[1] = Store(x87::fsin(angle));
        *turning = !final;
        return true;
    }
    default:
        return true;
    }
}
EI_REPLACE_METHOD(0x005C7430, UnitMotion::Sample);

bool UnitMotion::SamplePath(float time, bool* turning, float* position, float* direction, float* speedOut,
                            int* cellX, int* cellY) {
    double end = 0.0;
    int index = 0;
    bool found = false;
    if (segment != -1 && time >= segmentStart && !(time > segmentEnd)) {
        found = true;   // still in the cached segment
        index = segment;
    } else if (segment != -1 && !(segmentStart > time)) {
        end = segmentStart;     // later: walk on from the cached segment
        index = segment;
    } else {
        // Before the path: turn in place first.
        *turning = true;
        turnRate = baseTurnRate;
        float turnTime = AbsBits(Store(static_cast<double>(turn) / baseTurnRate));
        if (!(time > turnTime)) {
            float ratio = AbsBits(Store(static_cast<double>(turn) / turnRate));
            double angle;
            if (!(ratio >= time)) {
                angle = static_cast<double>(startAngle) + turn;
            } else {
                float sign = turn < 0.0f || x87::IsNaN(turn) ? kMinusOne : (turn > 0.0f ? kOne : kZero);
                angle = static_cast<double>(sign) * turnRate * time + startAngle;
            }
            direction[0] = Store(x87::fcos(angle));
            direction[1] = Store(x87::fsin(angle));
            return true;
        }
        end = turnTime;
        index = 0;
    }

    if (!found) {
        *turning = false;
        float saved = segmentStart;
        for (; index < points.size - 1; ++index) {
            segmentStart = Store(end);
            const MotionPoint& from = points.data[index];
            const MotionPoint& to = points.data[index + 1];
            std::memcpy(&fromX, &from.x, 4 * sizeof(float));     // x, y, dirX, dirY
            std::memcpy(&toX, &to.x, 4 * sizeof(float));
            double dx = static_cast<double>(fromX) - toX;
            double dy = static_cast<double>(fromY) - toY;
            double distance = x87::fsqrt(dy * dy + dx * dx);
            length = Store(distance);
            CopyBits(&speed, &from.speed);
            end = end + distance / speed;
            if (!(time > end)) {
                found = true;
                break;
            }
        }
        if (!found) {
            // Past the end: stand at the last point.
            segmentStart = saved;
            std::memset(speedOut, 0, sizeof(float));
            const MotionPoint& last = points.data[points.size - 1];
            CopyBits(&position[0], &last.x);
            CopyBits(&position[1], &last.y);
            CopyBits(&direction[0], &last.dirX);
            direction[2] = 0.0f;
            CopyBits(&direction[1], &last.dirY);
            *cellX = last.cellX;
            *cellY = last.cellY;
            return false;
        }
        curveX.Fit(fromX, fromDirX, toX, toDirX, length);
        curveY.Fit(fromY, fromDirY, toY, toDirY, length);
        segmentEnd = Store(end);
        segment = index;
    }

    *turning = false;
    CopyBits(speedOut, &speed);
    float u = Store((time - static_cast<double>(segmentStart)) * speed);
    position[0] = Store(curveX.Value(u));
    direction[0] = Store(curveX.Slope(u));
    position[1] = Store(curveY.Value(u));
    direction[1] = Store(curveY.Slope(u));
    *cellX = points.data[segment + 1].cellX;
    *cellY = points.data[segment + 1].cellY;
    return true;
}
EI_REPLACE_METHOD(0x005C6FC0, UnitMotion::SamplePath);

namespace {
// Brings `turn` (stored as float) into [-pi, pi] the way 005C67D0 does: the first comparison is
// on the unrounded difference, the later ones on each unrounded step.
void NormalizeTurn(UnitMotion* m, double turn) {
    m->turn = Store(turn);
    const double twoPi = static_cast<double>(kPi) + kPi;
    if (turn > kPi) {
        double v;
        do {
            v = static_cast<double>(m->turn) - twoPi;
            m->turn = Store(v);
        } while (v > kPi);
    }
    const float negPi = -kPi;
    if (negPi > m->turn) {
        double v;
        do {
            v = twoPi + m->turn;
            m->turn = Store(v);
        } while (!(v >= negPi));
    }
}

MotionWaypoint Waypoint(double x, double y) { return {Store(x), Store(y), 0}; }
} // namespace

void UnitMotion::Build(AIPath* path) {
    MotionOrder* order = OrderOf(path);
    segment = -1;
    points.size = 0;
    waypoints.size = 0;
    stepSpeeds.size = 0;
    CopyBits(&startAngle, &order->angle);

    switch (static_cast<unsigned>(order->kind)) {
    case 0:
    case 3:
        return;
    case 1: {
        float rate = AsFloat(order->arg6);
        float angle = AsFloat(order->arg5);
        float s = Store(x87::fsin(angle));
        double c = x87::fcos(angle);
        float cf = Store(c);
        if (Equal(c, 0.0) && Equal(s, 0.0)) {
            turn = 0.0f;
            return;
        }
        if (!Equal(rate, 0.0)) turnRate = rate;
        else turnRate = baseTurnRate;
        NormalizeTurn(this, Fpatan(s, cf) - startAngle);
        return;
    }
    case 4: {
        float angle = AsFloat(order->arg5);
        double s = x87::fsin(angle);
        double c = x87::fcos(angle);
        if (Equal(c, 0.0) && Equal(s, 0.0)) {
            turn = 0.0f;
            return;
        }
        turnRate = baseTurnRate;
        NormalizeTurn(this, Fpatan(s, c) - startAngle);
        return;
    }
    default:
        break;
    }

    // Kind 2 (and anything else): waypoints at the start, at each corner of the steps and at the
    // target, then one trajectory point per path cell.
    MotionWaypoint first;
    CopyBits(&first.x, &order->x);
    CopyBits(&first.y, &order->y);
    first.cells = 0;
    WaypointsSetAtGrow(&waypoints, nullptr, waypoints.size, &first);
    int previous = 0;
    for (PathCellList::Node* node = path->steps.head; node;) {
        PathCellList::Node* next = node->next;
        ++waypoints.data[waypoints.size - 1].cells;
        StepSpeedsSetAtGrow(&stepSpeeds, nullptr, stepSpeeds.size, Store(static_cast<double>(node->data.flags)));
        if (!next) break;
        PathCellList::Node* afterNext = next->next;
        const PathCell& cell = node->data;
        double cx = cell.x, cy = cell.y;
        int dir = Direction(cell, next->data);
        MotionWaypoint w;
        switch (dir) {
        case 2:
            w = Waypoint(cx, cy);
            WaypointsSetAtGrow(&waypoints, nullptr, waypoints.size, &w);
            break;
        case 4:
            w = Waypoint(cx, cy + 1.0f);
            WaypointsSetAtGrow(&waypoints, nullptr, waypoints.size, &w);
            break;
        case 6:
            w = Waypoint(cx + 1.0f, cy + 1.0f);
            WaypointsSetAtGrow(&waypoints, nullptr, waypoints.size, &w);
            break;
        case 8:
            w = Waypoint(cx + 1.0f, cy);
            WaypointsAdd(&waypoints, nullptr, &w);
            break;
        default:
            break;
        }

        bool corner = previous != 0 && DirectionDelta(dir, previous) > 1;
        if (!corner && afterNext) corner = DirectionDelta(dir, Direction(next->data, afterNext->data)) > 1;
        if (corner) {
            switch (dir) {
            case 1:
                w = Waypoint(cx + 0.5f, cy);
                WaypointsAdd(&waypoints, nullptr, &w);
                break;
            case 3:
                w = Waypoint(cx, cy + 0.5f);
                WaypointsSetAtGrow(&waypoints, nullptr, waypoints.size, &w);
                break;
            case 5:
                w = Waypoint(cx + 0.5f, cy + 1.0f);
                WaypointsAdd(&waypoints, nullptr, &w);
                break;
            case 7:
                w = Waypoint(cx + 1.0f, cy + 0.5f);
                WaypointsAdd(&waypoints, nullptr, &w);
                break;
            default:
                break;
            }
        }
        previous = dir;
        node = next;
    }

    MotionWaypoint last;
    std::memcpy(&last.x, &order->arg5, sizeof(float));
    std::memcpy(&last.y, &order->arg6, sizeof(float));
    last.cells = 0;
    WaypointsAdd(&waypoints, nullptr, &last);
    Subdivide();
    LimitSpeeds(baseTurnRate);

    // Face the first point: no turn when it is within ~25 degrees of the current facing.
    MotionPoint* p = points.data;
    if (x87::fcos(Fpatan(p->dirY, p->dirX) - startAngle) > 0.9) {
        turn = 0.0f;
        p->dirX = Store(x87::fcos(startAngle));
        points.data->dirY = Store(x87::fsin(startAngle));
        AssignCells(path);
        return;
    }
    if (Equal(p->dirX, 0.0) && Equal(p->dirY, 0.0)) {
        turn = 0.0f;
        AssignCells(path);
        return;
    }
    double angle = Fpatan(p->dirY, p->dirX);
    turnRate = baseTurnRate;
    NormalizeTurn(this, angle - startAngle);
    AssignCells(path);
}
EI_REPLACE_METHOD(0x005C67D0, UnitMotion::Build);

void UnitMotion::AssignCells(AIPath* path) {
    PathCellList::Node* head = path->steps.head;
    int i = 0;
    for (PathCellList::Node* node = head; node;) {
        PathCellList::Node* next = node->next;
        if (i >= points.size) break;
        points.data[i].cellX = node->data.x;
        points.data[i].cellY = node->data.y;
        ++i;
        node = next;
    }
    if (path->steps.count == 1) {
        MotionPoint& last = points.data[points.size - 1];
        last.cellX = head->data.x;
        last.cellY = head->data.y;
    }
}
EI_REPLACE_METHOD(0x005C6750, UnitMotion::AssignCells);

void UnitMotion::Subdivide() {
    int stepIndex = 0;
    for (int i = 0; i < waypoints.size - 1; ++i) {
        float startOffset = i == 0 ? -0.5f : 0.0f;
        float endOffset = i == waypoints.size - 2 ? 0.5f : 0.0f;
        MotionWaypoint* w = &waypoints.data[i];
        SubdivideSegment(w, w + 1, w->cells, startOffset, endOffset, &stepIndex);
    }
}
EI_REPLACE_METHOD(0x005C66C0, UnitMotion::Subdivide);

void UnitMotion::SubdivideSegment(const MotionWaypoint* a, const MotionWaypoint* b, int cells, float startOffset,
                                  float endOffset, int* stepIndex) {
    double dx = static_cast<double>(a->x) - b->x;
    double dy = static_cast<double>(a->y) - b->y;
    double distance = x87::fsqrt(dy * dy + dx * dx);
    if (Equal(distance, 0.0)) distance = kMinLength;
    MotionPoint p;
    p.cellX = -1;
    p.cellY = -1;
    p.dirX = Store((static_cast<double>(b->x) - a->x) / distance);
    p.dirY = Store((static_cast<double>(b->y) - a->y) / distance);

    if (!Equal(startOffset, 0.0) && !Equal(endOffset, 0.0) && cells == 1) {
        // A single cell from start to target: one point at each end.
        CopyBits(&p.x, &a->x);
        CopyBits(&p.y, &a->y);
        int size = points.size;
        p.speed = Store(static_cast<double>(stepSpeeds.data[*stepIndex]) * speedScale * kSpeedUnit);
        PointsSetSize(&points, nullptr, size + 1, -1);
        points.data[size] = p;
        size = points.size;
        CopyBits(&p.x, &b->x);
        CopyBits(&p.y, &b->y);
        PointsSetSize(&points, nullptr, size + 1, -1);
        points.data[size] = p;
        ++*stepIndex;
        return;
    }

    float inverse = Store(1.0f / (static_cast<double>(cells) + startOffset - endOffset));
    for (int k = 0; k < cells; ++k) {
        double f = (static_cast<double>(k) + startOffset + 0.5f) * inverse;
        double g = 1.0f - f;
        p.x = Store(f * b->x + g * a->x);
        p.y = Store(g * a->y + f * b->y);
        p.speed = Store(static_cast<double>(stepSpeeds.data[*stepIndex]) * speedScale * kSpeedUnit);
        int size = points.size;
        PointsSetSize(&points, nullptr, size + 1, -1);
        points.data[size] = p;
        ++*stepIndex;
    }
}
EI_REPLACE_METHOD(0x005C6370, UnitMotion::SubdivideSegment);

void UnitMotion::LimitSpeeds(float rate) {
    const double twoPi = static_cast<double>(kPi) + kPi;
    for (int i = 0; i < points.size - 1; ++i) {
        MotionPoint* p = &points.data[i];
        double a1 = Fpatan(p[0].dirY, p[0].dirX);
        double a2 = Fpatan(p[1].dirY, p[1].dirX);
        double angle = AbsBits(Store(a1 - a2));
        while (angle > twoPi) angle = angle - twoPi;
        if (angle > kPi) angle = twoPi - angle;
        double dx = static_cast<double>(p[0].x) - p[1].x;
        double dy = static_cast<double>(p[0].y) - p[1].y;
        float distance = Store(x87::fsqrt(dy * dy + dx * dx));
        double turnTime = angle / rate;
        double wayTime = static_cast<double>(distance) / p[0].speed;
        if (turnTime > wayTime) p[0].speed = Store(distance / turnTime);
    }
}
EI_REPLACE_METHOD(0x005C6290, UnitMotion::LimitSpeeds);
