// ParticleSource's per-type callbacks (emit, update, element tick); each names the source types using it.
#include "angles.h"
#include "core_maths.h"
#include "effects.h"
#include "game_singletons.h"
#include "map_object_figures.h"
#include "particle_common.h"
#include "random.h"
#include "unit.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstring>

// --- Casting effects (0x2027-0x202A): sparks rising around the caster -----------------------------

namespace {

const char* NoCarrierCasting() { return "Particle effect CastingElemental without carrier\n"; }
constexpr std::int32_t kCastAlphaByCountdown[7] = /*0078A964*/ {0, 32, 64, 112, 160, 208, 255};
constexpr std::int32_t kCastAlphaByLifeWide[14] = /*0078A980*/ {0, 32, 64, 112, 160, 208, 255, 255, 224, 192, 128, 64, 32, 16};   // 0x202A
constexpr std::int32_t kCastAlphaByLife[14] = /*0078A9BC*/ {0, 255, 255, 255, 255, 255, 255, 255, 224, 192, 128, 64, 32, 16};

// A random point of the cube [-1, 1)^3, drawn x, y, z.
Vector3 RandomInCube() {
    return {g_clientRandom.Range(-1.0f, 1.0f), g_clientRandom.Range(-1.0f, 1.0f), g_clientRandom.Range(-1.0f, 1.0f)};
}

// A random point in the unit ball (ten tries, the last one kept) and its squared length.
Vector3 RandomInBall(float& lengthSquared) {
    Vector3 v;
    for (int tries = 10;;) {
        v = RandomInCube();
        lengthSquared = LengthSquared(v);
        if (lengthSquared < 1.0f) return v;   // random: never NaN
        if (!(--tries > 0)) return v;
    }
}

Vector3 RandomInBall() {
    float lengthSquared;
    return RandomInBall(lengthSquared);
}

// The first emission: element 0 from the carrier's size, elements 1..lifetime random swirls.
// 0 (and a debug message) without a carrier.
bool SetUpCasting(ParticleSource* s) {
    if (!s->attachedTo) {
        game::DebugLog("%s", NoCarrierCasting());
        return false;
    }
    auto [offset, radius] = s->AttachmentOffset();
    radius *= 0.8f;
    float scale = s->attachedTo->GetClassId() == ClassId::Unit ? s->attachedTo->Radius() : radius;
    if (ParticleTypeOf(s->type) == ParticleType::CastingDivination) scale = scale + scale;
    else scale = scale * 3.0f;
    s->elements.resize(s->lifetime + 1);
    ParticleElement* e = s->elements.data();
    e->cast.alpha = 0xFF;
    e->ticks = 6;
    e->cast.radius = scale;
    if (ParticleTypeOf(s->type) == ParticleType::CastingDivination) {
        e->cast.size = 0.07f;
        e->cast.growth = 1.05f;
    } else {
        e->cast.size = 0.08f;
        e->cast.growth = 1.07f;
    }
    for (int i = 1; i <= s->lifetime; ++i) {
        s->elements[i].swirl.position = RandomInBall() * 10.0f;
        s->elements[i].swirl.velocity = RandomInBall() * 0.1f;
    }
    return true;
}

constexpr float kStillSpeed = 0.01f;   // the carrier's speed below which a sphere is round

const char* NoCarrierSphere() { return "Particle effect Link without carrier\n"; }

// Once per tick (the first emission): the sphere's height and radius ease towards the carrier's
// figure, the emission rate follows the source's size, and element 1 smooths the carrier's motion.
bool FollowCarrier(ParticleSource* s) {
    if (!s->attachedTo) {
        game::DebugLog("%s", NoCarrierSphere());
        return false;
    }
    auto [offset, radius] = s->AttachmentOffset();
    radius *= 1.2f;
    float height = offset.z;
    if (s->elements.empty()) {
        s->elements.resize(2);
        s->elements[1].sphereTilt.tiltCos = lib::Cos(0.01f);
        s->elements[1].sphereTilt.tiltSin = lib::Sin(0.01f);
        s->elements[1].pos = {};
        s->elements[0].pos = {};
        s->elements[0].pos.z = height;
        s->elements[0].sphere.radius = radius;
    }
    ParticleElement* e = s->elements.data();
    e->pos.z = e->pos.z * 0.9f + height * 0.1f;
    float& sphereRadius = s->elements[0].sphere.radius;
    sphereRadius = radius * 0.1f + sphereRadius * 0.9f;
    float rate = s->size * (ParticleTypeOf(s->type) == ParticleType::Antimagic ? 24.0f : 6.0f);
    s->emitCount = std::lround(rate);
    Vector3& smoothed = s->elements[1].pos;
    smoothed = smoothed * 0.7f + s->motion;
    float& speed = s->elements[0].sphere.speed;
    speed = std::sqrt(LengthSquared(smoothed));
    if (speed < kStillSpeed) speed = 0.0f;
    return true;
}

const char* NoCarrierRing() { return "Particle effect Casting without carrier\n"; }
const char* NoCarrierHealing() { return "Particle effect Healing without carrier\n"; }
constexpr int kFlameState = 0xFFFF;     // a burning-out flame (the update's last phase)

// The rings around the carrier (emitCount of them, already allocated): stacked from the carrier's feet with a
// random lift, their flames spaced evenly around the circle; the source's height is the radius growth per
// tick (Healing also sets the flame growth). Each ring counts down from `countdown`.
void PlaceRings(ParticleSource* s, int countdown) {
    float step = ei::kRadiansPerDegree * (360.0f / s->emitCount);
    auto [offset, fullRadius] = s->AttachmentOffset();
    float angle = 0.0f;
    offset.z -= fullRadius;
    float diameter = fullRadius + fullRadius;
    float rise = fullRadius * 1.1f * 0.027777778f;   // 1/36
    s->height = rise + rise;
    if (ParticleTypeOf(s->type) == ParticleType::Healing) s->growth = (s->size + 1.0f) * 0.0069444445f;   // 1/144
    if (s->emitCount > 0) {
        float low = diameter * -0.1f;
        float high = diameter * 0.1f;
        for (int i = 0; i < s->emitCount; ++i) {
            ParticleElement* e = &s->elements[i];
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
    s->emitState = 0;
}

// The flame phase: one flame per ring (state = the ring), then one call that only ends the phase
// (result 0). False once the phase is over.
bool EmitRingFlame(ParticleSource* s, Particle* p, int& result) {
    int emitted = s->emitState;
    if (emitted > std::ssize(s->elements)) return false;
    s->emitState = emitted + 1;
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
    p->pos = ring.pos + s->origin;
    if (ParticleTypeOf(s->type) == ParticleType::Healing) p->size = (s->size + 1.0f) * 0.1f;
    else p->size = s->size * 0.0015f;
    result = 1;
    return true;
}

// A spark in a box of +-spread around a ring's centre. The original's z uses -spread for both the range and
// the offset.
void PlaceRingSpark(ParticleSource* s, Particle* p, const ParticleElement& ring, float spread) {
    float low = -spread;
    p->pos.x = g_clientRandom.Range(0.0f, spread - low) + ring.pos.x + s->origin.x + low;
    p->pos.y = g_clientRandom.Range(0.0f, spread - low) + ring.pos.y + s->origin.y + low;
    p->pos.z = ring.pos.z - g_clientRandom.Range(0.0f, low) + s->origin.z + low;
}

void FollowRing(ParticleSource* s, Particle* p, int ring) {
    const ParticleElement& e = s->elements[ring];
    p->pos = s->origin + e.pos;
}

// Turns a ring's flame point 10 degrees around its centre; the ring rises.
void TurnRing(ParticleElement* e) {
    float& radius = e->flames.radius;
    float& angle = e->flames.angle;
    angle = (ei::kRadiansPerDegree * 10.0f) + angle;
    e->pos.x = lib::Sin(angle) * radius + e->flames.centre.x;
    e->pos.y = lib::Cos(angle) * radius + e->flames.centre.y;
    e->pos.z = e->flames.rise + e->pos.z;
}

// Alpha rising and falling: a spark by its life (from the start), a marker one entry on, a column backwards
// from the last (ColumnAlpha).
constexpr std::int32_t kRiseFallAlpha[19] = /*0078A914*/ {0, 0, 32, 64, 112, 160, 208, 255, 255, 255, 255, 255, 255, 255, 207, 159, 111,
                                                 63, 15};

// Adds orbiter `k` (the k-th of `lifetime`, evenly spaced around the carrier) and emits its marker
// particle (state -1 - k).
int AddOrbiter(ParticleSource* s, Particle* p) {
    float orbit, carrier;     // the orbit radius and the carrier's radius
    int k = lib::Count(s->elements);
    if (k == 0) {
        auto [offset, radius] = s->AttachmentOffset();
        carrier = radius;
        orbit = radius * 0.5f;
    } else {
        orbit = s->elements[0].orbiter.relative.x;
        carrier = s->elements[0].orbiter.scale;
    }
    s->elements.resize(k + 1);
    float share = k * kPi;
    float angle = (share + share) / s->lifetime;
    ParticleElement* e = &s->elements[k];
    float lift = carrier * 0.4f;
    float sign = 1.0f;
    float turn;
    switch (s->type - static_cast<int>(ParticleType::CastingIllusion)) {
    case 0:
    case 2:
        turn = 0.3f;
        break;
    case 1:
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
    e->pos = s->origin + relative;
    e->orbiter.turnCos = lib::Cos(turn);
    e->orbiter.previous = e->pos;
    e->orbiter.turnSin = lib::Sin(turn);
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

// The beam's frame from the extent (the first emission).
void SetUpLink(ParticleSource* s) {
    s->elements.resize(4);
    const Vector3& x = s->extent;
    s->elements[0].link.length = std::sqrt(LengthSquared(x));
    s->elements[0].link.inverseLength = 1.0f / s->elements[0].link.length;

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

    s->elements[1].linkAxis.axis = x / s->elements[0].link.length;
    s->elements[2].linkAxis.axis = side;
    s->elements[3].linkAxis.axis = cross;
    s->elements[0].ticks = 0xFF;
}

constexpr int kBlastCore = -1222;     // the core flash, emitted once at the start

constexpr std::int32_t kBlastAlpha[14] = /*0078A5DC*/ {255, 255, 207, 175, 128, 128, 128, 128, 128, 175, 207, 192, 128, 48};   // by frame

// A flame's size and frame timing: the size from the fragments' radius, the frame step random.
void BlastFlameTiming(ParticleSource* s, Particle* p) {
    float& flameSize = p->aux.x;
    float& framePosition = p->aux.y;
    float jitter = WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f;
    flameSize = s->elements[0].fragment.radius * s->size * jitter * 0.6f;
    framePosition = static_cast<float>(p->frame);
}

constexpr float kFogCell = 1.0f;   // the spacing of the puffs

// A random value in [low, high) of the spread box component, added to `base` (the original's order: the
// random part, the base, then the low end).
float SpreadAround(float base, float low, float high) {
    return g_clientRandom.Range(0.0f, high - low) + base + low;
}
Vector3 SpreadAround(const Vector3& base, const Vector3& low, const Vector3& high) {
    return {SpreadAround(base.x, low.x, high.x), SpreadAround(base.y, low.y, high.y),
            SpreadAround(base.z, low.z, high.z)};
}

// A random point of the box [low, high), drawn x, y, z.
Vector3 RandomInBox(const Vector3& low, const Vector3& high) {
    return {g_clientRandom.Range(low.x, high.x), g_clientRandom.Range(low.y, high.y),
            g_clientRandom.Range(low.z, high.z)};
}

constexpr std::int32_t kShowerAlpha[16] = /*0078A6B4*/ {0, 64, 128, 192, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

// alpha * 8 random unit directions (at least one) in the elements.
void ChooseShowerRays(ParticleSource* s) {
    s->rayCount = std::lround(s->alpha * 8.0f);
    if (s->rayCount < 1) s->rayCount = 1;
    int rays = s->rayCount;
    s->emitCount = rays * 10;
    s->elements.resize(rays);
    for (int i = 0; i < s->rayCount; ++i) {
        float lengthSquared;
        Vector3 v = RandomInBall(lengthSquared);
        s->elements[i].pos = v / std::sqrt(lengthSquared);
        s->elements[i].ray.age = 0;
    }
}

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
    high = s->size * 0.2f;
    low = s->size * -0.2f;
    return g_clientRandom.Range(0.0f, high - low);
}

// The alpha by remaining life (1-12), read backwards from kRiseFallAlpha's end. The update after death (life -1)
// reads past the table: 0 there (the original read the next table's first entry).
std::uint32_t ColumnAlpha(int life) { return life < 0 ? 0 : static_cast<std::uint32_t>(kRiseFallAlpha[19 - life]); }

// The ring from the carrier's figure (the first emission). False without a carrier.
bool SetUpColumn(ParticleSource* s) {
    if (!s->attachedTo) {
        game::DebugLog("%s", NoCarrierCasting());
        return false;
    }
    auto [offset, radius] = s->AttachmentOffset();
    float height = radius + radius;
    float foot = -offset.z - height * 0.3f;
    float ring = s->attachedTo->GetClassId() == ClassId::Unit ? s->attachedTo->Radius() : radius * 0.8f;
    s->elements.resize(1);
    ParticleElement* e = s->elements.data();
    e->column.radius = ring + ring;
    e->column.height = height;
    e->column.foot = foot;
    e->ticks = 1;
    return true;
}

const char* NoCarrierGas() { return "Particle effect Link without carrier\n"; }

constexpr float kFunnelSpans = 5.0f;   // the spans between the six rings

// The funnel's centre (x, y) at height h (0..1): between ring k and k+1 (h * 5 rings).
Vector2 FunnelCentre(const ParticleSource* s, float h) {
    int k = static_cast<int>(h);
    if (k >= lib::Count(s->elements) - 2) k = lib::Count(s->elements) - 2;
    float f = h - k;
    float t = MinKeepA(f, 1.0f);
    float u = 1.0f - t;
    const ParticleElement& a = s->elements[k];
    const ParticleElement& b = s->elements[k + 1];
    return t * XY(b.pos) + u * XY(a.pos);
}

constexpr std::int32_t kCylinderAlpha[32] = /*0078AB18*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255};

// The stars on the carrier's orbit (the first emission).
void SetUpCylinder(ParticleSource* s) {
    auto [offset, radius] = s->AttachmentOffset();
    float turn = (0.3f * kPi) * 0.13333334f;
    if (ParticleTypeOf(s->type) == ParticleType::Cylinder2) turn = -turn;
    float c = lib::Cos(turn);
    float sn = lib::Sin(turn);
    int count = lib::Count(s->elements);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        float orbit = radius * 0.7f;
        float twice = static_cast<float>(2 * i);
        ParticleElement& e = s->elements[i];
        e.pos.x = lib::Cos(twice * kPi / count) * orbit;
        e.pos.y = lib::Sin(twice * kPi / std::ssize(s->elements)) * orbit;
        e.pos.z = radius * 0.2f;
        e.star.step = {};
        e.star.rotation[0] = c;
        e.star.rotation[1] = sn;
        e.ticks = 18;
        e.star.rise = (2.0f / e.ticks) * radius * 0.04f;
        e.star.inverseOrbit = 1.0f / orbit;
        e.star.orbit = orbit;
        count = lib::Count(s->elements);
    }
}

constexpr float kTrailMoving = 0.00001f;   // the step below which a trail has no length

// The trail velocity: a random amount of the side axis (SideVelocity), plus one of the up axis (AddUp).
void SideVelocity(ParticleSource* s, Particle* p, float low, float high) {
    float q = g_clientRandom.Range(low, high);
    p->velocity = q * s->side;
}

void AddUp(ParticleSource* s, Particle* p, float q) {
    p->velocity += q * s->up;
}

// By age; entries 14-16 (0): a drop's last tick, and the update after its death, read past the 14 (the original's zero
// tail).
constexpr float kAcidSpread[17] = /*0078A61C*/ {0.0f, 0.85f, 0.93f, 1.0f, 0.97f, 0.93f, 0.87f, 0.8f, 0.73f, 0.65f, 0.55f, 0.43f,
                                             0.3f, 0.2f, 0.0f, 0.0f, 0.0f};
constexpr float kAcidSize[17] = /*0078A668*/ {1.1f, 0.85f, 0.93f, 1.0f, 0.97f, 0.93f, 0.87f, 0.8f, 0.73f, 0.65f, 0.55f, 0.43f,
                                           0.3f, 0.2f, 0.0f, 0.0f, 0.0f};

// Position: the drop's start plus its direction scaled by the source's size and the spread table.
void PlaceAcid(ParticleSource* s, Particle* p, int age, Vector3& out) {
    const Vector3& direction = p->velocity;
    const Vector3& launch = p->drift;
    out = s->size * direction * kAcidSpread[age] + launch;
}

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
Vector3 FigurePartPosition(Object3D* unit, const FigureMesh* part) {
    Vector3 at = unit->PartWorldPosition(part);
    return at;
}

const char* kLeftFoot = "ll3";
const char* kRightFoot = "rl3";
const char* kBody = "bd";

// By tick.
constexpr std::int32_t kModifierAlphaIn[12] = /*0078A800*/ {0, 128, 255, 255, 255, 255, 255, 255, 192, 128, 64, 32};
constexpr std::int32_t kModifierAlphaOut[12] = /*0078A88C*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 128};

constexpr std::int32_t kLightningAlpha[16] = /*0078A9F8*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};   // by life

bool IsZero(float f) {
    return f == 0.0f;
}

// By life.
constexpr std::int32_t kCurseAlpha[24] = /*0078AC98*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                              255, 255, 255, 255, 255, 255, 255};
constexpr std::int32_t kTransAlpha[16] = /*0078ACF8*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};

// base ^ exponent the way VC6 inlined pow here (through ln and 2^x).
float PowInline(float base, int exponent) { return lib::Exp(lib::Log(base) * exponent); }

// By quadrant.
constexpr float kDustAxisX[4] = /*0078AAB8*/ {0.707f, -0.707f, -0.707f, 0.707f};
constexpr float kDustAxisY[4] = /*0078AAC8*/ {0.707f, -0.707f, 0.707f, -0.707f};

bool Same(float a, float b) {
    return a == b;
}

} // namespace

// Casting (0x2027-0x202A). The first call sets the effect up (0x202A then emits its
// marker particle at the source); after that each particle starts on a random direction (pulled
// towards a random swirl), at the effect's radius around the source.
int ParticleSource::EmitCasting(Particle* p, int) {
    if (elements.empty()) {
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
    Vector3& direction = p->velocity;
    float& away = p->aux.x;     // the distance from the source
    float& inward = p->aux.y;   // its step in per tick
    float& swell = p->aux.z;    // the size's factor per tick
    p->color = 0xFFFFFF;
    p->frame = 14;
    p->life = 13;
    p->state = 0;
    p->frameTimer = 1;

    Vector3 v;
    float lengthSquared;
    for (;;) {
        v = RandomInCube();
        lengthSquared = LengthSquared(v);
        if (lengthSquared > 1.0f) continue;
        if (lengthSquared <= 0.0f) continue;   // random: never NaN
        break;
    }
    ParticleElement* state = elements.data();
    std::uint32_t span = static_cast<std::uint32_t>(frameDelay * lifetime + 1);
    int pick = static_cast<int>(g_clientRandom.Next() % span);
    if (pick > 0) {
        // Swirls 0 .. lifetime - 1: swirl 0 is element 0, the effect's state, whose radius and size the original
        // read as a swirl's x and y (z 0).
        const int swirl = (pick - 1) % lifetime;
        v += swirl == 0 ? Vector3{state->cast.radius, state->cast.size, 0.0f} : elements[swirl].swirl.position;
        lengthSquared = LengthSquared(v);
    }
    float inverse = 1.0f / std::sqrt(lengthSquared);
    direction = inverse * v;
    float reach = state->cast.radius * 0.6f;
    away = reach;
    inward = reach * (ParticleTypeOf(type) == ParticleType::CastingDivination ? 0.03846154f : 0.07692308f);   // 1/26, 1/13
    swell = state->cast.growth;
    p->pos = away * direction + origin;
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_8 + 0.6f) * state->cast.size * away;
    return 1;
}

// Casting particles: none once the effect's alpha is 0. The 0x202A marker stays at the
// source with a random size; the others fade by their life (capped by the effect's alpha), run
// their frames backwards (0x2027: every fourth tick held) and spiral in towards the source.
int ParticleSource::UpdateCasting(Particle* p) {
    const Vector3& direction = p->velocity;
    float& away = p->aux.x;
    const float& inward = p->aux.y;
    const float& swell = p->aux.z;
    ParticleElement* state = elements.data();
    if (state->cast.alpha == 0) return 0;
    if (p->state) {
        p->pos = origin;
        p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_3 + 0.85f) * elements[0].cast.radius * 0.3f;
        p->color = (static_cast<std::uint32_t>(elements[0].cast.alpha) << 24) | 0xFFFFFF;
        return 1;
    }
    int life = --p->life;
    if (life == 0) return 0;
    // Past the end of its life (the update after death): what the original read before the table, the countdown
    // table's last entry, or the zero after the wide table's 14 entries.
    std::int32_t fade = ParticleTypeOf(type) == ParticleType::CastingDivination ? ByLife(kCastAlphaByLifeWide, life, kCastAlphaByCountdown[6])
                                        : ByLife(kCastAlphaByLife, life, 0);
    std::int32_t cap = elements[0].cast.alpha;
    if (cap < fade) fade = cap;
    p->color = (static_cast<std::uint32_t>(fade) << 24) | 0xFFFFFF;
    if (ParticleTypeOf(type) == ParticleType::CastingFire && (life & 3) == 0) ++p->frame;
    float oldSize = p->size;
    float reach = away - inward;
    --p->frame;
    away = reach;
    p->pos = reach * direction + origin;
    p->size = oldSize * swell;
    return 1;
}

// Casting elements: element 0 counts down and takes its alpha from the table (frozen while an emitting source is
// attached); the swirls move by their velocity, which gets a random kick in [-k, k), damping and a pull back to the
// centre.
void ParticleSource::TickCasting(ParticleElement* e) {
    if (e == elements.data()) {
        if ((flags & ParticleSourceFlag::Emitting) && attachedTo) return;
        if (e->ticks > 0) --e->ticks;
        e->cast.alpha = kCastAlphaByCountdown[elements[0].ticks];
        return;
    }
    e->swirl.position += e->swirl.velocity;
    const Vector3& position = e->swirl.position;
    Vector3& velocity = e->swirl.velocity;
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

// --- Protection spheres (0x2015-0x2017) and AntiMagic (0x201A) -----------------------------------

// Spheres: a particle on the sphere around the carrier (a random direction at 0.65 of
// the radius), flattened against the carrier's motion when it moves; one in eleven is larger. The
// size follows the life table of the kind.
int ParticleSource::EmitOrbit(Particle* p, int index) {
    Vector3& onSphere = p->velocity;   // the offset from the sphere's centre
    float& sizeFactor = p->drift.x;
    if (index == 0 && !FollowCarrier(this)) return 0;
    float radius = elements[0].sphere.radius;
    float big;
    if (g_clientRandom.Next() % 11 == 0) {
        float base = ParticleTypeOf(type) == ParticleType::Antimagic ? 2.5f : ParticleTypeOf(type) == ParticleType::SphereElectricity ? 3.5f : 1.5f;
        big = g_clientRandom.Range(0.0f, 1.0f) + base;
    } else {
        big = WordToFloat(g_clientRandom.Next()) * kWordTo0_6 + 0.7f;
    }
    sizeFactor = big;

    onSphere = RandomInBall();
    float shell = radius * 0.65f;
    float scale = shell / std::sqrt(LengthSquared(onSphere));
    onSphere *= scale;
    p->color = 0xFFFFFFFF;
    p->frame = static_cast<int>(g_clientRandom.Next() % 11);
    p->life = lifetime;
    p->frameTimer = 1;

    // The carrier's motion flattens the sphere: the part of the direction against the motion
    // (clamped to [0, 1] of it) is taken off.
    Vector3 offset;
    float speed = elements[0].sphere.speed;
    if (speed > kStillSpeed) {
        const Vector3& m = elements[1].pos;
        float t = -(Dot(m, onSphere) / speed);
        t = MinKeepA(t, 1.0f);
        t = MaxKeepB(t, 0.0f);
        offset = onSphere - m * t;
    } else {
        offset = onSphere;
    }
    p->pos = origin + elements[0].pos + offset;
    if (ParticleTypeOf(type) == ParticleType::SphereFire) {
        float s = kSphereSizeByLife[p->life] * sizeFactor * radius;
        p->size = s + s;
    } else if (ParticleTypeOf(type) == ParticleType::Antimagic) {
        p->size = kAntimagicSizeByLife[p->life] * sizeFactor * radius;
    } else if (ParticleTypeOf(type) == ParticleType::SphereElectricity) {
        p->size = kSphereSizeByLife[p->life] * sizeFactor * radius;
    } else {
        p->size = kSphereSizeByLife[p->life] * sizeFactor * radius * 1.6f;
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
    if (elements.empty()) {
        if (!attachedTo) {
            game::DebugLog("%s", NoCarrierRing());
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
        float ringRadius = elements[0].flames.radius;
        float twoPi = kPi + kPi;
        float angle = g_clientRandom.Range(0.0f, twoPi);
        p->color = 0xFFFFFFFF;
        p->frame = 1;
        int age = elements[0].ticks;
        if (age >= 5) age = 5;
        std::uint32_t spread = g_clientRandom.Next();
        p->frameTimer = 1;
        p->state = -1;
        p->life = static_cast<int>(spread % 4u) + age + 1;
        float inner = (ringRadius - height * 20.0f) * 0.5f + ringRadius;
        float reach = g_clientRandom.Range(inner, ringRadius);
        const ParticleElement& ring = elements[0];
        p->pos.x = lib::Sin(angle) * reach + (origin.x + ring.pos.x);
        p->pos.y = lib::Cos(angle) * reach + (elements[0].pos.y + origin.y);
        p->pos.z = elements[0].pos.z + origin.z;
    } else {
        const ParticleElement& ring = elements[g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(elements))];
        p->frame = 1;
        p->life = 7;
        p->frameTimer = 1;
        p->color = 0xFFFFFFFF;
        p->state = -1;
        PlaceRingSpark(this, p, ring, ring.flames.radius * 0.1f + 0.001f);
    }
    p->size = size * 0.1f;
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
        p->size *= 0.7692308f;   // 1/1.3
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
    float& radius = e->flames.radius;
    if (t >= 27) {
        radius = height + radius;
    } else if (t >= 9) {
        radius = radius - height;
    } else {
        float target = height * 20.0f;
        radius = (radius - target) * 0.5f + target;
    }
    if (t >= 9) TurnRing(e);
}

// Healing (0x2006): the first call places 6 * size + 2 rings (needs a carrier); then
// one flame per ring; then sparks around a random ring's centre.
int ParticleSource::EmitHealing(Particle* p, int) {
    if (elements.empty()) {
        if (!attachedTo) {
            game::DebugLog("%s", NoCarrierHealing());
            return 0;
        }
        emitCount = std::lround(size * 6.0f + 2.0f);
        elements.resize(emitCount);
        PlaceRings(this, 36);
        emitCount *= 3;
    }
    int result;
    if (EmitRingFlame(this, p, result)) return result;
    const ParticleElement& ring = elements[g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(elements))];
    p->color = 0xFFFFFFFF;
    p->frame = 8;
    p->life = 8;
    p->frameTimer = 1;
    p->state = -1;
    PlaceRingSpark(this, p, ring, ring.flames.radius * 0.15f);
    p->size = (size + 1.0f) * 0.07f;
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
    float& radius = e->flames.radius;
    if (t >= 18) radius = height + radius;
    else radius = radius - height;
    TurnRing(e);
}

// --- CastingIllusion, Domination, Enchantment, Healing (0x202B-0x202E): orbiting lights ---------
// One element per orbiter (lifetime of them, added one per emission).

// Until all orbiters exist each emission adds one (needs a carrier); then sparks start at a random point
// between an orbiter's previous and current position and fly off with a random velocity from the spread box.
int ParticleSource::EmitCastSwirl(Particle* p, int) {
    Vector3& ahead = p->velocity;   // where the spark is next
    float& rise = p->aux.z;         // added to ahead.z per tick
    float& riseGrowth = p->aux.y;   // the rise's factor per tick
    if (std::ssize(elements) < lifetime) {
        if (!attachedTo) {
            game::DebugLog("%s", NoCarrierCasting());
            return 0;
        }
        return AddOrbiter(this, p);
    }
    int j = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(lifetime));
    ParticleElement* e = &elements[j];
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p->color = 0xFFFFFFFF;
    p->frame = 1;
    p->life = 28;
    p->frameTimer = 1;
    p->state = j;
    rise = (WordToFloat(g_clientRandom.Next()) * kWordTo0_35 + 0.85f) * e->orbiter.scale * 0.03f;
    riseGrowth = 1.03f;
    ahead = RandomInBox(spreadMin, spreadMax);
    const Vector3& previous = e->orbiter.previous;
    p->pos = previous + ahead;
    float u = 1.0f - t;
    ahead += t * previous + u * e->pos;
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_8 + 0.6f) * size * 0.2f;
    return 1;
}

// A marker (state < 0) sits on its orbiter with a flickering size and, once the source stops emitting (or
// loses its carrier), fades by the table and dies. A spark fades over its last 7 ticks, cycles frames 12..15,
// shrinks, and moves by its velocity, whose z changes by a growing step.
int ParticleSource::UpdateCastSwirl(Particle* p) {
    Vector3& ahead = p->velocity;
    float& rise = p->aux.z;
    const float& riseGrowth = p->aux.y;
    int state = p->state;
    if (state < 0) {
        const ParticleElement& e = elements[-(state + 1)];
        p->pos = e.pos;
        p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * 0.4f;
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
    float shrunk = p->size * 0.97f;
    p->pos = ahead;
    p->size = shrunk;
    ahead.z += rise;
    rise = riseGrowth * rise;
    return 1;
}

// Orbiters turn about the source's vertical axis (by -turn) and keep their previous world position.
void ParticleSource::TickCastSwirl(ParticleElement* e) {
    float c = e->orbiter.turnCos;
    float s = e->orbiter.turnSin;
    Vector3& relative = e->orbiter.relative;
    float y = c * relative.y - relative.x * s;
    float x = relative.x * c + relative.y * s;
    e->orbiter.previous = e->pos;
    relative.x = x;
    relative.y = y;
    e->pos = origin + relative;
}

// --- Link (0x2014): a beam of sparks from the source along its extent --------------------------

// Link: a spark at the source with a random offset across the beam (from the spread
// box), a twist rate from its distance to the axis and a random speed along the beam (fast and
// large sparks near the axis now and then).
int ParticleSource::EmitLink(Particle* p, int) {
    Vector3& offset = p->velocity;  // across the beam (x, y) and along it (z)
    float& twistCos = p->aux.x;     // the offset's turn per tick
    float& twistSin = p->aux.y;
    float& covered = p->drift.x;    // the way along the beam
    float& speed = p->drift.y;
    float& sparkSize = p->drift.z;
    if (elements.empty()) SetUpLink(this);
    p->color = 0xFFFFFF;
    float a = g_clientRandom.Range(-1.0f, 1.0f);
    float b = g_clientRandom.Range(-1.0f, 1.0f);
    float c = g_clientRandom.Range(spreadMin.z, spreadMax.z);
    offset.x = a * spreadMin.x;
    offset.y = b * spreadMin.y;
    offset.z = c;
    float reach = std::sqrt(b * b + a * a);
    float twist = reach * 0.2f;
    twistCos = lib::Cos(twist);
    twistSin = lib::Sin(twist);
    Vector3 start = offset + origin;
    p->pos = start;
    float factor;
    if ((g_clientRandom.Next() & 3) == 0 && reach < 0.5f) {
        speed = g_clientRandom.Range(0.1f, 0.12f);
        factor = g_clientRandom.Range(0.13f, 0.16f);
    } else {
        speed = g_clientRandom.Range(0.17f, 0.25f);
        factor = g_clientRandom.Range(0.03f, 0.05f);
    }
    sparkSize = factor * size;
    covered = 0.0f;
    p->size = sparkSize;
    p->frame = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(frameCount + 1));
    return 1;
}

// Link sparks run along the beam (drift.x the distance travelled, drift.y the speed), spinning around the axis;
// the beam bulges in the middle and the alpha peaks there (capped by element 0). Dropped past the beam's end.
int ParticleSource::UpdateLink(Particle* p) {
    Vector3& offset = p->velocity;
    const float& twistCos = p->aux.x;
    const float& twistSin = p->aux.y;
    float& covered = p->drift.x;
    const float& speed = p->drift.y;
    const float& sparkSize = p->drift.z;
    p->frame = (p->frame + 1) % frameCount;
    float travelled = speed + covered;
    covered = travelled;
    ParticleElement* e = elements.data();
    float along = travelled * e->link.inverseLength;
    if (travelled > e->link.length) return 0;

    float c = twistCos;
    float s = twistSin;
    float turnedY = offset.y * c + offset.x * s;
    float turnedX = c * offset.x - offset.y * s;
    offset.x = turnedX;
    offset.y = turnedY;
    const Vector3& direction = elements[1].linkAxis.axis;
    const Vector3& sideAxis = elements[2].linkAxis.axis;
    const Vector3& cross = elements[3].linkAxis.axis;
    Vector3 p3 = turnedX * sideAxis + origin + turnedY * cross + offset.z * direction + along * extent;
    p->pos = p3;

    float h = along - 0.5f;
    p->size = sparkSize;
    p->pos.z = (0.25f - h * h) * elements[0].link.length * 0.9f + p3.z;
    // Negative away from the middle: as an unsigned word above any cap.
    std::uint32_t fade = static_cast<std::uint32_t>(static_cast<int>((0.5f - std::fabs(h)) * 1000.0f));
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

// FireBlast: a random kind per particle (state = a draw % 29 - 14). The first particle
// after the fragments' first tick is the core flash. Negative kinds are flames inside the blast
// (a random point in the ball scaled by the radius, moving outwards; the earlier in the blast and
// the further out the more frames they skip); the others are smoke along fragment `state`'s path.
int ParticleSource::EmitBlast(Particle* p, int) {
    float& frameStep = p->aux.z;    // a flame's: added to its frame position per tick
    ParticleElement* fragments = elements.data();
    int kind = static_cast<int>(g_clientRandom.Next() % 29) - 14;
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
        Vector3 v = RandomInBall();
        float outside = 1.0f - LengthSquared(v);
        v *= size;
        p->pos = v * elements[0].fragment.radius + origin;
        p->velocity = v * elements[0].fragment.growth;
        p->size = 0.0f;
        int age = fragments->fragment.age;
        float threshold = (age / 7) + 0.2f;
        if (outside < threshold) {
            p->frame = 8;
            BlastFlameTiming(this, p);
            frameStep = g_clientRandom.Range(0.35f, 0.7f);
        } else {
            p->frame = static_cast<int>(g_clientRandom.Next() % 3);
            BlastFlameTiming(this, p);
            float t = g_clientRandom.Range(0.0f, 0.35f) * 0.5f;
            frameStep = t + outside * 0.35f / threshold + 0.175f;
        }
        return 1;
    }

    p->frame = static_cast<int>(g_clientRandom.Next() % 3);
    ParticleElement* e = &fragments[kind];
    if (e->fragment.frame == 0.0f) p->frame = 8;
    float u = g_clientRandom.Range(0.0f, 1.0f);
    p->pos = e->pos + origin - u * e->fragment.velocity * size;
    float g = g_clientRandom.Range(0.7f, 1.5f);
    float sized = g * (0.3f - u * 0.15f) *
                   (e->fragment.radius * 0.14285715f + 0.3f);   // 1/7
    p->size = sized * e->fragment.sizeFactor * size;
    p->velocity = e->fragment.velocity * size;
    return 1;
}

// FireBlast particles. Smoke (state >= 0) drifts with a 0.7 drag, shrinks to 0.8 and lasts until its frame
// reaches 7 (mod 8). The core flash (frame 15) grows once to 2.3 times the source's size, then disappears; it
// keeps the source alive while the fragments are younger than 3 ticks. Flames move by their velocity (slowing
// with the fragments' drag), swell, step through frames at an accelerating rate and fade by the frame.
int ParticleSource::UpdateBlast(Particle* p) {
    float& flameSize = p->aux.x;
    float& framePosition = p->aux.y;
    float& frameStep = p->aux.z;
    ParticleElement* fragments = elements.data();
    int state = p->state;
    if (state >= 0) {
        int frame = ++p->frame;
        if ((frame & 7) == 7) return 0;
        p->color = 0xFFFFFFFF;
        p->pos += p->velocity;
        p->velocity *= 0.7f;
        p->size *= 0.8f;
        return 1;
    }
    int frame = p->frame;
    if (frame >= 14) {
        if (state != kBlastCore) return 0;
        if (p->size == 0.0f) p->size = size * 2.3f;
        else p->size = 0.0f;
        return fragments->fragment.age < 3;
    }
    p->pos += p->velocity;
    ++p->life;
    p->size = flameSize;
    p->size = flameSize * (frame > 7 ? 1.03f : 1.01f);
    framePosition += frameStep;
    frameStep *= 1.02f;
    p->frame = std::lround(framePosition);
    flameSize = p->size;
    float drag = elements[0].fragment.decay - 0.03f;
    p->velocity *= drag;
    // A flame's frame can step past the table, up to 16: alpha 0 (the original read its block's zero tail, then
    // kAcidSpread's 0.0f).
    p->color = (static_cast<std::uint32_t>(p->frame < std::ssize(kBlastAlpha) ? kBlastAlpha[p->frame] : 0) << 24) | 0xFFFFFF;
    return 1;
}

// FireBlast fragments: the flame radius grows (its growth decaying), the age counts up
// (the emission stops after 6 ticks), the fragment flies by its velocity scaled by the source's
// size, slowing to 3/4 per tick and falling 0.03 per tick.
void ParticleSource::TickBlast(ParticleElement* e) {
    float& radius = e->fragment.radius;
    float& growthRate = e->fragment.growth;
    radius = radius + growthRate;
    int age = ++e->fragment.age;
    growthRate = e->fragment.decay * growthRate;
    if (age > 6) flags &= ~ParticleSourceFlag::Emitting;
    Vector3& velocity = e->fragment.velocity;
    e->pos += size * velocity;
    velocity *= 0.75f;
    velocity.z -= 0.03f;
}

// --- PoisonFog (0x2007, 0x2008): a disc of cloud puffs laid on the ground ------------------------

// PoisonFog: emits nothing itself. When the source has no particles it lays one puff
// per grid cell (jittered by a quarter cell) inside the disc of radius `size` around its position,
// on the ground, straight into its list (from the pool), and grows the bounds. Always 0.
int ParticleSource::EmitPoisonFog(Particle*, int) {
    if (!particles.empty()) return 0;
    float radius = size;
    int n = static_cast<int>((size + 1.0f) / kFogCell);
    for (int i = -n; i <= n; ++i) {
        float row = static_cast<float>(i);
        float radiusSquared = radius * radius;
        for (int j = -n; j <= n; ++j) {
            float x = j * kFogCell;
            float y = row * kFogCell;
            float high = kFogCell * 0.25f;
            float low = kFogCell * -0.25f;
            x = g_clientRandom.Range(low, high) + x;
            y = g_clientRandom.Range(low, high) + y;
            if (LengthSquared2D(y, x) >= radiusSquared) continue;
            Particle* q = Manager()->AcquireParticle();
            if (!q) continue;
            x += localPosition.x;
            y += localPosition.y;
            q->pos.x = x;
            q->pos.y = y;
            q->size = g_clientRandom.Range(1.0f, 1.5f) - 0.25f;
            q->pos.z = g_clientWorld.RawSurfaceHeightAt(x, y) + q->size;
            if (ParticleTypeOf(type) == ParticleType::PoisonCloud) {
                q->color = 0xFFFFAF7F;
            } else {
                int a = std::lround(alpha * 160.0f);
                if (a > 255) a = 255;
                q->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
            }
            q->life = lifetime;
            q->frame = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(frameCount));
            q->frameTimer = frameDelay;
            AddParticle(q);
        }
    }
    return 0;
}

// --- Geyser (0x2009): three vents that erupt in turn -----------------------------------------------

// Geyser: the first particle marks the source. Then a drop from a random vent: while
// the vent erupts it flies along the vent's direction (jittered by up to 20 / 10 degrees) with the
// vent's strength; otherwise a short-lived puff of mist near the source.
int ParticleSource::EmitGeyser(Particle* p, int) {
    float& dropSize = p->velocity.x;
    int vent = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(elements)));
    p->color = 0xFFFFFFFF;
    p->pos = origin;
    if (particles.empty()) {
        p->size = 0.0f;
        p->state = 1;
        p->frame = 15;
        return 1;
    }
    p->state = 0;
    p->frame = static_cast<int>(g_clientRandom.Next() % 12);
    const ParticleElement& e = elements[vent];
    float drop = size * 0.2f;
    p->life = 20;
    p->size = drop;
    if (e.ticks > 0) {
        p->life = 0x7FFFFFFF;
        float radian = ei::kRadiansPerDegree;
        float yaw = g_clientRandom.Range(-20.0f, 20.0f) * radian + e.vent.yaw;
        float tilt = g_clientRandom.Range(-10.0f, 10.0f) * radian + e.vent.tilt;
        float power = g_clientRandom.Range(0.0f, 0.2f) + e.vent.power - 0.1f;
        float sinTilt = lib::Sin(tilt);
        p->drift.x = lib::Sin(yaw) * sinTilt * power * height;
        p->drift.y = lib::Cos(yaw) * sinTilt * power * height;
        p->drift.z = lib::Cos(tilt) * power * height;
        p->pos = SpreadAround(p->pos, spreadMin, spreadMax);
        p->size = 0.0f;
        dropSize = drop;
        return 1;
    }
    p->life = 1;
    p->drift = {};
    float high = spreadMax.x * 0.5f;
    float low = spreadMin.x * 0.5f;
    p->pos.x = SpreadAround(p->pos.x, low, high);
    low = spreadMin.y * 0.5f;
    high = spreadMax.y * 0.5f;
    p->pos.y = SpreadAround(p->pos.y, low, high);
    p->size = size * 0.01f;
    return 1;
}

// Geyser: the marker flickers while the source emits (dropped once it stops); drops fly by drift
// (gravity pulling drift.z down by 0.06 height per tick), fade 8 alpha per tick (dropped below 10),
// shrink by 0.9 and cycle 4 frames.
int ParticleSource::UpdateGeyser(Particle* p) {
    float& dropSize = p->velocity.x;
    if (p->state != 0) {
        if (!(flags & ParticleSourceFlag::Emitting)) return 0;
        p->size = g_clientRandom.Range(0.03f, 0.3f) * size;
        return 1;
    }
    std::uint32_t a = p->color >> 24;
    if (a <= 9) return 0;
    if (--p->life <= 0) return 0;
    float shrunk = dropSize * 0.9f;
    dropSize = shrunk;
    p->pos += p->drift;
    int frame = p->frame;
    p->frame = (frame & ~3) | ((frame + 1) & 3);
    p->size = shrunk;
    p->color = ((a << 24) - 0x08000001u) | 0xFFFFFF;
    p->drift.z -= height * 0.06f;
    return 1;
}

// Geyser vents: an eruption counts down, then a pause of 3..36 ticks, then a new
// eruption of 1..7 ticks in a random direction (up to 10 degrees off vertical) and strength.
void ParticleSource::TickGeyser(ParticleElement* e) {
    if (e->ticks > 0) {
        if (--e->ticks == 0) e->vent.pause = static_cast<int>(g_clientRandom.Next() % 34) + 3;
        return;
    }
    if (--e->vent.pause > 0) return;
    e->ticks = static_cast<int>(g_clientRandom.Next() % 7) + 1;
    float radian = ei::kRadiansPerDegree;
    float yaw = g_clientRandom.Range(0.0f, 360.0f) * radian;
    float tilt = g_clientRandom.Range(0.0f, 10.0f) * radian;
    float power = g_clientRandom.Range(0.4f, 1.0f);
    e->vent.yaw = yaw;
    e->vent.tilt = tilt;
    e->vent.power = power;
}

// --- Shower (0x200F, 0x2031-0x2033): sparks thrown out in a few random directions ---------------

// Shower: the first call chooses the directions. A spark leaves the source along a
// random direction (jittered by +-0.03, scaled to the source's size and 1/6 of it per tick, one in
// eight half as fast again), already a random part of its first step out.
int ParticleSource::EmitShower(Particle* p, int) {
    float& speedKept = p->velocity.x;   // the speed's factor per tick
    float& sizeKept = p->velocity.y;    // the size's factor per tick
    if (elements[0].ray.age < 0) ChooseShowerRays(this);
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frame = elements[0].ticks;
    Vector3 jitter = {g_clientRandom.Range(-0.03f, 0.03f), g_clientRandom.Range(-0.03f, 0.03f),
                      g_clientRandom.Range(-0.03f, 0.03f)};
    const ParticleElement& ray = elements[g_clientRandom.Next() % static_cast<std::uint32_t>(rayCount)];
    p->pos = origin;
    p->size = typeParam;
    Vector3 d = size * (jitter + ray.pos);
    float speed = 1.0f / std::sqrt(LengthSquared(d)) * size * 0.16666667f;   // 1/6
    if ((g_clientRandom.Next() & 7) == 0) speed = speed * 1.5f;
    p->drift = speed * d;
    speedKept = 0.9f;
    sizeKept = 0.8f;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p->pos += t * p->drift;
    return 1;
}

// Shower sparks move, slow down, fall by 0.05 size per tick, shrink and fade by the life table.
int ParticleSource::UpdateShower(Particle* p) {
    const float& speedKept = p->velocity.x;
    const float& sizeKept = p->velocity.y;
    int life = --p->life;
    if (life == 0) return 0;
    p->pos += p->drift;
    p->color = (static_cast<std::uint32_t>(ByLife(kShowerAlpha, life, 0)) << 24) | 0xFFFFFF;   // before: kAcidSize's zero tail
    p->size = sizeKept * p->size;
    float keep = speedKept;
    p->drift *= keep;
    p->drift.z -= size * 0.05f;
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
            float t = 1.0f;
            for (int k = 0; k < 12; ++k) {
                g_bagSizes[k] = t;
                t *= 0.93f;
            }
        }
        std::uint32_t r = g_clientRandom.Next();
        p->state = lib::Count(particles);
        p->color = 0xFFFFFF;
        p->life = 15;
        p->frame = static_cast<int>(r & 3);
        p->pos = origin;
        p->size = 0.0f;
        return 1;
    }
    const ParticleElement& ball = elements[g_clientRandom.Next() % 80];
    int age = ball.ball.age;
    p->color = BagRgb(age);
    int life = ball.ticks + 1;
    if (life > 5) life = 5;
    p->life = life;
    p->frame = 4;
    p->state = -1;
    float u = g_clientRandom.Range(-1.0f, 0.0f);
    p->pos = u * ball.ball.step + ball.pos;
    p->size = g_bagSizes[age] * size * 0.1f;
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
    int age = ball->ball.age;
    p->size = g_bagSizes[age] * size * 0.15f;
    int fade = p->life > 9 ? 9 : p->life;
    p->color = (static_cast<std::uint32_t>(BagAlpha()[fade]) << 24) | BagRgb(age);
    const Vector3& step = ball->ball.step;
    auto isZero = [](float f) {
        return f == 0.0f;
    };
    if (isZero(step.x) && isZero(step.y) && isZero(step.z)) {
        std::memcpy(&p->prevPos, &p->pos, sizeof(Vector3) + sizeof(float));
        p->prevColor = p->color;
        p->size = size * 0.15f;
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
        e->ball.age = static_cast<int>(g_clientRandom.Next() % 3);
        e->pos = origin;
        float low, high;
        float r = BagSpread(this, low, high);
        e->pos.x = r + e->pos.x + low;
        r = BagSpread(this, low, high);
        e->pos.y = r + low + e->pos.y;
        r = BagSpread(this, low, high);
        e->pos.z = r + e->pos.z + low;
        e->ball.velocity = RandomInBall() * size * 0.2f;
        e->ticks = static_cast<int>(g_clientRandom.Next() % 3) + 7;
        e->ball.step = {};
        return;
    }
    ++e->ball.age;
    if (g_clientRandom.Next() % 6 == 0) {
        e->ball.velocity += RandomInBall() * size * 0.05f;
    }
    const Vector3& velocity = e->ball.velocity;
    e->pos += velocity;
    e->ball.step = velocity;
}

// --- 0x202F: sparks rising in a ring around the carrier ------------------------------------------

// 0x202F: a spark on a ring of random radius around the carrier (0.9-1.1 of the ring),
// at a random height of the spread box above the ring's foot, rising faster and faster.
int ParticleSource::EmitColumn(Particle* p, int) {
    Vector3& offset = p->velocity;  // from the source
    float& rise = p->aux.z;         // added to offset.z per tick
    if (elements.empty() && !SetUpColumn(this)) return 0;
    p->color = 0xFFFFFFFF;
    p->frame = 1;
    p->life = 13;
    p->frameTimer = 1;
    rise = (WordToFloat(g_clientRandom.Next()) * kWordTo0_6 + 0.7f) * elements[0].column.height * 0.1f;
    float ring = WordToFloat(g_clientRandom.Next()) * kWordTo0_2 + 0.9f;
    float angle = g_clientRandom.Range(0.0f, 6.28318f);
    offset.x = lib::Sin(angle) * ring;
    offset.y = lib::Cos(angle) * ring;
    float z = g_clientRandom.Range(spreadMin.z, spreadMax.z);
    offset.z = z;
    offset.z = z + elements[0].column.foot;
    p->pos = origin + offset;
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_8 + 0.6f) * size * 0.25f;
    return 1;
}

// 0x202F sparks: fade by life, step a frame, follow the source at their offset, shrink
// to 0.97 per tick and rise (the rise growing by 5% per tick).
int ParticleSource::UpdateColumn(Particle* p) {
    Vector3& offset = p->velocity;
    float& rise = p->aux.z;
    int life = --p->life;
    if (life == 0) return 0;
    float oldSize = p->size;
    p->color = (ColumnAlpha(life) << 24) | 0xFFFFFF;
    ++p->frame;
    p->pos = origin + offset;
    p->size = oldSize * 0.97f;
    offset.z += rise;
    rise *= 1.05f;
    return 1;
}

void ParticleSource::TickColumn(ParticleElement* e) { ++e->ticks; }

// --- Mushroom and Transform (0x200E, 0x2052): the cloud of the mushroom ---------------------------

// Mushroom: a puff from the source in a random upward direction (a fixed spread of 0,
// as ln 1 = 0), sized and framed by the cloud's age.
int ParticleSource::EmitMushroom(Particle* p, int) {
    if (elements.empty()) {
        elements.resize(1);
        ParticleElement* e = elements.data();
        e->ticks = 0;
        e->mushroom.spread = lib::Log(1.0f) * 0.2f * size;
        e->mushroom.capHeight = 0.0f;
        e->mushroom.stem = origin;
    }
    float spread = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * elements[0].mushroom.spread;
    Vector3 d = {g_clientRandom.Range(-1.0f, 1.0f), g_clientRandom.Range(-1.0f, 1.0f), g_clientRandom.Range(0.1f, 1.0f)};
    d = 1.0f / std::sqrt(LengthSquared(d)) * d;
    d.z += 0.12f;
    p->pos = d * spread + origin;
    p->pos.z += elements[0].mushroom.capHeight;
    p->size = elements[0].ticks * 0.008f + 0.1f;
    p->drift = d * size * 0.06f;
    p->size = size * p->size;
    p->life = static_cast<int>(g_clientRandom.Next() & 3) + 16;
    int bump = static_cast<int>(g_clientRandom.Next() % 3);
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
        if (p->frame < 8) p->size = p->size * 1.1f;
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
    p->pos += p->drift;
    p->drift *= 0.7f;
    const Vector3& stem = elements[0].mushroom.stem;
    float dx = p->pos.x - stem.x;
    float dy = p->pos.y - stem.y;
    float reach = std::sqrt(LengthSquared2D(dy, dx));
    float dz = p->pos.z - stem.z;
    if (elements[0].mushroom.capHeight > dz) {
        p->drift.x -= dx * size * 0.02f / reach;
        p->drift.y -= dy * size * 0.02f / reach;
    }
    return 1;
}

// --- BlueGas (Stench, 0x2013): gas swirling up around the carrier -------------------------------

// BlueGas: the first call measures the carrier (5 * size + 0.5 puffs per tick). A puff starts at a random
// point of the disc around the carrier's figure, turning about the axis faster the nearer it is, and rising.
int ParticleSource::EmitStench(Particle* p, int) {
    Vector3& around = p->velocity;  // the offset from the carrier's axis
    float& spinSin = p->drift.x;    // the offset's turn per tick
    float& spinCos = p->drift.y;
    float& rise = p->drift.z;       // added to around.z per tick
    if (elements.empty()) {
        if (!attachedTo) {
            game::DebugLog("%s", NoCarrierGas());
            return 0;
        }
        auto [offset, radius] = AttachmentOffset();
        elements.resize(1);
        elements[0].pos = offset;
        elements[0].gas.radius = radius * 0.3f;
        elements[0].gas.height = radius + radius;
        int rate = static_cast<int>(size * 5.0f + 0.5f);
        emitCount = rate < 1 ? 1 : rate;
    }
    float radius = elements[0].gas.radius;
    float carrierHeight = elements[0].gas.height;
    float t = g_clientRandom.Range(0.0f, radius);
    float twoPi = kPi + kPi;
    float angle = g_clientRandom.Range(0.0f, twoPi);
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
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
    p->pos = origin + elements[0].pos + around;
    p->size = radius * 0.7f;
    return 1;
}

// BlueGas puffs: step through the frames (dropped after the last), turn their offset
// about the axis, rise faster and faster, swell, and fade over the last 8 ticks.
int ParticleSource::UpdateStench(Particle* p) {
    Vector3& around = p->velocity;
    const float& spinSin = p->drift.x;
    const float& spinCos = p->drift.y;
    float& rise = p->drift.z;
    int life = --p->life;
    if (life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    p->pos -= around;
    float turnedY = Dot2D(around.x, around.y, spinSin, spinCos);
    float turnedX = spinCos * around.x - around.y * spinSin;
    around.x = turnedX;
    around.y = turnedY;
    around.z = rise + around.z;
    rise *= 1.02f;
    p->pos += around;
    p->size *= 1.01f;
    if (life < 8) p->color = ((p->color & 0xFF000000u) - 0x1F000001u) | 0xFFFFFF;
    return 1;
}

// --- Tornado (0x200A) and CurseHolder (0x2050): a funnel of rings ---------------------------------
// The rings run bottom to top.

// Tornado: a particle at a random height of the funnel (around the ring centres there),
// with a radius growing towards the top and a turn rate larger near the bottom; one in three is
// dust (state 0, frames 12-15), the others debris sized by their radius.
int ParticleSource::EmitTornado(Particle* p, int) {
    float& orbitAngle = p->velocity.x;      // around the funnel's centre
    float& orbitRadius = p->velocity.y;
    float& level = p->aux.x;                // the height up the funnel (0..1)
    float& opacity = p->aux.y;
    float& spin = p->drift.x;               // added to orbitAngle per tick
    float& radiusFactor = p->drift.y;       // orbitRadius's factor per tick
    float& climb = p->drift.z;              // added to level per tick
    float height01 = g_clientRandom.Range(0.0f, 1.0f);
    p->state = static_cast<int>(g_clientRandom.Next() % 3);
    p->color = tint;
    std::memcpy(&p->pos, &origin, 2 * sizeof(float));
    p->pos.z = origin.z + elements[0].pos.z;
    p->life = static_cast<int>(g_clientRandom.Next() % 9) + 100;
    float scaled = height01 * kFunnelSpans;
    int k = std::lround(scaled);
    if (k >= lib::Count(elements) - 2) k = lib::Count(elements) - 2;
    float f = scaled - k;
    float t = MinKeepA(f, 1.0f);
    float u = 1.0f - t;
    const ParticleElement& a = elements[k];
    const ParticleElement& b = elements[k + 1];
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
    radiusFactor = elements[0].funnel.damping;
    p->pos.x = lib::Sin(angle) * radius + p->pos.x + cx;
    p->pos.y = lib::Cos(angle) * radius + cy + p->pos.y;
    p->pos.z = height01 * elements[0].funnel.height + p->pos.z;
    if (p->state != 0) {
        p->size = (radius + 0.3f) * size;
        p->frame = static_cast<int>(g_clientRandom.Next() % 12);
    } else {
        p->frame = static_cast<int>(g_clientRandom.Next() & 3) + 12;
        p->size = size * 0.15f;
    }
    return 1;
}

// Tornado particles rise (dropped above the top), turn around the funnel's centre at
// their height (faster and faster), fade in, fade by the height, and animate.
int ParticleSource::UpdateTornado(Particle* p) {
    float& orbitAngle = p->velocity.x;
    float& orbitRadius = p->velocity.y;
    float& level = p->aux.x;
    float& opacity = p->aux.y;
    float& spin = p->drift.x;
    const float& radiusFactor = p->drift.y;
    float& climb = p->drift.z;
    if (--p->life == 0) return 0;
    level += climb;
    climb += 0.0001f;
    if (level > 1.0f) return 0;
    float scaled = level * kFunnelSpans;
    const Vector2 centre = FunnelCentre(this, scaled);
    orbitAngle += spin;
    spin *= 1.01f;
    float angle = orbitAngle;
    orbitRadius = radiusFactor * orbitRadius;
    float radius = orbitRadius;
    p->pos.x = lib::Sin(angle) * radius + origin.x + centre.x;
    p->pos.y = lib::Cos(angle) * radius + origin.y + centre.y;
    float base = elements[0].pos.z + origin.z;
    p->pos.z = base;
    p->pos.z = elements[0].funnel.height * level + base;
    if (p->life > 8) opacity = MinKeepA(opacity + 0.1f, 1.0f);
    else opacity = MaxKeepB(0.1f, opacity);
    int a = std::lround(std::sqrt(1.0f - level) * opacity * 255.0f);
    p->color = (p->color & 0xFFFFFF) | (static_cast<std::uint32_t>(a) << 24);
    if (p->state != 0) {
        p->frame = (p->frame + 1) % 12;
        p->size = (radius + 0.3f) * size;
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
    float bend = bottom->funnel.bend;
    if (bend == 0.0f) {
        Vector3 sway = bottom->pos - motion;
        sway.x *= 0.95f;
        sway.z = 0.0f;
        sway.y *= 0.95f;
        for (int i = 0; i < std::ssize(elements); ++i) {
            float c = lib::Cos(static_cast<float>(i) / static_cast<float>(std::ssize(elements) - 1) * kPi * 0.5f);
            elements[i].pos = sway * c;
        }
        elements[0].pos.z = g_clientWorld.RawSurfaceHeightAt(origin.x, origin.y) - origin.z + 1.0f;
    } else {
        elements[0].pos.z = g_clientWorld.RawSurfaceHeightAt(origin.x, origin.y) - origin.z + 1.0f;
        Vector3 n = (motion * 20.0f + elements[0].pos) * -1.0f;
        float length = std::sqrt(LengthSquared(n));
        if (length != 0.0f) n *= 1.0f / length;
        float lean = length * 0.5f;
        if (lean > 0.01f) lean = 0.01f;
        Vector3 g = n * lean + elements[0].pos;
        for (int i = 0; i < std::ssize(elements); ++i) {
            float f = static_cast<float>(i) / static_cast<float>(std::ssize(elements) - 1);
            float c = lib::Cos(f * kPi);
            float k = (c + (1.0f - f) * elements[0].funnel.bend + 1.0f) * 0.4f;
            elements[i].pos = g * k;
        }
    }
    if (!attachedTo) {
        float b = elements[0].funnel.bend;
        if (b == 0.0f && !particles.empty()) {
            Stop();
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
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size;
    return 1;
}

// --- Cylinder1 / Cylinder2 (0x203F, 0x2040): two stars circling the carrier --------------------
// A star's ticks count down and index kCylinderAlpha.

// Cylinder: the first particles are the stars (state = their index; the first one
// places them around the carrier); later ones are sparks along a random star's last step.
int ParticleSource::EmitCylinder(Particle* p, int) {
    Vector3& relative = p->velocity;    // a spark's position relative to the source
    Vector3& spiral = p->aux;           // a spark's step
    p->color = 0xFFFFFF;
    int count = lib::Count(particles);
    int stars = lib::Count(elements);
    if (count < stars) {
        if (count == 0) SetUpCylinder(this);
        p->frame = 0;
        p->state = count;
        const ParticleElement& e = elements[count];
        p->pos = e.pos + origin;
        p->size = 0.0f;
        return 1;
    }
    int j = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(stars));
    const ParticleElement& e = elements[j];
    int life = e.ticks + 6;
    if (life > 14) life = 14;
    p->life = life;
    if (life < 0) return 0;
    const Vector3& step = e.star.step;
    auto isZero = [](float f) {
        return f == 0.0f;
    };
    if (isZero(step.x) && isZero(step.y)) return 0;
    p->frame = 1;
    p->state = -1;
    float u = g_clientRandom.Range(-1.0f, 0.0f);
    relative = u * step + e.pos;
    spiral = step;
    p->size = 0.1f;     // 0.1
    p->pos = relative + origin;
    return 1;
}

// Cylinder: a star follows its element and fades by its countdown (dropped once the source stops); a spark
// fades by its life, drifts by its step, is held on its star's orbit radius and spirals round it, shrinking.
int ParticleSource::UpdateCylinder(Particle* p) {
    Vector3& relative = p->velocity;
    Vector3& spiral = p->aux;
    int state = p->state;
    if (state >= 0) {
        if (!(flags & ParticleSourceFlag::Emitting)) return 0;
        const ParticleElement& e = elements[state];
        p->color = (static_cast<std::uint32_t>(kCylinderAlpha[e.ticks]) << 24) | 0xFFFFFF;
        p->size = 0.3f;     // 0.3
        p->pos = origin + e.pos;
        return 1;
    }
    ++p->frame;
    int life = --p->life;
    if (life <= 0) return 0;
    p->color = (static_cast<std::uint32_t>(kCylinderAlpha[life]) << 24) | 0xFFFFFF;
    relative += spiral;
    float k = elements[0].star.orbit / std::sqrt(LengthSquared2D(relative.x, relative.y));
    float kx = k * relative.x;
    relative.x = kx;
    float ky = k * relative.y;
    relative.y = ky;
    float reach = std::sqrt(LengthSquared(spiral)) * elements[0].star.inverseOrbit;
    spiral = Vector3{ky * reach, -kx * reach, spiral.z} * 0.6f;
    p->pos = relative + origin;
    p->size *= 0.9f;
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
    float (&rotation)[2] = e->star.rotation;
    float nx = Dot2D(rotation[1], rotation[0], e->pos.y, e->pos.x);
    float ny = rotation[0] * e->pos.y - rotation[1] * e->pos.x;
    float& rise = e->star.rise;
    Vector3 next = {nx, ny, e->pos.z + rise};
    e->star.step = next - e->pos;
    rise *= 1.25f;
    e->pos = next;
    float twist = kPi * -0.013333334f;
    if (ParticleTypeOf(type) == ParticleType::Cylinder2) twist = -twist;
    float c = lib::Cos(twist);
    float s = lib::Sin(twist);
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
    float half = size * 0.5f;
    float t;
    if (index == 0) {
        p->size = size * 1.2f;
        p->state = 1;
        t = 0.0f;
    } else {
        p->size = half;
        p->state = 0;
        t = g_clientRandom.Range(-1.0f, 0.0f);
    }
    if (distance <= kTrailMoving) {
        p->velocity = {};
        return 1;
    }
    p->pos += t * motion;
    SideVelocity(this, p, spreadMin.x - half, half + spreadMax.x);
    AddUp(this, p, g_clientRandom.Range(spreadMin.y - half, half + spreadMax.y));
    if (p->state == 0) {
        p->pos += p->velocity;
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
        p->size *= 0.5f;
        p->state = 0;
    } else {
        p->pos -= p->velocity;
    }
    p->velocity *= 0.95f;
    p->pos += p->velocity;
    return 1;
}

// Arrow smoke: the first particle (once) is the arrow's head (state 2); the others are
// puffs along the last step (three in ten lingering, state 1) with a random sideways velocity.
int ParticleSource::EmitArrowSmoke(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    p->pos = origin;
    float t;
    if (index == 0 && emitState == 0) {
        p->size = size * 0.4f;
        emitState = 1;
        t = 0.0f;
        p->state = 2;
        p->frame = 0;
    } else {
        p->size = size * 0.2f;
        p->state = 0;
        t = g_clientRandom.Range(-1.0f, 0.0f);
        if (g_clientRandom.Next() % 100 > 70) p->state = 1;
        p->frame = static_cast<int>(g_clientRandom.Next() & 1) + 2;
    }
    if (distance <= kTrailMoving) {
        p->velocity = {};
        return 1;
    }
    p->pos += t * motion;
    SideVelocity(this, p, spreadMin.x, spreadMax.x);
    AddUp(this, p, g_clientRandom.Range(spreadMin.y, spreadMax.y));
    if (p->state == 0) {
        p->pos += p->velocity;
    }
    return 1;
}

// Arrow smoke: the head follows the source one step ahead with a random frame (dropped
// once the source stops); puffs step through the frames, swell (faster in the first 6 frames),
// and fly out by their velocity (growing by 1.1); lingering puffs keep their place.
int ParticleSource::UpdateArrowSmoke(Particle* p) {
    int state = p->state;
    if (state == 2) {
        p->pos = motion + origin;
        p->frame = static_cast<int>(g_clientRandom.Next() % 3);
        return static_cast<int>(flags & ParticleSourceFlag::Emitting);
    }
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    int frame = p->frame;
    if (state == 0) {
        if (frame < 6) p->size = p->size * 1.4f;
        else p->size = growth * p->size;
        p->pos -= p->velocity;
        p->velocity *= 1.1f;
        p->pos += p->velocity;
        return 1;
    }
    if (frame < 6) p->size = p->size * 1.4f;
    p->velocity *= 0.95f;
    return 1;
}

// --- Banshee (0x200D): a ring that widens around the source ---------------------------------------

// Banshee: the first call only sets the ring up (from the carrier's figure; 72 sparks per tick after it). A
// spark on the ring, at a random angle, between the ring and its eased inner edge.
int ParticleSource::EmitBanshee(Particle* p, int) {
    if (elements.empty()) {
        elements.resize(1);
        auto [offset, radius] = AttachmentOffset();
        float tall = radius * 3.0f;
        float widen = radius * 0.027777778f;   // 1/36
        height = widen + widen;
        elements[0].ticks = 8;
        elements[0].banshee.height = tall * 0.5f;
        elements[0].banshee.radius = 0.0f;
        emitCount = 72;
        return 0;
    }
    float ringRadius = elements[0].banshee.radius;
    float twoPi = kPi + kPi;
    float angle = g_clientRandom.Range(0.0f, twoPi);
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->frameTimer = 1;
    int age = elements[0].ticks;
    if (age >= 5) age = 5;
    p->life = static_cast<int>(g_clientRandom.Next() % 3u) + age + 1;
    float inner = (ringRadius - height * 30.0f) * 0.5f + ringRadius;
    float reach = inner + 0.001f >= ringRadius
                      ? inner
                      : g_clientRandom.Range(inner, ringRadius);   // also NaN
    p->pos.x = lib::Sin(angle) * reach + origin.x;
    p->pos.y = lib::Cos(angle) * reach + origin.y;
    p->pos.z = origin.z + elements[0].banshee.height;
    p->size = size * 0.15f;
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
    float target = height * 30.0f;
    float& radius = e->banshee.radius;
    radius = (radius - target) * 0.5f + target;
}

// --- Portal (0x203E) emission; its update and element tick are in particle_emitters.cpp ------------

// Portal: the first particle is the glow above the portal (state 1). The others start
// at a random point of the disc (denser near the edge), spin round it (four in eleven or so the
// other way, with a different frame range) and rise.
int ParticleSource::EmitPortal(Particle* p, int) {
    float& spinCos = p->velocity.x;     // the offset's turn per tick
    float& spinSin = p->velocity.y;
    float& riseRate = p->velocity.z;    // times the source's size: the rise per tick
    float& offsetX = p->aux.x;          // from the portal's centre
    float& offsetY = p->aux.y;
    float& sparkSize = p->aux.z;
    if (particles.empty()) {
        p->pos = origin;
        p->size = 0.0f;
        p->pos.z = 8.0f * 0.8f + p->pos.z;
        p->color = 0x60FFFFFF;
        p->state = 1;
        p->frame = 15;
        return 1;
    }
    p->state = 0;
    p->color = 0x40FFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float radius = 2.5f;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    float spread = (t + 1.0f) * t * 0.5f;
    float angle = g_clientRandom.Range(0.0f, 6.2832f);
    float reach = spread * radius;
    float x = lib::Sin(angle) * reach;
    float y = lib::Cos(angle) * reach;
    p->pos.x = x + origin.x;
    p->pos.y = y + origin.y;
    p->pos.z = origin.z;
    p->size = 0.0f;
    offsetY = y;
    sparkSize = (2.0f - spread) * 0.3f;
    offsetX = x;
    float spin = radius / 2.5f * 0.1f;
    if (g_clientRandom.Range(0.0f, 1.0f) > spread) {
        p->frame = static_cast<int>(g_clientRandom.Next() % 11) + 4;
    } else {
        p->frame = static_cast<int>(g_clientRandom.Next() & 3);
        spin = -spin;
    }
    spinCos = lib::Cos(spin);
    spinSin = lib::Sin(spin);
    riseRate = (1.0f - spread + 3.0f) * radius * 0.015f;
    p->pos.z = g_clientRandom.Range(-0.5f, 0.5f) * riseRate + p->pos.z;
    return 1;
}

// --- AcidRay (0x2012) ---------------------------------------------------------------------------

// AcidRay: a drop at a random point of the last step (when moving), flying sideways by
// the spread box along the side and up axes (or standing).
int ParticleSource::EmitAcidRay(Particle* p, int) {
    Vector3& direction = p->velocity;
    Vector3& launch = p->drift;     // the drop's start
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    p->pos = origin;
    p->state = 0;
    float t = g_clientRandom.Range(-1.0f, 0.0f);
    p->frame = static_cast<int>(g_clientRandom.Next() & 1);
    if (distance > kTrailMoving) {
        p->pos += t * motion;
        launch = p->pos;
        float q = g_clientRandom.Range(-1.0f, 1.0f);
        direction = q * side;
        float q2 = g_clientRandom.Range(-1.0f, 1.0f);
        direction += q2 * up;
    } else {
        direction = {};
        launch = origin;
    }
    Vector3 start;
    PlaceAcid(this, p, lifetime - p->life, start);
    p->pos = start;
    p->size = kAcidSize[lifetime - p->life] * size;
    return 1;
}

// AcidRay drops: fly out from their start by the velocity scaled by the source's size
// and the spread table (by age), sized by the size table, through the frames.
int ParticleSource::UpdateAcidRay(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    PlaceAcid(this, p, lifetime - life, p->pos);
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
    float speed = WordToFloat(g_clientRandom.Next()) * kWordTo0_6 + 0.8f;
    float lengthSquared;
    Vector3 v = RandomInBall(lengthSquared);
    Vector3 direction = v / std::sqrt(lengthSquared);
    float k = speed * size * speed * 0.2f;
    p->drift = direction * k;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p->pos = t * p->drift + origin;
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * 0.2f;
    p->life = 6;
    p->state = 0;
    p->frame = static_cast<int>(g_clientRandom.Next() % 3) + 1;
    return 1;
}

// --- ClayRing (0x2018): ten points running out from the source along the ground ---------------

// ClayRing: in the first tick (countdown 13) one marker per point; then clods between a
// random point's previous and current position, flying off at random.
int ParticleSource::EmitClayRing(Particle* p, int index) {
    Vector3& base = p->velocity;    // a clod's point on its path
    Vector3& scatter = p->aux;      // added to base, slowing
    p->color = (static_cast<std::uint32_t>(ByLife(kClayAlpha, elements[0].ticks, kClayBefore)) << 24) | 0xFFFFFF;
    p->life = lifetime;
    p->frame = 1;
    p->frameTimer = frameDelay;
    if (elements[0].ticks == 13 && index < std::ssize(elements)) {
        base = {};
        p->pos = elements[index].pos;
        p->size = 0.2f;     // 0.2
        p->frame = 0;
        p->state = index;
        return 1;
    }
    int j = static_cast<int>(g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(elements)));
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p->life = 5;
    const ParticleElement& e = elements[j];
    const Vector3& previous = e.clay.previous;
    float u = 1.0f - t;
    base = t * e.pos + u * previous;
    float sx = g_clientRandom.Range(-0.1f, 0.1f);
    float sy = g_clientRandom.Range(-0.1f, 0.1f);
    float sz = g_clientRandom.Range(-0.1f, 0.1f);
    scatter.x = sy;
    scatter.y = sx;
    scatter.z = sz;
    std::memcpy(&p->pos, &previous, sizeof(Vector3));
    p->size = 0.1f;     // 0.1
    return 1;
}

// ClayRing: markers stay on their point (alpha by the countdown); clods step through the
// frames, fly out (their scatter slowing by 0.9), fade by 48 per tick and shrink.
int ParticleSource::UpdateClayRing(Particle* p) {
    const Vector3& base = p->velocity;
    Vector3& scatter = p->aux;
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
    scatter *= 0.9f;
    float oldSize = p->size;
    p->pos = base + scatter;
    std::int32_t a = static_cast<std::int32_t>(p->color >> 24) - 0x30;
    if (a < 0) a = 0;
    p->size = oldSize * 0.9f;
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
    Vector3& previous = e->clay.previous;
    e->clay.previous = e->pos;
    Vector3& direction = e->clay.direction;
    float k = size >= 0.0f ? ClayReachBackwards(t) : kClayReach[t];
    e->pos = size * direction * k + origin;
    e->pos.z = g_clientWorld.RawSurfaceHeightAt(e->pos.x, e->pos.y) + 1.0f;
    float turnedX = direction.y * kClayTurnCos - direction.x * kClayTurnSin;
    float turnedY = direction.x * kClayTurnCos + direction.y * kClayTurnSin;
    direction.x = turnedY;
    direction.y = turnedX;
    auto isZero = [](float f) {
        return f == 0.0f;
    };
    if (isZero(previous.x) && isZero(previous.y)) e->clay.previous = e->pos;
}

// --- Falling (0x2034-0x2037): leaves or flakes drifting down from the carrier -------------------

// Falling: a leaf at the source blown by a random gust (a random reach up to the gust's, a random direction
// around it (the original's spread is ftol(10 degrees) = 0)), falling and spinning at random rates.
int ParticleSource::EmitFalling(Particle* p, int) {
    float& sizeFactor = p->velocity.x;  // per tick
    float& terminal = p->velocity.y;    // the lowest drift.z
    float& gravity = p->velocity.z;     // added to drift.z per tick
    p->color = 0xFFFFFFFF;
    p->frame = 12;
    p->pos = origin;
    p->size = g_clientRandom.Range(0.1f, 0.2f);
    const ParticleElement& gust = elements[g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(elements))];
    float reach = gust.wind.reach * 0.5f;
    int spread = static_cast<int>(ei::kRadiansPerDegree * 10.0f);
    float turn = spread * gust.wind.direction;
    float lowReach = -reach;
    float x = g_clientRandom.Range(0.0f, reach - lowReach) + gust.wind.reach + lowReach;
    float lowTurn = -turn;
    float angle = g_clientRandom.Range(0.0f, turn - lowTurn) + gust.wind.direction + lowTurn;
    sizeFactor = 1.0f;
    gravity = -0.12f;
    p->drift.x = lib::Sin(angle) * x;
    p->drift.y = lib::Cos(angle) * x;
    p->drift.z = 0.0f;
    gravity = g_clientRandom.Range(0.0f, 0.12f) + gravity - 0.06f;
    terminal = WordToFloat(g_clientRandom.Next()) * kWordTo0_4 - 0.6f;
    return 1;
}

// Falling gusts: a new gust gets a random reach and direction (truncated to whole
// radians); from the second tick on the source stops emitting and lets go of its carrier.
void ParticleSource::TickFalling(ParticleElement* e) {
    int age = e->wind.age;
    if (age == 0) {
        e->wind.reach = WordToFloat(g_clientRandom.Next()) * kWordTo0_2Gust + 0.1f;
        float whole = static_cast<float>(static_cast<int>(ei::kRadiansPerDegree * 360.0f));
        e->wind.direction = g_clientRandom.Range(0.0f, whole);
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
    float& sizeFactor = p->velocity.x;  // per tick
    float& slowing = p->velocity.y;     // the drift's factor per tick
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    float radian = ei::kRadiansPerDegree * 360.0f;
    if (g_clientRandom.Next() % 100 + 1 <= 60) {
        p->state = 8;
        p->life = lifetime * 8;
        p->frameTimer = 8;
        std::memcpy(&p->pos, &origin, 2 * sizeof(float));
        float low = spreadMin.y * -0.5f;
        float high = spreadMin.y * 0.5f;
        p->pos.z = g_clientRandom.Range(0.0f, high - low) + spreadMax.y + origin.z + low;
        p->size = size;
        sizeFactor = growth;
        slowing = 0.965f;
        float reach = spreadMin.x;
        p->drift.z = 0.0f;
        float angle = g_clientRandom.Range(0.0f, radian);
        p->drift.x = lib::Sin(angle) * reach;
        p->drift.y = lib::Cos(angle) * reach;
        return 1;
    }
    p->state = 4;
    p->life = lifetime * 4;
    p->frameTimer = 4;
    float reach = g_clientRandom.Range(0.0f, spreadMax.x);
    float angle = g_clientRandom.Range(0.0f, radian);
    p->pos.x = lib::Sin(angle) * reach + origin.x;
    p->pos.y = lib::Cos(angle) * reach + origin.y;
    p->pos.z = origin.z;
    p->size = size * 0.3f;
    sizeFactor = 1.02f;
    slowing = 1.0f;
    p->drift.x = 0.0f;
    p->drift.y = 0.0f;
    p->drift.z = height;
    return 1;
}

// VulcanSmoke: frames at the particle's own pace (state ticks per frame; dropped after the last), moving by drift,
// growing and slowing.
int ParticleSource::UpdateVulkan(Particle* p) {
    const float& sizeFactor = p->velocity.x;
    const float& slowing = p->velocity.y;
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = p->state;
    }
    p->pos += p->drift;
    p->size = sizeFactor * p->size;
    float keep = slowing;
    p->drift *= keep;
    return 1;
}

// --- FireStar / AcidStar (0x2042-ish), Regeneration, VisionStar and ZoneExit ----------------------

// FireStar: a single flash at the source (the source stops emitting at once) with the source's lifetime as its
// frame.
int ParticleSource::EmitFireStar(Particle* p, int) {
    float& flashSize = p->velocity.x;
    flags &= ~ParticleSourceFlag::Emitting;
    p->color = 0xFFFFFF;
    p->life = 3;
    p->frame = lifetime;
    p->pos = origin;
    p->size = 0.0f;
    flashSize = size;
    return 1;
}

// FireStar: shown for two ticks at the kept size (shrinking it by 0.9 each time), then dropped.
int ParticleSource::UpdateFireStar(Particle* p) {
    float& flashSize = p->velocity.x;
    if (--p->life <= 0) return 0;
    float shrunk = flashSize * 0.9f;
    p->color = 0xFFFFFFFF;
    p->size = flashSize;
    flashSize = shrunk;
    return 1;
}

// Regeneration: two particles at the source, the first on the carrier's left (-1), the second on its right (1).
int ParticleSource::EmitRegeneration(Particle* p, int) {
    float& sideSign = p->velocity.x;
    if (std::ssize(particles) > 1) return 0;
    p->color = 0xFFFFFF;
    p->frame = 3;
    p->pos = origin;
    p->size = 0.0f;
    sideSign = static_cast<float>(!particles.empty() ? 1 : -1);
    return 1;
}

// Regeneration: once the source no longer emits (or has no carrier) its size shrinks by
// a quarter per tick and the particles go below 0.02. Each particle eases (0.7 / 0.3) towards a
// point beside the carrier: its own x axis (from localRotation) scaled by the carrier's radius, the
// particle's side and 0.65; a little above, by 0.4 of the source's size. Its size flickers at random.
int ParticleSource::UpdateRegeneration(Particle* p) {
    const float& sideSign = p->velocity.x;
    if (!(flags & ParticleSourceFlag::Emitting) || !attachedTo) {
        float shrunk = size * 0.75f;
        size = shrunk;
        if (shrunk < 0.02f) return 0;
    }
    p->color = 0xFFFFFFFF;
    Vector3 target = origin;
    if (attachedTo) {
        const Object3D* carrier = attachedTo;
        float scale = carrier->bounds.radius * sideSign * 0.65f;
        target += carrier->localRotation.AxisX() * scale;
    }
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * 0.25f;
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
    Vector3& offset = p->velocity;  // a satellite's, from the source
    int count = lib::Count(particles);
    if (count > 4) return 0;
    if (count > 0 && lifetime != 1) return 0;
    p->color = 0xFFFFFF;
    p->frame = lifetime;
    p->pos = origin;
    p->size = 0.0f;
    if (lifetime != 1) return 1;
    if (count == 0) {
        offset.x = lib::Cos(0.0f);
        offset.y = lib::Sin(0.0f);
        offset.z = 0.0f;
    } else {
        float angle = kSpinAngle * SpinRate(count);
        offset.x = lib::Cos(angle);
        offset.y = lib::Sin(angle);
        offset.z = 0.0f;
    }
    float half = 0.5f;
    offset *= half;
    p->pos += offset;
    return 1;
}

// VisionStar: the particles follow the source; with lifetime 1 their offsets turn by
// 0.07 radians per tick (and they flicker less). VisionStar3 (0x204C) sits 0.7 higher. The size
// eases (0.95) towards a random flicker of the source's size; once the source stops emitting (or
// loses its carrier) the source's size is zeroed and the particles fade, dropped below 0.03.
int ParticleSource::UpdateVisionStar(Particle* p) {
    Vector3& offset = p->velocity;
    float oldSize = p->size;
    p->color = 0xFFFFFFFF;
    p->pos = origin;
    float flicker = 0.3f;     // 0.3
    if (lifetime == 1) {
        float c = lib::Cos(0.07f);
        flicker = 0.17f;     // 0.17
        float s = lib::Sin(0.07f);
        float turnedY = offset.y * c - offset.x * s;
        float turnedX = c * offset.x + offset.y * s;
        offset.y = turnedY;
        offset.x = turnedX;
        p->pos += offset;
    }
    if (ParticleTypeOf(type) == ParticleType::VisionStar4) p->pos.z = p->pos.z + 0.7f;
    float eased = g_clientRandom.Range(0.1f, 1.9f) * size * flicker * 0.05f + oldSize * 0.95f;
    p->size = eased;
    if ((flags & ParticleSourceFlag::Emitting) && attachedTo) return 1;
    p->size = eased * 0.95f;
    size = 0.0f;
    if (p->size < 0.03f) return 0;
    return 1;
}

// ZoneExit: alpha by the life left (kZoneExitAlpha), dropped past 0; rising 0.05 per tick, aux.x (0, never set)
// slowing by 0.9, cycling through four frames.
int ParticleSource::UpdateZoneExit(Particle* p) {
    const float& sparkSize = p->velocity.x;
    int life = p->life;
    p->color = (p->color & 0xFFFFFF) | (static_cast<std::uint32_t>(ByLife(kZoneExitAlpha, life, 255)) << 24);
    p->life = --life;
    if (life < 0) return 0;
    p->pos.z += 0.05f;
    p->aux.x *= 0.9f;
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->size = sparkSize;
    return 1;
}

// --- FeebleMind (0x2049) update ----------------------------------------------------------------

// FeebleMind: alpha by the life left (dropped past 0); the particle circles: its offset is taken off, turned by 0.07
// radians and grown by 1.1, then put back with a rise of 0.05. The size grows by 1.04 per tick, the frame cycles
// through 16.
int ParticleSource::UpdateFeebleMind(Particle* p) {
    Vector3& offset = p->velocity;
    float& growingSize = p->aux.z;
    int life = --p->life;
    if (life < 0) return 0;
    float nextSize = growingSize;
    p->color = (static_cast<std::uint32_t>(kFeebleAlpha[life]) << 24) | 0xFFFFFF;
    int frame = (p->frame + 1) & 15;
    p->pos -= offset;
    float c = lib::Cos(0.07f);
    float s = lib::Sin(0.07f);
    float turnedY = offset.y * c - offset.x * s;
    float turnedX = c * offset.x + offset.y * s;
    float grow = 1.1f;
    offset.x = turnedX * grow;
    offset.y = turnedY * grow;
    p->pos += offset;
    p->size = nextSize;
    p->frame = frame;
    p->pos.z += 0.05f;
    growingSize *= 1.04f;
    return 1;
}

// --- FeetCloud (0x2047): puffs at the carrier's feet; the emission is EmitFeetCloud --------------

// FeetCloud puffs: alpha by the life left (capped at 8 once the source stops emitting or loses its carrier;
// dropped past 0), the source's tint as the colour. The puff's offset grows by its spread (slowing by 0.9) and the
// puff eases (part of the way, which shrinks by 0.98) towards its foot (element `state`) plus the offset; its size
// shrinks by 0.8.
int ParticleSource::UpdateFeetCloud(Particle* p) {
    float& puffSize = p->velocity.x;
    float& ease = p->velocity.y;    // the part of the way to the target per tick
    Vector3& offset = p->aux;       // from the foot
    Vector3& spread = p->drift;     // added to the offset per tick
    int life = --p->life;
    if (life < 0) return 0;
    if (!(flags & ParticleSourceFlag::Emitting) || !attachedTo) {
        if (life > 8) life = 8;
        p->life = life;
    }
    p->color = (static_cast<std::uint32_t>(kFeetAlpha[life]) << 24) + tint;
    offset += spread;
    float pull = ease;
    Vector3 toward = (offset + elements[p->state].pos) * pull;
    p->pos = p->pos * (1.0f - pull) + toward;
    p->size = puffSize;
    puffSize *= 0.8f;
    ease *= 0.98f;
    float slow = 0.9f;
    spread *= slow;
    return 1;
}

// FeetCloud feet: on a unit carrier the element sits on its foot (left when ticks is set, else
// right), else on its body, else on its position raised by `above.z`; without one on the source.
void ParticleSource::TickFeetCloud(ParticleElement* e) {
    if (attachedTo && attachedTo->GetClassId() == ClassId::Unit) {
        Object3D* unit = attachedTo;
        FigureMesh* part = unit->FindPartByPrefix(e->ticks ? kLeftFoot : kRightFoot);
        if (!part) part = unit->FindPartByPrefix(kBody);
        if (part) {
            e->pos = FigurePartPosition(unit, part);
        } else {
            const Vector3& place = unit->localPosition;
            float raise = unit->bounds.boxMax.z;
            std::memcpy(&e->pos, &place, 2 * sizeof(float));
            e->pos.z = place.z + raise;
        }
    } else {
        e->pos = origin;
    }
    e->foot.radius = AttachmentOffset().radius;
    e->foot.countdown = 0;
}

// --- Silence (0x2048) ----------------------------------------------------------------------------

// Silence: the first two particles are orbs at the source (state 1) circling in step (the second starts at pi).
// Further ones are wisps (state 0) somewhere along a random orb's last step, thrown out in a random direction
// (inside the unit ball, normalised) by 0.8 of the orb's size.
int ParticleSource::EmitSilence(Particle* p, int) {
    if (std::ssize(particles) <= 1) {
        float& angle = p->velocity.x;       // around the source
        float& angleStep = p->velocity.y;
        float& tilt = p->aux.x;             // of the circle
        float& tiltStep = p->aux.y;
        float& orbitRadius = p->aux.z;      // kept while the carrier is gone
        p->color = 0xFFFFFF;
        p->frame = 6;
        p->pos = origin;
        p->size = 0.0f;
        float start = !particles.empty() ? 0.0f : 3.1416f;    // 0 / pi
        angle = start;
        tilt = start;
        angleStep = 0.1f;
        tiltStep = 0.12f;
        orbitRadius = 1.0f;
        p->state = 1;
        return 1;
    }
    float& wispSize = p->velocity.x;
    auto node = particles.begin();
    if (g_clientRandom.Next() & 1) ++node;
    const Particle* orb = *node;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    Vector3 start = orb->pos - t * orb->drift;
    Vector3 v = RandomInBall();
    p->size = 0.0f;
    p->pos = v * orb->size * 0.8f + start;
    wispSize = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * 0.08f;
    p->color = 0xFFFFFFFF;
    p->frame = 6;
    p->state = 0;
    p->life = 7;
    return 1;
}

// Silence: once the source stops emitting (or loses its carrier) the orbs shrink the source by 0.85 per tick,
// everything going below 0.02. An orb circles the source on a tilted circle (radius: half the carrier's), half a
// radius up, keeping its step in drift and flickering in size. A wisp shows its size (shrinking by 0.8) for its life.
int ParticleSource::UpdateSilence(Particle* p) {
    if (!(flags & ParticleSourceFlag::Emitting) || !attachedTo) {
        if (p->state) size = size * 0.85f;
        if (size < 0.02f) return 0;
    }
    if (!p->state) {
        float& wispSize = p->velocity.x;
        if (--p->life <= 0) return 0;
        float shrunk = wispSize * 0.8f;
        p->size = wispSize;
        wispSize = shrunk;
        return 1;
    }
    float& angle = p->velocity.x;
    const float& angleStep = p->velocity.y;
    float& tilt = p->aux.x;
    const float& tiltStep = p->aux.y;
    float& orbitRadius = p->aux.z;
    float radius;
    if (attachedTo) {
        radius = attachedTo->bounds.radius * 0.5f;
        orbitRadius = radius;
    } else {
        radius = orbitRadius;
    }
    float c = lib::Cos(angle);
    p->color = 0xFFFFFFFF;
    float sn = lib::Sin(angle);
    Vector3 circle = {c, sn, lib::Sin(tilt) * 0.5f};
    Vector3 next = circle * (radius / std::sqrt(LengthSquared(circle))) + origin;
    next.z = radius * 0.5f + next.z;
    p->drift = next - p->pos;
    p->pos = next;
    angle = angleStep + angle;
    tilt = tiltStep + tilt;
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_2 + 0.9f) * size * 0.22f;
    return 1;
}

// --- Modifier1-12 (0x201B-0x2026): update and element tick -------------------------------------------

// Modifier: the first twelve frames are the marker riding its element (state), its size
// shrinking (negative source size) or growing, alpha by the element's tick. Later frames are
// sparks stepping through the frames (back to 12 past the last), flying out by velocity plus their
// spread (slowing by 0.9), fading by 48 per tick and shrinking by 0.9.
int ParticleSource::UpdateModifier(Particle* p) {
    const Vector3& base = p->velocity;  // a spark's point between its element's places
    Vector3& scatter = p->aux;          // added to base, slowing
    if (--p->life == 0) return 0;
    if (p->frame < 12) {
        float oldSize = p->size;
        const ParticleElement& e = elements[p->state];
        p->pos = e.pos;
        // A marker outlives the tables (its element ticks on to 25): alpha 0 past them, the original's zero tail.
        const int tick = e.ticks;
        std::int32_t a = 0;
        if (size < 0.0f) {
            p->size = oldSize * 0.9345794f;    // 1 / 1.07
            if (tick < std::ssize(kModifierAlphaIn)) a = kModifierAlphaIn[tick];
        } else {
            p->size = oldSize * 1.07f;
            if (tick < std::ssize(kModifierAlphaOut)) a = kModifierAlphaOut[tick];
        }
        p->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
        return 1;
    }
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) p->frame = 12;
        p->frameTimer = frameDelay;
    }
    float slow = 0.9f;
    scatter *= slow;
    float oldSize = p->size;
    p->pos = scatter + base;
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
    e->pos = e->ring.offset + origin;
    e->pos.z = spreadMin.x + e->pos.z;
    e->ring.offset *= size >= 0.0f ? 0.71428573f : 1.4f;    // 1 / 1.4, 1.4
}

// --- LightningBlast (0x2030): update and element tick ------------------------------------------------

// LightningBlast: bolts (state n) ride element n-1; a flash (state set, flashes) shows once (and makes the source
// emit 50 per tick), then goes. Sparks (state 0): alpha by the life left (the table shifted by 22, a quirk kept),
// shrinking by 0.9; the jumping ones go to their target and step a frame, the others cycle four frames flying by
// drift (slowing by 0.6).
int ParticleSource::UpdateLightning(Particle* p) {
    float& sparkSize = p->aux.x;
    int life = --p->life;
    if (life < 0) return 0;
    if (p->state != 0) {
        const int& flashes = p->frameTimer;
        if (flashes != 0) {
            if (IsZero(p->size)) {
                emitCount = 50;
                p->size = sparkSize;
                return 1;
            }
            return 0;
        }
        p->pos = elements[p->state - 1].pos;
        p->size = sparkSize;
        return 1;
    }
    const int& jumps = p->frameTimer;
    const Vector3& target = p->velocity;
    p->color = (static_cast<std::uint32_t>(kLightningAlpha[life]) << 22) | 0xFFFFFF;
    if (jumps != 0) {
        float shrunk = sparkSize * 0.9f;
        ++p->frame;
        p->pos = target;
        p->size = sparkSize;
        sparkSize = shrunk;
        return 1;
    }
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->pos += p->drift;
    p->size = sparkSize;
    sparkSize *= 0.9f;
    float slow = 0.6f;
    p->drift *= slow;
    return 1;
}

// LightningBlast bolt heads: the source emits 50 per tick; each head remembers its position (bolt.base),
// moves by bolt.fork, falls (its z speed dropping by 0.075), and the source stops from the tenth tick on.
void ParticleSource::TickLightning(ParticleElement* e) {
    emitCount = 50;
    int tick = ++e->ticks;
    e->bolt.base = e->pos;
    e->pos += e->bolt.fork;
    e->bolt.fork.z -= 0.075f;
    if (tick >= 10) flags &= ~ParticleSourceFlag::Emitting;
}

// --- Teleport (0x2019) -----------------------------------------------------------------------------

// Teleport: a spark at a random point of a disc of the source's size (denser near the
// edge), a little below the source and lifted by its first z step. With a positive size it is
// opaque and fades out (state -19 per tick) while its step grows by 1.2; else it starts clear and
// fades in (state 19) while its step shrinks by 1/1.2.
int ParticleSource::EmitTeleport(Particle* p, int) {
    float& stepFactor = p->velocity.x;  // climb's factor per tick
    float& climb = p->velocity.z;       // the rise per tick
    float& sparkSize = p->aux.x;
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float magnitude = std::fabs(size);
    float t = g_clientRandom.Range(0.0f, 1.0f);
    float spread = (t + 1.0f) * t * 0.5f;
    float angle = g_clientRandom.Range(0.0f, 6.2832f);
    float reach = magnitude * spread;
    float sx = lib::Sin(angle) * reach;
    float cy = lib::Cos(angle) * reach;
    p->pos.y = cy + origin.y;
    p->pos.x = sx + origin.x;
    p->pos.z = origin.z;
    p->size = 0.0f;
    p->pos.z = g_clientRandom.Range(-0.4f, -0.2f) * magnitude + p->pos.z;
    sparkSize = (2.0f - spread) * 0.2f;    // (2 - spread) / 5
    if (g_clientRandom.Range(0.0f, 1.0f) > spread) {
        p->frame = static_cast<int>(g_clientRandom.Next() % 12) + 4;
    } else {
        p->frame = static_cast<int>(g_clientRandom.Next() & 3);
    }
    float rise = 1.0f - spread + 3.0f;
    if (size > 0.0f) {
        stepFactor = 1.2f;
        p->state = -19;
        climb = rise * magnitude * 0.15f;
    } else {
        p->color = 0xFFFFFF;
        stepFactor = 0.8333333f;     // 1 / 1.2
        p->state = 19;
        climb = rise * magnitude * 2.25f;
    }
    p->pos.z = climb + p->pos.z;
    return 1;
}

// Teleport sparks: frames 0-3 cycle (and 4-15 run on back to 4); the z step is redone scaled by its factor; shown at
// the spark's size (hidden in the last tick); alpha moves by the state per tick, an unsigned sum capped at 255 (a
// quirk kept: a fade out below 19 jumps back to 255).
int ParticleSource::UpdateTeleport(Particle* p) {
    const float& stepFactor = p->velocity.x;
    float& climb = p->velocity.z;
    const float& sparkSize = p->aux.x;
    int life = --p->life;
    if (life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame == 4) p->frame = 0;
        if (p->frame == 16) p->frame = 4;
        p->frameTimer = frameDelay;
    }
    p->pos.z -= climb;
    float step = stepFactor * climb;
    climb = step;
    p->pos.z = step + p->pos.z;
    if (life == 1) {
        p->size = 0.0f;
    } else {
        p->size = sparkSize;
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
    p->frame = static_cast<int>(g_clientRandom.Next() & 15);
    Vector3 a = RandomInBall() * size;
    p->size = 0.0f;
    p->pos = a + origin;
    float length = std::sqrt(LengthSquared(a));
    p->life = 20;
    if (length > 0.0f) {
        p->drift = a * (size * 0.1f) / length;
    } else {
        p->drift = {};
    }
    return 1;
}

// CurseStars: alpha by the life left (dropped past 0); a star's size starts
// as a random part of the source's (0.04 of it, flickering) and shrinks by 0.95; it drifts (slowing
// by 0.9). Without a carrier the source stops emitting and is let go (Attach(nullptr)).
int ParticleSource::UpdateCurseStars(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kCurseAlpha[life]) << 24) | 0xFFFFFF;
    if (p->size == 0.0f) {
        p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * 0.04f;
    } else {
        p->size *= 0.95f;
    }
    p->pos += p->drift;
    float slow = 0.9f;
    p->drift *= slow;
    if (!attachedTo) {
        Stop();
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
        p->frame = static_cast<int>(g_clientRandom.Next() % 12);
        Vector3& offset = p->velocity;  // the start, from the source
        float& sparkSize = p->aux.x;
        float lengthSquared;
        Vector3 v = RandomInBall(lengthSquared);
        offset = v / std::sqrt(lengthSquared) * size;
        p->pos = offset + origin;
        p->drift = -offset / static_cast<float>(p->life + 3);
        sparkSize = size * 0.05f;
    }
    p->color = 0xFFFFFF;
    p->size = 0.0f;
    return 1;
}

// StartTrans: the glow flickers at trans.scale while carried, then fades (by the life table, size times 0.6); the
// others come in, cycling four frames at their size, fading out over their last ticks (capped at 4 once the carrier
// is gone).
int ParticleSource::UpdateStartTrans(Particle* p) {
    if (p->state != 0) {
        if (!attachedTo) {
            int life = --p->life;
            if (life < 0) return 0;
            p->color = (static_cast<std::uint32_t>(kTransAlpha[life]) << 24) | 0xFFFFFF;
            p->size *= 0.6f;
            return 1;
        }
        p->color = 0xFFFFFFFF;
        p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * elements[0].trans.scale;
        return 1;
    }
    int life = --p->life;
    if (life < 0) return 0;
    if (!attachedTo) {
        if (life >= 4) life = 4;
        p->life = life;
    }
    const float& sparkSize = p->aux.x;
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->pos += p->drift;
    p->size = sparkSize;
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
    float reach = g_clientRandom.Range(0.0f, fifth);
    float turn = kPi + kPi;
    float angle = g_clientRandom.Range(0.0f, turn);
    float s = lib::Sin(angle);
    p->pos.x = reach * s + origin.x;
    float c = lib::Cos(angle);
    p->pos.y = reach * c + origin.y;
    float top = size + size;
    p->pos.z = g_clientRandom.Range(0.0f, top) + origin.z;
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
    float u = g_clientRandom.Range(-1.0f, 1.0f);
    p->pos.x = u * extent.x + p->pos.x;
    p->pos.y = u * extent.y + p->pos.y;
    float ground = g_clientWorld.RawSurfaceHeightAt(p->pos.x, p->pos.y);
    p->pos.z = p->pos.z - origin.z + ground;
    return 1;
}

// --- Ground dust (0x203A / 0x203B) emission ------------------------------------------------------

// Ground dust: the first 128 particles of a tick run out on a circle (index * 0.049
// radians, step 0.1); the others along the four diagonals (by 32, step index * 1/310). All at the
// source, in element 0's colour, 6 ticks.
int ParticleSource::EmitGroundDust(Particle* p, int index) {
    float& dustSize = p->aux.x;
    float& swell = p->aux.y;    // the size's factor per tick
    if (index >= 128) {
        int j = index - 128;
        int diagonal = j / 32;
        float fu = (j & 31) * 0.0032258064f;
        p->velocity.x = fu * kDustAxisX[diagonal];
        p->velocity.y = fu * kDustAxisY[diagonal];
    } else {
        float fu = index * 0.049140625f;
        p->velocity.x = lib::Sin(fu) * 0.1f;
        p->velocity.y = lib::Cos(fu) * 0.1f;
    }
    p->pos = origin;
    p->size = 0.0f;
    p->life = 6;
    p->frame = 15;
    p->velocity.z = 0.0f;
    dustSize = 0.07f;
    p->color = elements[0].plane.color;
    swell = 1.0f;
    return 1;
}

// --- ShapeChange glow (0x2039) emission and element tick; the update is UpdateGlow ---------------

// ShapeChange: on the first call one element: the last point (-1, -1, -1) and the colour by the alpha field (0
// grey, 1 green, 2 orange). Then one glow per point of the carrying unit (up to typeParam points, offset by
// the client tick), on the ground or platform under it, unless the point is where the last one was; the
// point's index goes in frameTimer and the life is -10 (set by UpdateGlow).
int ParticleSource::EmitGlow(Particle* p, int index) {
    int& order = p->frameTimer;     // the point's index in the emission
    MapObject* unit = attachedTo;
    if (elements.empty()) {
        elements.resize(1);
        elements[0].glow.mark = {-1.0f, -1.0f, -1.0f};
        switch (glowKind) {
        case GlowKind::Grey: elements[0].glow.color = 0xB0B0B0; break;
        case GlowKind::Green: elements[0].glow.color = 0x40FF40; break;
        case GlowKind::Orange: elements[0].glow.color = 0xFF8028; break;
        }
    }
    if (!unit) return 0;
    if (index > typeParam) return 0;
    Vector3 point = unit->PositionAt(index + g_clientWorld.tick);
    Vector3& mark = elements[0].glow.mark;
    if (Same(mark.x, point.x) && Same(mark.y, point.y) && Same(mark.z, point.z)) return 0;
    elements[0].glow.mark = point;
    Vector3 under = {point.x, point.y, 0.0f};
    float ground = g_clientWorld.RawSurfaceHeightAt(point.x, point.y);
    float top = g_clientWorld.platforms.SurfaceHeight(under, ground);
    p->pos.y = point.y;
    p->size = 0.07f;     // 0.07
    p->pos.x = point.x;
    p->frame = 15;
    p->life = -10;
    p->pos.z = top + 0.15f;
    order = index;
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
    float& sizeFactor = p->velocity.x;
    auto [offset, radius] = AttachmentOffset();
    p->color = 0xFFFFFF;
    p->frame = 3;
    Vector3 v = RandomInBall();
    p->size = 0.0f;
    p->pos = v * (radius * 0.8f) + origin;
    p->life = 20;
    sizeFactor = WordToFloat(g_clientRandom.Next()) * kWordTo0_6 + 0.7f;
    Unit* unit = UnitOrNull(attachedTo);   // stun rides units (the game reads any carrier as one)
    if (!unit) {
        Stop();
        return 0;
    }
    if (unit->moveMode == MoveMode::Run) {
        emitCount = 4;
        p->life = 5;
    } else {
        emitCount = 1;
    }
    if (unit->IsDead()) {
        Stop();
    }
    return 1;
}

// Stun stars: dropped past their life; else flickering at a random size of the source's times the star's factor.
// Without a carrier the source stops and is let go.
int ParticleSource::UpdateStun(Particle* p) {
    const float& sizeFactor = p->velocity.x;
    if (--p->life < 0) return 0;
    p->color = 0xFFFFFFFF;
    p->size = (WordToFloat(g_clientRandom.Next()) * kWordTo0_4 + 0.8f) * size * sizeFactor;
    if (!attachedTo) {
        Stop();
    }
    return 1;
}
