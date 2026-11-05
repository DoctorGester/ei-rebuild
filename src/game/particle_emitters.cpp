// Particle emit callbacks (of a ParticleSource) and the element array allocator.
#include "effects.h"
#include "map_object_figures.h"   // Object3D
#include "particle_common.h"

#include "random.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "world.h"
#include "game_singletons.h"
#include "lib/float_select.h"

#include <cmath>

namespace {

// --- LightningBlast ----------------------------------------------------------------------------------

constexpr float kTwoPi = 6.28318f;

// One point of a new bolt: a random offset (none for point 0) around the source, on the ground,
// with a fork direction.
void PlaceBoltPoint(ParticleSource* s, int i, Vector3& dir) {
    float radius = g_clientRandom.Range(0.0f, 1.0f);
    float angle = g_clientRandom.Range(0.0f, kTwoPi);
    dir = {lib::Sin(angle) * radius, lib::Cos(angle) * radius, 0.0f};
    if (i == 0) dir = {};
    Vector3 point = s->size * dir * 0.3f + s->origin;
    point.z = g_clientWorld.RawSurfaceHeightAt(point.x, point.y) + 0.3f;
    ParticleElement* points = s->elements.data();
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
int EmitAlongBolt(ParticleSource* s, Particle* p, ParticleElement* points) {
    int& flashes = p->frameTimer;
    float& sparkSize = p->aux.x;
    int emitted = points[0].bolt.emitted;
    p->frame = 0;
    p->life = 10;
    int index = emitted / 2;
    flashes = points[0].bolt.emitted & 1;
    p->state = index + 1;
    p->pos = points[index].pos;
    p->size = 0.0f;
    sparkSize = 0.1f;
    ++points[0].bolt.emitted;
    if (flashes == 0) return 1;
    sparkSize = g_clientRandom.Range(0.5f, 1.4f) * s->size * 0.2f;
    p->frame = static_cast<int>((g_clientRandom.Next() & 3) + 12);
    if (index == 0) sparkSize = s->size * 0.4f;
    p->size = sparkSize;
    return 1;
}

// A burst at the impact point: a random direction inside the unit disc (ten tries), spread by a
// random angle and distance.
int EmitBurst(ParticleSource* s, Particle* p, ParticleElement* points) {
    int& jumps = p->frameTimer;
    float& sparkSize = p->aux.x;
    p->color = 0x40FFFFFF;
    p->frame = 8;
    g_clientRandom.Next();   // draw kept
    float u = g_clientRandom.Range(0.0f, 1.0f);
    float spread = 1.0f - u * u;
    float angle = g_clientRandom.Range(0.0f, 6.28f);
    // A random point of the unit disc (ten tries) that nothing uses; draws kept.
    for (int tries = 10;;) {
        float vx = g_clientRandom.Range(-1.0f, 1.0f);
        float vy = g_clientRandom.Range(-1.0f, 1.0f);
        if (LengthSquared2D(vy, vx) < 1.0f) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    p->drift = {lib::Cos(angle) * spread, lib::Sin(angle) * spread, 0.0f};
    p->drift *= s->size * 0.3f;
    p->drift.z = 0.0f;
    p->pos.x = points[0].pos.x + p->drift.x;
    p->pos.y = points[0].pos.y + p->drift.y;
    float rise = (1.0f - spread) * s->size * 0.1f + points[0].pos.z;
    p->size = 0.0f;
    p->pos.z = rise;
    jumps = 0;
    p->life = std::lround(9.0f - spread * 3.0f);
    float roll = g_clientRandom.Range(0.1f, 0.14f);
    sparkSize = roll * (2.0f - spread) * s->size;
    return 1;
}

// A spark between a random point and its base, at a random fraction of the way, that jumps there.
int EmitSpark(ParticleSource* s, Particle* p) {
    int& jumps = p->frameTimer;
    float& sparkSize = p->aux.x;
    Vector3& target = p->velocity;
    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(s->elements));
    std::uint32_t index = g_clientRandom.Next() % count;
    p->frame = 1;
    sparkSize = g_clientRandom.Range(0.0f, 0.01f) * 3.0f + 0.06f;
    p->life = 6;
    jumps = 1;
    float u = g_clientRandom.Range(0.0f, 1.0f);
    float w = 1.0f - u;
    ParticleElement* points = s->elements.data();
    p->pos = points[index].pos;
    p->size = 0.0f;
    const ParticleElement& n = s->elements[index];
    target = u * n.pos + w * n.bolt.base;
    return 1;
}

// --- Modifier1-12 ------------------------------------------------------------------------------------

// By point 0's fade step; entry 10 (0): the last emitting tick, past the 10 (the original's zero tail).
constexpr std::int32_t kFadeAlpha[11] = /*0078A774*/ {0, 32, 64, 128, 255, 207, 159, 111, 63, 15, 0};

// The axis the points are spread around (their offsets are perpendicular to it); some types also
// raise or lower the ring (spreadMin.x holds the height).
Vector3 ModifierAxis(ParticleSource* s, float twiceRadius) {
    Vector3 axis = {};
    switch (ParticleTypeOf(s->type)) {
    case ParticleType::Modifier1:
        axis.z = 1.0f;
        break;
    case ParticleType::Modifier2:
    case ParticleType::Modifier3:
        s->spreadMin.x = twiceRadius * 0.4f + s->spreadMin.x;
        axis.z = 1.0f;
        break;
    case ParticleType::Modifier4:
        s->spreadMin.x -= twiceRadius * 0.4f;
        axis.z = 1.0f;
        break;
    case ParticleType::Modifier5:
    case ParticleType::Modifier6:
    case ParticleType::Modifier7:
    case ParticleType::Modifier8: {
        Vector3 y = s->attachedTo->localRotation.AxisY();
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
void PlaceModifierPoints(ParticleSource* s) {
    float radius = s->AttachmentOffset().radius;
    s->spreadMin.x = radius;
    float twice = radius + radius;
    Vector3 axis = ModifierAxis(s, twice);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        s->elements[i].ticks = 0;   // the fade step
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
        if (s->size < 0.0f) scale = scale * 0.04347826f;   // 1/23
        ParticleElement& point = s->elements[i];
        point.ring.offset = scale * v;
        point.pos = point.ring.offset + s->origin;
        point.pos.z = s->spreadMin.x + point.pos.z;
        point.ring.start = point.pos;
    }
}

// Alpha tables indexed by the ticks left.
constexpr std::int32_t kSparkAlpha[8] = /*0078AB98*/ {0, 64, 128, 255, 255, 255, 255, 255};
constexpr float kGlowAlpha[16] = /*0078AA78*/ {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                            1.0f, 1.0f};
constexpr std::int32_t kDustAlpha[16] = /*0078AAD8*/ {0, 64, 144, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};

} // namespace

// LightningBlast: on the first emission the bolt's ten points are placed; then the
// particles run along the bolt (two per point), then bursts at the impact point (up to five, while
// the source has fewer than 200 particles), then sparks between the points.
int ParticleSource::EmitLightning(Particle* p, int) {
    if (elements.empty()) {
        elements.resize(10);
        Vector3 dir;
        for (int i = 0; i < std::ssize(elements); ++i) PlaceBoltPoint(this, i, dir);
    }
    ParticleElement* points = elements.data();
    p->color = 0xFFFFFFFF;
    if (points[0].bolt.emitted < std::ssize(elements) * 2) return EmitAlongBolt(this, p, points);
    p->state = 0;
    if (std::ssize(particles) < 200 && points[0].ticks < 5) return EmitBurst(this, p, points);
    return EmitSpark(this, p);
}

// Modifier1-12 (needs a carrier): on the first emission |size| * 10 points (at least one) are scattered on a
// ring around the carrier. Then one particle per point while the fade has not started (frame = the modifier's
// number), then drifting sparks between a random point and its starting place.
int ParticleSource::EmitModifier(Particle* p, int index) {
    Vector3& base = p->velocity;    // a spark's point between its element's places
    Vector3& scatter = p->aux;      // added to base, slowing
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    if (elements.empty()) {
        if (!attachedTo) {
            game::DebugLog("Particle effect Modifier without carrier\n");
            return 0;
        }
        int count = static_cast<int>(AbsBits(size) * 10.0f);
        if (count < 1) count = 1;
        elements.resize(count);
        PlaceModifierPoints(this);
    }
    ParticleElement* points = elements.data();
    const int fade = points[0].ticks;
    p->color = (static_cast<std::uint32_t>(kFadeAlpha[fade]) << 24) | 0xFFFFFF;
    if (points[0].ticks == 0 && index < std::ssize(elements)) {
        unsigned kind = static_cast<unsigned>(type - static_cast<int>(ParticleType::Modifier1));
        if (kind <= 11) p->frame = static_cast<int>(kind);
        p->state = index;
        p->pos = points[index].pos;
        p->size = size >= 0.0f ? 0.125f : 0.2f;
        p->life = 25;
        return 1;
    }

    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(elements));
    std::uint32_t pick = g_clientRandom.Next() % count;
    float u = g_clientRandom.Range(0.0f, 1.0f);
    std::uint32_t frameRoll = g_clientRandom.Next();
    float w = 1.0f - u;
    p->life = 5;
    p->frame = static_cast<int>((frameRoll & 3) + 12);
    const ParticleElement& e = elements[pick];
    base = u * e.pos + w * e.ring.start;
    float jz = g_clientRandom.Range(-0.04f, 0.04f);
    float jy = g_clientRandom.Range(-0.04f, 0.04f);
    float jx = g_clientRandom.Range(-0.04f, 0.04f);
    scatter = {jx, jy, jz};
    p->pos = elements[pick].ring.start;
    p->size = 0.06f;
    return 1;
}

// --- Smaller callbacks ----------------------------------------------------------------------------

// ZoneExit: the density follows alpha * typeParam (at most 40 a tick); particles start at a random point of a
// turned ellipse (radii alpha and typeParam) around the source on the ground, red-tinted when extent.x is set,
// rising by an amount that is larger near the centre.
int ParticleSource::EmitZoneExit(Particle* p, int) {
    float& sparkSize = p->velocity.x;
    int count = std::lround(alpha * typeParam);
    if (count > 40) count = 40;
    emitCount = count;
    p->color = extent.x != 0.0f ? 0x00FF6060u : 0x00FFFFFFu;
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    p->life = 15;
    p->frameTimer = 1;
    p->state = 0;
    float a = g_clientRandom.Range(-1.0f, 1.0f);
    float b = g_clientRandom.Range(-1.0f, 1.0f);
    float falloff = (1.9f - a * a) - b * b;
    float x = (a * alpha * turnCos + origin.x) - b * turnSin * typeParam;
    float y = (turnSin * alpha * a + b * turnCos * typeParam) + origin.y;
    float ground = g_clientWorld.RawSurfaceHeightAt(x, y);
    p->pos.x = x;
    p->pos.y = y;
    p->size = 0.0f;
    p->pos.z = ground + 0.1f;
    float roll = WordToFloat(g_clientRandom.Next()) * kWordTo0_6 + 0.7f;
    sparkSize = roll * falloff * 0.05f;
    return 1;
}

// ShapeChange: on its first update (life -10) the particle's lifetime is set from its
// position in the emission (order * 8 / particle count + 7); the alpha follows kGlowAlpha.
int ParticleSource::UpdateGlow(Particle* p) {
    const int& order = p->frameTimer;
    if (p->life == -10) p->life = order * 8 / lib::Count(particles) + 7;
    if (--p->life < 0) return 0;
    int opacity = std::lround(kGlowAlpha[p->life] * 255.0f);
    p->color = (p->color & 0xFFFFFF) + (static_cast<std::uint32_t>(opacity) << 24);
    return 1;
}

// GroundDust1 / 2: dust moving along the ground, never below the plane the source measured (element 0),
// growing and slowing down.
int ParticleSource::UpdateGroundDust(Particle* p) {
    float& dustSize = p->aux.x;
    const float& swell = p->aux.y;
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kDustAlpha[p->life]) << 24) + (p->color & 0xFFFFFF);
    p->pos += p->velocity;
    float lifted = g_clientWorld.RawSurfaceHeightAt(p->pos.x, p->pos.y) + 0.1f;
    const auto& plane = elements[0].plane;
    float along = (p->pos.x - origin.x) * plane.normal.x + (p->pos.y - origin.y) * plane.normal.y;
    float onPlane = plane.height - along * plane.normal.z + 0.1f;
    p->pos.z = SelectLess(lifted, onPlane, onPlane, lifted);
    p->size = dustSize;
    p->velocity *= 1.05f;
    dustSize = swell * dustSize;
    return 1;
}

// GroundDust1 / 2: on the element's first tick, the ground plane under the source (the normal's z replaced by
// its reciprocal when non-zero); after the second the source stops emitting.
void ParticleSource::TickGroundPlane(ParticleElement* e) {
    if (e->ticks == 0) {
        auto& plane = e->plane;
        const PlatformSet::SurfacePlaneResult surface = g_clientWorld.platforms.SurfacePlane(origin.x, origin.y);
        plane.normal = surface.normal;
        plane.height = surface.height;
        if (plane.normal.z != 0.0f) plane.normal.z = 1.0f / plane.normal.z;   // a NaN stays the same NaN
    }
    if (++e->ticks > 1) flags &= ~ParticleSourceFlag::Emitting;
}

// Portal: particles spiral up (their offset turned each tick) and fade above 8 units; the spare ones (state set)
// flicker in size while the portal is on.
int ParticleSource::UpdatePortal(Particle* p) {
    const float& spinCos = p->velocity.x;
    const float& spinSin = p->velocity.y;
    float& riseRate = p->velocity.z;
    float& offsetX = p->aux.x;
    float& offsetY = p->aux.y;
    const float& sparkSize = p->aux.z;
    if (p->state != 0) {
        if (!(flags & ParticleSourceFlag::Emitting)) return 0;
        if (size < 1.0f) {
            p->size = 0.0f;
            return 1;
        }
        p->size = WordToFloat(g_clientRandom.Next()) * kWordTo0_4 * 1.5f + 1.2f;
        return 1;
    }
    if (p->life == 0) return 0;
    float c = spinCos, s = spinSin;
    float nx = offsetY * s + c * offsetX;
    int timer = p->frameTimer - 1;
    float ny = offsetY * c - offsetX * s;
    offsetX = nx;
    offsetY = ny;
    p->pos.x = nx + origin.x;
    p->pos.y = ny + origin.y;
    float rise = size * riseRate;
    p->frameTimer = timer;
    p->pos.z = rise + p->pos.z;
    riseRate *= 1.03f;
    if (timer == 0) {
        if (++p->frame == 4) p->frame = 0;
        if (p->frame == 15) p->frame = 4;
        p->frameTimer = frameDelay;
    }
    float climb = p->pos.z - origin.z;
    if (climb > 8.0f) {
        p->size = 0.0f;
        p->life = 0;
        p->color = 0x00FFFFFF;
        return 1;
    }
    p->size = sparkSize;
    float fade = (8.0f - climb) * (200.0f / 8.0f);
    int opacity = (std::lround(fade) + 0x37) >> 2;
    if (opacity >= 0x40) opacity = 0x40;
    p->color = (static_cast<std::uint32_t>(opacity) << 24) | 0xFFFFFF;
    return 1;
}

// Portal: |size| * 10 tries per tick (truncated by the game's _ftol).
void ParticleSource::TickPortal(ParticleElement*) { emitCount = static_cast<int>(size * 10.0f); }

// Sparks: the spare ones (state set) get a random size once and are dropped after; the
// rest fly along their drift, shrinking and slowing, with the alpha from kSparkAlpha.
int ParticleSource::UpdateSparks(Particle* p) {
    if (p->state != 0) {
        if (p->size != 0.0f) return 0;
        p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * 0.55f;
        return 1;
    }
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kSparkAlpha[p->life]) << 24) | 0xFFFFFF;
    p->pos += p->drift;
    p->size *= 0.9f;
    p->drift *= 0.6f;
    ++p->frame;
    return 1;
}

// FeebleMind: size * 2 tries per tick; each particle starts one step out from the
// source in a random horizontal direction.
int ParticleSource::EmitFeebleMind(Particle* p, int) {
    Vector3& offset = p->velocity;  // from the circle's centre
    float& growingSize = p->aux.z;
    emitCount = std::lround(size + size);
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    p->pos = origin;
    p->size = 0.0f;
    p->life = 20;
    float twoPi = kPi + kPi;
    offset.z = 0.0f;
    float angle = g_clientRandom.Range(0.0f, twoPi);
    offset.x = lib::Cos(angle) * 0.05f;
    offset.y = lib::Sin(angle) * 0.05f;
    p->pos += offset;
    float grow = (WordToFloat(g_clientRandom.Next()) * kWordTo0_2 + 0.9f) * size * 0.035f;
    growingSize = grow;
    return 1;
}

// FeetCloud: size * 8 tries per tick; a puff at one of the two feet (the element's position once it is
// tracked, else the source), drifting in a random direction inside the unit disc, rising.
int ParticleSource::EmitFeetCloud(Particle* p, int) {
    float& puffSize = p->velocity.x;
    float& ease = p->velocity.y;    // the part of the way to the target per tick
    Vector3& offset = p->aux;       // from the foot
    Vector3& spread = p->drift;     // added to the offset per tick
    emitCount = std::lround(size * 8.0f);
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    int foot = static_cast<int>(g_clientRandom.Next() & 1);
    p->state = foot;
    const ParticleElement& e = elements[foot];
    if (e.foot.countdown >= 0) p->pos = e.pos;
    else p->pos = origin;
    p->size = 0.0f;
    float x, y;
    for (int tries = 10;;) {
        x = g_clientRandom.Range(-1.0f, 1.0f);
        y = g_clientRandom.Range(-1.0f, 1.0f);
        if (LengthSquared2D(y, x) < 1.0f) break;   // integer-sourced: never NaN
        if (!(--tries > 0)) break;
    }
    float scale = e.foot.radius;
    spread = Vector3{x, y, 0.0f} * 0.1f * scale * size;
    offset = {};
    ease = 1.0f;
    p->life = 10;
    puffSize = size * e.foot.radius * 0.4f;
    p->pos.z = puffSize * 1.2f + p->pos.z;
    return 1;
}

// StartTrans: once the element's countdown runs out the source stops (Emitting and KeepAlive off) and detaches; the
// ring widens, and element 0's trans.count grows by 0.4 a tick and sets the tries per tick (at most 8).
void ParticleSource::TickStartTrans(ParticleElement* e) {
    int ticks = e->ticks--;
    if (ticks < 0) {
        Stop();
    }
    e->trans.scale = size * 0.02f + e->trans.scale;
    elements[0].trans.count += 0.4f;
    int count = std::lround(elements[0].trans.count);
    emitCount = count;
    if (!(count < 8)) emitCount = 8;
}
