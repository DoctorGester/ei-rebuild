// Particle effects of spells being cast or worn: each effect's set-up, emission, update and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/effects/particle_common.h"

#include "game/core/core_maths.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/core/random.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

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
        s.elements[i].swirl.position = effectdata::RandomInBall(g_particleRandom) * 10.0f;
        s.elements[i].swirl.velocity = effectdata::RandomInBall(g_particleRandom) * 0.1f;
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
        v = effectdata::RandomInCube(g_particleRandom);
        lengthSquared = LengthSquared(v);
        if (lengthSquared > 1.0f) continue;
        if (lengthSquared <= 0.0f) continue;   // random: never NaN
        break;
    }
    ParticleElement* state = s.elements.data();
    std::uint32_t span = static_cast<std::uint32_t>(s.frameDelay * s.lifetime + 1);
    int pick = static_cast<int>(g_particleRandom.Next() % span);
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
    p.size = (g_particleRandom.Below(0.8f) + 0.6f) * state->cast.size * away;
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
        p.size = (g_particleRandom.Below(0.3f) + 0.85f) * s.elements[0].cast.radius * 0.3f;
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
            float kick = g_particleRandom.Range(0.0f, bound - low);
            v = kick + v * 0.95f + low - at * 0.0005f;
        };
        kicked(velocity.x, position.x);
        kicked(velocity.y, position.y);
        kicked(velocity.z, position.z);
    }
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
            float lift = g_particleRandom.Range(low, high);
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
    p.pos.x = g_particleRandom.Range(0.0f, spread - low) + ring.pos.x + s.origin.x + low;
    p.pos.y = g_particleRandom.Range(0.0f, spread - low) + ring.pos.y + s.origin.y + low;
    p.pos.z = ring.pos.z - g_particleRandom.Range(0.0f, low) + s.origin.z + low;
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
        float angle = g_particleRandom.Range(0.0f, twoPi);
        p.color = 0xFFFFFFFF;
        p.frame = 1;
        int age = s.elements[0].ticks;
        if (age >= 5) age = 5;
        std::uint32_t spread = g_particleRandom.Next();
        p.frameTimer = 1;
        p.state = -1;
        p.life = static_cast<int>(spread % 4u) + age + 1;
        float inner = (ringRadius - s.height * 20.0f) * 0.5f + ringRadius;
        float reach = g_particleRandom.Range(inner, ringRadius);
        const ParticleElement& ring = s.elements[0];
        p.pos.x = lib::Sin(angle) * reach + (s.origin.x + ring.pos.x);
        p.pos.y = lib::Cos(angle) * reach + (s.elements[0].pos.y + s.origin.y);
        p.pos.z = s.elements[0].pos.z + s.origin.z;
    } else {
        const ParticleElement& ring = s.elements[g_particleRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements))];
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
    const ParticleElement& ring = s.elements[g_particleRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements))];
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

// Alpha rising and falling: a spark by its life (from the start), a marker one entry on.
constexpr std::int32_t kRiseFallAlpha[19] = /*0078A914*/ {0, 0, 32, 64, 112, 160, 208, 255, 255, 255, 255, 255, 255, 255, 207, 159, 111,
                                                 63, 15};

// A random point of the box [low, high), drawn x, y, z.
Vector3 RandomInBox(const Vector3& low, const Vector3& high) {
    return {g_particleRandom.Range(low.x, high.x), g_particleRandom.Range(low.y, high.y),
            g_particleRandom.Range(low.z, high.z)};
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
    int j = static_cast<int>(g_particleRandom.Next() % static_cast<std::uint32_t>(s.lifetime));
    ParticleElement* e = &s.elements[j];
    float t = g_particleRandom.Range(0.0f, 1.0f);
    p.color = 0xFFFFFFFF;
    p.frame = 1;
    p.life = 28;
    p.frameTimer = 1;
    p.state = j;
    rise = (g_particleRandom.Below(0.35f) + 0.85f) * e->orbiter.scale * 0.03f;
    riseGrowth = 1.03f;
    ahead = RandomInBox(s.spreadMin, s.spreadMax);
    const Vector3& previous = e->orbiter.previous;
    p.pos = previous + ahead;
    float u = 1.0f - t;
    ahead += t * previous + u * e->pos;
    p.size = (g_particleRandom.Below(0.8f) + 0.6f) * s.size * 0.2f;
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
        p.size = (g_particleRandom.Below(0.4f) + 0.8f) * s.size * 0.4f;
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
