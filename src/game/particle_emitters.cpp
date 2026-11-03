// Particle emit callbacks (of a ParticleSource) and the element array allocator.
#include "effects.h"
#include "map_object_figures.h"   // Object3D

#include "random.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "world.h"
#include "game_singletons.h"
#include "lib/float_select.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace {

// --- LightningBlast (0x2030) ----------------------------------------------------------------------

constexpr float kTwoPi = 6.28318f;
#define EI_POINT_THREE 0.3f
#define EI_QUARTER 0.25f
#define EI_ONE 1.0f
#define EI_HALF 0.5f
#define EI_POINT_FOUR 0.4f
#define EI_POINT_TWO 0.2f
#define EI_TENTH 0.1f
#define EI_THREE 3.0f
#define EI_NINE 9.0f
#define EI_TWO 2.0f
#define EI_SPARK_SIZE 0.06f
#define EI_SCALE_0_9 2.0954757e-10f   // ~0.9 * 2^-32
#define EI_SCALE_1 2.3283064e-10f   // ~2^-32
#define EI_SCALE_ANGLE 1.4621765e-09f   // ~2 pi * 2^-32
#define EI_SCALE_2 4.656613e-10f   // ~2^-31
#define EI_SCALE_0_04 9.3132255e-12f   // ~0.04 * 2^-32
#define EI_SCALE_0_01 2.3283064e-12f   // ~0.01 * 2^-32

// One point of a new bolt: a random offset (none for point 0) around the source, on the ground,
// with a fork direction.
void PlaceBoltPoint(ParticleSource* s, int i, Vector3& dir) {
    float radius = g_clientRandom.Range(0.0f, 1.0f);
    float angle = g_clientRandom.Range(0.0f, kTwoPi);
    dir = {lib::Sin(angle) * radius, lib::Cos(angle) * radius, 0.0f};
    if (i == 0) dir = {};
    Vector3 point = s->size * dir * EI_POINT_THREE + s->origin;
    point.z = g_clientWorld.RawSurfaceHeightAt(point.x, point.y) + EI_POINT_THREE;
    ParticleElement* points = s->elements.data();
    points[i].pos = point;
    points[i].bolt.base = point;

    float inverse = EI_ONE / std::sqrt(LengthSquared2D(dir.y, dir.x) + EI_QUARTER);
    dir *= inverse;
    dir.z = inverse * EI_HALF;
    points[i].bolt.fork = dir * EI_HALF;
    points[i].bolt.emitted = 0;
    points[i].ticks = 0;   // bursts
}

// Particles along the bolt: two per point (the odd ones flicker with a random size and frame).
int EmitAlongBolt(ParticleSource* s, Particle* p, ParticleElement* points) {
    int emitted = points[0].bolt.emitted;
    p->frame = 0;
    p->life = 10;
    int index = emitted / 2;
    p->frameTimer = points[0].bolt.emitted & 1;
    p->state = index + 1;
    p->pos = points[index].pos;
    p->size = 0.0f;
    p->aux.x = 0.1f;     // 0.1
    ++points[0].bolt.emitted;
    if (p->frameTimer == 0) return 1;
    std::uint32_t roll = g_clientRandom.Next();
    p->aux.x = (WordToFloat(roll) * EI_SCALE_0_9 + EI_HALF) * s->size * EI_POINT_TWO;
    p->frame = static_cast<int>((g_clientRandom.Next() & 3) + 12);
    if (index == 0) p->aux.x = s->size * EI_POINT_FOUR;
    p->size = p->aux.x;
    return 1;
}

// A burst at the impact point: a random direction inside the unit disc (ten tries), spread by a
// random angle and distance.
int EmitBurst(ParticleSource* s, Particle* p, ParticleElement* points) {
    p->color = 0x40FFFFFF;
    p->frame = 8;
    g_clientRandom.Next();
    float u = WordToFloat(g_clientRandom.Next()) * EI_SCALE_1;
    float spread = EI_ONE - u * u;
    float angle = WordToFloat(g_clientRandom.Next()) * EI_SCALE_ANGLE;
    float vx, vy;
    for (int tries = 10;;) {
        vx = WordToFloat(g_clientRandom.Next()) * EI_SCALE_2 - EI_ONE;
        float y = g_clientRandom.Range(-1.0f, 1.0f);
        vy = y;
        if (y * vy + vx * vx < EI_ONE) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    p->drift = {vx, vy, 0.0f};
    p->drift.z = 0.0f;
    p->drift.x = lib::Cos(angle) * spread;
    p->drift.y = lib::Sin(angle) * spread;
    float scale = s->size * EI_POINT_THREE;
    p->drift *= scale;
    p->drift.z = 0.0f;
    p->pos.x = points[0].pos.x + p->drift.x;
    p->pos.y = points[0].pos.y + p->drift.y;
    float rise = (EI_ONE - spread) * s->size * EI_TENTH + points[0].pos.z;
    p->size = 0.0f;
    p->pos.z = rise;
    p->frameTimer = 0;
    p->life = std::lround(EI_NINE - spread * EI_THREE);
    float roll = WordToFloat(g_clientRandom.Next()) * EI_SCALE_0_04 + EI_TENTH;
    p->aux.x = roll * (EI_TWO - spread) * s->size;
    return 1;
}

// A spark between a random point and its base, at a random fraction of the way.
int EmitSpark(ParticleSource* s, Particle* p) {
    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(s->elements));
    std::uint32_t index = g_clientRandom.Next() % count;
    p->frame = 1;
    p->aux.x = WordToFloat(g_clientRandom.Next()) * EI_SCALE_0_01 * EI_THREE + EI_SPARK_SIZE;
    p->life = 6;
    p->frameTimer = 1;
    float u = WordToFloat(g_clientRandom.Next()) * EI_SCALE_1;
    float w = EI_ONE - u;
    ParticleElement* points = s->elements.data();
    p->pos = points[index].pos;
    p->size = 0.0f;
    const ParticleElement& n = s->elements[index];
    p->velocity = u * n.pos + w * n.bolt.base;
    return 1;
}

// --- Modifier1-12 (0x201B-0x2026) ---------------------------------------------------------------

#define EI_TEN 10.0f
#define EI_ZERO 0.0f
#define EI_SMALL_RING 0.04347826f   // 1/23
#define EI_JITTER 0.04f
#define EI_SCALE_0_08 1.8626451e-11f   // ~0.08 * 2^-32

constexpr std::int32_t kFadeAlpha[35] = /*0078A774*/ {0, 32, 64, 128, 255, 207, 159, 111, 63, 15};
const char* NoCarrierMessage() { return "Particle effect Modifier without carrier\n"; }

// The axis the points are spread around (their offsets are perpendicular to it); some types also
// raise or lower the ring (spreadMin.x holds the height).
Vector3 ModifierAxis(ParticleSource* s, float twiceRadius) {
    Vector3 axis;
    axis = {};
    switch (s->type - static_cast<int>(ParticleType::Modifier1)) {
    case 0:
        axis.z = 1.0f;
        break;
    case 1:
    case 2:
        s->spreadMin.x = twiceRadius * EI_POINT_FOUR + s->spreadMin.x;
        axis.z = 1.0f;
        break;
    case 3:
        s->spreadMin.x -= twiceRadius * EI_POINT_FOUR;
        axis.z = 1.0f;
        break;
    case 4:
    case 5:
    case 6:
    case 7: {
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
            auto draw = [] { return WordToFloat(g_clientRandom.Next()) * EI_SCALE_2 - EI_ONE; };
            v = {draw(), draw(), draw()};
            v -= axis * Dot(axis, v);
            lengthSq = LengthSquared(v);
            if (lengthSq != 0.0f) break;   // a NaN ends the search too
        }
        float scale = twice / std::sqrt(lengthSq);
        if (s->size < EI_ZERO) scale = scale * EI_SMALL_RING;
        ParticleElement& point = s->elements[i];
        point.ring.offset = scale * v;
        point.pos = point.ring.offset + s->origin;
        point.pos.z = s->spreadMin.x + point.pos.z;
        point.ring.start = point.pos;
    }
}

#define EI_ZONE_EXIT_SPREAD 1.9f
#define EI_SCALE_0_03 1.3969838e-10f   // ~0.03 * 2^-32
#define EI_POINT_SEVEN 0.7f
#define EI_TWENTIETH 0.05f
#define EI_SCALE_0_4 9.3132266e-11f   // ~0.4 * 2^-32
#define EI_ONE_AND_HALF 1.5f
#define EI_ONE_POINT_TWO 1.2f
#define EI_PORTAL_GROWTH 1.03f
#define EI_EIGHT 8.0f
#define EI_TWO_HUNDRED 200.0f
#define EI_POINT_EIGHT 0.8f
#define EI_SPARK_SCALE 0.55f
#define EI_POINT_SIX 0.6f
#define EI_POINT_NINE 0.9f
#define EI_PI 3.1415927f
#define EI_FEEBLE_SPEED 0.05f   // 0.05
#define EI_SCALE_0_2 4.656614e-11f   // ~0.2 * 2^-32
#define EI_FEEBLE_SIZE 0.035f
#define EI_FEET_DENSITY 8.0f
#define EI_FEET_RISE 1.2f
#define EI_BYTE 255.0f
#define EI_DUST_GROWTH 1.05f
#define EI_TRANS_GROWTH 0.02f

// Alpha tables indexed by the ticks left.
constexpr std::int32_t kSparkAlpha[8] = /*0078AB98*/ {0, 64, 128, 255, 255, 255, 255, 255};
constexpr float kGlowAlpha[16] = /*0078AA78*/ {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                            1.0f, 1.0f};
constexpr std::int32_t kDustAlpha[16] = /*0078AAD8*/ {0, 64, 144, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};

// One random draw in [-1, 1) through the ISAAC results: Next() * ~2^-31 - 1.
float Signed(std::uint32_t roll) { return WordToFloat(roll) * EI_SCALE_2 - EI_ONE; }

} // namespace

// LightningBlast: on the first emission the bolt's ten points are placed; then the
// particles run along the bolt (two per point), then bursts at the impact point (up to five, while
// the source has fewer than 200 particles), then sparks between the points.
int ParticleSource::EmitLightning(Particle* p, int) {
    if (std::ssize(elements) == 0) {
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
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    if (std::ssize(elements) == 0) {
        if (!attachedTo) {
            game::DebugLog(NoCarrierMessage());
            return 0;
        }
        int count = static_cast<int>(AbsBits(size) * EI_TEN);
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
        p->size = 1.0f;
        lib::StoreBytes(&p->size, size >= EI_ZERO ? 0x3E000000u : 0x3E4CCCCDu);   // 0.125 / 0.2
        p->life = 25;
        return 1;
    }

    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(elements));
    std::uint32_t pick = g_clientRandom.Next() % count;
    float u = WordToFloat(g_clientRandom.Next()) * EI_SCALE_1;
    std::uint32_t frameRoll = g_clientRandom.Next();
    float w = EI_ONE - u;
    p->life = 5;
    p->frame = static_cast<int>((frameRoll & 3) + 12);
    const ParticleElement& e = elements[pick];
    p->velocity = u * e.pos + w * e.ring.start;
    std::uint32_t ra = g_clientRandom.Next();
    std::uint32_t rb = g_clientRandom.Next();
    std::uint32_t rc = g_clientRandom.Next();
    float jx = WordToFloat(rc) * EI_SCALE_0_08 - EI_JITTER;
    float jy = WordToFloat(rb) * EI_SCALE_0_08 - EI_JITTER;
    float jz = WordToFloat(ra) * EI_SCALE_0_08 - EI_JITTER;
    p->aux = {jx, jy, jz};
    p->pos = elements[pick].ring.start;
    p->size = 0.06f;   // 0.06
    return 1;
}

// --- Smaller callbacks ----------------------------------------------------------------------------

// ZoneExit: the density follows alpha * typeParam (at most 40 a tick); particles start at a random point of a
// turned ellipse (radii alpha and typeParam) around the source on the ground, red-tinted when extent.x is set,
// rising by an amount that is larger near the centre.
int ParticleSource::EmitZoneExit(Particle* p, int) {
    int count = std::lround(alpha * typeParam);
    if (count > 40) count = 40;
    emitCount = count;
    p->color = extent.x != 0.0f ? 0x00FF6060u : 0x00FFFFFFu;
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    p->life = 15;
    p->frameTimer = 1;
    p->state = 0;
    float a = Signed(g_clientRandom.Next());
    float b = Signed(g_clientRandom.Next());
    float falloff = (EI_ZONE_EXIT_SPREAD - a * a) - b * b;
    float x = (a * alpha * turnCos + origin.x) - b * turnSin * typeParam;
    float y = (turnSin * alpha * a + b * turnCos * typeParam) + origin.y;
    float ground = g_clientWorld.RawSurfaceHeightAt(x, y);
    p->pos.x = x;
    p->pos.y = y;
    p->size = 0.0f;
    p->pos.z = ground + EI_TENTH;
    float roll = WordToFloat(g_clientRandom.Next()) * EI_SCALE_0_03 + EI_POINT_SEVEN;
    p->velocity.x = roll * falloff * EI_TWENTIETH;
    return 1;
}

// 0x2039: on its first update (life -10) the particle's lifetime is set from its
// position in the emission (frameTimer * 8 / particle count + 7); the alpha follows kGlowAlpha.
int ParticleSource::UpdateGlow(Particle* p) {
    if (p->life == -10) p->life = p->frameTimer * 8 / lib::Count(particles) + 7;
    if (--p->life < 0) return 0;
    int opacity = std::lround(kGlowAlpha[p->life] * EI_BYTE);
    p->color = (p->color & 0xFFFFFF) + (static_cast<std::uint32_t>(opacity) << 24);
    return 1;
}

// 0x203A/0x203B: dust moving along the ground, never below the plane the source measured (element 0),
// growing and slowing down.
int ParticleSource::UpdateGroundDust(Particle* p) {
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kDustAlpha[p->life]) << 24) + (p->color & 0xFFFFFF);
    p->pos += p->velocity;
    float lifted = g_clientWorld.RawSurfaceHeightAt(p->pos.x, p->pos.y) + EI_TENTH;
    p->pos.z = lifted;
    const auto& plane = elements[0].plane;
    float along = (p->pos.x - origin.x) * plane.normal.x + (p->pos.y - origin.y) * plane.normal.y;
    float onPlane = plane.height - along * plane.normal.z + EI_TENTH;
    float ground = lifted;
    p->pos.z = SelectLess(ground, onPlane, onPlane, ground);
    p->size = p->aux.x;
    p->velocity *= EI_DUST_GROWTH;
    p->aux.x = p->aux.y * p->aux.x;
    return 1;
}

// 0x203A/0x203B: on the element's first tick, the ground plane under the source (the normal's z replaced by
// its reciprocal when non-zero); after the second the source stops emitting.
void ParticleSource::TickGroundPlane(ParticleElement* e) {
    if (e->ticks == 0) {
        auto& plane = e->plane;
        const PlatformSet::SurfacePlaneResult surface = g_clientWorld.platforms.SurfacePlane(origin.x, origin.y);
        plane.normal = surface.normal;
        plane.height = surface.height;
        if (plane.normal.z != 0.0f) plane.normal.z = EI_ONE / plane.normal.z;   // a NaN stays the same NaN
    }
    if (++e->ticks > 1) flags &= ~ParticleSourceFlag::Emitting;
}

// Portal: particles spiral up (their offset rotated by the turn in velocity.x/y each tick) and fade above 8
// units; the spare ones (state set) flicker in size while the portal is on.
int ParticleSource::UpdatePortal(Particle* p) {
    if (p->state != 0) {
        if (!(flags & ParticleSourceFlag::Emitting)) return 0;
        if (size < EI_ONE) {
            p->size = 0.0f;
            return 1;
        }
        p->size = WordToFloat(g_clientRandom.Next()) * EI_SCALE_0_4 * EI_ONE_AND_HALF + EI_ONE_POINT_TWO;
        return 1;
    }
    if (p->life == 0) return 0;
    float c = p->velocity.x, s = p->velocity.y;
    float nx = p->aux.y * s + c * p->aux.x;
    int timer = p->frameTimer - 1;
    float ny = p->aux.y * c - p->aux.x * s;
    float nyRounded = ny;
    p->aux.x = nx;
    p->aux.y = ny;
    p->pos.x = nx + origin.x;
    p->pos.y = nyRounded + origin.y;
    float rise = size * p->velocity.z;
    p->frameTimer = timer;
    p->pos.z = rise + p->pos.z;
    p->velocity.z *= EI_PORTAL_GROWTH;
    if (timer == 0) {
        if (++p->frame == 4) p->frame = 0;
        if (p->frame == 15) p->frame = 4;
        p->frameTimer = frameDelay;
    }
    float climb = p->pos.z - origin.z;
    if (climb > EI_EIGHT) {
        p->size = 0.0f;
        p->life = 0;
        p->color = 0x00FFFFFF;
        return 1;
    }
    p->size = p->aux.z;
    float fade = (EI_EIGHT - climb) * (EI_TWO_HUNDRED / EI_EIGHT);
    int opacity = (std::lround(fade) + 0x37) >> 2;
    if (opacity >= 0x40) opacity = 0x40;
    p->color = (static_cast<std::uint32_t>(opacity) << 24) | 0xFFFFFF;
    return 1;
}

// Portal: |size| * 10 tries per tick (truncated by the game's _ftol).
void ParticleSource::TickPortal(ParticleElement*) { emitCount = static_cast<int>(size * EI_TEN); }

// Sparks: the spare ones (state set) get a random size once and are dropped after; the
// rest fly along their drift, shrinking and slowing, with the alpha from kSparkAlpha.
int ParticleSource::UpdateSparks(Particle* p) {
    if (p->state != 0) {
        if (p->size != 0.0f) {
            p->size = 0.0f;
            return 0;
        }
        p->size = (WordToFloat(g_clientRandom.Next()) * EI_SCALE_0_4 + EI_POINT_EIGHT) * size *
                        EI_SPARK_SCALE;
        return 1;
    }
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kSparkAlpha[p->life]) << 24) | 0xFFFFFF;
    p->pos += p->drift;
    p->size *= EI_POINT_NINE;
    p->drift *= EI_POINT_SIX;
    ++p->frame;
    return 1;
}

// FeebleMind: size * 2 tries per tick; each particle starts one step out from the
// source in a random horizontal direction.
int ParticleSource::EmitFeebleMind(Particle* p, int) {
    emitCount = std::lround(size + size);
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    p->pos = origin;
    p->size = 0.0f;
    p->life = 20;
    float twoPi = EI_PI + EI_PI;
    std::uint32_t roll = g_clientRandom.Next();
    p->velocity.z = 0.0f;
    float angle = WordToFloat(roll) * twoPi * EI_SCALE_1;
    p->velocity.x = lib::Cos(angle) * EI_FEEBLE_SPEED;
    p->velocity.y = lib::Sin(angle) * EI_FEEBLE_SPEED;
    p->pos += p->velocity;
    float grow = (WordToFloat(g_clientRandom.Next()) * EI_SCALE_0_2 + EI_POINT_NINE) * size *
                       EI_FEEBLE_SIZE;
    p->aux.z = grow;
    return 1;
}

// FeetCloud: size * 8 tries per tick; a puff at one of the two feet (the element's position once it is
// tracked, else the source), drifting in a random direction inside the unit disc, rising.
int ParticleSource::EmitFeetCloud(Particle* p, int) {
    emitCount = std::lround(size * EI_FEET_DENSITY);
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    int foot = static_cast<int>(g_clientRandom.Next() & 1);
    p->state = foot;
    const ParticleElement& e = elements[foot];
    if (e.foot.countdown >= 0) p->pos = e.pos;
    else p->pos = origin;
    p->size = 1.0f;
    p->size = 0.0f;
    float x, y;
    for (int tries = 10;;) {
        x = Signed(g_clientRandom.Next());
        float wide = Signed(g_clientRandom.Next());
        y = wide;
        if (wide * y + x * x < EI_ONE) break;   // integer-sourced: never NaN
        if (!(--tries > 0)) break;
    }
    float scale = e.foot.radius;
    p->drift = Vector3{x, y, 0.0f} * EI_TENTH * scale * size;
    p->aux = {};
    p->velocity.y = 1.0f;
    p->life = 10;
    p->velocity.x = size * e.foot.radius * EI_POINT_FOUR;
    p->pos.z = p->velocity.x * EI_FEET_RISE + p->pos.z;
    return 1;
}

// StartTrans: once the element's countdown runs out the source stops (Emitting and KeepAlive off) and detaches; the
// ring widens, and element 0's trans.count grows by 0.4 a tick and sets the tries per tick (at most 8).
void ParticleSource::TickStartTrans(ParticleElement* e) {
    int ticks = e->ticks--;
    if (ticks < 0) {
        flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
        Attach(nullptr);
    }
    e->trans.scale = size * EI_TRANS_GROWTH + e->trans.scale;
    elements[0].trans.count += EI_POINT_FOUR;
    int count = std::lround(elements[0].trans.count);
    emitCount = count;
    if (!(count < 8)) emitCount = 8;
}
