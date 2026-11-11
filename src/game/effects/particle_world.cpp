// Particle effects of the world (fires, smoke, vents, the tornado, portals and markers): each effect's set-up,
// emission, update and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/effects/particle_common.h"

#include "game/core/core_maths.h"
#include "game/scene/camera.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "game/scene/light.h"
#include "game/objects/object3d.h"
#include "game/scene/scene_renderer.h"
#include "game/core/random.h"
#include "game/spells/spell.h"
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

// --- Geyser: three vents that erupt in turn ----------------------------------------------------------------

namespace {

// A random value in [low, high) of the spread box component, added to `base` (the original's order: the
// random part, the base, then the low end).
float SpreadAround(float base, float low, float high) {
    return g_particleRandom.Range(0.0f, high - low) + base + low;
}

Vector3 SpreadAround(const Vector3& base, const Vector3& low, const Vector3& high) {
    return {SpreadAround(base.x, low.x, high.x), SpreadAround(base.y, low.y, high.y),
            SpreadAround(base.z, low.z, high.z)};
}

} // namespace

// Geyser: the first particle marks the source. Then a drop from a random vent: while
// the vent erupts it flies along the vent's direction (jittered by up to 20 / 10 degrees) with the
// vent's strength; otherwise a short-lived puff of mist near the source.
int EmitGeyser(ParticleSource& s, Particle& p, int) {
    float& dropSize = p.velocity.x;
    int vent = static_cast<int>(g_particleRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements)));
    p.color = 0xFFFFFFFF;
    p.pos = s.origin;
    if (s.particles.empty()) {
        p.size = 0.0f;
        p.state = 1;
        p.frame = 15;
        return 1;
    }
    p.state = 0;
    p.frame = static_cast<int>(g_particleRandom.Next() % 12);
    const ParticleElement& e = s.elements[vent];
    float drop = s.size * 0.2f;
    p.life = 20;
    p.size = drop;
    if (e.ticks > 0) {
        p.life = 0x7FFFFFFF;
        float radian = ei::kRadiansPerDegree;
        float yaw = g_particleRandom.Range(-20.0f, 20.0f) * radian + e.vent.yaw;
        float tilt = g_particleRandom.Range(-10.0f, 10.0f) * radian + e.vent.tilt;
        float power = g_particleRandom.Range(0.0f, 0.2f) + e.vent.power - 0.1f;
        float sinTilt = lib::Sin(tilt);
        p.drift.x = lib::Sin(yaw) * sinTilt * power * s.height;
        p.drift.y = lib::Cos(yaw) * sinTilt * power * s.height;
        p.drift.z = lib::Cos(tilt) * power * s.height;
        p.pos = SpreadAround(p.pos, s.spreadMin, s.spreadMax);
        p.size = 0.0f;
        dropSize = drop;
        return 1;
    }
    p.life = 1;
    p.drift = {};
    float high = s.spreadMax.x * 0.5f;
    float low = s.spreadMin.x * 0.5f;
    p.pos.x = SpreadAround(p.pos.x, low, high);
    low = s.spreadMin.y * 0.5f;
    high = s.spreadMax.y * 0.5f;
    p.pos.y = SpreadAround(p.pos.y, low, high);
    p.size = s.size * 0.01f;
    return 1;
}

// Geyser: the marker flickers while the source emits (dropped once it stops); drops fly by drift
// (gravity pulling drift.z down by 0.06 height per tick), fade 8 alpha per tick (dropped below 10),
// shrink by 0.9 and cycle 4 frames.
int UpdateGeyser(ParticleSource& s, Particle& p) {
    float& dropSize = p.velocity.x;
    if (p.state != 0) {
        if (!(s.flags & ParticleSourceFlag::Emitting)) return 0;
        p.size = g_particleRandom.Range(0.03f, 0.3f) * s.size;
        return 1;
    }
    std::uint32_t a = p.color >> 24;
    if (a <= 9) return 0;
    if (--p.life <= 0) return 0;
    float shrunk = dropSize * 0.9f;
    dropSize = shrunk;
    p.pos += p.drift;
    int frame = p.frame;
    p.frame = (frame & ~3) | ((frame + 1) & 3);
    p.size = shrunk;
    p.color = ((a << 24) - 0x08000001u) | 0xFFFFFF;
    p.drift.z -= s.height * 0.06f;
    return 1;
}

// Geyser vents: an eruption counts down, then a pause of 3..36 ticks, then a new
// eruption of 1..7 ticks in a random direction (up to 10 degrees off vertical) and strength.
void TickGeyser(ParticleSource&, ParticleElement& e) {
    if (e.ticks > 0) {
        if (--e.ticks == 0) e.vent.pause = static_cast<int>(g_particleRandom.Next() % 34) + 3;
        return;
    }
    if (--e.vent.pause > 0) return;
    e.ticks = static_cast<int>(g_particleRandom.Next() % 7) + 1;
    float radian = ei::kRadiansPerDegree;
    float yaw = g_particleRandom.Range(0.0f, 360.0f) * radian;
    float tilt = g_particleRandom.Range(0.0f, 10.0f) * radian;
    float power = g_particleRandom.Range(0.4f, 1.0f);
    e.vent.yaw = yaw;
    e.vent.tilt = tilt;
    e.vent.power = power;
}

// --- Tornado and CurseHolder: a funnel of rings ------------------------------------------------------------

namespace {

constexpr float kFunnelSpans = 5.0f;   // the spans between the six rings

// The funnel's centre (x, y) at height h (0..1): between ring k and k+1 (h * 5 rings).
Vector2 FunnelCentre(const ParticleSource& s, float h) {
    int k = static_cast<int>(h);
    if (k >= lib::Count(s.elements) - 2) k = lib::Count(s.elements) - 2;
    float f = h - k;
    float t = MinKeepA(f, 1.0f);
    float u = 1.0f - t;
    const ParticleElement& a = s.elements[k];
    const ParticleElement& b = s.elements[k + 1];
    return t * XY(b.pos) + u * XY(a.pos);
}

// Tornado and CurseHolder: the six rings, bottom to top, stacked 2.4 apart.
void StackFunnelRings(ParticleSource& s) {
    for (int i = 0; i < std::ssize(s.elements); ++i) {
        ParticleElement& e = s.elements[i];
        e.pos = {0.0f, 0.0f, i * 12.0f * 0.2f};
        e.ticks = 0;
    }
}

} // namespace

// Tornado: the particles take the scene light's (the sun's) r, g, b with alpha 0x19.
void SetupTornado(ParticleSource& s) {
    const float* light = g_sceneRenderer.sun->color;
    std::uint32_t color = static_cast<std::uint32_t>(light[0] * 255.0f);
    color = (color | 0x1900) << 8;
    color |= static_cast<std::uint32_t>(light[1] * 255.0f);
    color <<= 8;
    color |= static_cast<std::uint32_t>(light[2] * 255.0f);
    s.tint = color;
    StackFunnelRings(s);
    s.elements[0].funnel.height = 12.0f;
    s.elements[0].funnel.bend = 0.5f;
    s.elements[0].funnel.damping = 0.99f;
}

void SetupCurseHolder(ParticleSource& s) {
    StackFunnelRings(s);
    s.elements[0].funnel.height = 12.0f * 0.4f;
    s.elements[0].funnel.bend = 0.0f;
    s.elements[0].funnel.damping = 0.997f;
}

// Tornado: a particle at a random height of the funnel (around the ring centres there),
// with a radius growing towards the top and a turn rate larger near the bottom; one in three is
// dust (state 0, frames 12-15), the others debris sized by their radius.
int EmitTornado(ParticleSource& s, Particle& p, int) {
    float& orbitAngle = p.velocity.x;      // around the funnel's centre
    float& orbitRadius = p.velocity.y;
    float& level = p.aux.x;                // the height up the funnel (0..1)
    float& opacity = p.aux.y;
    float& spin = p.drift.x;               // added to orbitAngle per tick
    float& radiusFactor = p.drift.y;       // orbitRadius's factor per tick
    float& climb = p.drift.z;              // added to level per tick
    float height01 = g_particleRandom.Range(0.0f, 1.0f);
    p.state = static_cast<int>(g_particleRandom.Next() % 3);
    p.color = s.tint;
    p.pos.x = s.origin.x;
    p.pos.y = s.origin.y;
    p.pos.z = s.origin.z + s.elements[0].pos.z;
    p.life = static_cast<int>(g_particleRandom.Next() % 9) + 100;
    float scaled = height01 * kFunnelSpans;
    int k = std::lround(scaled);
    if (k >= lib::Count(s.elements) - 2) k = lib::Count(s.elements) - 2;
    float f = scaled - k;
    float t = MinKeepA(f, 1.0f);
    float u = 1.0f - t;
    const ParticleElement& a = s.elements[k];
    const ParticleElement& b = s.elements[k + 1];
    float cx = t * b.pos.x + u * a.pos.x;
    float cy = u * a.pos.y + t * b.pos.y;
    float high = kPi;
    float low = -kPi;
    float angle = g_particleRandom.Range(low, high);
    opacity = 0.1f;
    level = height01;
    float w = 1.0f - height01;
    float w2 = w * w;
    float w4 = w2 * w2;
    orbitAngle = angle;
    float radius = g_particleRandom.Range(0.8f, 2.0f) * (w4 + 0.2f) * 2.0f;
    orbitRadius = radius;
    float turnHigh = kPi * 0.07f;
    float turnLow = kPi * 0.06f;
    float turn = g_particleRandom.Range(turnLow, turnHigh);
    spin = turn * (1.1f - w * w) * 1.5f;
    climb = 0.0f;
    radiusFactor = s.elements[0].funnel.damping;
    p.pos.x = lib::Sin(angle) * radius + p.pos.x + cx;
    p.pos.y = lib::Cos(angle) * radius + cy + p.pos.y;
    p.pos.z = height01 * s.elements[0].funnel.height + p.pos.z;
    if (p.state != 0) {
        p.size = (radius + 0.3f) * s.size;
        p.frame = static_cast<int>(g_particleRandom.Next() % 12);
    } else {
        p.frame = static_cast<int>(g_particleRandom.Next() & 3) + 12;
        p.size = s.size * 0.15f;
    }
    return 1;
}

// Tornado particles rise (dropped above the top), turn around the funnel's centre at
// their height (faster and faster), fade in, fade by the height, and animate.
int UpdateTornado(ParticleSource& s, Particle& p) {
    float& orbitAngle = p.velocity.x;
    float& orbitRadius = p.velocity.y;
    float& level = p.aux.x;
    float& opacity = p.aux.y;
    float& spin = p.drift.x;
    const float& radiusFactor = p.drift.y;
    float& climb = p.drift.z;
    if (--p.life == 0) return 0;
    level += climb;
    climb += 0.0001f;
    if (level > 1.0f) return 0;
    float scaled = level * kFunnelSpans;
    const Vector2 centre = FunnelCentre(s, scaled);
    orbitAngle += spin;
    spin *= 1.01f;
    float angle = orbitAngle;
    orbitRadius = radiusFactor * orbitRadius;
    float radius = orbitRadius;
    p.pos.x = lib::Sin(angle) * radius + s.origin.x + centre.x;
    p.pos.y = lib::Cos(angle) * radius + s.origin.y + centre.y;
    float base = s.elements[0].pos.z + s.origin.z;
    p.pos.z = base;
    p.pos.z = s.elements[0].funnel.height * level + base;
    if (p.life > 8) opacity = MinKeepA(opacity + 0.1f, 1.0f);
    else opacity = MaxKeepB(0.1f, opacity);
    int a = std::lround(std::sqrt(1.0f - level) * opacity * 255.0f);
    p.color = (p.color & 0xFFFFFF) | (static_cast<std::uint32_t>(a) << 24);
    if (p.state != 0) {
        p.frame = (p.frame + 1) % 12;
        p.size = (radius + 0.3f) * s.size;
    } else {
        p.frame = ((p.frame + 1) & 3) + 12;
    }
    return 1;
}

// Tornado rings. Without a bend the rings sway with the source's motion (damped 0.95, a half cosine up the funnel);
// with one they lean along the motion (at most 0.01 per tick) and bend with the height. The bottom ring sits 1 above
// the ground. A free source that lost its bend and still has particles stops and keeps going until they die.
void TickTornado(ParticleSource& s) {
    ParticleElement* bottom = s.elements.data();
    float bend = bottom->funnel.bend;
    if (bend == 0.0f) {
        Vector3 sway = bottom->pos - s.motion;
        sway.x *= 0.95f;
        sway.z = 0.0f;
        sway.y *= 0.95f;
        for (int i = 0; i < std::ssize(s.elements); ++i) {
            float c = lib::Cos(static_cast<float>(i) / static_cast<float>(std::ssize(s.elements) - 1) * kPi * 0.5f);
            s.elements[i].pos = sway * c;
        }
        s.elements[0].pos.z = g_clientWorld.RawSurfaceHeightAt(s.origin.x, s.origin.y) - s.origin.z + 1.0f;
    } else {
        s.elements[0].pos.z = g_clientWorld.RawSurfaceHeightAt(s.origin.x, s.origin.y) - s.origin.z + 1.0f;
        Vector3 n = (s.motion * 20.0f + s.elements[0].pos) * -1.0f;
        float length = std::sqrt(LengthSquared(n));
        if (length != 0.0f) n *= 1.0f / length;
        float lean = length * 0.5f;
        if (lean > 0.01f) lean = 0.01f;
        Vector3 g = n * lean + s.elements[0].pos;
        for (int i = 0; i < std::ssize(s.elements); ++i) {
            float f = static_cast<float>(i) / static_cast<float>(std::ssize(s.elements) - 1);
            float c = lib::Cos(f * kPi);
            float k = (c + (1.0f - f) * s.elements[0].funnel.bend + 1.0f) * 0.4f;
            s.elements[i].pos = g * k;
        }
    }
    if (!s.attachedTo) {
        float b = s.elements[0].funnel.bend;
        if (b == 0.0f && !s.particles.empty()) {
            s.Stop();
        }
    }
}

// --- ShapeChange: the glow of the path markers -------------------------------------------------------------

namespace {

constexpr float kGlowAlpha[16] = /*0078AA78*/ {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                            1.0f, 1.0f};

} // namespace

// ShapeChange: on the first call one element: the last point (-1, -1, -1) and the colour by the alpha field (0
// grey, 1 green, 2 orange). Then one glow per point of the carrying unit (up to typeParam points, offset by
// the client tick), on the ground or platform under it, unless the point is where the last one was; the
// point's index goes in frameTimer and the life is -10 (set by UpdateGlow).
int EmitGlow(ParticleSource& s, Particle& p, int index) {
    int& order = p.frameTimer;     // the point's index in the emission
    MapObject* unit = s.attachedTo;
    if (s.elements.empty()) {
        s.elements.resize(1);
        s.elements[0].glow.mark = {-1.0f, -1.0f, -1.0f};
        switch (s.glowKind) {
        case GlowKind::Grey: s.elements[0].glow.color = 0xB0B0B0; break;
        case GlowKind::Green: s.elements[0].glow.color = 0x40FF40; break;
        case GlowKind::Orange: s.elements[0].glow.color = 0xFF8028; break;
        }
    }
    if (!unit) return 0;
    if (index > s.typeParam) return 0;
    Vector3 point = unit->PositionAt(index + g_clientWorld.tick);
    Vector3& mark = s.elements[0].glow.mark;
    if (mark.x == point.x && mark.y == point.y && mark.z == point.z) return 0;
    s.elements[0].glow.mark = point;
    Vector3 under = {point.x, point.y, 0.0f};
    float ground = g_clientWorld.RawSurfaceHeightAt(point.x, point.y);
    float top = g_clientWorld.platforms.SurfaceHeight(under, ground);
    p.pos.y = point.y;
    p.size = 0.07f;
    p.pos.x = point.x;
    p.frame = 15;
    p.life = -10;
    p.pos.z = top + 0.15f;
    order = index;
    p.color = s.elements[0].glow.color;
    return 1;
}

// ShapeChange: on its first update (life -10) the particle's lifetime is set from its
// position in the emission (order * 8 / particle count + 7); the alpha follows kGlowAlpha.
int UpdateGlow(ParticleSource& s, Particle& p) {
    const int& order = p.frameTimer;
    if (p.life == -10) p.life = order * 8 / lib::Count(s.particles) + 7;
    if (--p.life < 0) return 0;
    int opacity = std::lround(kGlowAlpha[p.life] * 255.0f);
    p.color = (p.color & 0xFFFFFF) + (static_cast<std::uint32_t>(opacity) << 24);
    return 1;
}

// ShapeChange emits once: on the tick after the first emission made its element.
void TickGlow(ParticleSource& s) {
    if (!s.elements.empty()) s.flags &= ~ParticleSourceFlag::Emitting;
}
