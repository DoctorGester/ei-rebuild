// CCamera helpers the per-frame update uses: aiming along a direction (004110D0), turning the
// camera upright towards its target (0040D710) and keeping it above nearby objects (0040E4C0).
// See camera.h and docs/notes/core.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the order
// of operations and the NaN compares stay as written.
#include "camera.h"

#include "map_object_figures.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

using camera::D;
using camera::Store;

// FCOMP then TEST AH,0x40 (C3): equal, or unordered.
bool EqualOrUnordered(double a, double b) { return !(a < b) && !(a > b); }

} // namespace

// 004110D0: aims the camera along (dx, dy, dz): the view heading is the turn about Z that points
// the camera's X axis along the horizontal part (identity when that is straight along Y), then the
// local rotation tilts by -(90 degrees + elevation) about its own X axis. False (and nothing done)
// for a direction shorter than 1e-9.
bool CCamera::LookDirection(float dx, float dy, float dz) {
    double lengthSquare = (D(dx) * dx + D(dy) * dy) + D(dz) * dz;
    if (!(lengthSquare > CAM_LOOK_EPSILON)) return false;
    double inv = CAM_ONE_D / x87::fsqrt(lengthSquare);
    float invRounded = Store(inv);
    float nx = Store(inv * dx);
    float ny = Store(D(invRounded) * dy);
    float elevation = Store(CIasin(-(D(invRounded) * dz)));
    float heading = Store(CIacos(D(ny) / Hypot(nx, ny)));
    if (EqualOrUnordered(nx, CAM_ZERO) || EqualOrUnordered(heading, CAM_ZERO)) {
        std::memcpy(&viewRotation_, &camera::IdentityRotation(), sizeof(Quaternion));
    } else {
        double side = -D(nx);
        float z;
        if (!(side >= D(CAM_ZERO))) z = CAM_MINUS_ONE;
        else if (side > D(CAM_ZERO)) z = CAM_ONE;
        else z = CAM_ZERO;
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
    float angle = Store(-((CAM_PI * CAM_PER_180) * CAM_NINETY + D(elevation)));
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
    double inv = D(CAM_ONE) / x87::fsqrt(negZx * hy + D(zAxis.y) * zAxis.y);
    float invRounded = Store(inv);
    float hx = Store(inv * zAxis.y);
    hy = Store(D(hy) * invRounded);
    double dot = (D(xAxis.z) * hz + D(xAxis.y) * hy) + D(xAxis.x) * hx;
    float cosine = Store(dot);
    if (dot > D(CAM_ONE)) cosine = 1.0f;
    else if (!(D(cosine) >= D(CAM_MINUS_ONE))) cosine = -1.0f;
    float angle = Store(CIacos(cosine));
    float magnitude = angle;
    std::uint32_t bits;
    std::memcpy(&bits, &magnitude, 4);
    bits &= 0x7FFFFFFF;
    std::memcpy(&magnitude, &bits, 4);
    if (CAM_PI * CAM_PER_180 > D(magnitude)) return;
    float cx = Store(D(xAxis.y) * hz - D(xAxis.z) * hy);
    float cy = Store(D(xAxis.z) * hx - D(hz) * xAxis.x);
    float cz = Store(D(hy) * xAxis.x - D(xAxis.y) * hx);
    double invC = D(CAM_ONE) / x87::fsqrt((D(cz) * cz + D(cy) * cy) + D(cx) * cx);
    Vector3 axis;
    axis.x = Store(D(cx) * invC);
    axis.y = Store(D(cy) * invC);
    axis.z = Store(D(cz) * invC);
    double axisLength = x87::fsqrt((D(axis.z) * axis.z + D(axis.y) * axis.y) + D(axis.x) * axis.x);
    if (axisLength > CAM_ALIGN_EPSILON) {
        Quaternion turn;
        const Quaternion& r = *turn.SetAngleAxis(Store(D(angle) * D(CAM_TENTH)), axis);
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
    const Vector3* zero = &camera::ZeroVector();
    SampleTerrain(p, zero, zero, 1.0f, 2.0f, 2.0f, &average, &lowest, &highest);
    NearbyList list;
    std::uint8_t alloc = 0;
    ListConstruct411850(&list, nullptr, &alloc);
    WorldClient()->ObjectsInRange(reinterpret_cast<const float*>(p), 16.0f, &list);
    float best = 0.0f;
    for (NearbyNode* node = list.head->next; node != list.head; node = node->next) {
        const CObject3D* object = node->data;
        const float limit = CAM_OBJECT_EXTENT_LIMIT;
        if (object->above.x > limit || object->above.y > limit || object->above.z > limit ||
            object->center.z > limit)
            continue;
        float cx = Store(D(object->position.x) + object->center.x);
        float cy = Store(D(object->position.y) + object->center.y);
        float cz = p->z;
        double radius = Hypot(object->above.x, object->above.y);
        double dx = D(cx) - p->x;
        double dy = D(cy) - p->y;
        double dz = D(cz) - p->z;
        float outside = Store(x87::fsqrt((dz * dz + dy * dy) + dx * dx) - radius);
        double factor = CAM_ZERO;
        if (!(outside > CAM_ZERO)) factor = CAM_ONE;
        else if (!(outside > CAM_OBJECT_FADE)) factor = D(CAM_ONE) - D(outside) * D(CAM_TENTH);
        float height = Store(((D(object->position.z) + object->above.z) + object->center.z) * factor);
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
    for (NearbyNode* node = list.head->next; node != list.head;) {
        NearbyNode* next = node->next;
        if (node) {
            node->next = static_cast<NearbyNode*>(camera::NodeFreeList());
            camera::NodeFreeList() = node;
        }
        node = next;
    }
    list.head->next = list.head;
    list.head->prev = list.head;
    if (list.head) {
        list.head->next = static_cast<NearbyNode*>(camera::NodeFreeList());
        camera::NodeFreeList() = list.head;
    }
}
EI_REPLACE_METHOD(0x004110D0, CCamera::LookDirection);
EI_REPLACE_METHOD(0x0040D710, CCamera::AlignToTarget);
EI_REPLACE_METHOD(0x0040E4C0, CCamera::ClampAboveObjects);
