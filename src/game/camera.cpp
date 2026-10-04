// The camera (CCamera, core_transform.h): the per-frame update and view placement, focus and
// terrain sampling, panning / zoom / saved views, shakes and recorded camera paths
// (0040D9B0-004114F0). Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, StoreFloat where it stores to a float. Compiled with precise semantics so that the
// operation order is kept (/fp:fast may reassociate) and NaN compares stay as written.
#include "core_transform.h"

#include "base_manager.h"
#include "core_maths.h"
#include "terrain.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// Game globals.
Terrain* Map() { return *reinterpret_cast<Terrain* const*>(0x007AF738); }            // width / height
void* SurfaceOwner() { return reinterpret_cast<void*>(0x007AF5B8); }                  // TerrainOwner
void* SoundSystem() { return *reinterpret_cast<void* const*>(0x007AF784); }
CBaseManager* Objects() { return *reinterpret_cast<CBaseManager* const*>(0x007AF5F8); }
float CellSize() { return *reinterpret_cast<const float*>(0x0079BDB4); }
float GameTime() { return *reinterpret_cast<const float*>(0x0079B2A4); }
const Vector3* ZeroVector() { return reinterpret_cast<const Vector3*>(0x0079B5C8); }

// Constants the original multiplies at run time (read from the exe so the compiler cannot fold
// them: the products round at the FPU's precision).
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
#define EI_DEGREE (GameDouble(0x0073B9A0) * GameDouble(0x0073BA30))   // pi * (1 / 180)
#define EI_RIGHT_ANGLE (EI_DEGREE * GameDouble(0x0073BAB8))          // * 90

constexpr float kTrackGain = 0.2f;        // 0073BAD4
constexpr float kPanZoomSpan = 1.125f;    // 0073BAD0
constexpr float kHeightLag = 0.1f;        // 0073B850
constexpr float kEase = 1.1f;             // 0073BAC4
constexpr float kShakeFrequency = 0.26666668f;  // 0073BAC0
constexpr float kShakeRange = 20.0f;      // 0073B9D4
constexpr float kShakeFull = 8.0f;        // 0073B9D0
constexpr float kShakeTicks = 30.0f;      // 0073BAF0
constexpr float kAxisTiny = 1e-8f;        // 0073BAE0

float FloatAt(const void* object, std::size_t offset) {
    float f;
    std::memcpy(&f, static_cast<const char*>(object) + offset, sizeof(f));
    return f;
}

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

bool IsNaN(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return (bits & 0x7FFFFFFF) > 0x7F800000;
}

// FCOMP 0; FNSTSW; TEST AH,0x40 not taken: neither zero nor NaN.
bool NonZero(double v) { return v < 0.0 || v > 0.0; }

void CopyFloat(float* dst, const float* src) { std::memcpy(dst, src, sizeof(float)); }

// The original's branch-free clamp of a map coordinate to [0, last - 1]: NaN becomes 0, and a NaN
// limit wins.
float ClampToMap(float v, float last) {
    float low = !(v >= 0.0f) ? 0.0f : v;
    float limit = Store(last - 1.0f);
    return !(limit >= low) ? limit : low;
}

// Not rewritten.
__declspec(naked) double __fastcall SurfaceHeightRaw(void* /*owner*/, void*, float, float) EI_ORIGINAL(0x00564070)
__declspec(naked) double __fastcall LengthRaw(const Vector3*) EI_ORIGINAL(0x00401E20)
__declspec(naked) Quaternion* __fastcall QuatFromAngleAxis(Quaternion*, void*, float /*angle*/, const Vector3*)
    EI_ORIGINAL(0x0049E790)
__declspec(naked) Quaternion* __fastcall QuatFromAxisAngle(Quaternion*, void*, const Vector3*, float /*angle*/)
    EI_ORIGINAL(0x0049E800)
__declspec(naked) void __fastcall QuatSlerp(Quaternion*, void*, float, const Quaternion*, const Quaternion*)
    EI_ORIGINAL(0x0049ED20)
__declspec(naked) void __fastcall SetListener(void*, void*, const Vector3*, const Vector3*) EI_ORIGINAL(0x006B9750)
__declspec(naked) double __cdecl Hypot(double, double) EI_ORIGINAL(0x006FD026)   // _hypot
// The shake list's STLport instantiations (`list` is the address of the field holding the sentinel).
__declspec(naked) void __fastcall ShakeEnd(CameraShake* const*, void*, CameraShake** /*ret*/) EI_ORIGINAL(0x00411530)
__declspec(naked) void __fastcall ShakeInsert(CameraShake* const*, void*, CameraShake** /*ret*/, CameraShake* /*pos*/,
                                              const float* /*value*/) EI_ORIGINAL(0x00411630)
__declspec(naked) void __fastcall ShakeErase(CameraShake* const*, void*, CameraShake** /*ret*/, CameraShake* /*pos*/)
    EI_ORIGINAL(0x00411540)

// _CIacos / _CIpow: the CRT's x87 intrinsics (argument in ST0, or base in ST1 and exponent in ST0).
__declspec(naked) double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}
__declspec(naked) double __cdecl CIpow(double, double) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm mov eax, 0x006E81A0
    __asm call eax
    __asm ret
}
__declspec(naked) double __cdecl Fpatan(double /*y*/, double /*x*/) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm fpatan
    __asm ret
}
// e^x the way the original inlines it: 2^(x log2 e) through FRNDINT / F2XM1 / FSCALE.
__declspec(naked) double __cdecl Exp(double) {
    __asm {
        fld qword ptr [esp + 4]
        fldl2e
        fmulp st(1), st
        fld st(0)
        frndint
        fxch st(1)
        fsub st, st(1)
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        ret
    }
}

// Moves `rate` towards `target` by (target - rate) * response * dt; snaps to the target when the
// step is tiny or the rate's magnitude passes the target's.
void Approach(float* rate, const float* target, float response, float dt) {
    float step = Store((D(*target) - *rate) * response);
    if (!(D(AbsBits(step)) >= 0.001)) {
        CopyFloat(rate, target);
        return;
    }
    double before = D(*rate) * *rate;
    double after = D(step) * dt + *rate;
    *rate = Store(after);
    float after2 = Store(after * after);
    float target2 = Store(D(*target) * *target);
    if ((!(before >= target2) && after2 > target2) || (before > target2 && !(after2 >= target2)))
        CopyFloat(rate, target);
}

void ResetMotion(CCamera* c) {
    std::memcpy(&c->offset98_, ZeroVector(), sizeof(Vector3));
    std::memcpy(&c->offsetA4_, ZeroVector(), sizeof(Vector3));
    const float zero = 0.0f;
    CopyFloat(&c->turnRate_, &zero);
    CopyFloat(&c->turnTarget_, &zero);
    CopyFloat(&c->tiltRate_, &zero);
    CopyFloat(&c->tiltTarget_, &zero);
    CopyFloat(&c->zoomRate_, &zero);
    c->unknownD0_ = 0;
}

} // namespace


void __stdcall SampleTerrain(const Vector3* p, const Vector3*, const Vector3*, float step, float halfX, float halfY,
                             float* average, float* minimum, float* maximum) {
    double centre = SurfaceHeightRaw(SurfaceOwner(), nullptr, p->x, p->y);
    *minimum = Store(centre);
    *maximum = Store(centre);
    *average = Store(centre);
    int count = 1;
    float x = Store(D(p->x) - halfX);
    while (x <= D(halfX) + p->x) {
        if (x >= 0.0f && Map()->width > x) {
            float y = Store(D(p->y) - halfY);
            while (y <= D(halfY) + p->y) {
                if (y >= 0.0f && Map()->height > y) {
                    float h = Store(SurfaceHeightRaw(SurfaceOwner(), nullptr, x, y));
                    float low = *minimum;
                    *minimum = !(h >= low) ? h : low;
                    float high = *maximum;
                    *maximum = !(high >= h) ? h : high;
                    ++count;
                    *average = Store(D(h) + *average);
                }
                y = Store(D(y) + step);
            }
        }
        x = Store(D(x) + step);
    }
    *average = Store(D(*average) / static_cast<double>(count));
}
EI_REPLACE(0x0040D9B0, SampleTerrain);

Vector3* CubicCurve3::Evaluate(Vector3* out, float t) const {
    double z = ((D(t) * c[11] + c[10]) * t + c[9]) * t + c[8];
    double y = ((D(t) * c[7] + c[6]) * t + c[5]) * t + c[4];
    double x = ((D(t) * c[3] + c[2]) * t + c[1]) * t + c[0];
    out->x = Store(x);
    out->y = Store(y);
    out->z = Store(z);
    return out;
}
EI_REPLACE_METHOD(0x0040E460, CubicCurve3::Evaluate);

int CCamera::PlaybackStart() {
    if (keyCount_ == 0) return 0;
    std::memcpy(&localPosition_, &keys_[0].position, sizeof(Vector3));
    std::memcpy(&localRotation_, &keys_[0].rotation, sizeof(Quaternion));
    return 1;
}
EI_REPLACE_METHOD(0x0040F1B0, CCamera::PlaybackStart);

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
    float start = Store(D(a.time) + timeOffset_);
    double span = next - start;
    float u = 0.0f;
    if (NonZero(span)) u = Store((D(time) - start) / span);
    double dx = D(b.position.x) - a.position.x;
    float dy = Store(D(b.position.y) - a.position.y);
    float dz = Store(D(b.position.z) - a.position.z);
    float sy = Store(D(dy) * u);
    float sz = Store(D(dz) * u);
    localPosition_.x = Store(dx * u + a.position.x);
    localPosition_.y = Store(D(sy) + a.position.y);
    localPosition_.z = Store(D(sz) + a.position.z);
    QuatSlerp(&localRotation_, nullptr, u, &a.rotation, &b.rotation);
    return 1;
}
EI_REPLACE_METHOD(0x0040EFB0, CCamera::PlaybackStep);

void CCamera::Pan(const float* direction) {
    Vector3 right;
    localRotation_.AxisX(&right);
    const Quaternion& r = localRotation_;
    Vector3 up;
    up.z = Store((D(r.w) * r.x + D(r.y) * r.z) * 2.0);
    up.y = Store((D(r.w) * r.w - ((D(r.x) * r.x + D(r.z) * r.z) + D(r.y) * r.y)) + D(r.y) * r.y * 2.0);
    up.x = Store((D(r.y) * r.x - D(r.w) * r.z) * 2.0);
    double k1 = (1.0 / x87::fsqrt(D(right.y) * right.y + D(right.x) * right.x)) * direction[0];
    right.x = Store(k1 * right.x);
    double rightY = k1 * right.y;
    double k2 = (1.0 / x87::fsqrt(D(up.y) * up.y + D(up.x) * up.x)) * direction[1];
    up.x = Store(k2 * up.x);
    double upY = k2 * up.y;
    offsetA4_.x = Store(D(up.x) + right.x);
    offsetA4_.y = Store(upY + rightY);
    offsetA4_.z = 0.0f;
}
EI_REPLACE_METHOD(0x0040F230, CCamera::Pan);

void CCamera::StopMotion() {
    Vector3 right, up;
    localRotation_.AxisX(&right);
    localRotation_.AxisY(&up);
    // The original multiplies by 0 (keeping NaN and -0), so the zero is read from memory.
    const float zero = GameFloat(0x0073B858);
    right.x = Store(D(zero) * right.x);
    right.y = Store(D(right.y) * zero);
    right.z = 0.0f;
    up.x = Store(D(zero) * up.x);
    up.y = Store(D(up.y) * zero);
    up.z = 0.0f;
    Vector3 sum;
    AddVectors(&sum, right, up);
    std::memcpy(&offsetA4_, &sum, sizeof(Vector3));
    // SetTiltRate(0) / SetTurnRate(0) inlined: the rate is replaced only when it is NaN.
    tiltTarget_ = 0.0f;
    if (IsNaN(tiltRate_)) CopyFloat(&tiltRate_, &tiltTarget_);
    turnTarget_ = 0.0f;
    if (IsNaN(turnRate_)) CopyFloat(&turnRate_, &turnTarget_);
    unknownD0_ = 0;
}
EI_REPLACE_METHOD(0x0040F430, CCamera::StopMotion);

void CCamera::SetFocus(const Vector3* p) {
    std::memcpy(&focus_, p, sizeof(Vector3));
    float average, low, high;
    SampleTerrain(p, ZeroVector(), ZeroVector(), Store(D(CellSize()) * 0.5f), CellSize(), CellSize(), &average,
                  &low, &high);
    focus_.z = Store(D(average) + focusHeight_);
}
EI_REPLACE_METHOD(0x0040F5C0, CCamera::SetFocus);

void CCamera::ApplyView() {
    std::memcpy(&localRotation_, &viewRotation_, sizeof(Quaternion));
    Vector3 right;
    localRotation_.AxisX(&right);
    Quaternion tilt;
    QuatFromAngleAxis(&tilt, nullptr, Store(-(EI_RIGHT_ANGLE + pitch_)), &right);
    localRotation_.PreMultiply(tilt);
    Vector3 forward;
    localRotation_.AxisZ(&forward);
    const float d = distance_;
    float ox = Store(D(forward.x) * d);
    float oy = Store(D(forward.y) * d);
    float px = Store(D(focus_.x) - ox);
    float py = Store(D(focus_.y) - oy);
    float pz = Store(D(focus_.z) - D(d) * forward.z);
    localPosition_.x = px;
    localPosition_.y = py;
    localPosition_.z = pz;
    float before;
    CopyFloat(&before, &localPosition_.z);
    ClampAboveObjects();
    if (before >= localPosition_.z) return;
    // Lifted over an object: aim back down at the focus.
    double dz = D(localPosition_.z) - focus_.z;
    double across = Hypot(D(localPosition_.x) - focus_.x, D(localPosition_.y) - focus_.y);
    double angle = Fpatan(dz, across);
    std::memcpy(&localRotation_, &viewRotation_, sizeof(Quaternion));
    const Quaternion s = localRotation_;
    Vector3 axis;
    axis.x = Store((D(s.w) * s.w - ((D(s.z) * s.z + D(s.y) * s.y) + D(s.x) * s.x)) + D(s.x) * s.x * 2.0);
    axis.y = Store((D(s.x) * s.y + D(s.z) * s.w) * 2.0);
    axis.z = Store((D(s.x) * s.z - D(s.y) * s.w) * 2.0);
    Quaternion q;
    QuatFromAngleAxis(&q, nullptr, Store(-(EI_RIGHT_ANGLE + angle)), &axis);
    float y = Store(((D(q.z) * s.x - D(s.z) * q.x) + D(s.w) * q.y) + D(s.y) * q.w);
    float z = Store(((D(q.x) * s.y - D(s.x) * q.y) + D(s.w) * q.z) + D(s.z) * q.w);
    float w = Store(D(s.w) * q.w - ((D(s.x) * q.x + D(q.z) * s.z) + D(q.y) * s.y));
    float x = Store(((D(s.z) * q.y - D(q.z) * s.y) + D(s.w) * q.x) + D(s.x) * q.w);
    localRotation_.y = y;
    localRotation_.z = z;
    localRotation_.w = w;
    localRotation_.x = x;
}
EI_REPLACE_METHOD(0x0040F8B0, CCamera::ApplyView);

void CCamera::Update(float dt) {
    Vector3 listener;
    std::memcpy(&listener, &localPosition_, sizeof(Vector3));
    if (!(flags_ & 0x60)) {
        std::memcpy(&listener, &focus_, sizeof(Vector3));
        if (Map()) listener.z = Store(SurfaceHeightRaw(SurfaceOwner(), nullptr, focus_.x, focus_.y) + focusHeight_);
    }
    Vector3 forward;
    localRotation_.AxisZ(&forward);
    SetListener(SoundSystem(), nullptr, &listener, &forward);
    if (flags_ & 0x60) return;

    CEngineObject* target = Objects()->FindObject(static_cast<unsigned>(targetId_));
    if (flags_ & kFollowing) {
        if (target) {
            // Behind the target, looking along its heading.
            const CTransformObject* t = static_cast<const CTransformObject*>(target);
            double lift = D(FloatAt(target, 0x14C)) + FloatAt(target, 0x130);
            std::memcpy(&localPosition_.x, &t->localPosition_.x, sizeof(float));
            std::memcpy(&localPosition_.y, &t->localPosition_.y, sizeof(float));
            localPosition_.z = Store(lift + t->localPosition_.z);
            Vector3 side;
            t->localRotation_.AxisY(&side);
            double nx = -D(side.x);
            Vector3 dir;
            dir.y = -side.y;
            dir.z = 0.0f;
            float inv = Store(1.0 / x87::fsqrt(D(dir.y) * dir.y + nx * nx));
            dir.x = Store(nx * inv);
            dir.y = Store(D(dir.y) * inv);
            TurnTowards(dir);
            AlignToTarget();
            return;
        }
    } else if (target) {
        // Drift the focus a fifth of the way to the tracked object.
        const CTransformObject* t = static_cast<const CTransformObject*>(target);
        double dx = D(t->localPosition_.x) - focus_.x;
        float dy = Store(D(t->localPosition_.y) - focus_.y);
        float width = Map()->width;
        float dyStep = Store(D(dy) * kTrackGain);
        focus_.x = ClampToMap(Store(dx * kTrackGain + focus_.x), width);
        float height = Map()->height;
        focus_.y = ClampToMap(Store(D(dyStep) + focus_.y), height);
    }

    // Pan: the velocity eases towards its target.
    Vector3& velocity = offset98_;
    const Vector3& wanted = offsetA4_;
    Vector3 change = {Store(D(wanted.x) - velocity.x), Store(D(wanted.y) - velocity.y),
                      Store(D(wanted.z) - velocity.z)};
    Vector3 accel;
    ScaleVector(&accel, change, panResponse_);
    bool snap = true;
    if (LengthRaw(&accel) >= 0.001) {
        float before = Store(Dot(velocity, velocity));
        Vector3 step;
        ScaleVector(&step, accel, dt);
        velocity.AddInPlace(step);
        float after = Store(Dot(velocity, velocity));
        float goal = Store(Dot(wanted, wanted));
        snap = (!(before >= goal) && after > goal) || (before > goal && !(after >= goal));
    }
    if (snap) std::memcpy(&velocity, &wanted, sizeof(Vector3));

    if ((D(velocity.x) * velocity.x + D(velocity.y) * velocity.y) + D(velocity.z) * velocity.z > 0.0) {
        const CameraLimits& limits = (flags_ & kAlternate) ? settings_->alternate : settings_->normal;
        double scale = (D(distance_) - limits.min) / ((D(limits.max) - limits.min) * kPanZoomSpan) + kTrackGain;
        double moveX = D(dt) * velocity.x;
        float moveY = Store(D(dt) * velocity.y);
        float stepX = Store(moveX * scale);
        float stepY = Store(D(moveY) * scale);
        float width = Map()->width;
        focus_.x = ClampToMap(Store(D(stepX) + focus_.x), width);
        float height = Map()->height;
        float y = ClampToMap(Store(D(stepY) + focus_.y), height);
        targetId_ = -1;
        flags_ &= ~kFollowing;
        focus_.y = y;
        if (flags_ & kBounded) {
            double bx = D(focus_.x) - boundsCenter_.x;
            double by = D(focus_.y) - boundsCenter_.y;
            double d2 = by * by + bx * bx;
            double radius = boundsRadius_;
            if (d2 > radius * radius) {
                double inv = 1.0 / x87::fsqrt(d2);
                float ux = Store(inv * bx);
                double uy = inv * by;
                float px = Store(radius * ux);
                float ny = Store(radius * uy + boundsCenter_.y);
                focus_.x = Store(D(px) + boundsCenter_.x);
                focus_.y = ny;
            }
        }
    }

    // The focus height follows the target's or the ground's, a tenth of the way per frame.
    float average, low, high;
    SampleTerrain(&focus_, ZeroVector(), ZeroVector(), Store(D(CellSize()) * 0.5f), CellSize(), CellSize(),
                  &average, &low, &high);
    double ground = target ? D(FloatAt(target, 0x24)) : D(average);
    focus_.z = Store(((ground + focusHeight_) - focus_.z) * kHeightLag + focus_.z);

    // Tilt (pitch in degrees, clamped to the settings' range).
    Approach(&tiltRate_, &tiltTarget_, tiltResponse_, dt);
    if (NonZero(tiltRate_)) {
        double pitch = D(dt) * tiltRate_ + pitch_;
        pitch_ = Store(pitch);
        bool alternate = (flags_ & kAlternate) != 0;
        float most = Store(D(alternate ? settings_->altPitchMax : settings_->pitchMax) * EI_DEGREE);
        if (pitch > most) pitch_ = most;
        double least = D(alternate ? settings_->altPitchMin : settings_->pitchMin) * EI_DEGREE;
        if (least > pitch_) pitch_ = Store(least);
    }

    // Turn about Z.
    Approach(&turnRate_, &turnTarget_, turnResponse_, dt);
    if (NonZero(turnRate_)) {
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Quaternion q;
        QuatFromAxisAngle(&q, nullptr, &up, Store(D(dt) * turnRate_));
        const Quaternion r = viewRotation_;
        float x = Store(((D(r.z) * q.y - D(r.y) * q.z) + D(q.w) * r.x) + D(r.w) * q.x);
        float y = Store(((D(r.x) * q.z - D(r.z) * q.x) + D(q.w) * r.y) + D(r.w) * q.y);
        float z = Store(((D(r.y) * q.x - D(q.y) * r.x) + D(r.z) * q.w) + D(r.w) * q.z);
        float w = Store(D(q.w) * r.w - ((D(q.y) * r.y + D(r.z) * q.z) + D(r.x) * q.x));
        viewRotation_.z = z;
        viewRotation_.y = y;
        viewRotation_.w = w;
        viewRotation_.x = x;
    }

    // Zoom.
    float zoomTarget;
    std::memcpy(&zoomTarget, &unknownD0_, sizeof(float));
    Approach(&zoomRate_, &zoomTarget, zoomResponse_, dt);
    if (NonZero(zoomRate_)) distance_ = Store(D(dt) * zoomRate_ + distance_);

    const CameraLimits& limits = (flags_ & kAlternate) ? settings_->alternate : settings_->normal;
    if (!(distance_ >= limits.hardMin)) {
        CopyFloat(&distance_, &limits.hardMin);
    } else if (!(distance_ >= limits.min)) {
        distance_ = Store((D(limits.min) - distance_) * dt * kEase / (D(limits.min) - limits.hardMin) + distance_);
    } else if (distance_ > limits.hardMax) {
        CopyFloat(&distance_, &limits.hardMax);
    } else if (distance_ > limits.max) {
        distance_ =
            Store(D(distance_) - (D(distance_) - limits.max) * dt * kEase / (D(limits.hardMax) - limits.max));
    }

    ApplyView();

    // Shakes: damped oscillations added to the height.
    CameraShake* const* list = &shakes_;
    if (shakes_->next == shakes_) return;
    float offset = 0.0f;
    for (CameraShake* s = shakes_->next; s != shakes_;) {
        double damping = Exp(-(D(s->decay) * s->elapsed));
        double wave = x87::fcos(D(GameTime()) * s->elapsed * kShakeFrequency);
        offset = Store(wave * damping * s->amplitude + offset);
        double elapsed = D(dt) + s->elapsed;
        s->elapsed = Store(elapsed);
        if (!(elapsed >= s->duration)) {
            s = s->next;
        } else {
            CameraShake* next;
            ShakeErase(list, nullptr, &next, s);
            s = next;
        }
    }
    localPosition_.z = Store(D(offset) + localPosition_.z);
}
EI_REPLACE_METHOD(0x0040FB30, CCamera::Update);

void CCamera::ZoomBy(float steps) {
    double d = CIpow(GameDouble(0x0073BAD8), steps) * distance_;
    distance_ = Store(d);
    if (!settings_) return;
    const CameraLimits& limits = (flags_ & kAlternate) ? settings_->alternate : settings_->normal;
    if (!(d >= limits.min)) {
        CopyFloat(&distance_, &limits.min);
        return;
    }
    if (d > limits.max) CopyFloat(&distance_, &limits.max);
}
EI_REPLACE_METHOD(0x004107A0, CCamera::ZoomBy);

bool CCamera::SaveView(unsigned slot) {
    if (slot >= 8) return false;
    CameraView& v = views_[slot];
    CopyFloat(&v.distance, &distance_);
    CopyFloat(&v.pitch, &pitch_);
    std::memcpy(&v.rotation, &viewRotation_, sizeof(Quaternion));
    ViewsToSettings(settings_);
    return true;
}
EI_REPLACE_METHOD(0x00410860, CCamera::SaveView);

bool CCamera::RestoreView(unsigned slot) {
    if (slot >= 8) return false;
    CopyFloat(&distance_, &views_[slot].distance);
    CopyFloat(&pitch_, &views_[slot].pitch);
    ResetMotion(this);
    return true;
}
EI_REPLACE_METHOD(0x004108D0, CCamera::RestoreView);

void CCamera::SetDistancePitch(const CameraView* view) {
    CopyFloat(&distance_, &view->distance);
    CopyFloat(&pitch_, &view->pitch);
    ResetMotion(this);
}
EI_REPLACE_METHOD(0x00411470, CCamera::SetDistancePitch);

void CCamera::ViewsToSettings(CameraSettings* settings) {
    for (int i = 0; i < 4; ++i) {
        const CameraView& v = views_[i];
        CameraViewRecord& r = settings->views[i];
        CopyFloat(&r.distance, &v.distance);
        CopyFloat(&r.pitch, &v.pitch);
        const Quaternion& q = v.rotation;
        double sum = (D(q.z) * q.z + D(q.x) * q.x) + D(q.y) * q.y;
        float sumF = Store(sum);
        if (sum > kAxisTiny) {
            double angle = CIacos(q.w);
            angle = angle + angle;
            double inv = 1.0 / x87::fsqrt(sumF);
            float invF = Store(inv);
            r.axis.x = Store(inv * q.x);
            r.axis.y = Store(D(invF) * q.y);
            r.axis.z = Store(D(invF) * q.z);
            r.angle = Store(angle);
        } else {
            r.axis.x = 1.0f;
            r.axis.y = 0.0f;
            r.axis.z = 0.0f;
            r.angle = 0.0f;
        }
    }
}
EI_REPLACE_METHOD(0x00410D70, CCamera::ViewsToSettings);

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
EI_REPLACE_METHOD(0x00410D10, CCamera::ViewsFromSettings);

void CCamera::SetHeading(float angle) {
    Vector3 up = {0.0f, 0.0f, 1.0f};
    viewRotation_.FromAxisAngle(up, angle);
    ApplyView();
}
EI_REPLACE_METHOD(0x004109E0, CCamera::SetHeading);

void CCamera::SetFree(bool on) {
    if (on)
        flags_ |= kFree;
    else
        flags_ &= ~kFree;
}
EI_REPLACE_METHOD(0x00410E50, CCamera::SetFree);

void CCamera::LookFrom(const Vector3* from, const Vector3* to, int) {
    flags_ |= kFree;
    std::memcpy(&localPosition_, from, sizeof(Vector3));
    float dx = Store(D(to->x) - from->x);
    float dy = Store(D(to->y) - from->y);
    float dz = Store(D(to->z) - from->z);
    LookDirection(dx, dy, dz);
}
EI_REPLACE_METHOD(0x00411060, CCamera::LookFrom);

void CCamera::SetBounds(const Vector3* center, float radius) {
    std::memcpy(&boundsCenter_, center, sizeof(Vector3));
    CopyFloat(&boundsRadius_, &radius);
    flags_ |= kBounded;
}
EI_REPLACE_METHOD(0x00411360, CCamera::SetBounds);

void CCamera::Shake(const Vector3* source, float duration, float amplitude, float decay) {
    const Vector3& at = (flags_ & 0x60) ? localPosition_ : focus_;
    double distance = Hypot(D(at.x) - source->x, D(at.y) - source->y);
    float distanceF = Store(distance);
    if (distance >= kShakeRange) return;
    double strength = amplitude;
    if (distanceF > kShakeFull)
        strength = (1.0 - (D(distanceF) - kShakeFull) / (D(kShakeRange) - kShakeFull)) * amplitude;
    float entry[4];
    entry[0] = 0.0f;
    entry[1] = Store(D(duration) * kShakeTicks);
    entry[2] = Store(strength);
    CopyFloat(&entry[3], &decay);
    CameraShake* end;
    ShakeEnd(&shakes_, nullptr, &end);
    CameraShake* inserted;
    ShakeInsert(&shakes_, nullptr, &inserted, end, entry);
}
EI_REPLACE_METHOD(0x004113A0, CCamera::Shake);
