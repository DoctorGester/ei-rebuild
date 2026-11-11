// Particle effects of blasts: each effect's set-up, emission, update and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/effects/particle_common.h"

#include "game/core/core_maths.h"
#include "game/scene/camera.h"
#include "game/effects/effects.h"
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

// --- FireBlast: fifteen fragments flying out, flames and smoke at the centre -------------------------------

namespace {

constexpr int kBlastCore = -1222;     // the core flash, emitted once at the start

constexpr std::int32_t kBlastAlpha[14] = /*0078A5DC*/ {255, 255, 207, 175, 128, 128, 128, 128, 128, 175, 207, 192, 128, 48};   // by frame

// A flame's size and frame timing: the size from the fragments' radius, the frame step random.
void BlastFlameTiming(ParticleSource& s, Particle& p) {
    float& flameSize = p.aux.x;
    float& framePosition = p.aux.y;
    float jitter = g_particleRandom.Below(0.4f) + 0.8f;
    flameSize = s.elements[0].fragment.radius * s.size * jitter * 0.6f;
    framePosition = static_cast<float>(p.frame);
}

} // namespace

// FireBlast: the fifteen fragments, each flying in a random direction (a point in the unit ball shifted up by 0.7,
// ten tries) with a random speed.
void SetupFireBlast(ParticleSource& s) {
    for (int i = 0; i < std::ssize(s.elements); ++i) {
        s.elements[i].pos = {};
        float x, y, z;
        float lengthSquared;
        for (int tries = 10;;) {
            x = g_particleRandom.Range(-1.0f, 1.0f);
            y = g_particleRandom.Range(-1.0f, 1.0f);
            z = g_particleRandom.Range(-1.0f, 1.0f) + 0.7f;
            lengthSquared = LengthSquared(Vector3{x, y, z});
            if (lengthSquared < 1.0f) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        float length = std::sqrt(lengthSquared);
        Vector3 v = {x, y, z};
        Vector3 direction = v / length;
        float speed = g_particleRandom.Below(0.4f) + 0.8f;
        const Vector3 velocity = direction * speed;
        auto& fragment = s.elements[i].fragment;
        fragment.velocity = velocity * 0.8f;
        fragment.age = 0;
        fragment.radius = 0.0f;
        fragment.growth = 0.4f;
        fragment.decay = 0.7f;
        fragment.frame = WordToFloat(g_particleRandom.Next() % 6);
        fragment.sizeFactor = WordToFloat(g_particleRandom.Next() & 1);
    }
}

// FireBlast: a random kind per particle (state = a draw % 29 - 14). The first particle
// after the fragments' first tick is the core flash. Negative kinds are flames inside the blast
// (a random point in the ball scaled by the radius, moving outwards; the earlier in the blast and
// the further out the more frames they skip); the others are smoke along fragment `state`'s path.
int EmitBlast(ParticleSource& s, Particle& p, int) {
    float& frameStep = p.aux.z;    // a flame's: added to its frame position per tick
    ParticleElement* fragments = s.elements.data();
    int kind = static_cast<int>(g_particleRandom.Next() % 29) - 14;
    p.color = 0xFFFFFFFF;
    p.state = kind;
    if (s.particles.empty() && fragments->fragment.age == 1) {
        p.state = kBlastCore;
        p.pos = s.origin;
        p.frame = 15;
        p.size = 0.0f;
        return 1;
    }
    if (kind < 0) {
        p.life = 0;
        Vector3 v = RandomInBall();
        float outside = 1.0f - LengthSquared(v);
        v *= s.size;
        p.pos = v * s.elements[0].fragment.radius + s.origin;
        p.velocity = v * s.elements[0].fragment.growth;
        p.size = 0.0f;
        int age = fragments->fragment.age;
        float threshold = (age / 7) + 0.2f;
        if (outside < threshold) {
            p.frame = 8;
            BlastFlameTiming(s, p);
            frameStep = g_particleRandom.Range(0.35f, 0.7f);
        } else {
            p.frame = static_cast<int>(g_particleRandom.Next() % 3);
            BlastFlameTiming(s, p);
            float t = g_particleRandom.Range(0.0f, 0.35f) * 0.5f;
            frameStep = t + outside * 0.35f / threshold + 0.175f;
        }
        return 1;
    }

    p.frame = static_cast<int>(g_particleRandom.Next() % 3);
    ParticleElement* e = &fragments[kind];
    if (e->fragment.frame == 0.0f) p.frame = 8;
    float u = g_particleRandom.Range(0.0f, 1.0f);
    p.pos = e->pos + s.origin - u * e->fragment.velocity * s.size;
    float g = g_particleRandom.Range(0.7f, 1.5f);
    float sized = g * (0.3f - u * 0.15f) *
                   (e->fragment.radius * 0.14285715f + 0.3f);   // 1/7
    p.size = sized * e->fragment.sizeFactor * s.size;
    p.velocity = e->fragment.velocity * s.size;
    return 1;
}

// FireBlast particles. Smoke (state >= 0) drifts with a 0.7 drag, shrinks to 0.8 and lasts until its frame
// reaches 7 (mod 8). The core flash (frame 15) grows once to 2.3 times the source's size, then disappears; it
// keeps the source alive while the fragments are younger than 3 ticks. Flames move by their velocity (slowing
// with the fragments' drag), swell, step through frames at an accelerating rate and fade by the frame.
int UpdateBlast(ParticleSource& s, Particle& p) {
    float& flameSize = p.aux.x;
    float& framePosition = p.aux.y;
    float& frameStep = p.aux.z;
    ParticleElement* fragments = s.elements.data();
    int state = p.state;
    if (state >= 0) {
        int frame = ++p.frame;
        if ((frame & 7) == 7) return 0;
        p.color = 0xFFFFFFFF;
        p.pos += p.velocity;
        p.velocity *= 0.7f;
        p.size *= 0.8f;
        return 1;
    }
    int frame = p.frame;
    if (frame >= 14) {
        if (state != kBlastCore) return 0;
        if (p.size == 0.0f) p.size = s.size * 2.3f;
        else p.size = 0.0f;
        return fragments->fragment.age < 3;
    }
    p.pos += p.velocity;
    ++p.life;
    p.size = flameSize;
    p.size = flameSize * (frame > 7 ? 1.03f : 1.01f);
    framePosition += frameStep;
    frameStep *= 1.02f;
    p.frame = std::lround(framePosition);
    flameSize = p.size;
    float drag = s.elements[0].fragment.decay - 0.03f;
    p.velocity *= drag;
    // A flame's frame can step past the table, up to 16: alpha 0 (the original read its block's zero tail, then
    // AcidRay's spread table's first 0.0).
    p.color = (static_cast<std::uint32_t>(p.frame < std::ssize(kBlastAlpha) ? kBlastAlpha[p.frame] : 0) << 24) | 0xFFFFFF;
    return 1;
}

// FireBlast fragments: the flame radius grows (its growth decaying), the age counts up
// (the emission stops after 6 ticks), the fragment flies by its velocity scaled by the source's
// size, slowing to 3/4 per tick and falling 0.03 per tick.
void TickBlast(ParticleSource& s, ParticleElement& e) {
    float& radius = e.fragment.radius;
    float& growthRate = e.fragment.growth;
    radius = radius + growthRate;
    int age = ++e.fragment.age;
    growthRate = e.fragment.decay * growthRate;
    if (age > 6) s.flags &= ~ParticleSourceFlag::Emitting;
    Vector3& velocity = e.fragment.velocity;
    e.pos += s.size * velocity;
    velocity *= 0.75f;
    velocity.z -= 0.03f;
}

// --- LightningBlast: a bolt of ten points, bursts and sparks -----------------------------------------------

namespace {

constexpr float kTwoPi = 6.28318f;

// One point of a new bolt: a random offset (none for point 0) around the source, on the ground,
// with a fork direction.
void PlaceBoltPoint(ParticleSource& s, int i, Vector3& dir) {
    float radius = g_particleRandom.Range(0.0f, 1.0f);
    float angle = g_particleRandom.Range(0.0f, kTwoPi);
    dir = {lib::Sin(angle) * radius, lib::Cos(angle) * radius, 0.0f};
    if (i == 0) dir = {};
    Vector3 point = s.size * dir * 0.3f + s.origin;
    point.z = g_clientWorld.RawSurfaceHeightAt(point.x, point.y) + 0.3f;
    ParticleElement* points = s.elements.data();
    points[i].pos = point;
    points[i].bolt.base = point;

    float inverse = 1.0f / std::sqrt(LengthSquared2D(dir.y, dir.x) + 0.25f);
    dir *= inverse;
    dir.z = inverse * 0.5f;
    points[i].bolt.fork = dir * 0.5f;
    points[i].bolt.emitted = 0;
    points[i].ticks = 0;   // bursts
}

// Particles along the bolt: two per point (the odd ones flash with a random size and frame).
int EmitAlongBolt(ParticleSource& s, Particle& p, ParticleElement* points) {
    int& flashes = p.frameTimer;
    float& sparkSize = p.aux.x;
    int emitted = points[0].bolt.emitted;
    p.frame = 0;
    p.life = 10;
    int index = emitted / 2;
    flashes = points[0].bolt.emitted & 1;
    p.state = index + 1;
    p.pos = points[index].pos;
    p.size = 0.0f;
    sparkSize = 0.1f;
    ++points[0].bolt.emitted;
    if (flashes == 0) return 1;
    sparkSize = g_particleRandom.Range(0.5f, 1.4f) * s.size * 0.2f;
    p.frame = static_cast<int>((g_particleRandom.Next() & 3) + 12);
    if (index == 0) sparkSize = s.size * 0.4f;
    p.size = sparkSize;
    return 1;
}

// A burst at the impact point: a random direction inside the unit disc (ten tries), spread by a
// random angle and distance.
int EmitBurst(ParticleSource& s, Particle& p, ParticleElement* points) {
    int& jumps = p.frameTimer;
    float& sparkSize = p.aux.x;
    p.color = 0x40FFFFFF;
    p.frame = 8;
    g_particleRandom.Next();   // draw kept
    float u = g_particleRandom.Range(0.0f, 1.0f);
    float spread = 1.0f - u * u;
    float angle = g_particleRandom.Range(0.0f, 6.28f);
    // A random point of the unit disc (ten tries) that nothing uses; draws kept.
    for (int tries = 10;;) {
        float vx = g_particleRandom.Range(-1.0f, 1.0f);
        float vy = g_particleRandom.Range(-1.0f, 1.0f);
        if (LengthSquared2D(vy, vx) < 1.0f) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    p.drift = {lib::Cos(angle) * spread, lib::Sin(angle) * spread, 0.0f};
    p.drift *= s.size * 0.3f;
    p.drift.z = 0.0f;
    p.pos.x = points[0].pos.x + p.drift.x;
    p.pos.y = points[0].pos.y + p.drift.y;
    float rise = (1.0f - spread) * s.size * 0.1f + points[0].pos.z;
    p.size = 0.0f;
    p.pos.z = rise;
    jumps = 0;
    p.life = std::lround(9.0f - spread * 3.0f);
    float roll = g_particleRandom.Range(0.1f, 0.14f);
    sparkSize = roll * (2.0f - spread) * s.size;
    return 1;
}

// A spark between a random point and its base, at a random fraction of the way, that jumps there.
int EmitSpark(ParticleSource& s, Particle& p) {
    int& jumps = p.frameTimer;
    float& sparkSize = p.aux.x;
    Vector3& target = p.velocity;
    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(s.elements));
    std::uint32_t index = g_particleRandom.Next() % count;
    p.frame = 1;
    sparkSize = g_particleRandom.Range(0.0f, 0.01f) * 3.0f + 0.06f;
    p.life = 6;
    jumps = 1;
    float u = g_particleRandom.Range(0.0f, 1.0f);
    float w = 1.0f - u;
    ParticleElement* points = s.elements.data();
    p.pos = points[index].pos;
    p.size = 0.0f;
    const ParticleElement& n = s.elements[index];
    target = u * n.pos + w * n.bolt.base;
    return 1;
}

constexpr std::int32_t kLightningAlpha[16] = /*0078A9F8*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};   // by life

} // namespace

// LightningBlast: on the first emission the bolt's ten points are placed; then the
// particles run along the bolt (two per point), then bursts at the impact point (up to five, while
// the source has fewer than 200 particles), then sparks between the points.
int EmitLightning(ParticleSource& s, Particle& p, int) {
    if (s.elements.empty()) {
        s.elements.resize(10);
        Vector3 dir;
        for (int i = 0; i < std::ssize(s.elements); ++i) PlaceBoltPoint(s, i, dir);
    }
    ParticleElement* points = s.elements.data();
    p.color = 0xFFFFFFFF;
    if (points[0].bolt.emitted < std::ssize(s.elements) * 2) return EmitAlongBolt(s, p, points);
    p.state = 0;
    if (std::ssize(s.particles) < 200 && points[0].ticks < 5) return EmitBurst(s, p, points);
    return EmitSpark(s, p);
}

// LightningBlast: bolts (state n) ride element n-1; a flash (state set, flashes) shows once (and makes the source
// emit 50 per tick), then goes. Sparks (state 0): alpha by the life left (the table shifted by 22, a quirk kept),
// shrinking by 0.9; the jumping ones go to their target and step a frame, the others cycle four frames flying by
// drift (slowing by 0.6).
int UpdateLightning(ParticleSource& s, Particle& p) {
    float& sparkSize = p.aux.x;
    int life = --p.life;
    if (life < 0) return 0;
    if (p.state != 0) {
        const int& flashes = p.frameTimer;
        if (flashes != 0) {
            if (p.size == 0.0f) {
                s.emitCount = 50;
                p.size = sparkSize;
                return 1;
            }
            return 0;
        }
        p.pos = s.elements[p.state - 1].pos;
        p.size = sparkSize;
        return 1;
    }
    const int& jumps = p.frameTimer;
    const Vector3& target = p.velocity;
    p.color = (static_cast<std::uint32_t>(kLightningAlpha[life]) << 22) | 0xFFFFFF;
    if (jumps != 0) {
        float shrunk = sparkSize * 0.9f;
        ++p.frame;
        p.pos = target;
        p.size = sparkSize;
        sparkSize = shrunk;
        return 1;
    }
    p.frame = (p.frame & ~3) | ((p.frame + 1) & 3);
    p.pos += p.drift;
    p.size = sparkSize;
    sparkSize *= 0.9f;
    float slow = 0.6f;
    p.drift *= slow;
    return 1;
}

// LightningBlast bolt heads: the source emits 50 per tick; each head remembers its position (bolt.base),
// moves by bolt.fork, falls (its z speed dropping by 0.075), and the source stops from the tenth tick on.
void TickLightning(ParticleSource& s, ParticleElement& e) {
    s.emitCount = 50;
    int tick = ++e.ticks;
    e.bolt.base = e.pos;
    e.pos += e.bolt.fork;
    e.bolt.fork.z -= 0.075f;
    if (tick >= 10) s.flags &= ~ParticleSourceFlag::Emitting;
}

// --- Mushroom and Transform: the cloud of the mushroom -----------------------------------------------------

namespace {

constexpr float kMushroomWidth = 0.2f;

constexpr float kMushroomRise = 0.12f;

} // namespace

// Mushroom: a puff from the source in a random upward direction (a fixed spread of 0,
// as ln 1 = 0), sized and framed by the cloud's age.
int EmitMushroom(ParticleSource& s, Particle& p, int) {
    if (s.elements.empty()) {
        s.elements.resize(1);
        ParticleElement* e = s.elements.data();
        e->ticks = 0;
        e->mushroom.spread = lib::Log(1.0f) * 0.2f * s.size;
        e->mushroom.capHeight = 0.0f;
        e->mushroom.stem = s.origin;
    }
    float spread = (g_particleRandom.Below(0.4f) + 0.8f) * s.elements[0].mushroom.spread;
    Vector3 d = {g_particleRandom.Range(-1.0f, 1.0f), g_particleRandom.Range(-1.0f, 1.0f), g_particleRandom.Range(0.1f, 1.0f)};
    d = 1.0f / std::sqrt(LengthSquared(d)) * d;
    d.z += 0.12f;
    p.pos = d * spread + s.origin;
    p.pos.z += s.elements[0].mushroom.capHeight;
    p.size = s.elements[0].ticks * 0.008f + 0.1f;
    p.drift = d * s.size * 0.06f;
    p.size = s.size * p.size;
    p.life = static_cast<int>(g_particleRandom.Next() & 3) + 16;
    int bump = static_cast<int>(g_particleRandom.Next() % 3);
    p.frameTimer = 2;
    p.color = 0x40FFFFFF;
    int frame = s.elements[0].ticks / 10 + bump - 1;
    p.frame = frame < 0 ? 0 : frame;
    return 1;
}

// Mushroom puffs: step a frame every other tick up to frame 14 (swelling 10% per tick
// before frame 8), fade in by 64 per tick then out by 63 over the last 4 ticks, drift with a 0.7
// drag, and while below the cap's height get pulled towards the stem.
int UpdateMushroom(ParticleSource& s, Particle& p) {
    int life = --p.life;
    if (life == 0) return 0;
    if (p.frame < 14) {
        int frame = p.frame;
        if (--p.frameTimer == 0) {
            p.frameTimer = 1;
            p.frame = frame + 1;
        }
        if (p.frame < 8) p.size = p.size * 1.1f;
    }
    std::uint32_t color = p.color;
    if (life > 4) {
        std::uint32_t a = (color >> 24) + 0x40;
        if (a > 0xFF) a = 0xFF;
        color = a << 24;
    } else {
        color = (color & 0xFF000000u) - 0x3F000001u;
    }
    p.color = color | 0xFFFFFF;
    p.pos += p.drift;
    p.drift *= 0.7f;
    const Vector3& stem = s.elements[0].mushroom.stem;
    float dx = p.pos.x - stem.x;
    float dy = p.pos.y - stem.y;
    float reach = std::sqrt(LengthSquared2D(dy, dx));
    float dz = p.pos.z - stem.z;
    if (s.elements[0].mushroom.capHeight > dz) {
        p.drift.x -= dx * s.size * 0.02f / reach;
        p.drift.y -= dy * s.size * 0.02f / reach;
    }
    return 1;
}

// Mushroom cloud: the cap widens with ln(ticks + 1) and rises; the Mushroom shakes the camera on
// its first tick; emission stops after 30 ticks.
void TickMushroom(ParticleSource& s, ParticleElement& e) {
    if (ParticleTypeOf(s.type) == ParticleType::Mushroom && e.ticks == 0)
        g_camera.Shake(s.origin, 5.0f, s.size, 0.25f);
    int ticks = ++e.ticks;
    e.mushroom.spread = lib::Log(static_cast<float>(ticks + 1)) * s.size * kMushroomWidth;
    e.mushroom.capHeight = s.size * kMushroomRise + e.mushroom.capHeight;
    if (ticks == 30) s.flags &= ~ParticleSourceFlag::Emitting;
}

// --- ClayRing: ten points running out from the source along the ground -------------------------------------

namespace {

// By the countdown; entries 12 and 13 (0): the first ticks (it starts at 13), past the 12 (the original's zero tail).
constexpr std::int32_t kClayAlpha[14] = /*0078A734*/ {0, 128, 255, 255, 255, 255, 255, 255, 255, 160, 80, 32, 0, 0};

// The cosine and sine of the turn a point's direction takes per tick (about 18 degrees).
constexpr float kClayTurnCos = 0.95f;

constexpr float kClayTurnSin = 0.31225f;

constexpr float kClayReach[16] = /*0078A6F4*/ {0.0f, 0.025f, 0.056f, 0.092f, 0.136f, 0.188f, 0.251f, 0.326f, 0.417f, 0.525f,
                                            0.656f, 0.812f, 1.0f, 1.0f, 1.0f, 1.0f};

// What the original read for kClayAlpha at a countdown of -1: kClayReach's last entry (alpha 0 once shifted).
constexpr std::int32_t kClayBefore = std::bit_cast<std::int32_t>(kClayReach[15]);

// The reach read backwards from entry 14 (for a positive size).
float ClayReachBackwards(int t) { return kClayReach[14 - t]; }

} // namespace

// ClayRing: the ten points on a circle around the source's (reset) position.
void SetupClayRing(ParticleSource& s) {
    for (int i = 0; i < std::ssize(s.elements); ++i) {
        float angle = i * 0.62832f;   // 2 pi / 10
        Vector3& direction = s.elements[i].clay.direction;
        direction.x = lib::Sin(angle);
        direction.y = lib::Cos(angle);
        direction.z = 0.0f;
        s.elements[i].pos = s.localPosition;
        s.elements[i].clay.previous = s.localPosition;
        s.elements[i].ticks = 14;
    }
}

// ClayRing: in the first tick (countdown 13) one marker per point; then clods between a
// random point's previous and current position, flying off at random.
int EmitClayRing(ParticleSource& s, Particle& p, int index) {
    Vector3& base = p.velocity;    // a clod's point on its path
    Vector3& scatter = p.aux;      // added to base, slowing
    p.color = (static_cast<std::uint32_t>(ByLife(kClayAlpha, s.elements[0].ticks, kClayBefore)) << 24) | 0xFFFFFF;
    p.life = s.lifetime;
    p.frame = 1;
    p.frameTimer = s.frameDelay;
    if (s.elements[0].ticks == 13 && index < std::ssize(s.elements)) {
        base = {};
        p.pos = s.elements[index].pos;
        p.size = 0.2f;
        p.frame = 0;
        p.state = index;
        return 1;
    }
    int j = static_cast<int>(g_particleRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements)));
    float t = g_particleRandom.Range(0.0f, 1.0f);
    p.life = 5;
    const ParticleElement& e = s.elements[j];
    const Vector3& previous = e.clay.previous;
    float u = 1.0f - t;
    base = t * e.pos + u * previous;
    float sx = g_particleRandom.Range(-0.1f, 0.1f);
    float sy = g_particleRandom.Range(-0.1f, 0.1f);
    float sz = g_particleRandom.Range(-0.1f, 0.1f);
    scatter.x = sy;
    scatter.y = sx;
    scatter.z = sz;
    p.pos = previous;
    p.size = 0.1f;
    return 1;
}

// ClayRing: markers stay on their point (alpha by the countdown); clods step through the
// frames, fly out (their scatter slowing by 0.9), fade by 48 per tick and shrink.
int UpdateClayRing(ParticleSource& s, Particle& p) {
    const Vector3& base = p.velocity;
    Vector3& scatter = p.aux;
    if (--p.life == 0) return 0;
    if (p.frame == 0) {
        p.color = (static_cast<std::uint32_t>(ByLife(kClayAlpha, s.elements[0].ticks, kClayBefore)) << 24) | 0xFFFFFF;
        p.pos = s.elements[p.state].pos;
        p.size = 0.2f;
        return 1;
    }
    if (--p.frameTimer == 0) {
        if (++p.frame >= s.frameCount) return 0;
        p.frameTimer = s.frameDelay;
    }
    scatter *= 0.9f;
    float oldSize = p.size;
    p.pos = base + scatter;
    std::int32_t a = static_cast<std::int32_t>(p.color >> 24) - 0x30;
    if (a < 0) a = 0;
    p.size = oldSize * 0.9f;
    p.color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
    return 1;
}

// ClayRing points: count down (stopping the emission past 0), remember the previous
// position, move out from the source along their direction by the reach table (read backwards for
// a non-negative size), sit on the ground, and turn their direction a little.
void TickClayRing(ParticleSource& s, ParticleElement& e) {
    int t = --e.ticks;
    if (t < 0) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    Vector3& previous = e.clay.previous;
    e.clay.previous = e.pos;
    Vector3& direction = e.clay.direction;
    float k = s.size >= 0.0f ? ClayReachBackwards(t) : kClayReach[t];
    e.pos = s.size * direction * k + s.origin;
    e.pos.z = g_clientWorld.RawSurfaceHeightAt(e.pos.x, e.pos.y) + 1.0f;
    float turnedX = direction.y * kClayTurnCos - direction.x * kClayTurnSin;
    float turnedY = direction.x * kClayTurnCos + direction.y * kClayTurnSin;
    direction.x = turnedY;
    direction.y = turnedX;
    if (previous.x == 0.0f && previous.y == 0.0f) e.clay.previous = e.pos;
}

// --- PoisonFog and PoisonCloud: a disc of cloud puffs laid on the ground -----------------------------------

namespace {

constexpr float kFogCell = 1.0f;   // the spacing of the puffs

constexpr float kCloudAlpha = 4.5f;

} // namespace

// PoisonFog: emits nothing itself. When the source has no particles it lays one puff
// per grid cell (jittered by a quarter cell) inside the disc of radius `size` around its position,
// on the ground, straight into its list (from the pool), and grows the bounds. Always 0.
int EmitPoisonFog(ParticleSource& s, Particle&, int) {
    if (!s.particles.empty()) return 0;
    float radius = s.size;
    int n = static_cast<int>((s.size + 1.0f) / kFogCell);
    for (int i = -n; i <= n; ++i) {
        float row = static_cast<float>(i);
        float radiusSquared = radius * radius;
        for (int j = -n; j <= n; ++j) {
            float x = j * kFogCell;
            float y = row * kFogCell;
            float high = kFogCell * 0.25f;
            float low = kFogCell * -0.25f;
            x = g_particleRandom.Range(low, high) + x;
            y = g_particleRandom.Range(low, high) + y;
            if (LengthSquared2D(y, x) >= radiusSquared) continue;
            Particle* q = s.Manager()->AcquireParticle();
            if (!q) continue;
            x += s.localPosition.x;
            y += s.localPosition.y;
            q->pos.x = x;
            q->pos.y = y;
            q->size = g_particleRandom.Range(1.0f, 1.5f) - 0.25f;
            q->pos.z = g_clientWorld.RawSurfaceHeightAt(x, y) + q->size;
            if (ParticleTypeOf(s.type) == ParticleType::PoisonCloud) {
                q->color = 0xFFFFAF7F;
            } else {
                int a = std::lround(s.alpha * 160.0f);
                if (a > 255) a = 255;
                q->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
            }
            q->life = s.lifetime;
            q->frame = static_cast<int>(g_particleRandom.Next() % static_cast<std::uint32_t>(s.frameCount));
            q->frameTimer = s.frameDelay;
            s.AddParticle(q);
        }
    }
    return 0;
}

// Clouds: cycle the texture frames; once the source stops emitting, raise the alpha byte (it wraps) until it is at
// most 9, then drop the particle.
int UpdateCloud(ParticleSource& s, Particle& p) {
    if (--p.frameTimer == 0) {
        if (++p.frame >= s.frameCount) p.frame = 0;
        p.frameTimer = s.frameDelay;
    }
    if (s.flags & ParticleSourceFlag::Emitting) return 1;
    std::uint32_t a = p.color >> 24;
    if (a <= 9) return 0;
    int step = std::lround(s.alpha * kCloudAlpha);
    if (step < 1) step = 1;
    std::uint32_t rgb = ParticleTypeOf(s.type) == ParticleType::PoisonCloud ? 0xFFAF7Fu : 0xFFFFFFu;
    p.color = ((static_cast<std::uint32_t>(step) * 255 + a) << 24) | rgb;
    return 1;
}
