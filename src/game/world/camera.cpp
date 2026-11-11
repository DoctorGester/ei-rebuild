// The camera (Camera, camera.h): the per-frame update and view placement, focus and terrain sampling,
// panning / zoom / saved views, shakes and recorded camera paths.
#include "game/world/camera.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_maths.h"
#include "engine/scene/drawn_motion.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/sound/sound.h"
#include "game/terrain/terrain.h"
#include "game/world/world.h"
#include "engine/scene/scene_renderer.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

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
constexpr float kZoomStep = 1.05f;

// Branch-free clamp of a map coordinate to [0, last - 1]: NaN becomes 0, and a NaN limit wins.
float ClampToMap(float v, float last) {
    float low = MaxKeepB(v, 0.0f);
    float limit = last - 1.0f;
    return MinKeepA(limit, low);
}

// Moves `rate` towards `target` by (target - rate) * response * dt; snaps to the target when the
// step is tiny or the rate's magnitude passes the target's.
void Approach(float& rate, float target, float response, float dt) {
    float step = (target - rate) * response;
    if (AbsBits(step) < 0.001f) {
        rate = target;
        return;
    }
    float before = rate * rate;
    float after = step * dt + rate;
    rate = after;
    float after2 = after * after;
    float target2 = target * target;
    if ((before < target2 && after2 > target2) || (before > target2 && after2 < target2))
        rate = target;
}

// ---- The drawn view (camera::DrawnView; docs/notes/renderer.md section 14) -----------------------

// A tick whose update moved the focus by more than this (map units), or the camera nearer to its focus or
// farther from it by more, placed it anew (a map change, a jump of the focus): shown at once, not spread. A
// turn, pan, tilt, zoom or lift over the ground stays within both in a tick, however fast the camera turns.
constexpr float kViewCut = 8.0f;
// A settle left shorter than this (map units) is dropped.
constexpr float kSettleDone = 1e-4f;

// The last client tick's step of the camera (the change its update made: focus and distance; the turns as the
// rotations that undo them; `stretch`, the part of the camera's reach from its focus the step added, 1 - before /
// after; nothing for a cut, a placement or a load), the pose before that update, the part of the step the frames
// spread (`spread`: taken once, by the pass that ran the tick, so it stays the same until the next tick) and the
// part the frame has not reached yet. `placed`: placed since the last frame drew the world (ForgetViewStep).
// `settle`: what the drawn focus and position add to the camera's own while it glides onto a target given before
// the first frame after a placement (Camera::SetTargetId): the drawn view shows the glide's end at once, the
// camera's own state (saved, picked through) keeps the game's glide.
struct ViewStep {
    camera::ViewPose before{};
    Vector3 focus{};
    Quaternion undoRotation{1.0f, 0.0f, 0.0f, 0.0f}, undoHeading{1.0f, 0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    float stretch = 0.0f;
    float spread = 0.0f;
    bool spreadTaken = true;
    float back = 0.0f;
    bool placed = false;
    Vector3 settle{};
};
ViewStep g_viewStep;
camera::ViewPose g_frameView{};   // camera::FrameView

// The camera's pose as drawn before the tick's spread: its own, moved by the settle.
camera::ViewPose PoseOf(const Camera* c) {
    const Vector3& settle = g_viewStep.settle;
    return camera::ViewPose{c->localPosition + settle, c->localRotation, c->focus + settle, c->viewRotation, c->distance};
}

// The rotation that turns `to` back to `from` (from * to^-1). The inverse, not the conjugate: a camera file's key may
// hold a rotation that is not of unit length (Lost in Astral's mainmenu.cam: 1.16), and with the conjugate a camera
// standing still undid by a scale of its length squared, which the frames between ticks drew as a zoom in and out.
Quaternion Undo(const Quaternion& from, const Quaternion& to) {
    const float inverse = 1.0f / Dot(to, to);
    Quaternion undo{to.w * inverse, -to.x * inverse, -to.y * inverse, -to.z * inverse};
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
float Reach(const camera::ViewPose& pose) { return std::sqrt(DistanceSquared(pose.position, pose.focus)); }

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
    // A tick after a placement starts from a pose no frame showed (a load leaves the camera's own position and
    // turn as they were until its update places it by the loaded focus): shown at once, as a cut.
    const bool kept = !step.placed && DistanceSquared(now.focus, before.focus) <= kViewCut * kViewCut &&
                      std::fabs(reachNow - reachBefore) <= kViewCut;
    step.focus = kept ? now.focus - before.focus : kZeroVector;
    step.distance = kept ? now.distance - before.distance : 0.0f;
    step.stretch = kept && reachNow > 0.0f ? 1.0f - reachBefore / reachNow : 0.0f;
    step.undoRotation = kept ? Undo(before.rotation, now.rotation) : kIdentityRotation;
    step.undoHeading = kept ? Undo(before.heading, now.heading) : kIdentityRotation;
    step.spreadTaken = false;
}

void camera::ForgetViewStep() {
    g_viewStep = ViewStep{};
    g_viewStep.placed = true;
}

void camera::SettleBy(const Vector3& gap) { g_viewStep.settle = gap; }

void camera::ForgetSettle() { g_viewStep.settle = kZeroVector; }

void camera::ShrinkSettle(float keep, bool height) {
    Vector3& settle = g_viewStep.settle;
    if (height) {
        settle.z *= keep;
    } else {
        settle.x *= keep;
        settle.y *= keep;
    }
    if (LengthSquared(settle) < kSettleDone * kSettleDone) settle = kZeroVector;
}

void camera::SetViewProgress(float reached, float passTicks) {
    ViewStep& step = g_viewStep;
    if (!step.spreadTaken) {
        step.spread = drawn::PassSpread(passTicks);
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
    out.focus -= step.focus * back;
    out.position -= step.focus * back + (offset - turned);
    out.rotation.PreMultiply(part);
    out.heading = TurnedBack(out.heading, step.undoHeading, back);
    out.distance -= step.distance * back;
    return out;
}

void camera::TakeFrameView(const Camera* camera) {
    g_frameView = DrawnView(camera);
    g_sceneRenderer.view.position = g_frameView.position;
    g_sceneRenderer.view.rotation = g_frameView.rotation;
    g_viewStep.placed = false;
}

const camera::ViewPose& camera::FrameView() { return g_frameView; }

TerrainSample SampleTerrain(const Vector3& p, float step, float halfX, float halfY) {
    const float centre = g_clientWorld.terrain->CalmSurfaceHeight(p.x, p.y);
    TerrainSample sample{centre, centre, centre};
    int count = 1;
    float x = p.x - halfX;
    while (x <= halfX + p.x) {
        if (x >= 0.0f && g_clientWorld.terrain->width > x) {
            float y = p.y - halfY;
            while (y <= halfY + p.y) {
                if (y >= 0.0f && g_clientWorld.terrain->height > y) {
                    float h = g_clientWorld.terrain->CalmSurfaceHeight(x, y);
                    float low = sample.minimum;
                    sample.minimum = MinKeepA(h, low);
                    float high = sample.maximum;
                    sample.maximum = MaxKeepB(high, h);
                    ++count;
                    sample.average = h + sample.average;
                }
                y += step;
            }
        }
        x += step;
    }
    sample.average = sample.average / static_cast<float>(count);   // a grid's point count: exact as a float
    return sample;
}

bool Camera::PlaybackStart() {
    if (keys.empty()) return false;
    camera::ForgetViewStep();
    localPosition = keys[0].position;
    localRotation = keys[0].rotation;
    return true;
}

void Camera::PlaybackStep(float time) {
    if (!(flags & kPlaying) || keys.empty()) return;
    const std::ptrdiff_t last = std::ssize(keys) - 1;
    if (keyIndex >= last) {
        localPosition = keys[last].position;
        localRotation = keys[last].rotation;
        return;
    }
    float next = keys[keyIndex + 1].time + timeOffset;
    while (time > next) {
        ++keyIndex;
        if (keyIndex >= last) {
            localPosition = keys[last].position;
            localRotation = keys[last].rotation;
            flags &= ~kPlaying;
            return;
        }
        next = keys[keyIndex + 1].time + timeOffset;
    }
    const CameraKey& a = keys[keyIndex];
    const CameraKey& b = keys[keyIndex + 1];
    float start = a.time + timeOffset;
    float span = next - start;
    float u = 0.0f;
    if (span != 0.0f) u = (time - start) / span;
    localPosition = Lerp(a.position, b.position, u);
    localRotation.Slerp(u, a.rotation, b.rotation);
}

// x along the camera's X axis on the ground, y along that axis turned a quarter back about Z (towards the screen's
// bottom). Not along the camera's own Y axis: its ground part points the other way when the camera looks up (a pitch
// below 0, or a camera lifted over the ground and aimed back up at its focus) and has no length when it looks level.
void Camera::Pan(const Vector3& direction) {
    const Vector3 right = localRotation.AxisX();
    const Vector2 across = XY(right) * (1.0f / std::sqrt(LengthSquared2D(right.y, right.x)));
    const Vector2 back = {across.y, -across.x};
    panTarget = WithZ(across * direction.x + back * direction.y, 0.0f);
}

void Camera::StopMotion() {
    Vector3 right = localRotation.AxisX();
    Vector3 up = localRotation.AxisY();
    // Multiplies by 0 (keeping NaN and -0).
    const float zero = 0.0f;
    panTarget = WithZ(XY(right) * zero + XY(up) * zero, 0.0f);
    // The rates stay: a rate is replaced by a target of 0 only when its magnitude is below 0 (never).
    tiltTarget = 0.0f;
    turnTarget = 0.0f;
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
    localRotation.PreMultiply(Quaternion::AxisAngle(right, -(camera::kRightAngle + pitch)));
    Vector3 forward = localRotation.AxisZ();
    localPosition = focus - forward * distance;
    const float before = localPosition.z;
    ClampAboveObjects();
    if (before >= localPosition.z) return;
    // Lifted over an object: aim back down at the focus.
    float dz = localPosition.z - focus.z;
    float across = lib::Hypot(localPosition.x - focus.x, localPosition.y - focus.y);
    const float tiltAngle = -(camera::kRightAngle + lib::Atan2(dz, across));
    localRotation = viewRotation;
    const Quaternion s = localRotation;
    const Vector3 axis = s.AxisX();
    localRotation = Quaternion::AxisAngle(axis, tiltAngle) * s;
}

void Camera::Update(float dt) {
    Vector3 listener = localPosition;
    if (!(flags & kPlaced)) {
        listener = focus;
        if (g_clientWorld.terrain) listener.z = g_clientWorld.terrain->CalmSurfaceHeight(focus.x, focus.y) + focusHeight;
    }
    Vector3 forward = localRotation.AxisZ();
    g_clientWorld.sounds->SetListener(listener, forward);
    if (flags & kPlaced) {
        camera::ForgetSettle();
        return;
    }

    Object3D* target = g_clientWorld.objectManager->FindObjectAs<Object3D>(static_cast<unsigned>(targetId));
    if (flags & kFollowing) {
        if (target) {
            // Behind the target, looking along its heading.
            float lift = target->bounds.boxMax.z + target->bounds.center.z;
            localPosition = {target->localPosition.x, target->localPosition.y, lift + target->localPosition.z};
            Vector3 side = target->localRotation.AxisY();
            float nx = -side.x;
            Vector3 dir;
            dir.y = -side.y;
            dir.z = 0.0f;
            float inv = 1.0f / std::sqrt(dir.y * dir.y + nx * nx);
            dir.x = nx * inv;
            dir.y *= inv;
            TurnTowards(dir);
            AlignToTarget();
            camera::ForgetSettle();
            return;
        }
    } else if (target) {
        // Drift the focus a fifth of the way to the tracked object.
        float dx = target->localPosition.x - focus.x;
        float dy = target->localPosition.y - focus.y;
        float width = g_clientWorld.terrain->width;
        focus.x = ClampToMap(dx * kTrackGain + focus.x, width);
        float height = g_clientWorld.terrain->height;
        focus.y = ClampToMap(dy * kTrackGain + focus.y, height);
        // The drift is linear: the gap to the settled focus shrinks by the same part wherever the target goes.
        camera::ShrinkSettle(1.0f - kTrackGain, false);
    } else {
        camera::ShrinkSettle(0.0f, false);
    }

    // Pan: the velocity eases towards its target.
    Vector3& velocity = panVelocity;
    const Vector3& wanted = panTarget;
    Vector3 change = wanted - velocity;
    Vector3 accel = change * panResponse;
    bool snap = true;
    if (Length(accel) >= 0.001f) {
        float before = Dot(velocity, velocity);
        Vector3 step = accel * dt;
        velocity += step;
        float after = Dot(velocity, velocity);
        float goal = Dot(wanted, wanted);
        snap = (before < goal && after > goal) || (before > goal && after < goal);
    }
    if (snap) velocity = wanted;

    if (LengthSquared(velocity) > 0.0f) {
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
        camera::ShrinkSettle(0.0f, false);
        focus.y = y;
        if (flags & kBounded) {
            float bx = focus.x - boundsCenter.x;
            float by = focus.y - boundsCenter.y;
            float d2 = LengthSquared2D(by, bx);
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
    float ground = target ? target->localPosition.z : terrain.average;
    focus.z = ((ground + focusHeight) - focus.z) * kHeightLag + focus.z;
    camera::ShrinkSettle(1.0f - kHeightLag, true);

    // Tilt (pitch in degrees, clamped to the settings' range).
    Approach(tiltRate, tiltTarget, tiltResponse, dt);
    if (tiltRate != 0.0f) {
        float newPitch = dt * tiltRate + pitch;
        pitch = newPitch;
        bool alternate = (flags & kAlternate) != 0;
        float most = (alternate ? settings->briefingMaxPitch : settings->cameraDefaultMaxPitch) * ei::kRadiansPerDegree;
        if (newPitch > most) pitch = most;
        float least = (alternate ? settings->briefingMinPitch : settings->cameraDefaultMinPitch) * ei::kRadiansPerDegree;
        if (least > pitch) pitch = least;
    }

    Approach(turnRate, turnTarget, turnResponse, dt);
    if (turnRate != 0.0f) {
        const Vector3 up = {0.0f, 0.0f, 1.0f};
        viewRotation = Quaternion::AxisAngle(up, dt * turnRate) * viewRotation;
    }

    Approach(zoomRate, zoomTarget, zoomResponse, dt);
    if (zoomRate != 0.0f) distance = dt * zoomRate + distance;

    const CameraLimits limits = Limits();
    if (distance < limits.hardMin) {
        distance = limits.hardMin;
    } else if (distance < limits.min) {
        distance = (limits.min - distance) * dt * kEase / (limits.min - limits.hardMin) + distance;
    } else if (distance > limits.hardMax) {
        distance = limits.hardMax;
    } else if (distance > limits.max) {
        distance -= (distance - limits.max) * dt * kEase / (limits.hardMax - limits.max);
    }

    ApplyView();

    if (shakes.empty()) return;
    float offset = 0.0f;
    for (CameraShake& s : shakes) {
        // The cosine (64 bits) times the exponential, then the amplitude and the sum at 24 bits.
        float damping = lib::Exp(-(s.decay * s.elapsed));
        float wave = lib::Cos(kHalfPi * s.elapsed * kShakeFrequency);
        offset = (wave * damping) * s.amplitude + offset;
        s.elapsed = dt + s.elapsed;
    }
    std::erase_if(shakes, [](const CameraShake& s) { return s.elapsed >= s.duration; });   // a NaN stays
    localPosition.z = offset + localPosition.z;
    ClampAboveObjects();   // a shake down stops where the near plane would reach under the ground
}

void Camera::ZoomBy(float steps) {
    float d = lib::Pow(kZoomStep, steps) * distance;
    distance = d;
    if (!settings) return;
    const CameraLimits limits = Limits();
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
    SetDistancePitch(&views[slot]);
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
        float sum = LengthSquared(Vector3{q.x, q.y, q.z});
        if (sum > kAxisTiny) {
            const float arc = lib::Acos(q.w);
            float inv = 1.0f / std::sqrt(sum);
            r.axisX = inv * q.x;
            r.axisY = inv * q.y;
            r.axisZ = inv * q.z;
            r.angle = arc + arc;
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
        v.rotation = Quaternion::AxisAngle(axis, r.angle);
    }
}

void Camera::SetHeading(float angle) {
    camera::ForgetViewStep();
    Vector3 up = {0.0f, 0.0f, 1.0f};
    viewRotation = Quaternion::AxisAngle(up, angle);
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

void Camera::GroundPoint(Vector3* out) const {
    const Vector3 axis = localRotation.AxisZ();
    if (std::fabs(axis.z) > 1e-5f && -(localPosition.z / axis.z) > 0.0f) {
        *out = axis * -(localPosition.z / axis.z) + localPosition;
        return;
    }
    *out = localPosition;
}

void Camera::SetTurnRate(float rate) {
    turnTarget = rate;
    if (std::fabs(turnRate) < std::fabs(rate)) turnRate = turnTarget;
}

void Camera::SetTiltRate(float rate) {
    tiltTarget = rate;
    if (std::fabs(tiltRate) < std::fabs(rate)) tiltRate = tiltTarget;
}

void Camera::SetZoomTarget(float value) { zoomTarget = value; }

void Camera::SetTarget(const EngineObject* target) { SetTargetId(target ? target->id : -1); }

void Camera::SetTargetId(int newId) {
    targetId = newId;
    if (newId != -1) {
        panTarget = camera::kZeroVector;
        panVelocity = camera::kZeroVector;
    }
    // A target given before any frame showed the camera since its placement (the screens take the leader as they
    // are made after a load or a zone's entry) is drawn as reached: the first frame shows the focus where the
    // update's drift (a fifth of the way a call) and height lag (a tenth) settle it, not the placed focus gliding
    // there. The camera's own focus glides as the game's does (it is saved, and clicks pick through it).
    camera::ForgetSettle();
    if (newId == -1 || !g_viewStep.placed || (flags & kFollowing)) return;
    const Object3D* target = g_clientWorld.objectManager->FindObjectAs<Object3D>(static_cast<unsigned>(newId));
    if (!target) return;
    const Vector3 settled = {ClampToMap(target->localPosition.x, g_clientWorld.terrain->width),
                             ClampToMap(target->localPosition.y, g_clientWorld.terrain->height),
                             target->localPosition.z + focusHeight};
    camera::SettleBy(settled - focus);
}

void Camera::SetBounds(const Vector3& center, float radius) {
    boundsCenter = center;
    boundsRadius = radius;
    flags |= kBounded;
}

void Camera::Shake(const Vector3& source, float duration, float amplitude, float decay) {
    const Vector3& at = (flags & kPlaced) ? localPosition : focus;
    float newDistance = lib::Hypot(at.x - source.x, at.y - source.y);
    if (newDistance >= kShakeRange) return;
    float strength = amplitude;
    if (newDistance > kShakeFull) strength = (1.0f - (newDistance - kShakeFull) / (kShakeRange - kShakeFull)) * amplitude;
    shakes.push_back({0.0f, duration * kShakeTicks, strength, decay});
}
