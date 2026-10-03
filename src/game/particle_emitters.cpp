// Particle emit callbacks (+0x1E0 of a CParticleSource) in 0x00490000-0x00497000 and the element
// array allocator. Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the order
// of operations and the NaN compares stay as written.
#include "effects.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <bit>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// Constants the original reads from memory at run time.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// 00488970 IsaacRandom::Range and 00564070 TerrainOwner::SurfaceHeight leave their result
// unrounded in ST0.
__declspec(naked) double __fastcall RandomRange(IsaacRandom*, void*, float /*min*/, float /*max*/) EI_ORIGINAL(0x00488970)
__declspec(naked) double __fastcall SurfaceHeight(void* /*owner*/, void*, float /*x*/, float /*y*/) EI_ORIGINAL(0x00564070)
void* TerrainOwner() { return reinterpret_cast<void*>(0x007AF5B8); }

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
    std::uint8_t unknown_24[0xC];
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
    float radius = Store(Range(0.0f, 1.0f));
    double angle = Range(0.0f, kTwoPi);
    float dx = Store(x87::fsin(angle) * D(radius));
    float dy = Store(x87::fcos(angle) * D(radius));
    dir.x = dx;
    dir.y = dy;
    std::memset(&dir.z, 0, sizeof(float));
    if (i == 0) std::memset(&dir, 0, sizeof(dir));
    double size = s->size;
    float ax = Store(size * D(dir.x));
    float ay = Store(D(dir.y) * size);
    float az = Store(size * D(dir.z));
    float bx = Store(D(ax) * D(EI_POINT_THREE));
    float by = Store(D(ay) * D(EI_POINT_THREE));
    float bz = Store(D(az) * D(EI_POINT_THREE));
    float cx = Store(D(bx) + D(s->origin.x));
    float cy = Store(D(by) + D(s->origin.y));
    (void)Store(D(bz) + D(s->origin.z));
    float cz = Store(SurfaceHeight(TerrainOwner(), nullptr, cx, cy) + D(EI_POINT_THREE));
    BoltPoint* points = reinterpret_cast<BoltPoint*>(s->elements);
    points[i].pos = {cx, cy, cz};
    points[i].base = {cx, cy, cz};

    double inverse = D(EI_ONE) / x87::fsqrt(D(dir.y) * D(dir.y) + D(dir.x) * D(dir.x) + D(EI_QUARTER));
    dir.x = Store(inverse * D(dir.x));
    dir.y = Store(D(dir.y) * inverse);
    double halfInverse = inverse * D(EI_HALF);
    dir.z = Store(halfInverse);
    float fz = Store(halfInverse * D(EI_HALF));
    float fy = Store(D(dir.y) * D(EI_HALF));
    float fx = Store(D(dir.x) * D(EI_HALF));
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
    p->unknown_2C = std::bit_cast<float>(0x3DCCCCCDu);     // 0.1
    ++points[0].emitted;
    if (p->frameTimer == 0) return 1;
    std::uint32_t roll = GlobalRandom().Next();
    p->unknown_2C = Store((Unsigned(roll) * EI_SCALE_0_9 + D(EI_HALF)) * D(s->size) * D(EI_POINT_TWO));
    p->frame = static_cast<int>((GlobalRandom().Next() & 3) + 12);
    if (index == 0) p->unknown_2C = Store(D(s->size) * D(EI_POINT_FOUR));
    std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
    return 1;
}

// A burst at the impact point: a random direction inside the unit disc (ten tries), spread by a
// random angle and distance.
int EmitBurst(CParticleSource* s, Particle* p, BoltPoint* points) {
    p->color = 0x40FFFFFF;
    p->frame = 8;
    GlobalRandom().Next();
    double u = Unsigned(GlobalRandom().Next()) * EI_SCALE_1;
    float spread = Store(D(EI_ONE) - u * u);
    float angle = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_ANGLE);
    float vx, vy;
    for (int tries = 10;;) {
        vx = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
        double y = Range(-1.0f, 1.0f);
        vy = Store(y);
        if (!(y * D(vy) + D(vx) * D(vx) >= D(EI_ONE))) break;
        if (!(--tries > 0)) break;
    }
    p->drift = {vx, vy, 0.0f};
    std::memset(&p->drift.z, 0, sizeof(float));
    p->drift.x = Store(x87::fcos(angle) * D(spread));
    p->drift.y = Store(x87::fsin(angle) * D(spread));
    double scale = D(s->size) * D(EI_POINT_THREE);
    p->drift.x = Store(scale * D(p->drift.x));
    p->drift.y = Store(scale * D(p->drift.y));
    p->drift.z = Store(scale * D(p->drift.z));
    std::memset(&p->drift.z, 0, sizeof(float));
    p->pos.x = Store(D(points[0].pos.x) + D(p->drift.x));
    p->pos.y = Store(D(points[0].pos.y) + D(p->drift.y));
    double rise = (D(EI_ONE) - D(spread)) * D(s->size) * D(EI_TENTH) + D(points[0].pos.z);
    std::memset(&p->size, 0, sizeof(float));
    p->pos.z = Store(rise);
    p->frameTimer = 0;
    p->life = x87::fistp(Store(D(EI_NINE) - D(spread) * D(EI_THREE)));
    double roll = Unsigned(GlobalRandom().Next()) * EI_SCALE_0_04 + D(EI_TENTH);
    p->unknown_2C = Store(roll * (D(EI_TWO) - D(spread)) * D(s->size));
    return 1;
}

// A spark between a random point and its base, at a random fraction of the way.
int EmitSpark(CParticleSource* s, Particle* p) {
    std::uint32_t count = static_cast<std::uint32_t>(s->elementCount);
    std::uint32_t index = GlobalRandom().Next() % count;
    p->frame = 1;
    p->unknown_2C = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_0_01 * D(EI_THREE) + D(EI_SPARK_SIZE));
    p->life = 6;
    p->frameTimer = 1;
    double u = Unsigned(GlobalRandom().Next()) * EI_SCALE_1;
    double w = D(EI_ONE) - u;
    BoltPoint* points = reinterpret_cast<BoltPoint*>(s->elements);
    std::memcpy(&p->pos, &points[index].pos, sizeof(Vector3));
    std::memset(&p->size, 0, sizeof(float));
    const BoltPoint& n = reinterpret_cast<BoltPoint*>(s->elements)[index];
    float bx = Store(w * D(n.base.x));
    float by = Store(w * D(n.base.y));
    float bz = Store(w * D(n.base.z));
    float px = Store(u * D(n.pos.x));
    float py = Store(u * D(n.pos.y));
    double pz = u * D(n.pos.z);
    p->velocity.x = Store(D(px) + D(bx));
    p->velocity.y = Store(D(py) + D(by));
    p->velocity.z = Store(pz + D(bz));
    return 1;
}

// --- Modifier1-12 (0x201B-0x2026) ---------------------------------------------------------------

// Points scattered around the carrier (the source's elements).
struct ModifierPoint {
    Vector3 pos;                // +0x00 world position
    Vector3 start;              // +0x0C copy of pos
    Vector3 offset;             // +0x18 from the emission point
    std::uint8_t unknown_24[0x10];
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
__declspec(naked) void __cdecl DebugMessage(const char*) EI_ORIGINAL(0x0044C470)

// The game's _ftol (006E853C): truncation.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

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
        s->spreadMin.x = Store(D(twiceRadius) * D(EI_POINT_FOUR) + D(s->spreadMin.x));
        SetBits(&axis.z, 0x3F800000);
        break;
    case 3:
        s->spreadMin.x = Store(D(s->spreadMin.x) - D(twiceRadius) * D(EI_POINT_FOUR));
        SetBits(&axis.z, 0x3F800000);
        break;
    case 4:
    case 5:
    case 6:
    case 7: {
        Vector3 y;
        reinterpret_cast<const Quaternion*>(static_cast<const std::uint8_t*>(s->attachedTo) + 0x34)->AxisY(&y);
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
    float twice = Store(D(radius) + D(radius));
    Vector3 axis = ModifierAxis(s, twice);
    for (int i = 0; i < s->elementCount; ++i) {
        reinterpret_cast<ModifierPoint*>(s->elements)[i].fade = 0;
        float x, y;
        double z, lengthSq;
        for (;;) {
            x = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
            y = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
            z = Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE);
            double dot = (D(axis.z) * z + D(axis.y) * D(y)) + D(axis.x) * D(x);
            float px = Store(D(axis.x) * dot);
            float py = Store(D(axis.y) * dot);
            double pz = dot * D(axis.z);
            x = Store(D(x) - D(px));
            y = Store(D(y) - D(py));
            z = z - pz;
            lengthSq = (z * z + D(y) * D(y)) + D(x) * D(x);
            if (NonZero(lengthSq)) break;
        }
        double scale = D(twice) / x87::fsqrt(lengthSq);
        if (LessOrUnordered(s->size, EI_ZERO)) scale = scale * D(EI_SMALL_RING);
        ModifierPoint& point = reinterpret_cast<ModifierPoint*>(s->elements)[i];
        float ex = Store(scale * D(x));
        float ey = Store(scale * D(y));
        float ez = Store(scale * z);
        point.offset.x = ex;
        point.offset.y = ey;
        point.offset.z = ez;
        float wx = Store(D(ex) + D(s->origin.x));
        float wy = Store(D(ey) + D(s->origin.y));
        float wz = Store(D(ez) + D(s->origin.z));
        point.pos.x = wx;
        point.pos.y = wy;
        point.pos.z = wz;
        point.pos.z = Store(D(s->spreadMin.x) + D(point.pos.z));
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

__declspec(naked) Vector3* CParticleSource::AttachmentOffset(Vector3*, float*) EI_ORIGINAL(0x00485280)

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
        int count = GameFtol(D(AbsBits(size)) * D(EI_TEN));
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
    float u = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_1);
    std::uint32_t frameRoll = GlobalRandom().Next();
    double w = D(EI_ONE) - D(u);
    p->life = 5;
    p->frame = static_cast<int>((frameRoll & 3) + 12);
    const ModifierPoint& e = reinterpret_cast<ModifierPoint*>(elements)[pick];
    float b0 = Store(w * D(e.start.x));
    float b1 = Store(w * D(e.start.y));
    double b2 = w * D(e.start.z);
    double a0 = D(u) * D(e.pos.x);
    float a1 = Store(D(u) * D(e.pos.y));
    float a2 = Store(D(u) * D(e.pos.z));
    p->velocity.x = Store(a0 + D(b0));
    p->velocity.y = Store(D(a1) + D(b1));
    p->velocity.z = Store(D(a2) + b2);
    std::uint32_t ra = GlobalRandom().Next();
    std::uint32_t rb = GlobalRandom().Next();
    std::uint32_t rc = GlobalRandom().Next();
    float jx = Store(Unsigned(rc) * EI_SCALE_0_08 - D(EI_JITTER));
    float jy = Store(Unsigned(rb) * EI_SCALE_0_08 - D(EI_JITTER));
    float jz = Store(Unsigned(ra) * EI_SCALE_0_08 - D(EI_JITTER));
    std::memcpy(&p->unknown_2C, &jx, sizeof(float));
    std::memcpy(&p->unknown_30, &jy, sizeof(float));
    std::memcpy(&p->unknown_34, &jz, sizeof(float));
    std::memcpy(&p->pos, &reinterpret_cast<ModifierPoint*>(elements)[pick].start, sizeof(Vector3));
    SetBits(&p->size, 0x3D75C28F);   // 0.06
    return 1;
}
EI_REPLACE_METHOD(0x00490130, CParticleSource::EmitModifier);
