// Particle emit callbacks (+0x1E0 of a CParticleSource) in 0x00490000-0x00497000 and the element
// array allocator. Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "effects.h"
#include "map_object_figures.h"   // CObject3D

#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "originals_engine.h"
#include "originals_world.h"
#include "world.h"

#include <bit>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// Constants the original reads from memory at run time.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// 00488970 IsaacRandom::Range leaves its result unrounded in ST0.

double Range(float min, float max) { return RandomRange(&GlobalRandom(), nullptr, min, max); }

// The source's element array is MFC-style (data +0x154, size +0x158, max +0x15C, grow by +0x160);
// the emitters inline SetSize(n) for an empty array (no elements to keep, none zeroed).
void SetElementCount(CParticleSource* s, int n) {
    if (!s->elements) {
        s->elements = AllocateElements(n);
        s->elementCapacity = n;
        s->elementCount = n;
    } else if (s->elementCapacity < n) {
        int grow = s->elementGrowBy;
        if (grow == 0) {
            grow = s->elementCount / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int capacity = s->elementCapacity + grow;
        if (!(capacity > n)) capacity = n;
        ParticleElement* data = AllocateElements(capacity);
        std::memcpy(data, s->elements, static_cast<std::size_t>(s->elementCount) * sizeof(ParticleElement));
        gamecrt::operator_delete(reinterpret_cast<void**>(s->elements)[-1]);
        s->elements = data;
        s->elementCount = n;
        s->elementCapacity = capacity;
    } else {
        s->elementCount = n;
    }
}

// --- LightningBlast (0x2030) ----------------------------------------------------------------------

// The bolt's ten points (the source's elements): where the bolt touches the ground, the direction
// of its fork, and how far the emission has got.
struct BoltPoint {
    Vector3 pos;                // +0x00 on the ground
    Vector3 fork;               // +0x0C
    Vector3 base;               // +0x18 copy of pos
    std::uint8_t unused_24[0xC];
    int emitted;                // +0x30 point 0: particles emitted along the bolt
    int bursts;                 // +0x34 point 0
};
static_assert(sizeof(BoltPoint) == sizeof(ParticleElement));

constexpr float kTwoPi = std::bit_cast<float>(0x40C90FD0u);
#define EI_POINT_THREE GameFloat(0x0073C54C)       // 0.3
#define EI_QUARTER GameFloat(0x0073C3B0)           // 0.25
#define EI_ONE GameFloat(0x0073B84C)               // 1.0
#define EI_HALF GameFloat(0x0073B86C)              // 0.5
#define EI_POINT_FOUR GameFloat(0x0073B868)        // 0.4
#define EI_POINT_TWO GameFloat(0x0073BAD4)         // 0.2
#define EI_TENTH GameFloat(0x0073B850)             // 0.1
#define EI_THREE GameFloat(0x0073BAA4)             // 3.0
#define EI_NINE GameFloat(0x0073CF10)              // 9.0
#define EI_TWO GameFloat(0x0073CDB8)               // 2.0
#define EI_SPARK_SIZE GameFloat(0x0073CCB8)        // 0.06
#define EI_SCALE_0_9 GameDouble(0x0073CF20)        // ~0.9 * 2^-32
#define EI_SCALE_1 GameDouble(0x0073C468)          // ~2^-32
#define EI_SCALE_ANGLE GameDouble(0x0073CF18)      // ~2 pi * 2^-32
#define EI_SCALE_2 GameDouble(0x0073C508)          // ~2^-31
#define EI_SCALE_0_04 GameDouble(0x0073CF08)       // ~0.04 * 2^-32
#define EI_SCALE_0_01 GameDouble(0x0073CF00)       // ~0.01 * 2^-32

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
    float cz = static_cast<float>(WorldClient()->ClientRawSurfaceHeight(cx, cy) + D(EI_POINT_THREE));
    BoltPoint* points = reinterpret_cast<BoltPoint*>(s->elements);
    points[i].pos = {cx, cy, cz};
    points[i].base = {cx, cy, cz};

    double inverse = D(EI_ONE) / x87::fsqrt(LengthSquared2DUnrounded<double>(dir.y, dir.x) + D(EI_QUARTER));
    dir.x = static_cast<float>(inverse * D(dir.x));
    dir.y = static_cast<float>(D(dir.y) * inverse);
    double halfInverse = inverse * D(EI_HALF);
    dir.z = static_cast<float>(halfInverse);
    float fz = static_cast<float>(halfInverse * D(EI_HALF));
    float fy = static_cast<float>(D(dir.y) * D(EI_HALF));
    float fx = static_cast<float>(D(dir.x) * D(EI_HALF));
    points = reinterpret_cast<BoltPoint*>(s->elements);
    points[i].fork = {fx, fy, fz};
    points[i].emitted = 0;
    points[i].bursts = 0;
}

// Particles along the bolt: two per point (the odd ones flicker with a random size and frame).
int EmitAlongBolt(CParticleSource* s, Particle* p, BoltPoint* points) {
    int emitted = points[0].emitted;
    p->frame = 0;
    p->life = 10;
    int index = emitted / 2;
    p->frameTimer = points[0].emitted & 1;
    p->state = index + 1;
    std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    p->aux0 = std::bit_cast<float>(0x3DCCCCCDu);     // 0.1
    ++points[0].emitted;
    if (p->frameTimer == 0) return 1;
    std::uint32_t roll = GlobalRandom().Next();
    p->aux0 = static_cast<float>((Unsigned(roll) * EI_SCALE_0_9 + D(EI_HALF)) * D(s->size) * D(EI_POINT_TWO));
    p->frame = static_cast<int>((GlobalRandom().Next() & 3) + 12);
    if (index == 0) p->aux0 = static_cast<float>(D(s->size) * D(EI_POINT_FOUR));
    std::memcpy(&p->size, &p->aux0, sizeof(float));
    return 1;
}

// A burst at the impact point: a random direction inside the unit disc (ten tries), spread by a
// random angle and distance.
int EmitBurst(CParticleSource* s, Particle* p, BoltPoint* points) {
    p->color = 0x40FFFFFF;
    p->frame = 8;
    GlobalRandom().Next();
    double u = Unsigned(GlobalRandom().Next()) * EI_SCALE_1;
    float spread = static_cast<float>(D(EI_ONE) - u * u);
    float angle = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_ANGLE);
    float vx, vy;
    for (int tries = 10;;) {
        vx = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
        double y = Range(-1.0f, 1.0f);
        vy = static_cast<float>(y);
        if (!(y * D(vy) + D(vx) * D(vx) >= D(EI_ONE))) break;
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
    double roll = Unsigned(GlobalRandom().Next()) * EI_SCALE_0_04 + D(EI_TENTH);
    p->aux0 = static_cast<float>(roll * (D(EI_TWO) - D(spread)) * D(s->size));
    return 1;
}

// A spark between a random point and its base, at a random fraction of the way.
int EmitSpark(CParticleSource* s, Particle* p) {
    std::uint32_t count = static_cast<std::uint32_t>(s->elementCount);
    std::uint32_t index = GlobalRandom().Next() % count;
    p->frame = 1;
    p->aux0 = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_0_01 * D(EI_THREE) + D(EI_SPARK_SIZE));
    p->life = 6;
    p->frameTimer = 1;
    double u = Unsigned(GlobalRandom().Next()) * EI_SCALE_1;
    double w = D(EI_ONE) - u;
    BoltPoint* points = reinterpret_cast<BoltPoint*>(s->elements);
    std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    const BoltPoint& n = reinterpret_cast<BoltPoint*>(s->elements)[index];
    float bx = static_cast<float>(w * D(n.base.x));
    float by = static_cast<float>(w * D(n.base.y));
    float bz = static_cast<float>(w * D(n.base.z));
    float px = static_cast<float>(u * D(n.pos.x));
    float py = static_cast<float>(u * D(n.pos.y));
    double pz = u * D(n.pos.z);
    p->velocity.x = static_cast<float>(D(px) + D(bx));
    p->velocity.y = static_cast<float>(D(py) + D(by));
    p->velocity.z = static_cast<float>(pz + D(bz));
    return 1;
}

// --- Modifier1-12 (0x201B-0x2026) ---------------------------------------------------------------

// Points scattered around the carrier (the source's elements).
struct ModifierPoint {
    Vector3 pos;                // +0x00 world position
    Vector3 start;              // +0x0C copy of pos
    Vector3 offset;             // +0x18 from the emission point
    std::uint8_t unused_24[0x10];
    int fade;                   // +0x34 point 0: index into the alpha table 0078A774
};
static_assert(sizeof(ModifierPoint) == sizeof(ParticleElement));

#define EI_TEN GameFloat(0x0073BAAC)               // 10.0
#define EI_ZERO GameFloat(0x0073B858)              // 0.0
#define EI_SMALL_RING GameFloat(0x0073CEA0)        // 1/23
#define EI_JITTER GameFloat(0x0073CE90)            // 0.04
#define EI_SCALE_0_08 GameDouble(0x0073CE98)       // ~0.08 * 2^-32

const std::int32_t* FadeAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A774); }
const char* NoCarrierMessage() { return reinterpret_cast<const char*>(0x0078AFA4); }

// 0044C470: the game's debug message (cdecl, one argument).
void __cdecl DebugMessage(const char* a0) { game::DebugLog(a0); }

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

bool LessOrUnordered(double a, double b) { return !(a >= b); }

// FCOM; TEST AH,0x40 not taken: neither equal nor unordered.
bool NonZero(double v) { return v < 0.0 || v > 0.0; }

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
        static_cast<const CObject3D*>(s->attachedTo)->orientation.AxisY(&y);
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
    for (int i = 0; i < s->elementCount; ++i) {
        reinterpret_cast<ModifierPoint*>(s->elements)[i].fade = 0;
        float x, y;
        double z, lengthSq;
        for (;;) {
            x = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
            y = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
            z = Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE);
            double dot = DotUnrounded<Sum3::ZYX, double>(axis.x, axis.y, axis.z, x, y, z);
            float px = static_cast<float>(D(axis.x) * dot);
            float py = static_cast<float>(D(axis.y) * dot);
            double pz = dot * D(axis.z);
            x = static_cast<float>(D(x) - D(px));
            y = static_cast<float>(D(y) - D(py));
            z = z - pz;
            lengthSq = LengthSquaredUnrounded<Sum3::ZYX, double>(x, y, z);
            if (NonZero(lengthSq)) break;
        }
        double scale = D(twice) / x87::fsqrt(lengthSq);
        if (LessOrUnordered(s->size, EI_ZERO)) scale = scale * D(EI_SMALL_RING);
        ModifierPoint& point = reinterpret_cast<ModifierPoint*>(s->elements)[i];
        float ex = static_cast<float>(scale * D(x));
        float ey = static_cast<float>(scale * D(y));
        float ez = static_cast<float>(scale * z);
        point.offset.x = ex;
        point.offset.y = ey;
        point.offset.z = ez;
        float wx = static_cast<float>(D(ex) + D(s->origin.x));
        float wy = static_cast<float>(D(ey) + D(s->origin.y));
        float wz = static_cast<float>(D(ez) + D(s->origin.z));
        point.pos.x = wx;
        point.pos.y = wy;
        point.pos.z = wz;
        point.pos.z = static_cast<float>(D(s->spreadMin.x) + D(point.pos.z));
        std::memcpy(&point.start, &point.pos, sizeof(Vector3));
    }
}

} // namespace

// 00497EB0. Room for `count` 0x38-byte elements, 32-byte aligned; the block's own address is kept
// just below the returned pointer (for operator_delete). __fastcall.
ParticleElement* __fastcall AllocateElements(int count) {
    auto* raw = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 0x38 + 0x24));
    auto* aligned = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(raw) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(aligned)[-1] = raw;
    return reinterpret_cast<ParticleElement*>(aligned);
}
EI_REPLACE(0x00497EB0, AllocateElements);

// 004923B0. LightningBlast: on the first emission the bolt's ten points are placed; then the
// particles run along the bolt (two per point), then bursts at the impact point (up to five, while
// the source has fewer than 200 particles), then sparks between the points.
int CParticleSource::EmitLightning(Particle* p, int) {
    if (elementCount == 0) {
        SetElementCount(this, 10);
        Vector3 dir;
        for (int i = 0; i < elementCount; ++i) PlaceBoltPoint(this, i, dir);
    }
    BoltPoint* points = reinterpret_cast<BoltPoint*>(elements);
    p->color = 0xFFFFFFFF;
    if (points[0].emitted < elementCount * 2) return EmitAlongBolt(this, p, points);
    p->state = 0;
    if (particles.count < 200 && points[0].bursts < 5) return EmitBurst(this, p, points);
    return EmitSpark(this, p);
}
EI_REPLACE_METHOD(0x004923B0, CParticleSource::EmitLightning);

// 00490130. Modifier1-12 (needs a carrier): on the first emission |size| * 10 points (at least
// one) are scattered on a ring around the carrier. Then one particle per point while the fade has
// not started (frame = the modifier's number), then drifting sparks between a random point and
// its starting place.
int CParticleSource::EmitModifier(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    if (elementCount == 0) {
        if (!attachedTo) {
            DebugMessage(NoCarrierMessage());
            return 0;
        }
        int count = static_cast<int>(gamecrt::Ftol(D(AbsBits(size)) * D(EI_TEN)));
        if (count < 1) count = 1;
        SetElementCount(this, count);
        PlaceModifierPoints(this);
    }
    ModifierPoint* points = reinterpret_cast<ModifierPoint*>(elements);
    p->color = (static_cast<std::uint32_t>(FadeAlpha()[points[0].fade]) << 24) | 0xFFFFFF;
    if (points[0].fade == 0 && index < elementCount) {
        unsigned kind = static_cast<unsigned>(type_ - 0x201B);
        if (kind <= 11) p->frame = static_cast<int>(kind);
        p->state = index;
        std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
        SetBits(&p->size, 0x3F800000);
        SetBits(&p->size, LessOrUnordered(size, EI_ZERO) ? 0x3E4CCCCDu : 0x3E000000u);   // 0.2 / 0.125
        p->life = 25;
        return 1;
    }

    std::uint32_t count = static_cast<std::uint32_t>(elementCount);
    std::uint32_t pick = GlobalRandom().Next() % count;
    float u = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_1);
    std::uint32_t frameRoll = GlobalRandom().Next();
    double w = D(EI_ONE) - D(u);
    p->life = 5;
    p->frame = static_cast<int>((frameRoll & 3) + 12);
    const ModifierPoint& e = reinterpret_cast<ModifierPoint*>(elements)[pick];
    float b0 = static_cast<float>(w * D(e.start.x));
    float b1 = static_cast<float>(w * D(e.start.y));
    double b2 = w * D(e.start.z);
    double a0 = D(u) * D(e.pos.x);
    float a1 = static_cast<float>(D(u) * D(e.pos.y));
    float a2 = static_cast<float>(D(u) * D(e.pos.z));
    p->velocity.x = static_cast<float>(a0 + D(b0));
    p->velocity.y = static_cast<float>(D(a1) + D(b1));
    p->velocity.z = static_cast<float>(D(a2) + b2);
    std::uint32_t ra = GlobalRandom().Next();
    std::uint32_t rb = GlobalRandom().Next();
    std::uint32_t rc = GlobalRandom().Next();
    float jx = static_cast<float>(Unsigned(rc) * EI_SCALE_0_08 - D(EI_JITTER));
    float jy = static_cast<float>(Unsigned(rb) * EI_SCALE_0_08 - D(EI_JITTER));
    float jz = static_cast<float>(Unsigned(ra) * EI_SCALE_0_08 - D(EI_JITTER));
    std::memcpy(&p->aux0, &jx, sizeof(float));
    std::memcpy(&p->aux1, &jy, sizeof(float));
    std::memcpy(&p->aux2, &jz, sizeof(float));
    std::memcpy(&p->pos, &reinterpret_cast<ModifierPoint*>(elements)[pick].start, sizeof(Vector3));
    SetBits(&p->size, 0x3D75C28F);   // 0.06
    return 1;
}
EI_REPLACE_METHOD(0x00490130, CParticleSource::EmitModifier);

// --- Smaller callbacks ----------------------------------------------------------------------------

namespace {

#define EI_ZONE_EXIT_SPREAD GameFloat(0x0073CF2C)  // 1.9
#define EI_SCALE_0_03 GameDouble(0x0073CE10)       // ~0.03 * 2^-32
#define EI_POINT_SEVEN GameFloat(0x0073C538)       // 0.7
#define EI_TWENTIETH GameFloat(0x0073C52C)         // 0.05
#define EI_SCALE_0_4 GameDouble(0x0073CCB0)        // ~0.4 * 2^-32
#define EI_ONE_AND_HALF GameFloat(0x0073C51C)      // 1.5
#define EI_ONE_POINT_TWO GameFloat(0x0073CF5C)     // 1.2
#define EI_PORTAL_GROWTH GameFloat(0x0073CDE8)     // 1.03
#define EI_EIGHT GameFloat(0x0073CC7C)             // 8.0
#define EI_TWO_HUNDRED GameFloat(0x0073CF58)       // 200.0
#define EI_POINT_EIGHT GameFloat(0x0073CCA8)       // 0.8
#define EI_SPARK_SCALE GameFloat(0x0073CF60)       // 0.55
#define EI_POINT_SIX GameFloat(0x0073CDE0)         // 0.6
#define EI_POINT_NINE GameFloat(0x0073CD70)        // 0.9
#define EI_PI GameFloat(0x0073CAF0)                // pi
#define EI_FEEBLE_SPEED GameDouble(0x0073CF90)     // 0.05
#define EI_SCALE_0_2 GameDouble(0x0073CED8)        // ~0.2 * 2^-32
#define EI_FEEBLE_SIZE GameFloat(0x0073CF8C)       // 0.035
#define EI_FEET_DENSITY GameFloat(0x0073CE50)      // 8.0
#define EI_FEET_RISE GameFloat(0x0073CDF4)         // 1.2
#define EI_BYTE GameFloat(0x0073C370)              // 255.0
#define EI_DUST_GROWTH GameFloat(0x0073CEE0)       // 1.05
#define EI_TRANS_GROWTH GameFloat(0x0073CCD8)      // 0.02

// Alpha tables indexed by the ticks left.
const std::int32_t* SparkAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078AB98); }
const float* GlowAlpha() { return reinterpret_cast<const float*>(0x0078AA78); }
const std::int32_t* DustAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078AAD8); }

// 00561150 on the map object at 007AF78C: the ground plane at (x, y), as a normal and a distance.
void* PlaneMap() { return reinterpret_cast<void*>(0x007AF78C); }

// x87 select through FCOMP; SHL/SAR of the status word: `a` when a < b (or unordered), else `b`.
float SelectLess(float a, float b, float ifLess, float otherwise) {
    return LessOrUnordered(a, b) ? ifLess : otherwise;
}

// One random draw in [-1, 1) through the ISAAC results: Next() * ~2^-31 - 1.
double Signed(std::uint32_t roll) { return Unsigned(roll) * EI_SCALE_2 - D(EI_ONE); }

} // namespace

// 00492D30. ZoneExit: the density follows alpha * +0x11C (at most 40 a tick); particles start at a
// random point of an ellipse around the source (axes +0x118 * +0x120 and +0x124 * +0x11C) on the
// ground, red-tinted when +0x130 is set, rising by an amount that is larger near the centre.
int CParticleSource::EmitZoneExit(Particle* p, int) {
    int count = x87::fistp(static_cast<float>(D(alpha) * D(typeParam)));
    if (count > 40) count = 40;
    emitCount = count;
    p->color = NonZero(extent.x) ? 0x00FF6060u : 0x00FFFFFFu;
    p->frame = static_cast<int>(GlobalRandom().Next() & 15);
    p->life = 15;
    p->frameTimer = 1;
    p->state = 0;
    float a = static_cast<float>(Signed(GlobalRandom().Next()));
    double b = Signed(GlobalRandom().Next());
    float falloff = static_cast<float>((D(EI_ZONE_EXIT_SPREAD) - D(a) * D(a)) - b * b);
    float x = static_cast<float>((D(a) * D(alpha) * D(turnCos) + D(origin.x)) - b * D(turnSin) * D(typeParam));
    float y = static_cast<float>((D(turnSin) * D(alpha) * D(a) + b * D(turnCos) * D(typeParam)) + D(origin.y));
    double ground = WorldClient()->ClientRawSurfaceHeight(x, y);
    p->pos.x = x;
    p->pos.y = y;
    std::memset(&p->size, 0, sizeof(float));
    p->pos.z = static_cast<float>(ground + D(EI_TENTH));
    double roll = Unsigned(GlobalRandom().Next()) * EI_SCALE_0_03 + D(EI_POINT_SEVEN);
    p->velocity.x = static_cast<float>(roll * D(falloff) * D(EI_TWENTIETH));
    return 1;
}
EI_REPLACE_METHOD(0x00492D30, CParticleSource::EmitZoneExit);

// 00493280. 0x2039: on its first update (life -10) the particle's lifetime is set from its
// position in the emission (frameTimer * 8 / particle count + 7); the alpha follows the table at
// 0078AA78.
int CParticleSource::UpdateGlow(Particle* p) {
    if (p->life == -10) p->life = p->frameTimer * 8 / particles.count + 7;
    if (--p->life < 0) return 0;
    int opacity = x87::fistp(static_cast<float>(D(GlowAlpha()[p->life]) * D(EI_BYTE)));
    p->color = (p->color & 0xFFFFFF) + (static_cast<std::uint32_t>(opacity) << 24);
    return 1;
}
EI_REPLACE_METHOD(0x00493280, CParticleSource::UpdateGlow);

// 004933F0. 0x203A/0x203B: dust moving along the ground, never below the plane the source
// measured (element 0: distance +0x0C, normal +0x18), growing and slowing down.
int CParticleSource::UpdateGroundDust(Particle* p) {
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(DustAlpha()[p->life]) << 24) + (p->color & 0xFFFFFF);
    p->pos.x = static_cast<float>(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = static_cast<float>(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = static_cast<float>(D(p->velocity.z) + D(p->pos.z));
    double lifted = WorldClient()->ClientRawSurfaceHeight(p->pos.x, p->pos.y) + D(EI_TENTH);
    p->pos.z = static_cast<float>(lifted);
    const float* plane = reinterpret_cast<const float*>(elements);
    double along = (D(p->pos.x) - D(origin.x)) * D(plane[6]) + (D(p->pos.y) - D(origin.y)) * D(plane[7]);
    float onPlane = static_cast<float>(D(plane[3]) - along * D(plane[8]) + D(EI_TENTH));
    float ground = static_cast<float>(lifted);
    p->pos.z = SelectLess(ground, onPlane, onPlane, ground);
    std::memcpy(&p->size, &p->aux0, sizeof(float));
    p->velocity.x = static_cast<float>(D(p->velocity.x) * D(EI_DUST_GROWTH));
    p->velocity.y = static_cast<float>(D(p->velocity.y) * D(EI_DUST_GROWTH));
    p->velocity.z = static_cast<float>(D(p->velocity.z) * D(EI_DUST_GROWTH));
    p->aux0 = static_cast<float>(D(p->aux1) * D(p->aux0));
    return 1;
}
EI_REPLACE_METHOD(0x004933F0, CParticleSource::UpdateGroundDust);

// 004934F0. 0x203A/0x203B: on the element's first tick, the ground plane under the source (the
// normal's z replaced by its reciprocal when non-zero); after the second the source stops
// emitting.
void CParticleSource::TickGroundPlane(ParticleElement* e) {
    if (e->ticks == 0) {
        float* plane = reinterpret_cast<float*>(e);
        static_cast<PlatformSet*>(PlaneMap())->SurfacePlane(origin.x, origin.y, reinterpret_cast<Vector3*>(plane + 6), plane + 3);
        if (NonZero(plane[8])) plane[8] = static_cast<float>(D(EI_ONE) / D(plane[8]));
    }
    if (++e->ticks > 1) flags_ &= ~1u;
}
EI_REPLACE_METHOD(0x004934F0, CParticleSource::TickGroundPlane);

// 00494770. Portal: particles spiral up (their offset rotated by the turn in +0x20/+0x24 each
// tick) and fade above 8 units; the spare ones (state set) flicker in size while the portal is on.
int CParticleSource::UpdatePortal(Particle* p) {
    if (p->state != 0) {
        if (!(flags_ & 1)) return 0;
        if (LessOrUnordered(size, EI_ONE)) {
            std::memset(&p->size, 0, sizeof(float));
            return 1;
        }
        p->size = static_cast<float>(Unsigned(GlobalRandom().Next()) * EI_SCALE_0_4 * D(EI_ONE_AND_HALF) + D(EI_ONE_POINT_TWO));
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
EI_REPLACE_METHOD(0x00494770, CParticleSource::UpdatePortal);

// 00494930. Portal: |size| * 10 tries per tick (truncated by the game's _ftol).
void CParticleSource::TickPortal(ParticleElement*) { emitCount = static_cast<int>(gamecrt::Ftol(D(size) * D(EI_TEN))); }
EI_REPLACE_METHOD(0x00494930, CParticleSource::TickPortal);

// 00494CD0. Sparks: the spare ones (state set) get a random size once and are dropped after; the
// rest fly along their drift, shrinking and slowing, with the alpha from the table at 0078AB98.
int CParticleSource::UpdateSparks(Particle* p) {
    if (p->state != 0) {
        if (NonZero(p->size)) {
            std::memset(&p->size, 0, sizeof(float));
            return 0;
        }
        p->size = static_cast<float>((Unsigned(GlobalRandom().Next()) * EI_SCALE_0_4 + D(EI_POINT_EIGHT)) * D(size) *
                        D(EI_SPARK_SCALE));
        return 1;
    }
    if (--p->life < 0) return 0;
    p->color = (static_cast<std::uint32_t>(SparkAlpha()[p->life]) << 24) | 0xFFFFFF;
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
EI_REPLACE_METHOD(0x00494CD0, CParticleSource::UpdateSparks);

// 004957E0. FeebleMind: size * 2 tries per tick; each particle starts one step out from the
// source in a random horizontal direction.
int CParticleSource::EmitFeebleMind(Particle* p, int) {
    emitCount = x87::fistp(static_cast<float>(D(size) + D(size)));
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(GlobalRandom().Next() & 15);
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    p->life = 20;
    float twoPi = static_cast<float>(D(EI_PI) + D(EI_PI));
    std::uint32_t roll = GlobalRandom().Next();
    std::memset(&p->velocity.z, 0, sizeof(float));
    double angle = Unsigned(roll) * D(twoPi) * EI_SCALE_1;
    p->velocity.x = static_cast<float>(x87::fcos(angle) * EI_FEEBLE_SPEED);
    p->velocity.y = static_cast<float>(x87::fsin(angle) * EI_FEEBLE_SPEED);
    p->pos.x = static_cast<float>(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = static_cast<float>(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = static_cast<float>(D(p->velocity.z) + D(p->pos.z));
    float grow = static_cast<float>((Unsigned(GlobalRandom().Next()) * EI_SCALE_0_2 + D(EI_POINT_NINE)) * D(size) *
                       D(EI_FEEBLE_SIZE));
    std::memcpy(&p->aux2, &grow, sizeof(float));
    return 1;
}
EI_REPLACE_METHOD(0x004957E0, CParticleSource::EmitFeebleMind);

// 00495A30. FeetCloud: size * 8 tries per tick; a puff at one of the two feet (the element's
// position once it is tracked, else the source), drifting in a random direction inside the unit
// disc, rising.
int CParticleSource::EmitFeetCloud(Particle* p, int) {
    emitCount = x87::fistp(static_cast<float>(D(size) * D(EI_FEET_DENSITY)));
    p->color = 0x00FFFFFF;
    p->frame = static_cast<int>(GlobalRandom().Next() & 15);
    int foot = static_cast<int>(GlobalRandom().Next() & 1);
    p->state = foot;
    const ParticleElement& e = elements[foot];
    if (reinterpret_cast<const std::int32_t*>(&e)[12] >= 0) std::memcpy(&p->pos, &e.pos, sizeof(Vector3));
    else std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    std::memset(&p->size, 0, sizeof(float));
    float x, y;
    for (int tries = 10;;) {
        x = static_cast<float>(Signed(GlobalRandom().Next()));
        double wide = Signed(GlobalRandom().Next());
        y = static_cast<float>(wide);
        if (LessOrUnordered(wide * D(y) + D(x) * D(x), EI_ONE)) break;
        if (!(--tries > 0)) break;
    }
    float scale = e.value[3];          // +0x18
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
    p->velocity.x = static_cast<float>(D(size) * D(e.value[3]) * D(EI_POINT_FOUR));
    p->pos.z = static_cast<float>(D(p->velocity.x) * D(EI_FEET_RISE) + D(p->pos.z));
    return 1;
}
EI_REPLACE_METHOD(0x00495A30, CParticleSource::EmitFeetCloud);

// 00496990. StartTrans: once the element's countdown runs out the source stops (flags 1 and 4
// off) and detaches; the ring widens, and element 0's +0x1C grows by 0.4 a tick and sets the tries
// per tick (at most 8).
void CParticleSource::TickStartTrans(ParticleElement* e) {
    int ticks = e->ticks--;
    if (ticks < 0) {
        flags_ &= ~5u;
        Attach(nullptr);
    }
    e->value[3] = static_cast<float>(D(size) * D(EI_TRANS_GROWTH) + D(e->value[3]));
    elements[0].value[4] = static_cast<float>(D(elements[0].value[4]) + D(EI_POINT_FOUR));
    int count = x87::fistp(elements[0].value[4]);
    emitCount = count;
    if (!(count < 8)) emitCount = 8;
}
EI_REPLACE_METHOD(0x00496990, CParticleSource::TickStartTrans);
