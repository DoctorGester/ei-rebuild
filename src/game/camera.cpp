// The camera (CCamera, camera.h): the per-frame update and view placement, focus and terrain sampling,
// panning / zoom / saved views, shakes and recorded camera paths. Floating point
// follows the game's x87 code: `double` for values the game keeps on the FPU stack, static_cast<float> where
// it stores to a float.
#include "camera.h"

#include "base_manager.h"
#include "core_maths.h"
#include "map_object_figures.h"
#include "sound.h"
#include "terrain.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "lib/crt.h"
#include "lib/float_select.h"

#include <bit>
#include <cstring>

namespace {

using camera::D;

constexpr float kTrackGain = 0.2f;
constexpr float kPanZoomSpan = 1.125f;
constexpr float kHeightLag = 0.1f;
constexpr float kEase = 1.1f;
constexpr float kShakeFrequency = 0.26666668f;
constexpr float kShakeRange = 20.0f;
constexpr float kShakeFull = 8.0f;
constexpr float kShakeTicks = 30.0f;
constexpr float kAxisTiny = 1e-8f;


// Branch-free clamp of a map coordinate to [0, last - 1]: NaN becomes 0, and a NaN limit wins.
float ClampToMap(float v, float last) {
    float low = v >= 0.0f ? v : 0.0f;
    float limit = static_cast<float>(last - 1.0f);
    return low <= limit ? low : limit;
}

// Moves `rate` towards `target` by (target - rate) * response * dt; snaps to the target when the
// step is tiny or the rate's magnitude passes the target's.
void Approach(float* rate, const float* target, float response, float dt) {
    float step = static_cast<float>((D(*target) - *rate) * response);
    EI_NAN_AUDIT(step);
    if (D(AbsBits(step)) < 0.001) {
        *rate = *target;
        return;
    }
    double before = D(*rate) * *rate;
    double after = D(step) * dt + *rate;
    *rate = static_cast<float>(after);
    float after2 = static_cast<float>(after * after);
    float target2 = static_cast<float>(D(*target) * *target);
    EI_NAN_AUDIT(before, after2, target2);
    if ((before < target2 && after2 > target2) || (before > target2 && after2 < target2))
        *rate = *target;
}

// ---- The drawn view (camera::DrawnView; docs/notes/renderer.md section 14) -----------------------

// A tick whose update moved the focus by more than this (map units), or the camera nearer to its focus or
// farther from it by more, placed it anew (a map change, a jump of the focus): shown at once, not spread. A
// turn, pan, tilt, zoom or lift over the ground stays within both in a tick, however fast the camera turns.
constexpr float kViewCut = 8.0f;

// The last client tick's step of the camera (the change its update made: position, focus, distance; the
// turns as the rotations that undo them; nothing for a cut), the pose before that update, and the part
// of the step the frame has not reached yet.
struct ViewStep {
    camera::ViewPose before{};
    Vector3 position{}, focus{};
    Quaternion undoRotation{1.0f, 0.0f, 0.0f, 0.0f}, undoHeading{1.0f, 0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    float back = 0.0f;
};
ViewStep g_viewStep;
camera::ViewPose g_frameView{};   // camera::FrameView

camera::ViewPose PoseOf(const CCamera* c) {
    return camera::ViewPose{c->localPosition, c->localRotation, c->focus, c->viewRotation, c->distance};
}

Quaternion Conjugate(const Quaternion& q) { return Quaternion{q.w, -q.x, -q.y, -q.z}; }

// The rotation that turns `to` back to `from` (from * to^-1).
Quaternion Undo(const Quaternion& from, const Quaternion& to) {
    Quaternion undo = Conjugate(to);
    undo.PreMultiply(from);
    return undo;
}

// `r` turned back by the part `t` of `undo`.
Quaternion TurnedBack(const Quaternion& r, const Quaternion& undo, float t) {
    Quaternion part;
    part.Slerp(t, camera::kIdentityRotation, undo);
    Quaternion out = r;
    out.PreMultiply(part);
    return out;
}

// The camera's distance from its focus in `pose`.
float Reach(const camera::ViewPose& pose) { return std::sqrt(DistanceSquaredZYX(pose.position, pose.focus)); }

void ResetMotion(CCamera* c) {
    c->panVelocity = camera::kZeroVector;
    c->panTarget = camera::kZeroVector;
    const float zero = 0.0f;
    c->turnRate = zero;
    c->turnTarget = zero;
    c->tiltRate = zero;
    c->tiltTarget = zero;
    c->zoomRate = zero;
    c->zoomTarget = 0.0f;
}

} // namespace

void camera::NoteViewBeforeTick(const CCamera* camera) { g_viewStep.before = PoseOf(camera); }

void camera::NoteViewTick(const CCamera* camera) {
    ViewStep& step = g_viewStep;
    const ViewPose now = PoseOf(camera);
    const ViewPose& before = step.before;
    const bool kept = DistanceSquaredZYX(now.focus, before.focus) <= kViewCut * kViewCut &&
                      std::fabs(Reach(now) - Reach(before)) <= kViewCut;
    step.position = kept ? now.position - before.position : kZeroVector;
    step.focus = kept ? now.focus - before.focus : kZeroVector;
    step.distance = kept ? now.distance - before.distance : 0.0f;
    step.undoRotation = kept ? Undo(before.rotation, now.rotation) : kIdentityRotation;
    step.undoHeading = kept ? Undo(before.heading, now.heading) : kIdentityRotation;
}

void camera::SetViewProgress(float reached, float passTicks) {
    reached = reached > 0.0f ? (reached < 1.0f ? reached : 1.0f) : 0.0f;
    float spread = 2.0f - passTicks;
    spread = spread > 0.0f ? (spread < 1.0f ? spread : 1.0f) : 0.0f;
    g_viewStep.back = (1.0f - reached) * spread;
}

camera::ViewPose camera::DrawnView(const CCamera* camera) {
    const ViewStep& step = g_viewStep;
    const float back = step.back;
    ViewPose out = PoseOf(camera);
    out.position = out.position - step.position * back;
    out.rotation = TurnedBack(out.rotation, step.undoRotation, back);
    out.focus = out.focus - step.focus * back;
    out.heading = TurnedBack(out.heading, step.undoHeading, back);
    out.distance = out.distance - step.distance * back;
    return out;
}

void camera::TakeFrameView(const CCamera* camera) { g_frameView = DrawnView(camera); }

const camera::ViewPose& camera::FrameView() { return g_frameView; }

void SampleTerrain(const Vector3& p, float step, float halfX, float halfY, float* average, float* minimum,
                   float* maximum) {
    double centre = g_clientWorld.RawSurfaceHeightAt(p.x, p.y);
    *minimum = static_cast<float>(centre);
    *maximum = static_cast<float>(centre);
    *average = static_cast<float>(centre);
    int count = 1;
    float x = static_cast<float>(D(p.x) - halfX);
    while (x <= D(halfX) + p.x) {
        if (x >= 0.0f && g_clientWorld.terrain->width > x) {
            float y = static_cast<float>(D(p.y) - halfY);
            while (y <= D(halfY) + p.y) {
                if (y >= 0.0f && g_clientWorld.terrain->height > y) {
                    float h = static_cast<float>(g_clientWorld.RawSurfaceHeightAt(x, y));
                    float low = *minimum;
                    *minimum = h >= low ? low : h;
                    float high = *maximum;
                    *maximum = high >= h ? high : h;
                    ++count;
                    *average = static_cast<float>(D(h) + *average);
                }
                y = static_cast<float>(D(y) + step);
            }
        }
        x = static_cast<float>(D(x) + step);
    }
    *average = static_cast<float>(D(*average) / static_cast<double>(count));
}

int CCamera::PlaybackStart() {
    if (keys.empty()) return 0;
    localPosition = keys[0].position;
    localRotation = keys[0].rotation;
    return 1;
}

int CCamera::PlaybackStep(float time) {
    if (!(flags & kPlaying) || keys.empty()) return 0;
    const CameraKey* newKeys = keys.data();
    if (keyIndex >= std::ssize(keys) - 1) {
        localPosition = newKeys[std::ssize(keys) - 1].position;
        localRotation = newKeys[std::ssize(keys) - 1].rotation;
        return 0;
    }
    double next = D(newKeys[keyIndex + 1].time) + timeOffset;
    while (time > next) {
        ++keyIndex;
        if (keyIndex >= std::ssize(keys) - 1) {
            localPosition = newKeys[std::ssize(keys) - 1].position;
            localRotation = newKeys[std::ssize(keys) - 1].rotation;
            flags &= ~kPlaying;
            return 0;
        }
        next = D(newKeys[keyIndex + 1].time) + timeOffset;
    }
    const CameraKey& a = newKeys[keyIndex];
    const CameraKey& b = (&a)[1];
    float start = static_cast<float>(D(a.time) + timeOffset);
    double span = next - start;
    float u = 0.0f;
    EI_NAN_AUDIT(span);
    if (span != 0.0) u = static_cast<float>((D(time) - start) / span);
    double dx = D(b.position.x) - a.position.x;
    float dy = static_cast<float>(D(b.position.y) - a.position.y);
    float dz = static_cast<float>(D(b.position.z) - a.position.z);
    float sy = static_cast<float>(D(dy) * u);
    float sz = static_cast<float>(D(dz) * u);
    localPosition.x = static_cast<float>(dx * u + a.position.x);
    localPosition.y = static_cast<float>(D(sy) + a.position.y);
    localPosition.z = static_cast<float>(D(sz) + a.position.z);
    localRotation.Slerp(u, a.rotation, b.rotation);
    return 1;
}

void CCamera::Pan(const Vector3& direction) {
    Vector3 right;
    localRotation.AxisX(&right);
    const Quaternion& r = localRotation;
    Vector3 up;
    up.z = static_cast<float>(Dot2DUnrounded<double>(r.w, r.y, r.x, r.z) * 2.0);
    up.y = static_cast<float>((D(r.w) * r.w - LengthSquaredUnrounded<Sum3::ZXY, double>(r.x, r.y, r.z)) + D(r.y) * r.y * 2.0);
    up.x = static_cast<float>((D(r.y) * r.x - D(r.w) * r.z) * 2.0);
    double k1 = (1.0 / std::sqrt(LengthSquared2DUnrounded<double>(right.y, right.x))) * direction.x;
    right.x = static_cast<float>(k1 * right.x);
    double rightY = k1 * right.y;
    double k2 = (1.0 / std::sqrt(LengthSquared2DUnrounded<double>(up.y, up.x))) * direction.y;
    up.x = static_cast<float>(k2 * up.x);
    double upY = k2 * up.y;
    panTarget.x = static_cast<float>(D(up.x) + right.x);
    panTarget.y = static_cast<float>(upY + rightY);
    panTarget.z = 0.0f;
}

void CCamera::StopMotion() {
    Vector3 right, up;
    localRotation.AxisX(&right);
    localRotation.AxisY(&up);
    // Multiplies by 0 (keeping NaN and -0), so the zero is read from memory.
    const float zero = CAM_ZERO;
    right.x = static_cast<float>(D(zero) * right.x);
    right.y = static_cast<float>(D(right.y) * zero);
    right.z = 0.0f;
    up.x = static_cast<float>(D(zero) * up.x);
    up.y = static_cast<float>(D(up.y) * zero);
    up.z = 0.0f;
    Vector3 sum;
    AddVectors(&sum, right, up);
    panTarget = sum;
    // SetTiltRate(0) / SetTurnRate(0) inlined: a rate is replaced when its magnitude is below the
    // target's (never, for 0; a NaN rate is replaced).
    EI_NAN_AUDIT(tiltRate, turnRate);
    tiltTarget = 0.0f;
    if (std::fabs(tiltRate) < std::fabs(tiltTarget)) tiltRate = tiltTarget;
    turnTarget = 0.0f;
    if (std::fabs(turnRate) < std::fabs(turnTarget)) turnRate = turnTarget;
    zoomTarget = 0.0f;
}

void CCamera::SetFocus(const Vector3& p) {
    focus = p;
    float average, low, high;
    SampleTerrain(p, static_cast<float>(D(options::g_options.cameraTerrainArea) * 0.5f), options::g_options.cameraTerrainArea, options::g_options.cameraTerrainArea, &average,
                  &low, &high);
    focus.z = static_cast<float>(D(average) + focusHeight);
}

void CCamera::ApplyView() {
    localRotation = viewRotation;
    Vector3 right;
    localRotation.AxisX(&right);
    Quaternion tilt;
    tilt.SetAngleAxis(static_cast<float>(-(CAM_RIGHT_ANGLE + pitch)), right);
    localRotation.PreMultiply(tilt);
    Vector3 forward;
    localRotation.AxisZ(&forward);
    const float d = distance;
    float ox = static_cast<float>(D(forward.x) * d);
    float oy = static_cast<float>(D(forward.y) * d);
    float px = static_cast<float>(D(focus.x) - ox);
    float py = static_cast<float>(D(focus.y) - oy);
    float pz = static_cast<float>(D(focus.z) - D(d) * forward.z);
    localPosition.x = px;
    localPosition.y = py;
    localPosition.z = pz;
    float before;
    before = localPosition.z;
    ClampAboveObjects();
    if (before >= localPosition.z) return;
    // Lifted over an object: aim back down at the focus.
    double dz = D(localPosition.z) - focus.z;
    double across = libcrt::Hypot(D(localPosition.x) - focus.x, D(localPosition.y) - focus.y);
    double angle = x87::fpatan(dz, across);
    localRotation = viewRotation;
    const Quaternion s = localRotation;
    Vector3 axis;
    axis.x = static_cast<float>((D(s.w) * s.w - LengthSquaredUnrounded<Sum3::ZYX, double>(s.x, s.y, s.z)) + D(s.x) * s.x * 2.0);
    axis.y = static_cast<float>(Dot2DUnrounded<double>(s.x, s.z, s.y, s.w) * 2.0);
    axis.z = static_cast<float>((D(s.x) * s.z - D(s.y) * s.w) * 2.0);
    Quaternion q;
    q.SetAngleAxis(static_cast<float>(-(CAM_RIGHT_ANGLE + angle)), axis);
    float y = static_cast<float>(((D(q.z) * s.x - D(s.z) * q.x) + D(s.w) * q.y) + D(s.y) * q.w);
    float z = static_cast<float>(((D(q.x) * s.y - D(s.x) * q.y) + D(s.w) * q.z) + D(s.z) * q.w);
    float w = static_cast<float>(D(s.w) * q.w - DotUnrounded<Sum3::ZXY, double>(s.x, s.y, s.z, q.x, q.y, q.z));
    float x = static_cast<float>(((D(s.z) * q.y - D(q.z) * s.y) + D(s.w) * q.x) + D(s.x) * q.w);
    localRotation.y = y;
    localRotation.z = z;
    localRotation.w = w;
    localRotation.x = x;
}

void CCamera::Update(float dt) {
    Vector3 listener;
    listener = localPosition;
    if (!(flags & kPlaced)) {
        listener = focus;
        if (g_clientWorld.terrain) listener.z = static_cast<float>(g_clientWorld.RawSurfaceHeightAt(focus.x, focus.y) + focusHeight);
    }
    Vector3 forward;
    localRotation.AxisZ(&forward);
    g_clientWorld.sounds->SetListener(&listener.x, &forward.x);
    if (flags & kPlaced) return;

    CObject3D* target = g_clientWorld.objectManager->FindObjectAs<CObject3D>(static_cast<unsigned>(targetId));
    if (flags & kFollowing) {
        if (target) {
            // Behind the target, looking along its heading.
            const CTransformObject* t = target;
            const CObject3D* body = target;
            double lift = D(body->bounds.boxMax.z) + body->bounds.center.z;
            localPosition.x = t->localPosition.x;
            localPosition.y = t->localPosition.y;
            localPosition.z = static_cast<float>(lift + t->localPosition.z);
            Vector3 side;
            t->localRotation.AxisY(&side);
            double nx = -D(side.x);
            Vector3 dir;
            dir.y = -side.y;
            dir.z = 0.0f;
            float inv = static_cast<float>(1.0 / std::sqrt(D(dir.y) * dir.y + nx * nx));
            dir.x = static_cast<float>(nx * inv);
            dir.y = static_cast<float>(D(dir.y) * inv);
            TurnTowards(dir);
            AlignToTarget();
            return;
        }
    } else if (target) {
        // Drift the focus a fifth of the way to the tracked object.
        const CTransformObject* t = target;
        double dx = D(t->localPosition.x) - focus.x;
        float dy = static_cast<float>(D(t->localPosition.y) - focus.y);
        float width = g_clientWorld.terrain->width;
        float dyStep = static_cast<float>(D(dy) * kTrackGain);
        focus.x = ClampToMap(static_cast<float>(dx * kTrackGain + focus.x), width);
        float height = g_clientWorld.terrain->height;
        focus.y = ClampToMap(static_cast<float>(D(dyStep) + focus.y), height);
    }

    // Pan: the velocity eases towards its target.
    Vector3& velocity = panVelocity;
    const Vector3& wanted = panTarget;
    Vector3 change = {static_cast<float>(D(wanted.x) - velocity.x), static_cast<float>(D(wanted.y) - velocity.y),
                      static_cast<float>(D(wanted.z) - velocity.z)};
    Vector3 accel;
    ScaleVector(&accel, change, panResponse);
    bool snap = true;
    if (accel.Length() >= 0.001) {
        float before = static_cast<float>(Dot(velocity, velocity));
        Vector3 step;
        ScaleVector(&step, accel, dt);
        velocity.AddInPlace(step);
        float after = static_cast<float>(Dot(velocity, velocity));
        float goal = static_cast<float>(Dot(wanted, wanted));
        EI_NAN_AUDIT(before, after, goal);
        snap = (before < goal && after > goal) || (before > goal && after < goal);
    }
    if (snap) velocity = wanted;

    if (LengthSquaredUnrounded<Sum3::XYZ, double>(velocity) > 0.0) {
        const CameraLimits limits = Limits();
        double scale = (D(distance) - limits.min) / ((D(limits.max) - limits.min) * kPanZoomSpan) + kTrackGain;
        double moveX = D(dt) * velocity.x;
        float moveY = static_cast<float>(D(dt) * velocity.y);
        float stepX = static_cast<float>(moveX * scale);
        float stepY = static_cast<float>(D(moveY) * scale);
        float width = g_clientWorld.terrain->width;
        focus.x = ClampToMap(static_cast<float>(D(stepX) + focus.x), width);
        float height = g_clientWorld.terrain->height;
        float y = ClampToMap(static_cast<float>(D(stepY) + focus.y), height);
        targetId = -1;
        flags &= ~kFollowing;
        focus.y = y;
        if (flags & kBounded) {
            double bx = D(focus.x) - boundsCenter.x;
            double by = D(focus.y) - boundsCenter.y;
            double d2 = LengthSquared2DUnrounded(by, bx);
            double radius = boundsRadius;
            if (d2 > radius * radius) {
                double inv = 1.0 / std::sqrt(d2);
                float ux = static_cast<float>(inv * bx);
                double uy = inv * by;
                float px = static_cast<float>(radius * ux);
                float ny = static_cast<float>(radius * uy + boundsCenter.y);
                focus.x = static_cast<float>(D(px) + boundsCenter.x);
                focus.y = ny;
            }
        }
    }

    // The focus height follows the target's or the ground's, a tenth of the way per call.
    float average, low, high;
    SampleTerrain(focus, static_cast<float>(D(options::g_options.cameraTerrainArea) * 0.5f), options::g_options.cameraTerrainArea, options::g_options.cameraTerrainArea,
                  &average, &low, &high);
    const CObject3D* body = target;
    double ground = target ? D(body->localPosition.z) : D(average);
    focus.z = static_cast<float>(((ground + focusHeight) - focus.z) * kHeightLag + focus.z);

    // Tilt (pitch in degrees, clamped to the settings' range).
    Approach(&tiltRate, &tiltTarget, tiltResponse, dt);
    EI_NAN_AUDIT(tiltRate);
    if (tiltRate != 0.0f) {
        double newPitch = D(dt) * tiltRate + pitch;
        pitch = static_cast<float>(newPitch);
        bool alternate = (flags & kAlternate) != 0;
        float most = static_cast<float>(D(alternate ? settings->briefingMaxPitch : settings->cameraDefaultMaxPitch) * CAM_DEGREE);
        if (newPitch > most) pitch = most;
        double least = D(alternate ? settings->briefingMinPitch : settings->cameraDefaultMinPitch) * CAM_DEGREE;
        if (least > pitch) pitch = static_cast<float>(least);
    }

    Approach(&turnRate, &turnTarget, turnResponse, dt);
    EI_NAN_AUDIT(turnRate);
    if (turnRate != 0.0f) {
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Quaternion q;
        q.SetAxisAngle(up, static_cast<float>(D(dt) * turnRate));
        const Quaternion r = viewRotation;
        float x = static_cast<float>(((D(r.z) * q.y - D(r.y) * q.z) + D(q.w) * r.x) + D(r.w) * q.x);
        float y = static_cast<float>(((D(r.x) * q.z - D(r.z) * q.x) + D(q.w) * r.y) + D(r.w) * q.y);
        float z = static_cast<float>(((D(r.y) * q.x - D(q.y) * r.x) + D(r.z) * q.w) + D(r.w) * q.z);
        float w = static_cast<float>(D(q.w) * r.w - DotUnrounded<Sum3::ZYX, double>(q.x, q.y, q.z, r.x, r.y, r.z));
        viewRotation.z = z;
        viewRotation.y = y;
        viewRotation.w = w;
        viewRotation.x = x;
    }

    float currentZoomTarget = zoomTarget;
    Approach(&zoomRate, &currentZoomTarget, zoomResponse, dt);
    EI_NAN_AUDIT(zoomRate);
    if (zoomRate != 0.0f) distance = static_cast<float>(D(dt) * zoomRate + distance);

    const CameraLimits limits = Limits();
    EI_NAN_AUDIT(distance, limits.hardMin, limits.min);
    if (distance < limits.hardMin) {
        distance = limits.hardMin;
    } else if (distance < limits.min) {
        distance = static_cast<float>((D(limits.min) - distance) * dt * kEase / (D(limits.min) - limits.hardMin) + distance);
    } else if (distance > limits.hardMax) {
        distance = limits.hardMax;
    } else if (distance > limits.max) {
        distance =
            static_cast<float>(D(distance) - (D(distance) - limits.max) * dt * kEase / (D(limits.hardMax) - limits.max));
    }

    ApplyView();

    if (shakes.empty()) return;
    float offset = 0.0f;
    for (std::list<CameraShake>::iterator node = shakes.begin(); node != shakes.end();) {
        CameraShake* s = &(*node);
        double damping = x87::fexp(-(D(s->decay) * s->elapsed));
        double wave = x87::fcos(D(camera::kHalfPi) * s->elapsed * kShakeFrequency);
        offset = static_cast<float>(wave * damping * s->amplitude + offset);
        double elapsed = D(dt) + s->elapsed;
        s->elapsed = static_cast<float>(elapsed);
        if (elapsed >= s->duration)
            node = shakes.erase(node);
        else
            ++node;   // also NaN
    }
    localPosition.z = static_cast<float>(D(offset) + localPosition.z);
}

void CCamera::ZoomBy(float steps) {
    double d = libcrt::CiPow(CAM_ZOOM_STEP, steps) * distance;
    distance = static_cast<float>(d);
    if (!settings) return;
    const CameraLimits limits = Limits();
    EI_NAN_AUDIT(d, limits.min);
    if (d < limits.min) {
        distance = limits.min;
        return;
    }
    if (d > limits.max) distance = limits.max;
}

bool CCamera::SaveView(unsigned slot) {
    if (slot >= 8) return false;
    CameraView& v = views[slot];
    v.distance = distance;
    v.pitch = pitch;
    v.rotation = viewRotation;
    ViewsToSettings(settings);
    return true;
}

bool CCamera::RestoreView(unsigned slot) {
    if (slot >= 8) return false;
    distance = views[slot].distance;
    pitch = views[slot].pitch;
    ResetMotion(this);
    return true;
}

void CCamera::SetDistancePitch(const CameraView* view) {
    distance = view->distance;
    pitch = view->pitch;
    ResetMotion(this);
}

CameraLimits CCamera::Limits() const {
    const options::GameOptions* s = settings;
    if (flags & kAlternate)
        return {s->briefingMinDistance, s->briefingMaxDistance, s->briefingMinLimitDistance, s->briefingMaxLimitDistance};
    return {s->cameraMinDistance, s->cameraMaxDistance, s->cameraMinLimitDistance, s->cameraMaxLimitDistance};
}

void CCamera::ViewsToSettings(options::GameOptions* gameOptions) {
    for (int i = 0; i < 4; ++i) {
        const CameraView& v = views[i];
        options::CameraShortcutOptions& r = gameOptions->cameraShortcuts[i];
        r.distance = v.distance;
        r.pitch = v.pitch;
        const Quaternion& q = v.rotation;
        double sum = LengthSquaredUnrounded<Sum3::ZXY, double>(q.x, q.y, q.z);
        float sumF = static_cast<float>(sum);
        if (sum > kAxisTiny) {
            double angle = libcrt::CiAcos(q.w);
            angle = angle + angle;
            double inv = 1.0 / std::sqrt(sumF);
            float invF = static_cast<float>(inv);
            r.axisX = static_cast<float>(inv * q.x);
            r.axisY = static_cast<float>(D(invF) * q.y);
            r.axisZ = static_cast<float>(D(invF) * q.z);
            r.angle = static_cast<float>(angle);
        } else {
            r.axisX = 1.0f;
            r.axisY = 0.0f;
            r.axisZ = 0.0f;
            r.angle = 0.0f;
        }
    }
}

void CCamera::ViewsFromSettings(const options::GameOptions* gameOptions) {
    for (int i = 0; i < 4; ++i) {
        CameraView& v = views[i];
        const options::CameraShortcutOptions& r = gameOptions->cameraShortcuts[i];
        v.distance = r.distance;
        v.pitch = r.pitch;
        Vector3 axis = {r.axisX, r.axisY, r.axisZ};
        v.rotation.FromAxisAngle(axis, r.angle);
    }
}

void CCamera::SetHeading(float angle) {
    Vector3 up = {0.0f, 0.0f, 1.0f};
    viewRotation.FromAxisAngle(up, angle);
    ApplyView();
}

void CCamera::SetFree(bool on) {
    if (on)
        flags |= kFree;
    else
        flags &= ~kFree;
}

void CCamera::LookFrom(const Vector3& from, const Vector3& to) {
    flags |= kFree;
    localPosition = from;
    float dx = static_cast<float>(D(to.x) - from.x);
    float dy = static_cast<float>(D(to.y) - from.y);
    float dz = static_cast<float>(D(to.z) - from.z);
    LookDirection(dx, dy, dz);
}

void CCamera::SetBounds(const Vector3& center, float radius) {
    boundsCenter = center;
    boundsRadius = radius;
    flags |= kBounded;
}

void CCamera::Shake(const Vector3& source, float duration, float amplitude, float decay) {
    const Vector3& at = (flags & kPlaced) ? localPosition : focus;
    double newDistance = libcrt::Hypot(D(at.x) - source.x, D(at.y) - source.y);
    float distanceF = static_cast<float>(newDistance);
    if (newDistance >= kShakeRange) return;
    double strength = amplitude;
    if (distanceF > kShakeFull)
        strength = (1.0 - (D(distanceF) - kShakeFull) / (D(kShakeRange) - kShakeFull)) * amplitude;
    CameraShake entry;
    entry.elapsed = 0.0f;
    entry.duration = static_cast<float>(D(duration) * kShakeTicks);
    entry.amplitude = static_cast<float>(strength);
    entry.decay = decay;
    shakes.insert(shakes.end(), entry);
}
