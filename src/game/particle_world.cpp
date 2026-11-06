// Particle effects of the world (fires, smoke, vents, the tornado, portals and markers): each effect's set-up,
// emission, update and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/particle_common.h"

#include "game/angles.h"
#include "game/camera.h"
#include "game/core_maths.h"
#include "game/effects.h"
#include "game/game_singletons.h"
#include "game/light.h"
#include "game/map_object_figures.h"
#include "game/model.h"
#include "game/random.h"
#include "game/spell.h"
#include "game/unit.h"
#include "game/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

// --- CampFire, Fire, Smoke and Tendril ---------------------------------------------------------------------

namespace {

constexpr float kSmallPuffWiden = 0.3f;   // the smaller puffs' extra spread, times the size

constexpr float kSmokeGrowth = 1.06f;

constexpr float kSmokeDrift = 0.15f;

constexpr float kTendrilDensity = 14.285714f;

} // namespace

// A new smoke puff: a random offset in the source's spread (smaller puffs, 40% of them, spread
// wider), half of it applied at once, and a random height up to `height`.
int EmitSmoke(ParticleSource& s, Particle& p, int) {
    Vector3& spreadStep = p.velocity;  // half the puff's random offset: added again in its first two frames
    float& rise = p.drift.z;
    float extra = 0.0f;
    p.color = 0xFFFFFFFFu;
    p.life = s.lifetime;
    p.frame = ParticleTypeOf(s.type) != ParticleType::Smoke ? 0 : 8;
    p.frameTimer = s.frameDelay;
    p.size = s.size;
    if (g_clientRandom.Next() % 100 + 1 <= 40) {
        p.size = s.size * 0.5f;
        extra = s.size * kSmallPuffWiden;
    }
    float hi = extra + s.spreadMax.x;
    float lo = s.spreadMin.x - extra;
    spreadStep.x = g_clientRandom.Range(lo, hi);
    hi = extra + s.spreadMax.y;
    lo = s.spreadMin.y - extra;
    spreadStep.z = 0.0f;
    spreadStep.y = g_clientRandom.Range(lo, hi);
    spreadStep *= 0.5f;
    p.pos.x = s.origin.x + spreadStep.x;
    p.pos.y = s.origin.y + spreadStep.y;
    p.drift.x = 0.0f;
    p.drift.y = 0.0f;
    p.pos.z = g_clientRandom.Range(0.0f, s.height) + s.origin.z;
    rise = s.height;
    return 1;
}

// Smoke puffs: rise and spread for the first two frames, then grow; past frame 7 they drift with
// the wind and fire smoke dies (other smoke lives on, a quarter of the time each later frame).
int UpdateSmoke(ParticleSource& s, Particle& p) {
    const Vector3& spreadStep = p.velocity;   // in the first two frames
    float& rise = p.drift.z;
    if (--p.life == 0) return 0;
    if (--p.frameTimer == 0) {
        int frame = ++p.frame;
        if (frame >= s.frameCount) return 0;
        p.frameTimer = s.frameDelay;
        if (frame > 7) {
            if (ParticleTypeOf(s.type) == ParticleType::Fire || ParticleTypeOf(s.type) == ParticleType::Tendril) return 0;
            p.frameTimer = s.frameDelay << 2;
            if ((g_clientRandom.Next() & 3) == 0) return 0;
        }
        if (p.frame == 8) rise = rise * 0.5f;
    }
    int frame = p.frame;
    p.pos.z += rise;
    if (frame < 2) p.pos += spreadStep;
    if (frame < 7) {
        p.size = s.growth * p.size;
        return 1;
    }
    float oldSize = p.size;
    p.size = oldSize * kSmokeGrowth;
    float push = oldSize * kSmokeGrowth * s.windStrength * kSmokeDrift;
    p.pos += push * s.windDirection;
    return 1;
}

// A smoke puff (EmitSmoke), moved by one random amount in [-1, 1) times the extent in x
// and y, and set its own height above the ground there.
int EmitTendril(ParticleSource& s, Particle& p, int index) {
    if (!EmitSmoke(s, p, index)) return 0;
    float u = g_clientRandom.Range(-1.0f, 1.0f);
    p.pos.x = u * s.extent.x + p.pos.x;
    p.pos.y = u * s.extent.y + p.pos.y;
    float ground = g_clientWorld.RawSurfaceHeightAt(p.pos.x, p.pos.y);
    p.pos.z = p.pos.z - s.origin.z + ground;
    return 1;
}

// Tendril smoke: emission proportional to the length of `extent`, at least 6, at most a 16th of
// the particle limit.
void TickTendril(ParticleSource& s) {
    int n = static_cast<int>(Length(s.extent) * kTendrilDensity);
    s.emitCount = n;
    int count = n < 6 ? 6 : n;
    int cap = s.maxParticles / 16;
    s.emitCount = count < cap ? count : cap;
}

// --- VulcanSmoke: lava drops and smoke from a vent ---------------------------------------------------------

// VulcanSmoke: three in five are drops (state 8: 8 ticks per frame) thrown up at a random height in a random
// direction; the others smoke (state 4) at a random point of the vent, rising by the source's height.
int EmitVulkan(ParticleSource& s, Particle& p, int) {
    float& sizeFactor = p.velocity.x;  // per tick
    float& slowing = p.velocity.y;     // the drift's factor per tick
    p.color = 0xFFFFFFFF;
    p.frame = 0;
    float radian = ei::kRadiansPerDegree * 360.0f;
    if (g_clientRandom.Next() % 100 + 1 <= 60) {
        p.state = 8;
        p.life = s.lifetime * 8;
        p.frameTimer = 8;
        p.pos.x = s.origin.x;
        p.pos.y = s.origin.y;
        float low = s.spreadMin.y * -0.5f;
        float high = s.spreadMin.y * 0.5f;
        p.pos.z = g_clientRandom.Range(0.0f, high - low) + s.spreadMax.y + s.origin.z + low;
        p.size = s.size;
        sizeFactor = s.growth;
        slowing = 0.965f;
        float reach = s.spreadMin.x;
        p.drift.z = 0.0f;
        float angle = g_clientRandom.Range(0.0f, radian);
        p.drift.x = lib::Sin(angle) * reach;
        p.drift.y = lib::Cos(angle) * reach;
        return 1;
    }
    p.state = 4;
    p.life = s.lifetime * 4;
    p.frameTimer = 4;
    float reach = g_clientRandom.Range(0.0f, s.spreadMax.x);
    float angle = g_clientRandom.Range(0.0f, radian);
    p.pos.x = lib::Sin(angle) * reach + s.origin.x;
    p.pos.y = lib::Cos(angle) * reach + s.origin.y;
    p.pos.z = s.origin.z;
    p.size = s.size * 0.3f;
    sizeFactor = 1.02f;
    slowing = 1.0f;
    p.drift.x = 0.0f;
    p.drift.y = 0.0f;
    p.drift.z = s.height;
    return 1;
}

// VulcanSmoke: frames at the particle's own pace (state ticks per frame; dropped after the last), moving by drift,
// growing and slowing.
int UpdateVulkan(ParticleSource& s, Particle& p) {
    const float& sizeFactor = p.velocity.x;
    const float& slowing = p.velocity.y;
    if (--p.life == 0) return 0;
    if (--p.frameTimer == 0) {
        if (++p.frame >= s.frameCount) return 0;
        p.frameTimer = p.state;
    }
    p.pos += p.drift;
    p.size = sizeFactor * p.size;
    float keep = slowing;
    p.drift *= keep;
    return 1;
}

// --- Geyser: three vents that erupt in turn ----------------------------------------------------------------

namespace {

// A random value in [low, high) of the spread box component, added to `base` (the original's order: the
// random part, the base, then the low end).
float SpreadAround(float base, float low, float high) {
    return g_clientRandom.Range(0.0f, high - low) + base + low;
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
    int vent = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements)));
    p.color = 0xFFFFFFFF;
    p.pos = s.origin;
    if (s.particles.empty()) {
        p.size = 0.0f;
        p.state = 1;
        p.frame = 15;
        return 1;
    }
    p.state = 0;
    p.frame = static_cast<int>(g_clientRandom.Next() % 12);
    const ParticleElement& e = s.elements[vent];
    float drop = s.size * 0.2f;
    p.life = 20;
    p.size = drop;
    if (e.ticks > 0) {
        p.life = 0x7FFFFFFF;
        float radian = ei::kRadiansPerDegree;
        float yaw = g_clientRandom.Range(-20.0f, 20.0f) * radian + e.vent.yaw;
        float tilt = g_clientRandom.Range(-10.0f, 10.0f) * radian + e.vent.tilt;
        float power = g_clientRandom.Range(0.0f, 0.2f) + e.vent.power - 0.1f;
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
        p.size = g_clientRandom.Range(0.03f, 0.3f) * s.size;
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
        if (--e.ticks == 0) e.vent.pause = static_cast<int>(g_clientRandom.Next() % 34) + 3;
        return;
    }
    if (--e.vent.pause > 0) return;
    e.ticks = static_cast<int>(g_clientRandom.Next() % 7) + 1;
    float radian = ei::kRadiansPerDegree;
    float yaw = g_clientRandom.Range(0.0f, 360.0f) * radian;
    float tilt = g_clientRandom.Range(0.0f, 10.0f) * radian;
    float power = g_clientRandom.Range(0.4f, 1.0f);
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
    float height01 = g_clientRandom.Range(0.0f, 1.0f);
    p.state = static_cast<int>(g_clientRandom.Next() % 3);
    p.color = s.tint;
    p.pos.x = s.origin.x;
    p.pos.y = s.origin.y;
    p.pos.z = s.origin.z + s.elements[0].pos.z;
    p.life = static_cast<int>(g_clientRandom.Next() % 9) + 100;
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
    float angle = g_clientRandom.Range(low, high);
    opacity = 0.1f;
    level = height01;
    float w = 1.0f - height01;
    float w2 = w * w;
    float w4 = w2 * w2;
    orbitAngle = angle;
    float radius = g_clientRandom.Range(0.8f, 2.0f) * (w4 + 0.2f) * 2.0f;
    orbitRadius = radius;
    float turnHigh = kPi * 0.07f;
    float turnLow = kPi * 0.06f;
    float turn = g_clientRandom.Range(turnLow, turnHigh);
    spin = turn * (1.1f - w * w) * 1.5f;
    climb = 0.0f;
    radiusFactor = s.elements[0].funnel.damping;
    p.pos.x = lib::Sin(angle) * radius + p.pos.x + cx;
    p.pos.y = lib::Cos(angle) * radius + cy + p.pos.y;
    p.pos.z = height01 * s.elements[0].funnel.height + p.pos.z;
    if (p.state != 0) {
        p.size = (radius + 0.3f) * s.size;
        p.frame = static_cast<int>(g_clientRandom.Next() % 12);
    } else {
        p.frame = static_cast<int>(g_clientRandom.Next() & 3) + 12;
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

// --- Portal and PortalStar ---------------------------------------------------------------------------------

// Portal: the first particle is the glow above the portal (state 1). The others start
// at a random point of the disc (denser near the edge), spin round it (four in eleven or so the
// other way, with a different frame range) and rise.
int EmitPortal(ParticleSource& s, Particle& p, int) {
    float& spinCos = p.velocity.x;     // the offset's turn per tick
    float& spinSin = p.velocity.y;
    float& riseRate = p.velocity.z;    // times the source's size: the rise per tick
    float& offsetX = p.aux.x;          // from the portal's centre
    float& offsetY = p.aux.y;
    float& sparkSize = p.aux.z;
    if (s.particles.empty()) {
        p.pos = s.origin;
        p.size = 0.0f;
        p.pos.z = 8.0f * 0.8f + p.pos.z;
        p.color = 0x60FFFFFF;
        p.state = 1;
        p.frame = 15;
        return 1;
    }
    p.state = 0;
    p.color = 0x40FFFFFF;
    p.life = s.lifetime;
    p.frameTimer = s.frameDelay;
    float radius = 2.5f;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    float spread = (t + 1.0f) * t * 0.5f;
    float angle = g_clientRandom.Range(0.0f, 6.2832f);
    float reach = spread * radius;
    float x = lib::Sin(angle) * reach;
    float y = lib::Cos(angle) * reach;
    p.pos.x = x + s.origin.x;
    p.pos.y = y + s.origin.y;
    p.pos.z = s.origin.z;
    p.size = 0.0f;
    offsetY = y;
    sparkSize = (2.0f - spread) * 0.3f;
    offsetX = x;
    float spin = radius / 2.5f * 0.1f;
    if (g_clientRandom.Range(0.0f, 1.0f) > spread) {
        p.frame = static_cast<int>(g_clientRandom.Next() % 11) + 4;
    } else {
        p.frame = static_cast<int>(g_clientRandom.Next() & 3);
        spin = -spin;
    }
    spinCos = lib::Cos(spin);
    spinSin = lib::Sin(spin);
    riseRate = (1.0f - spread + 3.0f) * radius * 0.015f;
    p.pos.z = g_clientRandom.Range(-0.5f, 0.5f) * riseRate + p.pos.z;
    return 1;
}

// Portal: particles spiral up (their offset turned each tick) and fade above 8 units; the spare ones (state set)
// flicker in size while the portal is on.
int UpdatePortal(ParticleSource& s, Particle& p) {
    const float& spinCos = p.velocity.x;
    const float& spinSin = p.velocity.y;
    float& riseRate = p.velocity.z;
    float& offsetX = p.aux.x;
    float& offsetY = p.aux.y;
    const float& sparkSize = p.aux.z;
    if (p.state != 0) {
        if (!(s.flags & ParticleSourceFlag::Emitting)) return 0;
        if (s.size < 1.0f) {
            p.size = 0.0f;
            return 1;
        }
        p.size = g_clientRandom.Below(0.4f) * 1.5f + 1.2f;
        return 1;
    }
    if (p.life == 0) return 0;
    float c = spinCos, sn = spinSin;
    float nx = offsetY * sn + c * offsetX;
    int timer = p.frameTimer - 1;
    float ny = offsetY * c - offsetX * sn;
    offsetX = nx;
    offsetY = ny;
    p.pos.x = nx + s.origin.x;
    p.pos.y = ny + s.origin.y;
    float rise = s.size * riseRate;
    p.frameTimer = timer;
    p.pos.z = rise + p.pos.z;
    riseRate *= 1.03f;
    if (timer == 0) {
        if (++p.frame == 4) p.frame = 0;
        if (p.frame == 15) p.frame = 4;
        p.frameTimer = s.frameDelay;
    }
    float climb = p.pos.z - s.origin.z;
    if (climb > 8.0f) {
        p.size = 0.0f;
        p.life = 0;
        p.color = 0x00FFFFFF;
        return 1;
    }
    p.size = sparkSize;
    float fade = (8.0f - climb) * (200.0f / 8.0f);
    int opacity = (std::lround(fade) + 0x37) >> 2;
    if (opacity >= 0x40) opacity = 0x40;
    p.color = (static_cast<std::uint32_t>(opacity) << 24) | 0xFFFFFF;
    return 1;
}

// Portal: |size| * 10 tries per tick (truncated by the game's _ftol).
void TickPortal(ParticleSource& s) { s.emitCount = static_cast<int>(s.size * 10.0f); }

int EmitPortalStar(ParticleSource& s, Particle& p, int) {
    p.color = 0xFFFFFF;
    p.life = 15;
    p.frame = 0;
    p.pos = s.origin;
    p.size = 0.0f;
    return 1;
}

// PortalStar: while the source emits, full colour and a random size of 0.8-1.2 times
// the source's; dropped once it stops.
int UpdatePortalStar(ParticleSource& s, Particle& p) {
    if (!(s.flags & ParticleSourceFlag::Emitting)) return 0;
    p.color = 0xFFFFFFFF;
    p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size;
    return 1;
}

// --- ZoneExit ----------------------------------------------------------------------------------------------

namespace {

constexpr std::int32_t kZoneExitAlpha[16] = /*0078AA38*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};   // by life

} // namespace

// ZoneExit: the density follows alpha * typeParam (at most 40 a tick); particles start at a random point of a
// turned ellipse (radii alpha and typeParam) around the source on the ground, red-tinted when extent.x is set,
// rising by an amount that is larger near the centre.
int EmitZoneExit(ParticleSource& s, Particle& p, int) {
    float& sparkSize = p.velocity.x;
    int count = std::lround(s.alpha * s.typeParam);
    if (count > 40) count = 40;
    s.emitCount = count;
    p.color = s.extent.x != 0.0f ? 0x00FF6060u : 0x00FFFFFFu;
    p.frame = static_cast<int>(g_clientRandom.Next() & 15);
    p.life = 15;
    p.frameTimer = 1;
    p.state = 0;
    float a = g_clientRandom.Range(-1.0f, 1.0f);
    float b = g_clientRandom.Range(-1.0f, 1.0f);
    float falloff = (1.9f - a * a) - b * b;
    float x = (a * s.alpha * s.turnCos + s.origin.x) - b * s.turnSin * s.typeParam;
    float y = (s.turnSin * s.alpha * a + b * s.turnCos * s.typeParam) + s.origin.y;
    float ground = g_clientWorld.RawSurfaceHeightAt(x, y);
    p.pos.x = x;
    p.pos.y = y;
    p.size = 0.0f;
    p.pos.z = ground + 0.1f;
    float roll = g_clientRandom.Below(0.6f) + 0.7f;
    sparkSize = roll * falloff * 0.05f;
    return 1;
}

// ZoneExit: alpha by the life left (kZoneExitAlpha), dropped past 0; rising 0.05 per tick, aux.x (0, never set)
// slowing by 0.9, cycling through four frames.
int UpdateZoneExit(ParticleSource&, Particle& p) {
    const float& sparkSize = p.velocity.x;
    int life = p.life;
    p.color = (p.color & 0xFFFFFF) | (static_cast<std::uint32_t>(ByLife(kZoneExitAlpha, life, 255)) << 24);
    p.life = --life;
    if (life < 0) return 0;
    p.pos.z += 0.05f;
    p.aux.x *= 0.9f;
    p.frame = (p.frame & ~3) | ((p.frame + 1) & 3);
    p.size = sparkSize;
    return 1;
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

// --- GroundDust1 / 2 ---------------------------------------------------------------------------------------

namespace {

// By quadrant.
constexpr float kDustAxisX[4] = /*0078AAB8*/ {0.707f, -0.707f, -0.707f, 0.707f};

constexpr float kDustAxisY[4] = /*0078AAC8*/ {0.707f, -0.707f, 0.707f, -0.707f};

constexpr std::int32_t kDustAlpha[16] = /*0078AAD8*/ {0, 64, 144, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};

} // namespace

// GroundDust1 (green) / GroundDust2 (red): the element holds the dust's colour, then the ground plane.
void SetupGroundDust(ParticleSource& s) {
    s.elements[0].plane.color = ParticleTypeOf(s.type) == ParticleType::GroundDust1 ? 0x40FF40 : 0xFF4040;
}

// Ground dust: the first 128 particles of a tick run out on a circle (index * 0.049
// radians, step 0.1); the others along the four diagonals (by 32, step index * 1/310). All at the
// source, in element 0's colour, 6 ticks.
int EmitGroundDust(ParticleSource& s, Particle& p, int index) {
    float& dustSize = p.aux.x;
    float& swell = p.aux.y;    // the size's factor per tick
    if (index >= 128) {
        int j = index - 128;
        int diagonal = j / 32;
        float fu = (j & 31) * 0.0032258064f;
        p.velocity.x = fu * kDustAxisX[diagonal];
        p.velocity.y = fu * kDustAxisY[diagonal];
    } else {
        float fu = index * 0.049140625f;
        p.velocity.x = lib::Sin(fu) * 0.1f;
        p.velocity.y = lib::Cos(fu) * 0.1f;
    }
    p.pos = s.origin;
    p.size = 0.0f;
    p.life = 6;
    p.frame = 15;
    p.velocity.z = 0.0f;
    dustSize = 0.07f;
    p.color = s.elements[0].plane.color;
    swell = 1.0f;
    return 1;
}

// GroundDust1 / 2: dust moving along the ground, never below the plane the source measured (element 0),
// growing and slowing down.
int UpdateGroundDust(ParticleSource& s, Particle& p) {
    float& dustSize = p.aux.x;
    const float& swell = p.aux.y;
    if (--p.life < 0) return 0;
    p.color = (static_cast<std::uint32_t>(kDustAlpha[p.life]) << 24) + (p.color & 0xFFFFFF);
    p.pos += p.velocity;
    float lifted = g_clientWorld.RawSurfaceHeightAt(p.pos.x, p.pos.y) + 0.1f;
    const auto& plane = s.elements[0].plane;
    float along = (p.pos.x - s.origin.x) * plane.normal.x + (p.pos.y - s.origin.y) * plane.normal.y;
    float onPlane = plane.height - along * plane.normal.z + 0.1f;
    p.pos.z = SelectLess(lifted, onPlane, onPlane, lifted);
    p.size = dustSize;
    p.velocity *= 1.05f;
    dustSize = swell * dustSize;
    return 1;
}

// GroundDust1 / 2: on the element's first tick, the ground plane under the source (the normal's z replaced by
// its reciprocal when non-zero); after the second the source stops emitting.
void TickGroundPlane(ParticleSource& s, ParticleElement& e) {
    if (e.ticks == 0) {
        auto& plane = e.plane;
        const PlatformSet::SurfacePlaneResult surface = g_clientWorld.platforms.SurfacePlane(s.origin.x, s.origin.y);
        plane.normal = surface.normal;
        plane.height = surface.height;
        if (plane.normal.z != 0.0f) plane.normal.z = 1.0f / plane.normal.z;   // a NaN stays the same NaN
    }
    if (++e.ticks > 1) s.flags &= ~ParticleSourceFlag::Emitting;
}
