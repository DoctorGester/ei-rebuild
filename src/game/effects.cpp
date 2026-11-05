#include "camera.h"
#include "effects.h"

#include "core_maths.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "random.h"

#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/float_select.h"

#include <bit>
#include <cmath>
#include <cstring>

// The particle callbacks run for every live particle every frame.

namespace {

constexpr float kPointThree = 0.3f;
constexpr float kCloudAlpha = 4.5f;
constexpr float kSmokeGrowth = 1.06f;
constexpr float kSmokeDrift = 0.15f;
constexpr float kDebrisDamping = 0.85f;
constexpr float kDebrisGravity = 0.055f;
constexpr float kDebrisFadeIn = 4.0f;
constexpr float kDebrisGrowth = 1.13f;
constexpr float kTendrilDensity = 14.285714f;
constexpr float kMushroomWidth = 0.2f;
constexpr float kMushroomRise = 0.12f;
constexpr float kOrbitMinRadius = 0.01f;
constexpr float kSphereScale = 1.6f;
constexpr float kSphereBefore = 1.0f;   // the four bytes before kSphereSizeByLife in the original (ByLife)

} // namespace

// --- Source setters -----------------------------------------------------------------------------

void ParticleSource::ScaleSize(float factor) {
    spreadMin *= factor;
    spreadMax *= factor;
    size = factor * size;
    baseSize = factor * baseSize;
    height = factor * height;
    emitSpacing = factor * emitSpacing;
}

void ParticleSource::StopEmitting() { flags = (flags & ~ParticleSourceFlag::Emitting) | ParticleSourceFlag::KeepAlive; }

void ParticleSource::StartEmitting() { flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive; }

void ParticleSource::Stop() {
    flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    Attach(nullptr);
}

// --- Particle updates ---------------------------------------------------------------------------

// Falling drops: move by the drift, scale the size, accelerate down to the terminal speed (drift.z = max(drift.z +
// gravity, terminal)); dropped once below z = 0.
int ParticleSource::UpdateFalling(Particle* p) {
    const float& sizeFactor = p->velocity.x;
    const float& terminal = p->velocity.y;
    const float& gravity = p->velocity.z;
    if (p->pos.z < 0.0f) return 0;
    p->pos += p->drift;
    p->size = sizeFactor * p->size;
    float fall = gravity + p->drift.z;
    p->drift.z = MaxKeepB(fall, terminal);
    return 1;
}

// Clouds: cycle the texture frames; once the source stops emitting, raise the alpha byte (it wraps) until it is at
// most 9, then drop the particle.
int ParticleSource::UpdateCloud(Particle* p) {
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) p->frame = 0;
        p->frameTimer = frameDelay;
    }
    if (flags & ParticleSourceFlag::Emitting) return 1;
    std::uint32_t a = p->color >> 24;
    if (a <= 9) return 0;
    int step = std::lround(alpha * kCloudAlpha);
    if (step < 1) step = 1;
    std::uint32_t rgb = ParticleTypeOf(type) == ParticleType::PoisonCloud ? 0xFFAF7Fu : 0xFFFFFFu;
    p->color = ((static_cast<std::uint32_t>(step) * 255 + a) << 24) | rgb;
    return 1;
}

// Smoke puffs: rise and spread for the first two frames, then grow; past frame 7 they drift with
// the wind and fire smoke dies (other smoke lives on, a quarter of the time each later frame).
int ParticleSource::UpdateSmoke(Particle* p) {
    const Vector3& spreadStep = p->velocity;   // in the first two frames
    float& rise = p->drift.z;
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        int frame = ++p->frame;
        if (frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
        if (frame > 7) {
            if (ParticleTypeOf(type) == ParticleType::Fire || ParticleTypeOf(type) == ParticleType::Tendril) return 0;
            p->frameTimer = frameDelay << 2;
            if ((g_clientRandom.Next() & 3) == 0) return 0;
        }
        if (p->frame == 8) rise = rise * 0.5f;
    }
    int frame = p->frame;
    p->pos.z += rise;
    if (frame < 2) p->pos += spreadStep;
    if (frame < 7) {
        p->size = growth * p->size;
        return 1;
    }
    float oldSize = p->size;
    p->size = oldSize * kSmokeGrowth;
    float push = oldSize * kSmokeGrowth * windStrength * kSmokeDrift;
    p->pos += push * windDirection;
    return 1;
}

// A new smoke puff: a random offset in the source's spread (smaller puffs, 40% of them, spread
// wider), half of it applied at once, and a random height up to `height`.
int ParticleSource::EmitSmoke(Particle* p, int) {
    Vector3& spreadStep = p->velocity;  // half the puff's random offset: added again in its first two frames
    float& rise = p->drift.z;
    float extra = 0.0f;
    p->color = 0xFFFFFFFFu;
    p->life = lifetime;
    p->frame = ParticleTypeOf(type) != ParticleType::Smoke ? 0 : 8;
    p->frameTimer = frameDelay;
    p->size = size;
    if (g_clientRandom.Next() % 100 + 1 <= 40) {
        p->size = size * 0.5f;
        extra = size * kPointThree;
    }
    float hi = extra + spreadMax.x;
    float lo = spreadMin.x - extra;
    spreadStep.x = g_clientRandom.Range(lo, hi);
    hi = extra + spreadMax.y;
    lo = spreadMin.y - extra;
    spreadStep.z = 0.0f;
    spreadStep.y = g_clientRandom.Range(lo, hi);
    spreadStep *= 0.5f;
    p->pos.x = origin.x + spreadStep.x;
    p->pos.y = origin.y + spreadStep.y;
    p->drift.x = 0.0f;
    p->drift.y = 0.0f;
    p->pos.z = g_clientRandom.Range(0.0f, height) + origin.z;
    rise = height;
    return 1;
}

// Nuke debris: fly up and fall back with growing opacity; on landing (falling below the source's
// mid height) it turns to dust that spreads, grows and fades over 14 ticks.
int ParticleSource::UpdateDebris(Particle* p) {
    if (--p->life == 0) return 0;
    if (p->state != 0) {
        if (p->frame >= 14) return 0;
        p->pos.x = p->drift.x + p->pos.x;
        ++p->frame;
        std::uint32_t a = (p->color >> 24) - 16;
        p->pos.y += p->drift.y;
        p->drift.x = kDebrisDamping * p->drift.x;
        p->drift.y = kDebrisDamping * p->drift.y;
        p->size *= kDebrisGrowth;
        if (a > 0xFF) a = 0;
        p->color = (a << 24) | 0xFFFFFF;
        return 1;
    }
    float oldRise = p->drift.z;
    p->drift.z = kDebrisDamping * oldRise - kDebrisGravity;
    p->pos.z = (kDebrisDamping * oldRise - kDebrisGravity) + p->pos.z;
    std::uint32_t a = static_cast<std::uint32_t>((p->color >> 24) * kDebrisFadeIn);
    if (a > 0xFF) a = 0xFF;
    p->color = (a << 24) | 0xFFFFFF;
    if (p->drift.z < 0.0f && size * 0.5f + origin.z > p->pos.z) {
        float speed = AbsBits(p->drift.z);
        p->state = 1;
        p->drift.x = speed * p->drift.x;
        p->drift.y = speed * p->drift.y;
    }
    return 1;
}

// Orbiting particles (spheres, Antimagic): rotate the offset about z by element 1's tilt, flatten it onto the
// plane through element 1's normal at element 0's distance, and place it around the source; the size follows a
// curve over the lifetime.
int ParticleSource::UpdateOrbit(Particle* p) {
    Vector3& onSphere = p->velocity;
    const float& sizeFactor = p->drift.x;
    if (--p->life == 0) return 0;
    p->frame = (p->frame + 1) % frameCount;
    const ParticleElement* e = elements.data();
    float scale = e[0].sphere.radius;
    float c = e[1].sphereTilt.tiltCos;
    float s = e[1].sphereTilt.tiltSin;
    float x = onSphere.x;
    float y = onSphere.y;
    // Both are computed before either store; the plane test below reloads the stored floats.
    float ny = c * y + x * s;
    onSphere.x = c * x - s * y;
    onSphere.y = ny;
    Vector3 offset;
    float d = elements[0].sphere.speed;
    if (d > kOrbitMinRadius) {
        const Vector3& n = elements[1].pos;
        float t = -(Dot(onSphere, n) / d);
        t = MinKeepA(t, 1.0f);
        t = MaxKeepB(t, 0.0f);
        Vector3 along = t * n;
        offset = onSphere - along;
    } else {
        offset = onSphere;
    }
    const Vector3& center = elements[0].pos;
    p->pos = offset + (origin + center);
    switch (ParticleTypeOf(type)) {
    case ParticleType::SphereFire:
        p->size = ByLife(kSphereSizeByLife, p->life, kSphereBefore) * sizeFactor * scale * 2.0f;
        break;
    case ParticleType::Antimagic:
        p->size = ByLife(kAntimagicSizeByLife, p->life, kSphereSizeByLife[15]) * sizeFactor * scale;
        break;
    case ParticleType::SphereElectricity:
        p->size = ByLife(kSphereSizeByLife, p->life, kSphereBefore) * sizeFactor * scale;
        break;
    default:
        p->size = ByLife(kSphereSizeByLife, p->life, kSphereBefore) * sizeFactor * scale * kSphereScale;
        break;
    }
    return 1;
}

// --- Element updates ----------------------------------------------------------------------------

// Nuke: halve the emission each tick (down to 8) and stop emitting after 13 ticks.
void ParticleSource::TickNuke(ParticleElement* e) {
    ++e->ticks;
    if (emitCount > 8) emitCount = emitCount / 2;
    if (e->ticks == 13) flags &= ~ParticleSourceFlag::Emitting;
}

// Tendril smoke: emission proportional to the length of `extent`, at least 6, at most a 16th of
// the particle limit.
void ParticleSource::TickTendril(ParticleElement*) {
    int n = static_cast<int>(Length(extent) * kTendrilDensity);
    emitCount = n;
    int count = n < 6 ? 6 : n;
    int cap = maxParticles / 16;
    emitCount = count < cap ? count : cap;
}

// Mushroom cloud: the cap widens with ln(ticks + 1) and rises; the Mushroom shakes the camera on
// its first tick; emission stops after 30 ticks.
void ParticleSource::TickMushroom(ParticleElement* e) {
    if (ParticleTypeOf(type) == ParticleType::Mushroom && e->ticks == 0 && e == elements.data())
        g_camera.Shake(origin, 5.0f, size, 0.25f);
    int ticks = ++e->ticks;
    e->mushroom.spread = lib::Log(static_cast<float>(ticks + 1)) * size * kMushroomWidth;
    e->mushroom.capHeight = size * kMushroomRise + e->mushroom.capHeight;
    if (ticks == 30) flags &= ~ParticleSourceFlag::Emitting;
}
