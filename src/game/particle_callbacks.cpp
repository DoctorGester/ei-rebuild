// ParticleSource's per-type callbacks (emit, update, element tick); each names the source types using it.
#include "particle_common.h"

#include "angles.h"
#include "core_maths.h"
#include "map_object_figures.h"
#include "object_maths.h"
#include "unit.h"

#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/vecmath.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "world.h"

#include <bit>
#include <cmath>
#include <concepts>

using namespace particle;

// --- Casting effects (0x2027-0x202A): sparks rising around the caster -----------------------------

namespace {

#define EI_POINT_EIGHT 0.8f
#define EI_THREE 3.0f
#define EI_TEN 10.0f
#define EI_TENTH 0.1f
#define EI_POINT_SIX 0.6f
#define EI_THIRTEENTH 0.07692308f   // 1/13
#define EI_TWENTY_SIXTH 0.03846154f   // 1/26
#define EI_SCALE_0_4_HI 1.862645e-10f   // ~0.8 * 2^-32
#define EI_CAST_DRIFT 0.1f   // the swirl's random acceleration bound
#define EI_CAST_DAMP 0.95f   // velocity kept per tick
#define EI_CAST_PULL 0.0005f   // pull back to the centre
#define EI_SCALE_SIZE 6.984918e-11f   // size jitter scale
#define EI_SIZE_BASE 0.85f
#define EI_POINT_THREE 0.3f

const char* NoCarrierCasting() { return "Particle effect CastingElemental without carrier\n"; }
constexpr std::int32_t kCastAlphaByCountdown[7] = /*0078A964*/ {0, 32, 64, 112, 160, 208, 255};
constexpr std::int32_t kCastAlphaByLifeWide[15] = /*0078A980*/ {0, 32, 64, 112, 160, 208, 255, 255, 224, 192, 128, 64, 32, 16};   // 0x202A
constexpr std::int32_t kCastAlphaByLife[15] = /*0078A9BC*/ {0, 255, 255, 255, 255, 255, 255, 255, 224, 192, 128, 64, 32, 16};

// A random point in the unit ball (ten tries; the last one is kept), as the set-up draws it.
void RandomInBall(float& x, float& y, float& z) {
    for (int tries = 10;;) {
        x = SignedUnit();
        y = g_clientRandom.Range(-1.0f, 1.0f);
        float zu = g_clientRandom.Range(-1.0f, 1.0f);
        z = zu;
        float lengthSquared = zu * z + x * x + y * y;
        if (lengthSquared < EI_P_ONE) return;   // random: never NaN
        if (!(--tries > 0)) return;
    }
}

// The first emission: element 0 from the carrier's size, elements 1..lifetime random swirls.
// 0 (and a debug message) without a carrier.
bool SetUpCasting(ParticleSource* s) {
    if (!s->attachedTo) {
        game::DebugLog(NoCarrierCasting());
        return false;
    }
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    radius = radius * EI_POINT_EIGHT;
    float scale = s->attachedTo->GetClassId() == ClassId::Unit ? Carrier(s->attachedTo)->Radius() : radius;
    if (ParticleTypeOf(s->type) == ParticleType::CastingDivination) scale = scale + scale;
    else scale = scale * EI_THREE;
    float extent = scale;
    s->elements.resize(s->lifetime + 1);
    ParticleElement* e = s->elements.data();
    e->cast.alpha = 0xFF;
    e->ticks = 6;
    e->cast.radius = extent;
    if (ParticleTypeOf(s->type) == ParticleType::CastingDivination) {
        e->cast.size = 0.07f;     // 0.07
        e->cast.growth = 1.05f;     // 1.05
    } else {
        e->cast.size = 0.08f;     // 0.08
        e->cast.growth = 1.07f;     // 1.07
    }
    e->pos.x = 1.0f;
    for (int i = 1; i <= s->lifetime; ++i) {
        float x, y, z;
        RandomInBall(x, y, z);
        Vector3 a;
        a.Construct(x, y, z);
        float* swirl = &s->elements[i].swirl.position.x;
        swirl[0] = a.x * EI_TEN;
        swirl[1] = a.y * EI_TEN;
        swirl[2] = a.z * EI_TEN;
        RandomInBall(x, y, z);
        Vector3 b;
        b.Construct(x, y, z);
        float* drift = &s->elements[i].swirl.velocity.x;
        drift[0] = b.x * EI_TENTH;
        drift[1] = b.y * EI_TENTH;
        drift[2] = b.z * EI_TENTH;
        s->elements[i].pos.x = 0.0f;
    }
    return true;
}

#define EI_SPHERE_RADIUS 1.2f   // of the carrier's radius
#define EI_KEEP_0_9 0.9f
#define EI_TAKE_0_1 0.1f
#define EI_ANTIMAGIC_RATE 24.0f
#define EI_SPHERE_RATE 6.0f
#define EI_MOTION_KEEP 0.7f
#define EI_STILL_SPEED 0.01f
#define EI_TILT_ANGLE 0.01f   // 0.01
#define EI_BIG_ANTIMAGIC 2.5f
#define EI_BIG_EARTH 3.5f
#define EI_BIG_SPHERE 1.5f
#define EI_SCALE_0_03 1.3969838e-10f   // ~0.03 * 2^-32
#define EI_SHELL 0.65f
#define EI_AIR_SIZE 1.6f

const char* NoCarrierSphere() { return "Particle effect Link without carrier\n"; }

// A random point in the unit ball (ten tries), the squared length summed z, y, x.
void RandomInBallZYX(float& x, float& y, float& z) {
    for (int tries = 10;;) {
        x = SignedUnit();
        y = g_clientRandom.Range(-1.0f, 1.0f);
        float zu = g_clientRandom.Range(-1.0f, 1.0f);
        z = zu;
        float lengthSquared = zu * z + y * y + x * x;
        if (lengthSquared < EI_P_ONE) return;   // random: never NaN
        if (!(--tries > 0)) return;
    }
}

// Once per tick (the first emission): the sphere's height and radius ease towards the carrier's
// figure, the emission rate follows the source's size, and element 1 smooths the carrier's motion.
bool FollowCarrier(ParticleSource* s) {
    if (!s->attachedTo) {
        game::DebugLog(NoCarrierSphere());
        return false;
    }
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    radius = radius * EI_SPHERE_RADIUS;
    float height;
    height = offset.z;
    if (std::ssize(s->elements) == 0) {
        s->elements.resize(2);
        s->elements[1].sphereTilt.tiltCos = std::cos(EI_TILT_ANGLE);
        s->elements[1].sphereTilt.tiltSin = std::sin(EI_TILT_ANGLE);
        s->elements[1].pos = {};
        s->elements[0].pos = {};
        s->elements[0].pos.z = height;
        s->elements[0].sphere.radius = radius;
    }
    ParticleElement* e = s->elements.data();
    e->pos.z = e->pos.z * EI_KEEP_0_9 + height * EI_TAKE_0_1;
    float* sphereRadius = &s->elements[0].sphere.radius;
    *sphereRadius = radius * EI_TAKE_0_1 + (*sphereRadius) * EI_KEEP_0_9;
    float rate = s->size * (ParticleTypeOf(s->type) == ParticleType::Antimagic ? EI_ANTIMAGIC_RATE : EI_SPHERE_RATE);
    s->emitCount = std::lround(rate);
    Vector3& smoothed = s->elements[1].pos;
    float mx = smoothed.x * EI_MOTION_KEEP;
    float my = smoothed.y * EI_MOTION_KEEP;
    float mz = smoothed.z * EI_MOTION_KEEP;
    float sx = mx + s->motion.x;
    float sy = my + s->motion.y;
    float sz = mz + s->motion.z;
    smoothed.x = sx;
    smoothed.y = sy;
    smoothed.z = sz;
    const Vector3& m = s->elements[1].pos;
    float* speed = &s->elements[0].sphere.speed;
    *speed = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(m));
    EI_NAN_AUDIT(*speed);
    if (s->elements[0].sphere.speed < EI_STILL_SPEED) s->elements[0].sphere.speed = 0.0f;
    return true;
}

#define EI_FULL_TURN 360.0f
#define EI_PI 3.1415927f
#define EI_RING_WIDEN 1.1f
#define EI_PER_36 0.027777778f   // 1/36
#define EI_RISE_LOW (-0.1f)
#define EI_RISE_HIGH 0.1f
#define EI_FLAME_SIZE 0.0015f
#define EI_TWENTY 20.0f
#define EI_HALF 0.5f
#define EI_SPARK_SPREAD 0.001f
#define EI_FADE_SIZE 0.7692308f   // 1/1.3
#define EI_TURN_SPEED 10.0f   // degrees per tick
#define EI_TENTH_B 0.1f
#define EI_HEAL_RINGS 6.0f   // rings per unit of size
#define EI_HEAL_RINGS_BASE 2.0f
#define EI_HEAL_GROWTH 0.0069444445f   // 1/144
#define EI_HEAL_SPREAD 0.15f
#define EI_HEAL_SPARK_SIZE 0.07f

const char* NoCarrierRing() { return "Particle effect Casting without carrier\n"; }
const char* NoCarrierHealing() { return "Particle effect Healing without carrier\n"; }
constexpr int kFlameState = 0xFFFF;     // a burning-out flame (the update's last phase)

// The rings around the carrier (emitCount of them, already allocated): stacked from the carrier's feet with a
// random lift, their flames spaced evenly around the circle; the source's height is the radius growth per
// tick (Healing also sets the flame growth). Each ring counts down from `countdown`.
void PlaceRings(ParticleSource* s, int countdown) {
    float step = ei::kRadiansPerDegree * (EI_FULL_TURN / s->emitCount);
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    float fullRadius = radius;
    float angle = 0.0f;
    offset.z = offset.z - fullRadius;
    radius = fullRadius * EI_RING_WIDEN;
    float diameter = fullRadius + fullRadius;
    float rise = radius * EI_PER_36;
    s->height = rise + rise;
    if (ParticleTypeOf(s->type) == ParticleType::Healing) s->growth = (s->size + EI_P_ONE) * EI_HEAL_GROWTH;
    if (s->emitCount > 0) {
        float low = diameter * EI_RISE_LOW;
        float high = diameter * EI_RISE_HIGH;
        for (int i = 0; i < s->emitCount; ++i) {
            ParticleElement* e = &s->elements[i];
            e->word[9] = 0;
            e->ticks = countdown;
            float lift = Unsigned(Next()) * (high - low) * EI_P_SCALE_1 + low;
            e->flames.centre = offset;
            e->flames.centre.z = lift + e->flames.centre.z;
            e->pos = e->flames.centre;
            e->flames.radius = 0.0f;
            e->flames.angle = angle;
            e->flames.rise = (diameter - lift) * EI_PER_36;
            angle = angle + step;
        }
    }
    s->emitState = 0.0f;
}

// The flame phase: one flame per ring (state = the ring), then one call that only ends the phase
// (result 0). False once the phase is over.
bool EmitRingFlame(ParticleSource* s, Particle* p, int& result) {
    int emitted = static_cast<int>(Bits(&s->emitState));
    if (emitted > std::ssize(s->elements)) return false;
    lib::StoreBytes(&s->emitState, static_cast<std::uint32_t>(emitted + 1));
    if (emitted == std::ssize(s->elements)) {
        result = 0;
        return true;
    }
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = 36;
    p->frameTimer = 1;
    p->state = emitted;
    const ParticleElement& ring = s->elements[emitted];
    p->pos.x = ring.pos.x + s->origin.x;
    p->pos.y = ring.pos.y + s->origin.y;
    p->pos.z = ring.pos.z + s->origin.z;
    if (ParticleTypeOf(s->type) == ParticleType::Healing) p->size = (s->size + EI_P_ONE) * EI_TENTH_B;
    else p->size = s->size * EI_FLAME_SIZE;
    result = 1;
    return true;
}

// A spark in a box of +-spread around a ring's centre. The original's z uses -spread for both the range and
// the offset.
void PlaceRingSpark(ParticleSource* s, Particle* p, const ParticleElement& ring, float spread) {
    float low = -spread;
    float x = Unsigned(Next()) * (spread - low) * EI_P_SCALE_1 + ring.pos.x + s->origin.x + low;
    p->pos.x = x;
    float y = Unsigned(Next()) * (spread - low) * EI_P_SCALE_1 + ring.pos.y + s->origin.y + low;
    p->pos.y = y;
    p->pos.z = ring.pos.z - Unsigned(Next()) * low * EI_P_SCALE_1 + s->origin.z + low;
}

float RingRadius(const ParticleElement& ring) {
    float r;
    r = ring.flames.radius;
    return r;
}

void FollowRing(ParticleSource* s, Particle* p, int ring) {
    const ParticleElement& e = s->elements[ring];
    p->pos.x = s->origin.x + e.pos.x;
    p->pos.y = s->origin.y + e.pos.y;
    p->pos.z = s->origin.z + e.pos.z;
}

// Turns a ring's flame point 10 degrees around its centre; the ring rises.
void TurnRing(ParticleElement* e) {
    float* radius = &e->flames.radius;
    float* angle = &e->flames.angle;
    float turned = (ei::kRadiansPerDegree * EI_TURN_SPEED) + (*angle);
    *angle = turned;
    e->pos.x = std::sin(turned) * (*radius) + e->flames.centre.x;
    e->pos.y = std::cos(*angle) * (*radius) + e->flames.centre.y;
    e->pos.z = e->flames.rise + e->pos.z;
}

#define EI_PI_F 3.1415927f
#define EI_HALF_C 0.5f
#define EI_POINT_FOUR_C 0.4f
#define EI_SWIRL_TURN_SLOW 0.3f
#define EI_SWIRL_TURN_FAST 0.5f
#define EI_SWIRL_HIGH 1.6f
#define EI_SWIRL_SIZE_SCALE 8.149073e-11f   // ~0.35 * 2^-32
#define EI_SWIRL_SIZE_BASE 0.85f
#define EI_SWIRL_GROW 0.03f
#define EI_SWIRL_SPARK_SCALE 1.862645e-10f   // ~0.8 * 2^-32
#define EI_SWIRL_SPARK_BASE 0.6f
#define EI_SWIRL_SPARK_SIZE 0.2f
#define EI_SWIRL_SHRINK 0.97f
#define EI_MARKER_SIZE_SCALE 9.3132266e-11f   // ~0.4 * 2^-32
#define EI_MARKER_SIZE_BASE 0.8f

// Alpha rising and falling: a spark by its life (from the start), a marker one entry on, a column backwards
// from the last (ColumnAlpha).
constexpr std::int32_t kRiseFallAlpha[20] = /*0078A914*/ {0, 0, 32, 64, 112, 160, 208, 255, 255, 255, 255, 255, 255, 255, 207, 159, 111,
                                                 63, 15};

// Adds orbiter `k` (the k-th of `lifetime`, evenly spaced around the carrier) and emits its marker
// particle (state -1 - k).
int AddOrbiter(ParticleSource* s, Particle* p) {
    float orbit, carrier;     // the orbit radius and the carrier's radius
    int k = lib::Count(s->elements);
    if (k == 0) {
        Vector3 offset;
        float radius;
        s->AttachmentOffset(&offset, &radius);
        carrier = radius;
        orbit = radius * EI_HALF_C;
    } else {
        orbit = s->elements[0].orbiter.relative.x;
        carrier = s->elements[0].orbiter.scale;
    }
    s->elements.resize(k + 1);
    float share = k * EI_PI_F;
    float angle = (share + share) / s->lifetime;
    ParticleElement* e = &s->elements[k];
    float lift = carrier * EI_POINT_FOUR_C;
    float sign = 1.0f;
    float turn;
    switch (s->type - static_cast<int>(ParticleType::CastingIllusion)) {
    case 0:
    case 2:
        turn = EI_SWIRL_TURN_SLOW;
        break;
    case 1:
        turn = EI_SWIRL_TURN_FAST;
        lift = carrier * EI_SWIRL_HIGH;
        sign = -1.0f;
        break;
    default:
        turn = EI_SWIRL_TURN_FAST;
        break;
    }
    float* relative = &e->orbiter.relative.x;
    relative[0] = std::cos(angle) * orbit;
    e->word[9] = 0;
    e->ticks = 1;
    relative[1] = std::sin(angle) * orbit;
    relative[2] = lift;
    float px = s->origin.x + relative[0];
    float py = s->origin.y + relative[1];
    float pz = s->origin.z + relative[2];
    e->pos = {px, py, pz};
    e->orbiter.turnCos = std::cos(turn);
    float* previous = &e->orbiter.previous.x;
    previous[0] = px;
    previous[1] = py;
    previous[2] = pz;
    e->orbiter.turnSin = std::sin(turn);
    // Kept from the original (its locals read one slot off): the sign times the carrier's radius, not the orbit.
    e->orbiter.scale = sign * carrier;
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = 6;
    p->frameTimer = 1;
    p->state = -1 - k;
    p->pos = e->pos;
    p->size = 0.0f;
    return 1;
}

#define EI_LINK_DEGENERATE 1e-10f
#define EI_LINK_TWIST 0.2f
#define EI_LINK_HALF 0.5f
#define EI_LINK_QUARTER 0.25f
#define EI_LINK_BULGE 0.9f
#define EI_LINK_ALPHA 1000.0f

inline float F(std::uint32_t bits) {
    float f;
    f = std::bit_cast<float>(bits);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> inline float F(T) = delete;

// The beam's frame from the extent (the first emission).
void SetUpLink(ParticleSource* s) {
    s->elements.resize(4);
    const Vector3& x = s->extent;
    s->elements[0].link.length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(x));
    s->elements[0].link.inverseLength = EI_P_ONE / s->elements[0].link.length;

    // A side vector: (ey, -ex, 0), or (ey - ez, -ex, ex) when the extent is (nearly) vertical.
    Vector3 side;
    float zero = EI_P_ZERO;
    side.Construct(x.y - zero, zero - x.x, 0.0f);
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(side));
    EI_NAN_AUDIT(length);
    if (length < EI_LINK_DEGENERATE) {
        float ex = x.x;
        Vector3 other;
        other.Construct(x.y - x.z, -ex, ex);
        length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(other));
        side = other;
    }
    float inverse = EI_P_ONE / length;
    side.x = inverse * side.x;
    side.y = side.y * inverse;
    side.z = side.z * inverse;

    Vector3 cross;
    cross.Construct(side.z * x.y - side.y * x.z, side.x * x.z - side.z * x.x,
                    side.y * x.x - side.x * x.y);
    float crossInverse = EI_P_ONE / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(cross));
    cross.x = crossInverse * cross.x;
    cross.y = cross.y * crossInverse;
    cross.z = cross.z * crossInverse;

    float extentLength = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(x));
    Vector3 direction;
    direction.Construct(x.x / extentLength, x.y / extentLength, x.z / extentLength);
    s->elements[1].linkAxis.axis = direction;
    s->elements[2].linkAxis.axis = side;
    s->elements[3].linkAxis.axis = cross;
    s->elements[0].ticks = 0xFF;
}

#define EI_BLAST_FLAME_SIZE 0.6f
#define EI_BLAST_POINT_TWO 0.2f
#define EI_BLAST_RISE_SCALE 8.1490724e-11f   // ~0.35 * 2^-32
#define EI_BLAST_RISE 0.35f
#define EI_BLAST_RISE_BASE 0.175f
#define EI_BLAST_HALF 0.5f
#define EI_BLAST_DEBRIS_SCALE 1.8626452e-10f   // ~0.4 * 2^-31
#define EI_BLAST_POINT_SEVEN 0.7f
#define EI_BLAST_POINT_15 0.15f
#define EI_BLAST_POINT_THREE 0.3f
#define EI_BLAST_SEVENTH 0.14285715f   // 1/7
#define EI_BLAST_POINT_EIGHT 0.8f
#define EI_BLAST_SCALE_0_4 9.3132266e-11f   // ~0.4 * 2^-32
#define EI_BLAST_CORE_SIZE 2.3f
#define EI_BLAST_SWELL_LATE 1.03f
#define EI_BLAST_SWELL_EARLY 1.01f
#define EI_BLAST_FRAME_ACCEL 1.02f
#define EI_BLAST_DRAG 0.03f
#define EI_BLAST_SLOW 0.75f

constexpr int kBlastCore = -1222;     // the core flash, emitted once at the start

constexpr std::int32_t kBlastAlpha[16] = /*0078A5DC*/ {255, 255, 207, 175, 128, 128, 128, 128, 128, 175, 207, 192, 128, 48};   // by frame

// A flame's size and frame timing: the size from the fragments' radius, the frame step random.
void BlastFlameTiming(ParticleSource* s, Particle* p) {
    float jitter = Unsigned(Next()) * EI_BLAST_SCALE_0_4 + EI_BLAST_POINT_EIGHT;
    p->aux0 = s->elements[0].fragment.radius * s->size * jitter * EI_BLAST_FLAME_SIZE;
    p->aux1 = static_cast<float>(p->frame);
}

#define EI_FOG_CELL 1.0f   // the grid spacing
#define EI_FOG_QUARTER 0.25f
#define EI_FOG_JITTER_LOW (-0.25f)
#define EI_FOG_SIZE_SCALE 1.1641532e-10f   // ~0.5 * 2^-32
#define EI_FOG_SIZE_BASE 1.0f
#define EI_FOG_ALPHA 160.0f

#define EI_GEYSER_YAW_SCALE 9.313226e-09f   // ~40 * 2^-32 (degrees)
#define EI_GEYSER_YAW_LOW 20.0f
#define EI_GEYSER_TILT_SCALE 4.656613e-09f   // ~20 * 2^-32
#define EI_GEYSER_TILT_LOW 10.0f
#define EI_GEYSER_JET_SCALE 4.656613e-11f   // ~0.2 * 2^-32
#define EI_GEYSER_JET_LOW 0.1f
#define EI_GEYSER_DROP 0.2f
#define EI_GEYSER_HALF 0.5f
#define EI_GEYSER_MIST 0.01f
#define EI_GEYSER_MARK_SCALE 6.2864276e-11f   // ~0.0146 * 2^-32
#define EI_GEYSER_MARK_BASE 0.03f
#define EI_GEYSER_SHRINK 0.9f
#define EI_GEYSER_GRAVITY 0.06f
#define EI_GEYSER_ANGLE_SCALE 8.381903e-08f   // ~360 * 2^-32
#define EI_GEYSER_TILT_RANGE 2.3283064e-09f   // ~10 * 2^-32
#define EI_GEYSER_POWER_SCALE 1.3969839e-10f   // ~0.6 * 2^-32
#define EI_GEYSER_POWER_BASE 0.4f

// A random value in [low, high) of the spread box component, added to `base` (the original's order: the
// random part, the base, then the low end).
float SpreadAround(float base, float low, float high) {
    return Unsigned(Next()) * (high - low) * EI_P_SCALE_1 + base + low;
}

#define EI_SHOWER_RAYS 8.0f   // directions per unit of alpha
#define EI_SHOWER_JITTER_SCALE 1.3969838e-11f   // ~0.06 * 2^-32
#define EI_SHOWER_JITTER_LOW 0.03f
#define EI_SHOWER_SPEED 0.16666667f   // 1/6
#define EI_SHOWER_FAST 1.5f
#define EI_SHOWER_GRAVITY 0.05f

constexpr std::int32_t kShowerAlpha[16] = /*0078A6B4*/ {0, 64, 128, 192, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

// alpha * 8 random unit directions (at least one) in the elements.
void ChooseShowerRays(ParticleSource* s) {
    s->frameDelay = std::lround(s->alpha * EI_SHOWER_RAYS);
    if (s->frameDelay < 1) s->frameDelay = 1;
    int rays = s->frameDelay;
    s->emitCount = rays * 10;
    s->elements.resize(rays);
    for (int i = 0; i < s->frameDelay; ++i) {
        float x, y, z;
        float lengthSquared;
        for (int tries = 10;;) {
            x = g_clientRandom.Range(-1.0f, 1.0f);
            y = g_clientRandom.Range(-1.0f, 1.0f);
            float zu = g_clientRandom.Range(-1.0f, 1.0f);
            z = zu;
            lengthSquared = zu * z + y * y + x * x;
            if (lengthSquared < EI_P_ONE) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        float length = std::sqrt(lengthSquared);
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        s->elements[i].pos = direction;
        s->elements[i].ray.age = 0;
    }
}

#define EI_BAG_SHRINK 0.93f   // per age
#define EI_BAG_SPREAD_LOW (-0.2f)
#define EI_BAG_SPREAD_HIGH 0.2f
#define EI_BAG_SPARK_SIZE 0.1f
#define EI_BAG_BALL_SIZE 0.15f
#define EI_BAG_KICK 0.05f

float g_bagSizes[12] = /*007A9BFC*/ {};   // set by the first emission
const std::int32_t* BagColours() {   // r, g, b per age
    static const std::int32_t kValues[36] = {
        251, 254, 143, 252,
        227, 149, 252, 201,
        154, 255, 176, 154,
        255, 155, 163, 252,
        152, 176, 250, 150,
        195, 247, 149, 212,
        243, 147, 237, 239,
        144, 249, 239, 144,
        249, 239, 144, 249,
    };
    return kValues;
}
const std::int32_t* BagAlpha() {
    static const std::int32_t kValues[16] = {
        0, 64, 112, 255,
        255, 255, 255, 255,
        255, 255, 255, 255,
        255, 255, 255, 255,
    };
    return kValues;
}
constexpr int kBagGone = -13;

std::uint32_t BagRgb(int kind) {
    const std::int32_t* c = BagColours() + kind * 3;
    return ((static_cast<std::uint32_t>(c[0]) << 8 | static_cast<std::uint32_t>(c[1])) << 8) | static_cast<std::uint32_t>(c[2]);
}

// A random value of the throw box (-0.2 * size to 0.2 * size).
float BagSpread(const ParticleSource* s, float& low, float& high) {
    high = s->size * EI_BAG_SPREAD_HIGH;
    low = s->size * EI_BAG_SPREAD_LOW;
    return Unsigned(Next()) * (high - low) * EI_P_SCALE_1;
}

#define EI_COLUMN_FOOT 0.3f
#define EI_COLUMN_RADIUS 0.8f
#define EI_COLUMN_RISE_SCALE 1.3969838e-10f   // ~0.03 * 2^-32
#define EI_COLUMN_RISE_BASE 0.7f
#define EI_COLUMN_RISE 0.1f
#define EI_COLUMN_RING_SCALE 4.656614e-11f   // ~0.2 * 2^-32
#define EI_COLUMN_RING_BASE 0.9f
#define EI_COLUMN_ANGLE_SCALE 1.4629169e-09f   // ~2 pi * 2^-32
#define EI_COLUMN_SIZE_SCALE 1.862645e-10f   // ~0.8 * 2^-32
#define EI_COLUMN_SIZE_BASE 0.6f
#define EI_COLUMN_SIZE 0.25f
#define EI_COLUMN_SHRINK 0.97f
#define EI_COLUMN_SPEED_UP 1.05f

// The alpha by remaining life, read backwards from kRiseFallAlpha's last entry.
std::uint32_t ColumnAlpha(int life) { return static_cast<std::uint32_t>(kRiseFallAlpha[19 - life]); }

// The ring from the carrier's figure (the first emission). False without a carrier.
bool SetUpColumn(ParticleSource* s) {
    if (!s->attachedTo) {
        game::DebugLog(NoCarrierCasting());
        return false;
    }
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    float height = radius + radius;
    float foot = -offset.z - height * EI_COLUMN_FOOT;
    radius = radius * EI_COLUMN_RADIUS;
    float ring = s->attachedTo->GetClassId() == ClassId::Unit ? Carrier(s->attachedTo)->Radius() : radius;
    radius = ring + ring;
    s->elements.resize(1);
    ParticleElement* e = s->elements.data();
    e->word[9] = 0;
    e->column.radius = radius;
    e->value[4] = 0.0f;
    e->column.height = height;
    e->value[6] = 0.0f;
    e->value[7] = 0.0f;
    e->column.foot = foot;
    e->ticks = 1;
    return true;
}

#define EI_MUSHROOM_ONE 1.0f
#define EI_MUSHROOM_SPREAD 0.2f   // 0.2
#define EI_MUSHROOM_W_SCALE 9.3132266e-11f   // ~0.4 * 2^-32
#define EI_MUSHROOM_W_BASE 0.8f
#define EI_MUSHROOM_UP_SCALE 2.0954757e-10f   // ~0.9 * 2^-32
#define EI_MUSHROOM_UP_BASE 0.1f
#define EI_MUSHROOM_UP 0.12f
#define EI_MUSHROOM_GROW 0.008f   // per tick of age
#define EI_MUSHROOM_SPEED 0.06f
#define EI_MUSHROOM_SWELL 1.1f
#define EI_MUSHROOM_DRAG 0.7f
#define EI_MUSHROOM_PULL 0.02f

#define EI_GAS_RADIUS 0.3f
#define EI_GAS_RATE 5.0f
#define EI_GAS_ROUND 0.5f
#define EI_GAS_LOW (-0.3f)
#define EI_GAS_RISE_LOW 0.02f
#define EI_GAS_RISE_HIGH 0.03f
#define EI_GAS_SIZE 0.7f
#define EI_GAS_SPEED_UP 1.02f
#define EI_GAS_SWELL 1.01f

const char* NoCarrierGas() { return "Particle effect Link without carrier\n"; }

#define EI_FUNNEL_PI_F 5.0f   // rings per unit height
#define EI_FUNNEL_PI 3.1415927f
#define EI_FUNNEL_R_SCALE 2.7939678e-10f   // ~1.2 * 2^-32
#define EI_FUNNEL_R_BASE 0.8f
#define EI_FUNNEL_R_ADD 0.2f
#define EI_FUNNEL_TURN_HIGH 0.07f   // times pi
#define EI_FUNNEL_TURN_LOW 0.06f   // times pi
#define EI_FUNNEL_TURN_TOP 1.1f
#define EI_FUNNEL_TURN_SCALE 1.5f
#define EI_FUNNEL_SIZE 0.3f
#define EI_FUNNEL_DUST 0.15f
#define EI_FUNNEL_RISE 0.0001f
#define EI_FUNNEL_SPIN_UP 1.01f
#define EI_FUNNEL_ALPHA 255.0f
#define EI_FUNNEL_FADE 0.1f
#define EI_FUNNEL_SWAY 0.95f
#define EI_FUNNEL_HALF_ARC 0.5f
#define EI_FUNNEL_LEAN 20.0f
#define EI_FUNNEL_BACK (-1.0f)
#define EI_FUNNEL_HALF 0.5f
#define EI_FUNNEL_MAX_LEAN 0.01f
#define EI_FUNNEL_ONE 1.0f
#define EI_FUNNEL_BEND 0.4f

// The funnel's centre at height h (0..1): between ring k and k+1 (h * 5 rings), with the x and y blends in the
// original's two orders; returns y.
float FunnelCentre(const ParticleSource* s, float h, float& x) {
    int k = static_cast<int>(h);
    if (k >= lib::Count(s->elements) - 2) k = lib::Count(s->elements) - 2;
    float f = h - k;
    float t = MinKeepA(f, 1.0f);
    float u = EI_P_ONE - t;
    const ParticleElement& a = s->elements[k];
    const ParticleElement& b = s->elements[k + 1];
    x = t * b.pos.x + u * a.pos.x;
    return u * a.pos.y + t * b.pos.y;
}

#define EI_CYL_TURN 0.3f
#define EI_CYL_PI 3.1415927f
#define EI_CYL_TURN_SCALE 0.13333334f
#define EI_CYL_ORBIT 0.7f
#define EI_CYL_LIFT 0.2f
#define EI_CYL_TWO 2.0f
#define EI_CYL_RISE 0.04f
#define EI_CYL_RISE_UP 1.25f
#define EI_CYL_TWIST (-0.013333334f)
#define EI_CYL_SPARK_PULL 0.6f
#define EI_CYL_SPARK_SHRINK 0.9f

constexpr std::int32_t kCylinderAlpha[32] = /*0078AB18*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255};

// The stars on the carrier's orbit (the first emission).
void SetUpCylinder(ParticleSource* s) {
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    float turn = (EI_CYL_TURN * EI_CYL_PI) * EI_CYL_TURN_SCALE;
    if (ParticleTypeOf(s->type) == ParticleType::Cylinder2) turn = -turn;
    float c = std::cos(turn);
    float sn = std::sin(turn);
    int count = lib::Count(s->elements);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        float orbit = radius * EI_CYL_ORBIT;
        float twice = static_cast<float>(2 * i);
        ParticleElement& e = s->elements[i];
        e.pos.x = std::cos(twice * EI_CYL_PI / count) * orbit;
        e.pos.y = std::sin(twice * EI_CYL_PI / std::ssize(s->elements)) * orbit;
        e.pos.z = radius * EI_CYL_LIFT;
        e.star.step = {};
        e.star.rotation[0] = c;
        e.star.rotation[1] = sn;
        e.ticks = 18;
        e.star.rise = (EI_CYL_TWO / e.ticks) * radius * EI_CYL_RISE;
        e.star.inverseOrbit = EI_P_ONE / orbit;
        e.star.orbit = orbit;
        count = lib::Count(s->elements);
    }
}

#define EI_TRAIL_MOVING 0.00001f   // below this distance there is no trail
#define EI_TRAIL_HEAD 1.2f
#define EI_TRAIL_HALF 0.5f
#define EI_TRAIL_DRAG 0.95f
#define EI_ARROW_HEAD 0.4f
#define EI_ARROW_PUFF 0.2f
#define EI_ARROW_SWELL 1.4f
#define EI_ARROW_SPREAD 1.1f

// A random value of [low, high) (the original's spread box order).
float Between(float low, float high) { return Unsigned(Next()) * (high - low) * EI_P_SCALE_1 + low; }

// The trail velocity: a random amount of the side axis (SideVelocity), plus one of the up axis (AddUp).
void SideVelocity(ParticleSource* s, Particle* p, float low, float high) {
    float q = Between(low, high);
    p->velocity.x = q * s->side.x;
    p->velocity.y = q * s->side.y;
    p->velocity.z = q * s->side.z;
}

void AddUp(ParticleSource* s, Particle* p, float q) {
    float a = q * s->up.x;
    float b = q * s->up.y;
    float c = q * s->up.z;
    p->velocity.x = a + p->velocity.x;
    p->velocity.y = b + p->velocity.y;
    p->velocity.z = c + p->velocity.z;
}

#define EI_BANSHEE_TALL 3.0f
#define EI_BANSHEE_WIDEN 0.027777778f   // 1/36
#define EI_BANSHEE_HALF 0.5f
#define EI_BANSHEE_REACH 30.0f
#define EI_BANSHEE_EASE 0.5f
#define EI_BANSHEE_THIN 0.001f
#define EI_BANSHEE_SIZE 0.15f

#define EI_PORTAL_HEIGHT 8.0f
#define EI_PORTAL_RADIUS 2.5f
#define EI_PORTAL_ANGLE 1.4629215e-09f   // ~2 pi * 2^-32
#define EI_PORTAL_SPIN 0.1f
#define EI_PORTAL_RISE 0.015f
#define EI_PORTAL_HALF_C 0.5f
#define EI_PORTAL_TWO 2.0f
#define EI_PORTAL_SHRINK 0.3f

// By age.
constexpr float kAcidSpread[19] = /*0078A61C*/ {0.0f, 0.85f, 0.93f, 1.0f, 0.97f, 0.93f, 0.87f, 0.8f, 0.73f, 0.65f, 0.55f, 0.43f,
                                             0.3f, 0.2f};
constexpr float kAcidSize[19] = /*0078A668*/ {1.1f, 0.85f, 0.93f, 1.0f, 0.97f, 0.93f, 0.87f, 0.8f, 0.73f, 0.65f, 0.55f, 0.43f,
                                           0.3f, 0.2f};

// Position: the start (drift) plus the velocity scaled by the source's size and the spread table.
void PlaceAcid(ParticleSource* s, Particle* p, int age, Vector3& out) {
    float size = s->size;
    float sx = size * p->velocity.x;
    float sy = size * p->velocity.y;
    float sz = size * p->velocity.z;
    float k = kAcidSpread[age];
    Vector3 v;
    v.Construct(sx * k, sy * k, sz * k);
    out.Construct(v.x + p->drift.x, v.y + p->drift.y, v.z + p->drift.z);
}

#define EI_CLAY_SCATTER_SCALE 4.656613e-11f   // ~0.2 * 2^-32
#define EI_CLAY_SCATTER_LOW 0.1f
#define EI_CLAY_SLOW 0.9f
#define EI_CLAY_COS 0.95f
#define EI_CLAY_SIN 0.31225f

constexpr std::int32_t kClayAlpha[16] = /*0078A734*/ {0, 128, 255, 255, 255, 255, 255, 255, 255, 160, 80, 32};
constexpr float kClayReach[16] = /*0078A6F4*/ {0.0f, 0.025f, 0.056f, 0.092f, 0.136f, 0.188f, 0.251f, 0.326f, 0.417f, 0.525f,
                                            0.656f, 0.812f, 1.0f, 1.0f, 1.0f, 1.0f};
// What the original read for kClayAlpha at a countdown of -1: kClayReach's last entry (alpha 0 once shifted).
constexpr std::int32_t kClayBefore = std::bit_cast<std::int32_t>(kClayReach[15]);
// The reach read backwards from entry 14 (for a positive size).
float ClayReachBackwards(int t) { return kClayReach[14 - t]; }

#define EI_FALL_SIZE_SCALE 2.3283065e-11f   // ~0.1 * 2^-32
#define EI_FALL_SIZE_BASE 0.1f
#define EI_FALL_HALF 0.5f
#define EI_FALL_TEN 10.0f
#define EI_FALL_DROP_SCALE 2.7939677e-11f   // ~0.12 * 2^-32
#define EI_FALL_DROP 0.06f
#define EI_FALL_SPIN_SCALE 9.3132266e-11f   // ~0.4 * 2^-32
#define EI_FALL_SPIN 0.6f
#define EI_FALL_REACH_SCALE 4.6566133e-11f   // ~0.2 * 2^-32
#define EI_FALL_TURN 360.0f

#define EI_VULKAN_LOW (-0.5f)
#define EI_VULKAN_HALF 0.5f
#define EI_VULKAN_SMOKE 0.3f

// The angle VisionStar's satellites turn by, times their rate: 2 pi (pi + pi, a static initialiser's fold).
constexpr float kSpinAngle = /*007A9BF8*/ 6.2831855f;

// The turn per satellite of the star: the rate for the satellite that follows `count` particles (1-4).
float SpinRate(int count) {
    switch (count) {
    case 1: return 0.2f;
    case 2: return 0.4f;
    case 3: return 0.6f;
    default: return 0.8f;
    }
}

constexpr std::int32_t kZoneExitAlpha[16] = /*0078AA38*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};   // by life

// By life.
constexpr std::int32_t kFeebleAlpha[24] = /*0078ABB8*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                               255, 255, 255, 255, 255, 255, 255};

// By life.
constexpr std::int32_t kFeetAlpha[24] = /*0078AC18*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                             255, 255, 255, 255, 255, 255, 255};

// A figure part's world position.
Vector3* FigurePartPosition(CObject3D* unit, Vector3* out, const FigureMesh* part) {
    unit->PartWorldPosition(&out->x, part);
    return out;
}

const char* kLeftFoot = "ll3";
const char* kRightFoot = "rl3";
const char* kBody = "bd";

// By tick.
constexpr std::int32_t kModifierAlphaIn[35] = /*0078A800*/ {0, 128, 255, 255, 255, 255, 255, 255, 192, 128, 64, 32};
constexpr std::int32_t kModifierAlphaOut[34] = /*0078A88C*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 128};

constexpr std::int32_t kLightningAlpha[16] = /*0078A9F8*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};   // by life

bool IsZero(float f) {
    EI_NAN_AUDIT(f);
    return f == 0.0f;
}

// By life.
constexpr std::int32_t kCurseAlpha[24] = /*0078AC98*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                              255, 255, 255, 255, 255, 255, 255};
constexpr std::int32_t kTransAlpha[16] = /*0078ACF8*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};

// A random point of the unit ball (ten tries, the last one kept), each coordinate rounded.
void RandomInBall(float& x, float& y, float& z, float& lengthSquared) {
    for (int tries = 10;;) {
        x = SignedUnit();
        y = SignedUnit();
        z = g_clientRandom.Range(-1.0f, 1.0f);
        lengthSquared = LengthSquaredUnrounded<Sum3::ZYX>(x, y, z);   // (y + z) + x
        if (lengthSquared < EI_P_ONE) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
}

// base ^ exponent the way VC6 inlined pow here (through ln and 2^x).
float PowInline(float base, int exponent) { return std::exp(std::log(base) * exponent); }

// By quadrant.
constexpr float kDustAxisX[4] = /*0078AAB8*/ {0.707f, -0.707f, -0.707f, 0.707f};
constexpr float kDustAxisY[4] = /*0078AAC8*/ {0.707f, -0.707f, 0.707f, -0.707f};

bool Same(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}

} // namespace

// Casting (0x2027-0x202A). The first call sets the effect up (0x202A then emits its
// marker particle at the source); after that each particle starts on a random direction (pulled
// towards a random swirl), at the effect's radius around the source.
int ParticleSource::EmitCasting(Particle* p, int) {
    if (std::ssize(elements) == 0) {
        if (!SetUpCasting(this)) return 0;
        if (ParticleTypeOf(type) == ParticleType::CastingDivination) {
            p->color = 0xFFFFFF;
            p->frame = 0;
            p->state = 1;
            p->pos = origin;
            p->size = 0.0f;
            return 1;
        }
    }
    p->color = 0xFFFFFF;
    p->frame = 14;
    p->life = 13;
    p->state = 0;
    p->frameTimer = 1;

    float x, y, z, lengthSquared;
    for (;;) {
        x = SignedUnit();
        y = SignedUnit();
        float zu = Unsigned(Next()) * EI_P_SCALE_2 - EI_P_ONE;
        z = zu;
        float sum = zu * z + y * y + x * x;
        lengthSquared = sum;
        if (sum > EI_P_ONE) continue;
        if (lengthSquared <= EI_P_ZERO) continue;   // random: never NaN
        break;
    }
    std::uint32_t span = static_cast<std::uint32_t>(frameDelay * lifetime + 1);
    int pick = static_cast<int>(Next() % span);
    if (pick > 0) {
        const ParticleElement& swirl = elements[(pick - 1) % lifetime];
        const float* toward = &swirl.swirl.position.x;
        x = x + toward[0];
        y = y + toward[1];
        float zu = z + toward[2];
        z = zu;
        lengthSquared = zu * z + y * y + x * x;
    }
    float inverse = EI_P_ONE / std::sqrt(lengthSquared);
    p->velocity.x = inverse * x;
    p->velocity.y = inverse * y;
    p->velocity.z = inverse * z;
    ParticleElement* state = elements.data();
    float reach = state->cast.radius * EI_POINT_SIX;
    p->aux0 = reach;
    p->aux1 = reach * (ParticleTypeOf(type) == ParticleType::CastingDivination ? EI_TWENTY_SIXTH : EI_THIRTEENTH);
    p->aux2 = state->cast.growth;
    float r = p->aux0;
    float ox = r * p->velocity.x;
    float oy = r * p->velocity.y;
    float oz = r * p->velocity.z;
    float px = ox + origin.x;
    float py = oy + origin.y;
    float pz = oz + origin.z;
    p->size = 1.0f;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = (Unsigned(Next()) * EI_SCALE_0_4_HI + EI_POINT_SIX) * state->cast.size *
                    p->aux0;
    return 1;
}

// Casting particles: none once the effect's alpha is 0. The 0x202A marker stays at the
// source with a random size; the others fade by their life (capped by the effect's alpha), run
// their frames backwards (0x2027: every fourth tick held) and spiral in towards the source.
int ParticleSource::UpdateCasting(Particle* p) {
    ParticleElement* state = elements.data();
    if (state->cast.alpha == 0) return 0;
    if (p->state) {
        p->pos = origin;
        p->size = 1.0f;
        p->size = (Unsigned(Next()) * EI_SCALE_SIZE + EI_SIZE_BASE) * elements[0].cast.radius *
                        EI_POINT_THREE;
        p->color = (static_cast<std::uint32_t>(elements[0].cast.alpha) << 24) | 0xFFFFFF;
        return 1;
    }
    int life = --p->life;
    if (life == 0) return 0;
    // Past the end of its life (the update after death): the countdown table's last entry, or the wide table's.
    std::int32_t fade = ParticleTypeOf(type) == ParticleType::CastingDivination ? ByLife(kCastAlphaByLifeWide, life, kCastAlphaByCountdown[6])
                                        : ByLife(kCastAlphaByLife, life, kCastAlphaByLifeWide[14]);
    std::int32_t cap = elements[0].cast.alpha;
    if (cap < fade) fade = cap;
    p->color = (static_cast<std::uint32_t>(fade) << 24) | 0xFFFFFF;
    if (ParticleTypeOf(type) == ParticleType::CastingFire && (life & 3) == 0) ++p->frame;
    float oldSize = p->size;
    float reach = p->aux0 - p->aux1;
    --p->frame;
    p->aux0 = reach;
    float ox = reach * p->velocity.x;
    float oy = reach * p->velocity.y;
    float oz = reach * p->velocity.z;
    float px = ox + origin.x;
    float py = oy + origin.y;
    float pz = oz + origin.z;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = oldSize * p->aux2;
    return 1;
}

// Casting elements: element 0 (pos.x nonzero) counts down and takes its alpha from the
// table (frozen while an emitting source is attached); the swirls (pos.x zero, or NaN) move by
// their velocity, which gets a random kick in [-k, k), damping and a pull back to the centre.
void ParticleSource::TickCasting(ParticleElement* e) {
    EI_NAN_AUDIT(e->pos.x);
    if (e->pos.x != 0.0f) {
        if ((flags & ParticleSourceFlag::Emitting) && attachedTo) return;
        if (e->ticks > 0) --e->ticks;
        e->cast.alpha = kCastAlphaByCountdown[elements[0].ticks];
        return;
    }
    float* pos = &e->swirl.position.x;
    float* velocity = &e->swirl.velocity.x;
    for (int i = 0; i < 3; ++i) pos[i] = velocity[i] + pos[i];
    float bound = EI_CAST_DRIFT;
    float low = -bound;
    for (int i = 0; i < 3; ++i) {
        float kick = Unsigned(Next()) * (bound - low) * EI_P_SCALE_1;
        velocity[i] = kick + velocity[i] * EI_CAST_DAMP + low - pos[i] * EI_CAST_PULL;
    }
}

// --- Protection spheres (0x2015-0x2017) and AntiMagic (0x201A) -----------------------------------

// Spheres: a particle on the sphere around the carrier (a random direction at 0.65 of
// the radius), flattened against the carrier's motion when it moves; one in eleven is larger. The
// size follows the life table of the kind.
int ParticleSource::EmitOrbit(Particle* p, int index) {
    if (index == 0 && !FollowCarrier(this)) return 0;
    float radius;
    radius = elements[0].sphere.radius;
    float big;
    if (Next() % 11 == 0) {
        float base = ParticleTypeOf(type) == ParticleType::Antimagic ? EI_BIG_ANTIMAGIC : ParticleTypeOf(type) == ParticleType::SphereElectricity ? EI_BIG_EARTH : EI_BIG_SPHERE;
        big = Unsigned(Next()) * EI_P_SCALE_1 + base;
    } else {
        big = Unsigned(Next()) * EI_SCALE_0_03 + EI_MOTION_KEEP;
    }
    p->drift.x = big;

    float x, y, z;
    RandomInBallZYX(x, y, z);
    Vector3 v;
    v.Construct(x, y, z);
    p->velocity = v;
    float shell = radius * EI_SHELL;
    float scale = shell / std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(p->velocity));
    p->velocity.x = scale * p->velocity.x;
    p->velocity.y = scale * p->velocity.y;
    p->velocity.z = scale * p->velocity.z;
    p->color = 0xFFFFFFFF;
    p->frame = static_cast<int>(Next() % 11);
    p->life = lifetime;
    p->frameTimer = 1;

    // The carrier's motion flattens the sphere: the part of the direction against the motion
    // (clamped to [0, 1] of it) is taken off.
    Vector3 offset;
    float speed = elements[0].sphere.speed;
    if (speed > EI_STILL_SPEED) {
        const Vector3& m = elements[1].pos;
        float t = -(DotUnrounded<Sum3::ZYX>(m, p->velocity) / speed);
        t = MinKeepA(t, 1.0f);
        t = MaxKeepB(t, 0.0f);
        Vector3 along;
        SubtractVectors(&offset, p->velocity, *ScaleVector(&along, m, t));
    } else {
        offset = p->velocity;
    }
    const Vector3& centre = elements[0].pos;
    float cx = origin.x + centre.x;
    float cy = origin.y + centre.y;
    float cz = origin.z + centre.z;
    p->size = 1.0f;
    float px = cx + offset.x;
    float py = offset.y + cy;
    float pz = offset.z + cz;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    if (ParticleTypeOf(type) == ParticleType::SphereFire) {
        float s = kSphereSizeByLife[p->life] * p->drift.x * radius;
        p->size = s + s;
    } else if (ParticleTypeOf(type) == ParticleType::Antimagic) {
        p->size = kAntimagicSizeByLife[p->life] * p->drift.x * radius;
    } else if (ParticleTypeOf(type) == ParticleType::SphereElectricity) {
        p->size = kSphereSizeByLife[p->life] * p->drift.x * radius;
    } else {
        p->size = kSphereSizeByLife[p->life] * p->drift.x * radius * EI_AIR_SIZE;
    }
    return 1;
}

// --- Casting (0x200B) and Healing (0x2006): rings of flames rising around the caster -------------
// emitState counts the flames emitted after the set-up (one per ring). The two types share the shapes;
// they differ in the ring count, timings, sizes and the spark pattern.

// Casting (0x200B): the first call places three rings (needs a carrier); then one flame
// per ring; then, while ring 0 is young, sparks scattered around ring 0's circle (72 per tick),
// else sparks around a random ring's centre.
int ParticleSource::EmitCastRing(Particle* p, int) {
    if (std::ssize(elements) == 0) {
        if (!attachedTo) {
            game::DebugLog(NoCarrierRing());
            return 0;
        }
        emitCount = 3;
        elements.resize(3);
        growth = 0.0f;
        PlaceRings(this, 45);
        emitCount *= 9;
    }
    int result;
    if (EmitRingFlame(this, p, result)) return result;
    if (elements[0].ticks < 9) {
        // On ring 0's circle, between the ring and a spread that narrows with the height.
        emitCount = 72;
        float ringRadius = RingRadius(elements[0]);
        float twoPi = EI_PI + EI_PI;
        std::uint32_t r = Next();
        p->color = 0xFFFFFFFF;
        p->frame = 1;
        float angle = Unsigned(r) * twoPi * EI_P_SCALE_1;
        int age = elements[0].ticks;
        if (age >= 5) age = 5;
        std::uint32_t spread = Next();
        p->frameTimer = 1;
        p->state = -1;
        p->life = static_cast<int>(spread % 4u) + age + 1;
        float inner = (ringRadius - height * EI_TWENTY) * EI_HALF + ringRadius;
        float reach = Unsigned(Next()) * (ringRadius - inner) * EI_P_SCALE_1 + inner;
        const ParticleElement& ring = elements[0];
        p->pos.x = std::sin(angle) * reach + (origin.x + ring.pos.x);
        p->pos.y = std::cos(angle) * reach + (elements[0].pos.y + origin.y);
        p->pos.z = elements[0].pos.z + origin.z;
    } else {
        const ParticleElement& ring = elements[Next() % static_cast<std::uint32_t>(std::ssize(elements))];
        p->frame = 1;
        p->life = 7;
        p->frameTimer = 1;
        p->color = 0xFFFFFFFF;
        p->state = -1;
        PlaceRingSpark(this, p, ring, RingRadius(ring) * EI_TENTH_B + EI_SPARK_SPREAD);
    }
    p->size = size * EI_TENTH_B;
    return 1;
}

// Casting particles: a flame (state = its ring) grows for 18 ticks, shrinks for 18 and
// rides its ring; then it burns out for 8 ticks (shrinking, alpha falling 30 per tick). Sparks
// (state -1) step through their frames.
int ParticleSource::UpdateCastRing(Particle* p) {
    int life = --p->life;
    if (life == 0) {
        if (p->state < 0 || p->state == kFlameState) return 0;
        p->state = kFlameState;
        p->life = 8;
        return 1;
    }
    int ring = p->state;
    if (ring == kFlameState) {
        p->size = p->size * EI_FADE_SIZE;
        p->color = ((p->color & 0xFF000000u) - 0x1E000001u) | 0xFFFFFF;
        return 1;
    }
    if (ring < 0) {
        ++p->frame;
        return 1;
    }
    p->frameTimer = 1;
    if (life < 36 && life >= 18) p->size = p->size - growth;
    else p->size = growth + p->size;
    FollowRing(this, p, ring);
    return 1;
}

// Casting rings: each counts down (stopping the emission at 0); the radius grows by the
// source's height for 18 ticks, then shrinks, then eases towards 20 times the height; while
// above 9 ticks the flame point turns around the centre and the ring rises.
void ParticleSource::TickCastRing(ParticleElement* e) {
    int t = --e->ticks;
    if (t == 0) {
        flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float* radius = &e->flames.radius;
    if (t >= 27) {
        *radius = height + (*radius);
    } else if (t >= 9) {
        *radius = (*radius) - height;
    } else {
        float target = height * EI_TWENTY;
        *radius = ((*radius) - target) * EI_HALF + target;
    }
    if (t >= 9) TurnRing(e);
}

// Healing (0x2006): the first call places 6 * size + 2 rings (needs a carrier); then
// one flame per ring; then sparks around a random ring's centre.
int ParticleSource::EmitHealing(Particle* p, int) {
    if (std::ssize(elements) == 0) {
        if (!attachedTo) {
            game::DebugLog(NoCarrierHealing());
            return 0;
        }
        emitCount = std::lround(size * EI_HEAL_RINGS + EI_HEAL_RINGS_BASE);
        elements.resize(emitCount);
        PlaceRings(this, 36);
        emitCount *= 3;
    }
    int result;
    if (EmitRingFlame(this, p, result)) return result;
    const ParticleElement& ring = elements[Next() % static_cast<std::uint32_t>(std::ssize(elements))];
    p->color = 0xFFFFFFFF;
    p->frame = 8;
    p->life = 8;
    p->frameTimer = 1;
    p->state = -1;
    PlaceRingSpark(this, p, ring, RingRadius(ring) * EI_HEAL_SPREAD);
    p->size = (size + EI_P_ONE) * EI_HEAL_SPARK_SIZE;
    return 1;
}

// Healing particles: like Casting's, but a flame cycles 8 frames, grows for its first
// 18 ticks of life and shrinks after, and burns out without shrinking.
int ParticleSource::UpdateHealing(Particle* p) {
    int life = --p->life;
    if (life == 0) {
        if (p->state < 0 || p->state == kFlameState) return 0;
        p->state = kFlameState;
        p->life = 8;
        return 1;
    }
    int ring = p->state;
    if (ring == kFlameState) {
        p->color = ((p->color & 0xFF000000u) - 0x1E000001u) | 0xFFFFFF;
        return 1;
    }
    if (ring < 0) {
        ++p->frame;
        return 1;
    }
    if (++p->frame >= 8) p->frame = 0;
    p->frameTimer = 1;
    if (life >= 18) p->size = growth + p->size;
    else p->size = p->size - growth;
    FollowRing(this, p, ring);
    return 1;
}

// Healing rings: each counts down (stopping the emission at 0); the radius grows by the
// source's height for 18 ticks, then shrinks; the flame point keeps turning and the ring rising.
void ParticleSource::TickHealing(ParticleElement* e) {
    int t = --e->ticks;
    if (t == 0) {
        flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float* radius = &e->flames.radius;
    if (t >= 18) *radius = height + (*radius);
    else *radius = (*radius) - height;
    TurnRing(e);
}

// --- CastingIllusion, Domination, Enchantment, Healing (0x202B-0x202E): orbiting lights ---------
// One element per orbiter (lifetime of them, added one per emission).

// Until all orbiters exist each emission adds one (needs a carrier); then sparks start at a random point
// between an orbiter's previous and current position and fly off with a random velocity from the spread box.
int ParticleSource::EmitCastSwirl(Particle* p, int) {
    if (std::ssize(elements) < lifetime) {
        if (!attachedTo) {
            game::DebugLog(NoCarrierCasting());
            return 0;
        }
        return AddOrbiter(this, p);
    }
    int j = static_cast<int>(Next() % static_cast<std::uint32_t>(lifetime));
    ParticleElement* e = &elements[j];
    std::uint32_t r = Next();
    p->color = 0xFFFFFFFF;
    p->frame = 1;
    p->life = 28;
    p->frameTimer = 1;
    p->state = j;
    float t = Unsigned(r) * EI_P_SCALE_1;
    p->aux2 = (Unsigned(Next()) * EI_SWIRL_SIZE_SCALE + EI_SWIRL_SIZE_BASE) *
                                    e->orbiter.scale * EI_SWIRL_GROW;
    p->aux1 = 1.03f;     // 1.03
    float* v = &p->velocity.x;
    const float* low = &spreadMin.x;
    const float* high = &spreadMax.x;
    for (int i = 0; i < 3; ++i) v[i] = Unsigned(Next()) * (high[i] - low[i]) * EI_P_SCALE_1 + low[i];
    const float* previous = &e->orbiter.previous.x;
    float px = previous[0] + v[0];
    float py = v[1] + previous[1];
    float pz = v[2] + previous[2];
    p->size = 1.0f;
    p->pos.y = py;
    p->pos.z = pz;
    p->pos.x = px;
    float u = EI_P_ONE - t;
    float a0 = u * e->pos.x;
    float a1 = u * e->pos.y;
    float a2 = u * e->pos.z;
    float b0 = t * previous[0];
    float b1 = t * previous[1];
    float b2 = t * previous[2];
    float c0 = b0 + a0;
    float c1 = b1 + a1;
    float c2 = a2 + b2;
    v[0] = c0 + v[0];
    v[1] = c1 + v[1];
    v[2] = c2 + v[2];
    p->size = (Unsigned(Next()) * EI_SWIRL_SPARK_SCALE + EI_SWIRL_SPARK_BASE) * size *
                    EI_SWIRL_SPARK_SIZE;
    return 1;
}

// A marker (state < 0) sits on its orbiter with a flickering size and, once the source stops emitting (or
// loses its carrier), fades by the table and dies. A spark fades over its last 7 ticks, cycles frames 12..15,
// shrinks, and moves by its velocity, whose z changes by a growing step.
int ParticleSource::UpdateCastSwirl(Particle* p) {
    int state = p->state;
    if (state < 0) {
        const ParticleElement& e = elements[-(state + 1)];
        p->pos = e.pos;
        p->size = 1.0f;
        p->size = (Unsigned(Next()) * EI_MARKER_SIZE_SCALE + EI_MARKER_SIZE_BASE) * size *
                        EI_POINT_FOUR_C;
        if ((flags & ParticleSourceFlag::Emitting) && attachedTo) return 1;
        int life = p->life;
        if (life < 0) return 0;
        p->color = (static_cast<std::uint32_t>(kRiseFallAlpha[life + 1]) << 24) | 0xFFFFFF;
        p->life = life - 1;
        return 1;
    }
    int life = --p->life;
    if (life == 0) return 0;
    if (life < 7) p->color = (static_cast<std::uint32_t>(ByLife(kRiseFallAlpha, life, 0)) << 24) | 0xFFFFFF;
    if (++p->frame == 16) p->frame = 12;
    float shrunk = p->size * EI_SWIRL_SHRINK;
    p->pos = p->velocity;
    p->size = shrunk;
    float* step = &p->aux2;
    p->velocity.z = p->velocity.z + (*step);
    *step = p->aux1 * (*step);
    return 1;
}

// Orbiters turn about the source's vertical axis (by -turn) and keep their previous world position.
void ParticleSource::TickCastSwirl(ParticleElement* e) {
    float c = e->orbiter.turnCos;
    float s = e->orbiter.turnSin;
    float* relative = &e->orbiter.relative.x;
    float y = c * relative[1] - relative[0] * s;
    float x = relative[0] * c + relative[1] * s;
    e->orbiter.previous = e->pos;
    relative[0] = x;
    relative[1] = y;
    float px = origin.x + relative[0];
    float py = origin.y + relative[1];
    float pz = origin.z + relative[2];
    e->pos.x = px;
    e->pos.y = py;
    e->pos.z = pz;
}

// --- Link (0x2014): a beam of sparks from the source along its extent --------------------------

// Link: a spark at the source with a random offset across the beam (from the spread
// box), a twist rate from its distance to the axis and a random speed along the beam (fast and
// large sparks near the axis now and then).
int ParticleSource::EmitLink(Particle* p, int) {
    if (std::ssize(elements) == 0) SetUpLink(this);
    p->color = 0xFFFFFF;
    float a = SignedUnit();
    float b = SignedUnit();
    float c = Unsigned(Next()) * (spreadMax.z - spreadMin.z) * EI_P_SCALE_1 + spreadMin.z;
    p->velocity.x = a * spreadMin.x;
    p->velocity.y = b * spreadMin.y;
    p->velocity.z = c;
    float reach = std::sqrt(b * b + a * a);
    float reachRounded = reach;
    float twist = reach * EI_LINK_TWIST;
    p->aux0 = std::cos(twist);
    p->aux1 = std::sin(twist);
    Vector3 start;
    start.Construct(p->velocity.x + origin.x, origin.y + p->velocity.y,
                    origin.z + p->velocity.z);
    p->PlaceAt(start);
    float factor;
    EI_NAN_AUDIT(reachRounded);
    if ((Next() & 3) == 0 && reachRounded < EI_LINK_HALF) {
        p->drift.y = g_clientRandom.Range(F(0x3DCCCCCD), F(0x3DF5C28F));     // 0.1 .. 0.12
        factor = g_clientRandom.Range(F(0x3E051EB8), F(0x3E23D70A));                           // 0.13 .. 0.16
    } else {
        p->drift.y = g_clientRandom.Range(F(0x3E2E147B), F(0x3E800000));     // 0.17 .. 0.25
        factor = g_clientRandom.Range(F(0x3CF5C28F), F(0x3D4CCCCD));                           // 0.03 .. 0.05
    }
    p->drift.z = factor * size;
    p->drift.x = 0.0f;
    p->size = p->drift.z;
    p->frame = static_cast<int>(Next() % static_cast<std::uint32_t>(frameCount + 1));
    return 1;
}

// Link sparks run along the beam (drift.x the distance travelled, drift.y the speed), spinning around the axis;
// the beam bulges in the middle and the alpha peaks there (capped by element 0). Dropped past the beam's end.
int ParticleSource::UpdateLink(Particle* p) {
    p->frame = (p->frame + 1) % frameCount;
    float travelled = p->drift.y + p->drift.x;
    p->drift.x = travelled;
    ParticleElement* e = elements.data();
    float along = travelled * e->link.inverseLength;
    if (travelled > e->link.length) return 0;

    float c = p->aux0;
    float s = p->aux1;
    float turnedY = p->velocity.y * c + p->velocity.x * s;
    float turnedX = c * p->velocity.x - p->velocity.y * s;
    p->velocity.x = turnedX;
    p->velocity.y = turnedY;
    float ax = along * extent.x;
    float ay = along * extent.y;
    float az = along * extent.z;
    const float* direction = &elements[1].linkAxis.axis.x;
    const float* sideAxis = &elements[2].linkAxis.axis.x;
    const float* cross = &elements[3].linkAxis.axis.x;
    float q = p->velocity.z;
    float dx = q * direction[0];
    float dy = q * direction[1];
    float dz = q * direction[2];
    float cx = turnedY * cross[0];
    float cy = turnedY * cross[1];
    float cz = turnedY * cross[2];
    float vx = p->velocity.x;
    float sx = vx * sideAxis[0];
    float sy = vx * sideAxis[1];
    float sz = vx * sideAxis[2];
    float bx = sx + origin.x;
    float by = sy + origin.y;
    Vector3 p1;
    p1.Construct(bx + cx, by + cy, sz + origin.z + cz);
    Vector3 p2;
    p2.Construct(p1.x + dx, p1.y + dy, p1.z + dz);
    Vector3 p3;
    p3.Construct(p2.x + ax, p2.y + ay, p2.z + az);
    p->pos = p3;

    float h = along - EI_LINK_HALF;
    p->size = 1.0f;
    p->size = p->drift.z;
    p->pos.z = (EI_LINK_QUARTER - h * h) * elements[0].link.length * EI_LINK_BULGE + p3.z;
    // Negative away from the middle: as an unsigned word above any cap.
    std::uint32_t fade = static_cast<std::uint32_t>(static_cast<int>((EI_LINK_HALF - std::fabs(h)) * EI_LINK_ALPHA));
    std::uint32_t cap = static_cast<std::uint32_t>(elements[0].ticks);
    if (cap < fade) fade = cap;
    p->color = (fade << 24) | 0xFFFFFF;
    return 1;
}

// Link element 0: once the source stops emitting the alpha cap falls by 16 per tick
// (not below 0). (The other elements' caps are unused.)
void ParticleSource::TickLink(ParticleElement* e) {
    if (!(flags & ParticleSourceFlag::Emitting)) e->ticks -= 16;
    std::int32_t cap = e->ticks;
    e->ticks = cap < 0 ? 0 : cap;
}

// --- FireBlast (0x2002): fifteen fragments flying out, flames and smoke at the centre ------------

// FireBlast: a random kind per particle (state = Next() % 29 - 14). The first particle
// after the fragments' first tick is the core flash. Negative kinds are flames inside the blast
// (a random point in the ball scaled by the radius, moving outwards; the earlier in the blast and
// the further out the more frames they skip); the others are smoke along fragment `state`'s path.
int ParticleSource::EmitBlast(Particle* p, int) {
    ParticleElement* fragments = elements.data();
    int kind = static_cast<int>(Next() % 29) - 14;
    p->color = 0xFFFFFFFF;
    p->state = kind;
    if (particles.empty() && fragments->fragment.age == 1) {
        p->state = kBlastCore;
        p->pos = origin;
        p->frame = 15;
        p->size = 0.0f;
        return 1;
    }
    if (kind < 0) {
        p->life = 0;
        float x, y, z;
        RandomInBallZYX(x, y, z);
        Vector3 v;
        v.Construct(x, y, z);
        float outside = EI_P_ONE - LengthSquaredUnrounded<Sum3::ZYX>(v);
        float outsideRounded = outside;
        float scale = size;
        v.x = scale * v.x;
        v.y = v.y * scale;
        v.z = v.z * scale;
        float radius = elements[0].fragment.radius;
        float ax = v.x * radius;
        float ay = v.y * radius;
        float az = radius * v.z;
        float px = ax + origin.x;
        float py = ay + origin.y;
        float pz = az + origin.z;
        p->size = 1.0f;
        p->pos.y = py;
        p->pos.x = px;
        p->pos.z = pz;
        float spreadRate = elements[0].fragment.growth;
        p->velocity.x = v.x * spreadRate;
        p->size = 0.0f;
        p->velocity.y = v.y * spreadRate;
        p->velocity.z = v.z * spreadRate;
        int age = fragments->fragment.age;
        float threshold = (age / 7) + EI_BLAST_POINT_TWO;
        EI_NAN_AUDIT(outside);
        if (outside < threshold) {
            p->frame = 8;
            BlastFlameTiming(this, p);
            p->aux2 = Unsigned(Next()) * EI_BLAST_RISE_SCALE + EI_BLAST_RISE;
        } else {
            p->frame = static_cast<int>(Next() % 3);
            BlastFlameTiming(this, p);
            float t = Unsigned(Next()) * EI_BLAST_RISE_SCALE * EI_BLAST_HALF;
            p->aux2 =
                t + outsideRounded * EI_BLAST_RISE / threshold + EI_BLAST_RISE_BASE;
        }
        return 1;
    }

    p->frame = static_cast<int>(Next() % 3);
    ParticleElement* e = &fragments[kind];
    EI_NAN_AUDIT(e->fragment.frame);
    if (e->fragment.frame == 0.0f) p->frame = 8;
    float u = Unsigned(Next()) * EI_P_SCALE_1;
    float scale = size;
    const float* velocity = &e->fragment.velocity.x;
    float ux = u * velocity[0];
    float uy = u * velocity[1];
    float uz = u * velocity[2];
    float sx = ux * scale;
    float sy = uy * scale;
    float sz = uz * scale;
    float px = e->pos.x + origin.x;
    float py = origin.y + e->pos.y;
    Vector3 start;
    start.Construct(px - sx, py - sy, origin.z + e->pos.z - sz);
    p->pos = start;
    p->size = 1.0f;
    float g = Unsigned(Next()) * EI_BLAST_DEBRIS_SCALE + EI_BLAST_POINT_SEVEN;
    float sized = g * (EI_BLAST_POINT_THREE - u * EI_BLAST_POINT_15) *
                   (e->fragment.radius * EI_BLAST_SEVENTH + EI_BLAST_POINT_THREE);
    p->size = sized * e->fragment.sizeFactor * size;
    Vector3 drift;
    ScaleVector(&drift, e->fragment.velocity, size);
    p->velocity = drift;
    return 1;
}

// FireBlast particles. Smoke (state >= 0) drifts with a 0.7 drag, shrinks to 0.8 and lasts until its frame
// reaches 7 (mod 8). The core flash (frame 15) grows once to 2.3 times the source's size, then disappears; it
// keeps the source alive while the fragments are younger than 3 ticks. Flames move by their velocity (slowing
// with the fragments' drag), swell, step through frames at an accelerating rate and fade by the frame.
int ParticleSource::UpdateBlast(Particle* p) {
    ParticleElement* fragments = elements.data();
    int state = p->state;
    if (state >= 0) {
        int frame = ++p->frame;
        if ((frame & 7) == 7) return 0;
        p->color = 0xFFFFFFFF;
        p->pos.x = p->velocity.x + p->pos.x;
        p->pos.y = p->velocity.y + p->pos.y;
        p->pos.z = p->velocity.z + p->pos.z;
        p->velocity.x = p->velocity.x * EI_BLAST_POINT_SEVEN;
        p->velocity.y = p->velocity.y * EI_BLAST_POINT_SEVEN;
        p->velocity.z = p->velocity.z * EI_BLAST_POINT_SEVEN;
        p->size = p->size * EI_BLAST_POINT_EIGHT;
        return 1;
    }
    int frame = p->frame;
    if (frame >= 14) {
        if (state != kBlastCore) return 0;
        EI_NAN_AUDIT(p->size);
        if (p->size == 0.0f) p->size = size * EI_BLAST_CORE_SIZE;
        else p->size = 0.0f;
        return fragments->fragment.age < 3;
    }
    p->pos.x = p->pos.x + p->velocity.x;
    ++p->life;
    p->pos.y = p->velocity.y + p->pos.y;
    p->pos.z = p->velocity.z + p->pos.z;
    float flameSize = p->aux0;
    p->size = flameSize;
    p->size = flameSize * (frame > 7 ? EI_BLAST_SWELL_LATE : EI_BLAST_SWELL_EARLY);
    float* step = &p->aux2;
    p->aux1 = p->aux1 + (*step);
    *step = (*step) * EI_BLAST_FRAME_ACCEL;
    p->frame = std::lround(p->aux1);
    p->aux0 = p->size;
    float drag = elements[0].fragment.decay - EI_BLAST_DRAG;
    p->velocity.x = drag * p->velocity.x;
    p->velocity.y = drag * p->velocity.y;
    p->velocity.z = drag * p->velocity.z;
    // A flame's frame can step past the table: the original read kAcidSpread after it there (0.0f: alpha 0).
    p->color = (static_cast<std::uint32_t>(p->frame < std::ssize(kBlastAlpha) ? kBlastAlpha[p->frame] : 0) << 24) | 0xFFFFFF;
    return 1;
}

// FireBlast fragments: the flame radius grows (its growth decaying), the age counts up
// (the emission stops after 6 ticks), the fragment flies by its velocity scaled by the source's
// size, slowing to 3/4 per tick and falling 0.03 per tick.
void ParticleSource::TickBlast(ParticleElement* e) {
    float* radius = &e->fragment.radius;
    float* growthRate = &e->fragment.growth;
    *radius = (*radius) + (*growthRate);
    int age = ++e->fragment.age;
    *growthRate = e->fragment.decay * (*growthRate);
    if (age > 6) flags &= ~ParticleSourceFlag::Emitting;
    float scale = size;
    float* velocity = &e->fragment.velocity.x;
    float dx = scale * velocity[0];
    float dy = scale * velocity[1];
    float dz = scale * velocity[2];
    e->pos.x = dx + e->pos.x;
    e->pos.y = dy + e->pos.y;
    e->pos.z = dz + e->pos.z;
    velocity[0] = velocity[0] * EI_BLAST_SLOW;
    velocity[1] = velocity[1] * EI_BLAST_SLOW;
    velocity[2] = velocity[2] * EI_BLAST_SLOW;
    velocity[2] = velocity[2] - EI_BLAST_DRAG;
}

// --- PoisonFog (0x2007, 0x2008): a disc of cloud puffs laid on the ground ------------------------

// PoisonFog: emits nothing itself. When the source has no particles it lays one puff
// per grid cell (jittered by a quarter cell) inside the disc of radius `size` around its position,
// on the ground, straight into its list (from the pool), and grows the bounds. Always 0.
int ParticleSource::EmitPoisonFog(Particle*, int) {
    if (!particles.empty()) return 0;
    float radius = size;
    int n = static_cast<int>((size + EI_P_ONE) / EI_FOG_CELL);
    for (int i = -n; i <= n; ++i) {
        float row = static_cast<float>(i);
        float radiusSquared = radius * radius;
        for (int j = -n; j <= n; ++j) {
            float x = j * EI_FOG_CELL;
            float y = row * EI_FOG_CELL;
            float high = EI_FOG_CELL * EI_FOG_QUARTER;
            float low = EI_FOG_CELL * EI_FOG_JITTER_LOW;
            x = Unsigned(Next()) * (high - low) * EI_P_SCALE_1 + low + x;
            float yu = Unsigned(Next()) * (high - low) * EI_P_SCALE_1 + low + y;
            y = yu;
            if (yu * y + x * x >= radiusSquared) continue;
            Particle* q = Manager()->AcquireParticle();
            if (!q) continue;
            float px = x + localPosition.x;
            x = px;
            y = y + localPosition.y;
            q->pos.x = px;
            q->pos.y = y;
            q->size = Unsigned(Next()) * EI_FOG_SIZE_SCALE + EI_FOG_SIZE_BASE - EI_FOG_QUARTER;
            q->pos.z = GroundHeight(x, y) + q->size;
            if (ParticleTypeOf(type) == ParticleType::PoisonCloud) {
                q->color = 0xFFFFAF7F;
            } else {
                int a = std::lround(alpha * EI_FOG_ALPHA);
                if (a > 255) a = 255;
                q->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
            }
            q->life = lifetime;
            q->frame = static_cast<int>(Next() % static_cast<std::uint32_t>(frameCount));
            q->frameTimer = frameDelay;
            std::memcpy(&q->prevPos, &q->pos, sizeof(Vector3) + sizeof(float));
            q->dead = 0;
            q->prevColor = q->color;
            q->prevFrame = q->frame;
            boundsMin.x = MinKeepA(q->pos.x, boundsMin.x);
            boundsMin.y = MinKeepA(q->pos.y, boundsMin.y);
            boundsMin.z = MinKeepA(q->pos.z, boundsMin.z);
            boundsMax.x = MaxKeepB(boundsMax.x, q->pos.x);
            boundsMax.y = MaxKeepB(boundsMax.y, q->pos.y);
            boundsMax.z = MaxKeepB(boundsMax.z, q->pos.z);
            particles.push_back(q);
        }
    }
    return 0;
}

// --- Geyser (0x2009): three vents that erupt in turn -----------------------------------------------
// A vent's pos: the jet's two direction angles (degrees off vertical, turned to radians) and its strength.

// Geyser: the first particle marks the source. Then a drop from a random vent: while
// the vent erupts it flies along the vent's direction (jittered by up to 20 / 10 degrees) with the
// vent's strength; otherwise a short-lived puff of mist near the source.
int ParticleSource::EmitGeyser(Particle* p, int) {
    int vent = static_cast<int>(Next() % static_cast<std::uint32_t>(std::ssize(elements)));
    p->color = 0xFFFFFFFF;
    p->pos = origin;
    if (particles.empty()) {
        p->size = 0.0f;
        p->state = 1;
        p->frame = 15;
        return 1;
    }
    p->state = 0;
    p->frame = static_cast<int>(Next() % 12);
    const ParticleElement& e = elements[vent];
    float drop = size * EI_GEYSER_DROP;
    p->life = 20;
    p->size = drop;
    if (e.ticks > 0) {
        p->life = 0x7FFFFFFF;
        float radian = ei::kRadiansPerDegree;
        float yaw = (Unsigned(Next()) * EI_GEYSER_YAW_SCALE - EI_GEYSER_YAW_LOW) * radian + e.pos.x;
        float tilt = (Unsigned(Next()) * EI_GEYSER_TILT_SCALE - EI_GEYSER_TILT_LOW) * radian + e.pos.y;
        float power = Unsigned(Next()) * EI_GEYSER_JET_SCALE + e.pos.z - EI_GEYSER_JET_LOW;
        float sinTilt = std::sin(tilt);
        p->drift.x = std::sin(yaw) * sinTilt * power * height;
        p->drift.y = std::cos(yaw) * sinTilt * power * height;
        p->drift.z = std::cos(tilt) * power * height;
        p->pos.x = SpreadAround(p->pos.x, spreadMin.x, spreadMax.x);
        p->pos.y = SpreadAround(p->pos.y, spreadMin.y, spreadMax.y);
        float z = SpreadAround(p->pos.z, spreadMin.z, spreadMax.z);
        p->size = 0.0f;
        p->velocity.x = drop;
        p->pos.z = z;
        return 1;
    }
    p->life = 1;
    p->drift = {};
    float high = spreadMax.x * EI_GEYSER_HALF;
    float low = spreadMin.x * EI_GEYSER_HALF;
    p->pos.x = SpreadAround(p->pos.x, low, high);
    low = spreadMin.y * EI_GEYSER_HALF;
    high = spreadMax.y * EI_GEYSER_HALF;
    p->pos.y = SpreadAround(p->pos.y, low, high);
    p->size = size * EI_GEYSER_MIST;
    return 1;
}

// Geyser: the marker flickers while the source emits (dropped once it stops); drops fly by drift
// (gravity pulling drift.z down by 0.06 height per tick), fade 8 alpha per tick (dropped below 10),
// shrink by 0.9 (velocity.x holds the size) and cycle 4 frames.
int ParticleSource::UpdateGeyser(Particle* p) {
    if (p->state != 0) {
        if (!(flags & ParticleSourceFlag::Emitting)) {
            p->size = 0.0f;
            return 0;
        }
        p->size = (Unsigned(Next()) * EI_GEYSER_MARK_SCALE + EI_GEYSER_MARK_BASE) * size;
        return 1;
    }
    std::uint32_t a = p->color >> 24;
    if (a <= 9) return 0;
    if (--p->life <= 0) return 0;
    float shrunk = p->velocity.x * EI_GEYSER_SHRINK;
    p->velocity.x = shrunk;
    p->pos.x = p->drift.x + p->pos.x;
    int frame = p->frame;
    p->frame = (frame & ~3) | ((frame + 1) & 3);
    p->pos.y = p->drift.y + p->pos.y;
    p->pos.z = p->pos.z + p->drift.z;
    p->size = shrunk;
    p->color = ((a << 24) - 0x08000001u) | 0xFFFFFF;
    p->drift.z = p->drift.z - height * EI_GEYSER_GRAVITY;
    return 1;
}

// Geyser vents: an eruption counts down, then a pause of 3..36 ticks, then a new
// eruption of 1..7 ticks in a random direction (up to 10 degrees off vertical) and strength.
void ParticleSource::TickGeyser(ParticleElement* e) {
    if (e->ticks > 0) {
        if (--e->ticks == 0) e->vent.pause = static_cast<int>(Next() % 34) + 3;
        return;
    }
    if (--e->vent.pause > 0) return;
    e->ticks = static_cast<int>(Next() % 7) + 1;
    float radian = ei::kRadiansPerDegree;
    float yaw = Unsigned(Next()) * EI_GEYSER_ANGLE_SCALE * radian;
    float tilt = Unsigned(Next()) * EI_GEYSER_TILT_RANGE * radian;
    float power = Unsigned(Next()) * EI_GEYSER_POWER_SCALE + EI_GEYSER_POWER_BASE;
    e->pos.x = yaw;
    e->pos.y = tilt;
    e->pos.z = power;
}

// --- Shower (0x200F, 0x2031-0x2033): sparks thrown out in a few random directions ---------------

// Shower: the first call chooses the directions. A spark leaves the source along a
// random direction (jittered by +-0.03, scaled to the source's size and 1/6 of it per tick, one in
// eight half as fast again), already a random part of its first step out.
int ParticleSource::EmitShower(Particle* p, int) {
    if (elements[0].ray.age < 0) ChooseShowerRays(this);
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frame = elements[0].ticks;
    float a = Unsigned(Next()) * EI_SHOWER_JITTER_SCALE - EI_SHOWER_JITTER_LOW;
    float b = Unsigned(Next()) * EI_SHOWER_JITTER_SCALE - EI_SHOWER_JITTER_LOW;
    float c = Unsigned(Next()) * EI_SHOWER_JITTER_SCALE - EI_SHOWER_JITTER_LOW;
    const ParticleElement& ray = elements[Next() % static_cast<std::uint32_t>(frameDelay)];
    float scale = size;
    float ax = a + ray.pos.x;
    b = b + ray.pos.y;
    c = c + ray.pos.z;
    p->pos = origin;
    p->size = typeParam;
    a = scale * ax;
    b = scale * b;
    c = scale * c;
    float speed = EI_P_ONE / std::sqrt(c * c + b * b + a * a) * size * EI_SHOWER_SPEED;
    if ((Next() & 7) == 0) speed = speed * EI_SHOWER_FAST;
    p->drift.x = speed * a;
    p->velocity.x = 0.9f;     // 0.9: the speed kept per tick
    p->velocity.y = 0.8f;     // 0.8: the size kept per tick
    p->drift.y = speed * b;
    p->drift.z = speed * c;
    float t = Unsigned(Next()) * EI_P_SCALE_1;
    float dx = t * p->drift.x;
    float dy = t * p->drift.y;
    float dz = t * p->drift.z;
    p->pos.x = dx + p->pos.x;
    p->pos.y = dy + p->pos.y;
    p->pos.z = dz + p->pos.z;
    return 1;
}

// Shower sparks move, slow down (by velocity.x), fall by 0.05 size per tick, shrink (by velocity.y)
// and fade by the life table.
int ParticleSource::UpdateShower(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    p->pos.x = p->drift.x + p->pos.x;
    p->color = (static_cast<std::uint32_t>(ByLife(kShowerAlpha, life, 0)) << 24) | 0xFFFFFF;   // before: kAcidSize's zero tail
    p->pos.y = p->drift.y + p->pos.y;
    p->pos.z = p->pos.z + p->drift.z;
    p->size = p->velocity.y * p->size;
    float keep = p->velocity.x;
    p->drift.x = keep * p->drift.x;
    p->drift.y = keep * p->drift.y;
    p->drift.z = keep * p->drift.z;
    p->drift.z = p->drift.z - size * EI_SHOWER_GRAVITY;
    return 1;
}

// Shower directions age; after 5 ticks the source stops emitting and lets go of its carrier.
void ParticleSource::TickShower(ParticleElement* e) {
    if (++e->ray.age > 5) {
        flags &= ~ParticleSourceFlag::Emitting;
        Attach(nullptr);
    }
}

// --- Bag (0x203C): eighty coloured balls bouncing around the carrier ----------------------------------
// A ball's ticks run down to its next throw (-13: no carrier, the ball is gone).

// Bag: the first 80 particles are the balls (state = their index; the first one fills the size table: 0.93 to
// the power of the age); later ones are sparks along a random ball's last step, in the ball's colour.
int ParticleSource::EmitBag(Particle* p, int index) {
    if (std::ssize(particles) < 80) {
        if (index == 0) {
            float t = EI_P_ONE;
            for (int k = 0; k < 12; ++k) {
                g_bagSizes[k] = t;
                t = t * EI_BAG_SHRINK;
            }
        }
        std::uint32_t r = Next();
        p->state = lib::Count(particles);
        p->color = 0xFFFFFF;
        p->life = 15;
        p->frame = static_cast<int>(r & 3);
        p->pos = origin;
        p->size = 0.0f;
        return 1;
    }
    const ParticleElement& ball = elements[Next() % 80];
    int age = ball.ball.age;
    p->color = BagRgb(age);
    int life = ball.ticks + 1;
    if (life > 5) life = 5;
    p->life = life;
    p->frame = 4;
    p->state = -1;
    float u = Unsigned(Next()) * EI_P_SCALE_1 - EI_P_ONE;
    const float* step = &ball.ball.step.x;
    float ax = u * step[0];
    float ay = u * step[1];
    float az = u * step[2];
    float px = ax + ball.pos.x;
    float py = ay + ball.pos.y;
    float pz = az + ball.pos.z;
    p->size = 1.0f;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = g_bagSizes[age] * size * EI_BAG_SPARK_SIZE;
    return 1;
}

// Bag: a spark fades by its life and steps a frame; a ball follows its element (dropped
// once the ball is gone), cycles 4 frames, takes the colour and size of its age, and while the
// ball rests (no last step) its previous position is pinned to the current one.
int ParticleSource::UpdateBag(Particle* p) {
    int state = p->state;
    if (state < 0) {
        int life = --p->life;
        if (life <= 0) return 0;
        ++p->frame;
        p->color = (static_cast<std::uint32_t>(BagAlpha()[life]) << 24) + (p->color & 0xFFFFFF);
        return 1;
    }
    ParticleElement* ball = &elements[state];
    if (ball->ticks == kBagGone) return 0;
    p->frame = (p->frame + 1) & 3;
    p->pos = ball->pos;
    p->size = 1.0f;
    int age = ball->ball.age;
    p->size = g_bagSizes[age] * size * EI_BAG_BALL_SIZE;
    int fade = p->life > 9 ? 9 : p->life;
    p->color = (static_cast<std::uint32_t>(BagAlpha()[fade]) << 24) | BagRgb(age);
    const float* step = &ball->ball.step.x;
    auto isZero = [](float f) {
        EI_NAN_AUDIT(f);
        return f == 0.0f;
    };
    if (isZero(step[0]) && isZero(step[1]) && isZero(step[2])) {
        std::memcpy(&p->prevPos, &p->pos, sizeof(Vector3) + sizeof(float));
        p->prevColor = p->color;
        p->size = size * EI_BAG_BALL_SIZE;
    }
    return 1;
}

// Bag balls: when a ball's countdown runs out it is thrown again from the source (a random kind 0-2, a random
// offset, a random velocity of 0.2 size) and rests 7-9 ticks; while flying it ages, now and then gets a
// random kick, and moves (its step is kept). Without a carrier the ball is gone.
void ParticleSource::TickBag(ParticleElement* e) {
    if (--e->ticks < 0) {
        if (!attachedTo) {
            e->ticks = kBagGone;
            return;
        }
        e->ball.age = static_cast<int>(Next() % 3);
        e->pos = origin;
        float low, high;
        float r = BagSpread(this, low, high);
        e->pos.x = r + e->pos.x + low;
        r = BagSpread(this, low, high);
        e->pos.y = r + low + e->pos.y;
        r = BagSpread(this, low, high);
        e->pos.z = r + e->pos.z + low;
        float x, y, z;
        RandomInBallZYX(x, y, z);
        Vector3 v;
        v.Construct(x, y, z);
        float scale = size;
        float sx = v.x * scale;
        float sy = v.y * scale;
        Vector3 velocity;
        velocity.Construct(sx * EI_BAG_SPREAD_HIGH, sy * EI_BAG_SPREAD_HIGH,
                           scale * v.z * EI_BAG_SPREAD_HIGH);
        e->ball.velocity = velocity;
        e->ticks = static_cast<int>(Next() % 3) + 7;
        e->ball.step = {};
        return;
    }
    ++e->ball.age;
    if (Next() % 6 == 0) {
        float x, y, z;
        for (int tries = 10;;) {
            x = g_clientRandom.Range(-1.0f, 1.0f);
            y = g_clientRandom.Range(-1.0f, 1.0f);
            z = g_clientRandom.Range(-1.0f, 1.0f);
            float lengthSquared = LengthSquaredUnrounded<Sum3::XYZ>(x, y, z);   // (y + x) + z
            if (lengthSquared < EI_P_ONE) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        Vector3 v;
        v.Construct(x, y, z);
        float scale = size;
        Vector3 w;
        w.Construct(v.x * scale, v.y * scale, v.z * scale);
        Vector3 kick;
        kick.Construct(w.x * EI_BAG_KICK, w.y * EI_BAG_KICK, w.z * EI_BAG_KICK);
        float* velocity = &e->ball.velocity.x;
        velocity[0] = kick.x + velocity[0];
        velocity[1] = kick.y + velocity[1];
        velocity[2] = kick.z + velocity[2];
    }
    const Vector3& velocity = e->ball.velocity;
    e->pos.AddInPlace(velocity);
    e->ball.step = velocity;
}

// --- 0x202F: sparks rising in a ring around the carrier ------------------------------------------

// 0x202F: a spark on a ring of random radius around the carrier (0.9-1.1 of the ring),
// at a random height of the spread box above the ring's foot, rising faster and faster.
int ParticleSource::EmitColumn(Particle* p, int) {
    if (std::ssize(elements) == 0 && !SetUpColumn(this)) return 0;
    p->color = 0xFFFFFFFF;
    p->frame = 1;
    p->life = 13;
    p->frameTimer = 1;
    p->aux2 = (Unsigned(Next()) * EI_COLUMN_RISE_SCALE + EI_COLUMN_RISE_BASE) *
                                    elements[0].column.height * EI_COLUMN_RISE;
    float ring = Unsigned(Next()) * EI_COLUMN_RING_SCALE + EI_COLUMN_RING_BASE;
    float angle = Unsigned(Next()) * EI_COLUMN_ANGLE_SCALE;
    p->velocity.x = std::sin(angle) * ring;
    p->velocity.y = std::cos(angle) * ring;
    float z = Unsigned(Next()) * (spreadMax.z - spreadMin.z) * EI_P_SCALE_1 + spreadMin.z;
    p->velocity.z = z;
    p->velocity.z = z + elements[0].column.foot;
    float px = origin.x + p->velocity.x;
    float py = p->velocity.y + origin.y;
    float pz = p->velocity.z + origin.z;
    p->size = 1.0f;
    p->pos.y = py;
    p->pos.z = pz;
    p->pos.x = px;
    p->size = (Unsigned(Next()) * EI_COLUMN_SIZE_SCALE + EI_COLUMN_SIZE_BASE) * size * EI_COLUMN_SIZE;
    return 1;
}

// 0x202F sparks: fade by life, step a frame, follow the source at their offset, shrink
// to 0.97 per tick and rise (the rise growing by 5% per tick).
int ParticleSource::UpdateColumn(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    float oldSize = p->size;
    p->color = (ColumnAlpha(life) << 24) | 0xFFFFFF;
    ++p->frame;
    float px = origin.x + p->velocity.x;
    float py = origin.y + p->velocity.y;
    float pz = origin.z + p->velocity.z;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = oldSize * EI_COLUMN_SHRINK;
    float* rise = &p->aux2;
    p->velocity.z = p->velocity.z + (*rise);
    *rise = (*rise) * EI_COLUMN_SPEED_UP;
    return 1;
}

void ParticleSource::TickColumn(ParticleElement* e) { ++e->ticks; }

// --- Mushroom and Transform (0x200E, 0x2052): the cloud of the mushroom ---------------------------

// Mushroom: a puff from the source in a random upward direction (a fixed spread of 0,
// as ln 1 = 0), sized and framed by the cloud's age.
int ParticleSource::EmitMushroom(Particle* p, int) {
    if (std::ssize(elements) == 0) {
        elements.resize(1);
        ParticleElement* e = elements.data();
        e->ticks = 0;
        e->pos.x = std::log(EI_MUSHROOM_ONE) * EI_MUSHROOM_SPREAD * size;
        elements[0].pos.z = 0.0f;
        elements[0].mushroom.stem = origin;
    }
    float spread = (Unsigned(Next()) * EI_MUSHROOM_W_SCALE + EI_MUSHROOM_W_BASE) * elements[0].pos.x;
    float a = SignedUnit();
    float b = SignedUnit();
    float c = Unsigned(Next()) * EI_MUSHROOM_UP_SCALE + EI_MUSHROOM_UP_BASE;
    float k = EI_MUSHROOM_ONE / std::sqrt(c * c + b * b + a * a);
    float dx = k * a;
    p->drift.x = dx;
    float dy = k * b;
    p->drift.y = dy;
    float dz = k * c + EI_MUSHROOM_UP;
    p->drift.z = dz;
    p->pos.x = dx * spread + origin.x;
    p->pos.y = dy * spread + origin.y;
    p->pos.z = dz * spread + origin.z + elements[0].pos.z;
    p->size = elements[0].ticks * EI_MUSHROOM_GROW + EI_MUSHROOM_UP_BASE;
    p->drift.x = dx * size * EI_MUSHROOM_SPEED;
    p->drift.y = dy * size * EI_MUSHROOM_SPEED;
    p->drift.z = dz * size * EI_MUSHROOM_SPEED;
    p->size = size * p->size;
    p->life = static_cast<int>(Next() & 3) + 16;
    int bump = static_cast<int>(Next() % 3);
    p->frameTimer = 2;
    p->color = 0x40FFFFFF;
    int frame = elements[0].ticks / 10 + bump - 1;
    p->frame = frame < 0 ? 0 : frame;
    return 1;
}

// Mushroom puffs: step a frame every other tick up to frame 14 (swelling 10% per tick
// before frame 8), fade in by 64 per tick then out by 63 over the last 4 ticks, drift with a 0.7
// drag, and while below the cap's height get pulled towards the stem.
int ParticleSource::UpdateMushroom(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    if (p->frame < 14) {
        int frame = p->frame;
        if (--p->frameTimer == 0) {
            p->frameTimer = 1;
            p->frame = frame + 1;
        }
        if (p->frame < 8) p->size = p->size * EI_MUSHROOM_SWELL;
    }
    std::uint32_t color = p->color;
    if (life > 4) {
        std::uint32_t a = (color >> 24) + 0x40;
        if (a > 0xFF) a = 0xFF;
        color = a << 24;
    } else {
        color = (color & 0xFF000000u) - 0x3F000001u;
    }
    p->color = color | 0xFFFFFF;
    p->pos.x = p->pos.x + p->drift.x;
    p->pos.y = p->drift.y + p->pos.y;
    p->pos.z = p->drift.z + p->pos.z;
    float vx = p->drift.x * EI_MUSHROOM_DRAG;
    p->drift.x = vx;
    float vy = p->drift.y * EI_MUSHROOM_DRAG;
    p->drift.y = vy;
    p->drift.z = p->drift.z * EI_MUSHROOM_DRAG;
    const float* stem = &elements[0].mushroom.stem.x;
    float dx = p->pos.x - stem[0];
    float dyu = p->pos.y - stem[1];
    float dy = dyu;
    float reach = std::sqrt(dyu * dy + dx * dx);
    float dz = p->pos.z - stem[2];
    if (elements[0].pos.z > dz) {
        p->drift.x = vx - dx * size * EI_MUSHROOM_PULL / reach;
        p->drift.y = vy - dy * size * EI_MUSHROOM_PULL / reach;
    }
    return 1;
}

// --- BlueGas (Stench, 0x2013): gas swirling up around the carrier -------------------------------

// BlueGas: the first call measures the carrier (5 * size + 0.5 puffs per tick). A puff starts at a random
// point of the disc around the carrier's figure, turning about the axis faster the nearer it is, and rising.
int ParticleSource::EmitStench(Particle* p, int) {
    if (std::ssize(elements) == 0) {
        if (!attachedTo) {
            game::DebugLog(NoCarrierGas());
            return 0;
        }
        Vector3 offset;
        float radius;
        AttachmentOffset(&offset, &radius);
        float r = radius;
        float gasRadius = r * EI_GAS_RADIUS;
        float carrierHeight = r + r;
        elements.resize(1);
        elements[0].pos = offset;
        elements[0].gas.radius = gasRadius;
        elements[0].gas.height = carrierHeight;
        int rate = static_cast<int>(size * EI_GAS_RATE + EI_GAS_ROUND);
        emitCount = rate < 1 ? 1 : rate;
    }
    float radius = elements[0].gas.radius;
    float carrierHeight = elements[0].gas.height;
    float t = Unsigned(Next()) * radius * EI_P_SCALE_1;
    float twoPi = 3.1415927f + 3.1415927f;
    float angle = Unsigned(Next()) * twoPi * EI_P_SCALE_1;
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float turn = t * EI_GAS_RADIUS / radius;
    float low = radius * EI_GAS_LOW;
    float vx = std::sin(angle) * t;
    float vy = std::cos(angle) * t;
    float vz = Unsigned(Next()) * (radius * EI_GAS_RADIUS - low) * EI_P_SCALE_1 + low;
    p->velocity = {vx, vy, vz};
    float riseLow = carrierHeight * EI_GAS_RISE_LOW;
    std::uint32_t r4 = Next();
    p->drift.x = std::sin(turn);
    p->drift.y = std::cos(turn);
    p->drift.z = Unsigned(r4) * (carrierHeight * EI_GAS_RISE_HIGH - riseLow) * EI_P_SCALE_1 + riseLow;
    const ParticleElement& e = elements[0];
    float cx = origin.x + e.pos.x;
    float cy = origin.y + e.pos.y;
    float cz = origin.z + e.pos.z;
    Vector3 start;
    start.x = cx + p->velocity.x;
    start.y = cy + p->velocity.y;
    start.z = cz + p->velocity.z;
    p->PlaceAt(start);
    p->size = radius * EI_GAS_SIZE;
    return 1;
}

// BlueGas puffs: step through the frames (dropped after the last), turn their offset
// about the axis, rise faster and faster, swell, and fade over the last 8 ticks.
int ParticleSource::UpdateStench(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    p->pos.x = p->pos.x - p->velocity.x;
    p->pos.y = p->pos.y - p->velocity.y;
    p->pos.z = p->pos.z - p->velocity.z;
    float turnedY = Dot2DUnrounded(p->velocity.x, p->velocity.y, p->drift.x, p->drift.y);
    float turnedX = p->drift.y * p->velocity.x - p->velocity.y * p->drift.x;
    p->velocity.x = turnedX;
    p->velocity.y = turnedY;
    p->velocity.z = p->drift.z + p->velocity.z;
    p->drift.z = p->drift.z * EI_GAS_SPEED_UP;
    p->pos.x = p->velocity.x + p->pos.x;
    p->pos.y = p->velocity.y + p->pos.y;
    p->pos.z = p->velocity.z + p->pos.z;
    p->size = p->size * EI_GAS_SWELL;
    if (life < 8) p->color = ((p->color & 0xFF000000u) - 0x1F000001u) | 0xFFFFFF;
    return 1;
}

// --- Tornado (0x200A) and CurseHolder (0x2050): a funnel of rings ---------------------------------
// The rings run bottom to top. A particle: velocity.x / .y its angle and radius, aux0 its height (0..1),
// aux1 its alpha, drift.x / .y / .z its turn, radius factor and rise per tick.

// Tornado: a particle at a random height of the funnel (around the ring centres there),
// with a radius growing towards the top and a turn rate larger near the bottom; one in three is
// dust (state 0, frames 12-15), the others debris sized by their radius.
int ParticleSource::EmitTornado(Particle* p, int) {
    float height01 = Unsigned(Next()) * EI_P_SCALE_1;
    p->state = static_cast<int>(Next() % 3);
    p->color = static_cast<std::uint32_t>(frameDelay);
    std::memcpy(&p->pos, &origin, 2 * sizeof(float));
    p->pos.z = origin.z + elements[0].pos.z;
    p->life = static_cast<int>(Next() % 9) + 100;
    float scaled = height01 * EI_FUNNEL_PI_F;
    int k = std::lround(scaled);
    if (k >= lib::Count(elements) - 2) k = lib::Count(elements) - 2;
    float f = scaled - k;
    float t = MinKeepA(f, 1.0f);
    float u = EI_P_ONE - t;
    const ParticleElement& a = elements[k];
    const ParticleElement& b = elements[k + 1];
    float cx = t * b.pos.x + u * a.pos.x;
    float cy = u * a.pos.y + t * b.pos.y;
    float high = EI_FUNNEL_PI;
    float low = -EI_FUNNEL_PI;
    std::uint32_t r = Next();
    p->aux1 = 0.1f;     // 0.1
    p->aux0 = height01;
    float angle = Unsigned(r) * (high - low) * EI_P_SCALE_1 + low;
    float w = EI_P_ONE - height01;
    float wRounded = w;
    float w2 = w * w;
    float w4 = w2 * w2;
    p->velocity.x = angle;
    float radius = (Unsigned(Next()) * EI_FUNNEL_R_SCALE + EI_FUNNEL_R_BASE) * (w4 + EI_FUNNEL_R_ADD) * 2.0f;
    p->velocity.y = radius;
    float turnHigh = EI_FUNNEL_PI * EI_FUNNEL_TURN_HIGH;
    float turnLow = EI_FUNNEL_PI * EI_FUNNEL_TURN_LOW;
    float turn = Unsigned(Next()) * (turnHigh - turnLow) * EI_P_SCALE_1 + turnLow;
    p->drift.x = turn * (EI_FUNNEL_TURN_TOP - wRounded * wRounded) * EI_FUNNEL_TURN_SCALE;
    p->drift.z = 0.0f;
    p->drift.y = elements[0].funnel.damping;
    p->pos.x = std::sin(angle) * radius + p->pos.x + cx;
    p->pos.y = std::cos(angle) * radius + cy + p->pos.y;
    p->pos.z = height01 * elements[0].funnel.height + p->pos.z;
    if (p->state != 0) {
        p->size = (radius + EI_FUNNEL_SIZE) * size;
        p->frame = static_cast<int>(Next() % 12);
    } else {
        p->frame = static_cast<int>(Next() & 3) + 12;
        p->size = size * EI_FUNNEL_DUST;
    }
    return 1;
}

// Tornado particles rise (dropped above the top), turn around the funnel's centre at
// their height (faster and faster), fade in, fade by the height, and animate.
int ParticleSource::UpdateTornado(Particle* p) {
    if (--p->life == 0) return 0;
    p->aux0 = p->aux0 + p->drift.z;
    p->drift.z = p->drift.z + EI_FUNNEL_RISE;
    if (p->aux0 > 1.0f) return 0;
    float scaled = p->aux0 * EI_FUNNEL_PI_F;
    float cx;
    float cy = FunnelCentre(this, scaled, cx);
    p->velocity.x = p->velocity.x + p->drift.x;
    p->drift.x = p->drift.x * EI_FUNNEL_SPIN_UP;
    float angle = p->velocity.x;
    p->velocity.y = p->drift.y * p->velocity.y;
    float radius = p->velocity.y;
    p->pos.x = std::sin(angle) * radius + origin.x + cx;
    p->pos.y = std::cos(angle) * radius + origin.y + cy;
    float base = elements[0].pos.z + origin.z;
    p->pos.z = base;
    p->pos.z = elements[0].funnel.height * p->aux0 + base;
    if (p->life > 8) p->aux1 = MinKeepA(p->aux1 + EI_FUNNEL_FADE, 1.0f);
    else p->aux1 = MaxKeepB(0.1f, p->aux1);
    int a = std::lround(std::sqrt(EI_P_ONE - p->aux0) * p->aux1 * EI_FUNNEL_ALPHA);
    p->color = (p->color & 0xFFFFFF) | (static_cast<std::uint32_t>(a) << 24);
    if (p->state != 0) {
        p->frame = (p->frame + 1) % 12;
        p->size = (radius + EI_FUNNEL_SIZE) * size;
    } else {
        p->frame = ((p->frame + 1) & 3) + 12;
    }
    return 1;
}

// Tornado rings (all updated from element 0's call). Without a bend the rings sway with
// the source's motion (damped 0.95, a half cosine up the funnel); with one they lean along the
// motion (at most 0.01 per tick) and bend with the height. The bottom ring sits 1 above the ground.
// A free source that lost its bend and still has particles stops and keeps going until they die.
void ParticleSource::TickTornado(ParticleElement* e) {
    if (e != elements.data()) return;
    ParticleElement* bottom = elements.data();
    float bend;
    bend = bottom->funnel.bend;
    EI_NAN_AUDIT(bend);
    if (bend == 0.0f) {
        Vector3 sway;
        sway.Construct(bottom->pos.x - motion.x, bottom->pos.y - motion.y,
                       bottom->pos.z - motion.z);
        sway.x = sway.x * EI_FUNNEL_SWAY;
        sway.z = 0.0f;
        sway.y = sway.y * EI_FUNNEL_SWAY;
        for (int i = 0; i < std::ssize(elements); ++i) {
            float c = std::cos(static_cast<float>(i) / static_cast<float>(std::ssize(elements) - 1) * EI_FUNNEL_PI *
                                 EI_FUNNEL_HALF_ARC);
            ParticleElement& ring = elements[i];
            float x = sway.x * c;
            float y = sway.y * c;
            ring.pos = {x, y, c * sway.z};
        }
        elements[0].pos.z = GroundHeight(origin.x, origin.y) - origin.z + EI_P_ONE;
    } else {
        elements[0].pos.z = GroundHeight(origin.x, origin.y) - origin.z + EI_P_ONE;
        float mx = motion.x * EI_FUNNEL_LEAN;
        float my = motion.y * EI_FUNNEL_LEAN;
        float mz = motion.z * EI_FUNNEL_LEAN;
        const ParticleElement& b0 = elements[0];
        float sx = mx + b0.pos.x;
        float sy = my + b0.pos.y;
        float sz = mz + b0.pos.z;
        float ax = sx * EI_FUNNEL_BACK;
        float ay = sy * EI_FUNNEL_BACK;
        float az = sz * EI_FUNNEL_BACK;
        Vector3 n = {ax, ay, az};
        float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(ax, ay, az));
        EI_NAN_AUDIT(length);
        if (length != 0.0f) {
            float inverse = EI_P_ONE / length;
            n.x = ax * inverse;
            n.y = ay * inverse;
            n.z = az * inverse;
        }
        float lean = length * EI_FUNNEL_HALF;
        if (lean > EI_FUNNEL_MAX_LEAN) lean = EI_FUNNEL_MAX_LEAN;
        Vector3 d;
        d.Construct(n.x * lean, n.y * lean, n.z * lean);
        const ParticleElement& b1 = elements[0];
        float gx = d.x + b1.pos.x;
        float gy = d.y + b1.pos.y;
        float gz = d.z + b1.pos.z;
        for (int i = 0; i < std::ssize(elements); ++i) {
            float f = static_cast<float>(i) / static_cast<float>(std::ssize(elements) - 1);
            float c = std::cos(f * EI_FUNNEL_PI);
            float k = (c + (EI_P_ONE - f) * elements[0].funnel.bend + EI_FUNNEL_ONE) * EI_FUNNEL_BEND;
            ParticleElement& ring = elements[i];
            float x = gx * k;
            float y = gy * k;
            float z = gz * k;
            ring.pos = {x, y, z};
        }
    }
    if (!attachedTo) {
        float b;
        b = elements[0].funnel.bend;
        EI_NAN_AUDIT(b);
        if (b == 0.0f && !particles.empty()) {
            flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
            Attach(nullptr);
        }
    }
}

// --- PortalStar (0x203D): one flickering star ------------------------------------------------------

int ParticleSource::EmitPortalStar(Particle* p, int) {
    p->color = 0xFFFFFF;
    p->life = 15;
    p->frame = 0;
    p->pos = origin;
    p->size = 0.0f;
    return 1;
}

// PortalStar: while the source emits, full colour and a random size of 0.8-1.2 times
// the source's; dropped once it stops.
int ParticleSource::UpdatePortalStar(Particle* p) {
    if (!(flags & ParticleSourceFlag::Emitting)) return 0;
    p->color = 0xFFFFFFFF;
    p->size = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) * size;
    return 1;
}

// --- Cylinder1 / Cylinder2 (0x203F, 0x2040): two stars circling the carrier --------------------
// A star's ticks count down and index kCylinderAlpha.

// Cylinder: the first particles are the stars (state = their index; the first one
// places them around the carrier); later ones are sparks along a random star's last step.
int ParticleSource::EmitCylinder(Particle* p, int) {
    p->color = 0xFFFFFF;
    int count = lib::Count(particles);
    int stars = lib::Count(elements);
    if (count < stars) {
        if (count == 0) SetUpCylinder(this);
        p->frame = 0;
        p->state = count;
        const ParticleElement& e = elements[count];
        p->pos.x = e.pos.x + origin.x;
        p->pos.y = e.pos.y + origin.y;
        p->pos.z = e.pos.z + origin.z;
        p->size = 0.0f;
        return 1;
    }
    int j = static_cast<int>(Next() % static_cast<std::uint32_t>(stars));
    const ParticleElement& e = elements[j];
    int life = e.ticks + 6;
    if (life > 14) life = 14;
    p->life = life;
    if (life < 0) return 0;
    const float* step = &e.star.step.x;
    auto isZero = [](float f) {
        EI_NAN_AUDIT(f);
        return f == 0.0f;
    };
    if (isZero(step[0]) && isZero(step[1])) return 0;
    p->frame = 1;
    p->state = -1;
    float u = Unsigned(Next()) * EI_P_SCALE_1 - EI_P_ONE;
    float a = u * step[0];
    float b = u * step[1];
    float c = u * step[2];
    p->velocity.x = a + e.pos.x;
    p->velocity.y = b + e.pos.y;
    p->velocity.z = c + e.pos.z;
    std::memcpy(&p->aux0, step, sizeof(Vector3));
    float px = p->velocity.x + origin.x;
    float py = origin.y + p->velocity.y;
    float pz = origin.z + p->velocity.z;
    p->size = 0.1f;     // 0.1
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    return 1;
}

// Cylinder: a star follows its element and fades by its countdown (dropped once the source stops); a spark
// fades by its life, drifts by its step, is held on its star's orbit radius and spirals round it, shrinking.
int ParticleSource::UpdateCylinder(Particle* p) {
    int state = p->state;
    if (state >= 0) {
        if (!(flags & ParticleSourceFlag::Emitting)) {
            p->size = 0.0f;
            p->color = 0xFFFFFF;
            return 0;
        }
        const ParticleElement& e = elements[state];
        p->color = (static_cast<std::uint32_t>(kCylinderAlpha[e.ticks]) << 24) | 0xFFFFFF;
        float px = origin.x + e.pos.x;
        float py = origin.y + e.pos.y;
        float pz = origin.z + e.pos.z;
        p->size = 0.3f;     // 0.3
        p->pos.y = py;
        p->pos.x = px;
        p->pos.z = pz;
        return 1;
    }
    ++p->frame;
    int life = --p->life;
    if (life <= 0) return 0;
    p->color = (static_cast<std::uint32_t>(kCylinderAlpha[life]) << 24) | 0xFFFFFF;
    float* step = &p->aux0;
    p->velocity.x = p->velocity.x + step[0];
    p->velocity.y = step[1] + p->velocity.y;
    p->velocity.z = step[2] + p->velocity.z;
    float k = elements[0].star.orbit / std::sqrt(LengthSquared2DUnrounded(p->velocity.x, p->velocity.y));
    float kx = k * p->velocity.x;
    p->velocity.x = kx;
    float ky = k * p->velocity.y;
    p->velocity.y = ky;
    float reach =
        std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(step[0], step[1], step[2])) * elements[0].star.inverseOrbit;
    step[0] = ky * reach * EI_CYL_SPARK_PULL;
    step[1] = -kx * reach * EI_CYL_SPARK_PULL;
    step[2] = step[2] * EI_CYL_SPARK_PULL;
    float oldSize = p->size;
    float px = p->velocity.x + origin.x;
    float py = p->velocity.y + origin.y;
    float pz = p->velocity.z + origin.z;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = oldSize * EI_CYL_SPARK_SHRINK;
    return 1;
}

// Cylinder stars: element 0 lowers the emission rate (by 2 on odd counts, 1 on even);
// each star counts down (stopping the source at 0), turns by its rotation, rises (faster and
// faster), and its rotation turns the other way, slowly.
void ParticleSource::TickCylinder(ParticleElement* e) {
    if (e == elements.data()) {
        if (e->ticks % 2 != 0) --emitCount;
        --emitCount;
    }
    int t = e->ticks--;
    if (t <= 0) {
        flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float* rotation = e->star.rotation;
    float nx = Dot2DUnrounded(rotation[1], rotation[0], e->pos.y, e->pos.x);
    float ny = rotation[0] * e->pos.y - rotation[1] * e->pos.x;
    float* rise = &e->star.rise;
    float nz = e->pos.z + (*rise);
    float* step = &e->star.step.x;
    step[0] = nx - e->pos.x;
    step[1] = ny - e->pos.y;
    step[2] = nz - e->pos.z;
    *rise = (*rise) * EI_CYL_RISE_UP;
    e->pos = {nx, ny, nz};
    float twist = EI_CYL_PI * EI_CYL_TWIST;
    if (ParticleTypeOf(type) == ParticleType::Cylinder2) twist = -twist;
    float c = std::cos(twist);
    float s = std::sin(twist);
    float a = s * rotation[1] + c * rotation[0];
    float b = c * rotation[1] - s * rotation[0];
    rotation[0] = a;
    rotation[1] = b;
}

// --- FireBall (0x2000) and the arrow smokes (FireArrow 0x2011, 0x204E): trails of a moving source
// A trail particle starts somewhere along the source's motion of the last tick and gets a velocity
// across the motion (the spread box along the source's side and up axes); the first particle of the
// fireball / arrow is its head (state 1 / 2).

// FireBall: index 0 is the ball itself (a 1.2 size particle at the source); the others
// are smoke along the last step with a random sideways velocity (the spread box widened by half
// the size) and are put one step out.
int ParticleSource::EmitFireBall(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    p->pos = origin;
    p->size = 1.0f;
    float half = size * EI_TRAIL_HALF;
    float halfRounded = half;
    float t;
    if (index == 0) {
        p->size = size * EI_TRAIL_HEAD;
        p->state = 1;
        t = EI_P_ZERO;
    } else {
        p->size = half;
        std::uint32_t r = Next();
        p->state = 0;
        t = Unsigned(r) * EI_P_SCALE_1 - EI_P_ONE;
    }
    EI_NAN_AUDIT(distance);
    if (distance <= EI_TRAIL_MOVING) {
        p->velocity = {};
        return 1;
    }
    float ax = t * motion.x;
    float ay = t * motion.y;
    float az = t * motion.z;
    p->pos.x = ax + p->pos.x;
    p->pos.y = ay + p->pos.y;
    p->pos.z = az + p->pos.z;
    float high = halfRounded + spreadMax.x;
    float low = spreadMin.x - halfRounded;
    SideVelocity(this, p, low, high);
    high = halfRounded + spreadMax.y;
    low = spreadMin.y - halfRounded;
    AddUp(this, p, Between(low, high));
    if (p->state == 0) {
        p->pos.x = p->velocity.x + p->pos.x;
        p->pos.y = p->velocity.y + p->pos.y;
        p->pos.z = p->velocity.z + p->pos.z;
    }
    return 1;
}

// FireBall: step through the frames (dropped after the last), grow by the source's
// growth; the ball halves once and stays; smoke drifts by its velocity (0.95 drag).
int ParticleSource::UpdateFireBall(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    p->size = growth * p->size;
    if (p->state != 0) {
        p->size = p->size * EI_TRAIL_HALF;
        p->state = 0;
    } else {
        p->pos.x = p->pos.x - p->velocity.x;
        p->pos.y = p->pos.y - p->velocity.y;
        p->pos.z = p->pos.z - p->velocity.z;
    }
    p->velocity.x = p->velocity.x * EI_TRAIL_DRAG;
    p->velocity.y = p->velocity.y * EI_TRAIL_DRAG;
    p->velocity.z = p->velocity.z * EI_TRAIL_DRAG;
    p->pos.x = p->velocity.x + p->pos.x;
    p->pos.y = p->velocity.y + p->pos.y;
    p->pos.z = p->velocity.z + p->pos.z;
    return 1;
}

// Arrow smoke: the first particle (once) is the arrow's head (state 2); the others are
// puffs along the last step (three in ten lingering, state 1) with a random sideways velocity.
int ParticleSource::EmitArrowSmoke(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    p->pos = origin;
    p->size = 1.0f;
    float t;
    if (index == 0 && Bits(&emitState) == 0) {
        p->size = size * EI_ARROW_HEAD;
        lib::StoreBytes(&emitState, 1);
        t = 0.0f;
        p->state = 2;
        p->frame = 0;
    } else {
        p->size = size * EI_ARROW_PUFF;
        std::uint32_t r = Next();
        p->state = 0;
        t = Unsigned(r) * EI_P_SCALE_1 - EI_P_ONE;
        if (Next() % 100 > 70) p->state = 1;
        p->frame = static_cast<int>(Next() & 1) + 2;
    }
    EI_NAN_AUDIT(distance);
    if (distance <= EI_TRAIL_MOVING) {
        p->velocity = {};
        return 1;
    }
    float ax = t * motion.x;
    float ay = t * motion.y;
    float az = t * motion.z;
    p->pos.x = ax + p->pos.x;
    p->pos.y = ay + p->pos.y;
    p->pos.z = az + p->pos.z;
    SideVelocity(this, p, spreadMin.x, spreadMax.x);
    AddUp(this, p, Between(spreadMin.y, spreadMax.y));
    if (p->state == 0) {
        p->pos.x = p->pos.x + p->velocity.x;
        p->pos.y = p->velocity.y + p->pos.y;
        p->pos.z = p->velocity.z + p->pos.z;
    }
    return 1;
}

// Arrow smoke: the head follows the source one step ahead with a random frame (dropped
// once the source stops); puffs step through the frames, swell (faster in the first 6 frames),
// and fly out by their velocity (growing by 1.1); lingering puffs keep their place.
int ParticleSource::UpdateArrowSmoke(Particle* p) {
    int state = p->state;
    if (state == 2) {
        p->pos.x = motion.x + origin.x;
        p->pos.y = motion.y + origin.y;
        p->pos.z = motion.z + origin.z;
        p->frame = static_cast<int>(Next() % 3);
        return static_cast<int>(flags & ParticleSourceFlag::Emitting);
    }
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    int frame = p->frame;
    if (state == 0) {
        if (frame < 6) p->size = p->size * EI_ARROW_SWELL;
        else p->size = growth * p->size;
        p->pos.x = p->pos.x - p->velocity.x;
        p->pos.y = p->pos.y - p->velocity.y;
        p->pos.z = p->pos.z - p->velocity.z;
        p->velocity.x = p->velocity.x * EI_ARROW_SPREAD;
        p->velocity.y = p->velocity.y * EI_ARROW_SPREAD;
        p->velocity.z = p->velocity.z * EI_ARROW_SPREAD;
        p->pos.x = p->velocity.x + p->pos.x;
        p->pos.y = p->pos.y + p->velocity.y;
        p->pos.z = p->velocity.z + p->pos.z;
        return 1;
    }
    if (frame < 6) p->size = p->size * EI_ARROW_SWELL;
    p->velocity.x = p->velocity.x * EI_TRAIL_DRAG;
    p->velocity.y = p->velocity.y * EI_TRAIL_DRAG;
    p->velocity.z = p->velocity.z * EI_TRAIL_DRAG;
    return 1;
}

// --- Banshee (0x200D): a ring that widens around the source ---------------------------------------

// Banshee: the first call only sets the ring up (from the carrier's figure; 72 sparks per tick after it). A
// spark on the ring, at a random angle, between the ring and its eased inner edge.
int ParticleSource::EmitBanshee(Particle* p, int) {
    if (std::ssize(elements) == 0) {
        elements.resize(1);
        Vector3 offset;
        float radius;
        AttachmentOffset(&offset, &radius);
        float tall = radius * EI_BANSHEE_TALL;
        float widen = radius * EI_BANSHEE_WIDEN;
        height = widen + widen;
        elements[0].ticks = 8;
        elements[0].pos.z = tall * EI_BANSHEE_HALF;
        elements[0].banshee.radius = 0.0f;
        emitCount = 72;
        return 0;
    }
    float ringRadius;
    ringRadius = elements[0].banshee.radius;
    float twoPi = 3.1415927f + 3.1415927f;
    std::uint32_t r = Next();
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->frameTimer = 1;
    float angle = Unsigned(r) * twoPi * EI_P_SCALE_1;
    int age = elements[0].ticks;
    if (age >= 5) age = 5;
    p->life = static_cast<int>(Next() % 3u) + age + 1;
    float inner = (ringRadius - height * EI_BANSHEE_REACH) * EI_BANSHEE_EASE + ringRadius;
    float reach;
    if (inner + EI_BANSHEE_THIN >= ringRadius)
        reach = inner;
    else
        reach = Unsigned(Next()) * (ringRadius - inner) * EI_P_SCALE_1 + inner;   // also NaN
    p->pos.x = std::sin(angle) * reach + origin.x;
    p->pos.y = std::cos(angle) * reach + origin.y;
    p->pos.z = origin.z + elements[0].pos.z;
    p->size = size * EI_BANSHEE_SIZE;
    return 1;
}

int ParticleSource::UpdateBanshee(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        p->frameTimer = 1;
        ++p->frame;
    }
    return 1;
}

// Banshee ring: counts down (stopping the emission at 0) while its radius eases towards
// 30 times the source's height.
void ParticleSource::TickBanshee(ParticleElement* e) {
    if (--e->ticks == 0) {
        flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float target = height * EI_BANSHEE_REACH;
    float* radius = &e->banshee.radius;
    *radius = ((*radius) - target) * EI_BANSHEE_EASE + target;
}

// --- Portal (0x203E) emission; its update and element tick are in particle_emitters.cpp ------------

// Portal: the first particle is the glow above the portal (state 1). The others start
// at a random point of the disc (denser near the edge), spin round it (four in eleven or so the
// other way, with a different frame range) and rise.
int ParticleSource::EmitPortal(Particle* p, int) {
    if (particles.empty()) {
        p->pos = origin;
        p->size = 0.0f;
        p->pos.z = EI_PORTAL_HEIGHT * 0.8f + p->pos.z;
        p->color = 0x60FFFFFF;
        p->state = 1;
        p->frame = 15;
        return 1;
    }
    p->state = 0;
    p->color = 0x40FFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float radius = EI_PORTAL_RADIUS;
    float t = Unsigned(Next()) * EI_P_SCALE_1;
    float spread = (t + EI_P_ONE) * t * EI_PORTAL_HALF_C;
    float angle = Unsigned(Next()) * EI_PORTAL_ANGLE;
    float reach = spread * radius;
    float x = std::sin(angle) * reach;
    float y = std::cos(angle) * reach;
    p->pos.x = x + origin.x;
    p->pos.y = y + origin.y;
    p->pos.z = origin.z;
    p->size = 0.0f;
    p->aux1 = y;
    p->aux2 = (EI_PORTAL_TWO - spread) * EI_PORTAL_SHRINK;
    p->aux0 = x;
    float spin = radius / EI_PORTAL_RADIUS * EI_PORTAL_SPIN;
    if (Unsigned(Next()) * EI_P_SCALE_1 > spread) {
        p->frame = static_cast<int>(Next() % 11) + 4;
    } else {
        p->frame = static_cast<int>(Next() & 3);
        spin = -spin;
    }
    p->velocity.x = std::cos(spin);
    p->velocity.y = std::sin(spin);
    p->velocity.z = (EI_P_ONE - spread + 3.0f) * radius * EI_PORTAL_RISE;
    p->pos.z = (Unsigned(Next()) * EI_P_SCALE_1 - EI_PORTAL_HALF_C) * p->velocity.z + p->pos.z;
    return 1;
}

// --- AcidRay (0x2012) ---------------------------------------------------------------------------

// AcidRay: a drop at a random point of the last step (when moving), flying sideways by
// the spread box along the side and up axes (or standing).
int ParticleSource::EmitAcidRay(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    p->pos = origin;
    p->size = 1.0f;
    std::uint32_t r = Next();
    p->state = 0;
    float t = Unsigned(r) * EI_P_SCALE_1 - EI_P_ONE;
    p->frame = static_cast<int>(Next() & 1);
    if (distance > EI_TRAIL_MOVING) {
        float ax = t * motion.x;
        float ay = t * motion.y;
        float az = t * motion.z;
        float px = ax + p->pos.x;
        p->pos.x = px;
        float py = ay + p->pos.y;
        p->pos.y = py;
        float pz = az + p->pos.z;
        p->drift.y = py;
        p->pos.z = pz;
        p->drift.z = pz;
        p->drift.x = px;
        float q = Unsigned(Next()) * EI_P_SCALE_2 - EI_P_ONE;
        p->velocity.x = q * side.x;
        p->velocity.y = q * side.y;
        p->velocity.z = q * side.z;
        float q2 = Unsigned(Next()) * EI_P_SCALE_2 - EI_P_ONE;
        float a = q2 * up.x;
        float b = q2 * up.y;
        float c = q2 * up.z;
        p->velocity.x = a + p->velocity.x;
        p->velocity.y = b + p->velocity.y;
        p->velocity.z = c + p->velocity.z;
    } else {
        p->velocity = {};
        p->drift = origin;
    }
    Vector3 start;
    PlaceAcid(this, p, lifetime - p->life, start);
    p->PlaceAt(start);
    p->size = kAcidSize[lifetime - p->life] * size;
    return 1;
}

// AcidRay drops: fly out from their start by the velocity scaled by the source's size
// and the spread table (by age), sized by the size table, through the frames.
int ParticleSource::UpdateAcidRay(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    float wideSize = size;
    float sx = wideSize * p->velocity.x;
    float sy = wideSize * p->velocity.y;
    float sz = wideSize * p->velocity.z;
    float k = kAcidSpread[lifetime - life];
    float ax = sx * k;
    float ay = sy * k;
    float az = sz * k;
    float px = ax + p->drift.x;
    float py = ay + p->drift.y;
    float pz = az + p->drift.z;
    p->pos.y = py;
    p->size = 1.0f;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = kAcidSize[lifetime - life] * size;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    return 1;
}

// --- Sparks (0x2043) emission; the update is UpdateSparks (particle_emitters.cpp) ----------------

// Sparks: the first particle marks the source (state 1); after it the source stops
// emitting and each further one is a spark flying in a random direction (a random point of the
// unit ball, normalised) with a random speed (squared) and a random part of its first step done.
int ParticleSource::EmitSparks(Particle* p, int) {
    if (particles.empty()) {
        p->color = 0xFFFFFFFF;
        p->pos = origin;
        p->size = 0.0f;
        p->frame = 0;
        p->state = 1;
        return 1;
    }
    flags &= ~ParticleSourceFlag::Emitting;
    p->color = 0xFFFFFFFF;
    float speed = Unsigned(Next()) * 1.3969838e-10f + 0.8f;
    float x, y, z;
    float lengthSquared;
    for (int tries = 10;;) {
        x = SignedUnit();
        y = g_clientRandom.Range(-1.0f, 1.0f);
        float zu = g_clientRandom.Range(-1.0f, 1.0f);
        z = zu;
        lengthSquared = zu * z + y * y + x * x;
        if (lengthSquared < EI_P_ONE) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    float length = std::sqrt(lengthSquared);
    Vector3 v;
    v.Construct(x, y, z);
    Vector3 direction;
    DivideVector(&direction, v, length);
    float k = speed * size * speed * 0.2f;
    p->drift.x = direction.x * k;
    p->drift.y = direction.y * k;
    p->drift.z = direction.z * k;
    float t = Unsigned(Next()) * EI_P_SCALE_1;
    float a = t * p->drift.x;
    float b = t * p->drift.y;
    float c = t * p->drift.z;
    float px = a + origin.x;
    float py = b + origin.y;
    float pz = c + origin.z;
    p->size = 1.0f;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    p->size = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) * size *
                    0.2f;
    p->life = 6;
    p->state = 0;
    p->frame = static_cast<int>(Next() % 3) + 1;
    return 1;
}

// --- ClayRing (0x2018): ten points running out from the source along the ground ---------------

// ClayRing: in the first tick (countdown 13) one marker per point; then clods between a
// random point's previous and current position, flying off at random.
int ParticleSource::EmitClayRing(Particle* p, int index) {
    p->color = (static_cast<std::uint32_t>(ByLife(kClayAlpha, elements[0].ticks, kClayBefore)) << 24) | 0xFFFFFF;
    p->life = lifetime;
    p->frame = 1;
    p->frameTimer = frameDelay;
    if (elements[0].ticks == 13 && index < std::ssize(elements)) {
        p->velocity = {};
        p->pos = elements[index].pos;
        p->size = 0.2f;     // 0.2
        p->frame = 0;
        p->state = index;
        return 1;
    }
    int j = static_cast<int>(Next() % static_cast<std::uint32_t>(std::ssize(elements)));
    float t = Unsigned(Next()) * EI_P_SCALE_1;
    p->life = 5;
    const ParticleElement& e = elements[j];
    const float* previous = &e.clay.previous.x;
    float u = EI_P_ONE - t;
    float a = u * previous[0];
    float b = u * previous[1];
    float c = u * previous[2];
    float d = t * e.pos.x;
    float f = t * e.pos.y;
    float g = t * e.pos.z;
    p->velocity.x = d + a;
    p->velocity.y = f + b;
    p->velocity.z = g + c;
    float sx = Unsigned(Next()) * EI_CLAY_SCATTER_SCALE - EI_CLAY_SCATTER_LOW;
    float sy = Unsigned(Next()) * EI_CLAY_SCATTER_SCALE - EI_CLAY_SCATTER_LOW;
    float sz = Unsigned(Next()) * EI_CLAY_SCATTER_SCALE - EI_CLAY_SCATTER_LOW;
    p->aux0 = sy;
    p->aux1 = sx;
    p->aux2 = sz;
    std::memcpy(&p->pos, previous, sizeof(Vector3));
    p->size = 0.1f;     // 0.1
    return 1;
}

// ClayRing: markers stay on their point (alpha by the countdown); clods step through the
// frames, fly out (their scatter slowing by 0.9), fade by 48 per tick and shrink.
int ParticleSource::UpdateClayRing(Particle* p) {
    if (--p->life == 0) return 0;
    if (p->frame == 0) {
        p->color = (static_cast<std::uint32_t>(ByLife(kClayAlpha, elements[0].ticks, kClayBefore)) << 24) | 0xFFFFFF;
        p->pos = elements[p->state].pos;
        p->size = 0.2f;     // 0.2
        return 1;
    }
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    float* scatter = &p->aux0;
    for (int i = 0; i < 3; ++i) scatter[i] = scatter[i] * EI_CLAY_SLOW;
    float oldSize = p->size;
    float px = p->velocity.x + scatter[0];
    float py = p->velocity.y + scatter[1];
    float pz = p->velocity.z + scatter[2];
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    std::int32_t a = static_cast<std::int32_t>(p->color >> 24) - 0x30;
    if (a < 0) a = 0;
    p->size = oldSize * EI_CLAY_SLOW;
    p->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
    return 1;
}

// ClayRing points: count down (stopping the emission past 0), remember the previous
// position, move out from the source along their direction by the reach table (read backwards for
// a non-negative size), sit on the ground, and turn their direction a little.
void ParticleSource::TickClayRing(ParticleElement* e) {
    int t = --e->ticks;
    if (t < 0) {
        flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    float* previous = &e->clay.previous.x;
    e->clay.previous = e->pos;
    float* direction = &e->clay.direction.x;
    float s = size;
    float a = s * direction[0];
    float b = s * direction[1];
    float c = s * direction[2];
    float k = size >= 0.0f ? ClayReachBackwards(t) : kClayReach[t];
    float ka = a * k;
    float kb = b * k;
    float kc = c * k;
    float px = ka + origin.x;
    float py = kb + origin.y;
    float pz = kc + origin.z;
    e->pos = {px, py, pz};
    e->pos.z = GroundHeight(e->pos.x, e->pos.y) + EI_P_ONE;
    float turnedX = direction[1] * EI_CLAY_COS - direction[0] * EI_CLAY_SIN;
    float turnedY = direction[0] * EI_CLAY_COS + direction[1] * EI_CLAY_SIN;
    direction[0] = turnedY;
    direction[1] = turnedX;
    auto isZero = [](float f) {
        EI_NAN_AUDIT(f);
        return f == 0.0f;
    };
    if (isZero(previous[0]) && isZero(previous[1])) e->clay.previous = e->pos;
}

// --- Falling (0x2034-0x2037): leaves or flakes drifting down from the carrier -------------------

// Falling: a leaf at the source blown by a random gust (a random reach up to the gust's, a random direction
// around it (the original's spread is ftol(10 degrees) = 0)), falling and spinning at random rates.
int ParticleSource::EmitFalling(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->frame = 12;
    p->pos = origin;
    p->size = Unsigned(Next()) * EI_FALL_SIZE_SCALE + EI_FALL_SIZE_BASE;
    const ParticleElement& gust = elements[Next() % static_cast<std::uint32_t>(std::ssize(elements))];
    float reach = gust.wind.reach * EI_FALL_HALF;
    int spread = static_cast<int>(ei::kRadiansPerDegree * EI_FALL_TEN);
    float turn = spread * gust.wind.direction;
    float lowReach = -reach;
    float x = Unsigned(Next()) * (reach - lowReach) * EI_P_SCALE_1 + gust.wind.reach + lowReach;
    float lowTurn = -turn;
    float angle = Unsigned(Next()) * (turn - lowTurn) * EI_P_SCALE_1 + gust.wind.direction + lowTurn;
    p->velocity.x = 1.0f;
    p->velocity.z = -0.12f;     // -0.12
    p->drift.x = std::sin(angle) * x;
    p->drift.y = std::cos(angle) * x;
    p->drift.z = 0.0f;
    p->velocity.z = Unsigned(Next()) * EI_FALL_DROP_SCALE + p->velocity.z - EI_FALL_DROP;
    p->velocity.y = Unsigned(Next()) * EI_FALL_SPIN_SCALE - EI_FALL_SPIN;
    return 1;
}

// Falling gusts: a new gust gets a random reach and direction (truncated to whole
// radians); from the second tick on the source stops emitting and lets go of its carrier.
void ParticleSource::TickFalling(ParticleElement* e) {
    int age = e->wind.age;
    if (age == 0) {
        e->wind.reach = Unsigned(Next()) * EI_FALL_REACH_SCALE + EI_FALL_SIZE_BASE;
        float whole = static_cast<float>(static_cast<int>(ei::kRadiansPerDegree * EI_FALL_TURN));
        e->wind.direction = Unsigned(Next()) * whole * EI_P_SCALE_1;
        age = e->wind.age;
    }
    if (age > 0) {
        flags &= ~ParticleSourceFlag::Emitting;
        Attach(nullptr);
    }
    ++e->wind.age;
}

// --- VulcanSmoke (0x2005): lava drops and smoke from a vent ---------------------------------------

// VulcanSmoke: three in five are drops (state 8: 8 ticks per frame) thrown up at a random height in a random
// direction; the others smoke (state 4) at a random point of the vent, rising by the source's height.
int ParticleSource::EmitVulkan(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    float radian = ei::kRadiansPerDegree * EI_FALL_TURN;
    if (Next() % 100 + 1 <= 60) {
        p->state = 8;
        p->life = lifetime * 8;
        p->frameTimer = 8;
        std::memcpy(&p->pos, &origin, 2 * sizeof(float));
        float low = spreadMin.y * EI_VULKAN_LOW;
        float high = spreadMin.y * EI_VULKAN_HALF;
        p->pos.z = Unsigned(Next()) * (high - low) * EI_P_SCALE_1 + spreadMax.y + origin.z + low;
        p->size = size;
        p->velocity.x = growth;
        p->velocity.y = 0.965f;     // 0.965
        float reach = spreadMin.x;
        float span = radian;
        std::uint32_t r = Next();
        p->drift.z = 0.0f;
        float angle = Unsigned(r) * span * EI_P_SCALE_1;
        p->drift.x = std::sin(angle) * reach;
        p->drift.y = std::cos(angle) * reach;
        return 1;
    }
    p->state = 4;
    p->life = lifetime * 4;
    p->frameTimer = 4;
    float reach = Unsigned(Next()) * spreadMax.x * EI_P_SCALE_1;
    float span = radian;
    float angle = Unsigned(Next()) * span * EI_P_SCALE_1;
    p->pos.x = std::sin(angle) * reach + origin.x;
    p->pos.y = std::cos(angle) * reach + origin.y;
    p->pos.z = origin.z;
    p->size = size * EI_VULKAN_SMOKE;
    p->velocity.x = 1.02f;     // 1.02
    p->velocity.y = 1.0f;
    p->drift.x = 0.0f;
    p->drift.y = 0.0f;
    p->drift.z = height;
    return 1;
}

// VulcanSmoke: frames at the particle's own pace (state ticks per frame; dropped after
// the last), moving by drift, growing by velocity.x and slowing by velocity.y.
int ParticleSource::UpdateVulkan(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = p->state;
    }
    p->pos.x = p->drift.x + p->pos.x;
    p->pos.y = p->drift.y + p->pos.y;
    p->pos.z = p->drift.z + p->pos.z;
    p->size = p->velocity.x * p->size;
    float keep = p->velocity.y;
    p->drift.x = keep * p->drift.x;
    p->drift.y = keep * p->drift.y;
    p->drift.z = keep * p->drift.z;
    return 1;
}

// --- FireStar / AcidStar (0x2042-ish), Regeneration, VisionStar and ZoneExit ----------------------

// FireStar: a single flash at the source (the source stops emitting at once) with the
// source's lifetime as its frame and its size kept in velocity.x.
int ParticleSource::EmitFireStar(Particle* p, int) {
    flags &= ~ParticleSourceFlag::Emitting;
    p->color = 0xFFFFFF;
    p->life = 3;
    p->frame = lifetime;
    p->pos = origin;
    p->size = 0.0f;
    p->velocity.x = size;
    return 1;
}

// FireStar: shown for two ticks at the kept size (shrinking it by 0.9 each time), then hidden and dropped.
int ParticleSource::UpdateFireStar(Particle* p) {
    if (--p->life <= 0) {
        p->color = 0xFFFFFF;
        p->size = 0.0f;
        return 0;
    }
    float shrunk = p->velocity.x * 0.9f;
    p->color = 0xFFFFFFFF;
    p->size = p->velocity.x;
    p->velocity.x = shrunk;
    return 1;
}

// Regeneration: two particles at the source, velocity.x -1 for the first and 1 for the
// second (the sides they circle on).
int ParticleSource::EmitRegeneration(Particle* p, int) {
    if (std::ssize(particles) > 1) return 0;
    p->color = 0xFFFFFF;
    p->frame = 3;
    p->pos = origin;
    p->size = 0.0f;
    p->velocity.x = static_cast<float>(!particles.empty() ? 1 : -1);
    return 1;
}

// Regeneration: once the source no longer emits (or has no carrier) its size shrinks by
// a quarter per tick and the particles go below 0.02. Each particle eases (0.7 / 0.3) towards a
// point beside the carrier: its own x axis (from localRotation) scaled by the carrier's radius, the
// particle's side and 0.65; a little above, by 0.4 of the source's size. Its size flickers at random.
int ParticleSource::UpdateRegeneration(Particle* p) {
    if (!(flags & ParticleSourceFlag::Emitting) || !attachedTo) {
        float shrunk = size * 0.75f;
        size = shrunk;
        EI_NAN_AUDIT(shrunk);
        if (shrunk < 0.02f) {
            p->size = 0.0f;
            p->color = 0xFFFFFF;
            return 0;
        }
    }
    p->color = 0xFFFFFFFF;
    Vector3 target;
    target = origin;
    if (attachedTo) {
        const CObject3D* carrier = attachedTo;
        const Quaternion& turn = carrier->localRotation;
        float w = turn.w, x = turn.x, y = turn.y, z = turn.z;
        float xx = x * x;
        float axisX = w * w - LengthSquaredUnrounded<Sum3::ZYX>(x, y, z) + (xx + xx);
        float axisY = Dot2DUnrounded(x, z, y, w) * 2.0f;   // (xy + zw) * 2
        float axisZ = (x * z - y * w) * 2.0f;
        float scale = carrier->bounds.radius * p->velocity.x * 0.65f;
        float offsetY = axisY * scale;
        float offsetZ = axisZ * scale;
        target.x = axisX * scale + target.x;
        target.y = offsetY + target.y;
        target.z = offsetZ + target.z;
    }
    p->size = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) * size *
                    0.25f;                       // (0.8 + [0, 0.4)) * size / 4
    float keep = 0.7f;
    float pull = 0.3f;
    p->pos.x = p->pos.x * keep + target.x * pull;
    p->pos.y = p->pos.y * keep + target.y * pull;
    p->pos.z = p->pos.z * keep + size * 0.4f + target.z * pull;
    return 1;
}

// VisionStar: up to five particles. With lifetime 1 they are a star (frame 1) at the
// source and four satellites half a unit out, at the angles 2 pi times 0.2, 0.4, 0.6 and 0.8 (the
// first at angle 0); with another lifetime (used as the frame) just the one particle.
int ParticleSource::EmitVisionStar(Particle* p, int) {
    int count = lib::Count(particles);
    if (count > 4) return 0;
    if (count > 0 && lifetime != 1) return 0;
    p->color = 0xFFFFFF;
    p->frame = lifetime;
    p->pos = origin;
    p->size = 0.0f;
    if (lifetime != 1) return 1;
    if (count == 0) {
        p->velocity.x = std::cos(0.0f);
        p->velocity.y = std::sin(0.0f);
        p->velocity.z = 0.0f;
    } else {
        float angle = kSpinAngle * SpinRate(count);
        p->velocity.x = std::cos(angle);
        p->velocity.y = std::sin(angle);
        p->velocity.z = 0.0f;
    }
    float half = 0.5f;
    p->velocity.x = p->velocity.x * half;
    p->velocity.y = p->velocity.y * half;
    p->velocity.z = p->velocity.z * half;
    p->pos.x = p->pos.x + p->velocity.x;
    p->pos.y = p->velocity.y + p->pos.y;
    p->pos.z = p->velocity.z + p->pos.z;
    return 1;
}

// VisionStar: the particles follow the source; with lifetime 1 their offsets turn by
// 0.07 radians per tick (and they flicker less). VisionStar3 (0x204C) sits 0.7 higher. The size
// eases (0.95) towards a random flicker of the source's size; once the source stops emitting (or
// loses its carrier) the source's size is zeroed and the particles fade, dropped below 0.03.
int ParticleSource::UpdateVisionStar(Particle* p) {
    float oldSize = p->size;
    p->color = 0xFFFFFFFF;
    p->pos = origin;
    p->size = 1.0f;
    float flicker;
    flicker = 0.3f;     // 0.3
    if (lifetime == 1) {
        float c = std::cos(0.07f);
        flicker = 0.17f;     // 0.17
        float s = std::sin(0.07f);
        float turnedY = p->velocity.y * c - p->velocity.x * s;
        float turnedX = c * p->velocity.x + p->velocity.y * s;
        p->velocity.y = turnedY;
        p->velocity.x = turnedX;
        p->pos.x = p->pos.x + p->velocity.x;
        p->pos.y = p->velocity.y + p->pos.y;
        p->pos.z = p->velocity.z + p->pos.z;
    }
    if (ParticleTypeOf(type) == ParticleType::VisionStar4) p->pos.z = p->pos.z + 0.7f;
    float eased = (Unsigned(Next()) * 4.1909515e-10f + 0.1f) * size * flicker *
                       0.05f +
                   oldSize * 0.95f;
    p->size = eased;
    if ((flags & ParticleSourceFlag::Emitting) && attachedTo) return 1;
    p->size = eased * 0.95f;
    size = 0.0f;
    EI_NAN_AUDIT(p->size);
    if (p->size < 0.03f) {
        p->size = 0.0f;
        p->color = 0xFFFFFF;
        return 0;
    }
    return 1;
}

// ZoneExit: alpha by the life left (kZoneExitAlpha), dropped past 0; rising 0.05
// per tick, aux0 slowing by 0.9, cycling through four frames, sized by velocity.x.
int ParticleSource::UpdateZoneExit(Particle* p) {
    int life = p->life;
    p->color = (p->color & 0xFFFFFF) | (static_cast<std::uint32_t>(ByLife(kZoneExitAlpha, life, 255)) << 24);
    p->life = --life;
    if (life < 0) return 0;
    p->pos.z = p->pos.z + 0.05f;
    p->aux0 = p->aux0 * 0.9f;
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->size = p->velocity.x;
    return 1;
}

// --- FeebleMind (0x2049) update ----------------------------------------------------------------

// FeebleMind: alpha by the life left (dropped past 0); the particle circles: its offset
// (velocity) is taken off, turned by 0.07 radians and grown by 1.1, then put back with a rise of
// 0.05. The size is aux2 (growing by 1.04 per tick), the frame cycles through 16.
int ParticleSource::UpdateFeebleMind(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    float nextSize = p->aux2;
    p->color = (static_cast<std::uint32_t>(kFeebleAlpha[life]) << 24) | 0xFFFFFF;
    int frame = (p->frame + 1) & 15;
    p->pos.x = p->pos.x - p->velocity.x;
    p->pos.y = p->pos.y - p->velocity.y;
    p->pos.z = p->pos.z - p->velocity.z;
    float c = std::cos(0.07f);
    float s = std::sin(0.07f);
    float turnedY = p->velocity.y * c - p->velocity.x * s;
    float turnedX = c * p->velocity.x + p->velocity.y * s;
    float grow = 1.1f;
    p->velocity.x = turnedX * grow;
    p->velocity.y = turnedY * grow;
    p->pos.x = p->velocity.x + p->pos.x;
    p->pos.y = p->velocity.y + p->pos.y;
    float z = p->velocity.z + p->pos.z;
    p->size = nextSize;
    p->frame = frame;
    p->pos.z = z + 0.05f;
    p->aux2 = p->aux2 * 1.04f;
    return 1;
}

// --- FeetCloud (0x2047): puffs at the carrier's feet; the emission is EmitFeetCloud --------------

// FeetCloud puffs: alpha by the life left (capped at 8 once the source stops emitting or loses its carrier;
// dropped past 0), the source's lifetime as the colour. The puff's offset (aux0..aux2) grows by its speed
// (drift, slowing by 0.9) and the puff eases (velocity.y of the way, which shrinks by 0.98) towards its foot
// (element `state`) plus the offset; its size is velocity.x (shrinking by 0.8).
int ParticleSource::UpdateFeetCloud(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    if (!(flags & ParticleSourceFlag::Emitting) || !attachedTo) {
        if (life > 8) life = 8;
        p->life = life;
    }
    p->color = (static_cast<std::uint32_t>(kFeetAlpha[life]) << 24) + static_cast<std::uint32_t>(lifetime);
    float* offset = &p->aux0;
    offset[0] = p->drift.x + offset[0];
    offset[1] = p->drift.y + offset[1];
    offset[2] = p->drift.z + offset[2];
    float oldY = p->pos.y, oldZ = p->pos.z;
    float oldX = p->pos.x;
    float pull = p->velocity.y;
    const ParticleElement& foot = elements[p->state];
    float targetX = offset[0] + foot.pos.x;
    float targetY = offset[1] + foot.pos.y;
    float targetZ = offset[2] + foot.pos.z;
    float towardX = targetX * pull;
    float towardY = targetY * pull;
    float towardZ = targetZ * pull;
    float keep = EI_P_ONE - pull;
    float keepF = keep;
    float x = oldX * keep + towardX;
    float keptY = keepF * oldY;
    float keptZ = keepF * oldZ;
    p->pos.y = keptY + towardY;
    p->size = p->velocity.x;
    p->pos.x = x;
    p->pos.z = keptZ + towardZ;
    p->velocity.x = p->velocity.x * 0.8f;
    p->velocity.y = p->velocity.y * 0.98f;
    float slow = 0.9f;
    p->drift.x = p->drift.x * slow;
    p->drift.y = p->drift.y * slow;
    p->drift.z = p->drift.z * slow;
    return 1;
}

// FeetCloud feet: on a unit carrier the element sits on its foot (left when ticks is set, else
// right), else on its body, else on its position raised by `above.z`; without one on the source.
void ParticleSource::TickFeetCloud(ParticleElement* e) {
    if (attachedTo && attachedTo->GetClassId() == ClassId::Unit) {
        CObject3D* unit = attachedTo;
        FigureMesh* part = unit->FindPartByPrefix(e->ticks ? kLeftFoot : kRightFoot);
        if (!part) part = unit->FindPartByPrefix(kBody);
        if (part) {
            Vector3 at;
            FigurePartPosition(unit, &at, part);
            e->pos = at;
        } else {
            const float* place = &unit->localPosition.x;
            float raise = unit->bounds.boxMax.z;
            std::memcpy(&e->pos, place, 2 * sizeof(float));
            e->pos.z = place[2] + raise;
        }
    } else {
        e->pos = origin;
    }
    Vector3 offset;
    AttachmentOffset(&offset, &e->foot.radius);
    e->foot.countdown = 0;
}

// --- Silence (0x2048) ----------------------------------------------------------------------------

// Silence: the first two particles are orbs at the source (state 1) circling in step
// (velocity.x the angle, velocity.y its step; aux0 the tilt, aux1 its step; the second starts at pi).
// Further ones are wisps (state 0) somewhere along a random orb's last step, thrown out in a
// random direction (inside the unit ball, normalised) by 0.8 of the orb's size.
int ParticleSource::EmitSilence(Particle* p, int) {
    if (std::ssize(particles) <= 1) {
        p->color = 0xFFFFFF;
        p->frame = 6;
        p->pos = origin;
        p->size = 0.0f;
        float start = !particles.empty() ? 0.0f : 3.1416f;    // 0 / pi
        p->velocity.x = start;
        p->aux0 = start;
        p->velocity.y = 0.1f;     // 0.1
        p->aux1 = 0.12f;     // 0.12
        p->aux2 = 1.0f;
        p->state = 1;
        return 1;
    }
    auto node = particles.begin();
    if (Next() & 1) ++node;
    const Particle* orb = *node;
    float t = Unsigned(Next()) * EI_P_SCALE_1;
    float startX = orb->pos.x - t * orb->drift.x;
    float startY = orb->pos.y - t * orb->drift.y;
    float startZ = orb->pos.z - t * orb->drift.z;
    float x, y, z;
    for (int tries = 10;;) {
        x = SignedUnit();
        y = g_clientRandom.Range(-1.0f, 1.0f);
        float zu = g_clientRandom.Range(-1.0f, 1.0f);
        z = zu;
        if (zu * z + y * y + x * x < EI_P_ONE) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    Vector3 v;
    v.Construct(x, y, z);
    float s = orb->size;
    float a = v.x * s;
    float b = v.y * s;
    float c = s * v.z;
    p->size = 0.0f;
    float k = 0.8f;
    float a8 = a * k;
    float b8 = b * k;
    float c8 = c * k;
    p->pos.x = a8 + startX;
    float y8 = b8 + startY;
    p->pos.z = c8 + startZ;
    p->pos.y = y8;
    p->velocity.x = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) * size *
                          0.08f;    // (0.8 + [0, 0.4)) * size * 0.08
    p->color = 0xFFFFFFFF;
    p->frame = 6;
    p->state = 0;
    p->life = 7;
    return 1;
}

// Silence: once the source stops emitting (or loses its carrier) the orbs
// shrink the source by 0.85 per tick, everything going below 0.02. An orb circles the source on a
// tilted circle (radius: half the carrier's, kept in aux2), half a radius up, keeping its step in
// drift and flickering in size. A wisp shows velocity.x (shrinking by 0.8) for its life.
int ParticleSource::UpdateSilence(Particle* p) {
    if (!(flags & ParticleSourceFlag::Emitting) || !attachedTo) {
        if (p->state) size = size * 0.85f;
        EI_NAN_AUDIT(size);
        if (size < 0.02f) {
            p->size = 0.0f;
            p->color = 0xFFFFFF;
            return 0;
        }
    }
    if (!p->state) {
        if (--p->life <= 0) {
            p->size = 0.0f;
            return 0;
        }
        float shrunk = p->velocity.x * 0.8f;
        p->size = p->velocity.x;
        p->velocity.x = shrunk;
        return 1;
    }
    float radius;
    float* kept = &p->aux2;
    if (attachedTo) {
        radius = attachedTo->bounds.radius *
                 0.5f;
        *kept = radius;
    } else {
        radius = *kept;
    }
    float c = std::cos(p->velocity.x);
    p->color = 0xFFFFFFFF;
    float sn = std::sin(p->velocity.x);
    float hu = std::sin(p->aux0) * 0.5f;
    float h = hu;
    float length = std::sqrt(hu * h + sn * sn + c * c);
    float ku = radius / length;
    float k = ku;
    float cx = c * ku;
    float sy = k * sn;
    float sz = k * h;
    float nx = cx + origin.x;
    float ny = sy + origin.y;
    float nz = sz + origin.z;
    float z = radius * 0.5f + nz;
    p->size = 1.0f;
    p->drift.x = nx - p->pos.x;
    p->pos.x = nx;
    p->drift.y = ny - p->pos.y;
    p->pos.y = ny;
    p->drift.z = z - p->pos.z;
    p->pos.z = z;
    p->velocity.x = p->velocity.y + p->velocity.x;
    p->aux0 = p->aux1 + p->aux0;
    p->size = (Unsigned(Next()) * 4.656614e-11f + 0.9f) * size *
                    0.22f;    // (0.9 + [0, 0.2)) * size * 0.22
    return 1;
}

// --- Modifier1-12 (0x201B-0x2026): update and element tick -------------------------------------------

// Modifier: the first twelve frames are the marker riding its element (state), its size
// shrinking (negative source size) or growing, alpha by the element's tick. Later frames are
// sparks stepping through the frames (back to 12 past the last), flying out by velocity plus their
// spread (aux0..aux2, slowing by 0.9), fading by 48 per tick and shrinking by 0.9.
int ParticleSource::UpdateModifier(Particle* p) {
    if (--p->life == 0) return 0;
    if (p->frame < 12) {
        float oldSize = p->size;
        const ParticleElement& e = elements[p->state];
        p->pos = e.pos;
        p->size = 1.0f;
        EI_NAN_AUDIT(size);
        if (size < EI_P_ZERO) {
            p->size = oldSize * 0.9345794f;    // 1 / 1.07
            p->color = (static_cast<std::uint32_t>(kModifierAlphaIn[elements[p->state].ticks]) << 24) | 0xFFFFFF;
        } else {
            p->size = oldSize * 1.07f;
            p->color = (static_cast<std::uint32_t>(kModifierAlphaOut[elements[p->state].ticks]) << 24) | 0xFFFFFF;
        }
        return 1;
    }
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) p->frame = 12;
        p->frameTimer = frameDelay;
    }
    float* spread = &p->aux0;
    float slow = 0.9f;
    for (int i = 0; i < 3; ++i) spread[i] = spread[i] * slow;
    float oldSize = p->size;
    float x = spread[0] + p->velocity.x;
    float y = spread[1] + p->velocity.y;
    float z = spread[2] + p->velocity.z;
    p->pos.y = y;
    p->pos.x = x;
    p->pos.z = z;
    std::int32_t a = static_cast<std::int32_t>(p->color >> 24) - 0x30;
    if (a < 0) a = 0;
    p->size = oldSize * slow;
    p->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
    return 1;
}

// Modifier markers: for ten ticks (then the source stops emitting) each remembers its
// position and moves to the source plus its offset (ring.offset), raised by spreadMin.x; the offset
// widens by 1.4 (negative source size) or narrows by 1/1.4 per tick.
void ParticleSource::TickModifier(ParticleElement* e) {
    int tick = e->ticks++;
    if (tick >= 10) {
        flags &= ~ParticleSourceFlag::Emitting;
        return;
    }
    e->ring.start = e->pos;
    float* offset = &e->ring.offset.x;
    e->pos.x = offset[0] + origin.x;
    e->pos.y = offset[1] + origin.y;
    e->pos.z = offset[2] + origin.z;
    e->pos.z = spreadMin.x + e->pos.z;
    float k = size >= EI_P_ZERO ? 0.71428573f : 1.4f;    // 1 / 1.4, 1.4
    for (int i = 0; i < 3; ++i) offset[i] = k * offset[i];
}

// --- LightningBlast (0x2030): update and element tick ------------------------------------------------

// LightningBlast: bolts (state n) ride element n-1; a flash (state and frameTimer set) shows once (and makes
// the source emit 50 per tick), then goes. Sparks (state 0): alpha by the life left (the table shifted by 22,
// a quirk kept), size aux0 (shrinking by 0.9); with frameTimer set they jump to velocity and step a frame,
// else cycle four frames flying by drift (slowing by 0.6).
int ParticleSource::UpdateLightning(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    if (p->state != 0) {
        if (p->frameTimer != 0) {
            if (IsZero(p->size)) {
                emitCount = 50;
                p->size = p->aux0;
                return 1;
            }
            p->size = 0.0f;
            return 0;
        }
        p->pos = elements[p->state - 1].pos;
        p->size = p->aux0;
        return 1;
    }
    p->color = (static_cast<std::uint32_t>(kLightningAlpha[life]) << 22) | 0xFFFFFF;
    if (p->frameTimer != 0) {
        float shrunk = p->aux0 * 0.9f;
        ++p->frame;
        p->pos = p->velocity;
        p->size = p->aux0;
        p->aux0 = shrunk;
        return 1;
    }
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->pos.x = p->drift.x + p->pos.x;
    p->pos.y = p->drift.y + p->pos.y;
    p->size = p->aux0;
    p->pos.z = p->drift.z + p->pos.z;
    p->aux0 = p->aux0 * 0.9f;
    float slow = 0.6f;
    p->drift.x = p->drift.x * slow;
    p->drift.y = p->drift.y * slow;
    p->drift.z = p->drift.z * slow;
    return 1;
}

// LightningBlast bolt heads: the source emits 50 per tick; each head remembers its position (bolt.base),
// moves by bolt.fork, falls (its z speed dropping by 0.075), and the source stops from the tenth tick on.
void ParticleSource::TickLightning(ParticleElement* e) {
    emitCount = 50;
    int tick = ++e->ticks;
    e->bolt.base = e->pos;
    const float* speed = &e->bolt.fork.x;
    e->pos.x = speed[0] + e->pos.x;
    e->pos.y = speed[1] + e->pos.y;
    e->pos.z = speed[2] + e->pos.z;
    e->bolt.fork.z = speed[2] - 0.075f;
    if (tick >= 10) flags &= ~ParticleSourceFlag::Emitting;
}

// --- Teleport (0x2019) -----------------------------------------------------------------------------

// Teleport: a spark at a random point of a disc of the source's size (denser near the
// edge), a little below the source and lifted by its first z step. With a positive size it is
// opaque and fades out (state -19 per tick) while its step grows by 1.2; else it starts clear and
// fades in (state 19) while its step shrinks by 1/1.2.
int ParticleSource::EmitTeleport(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float magnitude;
    lib::StoreBytes(&magnitude, Bits(&size) & 0x7FFFFFFFu);
    float t = Unsigned(Next()) * EI_P_SCALE_1;
    float spread = (t + EI_P_ONE) * t * 0.5f;
    float angle = Unsigned(Next()) * 1.4629215e-09f;    // 2 pi * 2^-32
    float reach = magnitude * spread;
    float sx = std::sin(angle) * reach;
    float cy = std::cos(angle) * reach;
    p->pos.y = cy + origin.y;
    p->pos.x = sx + origin.x;
    p->pos.z = origin.z;
    p->size = 0.0f;
    p->pos.z = (Unsigned(Next()) * 4.656613e-11f - 0.4f) * magnitude +
                     p->pos.z;    // [-0.4, -0.2) * |size|
    p->aux0 = (2.0f - spread) * 0.2f;    // (2 - spread) / 5
    if (Unsigned(Next()) * EI_P_SCALE_1 > spread) {
        p->frame = static_cast<int>(Next() % 12) + 4;
    } else {
        p->frame = static_cast<int>(Next() & 3);
    }
    float rise = EI_P_ONE - spread + 3.0f;
    if (size > EI_P_ZERO) {
        p->velocity.x = 1.2f;     // 1.2
        p->state = -19;
        p->velocity.z = rise * magnitude * 0.15f;
    } else {
        p->color = 0xFFFFFF;
        p->velocity.x = 0.8333333f;     // 1 / 1.2
        p->state = 19;
        p->velocity.z = rise * magnitude * 2.25f;
    }
    p->pos.z = p->velocity.z + p->pos.z;
    return 1;
}

// Teleport sparks: frames 0-3 cycle (and 4-15 run on back to 4); the z step (velocity.z) is redone scaled
// by velocity.x; shown at size aux0 (hidden in the last tick); alpha moves by the state per tick, an
// unsigned sum capped at 255 (a quirk kept: a fade out below 19 jumps back to 255).
int ParticleSource::UpdateTeleport(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame == 4) p->frame = 0;
        if (p->frame == 16) p->frame = 4;
        p->frameTimer = frameDelay;
    }
    p->pos.z = p->pos.z - p->velocity.z;
    float step = p->velocity.x * p->velocity.z;
    p->velocity.z = step;
    p->pos.z = step + p->pos.z;
    if (life == 1) {
        p->size = 0.0f;
    } else {
        p->size = p->aux0;
    }
    std::uint32_t a = (p->color >> 24) + static_cast<std::uint32_t>(p->state);
    if (a >= 0xFF) a = 0xFF;
    p->color = (a << 24) | 0xFFFFFF;
    return 1;
}

void ParticleSource::TickTeleport(ParticleElement* e) {
    if (++e->ticks > 35) flags &= ~ParticleSourceFlag::Emitting;
}

// --- CurseStars (0x2050) and StartTrans (0x2051) -------------------------------------------------

// CurseStars: a star at a random point of the ball of the source's size around the
// source, drifting outwards (a tenth of the size per tick), for 20 ticks.
int ParticleSource::EmitCurseStars(Particle* p, int) {
    p->color = 0xFFFFFF;
    p->frame = static_cast<int>(Next() & 15);
    float x, y, z;
    float unused;
    RandomInBall(x, y, z, unused);
    Vector3 v;
    v.Construct(x, y, z);
    float s = size;
    float a = v.x * s;
    float b = v.y * s;
    float c = s * v.z;
    float px = a + origin.x;
    float py = b + origin.y;
    float pz = c + origin.z;
    p->size = 0.0f;
    p->pos.y = py;
    p->pos.x = px;
    p->pos.z = pz;
    float length = std::sqrt(c * c + b * b + a * a);
    float lengthF = length;
    p->life = 20;
    if (length > EI_P_ZERO) {
        float k = size * 0.1f;
        float ak = a * k;
        float bk = b * k;
        float ck = c * k;
        p->drift.x = ak / lengthF;
        p->drift.y = bk / lengthF;
        p->drift.z = ck / lengthF;
    } else {
        p->drift = {};
    }
    return 1;
}

// CurseStars: alpha by the life left (hidden and dropped past 0); a star's size starts
// as a random part of the source's (0.04 of it, flickering) and shrinks by 0.95; it drifts (slowing
// by 0.9). Without a carrier the source stops emitting and is let go (Attach(nullptr)).
int ParticleSource::UpdateCurseStars(Particle* p) {
    int life = --p->life;
    if (life < 0) {
        p->color = 0xFFFFFF;
        p->size = 0.0f;
        return 0;
    }
    p->color = (static_cast<std::uint32_t>(kCurseAlpha[life]) << 24) | 0xFFFFFF;
    EI_NAN_AUDIT(p->size);
    if (p->size == 0.0f) {
        p->size = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) * size *
                        0.04f;
    } else {
        p->size = p->size * 0.95f;
    }
    p->pos.x = p->drift.x + p->pos.x;
    p->pos.y = p->drift.y + p->pos.y;
    p->pos.z = p->drift.z + p->pos.z;
    float slow = 0.9f;
    p->drift.x = p->drift.x * slow;
    p->drift.y = p->drift.y * slow;
    p->drift.z = p->drift.z * slow;
    if (!attachedTo) {
        flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
        Attach(nullptr);
    }
    return 1;
}

// StartTrans: the first particle is the glow (state 1, frame 12, 4 ticks); the others
// (15 ticks) start in a random direction at the source's size from it and come back in 18 ticks.
int ParticleSource::EmitStartTrans(Particle* p, int) {
    if (particles.empty()) {
        p->life = 4;
        p->state = 1;
        p->frame = 12;
        p->pos = origin;
    } else {
        p->life = 15;
        p->state = 0;
        p->frame = static_cast<int>(Next() % 12);
        float x, y, z;
        float lengthSquared;
        RandomInBall(x, y, z, lengthSquared);
        float length = std::sqrt(lengthSquared);
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        float s = size;
        p->velocity.x = direction.x * s;
        p->velocity.y = direction.y * s;
        p->velocity.z = direction.z * s;
        float px = p->velocity.x + origin.x;
        float py = p->velocity.y + origin.y;
        float pz = p->velocity.z + origin.z;
        p->pos.y = py;
        p->pos.x = px;
        p->pos.z = pz;
        float ticks = static_cast<float>(p->life + 3);
        float nx = -p->velocity.x;
        float ny = -p->velocity.y;
        float nz = -p->velocity.z;
        p->drift.x = nx / ticks;
        p->drift.y = ny / ticks;
        p->drift.z = nz / ticks;
        p->aux0 = size * 0.05f;
    }
    p->color = 0xFFFFFF;
    p->size = 0.0f;
    return 1;
}

// StartTrans: the glow flickers at trans.scale while carried, then fades (by the
// life table, size times 0.6); the others come in, cycling four frames at size aux0, fading out over
// their last ticks (capped at 4 once the carrier is gone).
int ParticleSource::UpdateStartTrans(Particle* p) {
    if (p->state != 0) {
        if (!attachedTo) {
            int life = --p->life;
            if (life < 0) return 0;
            p->color = (static_cast<std::uint32_t>(kTransAlpha[life]) << 24) | 0xFFFFFF;
            p->size = p->size * 0.6f;
            return 1;
        }
        p->color = 0xFFFFFFFF;
        p->size = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) *
                        elements[0].trans.scale;
        return 1;
    }
    int life = --p->life;
    if (life < 0) return 0;
    if (!attachedTo) {
        if (life >= 4) life = 4;
        p->life = life;
    }
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->pos.x = p->drift.x + p->pos.x;
    p->pos.y = p->drift.y + p->pos.y;
    p->size = p->aux0;
    p->pos.z = p->drift.z + p->pos.z;
    p->color = (static_cast<std::uint32_t>(kTransAlpha[p->life]) << 24) | 0xFFFFFF;
    return 1;
}

// --- Nuke emission ------------------------------------------------------------------------------------

// Nuke: a puff at a random point of the disc of a fifth of the source's size, at a random height up to twice
// the size, drifting outwards and up (less as the blast ages: element 0's tick); later puffs live shorter,
// start at a later frame and are more opaque (4 ^ tick * 4, capped at 255).
int ParticleSource::EmitNuke(Particle* p, int) {
    int tick = elements[0].ticks;
    int rise = 11 - tick;
    float fifth = size * 0.2f;
    float reach = Unsigned(Next()) * fifth * EI_P_SCALE_1;
    float turn = 3.1415927f + 3.1415927f;    // 2 pi
    float angle = Unsigned(Next()) * turn * EI_P_SCALE_1;
    float s = std::sin(angle);
    p->pos.x = reach * s + origin.x;
    float c = std::cos(angle);
    p->pos.y = reach * c + origin.y;
    float top = size + size;
    p->pos.z = Unsigned(Next()) * top * EI_P_SCALE_1 + origin.z;
    p->size = size * 0.2f;
    p->drift.x = s;
    p->drift.y = c;
    p->drift.z = rise * size * 0.1f;
    p->life = rise + 30;
    p->frame = elements[0].ticks / 3;
    std::uint32_t a = static_cast<std::uint32_t>(
        static_cast<int>(PowInline(4.0f, elements[0].ticks) * 4.0f));
    if (a > 0xFF) a = 0xFF;
    p->state = 0;
    p->color = (a << 24) | 0xFFFFFF;
    return 1;
}

// --- Tendril emission (EmitSmoke spread over the extent and laid on the ground) ------------------

// A smoke puff (EmitSmoke), moved by one random amount in [-1, 1) times the extent in x
// and y, and set its own height above the ground there.
int ParticleSource::EmitTendril(Particle* p, int index) {
    if (!EmitSmoke(p, index)) return 0;
    float u = Unsigned(Next()) * EI_P_SCALE_2 - EI_P_ONE;
    p->pos.x = u * extent.x + p->pos.x;
    p->pos.y = u * extent.y + p->pos.y;
    float ground = GroundHeight(p->pos.x, p->pos.y);
    p->pos.z = p->pos.z - origin.z + ground;
    return 1;
}

// --- Ground dust (0x203A / 0x203B) emission ------------------------------------------------------

// Ground dust: the first 128 particles of a tick run out on a circle (index * 0.049
// radians, step 0.1); the others along the four diagonals (by 32, step index * 1/310). All at the
// source, in element 0's colour, 6 ticks.
int ParticleSource::EmitGroundDust(Particle* p, int index) {
    float a, b;
    if (index >= 128) {
        int j = index - 128;
        int diagonal = j / 32;
        float fu = (j & 31) * 0.0032258064f;
        float f = fu;
        a = fu * kDustAxisX[diagonal];
        b = f * kDustAxisY[diagonal];
    } else {
        float fu = index * 0.049140625f;
        float f = fu;
        a = std::sin(fu) * 0.1f;
        b = std::cos(f) * 0.1f;
    }
    p->pos = origin;
    p->size = 0.0f;
    p->life = 6;
    p->velocity.x = a;
    p->frame = 15;
    p->velocity.y = b;
    p->velocity.z = 0.0f;
    p->aux0 = 0.07f;     // 0.07
    p->color = elements[0].plane.color;
    p->aux1 = 1.0f;
    return 1;
}

// --- ShapeChange glow (0x2039) emission and element tick; the update is UpdateGlow ---------------

// ShapeChange: on the first call one element: the last point (-1, -1, -1) and the colour by the alpha field (0
// grey, 1 green, 2 orange). Then one glow per point of the carrying unit (up to typeParam points, offset by
// the client tick), on the ground or platform under it, unless the point is where the last one was; the
// point's index goes in frameTimer and the life is -10 (set by UpdateGlow).
int ParticleSource::EmitGlow(Particle* p, int index) {
    CObject3D* unit = attachedTo;
    if (std::ssize(elements) == 0) {
        elements.resize(1);
        float* mark = &elements[0].glow.mark.x;
        mark[0] = -1.0f;
        mark[1] = -1.0f;
        mark[2] = -1.0f;
        EI_NAN_AUDIT(alpha);
        if (alpha == 0.0f) {
            elements[0].glow.color = 0xB0B0B0;
        } else if (Bits(&alpha) == 0x3F800000) {
            elements[0].glow.color = 0x40FF40;
        } else if (Bits(&alpha) == 0x40000000) {
            elements[0].glow.color = 0xFF8028;
        }
    }
    if (!unit) return 0;
    if (index > typeParam) return 0;
    Vector3 scratch;
    Vector3 point = *Carrier(unit)->PositionAt(&scratch, index + g_clientWorld.tick);
    float* mark = &elements[0].glow.mark.x;
    if (Same(mark[0], point.x) && Same(mark[1], point.y) && Same(mark[2], point.z)) return 0;
    elements[0].glow.mark = point;
    Vector3 under;
    under.x = point.x;
    under.y = point.y;
    under.z = 0.0f;
    float ground = static_cast<float>(g_clientWorld.RawSurfaceHeightAt(point.x, point.y));
    float top = g_clientWorld.platforms.SurfaceHeight(under, ground);
    p->pos.y = point.y;
    p->size = 0.07f;     // 0.07
    p->pos.x = point.x;
    p->frame = 15;
    p->life = -10;
    p->pos.z = top + 0.15f;
    p->frameTimer = index;
    p->color = elements[0].glow.color;
    return 1;
}

void ParticleSource::TickGlow(ParticleElement*) {
    flags &= ~ParticleSourceFlag::Emitting;
}

// --- Stun (0x204D) -------------------------------------------------------------------------------

// Stun: a star at a random point of the ball of 0.8 times the carrier figure's radius around the source, for
// 20 ticks, with a random size factor in velocity.x. A unit carrier in state 3 gets four stars a tick living 5
// ticks, else one; once the carrier is gone (nothing emitted) or it is dead (IsDead) the source stops and lets
// it go.
int ParticleSource::EmitStun(Particle* p, int) {
    Vector3 offset;
    float radius;
    AttachmentOffset(&offset, &radius);
    p->color = 0xFFFFFF;
    p->frame = 3;
    float x, y, z;
    for (int tries = 10;;) {
        x = SignedUnit();
        y = SignedUnit();
        float zu = g_clientRandom.Range(-1.0f, 1.0f);
        z = zu;
        if (zu * z + y * y + x * x < EI_P_ONE) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    Vector3 v;
    v.Construct(x, y, z);
    float k = radius * 0.8f;
    float a = v.x * k;
    float b = v.y * k;
    float c = k * v.z;
    float px = a + origin.x;
    float py = b + origin.y;
    float pz = c + origin.z;
    p->size = 0.0f;
    p->pos.y = py;
    p->pos.z = pz;
    p->pos.x = px;
    p->life = 20;
    p->velocity.x = Unsigned(Next()) * 1.3969838e-10f + 0.7f;
    auto* unit = static_cast<CUnit*>(attachedTo);   // the carrier is taken as a unit
    if (!unit) {
        flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
        Attach(nullptr);
        return 0;
    }
    if (unit->moveMode == 3) {
        emitCount = 4;
        p->life = 5;
    } else {
        emitCount = 1;
    }
    if (unit->IsDead()) {
        flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
        Attach(nullptr);
    }
    return 1;
}

// Stun stars: hidden and dropped past their life; else flickering at a random size of
// the source's times velocity.x. Without a carrier the source stops and is let go.
int ParticleSource::UpdateStun(Particle* p) {
    if (--p->life < 0) {
        p->color = 0xFFFFFF;
        p->size = 0.0f;
        return 0;
    }
    p->color = 0xFFFFFFFF;
    p->size = (Unsigned(Next()) * 9.3132266e-11f + 0.8f) * size *
                    p->velocity.x;
    if (!attachedTo) {
        flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
        Attach(nullptr);
    }
    return 1;
}
