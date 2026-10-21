// CCamera helpers the per-frame update uses: aiming along a direction (004110D0), turning the
// camera upright towards its target (0040D710) and keeping it above nearby objects (0040E4C0).
// See camera.h and docs/notes/core.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "camera.h"

#include "map_object_figures.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_library.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

namespace {

using camera::D;

} // namespace

// 004110D0: aims the camera along (dx, dy, dz): the view heading is the turn about Z that points
// the camera's X axis along the horizontal part (identity when that is straight along Y), then the
// local rotation tilts by -(90 degrees + elevation) about its own X axis. False (and nothing done)
// for a direction shorter than 1e-9.
bool CCamera::LookDirection(float dx, float dy, float dz) {
    double lengthSquare = LengthSquaredUnrounded<Sum3::XYZ, double>(dx, dy, dz);
    EI_NAN_AUDIT(lengthSquare);
    if (lengthSquare <= CAM_LOOK_EPSILON) return false;
    double inv = CAM_ONE_D / std::sqrt(lengthSquare);
    float invRounded = static_cast<float>(inv);
    float nx = static_cast<float>(inv * dx);
    float ny = static_cast<float>(D(invRounded) * dy);
    float elevation = static_cast<float>(CIasin(-(D(invRounded) * dz)));
    float heading = static_cast<float>(CIacos(D(ny) / libcrt::Hypot(nx, ny)));
    EI_NAN_AUDIT(nx, heading);
    if (nx == CAM_ZERO || heading == CAM_ZERO) {
        std::memcpy(&viewRotation_, &camera::kIdentityRotation, sizeof(Quaternion));
    } else {
        double side = -D(nx);
        float z;
        EI_NAN_AUDIT(side);
        if (side < D(CAM_ZERO)) z = CAM_MINUS_ONE;
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
    double e1 = Dot2DUnrounded<double>(q.w, q.x, q.z, q.y);
    double e2 = D(q.x) * q.z - D(q.w) * q.y;
    Vector3 xAxis;
    xAxis.x = static_cast<float>((D(q.w) * q.w - (LengthSquared2DUnrounded<double>(q.y, q.z) + xx)) + (xx + xx));
    xAxis.y = static_cast<float>(e1 + e1);
    xAxis.z = static_cast<float>(e2 + e2);
    float angle = static_cast<float>(-(CAM_RIGHT_ANGLE + D(elevation)));
    Quaternion turn;
    const Quaternion& r = *turn.SetAngleAxis(angle, xAxis);
    Quaternion& t = localRotation_;
    double x = ((D(t.z) * r.y - D(t.y) * r.z) + D(r.w) * t.x) + D(t.w) * r.x;
    float y = static_cast<float>(((D(t.x) * r.z - D(t.z) * r.x) + D(r.w) * t.y) + D(t.w) * r.y);
    float zz = static_cast<float>(((D(t.y) * r.x - D(r.y) * t.x) + D(r.w) * t.z) + D(t.w) * r.z);
    double w = D(r.w) * t.w - DotUnrounded<Sum3::ZYX, double>(r.x, r.y, r.z, t.x, t.y, t.z);
    t.z = zz;
    t.y = y;
    t.w = static_cast<float>(w);
    t.x = static_cast<float>(x);
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
    float hy = static_cast<float>(negZx);
    float hz = 0.0f;
    double inv = D(CAM_ONE) / std::sqrt(negZx * hy + D(zAxis.y) * zAxis.y);
    float invRounded = static_cast<float>(inv);
    float hx = static_cast<float>(inv * zAxis.y);
    hy = static_cast<float>(D(hy) * invRounded);
    double dot = DotUnrounded<Sum3::ZYX, double>(xAxis.x, xAxis.y, xAxis.z, hx, hy, hz);
    float cosine = static_cast<float>(dot);
    EI_NAN_AUDIT(dot);
    if (dot > D(CAM_ONE)) cosine = 1.0f;
    else if (D(cosine) < D(CAM_MINUS_ONE)) cosine = -1.0f;
    float angle = static_cast<float>(CIacos(cosine));
    float magnitude = angle;
    std::uint32_t bits;
    std::memcpy(&bits, &magnitude, 4);
    bits &= 0x7FFFFFFF;
    std::memcpy(&magnitude, &bits, 4);
    if (CAM_DEGREE > D(magnitude)) return;
    float cx = static_cast<float>(D(xAxis.y) * hz - D(xAxis.z) * hy);
    float cy = static_cast<float>(D(xAxis.z) * hx - D(hz) * xAxis.x);
    float cz = static_cast<float>(D(hy) * xAxis.x - D(xAxis.y) * hx);
    double invC = D(CAM_ONE) / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(cx, cy, cz));
    Vector3 axis;
    axis.x = static_cast<float>(D(cx) * invC);
    axis.y = static_cast<float>(D(cy) * invC);
    axis.z = static_cast<float>(D(cz) * invC);
    double axisLength = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(axis));
    if (axisLength > CAM_ALIGN_EPSILON) {
        Quaternion turn;
        const Quaternion& r = *turn.SetAngleAxis(static_cast<float>(D(angle) * D(CAM_TENTH)), axis);
        double x = ((D(r.y) * q.z - D(q.y) * r.z) + D(q.x) * r.w) + D(r.x) * q.w;
        float y = static_cast<float>(((D(q.x) * r.z - D(r.x) * q.z) + D(q.y) * r.w) + D(r.y) * q.w);
        float z = static_cast<float>(((D(q.y) * r.x - D(r.y) * q.x) + D(r.w) * q.z) + D(r.z) * q.w);
        double w = D(r.w) * q.w - DotUnrounded<Sum3::ZXY, double>(q.x, q.y, q.z, r.x, r.y, r.z);
        q.z = z;
        q.y = y;
        q.w = static_cast<float>(w);
        q.x = static_cast<float>(x);
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
    const Vector3* zero = &camera::kZeroVector;
    SampleTerrain(p, zero, zero, 1.0f, 2.0f, 2.0f, &average, &lowest, &highest);
    NearbyList list;
    list.Construct();   // 00411850
    TheClientWorld()->ObjectsInRange(reinterpret_cast<const float*>(p), 16.0f, &list);
    float best = 0.0f;
    for (NearbyNode* node = list.head->next; node != list.head; node = node->next) {
        const CObject3D* object = node->data;
        const float limit = CAM_OBJECT_EXTENT_LIMIT;
        if (object->above.x > limit || object->above.y > limit || object->above.z > limit ||
            object->center.z > limit)
            continue;
        float cx = static_cast<float>(D(object->position.x) + object->center.x);
        float cy = static_cast<float>(D(object->position.y) + object->center.y);
        float cz = p->z;
        double radius = libcrt::Hypot(object->above.x, object->above.y);
        double dx = D(cx) - p->x;
        double dy = D(cy) - p->y;
        double dz = D(cz) - p->z;
        float outside = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) - radius);
        double factor = CAM_ZERO;
        EI_NAN_AUDIT(outside);
        if (outside <= CAM_ZERO) factor = CAM_ONE;
        else if (outside <= CAM_OBJECT_FADE) factor = D(CAM_ONE) - D(outside) * D(CAM_TENTH);
        float height = static_cast<float>(((D(object->position.z) + object->above.z) + object->center.z) * factor);
        best = best >= height ? best : height;
    }
    CameraSettings* s = settings_;
    float raised;
    if (best > highest) {
        float over = static_cast<float>(D(best) - D(highest));
        float limit = s->maxRaise;
        float used = over >= limit ? limit : over;
        float wanted = static_cast<float>((D(used) + highest) + D(s->clearance));
        float z = p->z;
        raised = z >= wanted ? z : wanted;
    } else {
        float top = best >= highest ? best : highest;
        float wanted = static_cast<float>(D(top) + D(s->clearance));
        float z = p->z;
        raised = z >= wanted ? z : wanted;
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
