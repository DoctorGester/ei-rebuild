// The camera (CCamera, camera.h): the per-frame update and view placement, focus and
// terrain sampling, panning / zoom / saved views, shakes and recorded camera paths
// (0040D9B0-004114F0). Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "camera.h"

#include "base_manager.h"
#include "core_maths.h"
#include "map_object_figures.h"
#include "sound.h"
#include "terrain.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "game_singletons.h"
#include "lib/crt.h"

#include <cstring>

namespace {

using camera::D;

constexpr float kTrackGain = 0.2f;        // 0073BAD4
constexpr float kPanZoomSpan = 1.125f;    // 0073BAD0
constexpr float kHeightLag = 0.1f;        // 0073B850
constexpr float kEase = 1.1f;             // 0073BAC4
constexpr float kShakeFrequency = 0.26666668f;  // 0073BAC0
constexpr float kShakeRange = 20.0f;      // 0073B9D4
constexpr float kShakeFull = 8.0f;        // 0073B9D0
constexpr float kShakeTicks = 30.0f;      // 0073BAF0
constexpr float kAxisTiny = 1e-8f;        // 0073BAE0

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void CopyFloat(float* dst, const float* src) { std::memcpy(dst, src, sizeof(float)); }

// The original's branch-free clamp of a map coordinate to [0, last - 1]: NaN becomes 0, and a NaN
// limit wins.
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
        CopyFloat(rate, target);
        return;
    }
    double before = D(*rate) * *rate;
    double after = D(step) * dt + *rate;
    *rate = static_cast<float>(after);
    float after2 = static_cast<float>(after * after);
    float target2 = static_cast<float>(D(*target) * *target);
    EI_NAN_AUDIT(before, after2, target2);
    if ((before < target2 && after2 > target2) || (before > target2 && after2 < target2))
        CopyFloat(rate, target);
}

void ResetMotion(CCamera* c) {
    std::memcpy(&c->offset98_, &camera::kZeroVector, sizeof(Vector3));
    std::memcpy(&c->offsetA4_, &camera::kZeroVector, sizeof(Vector3));
    const float zero = 0.0f;
    CopyFloat(&c->turnRate_, &zero);
    CopyFloat(&c->turnTarget_, &zero);
    CopyFloat(&c->tiltRate_, &zero);
    CopyFloat(&c->tiltTarget_, &zero);
    CopyFloat(&c->zoomRate_, &zero);
    c->zoomTarget_ = 0;
}

} // namespace

void __stdcall SampleTerrain(const Vector3* p, const Vector3*, const Vector3*, float step, float halfX, float halfY,
                             float* average, float* minimum, float* maximum) {
    double centre = TheClientWorld()->ClientRawSurfaceHeight(p->x, p->y);
    *minimum = static_cast<float>(centre);
    *maximum = static_cast<float>(centre);
    *average = static_cast<float>(centre);
    int count = 1;
    float x = static_cast<float>(D(p->x) - halfX);
    while (x <= D(halfX) + p->x) {
        if (x >= 0.0f && camera::TheTerrain()->width > x) {
            float y = static_cast<float>(D(p->y) - halfY);
            while (y <= D(halfY) + p->y) {
                if (y >= 0.0f && camera::TheTerrain()->height > y) {
                    float h = static_cast<float>(TheClientWorld()->ClientRawSurfaceHeight(x, y));
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

Vector3* CubicCurve3::Evaluate(Vector3* out, float t) const {
    double z = ((D(t) * c[11] + c[10]) * t + c[9]) * t + c[8];
    double y = ((D(t) * c[7] + c[6]) * t + c[5]) * t + c[4];
    double x = ((D(t) * c[3] + c[2]) * t + c[1]) * t + c[0];
    out->x = static_cast<float>(x);
    out->y = static_cast<float>(y);
    out->z = static_cast<float>(z);
    return out;
}

int CCamera::PlaybackStart() {
    if (keyCount_ == 0) return 0;
    std::memcpy(&localPosition_, &keys_[0].position, sizeof(Vector3));
    std::memcpy(&localRotation_, &keys_[0].rotation, sizeof(Quaternion));
    return 1;
}

int CCamera::PlaybackStep(float time) {
    if (!(flags_ & kPlaying) || keyCount_ == 0) return 0;
    const CameraKey* keys = keys_;
    if (keyIndex_ >= keyCount_ - 1) {
        std::memcpy(&localPosition_, &keys[keyCount_ - 1].position, sizeof(Vector3));
        std::memcpy(&localRotation_, &keys[keyCount_ - 1].rotation, sizeof(Quaternion));
        return 0;
    }
    double next = D(keys[keyIndex_ + 1].time) + timeOffset_;
    while (time > next) {
        ++keyIndex_;
        if (keyIndex_ >= keyCount_ - 1) {
            std::memcpy(&localPosition_, &keys[keyCount_ - 1].position, sizeof(Vector3));
            std::memcpy(&localRotation_, &keys[keyCount_ - 1].rotation, sizeof(Quaternion));
            flags_ &= ~kPlaying;
            return 0;
        }
        next = D(keys[keyIndex_ + 1].time) + timeOffset_;
    }
    const CameraKey& a = keys[keyIndex_];
    const CameraKey& b = (&a)[1];
    float start = static_cast<float>(D(a.time) + timeOffset_);
    double span = next - start;
    float u = 0.0f;
    EI_NAN_AUDIT(span);
    if (span != 0.0) u = static_cast<float>((D(time) - start) / span);
    double dx = D(b.position.x) - a.position.x;
    float dy = static_cast<float>(D(b.position.y) - a.position.y);
    float dz = static_cast<float>(D(b.position.z) - a.position.z);
    float sy = static_cast<float>(D(dy) * u);
    float sz = static_cast<float>(D(dz) * u);
    localPosition_.x = static_cast<float>(dx * u + a.position.x);
    localPosition_.y = static_cast<float>(D(sy) + a.position.y);
    localPosition_.z = static_cast<float>(D(sz) + a.position.z);
    localRotation_.Slerp(u, a.rotation, b.rotation);
    return 1;
}

void CCamera::Pan(const Vector3* direction) {
    Vector3 right;
    localRotation_.AxisX(&right);
    const Quaternion& r = localRotation_;
    Vector3 up;
    up.z = static_cast<float>(Dot2DUnrounded<double>(r.w, r.y, r.x, r.z) * 2.0);
    up.y = static_cast<float>((D(r.w) * r.w - LengthSquaredUnrounded<Sum3::ZXY, double>(r.x, r.y, r.z)) + D(r.y) * r.y * 2.0);
    up.x = static_cast<float>((D(r.y) * r.x - D(r.w) * r.z) * 2.0);
    double k1 = (1.0 / std::sqrt(LengthSquared2DUnrounded<double>(right.y, right.x))) * direction->x;
    right.x = static_cast<float>(k1 * right.x);
    double rightY = k1 * right.y;
    double k2 = (1.0 / std::sqrt(LengthSquared2DUnrounded<double>(up.y, up.x))) * direction->y;
    up.x = static_cast<float>(k2 * up.x);
    double upY = k2 * up.y;
    offsetA4_.x = static_cast<float>(D(up.x) + right.x);
    offsetA4_.y = static_cast<float>(upY + rightY);
    offsetA4_.z = 0.0f;
}

void CCamera::StopMotion() {
    Vector3 right, up;
    localRotation_.AxisX(&right);
    localRotation_.AxisY(&up);
    // The original multiplies by 0 (keeping NaN and -0), so the zero is read from memory.
    const float zero = CAM_ZERO;
    right.x = static_cast<float>(D(zero) * right.x);
    right.y = static_cast<float>(D(right.y) * zero);
    right.z = 0.0f;
    up.x = static_cast<float>(D(zero) * up.x);
    up.y = static_cast<float>(D(up.y) * zero);
    up.z = 0.0f;
    Vector3 sum;
    AddVectors(&sum, right, up);
    std::memcpy(&offsetA4_, &sum, sizeof(Vector3));
    // SetTiltRate(0) / SetTurnRate(0) inlined: a rate is replaced when its magnitude is below the
    // target's (never, for 0; the original's FCOM replaced a NaN rate).
    EI_NAN_AUDIT(tiltRate_, turnRate_);
    tiltTarget_ = 0.0f;
    if (std::fabs(tiltRate_) < std::fabs(tiltTarget_)) CopyFloat(&tiltRate_, &tiltTarget_);
    turnTarget_ = 0.0f;
    if (std::fabs(turnRate_) < std::fabs(turnTarget_)) CopyFloat(&turnRate_, &turnTarget_);
    zoomTarget_ = 0;
}

void CCamera::SetFocus(const Vector3* p) {
    std::memcpy(&focus_, p, sizeof(Vector3));
    float average, low, high;
    SampleTerrain(p, &camera::kZeroVector, &camera::kZeroVector, static_cast<float>(D(camera::CellSize()) * 0.5f), camera::CellSize(), camera::CellSize(), &average,
                  &low, &high);
    focus_.z = static_cast<float>(D(average) + focusHeight_);
}

void CCamera::ApplyView() {
    std::memcpy(&localRotation_, &viewRotation_, sizeof(Quaternion));
    Vector3 right;
    localRotation_.AxisX(&right);
    Quaternion tilt;
    tilt.SetAngleAxis(static_cast<float>(-(CAM_RIGHT_ANGLE + pitch_)), right);
    localRotation_.PreMultiply(tilt);
    Vector3 forward;
    localRotation_.AxisZ(&forward);
    const float d = distance_;
    float ox = static_cast<float>(D(forward.x) * d);
    float oy = static_cast<float>(D(forward.y) * d);
    float px = static_cast<float>(D(focus_.x) - ox);
    float py = static_cast<float>(D(focus_.y) - oy);
    float pz = static_cast<float>(D(focus_.z) - D(d) * forward.z);
    localPosition_.x = px;
    localPosition_.y = py;
    localPosition_.z = pz;
    float before;
    CopyFloat(&before, &localPosition_.z);
    ClampAboveObjects();
    if (before >= localPosition_.z) return;
    // Lifted over an object: aim back down at the focus.
    double dz = D(localPosition_.z) - focus_.z;
    double across = libcrt::Hypot(D(localPosition_.x) - focus_.x, D(localPosition_.y) - focus_.y);
    double angle = x87::fpatan(dz, across);
    std::memcpy(&localRotation_, &viewRotation_, sizeof(Quaternion));
    const Quaternion s = localRotation_;
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
    localRotation_.y = y;
    localRotation_.z = z;
    localRotation_.w = w;
    localRotation_.x = x;
}

void CCamera::Update(float dt) {
    Vector3 listener;
    std::memcpy(&listener, &localPosition_, sizeof(Vector3));
    if (!(flags_ & kPlaced)) {
        std::memcpy(&listener, &focus_, sizeof(Vector3));
        if (camera::TheTerrain()) listener.z = static_cast<float>(TheClientWorld()->ClientRawSurfaceHeight(focus_.x, focus_.y) + focusHeight_);
    }
    Vector3 forward;
    localRotation_.AxisZ(&forward);
    TheSoundManager()->SetListener(&listener.x, &forward.x);
    if (flags_ & kPlaced) return;

    CEngineObject* target = TheObjectManager()->FindObject(static_cast<unsigned>(targetId_));
    if (flags_ & kFollowing) {
        if (target) {
            // Behind the target, looking along its heading.
            const CTransformObject* t = static_cast<const CTransformObject*>(target);
            const CObject3D* body = static_cast<const CObject3D*>(target);
            double lift = D(body->above.z) + body->center.z;
            std::memcpy(&localPosition_.x, &t->localPosition_.x, sizeof(float));
            std::memcpy(&localPosition_.y, &t->localPosition_.y, sizeof(float));
            localPosition_.z = static_cast<float>(lift + t->localPosition_.z);
            Vector3 side;
            t->localRotation_.AxisY(&side);
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
        const CTransformObject* t = static_cast<const CTransformObject*>(target);
        double dx = D(t->localPosition_.x) - focus_.x;
        float dy = static_cast<float>(D(t->localPosition_.y) - focus_.y);
        float width = camera::TheTerrain()->width;
        float dyStep = static_cast<float>(D(dy) * kTrackGain);
        focus_.x = ClampToMap(static_cast<float>(dx * kTrackGain + focus_.x), width);
        float height = camera::TheTerrain()->height;
        focus_.y = ClampToMap(static_cast<float>(D(dyStep) + focus_.y), height);
    }

    // Pan: the velocity eases towards its target.
    Vector3& velocity = offset98_;
    const Vector3& wanted = offsetA4_;
    Vector3 change = {static_cast<float>(D(wanted.x) - velocity.x), static_cast<float>(D(wanted.y) - velocity.y),
                      static_cast<float>(D(wanted.z) - velocity.z)};
    Vector3 accel;
    ScaleVector(&accel, change, panResponse_);
    bool snap = true;
    if (LengthRaw(&accel) >= 0.001) {
        float before = static_cast<float>(Dot(velocity, velocity));
        Vector3 step;
        ScaleVector(&step, accel, dt);
        velocity.AddInPlace(step);
        float after = static_cast<float>(Dot(velocity, velocity));
        float goal = static_cast<float>(Dot(wanted, wanted));
        EI_NAN_AUDIT(before, after, goal);
        snap = (before < goal && after > goal) || (before > goal && after < goal);
    }
    if (snap) std::memcpy(&velocity, &wanted, sizeof(Vector3));

    if (LengthSquaredUnrounded<Sum3::XYZ, double>(velocity) > 0.0) {
        const CameraLimits& limits = (flags_ & kAlternate) ? settings_->alternate : settings_->normal;
        double scale = (D(distance_) - limits.min) / ((D(limits.max) - limits.min) * kPanZoomSpan) + kTrackGain;
        double moveX = D(dt) * velocity.x;
        float moveY = static_cast<float>(D(dt) * velocity.y);
        float stepX = static_cast<float>(moveX * scale);
        float stepY = static_cast<float>(D(moveY) * scale);
        float width = camera::TheTerrain()->width;
        focus_.x = ClampToMap(static_cast<float>(D(stepX) + focus_.x), width);
        float height = camera::TheTerrain()->height;
        float y = ClampToMap(static_cast<float>(D(stepY) + focus_.y), height);
        targetId_ = -1;
        flags_ &= ~kFollowing;
        focus_.y = y;
        if (flags_ & kBounded) {
            double bx = D(focus_.x) - boundsCenter_.x;
            double by = D(focus_.y) - boundsCenter_.y;
            double d2 = LengthSquared2DUnrounded(by, bx);
            double radius = boundsRadius_;
            if (d2 > radius * radius) {
                double inv = 1.0 / std::sqrt(d2);
                float ux = static_cast<float>(inv * bx);
                double uy = inv * by;
                float px = static_cast<float>(radius * ux);
                float ny = static_cast<float>(radius * uy + boundsCenter_.y);
                focus_.x = static_cast<float>(D(px) + boundsCenter_.x);
                focus_.y = ny;
            }
        }
    }

    // The focus height follows the target's or the ground's, a tenth of the way per frame.
    float average, low, high;
    SampleTerrain(&focus_, &camera::kZeroVector, &camera::kZeroVector, static_cast<float>(D(camera::CellSize()) * 0.5f), camera::CellSize(), camera::CellSize(),
                  &average, &low, &high);
    double ground = target ? D(static_cast<const CObject3D*>(target)->position.z) : D(average);
    focus_.z = static_cast<float>(((ground + focusHeight_) - focus_.z) * kHeightLag + focus_.z);

    // Tilt (pitch in degrees, clamped to the settings' range).
    Approach(&tiltRate_, &tiltTarget_, tiltResponse_, dt);
    EI_NAN_AUDIT(tiltRate_);
    if (tiltRate_ != 0.0f) {
        double pitch = D(dt) * tiltRate_ + pitch_;
        pitch_ = static_cast<float>(pitch);
        bool alternate = (flags_ & kAlternate) != 0;
        float most = static_cast<float>(D(alternate ? settings_->altPitchMax : settings_->pitchMax) * CAM_DEGREE);
        if (pitch > most) pitch_ = most;
        double least = D(alternate ? settings_->altPitchMin : settings_->pitchMin) * CAM_DEGREE;
        if (least > pitch_) pitch_ = static_cast<float>(least);
    }

    // Turn about Z.
    Approach(&turnRate_, &turnTarget_, turnResponse_, dt);
    EI_NAN_AUDIT(turnRate_);
    if (turnRate_ != 0.0f) {
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Quaternion q;
        q.SetAxisAngle(up, static_cast<float>(D(dt) * turnRate_));
        const Quaternion r = viewRotation_;
        float x = static_cast<float>(((D(r.z) * q.y - D(r.y) * q.z) + D(q.w) * r.x) + D(r.w) * q.x);
        float y = static_cast<float>(((D(r.x) * q.z - D(r.z) * q.x) + D(q.w) * r.y) + D(r.w) * q.y);
        float z = static_cast<float>(((D(r.y) * q.x - D(q.y) * r.x) + D(r.z) * q.w) + D(r.w) * q.z);
        float w = static_cast<float>(D(q.w) * r.w - DotUnrounded<Sum3::ZYX, double>(q.x, q.y, q.z, r.x, r.y, r.z));
        viewRotation_.z = z;
        viewRotation_.y = y;
        viewRotation_.w = w;
        viewRotation_.x = x;
    }

    // Zoom.
    float zoomTarget;
    std::memcpy(&zoomTarget, &zoomTarget_, sizeof(float));
    Approach(&zoomRate_, &zoomTarget, zoomResponse_, dt);
    EI_NAN_AUDIT(zoomRate_);
    if (zoomRate_ != 0.0f) distance_ = static_cast<float>(D(dt) * zoomRate_ + distance_);

    const CameraLimits& limits = (flags_ & kAlternate) ? settings_->alternate : settings_->normal;
    EI_NAN_AUDIT(distance_, limits.hardMin, limits.min);
    if (distance_ < limits.hardMin) {
        CopyFloat(&distance_, &limits.hardMin);
    } else if (distance_ < limits.min) {
        distance_ = static_cast<float>((D(limits.min) - distance_) * dt * kEase / (D(limits.min) - limits.hardMin) + distance_);
    } else if (distance_ > limits.hardMax) {
        CopyFloat(&distance_, &limits.hardMax);
    } else if (distance_ > limits.max) {
        distance_ =
            static_cast<float>(D(distance_) - (D(distance_) - limits.max) * dt * kEase / (D(limits.hardMax) - limits.max));
    }

    ApplyView();

    // Shakes: damped oscillations added to the height.
    if (shakes_.Empty()) return;
    float offset = 0.0f;
    for (ShakeList::Node* node = shakes_.First(); node != shakes_.End();) {
        CameraShake* s = &node->data;
        double damping = x87::fexp(-(D(s->decay) * s->elapsed));
        double wave = x87::fcos(D(camera::kHalfPi) * s->elapsed * kShakeFrequency);
        offset = static_cast<float>(wave * damping * s->amplitude + offset);
        double elapsed = D(dt) + s->elapsed;
        s->elapsed = static_cast<float>(elapsed);
        if (elapsed >= s->duration)
            node = ShakeList::Erase(node);
        else
            node = node->next;   // also NaN
    }
    localPosition_.z = static_cast<float>(D(offset) + localPosition_.z);
}
EI_REPLACE_METHOD(0x0040FB30, CCamera::Update);

void CCamera::ZoomBy(float steps) {
    double d = CIpow(CAM_ZOOM_STEP, steps) * distance_;
    distance_ = static_cast<float>(d);
    if (!settings_) return;
    const CameraLimits& limits = (flags_ & kAlternate) ? settings_->alternate : settings_->normal;
    EI_NAN_AUDIT(d, limits.min);
    if (d < limits.min) {
        CopyFloat(&distance_, &limits.min);
        return;
    }
    if (d > limits.max) CopyFloat(&distance_, &limits.max);
}

bool CCamera::SaveView(unsigned slot) {
    if (slot >= 8) return false;
    CameraView& v = views_[slot];
    CopyFloat(&v.distance, &distance_);
    CopyFloat(&v.pitch, &pitch_);
    std::memcpy(&v.rotation, &viewRotation_, sizeof(Quaternion));
    ViewsToSettings(settings_);
    return true;
}

bool CCamera::RestoreView(unsigned slot) {
    if (slot >= 8) return false;
    CopyFloat(&distance_, &views_[slot].distance);
    CopyFloat(&pitch_, &views_[slot].pitch);
    ResetMotion(this);
    return true;
}

void CCamera::SetDistancePitch(const CameraView* view) {
    CopyFloat(&distance_, &view->distance);
    CopyFloat(&pitch_, &view->pitch);
    ResetMotion(this);
}

void CCamera::ViewsToSettings(CameraSettings* settings) {
    for (int i = 0; i < 4; ++i) {
        const CameraView& v = views_[i];
        CameraViewRecord& r = settings->views[i];
        CopyFloat(&r.distance, &v.distance);
        CopyFloat(&r.pitch, &v.pitch);
        const Quaternion& q = v.rotation;
        double sum = LengthSquaredUnrounded<Sum3::ZXY, double>(q.x, q.y, q.z);
        float sumF = static_cast<float>(sum);
        if (sum > kAxisTiny) {
            double angle = gamecrt::acos(q.w);
            angle = angle + angle;
            double inv = 1.0 / std::sqrt(sumF);
            float invF = static_cast<float>(inv);
            r.axis.x = static_cast<float>(inv * q.x);
            r.axis.y = static_cast<float>(D(invF) * q.y);
            r.axis.z = static_cast<float>(D(invF) * q.z);
            r.angle = static_cast<float>(angle);
        } else {
            r.axis.x = 1.0f;
            r.axis.y = 0.0f;
            r.axis.z = 0.0f;
            r.angle = 0.0f;
        }
    }
}

void CCamera::ViewsFromSettings(const CameraSettings* settings) {
    for (int i = 0; i < 4; ++i) {
        CameraView& v = views_[i];
        const CameraViewRecord& r = settings->views[i];
        CopyFloat(&v.distance, &r.distance);
        CopyFloat(&v.pitch, &r.pitch);
        Vector3 axis = r.axis;
        v.rotation.FromAxisAngle(axis, r.angle);
    }
}

void CCamera::SetHeading(float angle) {
    Vector3 up = {0.0f, 0.0f, 1.0f};
    viewRotation_.FromAxisAngle(up, angle);
    ApplyView();
}

void CCamera::SetFree(bool on) {
    if (on)
        flags_ |= kFree;
    else
        flags_ &= ~kFree;
}

void CCamera::LookFrom(const Vector3* from, const Vector3* to, int) {
    flags_ |= kFree;
    std::memcpy(&localPosition_, from, sizeof(Vector3));
    float dx = static_cast<float>(D(to->x) - from->x);
    float dy = static_cast<float>(D(to->y) - from->y);
    float dz = static_cast<float>(D(to->z) - from->z);
    LookDirection(dx, dy, dz);
}

void CCamera::SetBounds(const Vector3* center, float radius) {
    std::memcpy(&boundsCenter_, center, sizeof(Vector3));
    CopyFloat(&boundsRadius_, &radius);
    flags_ |= kBounded;
}

void CCamera::Shake(const Vector3* source, float duration, float amplitude, float decay) {
    const Vector3& at = (flags_ & kPlaced) ? localPosition_ : focus_;
    double distance = libcrt::Hypot(D(at.x) - source->x, D(at.y) - source->y);
    float distanceF = static_cast<float>(distance);
    if (distance >= kShakeRange) return;
    double strength = amplitude;
    if (distanceF > kShakeFull)
        strength = (1.0 - (D(distanceF) - kShakeFull) / (D(kShakeRange) - kShakeFull)) * amplitude;
    CameraShake entry;
    entry.elapsed = 0.0f;
    entry.duration = static_cast<float>(D(duration) * kShakeTicks);
    entry.amplitude = static_cast<float>(strength);
    CopyFloat(&entry.decay, &decay);
    ShakeList::Insert(shakes_.End(), entry);
}
