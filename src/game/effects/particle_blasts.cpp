// Particle effects of blasts: each effect's set-up, emission, update and element tick.
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
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

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
        Vector3 v = effectdata::RandomInBall(g_particleRandom);
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
