// CCamera helpers the per-frame update uses: aiming along a direction, turning the camera upright towards its
// target and keeping it above nearby objects. See camera.h and docs/notes/core.md. Floating point follows the
// game's x87 code: `double` for values the game keeps on the FPU stack, static_cast<float> where it stores to
// a float.
#include "camera.h"
#include "game_singletons.h"
#include "map_object_figures.h"
#include "world.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {

using camera::D;

} // namespace

// Aims the camera along (dx, dy, dz): the view heading is the turn about Z that points the camera's X axis
// along the horizontal part (identity when that is straight along Y), then the local rotation tilts by -(90
// degrees + elevation) about its own X axis. False (and nothing done) for a direction shorter than 1e-9.
bool CCamera::LookDirection(float dx, float dy, float dz) {
    double lengthSquare = LengthSquaredUnrounded<Sum3::XYZ, double>(dx, dy, dz);
    EI_NAN_AUDIT(lengthSquare);
    if (lengthSquare <= CAM_LOOK_EPSILON) return false;
    double inv = CAM_ONE_D / std::sqrt(lengthSquare);
    float invRounded = static_cast<float>(inv);
    float nx = static_cast<float>(inv * dx);
    float ny = static_cast<float>(D(invRounded) * dy);
    float elevation = static_cast<float>(libcrt::CiAsin(-(D(invRounded) * dz)));
    float heading = static_cast<float>(libcrt::CiAcos(D(ny) / libcrt::Hypot(nx, ny)));
    EI_NAN_AUDIT(nx, heading);
    if (nx == CAM_ZERO || heading == CAM_ZERO) {
        viewRotation_ = camera::kIdentityRotation;
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
    localRotation_ = viewRotation_;
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

// Turns the camera a tenth of the way about its own X-axis-and-horizontal cross product
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
    float angle = static_cast<float>(libcrt::CiAcos(cosine));
    float magnitude = angle;
    magnitude = AbsBits(magnitude);
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

// Keeps the camera above the terrain and the objects within 16 of it: an object (all
// extents up to 100) counts with its top height, fully when the camera is inside its footprint
// radius and fading out over the next 10; the camera height becomes at least the highest of these
// (raised over the highest terrain by at most settings->cameraMaxHeight_) plus settings->cameraMinHeight_.
void CCamera::ClampAboveObjects() {
    Vector3* p = &localPosition_;
    float average, lowest, highest;
    const Vector3* zero = &camera::kZeroVector;
    SampleTerrain(p, zero, zero, 1.0f, 2.0f, 2.0f, &average, &lowest, &highest);
    std::list<CObject3D*> list;
    g_clientWorld->ObjectsInRange(p, 16.0f, &list);
    float best = 0.0f;
    for (auto& node : list) {
        const CObject3D* object = node;
        const float limit = CAM_OBJECT_EXTENT_LIMIT;
        if (object->bounds.boxMax.x > limit || object->bounds.boxMax.y > limit || object->bounds.boxMax.z > limit ||
            object->bounds.center.z > limit)
            continue;
        float cx = static_cast<float>(D(object->localPosition_.x) + object->bounds.center.x);
        float cy = static_cast<float>(D(object->localPosition_.y) + object->bounds.center.y);
        float cz = p->z;
        double radius = libcrt::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y);
        double dx = D(cx) - p->x;
        double dy = D(cy) - p->y;
        double dz = D(cz) - p->z;
        float outside = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) - radius);
        double factor = CAM_ZERO;
        EI_NAN_AUDIT(outside);
        if (outside <= CAM_ZERO) factor = CAM_ONE;
        else if (outside <= CAM_OBJECT_FADE) factor = D(CAM_ONE) - D(outside) * D(CAM_TENTH);
        float height = static_cast<float>(((D(object->localPosition_.z) + object->bounds.boxMax.z) + object->bounds.center.z) * factor);
        best = best >= height ? best : height;
    }
    const options::GameOptions* s = settings_;
    float raised;
    if (best > highest) {
        float over = static_cast<float>(D(best) - D(highest));
        float limit = s->cameraMaxHeight_;
        float used = over >= limit ? limit : over;
        float wanted = static_cast<float>((D(used) + highest) + D(s->cameraMinHeight_));
        float z = p->z;
        raised = z >= wanted ? z : wanted;
    } else {
        float top = best >= highest ? best : highest;
        float wanted = static_cast<float>(D(top) + D(s->cameraMinHeight_));
        float z = p->z;
        raised = z >= wanted ? z : wanted;
    }
    p->z = raised;
}
