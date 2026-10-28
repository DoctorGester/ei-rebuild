// Particle emit callbacks (of a CParticleSource) and the element array allocator.
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU stack,
// static_cast<float> where it stores to a float.
#include "effects.h"
#include "map_object_figures.h"   // CObject3D

#include "random.h"

#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "world.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// IsaacRandom::Range leaves its result unrounded in ST0.
double Range(float min, float max) { return g_random.Range(min, max); }

// --- LightningBlast (0x2030) ----------------------------------------------------------------------

constexpr float kTwoPi = std::bit_cast<float>(0x40C90FD0u);
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
#define EI_SCALE_0_9 2.0954757378615665e-10   // ~0.9 * 2^-32
#define EI_SCALE_1 2.3283064370807974e-10   // ~2^-32
#define EI_SCALE_ANGLE 1.4621764913365538e-9   // ~2 pi * 2^-32
#define EI_SCALE_2 4.656612874161595e-10   // ~2^-31
#define EI_SCALE_0_04 9.313225540156372e-12   // ~0.04 * 2^-32
#define EI_SCALE_0_01 2.328306385039093e-12   // ~0.01 * 2^-32

// One point of a new bolt: a random offset (none for point 0) around the source, on the ground,
// with a fork direction.
void PlaceBoltPoint(CParticleSource* s, int i, Vector3& dir) {
    float radius = static_cast<float>(Range(0.0f, 1.0f));
    double angle = Range(0.0f, kTwoPi);
    float dx = static_cast<float>(x87::fsin(angle) * D(radius));
    float dy = static_cast<float>(x87::fcos(angle) * D(radius));
    dir.x = dx;
    dir.y = dy;
    std::memset(&dir.z, 0, sizeof(float));
    if (i == 0) std::memset(&dir, 0, sizeof(dir));
    double size = s->size;
    float ax = static_cast<float>(size * D(dir.x));
    float ay = static_cast<float>(D(dir.y) * size);
    float az = static_cast<float>(size * D(dir.z));
    float bx = static_cast<float>(D(ax) * D(EI_POINT_THREE));
    float by = static_cast<float>(D(ay) * D(EI_POINT_THREE));
    float bz = static_cast<float>(D(az) * D(EI_POINT_THREE));
    float cx = static_cast<float>(D(bx) + D(s->origin.x));
    float cy = static_cast<float>(D(by) + D(s->origin.y));
    (void)static_cast<float>(D(bz) + D(s->origin.z));
    float cz = static_cast<float>(g_clientWorld->RawSurfaceHeightAt(cx, cy) + D(EI_POINT_THREE));
    ParticleElement* points = s->elements.data();
    points[i].pos = {cx, cy, cz};
    points[i].bolt.base = {cx, cy, cz};

    double inverse = D(EI_ONE) / std::sqrt(LengthSquared2DUnrounded<double>(dir.y, dir.x) + D(EI_QUARTER));
    dir.x = static_cast<float>(inverse * D(dir.x));
    dir.y = static_cast<float>(D(dir.y) * inverse);
    double halfInverse = inverse * D(EI_HALF);
    dir.z = static_cast<float>(halfInverse);
    float fz = static_cast<float>(halfInverse * D(EI_HALF));
    float fy = static_cast<float>(D(dir.y) * D(EI_HALF));
    float fx = static_cast<float>(D(dir.x) * D(EI_HALF));
    points = s->elements.data();
    points[i].bolt.fork = {fx, fy, fz};
    points[i].bolt.emitted = 0;
    points[i].ticks = 0;   // bursts
}

// Particles along the bolt: two per point (the odd ones flicker with a random size and frame).
int EmitAlongBolt(CParticleSource* s, Particle* p, ParticleElement* points) {
    int emitted = points[0].bolt.emitted;
    p->frame = 0;
    p->life = 10;
    int index = emitted / 2;
    p->frameTimer = points[0].bolt.emitted & 1;
    p->state = index + 1;
    std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    p->aux0 = std::bit_cast<float>(0x3DCCCCCDu);     // 0.1
    ++points[0].bolt.emitted;
    if (p->frameTimer == 0) return 1;
    std::uint32_t roll = g_random.Next();
    p->aux0 = static_cast<float>((Unsigned(roll) * EI_SCALE_0_9 + D(EI_HALF)) * D(s->size) * D(EI_POINT_TWO));
    p->frame = static_cast<int>((g_random.Next() & 3) + 12);
    if (index == 0) p->aux0 = static_cast<float>(D(s->size) * D(EI_POINT_FOUR));
    std::memcpy(&p->size, &p->aux0, sizeof(float));
    return 1;
}

// A burst at the impact point: a random direction inside the unit disc (ten tries), spread by a
// random angle and distance.
int EmitBurst(CParticleSource* s, Particle* p, ParticleElement* points) {
    p->color = 0x40FFFFFF;
    p->frame = 8;
    g_random.Next();
    double u = Unsigned(g_random.Next()) * EI_SCALE_1;
    float spread = static_cast<float>(D(EI_ONE) - u * u);
    float angle = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_ANGLE);
    float vx, vy;
    for (int tries = 10;;) {
        vx = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_2 - D(EI_ONE));
        double y = Range(-1.0f, 1.0f);
        vy = static_cast<float>(y);
        if (y * D(vy) + D(vx) * D(vx) < D(EI_ONE)) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
    p->drift = {vx, vy, 0.0f};
    std::memset(&p->drift.z, 0, sizeof(float));
    p->drift.x = static_cast<float>(x87::fcos(angle) * D(spread));
    p->drift.y = static_cast<float>(x87::fsin(angle) * D(spread));
    double scale = D(s->size) * D(EI_POINT_THREE);
    p->drift.x = static_cast<float>(scale * D(p->drift.x));
    p->drift.y = static_cast<float>(scale * D(p->drift.y));
    p->drift.z = static_cast<float>(scale * D(p->drift.z));
    std::memset(&p->drift.z, 0, sizeof(float));
    p->pos.x = static_cast<float>(D(points[0].pos.x) + D(p->drift.x));
    p->pos.y = static_cast<float>(D(points[0].pos.y) + D(p->drift.y));
    double rise = (D(EI_ONE) - D(spread)) * D(s->size) * D(EI_TENTH) + D(points[0].pos.z);
    std::memset(&p->size, 0, sizeof(float));
    p->pos.z = static_cast<float>(rise);
    p->frameTimer = 0;
    p->life = x87::fistp(static_cast<float>(D(EI_NINE) - D(spread) * D(EI_THREE)));
    double roll = Unsigned(g_random.Next()) * EI_SCALE_0_04 + D(EI_TENTH);
    p->aux0 = static_cast<float>(roll * (D(EI_TWO) - D(spread)) * D(s->size));
    return 1;
}

// A spark between a random point and its base, at a random fraction of the way.
int EmitSpark(CParticleSource* s, Particle* p) {
    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(s->elements));
    std::uint32_t index = g_random.Next() % count;
    p->frame = 1;
    p->aux0 = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_0_01 * D(EI_THREE) + D(EI_SPARK_SIZE));
    p->life = 6;
    p->frameTimer = 1;
    double u = Unsigned(g_random.Next()) * EI_SCALE_1;
    double w = D(EI_ONE) - u;
    ParticleElement* points = s->elements.data();
    std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    const ParticleElement& n = s->elements[index];
    float bx = static_cast<float>(w * D(n.bolt.base.x));
    float by = static_cast<float>(w * D(n.bolt.base.y));
    float bz = static_cast<float>(w * D(n.bolt.base.z));
    float px = static_cast<float>(u * D(n.pos.x));
    float py = static_cast<float>(u * D(n.pos.y));
    double pz = u * D(n.pos.z);
    p->velocity.x = static_cast<float>(D(px) + D(bx));
    p->velocity.y = static_cast<float>(D(py) + D(by));
    p->velocity.z = static_cast<float>(pz + D(bz));
    return 1;
}

// --- Modifier1-12 (0x201B-0x2026) ---------------------------------------------------------------

#define EI_TEN 10.0f
#define EI_ZERO 0.0f
#define EI_SMALL_RING 0.04347826f   // 1/23
#define EI_JITTER 0.04f
#define EI_SCALE_0_08 1.8626451080312744e-11   // ~0.08 * 2^-32

constexpr std::int32_t kFadeAlpha[35] = /*0078A774*/ {0, 32, 64, 128, 255, 207, 159, 111, 63, 15};
const char* NoCarrierMessage() { return "Particle effect Modifier without carrier\n"; }

void DebugMessage(const char* a0) { game::DebugLog(a0); }

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void SetBits(void* dst, std::uint32_t bits) { std::memcpy(dst, &bits, sizeof(bits)); }

// The axis the points are spread around (their offsets are perpendicular to it); some types also
// raise or lower the ring (spreadMin.x holds the height).
Vector3 ModifierAxis(CParticleSource* s, float twiceRadius) {
    Vector3 axis;
    std::memset(&axis, 0, sizeof(axis));
    switch (s->type_ - 0x201B) {
    case 0:
        SetBits(&axis.z, 0x3F800000);
        break;
    case 1:
    case 2:
        s->spreadMin.x = static_cast<float>(D(twiceRadius) * D(EI_POINT_FOUR) + D(s->spreadMin.x));
        SetBits(&axis.z, 0x3F800000);
        break;
    case 3:
        s->spreadMin.x = static_cast<float>(D(s->spreadMin.x) - D(twiceRadius) * D(EI_POINT_FOUR));
        SetBits(&axis.z, 0x3F800000);
        break;
    case 4:
    case 5:
    case 6:
    case 7: {
        Vector3 y;
        s->attachedTo->localRotation_.AxisY(&y);
        std::memcpy(&axis, &y, sizeof(axis));
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
void PlaceModifierPoints(CParticleSource* s) {
    Vector3 unused;
    float radius;
    s->AttachmentOffset(&unused, &radius);
    std::memcpy(&s->spreadMin.x, &radius, sizeof(float));
    float twice = static_cast<float>(D(radius) + D(radius));
    Vector3 axis = ModifierAxis(s, twice);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        s->elements[i].ticks = 0;   // the fade step
        float x, y;
        double z, lengthSq;
        for (;;) {
            x = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_2 - D(EI_ONE));
            y = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_2 - D(EI_ONE));
            z = Unsigned(g_random.Next()) * EI_SCALE_2 - D(EI_ONE);
            double dot = DotUnrounded<Sum3::ZYX, double>(axis.x, axis.y, axis.z, x, y, z);
            float px = static_cast<float>(D(axis.x) * dot);
            float py = static_cast<float>(D(axis.y) * dot);
            double pz = dot * D(axis.z);
            x = static_cast<float>(D(x) - D(px));
            y = static_cast<float>(D(y) - D(py));
            z = z - pz;
            lengthSq = LengthSquaredUnrounded<Sum3::ZYX, double>(x, y, z);
            EI_NAN_AUDIT(lengthSq);
            if (lengthSq != 0.0) break;   // a NaN ends the search too
        }
        double scale = D(twice) / std::sqrt(lengthSq);
        EI_NAN_AUDIT(s->size);
        if (s->size < EI_ZERO) scale = scale * D(EI_SMALL_RING);
        ParticleElement& point = s->elements[i];
        float ex = static_cast<float>(scale * D(x));
        float ey = static_cast<float>(scale * D(y));
        float ez = static_cast<float>(scale * z);
        point.ring.offset.x = ex;
        point.ring.offset.y = ey;
        point.ring.offset.z = ez;
        float wx = static_cast<float>(D(ex) + D(s->origin.x));
        float wy = static_cast<float>(D(ey) + D(s->origin.y));
        float wz = static_cast<float>(D(ez) + D(s->origin.z));
        point.pos.x = wx;
        point.pos.y = wy;
        point.pos.z = wz;
        point.pos.z = static_cast<float>(D(s->spreadMin.x) + D(point.pos.z));
        std::memcpy(&point.ring.start, &point.pos, sizeof(Vector3));
    }
}

#define EI_ZONE_EXIT_SPREAD 1.9f
#define EI_SCALE_0_03 1.3969837789817515e-10   // ~0.03 * 2^-32
#define EI_POINT_SEVEN 0.7f
#define EI_TWENTIETH 0.05f
#define EI_SCALE_0_4 9.313226580990458e-11   // ~0.4 * 2^-32
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
#define EI_FEEBLE_SPEED 0.05000000074505806   // 0.05
#define EI_SCALE_0_2 4.6566139843846196e-11   // ~0.2 * 2^-32
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

// x87 select through FCOMP; SHL/SAR of the status word: `a` when a < b (or unordered), else `b`.
float SelectLess(float a, float b, float ifLess, float otherwise) {
    return a >= b ? otherwise : ifLess;
}

// One random draw in [-1, 1) through the ISAAC results: Next() * ~2^-31 - 1.
double Signed(std::uint32_t roll) { return Unsigned(roll) * EI_SCALE_2 - D(EI_ONE); }

} // namespace

// LightningBlast: on the first emission the bolt's ten points are placed; then the
// particles run along the bolt (two per point), then bursts at the impact point (up to five, while
// the source has fewer than 200 particles), then sparks between the points.
int CParticleSource::EmitLightning(Particle* p, int) {
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
int CParticleSource::EmitModifier(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    if (std::ssize(elements) == 0) {
        if (!attachedTo) {
            DebugMessage(NoCarrierMessage());
            return 0;
        }
        int count = static_cast<int>(libcrt::Ftol(D(AbsBits(size)) * D(EI_TEN)));
        if (count < 1) count = 1;
        elements.resize(count);
        PlaceModifierPoints(this);
    }
    ParticleElement* points = elements.data();
    const int fade = points[0].ticks;
    p->color = (static_cast<std::uint32_t>(kFadeAlpha[fade]) << 24) | 0xFFFFFF;
    if (points[0].ticks == 0 && index < std::ssize(elements)) {
        unsigned kind = static_cast<unsigned>(type_ - 0x201B);
        if (kind <= 11) p->frame = static_cast<int>(kind);
        p->state = index;
        std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
        SetBits(&p->size, 0x3F800000);
        SetBits(&p->size, size >= EI_ZERO ? 0x3E000000u : 0x3E4CCCCDu);   // 0.125 / 0.2
        p->life = 25;
        return 1;
    }

    std::uint32_t count = static_cast<std::uint32_t>(std::ssize(elements));
    std::uint32_t pick = g_random.Next() % count;
    float u = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_1);
    std::uint32_t frameRoll = g_random.Next();
    double w = D(EI_ONE) - D(u);
    p->life = 5;
    p->frame = static_cast<int>((frameRoll & 3) + 12);
    const ParticleElement& e = elements[pick];
    float b0 = static_cast<float>(w * D(e.ring.start.x));
    float b1 = static_cast<float>(w * D(e.ring.start.y));
    double b2 = w * D(e.ring.start.z);
    double a0 = D(u) * D(e.pos.x);
    float a1 = static_cast<float>(D(u) * D(e.pos.y));
    float a2 = static_cast<float>(D(u) * D(e.pos.z));
    p->velocity.x = static_cast<float>(a0 + D(b0));
    p->velocity.y = static_cast<float>(D(a1) + D(b1));
    p->velocity.z = static_cast<float>(D(a2) + b2);
    std::uint32_t ra = g_random.Next();
    std::uint32_t rb = g_random.Next();
    std::uint32_t rc = g_random.Next();
    float jx = static_cast<float>(Unsigned(rc) * EI_SCALE_0_08 - D(EI_JITTER));
    float jy = static_cast<float>(Unsigned(rb) * EI_SCALE_0_08 - D(EI_JITTER));
    float jz = static_cast<float>(Unsigned(ra) * EI_SCALE_0_08 - D(EI_JITTER));
    std::memcpy(&p->aux0, &jx, sizeof(float));
    std::memcpy(&p->aux1, &jy, sizeof(float));
    std::memcpy(&p->aux2, &jz, sizeof(float));
    std::memcpy(&p->pos, &elements[pick].ring.start, sizeof(Vector3));
    SetBits(&p->size, 0x3D75C28F);   // 0.06
    return 1;
}

// --- Smaller callbacks ----------------------------------------------------------------------------

// ZoneExit: the density follows alpha * typeParam (at most 40 a tick); particles start at a random point of a
// turned ellipse (radii alpha and typeParam) around the source on the ground, red-tinted when extent.x is set,
// rising by an amount that is larger near the centre.
int CParticleSource::EmitZoneExit(Particle* p, int) {
    int count = x87::fistp(static_cast<float>(D(alpha) * D(typeParam)));
    if (count > 40) count = 40;
    emitCount = count;
    EI_NAN_AUDIT(extent.x);
    p->color = extent.x != 0.0f ? 0x00FF6060u : 0x00FFFFFFu;
    p->frame = static_cast<int>(g_random.Next() & 15);
    p->life = 15;
    p->frameTimer = 1;
    p->state = 0;
    float a = static_cast<float>(Signed(g_random.Next()));
    double b = Signed(g_random.Next());
    float falloff = static_cast<float>((D(EI_ZONE_EXIT_SPREAD) - D(a) * D(a)) - b * b);
    float x = static_cast<float>((D(a) * D(alpha) * D(turnCos) + D(origin.x)) - b * D(turnSin) * D(typeParam));
    float y = static_cast<float>((D(turnSin) * D(alpha) * D(a) + b * D(turnCos) * D(typeParam)) + D(origin.y));
    double ground = g_clientWorld->RawSurfaceHeightAt(x, y);
    p->pos.x = x;
    p->pos.y = y;
    std::memset(&p->size, 0, sizeof(float));
    p->pos.z = static_cast<float>(ground + D(EI_TENTH));
    double roll = Unsigned(g_random.Next()) * EI_SCALE_0_03 + D(EI_POINT_SEVEN);
    p->velocity.x = static_cast<float>(roll * D(falloff) * D(EI_TWENTIETH));
    return 1;
}

// 0x2039: on its first update (life -10) the particle's lifetime is set from its
// position in the emission (frameTimer * 8 / particle count + 7); the alpha follows kGlowAlpha.
int CParticleSource::UpdateGlow(Particle* p) {
    if (p->life == -10) p->life = p->frameTimer * 8 / std::ssize(particles) + 7;
    if (--p->life < 0) return 0;
    int opacity = x87::fistp(static_cast<float>(D(kGlowAlpha[p->life]) * D(EI_BYTE)));
    p->color = (p->color & 0xFFFFFF) + (static_cast<std::uint32_t>(opacity) << 24);
    return 1;
}

// 0x203A/0x203B: dust moving along the ground, never below the plane the source measured (element 0),
// growing and slowing down.
int CParticleSource::UpdateGroundDust(Particle* p) {
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kDustAlpha[p->life]) << 24) + (p->color & 0xFFFFFF);
    p->pos.x = static_cast<float>(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = static_cast<float>(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = static_cast<float>(D(p->velocity.z) + D(p->pos.z));
    double lifted = g_clientWorld->RawSurfaceHeightAt(p->pos.x, p->pos.y) + D(EI_TENTH);
    p->pos.z = static_cast<float>(lifted);
    const auto& plane = elements[0].plane;
    double along = (D(p->pos.x) - D(origin.x)) * D(plane.normal.x) + (D(p->pos.y) - D(origin.y)) * D(plane.normal.y);
    float onPlane = static_cast<float>(D(plane.height) - along * D(plane.normal.z) + D(EI_TENTH));
    float ground = static_cast<float>(lifted);
    p->pos.z = SelectLess(ground, onPlane, onPlane, ground);
    std::memcpy(&p->size, &p->aux0, sizeof(float));
    p->velocity.x = static_cast<float>(D(p->velocity.x) * D(EI_DUST_GROWTH));
    p->velocity.y = static_cast<float>(D(p->velocity.y) * D(EI_DUST_GROWTH));
    p->velocity.z = static_cast<float>(D(p->velocity.z) * D(EI_DUST_GROWTH));
    p->aux0 = static_cast<float>(D(p->aux1) * D(p->aux0));
    return 1;
}

// 0x203A/0x203B: on the element's first tick, the ground plane under the source (the normal's z replaced by
// its reciprocal when non-zero); after the second the source stops emitting.
void CParticleSource::TickGroundPlane(ParticleElement* e) {
    if (e->ticks == 0) {
        auto& plane = e->plane;
        g_clientWorld->platforms_.SurfacePlane(origin.x, origin.y, &plane.normal, &plane.height);
        if (plane.normal.z != 0.0f) plane.normal.z = static_cast<float>(D(EI_ONE) / D(plane.normal.z));   // a NaN stays the same NaN
    }
    if (++e->ticks > 1) flags_ &= ~1u;
}

// Portal: particles spiral up (their offset rotated by the turn in velocity.x/y each tick) and fade above 8
// units; the spare ones (state set) flicker in size while the portal is on.
int CParticleSource::UpdatePortal(Particle* p) {
    if (p->state != 0) {
        if (!(flags_ & 1)) return 0;
        EI_NAN_AUDIT(size);
        if (size < EI_ONE) {
            std::memset(&p->size, 0, sizeof(float));
            return 1;
        }
        p->size = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_0_4 * D(EI_ONE_AND_HALF) + D(EI_ONE_POINT_TWO));
        return 1;
    }
    if (p->life == 0) return 0;
    double c = p->velocity.x, s = p->velocity.y;
    double nx = D(p->aux1) * s + c * D(p->aux0);
    int timer = p->frameTimer - 1;
    double ny = D(p->aux1) * c - D(p->aux0) * s;
    float nyRounded = static_cast<float>(ny);
    p->aux0 = static_cast<float>(nx);
    p->aux1 = static_cast<float>(ny);
    p->pos.x = static_cast<float>(nx + D(origin.x));
    p->pos.y = static_cast<float>(D(nyRounded) + D(origin.y));
    double rise = D(size) * D(p->velocity.z);
    p->frameTimer = timer;
    p->pos.z = static_cast<float>(rise + D(p->pos.z));
    p->velocity.z = static_cast<float>(D(p->velocity.z) * D(EI_PORTAL_GROWTH));
    if (timer == 0) {
        if (++p->frame == 4) p->frame = 0;
        if (p->frame == 15) p->frame = 4;
        p->frameTimer = frameDelay;
    }
    double climb = D(p->pos.z) - D(origin.z);
    if (climb > D(EI_EIGHT)) {
        std::memset(&p->size, 0, sizeof(float));
        p->life = 0;
        p->color = 0x00FFFFFF;
        return 1;
    }
    std::memcpy(&p->size, &p->aux2, sizeof(float));
    float fade = static_cast<float>((D(EI_EIGHT) - climb) * (D(EI_TWO_HUNDRED) / D(EI_EIGHT)));
    int opacity = (x87::fistp(fade) + 0x37) >> 2;
    if (opacity >= 0x40) opacity = 0x40;
    p->color = (static_cast<std::uint32_t>(opacity) << 24) | 0xFFFFFF;
    return 1;
}

// Portal: |size| * 10 tries per tick (truncated by the game's _ftol).
void CParticleSource::TickPortal(ParticleElement*) { emitCount = static_cast<int>(libcrt::Ftol(D(size) * D(EI_TEN))); }

// Sparks: the spare ones (state set) get a random size once and are dropped after; the
// rest fly along their drift, shrinking and slowing, with the alpha from kSparkAlpha.
int CParticleSource::UpdateSparks(Particle* p) {
    if (p->state != 0) {
        EI_NAN_AUDIT(p->size);
        if (p->size != 0.0f) {
            std::memset(&p->size, 0, sizeof(float));
            return 0;
        }
        p->size = static_cast<float>((Unsigned(g_random.Next()) * EI_SCALE_0_4 + D(EI_POINT_EIGHT)) * D(size) *
                        D(EI_SPARK_SCALE));
        return 1;
    }
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(kSparkAlpha[p->life]) << 24) | 0xFFFFFF;
    p->pos.x = static_cast<float>(D(p->drift.x) + D(p->pos.x));
    p->pos.y = static_cast<float>(D(p->drift.y) + D(p->pos.y));
    p->pos.z = static_cast<float>(D(p->drift.z) + D(p->pos.z));
    p->size = static_cast<float>(D(p->size) * D(EI_POINT_NINE));
    p->drift.x = static_cast<float>(D(p->drift.x) * D(EI_POINT_SIX));
    p->drift.y = static_cast<float>(D(p->drift.y) * D(EI_POINT_SIX));
    p->drift.z = static_cast<float>(D(p->drift.z) * D(EI_POINT_SIX));
    ++p->frame;
    return 1;
}

// FeebleMind: size * 2 tries per tick; each particle starts one step out from the
// source in a random horizontal direction.
int CParticleSource::EmitFeebleMind(Particle* p, int) {
    emitCount = x87::fistp(static_cast<float>(D(size) + D(size)));
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(g_random.Next() & 15);
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    p->life = 20;
    float twoPi = static_cast<float>(D(EI_PI) + D(EI_PI));
    std::uint32_t roll = g_random.Next();
    std::memset(&p->velocity.z, 0, sizeof(float));
    double angle = Unsigned(roll) * D(twoPi) * EI_SCALE_1;
    p->velocity.x = static_cast<float>(x87::fcos(angle) * EI_FEEBLE_SPEED);
    p->velocity.y = static_cast<float>(x87::fsin(angle) * EI_FEEBLE_SPEED);
    p->pos.x = static_cast<float>(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = static_cast<float>(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = static_cast<float>(D(p->velocity.z) + D(p->pos.z));
    float grow = static_cast<float>((Unsigned(g_random.Next()) * EI_SCALE_0_2 + D(EI_POINT_NINE)) * D(size) *
                       D(EI_FEEBLE_SIZE));
    std::memcpy(&p->aux2, &grow, sizeof(float));
    return 1;
}

// FeetCloud: size * 8 tries per tick; a puff at one of the two feet (the element's position once it is
// tracked, else the source), drifting in a random direction inside the unit disc, rising.
int CParticleSource::EmitFeetCloud(Particle* p, int) {
    emitCount = x87::fistp(static_cast<float>(D(size) * D(EI_FEET_DENSITY)));
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(g_random.Next() & 15);
    int foot = static_cast<int>(g_random.Next() & 1);
    p->state = foot;
    const ParticleElement& e = elements[foot];
    if (e.foot.countdown >= 0) std::memcpy(&p->pos, &e.pos, sizeof(Vector3));
    else std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    std::memset(&p->size, 0, sizeof(float));
    float x, y;
    for (int tries = 10;;) {
        x = static_cast<float>(Signed(g_random.Next()));
        double wide = Signed(g_random.Next());
        y = static_cast<float>(wide);
        if (wide * D(y) + D(x) * D(x) < EI_ONE) break;   // integer-sourced: never NaN
        if (!(--tries > 0)) break;
    }
    float scale = e.foot.radius;
    double dx = D(x) * D(EI_TENTH);
    float dy = static_cast<float>(D(y) * D(EI_TENTH));
    float dz = static_cast<float>(0.0 * D(EI_TENTH));
    double sx = dx * D(scale);
    float sy = static_cast<float>(D(dy) * D(scale));
    float sz = static_cast<float>(D(dz) * D(scale));
    p->drift.x = static_cast<float>(sx * D(size));
    p->drift.y = static_cast<float>(D(sy) * D(size));
    p->drift.z = static_cast<float>(D(sz) * D(size));
    std::memset(&p->aux0, 0, sizeof(float));
    std::memset(&p->aux1, 0, sizeof(float));
    std::memset(&p->aux2, 0, sizeof(float));
    SetBits(&p->velocity.y, 0x3F800000);
    p->life = 10;
    p->velocity.x = static_cast<float>(D(size) * D(e.foot.radius) * D(EI_POINT_FOUR));
    p->pos.z = static_cast<float>(D(p->velocity.x) * D(EI_FEET_RISE) + D(p->pos.z));
    return 1;
}

// StartTrans: once the element's countdown runs out the source stops (flags 1 and 4 off) and detaches; the
// ring widens, and element 0's trans.count grows by 0.4 a tick and sets the tries per tick (at most 8).
void CParticleSource::TickStartTrans(ParticleElement* e) {
    int ticks = e->ticks--;
    if (ticks < 0) {
        flags_ &= ~5u;
        Attach(nullptr);
    }
    e->trans.scale = static_cast<float>(D(size) * D(EI_TRANS_GROWTH) + D(e->trans.scale));
    elements[0].trans.count = static_cast<float>(D(elements[0].trans.count) + D(EI_POINT_FOUR));
    int count = x87::fistp(elements[0].trans.count);
    emitCount = count;
    if (!(count < 8)) emitCount = 8;
}
