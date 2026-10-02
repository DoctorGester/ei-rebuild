#include "effects.h"

#include "core_maths.h"
#include "object_maths.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>

// The particle callbacks run for every live particle every frame. The order of operations in each
// expression follows the original instruction sequence; values the original rounds through a float
// temporary go through StoreFloat, values it keeps on the FPU stack are written inline.

namespace {

constexpr float kZero = 0.0f;
constexpr float kHalf = 0.5f;                                           // 0073B86C
constexpr float kOne = 1.0f;                                            // 0073B84C
constexpr double kHalfDouble = 0.5;                                     // 0073CC88
// The 2^-32 scale of the random range helpers (0073C468); its last bit is set (sic).
constexpr double kRandomScale = std::bit_cast<double>(0x3DF0000000100000ull);
constexpr float kPointThree = std::bit_cast<float>(0x3E99999Au);       // 0073C54C
constexpr float kCloudAlpha = 4.5f;                                     // 0073CDAC
constexpr float kSmokeGrowth = std::bit_cast<float>(0x3F87AE14u);      // 0073CE38 (1.06)
constexpr float kSmokeDrift = std::bit_cast<float>(0x3E19999Au);       // 0073CD18 (0.15)
constexpr float kDebrisDamping = std::bit_cast<float>(0x3F59999Au);    // 0073CB1C (0.85)
constexpr float kDebrisGravity = std::bit_cast<float>(0x3D6147AEu);    // 0073CB20 (0.055)
constexpr float kDebrisFadeIn = 4.0f;                                   // 0073CB24
constexpr float kDebrisGrowth = std::bit_cast<float>(0x3F90A3D7u);     // 0073CCF0 (1.13)
constexpr float kTendrilDensity = std::bit_cast<float>(0x41649249u);   // 0073CE3C (14.285714)
constexpr double kMushroomWidth = 0.2;                                  // 0073CCE0
constexpr float kMushroomRise = std::bit_cast<float>(0x3DF5C28Fu);     // 0073CCC0 (0.12)
constexpr double kOrbitMinRadius = 0.01;                                // 0073CE20
constexpr float kSphereScale = std::bit_cast<float>(0x3FCCCCCDu);      // 0073CC80 (1.6)

// Size curves of the orbiting particles, indexed by the ticks left (0073CB38, 0073CB78).
const float* SizeCurve() { return reinterpret_cast<const float*>(0x0073CB38); }
const float* AntimagicSizeCurve() { return reinterpret_cast<const float*>(0x0073CB78); }

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// x87 FCOM: "ST < operand or unordered" (C0), as `TEST AH,1` reads it.
bool LessOrUnordered(double a, double b) { return !(a >= b); }

// Reads a float back from memory, as the original does after storing it (the compiler would
// otherwise reuse the unrounded register value).
float Load(const float& f) { return *static_cast<const volatile float*>(&f); }

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// The game's _ftol (006E853C): truncation.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// FLDLN2; FILD n; FYL2X: ln(n), left in ST0.
__declspec(naked) double __cdecl LnOfInt(int) {
    __asm {
        fldln2
        fild dword ptr [esp + 4]
        fyl2x
        ret
    }
}

// 004113A0: the camera's shake around a point (the camera object is 0079B2A8).
// Arguments: camera, unused EDX, point, strength, range, duration.
__declspec(naked) void __fastcall CameraShake(void*, void*, const Vector3*, float, float, float) EI_ORIGINAL(0x004113A0)
void* TheCamera() { return reinterpret_cast<void*>(0x0079B2A8); }

constexpr int kTypeFire = 0x2003;
constexpr int kTypeSmoke = 0x2004;
constexpr int kTypePoisonCloud = 0x2008;
constexpr int kTypeMushroom = 0x200E;
constexpr int kTypeTendril = 0x2010;
constexpr int kTypeSphereElectricity = 0x2016;
constexpr int kTypeSphereFire = 0x2017;
constexpr int kTypeAntimagic = 0x201A;

} // namespace

__declspec(naked) void CParticleSource::Attach(void*) EI_ORIGINAL(0x004849F0)

// --- Free helpers -------------------------------------------------------------------------------

Vector3* __fastcall DivideVector(Vector3* out, const Vector3& v, float d) {
    float z = v.z / d;
    float y = v.y / d;
    float x = v.x / d;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}
EI_REPLACE(0x00488940, DivideVector);

float IsaacRandom::Range(float min, float max) {
    return static_cast<float>(Unsigned(Next()) * (max - min) * kRandomScale + min);
}
EI_REPLACE_METHOD(0x00488970, IsaacRandom::Range);

// --- Source setters -----------------------------------------------------------------------------

void CParticleSource::ScaleSize(float factor) {
    spreadMin.x = factor * spreadMin.x;
    spreadMin.y = factor * spreadMin.y;
    spreadMin.z = factor * spreadMin.z;
    spreadMax.x = factor * spreadMax.x;
    spreadMax.y = factor * spreadMax.y;
    spreadMax.z = factor * spreadMax.z;
    size = factor * size;
    baseSize = factor * baseSize;
    height = factor * height;
    emitSpacing = factor * emitSpacing;
}
EI_REPLACE_METHOD(0x00485480, CParticleSource::ScaleSize);

void CParticleSource::StopEmitting() { flags_ = (flags_ & ~1u) | 4u; }
EI_REPLACE_METHOD(0x00485440, CParticleSource::StopEmitting);

void CParticleSource::StartEmitting() { flags_ |= 5u; }
EI_REPLACE_METHOD(0x00485450, CParticleSource::StartEmitting);

void CParticleSource::Stop() {
    flags_ &= ~5u;
    Attach(nullptr);
}
EI_REPLACE_METHOD(0x00485360, CParticleSource::Stop);

// --- Particle updates ---------------------------------------------------------------------------

// Falling drops: move by the drift, scale the size, accelerate down to the terminal speed in
// velocity.y (drift.z = max(drift.z + velocity.z, velocity.y)); dropped once below z = 0.
int CParticleSource::UpdateFalling(Particle* p) {
    if (LessOrUnordered(p->pos.z, kZero)) return 0;
    float terminal;
    std::memcpy(&terminal, &p->velocity.y, sizeof(terminal));
    p->pos.x = p->drift.x + p->pos.x;
    p->pos.y = p->drift.y + p->pos.y;
    p->pos.z = p->drift.z + p->pos.z;
    p->size = p->velocity.x * p->size;
    float fall = StoreFloat(p->velocity.z + p->drift.z);
    p->drift.z = LessOrUnordered(fall, terminal) ? terminal : fall;
    return 1;
}
EI_REPLACE_METHOD(0x0048F670, CParticleSource::UpdateFalling);

// Clouds: cycle the texture frames; once the source stops emitting, raise the alpha byte (it wraps:
// the original's arithmetic) until it is at most 9, then drop the particle.
int CParticleSource::UpdateCloud(Particle* p) {
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) p->frame = 0;
        p->frameTimer = frameDelay;
    }
    if (flags_ & 1) return 1;
    std::uint32_t a = p->color >> 24;
    if (a <= 9) return 0;
    int step = x87::fistp(alpha * kCloudAlpha);
    if (step < 1) step = 1;
    std::uint32_t rgb = type_ == kTypePoisonCloud ? 0xFFAF7Fu : 0xFFFFFFu;
    p->color = ((static_cast<std::uint32_t>(step) * 255 + a) << 24) | rgb;
    return 1;
}
EI_REPLACE_METHOD(0x0048B570, CParticleSource::UpdateCloud);

// Smoke puffs: rise and spread for the first two frames, then grow; past frame 7 they drift with
// the wind and fire smoke dies (other smoke lives on, a quarter of the time each later frame).
int CParticleSource::UpdateSmoke(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        int frame = ++p->frame;
        if (frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
        if (frame > 7) {
            if (type_ == kTypeFire || type_ == kTypeTendril) return 0;
            p->frameTimer = frameDelay << 2;
            if ((GlobalRandom().Next() & 3) == 0) return 0;
        }
        if (p->frame == 8) p->drift.z = static_cast<float>(p->drift.z * kHalfDouble);
    }
    // The risen z stays on the FPU stack for the second addition (both sums are the same value).
    int frame = p->frame;
    float oldZ = p->pos.z;
    p->pos.z = p->drift.z + oldZ;
    if (frame < 2) {
        p->pos.x = p->velocity.x + p->pos.x;
        p->pos.y = p->velocity.y + p->pos.y;
        p->pos.z = (p->drift.z + oldZ) + p->velocity.z;
    }
    if (frame < 7) {
        p->size = growth * p->size;
        return 1;
    }
    float oldSize = p->size;
    p->size = oldSize * kSmokeGrowth;
    float push = oldSize * kSmokeGrowth * windStrength * kSmokeDrift;   // unrounded, as on the FPU stack
    p->pos.x = push * windDirection.x + p->pos.x;
    p->pos.y = push * windDirection.y + p->pos.y;
    p->pos.z = push * windDirection.z + p->pos.z;
    return 1;
}
EI_REPLACE_METHOD(0x0048EC20, CParticleSource::UpdateSmoke);

// A new smoke puff: a random offset in the source's spread (smaller puffs, 40% of them, spread
// wider), half of it applied at once, and a random height up to `height`.
int CParticleSource::EmitSmoke(Particle* p, int) {
    float extra = 0.0f;
    p->color = 0xFFFFFFFFu;
    p->life = lifetime;
    p->frame = type_ != kTypeSmoke ? 0 : 8;
    p->frameTimer = frameDelay;
    std::memcpy(&p->size, &size, sizeof(float));
    if (GlobalRandom().Next() % 100 + 1 <= 40) {
        p->size = size * kHalf;
        extra = StoreFloat(size * kPointThree);
    }
    float hi = StoreFloat(extra + spreadMax.x);
    float lo = StoreFloat(spreadMin.x - extra);
    p->velocity.x = static_cast<float>(Unsigned(GlobalRandom().Next()) * (hi - lo) * kRandomScale + lo);
    hi = StoreFloat(extra + spreadMax.y);
    lo = StoreFloat(spreadMin.y - extra);
    std::uint32_t r = GlobalRandom().Next();
    p->velocity.z = 0.0f;
    p->velocity.y = static_cast<float>(Unsigned(r) * (hi - lo) * kRandomScale + lo);
    p->velocity.x = Load(p->velocity.x) * kHalf;
    p->velocity.y = Load(p->velocity.y) * kHalf;
    p->velocity.z = Load(p->velocity.z) * kHalf;
    p->pos.x = origin.x + Load(p->velocity.x);
    p->pos.y = origin.y + Load(p->velocity.y);
    float h = height;
    r = GlobalRandom().Next();
    p->drift.x = 0.0f;
    p->drift.y = 0.0f;
    p->pos.z = static_cast<float>(Unsigned(r) * h * kRandomScale + origin.z);
    std::memcpy(&p->drift.z, &height, sizeof(float));
    return 1;
}
EI_REPLACE_METHOD(0x0048E9E0, CParticleSource::EmitSmoke);

// Nuke debris: fly up and fall back with growing opacity; on landing (falling below the source's
// mid height) it turns to dust that spreads, grows and fades over 14 ticks.
int CParticleSource::UpdateDebris(Particle* p) {
    if (--p->life == 0) return 0;
    if (p->state != 0) {
        if (p->frame >= 14) return 0;
        p->pos.x = p->drift.x + p->pos.x;
        ++p->frame;
        std::uint32_t a = (p->color >> 24) - 16;
        p->pos.y = p->pos.y + p->drift.y;
        p->drift.x = kDebrisDamping * p->drift.x;
        p->drift.y = kDebrisDamping * p->drift.y;
        p->size = p->size * kDebrisGrowth;
        if (a > 0xFF) a = 0;
        p->color = (a << 24) | 0xFFFFFF;
        return 1;
    }
    // The new vertical speed stays on the FPU stack for the position update.
    float oldRise = p->drift.z;
    p->drift.z = kDebrisDamping * oldRise - kDebrisGravity;
    p->pos.z = (kDebrisDamping * oldRise - kDebrisGravity) + p->pos.z;
    std::uint32_t a = static_cast<std::uint32_t>(GameFtol(Unsigned(p->color >> 24) * kDebrisFadeIn));
    if (a > 0xFF) a = 0xFF;
    p->color = (a << 24) | 0xFFFFFF;
    if (LessOrUnordered(p->drift.z, kZero) && size * kHalfDouble + origin.z > p->pos.z) {
        float speed = std::bit_cast<float>(std::bit_cast<std::uint32_t>(p->drift.z) & 0x7FFFFFFFu);
        p->state = 1;
        p->drift.x = speed * p->drift.x;
        p->drift.y = speed * p->drift.y;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00489190, CParticleSource::UpdateDebris);

// Orbiting particles (spheres, Antimagic): rotate the offset about z by the angle in element 1
// (cos, sin at its +0x18/+0x1C), flatten it onto the plane through element 1's normal at distance
// element 0 +0x20, and place it around the source; the size follows a curve over the lifetime.
int CParticleSource::UpdateOrbit(Particle* p) {
    if (--p->life == 0) return 0;
    p->frame = (p->frame + 1) % frameCount;
    const ParticleElement* e = elements;
    float scale;
    std::memcpy(&scale, &e[0].value[3], sizeof(scale));
    float c = e[1].value[3];
    float s = e[1].value[4];
    float x = p->velocity.x;
    float y = p->velocity.y;
    // Both are computed before either store; the plane test below reloads the stored floats.
    float ny = c * y + x * s;
    p->velocity.x = StoreFloat(c * x - s * y);
    p->velocity.y = StoreFloat(ny);
    Vector3 offset;
    float d = elements[0].value[5];
    if (d > kOrbitMinRadius) {
        const Vector3& n = elements[1].pos;
        float t = StoreFloat(-((p->velocity.z * n.z + p->velocity.y * n.y + p->velocity.x * n.x) / d));
        t = LessOrUnordered(t, kOne) ? t : kOne;
        t = LessOrUnordered(t, kZero) ? kZero : t;
        Vector3 along;
        along.Construct(t * n.x, t * n.y, t * n.z);
        SubtractVectors(&offset, p->velocity, along);
    } else {
        std::memcpy(&offset, &p->velocity, sizeof(offset));
    }
    const Vector3& center = elements[0].pos;
    float by = StoreFloat(origin.y + center.y);
    float bz = StoreFloat(origin.z + center.z);
    p->size = kOne;
    float px = (origin.x + center.x) + offset.x;
    p->pos.y = offset.y + by;
    p->pos.z = offset.z + bz;
    p->pos.x = px;
    switch (type_) {
    case kTypeSphereFire:
        p->size = SizeCurve()[p->life] * p->drift.x * scale * 2.0f;
        break;
    case kTypeAntimagic:
        p->size = AntimagicSizeCurve()[p->life] * p->drift.x * scale;
        break;
    case kTypeSphereElectricity:
        p->size = SizeCurve()[p->life] * p->drift.x * scale;
        break;
    default:
        p->size = SizeCurve()[p->life] * p->drift.x * scale * kSphereScale;
        break;
    }
    return 1;
}
EI_REPLACE_METHOD(0x0048E7C0, CParticleSource::UpdateOrbit);

// --- Element updates ----------------------------------------------------------------------------

// Nuke: halve the emission each tick (down to 8) and stop emitting after 13 ticks.
void CParticleSource::TickNuke(ParticleElement* e) {
    ++e->ticks;
    if (emitCount > 8) emitCount = emitCount / 2;
    if (e->ticks == 13) flags_ &= ~1u;
}
EI_REPLACE_METHOD(0x004892E0, CParticleSource::TickNuke);

// Tendril smoke: emission proportional to the length of `extent`, at least 6, at most a 16th of
// the particle limit.
void CParticleSource::TickTendril(ParticleElement*) {
    int n = GameFtol(x87::fsqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * kTendrilDensity);
    emitCount = n;
    int count = n < 6 ? 6 : n;
    int cap = maxParticles / 16;
    emitCount = count < cap ? count : cap;
}
EI_REPLACE_METHOD(0x0048EE20, CParticleSource::TickTendril);

// Mushroom cloud: the cap widens with ln(ticks + 1) and rises; the Mushroom shakes the camera on
// its first tick; emission stops after 30 ticks.
void CParticleSource::TickMushroom(ParticleElement* e) {
    if (type_ == kTypeMushroom && e->ticks == 0 && e == elements)
        CameraShake(TheCamera(), nullptr, &origin, 5.0f, size, 0.25f);
    int ticks = ++e->ticks;
    e->pos.x = static_cast<float>(LnOfInt(ticks + 1) * size * kMushroomWidth);
    e->pos.z = size * kMushroomRise + e->pos.z;
    if (ticks == 30) flags_ &= ~1u;
}
EI_REPLACE_METHOD(0x00488F00, CParticleSource::TickMushroom);
