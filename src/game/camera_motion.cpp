// Camera helpers the per-frame update uses: aiming along a direction, turning the camera upright towards its
// target and keeping it above nearby objects. See camera.h and docs/notes/core.md. Floating point: float maths in
// the original's order (its x87 registers held floats at the main thread's 24-bit precision); an operation on the
// CRT's double results through lib/pc24.h.
#include "camera.h"
#include "game_singletons.h"
#include "map_object_figures.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

#include <iterator>
#include <vector>

// Aims the camera along (dx, dy, dz): the view heading is the turn about Z that points the camera's X axis
// along the horizontal part (identity when that is straight along Y), then the local rotation tilts by -(90
// degrees + elevation) about its own X axis. False (and nothing done) for a direction shorter than 1e-9.
bool Camera::LookDirection(float dx, float dy, float dz) {
    float lengthSquare = LengthSquaredUnrounded<Sum3::XYZ, float>(dx, dy, dz);
    EI_NAN_AUDIT(lengthSquare);
    if (lengthSquare <= CAM_LOOK_EPSILON) return false;   // the double constant (exact as a compare)
    float inv = 1.0f / std::sqrt(lengthSquare);
    float nx = inv * dx;
    float ny = inv * dy;
    float elevation = static_cast<float>(libcrt::CiAsin(-(inv * dz)));
    // ny over _hypot's double rounds once at 24 bits; the CRT's arc cosine returns a double.
    float heading = static_cast<float>(libcrt::CiAcos(pc24::Div(ny, libcrt::Hypot(nx, ny))));
    EI_NAN_AUDIT(nx, heading);
    if (nx == CAM_ZERO || heading == CAM_ZERO) {
        viewRotation = camera::kIdentityRotation;
    } else {
        float side = -nx;
        float z;
        EI_NAN_AUDIT(side);
        if (side < CAM_ZERO) z = CAM_MINUS_ONE;
        else if (side > CAM_ZERO) z = CAM_ONE;
        else z = CAM_ZERO;
        Vector3 axis;
        axis.x = 0.0f;
        axis.y = 0.0f;
        axis.z = z;
        viewRotation.FromAxisAngle(axis, heading);
    }
    localRotation = viewRotation;
    const Quaternion& q = localRotation;
    float xx = q.x * q.x;
    float e1 = Dot2DUnrounded<float>(q.w, q.x, q.z, q.y);
    float e2 = q.x * q.z - q.w * q.y;
    Vector3 xAxis;
    xAxis.x = (q.w * q.w - (LengthSquared2DUnrounded<float>(q.y, q.z) + xx)) + (xx + xx);
    xAxis.y = e1 + e1;
    xAxis.z = e2 + e2;
    float angle = -(CAM_RIGHT_ANGLE + elevation);
    Quaternion turn;
    const Quaternion& r = *turn.SetAngleAxis(angle, xAxis);
    Quaternion& t = localRotation;
    float x = ((t.z * r.y - t.y * r.z) + r.w * t.x) + t.w * r.x;
    float y = ((t.x * r.z - t.z * r.x) + r.w * t.y) + t.w * r.y;
    float zz = ((t.y * r.x - r.y * t.x) + r.w * t.z) + t.w * r.z;
    float w = r.w * t.w - DotUnrounded<Sum3::ZYX, float>(r.x, r.y, r.z, t.x, t.y, t.z);
    t.z = zz;
    t.y = y;
    t.w = w;
    t.x = x;
    return true;
}

// Turns the camera a tenth of the way about its own X-axis-and-horizontal cross product
// so that its X axis lines up with the horizontal direction across the view (Z axis turned a
// quarter about +Z); nothing below one degree. Then lifts the camera over nearby objects.
void Camera::AlignToTarget() {
    Quaternion& q = localRotation;
    const Vector3 zAxis = q.AxisZ();
    const Vector3 xAxis = q.AxisX();
    float hy = -zAxis.x;
    float hz = 0.0f;
    float inv = CAM_ONE / std::sqrt(hy * hy + zAxis.y * zAxis.y);
    float hx = inv * zAxis.y;
    hy = hy * inv;
    float dot = DotUnrounded<Sum3::ZYX, float>(xAxis.x, xAxis.y, xAxis.z, hx, hy, hz);
    float cosine = dot;
    EI_NAN_AUDIT(dot);
    if (dot > CAM_ONE) cosine = 1.0f;
    else if (cosine < CAM_MINUS_ONE) cosine = -1.0f;
    float angle = static_cast<float>(libcrt::CiAcos(cosine));
    float magnitude = AbsBits(angle);
    if (CAM_DEGREE > magnitude) return;
    float cx = xAxis.y * hz - xAxis.z * hy;
    float cy = xAxis.z * hx - hz * xAxis.x;
    float cz = hy * xAxis.x - xAxis.y * hx;
    float invC = CAM_ONE / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(cx, cy, cz));
    Vector3 axis;
    axis.x = cx * invC;
    axis.y = cy * invC;
    axis.z = cz * invC;
    float axisLength = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(axis));
    if (axisLength > CAM_ALIGN_EPSILON) {   // the double constant (exact as a compare)
        Quaternion turn;
        const Quaternion& r = *turn.SetAngleAxis(angle * CAM_TENTH, axis);
        float x = ((r.y * q.z - q.y * r.z) + q.x * r.w) + r.x * q.w;
        float y = ((q.x * r.z - r.x * q.z) + q.y * r.w) + r.y * q.w;
        float z = ((q.y * r.x - r.y * q.x) + r.w * q.z) + r.z * q.w;
        float w = r.w * q.w - DotUnrounded<Sum3::ZXY, float>(q.x, q.y, q.z, r.x, r.y, r.z);
        q.z = z;
        q.y = y;
        q.w = w;
        q.x = x;
    }
    ClampAboveObjects();
}

// Keeps the camera above the terrain and the objects within 16 of it: an object (all
// extents up to 100) counts with its top height, fully when the camera is inside its footprint
// radius and fading out over the next 10; the camera height becomes at least the highest of these
// (raised over the highest terrain by at most settings->cameraMaxHeight) plus settings->cameraMinHeight.
void Camera::ClampAboveObjects() {
    Vector3* p = &localPosition;
    const float highest = SampleTerrain(*p, 1.0f, 2.0f, 2.0f).maximum;
    std::vector<CObject3D*> list = g_clientWorld.ObjectsInRange(*p, 16.0f);
    float best = 0.0f;
    for (auto& node : list) {
        const CObject3D* object = node;
        const float limit = CAM_OBJECT_EXTENT_LIMIT;
        if (object->bounds.boxMax.x > limit || object->bounds.boxMax.y > limit || object->bounds.boxMax.z > limit ||
            object->bounds.center.z > limit)
            continue;
        float cx = object->localPosition.x + object->bounds.center.x;
        float cy = object->localPosition.y + object->bounds.center.y;
        float cz = p->z;
        double radius = libcrt::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y);
        float dx = cx - p->x;
        float dy = cy - p->y;
        float dz = cz - p->z;
        // The distance less _hypot's double, rounded once at 24 bits.
        float outside = pc24::Sub(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz)), radius);
        float factor = CAM_ZERO;
        EI_NAN_AUDIT(outside);
        if (outside <= CAM_ZERO) factor = CAM_ONE;
        else if (outside <= CAM_OBJECT_FADE) factor = CAM_ONE - outside * CAM_TENTH;
        float height = ((object->localPosition.z + object->bounds.boxMax.z) + object->bounds.center.z) * factor;
        best = MaxKeepB(best, height);
    }
    const options::GameOptions* s = settings;
    float raised;
    if (best > highest) {
        float over = best - highest;
        float limit = s->cameraMaxHeight;
        float used = MinKeepA(over, limit);
        float wanted = (used + highest) + s->cameraMinHeight;
        float z = p->z;
        raised = MaxKeepB(z, wanted);
    } else {
        float top = MaxKeepB(best, highest);
        float wanted = top + s->cameraMinHeight;
        float z = p->z;
        raised = MaxKeepB(z, wanted);
    }
    p->z = raised;
}
