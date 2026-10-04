// CCamera helpers the per-frame update uses: aiming along a direction (004110D0), turning the
// camera upright towards its target (0040D710) and keeping it above nearby objects (0040E4C0).
// See core_transform.h and docs/notes/unmapped_410_470.md ("Seventh round").
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the order
// of operations and the NaN compares stay as written.
#include "core_transform.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
float GameFloat(std::uintptr_t a) { return *reinterpret_cast<const volatile float*>(a); }
double GameDouble(std::uintptr_t a) { return *reinterpret_cast<const volatile double*>(a); }
float FloatAt(const void* object, std::size_t offset) {
    float v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(object) + offset, 4);
    return v;
}
#define EI_ONE GameFloat(0x0073B84C)         // 1.0f
#define EI_MINUS_ONE GameFloat(0x0073B854)   // -1.0f
#define EI_ZERO GameFloat(0x0073B858)        // 0.0f
#define EI_TENTH GameFloat(0x0073BA80)       // 0.1f
#define EI_PI GameDouble(0x0073B9A0)
#define EI_PER_180 GameDouble(0x0073BA30)    // 1/180
#define EI_NINETY GameDouble(0x0073BAB8)     // 90
#define EI_ONE_D GameDouble(0x0073BAC8)      // 1.0

// _CIacos / _CIasin (argument and result in ST0) and the CRT function at 006FD026 (two doubles,
// result in ST0; the hypotenuse here).
__declspec(naked) double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}
__declspec(naked) double __cdecl CIasin(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E83C0
    __asm call eax
    __asm ret
}
__declspec(naked) double __cdecl Hypot(double /*x*/, double /*y*/) EI_ORIGINAL(0x006FD026)

// The world's object query (0x7AF5B8): the objects within `radius` of `p` into an STLport list.
__declspec(naked) void __fastcall ObjectsNear(void* /*world*/, void*, const Vector3* /*p*/, float /*radius*/, void* /*list*/) EI_ORIGINAL(0x0056DAF0)
// STLport list<CEngineObject*> constructor (library).
__declspec(naked) void __fastcall ObjectListConstruct(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00411850)
constexpr std::uintptr_t kWorldObjects = 0x007AF5B8;
// The node allocator's free list for 12-byte nodes.
void*& NodeFreeList() { return *reinterpret_cast<void**>(0x0079B228); }

// FCOMP then TEST AH,0x40 (C3): equal, or unordered.
bool EqualOrUnordered(double a, double b) { return !(a < b) && !(a > b); }

// The identity rotation the engine keeps at 0079B5B8.
const void* IdentityRotation() { return reinterpret_cast<const void*>(0x0079B5B8); }

} // namespace

// 004110D0: aims the camera along (dx, dy, dz): the view heading is the turn about Z that points
// the camera's X axis along the horizontal part (identity when that is straight along Y), then the
// local rotation tilts by -(90 degrees + elevation) about its own X axis. False (and nothing done)
// for a direction shorter than 1e-9.
bool CCamera::LookDirection(float dx, float dy, float dz) {
    double lengthSquare = (D(dx) * dx + D(dy) * dy) + D(dz) * dz;
    if (!(lengthSquare > GameDouble(0x0073BAE8))) return false;
    double inv = EI_ONE_D / x87::fsqrt(lengthSquare);
    float invRounded = Store(inv);
    float nx = Store(inv * dx);
    float ny = Store(D(invRounded) * dy);
    float elevation = Store(CIasin(-(D(invRounded) * dz)));
    float heading = Store(CIacos(D(ny) / Hypot(nx, ny)));
    if (EqualOrUnordered(nx, EI_ZERO) || EqualOrUnordered(heading, EI_ZERO)) {
        std::memcpy(&viewRotation_, IdentityRotation(), sizeof(Quaternion));
    } else {
        double side = -D(nx);
        float z;
        if (!(side >= D(EI_ZERO))) z = EI_MINUS_ONE;
        else if (side > D(EI_ZERO)) z = EI_ONE;
        else z = EI_ZERO;
        Vector3 axis;
        axis.x = 0.0f;
        axis.y = 0.0f;
        axis.z = z;
        viewRotation_.FromAxisAngle(axis, heading);
    }
    std::memcpy(&localRotation_, &viewRotation_, sizeof(Quaternion));
    const Quaternion& q = localRotation_;
    double xx = D(q.x) * q.x;
    double e1 = D(q.w) * q.z + D(q.x) * q.y;
    double e2 = D(q.x) * q.z - D(q.w) * q.y;
    Vector3 xAxis;
    xAxis.x = Store((D(q.w) * q.w - ((D(q.y) * q.y + D(q.z) * q.z) + xx)) + (xx + xx));
    xAxis.y = Store(e1 + e1);
    xAxis.z = Store(e2 + e2);
    float angle = Store(-((EI_PI * EI_PER_180) * EI_NINETY + D(elevation)));
    Quaternion turn;
    const Quaternion& r = *turn.SetAngleAxis(angle, xAxis);
    Quaternion& t = localRotation_;
    double x = ((D(t.z) * r.y - D(t.y) * r.z) + D(r.w) * t.x) + D(t.w) * r.x;
    float y = Store(((D(t.x) * r.z - D(t.z) * r.x) + D(r.w) * t.y) + D(t.w) * r.y);
    float zz = Store(((D(t.y) * r.x - D(r.y) * t.x) + D(r.w) * t.z) + D(t.w) * r.z);
    double w = D(r.w) * t.w - ((D(r.y) * t.y + D(t.z) * r.z) + D(t.x) * r.x);
    t.z = zz;
    t.y = y;
    t.w = Store(w);
    t.x = Store(x);
    return true;
}

// 0040D710: turns the camera a tenth of the way about its own X-axis-and-horizontal cross product
// so that its X axis lines up with the horizontal direction across the view (Z axis turned a
// quarter about +Z); nothing below one degree. Then lifts the camera over nearby objects.
void CCamera::AlignToTarget() {
    Quaternion& q = localRotation_;
    Vector3 zAxis, xAxis;
    q.AxisZ(&zAxis);
    q.AxisX(&xAxis);
    double negZx = -D(zAxis.x);
    float hy = Store(negZx);
    float hz = 0.0f;
    double inv = D(EI_ONE) / x87::fsqrt(negZx * hy + D(zAxis.y) * zAxis.y);
    float invRounded = Store(inv);
    float hx = Store(inv * zAxis.y);
    hy = Store(D(hy) * invRounded);
    double dot = (D(xAxis.z) * hz + D(xAxis.y) * hy) + D(xAxis.x) * hx;
    float cosine = Store(dot);
    if (dot > D(EI_ONE)) cosine = 1.0f;
    else if (!(D(cosine) >= D(EI_MINUS_ONE))) cosine = -1.0f;
    float angle = Store(CIacos(cosine));
    float magnitude = angle;
    std::uint32_t bits;
    std::memcpy(&bits, &magnitude, 4);
    bits &= 0x7FFFFFFF;
    std::memcpy(&magnitude, &bits, 4);
    if (EI_PI * EI_PER_180 > D(magnitude)) return;
    float cx = Store(D(xAxis.y) * hz - D(xAxis.z) * hy);
    float cy = Store(D(xAxis.z) * hx - D(hz) * xAxis.x);
    float cz = Store(D(hy) * xAxis.x - D(xAxis.y) * hx);
    double invC = D(EI_ONE) / x87::fsqrt((D(cz) * cz + D(cy) * cy) + D(cx) * cx);
    Vector3 axis;
    axis.x = Store(D(cx) * invC);
    axis.y = Store(D(cy) * invC);
    axis.z = Store(D(cz) * invC);
    double axisLength = x87::fsqrt((D(axis.z) * axis.z + D(axis.y) * axis.y) + D(axis.x) * axis.x);
    if (axisLength > GameDouble(0x0073BA90)) {
        Quaternion turn;
        const Quaternion& r = *turn.SetAngleAxis(Store(D(angle) * D(EI_TENTH)), axis);
        double x = ((D(r.y) * q.z - D(q.y) * r.z) + D(q.x) * r.w) + D(r.x) * q.w;
        float y = Store(((D(q.x) * r.z - D(r.x) * q.z) + D(q.y) * r.w) + D(r.y) * q.w);
        float z = Store(((D(q.y) * r.x - D(r.y) * q.x) + D(r.w) * q.z) + D(r.z) * q.w);
        double w = D(r.w) * q.w - ((D(q.x) * r.x + D(r.z) * q.z) + D(r.y) * q.y);
        q.z = z;
        q.y = y;
        q.w = Store(w);
        q.x = Store(x);
    }
    ClampAboveObjects();
}

// 0040E4C0: keeps the camera above the terrain and the objects within 16 of it: an object (all
// extents up to 100) counts with its top height, fully when the camera is inside its footprint
// radius and fading out over the next 10; the camera height becomes at least the highest of these
// (raised over the highest terrain by at most settings->maxRaise) plus settings->clearance.
void CCamera::ClampAboveObjects() {
    Vector3* p = &localPosition_;
    float average, lowest, highest;
    const Vector3* zero = reinterpret_cast<const Vector3*>(0x0079B5C8);
    SampleTerrain(p, zero, zero, 1.0f, 2.0f, 2.0f, &average, &lowest, &highest);
    void** list[1];
    std::uint8_t alloc = 0;
    ObjectListConstruct(list, nullptr, &alloc);
    ObjectsNear(reinterpret_cast<void*>(kWorldObjects), nullptr, p, 16.0f, list);
    float best = 0.0f;
    void** sentinel = list[0];
    for (void** node = static_cast<void**>(sentinel[0]); node != list[0]; node = static_cast<void**>(node[0])) {
        const void* object = node[2];
        const float limit = GameFloat(0x0073BAB0);
        if (FloatAt(object, 0x144) > limit || FloatAt(object, 0x148) > limit || FloatAt(object, 0x14C) > limit ||
            FloatAt(object, 0x130) > limit)
            continue;
        float cx = Store(D(FloatAt(object, 0x1C)) + FloatAt(object, 0x128));
        float cy = Store(D(FloatAt(object, 0x20)) + FloatAt(object, 0x12C));
        float cz = p->z;
        double radius = Hypot(FloatAt(object, 0x144), FloatAt(object, 0x148));
        double dx = D(cx) - p->x;
        double dy = D(cy) - p->y;
        double dz = D(cz) - p->z;
        float outside = Store(x87::fsqrt((dz * dz + dy * dy) + dx * dx) - radius);
        double factor = EI_ZERO;
        if (!(outside > EI_ZERO)) factor = EI_ONE;
        else if (!(outside > GameFloat(0x0073BAAC))) factor = D(EI_ONE) - D(outside) * D(EI_TENTH);
        float height = Store(((D(FloatAt(node[2], 0x24)) + FloatAt(object, 0x14C)) + FloatAt(object, 0x130)) * factor);
        if (!(best >= height)) best = height;
    }
    CameraSettings* s = settings_;
    float raised;
    if (best > highest) {
        float over = Store(D(best) - D(highest));
        float limit = s->maxRaise;
        float used = !(over >= limit) ? over : limit;
        float wanted = Store((D(used) + highest) + D(s->clearance));
        float z = p->z;
        raised = !(z >= wanted) ? wanted : z;
    } else {
        float top = !(best >= highest) ? highest : best;
        float wanted = Store(D(top) + D(s->clearance));
        float z = p->z;
        raised = !(z >= wanted) ? wanted : z;
    }
    p->z = raised;
    // The list's destructor, inlined: every node and the sentinel back to the node pool.
    for (void** node = static_cast<void**>(list[0][0]); node != list[0];) {
        void** next = static_cast<void**>(node[0]);
        if (node) {
            node[0] = NodeFreeList();
            NodeFreeList() = node;
        }
        node = next;
    }
    list[0][0] = list[0];
    list[0][1] = list[0];
    if (list[0]) {
        list[0][0] = NodeFreeList();
        NodeFreeList() = list[0];
    }
}

namespace {
bool __fastcall LookDirectionHook(CCamera* self, void*, float dx, float dy, float dz) { return self->LookDirection(dx, dy, dz); }
void __fastcall AlignToTargetHook(CCamera* self) { self->AlignToTarget(); }
void __fastcall ClampAboveObjectsHook(CCamera* self) { self->ClampAboveObjects(); }
} // namespace
EI_REPLACE(0x004110D0, LookDirectionHook);
EI_REPLACE(0x0040D710, AlignToTargetHook);
EI_REPLACE(0x0040E4C0, ClampAboveObjectsHook);
