// Particle effects of spells being cast or worn: each effect's set-up, emission, update and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/particle_common.h"

#include "game/core/core_maths.h"
#include "game/camera.h"
#include "game/core/core_maths.h"
#include "game/effects.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/core/random.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

// --- CastingFire ... CastingDivination: sparks rising around the caster ------------------------------------

namespace {

constexpr std::int32_t kCastAlphaByCountdown[7] = /*0078A964*/ {0, 32, 64, 112, 160, 208, 255};

constexpr std::int32_t kCastAlphaByLifeWide[14] = /*0078A980*/ {0, 32, 64, 112, 160, 208, 255, 255, 224, 192, 128, 64, 32, 16};   // CastingDivination

constexpr std::int32_t kCastAlphaByLife[14] = /*0078A9BC*/ {0, 255, 255, 255, 255, 255, 255, 255, 224, 192, 128, 64, 32, 16};

// The first emission: element 0 from the carrier's size, elements 1..lifetime random swirls.
// 0 (and a debug message) without a carrier.
bool SetUpCasting(ParticleSource& s) {
    if (!s.attachedTo) {
        game::DebugLog("Particle effect CastingElemental without carrier\n");
        return false;
    }
    auto [offset, radius] = s.AttachmentOffset();
    radius *= 0.8f;
    float scale = s.attachedTo->GetClassId() == ClassId::Unit ? s.attachedTo->Radius() : radius;
    if (ParticleTypeOf(s.type) == ParticleType::CastingDivination) scale = scale + scale;
    else scale = scale * 3.0f;
    s.elements.resize(s.lifetime + 1);
    ParticleElement* e = s.elements.data();
    e->cast.alpha = 0xFF;
    e->ticks = 6;
    e->cast.radius = scale;
    if (ParticleTypeOf(s.type) == ParticleType::CastingDivination) {
        e->cast.size = 0.07f;
        e->cast.growth = 1.05f;
    } else {
        e->cast.size = 0.08f;
        e->cast.growth = 1.07f;
    }
    for (int i = 1; i <= s.lifetime; ++i) {
        s.elements[i].swirl.position = RandomInBall() * 10.0f;
        s.elements[i].swirl.velocity = RandomInBall() * 0.1f;
    }
    return true;
}

} // namespace

// CastingFire ... CastingDivination. The first call sets the effect up (CastingDivination then emits its
// marker particle at the source); after that each particle starts on a random direction (pulled
// towards a random swirl), at the effect's radius around the source.
int EmitCasting(ParticleSource& s, Particle& p, int) {
    if (s.elements.empty()) {
        if (!SetUpCasting(s)) return 0;
        if (ParticleTypeOf(s.type) == ParticleType::CastingDivination) {
            p.color = 0xFFFFFF;
            p.frame = 0;
            p.state = 1;
            p.pos = s.origin;
            p.size = 0.0f;
            return 1;
        }
    }
    Vector3& direction = p.velocity;
    float& away = p.aux.x;     // the distance from the source
    float& inward = p.aux.y;   // its step in per tick
    float& swell = p.aux.z;    // the size's factor per tick
    p.color = 0xFFFFFF;
    p.frame = 14;
    p.life = 13;
    p.state = 0;
    p.frameTimer = 1;

    Vector3 v;
    float lengthSquared;
    for (;;) {
        v = RandomInCube();
        lengthSquared = LengthSquared(v);
        if (lengthSquared > 1.0f) continue;
        if (lengthSquared <= 0.0f) continue;   // random: never NaN
        break;
    }
    ParticleElement* state = s.elements.data();
    std::uint32_t span = static_cast<std::uint32_t>(s.frameDelay * s.lifetime + 1);
    int pick = static_cast<int>(g_clientRandom.Next() % span);
    if (pick > 0) {
        // Swirls 0 .. lifetime - 1: swirl 0 is element 0, the effect's state, whose radius and size the original
        // read as a swirl's x and y (z 0).
        const int swirl = (pick - 1) % s.lifetime;
        v += swirl == 0 ? Vector3{state->cast.radius, state->cast.size, 0.0f} : s.elements[swirl].swirl.position;
        lengthSquared = LengthSquared(v);
    }
    float inverse = 1.0f / std::sqrt(lengthSquared);
    direction = inverse * v;
    float reach = state->cast.radius * 0.6f;
    away = reach;
    inward = reach * (ParticleTypeOf(s.type) == ParticleType::CastingDivination ? 0.03846154f : 0.07692308f);   // 1/26, 1/13
    swell = state->cast.growth;
    p.pos = away * direction + s.origin;
    p.size = (g_clientRandom.Below(0.8f) + 0.6f) * state->cast.size * away;
    return 1;
}

// Casting particles: none once the effect's alpha is 0. The CastingDivination marker stays at the
// source with a random size; the others fade by their life (capped by the effect's alpha), run
// their frames backwards (CastingFire: every fourth tick held) and spiral in towards the source.
int UpdateCasting(ParticleSource& s, Particle& p) {
    const Vector3& direction = p.velocity;
    float& away = p.aux.x;
    const float& inward = p.aux.y;
    const float& swell = p.aux.z;
    ParticleElement* state = s.elements.data();
    if (state->cast.alpha == 0) return 0;
    if (p.state) {
        p.pos = s.origin;
        p.size = (g_clientRandom.Below(0.3f) + 0.85f) * s.elements[0].cast.radius * 0.3f;
        p.color = (static_cast<std::uint32_t>(s.elements[0].cast.alpha) << 24) | 0xFFFFFF;
        return 1;
    }
    int life = --p.life;
    if (life == 0) return 0;
    // Past the end of its life (the update after death): what the original read before the table, the countdown
    // table's last entry, or the zero after the wide table's 14 entries.
    std::int32_t fade = ParticleTypeOf(s.type) == ParticleType::CastingDivination
                            ? ByLife(kCastAlphaByLifeWide, life, kCastAlphaByCountdown[6])
                            : ByLife(kCastAlphaByLife, life, 0);
    std::int32_t cap = s.elements[0].cast.alpha;
    if (cap < fade) fade = cap;
    p.color = (static_cast<std::uint32_t>(fade) << 24) | 0xFFFFFF;
    if (ParticleTypeOf(s.type) == ParticleType::CastingFire && (life & 3) == 0) ++p.frame;
    float oldSize = p.size;
    float reach = away - inward;
    --p.frame;
    away = reach;
    p.pos = reach * direction + s.origin;
    p.size = oldSize * swell;
    return 1;
}

// Casting elements (made by the first emission): element 0 counts down and takes its alpha from the table (frozen
// while an emitting source is attached); the swirls move by their velocity, which gets a random kick in [-k, k),
// damping and a pull back to the centre.
void TickCasting(ParticleSource& s) {
    if (s.elements.empty()) return;
    ParticleElement& state = s.elements[0];
    if (!((s.flags & ParticleSourceFlag::Emitting) && s.attachedTo)) {
        if (state.ticks > 0) --state.ticks;
        state.cast.alpha = kCastAlphaByCountdown[state.ticks];
    }
    for (int i = 1; i < std::ssize(s.elements); ++i) {
        ParticleElement& e = s.elements[i];
        e.swirl.position += e.swirl.velocity;
        const Vector3& position = e.swirl.position;
        Vector3& velocity = e.swirl.velocity;
        float bound = 0.1f;
        float low = -bound;
        auto kicked = [&](float& v, float at) {
            float kick = g_clientRandom.Range(0.0f, bound - low);
            v = kick + v * 0.95f + low - at * 0.0005f;
        };
        kicked(velocity.x, position.x);
        kicked(velocity.y, position.y);
        kicked(velocity.z, position.z);
    }
}

// --- Protection spheres (SphereAcid, SphereElectricity, SphereFire) and Antimagic --------------------------

namespace {

constexpr float kStillSpeed = 0.01f;   // the carrier's speed below which a sphere is round

constexpr float kSphereScale = 1.6f;

constexpr float kSphereBefore = 1.0f;   // the four bytes before kSphereSizeByLife in the original (ByLife)

// Once per tick (the first emission): the sphere's height and radius ease towards the carrier's
// figure, the emission rate follows the source's size, and element 1 smooths the carrier's motion.
bool FollowCarrier(ParticleSource& s) {
    if (!s.attachedTo) {
        game::DebugLog("Particle effect Link without carrier\n");
        return false;
    }
    auto [offset, radius] = s.AttachmentOffset();
    radius *= 1.2f;
    float height = offset.z;
    if (s.elements.empty()) {
        s.elements.resize(2);
        s.elements[1].sphereTilt.tiltCos = lib::Cos(0.01f);
        s.elements[1].sphereTilt.tiltSin = lib::Sin(0.01f);
        s.elements[1].pos = {};
        s.elements[0].pos = {};
        s.elements[0].pos.z = height;
        s.elements[0].sphere.radius = radius;
    }
    ParticleElement* e = s.elements.data();
    e->pos.z = e->pos.z * 0.9f + height * 0.1f;
    float& sphereRadius = s.elements[0].sphere.radius;
    sphereRadius = radius * 0.1f + sphereRadius * 0.9f;
    float rate = s.size * (ParticleTypeOf(s.type) == ParticleType::Antimagic ? 24.0f : 6.0f);
    s.emitCount = std::lround(rate);
    Vector3& smoothed = s.elements[1].pos;
    smoothed = smoothed * 0.7f + s.motion;
    float& speed = s.elements[0].sphere.speed;
    speed = std::sqrt(LengthSquared(smoothed));
    if (speed < kStillSpeed) speed = 0.0f;
    return true;
}

// The size of an orbiting sphere's particle by the ticks it has left (the fire, electricity and air spheres'
// 16 entries; the antimagic sphere's 6).
constexpr float kSphereSizeByLife[16] = /*0073CB38*/ {
    0.01f, 0.02f, 0.03f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.03f,
    0.02f, 0.01f};
constexpr float kAntimagicSizeByLife[6] = /*0073CB78*/ {0.01f, 0.015f, 0.02f, 0.03f, 0.015f, 0.01f};

} // namespace

// Spheres: a particle on the sphere around the carrier (a random direction at 0.65 of
// the radius), flattened against the carrier's motion when it moves; one in eleven is larger. The
// size follows the life table of the kind.
int EmitOrbit(ParticleSource& s, Particle& p, int index) {
    Vector3& onSphere = p.velocity;   // the offset from the sphere's centre
    float& sizeFactor = p.drift.x;
    if (index == 0 && !FollowCarrier(s)) return 0;
    float radius = s.elements[0].sphere.radius;
    float big;
    if (g_clientRandom.Next() % 11 == 0) {
        float base = ParticleTypeOf(s.type) == ParticleType::Antimagic           ? 2.5f
                     : ParticleTypeOf(s.type) == ParticleType::SphereElectricity ? 3.5f
                                                                                 : 1.5f;
        big = g_clientRandom.Range(0.0f, 1.0f) + base;
    } else {
        big = g_clientRandom.Below(0.6f) + 0.7f;
    }
    sizeFactor = big;

    onSphere = RandomInBall();
    float shell = radius * 0.65f;
    float scale = shell / std::sqrt(LengthSquared(onSphere));
    onSphere *= scale;
    p.color = 0xFFFFFFFF;
    p.frame = static_cast<int>(g_clientRandom.Next() % 11);
    p.life = s.lifetime;
    p.frameTimer = 1;

    // The carrier's motion flattens the sphere: the part of the direction against the motion
    // (clamped to [0, 1] of it) is taken off.
    Vector3 offset;
    float speed = s.elements[0].sphere.speed;
    if (speed > kStillSpeed) {
        const Vector3& m = s.elements[1].pos;
        float t = -(Dot(m, onSphere) / speed);
        t = MinKeepA(t, 1.0f);
        t = MaxKeepB(t, 0.0f);
        offset = onSphere - m * t;
    } else {
        offset = onSphere;
    }
    p.pos = s.origin + s.elements[0].pos + offset;
    if (ParticleTypeOf(s.type) == ParticleType::SphereFire) {
        float fireSize = kSphereSizeByLife[p.life] * sizeFactor * radius;
        p.size = fireSize + fireSize;
    } else if (ParticleTypeOf(s.type) == ParticleType::Antimagic) {
        p.size = kAntimagicSizeByLife[p.life] * sizeFactor * radius;
    } else if (ParticleTypeOf(s.type) == ParticleType::SphereElectricity) {
        p.size = kSphereSizeByLife[p.life] * sizeFactor * radius;
    } else {
        p.size = kSphereSizeByLife[p.life] * sizeFactor * radius * 1.6f;
    }
    return 1;
}

// Orbiting particles (spheres, Antimagic): rotate the offset about z by element 1's tilt, flatten it onto the
// plane through element 1's normal at element 0's distance, and place it around the source; the size follows a
// curve over the lifetime.
int UpdateOrbit(ParticleSource& s, Particle& p) {
    Vector3& onSphere = p.velocity;
    const float& sizeFactor = p.drift.x;
    if (--p.life == 0) return 0;
    p.frame = (p.frame + 1) % s.frameCount;
    const ParticleElement* e = s.elements.data();
    float scale = e[0].sphere.radius;
    float c = e[1].sphereTilt.tiltCos;
    float sn = e[1].sphereTilt.tiltSin;
    float x = onSphere.x;
    float y = onSphere.y;
    // Both are computed before either store; the plane test below reloads the stored floats.
    float ny = c * y + x * sn;
    onSphere.x = c * x - sn * y;
    onSphere.y = ny;
    Vector3 offset;
    float d = s.elements[0].sphere.speed;
    if (d > kStillSpeed) {
        const Vector3& n = s.elements[1].pos;
        float t = -(Dot(onSphere, n) / d);
        t = MinKeepA(t, 1.0f);
        t = MaxKeepB(t, 0.0f);
        Vector3 along = t * n;
        offset = onSphere - along;
    } else {
        offset = onSphere;
    }
    const Vector3& center = s.elements[0].pos;
    p.pos = offset + (s.origin + center);
    switch (ParticleTypeOf(s.type)) {
    case ParticleType::SphereFire:
        p.size = ByLife(kSphereSizeByLife, p.life, kSphereBefore) * sizeFactor * scale * 2.0f;
        break;
    case ParticleType::Antimagic:
        p.size = ByLife(kAntimagicSizeByLife, p.life, kSphereSizeByLife[15]) * sizeFactor * scale;
        break;
    case ParticleType::SphereElectricity:
        p.size = ByLife(kSphereSizeByLife, p.life, kSphereBefore) * sizeFactor * scale;
        break;
    default:
        p.size = ByLife(kSphereSizeByLife, p.life, kSphereBefore) * sizeFactor * scale * kSphereScale;
        break;
    }
    return 1;
}

// --- Casting and Healing: rings of flames rising around the caster -----------------------------------------

namespace {

constexpr int kFlameState = 0xFFFF;     // a burning-out flame (the update's last phase)

// The rings around the carrier (emitCount of them, already allocated): stacked from the carrier's feet with a
// random lift, their flames spaced evenly around the circle; the source's height is the radius growth per
// tick (Healing also sets the flame growth). Each ring counts down from `countdown`.
void PlaceRings(ParticleSource& s, int countdown) {
    float step = ei::kRadiansPerDegree * (360.0f / s.emitCount);
    auto [offset, fullRadius] = s.AttachmentOffset();
    float angle = 0.0f;
    offset.z -= fullRadius;
    float diameter = fullRadius + fullRadius;
    float rise = fullRadius * 1.1f * 0.027777778f;   // 1/36
    s.height = rise + rise;
    if (ParticleTypeOf(s.type) == ParticleType::Healing) s.growth = (s.size + 1.0f) * 0.0069444445f;   // 1/144
    if (s.emitCount > 0) {
        float low = diameter * -0.1f;
        float high = diameter * 0.1f;
        for (int i = 0; i < s.emitCount; ++i) {
            ParticleElement* e = &s.elements[i];
            e->ticks = countdown;
            float lift = g_clientRandom.Range(low, high);
            e->flames.centre = offset;
            e->flames.centre.z = lift + e->flames.centre.z;
            e->pos = e->flames.centre;
            e->flames.radius = 0.0f;
            e->flames.angle = angle;
            e->flames.rise = (diameter - lift) * 0.027777778f;   // 1/36
            angle += step;
        }
    }
    s.emitState = 0;
}

// The flame phase: one flame per ring (state = the ring), then one call that only ends the phase
// (result 0). False once the phase is over.
bool EmitRingFlame(ParticleSource& s, Particle& p, int& result) {
    int emitted = s.emitState;
    if (emitted > std::ssize(s.elements)) return false;
    s.emitState = emitted + 1;
    if (emitted == std::ssize(s.elements)) {
        result = 0;
        return true;
    }
    p.color = 0xFFFFFFFF;
    p.frame = 0;
    p.life = 36;
    p.frameTimer = 1;
    p.state = emitted;
    const ParticleElement& ring = s.elements[emitted];
    p.pos = ring.pos + s.origin;
    if (ParticleTypeOf(s.type) == ParticleType::Healing) p.size = (s.size + 1.0f) * 0.1f;
    else p.size = s.size * 0.0015f;
    result = 1;
    return true;
}

// A spark in a box of +-spread around a ring's centre. The original's z uses -spread for both the range and
// the offset.
void PlaceRingSpark(ParticleSource& s, Particle& p, const ParticleElement& ring, float spread) {
    float low = -spread;
    p.pos.x = g_clientRandom.Range(0.0f, spread - low) + ring.pos.x + s.origin.x + low;
    p.pos.y = g_clientRandom.Range(0.0f, spread - low) + ring.pos.y + s.origin.y + low;
    p.pos.z = ring.pos.z - g_clientRandom.Range(0.0f, low) + s.origin.z + low;
}

void FollowRing(ParticleSource& s, Particle& p, int ring) {
    const ParticleElement& e = s.elements[ring];
    p.pos = s.origin + e.pos;
}

// Turns a ring's flame point 10 degrees around its centre; the ring rises.
void TurnRing(ParticleElement& e) {
    float& radius = e.flames.radius;
    float& angle = e.flames.angle;
    angle = (ei::kRadiansPerDegree * 10.0f) + angle;
    e.pos.x = lib::Sin(angle) * radius + e.flames.centre.x;
    e.pos.y = lib::Cos(angle) * radius + e.flames.centre.y;
    e.pos.z = e.flames.rise + e.pos.z;
}

} // namespace

// emitState counts the flames emitted after the set-up (one per ring). The two types share the shapes;
// they differ in the ring count, timings, sizes and the spark pattern.

// Casting: the first call places three rings (needs a carrier); then one flame
// per ring; then, while ring 0 is young, sparks scattered around ring 0's circle (72 per tick),
// else sparks around a random ring's centre.
int EmitCastRing(ParticleSource& s, Particle& p, int) {
    if (s.elements.empty()) {
        if (!s.attachedTo) {
            game::DebugLog("Particle effect Casting without carrier\n");
            return 0;
        }
        s.emitCount = 3;
        s.elements.resize(3);
        s.growth = 0.0f;
        PlaceRings(s, 45);
        s.emitCount *= 9;
    }
    int result;
    if (EmitRingFlame(s, p, result)) return result;
    if (s.elements[0].ticks < 9) {
        // On ring 0's circle, between the ring and a spread that narrows with the height.
        s.emitCount = 72;
        float ringRadius = s.elements[0].flames.radius;
        float twoPi = kPi + kPi;
        float angle = g_clientRandom.Range(0.0f, twoPi);
        p.color = 0xFFFFFFFF;
        p.frame = 1;
        int age = s.elements[0].ticks;
        if (age >= 5) age = 5;
        std::uint32_t spread = g_clientRandom.Next();
        p.frameTimer = 1;
        p.state = -1;
        p.life = static_cast<int>(spread % 4u) + age + 1;
        float inner = (ringRadius - s.height * 20.0f) * 0.5f + ringRadius;
        float reach = g_clientRandom.Range(inner, ringRadius);
        const ParticleElement& ring = s.elements[0];
        p.pos.x = lib::Sin(angle) * reach + (s.origin.x + ring.pos.x);
        p.pos.y = lib::Cos(angle) * reach + (s.elements[0].pos.y + s.origin.y);
        p.pos.z = s.elements[0].pos.z + s.origin.z;
    } else {
        const ParticleElement& ring = s.elements[g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements))];
        p.frame = 1;
        p.life = 7;
        p.frameTimer = 1;
        p.color = 0xFFFFFFFF;
        p.state = -1;
        PlaceRingSpark(s, p, ring, ring.flames.radius * 0.1f + 0.001f);
    }
    p.size = s.size * 0.1f;
    return 1;
}

// Casting particles: a flame (state = its ring) grows for 18 ticks, shrinks for 18 and
// rides its ring; then it burns out for 8 ticks (shrinking, alpha falling 30 per tick). Sparks
// (state -1) step through their frames.
int UpdateCastRing(ParticleSource& s, Particle& p) {
    int life = --p.life;
    if (life == 0) {
        if (p.state < 0 || p.state == kFlameState) return 0;
        p.state = kFlameState;
        p.life = 8;
        return 1;
    }
    int ring = p.state;
    if (ring == kFlameState) {
        p.size *= 0.7692308f;   // 1/1.3
        p.color = ((p.color & 0xFF000000u) - 0x1E000001u) | 0xFFFFFF;
        return 1;
    }
    if (ring < 0) {
        ++p.frame;
        return 1;
    }
    p.frameTimer = 1;
    if (life < 36 && life >= 18) p.size = p.size - s.growth;
    else p.size = s.growth + p.size;
    FollowRing(s, p, ring);
    return 1;
}

// Casting rings: each counts down (stopping the emission at 0); the radius grows by the
// source's height for 18 ticks, then shrinks, then eases towards 20 times the height; while
// above 9 ticks the flame point turns around the centre and the ring rises.
void TickCastRing(ParticleSource& s, ParticleElement& e) {
    int t = --e.ticks;
    if (t == 0) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float& radius = e.flames.radius;
    if (t >= 27) {
        radius = s.height + radius;
    } else if (t >= 9) {
        radius = radius - s.height;
    } else {
        float target = s.height * 20.0f;
        radius = (radius - target) * 0.5f + target;
    }
    if (t >= 9) TurnRing(e);
}

// Healing: the first call places 6 * size + 2 rings (needs a carrier); then
// one flame per ring; then sparks around a random ring's centre.
int EmitHealing(ParticleSource& s, Particle& p, int) {
    if (s.elements.empty()) {
        if (!s.attachedTo) {
            game::DebugLog("Particle effect Healing without carrier\n");
            return 0;
        }
        s.emitCount = std::lround(s.size * 6.0f + 2.0f);
        s.elements.resize(s.emitCount);
        PlaceRings(s, 36);
        s.emitCount *= 3;
    }
    int result;
    if (EmitRingFlame(s, p, result)) return result;
    const ParticleElement& ring = s.elements[g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements))];
    p.color = 0xFFFFFFFF;
    p.frame = 8;
    p.life = 8;
    p.frameTimer = 1;
    p.state = -1;
    PlaceRingSpark(s, p, ring, ring.flames.radius * 0.15f);
    p.size = (s.size + 1.0f) * 0.07f;
    return 1;
}

// Healing particles: like Casting's, but a flame cycles 8 frames, grows for its first
// 18 ticks of life and shrinks after, and burns out without shrinking.
int UpdateHealing(ParticleSource& s, Particle& p) {
    int life = --p.life;
    if (life == 0) {
        if (p.state < 0 || p.state == kFlameState) return 0;
        p.state = kFlameState;
        p.life = 8;
        return 1;
    }
    int ring = p.state;
    if (ring == kFlameState) {
        p.color = ((p.color & 0xFF000000u) - 0x1E000001u) | 0xFFFFFF;
        return 1;
    }
    if (ring < 0) {
        ++p.frame;
        return 1;
    }
    if (++p.frame >= 8) p.frame = 0;
    p.frameTimer = 1;
    if (life >= 18) p.size = s.growth + p.size;
    else p.size = p.size - s.growth;
    FollowRing(s, p, ring);
    return 1;
}

// Healing rings: each counts down (stopping the emission at 0); the radius grows by the
// source's height for 18 ticks, then shrinks; the flame point keeps turning and the ring rising.
void TickHealing(ParticleSource& s, ParticleElement& e) {
    int t = --e.ticks;
    if (t == 0) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float& radius = e.flames.radius;
    if (t >= 18) radius = s.height + radius;
    else radius = radius - s.height;
    TurnRing(e);
}

// --- CastingIllusion, CastingDomination, CastingEnchantment, CastingHealing: orbiting lights ---------------

namespace {

// Alpha rising and falling: a spark by its life (from the start), a marker one entry on, a column backwards
// from the last (ColumnAlpha).
constexpr std::int32_t kRiseFallAlpha[19] = /*0078A914*/ {0, 0, 32, 64, 112, 160, 208, 255, 255, 255, 255, 255, 255, 255, 207, 159, 111,
                                                 63, 15};

// A random point of the box [low, high), drawn x, y, z.
Vector3 RandomInBox(const Vector3& low, const Vector3& high) {
    return {g_clientRandom.Range(low.x, high.x), g_clientRandom.Range(low.y, high.y),
            g_clientRandom.Range(low.z, high.z)};
}

// Adds orbiter `k` (the k-th of `lifetime`, evenly spaced around the carrier) and emits its marker
// particle (state -1 - k).
int AddOrbiter(ParticleSource& s, Particle& p) {
    float orbit, carrier;     // the orbit radius and the carrier's radius
    int k = lib::Count(s.elements);
    if (k == 0) {
        auto [offset, radius] = s.AttachmentOffset();
        carrier = radius;
        orbit = radius * 0.5f;
    } else {
        orbit = s.elements[0].orbiter.relative.x;
        carrier = s.elements[0].orbiter.scale;
    }
    s.elements.resize(k + 1);
    float share = k * kPi;
    float angle = (share + share) / s.lifetime;
    ParticleElement* e = &s.elements[k];
    float lift = carrier * 0.4f;
    float sign = 1.0f;
    float turn;
    switch (ParticleTypeOf(s.type)) {
    case ParticleType::CastingIllusion:
    case ParticleType::CastingEnchantment:
        turn = 0.3f;
        break;
    case ParticleType::CastingDomination:
        turn = 0.5f;
        lift = carrier * 1.6f;
        sign = -1.0f;
        break;
    default:
        turn = 0.5f;
        break;
    }
    Vector3& relative = e->orbiter.relative;
    relative.x = lib::Cos(angle) * orbit;
    e->ticks = 1;
    relative.y = lib::Sin(angle) * orbit;
    relative.z = lift;
    e->pos = s.origin + relative;
    e->orbiter.turnCos = lib::Cos(turn);
    e->orbiter.previous = e->pos;
    e->orbiter.turnSin = lib::Sin(turn);
    // Kept from the original (its locals read one slot off): the sign times the carrier's radius, not the orbit.
    e->orbiter.scale = sign * carrier;
    p.color = 0xFFFFFFFF;
    p.frame = 0;
    p.life = 6;
    p.frameTimer = 1;
    p.state = -1 - k;
    p.pos = e->pos;
    p.size = 0.0f;
    return 1;
}

} // namespace

// One element per orbiter (lifetime of them, added one per emission).

// Until all orbiters exist each emission adds one (needs a carrier); then sparks start at a random point
// between an orbiter's previous and current position and fly off with a random velocity from the spread box.
int EmitCastSwirl(ParticleSource& s, Particle& p, int) {
    Vector3& ahead = p.velocity;   // where the spark is next
    float& rise = p.aux.z;         // added to ahead.z per tick
    float& riseGrowth = p.aux.y;   // the rise's factor per tick
    if (std::ssize(s.elements) < s.lifetime) {
        if (!s.attachedTo) {
            game::DebugLog("Particle effect CastingElemental without carrier\n");
            return 0;
        }
        return AddOrbiter(s, p);
    }
    int j = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(s.lifetime));
    ParticleElement* e = &s.elements[j];
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p.color = 0xFFFFFFFF;
    p.frame = 1;
    p.life = 28;
    p.frameTimer = 1;
    p.state = j;
    rise = (g_clientRandom.Below(0.35f) + 0.85f) * e->orbiter.scale * 0.03f;
    riseGrowth = 1.03f;
    ahead = RandomInBox(s.spreadMin, s.spreadMax);
    const Vector3& previous = e->orbiter.previous;
    p.pos = previous + ahead;
    float u = 1.0f - t;
    ahead += t * previous + u * e->pos;
    p.size = (g_clientRandom.Below(0.8f) + 0.6f) * s.size * 0.2f;
    return 1;
}

// A marker (state < 0) sits on its orbiter with a flickering size and, once the source stops emitting (or
// loses its carrier), fades by the table and dies. A spark fades over its last 7 ticks, cycles frames 12..15,
// shrinks, and moves by its velocity, whose z changes by a growing step.
int UpdateCastSwirl(ParticleSource& s, Particle& p) {
    Vector3& ahead = p.velocity;
    float& rise = p.aux.z;
    const float& riseGrowth = p.aux.y;
    int state = p.state;
    if (state < 0) {
        const ParticleElement& e = s.elements[-(state + 1)];
        p.pos = e.pos;
        p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * 0.4f;
        if ((s.flags & ParticleSourceFlag::Emitting) && s.attachedTo) return 1;
        int life = p.life;
        if (life < 0) return 0;
        p.color = (static_cast<std::uint32_t>(kRiseFallAlpha[life + 1]) << 24) | 0xFFFFFF;
        p.life = life - 1;
        return 1;
    }
    int life = --p.life;
    if (life == 0) return 0;
    if (life < 7) p.color = (static_cast<std::uint32_t>(ByLife(kRiseFallAlpha, life, 0)) << 24) | 0xFFFFFF;
    if (++p.frame == 16) p.frame = 12;
    float shrunk = p.size * 0.97f;
    p.pos = ahead;
    p.size = shrunk;
    ahead.z += rise;
    rise = riseGrowth * rise;
    return 1;
}

// Orbiters turn about the source's vertical axis (by -turn) and keep their previous world position.
void TickCastSwirl(ParticleSource& s, ParticleElement& e) {
    float c = e.orbiter.turnCos;
    float sn = e.orbiter.turnSin;
    Vector3& relative = e.orbiter.relative;
    float y = c * relative.y - relative.x * sn;
    float x = relative.x * c + relative.y * sn;
    e.orbiter.previous = e.pos;
    relative.x = x;
    relative.y = y;
    e.pos = s.origin + relative;
}

// --- TeleportRing: sparks rising in a ring around the carrier ----------------------------------------------

namespace {

// The alpha by remaining life (1-12), read backwards from kRiseFallAlpha's end. The update after death (life -1)
// reads past the table: 0 there (the original read the next table's first entry).
std::uint32_t ColumnAlpha(int life) { return life < 0 ? 0 : static_cast<std::uint32_t>(kRiseFallAlpha[19 - life]); }

// The ring from the carrier's figure (the first emission). False without a carrier.
bool SetUpColumn(ParticleSource& s) {
    if (!s.attachedTo) {
        game::DebugLog("Particle effect CastingElemental without carrier\n");
        return false;
    }
    auto [offset, radius] = s.AttachmentOffset();
    float height = radius + radius;
    float foot = -offset.z - height * 0.3f;
    float ring = s.attachedTo->GetClassId() == ClassId::Unit ? s.attachedTo->Radius() : radius * 0.8f;
    s.elements.resize(1);
    ParticleElement* e = s.elements.data();
    e->column.radius = ring + ring;
    e->column.height = height;
    e->column.foot = foot;
    e->ticks = 1;
    return true;
}

} // namespace

// TeleportRing: a spark on a ring of random radius around the carrier (0.9-1.1 of the ring),
// at a random height of the spread box above the ring's foot, rising faster and faster.
int EmitColumn(ParticleSource& s, Particle& p, int) {
    Vector3& offset = p.velocity;  // from the source
    float& rise = p.aux.z;         // added to offset.z per tick
    if (s.elements.empty() && !SetUpColumn(s)) return 0;
    p.color = 0xFFFFFFFF;
    p.frame = 1;
    p.life = 13;
    p.frameTimer = 1;
    rise = (g_clientRandom.Below(0.6f) + 0.7f) * s.elements[0].column.height * 0.1f;
    float ring = g_clientRandom.Below(0.2f) + 0.9f;
    float angle = g_clientRandom.Range(0.0f, 6.28318f);
    offset.x = lib::Sin(angle) * ring;
    offset.y = lib::Cos(angle) * ring;
    float z = g_clientRandom.Range(s.spreadMin.z, s.spreadMax.z);
    offset.z = z;
    offset.z = z + s.elements[0].column.foot;
    p.pos = s.origin + offset;
    p.size = (g_clientRandom.Below(0.8f) + 0.6f) * s.size * 0.25f;
    return 1;
}

// TeleportRing sparks: fade by life, step a frame, follow the source at their offset, shrink
// to 0.97 per tick and rise (the rise growing by 5% per tick).
int UpdateColumn(ParticleSource& s, Particle& p) {
    Vector3& offset = p.velocity;
    float& rise = p.aux.z;
    int life = --p.life;
    if (life == 0) return 0;
    float oldSize = p.size;
    p.color = (ColumnAlpha(life) << 24) | 0xFFFFFF;
    ++p.frame;
    p.pos = s.origin + offset;
    p.size = oldSize * 0.97f;
    offset.z += rise;
    rise *= 1.05f;
    return 1;
}

void TickColumn(ParticleSource&, ParticleElement& e) { ++e.ticks; }

// --- Cylinder1 / Cylinder2: two stars circling the carrier -------------------------------------------------

namespace {

constexpr std::int32_t kCylinderAlpha[32] = /*0078AB18*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255};

// The stars on the carrier's orbit (the first emission).
void PlaceCylinderStars(ParticleSource& s) {
    auto [offset, radius] = s.AttachmentOffset();
    float turn = (0.3f * kPi) * 0.13333334f;
    if (ParticleTypeOf(s.type) == ParticleType::Cylinder2) turn = -turn;
    float c = lib::Cos(turn);
    float sn = lib::Sin(turn);
    int count = lib::Count(s.elements);
    for (int i = 0; i < std::ssize(s.elements); ++i) {
        float orbit = radius * 0.7f;
        float twice = static_cast<float>(2 * i);
        ParticleElement& e = s.elements[i];
        e.pos.x = lib::Cos(twice * kPi / count) * orbit;
        e.pos.y = lib::Sin(twice * kPi / std::ssize(s.elements)) * orbit;
        e.pos.z = radius * 0.2f;
        e.star.step = {};
        e.star.rotation[0] = c;
        e.star.rotation[1] = sn;
        e.ticks = 18;
        e.star.rise = (2.0f / e.ticks) * radius * 0.04f;
        e.star.inverseOrbit = 1.0f / orbit;
        e.star.orbit = orbit;
        count = lib::Count(s.elements);
    }
}

} // namespace

// Cylinder1 / 2 (the casting circles): the two stars count down from 100.
void SetupCylinder(ParticleSource& s) {
    for (ParticleElement& e : s.elements) e.ticks = 100;
}

// A star's ticks count down and index kCylinderAlpha.

// Cylinder: the first particles are the stars (state = their index; the first one
// places them around the carrier); later ones are sparks along a random star's last step.
int EmitCylinder(ParticleSource& s, Particle& p, int) {
    Vector3& relative = p.velocity;    // a spark's position relative to the source
    Vector3& spiral = p.aux;           // a spark's step
    p.color = 0xFFFFFF;
    int count = lib::Count(s.particles);
    int stars = lib::Count(s.elements);
    if (count < stars) {
        if (count == 0) PlaceCylinderStars(s);
        p.frame = 0;
        p.state = count;
        const ParticleElement& e = s.elements[count];
        p.pos = e.pos + s.origin;
        p.size = 0.0f;
        return 1;
    }
    int j = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(stars));
    const ParticleElement& e = s.elements[j];
    int life = e.ticks + 6;
    if (life > 14) life = 14;
    p.life = life;
    if (life < 0) return 0;
    const Vector3& step = e.star.step;
    if (step.x == 0.0f && step.y == 0.0f) return 0;
    p.frame = 1;
    p.state = -1;
    float u = g_clientRandom.Range(-1.0f, 0.0f);
    relative = u * step + e.pos;
    spiral = step;
    p.size = 0.1f;
    p.pos = relative + s.origin;
    return 1;
}

// Cylinder: a star follows its element and fades by its countdown (dropped once the source stops); a spark
// fades by its life, drifts by its step, is held on its star's orbit radius and spirals round it, shrinking.
int UpdateCylinder(ParticleSource& s, Particle& p) {
    Vector3& relative = p.velocity;
    Vector3& spiral = p.aux;
    int state = p.state;
    if (state >= 0) {
        if (!(s.flags & ParticleSourceFlag::Emitting)) return 0;
        const ParticleElement& e = s.elements[state];
        p.color = (static_cast<std::uint32_t>(kCylinderAlpha[e.ticks]) << 24) | 0xFFFFFF;
        p.size = 0.3f;
        p.pos = s.origin + e.pos;
        return 1;
    }
    ++p.frame;
    int life = --p.life;
    if (life <= 0) return 0;
    p.color = (static_cast<std::uint32_t>(kCylinderAlpha[life]) << 24) | 0xFFFFFF;
    relative += spiral;
    float k = s.elements[0].star.orbit / std::sqrt(LengthSquared2D(relative.x, relative.y));
    float kx = k * relative.x;
    relative.x = kx;
    float ky = k * relative.y;
    relative.y = ky;
    float reach = std::sqrt(LengthSquared(spiral)) * s.elements[0].star.inverseOrbit;
    spiral = Vector3{ky * reach, -kx * reach, spiral.z} * 0.6f;
    p.pos = relative + s.origin;
    p.size *= 0.9f;
    return 1;
}

// Cylinder stars: the emission rate drops (by 2 on star 0's odd counts, 1 on even); each star counts down
// (stopping the source at 0), turns by its rotation, rises (faster and faster), and its rotation turns the other
// way, slowly.
void TickCylinder(ParticleSource& s) {
    if (s.elements[0].ticks % 2 != 0) --s.emitCount;
    --s.emitCount;
    for (int i = 0; i < std::ssize(s.elements); ++i) {
        ParticleElement& e = s.elements[i];
        int t = e.ticks--;
        if (t <= 0) {
            s.flags &= ~ParticleSourceFlag::Emitting;
            continue;
        }
        float (&rotation)[2] = e.star.rotation;
        float nx = Dot2D(rotation[1], rotation[0], e.pos.y, e.pos.x);
        float ny = rotation[0] * e.pos.y - rotation[1] * e.pos.x;
        float& rise = e.star.rise;
        Vector3 next = {nx, ny, e.pos.z + rise};
        e.star.step = next - e.pos;
        rise *= 1.25f;
        e.pos = next;
        float twist = kPi * -0.013333334f;
        if (ParticleTypeOf(s.type) == ParticleType::Cylinder2) twist = -twist;
        float c = lib::Cos(twist);
        float sn = lib::Sin(twist);
        float a = sn * rotation[1] + c * rotation[0];
        float b = c * rotation[1] - sn * rotation[0];
        rotation[0] = a;
        rotation[1] = b;
    }
}

// --- Modifier1-12: a ring of points around the carrier -----------------------------------------------------

namespace {

// By point 0's fade step; entry 10 (0): the last emitting tick, past the 10 (the original's zero tail).
constexpr std::int32_t kFadeAlpha[11] = /*0078A774*/ {0, 32, 64, 128, 255, 207, 159, 111, 63, 15, 0};

// The axis the points are spread around (their offsets are perpendicular to it); some types also
// raise or lower the ring (spreadMin.x holds the height).
Vector3 ModifierAxis(ParticleSource& s, float twiceRadius) {
    Vector3 axis = {};
    switch (ParticleTypeOf(s.type)) {
    case ParticleType::Modifier1:
        axis.z = 1.0f;
        break;
    case ParticleType::Modifier2:
    case ParticleType::Modifier3:
        s.spreadMin.x = twiceRadius * 0.4f + s.spreadMin.x;
        axis.z = 1.0f;
        break;
    case ParticleType::Modifier4:
        s.spreadMin.x -= twiceRadius * 0.4f;
        axis.z = 1.0f;
        break;
    case ParticleType::Modifier5:
    case ParticleType::Modifier6:
    case ParticleType::Modifier7:
    case ParticleType::Modifier8: {
        Vector3 y = s.attachedTo->localRotation.AxisY();
        axis = y;
        break;
    }
    default:
        break;
    }
    return axis;
}

// Each point: a random direction with its component along the axis removed (retried while that
// leaves nothing), scaled to the ring's radius (much smaller for a negative size), around the
// emission point, at the ring's height.
void PlaceModifierPoints(ParticleSource& s) {
    float radius = s.AttachmentOffset().radius;
    s.spreadMin.x = radius;
    float twice = radius + radius;
    Vector3 axis = ModifierAxis(s, twice);
    for (int i = 0; i < std::ssize(s.elements); ++i) {
        s.elements[i].ticks = 0;   // the fade step
        Vector3 v;
        float lengthSq;
        for (;;) {
            v = {g_clientRandom.Range(-1.0f, 1.0f), g_clientRandom.Range(-1.0f, 1.0f),
                 g_clientRandom.Range(-1.0f, 1.0f)};
            v -= axis * Dot(axis, v);
            lengthSq = LengthSquared(v);
            if (lengthSq != 0.0f) break;   // a NaN ends the search too
        }
        float scale = twice / std::sqrt(lengthSq);
        if (s.size < 0.0f) scale = scale * 0.04347826f;   // 1/23
        ParticleElement& point = s.elements[i];
        point.ring.offset = scale * v;
        point.pos = point.ring.offset + s.origin;
        point.pos.z = s.spreadMin.x + point.pos.z;
        point.ring.start = point.pos;
    }
}

// By tick.
constexpr std::int32_t kModifierAlphaIn[12] = /*0078A800*/ {0, 128, 255, 255, 255, 255, 255, 255, 192, 128, 64, 32};

constexpr std::int32_t kModifierAlphaOut[12] = /*0078A88C*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 128};

} // namespace

// Modifier1-12 (needs a carrier): on the first emission |size| * 10 points (at least one) are scattered on a
// ring around the carrier. Then one particle per point while the fade has not started (frame = the modifier's
// number), then drifting sparks between a random point and its starting place.
int EmitModifier(ParticleSource& s, Particle& p, int index) {
    Vector3& base = p.velocity;    // a spark's point between its element's places
    Vector3& scatter = p.aux;      // added to base, slowing
    p.color = 0xFFFFFFFF;
    p.life = s.lifetime;
    p.frameTimer = s.frameDelay;
    if (s.elements.empty()) {
        if (!s.attachedTo) {
            game::DebugLog("Particle effect Modifier without carrier\n");
            return 0;
        }
        int count = static_cast<int>(AbsBits(s.size) * 10.0f);
        if (count < 1) count = 1;
        s.elements.resize(count);
        PlaceModifierPoints(s);
    }
    ParticleElement* points = s.elements.data();
    const int fade = points[0].ticks;
    p.color = (static_cast<std::uint32_t>(kFadeAlpha[fade]) << 24) | 0xFFFFFF;
    if (points[0].ticks == 0 && index < std::ssize(s.elements)) {
        unsigned kind = static_cast<unsigned>(s.type - static_cast<int>(ParticleType::Modifier1));
        if (kind <= 11) p.frame = static_cast<int>(kind);
        p.state = index;
        p.pos = points[index].pos;
        p.size = s.size >= 0.0f ? 0.125f : 0.2f;
        p.life = 25;
        return 1;
    }

    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(s.elements));
    std::uint32_t pick = g_clientRandom.Next() % count;
    float u = g_clientRandom.Range(0.0f, 1.0f);
    std::uint32_t frameRoll = g_clientRandom.Next();
    float w = 1.0f - u;
    p.life = 5;
    p.frame = static_cast<int>((frameRoll & 3) + 12);
    const ParticleElement& e = s.elements[pick];
    base = u * e.pos + w * e.ring.start;
    float jz = g_clientRandom.Range(-0.04f, 0.04f);
    float jy = g_clientRandom.Range(-0.04f, 0.04f);
    float jx = g_clientRandom.Range(-0.04f, 0.04f);
    scatter = {jx, jy, jz};
    p.pos = s.elements[pick].ring.start;
    p.size = 0.06f;
    return 1;
}

// Modifier: the first twelve frames are the marker riding its element (state), its size
// shrinking (negative source size) or growing, alpha by the element's tick. Later frames are
// sparks stepping through the frames (back to 12 past the last), flying out by velocity plus their
// spread (slowing by 0.9), fading by 48 per tick and shrinking by 0.9.
int UpdateModifier(ParticleSource& s, Particle& p) {
    const Vector3& base = p.velocity;  // a spark's point between its element's places
    Vector3& scatter = p.aux;          // added to base, slowing
    if (--p.life == 0) return 0;
    if (p.frame < 12) {
        float oldSize = p.size;
        const ParticleElement& e = s.elements[p.state];
        p.pos = e.pos;
        // A marker outlives the tables (its element ticks on to 25): alpha 0 past them, the original's zero tail.
        const int tick = e.ticks;
        std::int32_t a = 0;
        if (s.size < 0.0f) {
            p.size = oldSize * 0.9345794f;    // 1 / 1.07
            if (tick < std::ssize(kModifierAlphaIn)) a = kModifierAlphaIn[tick];
        } else {
            p.size = oldSize * 1.07f;
            if (tick < std::ssize(kModifierAlphaOut)) a = kModifierAlphaOut[tick];
        }
        p.color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
        return 1;
    }
    if (--p.frameTimer == 0) {
        if (++p.frame >= s.frameCount) p.frame = 12;
        p.frameTimer = s.frameDelay;
    }
    float slow = 0.9f;
    scatter *= slow;
    float oldSize = p.size;
    p.pos = scatter + base;
    std::int32_t a = static_cast<std::int32_t>(p.color >> 24) - 0x30;
    if (a < 0) a = 0;
    p.size = oldSize * slow;
    p.color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
    return 1;
}

// Modifier markers: for ten ticks (then the source stops emitting) each remembers its
// position and moves to the source plus its offset (ring.offset), raised by spreadMin.x; the offset
// widens by 1.4 (negative source size) or narrows by 1/1.4 per tick.
void TickModifier(ParticleSource& s, ParticleElement& e) {
    int tick = e.ticks++;
    if (tick >= 10) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    e.ring.start = e.pos;
    e.pos = e.ring.offset + s.origin;
    e.pos.z = s.spreadMin.x + e.pos.z;
    e.ring.offset *= s.size >= 0.0f ? 0.71428573f : 1.4f;    // 1 / 1.4, 1.4
}

// --- Link: a beam of sparks from the source along its extent -----------------------------------------------

namespace {

// The beam's frame from the extent (the first emission).
void SetUpLink(ParticleSource& s) {
    s.elements.resize(4);
    const Vector3& x = s.extent;
    s.elements[0].link.length = std::sqrt(LengthSquared(x));
    s.elements[0].link.inverseLength = 1.0f / s.elements[0].link.length;

    // A side vector: (ey, -ex, 0), or (ey - ez, -ex, ex) when the extent is (nearly) vertical.
    // (0 - ex, not -ex: the same value, but +0 for ex = 0.)
    Vector3 side = {x.y, 0.0f - x.x, 0.0f};
    float length = std::sqrt(LengthSquared(side));
    if (length < 1e-10f) {
        side = {x.y - x.z, -x.x, x.x};
        length = std::sqrt(LengthSquared(side));
    }
    float inverse = 1.0f / length;
    side *= inverse;

    Vector3 cross = Cross(x, side);
    float crossInverse = 1.0f / std::sqrt(LengthSquared(cross));
    cross *= crossInverse;

    s.elements[1].linkAxis.axis = x / s.elements[0].link.length;
    s.elements[2].linkAxis.axis = side;
    s.elements[3].linkAxis.axis = cross;
    s.elements[0].ticks = 0xFF;
}

} // namespace

// Link: a spark at the source with a random offset across the beam (from the spread
// box), a twist rate from its distance to the axis and a random speed along the beam (fast and
// large sparks near the axis now and then).
int EmitLink(ParticleSource& s, Particle& p, int) {
    Vector3& offset = p.velocity;  // across the beam (x, y) and along it (z)
    float& twistCos = p.aux.x;     // the offset's turn per tick
    float& twistSin = p.aux.y;
    float& covered = p.drift.x;    // the way along the beam
    float& speed = p.drift.y;
    float& sparkSize = p.drift.z;
    if (s.elements.empty()) SetUpLink(s);
    p.color = 0xFFFFFF;
    float a = g_clientRandom.Range(-1.0f, 1.0f);
    float b = g_clientRandom.Range(-1.0f, 1.0f);
    float c = g_clientRandom.Range(s.spreadMin.z, s.spreadMax.z);
    offset.x = a * s.spreadMin.x;
    offset.y = b * s.spreadMin.y;
    offset.z = c;
    float reach = std::sqrt(b * b + a * a);
    float twist = reach * 0.2f;
    twistCos = lib::Cos(twist);
    twistSin = lib::Sin(twist);
    Vector3 start = offset + s.origin;
    p.pos = start;
    float factor;
    if ((g_clientRandom.Next() & 3) == 0 && reach < 0.5f) {
        speed = g_clientRandom.Range(0.1f, 0.12f);
        factor = g_clientRandom.Range(0.13f, 0.16f);
    } else {
        speed = g_clientRandom.Range(0.17f, 0.25f);
        factor = g_clientRandom.Range(0.03f, 0.05f);
    }
    sparkSize = factor * s.size;
    covered = 0.0f;
    p.size = sparkSize;
    p.frame = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(s.frameCount + 1));
    return 1;
}

// Link sparks run along the beam (drift.x the distance travelled, drift.y the speed), spinning around the axis;
// the beam bulges in the middle and the alpha peaks there (capped by element 0). Dropped past the beam's end.
int UpdateLink(ParticleSource& s, Particle& p) {
    Vector3& offset = p.velocity;
    const float& twistCos = p.aux.x;
    const float& twistSin = p.aux.y;
    float& covered = p.drift.x;
    const float& speed = p.drift.y;
    const float& sparkSize = p.drift.z;
    p.frame = (p.frame + 1) % s.frameCount;
    float travelled = speed + covered;
    covered = travelled;
    ParticleElement* e = s.elements.data();
    float along = travelled * e->link.inverseLength;
    if (travelled > e->link.length) return 0;

    float c = twistCos;
    float sn = twistSin;
    float turnedY = offset.y * c + offset.x * sn;
    float turnedX = c * offset.x - offset.y * sn;
    offset.x = turnedX;
    offset.y = turnedY;
    const Vector3& direction = s.elements[1].linkAxis.axis;
    const Vector3& sideAxis = s.elements[2].linkAxis.axis;
    const Vector3& cross = s.elements[3].linkAxis.axis;
    Vector3 p3 = turnedX * sideAxis + s.origin + turnedY * cross + offset.z * direction + along * s.extent;
    p.pos = p3;

    float h = along - 0.5f;
    p.size = sparkSize;
    p.pos.z = (0.25f - h * h) * s.elements[0].link.length * 0.9f + p3.z;
    // Negative away from the middle: as an unsigned word above any cap.
    std::uint32_t fade = static_cast<std::uint32_t>(static_cast<int>((0.5f - std::fabs(h)) * 1000.0f));
    std::uint32_t cap = static_cast<std::uint32_t>(s.elements[0].ticks);
    if (cap < fade) fade = cap;
    p.color = (fade << 24) | 0xFFFFFF;
    return 1;
}

// Link element 0: once the source stops emitting the alpha cap falls by 16 per tick
// (not below 0). (The other elements' caps are unused.)
void TickLink(ParticleSource& s, ParticleElement& e) {
    if (!(s.flags & ParticleSourceFlag::Emitting)) e.ticks -= 16;
    std::int32_t cap = e.ticks;
    e.ticks = cap < 0 ? 0 : cap;
}

// --- BlueGas (the Stench texture): gas swirling up around the carrier --------------------------------------

// BlueGas: the first call measures the carrier (5 * size + 0.5 puffs per tick). A puff starts at a random
// point of the disc around the carrier's figure, turning about the axis faster the nearer it is, and rising.
int EmitStench(ParticleSource& s, Particle& p, int) {
    Vector3& around = p.velocity;  // the offset from the carrier's axis
    float& spinSin = p.drift.x;    // the offset's turn per tick
    float& spinCos = p.drift.y;
    float& rise = p.drift.z;       // added to around.z per tick
    if (s.elements.empty()) {
        if (!s.attachedTo) {
            game::DebugLog("Particle effect Link without carrier\n");
            return 0;
        }
        auto [offset, radius] = s.AttachmentOffset();
        s.elements.resize(1);
        s.elements[0].pos = offset;
        s.elements[0].gas.radius = radius * 0.3f;
        s.elements[0].gas.height = radius + radius;
        int rate = static_cast<int>(s.size * 5.0f + 0.5f);
        s.emitCount = rate < 1 ? 1 : rate;
    }
    float radius = s.elements[0].gas.radius;
    float carrierHeight = s.elements[0].gas.height;
    float t = g_clientRandom.Range(0.0f, radius);
    float twoPi = kPi + kPi;
    float angle = g_clientRandom.Range(0.0f, twoPi);
    p.color = 0xFFFFFFFF;
    p.frame = 0;
    p.life = s.lifetime;
    p.frameTimer = s.frameDelay;
    float turn = t * 0.3f / radius;
    float low = radius * -0.3f;
    float vx = lib::Sin(angle) * t;
    float vy = lib::Cos(angle) * t;
    float vz = g_clientRandom.Range(low, radius * 0.3f);
    around = {vx, vy, vz};
    float riseLow = carrierHeight * 0.02f;
    spinSin = lib::Sin(turn);
    spinCos = lib::Cos(turn);
    rise = g_clientRandom.Range(riseLow, carrierHeight * 0.03f);
    p.pos = s.origin + s.elements[0].pos + around;
    p.size = radius * 0.7f;
    return 1;
}

// BlueGas puffs: step through the frames (dropped after the last), turn their offset
// about the axis, rise faster and faster, swell, and fade over the last 8 ticks.
int UpdateStench(ParticleSource& s, Particle& p) {
    Vector3& around = p.velocity;
    const float& spinSin = p.drift.x;
    const float& spinCos = p.drift.y;
    float& rise = p.drift.z;
    int life = --p.life;
    if (life == 0) return 0;
    if (--p.frameTimer == 0) {
        if (++p.frame >= s.frameCount) return 0;
        p.frameTimer = s.frameDelay;
    }
    p.pos -= around;
    float turnedY = Dot2D(around.x, around.y, spinSin, spinCos);
    float turnedX = spinCos * around.x - around.y * spinSin;
    around.x = turnedX;
    around.y = turnedY;
    around.z = rise + around.z;
    rise *= 1.02f;
    p.pos += around;
    p.size *= 1.01f;
    if (life < 8) p.color = ((p.color & 0xFF000000u) - 0x1F000001u) | 0xFFFFFF;
    return 1;
}
