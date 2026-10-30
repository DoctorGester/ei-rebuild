// The camera (Camera, camera.h): the per-frame update and view placement, focus and terrain sampling,
// panning / zoom / saved views, shakes and recorded camera paths. Floating point: float maths in the original's
// order (its x87 registers held floats at the main thread's 24-bit precision); a transcendental, the CRT's double
// results and the wide constants through lib/pc24.h (docs/notes/cleanup-tooling.md, "The x87 files to SSE2").
#include "camera.h"

#include "base_manager.h"
#include "core_maths.h"
#include "map_object_figures.h"
#include "sound.h"
#include "terrain.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "lib/crt.h"
#include "lib/float_select.h"

namespace {

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
    float low = MaxKeepB(v, 0.0f);
    float limit = last - 1.0f;
    return MinKeepA(limit, low);
}

// Moves `rate` towards `target` by (target - rate) * response * dt; snaps to the target when the
// step is tiny or the rate's magnitude passes the target's.
void Approach(float* rate, const float* target, float response, float dt) {
    float step = (*target - *rate) * response;
    EI_NAN_AUDIT(step);
    if (AbsBits(step) < 0.001) {   // the double constant (exact as a compare)
        *rate = *target;
        return;
    }
    float before = *rate * *rate;
    float after = step * dt + *rate;
    *rate = after;
    float after2 = after * after;
    float target2 = *target * *target;
    EI_NAN_AUDIT(before, after2, target2);
    if ((before < target2 && after2 > target2) || (before > target2 && after2 < target2))
        *rate = *target;
}

// ---- The drawn view (camera::DrawnView; docs/notes/renderer.md section 14) -----------------------

// A tick whose update moved the focus by more than this (map units), or the camera nearer to its focus or
// farther from it by more, placed it anew (a map change, a jump of the focus): shown at once, not spread. A
// turn, pan, tilt, zoom or lift over the ground stays within both in a tick, however fast the camera turns.
constexpr float kViewCut = 8.0f;

// The last client tick's step of the camera (the change its update made: focus and distance; the turns as the
// rotations that undo them; `stretch`, the part of the camera's reach from its focus the step added, 1 - before /
// after; nothing for a cut, a placement or a load), the pose before that update, the part of the step the frames
// spread (`spread`: taken once, by the pass that ran the tick, so it stays the same until the next tick) and the
// part the frame has not reached yet.
struct ViewStep {
    camera::ViewPose before{};
    Vector3 focus{};
    Quaternion undoRotation{1.0f, 0.0f, 0.0f, 0.0f}, undoHeading{1.0f, 0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    float stretch = 0.0f;
    float spread = 0.0f;
    bool spreadTaken = true;
    float back = 0.0f;
};
ViewStep g_viewStep;
camera::ViewPose g_frameView{};   // camera::FrameView

camera::ViewPose PoseOf(const Camera* c) {
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

void ResetMotion(Camera* c) {
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

void camera::NoteViewBeforeTick(const Camera* camera) { g_viewStep.before = PoseOf(camera); }

void camera::NoteViewTick(const Camera* camera) {
    ViewStep& step = g_viewStep;
    const ViewPose now = PoseOf(camera);
    const ViewPose& before = step.before;
    const float reachNow = Reach(now), reachBefore = Reach(before);
    const bool kept =
        DistanceSquaredZYX(now.focus, before.focus) <= kViewCut * kViewCut && std::fabs(reachNow - reachBefore) <= kViewCut;
    step.focus = kept ? now.focus - before.focus : kZeroVector;
    step.distance = kept ? now.distance - before.distance : 0.0f;
    step.stretch = kept && reachNow > 0.0f ? 1.0f - reachBefore / reachNow : 0.0f;
    step.undoRotation = kept ? Undo(before.rotation, now.rotation) : kIdentityRotation;
    step.undoHeading = kept ? Undo(before.heading, now.heading) : kIdentityRotation;
    step.spreadTaken = false;
}

void camera::ForgetViewStep() { g_viewStep = ViewStep{}; }

void camera::SetViewProgress(float reached, float passTicks) {
    ViewStep& step = g_viewStep;
    if (!step.spreadTaken) {
        const float spread = 2.0f - passTicks;
        step.spread = spread > 0.0f ? (spread < 1.0f ? spread : 1.0f) : 0.0f;
        step.spreadTaken = true;
    }
    reached = reached > 0.0f ? (reached < 1.0f ? reached : 1.0f) : 0.0f;
    step.back = (1.0f - reached) * step.spread;
}

// The camera's offset from its focus turns back with its rotation and its length goes back by the step's stretch,
// so in a turn the drawn camera stays on the sphere around the drawn focus (along the arc, not the chord); with
// nothing to take back the position is the camera's own to the bit.
camera::ViewPose camera::DrawnView(const Camera* camera) {
    const ViewStep& step = g_viewStep;
    const float back = step.back;
    ViewPose out = PoseOf(camera);
    const Vector3 offset = out.position - out.focus;
    Quaternion part;
    part.Slerp(back, kIdentityRotation, step.undoRotation);
    const Vector3 turned = part.Rotate(offset) * (1.0f - step.stretch * back);
    out.focus = out.focus - step.focus * back;
    out.position = out.position - (step.focus * back + (offset - turned));
    out.rotation.PreMultiply(part);
    out.heading = TurnedBack(out.heading, step.undoHeading, back);
    out.distance = out.distance - step.distance * back;
    return out;
}

void camera::TakeFrameView(const Camera* camera) { g_frameView = DrawnView(camera); }

const camera::ViewPose& camera::FrameView() { return g_frameView; }

TerrainSample SampleTerrain(const Vector3& p, float step, float halfX, float halfY) {
    const float centre = static_cast<float>(g_clientWorld.RawSurfaceHeightAt(p.x, p.y));
    TerrainSample sample{centre, centre, centre};
    int count = 1;
    float x = p.x - halfX;
    while (x <= halfX + p.x) {
        if (x >= 0.0f && g_clientWorld.terrain->width > x) {
            float y = p.y - halfY;
            while (y <= halfY + p.y) {
                if (y >= 0.0f && g_clientWorld.terrain->height > y) {
                    float h = static_cast<float>(g_clientWorld.RawSurfaceHeightAt(x, y));
                    float low = sample.minimum;
                    sample.minimum = MinKeepA(h, low);
                    float high = sample.maximum;
                    sample.maximum = MaxKeepB(high, h);
                    ++count;
                    sample.average = h + sample.average;
                }
                y = y + step;
            }
        }
        x = x + step;
    }
    sample.average = sample.average / static_cast<float>(count);   // a grid's point count: exact as a float
    return sample;
}

int Camera::PlaybackStart() {
    if (keys.empty()) return 0;
    camera::ForgetViewStep();
    localPosition = keys[0].position;
    localRotation = keys[0].rotation;
    return 1;
}

int Camera::PlaybackStep(float time) {
    if (!(flags & kPlaying) || keys.empty()) return 0;
    const CameraKey* newKeys = keys.data();
    if (keyIndex >= std::ssize(keys) - 1) {
        localPosition = newKeys[std::ssize(keys) - 1].position;
        localRotation = newKeys[std::ssize(keys) - 1].rotation;
        return 0;
    }
    float next = newKeys[keyIndex + 1].time + timeOffset;
    while (time > next) {
        ++keyIndex;
        if (keyIndex >= std::ssize(keys) - 1) {
            localPosition = newKeys[std::ssize(keys) - 1].position;
            localRotation = newKeys[std::ssize(keys) - 1].rotation;
            flags &= ~kPlaying;
            return 0;
        }
        next = newKeys[keyIndex + 1].time + timeOffset;
    }
    const CameraKey& a = newKeys[keyIndex];
    const CameraKey& b = (&a)[1];
    float start = a.time + timeOffset;
    float span = next - start;
    float u = 0.0f;
    EI_NAN_AUDIT(span);
    if (span != 0.0f) u = (time - start) / span;
    float dx = b.position.x - a.position.x;
    float dy = b.position.y - a.position.y;
    float dz = b.position.z - a.position.z;
    localPosition.x = dx * u + a.position.x;
    localPosition.y = dy * u + a.position.y;
    localPosition.z = dz * u + a.position.z;
    localRotation.Slerp(u, a.rotation, b.rotation);
    return 1;
}

void Camera::Pan(const Vector3& direction) {
    Vector3 right = localRotation.AxisX();
    const Quaternion& r = localRotation;
    Vector3 up;
    up.z = Dot2DUnrounded<float>(r.w, r.y, r.x, r.z) * 2.0f;
    up.y = (r.w * r.w - LengthSquaredUnrounded<Sum3::ZXY, float>(r.x, r.y, r.z)) + r.y * r.y * 2.0f;
    up.x = (r.y * r.x - r.w * r.z) * 2.0f;
    float k1 = (1.0f / std::sqrt(LengthSquared2DUnrounded<float>(right.y, right.x))) * direction.x;
    right.x = k1 * right.x;
    float rightY = k1 * right.y;
    float k2 = (1.0f / std::sqrt(LengthSquared2DUnrounded<float>(up.y, up.x))) * direction.y;
    up.x = k2 * up.x;
    float upY = k2 * up.y;
    panTarget.x = up.x + right.x;
    panTarget.y = upY + rightY;
    panTarget.z = 0.0f;
}

void Camera::StopMotion() {
    Vector3 right = localRotation.AxisX();
    Vector3 up = localRotation.AxisY();
    // Multiplies by 0 (keeping NaN and -0), so the zero is read from memory.
    const float zero = CAM_ZERO;
    right.x = zero * right.x;
    right.y = right.y * zero;
    right.z = 0.0f;
    up.x = zero * up.x;
    up.y = up.y * zero;
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

void Camera::SetFocus(const Vector3& p) {
    camera::ForgetViewStep();
    focus = p;
    const TerrainSample ground = SampleTerrain(p, options::g_options.cameraTerrainArea * 0.5f,
                                               options::g_options.cameraTerrainArea, options::g_options.cameraTerrainArea);
    focus.z = ground.average + focusHeight;
}

void Camera::ApplyView() {
    localRotation = viewRotation;
    Vector3 right = localRotation.AxisX();
    Quaternion tilt;
    tilt.SetAngleAxis(-(CAM_RIGHT_ANGLE + pitch), right);
    localRotation.PreMultiply(tilt);
    Vector3 forward = localRotation.AxisZ();
    const float d = distance;
    float ox = forward.x * d;
    float oy = forward.y * d;
    float px = focus.x - ox;
    float py = focus.y - oy;
    float pz = focus.z - d * forward.z;
    localPosition.x = px;
    localPosition.y = py;
    localPosition.z = pz;
    float before;
    before = localPosition.z;
    ClampAboveObjects();
    if (before >= localPosition.z) return;
    // Lifted over an object: aim back down at the focus.
    float dz = localPosition.z - focus.z;
    double across = libcrt::Hypot(localPosition.x - focus.x, localPosition.y - focus.y);
    // The arc tangent of the float and _hypot's double, unrounded into the add of the right angle.
    const float tiltAngle = -pc24::Add(CAM_RIGHT_ANGLE, pc24::Atan2(dz, across));
    localRotation = viewRotation;
    const Quaternion s = localRotation;
    Vector3 axis;
    axis.x = (s.w * s.w - LengthSquaredUnrounded<Sum3::ZYX, float>(s.x, s.y, s.z)) + s.x * s.x * 2.0f;
    axis.y = Dot2DUnrounded<float>(s.x, s.z, s.y, s.w) * 2.0f;
    axis.z = (s.x * s.z - s.y * s.w) * 2.0f;
    Quaternion q;
    q.SetAngleAxis(tiltAngle, axis);
    float y = ((q.z * s.x - s.z * q.x) + s.w * q.y) + s.y * q.w;
    float z = ((q.x * s.y - s.x * q.y) + s.w * q.z) + s.z * q.w;
    float w = s.w * q.w - DotUnrounded<Sum3::ZXY, float>(s.x, s.y, s.z, q.x, q.y, q.z);
    float x = ((s.z * q.y - q.z * s.y) + s.w * q.x) + s.x * q.w;
    localRotation.y = y;
    localRotation.z = z;
    localRotation.w = w;
    localRotation.x = x;
}

void Camera::Update(float dt) {
    Vector3 listener;
    listener = localPosition;
    if (!(flags & kPlaced)) {
        listener = focus;
        if (g_clientWorld.terrain) listener.z = pc24::Add(g_clientWorld.RawSurfaceHeightAt(focus.x, focus.y), focusHeight);
    }
    Vector3 forward = localRotation.AxisZ();
    g_clientWorld.sounds->SetListener(&listener.x, &forward.x);
    if (flags & kPlaced) return;

    CObject3D* target = g_clientWorld.objectManager->FindObjectAs<CObject3D>(static_cast<unsigned>(targetId));
    if (flags & kFollowing) {
        if (target) {
            // Behind the target, looking along its heading.
            const TransformObject* t = target;
            const CObject3D* body = target;
            float lift = body->bounds.boxMax.z + body->bounds.center.z;
            localPosition.x = t->localPosition.x;
            localPosition.y = t->localPosition.y;
            localPosition.z = lift + t->localPosition.z;
            Vector3 side = t->localRotation.AxisY();
            float nx = -side.x;
            Vector3 dir;
            dir.y = -side.y;
            dir.z = 0.0f;
            float inv = 1.0f / std::sqrt(dir.y * dir.y + nx * nx);
            dir.x = nx * inv;
            dir.y = dir.y * inv;
            TurnTowards(dir);
            AlignToTarget();
            return;
        }
    } else if (target) {
        // Drift the focus a fifth of the way to the tracked object.
        const TransformObject* t = target;
        float dx = t->localPosition.x - focus.x;
        float dy = t->localPosition.y - focus.y;
        float width = g_clientWorld.terrain->width;
        focus.x = ClampToMap(dx * kTrackGain + focus.x, width);
        float height = g_clientWorld.terrain->height;
        focus.y = ClampToMap(dy * kTrackGain + focus.y, height);
    }

    // Pan: the velocity eases towards its target.
    Vector3& velocity = panVelocity;
    const Vector3& wanted = panTarget;
    Vector3 change = {wanted.x - velocity.x, wanted.y - velocity.y, wanted.z - velocity.z};
    Vector3 accel;
    ScaleVector(&accel, change, panResponse);
    bool snap = true;
    if (accel.Length() >= 0.001) {   // the double constant (exact as a compare)
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

    if (LengthSquaredUnrounded<Sum3::XYZ, float>(velocity) > 0.0f) {
        const CameraLimits limits = Limits();
        float scale = (distance - limits.min) / ((limits.max - limits.min) * kPanZoomSpan) + kTrackGain;
        float moveX = dt * velocity.x;
        float moveY = dt * velocity.y;
        float stepX = moveX * scale;
        float stepY = moveY * scale;
        float width = g_clientWorld.terrain->width;
        focus.x = ClampToMap(stepX + focus.x, width);
        float height = g_clientWorld.terrain->height;
        float y = ClampToMap(stepY + focus.y, height);
        targetId = -1;
        flags &= ~kFollowing;
        focus.y = y;
        if (flags & kBounded) {
            float bx = focus.x - boundsCenter.x;
            float by = focus.y - boundsCenter.y;
            float d2 = LengthSquared2DUnrounded(by, bx);
            float radius = boundsRadius;
            if (d2 > radius * radius) {
                float inv = 1.0f / std::sqrt(d2);
                float ux = inv * bx;
                float uy = inv * by;
                float px = radius * ux;
                float ny = radius * uy + boundsCenter.y;
                focus.x = px + boundsCenter.x;
                focus.y = ny;
            }
        }
    }

    // The focus height follows the target's or the ground's, a tenth of the way per call.
    const TerrainSample terrain = SampleTerrain(focus, options::g_options.cameraTerrainArea * 0.5f,
                                                options::g_options.cameraTerrainArea, options::g_options.cameraTerrainArea);
    const CObject3D* body = target;
    float ground = target ? body->localPosition.z : terrain.average;
    focus.z = ((ground + focusHeight) - focus.z) * kHeightLag + focus.z;

    // Tilt (pitch in degrees, clamped to the settings' range).
    Approach(&tiltRate, &tiltTarget, tiltResponse, dt);
    EI_NAN_AUDIT(tiltRate);
    if (tiltRate != 0.0f) {
        float newPitch = dt * tiltRate + pitch;
        pitch = newPitch;
        bool alternate = (flags & kAlternate) != 0;
        float most = (alternate ? settings->briefingMaxPitch : settings->cameraDefaultMaxPitch) * CAM_DEGREE;
        if (newPitch > most) pitch = most;
        float least = (alternate ? settings->briefingMinPitch : settings->cameraDefaultMinPitch) * CAM_DEGREE;
        if (least > pitch) pitch = least;
    }

    Approach(&turnRate, &turnTarget, turnResponse, dt);
    EI_NAN_AUDIT(turnRate);
    if (turnRate != 0.0f) {
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Quaternion q;
        q.SetAxisAngle(up, dt * turnRate);
        const Quaternion r = viewRotation;
        float x = ((r.z * q.y - r.y * q.z) + q.w * r.x) + r.w * q.x;
        float y = ((r.x * q.z - r.z * q.x) + q.w * r.y) + r.w * q.y;
        float z = ((r.y * q.x - q.y * r.x) + r.z * q.w) + r.w * q.z;
        float w = q.w * r.w - DotUnrounded<Sum3::ZYX, float>(q.x, q.y, q.z, r.x, r.y, r.z);
        viewRotation.z = z;
        viewRotation.y = y;
        viewRotation.w = w;
        viewRotation.x = x;
    }

    float currentZoomTarget = zoomTarget;
    Approach(&zoomRate, &currentZoomTarget, zoomResponse, dt);
    EI_NAN_AUDIT(zoomRate);
    if (zoomRate != 0.0f) distance = dt * zoomRate + distance;

    const CameraLimits limits = Limits();
    EI_NAN_AUDIT(distance, limits.hardMin, limits.min);
    if (distance < limits.hardMin) {
        distance = limits.hardMin;
    } else if (distance < limits.min) {
        distance = (limits.min - distance) * dt * kEase / (limits.min - limits.hardMin) + distance;
    } else if (distance > limits.hardMax) {
        distance = limits.hardMax;
    } else if (distance > limits.max) {
        distance = distance - (distance - limits.max) * dt * kEase / (limits.hardMax - limits.max);
    }

    ApplyView();

    if (shakes.empty()) return;
    float offset = 0.0f;
    for (auto node = shakes.begin(); node != shakes.end();) {
        CameraShake* s = &(*node);
        // The cosine (64 bits) times the exponential, then the amplitude and the sum at 24 bits.
        float damping = pc24::Exp(-(s->decay * s->elapsed));
        pc24::Unrounded wave = pc24::Cos(camera::kHalfPi * s->elapsed * kShakeFrequency);
        offset = pc24::Mul(wave, damping) * s->amplitude + offset;
        float elapsed = dt + s->elapsed;
        s->elapsed = elapsed;
        if (elapsed >= s->duration)
            node = shakes.erase(node);
        else
            ++node;   // also NaN
    }
    localPosition.z = offset + localPosition.z;
}

void Camera::ZoomBy(float steps) {
    float d = pc24::Mul(libcrt::CiPow(CAM_ZOOM_STEP, steps), distance);
    distance = d;
    if (!settings) return;
    const CameraLimits limits = Limits();
    EI_NAN_AUDIT(d, limits.min);
    if (d < limits.min) {
        distance = limits.min;
        return;
    }
    if (d > limits.max) distance = limits.max;
}

bool Camera::SaveView(unsigned slot) {
    if (slot >= 8) return false;
    CameraView& v = views[slot];
    v.distance = distance;
    v.pitch = pitch;
    v.rotation = viewRotation;
    ViewsToSettings(settings);
    return true;
}

bool Camera::RestoreView(unsigned slot) {
    if (slot >= 8) return false;
    camera::ForgetViewStep();
    distance = views[slot].distance;
    pitch = views[slot].pitch;
    ResetMotion(this);
    return true;
}

void Camera::SetDistancePitch(const CameraView* view) {
    camera::ForgetViewStep();
    distance = view->distance;
    pitch = view->pitch;
    ResetMotion(this);
}

CameraLimits Camera::Limits() const {
    const options::GameOptions* s = settings;
    if (flags & kAlternate)
        return {s->briefingMinDistance, s->briefingMaxDistance, s->briefingMinLimitDistance, s->briefingMaxLimitDistance};
    return {s->cameraMinDistance, s->cameraMaxDistance, s->cameraMinLimitDistance, s->cameraMaxLimitDistance};
}

void Camera::ViewsToSettings(options::GameOptions* gameOptions) {
    for (int i = 0; i < 4; ++i) {
        const CameraView& v = views[i];
        options::CameraShortcutOptions& r = gameOptions->cameraShortcuts[i];
        r.distance = v.distance;
        r.pitch = v.pitch;
        const Quaternion& q = v.rotation;
        float sum = LengthSquaredUnrounded<Sum3::ZXY, float>(q.x, q.y, q.z);
        if (sum > kAxisTiny) {
            const double arc = libcrt::CiAcos(q.w);   // the CRT's double (FPATAN's 64 bits stored)
            float inv = 1.0f / std::sqrt(sum);
            r.axisX = inv * q.x;
            r.axisY = inv * q.y;
            r.axisZ = inv * q.z;
            r.angle = pc24::Add(arc, arc);
        } else {
            r.axisX = 1.0f;
            r.axisY = 0.0f;
            r.axisZ = 0.0f;
            r.angle = 0.0f;
        }
    }
}

void Camera::ViewsFromSettings(const options::GameOptions* gameOptions) {
    for (int i = 0; i < 4; ++i) {
        CameraView& v = views[i];
        const options::CameraShortcutOptions& r = gameOptions->cameraShortcuts[i];
        v.distance = r.distance;
        v.pitch = r.pitch;
        Vector3 axis = {r.axisX, r.axisY, r.axisZ};
        v.rotation.FromAxisAngle(axis, r.angle);
    }
}

void Camera::SetHeading(float angle) {
    camera::ForgetViewStep();
    Vector3 up = {0.0f, 0.0f, 1.0f};
    viewRotation.FromAxisAngle(up, angle);
    ApplyView();
}

void Camera::SetFree(bool on) {
    if (on)
        flags |= kFree;
    else
        flags &= ~kFree;
}

void Camera::LookFrom(const Vector3& from, const Vector3& to) {
    camera::ForgetViewStep();
    flags |= kFree;
    localPosition = from;
    LookDirection(to.x - from.x, to.y - from.y, to.z - from.z);
}

void Camera::SetBounds(const Vector3& center, float radius) {
    boundsCenter = center;
    boundsRadius = radius;
    flags |= kBounded;
}

void Camera::Shake(const Vector3& source, float duration, float amplitude, float decay) {
    const Vector3& at = (flags & kPlaced) ? localPosition : focus;
    double newDistance = libcrt::Hypot(at.x - source.x, at.y - source.y);
    float distanceF = static_cast<float>(newDistance);
    if (newDistance >= kShakeRange) return;   // the CRT's double (exact as a compare)
    float strength = amplitude;
    if (distanceF > kShakeFull) strength = (1.0f - (distanceF - kShakeFull) / (kShakeRange - kShakeFull)) * amplitude;
    CameraShake entry;
    entry.elapsed = 0.0f;
    entry.duration = duration * kShakeTicks;
    entry.amplitude = strength;
    entry.decay = decay;
    shakes.push_back(entry);
}
