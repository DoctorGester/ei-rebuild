// Particle effects that mark a unit (stars, clouds, sparks around it): each effect's set-up, emission, update
// and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/effects/particle_common.h"

#include "game/core/core_maths.h"
#include "game/scene/camera.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/core/random.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

// --- Silence -----------------------------------------------------------------------------------------------

// Silence: the first two particles are orbs at the source (state 1) circling in step (the second starts at pi).
// Further ones are wisps (state 0) somewhere along a random orb's last step, thrown out in a random direction
// (inside the unit ball, normalised) by 0.8 of the orb's size.
int EmitSilence(ParticleSource& s, Particle& p, int) {
    if (std::ssize(s.particles) <= 1) {
        float& angle = p.velocity.x;       // around the source
        float& angleStep = p.velocity.y;
        float& tilt = p.aux.x;             // of the circle
        float& tiltStep = p.aux.y;
        float& orbitRadius = p.aux.z;      // kept while the carrier is gone
        p.color = 0xFFFFFF;
        p.frame = 6;
        p.pos = s.origin;
        p.size = 0.0f;
        float start = !s.particles.empty() ? 0.0f : 3.1416f;    // 0 / pi
        angle = start;
        tilt = start;
        angleStep = 0.1f;
        tiltStep = 0.12f;
        orbitRadius = 1.0f;
        p.state = 1;
        return 1;
    }
    float& wispSize = p.velocity.x;
    auto node = s.particles.begin();
    if (g_particleRandom.Next() & 1) ++node;
    const Particle* orb = *node;
    float t = g_particleRandom.Range(0.0f, 1.0f);
    Vector3 start = orb->pos - t * orb->drift;
    Vector3 v = RandomInBall();
    p.size = 0.0f;
    p.pos = v * orb->size * 0.8f + start;
    wispSize = (g_particleRandom.Below(0.4f) + 0.8f) * s.size * 0.08f;
    p.color = 0xFFFFFFFF;
    p.frame = 6;
    p.state = 0;
    p.life = 7;
    return 1;
}

// Silence: once the source stops emitting (or loses its carrier) the orbs shrink the source by 0.85 per tick,
// everything going below 0.02. An orb circles the source on a tilted circle (radius: half the carrier's), half a
// radius up, keeping its step in drift and flickering in size. A wisp shows its size (shrinking by 0.8) for its life.
int UpdateSilence(ParticleSource& s, Particle& p) {
    if (!(s.flags & ParticleSourceFlag::Emitting) || !s.attachedTo) {
        if (p.state) s.size = s.size * 0.85f;
        if (s.size < 0.02f) return 0;
    }
    if (!p.state) {
        float& wispSize = p.velocity.x;
        if (--p.life <= 0) return 0;
        float shrunk = wispSize * 0.8f;
        p.size = wispSize;
        wispSize = shrunk;
        return 1;
    }
    float& angle = p.velocity.x;
    const float& angleStep = p.velocity.y;
    float& tilt = p.aux.x;
    const float& tiltStep = p.aux.y;
    float& orbitRadius = p.aux.z;
    float radius;
    if (s.attachedTo) {
        radius = s.attachedTo->bounds.radius * 0.5f;
        orbitRadius = radius;
    } else {
        radius = orbitRadius;
    }
    float c = lib::Cos(angle);
    p.color = 0xFFFFFFFF;
    float sn = lib::Sin(angle);
    Vector3 circle = {c, sn, lib::Sin(tilt) * 0.5f};
    Vector3 next = circle * (radius / std::sqrt(LengthSquared(circle))) + s.origin;
    next.z = radius * 0.5f + next.z;
    p.drift = next - p.pos;
    p.pos = next;
    angle = angleStep + angle;
    tilt = tiltStep + tilt;
    p.size = (g_particleRandom.Below(0.2f) + 0.9f) * s.size * 0.22f;
    return 1;
}

// --- Shower1-4: sparks thrown out in a few random directions -----------------------------------------------

namespace {

constexpr std::int32_t kShowerAlpha[16] = /*0078A6B4*/ {0, 64, 128, 192, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

// alpha * 8 random unit directions (at least one) in the elements.
void ChooseShowerRays(ParticleSource& s) {
    s.rayCount = std::lround(s.alpha * 8.0f);
    if (s.rayCount < 1) s.rayCount = 1;
    int rays = s.rayCount;
    s.emitCount = rays * 10;
    s.elements.resize(rays);
    for (int i = 0; i < s.rayCount; ++i) {
        float lengthSquared;
        Vector3 v = RandomInBall(lengthSquared);
        s.elements[i].pos = v / std::sqrt(lengthSquared);
        s.elements[i].ray.age = 0;
    }
}

} // namespace

// Shower1-4 (the RainSnow texture): the element's ticks pick the frame; its age waits for the directions.
void SetupShower(ParticleSource& s) {
    s.elements[0].ray.age = -100;
    switch (ParticleTypeOf(s.type)) {
    case ParticleType::Shower1: s.elements[0].ticks = 12; break;
    case ParticleType::Shower2: s.elements[0].ticks = 13; break;
    case ParticleType::Shower3: s.elements[0].ticks = 14; break;
    case ParticleType::Shower4: s.elements[0].ticks = 15; break;
    default: break;
    }
}

// Shower: the first call chooses the directions. A spark leaves the source along a
// random direction (jittered by +-0.03, scaled to the source's size and 1/6 of it per tick, one in
// eight half as fast again), already a random part of its first step out.
int EmitShower(ParticleSource& s, Particle& p, int) {
    float& speedKept = p.velocity.x;   // the speed's factor per tick
    float& sizeKept = p.velocity.y;    // the size's factor per tick
    if (s.elements[0].ray.age < 0) ChooseShowerRays(s);
    p.color = 0xFFFFFFFF;
    p.life = s.lifetime;
    p.frame = s.elements[0].ticks;
    Vector3 jitter = {g_particleRandom.Range(-0.03f, 0.03f), g_particleRandom.Range(-0.03f, 0.03f),
                      g_particleRandom.Range(-0.03f, 0.03f)};
    const ParticleElement& ray = s.elements[g_particleRandom.Next() % static_cast<std::uint32_t>(s.rayCount)];
    p.pos = s.origin;
    p.size = s.typeParam;
    Vector3 d = s.size * (jitter + ray.pos);
    float speed = 1.0f / std::sqrt(LengthSquared(d)) * s.size * 0.16666667f;   // 1/6
    if ((g_particleRandom.Next() & 7) == 0) speed = speed * 1.5f;
    p.drift = speed * d;
    speedKept = 0.9f;
    sizeKept = 0.8f;
    float t = g_particleRandom.Range(0.0f, 1.0f);
    p.pos += t * p.drift;
    return 1;
}

// Shower sparks move, slow down, fall by 0.05 size per tick, shrink and fade by the life table.
int UpdateShower(ParticleSource& s, Particle& p) {
    const float& speedKept = p.velocity.x;
    const float& sizeKept = p.velocity.y;
    int life = --p.life;
    if (life == 0) return 0;
    p.pos += p.drift;
    p.color = (static_cast<std::uint32_t>(ByLife(kShowerAlpha, life, 0)) << 24) | 0xFFFFFF;   // before: AcidRay's size table's zero tail
    p.size = sizeKept * p.size;
    float keep = speedKept;
    p.drift *= keep;
    p.drift.z -= s.size * 0.05f;
    return 1;
}

// Shower directions age; after 5 ticks the source stops emitting and lets go of its carrier.
void TickShower(ParticleSource& s, ParticleElement& e) {
    if (++e.ray.age > 5) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        s.Attach(nullptr);
    }
}

// --- Bag: eighty coloured balls bouncing around the carrier ------------------------------------------------

namespace {

// A ball's size factor by its age: 0.93 to the power of the age.
constexpr std::array<float, 12> kBagSizes = [] {
    std::array<float, 12> sizes = {};
    float t = 1.0f;
    for (float& s : sizes) {
        s = t;
        t *= 0.93f;
    }
    return sizes;
}();

// A ball's r, g, b by its age.
constexpr std::int32_t kBagColours[36] = {
    251, 254, 143, 252, 227, 149, 252, 201, 154, 255, 176, 154, 255, 155, 163, 252, 152, 176,
    250, 150, 195, 247, 149, 212, 243, 147, 237, 239, 144, 249, 239, 144, 249, 239, 144, 249,
};

// A spark's alpha by its life; a ball's by its life up to 9.
constexpr std::int32_t kBagAlpha[16] = {0, 64, 112, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

constexpr int kBagGone = -13;

std::uint32_t BagRgb(int kind) {
    const std::int32_t* c = kBagColours + kind * 3;
    return ((static_cast<std::uint32_t>(c[0]) << 8 | static_cast<std::uint32_t>(c[1])) << 8) | static_cast<std::uint32_t>(c[2]);
}

// A random value of the throw box (-0.2 * size to 0.2 * size).
float BagSpread(const ParticleSource& s, float& low, float& high) {
    high = s.size * 0.2f;
    low = s.size * -0.2f;
    return g_particleRandom.Range(0.0f, high - low);
}

} // namespace

// A ball's ticks run down to its next throw (-13: no carrier, the ball is gone).

// Bag: the first kBagBalls particles are the balls (state = their index); later ones are sparks along a random
// ball's last step, in the ball's colour.
int EmitBag(ParticleSource& s, Particle& p, int) {
    if (std::ssize(s.particles) < kBagBalls) {
        std::uint32_t r = g_particleRandom.Next();
        p.state = lib::Count(s.particles);
        p.color = 0xFFFFFF;
        p.life = 15;
        p.frame = static_cast<int>(r & 3);
        p.pos = s.origin;
        p.size = 0.0f;
        return 1;
    }
    const ParticleElement& ball = s.elements[g_particleRandom.Next() % kBagBalls];
    int age = ball.ball.age;
    p.color = BagRgb(age);
    int life = ball.ticks + 1;
    if (life > 5) life = 5;
    p.life = life;
    p.frame = 4;
    p.state = -1;
    float u = g_particleRandom.Range(-1.0f, 0.0f);
    p.pos = u * ball.ball.step + ball.pos;
    p.size = kBagSizes[age] * s.size * 0.1f;
    return 1;
}

// Bag: a spark fades by its life and steps a frame; a ball follows its element (dropped
// once the ball is gone), cycles 4 frames, takes the colour and size of its age, and while the
// ball rests (no last step) its previous position is pinned to the current one.
int UpdateBag(ParticleSource& s, Particle& p) {
    int state = p.state;
    if (state < 0) {
        int life = --p.life;
        if (life <= 0) return 0;
        ++p.frame;
        p.color = (static_cast<std::uint32_t>(kBagAlpha[life]) << 24) + (p.color & 0xFFFFFF);
        return 1;
    }
    ParticleElement* ball = &s.elements[state];
    if (ball->ticks == kBagGone) return 0;
    p.frame = (p.frame + 1) & 3;
    p.pos = ball->pos;
    int age = ball->ball.age;
    p.size = kBagSizes[age] * s.size * 0.15f;
    int fade = p.life > 9 ? 9 : p.life;
    p.color = (static_cast<std::uint32_t>(kBagAlpha[fade]) << 24) | BagRgb(age);
    const Vector3& step = ball->ball.step;
    if (step.x == 0.0f && step.y == 0.0f && step.z == 0.0f) {
        p.prevPos = p.pos;
        p.prevSize = p.size;
        p.prevColor = p.color;
        p.size = s.size * 0.15f;
    }
    return 1;
}

// Bag balls: when a ball's countdown runs out it is thrown again from the source (a random kind 0-2, a random
// offset, a random velocity of 0.2 size) and rests 7-9 ticks; while flying it ages, now and then gets a
// random kick, and moves (its step is kept). Without a carrier the ball is gone.
void TickBag(ParticleSource& s, ParticleElement& e) {
    if (--e.ticks < 0) {
        if (!s.attachedTo) {
            e.ticks = kBagGone;
            return;
        }
        e.ball.age = static_cast<int>(g_particleRandom.Next() % 3);
        e.pos = s.origin;
        float low, high;
        float r = BagSpread(s, low, high);
        e.pos.x = r + e.pos.x + low;
        r = BagSpread(s, low, high);
        e.pos.y = r + low + e.pos.y;
        r = BagSpread(s, low, high);
        e.pos.z = r + e.pos.z + low;
        e.ball.velocity = RandomInBall() * s.size * 0.2f;
        e.ticks = static_cast<int>(g_particleRandom.Next() % 3) + 7;
        e.ball.step = {};
        return;
    }
    ++e.ball.age;
    if (g_particleRandom.Next() % 6 == 0) {
        e.ball.velocity += RandomInBall() * s.size * 0.05f;
    }
    const Vector3& velocity = e.ball.velocity;
    e.pos += velocity;
    e.ball.step = velocity;
}
