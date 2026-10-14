// CParticleSource per-type callbacks (+0x1E0 emit, +0x1E4 update, +0x1E8 element) not covered by
// effects.cpp / particle_emitters.cpp. Names are provisional; each notes its original address
// and the types (see Create, particle_create.cpp) that use it.
#include "particle_common.h"

#include "core_maths.h"
#include "map_object_figures.h"
#include "object_maths.h"
#include "unit.h"

#include "lib/vecmath.h"

#include <concepts>

#pragma float_control(precise, on)

using namespace particle;

// --- Casting effects (0x2027-0x202A): sparks rising around the caster -----------------------------
//
// Element 0 holds the effect's state: +0x00 1.0 once set up, +0x0C the particle growth, +0x18 the
// radius, +0x1C the particle size, +0x30 the alpha, +0x34 a countdown (the alpha follows the
// table at 0078A964 by it). Elements 1..lifetime are swirls: +0x18 a position drifting by +0x24.

namespace {

#define EI_POINT_EIGHT GameFloat(0x0073CCA8)        // 0.8
#define EI_THREE GameFloat(0x0073BAA4)              // 3.0
#define EI_TEN GameFloat(0x0073CB98)                // 10.0
#define EI_TENTH GameFloat(0x0073CB9C)              // 0.1
#define EI_POINT_SIX GameFloat(0x0073CDE0)          // 0.6
#define EI_THIRTEENTH GameFloat(0x0073CEE4)         // 1/13
#define EI_TWENTY_SIXTH GameFloat(0x0073CEE8)       // 1/26
#define EI_SCALE_0_4_HI GameDouble(0x0073CEB0)      // ~0.8 * 2^-32
#define EI_CAST_DRIFT GameFloat(0x0073CBA0)         // the swirl's random acceleration bound
#define EI_CAST_DAMP GameFloat(0x0073CD50)          // velocity kept per tick
#define EI_CAST_PULL GameFloat(0x0073CEF8)          // pull back to the centre
#define EI_SCALE_SIZE GameDouble(0x0073CEF0)        // size jitter scale
#define EI_SIZE_BASE GameFloat(0x0073CEB8)
#define EI_POINT_THREE GameFloat(0x0073C54C)        // 0.3

const char* NoCarrierCasting() { return reinterpret_cast<const char*>(0x0078AFD0); }
const std::int32_t* CastAlphaByCountdown() { return reinterpret_cast<const std::int32_t*>(0x0078A964); }
const std::int32_t* CastAlphaByLifeWide() { return reinterpret_cast<const std::int32_t*>(0x0078A980); }   // 0x202A
const std::int32_t* CastAlphaByLife() { return reinterpret_cast<const std::int32_t*>(0x0078A9BC); }

// A random point in the unit ball (ten tries; the last one is kept), as the set-up draws it.
void RandomInBall(float& x, float& y, float& z) {
    for (int tries = 10;;) {
        x = SignedUnit();
        y = Store(Range(-1.0f, 1.0f));
        double zu = Range(-1.0f, 1.0f);
        z = Store(zu);
        double lengthSquared = zu * D(z) + D(x) * D(x) + D(y) * D(y);
        if (!(lengthSquared >= D(EI_P_ONE))) return;
        if (!(--tries > 0)) return;
    }
}

// The first emission: element 0 from the carrier's size, elements 1..lifetime random swirls.
// 0 (and a debug message) without a carrier.
bool SetUpCasting(CParticleSource* s) {
    if (!s->attachedTo) {
        DebugMessage(NoCarrierCasting());
        return false;
    }
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    radius = Store(D(radius) * D(EI_POINT_EIGHT));
    double scale = ClassOf(s->attachedTo) == kUnitClass ? UnitScale(s->attachedTo) : D(radius);
    if (s->type_ == 0x202A) scale = scale + scale;
    else scale = scale * D(EI_THREE);
    float extent = Store(scale);
    SetElementCount(s, s->lifetime + 1);
    ParticleElement* e = s->elements;
    *ElementInt(e, 0x30) = 0xFF;
    *ElementInt(e, 0x34) = 6;
    *ElementFloat(e, 0x18) = extent;
    if (s->type_ == 0x202A) {
        SetBits(ElementFloat(e, 0x1C), 0x3D8F5C29);     // 0.07
        SetBits(ElementFloat(e, 0x0C), 0x3F866666);     // 1.05
    } else {
        SetBits(ElementFloat(e, 0x1C), 0x3DA3D70A);     // 0.08
        SetBits(ElementFloat(e, 0x0C), 0x3F88F5C3);     // 1.07
    }
    SetBits(ElementFloat(e, 0x00), 0x3F800000);
    for (int i = 1; i <= s->lifetime; ++i) {
        float x, y, z;
        RandomInBall(x, y, z);
        Vector3 a;
        a.Construct(x, y, z);
        float* swirl = ElementFloat(&s->elements[i], 0x18);
        swirl[0] = Store(D(a.x) * D(EI_TEN));
        swirl[1] = Store(D(a.y) * D(EI_TEN));
        swirl[2] = Store(D(a.z) * D(EI_TEN));
        RandomInBall(x, y, z);
        Vector3 b;
        b.Construct(x, y, z);
        float* drift = ElementFloat(&s->elements[i], 0x24);
        drift[0] = Store(D(b.x) * D(EI_TENTH));
        drift[1] = Store(D(b.y) * D(EI_TENTH));
        drift[2] = Store(D(b.z) * D(EI_TENTH));
        SetBits(ElementFloat(&s->elements[i], 0x00), 0);
    }
    return true;
}

} // namespace

// 004918E0. Casting (0x2027-0x202A). The first call sets the effect up (0x202A then emits its
// marker particle at the source); after that each particle starts on a random direction (pulled
// towards a random swirl), at the effect's radius around the source.
int CParticleSource::EmitCasting(Particle* p, int) {
    if (elementCount == 0) {
        if (!SetUpCasting(this)) return 0;
        if (type_ == 0x202A) {
            p->color = 0xFFFFFF;
            p->frame = 0;
            p->state = 1;
            std::memcpy(&p->pos, &origin, sizeof(Vector3));
            SetBits(&p->size, 0);
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
        double zu = Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE);
        z = Store(zu);
        double sum = zu * D(z) + D(y) * D(y) + D(x) * D(x);
        lengthSquared = Store(sum);
        if (sum > D(EI_P_ONE)) continue;
        if (!(D(lengthSquared) > D(EI_P_ZERO))) continue;
        break;
    }
    std::uint32_t span = static_cast<std::uint32_t>(frameDelay * lifetime + 1);
    int pick = static_cast<int>(Next() % span);
    if (pick > 0) {
        const ParticleElement& swirl = elements[(pick - 1) % lifetime];
        const float* toward = &swirl.value[3];
        x = Store(D(x) + D(toward[0]));
        y = Store(D(y) + D(toward[1]));
        double zu = D(z) + D(toward[2]);
        z = Store(zu);
        lengthSquared = Store(zu * D(z) + D(y) * D(y) + D(x) * D(x));
    }
    double inverse = D(EI_P_ONE) / x87::fsqrt(D(lengthSquared));
    p->velocity.x = Store(inverse * D(x));
    p->velocity.y = Store(inverse * D(y));
    p->velocity.z = Store(inverse * D(z));
    ParticleElement* state = elements;
    double reach = D(*ElementFloat(state, 0x18)) * D(EI_POINT_SIX);
    p->unknown_2C = Store(reach);
    p->unknown_30 = Store(reach * D(type_ == 0x202A ? EI_TWENTY_SIXTH : EI_THIRTEENTH));
    std::memcpy(&p->unknown_34, ElementFloat(state, 0x0C), sizeof(float));
    double r = D(p->unknown_2C);
    float ox = Store(r * D(p->velocity.x));
    float oy = Store(r * D(p->velocity.y));
    double oz = r * D(p->velocity.z);
    double px = D(ox) + D(origin.x);
    float py = Store(D(oy) + D(origin.y));
    float pz = Store(oz + D(origin.z));
    SetBits(&p->size, 0x3F800000);
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store((Unsigned(Next()) * EI_SCALE_0_4_HI + D(EI_POINT_SIX)) * D(*ElementFloat(state, 0x1C)) *
                    D(p->unknown_2C));
    return 1;
}
EI_REPLACE_METHOD(0x004918E0, CParticleSource::EmitCasting);

// 00492030. Casting particles: none once the effect's alpha is 0. The 0x202A marker stays at the
// source with a random size; the others fade by their life (capped by the effect's alpha), run
// their frames backwards (0x2027: every fourth tick held) and spiral in towards the source.
int CParticleSource::UpdateCasting(Particle* p) {
    ParticleElement* state = elements;
    if (*ElementInt(state, 0x30) == 0) return 0;
    if (p->state) {
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
        SetBits(&p->size, 0x3F800000);
        p->size = Store((Unsigned(Next()) * EI_SCALE_SIZE + D(EI_SIZE_BASE)) * D(*ElementFloat(elements, 0x18)) *
                        D(EI_POINT_THREE));
        p->color = (static_cast<std::uint32_t>(*ElementInt(elements, 0x30)) << 24) | 0xFFFFFF;
        return 1;
    }
    int life = --p->life;
    if (life == 0) return 0;
    std::int32_t fade = (type_ == 0x202A ? CastAlphaByLifeWide() : CastAlphaByLife())[life];
    std::int32_t cap = *ElementInt(elements, 0x30);
    if (cap < fade) fade = cap;
    p->color = (static_cast<std::uint32_t>(fade) << 24) | 0xFFFFFF;
    if (type_ == 0x2027 && (life & 3) == 0) ++p->frame;
    double oldSize = D(p->size);
    double reach = D(p->unknown_2C) - D(p->unknown_30);
    --p->frame;
    p->unknown_2C = Store(reach);
    float ox = Store(reach * D(p->velocity.x));
    float oy = Store(reach * D(p->velocity.y));
    double oz = reach * D(p->velocity.z);
    double px = D(ox) + D(origin.x);
    float py = Store(D(oy) + D(origin.y));
    float pz = Store(oz + D(origin.z));
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store(oldSize * D(*ParticleFloat(p, 0x34)));
    return 1;
}
EI_REPLACE_METHOD(0x00492030, CParticleSource::UpdateCasting);

// 004921D0. Casting elements: element 0 (+0x00 nonzero) counts down and takes its alpha from the
// table (frozen while an emitting source is attached); the swirls (+0x00 zero, or NaN) move by
// their velocity, which gets a random kick in [-k, k), damping and a pull back to the centre.
void CParticleSource::TickCasting(ParticleElement* e) {
    float* f = ElementFloat(e, 0);
    if (!(f[0] == 0.0f || x87::IsNaN(f[0]))) {
        if ((flags_ & 1) && attachedTo) return;
        if (e->ticks > 0) --e->ticks;
        *ElementInt(e, 0x30) = CastAlphaByCountdown()[*ElementInt(elements, 0x34)];
        return;
    }
    float* pos = ElementFloat(e, 0x18);
    float* velocity = ElementFloat(e, 0x24);
    for (int i = 0; i < 3; ++i) pos[i] = Store(D(velocity[i]) + D(pos[i]));
    float bound = EI_CAST_DRIFT;
    float low = Store(-D(bound));
    for (int i = 0; i < 3; ++i) {
        double kick = Unsigned(Next()) * (D(bound) - D(low)) * EI_P_SCALE_1;
        velocity[i] = Store(kick + D(velocity[i]) * D(EI_CAST_DAMP) + D(low) - D(pos[i]) * D(EI_CAST_PULL));
    }
}
EI_REPLACE_METHOD(0x004921D0, CParticleSource::TickCasting);

// --- Protection spheres (0x2015-0x2017) and AntiMagic (0x201A) -----------------------------------
//
// Element 0 follows the carrier: +0x08 the height above it, +0x18 the sphere's radius, +0x20 the
// speed of the carrier (whose smoothed motion is element 1's position).

namespace {

#define EI_SPHERE_RADIUS GameFloat(0x0073CDF4)      // 1.2 (of the carrier's radius)
#define EI_KEEP_0_9 GameFloat(0x0073CD70)           // 0.9
#define EI_TAKE_0_1 GameFloat(0x0073B850)           // 0.1
#define EI_ANTIMAGIC_RATE GameFloat(0x0073CE28)     // 24.0
#define EI_SPHERE_RATE GameFloat(0x0073BA98)        // 6.0
#define EI_MOTION_KEEP GameFloat(0x0073C538)        // 0.7
#define EI_STILL_SPEED GameDouble(0x0073CE20)       // 0.01
#define EI_TILT_ANGLE GameDouble(0x0073CE30)        // 0.01
#define EI_BIG_ANTIMAGIC GameFloat(0x0073CE18)      // 2.5
#define EI_BIG_EARTH GameFloat(0x0073C534)          // 3.5
#define EI_BIG_SPHERE GameFloat(0x0073C51C)         // 1.5
#define EI_SCALE_0_03 GameDouble(0x0073CE10)        // ~0.03 * 2^-32
#define EI_SHELL GameFloat(0x0073CE08)              // 0.65
#define EI_AIR_SIZE GameFloat(0x0073CC80)           // 1.6

const char* NoCarrierSphere() { return reinterpret_cast<const char*>(0x0078AF7C); }
const float* SphereSizeByLife() { return reinterpret_cast<const float*>(0x0073CB38); }
const float* AntimagicSizeByLife() { return reinterpret_cast<const float*>(0x0073CB78); }

// A random point in the unit ball (ten tries), the squared length summed z, y, x.
void RandomInBallZYX(float& x, float& y, float& z) {
    for (int tries = 10;;) {
        x = SignedUnit();
        y = Store(Range(-1.0f, 1.0f));
        double zu = Range(-1.0f, 1.0f);
        z = Store(zu);
        double lengthSquared = zu * D(z) + D(y) * D(y) + D(x) * D(x);
        if (!(lengthSquared >= D(EI_P_ONE))) return;
        if (!(--tries > 0)) return;
    }
}

// Once per tick (the first emission): the sphere's height and radius ease towards the carrier's
// figure, the emission rate follows the source's size, and element 1 smooths the carrier's motion.
bool FollowCarrier(CParticleSource* s) {
    if (!s->attachedTo) {
        DebugMessage(NoCarrierSphere());
        return false;
    }
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    radius = Store(D(radius) * D(EI_SPHERE_RADIUS));
    float height;
    std::memcpy(&height, &offset.z, sizeof(float));
    if (s->elementCount == 0) {
        SetElementCount(s, 2);
        *ElementFloat(s->elements, 0x50) = Store(x87::fcos(EI_TILT_ANGLE));
        *ElementFloat(s->elements, 0x54) = Store(x87::fsin(EI_TILT_ANGLE));
        std::memset(&s->elements[1].pos, 0, sizeof(Vector3));
        std::memset(&s->elements[0].pos, 0, sizeof(Vector3));
        std::memcpy(&s->elements[0].pos.z, &height, sizeof(float));
        std::memcpy(ElementFloat(s->elements, 0x18), &radius, sizeof(float));
    }
    ParticleElement* e = s->elements;
    e->pos.z = Store(D(e->pos.z) * D(EI_KEEP_0_9) + D(height) * D(EI_TAKE_0_1));
    float* sphereRadius = ElementFloat(s->elements, 0x18);
    *sphereRadius = Store(D(radius) * D(EI_TAKE_0_1) + D(*sphereRadius) * D(EI_KEEP_0_9));
    float rate = Store(D(s->size) * D(s->type_ == 0x201A ? EI_ANTIMAGIC_RATE : EI_SPHERE_RATE));
    s->emitCount = x87::fistp(rate);
    Vector3& smoothed = s->elements[1].pos;
    double mx = D(smoothed.x) * D(EI_MOTION_KEEP);
    float my = Store(D(smoothed.y) * D(EI_MOTION_KEEP));
    float mz = Store(D(smoothed.z) * D(EI_MOTION_KEEP));
    float sx = Store(mx + D(s->motion.x));
    float sy = Store(D(my) + D(s->motion.y));
    float sz = Store(D(mz) + D(s->motion.z));
    smoothed.x = sx;
    smoothed.y = sy;
    smoothed.z = sz;
    const Vector3& m = s->elements[1].pos;
    float* speed = ElementFloat(s->elements, 0x20);
    *speed = Store(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(m)));
    if (!(D(*ElementFloat(s->elements, 0x20)) >= EI_STILL_SPEED)) SetBits(ElementFloat(s->elements, 0x20), 0);
    return true;
}

} // namespace

// 0048E070. Spheres: a particle on the sphere around the carrier (a random direction at 0.65 of
// the radius), flattened against the carrier's motion when it moves; one in eleven is larger. The
// size follows the life table of the kind.
int CParticleSource::EmitOrbit(Particle* p, int index) {
    if (index == 0 && !FollowCarrier(this)) return 0;
    float radius;
    std::memcpy(&radius, ElementFloat(elements, 0x18), sizeof(float));
    double big;
    if (Next() % 11 == 0) {
        float base = type_ == 0x201A ? EI_BIG_ANTIMAGIC : type_ == 0x2016 ? EI_BIG_EARTH : EI_BIG_SPHERE;
        big = Unsigned(Next()) * EI_P_SCALE_1 + D(base);
    } else {
        big = Unsigned(Next()) * EI_SCALE_0_03 + D(EI_MOTION_KEEP);
    }
    p->drift.x = Store(big);

    float x, y, z;
    RandomInBallZYX(x, y, z);
    Vector3 v;
    v.Construct(x, y, z);
    std::memcpy(&p->velocity, &v, sizeof(Vector3));
    double shell = D(radius) * D(EI_SHELL);
    double scale = shell / x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(p->velocity));
    p->velocity.x = Store(scale * D(p->velocity.x));
    p->velocity.y = Store(scale * D(p->velocity.y));
    p->velocity.z = Store(scale * D(p->velocity.z));
    p->color = 0xFFFFFFFF;
    p->frame = static_cast<int>(Next() % 11);
    p->life = lifetime;
    p->frameTimer = 1;

    // The carrier's motion flattens the sphere: the part of the direction against the motion
    // (clamped to [0, 1] of it) is taken off.
    Vector3 offset;
    double speed = D(*ElementFloat(elements, 0x20));
    if (speed > EI_STILL_SPEED) {
        const Vector3& m = elements[1].pos;
        float t = Store(-(DotUnrounded<Sum3::ZYX, double>(m, p->velocity) / speed));
        if (t >= 1.0f) t = 1.0f;
        if (!(t >= 0.0f)) t = 0.0f;
        Vector3 along;
        SubtractVectors(&offset, p->velocity, *ScaleVector(&along, m, t));
    } else {
        std::memcpy(&offset, &p->velocity, sizeof(Vector3));
    }
    const Vector3& centre = elements[0].pos;
    double cx = D(origin.x) + D(centre.x);
    float cy = Store(D(origin.y) + D(centre.y));
    float cz = Store(D(origin.z) + D(centre.z));
    SetBits(&p->size, 0x3F800000);
    double px = cx + D(offset.x);
    float py = Store(D(offset.y) + D(cy));
    float pz = Store(D(offset.z) + D(cz));
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    if (type_ == 0x2017) {
        double s = D(SphereSizeByLife()[p->life]) * D(p->drift.x) * D(radius);
        p->size = Store(s + s);
    } else if (type_ == 0x201A) {
        p->size = Store(D(AntimagicSizeByLife()[p->life]) * D(p->drift.x) * D(radius));
    } else if (type_ == 0x2016) {
        p->size = Store(D(SphereSizeByLife()[p->life]) * D(p->drift.x) * D(radius));
    } else {
        p->size = Store(D(SphereSizeByLife()[p->life]) * D(p->drift.x) * D(radius) * D(EI_AIR_SIZE));
    }
    return 1;
}
EI_REPLACE_METHOD(0x0048E070, CParticleSource::EmitOrbit);

// --- Casting (0x200B) and Healing (0x2006): rings of flames rising around the caster -------------
//
// Each element is a ring: +0x00 its centre (relative to the source), +0x0C its radius, +0x10 the
// angle of its leading flame, +0x14 its rise per tick, +0x18 its starting centre, +0x34 its
// countdown. +0x12C counts the flames emitted after the set-up (one per ring). The two types share
// the shapes; they differ in the ring count, timings, sizes and the spark pattern.

namespace {

#define EI_FULL_TURN GameFloat(0x0073CD08)          // 360.0
#define EI_PI GameDouble(0x0073CAE8)                // pi
#define EI_PER_DEGREE GameDouble(0x0073BA30)        // 1/180
#define EI_RING_WIDEN GameFloat(0x0073BAC4)         // 1.1
#define EI_PER_36 GameFloat(0x0073CD04)             // 1/36
#define EI_RISE_LOW GameFloat(0x0073CD00)           // -0.1
#define EI_RISE_HIGH GameFloat(0x0073BA80)          // 0.1
#define EI_FLAME_SIZE GameFloat(0x0073CCFC)         // 0.0015
#define EI_TWENTY GameFloat(0x0073CCF8)             // 20.0
#define EI_HALF GameFloat(0x0073CB28)               // 0.5
#define EI_SPARK_SPREAD GameFloat(0x0073CCF4)       // 0.001
#define EI_FADE_SIZE GameFloat(0x0073CD0C)          // 1/1.3
#define EI_TURN_SPEED GameDouble(0x0073CD10)        // 10.0 (degrees per tick)
#define EI_TENTH_B GameFloat(0x0073B850)            // 0.1
#define EI_HEAL_RINGS GameFloat(0x0073BA98)         // 6.0 rings per unit of size
#define EI_HEAL_RINGS_BASE GameFloat(0x0073CDB8)    // 2.0
#define EI_HEAL_GROWTH GameFloat(0x0073CDB4)        // 1/144
#define EI_HEAL_SPREAD GameFloat(0x0073CD18)        // 0.15
#define EI_HEAL_SPARK_SIZE GameFloat(0x0073CDB0)    // 0.07

const char* NoCarrierRing() { return reinterpret_cast<const char*>(0x0078AF24); }
const char* NoCarrierHealing() { return reinterpret_cast<const char*>(0x0078AF50); }
constexpr int kFlameState = 0xFFFF;     // a burning-out flame (the update's last phase)

// The rings around the carrier (emitCount of them, already allocated): stacked from the carrier's
// feet with a random lift, their flames spaced evenly around the circle; the source's height is
// the radius growth per tick (Healing also sets the flame growth). Each ring counts down from
// `countdown`.
void PlaceRings(CParticleSource* s, int countdown) {
    float step = Store(EI_PI * EI_PER_DEGREE * (D(EI_FULL_TURN) / static_cast<double>(s->emitCount)));
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    double fullRadius = D(radius);
    float angle = 0.0f;
    offset.z = Store(D(offset.z) - fullRadius);
    radius = Store(fullRadius * D(EI_RING_WIDEN));
    float diameter = Store(fullRadius + fullRadius);
    double rise = D(radius) * D(EI_PER_36);
    s->height = Store(rise + rise);
    if (s->type_ == 0x2006) s->growth = Store((D(s->size) + D(EI_P_ONE)) * D(EI_HEAL_GROWTH));
    if (s->emitCount > 0) {
        float low = Store(D(diameter) * D(EI_RISE_LOW));
        float high = Store(D(diameter) * D(EI_RISE_HIGH));
        for (int i = 0; i < s->emitCount; ++i) {
            ParticleElement* e = &s->elements[i];
            *ElementInt(e, 0x30) = 0;
            *ElementInt(e, 0x34) = countdown;
            double lift = Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1 + D(low);
            float* start = ElementFloat(e, 0x18);
            std::memcpy(start, &offset, sizeof(Vector3));
            start[2] = Store(lift + D(start[2]));
            std::memcpy(&e->pos, start, sizeof(Vector3));
            SetBits(ElementFloat(e, 0x0C), 0);
            *ElementFloat(e, 0x10) = angle;
            *ElementFloat(e, 0x14) = Store((D(diameter) - lift) * D(EI_PER_36));
            angle = Store(D(angle) + D(step));
        }
    }
    SetBits(&s->unknown_12C, 0);
}

// The flame phase: one flame per ring (state = the ring), then one call that only ends the phase
// (result 0). False once the phase is over.
bool EmitRingFlame(CParticleSource* s, Particle* p, int& result) {
    int emitted = static_cast<int>(Bits(&s->unknown_12C));
    if (emitted > s->elementCount) return false;
    SetBits(&s->unknown_12C, static_cast<std::uint32_t>(emitted + 1));
    if (emitted == s->elementCount) {
        result = 0;
        return true;
    }
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = 36;
    p->frameTimer = 1;
    p->state = emitted;
    const ParticleElement& ring = s->elements[emitted];
    p->pos.x = Store(D(ring.pos.x) + D(s->origin.x));
    p->pos.y = Store(D(ring.pos.y) + D(s->origin.y));
    p->pos.z = Store(D(ring.pos.z) + D(s->origin.z));
    if (s->type_ == 0x2006) p->size = Store((D(s->size) + D(EI_P_ONE)) * D(EI_TENTH_B));
    else p->size = Store(D(s->size) * D(EI_FLAME_SIZE));
    result = 1;
    return true;
}

// A spark in a box of +-spread around a ring's centre (`spread` computed unrounded by the caller).
// The original's z uses -spread for both the range and the offset.
void PlaceRingSpark(CParticleSource* s, Particle* p, const ParticleElement& ring, double spreadUnrounded) {
    float spread = Store(spreadUnrounded);
    float low = Store(-spreadUnrounded);
    double x = Unsigned(Next()) * (D(spread) - D(low)) * EI_P_SCALE_1 + D(ring.pos.x) + D(s->origin.x) + D(low);
    p->pos.x = Store(x);
    double y = Unsigned(Next()) * (D(spread) - D(low)) * EI_P_SCALE_1 + D(ring.pos.y) + D(s->origin.y) + D(low);
    p->pos.y = Store(y);
    p->pos.z = Store(D(ring.pos.z) - Unsigned(Next()) * D(low) * EI_P_SCALE_1 + D(s->origin.z) + D(low));
}

float RingRadius(const ParticleElement& ring) {
    float r;
    std::memcpy(&r, &ring.value[0], sizeof(float));
    return r;
}

// Moves a flame with its ring.
void FollowRing(CParticleSource* s, Particle* p, int ring) {
    const ParticleElement& e = s->elements[ring];
    p->pos.x = Store(D(s->origin.x) + D(e.pos.x));
    p->pos.y = Store(D(s->origin.y) + D(e.pos.y));
    p->pos.z = Store(D(s->origin.z) + D(e.pos.z));
}

// Turns a ring's flame point 10 degrees around its centre; the ring rises.
void TurnRing(ParticleElement* e) {
    float* radius = ElementFloat(e, 0x0C);
    float* angle = ElementFloat(e, 0x10);
    double turned = EI_PI * EI_PER_DEGREE * EI_TURN_SPEED + D(*angle);
    *angle = Store(turned);
    e->pos.x = Store(x87::fsin(turned) * D(*radius) + D(*ElementFloat(e, 0x18)));
    e->pos.y = Store(x87::fcos(D(*angle)) * D(*radius) + D(*ElementFloat(e, 0x1C)));
    e->pos.z = Store(D(*ElementFloat(e, 0x14)) + D(e->pos.z));
}

} // namespace

// 00489320. Casting (0x200B): the first call places three rings (needs a carrier); then one flame
// per ring; then, while ring 0 is young, sparks scattered around ring 0's circle (72 per tick),
// else sparks around a random ring's centre.
int CParticleSource::EmitCastRing(Particle* p, int) {
    if (elementCount == 0) {
        if (!attachedTo) {
            DebugMessage(NoCarrierRing());
            return 0;
        }
        emitCount = 3;
        SetElementCount(this, 3);
        SetBits(&growth, 0);
        PlaceRings(this, 45);
        emitCount *= 9;
    }
    int result;
    if (EmitRingFlame(this, p, result)) return result;
    if (elements[0].ticks < 9) {
        // On ring 0's circle, between the ring and a spread that narrows with the height.
        emitCount = 72;
        float ringRadius = RingRadius(elements[0]);
        float twoPi = Store(EI_PI + EI_PI);
        std::uint32_t r = Next();
        p->color = 0xFFFFFFFF;
        p->frame = 1;
        float angle = Store(Unsigned(r) * D(twoPi) * EI_P_SCALE_1);
        int age = elements[0].ticks;
        if (age >= 5) age = 5;
        std::uint32_t spread = Next();
        p->frameTimer = 1;
        p->state = -1;
        p->life = static_cast<int>(spread % 4u) + age + 1;
        float inner = Store((D(ringRadius) - D(height) * D(EI_TWENTY)) * D(EI_HALF) + D(ringRadius));
        float reach = Store(Unsigned(Next()) * (D(ringRadius) - D(inner)) * EI_P_SCALE_1 + D(inner));
        const ParticleElement& ring = elements[0];
        p->pos.x = Store(x87::fsin(D(angle)) * D(reach) + (D(origin.x) + D(ring.pos.x)));
        p->pos.y = Store(x87::fcos(D(angle)) * D(reach) + (D(elements[0].pos.y) + D(origin.y)));
        p->pos.z = Store(D(elements[0].pos.z) + D(origin.z));
    } else {
        const ParticleElement& ring = elements[Next() % static_cast<std::uint32_t>(elementCount)];
        p->frame = 1;
        p->life = 7;
        p->frameTimer = 1;
        p->color = 0xFFFFFFFF;
        p->state = -1;
        PlaceRingSpark(this, p, ring, D(RingRadius(ring)) * D(EI_TENTH_B) + D(EI_SPARK_SPREAD));
    }
    p->size = Store(D(size) * D(EI_TENTH_B));
    return 1;
}
EI_REPLACE_METHOD(0x00489320, CParticleSource::EmitCastRing);

// 00489A50. Casting particles: a flame (state = its ring) grows for 18 ticks, shrinks for 18 and
// rides its ring; then it burns out for 8 ticks (shrinking, alpha falling 30 per tick). Sparks
// (state -1) step through their frames.
int CParticleSource::UpdateCastRing(Particle* p) {
    int life = --p->life;
    if (life == 0) {
        if (p->state < 0 || p->state == kFlameState) return 0;
        p->state = kFlameState;
        p->life = 8;
        return 1;
    }
    int ring = p->state;
    if (ring == kFlameState) {
        p->size = Store(D(p->size) * D(EI_FADE_SIZE));
        p->color = ((p->color & 0xFF000000u) - 0x1E000001u) | 0xFFFFFF;
        return 1;
    }
    if (ring < 0) {
        ++p->frame;
        return 1;
    }
    p->frameTimer = 1;
    if (life < 36 && life >= 18) p->size = Store(D(p->size) - D(growth));
    else p->size = Store(D(growth) + D(p->size));
    FollowRing(this, p, ring);
    return 1;
}
EI_REPLACE_METHOD(0x00489A50, CParticleSource::UpdateCastRing);

// 00489B40. Casting rings: each counts down (stopping the emission at 0); the radius grows by the
// source's height for 18 ticks, then shrinks, then eases towards 20 times the height; while
// above 9 ticks the flame point turns around the centre and the ring rises.
void CParticleSource::TickCastRing(ParticleElement* e) {
    int t = --e->ticks;
    if (t == 0) {
        flags_ &= ~1u;
        return;
    }
    float* radius = ElementFloat(e, 0x0C);
    if (t >= 27) {
        *radius = Store(D(height) + D(*radius));
    } else if (t >= 9) {
        *radius = Store(D(*radius) - D(height));
    } else {
        double target = D(height) * D(EI_TWENTY);
        *radius = Store((D(*radius) - target) * D(EI_HALF) + target);
    }
    if (t >= 9) TurnRing(e);
}
EI_REPLACE_METHOD(0x00489B40, CParticleSource::TickCastRing);

// 0048B630. Healing (0x2006): the first call places 6 * size + 2 rings (needs a carrier); then
// one flame per ring; then sparks around a random ring's centre.
int CParticleSource::EmitHealing(Particle* p, int) {
    if (elementCount == 0) {
        if (!attachedTo) {
            DebugMessage(NoCarrierHealing());
            return 0;
        }
        emitCount = x87::fistp(Store(D(size) * D(EI_HEAL_RINGS) + D(EI_HEAL_RINGS_BASE)));
        SetElementCount(this, emitCount);
        PlaceRings(this, 36);
        emitCount *= 3;
    }
    int result;
    if (EmitRingFlame(this, p, result)) return result;
    const ParticleElement& ring = elements[Next() % static_cast<std::uint32_t>(elementCount)];
    p->color = 0xFFFFFFFF;
    p->frame = 8;
    p->life = 8;
    p->frameTimer = 1;
    p->state = -1;
    PlaceRingSpark(this, p, ring, D(RingRadius(ring)) * D(EI_HEAL_SPREAD));
    p->size = Store((D(size) + D(EI_P_ONE)) * D(EI_HEAL_SPARK_SIZE));
    return 1;
}
EI_REPLACE_METHOD(0x0048B630, CParticleSource::EmitHealing);

// 0048BBE0. Healing particles: like Casting's, but a flame cycles 8 frames, grows for its first
// 18 ticks of life and shrinks after, and burns out without shrinking.
int CParticleSource::UpdateHealing(Particle* p) {
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
    if (life >= 18) p->size = Store(D(growth) + D(p->size));
    else p->size = Store(D(p->size) - D(growth));
    FollowRing(this, p, ring);
    return 1;
}
EI_REPLACE_METHOD(0x0048BBE0, CParticleSource::UpdateHealing);

// 0048BCE0. Healing rings: each counts down (stopping the emission at 0); the radius grows by the
// source's height for 18 ticks, then shrinks; the flame point keeps turning and the ring rising.
void CParticleSource::TickHealing(ParticleElement* e) {
    int t = --e->ticks;
    if (t == 0) {
        flags_ &= ~1u;
        return;
    }
    float* radius = ElementFloat(e, 0x0C);
    if (t >= 18) *radius = Store(D(height) + D(*radius));
    else *radius = Store(D(*radius) - D(height));
    TurnRing(e);
}
EI_REPLACE_METHOD(0x0048BCE0, CParticleSource::TickHealing);

// --- CastingIllusion, Domination, Enchantment, Healing (0x202B-0x202E): orbiting lights ---------
//
// One element per orbiter (lifetime of them, added one per emission): +0x00 its world position,
// +0x0C / +0x10 the cosine and sine of its turn per tick, +0x14 the spark size (the carrier's
// radius, negated for Domination), +0x18 its position relative to the source, +0x24 the previous
// world position.

namespace {

#define EI_PI_F GameFloat(0x0073CAF0)               // pi
#define EI_HALF_C GameFloat(0x0073B86C)             // 0.5
#define EI_POINT_FOUR_C GameFloat(0x0073B868)       // 0.4
#define EI_SWIRL_TURN_SLOW GameFloat(0x0073CB94)    // 0.3
#define EI_SWIRL_TURN_FAST GameFloat(0x0073CB90)    // 0.5
#define EI_SWIRL_HIGH GameFloat(0x0073CC80)         // 1.6
#define EI_SWIRL_SIZE_SCALE GameDouble(0x0073CEC0)  // ~0.35 * 2^-32
#define EI_SWIRL_SIZE_BASE GameFloat(0x0073CEB8)    // 0.85
#define EI_SWIRL_GROW GameFloat(0x0073CD74)         // 0.03
#define EI_SWIRL_SPARK_SCALE GameDouble(0x0073CEB0) // ~0.8 * 2^-32
#define EI_SWIRL_SPARK_BASE GameFloat(0x0073CDE0)   // 0.6
#define EI_SWIRL_SPARK_SIZE GameFloat(0x0073BAD4)   // 0.2
#define EI_SWIRL_SHRINK GameFloat(0x0073CEC8)       // 0.97
#define EI_MARKER_SIZE_SCALE GameDouble(0x0073CCB0) // ~0.4 * 2^-32
#define EI_MARKER_SIZE_BASE GameFloat(0x0073CCA8)   // 0.8

const std::int32_t* SwirlMarkerAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A918); }
const std::int32_t* SwirlSparkAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A914); }

// Adds orbiter `k` (the k-th of `lifetime`, evenly spaced around the carrier) and emits its marker
// particle (state -1 - k).
int AddOrbiter(CParticleSource* s, Particle* p) {
    float orbit, carrier;     // the orbit radius and the carrier's radius
    int k = s->elementCount;
    if (k == 0) {
        Vector3 offset;
        float radius;
        s->AttachmentOffset(&offset, &radius);
        carrier = radius;
        orbit = Store(D(radius) * D(EI_HALF_C));
    } else {
        std::memcpy(&orbit, ElementFloat(s->elements, 0x18), sizeof(float));
        std::memcpy(&carrier, ElementFloat(s->elements, 0x14), sizeof(float));
    }
    SetElementCount(s, k + 1);
    double share = static_cast<double>(k) * D(EI_PI_F);
    double angle = (share + share) / static_cast<double>(s->lifetime);
    ParticleElement* e = &s->elements[k];
    float lift = Store(D(carrier) * D(EI_POINT_FOUR_C));
    float sign = 1.0f;
    double turn;
    switch (s->type_ - 0x202B) {
    case 0:
    case 2:
        turn = EI_SWIRL_TURN_SLOW;
        break;
    case 1:
        turn = EI_SWIRL_TURN_FAST;
        lift = Store(D(carrier) * D(EI_SWIRL_HIGH));
        sign = -1.0f;
        break;
    default:
        turn = EI_SWIRL_TURN_FAST;
        break;
    }
    float* relative = ElementFloat(e, 0x18);
    relative[0] = Store(x87::fcos(angle) * D(orbit));
    *ElementInt(e, 0x30) = 0;
    *ElementInt(e, 0x34) = 1;
    relative[1] = Store(x87::fsin(angle) * D(orbit));
    relative[2] = lift;
    float px = Store(D(s->origin.x) + D(relative[0]));
    float py = Store(D(s->origin.y) + D(relative[1]));
    float pz = Store(D(s->origin.z) + D(relative[2]));
    e->pos = {px, py, pz};
    *ElementFloat(e, 0x0C) = Store(x87::fcos(turn));
    float* previous = ElementFloat(e, 0x24);
    previous[0] = px;
    previous[1] = py;
    previous[2] = pz;
    *ElementFloat(e, 0x10) = Store(x87::fsin(turn));
    // (The original reads its locals here one slot off after a POP: the sign times the carrier's
    // radius, not the orbit times it.)
    *ElementFloat(e, 0x14) = Store(D(sign) * D(carrier));
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = 6;
    p->frameTimer = 1;
    p->state = -1 - k;
    std::memcpy(&p->pos, &e->pos, sizeof(Vector3));
    SetBits(&p->size, 0);
    return 1;
}

} // namespace

// 00490C10. Until all orbiters exist each emission adds one (needs a carrier); then sparks start
// at a random point between an orbiter's previous and current position and fly off with a random
// velocity from the spread box.
int CParticleSource::EmitCastSwirl(Particle* p, int) {
    if (elementCount < lifetime) {
        if (!attachedTo) {
            DebugMessage(NoCarrierCasting());
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
    float t = Store(Unsigned(r) * EI_P_SCALE_1);
    *ParticleFloat(p, 0x34) = Store((Unsigned(Next()) * EI_SWIRL_SIZE_SCALE + D(EI_SWIRL_SIZE_BASE)) *
                                    D(*ElementFloat(e, 0x14)) * D(EI_SWIRL_GROW));
    SetBits(ParticleFloat(p, 0x30), 0x3F83D70A);     // 1.03
    float* v = &p->velocity.x;
    const float* low = &spreadMin.x;
    const float* high = &spreadMax.x;
    for (int i = 0; i < 3; ++i) v[i] = Store(Unsigned(Next()) * (D(high[i]) - D(low[i])) * EI_P_SCALE_1 + D(low[i]));
    const float* previous = ElementFloat(e, 0x24);
    double px = D(previous[0]) + D(v[0]);
    float py = Store(D(v[1]) + D(previous[1]));
    float pz = Store(D(v[2]) + D(previous[2]));
    SetBits(&p->size, 0x3F800000);
    p->pos.y = py;
    p->pos.z = pz;
    p->pos.x = Store(px);
    double u = D(EI_P_ONE) - D(t);
    float a0 = Store(u * D(e->pos.x));
    float a1 = Store(u * D(e->pos.y));
    double a2 = u * D(e->pos.z);
    double b0 = D(t) * D(previous[0]);
    float b1 = Store(D(t) * D(previous[1]));
    float b2 = Store(D(t) * D(previous[2]));
    float c0 = Store(b0 + D(a0));
    float c1 = Store(D(b1) + D(a1));
    double c2 = a2 + D(b2);
    v[0] = Store(D(c0) + D(v[0]));
    v[1] = Store(D(c1) + D(v[1]));
    v[2] = Store(c2 + D(v[2]));
    p->size = Store((Unsigned(Next()) * EI_SWIRL_SPARK_SCALE + D(EI_SWIRL_SPARK_BASE)) * D(size) *
                    D(EI_SWIRL_SPARK_SIZE));
    return 1;
}
EI_REPLACE_METHOD(0x00490C10, CParticleSource::EmitCastSwirl);

// 00491230. A marker (state < 0) sits on its orbiter with a flickering size and, once the source
// stops emitting (or loses its carrier), fades by the table and dies. A spark fades over its last
// 7 ticks, cycles frames 12..15, shrinks, and moves by its velocity, whose z changes by a growing
// step.
int CParticleSource::UpdateCastSwirl(Particle* p) {
    int state = p->state;
    if (state < 0) {
        const ParticleElement& e = elements[-(state + 1)];
        std::memcpy(&p->pos, &e.pos, sizeof(Vector3));
        SetBits(&p->size, 0x3F800000);
        p->size = Store((Unsigned(Next()) * EI_MARKER_SIZE_SCALE + D(EI_MARKER_SIZE_BASE)) * D(size) *
                        D(EI_POINT_FOUR_C));
        if ((flags_ & 1) && attachedTo) return 1;
        int life = p->life;
        if (life < 0) return 0;
        p->color = (static_cast<std::uint32_t>(SwirlMarkerAlpha()[life]) << 24) | 0xFFFFFF;
        p->life = life - 1;
        return 1;
    }
    int life = --p->life;
    if (life == 0) return 0;
    if (life < 7) p->color = (static_cast<std::uint32_t>(SwirlSparkAlpha()[life]) << 24) | 0xFFFFFF;
    if (++p->frame == 16) p->frame = 12;
    double shrunk = D(p->size) * D(EI_SWIRL_SHRINK);
    std::memcpy(&p->pos, &p->velocity, sizeof(Vector3));
    p->size = Store(shrunk);
    float* step = ParticleFloat(p, 0x34);
    p->velocity.z = Store(D(p->velocity.z) + D(*step));
    *step = Store(D(*ParticleFloat(p, 0x30)) * D(*step));
    return 1;
}
EI_REPLACE_METHOD(0x00491230, CParticleSource::UpdateCastSwirl);

// 00491390. Orbiters turn about the source's vertical axis (by -turn) and keep their previous
// world position.
void CParticleSource::TickCastSwirl(ParticleElement* e) {
    float c = *ElementFloat(e, 0x0C);
    float s = *ElementFloat(e, 0x10);
    float* relative = ElementFloat(e, 0x18);
    double y = D(c) * D(relative[1]) - D(relative[0]) * D(s);
    double x = D(relative[0]) * D(c) + D(relative[1]) * D(s);
    std::memcpy(ElementFloat(e, 0x24), &e->pos, sizeof(Vector3));
    relative[0] = Store(x);
    relative[1] = Store(y);
    float px = Store(D(origin.x) + D(relative[0]));
    float py = Store(D(origin.y) + D(relative[1]));
    float pz = Store(D(origin.z) + D(relative[2]));
    e->pos.x = px;
    e->pos.y = py;
    e->pos.z = pz;
}
EI_REPLACE_METHOD(0x00491390, CParticleSource::TickCastSwirl);

// --- Link (0x2014): a beam of sparks from the source along its extent (+0x130) ------------------
//
// Element 0: +0x18 the beam's length, +0x1C its inverse, +0x34 the alpha cap (255, falling by 16
// per tick once the source stops). Elements 1-3 hold the beam's frame at +0x18: 1 the unit
// direction, 2 a unit side vector, 3 the unit cross product of the extent and the side.

namespace {

#define EI_LINK_DEGENERATE GameDouble(0x0073CE00)   // 1e-10
#define EI_LINK_TWIST GameFloat(0x0073BAD4)         // 0.2
#define EI_LINK_HALF GameFloat(0x0073B86C)          // 0.5
#define EI_LINK_QUARTER GameFloat(0x0073C3B0)       // 0.25
#define EI_LINK_BULGE GameFloat(0x0073CD70)         // 0.9
#define EI_LINK_ALPHA GameFloat(0x0073C34C)         // 1000.0

inline float F(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> inline float F(T) = delete;

// The beam's frame from the extent (the first emission).
void SetUpLink(CParticleSource* s) {
    SetElementCount(s, 4);
    const Vector3& x = s->extent;
    *ElementFloat(s->elements, 0x18) = Store(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(x)));
    *ElementFloat(s->elements, 0x1C) = Store(D(EI_P_ONE) / D(*ElementFloat(s->elements, 0x18)));

    // A side vector: (ey, -ex, 0), or (ey - ez, -ex, ex) when the extent is (nearly) vertical.
    Vector3 side;
    double zero = EI_P_ZERO;
    side.Construct(Store(D(x.y) - zero), Store(zero - D(x.x)), 0.0f);
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(side));
    if (!(length >= EI_LINK_DEGENERATE)) {
        float ex = x.x;
        Vector3 other;
        other.Construct(Store(D(x.y) - D(x.z)), Store(-D(ex)), ex);
        length = x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(other));
        side = other;
    }
    double inverse = D(EI_P_ONE) / length;
    side.x = Store(inverse * D(side.x));
    side.y = Store(D(side.y) * inverse);
    side.z = Store(D(side.z) * inverse);

    Vector3 cross;
    cross.Construct(Store(D(side.z) * D(x.y) - D(side.y) * D(x.z)), Store(D(side.x) * D(x.z) - D(side.z) * D(x.x)),
                    Store(D(side.y) * D(x.x) - D(side.x) * D(x.y)));
    double crossInverse = D(EI_P_ONE) / x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(cross));
    cross.x = Store(crossInverse * D(cross.x));
    cross.y = Store(D(cross.y) * crossInverse);
    cross.z = Store(D(cross.z) * crossInverse);

    double extentLength = x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(x));
    Vector3 direction;
    direction.Construct(Store(D(x.x) / extentLength), Store(D(x.y) / extentLength), Store(D(x.z) / extentLength));
    std::memcpy(ElementFloat(&s->elements[1], 0x18), &direction, sizeof(Vector3));
    std::memcpy(ElementFloat(&s->elements[2], 0x18), &side, sizeof(Vector3));
    std::memcpy(ElementFloat(&s->elements[3], 0x18), &cross, sizeof(Vector3));
    *ElementInt(s->elements, 0x34) = 0xFF;
}

} // namespace

// 0048D8D0. Link: a spark at the source with a random offset across the beam (from the spread
// box), a twist rate from its distance to the axis and a random speed along the beam (fast and
// large sparks near the axis now and then).
int CParticleSource::EmitLink(Particle* p, int) {
    if (elementCount == 0) SetUpLink(this);
    p->color = 0xFFFFFF;
    float a = SignedUnit();
    float b = SignedUnit();
    double c = Unsigned(Next()) * (D(spreadMax.z) - D(spreadMin.z)) * EI_P_SCALE_1 + D(spreadMin.z);
    p->velocity.x = Store(D(a) * D(spreadMin.x));
    p->velocity.y = Store(D(b) * D(spreadMin.y));
    p->velocity.z = Store(c);
    double reach = x87::fsqrt(D(b) * D(b) + D(a) * D(a));
    float reachRounded = Store(reach);
    double twist = reach * D(EI_LINK_TWIST);
    p->unknown_2C = Store(x87::fcos(twist));
    p->unknown_30 = Store(x87::fsin(twist));
    Vector3 start;
    start.Construct(Store(D(p->velocity.x) + D(origin.x)), Store(D(origin.y) + D(p->velocity.y)),
                    Store(D(origin.z) + D(p->velocity.z)));
    reinterpret_cast<Vector4*>(p)->FromPoint(start);
    double factor;
    if ((Next() & 3) == 0 && !(D(reachRounded) >= D(EI_LINK_HALF))) {
        *ParticleFloat(p, 0x3C) = Store(Range(F(0x3DCCCCCD), F(0x3DF5C28F)));     // 0.1 .. 0.12
        factor = Range(F(0x3E051EB8), F(0x3E23D70A));                           // 0.13 .. 0.16
    } else {
        *ParticleFloat(p, 0x3C) = Store(Range(F(0x3E2E147B), F(0x3E800000)));     // 0.17 .. 0.25
        factor = Range(F(0x3CF5C28F), F(0x3D4CCCCD));                           // 0.03 .. 0.05
    }
    *ParticleFloat(p, 0x40) = Store(factor * D(size));
    SetBits(ParticleFloat(p, 0x38), 0);
    std::memcpy(&p->size, ParticleFloat(p, 0x40), sizeof(float));
    p->frame = static_cast<int>(Next() % static_cast<std::uint32_t>(frameCount + 1));
    return 1;
}
EI_REPLACE_METHOD(0x0048D8D0, CParticleSource::EmitLink);

// 0048DE30. Link sparks run along the beam (+0x38 the distance travelled, +0x3C the speed),
// spinning around the axis; the beam bulges in the middle and the alpha peaks there (capped by
// element 0). Dropped past the beam's end.
int CParticleSource::UpdateLink(Particle* p) {
    p->frame = (p->frame + 1) % frameCount;
    double travelled = D(*ParticleFloat(p, 0x3C)) + D(*ParticleFloat(p, 0x38));
    *ParticleFloat(p, 0x38) = Store(travelled);
    ParticleElement* e = elements;
    float along = Store(travelled * D(*ElementFloat(e, 0x1C)));
    if (travelled > D(*ElementFloat(e, 0x18))) return 0;

    double c = D(p->unknown_2C);
    double s = D(p->unknown_30);
    double turnedY = D(p->velocity.y) * c + D(p->velocity.x) * s;
    double turnedX = c * D(p->velocity.x) - D(p->velocity.y) * s;
    p->velocity.x = Store(turnedX);
    p->velocity.y = Store(turnedY);
    float ax = Store(D(along) * D(extent.x));
    float ay = Store(D(along) * D(extent.y));
    float az = Store(D(along) * D(extent.z));
    const float* direction = ElementFloat(&elements[1], 0x18);
    const float* sideAxis = ElementFloat(&elements[2], 0x18);
    const float* cross = ElementFloat(&elements[3], 0x18);
    double q = D(p->velocity.z);
    float dx = Store(q * D(direction[0]));
    float dy = Store(q * D(direction[1]));
    float dz = Store(q * D(direction[2]));
    float cx = Store(turnedY * D(cross[0]));
    float cy = Store(turnedY * D(cross[1]));
    double cz = turnedY * D(cross[2]);
    double vx = D(p->velocity.x);
    float sx = Store(vx * D(sideAxis[0]));
    float sy = Store(vx * D(sideAxis[1]));
    double sz = vx * D(sideAxis[2]);
    float bx = Store(D(sx) + D(origin.x));
    float by = Store(D(sy) + D(origin.y));
    Vector3 p1;
    p1.Construct(Store(D(bx) + D(cx)), Store(D(by) + D(cy)), Store(sz + D(origin.z) + cz));
    Vector3 p2;
    p2.Construct(Store(D(p1.x) + D(dx)), Store(D(p1.y) + D(dy)), Store(D(p1.z) + D(dz)));
    Vector3 p3;
    p3.Construct(Store(D(p2.x) + D(ax)), Store(D(p2.y) + D(ay)), Store(D(p2.z) + D(az)));
    std::memcpy(&p->pos, &p3, sizeof(Vector3));

    double h = D(along) - D(EI_LINK_HALF);
    SetBits(&p->size, 0x3F800000);
    std::memcpy(&p->size, ParticleFloat(p, 0x40), sizeof(float));
    p->pos.z = Store((D(EI_LINK_QUARTER) - h * h) * D(*ElementFloat(elements, 0x18)) * D(EI_LINK_BULGE) + D(p3.z));
    float hr = Store(h);
    std::uint32_t absBits = Bits(&hr) & 0x7FFFFFFFu;
    float offCentre;
    std::memcpy(&offCentre, &absBits, sizeof(float));
    std::uint32_t fade = static_cast<std::uint32_t>(GameFtol((D(EI_LINK_HALF) - D(offCentre)) * D(EI_LINK_ALPHA)));
    std::uint32_t cap = static_cast<std::uint32_t>(*ElementInt(elements, 0x34));
    if (cap < fade) fade = cap;
    p->color = (fade << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x0048DE30, CParticleSource::UpdateLink);

// 0048E040. Link element 0: once the source stops emitting the alpha cap falls by 16 per tick
// (not below 0). (The other elements' caps are unused.)
void CParticleSource::TickLink(ParticleElement* e) {
    if (!(flags_ & 1)) *ElementInt(e, 0x34) -= 16;
    std::int32_t cap = *ElementInt(e, 0x34);
    *ElementInt(e, 0x34) = cap < 0 ? 0 : cap;
}
EI_REPLACE_METHOD(0x0048E040, CParticleSource::TickLink);

// --- FireBlast (0x2002): fifteen fragments flying out, flames and smoke at the centre ------------
//
// The fragments (elements, set up by Create): +0x00 position, +0x0C velocity, +0x18 / +0x1C the
// flame radius and its growth (scaled by +0x20 each tick), +0x24 / +0x28 a random frame and size
// factor, +0x30 the age in ticks.

namespace {

#define EI_BLAST_FLAME_SIZE GameFloat(0x0073CDE0)    // 0.6
#define EI_BLAST_POINT_TWO GameFloat(0x0073BAD4)     // 0.2
#define EI_BLAST_RISE_SCALE GameDouble(0x0073CDD8)   // ~0.35 * 2^-32
#define EI_BLAST_RISE GameFloat(0x0073CDD4)          // 0.35
#define EI_BLAST_RISE_BASE GameFloat(0x0073CDD0)     // 0.175
#define EI_BLAST_HALF GameFloat(0x0073B86C)          // 0.5
#define EI_BLAST_DEBRIS_SCALE GameDouble(0x0073C540) // ~0.4 * 2^-31
#define EI_BLAST_POINT_SEVEN GameFloat(0x0073C538)   // 0.7
#define EI_BLAST_POINT_15 GameFloat(0x0073CD18)      // 0.15
#define EI_BLAST_POINT_THREE GameFloat(0x0073C54C)   // 0.3
#define EI_BLAST_SEVENTH GameFloat(0x0073CDCC)       // 1/7
#define EI_BLAST_POINT_EIGHT GameFloat(0x0073CCA8)   // 0.8
#define EI_BLAST_SCALE_0_4 GameDouble(0x0073CCB0)    // ~0.4 * 2^-32
#define EI_BLAST_CORE_SIZE GameFloat(0x0073CDEC)     // 2.3
#define EI_BLAST_SWELL_LATE GameFloat(0x0073CDE8)    // 1.03
#define EI_BLAST_SWELL_EARLY GameFloat(0x0073CD40)   // 1.01
#define EI_BLAST_FRAME_ACCEL GameFloat(0x0073CDE4)   // 1.02
#define EI_BLAST_DRAG GameFloat(0x0073CD74)          // 0.03
#define EI_BLAST_SLOW GameFloat(0x0073C524)          // 0.75

constexpr int kBlastCore = -1222;     // the core flash, emitted once at the start

const std::int32_t* BlastAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A5DC); }

// A flame's size and frame timing: the size from the fragments' radius, the frame step random.
void BlastFlameTiming(CParticleSource* s, Particle* p) {
    double jitter = Unsigned(Next()) * EI_BLAST_SCALE_0_4 + D(EI_BLAST_POINT_EIGHT);
    p->unknown_2C = Store(D(*ElementFloat(s->elements, 0x18)) * D(s->size) * jitter * D(EI_BLAST_FLAME_SIZE));
    p->unknown_30 = Store(static_cast<double>(p->frame));
}

} // namespace

// 0048C090. FireBlast: a random kind per particle (state = Next() % 29 - 14). The first particle
// after the fragments' first tick is the core flash. Negative kinds are flames inside the blast
// (a random point in the ball scaled by the radius, moving outwards; the earlier in the blast and
// the further out the more frames they skip); the others are smoke along fragment `state`'s path.
int CParticleSource::EmitBlast(Particle* p, int) {
    ParticleElement* fragments = elements;
    int kind = static_cast<int>(Next() % 29) - 14;
    p->color = 0xFFFFFFFF;
    p->state = kind;
    if (particles.count == 0 && *ElementInt(fragments, 0x30) == 1) {
        p->state = kBlastCore;
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
        p->frame = 15;
        SetBits(&p->size, 0);
        return 1;
    }
    if (kind < 0) {
        p->life = 0;
        float x, y, z;
        RandomInBallZYX(x, y, z);
        Vector3 v;
        v.Construct(x, y, z);
        double outside = D(EI_P_ONE) - LengthSquaredUnrounded<Sum3::ZYX, double>(v);
        float outsideRounded = Store(outside);
        double scale = D(size);
        v.x = Store(scale * D(v.x));
        v.y = Store(D(v.y) * scale);
        v.z = Store(D(v.z) * scale);
        double radius = D(*ElementFloat(elements, 0x18));
        float ax = Store(D(v.x) * radius);
        float ay = Store(D(v.y) * radius);
        double az = radius * D(v.z);
        double px = D(ax) + D(origin.x);
        float py = Store(D(ay) + D(origin.y));
        float pz = Store(az + D(origin.z));
        SetBits(&p->size, 0x3F800000);
        p->pos.y = py;
        p->pos.x = Store(px);
        p->pos.z = pz;
        double spreadRate = D(*ElementFloat(elements, 0x1C));
        p->velocity.x = Store(D(v.x) * spreadRate);
        SetBits(&p->size, 0);
        p->velocity.y = Store(D(v.y) * spreadRate);
        p->velocity.z = Store(D(v.z) * spreadRate);
        int age = *ElementInt(fragments, 0x30);
        float threshold = Store(static_cast<double>(age / 7) + D(EI_BLAST_POINT_TWO));
        if (!(outside >= D(threshold))) {
            p->frame = 8;
            BlastFlameTiming(this, p);
            *ParticleFloat(p, 0x34) = Store(Unsigned(Next()) * EI_BLAST_RISE_SCALE + D(EI_BLAST_RISE));
        } else {
            p->frame = static_cast<int>(Next() % 3);
            BlastFlameTiming(this, p);
            double t = Unsigned(Next()) * EI_BLAST_RISE_SCALE * D(EI_BLAST_HALF);
            *ParticleFloat(p, 0x34) =
                Store(t + D(outsideRounded) * D(EI_BLAST_RISE) / D(threshold) + D(EI_BLAST_RISE_BASE));
        }
        return 1;
    }

    p->frame = static_cast<int>(Next() % 3);
    ParticleElement* e = &fragments[kind];
    if (*ElementFloat(e, 0x24) == 0.0f || x87::IsNaN(*ElementFloat(e, 0x24))) p->frame = 8;
    float u = Store(Unsigned(Next()) * EI_P_SCALE_1);
    float scale = size;
    const float* velocity = ElementFloat(e, 0x0C);
    double ux = D(u) * D(velocity[0]);
    float uy = Store(D(u) * D(velocity[1]));
    float uz = Store(D(u) * D(velocity[2]));
    double sx = ux * D(scale);
    double sy = D(uy) * D(scale);
    double sz = D(uz) * D(scale);
    float px = Store(D(e->pos.x) + D(origin.x));
    float py = Store(D(origin.y) + D(e->pos.y));
    Vector3 start;
    start.Construct(Store(D(px) - sx), Store(D(py) - sy), Store(D(origin.z) + D(e->pos.z) - sz));
    std::memcpy(&p->pos, &start, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    double g = Unsigned(Next()) * EI_BLAST_DEBRIS_SCALE + D(EI_BLAST_POINT_SEVEN);
    double sized = g * (D(EI_BLAST_POINT_THREE) - D(u) * D(EI_BLAST_POINT_15)) *
                   (D(*ElementFloat(e, 0x18)) * D(EI_BLAST_SEVENTH) + D(EI_BLAST_POINT_THREE));
    p->size = Store(sized * D(*ElementFloat(e, 0x28)) * D(size));
    Vector3 drift;
    ScaleVector(&drift, *reinterpret_cast<const Vector3*>(velocity), size);
    std::memcpy(&p->velocity, &drift, sizeof(Vector3));
    return 1;
}
EI_REPLACE_METHOD(0x0048C090, CParticleSource::EmitBlast);

// 0048C590. FireBlast particles. Smoke (state >= 0) drifts with a 0.7 drag, shrinks to 0.8 and
// lasts until its frame reaches 7 (mod 8). The core flash (frame 15) grows once to 2.3 times the
// source's size, then disappears; it keeps the source alive while the fragments are younger than
// 3 ticks. Flames move by their velocity (slowing with the fragments' drag), swell, step through
// frames at an accelerating rate and fade by the frame.
int CParticleSource::UpdateBlast(Particle* p) {
    ParticleElement* fragments = elements;
    int state = p->state;
    if (state >= 0) {
        int frame = ++p->frame;
        if ((frame & 7) == 7) return 0;
        p->color = 0xFFFFFFFF;
        p->pos.x = Store(D(p->velocity.x) + D(p->pos.x));
        p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
        p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
        p->velocity.x = Store(D(p->velocity.x) * D(EI_BLAST_POINT_SEVEN));
        p->velocity.y = Store(D(p->velocity.y) * D(EI_BLAST_POINT_SEVEN));
        p->velocity.z = Store(D(p->velocity.z) * D(EI_BLAST_POINT_SEVEN));
        p->size = Store(D(p->size) * D(EI_BLAST_POINT_EIGHT));
        return 1;
    }
    int frame = p->frame;
    if (frame >= 14) {
        if (state != kBlastCore) return 0;
        if (p->size == 0.0f || x87::IsNaN(p->size)) p->size = Store(D(size) * D(EI_BLAST_CORE_SIZE));
        else SetBits(&p->size, 0);
        return *ElementInt(fragments, 0x30) < 3;
    }
    p->pos.x = Store(D(p->pos.x) + D(p->velocity.x));
    ++p->life;
    p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    double flameSize = D(p->unknown_2C);
    p->size = Store(flameSize);
    p->size = Store(flameSize * D(frame > 7 ? EI_BLAST_SWELL_LATE : EI_BLAST_SWELL_EARLY));
    float* step = ParticleFloat(p, 0x34);
    p->unknown_30 = Store(D(p->unknown_30) + D(*step));
    *step = Store(D(*step) * D(EI_BLAST_FRAME_ACCEL));
    p->frame = x87::fistp(p->unknown_30);
    std::memcpy(&p->unknown_2C, &p->size, sizeof(float));
    double drag = D(*ElementFloat(elements, 0x20)) - D(EI_BLAST_DRAG);
    p->velocity.x = Store(drag * D(p->velocity.x));
    p->velocity.y = Store(drag * D(p->velocity.y));
    p->velocity.z = Store(drag * D(p->velocity.z));
    p->color = (static_cast<std::uint32_t>(BlastAlpha()[p->frame]) << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x0048C590, CParticleSource::UpdateBlast);

// 0048C760. FireBlast fragments: the flame radius grows (its growth decaying), the age counts up
// (the emission stops after 6 ticks), the fragment flies by its velocity scaled by the source's
// size, slowing to 3/4 per tick and falling 0.03 per tick.
void CParticleSource::TickBlast(ParticleElement* e) {
    float* radius = ElementFloat(e, 0x18);
    float* growthRate = ElementFloat(e, 0x1C);
    *radius = Store(D(*radius) + D(*growthRate));
    int age = ++*ElementInt(e, 0x30);
    *growthRate = Store(D(*ElementFloat(e, 0x20)) * D(*growthRate));
    if (age > 6) flags_ &= ~1u;
    double scale = D(size);
    float* velocity = ElementFloat(e, 0x0C);
    float dx = Store(scale * D(velocity[0]));
    float dy = Store(scale * D(velocity[1]));
    double dz = scale * D(velocity[2]);
    e->pos.x = Store(D(dx) + D(e->pos.x));
    e->pos.y = Store(D(dy) + D(e->pos.y));
    e->pos.z = Store(dz + D(e->pos.z));
    velocity[0] = Store(D(velocity[0]) * D(EI_BLAST_SLOW));
    velocity[1] = Store(D(velocity[1]) * D(EI_BLAST_SLOW));
    velocity[2] = Store(D(velocity[2]) * D(EI_BLAST_SLOW));
    velocity[2] = Store(D(velocity[2]) - D(EI_BLAST_DRAG));
}
EI_REPLACE_METHOD(0x0048C760, CParticleSource::TickBlast);

// --- PoisonFog (0x2007, 0x2008): a disc of cloud puffs laid on the ground ------------------------

namespace {

#define EI_FOG_CELL GameFloat(0x0073CB34)            // 1.0: the grid spacing
#define EI_FOG_QUARTER GameFloat(0x0073C3B0)         // 0.25
#define EI_FOG_JITTER_LOW GameFloat(0x0073CDA8)      // -0.25
#define EI_FOG_SIZE_SCALE GameDouble(0x0073CDA0)     // ~0.5 * 2^-32
#define EI_FOG_SIZE_BASE GameFloat(0x0073CB30)       // 1.0
#define EI_FOG_ALPHA GameFloat(0x0073CD98)           // 160.0

} // namespace

// 0048B080. PoisonFog: emits nothing itself. When the source has no particles it lays one puff
// per grid cell (jittered by a quarter cell) inside the disc of radius `size` around its position,
// on the ground, straight into its list (from the pool), and grows the bounds. Always 0.
int CParticleSource::EmitPoisonFog(Particle*, int) {
    if (particles.count != 0) return 0;
    float radius = size;
    int n = GameFtol((D(size) + D(EI_P_ONE)) / D(EI_FOG_CELL));
    for (int i = -n; i <= n; ++i) {
        float row = Store(static_cast<double>(i));
        float radiusSquared = Store(D(radius) * D(radius));
        for (int j = -n; j <= n; ++j) {
            float x = Store(static_cast<double>(j) * D(EI_FOG_CELL));
            float y = Store(D(row) * D(EI_FOG_CELL));
            float high = Store(D(EI_FOG_CELL) * D(EI_FOG_QUARTER));
            float low = Store(D(EI_FOG_CELL) * D(EI_FOG_JITTER_LOW));
            x = Store(Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1 + D(low) + D(x));
            double yu = Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1 + D(low) + D(y);
            y = Store(yu);
            if (yu * D(y) + D(x) * D(x) >= D(radiusSquared)) continue;
            Particle* q = static_cast<CParticleManager*>(owner_)->AcquireParticle();
            if (!q) continue;
            double px = D(x) + D(position.x);
            x = Store(px);
            y = Store(D(y) + D(position.y));
            q->pos.x = Store(px);
            q->pos.y = y;
            q->size = Store(Unsigned(Next()) * EI_FOG_SIZE_SCALE + D(EI_FOG_SIZE_BASE) - D(EI_FOG_QUARTER));
            q->pos.z = Store(GroundHeight(x, y) + D(q->size));
            if (type_ == 0x2008) {
                q->color = 0xFFFFAF7F;
            } else {
                int a = x87::fistp(Store(D(alpha) * D(EI_FOG_ALPHA)));
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
            boundsMin.x = PickIfLess(q->pos.x, boundsMin.x);
            boundsMin.y = PickIfLess(q->pos.y, boundsMin.y);
            boundsMin.z = PickIfLess(q->pos.z, boundsMin.z);
            boundsMax.x = !(boundsMax.x >= q->pos.x) ? q->pos.x : boundsMax.x;
            boundsMax.y = !(boundsMax.y >= q->pos.y) ? q->pos.y : boundsMax.y;
            boundsMax.z = !(boundsMax.z >= q->pos.z) ? q->pos.z : boundsMax.z;
            particles.AddTail(q);
        }
    }
    return 0;
}
EI_REPLACE_METHOD(0x0048B080, CParticleSource::EmitPoisonFog);

// --- Geyser (0x2009): three vents that erupt in turn -----------------------------------------------
//
// Each element is a vent: +0x00 / +0x04 the jet's direction angles (degrees off vertical, turned to
// radians), +0x08 its strength, +0x30 the pause before the next eruption, +0x34 the eruption's
// remaining ticks.

namespace {

#define EI_GEYSER_YAW_SCALE GameDouble(0x0073CD68)   // ~40 * 2^-32 (degrees)
#define EI_GEYSER_YAW_LOW GameFloat(0x0073CCF8)      // 20.0
#define EI_GEYSER_TILT_SCALE GameDouble(0x0073CD60)  // ~20 * 2^-32
#define EI_GEYSER_TILT_LOW GameFloat(0x0073BAAC)     // 10.0
#define EI_GEYSER_JET_SCALE GameDouble(0x0073CD58)   // ~0.2 * 2^-32
#define EI_GEYSER_JET_LOW GameFloat(0x0073B850)      // 0.1
#define EI_GEYSER_PI GameDouble(0x0073CAE8)          // pi
#define EI_GEYSER_PER_DEGREE GameDouble(0x0073BA30)  // 1/180
#define EI_GEYSER_DROP GameFloat(0x0073BAD4)         // 0.2
#define EI_GEYSER_HALF GameFloat(0x0073B86C)         // 0.5
#define EI_GEYSER_MIST GameFloat(0x0073C548)         // 0.01
#define EI_GEYSER_MARK_SCALE GameDouble(0x0073CD78)  // ~0.0146 * 2^-32
#define EI_GEYSER_MARK_BASE GameFloat(0x0073CD74)    // 0.03
#define EI_GEYSER_SHRINK GameFloat(0x0073CD70)       // 0.9
#define EI_GEYSER_GRAVITY GameFloat(0x0073CCB8)      // 0.06
#define EI_GEYSER_ANGLE_SCALE GameDouble(0x0073CD90) // ~360 * 2^-32
#define EI_GEYSER_TILT_RANGE GameDouble(0x0073CD88)  // ~10 * 2^-32
#define EI_GEYSER_POWER_SCALE GameDouble(0x0073CD80) // ~0.6 * 2^-32
#define EI_GEYSER_POWER_BASE GameFloat(0x0073B868)   // 0.4

// A random value in [low, high) of the spread box component, added to `base` (the original's
// order: the random part, the base, then the low end).
float SpreadAround(float base, float low, float high) {
    return Store(Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1 + D(base) + D(low));
}

} // namespace

// 0048A8F0. Geyser: the first particle marks the source. Then a drop from a random vent: while
// the vent erupts it flies along the vent's direction (jittered by up to 20 / 10 degrees) with the
// vent's strength; otherwise a short-lived puff of mist near the source.
int CParticleSource::EmitGeyser(Particle* p, int) {
    int vent = static_cast<int>(Next() % static_cast<std::uint32_t>(elementCount));
    p->color = 0xFFFFFFFF;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    if (particles.count == 0) {
        SetBits(&p->size, 0);
        p->state = 1;
        p->frame = 15;
        return 1;
    }
    p->state = 0;
    p->frame = static_cast<int>(Next() % 12);
    const ParticleElement& e = elements[vent];
    float drop = Store(D(size) * D(EI_GEYSER_DROP));
    p->life = 20;
    p->size = drop;
    if (e.ticks > 0) {
        p->life = 0x7FFFFFFF;
        double radian = EI_GEYSER_PI * EI_GEYSER_PER_DEGREE;
        float yaw = Store((Unsigned(Next()) * EI_GEYSER_YAW_SCALE - D(EI_GEYSER_YAW_LOW)) * radian + D(e.pos.x));
        float tilt = Store((Unsigned(Next()) * EI_GEYSER_TILT_SCALE - D(EI_GEYSER_TILT_LOW)) * radian + D(e.pos.y));
        double power = Unsigned(Next()) * EI_GEYSER_JET_SCALE + D(e.pos.z) - D(EI_GEYSER_JET_LOW);
        double sinTilt = x87::fsin(D(tilt));
        p->drift.x = Store(x87::fsin(D(yaw)) * sinTilt * power * D(height));
        p->drift.y = Store(x87::fcos(D(yaw)) * sinTilt * power * D(height));
        p->drift.z = Store(x87::fcos(D(tilt)) * power * D(height));
        p->pos.x = SpreadAround(p->pos.x, spreadMin.x, spreadMax.x);
        p->pos.y = SpreadAround(p->pos.y, spreadMin.y, spreadMax.y);
        float z = SpreadAround(p->pos.z, spreadMin.z, spreadMax.z);
        SetBits(&p->size, 0);
        p->velocity.x = drop;
        p->pos.z = z;
        return 1;
    }
    p->life = 1;
    std::memset(&p->drift, 0, sizeof(Vector3));
    float high = Store(D(spreadMax.x) * D(EI_GEYSER_HALF));
    float low = Store(D(spreadMin.x) * D(EI_GEYSER_HALF));
    p->pos.x = SpreadAround(p->pos.x, low, high);
    low = Store(D(spreadMin.y) * D(EI_GEYSER_HALF));
    high = Store(D(spreadMax.y) * D(EI_GEYSER_HALF));
    p->pos.y = SpreadAround(p->pos.y, low, high);
    p->size = Store(D(size) * D(EI_GEYSER_MIST));
    return 1;
}
EI_REPLACE_METHOD(0x0048A8F0, CParticleSource::EmitGeyser);

// 0048ADA0. Geyser: the marker flickers while the source emits (dropped once it stops); drops fly
// by +0x38 (gravity pulling +0x40 down by 0.06 height per tick), fade 8 alpha per tick (dropped
// below 10), shrink by 0.9 (+0x20 holds the size) and cycle 4 frames.
int CParticleSource::UpdateGeyser(Particle* p) {
    if (p->state != 0) {
        if (!(flags_ & 1)) {
            SetBits(&p->size, 0);
            return 0;
        }
        p->size = Store((Unsigned(Next()) * EI_GEYSER_MARK_SCALE + D(EI_GEYSER_MARK_BASE)) * D(size));
        return 1;
    }
    std::uint32_t a = p->color >> 24;
    if (a <= 9) return 0;
    if (--p->life <= 0) return 0;
    double shrunk = D(p->velocity.x) * D(EI_GEYSER_SHRINK);
    p->velocity.x = Store(shrunk);
    p->pos.x = Store(D(p->drift.x) + D(p->pos.x));
    int frame = p->frame;
    p->frame = (frame & ~3) | ((frame + 1) & 3);
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    p->pos.z = Store(D(p->pos.z) + D(p->drift.z));
    p->size = Store(shrunk);
    p->color = ((a << 24) - 0x08000001u) | 0xFFFFFF;
    p->drift.z = Store(D(p->drift.z) - D(height) * D(EI_GEYSER_GRAVITY));
    return 1;
}
EI_REPLACE_METHOD(0x0048ADA0, CParticleSource::UpdateGeyser);

// 0048AEC0. Geyser vents: an eruption counts down, then a pause of 3..36 ticks, then a new
// eruption of 1..7 ticks in a random direction (up to 10 degrees off vertical) and strength.
void CParticleSource::TickGeyser(ParticleElement* e) {
    if (e->ticks > 0) {
        if (--e->ticks == 0) *ElementInt(e, 0x30) = static_cast<int>(Next() % 34) + 3;
        return;
    }
    if (--*ElementInt(e, 0x30) > 0) return;
    e->ticks = static_cast<int>(Next() % 7) + 1;
    double radian = EI_GEYSER_PI * EI_GEYSER_PER_DEGREE;
    float yaw = Store(Unsigned(Next()) * EI_GEYSER_ANGLE_SCALE * radian);
    float tilt = Store(Unsigned(Next()) * EI_GEYSER_TILT_RANGE * radian);
    float power = Store(Unsigned(Next()) * EI_GEYSER_POWER_SCALE + D(EI_GEYSER_POWER_BASE));
    e->pos.x = yaw;
    e->pos.y = tilt;
    e->pos.z = power;
}
EI_REPLACE_METHOD(0x0048AEC0, CParticleSource::TickGeyser);

// --- Shower (0x200F, 0x2031-0x2033): sparks thrown out in a few random directions ---------------
//
// Element 0's +0x34 is the texture frame of the kind (12-15, set by Create) and +0x30 is negative
// until the directions are chosen; then each element's position is a unit direction and +0x30 its
// age in ticks.

namespace {

#define EI_SHOWER_RAYS GameFloat(0x0073CE50)         // 8.0 directions per unit of alpha
#define EI_SHOWER_JITTER_SCALE GameDouble(0x0073CE48) // ~0.06 * 2^-32
#define EI_SHOWER_JITTER_LOW GameFloat(0x0073CD74)   // 0.03
#define EI_SHOWER_SPEED GameFloat(0x0073CE40)        // 1/6
#define EI_SHOWER_FAST GameFloat(0x0073C51C)         // 1.5
#define EI_SHOWER_GRAVITY GameFloat(0x0073C52C)      // 0.05

const std::int32_t* ShowerAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A6B4); }

// alpha * 8 random unit directions (at least one) in the elements.
void ChooseShowerRays(CParticleSource* s) {
    s->frameDelay = x87::fistp(Store(D(s->alpha) * D(EI_SHOWER_RAYS)));
    if (s->frameDelay < 1) s->frameDelay = 1;
    int rays = s->frameDelay;
    s->emitCount = rays * 10;
    SetElementCount(s, rays);
    for (int i = 0; i < s->frameDelay; ++i) {
        float x, y, z;
        double lengthSquared;
        for (int tries = 10;;) {
            x = Store(Range(-1.0f, 1.0f));
            y = Store(Range(-1.0f, 1.0f));
            double zu = Range(-1.0f, 1.0f);
            z = Store(zu);
            lengthSquared = zu * D(z) + D(y) * D(y) + D(x) * D(x);
            if (!(lengthSquared >= D(EI_P_ONE))) break;
            if (!(--tries > 0)) break;
        }
        float length = Store(x87::fsqrt(lengthSquared));
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        std::memcpy(&s->elements[i].pos, &direction, sizeof(Vector3));
        *ElementInt(&s->elements[i], 0x30) = 0;
    }
}

} // namespace

// 0048EEA0. Shower: the first call chooses the directions. A spark leaves the source along a
// random direction (jittered by +-0.03, scaled to the source's size and 1/6 of it per tick, one in
// eight half as fast again), already a random part of its first step out.
int CParticleSource::EmitShower(Particle* p, int) {
    if (*ElementInt(elements, 0x30) < 0) ChooseShowerRays(this);
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frame = *ElementInt(elements, 0x34);
    float a = Store(Unsigned(Next()) * EI_SHOWER_JITTER_SCALE - D(EI_SHOWER_JITTER_LOW));
    float b = Store(Unsigned(Next()) * EI_SHOWER_JITTER_SCALE - D(EI_SHOWER_JITTER_LOW));
    float c = Store(Unsigned(Next()) * EI_SHOWER_JITTER_SCALE - D(EI_SHOWER_JITTER_LOW));
    const ParticleElement& ray = elements[Next() % static_cast<std::uint32_t>(frameDelay)];
    float scale = size;
    double ax = D(a) + D(ray.pos.x);
    b = Store(D(b) + D(ray.pos.y));
    c = Store(D(c) + D(ray.pos.z));
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    std::memcpy(&p->size, &unknown_11C, sizeof(float));
    a = Store(D(scale) * ax);
    b = Store(D(scale) * D(b));
    c = Store(D(scale) * D(c));
    float speed = Store(D(EI_P_ONE) / x87::fsqrt(D(c) * D(c) + D(b) * D(b) + D(a) * D(a)) * D(size) * D(EI_SHOWER_SPEED));
    if ((Next() & 7) == 0) speed = Store(D(speed) * D(EI_SHOWER_FAST));
    p->drift.x = Store(D(speed) * D(a));
    SetBits(&p->velocity.x, 0x3F666666);     // 0.9: the speed kept per tick
    SetBits(&p->velocity.y, 0x3F4CCCCD);     // 0.8: the size kept per tick
    p->drift.y = Store(D(speed) * D(b));
    p->drift.z = Store(D(speed) * D(c));
    double t = Unsigned(Next()) * EI_P_SCALE_1;
    float dx = Store(t * D(p->drift.x));
    float dy = Store(t * D(p->drift.y));
    double dz = t * D(p->drift.z);
    p->pos.x = Store(D(dx) + D(p->pos.x));
    p->pos.y = Store(D(dy) + D(p->pos.y));
    p->pos.z = Store(dz + D(p->pos.z));
    return 1;
}
EI_REPLACE_METHOD(0x0048EEA0, CParticleSource::EmitShower);

// 0048F300. Shower sparks move, slow down (+0x20), fall by 0.05 size per tick, shrink (+0x24) and
// fade by the life table.
int CParticleSource::UpdateShower(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    p->pos.x = Store(D(p->drift.x) + D(p->pos.x));
    p->color = (static_cast<std::uint32_t>(ShowerAlpha()[life]) << 24) | 0xFFFFFF;
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    p->pos.z = Store(D(p->pos.z) + D(p->drift.z));
    p->size = Store(D(p->velocity.y) * D(p->size));
    double keep = D(p->velocity.x);
    p->drift.x = Store(keep * D(p->drift.x));
    p->drift.y = Store(keep * D(p->drift.y));
    p->drift.z = Store(keep * D(p->drift.z));
    p->drift.z = Store(D(p->drift.z) - D(size) * D(EI_SHOWER_GRAVITY));
    return 1;
}
EI_REPLACE_METHOD(0x0048F300, CParticleSource::UpdateShower);

// 0048F380. Shower directions age; after 5 ticks the source stops emitting and lets go of its
// carrier.
void CParticleSource::TickShower(ParticleElement* e) {
    if (++*ElementInt(e, 0x30) > 5) {
        flags_ &= ~1u;
        Attach(nullptr);
    }
}
EI_REPLACE_METHOD(0x0048F380, CParticleSource::TickShower);

// --- Bag (0x203C): eighty coloured balls bouncing around the carrier ------------------------------
//
// Each element is a ball: +0x00 position, +0x0C its last step, +0x18 velocity, +0x30 its age
// (indexing the size table 007A9BFC and, for the first three, the colour table 0073CBA4),
// +0x34 the ticks until it is thrown again (-13: no carrier, the ball is gone).

namespace {

#define EI_BAG_SHRINK GameFloat(0x0073CF40)          // 0.93 per age
#define EI_BAG_SPREAD_LOW GameFloat(0x0073CF44)      // -0.2
#define EI_BAG_SPREAD_HIGH GameFloat(0x0073BAD4)     // 0.2
#define EI_BAG_SPARK_SIZE GameFloat(0x0073B850)      // 0.1
#define EI_BAG_BALL_SIZE GameFloat(0x0073CD18)       // 0.15
#define EI_BAG_KICK GameFloat(0x0073C52C)            // 0.05

float* BagSizes() { return reinterpret_cast<float*>(0x007A9BFC); }     // 12 entries
const std::int32_t* BagColours() { return reinterpret_cast<const std::int32_t*>(0x0073CBA4); }   // r, g, b
const std::int32_t* BagAlpha() { return reinterpret_cast<const std::int32_t*>(0x0073CC34); }
constexpr int kBagGone = -13;

std::uint32_t BagRgb(int kind) {
    const std::int32_t* c = BagColours() + kind * 3;
    return ((static_cast<std::uint32_t>(c[0]) << 8 | static_cast<std::uint32_t>(c[1])) << 8) | static_cast<std::uint32_t>(c[2]);
}

// A random value of the throw box (+-0.2 of the size... from -0.2 * size to 0.2 * size).
double BagSpread(const CParticleSource* s, float& low, float& high) {
    high = Store(D(s->size) * D(EI_BAG_SPREAD_HIGH));
    low = Store(D(s->size) * D(EI_BAG_SPREAD_LOW));
    return Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1;
}

} // namespace

// 00493560. Bag: the first 80 particles are the balls (state = their index; the first one fills the
// size table: 0.93 to the power of the age); later ones are sparks along a random ball's last step,
// in the ball's colour.
int CParticleSource::EmitBag(Particle* p, int index) {
    if (particles.count < 80) {
        if (index == 0) {
            double t = D(EI_P_ONE);
            for (int k = 0; k < 12; ++k) {
                BagSizes()[k] = Store(t);
                t = t * D(EI_BAG_SHRINK);
            }
        }
        std::uint32_t r = Next();
        p->state = particles.count;
        p->color = 0xFFFFFF;
        p->life = 15;
        p->frame = static_cast<int>(r & 3);
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
        SetBits(&p->size, 0);
        return 1;
    }
    const ParticleElement& ball = elements[Next() % 80];
    int age = *reinterpret_cast<const std::int32_t*>(&ball.value[9]);
    p->color = BagRgb(age);
    int life = ball.ticks + 1;
    if (life > 5) life = 5;
    p->life = life;
    p->frame = 4;
    p->state = -1;
    double u = Unsigned(Next()) * EI_P_SCALE_1 - D(EI_P_ONE);
    const float* step = &ball.value[0];
    float ax = Store(u * D(step[0]));
    float ay = Store(u * D(step[1]));
    double az = u * D(step[2]);
    double px = D(ax) + D(ball.pos.x);
    float py = Store(D(ay) + D(ball.pos.y));
    float pz = Store(az + D(ball.pos.z));
    SetBits(&p->size, 0x3F800000);
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store(D(BagSizes()[age]) * D(size) * D(EI_BAG_SPARK_SIZE));
    return 1;
}
EI_REPLACE_METHOD(0x00493560, CParticleSource::EmitBag);

// 00493770. Bag: a spark fades by its life and steps a frame; a ball follows its element (dropped
// once the ball is gone), cycles 4 frames, takes the colour and size of its age, and while the
// ball rests (no last step) its previous position is pinned to the current one.
int CParticleSource::UpdateBag(Particle* p) {
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
    std::memcpy(&p->pos, &ball->pos, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    int age = *ElementInt(ball, 0x30);
    p->size = Store(D(BagSizes()[age]) * D(size) * D(EI_BAG_BALL_SIZE));
    int fade = p->life > 9 ? 9 : p->life;
    p->color = (static_cast<std::uint32_t>(BagAlpha()[fade]) << 24) | BagRgb(age);
    const float* step = ElementFloat(ball, 0x0C);
    auto isZero = [](float f) { return f == 0.0f || x87::IsNaN(f); };
    if (isZero(step[0]) && isZero(step[1]) && isZero(step[2])) {
        std::memcpy(&p->prevPos, &p->pos, sizeof(Vector3) + sizeof(float));
        p->prevColor = p->color;
        p->size = Store(D(size) * D(EI_BAG_BALL_SIZE));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00493770, CParticleSource::UpdateBag);

// 004938C0. Bag balls: when a ball's countdown runs out it is thrown again from the source (a
// random kind 0-2, a random offset, a random velocity of 0.2 size) and rests 7-9 ticks; while
// flying it ages, now and then gets a random kick, and moves (its step is kept). Without a
// carrier the ball is gone.
void CParticleSource::TickBag(ParticleElement* e) {
    if (--e->ticks < 0) {
        if (!attachedTo) {
            e->ticks = kBagGone;
            return;
        }
        *ElementInt(e, 0x30) = static_cast<int>(Next() % 3);
        std::memcpy(&e->pos, &origin, sizeof(Vector3));
        float low, high;
        double r = BagSpread(this, low, high);
        e->pos.x = Store(r + D(e->pos.x) + D(low));
        r = BagSpread(this, low, high);
        e->pos.y = Store(r + D(low) + D(e->pos.y));
        r = BagSpread(this, low, high);
        e->pos.z = Store(r + D(e->pos.z) + D(low));
        float x, y, z;
        RandomInBallZYX(x, y, z);
        Vector3 v;
        v.Construct(x, y, z);
        double scale = D(size);
        float sx = Store(D(v.x) * scale);
        float sy = Store(D(v.y) * scale);
        Vector3 velocity;
        velocity.Construct(Store(D(sx) * D(EI_BAG_SPREAD_HIGH)), Store(D(sy) * D(EI_BAG_SPREAD_HIGH)),
                           Store(scale * D(v.z) * D(EI_BAG_SPREAD_HIGH)));
        std::memcpy(ElementFloat(e, 0x18), &velocity, sizeof(Vector3));
        e->ticks = static_cast<int>(Next() % 3) + 7;
        std::memset(ElementFloat(e, 0x0C), 0, sizeof(Vector3));
        return;
    }
    ++*ElementInt(e, 0x30);
    if (Next() % 6 == 0) {
        float x, y, z;
        for (int tries = 10;;) {
            x = Store(Range(-1.0f, 1.0f));
            y = Store(Range(-1.0f, 1.0f));
            z = Store(Range(-1.0f, 1.0f));
            double lengthSquared = LengthSquaredUnrounded<Sum3::XYZ, double>(x, y, z);   // (y + x) + z
            if (!(lengthSquared >= D(EI_P_ONE))) break;
            if (!(--tries > 0)) break;
        }
        Vector3 v;
        v.Construct(x, y, z);
        double scale = D(size);
        Vector3 w;
        w.Construct(Store(D(v.x) * scale), Store(D(v.y) * scale), Store(D(v.z) * scale));
        Vector3 kick;
        kick.Construct(Store(D(w.x) * D(EI_BAG_KICK)), Store(D(w.y) * D(EI_BAG_KICK)), Store(D(w.z) * D(EI_BAG_KICK)));
        float* velocity = ElementFloat(e, 0x18);
        velocity[0] = Store(D(kick.x) + D(velocity[0]));
        velocity[1] = Store(D(kick.y) + D(velocity[1]));
        velocity[2] = Store(D(kick.z) + D(velocity[2]));
    }
    const Vector3& velocity = *reinterpret_cast<const Vector3*>(ElementFloat(e, 0x18));
    e->pos.AddInPlace(velocity);
    std::memcpy(ElementFloat(e, 0x0C), &velocity, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x004938C0, CParticleSource::TickBag);

// --- 0x202F: sparks rising in a ring around the carrier ------------------------------------------
//
// Element 0: +0x18 the ring radius, +0x20 the carrier's height, +0x24 the ring's offset (z below
// the carrier's centre), +0x34 its age.

namespace {

#define EI_COLUMN_FOOT GameFloat(0x0073C54C)         // 0.3
#define EI_COLUMN_RADIUS GameFloat(0x0073CCA8)       // 0.8
#define EI_COLUMN_RISE_SCALE GameDouble(0x0073CE10)  // ~0.03 * 2^-32
#define EI_COLUMN_RISE_BASE GameFloat(0x0073C538)    // 0.7
#define EI_COLUMN_RISE GameFloat(0x0073B850)         // 0.1
#define EI_COLUMN_RING_SCALE GameDouble(0x0073CED8)  // ~0.2 * 2^-32
#define EI_COLUMN_RING_BASE GameFloat(0x0073CD70)    // 0.9
#define EI_COLUMN_ANGLE_SCALE GameDouble(0x0073CED0) // ~2 pi * 2^-32
#define EI_COLUMN_SIZE_SCALE GameDouble(0x0073CEB0)  // ~0.8 * 2^-32
#define EI_COLUMN_SIZE_BASE GameFloat(0x0073CDE0)    // 0.6
#define EI_COLUMN_SIZE GameFloat(0x0073C3B0)         // 0.25
#define EI_COLUMN_SHRINK GameFloat(0x0073CEC8)       // 0.97
#define EI_COLUMN_SPEED_UP GameFloat(0x0073CEE0)     // 1.05

// The alpha by remaining life, read backwards from 0078A960.
std::uint32_t ColumnAlpha(int life) {
    return static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t*>(0x0078A960 - 4 * life));
}

// The ring from the carrier's figure (the first emission). False without a carrier.
bool SetUpColumn(CParticleSource* s) {
    if (!s->attachedTo) {
        DebugMessage(NoCarrierCasting());
        return false;
    }
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    float height = Store(D(radius) + D(radius));
    float foot = Store(-D(offset.z) - D(height) * D(EI_COLUMN_FOOT));
    radius = Store(D(radius) * D(EI_COLUMN_RADIUS));
    double ring = ClassOf(s->attachedTo) == kUnitClass ? UnitScale(s->attachedTo) : D(radius);
    radius = Store(ring + ring);
    SetElementCount(s, 1);
    ParticleElement* e = s->elements;
    *ElementInt(e, 0x30) = 0;
    *ElementFloat(e, 0x18) = radius;
    SetBits(ElementFloat(e, 0x1C), 0);
    *ElementFloat(e, 0x20) = height;
    SetBits(ElementFloat(e, 0x24), 0);
    SetBits(ElementFloat(e, 0x28), 0);
    *ElementFloat(e, 0x2C) = foot;
    e->ticks = 1;
    return true;
}

} // namespace

// 00491420. 0x202F: a spark on a ring of random radius around the carrier (0.9-1.1 of the ring),
// at a random height of the spread box above the ring's foot, rising faster and faster.
int CParticleSource::EmitColumn(Particle* p, int) {
    if (elementCount == 0 && !SetUpColumn(this)) return 0;
    p->color = 0xFFFFFFFF;
    p->frame = 1;
    p->life = 13;
    p->frameTimer = 1;
    *ParticleFloat(p, 0x34) = Store((Unsigned(Next()) * EI_COLUMN_RISE_SCALE + D(EI_COLUMN_RISE_BASE)) *
                                    D(*ElementFloat(elements, 0x20)) * D(EI_COLUMN_RISE));
    float ring = Store(Unsigned(Next()) * EI_COLUMN_RING_SCALE + D(EI_COLUMN_RING_BASE));
    double angle = Unsigned(Next()) * EI_COLUMN_ANGLE_SCALE;
    p->velocity.x = Store(x87::fsin(angle) * D(ring));
    p->velocity.y = Store(x87::fcos(angle) * D(ring));
    double z = Unsigned(Next()) * (D(spreadMax.z) - D(spreadMin.z)) * EI_P_SCALE_1 + D(spreadMin.z);
    p->velocity.z = Store(z);
    p->velocity.z = Store(z + D(*ElementFloat(elements, 0x2C)));
    double px = D(origin.x) + D(p->velocity.x);
    float py = Store(D(p->velocity.y) + D(origin.y));
    float pz = Store(D(p->velocity.z) + D(origin.z));
    SetBits(&p->size, 0x3F800000);
    p->pos.y = py;
    p->pos.z = pz;
    p->pos.x = Store(px);
    p->size = Store((Unsigned(Next()) * EI_COLUMN_SIZE_SCALE + D(EI_COLUMN_SIZE_BASE)) * D(size) * D(EI_COLUMN_SIZE));
    return 1;
}
EI_REPLACE_METHOD(0x00491420, CParticleSource::EmitColumn);

// 00491830. 0x202F sparks: fade by life, step a frame, follow the source at their offset, shrink
// to 0.97 per tick and rise (the rise growing by 5% per tick).
int CParticleSource::UpdateColumn(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    double oldSize = D(p->size);
    p->color = (ColumnAlpha(life) << 24) | 0xFFFFFF;
    ++p->frame;
    double px = D(origin.x) + D(p->velocity.x);
    float py = Store(D(origin.y) + D(p->velocity.y));
    float pz = Store(D(origin.z) + D(p->velocity.z));
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store(oldSize * D(EI_COLUMN_SHRINK));
    float* rise = ParticleFloat(p, 0x34);
    p->velocity.z = Store(D(p->velocity.z) + D(*rise));
    *rise = Store(D(*rise) * D(EI_COLUMN_SPEED_UP));
    return 1;
}
EI_REPLACE_METHOD(0x00491830, CParticleSource::UpdateColumn);

// 004918D0. 0x202F: element 0 ages.
void CParticleSource::TickColumn(ParticleElement* e) { ++e->ticks; }
EI_REPLACE_METHOD(0x004918D0, CParticleSource::TickColumn);

// --- Mushroom and Transform (0x200E, 0x2052): the cloud of the mushroom ---------------------------
//
// Element 0 (its tick is TickMushroom, effects.cpp): +0x00 the spread of new puffs, +0x08 the cap's
// height, +0x18 the stem's foot, +0x34 the age.

namespace {

#define EI_MUSHROOM_ONE GameDouble(0x0073BAC8)       // 1.0
#define EI_MUSHROOM_SPREAD GameDouble(0x0073CCD0)    // 0.2
#define EI_MUSHROOM_W_SCALE GameDouble(0x0073CCB0)   // ~0.4 * 2^-32
#define EI_MUSHROOM_W_BASE GameFloat(0x0073CCA8)     // 0.8
#define EI_MUSHROOM_UP_SCALE GameDouble(0x0073CCC8)  // ~0.9 * 2^-32
#define EI_MUSHROOM_UP_BASE GameFloat(0x0073B850)    // 0.1
#define EI_MUSHROOM_UP GameFloat(0x0073CCC0)         // 0.12
#define EI_MUSHROOM_GROW GameFloat(0x0073CCBC)       // 0.008 per tick of age
#define EI_MUSHROOM_SPEED GameFloat(0x0073CCB8)      // 0.06
#define EI_MUSHROOM_SWELL GameFloat(0x0073BAC4)      // 1.1
#define EI_MUSHROOM_DRAG GameFloat(0x0073C538)       // 0.7
#define EI_MUSHROOM_PULL GameFloat(0x0073CCD8)       // 0.02

} // namespace

// 004889D0. Mushroom: a puff from the source in a random upward direction (a fixed spread of 0,
// as ln 1 = 0), sized and framed by the cloud's age.
int CParticleSource::EmitMushroom(Particle* p, int) {
    if (elementCount == 0) {
        SetElementCount(this, 1);
        ParticleElement* e = elements;
        e->ticks = 0;
        e->pos.x = Store(Ln(EI_MUSHROOM_ONE) * EI_MUSHROOM_SPREAD * D(size));
        SetBits(&elements[0].pos.z, 0);
        std::memcpy(ElementFloat(elements, 0x18), &origin, sizeof(Vector3));
    }
    float spread = Store((Unsigned(Next()) * EI_MUSHROOM_W_SCALE + D(EI_MUSHROOM_W_BASE)) * D(elements[0].pos.x));
    float a = SignedUnit();
    float b = SignedUnit();
    double c = Unsigned(Next()) * EI_MUSHROOM_UP_SCALE + D(EI_MUSHROOM_UP_BASE);
    double k = EI_MUSHROOM_ONE / x87::fsqrt(c * c + D(b) * D(b) + D(a) * D(a));
    float dx = Store(k * D(a));
    p->drift.x = dx;
    float dy = Store(k * D(b));
    p->drift.y = dy;
    double dz = k * c + D(EI_MUSHROOM_UP);
    p->drift.z = Store(dz);
    p->pos.x = Store(D(dx) * D(spread) + D(origin.x));
    p->pos.y = Store(D(dy) * D(spread) + D(origin.y));
    p->pos.z = Store(dz * D(spread) + D(origin.z) + D(elements[0].pos.z));
    p->size = Store(static_cast<double>(elements[0].ticks) * D(EI_MUSHROOM_GROW) + D(EI_MUSHROOM_UP_BASE));
    p->drift.x = Store(D(dx) * D(size) * D(EI_MUSHROOM_SPEED));
    p->drift.y = Store(D(dy) * D(size) * D(EI_MUSHROOM_SPEED));
    p->drift.z = Store(dz * D(size) * D(EI_MUSHROOM_SPEED));
    p->size = Store(D(size) * D(p->size));
    p->life = static_cast<int>(Next() & 3) + 16;
    int bump = static_cast<int>(Next() % 3);
    p->frameTimer = 2;
    p->color = 0x40FFFFFF;
    int frame = elements[0].ticks / 10 + bump - 1;
    p->frame = frame < 0 ? 0 : frame;
    return 1;
}
EI_REPLACE_METHOD(0x004889D0, CParticleSource::EmitMushroom);

// 00488DB0. Mushroom puffs: step a frame every other tick up to frame 14 (swelling 10% per tick
// before frame 8), fade in by 64 per tick then out by 63 over the last 4 ticks, drift with a 0.7
// drag, and while below the cap's height get pulled towards the stem.
int CParticleSource::UpdateMushroom(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    if (p->frame < 14) {
        int frame = p->frame;
        if (--p->frameTimer == 0) {
            p->frameTimer = 1;
            p->frame = frame + 1;
        }
        if (p->frame < 8) p->size = Store(D(p->size) * D(EI_MUSHROOM_SWELL));
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
    p->pos.x = Store(D(p->pos.x) + D(p->drift.x));
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    p->pos.z = Store(D(p->drift.z) + D(p->pos.z));
    double vx = D(p->drift.x) * D(EI_MUSHROOM_DRAG);
    p->drift.x = Store(vx);
    float vy = Store(D(p->drift.y) * D(EI_MUSHROOM_DRAG));
    p->drift.y = vy;
    p->drift.z = Store(D(p->drift.z) * D(EI_MUSHROOM_DRAG));
    const float* stem = ElementFloat(elements, 0x18);
    double dx = D(p->pos.x) - D(stem[0]);
    double dyu = D(p->pos.y) - D(stem[1]);
    float dy = Store(dyu);
    float reach = Store(x87::fsqrt(dyu * D(dy) + dx * dx));
    double dz = D(p->pos.z) - D(stem[2]);
    if (D(elements[0].pos.z) > dz) {
        p->drift.x = Store(vx - dx * D(size) * D(EI_MUSHROOM_PULL) / D(reach));
        p->drift.y = Store(D(vy) - D(dy) * D(size) * D(EI_MUSHROOM_PULL) / D(reach));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00488DB0, CParticleSource::UpdateMushroom);

// --- BlueGas (Stench, 0x2013): gas swirling up around the carrier -------------------------------
//
// Element 0: +0x00 the carrier's figure offset, +0x18 the gas radius, +0x20 the carrier's height.

namespace {

#define EI_GAS_RADIUS GameFloat(0x0073C54C)          // 0.3
#define EI_GAS_RATE GameFloat(0x0073BF34)            // 5.0
#define EI_GAS_ROUND GameFloat(0x0073B86C)           // 0.5
#define EI_GAS_LOW GameFloat(0x0073CDFC)             // -0.3
#define EI_GAS_RISE_LOW GameFloat(0x0073CCD8)        // 0.02
#define EI_GAS_RISE_HIGH GameFloat(0x0073CD74)       // 0.03
#define EI_GAS_SIZE GameFloat(0x0073C538)            // 0.7
#define EI_GAS_SPEED_UP GameFloat(0x0073CDE4)        // 1.02
#define EI_GAS_SWELL GameFloat(0x0073CD40)           // 1.01

const char* NoCarrierGas() { return reinterpret_cast<const char*>(0x0078AF7C); }

} // namespace

// 0048D420. BlueGas: the first call measures the carrier (5 * size + 0.5 puffs per tick). A puff
// starts at a random point of the disc around the carrier's figure, turning about the axis faster
// the nearer it is, and rising.
int CParticleSource::EmitStench(Particle* p, int) {
    if (elementCount == 0) {
        if (!attachedTo) {
            DebugMessage(NoCarrierGas());
            return 0;
        }
        Vector3 offset;
        float radius;
        AttachmentOffset(&offset, &radius);
        double r = D(radius);
        float gasRadius = Store(r * D(EI_GAS_RADIUS));
        float carrierHeight = Store(r + r);
        SetElementCount(this, 1);
        std::memcpy(&elements[0].pos, &offset, sizeof(Vector3));
        *ElementFloat(elements, 0x18) = gasRadius;
        *ElementFloat(elements, 0x20) = carrierHeight;
        int rate = GameFtol(D(size) * D(EI_GAS_RATE) + D(EI_GAS_ROUND));
        emitCount = rate < 1 ? 1 : rate;
    }
    float radius = *ElementFloat(elements, 0x18);
    float carrierHeight = *ElementFloat(elements, 0x20);
    float t = Store(Unsigned(Next()) * D(radius) * EI_P_SCALE_1);
    float twoPi = Store(GameDouble(0x0073CAE8) + GameDouble(0x0073CAE8));
    double angle = Unsigned(Next()) * D(twoPi) * EI_P_SCALE_1;
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float turn = Store(D(t) * D(EI_GAS_RADIUS) / D(radius));
    float low = Store(D(radius) * D(EI_GAS_LOW));
    float vx = Store(x87::fsin(angle) * D(t));
    float vy = Store(x87::fcos(angle) * D(t));
    float vz = Store(Unsigned(Next()) * (D(radius) * D(EI_GAS_RADIUS) - D(low)) * EI_P_SCALE_1 + D(low));
    p->velocity = {vx, vy, vz};
    float riseLow = Store(D(carrierHeight) * D(EI_GAS_RISE_LOW));
    std::uint32_t r4 = Next();
    p->drift.x = Store(x87::fsin(D(turn)));
    p->drift.y = Store(x87::fcos(D(turn)));
    p->drift.z = Store(Unsigned(r4) * (D(carrierHeight) * D(EI_GAS_RISE_HIGH) - D(riseLow)) * EI_P_SCALE_1 + D(riseLow));
    const ParticleElement& e = elements[0];
    double cx = D(origin.x) + D(e.pos.x);
    float cy = Store(D(origin.y) + D(e.pos.y));
    float cz = Store(D(origin.z) + D(e.pos.z));
    Vector3 start;
    start.x = Store(cx + D(p->velocity.x));
    start.y = Store(D(cy) + D(p->velocity.y));
    start.z = Store(D(cz) + D(p->velocity.z));
    reinterpret_cast<Vector4*>(p)->FromPoint(start);
    p->size = Store(D(radius) * D(EI_GAS_SIZE));
    return 1;
}
EI_REPLACE_METHOD(0x0048D420, CParticleSource::EmitStench);

// 0048D7F0. BlueGas puffs: step through the frames (dropped after the last), turn their offset
// about the axis, rise faster and faster, swell, and fade over the last 8 ticks.
int CParticleSource::UpdateStench(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    p->pos.x = Store(D(p->pos.x) - D(p->velocity.x));
    p->pos.y = Store(D(p->pos.y) - D(p->velocity.y));
    p->pos.z = Store(D(p->pos.z) - D(p->velocity.z));
    double turnedY = Dot2DUnrounded<double>(p->velocity.x, p->velocity.y, p->drift.x, p->drift.y);
    double turnedX = D(p->drift.y) * D(p->velocity.x) - D(p->velocity.y) * D(p->drift.x);
    p->velocity.x = Store(turnedX);
    p->velocity.y = Store(turnedY);
    p->velocity.z = Store(D(p->drift.z) + D(p->velocity.z));
    p->drift.z = Store(D(p->drift.z) * D(EI_GAS_SPEED_UP));
    p->pos.x = Store(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    p->size = Store(D(p->size) * D(EI_GAS_SWELL));
    if (life < 8) p->color = ((p->color & 0xFF000000u) - 0x1F000001u) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x0048D7F0, CParticleSource::UpdateStench);

// --- Tornado (0x200A) and CurseHolder (0x2050): a funnel of rings ---------------------------------
//
// The elements are the funnel's rings (bottom to top): their centres, relative to the source.
// Element 0 also holds +0x24 the funnel's height, +0x28 its bend (0: the rings sway with the
// source's motion; otherwise they lean along it) and +0x2C the particles' spin damping.
// A particle: +0x20 its angle, +0x24 its radius, +0x2C its height (0..1), +0x30 its alpha,
// +0x38 its turn per tick, +0x3C the radius factor per tick, +0x40 its rise per tick.

namespace {

#define EI_FUNNEL_PI_F GameFloat(0x0073BF34)         // 5.0: rings per unit height
#define EI_FUNNEL_PI GameDouble(0x0073CAE8)          // pi
#define EI_FUNNEL_R_SCALE GameDouble(0x0073CD30)     // ~1.2 * 2^-32
#define EI_FUNNEL_R_BASE GameFloat(0x0073CCA8)       // 0.8
#define EI_FUNNEL_R_ADD GameFloat(0x0073BAD4)        // 0.2
#define EI_FUNNEL_TURN_HIGH GameDouble(0x0073CD28)   // 0.07 (times pi)
#define EI_FUNNEL_TURN_LOW GameDouble(0x0073CD20)    // 0.06 (times pi)
#define EI_FUNNEL_TURN_TOP GameFloat(0x0073BAC4)     // 1.1
#define EI_FUNNEL_TURN_SCALE GameFloat(0x0073C51C)   // 1.5
#define EI_FUNNEL_SIZE GameFloat(0x0073C54C)         // 0.3
#define EI_FUNNEL_DUST GameFloat(0x0073CD18)         // 0.15
#define EI_FUNNEL_RISE GameFloat(0x0073CD44)         // 0.0001
#define EI_FUNNEL_SPIN_UP GameFloat(0x0073CD40)      // 1.01
#define EI_FUNNEL_ALPHA GameDouble(0x0073CD38)       // 255.0
#define EI_FUNNEL_FADE GameFloat(0x0073B850)         // 0.1
#define EI_FUNNEL_SWAY GameFloat(0x0073CD50)         // 0.95
#define EI_FUNNEL_HALF_ARC GameDouble(0x0073CC88)    // 0.5
#define EI_FUNNEL_LEAN GameFloat(0x0073CCF8)         // 20.0
#define EI_FUNNEL_BACK GameFloat(0x0073B854)         // -1.0
#define EI_FUNNEL_HALF GameFloat(0x0073B86C)         // 0.5
#define EI_FUNNEL_MAX_LEAN GameFloat(0x0073C548)     // 0.01
#define EI_FUNNEL_ONE GameDouble(0x0073BAC8)         // 1.0
#define EI_FUNNEL_BEND GameDouble(0x0073CD48)        // 0.4

// The funnel's centre at height h (0..1): between ring k and k+1 (h * 5 rings), with the x and y
// blends in the original's two orders; y is returned unrounded.
double FunnelCentre(const CParticleSource* s, double h, float& x) {
    int k = GameFtol(h);
    if (k >= s->elementCount - 2) k = s->elementCount - 2;
    float f = Store(h - static_cast<double>(k));
    float t = PickIfLess(f, 1.0f);
    double u = D(EI_P_ONE) - D(t);
    const ParticleElement& a = s->elements[k];
    const ParticleElement& b = s->elements[k + 1];
    x = Store(D(t) * D(b.pos.x) + u * D(a.pos.x));
    return u * D(a.pos.y) + D(t) * D(b.pos.y);
}

} // namespace

// 00489F70. Tornado: a particle at a random height of the funnel (around the ring centres there),
// with a radius growing towards the top and a turn rate larger near the bottom; one in three is
// dust (state 0, frames 12-15), the others debris sized by their radius.
int CParticleSource::EmitTornado(Particle* p, int) {
    float height01 = Store(Unsigned(Next()) * EI_P_SCALE_1);
    p->state = static_cast<int>(Next() % 3);
    p->color = static_cast<std::uint32_t>(frameDelay);
    std::memcpy(&p->pos, &origin, 2 * sizeof(float));
    p->pos.z = Store(D(origin.z) + D(elements[0].pos.z));
    p->life = static_cast<int>(Next() % 9) + 100;
    float scaled = Store(D(height01) * D(EI_FUNNEL_PI_F));
    int k = x87::fistp(scaled);
    if (k >= elementCount - 2) k = elementCount - 2;
    float f = Store(D(scaled) - static_cast<double>(k));
    float t = PickIfLess(f, 1.0f);
    double u = D(EI_P_ONE) - D(t);
    const ParticleElement& a = elements[k];
    const ParticleElement& b = elements[k + 1];
    float cx = Store(D(t) * D(b.pos.x) + u * D(a.pos.x));
    float cy = Store(u * D(a.pos.y) + D(t) * D(b.pos.y));
    float high = Store(EI_FUNNEL_PI);
    float low = Store(-EI_FUNNEL_PI);
    std::uint32_t r = Next();
    SetBits(&p->unknown_30, 0x3DCCCCCD);     // 0.1
    p->unknown_2C = height01;
    float angle = Store(Unsigned(r) * (D(high) - D(low)) * EI_P_SCALE_1 + D(low));
    double w = D(EI_P_ONE) - D(height01);
    float wRounded = Store(w);
    double w2 = w * w;
    float w4 = Store(w2 * w2);
    p->velocity.x = angle;
    float radius = Store((Unsigned(Next()) * EI_FUNNEL_R_SCALE + D(EI_FUNNEL_R_BASE)) * (D(w4) + D(EI_FUNNEL_R_ADD)) * 2.0);
    p->velocity.y = radius;
    float turnHigh = Store(EI_FUNNEL_PI * EI_FUNNEL_TURN_HIGH);
    float turnLow = Store(EI_FUNNEL_PI * EI_FUNNEL_TURN_LOW);
    double turn = Unsigned(Next()) * (D(turnHigh) - D(turnLow)) * EI_P_SCALE_1 + D(turnLow);
    p->drift.x = Store(turn * (D(EI_FUNNEL_TURN_TOP) - D(wRounded) * D(wRounded)) * D(EI_FUNNEL_TURN_SCALE));
    SetBits(&p->drift.z, 0);
    std::memcpy(&p->drift.y, ElementFloat(elements, 0x2C), sizeof(float));
    p->pos.x = Store(x87::fsin(D(angle)) * D(radius) + D(p->pos.x) + D(cx));
    p->pos.y = Store(x87::fcos(D(angle)) * D(radius) + D(cy) + D(p->pos.y));
    p->pos.z = Store(D(height01) * D(*ElementFloat(elements, 0x24)) + D(p->pos.z));
    if (p->state != 0) {
        p->size = Store((D(radius) + D(EI_FUNNEL_SIZE)) * D(size));
        p->frame = static_cast<int>(Next() % 12);
    } else {
        p->frame = static_cast<int>(Next() & 3) + 12;
        p->size = Store(D(size) * D(EI_FUNNEL_DUST));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00489F70, CParticleSource::EmitTornado);

// 0048A310. Tornado particles rise (dropped above the top), turn around the funnel's centre at
// their height (faster and faster), fade in, fade by the height, and animate.
int CParticleSource::UpdateTornado(Particle* p) {
    if (--p->life == 0) return 0;
    p->unknown_2C = Store(D(p->unknown_2C) + D(p->drift.z));
    p->drift.z = Store(D(p->drift.z) + D(EI_FUNNEL_RISE));
    if (p->unknown_2C > 1.0f) return 0;
    double scaled = D(p->unknown_2C) * D(EI_FUNNEL_PI_F);
    float cx;
    double cy = FunnelCentre(this, scaled, cx);
    p->velocity.x = Store(D(p->velocity.x) + D(p->drift.x));
    p->drift.x = Store(D(p->drift.x) * D(EI_FUNNEL_SPIN_UP));
    float angle = p->velocity.x;
    p->velocity.y = Store(D(p->drift.y) * D(p->velocity.y));
    float radius = p->velocity.y;
    p->pos.x = Store(x87::fsin(D(angle)) * D(radius) + D(origin.x) + D(cx));
    p->pos.y = Store(x87::fcos(D(angle)) * D(radius) + D(origin.y) + cy);
    double base = D(elements[0].pos.z) + D(origin.z);
    p->pos.z = Store(base);
    p->pos.z = Store(D(*ElementFloat(elements, 0x24)) * D(p->unknown_2C) + base);
    if (p->life > 8) p->unknown_30 = PickIfLess(Store(D(p->unknown_30) + D(EI_FUNNEL_FADE)), 1.0f);
    else p->unknown_30 = !(0.1f >= p->unknown_30) ? p->unknown_30 : 0.1f;
    int a = x87::fistp(Store(x87::fsqrt(D(EI_P_ONE) - D(p->unknown_2C)) * D(p->unknown_30) * EI_FUNNEL_ALPHA));
    p->color = (p->color & 0xFFFFFF) | (static_cast<std::uint32_t>(a) << 24);
    if (p->state != 0) {
        p->frame = (p->frame + 1) % 12;
        p->size = Store((D(radius) + D(EI_FUNNEL_SIZE)) * D(size));
    } else {
        p->frame = ((p->frame + 1) & 3) + 12;
    }
    return 1;
}
EI_REPLACE_METHOD(0x0048A310, CParticleSource::UpdateTornado);

// 0048A560. Tornado rings (all updated from element 0's call). Without a bend the rings sway with
// the source's motion (damped 0.95, a half cosine up the funnel); with one they lean along the
// motion (at most 0.01 per tick) and bend with the height. The bottom ring sits 1 above the ground.
// A free source that lost its bend and still has particles stops and keeps going until they die.
void CParticleSource::TickTornado(ParticleElement* e) {
    if (e != elements) return;
    ParticleElement* bottom = elements;
    float bend;
    std::memcpy(&bend, ElementFloat(bottom, 0x28), sizeof(float));
    if (bend == 0.0f || x87::IsNaN(bend)) {
        Vector3 sway;
        sway.Construct(Store(D(bottom->pos.x) - D(motion.x)), Store(D(bottom->pos.y) - D(motion.y)),
                       Store(D(bottom->pos.z) - D(motion.z)));
        sway.x = Store(D(sway.x) * D(EI_FUNNEL_SWAY));
        SetBits(&sway.z, 0);
        sway.y = Store(D(sway.y) * D(EI_FUNNEL_SWAY));
        for (int i = 0; i < elementCount; ++i) {
            double c = x87::fcos(static_cast<double>(i) / static_cast<double>(elementCount - 1) * EI_FUNNEL_PI *
                                 EI_FUNNEL_HALF_ARC);
            ParticleElement& ring = elements[i];
            float x = Store(D(sway.x) * c);
            float y = Store(D(sway.y) * c);
            ring.pos = {x, y, Store(c * D(sway.z))};
        }
        elements[0].pos.z = Store(GroundHeight(origin.x, origin.y) - D(origin.z) + D(EI_P_ONE));
    } else {
        elements[0].pos.z = Store(GroundHeight(origin.x, origin.y) - D(origin.z) + D(EI_P_ONE));
        double mx = D(motion.x) * D(EI_FUNNEL_LEAN);
        float my = Store(D(motion.y) * D(EI_FUNNEL_LEAN));
        float mz = Store(D(motion.z) * D(EI_FUNNEL_LEAN));
        const ParticleElement& b0 = elements[0];
        double sx = mx + D(b0.pos.x);
        float sy = Store(D(my) + D(b0.pos.y));
        float sz = Store(D(mz) + D(b0.pos.z));
        float ax = Store(sx * D(EI_FUNNEL_BACK));
        float ay = Store(D(sy) * D(EI_FUNNEL_BACK));
        float az = Store(D(sz) * D(EI_FUNNEL_BACK));
        Vector3 n = {ax, ay, az};
        double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(ax, ay, az));
        if (!(length == 0.0 || x87::IsNaN(Store(length)))) {
            double inverse = D(EI_P_ONE) / length;
            n.x = Store(D(ax) * inverse);
            n.y = Store(D(ay) * inverse);
            n.z = Store(D(az) * inverse);
        }
        double lean = length * D(EI_FUNNEL_HALF);
        if (lean > D(EI_FUNNEL_MAX_LEAN)) lean = D(EI_FUNNEL_MAX_LEAN);
        Vector3 d;
        d.Construct(Store(D(n.x) * lean), Store(D(n.y) * lean), Store(D(n.z) * lean));
        const ParticleElement& b1 = elements[0];
        float gx = Store(D(d.x) + D(b1.pos.x));
        float gy = Store(D(d.y) + D(b1.pos.y));
        float gz = Store(D(d.z) + D(b1.pos.z));
        for (int i = 0; i < elementCount; ++i) {
            double f = static_cast<double>(i) / static_cast<double>(elementCount - 1);
            double c = x87::fcos(f * EI_FUNNEL_PI);
            double k = (c + (D(EI_P_ONE) - f) * D(*ElementFloat(elements, 0x28)) + EI_FUNNEL_ONE) * EI_FUNNEL_BEND;
            ParticleElement& ring = elements[i];
            float x = Store(D(gx) * k);
            float y = Store(D(gy) * k);
            float z = Store(D(gz) * k);
            ring.pos = {x, y, z};
        }
    }
    if (!attachedTo) {
        float b;
        std::memcpy(&b, ElementFloat(elements, 0x28), sizeof(float));
        if ((b == 0.0f || x87::IsNaN(b)) && particles.count != 0) {
            flags_ &= ~5u;
            Attach(nullptr);
        }
    }
}
EI_REPLACE_METHOD(0x0048A560, CParticleSource::TickTornado);

// --- PortalStar (0x203D): one flickering star ------------------------------------------------------

// 00493D10. PortalStar: the star at the source.
int CParticleSource::EmitPortalStar(Particle* p, int) {
    p->color = 0xFFFFFF;
    p->life = 15;
    p->frame = 0;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0);
    return 1;
}
EI_REPLACE_METHOD(0x00493D10, CParticleSource::EmitPortalStar);

// 00493D50. PortalStar: while the source emits, full colour and a random size of 0.8-1.2 times
// the source's; dropped once it stops.
int CParticleSource::UpdatePortalStar(Particle* p) {
    if (!(flags_ & 1)) return 0;
    p->color = 0xFFFFFFFF;
    p->size = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) * D(size));
    return 1;
}
EI_REPLACE_METHOD(0x00493D50, CParticleSource::UpdatePortalStar);

// --- Cylinder1 / Cylinder2 (0x203F, 0x2040): two stars circling the carrier --------------------
//
// Each element is a star: +0x00 its position (relative to the source), +0x0C its last step,
// +0x18 / +0x1C the cosine and sine of its turn per tick, +0x24 the inverse of its orbit radius,
// +0x28 the orbit radius, +0x2C its rise per tick, +0x34 its countdown (also indexing the alpha
// table 0078AB18).

namespace {

#define EI_CYL_TURN GameFloat(0x0073CC74)            // 0.3
#define EI_CYL_PI GameFloat(0x0073CAF0)              // pi
#define EI_CYL_TURN_SCALE GameFloat(0x0073CF48)      // 0.1333
#define EI_CYL_ORBIT GameFloat(0x0073C538)           // 0.7
#define EI_CYL_LIFT GameFloat(0x0073BAD4)            // 0.2
#define EI_CYL_TWO GameFloat(0x0073CDB8)             // 2.0
#define EI_CYL_RISE GameFloat(0x0073CE90)            // 0.04
#define EI_CYL_RISE_UP GameFloat(0x0073CF50)         // 1.25
#define EI_CYL_TWIST GameFloat(0x0073CF4C)           // -0.01333
#define EI_CYL_SPARK_PULL GameFloat(0x0073CDE0)      // 0.6
#define EI_CYL_SPARK_SHRINK GameFloat(0x0073CD70)    // 0.9

const std::int32_t* CylinderAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078AB18); }

// The stars on the carrier's orbit (the first emission).
void SetUpCylinder(CParticleSource* s) {
    Vector3 offset;
    float radius;
    s->AttachmentOffset(&offset, &radius);
    double turn = D(EI_CYL_TURN) * D(EI_CYL_PI) * D(EI_CYL_TURN_SCALE);
    if (s->type_ == 0x2040) turn = -turn;
    float c = Store(x87::fcos(turn));
    float sn = Store(x87::fsin(turn));
    int count = s->elementCount;
    for (int i = 0; i < s->elementCount; ++i) {
        double orbit = D(radius) * D(EI_CYL_ORBIT);
        double twice = static_cast<double>(2 * i);
        ParticleElement& e = s->elements[i];
        e.pos.x = Store(x87::fcos(twice * D(EI_CYL_PI) / static_cast<double>(count)) * orbit);
        e.pos.y = Store(x87::fsin(twice * D(EI_CYL_PI) / static_cast<double>(s->elementCount)) * orbit);
        e.pos.z = Store(D(radius) * D(EI_CYL_LIFT));
        std::memset(ElementFloat(&e, 0x0C), 0, sizeof(Vector3));
        *ElementFloat(&e, 0x18) = c;
        *ElementFloat(&e, 0x1C) = sn;
        e.ticks = 18;
        *ElementFloat(&e, 0x2C) = Store(D(EI_CYL_TWO) / static_cast<double>(e.ticks) * D(radius) * D(EI_CYL_RISE));
        *ElementFloat(&e, 0x24) = Store(D(EI_P_ONE) / orbit);
        *ElementFloat(&e, 0x28) = Store(orbit);
        count = s->elementCount;
    }
}

} // namespace

// 00493DE0. Cylinder: the first particles are the stars (state = their index; the first one
// places them around the carrier); later ones are sparks along a random star's last step.
int CParticleSource::EmitCylinder(Particle* p, int) {
    p->color = 0xFFFFFF;
    int count = particles.count;
    int stars = elementCount;
    if (count < stars) {
        if (count == 0) SetUpCylinder(this);
        p->frame = 0;
        p->state = count;
        const ParticleElement& e = elements[count];
        p->pos.x = Store(D(e.pos.x) + D(origin.x));
        p->pos.y = Store(D(e.pos.y) + D(origin.y));
        p->pos.z = Store(D(e.pos.z) + D(origin.z));
        SetBits(&p->size, 0);
        return 1;
    }
    int j = static_cast<int>(Next() % static_cast<std::uint32_t>(stars));
    const ParticleElement& e = elements[j];
    int life = e.ticks + 6;
    if (life > 14) life = 14;
    p->life = life;
    if (life < 0) return 0;
    const float* step = &e.value[0];
    auto isZero = [](float f) { return f == 0.0f || x87::IsNaN(f); };
    if (isZero(step[0]) && isZero(step[1])) return 0;
    p->frame = 1;
    p->state = -1;
    double u = Unsigned(Next()) * EI_P_SCALE_1 - D(EI_P_ONE);
    float a = Store(u * D(step[0]));
    float b = Store(u * D(step[1]));
    double c = u * D(step[2]);
    p->velocity.x = Store(D(a) + D(e.pos.x));
    p->velocity.y = Store(D(b) + D(e.pos.y));
    p->velocity.z = Store(c + D(e.pos.z));
    std::memcpy(&p->unknown_2C, step, sizeof(Vector3));
    double px = D(p->velocity.x) + D(origin.x);
    float py = Store(D(origin.y) + D(p->velocity.y));
    float pz = Store(D(origin.z) + D(p->velocity.z));
    SetBits(&p->size, 0x3DCCCCCD);     // 0.1
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    return 1;
}
EI_REPLACE_METHOD(0x00493DE0, CParticleSource::EmitCylinder);

// 00494170. Cylinder: a star follows its element and fades by its countdown (dropped
// once the source stops); a spark fades by its life, drifts by its step, is held on its star's
// orbit radius and spirals round it, shrinking.
int CParticleSource::UpdateCylinder(Particle* p) {
    int state = p->state;
    if (state >= 0) {
        if (!(flags_ & 1)) {
            SetBits(&p->size, 0);
            p->color = 0xFFFFFF;
            return 0;
        }
        const ParticleElement& e = elements[state];
        p->color = (static_cast<std::uint32_t>(CylinderAlpha()[e.ticks]) << 24) | 0xFFFFFF;
        double px = D(origin.x) + D(e.pos.x);
        float py = Store(D(origin.y) + D(e.pos.y));
        float pz = Store(D(origin.z) + D(e.pos.z));
        SetBits(&p->size, 0x3E99999A);     // 0.3
        p->pos.y = py;
        p->pos.x = Store(px);
        p->pos.z = pz;
        return 1;
    }
    ++p->frame;
    int life = --p->life;
    if (life <= 0) return 0;
    p->color = (static_cast<std::uint32_t>(CylinderAlpha()[life]) << 24) | 0xFFFFFF;
    float* step = &p->unknown_2C;
    p->velocity.x = Store(D(p->velocity.x) + D(step[0]));
    p->velocity.y = Store(D(step[1]) + D(p->velocity.y));
    p->velocity.z = Store(D(step[2]) + D(p->velocity.z));
    double k = D(*ElementFloat(elements, 0x28)) / x87::fsqrt(LengthSquared2DUnrounded<double>(p->velocity.x, p->velocity.y));
    float kx = Store(k * D(p->velocity.x));
    p->velocity.x = kx;
    double ky = k * D(p->velocity.y);
    p->velocity.y = Store(ky);
    float reach =
        Store(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(step[0], step[1], step[2])) * D(*ElementFloat(elements, 0x24)));
    step[0] = Store(ky * D(reach) * D(EI_CYL_SPARK_PULL));
    step[1] = Store(-D(kx) * D(reach) * D(EI_CYL_SPARK_PULL));
    step[2] = Store(D(step[2]) * D(EI_CYL_SPARK_PULL));
    double oldSize = D(p->size);
    double px = D(p->velocity.x) + D(origin.x);
    float py = Store(D(p->velocity.y) + D(origin.y));
    float pz = Store(D(p->velocity.z) + D(origin.z));
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store(oldSize * D(EI_CYL_SPARK_SHRINK));
    return 1;
}
EI_REPLACE_METHOD(0x00494170, CParticleSource::UpdateCylinder);

// 00494340. Cylinder stars: element 0 lowers the emission rate (by 2 on odd counts, 1 on even);
// each star counts down (stopping the source at 0), turns by its rotation, rises (faster and
// faster), and its rotation turns the other way, slowly.
void CParticleSource::TickCylinder(ParticleElement* e) {
    if (e == elements) {
        if (e->ticks % 2 != 0) --emitCount;
        --emitCount;
    }
    int t = e->ticks--;
    if (t <= 0) {
        flags_ &= ~1u;
        return;
    }
    float* rotation = ElementFloat(e, 0x18);
    float nx = Store(Dot2DUnrounded<double>(rotation[1], rotation[0], e->pos.y, e->pos.x));
    float ny = Store(D(rotation[0]) * D(e->pos.y) - D(rotation[1]) * D(e->pos.x));
    float* rise = ElementFloat(e, 0x2C);
    float nz = Store(D(e->pos.z) + D(*rise));
    float* step = ElementFloat(e, 0x0C);
    step[0] = Store(D(nx) - D(e->pos.x));
    step[1] = Store(D(ny) - D(e->pos.y));
    step[2] = Store(D(nz) - D(e->pos.z));
    *rise = Store(D(*rise) * D(EI_CYL_RISE_UP));
    e->pos = {nx, ny, nz};
    double twist = D(EI_CYL_PI) * D(EI_CYL_TWIST);
    if (type_ == 0x2040) twist = -twist;
    float c = Store(x87::fcos(twist));
    double s = x87::fsin(twist);
    double a = s * D(rotation[1]) + D(c) * D(rotation[0]);
    float b = Store(D(c) * D(rotation[1]) - s * D(rotation[0]));
    rotation[0] = Store(a);
    rotation[1] = b;
}
EI_REPLACE_METHOD(0x00494340, CParticleSource::TickCylinder);

// --- FireBall (0x2000) and the arrow smokes (FireArrow 0x2011, 0x204E): trails of a moving source
//
// A trail particle starts somewhere along the source's motion of the last tick and gets a velocity
// across the motion (the spread box along the source's side and up axes); the first particle of the
// fireball / arrow is its head (state 1 / 2).

namespace {

#define EI_TRAIL_MOVING GameFloat(0x0073CDF0)        // 1e-5: below this distance there is no trail
#define EI_TRAIL_HEAD GameFloat(0x0073CDF4)          // 1.2
#define EI_TRAIL_HALF GameFloat(0x0073B86C)          // 0.5
#define EI_TRAIL_DRAG GameFloat(0x0073CD50)          // 0.95
#define EI_ARROW_HEAD GameFloat(0x0073B868)          // 0.4
#define EI_ARROW_PUFF GameFloat(0x0073BAD4)          // 0.2
#define EI_ARROW_SWELL GameFloat(0x0073CDF8)         // 1.4
#define EI_ARROW_SPREAD GameFloat(0x0073BAC4)        // 1.1

// A random value of [low, high) (the original's spread box order).
double Between(float low, float high) { return Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1 + D(low); }

// The trail velocity: a random amount of the side axis (SideVelocity), plus one of the up axis
// (AddUp).
void SideVelocity(CParticleSource* s, Particle* p, float low, float high) {
    double q = Between(low, high);
    p->velocity.x = Store(q * D(s->side.x));
    p->velocity.y = Store(q * D(s->side.y));
    p->velocity.z = Store(q * D(s->side.z));
}

void AddUp(CParticleSource* s, Particle* p, double q) {
    float a = Store(q * D(s->up.x));
    float b = Store(q * D(s->up.y));
    double c = q * D(s->up.z);
    p->velocity.x = Store(D(a) + D(p->velocity.x));
    p->velocity.y = Store(D(b) + D(p->velocity.y));
    p->velocity.z = Store(c + D(p->velocity.z));
}

} // namespace

// 0048C800. FireBall: index 0 is the ball itself (a 1.2 size particle at the source); the others
// are smoke along the last step with a random sideways velocity (the spread box widened by half
// the size) and are put one step out.
int CParticleSource::EmitFireBall(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    double half = D(size) * D(EI_TRAIL_HALF);
    float halfRounded = Store(half);
    double t;
    if (index == 0) {
        p->size = Store(D(size) * D(EI_TRAIL_HEAD));
        p->state = 1;
        t = D(EI_P_ZERO);
    } else {
        p->size = Store(half);
        std::uint32_t r = Next();
        p->state = 0;
        t = Unsigned(r) * EI_P_SCALE_1 - D(EI_P_ONE);
    }
    if (!(distance > EI_TRAIL_MOVING)) {
        std::memset(&p->velocity, 0, sizeof(Vector3));
        return 1;
    }
    float ax = Store(t * D(motion.x));
    float ay = Store(t * D(motion.y));
    double az = t * D(motion.z);
    p->pos.x = Store(D(ax) + D(p->pos.x));
    p->pos.y = Store(D(ay) + D(p->pos.y));
    p->pos.z = Store(az + D(p->pos.z));
    float high = Store(D(halfRounded) + D(spreadMax.x));
    float low = Store(D(spreadMin.x) - D(halfRounded));
    SideVelocity(this, p, low, high);
    high = Store(D(halfRounded) + D(spreadMax.y));
    low = Store(D(spreadMin.y) - D(halfRounded));
    AddUp(this, p, Between(low, high));
    if (p->state == 0) {
        p->pos.x = Store(D(p->velocity.x) + D(p->pos.x));
        p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
        p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    }
    return 1;
}
EI_REPLACE_METHOD(0x0048C800, CParticleSource::EmitFireBall);

// 0048CAD0. FireBall: step through the frames (dropped after the last), grow by the source's
// growth; the ball halves once and stays; smoke drifts by its velocity (0.95 drag).
int CParticleSource::UpdateFireBall(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    p->size = Store(D(growth) * D(p->size));
    if (p->state != 0) {
        p->size = Store(D(p->size) * D(EI_TRAIL_HALF));
        p->state = 0;
    } else {
        p->pos.x = Store(D(p->pos.x) - D(p->velocity.x));
        p->pos.y = Store(D(p->pos.y) - D(p->velocity.y));
        p->pos.z = Store(D(p->pos.z) - D(p->velocity.z));
    }
    p->velocity.x = Store(D(p->velocity.x) * D(EI_TRAIL_DRAG));
    p->velocity.y = Store(D(p->velocity.y) * D(EI_TRAIL_DRAG));
    p->velocity.z = Store(D(p->velocity.z) * D(EI_TRAIL_DRAG));
    p->pos.x = Store(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    return 1;
}
EI_REPLACE_METHOD(0x0048CAD0, CParticleSource::UpdateFireBall);

// 0048CBA0. Arrow smoke: the first particle (once) is the arrow's head (state 2); the others are
// puffs along the last step (three in ten lingering, state 1) with a random sideways velocity.
int CParticleSource::EmitArrowSmoke(Particle* p, int index) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    float t;
    if (index == 0 && Bits(&unknown_12C) == 0) {
        p->size = Store(D(size) * D(EI_ARROW_HEAD));
        SetBits(&unknown_12C, 1);
        SetBits(&t, 0);
        p->state = 2;
        p->frame = 0;
    } else {
        p->size = Store(D(size) * D(EI_ARROW_PUFF));
        std::uint32_t r = Next();
        p->state = 0;
        t = Store(Unsigned(r) * EI_P_SCALE_1 - D(EI_P_ONE));
        if (Next() % 100 > 70) p->state = 1;
        p->frame = static_cast<int>(Next() & 1) + 2;
    }
    if (!(distance > EI_TRAIL_MOVING)) {
        std::memset(&p->velocity, 0, sizeof(Vector3));
        return 1;
    }
    double ax = D(t) * D(motion.x);
    float ay = Store(D(t) * D(motion.y));
    float az = Store(D(t) * D(motion.z));
    p->pos.x = Store(ax + D(p->pos.x));
    p->pos.y = Store(D(ay) + D(p->pos.y));
    p->pos.z = Store(D(az) + D(p->pos.z));
    SideVelocity(this, p, spreadMin.x, spreadMax.x);
    AddUp(this, p, Between(spreadMin.y, spreadMax.y));
    if (p->state == 0) {
        p->pos.x = Store(D(p->pos.x) + D(p->velocity.x));
        p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
        p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    }
    return 1;
}
EI_REPLACE_METHOD(0x0048CBA0, CParticleSource::EmitArrowSmoke);

// 0048CED0. Arrow smoke: the head follows the source one step ahead with a random frame (dropped
// once the source stops); puffs step through the frames, swell (faster in the first 6 frames),
// and fly out by their velocity (growing by 1.1); lingering puffs keep their place.
int CParticleSource::UpdateArrowSmoke(Particle* p) {
    int state = p->state;
    if (state == 2) {
        p->pos.x = Store(D(motion.x) + D(origin.x));
        p->pos.y = Store(D(motion.y) + D(origin.y));
        p->pos.z = Store(D(motion.z) + D(origin.z));
        p->frame = static_cast<int>(Next() % 3);
        return static_cast<int>(flags_ & 1);
    }
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    int frame = p->frame;
    if (state == 0) {
        if (frame < 6) p->size = Store(D(p->size) * D(EI_ARROW_SWELL));
        else p->size = Store(D(growth) * D(p->size));
        p->pos.x = Store(D(p->pos.x) - D(p->velocity.x));
        p->pos.y = Store(D(p->pos.y) - D(p->velocity.y));
        p->pos.z = Store(D(p->pos.z) - D(p->velocity.z));
        p->velocity.x = Store(D(p->velocity.x) * D(EI_ARROW_SPREAD));
        p->velocity.y = Store(D(p->velocity.y) * D(EI_ARROW_SPREAD));
        p->velocity.z = Store(D(p->velocity.z) * D(EI_ARROW_SPREAD));
        p->pos.x = Store(D(p->velocity.x) + D(p->pos.x));
        p->pos.y = Store(D(p->pos.y) + D(p->velocity.y));
        p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
        return 1;
    }
    if (frame < 6) p->size = Store(D(p->size) * D(EI_ARROW_SWELL));
    p->velocity.x = Store(D(p->velocity.x) * D(EI_TRAIL_DRAG));
    p->velocity.y = Store(D(p->velocity.y) * D(EI_TRAIL_DRAG));
    p->velocity.z = Store(D(p->velocity.z) * D(EI_TRAIL_DRAG));
    return 1;
}
EI_REPLACE_METHOD(0x0048CED0, CParticleSource::UpdateArrowSmoke);

// --- Banshee (0x200D): a ring that widens around the source ---------------------------------------
//
// Element 0: +0x08 the ring's height, +0x0C its radius (easing towards 30 times the source's
// height), +0x34 the countdown.

namespace {

#define EI_BANSHEE_TALL GameFloat(0x0073BAA4)        // 3.0
#define EI_BANSHEE_WIDEN GameFloat(0x0073CD04)       // 1/36
#define EI_BANSHEE_HALF GameFloat(0x0073B86C)        // 0.5
#define EI_BANSHEE_REACH GameFloat(0x0073BAF0)       // 30.0
#define EI_BANSHEE_EASE GameFloat(0x0073CB2C)        // 0.5
#define EI_BANSHEE_THIN GameFloat(0x0073CCF4)        // 0.001
#define EI_BANSHEE_SIZE GameFloat(0x0073CD18)        // 0.15

} // namespace

// 00489BE0. Banshee: the first call only sets the ring up (from the carrier's figure; 72 sparks
// per tick after it). A spark on the ring, at a random angle, between the ring and its eased
// inner edge.
int CParticleSource::EmitBanshee(Particle* p, int) {
    if (elementCount == 0) {
        SetElementCount(this, 1);
        Vector3 offset;
        float radius;
        AttachmentOffset(&offset, &radius);
        double tall = D(radius) * D(EI_BANSHEE_TALL);
        double widen = D(radius) * D(EI_BANSHEE_WIDEN);
        height = Store(widen + widen);
        elements[0].ticks = 8;
        elements[0].pos.z = Store(tall * D(EI_BANSHEE_HALF));
        SetBits(ElementFloat(elements, 0x0C), 0);
        emitCount = 72;
        return 0;
    }
    float ringRadius;
    std::memcpy(&ringRadius, ElementFloat(elements, 0x0C), sizeof(float));
    float twoPi = Store(GameDouble(0x0073CAE8) + GameDouble(0x0073CAE8));
    std::uint32_t r = Next();
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->frameTimer = 1;
    float angle = Store(Unsigned(r) * D(twoPi) * EI_P_SCALE_1);
    int age = elements[0].ticks;
    if (age >= 5) age = 5;
    p->life = static_cast<int>(Next() % 3u) + age + 1;
    double innerUnrounded = (D(ringRadius) - D(height) * D(EI_BANSHEE_REACH)) * D(EI_BANSHEE_EASE) + D(ringRadius);
    float inner = Store(innerUnrounded);
    float reach;
    if (!(innerUnrounded + D(EI_BANSHEE_THIN) >= D(ringRadius)))
        reach = Store(Unsigned(Next()) * (D(ringRadius) - D(inner)) * EI_P_SCALE_1 + D(inner));
    else
        reach = inner;
    p->pos.x = Store(x87::fsin(D(angle)) * D(reach) + D(origin.x));
    p->pos.y = Store(x87::fcos(D(angle)) * D(reach) + D(origin.y));
    p->pos.z = Store(D(origin.z) + D(elements[0].pos.z));
    p->size = Store(D(size) * D(EI_BANSHEE_SIZE));
    return 1;
}
EI_REPLACE_METHOD(0x00489BE0, CParticleSource::EmitBanshee);

// 00489EF0. Banshee sparks: one frame per tick until they die.
int CParticleSource::UpdateBanshee(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        p->frameTimer = 1;
        ++p->frame;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00489EF0, CParticleSource::UpdateBanshee);

// 00489F30. Banshee ring: counts down (stopping the emission at 0) while its radius eases towards
// 30 times the source's height.
void CParticleSource::TickBanshee(ParticleElement* e) {
    if (--e->ticks == 0) {
        flags_ &= ~1u;
        return;
    }
    double target = D(height) * D(EI_BANSHEE_REACH);
    float* radius = ElementFloat(e, 0x0C);
    *radius = Store((D(*radius) - target) * D(EI_BANSHEE_EASE) + target);
}
EI_REPLACE_METHOD(0x00489F30, CParticleSource::TickBanshee);

// --- Portal (0x203E) emission; its update and element tick are in particle_emitters.cpp ------------

namespace {

#define EI_PORTAL_HEIGHT GameFloat(0x0073CC7C)       // 8.0
#define EI_PORTAL_RADIUS GameFloat(0x0073CC78)       // 2.5
#define EI_PORTAL_ANGLE GameDouble(0x0073CE88)       // ~2 pi * 2^-32
#define EI_PORTAL_SPIN GameFloat(0x0073B850)         // 0.1
#define EI_PORTAL_RISE GameFloat(0x0073CF54)         // 0.015
#define EI_PORTAL_HALF_C GameFloat(0x0073B86C)       // 0.5
#define EI_PORTAL_TWO GameFloat(0x0073CDB8)          // 2.0
#define EI_PORTAL_SHRINK GameFloat(0x0073C54C)       // 0.3

} // namespace

// 00494470. Portal: the first particle is the glow above the portal (state 1). The others start
// at a random point of the disc (denser near the edge), spin round it (four in eleven or so the
// other way, with a different frame range) and rise.
int CParticleSource::EmitPortal(Particle* p, int) {
    if (particles.count == 0) {
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
        SetBits(&p->size, 0);
        p->pos.z = Store(D(EI_PORTAL_HEIGHT) * D(GameFloat(0x0073CCA8)) + D(p->pos.z));
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
    double t = Unsigned(Next()) * EI_P_SCALE_1;
    float spread = Store((t + D(EI_P_ONE)) * t * D(EI_PORTAL_HALF_C));
    float angle = Store(Unsigned(Next()) * EI_PORTAL_ANGLE);
    double reach = D(spread) * D(radius);
    double x = x87::fsin(D(angle)) * reach;
    float y = Store(x87::fcos(D(angle)) * reach);
    p->pos.x = Store(x + D(origin.x));
    p->pos.y = Store(D(y) + D(origin.y));
    p->pos.z = Store(D(origin.z));
    SetBits(&p->size, 0);
    p->unknown_30 = y;
    *ParticleFloat(p, 0x34) = Store((D(EI_PORTAL_TWO) - D(spread)) * D(EI_PORTAL_SHRINK));
    p->unknown_2C = Store(x);
    float spin = Store(D(radius) / D(EI_PORTAL_RADIUS) * D(EI_PORTAL_SPIN));
    if (Unsigned(Next()) * EI_P_SCALE_1 > D(spread)) {
        p->frame = static_cast<int>(Next() % 11) + 4;
    } else {
        p->frame = static_cast<int>(Next() & 3);
        spin = Store(-D(spin));
    }
    p->velocity.x = Store(x87::fcos(D(spin)));
    p->velocity.y = Store(x87::fsin(D(spin)));
    p->velocity.z = Store((D(EI_P_ONE) - D(spread) + D(GameFloat(0x0073BAA4))) * D(radius) * D(EI_PORTAL_RISE));
    p->pos.z = Store((Unsigned(Next()) * EI_P_SCALE_1 - D(EI_PORTAL_HALF_C)) * D(p->velocity.z) + D(p->pos.z));
    return 1;
}
EI_REPLACE_METHOD(0x00494470, CParticleSource::EmitPortal);

// --- AcidRay (0x2012) ---------------------------------------------------------------------------

namespace {

const float* AcidSpread() { return reinterpret_cast<const float*>(0x0078A61C); }    // by age
const float* AcidSize() { return reinterpret_cast<const float*>(0x0078A668); }      // by age

// Position: the start (+0x38) plus the velocity scaled by the source's size and the spread table.
void PlaceAcid(CParticleSource* s, Particle* p, int age, Vector3& out) {
    double size = D(s->size);
    float sx = Store(size * D(p->velocity.x));
    float sy = Store(size * D(p->velocity.y));
    double sz = size * D(p->velocity.z);
    float k = AcidSpread()[age];
    Vector3 v;
    v.Construct(Store(D(sx) * D(k)), Store(D(sy) * D(k)), Store(sz * D(k)));
    out.Construct(Store(D(v.x) + D(p->drift.x)), Store(D(v.y) + D(p->drift.y)), Store(D(v.z) + D(p->drift.z)));
}

} // namespace

// 0048D050. AcidRay: a drop at a random point of the last step (when moving), flying sideways by
// the spread box along the side and up axes (or standing).
int CParticleSource::EmitAcidRay(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    std::uint32_t r = Next();
    p->state = 0;
    float t = Store(Unsigned(r) * EI_P_SCALE_1 - D(EI_P_ONE));
    p->frame = static_cast<int>(Next() & 1);
    if (distance > EI_TRAIL_MOVING) {
        double ax = D(t) * D(motion.x);
        float ay = Store(D(t) * D(motion.y));
        float az = Store(D(t) * D(motion.z));
        double px = ax + D(p->pos.x);
        p->pos.x = Store(px);
        float py = Store(D(ay) + D(p->pos.y));
        p->pos.y = py;
        double pz = D(az) + D(p->pos.z);
        p->drift.y = py;
        p->pos.z = Store(pz);
        p->drift.z = Store(pz);
        p->drift.x = Store(px);
        double q = Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE);
        p->velocity.x = Store(q * D(side.x));
        p->velocity.y = Store(q * D(side.y));
        p->velocity.z = Store(q * D(side.z));
        double q2 = Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE);
        float a = Store(q2 * D(up.x));
        float b = Store(q2 * D(up.y));
        double c = q2 * D(up.z);
        p->velocity.x = Store(D(a) + D(p->velocity.x));
        p->velocity.y = Store(D(b) + D(p->velocity.y));
        p->velocity.z = Store(c + D(p->velocity.z));
    } else {
        std::memset(&p->velocity, 0, sizeof(Vector3));
        std::memcpy(&p->drift, &origin, sizeof(Vector3));
    }
    Vector3 start;
    PlaceAcid(this, p, lifetime - p->life, start);
    reinterpret_cast<Vector4*>(p)->FromPoint(start);
    p->size = Store(D(AcidSize()[lifetime - p->life]) * D(size));
    return 1;
}
EI_REPLACE_METHOD(0x0048D050, CParticleSource::EmitAcidRay);

// 0048D330. AcidRay drops: fly out from their start by the velocity scaled by the source's size
// and the spread table (by age), sized by the size table, through the frames.
int CParticleSource::UpdateAcidRay(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    double size_ = D(size);
    float sx = Store(size_ * D(p->velocity.x));
    float sy = Store(size_ * D(p->velocity.y));
    double sz = size_ * D(p->velocity.z);
    double k = D(AcidSpread()[lifetime - life]);
    float ax = Store(D(sx) * k);
    float ay = Store(D(sy) * k);
    double az = sz * k;
    double px = D(ax) + D(p->drift.x);
    float py = Store(D(ay) + D(p->drift.y));
    float pz = Store(az + D(p->drift.z));
    p->pos.y = py;
    SetBits(&p->size, 0x3F800000);
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store(D(AcidSize()[lifetime - life]) * D(size));
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    return 1;
}
EI_REPLACE_METHOD(0x0048D330, CParticleSource::UpdateAcidRay);

// --- Sparks (0x2043) emission; the update is UpdateSparks (particle_emitters.cpp) ----------------

// 00494A10. Sparks: the first particle marks the source (state 1); after it the source stops
// emitting and each further one is a spark flying in a random direction (a random point of the
// unit ball, normalised) with a random speed (squared) and a random part of its first step done.
int CParticleSource::EmitSparks(Particle* p, int) {
    if (particles.count == 0) {
        p->color = 0xFFFFFFFF;
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
        SetBits(&p->size, 0);
        p->frame = 0;
        p->state = 1;
        return 1;
    }
    flags_ &= ~1u;
    p->color = 0xFFFFFFFF;
    float speed = Store(Unsigned(Next()) * GameDouble(0x0073CE10) + D(GameFloat(0x0073CCA8)));
    float x, y, z;
    double lengthSquared;
    for (int tries = 10;;) {
        x = SignedUnit();
        y = Store(Range(-1.0f, 1.0f));
        double zu = Range(-1.0f, 1.0f);
        z = Store(zu);
        lengthSquared = zu * D(z) + D(y) * D(y) + D(x) * D(x);
        if (!(lengthSquared >= D(EI_P_ONE))) break;
        if (!(--tries > 0)) break;
    }
    float length = Store(x87::fsqrt(lengthSquared));
    Vector3 v;
    v.Construct(x, y, z);
    Vector3 direction;
    DivideVector(&direction, v, length);
    double k = D(speed) * D(size) * D(speed) * D(GameFloat(0x0073BAD4));
    p->drift.x = Store(D(direction.x) * k);
    p->drift.y = Store(D(direction.y) * k);
    p->drift.z = Store(D(direction.z) * k);
    double t = Unsigned(Next()) * EI_P_SCALE_1;
    float a = Store(t * D(p->drift.x));
    float b = Store(t * D(p->drift.y));
    double c = t * D(p->drift.z);
    double px = D(a) + D(origin.x);
    float py = Store(D(b) + D(origin.y));
    float pz = Store(c + D(origin.z));
    SetBits(&p->size, 0x3F800000);
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    p->size = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) * D(size) *
                    D(GameFloat(0x0073BAD4)));
    p->life = 6;
    p->state = 0;
    p->frame = static_cast<int>(Next() % 3) + 1;
    return 1;
}
EI_REPLACE_METHOD(0x00494A10, CParticleSource::EmitSparks);

// --- ClayRing (0x2018): ten points running out from the source along the ground ---------------
//
// Each element (set up by Create): +0x00 the point on the ground, +0x0C its previous position,
// +0x18 its direction (turning a little each tick), +0x34 the countdown (14).

namespace {

#define EI_CLAY_SCATTER_SCALE GameDouble(0x0073CD58)  // ~0.2 * 2^-32
#define EI_CLAY_SCATTER_LOW GameFloat(0x0073B850)     // 0.1
#define EI_CLAY_SLOW GameFloat(0x0073CD70)            // 0.9
#define EI_CLAY_COS GameFloat(0x0073CD50)             // 0.95
#define EI_CLAY_SIN GameFloat(0x0073CE78)             // 0.31225

const std::int32_t* ClayAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A734); }
const float* ClayReach() { return reinterpret_cast<const float*>(0x0078A6F4); }
// The reach read backwards from 0078A72C (for a positive size).
float ClayReachBackwards(int t) { return *reinterpret_cast<const float*>(0x0078A72C - 4 * t); }

} // namespace

// 0048F7F0. ClayRing: in the first tick (countdown 13) one marker per point; then clods between a
// random point's previous and current position, flying off at random.
int CParticleSource::EmitClayRing(Particle* p, int index) {
    p->color = (static_cast<std::uint32_t>(ClayAlpha()[elements[0].ticks]) << 24) | 0xFFFFFF;
    p->life = lifetime;
    p->frame = 1;
    p->frameTimer = frameDelay;
    if (elements[0].ticks == 13 && index < elementCount) {
        std::memset(&p->velocity, 0, sizeof(Vector3));
        std::memcpy(&p->pos, &elements[index].pos, sizeof(Vector3));
        SetBits(&p->size, 0x3E4CCCCD);     // 0.2
        p->frame = 0;
        p->state = index;
        return 1;
    }
    int j = static_cast<int>(Next() % static_cast<std::uint32_t>(elementCount));
    double t = Unsigned(Next()) * EI_P_SCALE_1;
    p->life = 5;
    const ParticleElement& e = elements[j];
    const float* previous = &e.value[0];
    double u = D(EI_P_ONE) - t;
    float a = Store(u * D(previous[0]));
    float b = Store(u * D(previous[1]));
    float c = Store(u * D(previous[2]));
    float d = Store(t * D(e.pos.x));
    float f = Store(t * D(e.pos.y));
    double g = t * D(e.pos.z);
    p->velocity.x = Store(D(d) + D(a));
    p->velocity.y = Store(D(f) + D(b));
    p->velocity.z = Store(g + D(c));
    float sx = Store(Unsigned(Next()) * EI_CLAY_SCATTER_SCALE - D(EI_CLAY_SCATTER_LOW));
    float sy = Store(Unsigned(Next()) * EI_CLAY_SCATTER_SCALE - D(EI_CLAY_SCATTER_LOW));
    float sz = Store(Unsigned(Next()) * EI_CLAY_SCATTER_SCALE - D(EI_CLAY_SCATTER_LOW));
    p->unknown_2C = sy;
    p->unknown_30 = sx;
    *ParticleFloat(p, 0x34) = sz;
    std::memcpy(&p->pos, previous, sizeof(Vector3));
    SetBits(&p->size, 0x3DCCCCCD);     // 0.1
    return 1;
}
EI_REPLACE_METHOD(0x0048F7F0, CParticleSource::EmitClayRing);

// 0048FAB0. ClayRing: markers stay on their point (alpha by the countdown); clods step through the
// frames, fly out (their scatter slowing by 0.9), fade by 48 per tick and shrink.
int CParticleSource::UpdateClayRing(Particle* p) {
    if (--p->life == 0) return 0;
    if (p->frame == 0) {
        p->color = (static_cast<std::uint32_t>(ClayAlpha()[elements[0].ticks]) << 24) | 0xFFFFFF;
        std::memcpy(&p->pos, &elements[p->state].pos, sizeof(Vector3));
        SetBits(&p->size, 0x3E4CCCCD);     // 0.2
        return 1;
    }
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = frameDelay;
    }
    float* scatter = &p->unknown_2C;
    for (int i = 0; i < 3; ++i) scatter[i] = Store(D(scatter[i]) * D(EI_CLAY_SLOW));
    double oldSize = D(p->size);
    double px = D(p->velocity.x) + D(scatter[0]);
    float py = Store(D(p->velocity.y) + D(scatter[1]));
    float pz = Store(D(p->velocity.z) + D(scatter[2]));
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    std::int32_t a = static_cast<std::int32_t>(p->color >> 24) - 0x30;
    if (a < 0) a = 0;
    p->size = Store(oldSize * D(EI_CLAY_SLOW));
    p->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x0048FAB0, CParticleSource::UpdateClayRing);

// 0048FBE0. ClayRing points: count down (stopping the emission past 0), remember the previous
// position, move out from the source along their direction by the reach table (read backwards for
// a non-negative size), sit on the ground, and turn their direction a little.
void CParticleSource::TickClayRing(ParticleElement* e) {
    int t = --e->ticks;
    if (t < 0) {
        flags_ &= ~1u;
        return;
    }
    float* previous = ElementFloat(e, 0x0C);
    std::memcpy(previous, &e->pos, sizeof(Vector3));
    float* direction = ElementFloat(e, 0x18);
    double s = D(size);
    float a = Store(s * D(direction[0]));
    float b = Store(s * D(direction[1]));
    double c = s * D(direction[2]);
    double k = !(size >= 0.0f) ? D(ClayReach()[t]) : D(ClayReachBackwards(t));
    float ka = Store(D(a) * k);
    float kb = Store(D(b) * k);
    double kc = c * k;
    float px = Store(D(ka) + D(origin.x));
    float py = Store(D(kb) + D(origin.y));
    float pz = Store(kc + D(origin.z));
    e->pos = {px, py, pz};
    e->pos.z = Store(GroundHeight(e->pos.x, e->pos.y) + D(EI_P_ONE));
    double turnedX = D(direction[1]) * D(EI_CLAY_COS) - D(direction[0]) * D(EI_CLAY_SIN);
    double turnedY = D(direction[0]) * D(EI_CLAY_COS) + D(direction[1]) * D(EI_CLAY_SIN);
    direction[0] = Store(turnedY);
    direction[1] = Store(turnedX);
    auto isZero = [](float f) { return f == 0.0f || x87::IsNaN(f); };
    if (isZero(previous[0]) && isZero(previous[1])) std::memcpy(previous, &e->pos, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x0048FBE0, CParticleSource::TickClayRing);

// --- Falling (0x2034-0x2037): leaves or flakes drifting down from the carrier -------------------
//
// The elements are gusts: +0x0C the gust's reach, +0x10 its direction (degrees turned to
// radians, truncated), +0x30 its age.

namespace {

#define EI_FALL_SIZE_SCALE GameDouble(0x0073CE68)    // ~0.1 * 2^-32
#define EI_FALL_SIZE_BASE GameFloat(0x0073B850)      // 0.1
#define EI_FALL_HALF GameFloat(0x0073B86C)           // 0.5
#define EI_FALL_PI GameDouble(0x0073CAE8)            // pi
#define EI_FALL_PER_DEGREE GameDouble(0x0073BA30)    // 1/180
#define EI_FALL_TEN GameDouble(0x0073CD10)           // 10.0
#define EI_FALL_DROP_SCALE GameDouble(0x0073CE60)    // ~0.12 * 2^-32
#define EI_FALL_DROP GameFloat(0x0073CCB8)           // 0.06
#define EI_FALL_SPIN_SCALE GameDouble(0x0073CE58)    // ~0.4 * 2^-32
#define EI_FALL_SPIN GameFloat(0x0073CDE0)           // 0.6
#define EI_FALL_REACH_SCALE GameDouble(0x0073CE70)   // ~0.2 * 2^-32
#define EI_FALL_TURN GameDouble(0x0073CDC0)          // 360.0

} // namespace

// 0048F3B0. Falling: a leaf at the source blown by a random gust (a random reach up to the gust's,
// a random direction around it (the original's spread is ftol(10 degrees) = 0)), falling and
// spinning at random rates.
int CParticleSource::EmitFalling(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->frame = 12;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    p->size = Store(Unsigned(Next()) * EI_FALL_SIZE_SCALE + D(EI_FALL_SIZE_BASE));
    const ParticleElement& gust = elements[Next() % static_cast<std::uint32_t>(elementCount)];
    const float* g = &gust.value[0];
    float reach = Store(D(g[0]) * D(EI_FALL_HALF));
    int spread = GameFtol(EI_FALL_PI * EI_FALL_PER_DEGREE * EI_FALL_TEN);
    float turn = Store(static_cast<double>(spread) * D(g[1]));
    float lowReach = Store(-D(reach));
    float x = Store(Unsigned(Next()) * (D(reach) - D(lowReach)) * EI_P_SCALE_1 + D(g[0]) + D(lowReach));
    float lowTurn = Store(-D(turn));
    double angle = Unsigned(Next()) * (D(turn) - D(lowTurn)) * EI_P_SCALE_1 + D(g[1]) + D(lowTurn);
    SetBits(&p->velocity.x, 0x3F800000);
    SetBits(&p->velocity.z, 0xBDF5C28F);     // -0.12
    p->drift.x = Store(x87::fsin(angle) * D(x));
    p->drift.y = Store(x87::fcos(angle) * D(x));
    SetBits(&p->drift.z, 0);
    p->velocity.z = Store(Unsigned(Next()) * EI_FALL_DROP_SCALE + D(p->velocity.z) - D(EI_FALL_DROP));
    p->velocity.y = Store(Unsigned(Next()) * EI_FALL_SPIN_SCALE - D(EI_FALL_SPIN));
    return 1;
}
EI_REPLACE_METHOD(0x0048F3B0, CParticleSource::EmitFalling);

// 0048F6F0. Falling gusts: a new gust gets a random reach and direction (truncated to whole
// radians); from the second tick on the source stops emitting and lets go of its carrier.
void CParticleSource::TickFalling(ParticleElement* e) {
    int age = *ElementInt(e, 0x30);
    if (age == 0) {
        *ElementFloat(e, 0x0C) = Store(Unsigned(Next()) * EI_FALL_REACH_SCALE + D(EI_FALL_SIZE_BASE));
        float whole = Store(static_cast<double>(GameFtol(EI_FALL_PI * EI_FALL_PER_DEGREE * EI_FALL_TURN)));
        *ElementFloat(e, 0x10) = Store(Unsigned(Next()) * D(whole) * EI_P_SCALE_1);
        age = *ElementInt(e, 0x30);
    }
    if (age > 0) {
        flags_ &= ~1u;
        Attach(nullptr);
    }
    ++*ElementInt(e, 0x30);
}
EI_REPLACE_METHOD(0x0048F6F0, CParticleSource::TickFalling);

// --- VulcanSmoke (0x2005): lava drops and smoke from a vent ---------------------------------------

namespace {

#define EI_VULKAN_LOW GameFloat(0x0073CDC8)          // -0.5
#define EI_VULKAN_HALF GameFloat(0x0073B86C)         // 0.5
#define EI_VULKAN_SMOKE GameFloat(0x0073C54C)        // 0.3

} // namespace

// 0048BD50. VulcanSmoke: three in five are drops (state 8: 8 ticks per frame) thrown up at a
// random height in a random direction; the others smoke (state 4) at a random point of the vent,
// rising by the source's height.
int CParticleSource::EmitVulkan(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    double radian = EI_FALL_PI * EI_FALL_PER_DEGREE * EI_FALL_TURN;
    if (Next() % 100 + 1 <= 60) {
        p->state = 8;
        p->life = lifetime * 8;
        p->frameTimer = 8;
        std::memcpy(&p->pos, &origin, 2 * sizeof(float));
        float low = Store(D(spreadMin.y) * D(EI_VULKAN_LOW));
        float high = Store(D(spreadMin.y) * D(EI_VULKAN_HALF));
        p->pos.z = Store(Unsigned(Next()) * (D(high) - D(low)) * EI_P_SCALE_1 + D(spreadMax.y) + D(origin.z) + D(low));
        std::memcpy(&p->size, &size, sizeof(float));
        std::memcpy(&p->velocity.x, &growth, sizeof(float));
        SetBits(&p->velocity.y, 0x3F770A3D);     // 0.965
        float reach = spreadMin.x;
        float span = Store(radian);
        std::uint32_t r = Next();
        SetBits(&p->drift.z, 0);
        double angle = Unsigned(r) * D(span) * EI_P_SCALE_1;
        p->drift.x = Store(x87::fsin(angle) * D(reach));
        p->drift.y = Store(x87::fcos(angle) * D(reach));
        return 1;
    }
    p->state = 4;
    p->life = lifetime * 4;
    p->frameTimer = 4;
    float reach = Store(Unsigned(Next()) * D(spreadMax.x) * EI_P_SCALE_1);
    float span = Store(radian);
    double angle = Unsigned(Next()) * D(span) * EI_P_SCALE_1;
    p->pos.x = Store(x87::fsin(angle) * D(reach) + D(origin.x));
    p->pos.y = Store(x87::fcos(angle) * D(reach) + D(origin.y));
    std::memcpy(&p->pos.z, &origin.z, sizeof(float));
    p->size = Store(D(size) * D(EI_VULKAN_SMOKE));
    SetBits(&p->velocity.x, 0x3F828F5C);     // 1.02
    SetBits(&p->velocity.y, 0x3F800000);
    SetBits(&p->drift.x, 0);
    SetBits(&p->drift.y, 0);
    std::memcpy(&p->drift.z, &height, sizeof(float));
    return 1;
}
EI_REPLACE_METHOD(0x0048BD50, CParticleSource::EmitVulkan);

// 0048C010. VulcanSmoke: frames at the particle's own pace (state ticks per frame; dropped after
// the last), moving by +0x38, growing by +0x20 and slowing by +0x24.
int CParticleSource::UpdateVulkan(Particle* p) {
    if (--p->life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) return 0;
        p->frameTimer = p->state;
    }
    p->pos.x = Store(D(p->drift.x) + D(p->pos.x));
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    p->pos.z = Store(D(p->drift.z) + D(p->pos.z));
    p->size = Store(D(p->velocity.x) * D(p->size));
    double keep = D(p->velocity.y);
    p->drift.x = Store(keep * D(p->drift.x));
    p->drift.y = Store(keep * D(p->drift.y));
    p->drift.z = Store(keep * D(p->drift.z));
    return 1;
}
EI_REPLACE_METHOD(0x0048C010, CParticleSource::UpdateVulkan);

// --- FireStar / AcidStar (0x2042-ish), Regeneration, VisionStar and ZoneExit ----------------------

// 00494950. FireStar: a single flash at the source (the source stops emitting at once) with the
// source's lifetime as its frame and its size kept in +0x20.
int CParticleSource::EmitFireStar(Particle* p, int) {
    flags_ &= ~1u;
    p->color = 0xFFFFFF;
    p->life = 3;
    p->frame = lifetime;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0);
    std::memcpy(&p->velocity.x, &size, sizeof(float));
    return 1;
}
EI_REPLACE_METHOD(0x00494950, CParticleSource::EmitFireStar);

// 004949B0. FireStar: shown for two ticks at the kept size (shrinking it by 0.9 each time), then
// hidden and dropped.
int CParticleSource::UpdateFireStar(Particle* p) {
    if (--p->life <= 0) {
        p->color = 0xFFFFFF;
        SetBits(&p->size, 0);
        return 0;
    }
    double shrunk = D(p->velocity.x) * D(GameFloat(0x0073CD70));    // 0.9
    p->color = 0xFFFFFFFF;
    std::memcpy(&p->size, &p->velocity.x, sizeof(float));
    p->velocity.x = Store(shrunk);
    return 1;
}
EI_REPLACE_METHOD(0x004949B0, CParticleSource::UpdateFireStar);

// 004950B0. Regeneration: two particles at the source, +0x20 = -1 for the first and 1 for the
// second (the sides they circle on).
int CParticleSource::EmitRegeneration(Particle* p, int) {
    if (particles.count > 1) return 0;
    p->color = 0xFFFFFF;
    p->frame = 3;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0);
    p->velocity.x = static_cast<float>(particles.count != 0 ? 1 : -1);
    return 1;
}
EI_REPLACE_METHOD(0x004950B0, CParticleSource::EmitRegeneration);

// 00495120. Regeneration: once the source no longer emits (or has no carrier) its size shrinks by
// a quarter per tick and the particles go below 0.02. Each particle eases (0.7 / 0.3) towards a
// point beside the carrier: its own x axis (the first column of its orientation quaternion at
// +0x34, w first) scaled by the carrier's size (+0x134), the particle's side and 0.65; a little
// above, by 0.4 of the source's size. Its size flickers at random.
int CParticleSource::UpdateRegeneration(Particle* p) {
    if (!(flags_ & 1) || !attachedTo) {
        double shrunk = D(size) * D(GameFloat(0x0073C524));          // 0.75
        size = Store(shrunk);
        if (!(shrunk >= D(GameFloat(0x0073CCD8)))) {                    // 0.02
            SetBits(&p->size, 0);
            p->color = 0xFFFFFF;
            return 0;
        }
    }
    p->color = 0xFFFFFFFF;
    Vector3 target;
    std::memcpy(&target, &origin, sizeof(Vector3));
    if (attachedTo) {
        const auto* carrier = static_cast<const CObject3D*>(attachedTo);
        const Quaternion& turn = carrier->orientation;
        double w = D(turn.w), x = D(turn.x), y = D(turn.y), z = D(turn.z);
        double xx = x * x;
        double axisX = w * w - LengthSquaredUnrounded<Sum3::ZYX>(x, y, z) + (xx + xx);
        float axisY = Store(Dot2DUnrounded(x, z, y, w) * 2.0);   // (xy + zw) * 2
        float axisZ = Store((x * z - y * w) * 2.0);
        float scale = Store(D(carrier->radius) * D(p->velocity.x) * D(GameFloat(0x0073CE08)));    // 0.65
        float offsetY = Store(D(axisY) * D(scale));
        float offsetZ = Store(D(axisZ) * D(scale));
        target.x = Store(axisX * D(scale) + D(target.x));
        target.y = Store(D(offsetY) + D(target.y));
        target.z = Store(D(offsetZ) + D(target.z));
    }
    p->size = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) * D(size) *
                    D(GameFloat(0x0073C3B0)));                       // (0.8 + [0, 0.4)) * size / 4
    double keep = D(GameFloat(0x0073C538));                          // 0.7
    double pull = D(GameFloat(0x0073C54C));                          // 0.3
    p->pos.x = Store(D(p->pos.x) * keep + D(target.x) * pull);
    p->pos.y = Store(D(p->pos.y) * keep + D(target.y) * pull);
    p->pos.z = Store(D(p->pos.z) * keep + D(size) * D(GameFloat(0x0073B868)) + D(target.z) * pull);
    return 1;
}
EI_REPLACE_METHOD(0x00495120, CParticleSource::UpdateRegeneration);

namespace {

// A running angle kept by the game (007A9BF8); VisionStar's satellites turn by it.
float SpinAngle() { return *reinterpret_cast<const volatile float*>(0x007A9BF8); }

} // namespace

// 00494E00. VisionStar: up to five particles. With lifetime 1 they are a star (frame 1) at the
// source and four satellites half a unit out, at the running angle times 0.2, 0.4, 0.6 and 0.8 (the
// first at angle 0); with another lifetime (used as the frame) just the one particle.
int CParticleSource::EmitVisionStar(Particle* p, int) {
    int count = particles.count;
    if (count > 4) return 0;
    if (count > 0 && lifetime != 1) return 0;
    p->color = 0xFFFFFF;
    p->frame = lifetime;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0);
    if (lifetime != 1) return 1;
    if (count == 0) {
        p->velocity.x = Store(x87::fcos(GameDouble(0x0073C340)));
        p->velocity.y = Store(x87::fsin(GameDouble(0x0073C340)));
        SetBits(&p->velocity.z, 0);
    } else {
        static const std::uintptr_t kRates[] = {0x0073CCA0, 0x0073CF6C, 0x0073CF68, 0x0073CF64};
        double angle = D(SpinAngle()) * D(GameFloat(kRates[count - 1]));
        p->velocity.x = Store(x87::fcos(angle));
        p->velocity.y = Store(x87::fsin(angle));
        SetBits(&p->velocity.z, 0);
    }
    double half = D(GameFloat(0x0073B86C));
    p->velocity.x = Store(D(p->velocity.x) * half);
    p->velocity.y = Store(D(p->velocity.y) * half);
    p->velocity.z = Store(D(p->velocity.z) * half);
    p->pos.x = Store(D(p->pos.x) + D(p->velocity.x));
    p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
    p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    return 1;
}
EI_REPLACE_METHOD(0x00494E00, CParticleSource::EmitVisionStar);

// 00494F30. VisionStar: the particles follow the source; with lifetime 1 their offsets turn by
// 0.07 radians per tick (and they flicker less). VisionStar3 (0x204C) sits 0.7 higher. The size
// eases (0.95) towards a random flicker of the source's size; once the source stops emitting (or
// loses its carrier) the source's size is zeroed and the particles fade, dropped below 0.03.
int CParticleSource::UpdateVisionStar(Particle* p) {
    float oldSize = p->size;
    p->color = 0xFFFFFFFF;
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0x3F800000);
    float flicker;
    SetBits(&flicker, 0x3E99999A);     // 0.3
    if (lifetime == 1) {
        double c = x87::fcos(GameDouble(0x0073CF78));
        SetBits(&flicker, 0x3E2E147B);     // 0.17
        double s = x87::fsin(GameDouble(0x0073CF78));
        float turnedY = Store(D(p->velocity.y) * c - D(p->velocity.x) * s);
        double turnedX = c * D(p->velocity.x) + D(p->velocity.y) * s;
        p->velocity.y = turnedY;
        p->velocity.x = Store(turnedX);
        p->pos.x = Store(D(p->pos.x) + D(p->velocity.x));
        p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
        p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    }
    if (type_ == 0x204C) p->pos.z = Store(D(p->pos.z) + D(GameFloat(0x0073C538)));
    double eased = (Unsigned(Next()) * GameDouble(0x0073CF70) + D(GameFloat(0x0073B850))) * D(size) * D(flicker) *
                       D(GameFloat(0x0073C52C)) +
                   D(oldSize) * D(GameFloat(0x0073CD50));
    p->size = Store(eased);
    if ((flags_ & 1) && attachedTo) return 1;
    p->size = Store(eased * D(GameFloat(0x0073CD50)));
    SetBits(&size, 0);
    if (!(p->size >= GameFloat(0x0073CD74))) {
        SetBits(&p->size, 0);
        p->color = 0xFFFFFF;
        return 0;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00494F30, CParticleSource::UpdateVisionStar);

namespace {

const std::int32_t* ZoneExitAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078AA38); }    // by life

} // namespace

// 00492F40. ZoneExit: alpha by the life left (the table at 0078AA38), dropped past 0; rising 0.05
// per tick, +0x2C slowing by 0.9, cycling through four frames, sized by +0x20.
int CParticleSource::UpdateZoneExit(Particle* p) {
    int life = p->life;
    p->color = (p->color & 0xFFFFFF) | (static_cast<std::uint32_t>(ZoneExitAlpha()[life]) << 24);
    p->life = --life;
    if (life < 0) return 0;
    p->pos.z = Store(D(p->pos.z) + D(GameFloat(0x0073C52C)));
    p->unknown_2C = Store(D(p->unknown_2C) * D(GameFloat(0x0073CD70)));
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    std::memcpy(&p->size, &p->velocity.x, sizeof(float));
    return 1;
}
EI_REPLACE_METHOD(0x00492F40, CParticleSource::UpdateZoneExit);

// --- FeebleMind (0x2049) update ----------------------------------------------------------------

namespace {

const std::int32_t* FeebleAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078ABB8); }    // by life

} // namespace

// 00495950. FeebleMind: alpha by the life left (dropped past 0); the particle circles: its offset
// (+0x20) is taken off, turned by 0.07 radians and grown by 1.1, then put back with a rise of
// 0.05. The size is +0x34 (growing by 1.04 per tick), the frame cycles through 16.
int CParticleSource::UpdateFeebleMind(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    float nextSize = *ParticleFloat(p, 0x34);
    p->color = (static_cast<std::uint32_t>(FeebleAlpha()[life]) << 24) | 0xFFFFFF;
    int frame = (p->frame + 1) & 15;
    p->pos.x = Store(D(p->pos.x) - D(p->velocity.x));
    p->pos.y = Store(D(p->pos.y) - D(p->velocity.y));
    p->pos.z = Store(D(p->pos.z) - D(p->velocity.z));
    double c = x87::fcos(GameDouble(0x0073CF78));
    double s = x87::fsin(GameDouble(0x0073CF78));
    float turnedY = Store(D(p->velocity.y) * c - D(p->velocity.x) * s);
    double turnedX = c * D(p->velocity.x) + D(p->velocity.y) * s;
    double grow = D(GameFloat(0x0073BAC4));                          // 1.1
    p->velocity.x = Store(turnedX * grow);
    p->velocity.y = Store(D(turnedY) * grow);
    p->pos.x = Store(D(p->velocity.x) + D(p->pos.x));
    p->pos.y = Store(D(p->velocity.y) + D(p->pos.y));
    double z = D(p->velocity.z) + D(p->pos.z);
    p->size = nextSize;
    p->frame = frame;
    p->pos.z = Store(z + D(GameFloat(0x0073C52C)));
    *ParticleFloat(p, 0x34) = Store(D(*ParticleFloat(p, 0x34)) * D(GameFloat(0x0073CF98)));    // 1.04
    return 1;
}
EI_REPLACE_METHOD(0x00495950, CParticleSource::UpdateFeebleMind);

// --- FeetCloud (0x2047): puffs at the carrier's feet; the emission is EmitFeetCloud --------------

namespace {

const std::int32_t* FeetAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078AC18); }    // by life

// Not rewritten (forwarders): the figure part named `name` (00516A80, null if none) and a part's
// world position (005188B0).
void* FindFigurePart(void* unit, const char* name) {
    using Fn = void*(__thiscall*)(void*, const char*);
    return reinterpret_cast<Fn>(0x00516A80)(unit, name);
}
Vector3* FigurePartPosition(void* unit, Vector3* out, void* part) {
    using Fn = Vector3*(__thiscall*)(void*, Vector3*, void*);
    return reinterpret_cast<Fn>(0x005188B0)(unit, out, part);
}

const char* kLeftFoot = reinterpret_cast<const char*>(0x0078B008);     // "ll3"
const char* kRightFoot = reinterpret_cast<const char*>(0x0078B004);    // "rl3"
const char* kBody = reinterpret_cast<const char*>(0x0078AD38);         // "bd"

} // namespace

// 00495C80. FeetCloud puffs: alpha by the life left (capped at 8 once the source stops emitting
// or loses its carrier; dropped past 0), the source's lifetime as the colour. The puff's offset
// (+0x2C) grows by its speed (+0x38, slowing by 0.9) and the puff eases (+0x24 of the way, which
// shrinks by 0.98) towards its foot (the element of +0x54) plus the offset; its size is +0x20
// (shrinking by 0.8).
int CParticleSource::UpdateFeetCloud(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    if (!(flags_ & 1) || !attachedTo) {
        if (life > 8) life = 8;
        p->life = life;
    }
    p->color = (static_cast<std::uint32_t>(FeetAlpha()[life]) << 24) + static_cast<std::uint32_t>(lifetime);
    float* offset = &p->unknown_2C;
    offset[0] = Store(D(p->drift.x) + D(offset[0]));
    offset[1] = Store(D(p->drift.y) + D(offset[1]));
    offset[2] = Store(D(p->drift.z) + D(offset[2]));
    float oldY = p->pos.y, oldZ = p->pos.z;
    double oldX = D(p->pos.x);
    float pull = p->velocity.y;
    const ParticleElement& foot = elements[p->state];
    double targetX = D(offset[0]) + D(foot.pos.x);
    float targetY = Store(D(offset[1]) + D(foot.pos.y));
    float targetZ = Store(D(offset[2]) + D(foot.pos.z));
    float towardX = Store(targetX * D(pull));
    float towardY = Store(D(targetY) * D(pull));
    float towardZ = Store(D(targetZ) * D(pull));
    double keep = D(EI_P_ONE) - D(pull);
    float keepF = Store(keep);
    double x = oldX * keep + D(towardX);
    float keptY = Store(D(keepF) * D(oldY));
    float keptZ = Store(D(keepF) * D(oldZ));
    p->pos.y = Store(D(keptY) + D(towardY));
    std::memcpy(&p->size, &p->velocity.x, sizeof(float));
    p->pos.x = Store(x);
    p->pos.z = Store(D(keptZ) + D(towardZ));
    p->velocity.x = Store(D(p->velocity.x) * D(GameFloat(0x0073CCA8)));    // 0.8
    p->velocity.y = Store(D(p->velocity.y) * D(GameFloat(0x0073CF9C)));    // 0.98
    double slow = D(GameFloat(0x0073CD70));                                 // 0.9
    p->drift.x = Store(D(p->drift.x) * slow);
    p->drift.y = Store(D(p->drift.y) * slow);
    p->drift.z = Store(D(p->drift.z) * slow);
    return 1;
}
EI_REPLACE_METHOD(0x00495C80, CParticleSource::UpdateFeetCloud);

// 00495E00. FeetCloud feet: on a unit carrier the element sits on its foot (left when +0x34 is
// set, else right), else on its body, else on its position raised by +0x14C; without one on the
// source. +0x18 gets the carrier figure's radius and the countdown +0x30 is reset.
void CParticleSource::TickFeetCloud(ParticleElement* e) {
    if (attachedTo && ClassOf(attachedTo) == kUnitClass) {
        void* unit = attachedTo;
        void* part = FindFigurePart(unit, *ElementInt(e, 0x34) ? kLeftFoot : kRightFoot);
        if (!part) part = FindFigurePart(unit, kBody);
        if (part) {
            Vector3 at;
            FigurePartPosition(unit, &at, part);
            e->pos = at;
        } else {
            const float* place = &static_cast<const CObject3D*>(unit)->position.x;
            float raise = static_cast<const CObject3D*>(unit)->above.z;
            std::memcpy(&e->pos, place, 2 * sizeof(float));
            e->pos.z = Store(D(place[2]) + D(raise));
        }
    } else {
        std::memcpy(&e->pos, &origin, sizeof(Vector3));
    }
    Vector3 offset;
    AttachmentOffset(&offset, ElementFloat(e, 0x18));
    *ElementInt(e, 0x30) = 0;
}
EI_REPLACE_METHOD(0x00495E00, CParticleSource::TickFeetCloud);

// --- Silence (0x2048) ----------------------------------------------------------------------------

// 00495320. Silence: the first two particles are orbs at the source (state 1) circling in step
// (+0x20 the angle, +0x24 its step; +0x2C the tilt, +0x30 its step; the second starts at pi).
// Further ones are wisps (state 0) somewhere along a random orb's last step, thrown out in a
// random direction (inside the unit ball, normalised) by 0.8 of the orb's size.
int CParticleSource::EmitSilence(Particle* p, int) {
    if (particles.count <= 1) {
        p->color = 0xFFFFFF;
        p->frame = 6;
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
        SetBits(&p->size, 0);
        float start = particles.count != 0 ? GameFloat(0x0073B858) : GameFloat(0x0073CF80);    // 0 / pi
        p->velocity.x = start;
        p->unknown_2C = start;
        SetBits(&p->velocity.y, 0x3DCCCCCD);     // 0.1
        SetBits(&p->unknown_30, 0x3DF5C28F);     // 0.12
        SetBits(ParticleFloat(p, 0x34), 0x3F800000);
        p->state = 1;
        return 1;
    }
    auto* node = particles.head;
    if (Next() & 1) node = node->next;
    const Particle* orb = node->data;
    double t = Unsigned(Next()) * EI_P_SCALE_1;
    float startX = Store(D(orb->pos.x) - t * D(orb->drift.x));
    float startY = Store(D(orb->pos.y) - t * D(orb->drift.y));
    float startZ = Store(D(orb->pos.z) - t * D(orb->drift.z));
    float x, y, z;
    for (int tries = 10;;) {
        x = SignedUnit();
        y = Store(Range(-1.0f, 1.0f));
        double zu = Range(-1.0f, 1.0f);
        z = Store(zu);
        if (!(zu * D(z) + D(y) * D(y) + D(x) * D(x) >= D(EI_P_ONE))) break;
        if (!(--tries > 0)) break;
    }
    Vector3 v;
    v.Construct(x, y, z);
    double s = D(orb->size);
    float a = Store(D(v.x) * s);
    float b = Store(D(v.y) * s);
    double c = s * D(v.z);
    SetBits(&p->size, 0);
    double k = D(GameFloat(0x0073CCA8));    // 0.8
    float a8 = Store(D(a) * k);
    float b8 = Store(D(b) * k);
    double c8 = c * k;
    p->pos.x = Store(D(a8) + D(startX));
    double y8 = D(b8) + D(startY);
    p->pos.z = Store(c8 + D(startZ));
    p->pos.y = Store(y8);
    p->velocity.x = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) * D(size) *
                          D(GameFloat(0x0073CF84)));    // (0.8 + [0, 0.4)) * size * 0.08
    p->color = 0xFFFFFFFF;
    p->frame = 6;
    p->state = 0;
    p->life = 7;
    return 1;
}
EI_REPLACE_METHOD(0x00495320, CParticleSource::EmitSilence);

// 004955D0. Silence: once the source stops emitting (or loses its carrier) the orbs
// shrink the source by 0.85 per tick, everything going below 0.02. An orb circles the source on a
// tilted circle (radius: half the carrier's +0x134, kept in +0x34), half a radius up, keeping its
// step in +0x38 and flickering in size. A wisp shows +0x20 (shrinking by 0.8) for its life.
int CParticleSource::UpdateSilence(Particle* p) {
    if (!(flags_ & 1) || !attachedTo) {
        if (p->state) size = Store(D(size) * D(GameFloat(0x0073CEB8)));    // 0.85
        if (!(size >= GameFloat(0x0073CCD8))) {                                 // 0.02
            SetBits(&p->size, 0);
            p->color = 0xFFFFFF;
            return 0;
        }
    }
    if (!p->state) {
        if (--p->life <= 0) {
            SetBits(&p->size, 0);
            return 0;
        }
        double shrunk = D(p->velocity.x) * D(GameFloat(0x0073CCA8));
        std::memcpy(&p->size, &p->velocity.x, sizeof(float));
        p->velocity.x = Store(shrunk);
        return 1;
    }
    double radius;
    float* kept = ParticleFloat(p, 0x34);
    if (attachedTo) {
        radius = D(static_cast<const CObject3D*>(attachedTo)->radius) *
                 D(GameFloat(0x0073B86C));
        *kept = Store(radius);
    } else {
        radius = D(*kept);
    }
    double c = x87::fcos(D(p->velocity.x));
    p->color = 0xFFFFFFFF;
    float sn = Store(x87::fsin(D(p->velocity.x)));
    double hu = x87::fsin(D(p->unknown_2C)) * GameDouble(0x0073CC88);    // * 0.5
    float h = Store(hu);
    double length = x87::fsqrt(hu * D(h) + D(sn) * D(sn) + c * c);
    double ku = radius / length;
    float k = Store(ku);
    double cx = c * ku;
    float sy = Store(D(k) * D(sn));
    float sz = Store(D(k) * D(h));
    float nx = Store(cx + D(origin.x));
    float ny = Store(D(sy) + D(origin.y));
    double nz = D(sz) + D(origin.z);
    double z = radius * D(GameFloat(0x0073B86C)) + nz;
    SetBits(&p->size, 0x3F800000);
    p->drift.x = Store(D(nx) - D(p->pos.x));
    p->pos.x = nx;
    p->drift.y = Store(D(ny) - D(p->pos.y));
    p->pos.y = ny;
    p->drift.z = Store(z - D(p->pos.z));
    p->pos.z = Store(z);
    p->velocity.x = Store(D(p->velocity.y) + D(p->velocity.x));
    p->unknown_2C = Store(D(p->unknown_30) + D(p->unknown_2C));
    p->size = Store((Unsigned(Next()) * GameDouble(0x0073CED8) + D(GameFloat(0x0073CD70))) * D(size) *
                    D(GameFloat(0x0073CF88)));    // (0.9 + [0, 0.2)) * size * 0.22
    return 1;
}
EI_REPLACE_METHOD(0x004955D0, CParticleSource::UpdateSilence);

// --- Modifier1-12 (0x201B-0x2026): update and element tick; the emission is 00490130 ---------------

namespace {

const std::int32_t* ModifierAlphaIn() { return reinterpret_cast<const std::int32_t*>(0x0078A800); }     // by tick
const std::int32_t* ModifierAlphaOut() { return reinterpret_cast<const std::int32_t*>(0x0078A88C); }    // by tick

// x87 FCOMP's "below" (also taken when unordered).
bool Below(double a, double b) { return !(a >= b); }

} // namespace

// 00490990. Modifier: the first twelve frames are the marker riding its element (state), its size
// shrinking (negative source size) or growing, alpha by the element's tick. Later frames are
// sparks stepping through the frames (back to 12 past the last), flying out by +0x20 plus their
// spread (+0x2C, slowing by 0.9), fading by 48 per tick and shrinking by 0.9.
int CParticleSource::UpdateModifier(Particle* p) {
    if (--p->life == 0) return 0;
    if (p->frame < 12) {
        double oldSize = D(p->size);
        const ParticleElement& e = elements[p->state];
        std::memcpy(&p->pos, &e.pos, sizeof(Vector3));
        SetBits(&p->size, 0x3F800000);
        if (Below(D(size), D(EI_P_ZERO))) {
            p->size = Store(oldSize * D(GameFloat(0x0073CEA8)));    // 1 / 1.07
            p->color = (static_cast<std::uint32_t>(ModifierAlphaIn()[elements[p->state].ticks]) << 24) | 0xFFFFFF;
        } else {
            p->size = Store(oldSize * D(GameFloat(0x0073CEA4)));    // 1.07
            p->color = (static_cast<std::uint32_t>(ModifierAlphaOut()[elements[p->state].ticks]) << 24) | 0xFFFFFF;
        }
        return 1;
    }
    if (--p->frameTimer == 0) {
        if (++p->frame >= frameCount) p->frame = 12;
        p->frameTimer = frameDelay;
    }
    float* spread = &p->unknown_2C;
    double slow = D(GameFloat(0x0073CD70));    // 0.9
    for (int i = 0; i < 3; ++i) spread[i] = Store(D(spread[i]) * slow);
    double oldSize = D(p->size);
    double x = D(spread[0]) + D(p->velocity.x);
    float y = Store(D(spread[1]) + D(p->velocity.y));
    float z = Store(D(spread[2]) + D(p->velocity.z));
    p->pos.y = y;
    p->pos.x = Store(x);
    p->pos.z = z;
    std::int32_t a = static_cast<std::int32_t>(p->color >> 24) - 0x30;
    if (a < 0) a = 0;
    p->size = Store(oldSize * slow);
    p->color = (static_cast<std::uint32_t>(a) << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x00490990, CParticleSource::UpdateModifier);

// 00490B00. Modifier markers: for ten ticks (then the source stops emitting) each remembers its
// position and moves to the source plus its offset (+0x18), raised by spreadMin.x; the offset
// widens by 1.4 (negative source size) or narrows by 1/1.4 per tick.
void CParticleSource::TickModifier(ParticleElement* e) {
    int tick = e->ticks++;
    if (tick >= 10) {
        flags_ &= ~1u;
        return;
    }
    std::memcpy(ElementFloat(e, 0x0C), &e->pos, sizeof(Vector3));
    float* offset = ElementFloat(e, 0x18);
    e->pos.x = Store(D(offset[0]) + D(origin.x));
    e->pos.y = Store(D(offset[1]) + D(origin.y));
    e->pos.z = Store(D(offset[2]) + D(origin.z));
    e->pos.z = Store(D(spreadMin.x) + D(e->pos.z));
    double k = Below(D(size), D(EI_P_ZERO)) ? D(GameFloat(0x0073CDF8)) : D(GameFloat(0x0073CEAC));    // 1.4 / 1 / 1.4
    for (int i = 0; i < 3; ++i) offset[i] = Store(k * D(offset[i]));
}
EI_REPLACE_METHOD(0x00490B00, CParticleSource::TickModifier);

// --- LightningBlast (0x2030): update and element tick; the emission is 004923B0 -------------------

namespace {

const std::int32_t* LightningAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078A9F8); }    // by life

bool ZeroOrNaN(float f) { return f == 0.0f || x87::IsNaN(f); }

} // namespace

// 00492B80. LightningBlast: bolts (state n) ride element n-1; a flash (state set, +0x4C set) shows
// once (and makes the source emit 50 per tick), then goes. Sparks (state 0): alpha by the life
// left (the table shifted by 22, as the original), size +0x2C (shrinking by 0.9); with +0x4C set
// they jump to +0x20 and step a frame, else cycle four frames flying by +0x38 (slowing by 0.6).
int CParticleSource::UpdateLightning(Particle* p) {
    int life = --p->life;
    if (life < 0) return 0;
    if (p->state != 0) {
        if (p->frameTimer != 0) {
            if (ZeroOrNaN(p->size)) {
                emitCount = 50;
                std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
                return 1;
            }
            SetBits(&p->size, 0);
            return 0;
        }
        std::memcpy(&p->pos, &elements[p->state - 1].pos, sizeof(Vector3));
        std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
        return 1;
    }
    p->color = (static_cast<std::uint32_t>(LightningAlpha()[life]) << 22) | 0xFFFFFF;
    if (p->frameTimer != 0) {
        double shrunk = D(p->unknown_2C) * D(GameFloat(0x0073CD70));
        ++p->frame;
        std::memcpy(&p->pos, &p->velocity, sizeof(Vector3));
        std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
        p->unknown_2C = Store(shrunk);
        return 1;
    }
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->pos.x = Store(D(p->drift.x) + D(p->pos.x));
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
    p->pos.z = Store(D(p->drift.z) + D(p->pos.z));
    p->unknown_2C = Store(D(p->unknown_2C) * D(GameFloat(0x0073CD70)));
    double slow = D(GameFloat(0x0073CDE0));    // 0.6
    p->drift.x = Store(D(p->drift.x) * slow);
    p->drift.y = Store(D(p->drift.y) * slow);
    p->drift.z = Store(D(p->drift.z) * slow);
    return 1;
}
EI_REPLACE_METHOD(0x00492B80, CParticleSource::UpdateLightning);

// 00492CC0. LightningBlast bolt heads: the source emits 50 per tick; each head remembers its
// position (+0x18), moves by its speed (+0x0C), falls (its z speed dropping by 0.075), and the
// source stops emitting from the tenth tick on.
void CParticleSource::TickLightning(ParticleElement* e) {
    emitCount = 50;
    int tick = ++e->ticks;
    std::memcpy(ElementFloat(e, 0x18), &e->pos, sizeof(Vector3));
    const float* speed = ElementFloat(e, 0x0C);
    e->pos.x = Store(D(speed[0]) + D(e->pos.x));
    e->pos.y = Store(D(speed[1]) + D(e->pos.y));
    e->pos.z = Store(D(speed[2]) + D(e->pos.z));
    *ElementFloat(e, 0x14) = Store(D(speed[2]) - D(GameFloat(0x0073CF28)));
    if (tick >= 10) flags_ &= ~1u;
}
EI_REPLACE_METHOD(0x00492CC0, CParticleSource::TickLightning);

// --- Teleport (0x2019) -----------------------------------------------------------------------------

// 0048FDB0. Teleport: a spark at a random point of a disc of the source's size (denser near the
// edge), a little below the source and lifted by its first z step. With a positive size it is
// opaque and fades out (state -19 per tick) while its step grows by 1.2; else it starts clear and
// fades in (state 19) while its step shrinks by 1/1.2.
int CParticleSource::EmitTeleport(Particle* p, int) {
    p->color = 0xFFFFFFFF;
    p->frame = 0;
    p->life = lifetime;
    p->frameTimer = frameDelay;
    float magnitude;
    SetBits(&magnitude, Bits(&size) & 0x7FFFFFFFu);
    double t = Unsigned(Next()) * EI_P_SCALE_1;
    float spread = Store((t + D(EI_P_ONE)) * t * D(GameFloat(0x0073B86C)));
    double angle = Unsigned(Next()) * GameDouble(0x0073CE88);    // 2 pi * 2^-32
    double reach = D(magnitude) * D(spread);
    float sx = Store(x87::fsin(angle) * reach);
    double cy = x87::fcos(angle) * reach;
    p->pos.y = Store(cy + D(origin.y));
    p->pos.x = Store(D(sx) + D(origin.x));
    std::memcpy(&p->pos.z, &origin.z, sizeof(float));
    SetBits(&p->size, 0);
    p->pos.z = Store((Unsigned(Next()) * GameDouble(0x0073CD58) - D(GameFloat(0x0073B868))) * D(magnitude) +
                     D(p->pos.z));    // [-0.4, -0.2) * |size|
    p->unknown_2C = Store((D(GameFloat(0x0073CDB8)) - D(spread)) * D(GameFloat(0x0073BAD4)));    // (2 - spread) / 5
    if (Unsigned(Next()) * EI_P_SCALE_1 > D(spread)) {
        p->frame = static_cast<int>(Next() % 12) + 4;
    } else {
        p->frame = static_cast<int>(Next() & 3);
    }
    double rise = D(EI_P_ONE) - D(spread) + D(GameFloat(0x0073BAA4));    // 1 - spread + 3
    if (size > EI_P_ZERO) {
        SetBits(&p->velocity.x, 0x3F99999A);     // 1.2
        p->state = -19;
        p->velocity.z = Store(rise * D(magnitude) * D(GameFloat(0x0073CE80)));    // 0.15
    } else {
        p->color = 0xFFFFFF;
        SetBits(&p->velocity.x, 0x3F555555);     // 1 / 1.2
        p->state = 19;
        p->velocity.z = Store(rise * D(magnitude) * D(GameFloat(0x0073CE7C)));    // 2.25
    }
    p->pos.z = Store(D(p->velocity.z) + D(p->pos.z));
    return 1;
}
EI_REPLACE_METHOD(0x0048FDB0, CParticleSource::EmitTeleport);

// 00490060. Teleport sparks: frames 0-3 cycle (and 4-15 run on back to 4); the z step (+0x28) is
// redone scaled by +0x20; shown at +0x2C (hidden in the last tick); alpha moves by the state per
// tick, an unsigned sum capped at 255 (so a fade out below 19 jumps back to 255, as the original).
int CParticleSource::UpdateTeleport(Particle* p) {
    int life = --p->life;
    if (life == 0) return 0;
    if (--p->frameTimer == 0) {
        if (++p->frame == 4) p->frame = 0;
        if (p->frame == 16) p->frame = 4;
        p->frameTimer = frameDelay;
    }
    p->pos.z = Store(D(p->pos.z) - D(p->velocity.z));
    double step = D(p->velocity.x) * D(p->velocity.z);
    p->velocity.z = Store(step);
    p->pos.z = Store(step + D(p->pos.z));
    if (life == 1) {
        SetBits(&p->size, 0);
    } else {
        std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
    }
    std::uint32_t a = (p->color >> 24) + static_cast<std::uint32_t>(p->state);
    if (a >= 0xFF) a = 0xFF;
    p->color = (a << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x00490060, CParticleSource::UpdateTeleport);

// 00490110. Teleport: the source stops emitting after 35 ticks.
void CParticleSource::TickTeleport(ParticleElement* e) {
    if (++e->ticks > 35) flags_ &= ~1u;
}
EI_REPLACE_METHOD(0x00490110, CParticleSource::TickTeleport);

// --- CurseStars (0x2050) and StartTrans (0x2051) -------------------------------------------------

namespace {

const std::int32_t* CurseAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078AC98); }    // by life
const std::int32_t* TransAlpha() { return reinterpret_cast<const std::int32_t*>(0x0078ACF8); }    // by life

// A random point of the unit ball (ten tries, the last one kept), each coordinate rounded.
void RandomInBall(float& x, float& y, float& z, double& lengthSquared) {
    for (int tries = 10;;) {
        x = SignedUnit();
        y = SignedUnit();
        z = Store(Range(-1.0f, 1.0f));
        lengthSquared = LengthSquaredUnrounded<Sum3::ZYX, double>(x, y, z);   // (y + z) + x
        if (!(lengthSquared >= D(EI_P_ONE))) break;
        if (!(--tries > 0)) break;
    }
}

} // namespace

// 00496270. CurseStars: a star at a random point of the ball of the source's size around the
// source, drifting outwards (a tenth of the size per tick), for 20 ticks.
int CParticleSource::EmitCurseStars(Particle* p, int) {
    p->color = 0xFFFFFF;
    p->frame = static_cast<int>(Next() & 15);
    float x, y, z;
    double unused;
    RandomInBall(x, y, z, unused);
    Vector3 v;
    v.Construct(x, y, z);
    double s = D(size);
    float a = Store(D(v.x) * s);
    float b = Store(D(v.y) * s);
    double c = s * D(v.z);
    double px = D(a) + D(origin.x);
    float py = Store(D(b) + D(origin.y));
    float pz = Store(c + D(origin.z));
    SetBits(&p->size, 0);
    p->pos.y = py;
    p->pos.x = Store(px);
    p->pos.z = pz;
    double length = x87::fsqrt(c * c + D(b) * D(b) + D(a) * D(a));
    float lengthF = Store(length);
    p->life = 20;
    if (length > D(EI_P_ZERO)) {
        double k = D(size) * D(GameFloat(0x0073B850));    // 0.1
        float ak = Store(D(a) * k);
        float bk = Store(D(b) * k);
        double ck = c * k;
        p->drift.x = Store(D(ak) / D(lengthF));
        p->drift.y = Store(D(bk) / D(lengthF));
        p->drift.z = Store(ck / D(lengthF));
    } else {
        std::memset(&p->drift, 0, sizeof(Vector3));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00496270, CParticleSource::EmitCurseStars);

// 004964B0. CurseStars: alpha by the life left (hidden and dropped past 0); a star's size starts
// as a random part of the source's (0.04 of it, flickering) and shrinks by 0.95; it drifts (slowing
// by 0.9). Without a carrier the source stops emitting and is let go (Attach(nullptr)).
int CParticleSource::UpdateCurseStars(Particle* p) {
    int life = --p->life;
    if (life < 0) {
        p->color = 0xFFFFFF;
        SetBits(&p->size, 0);
        return 0;
    }
    p->color = (static_cast<std::uint32_t>(CurseAlpha()[life]) << 24) | 0xFFFFFF;
    if (p->size == 0.0f || x87::IsNaN(p->size)) {
        p->size = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) * D(size) *
                        D(GameFloat(0x0073CE90)));
    } else {
        p->size = Store(D(p->size) * D(GameFloat(0x0073CD50)));
    }
    p->pos.x = Store(D(p->drift.x) + D(p->pos.x));
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    p->pos.z = Store(D(p->drift.z) + D(p->pos.z));
    double slow = D(GameFloat(0x0073CD70));
    p->drift.x = Store(D(p->drift.x) * slow);
    p->drift.y = Store(D(p->drift.y) * slow);
    p->drift.z = Store(D(p->drift.z) * slow);
    if (!attachedTo) {
        flags_ &= ~5u;
        Attach(nullptr);
    }
    return 1;
}
EI_REPLACE_METHOD(0x004964B0, CParticleSource::UpdateCurseStars);

// 004965E0. StartTrans: the first particle is the glow (state 1, frame 12, 4 ticks); the others
// (15 ticks) start in a random direction at the source's size from it and come back in 18 ticks.
int CParticleSource::EmitStartTrans(Particle* p, int) {
    if (particles.count == 0) {
        p->life = 4;
        p->state = 1;
        p->frame = 12;
        std::memcpy(&p->pos, &origin, sizeof(Vector3));
    } else {
        p->life = 15;
        p->state = 0;
        p->frame = static_cast<int>(Next() % 12);
        float x, y, z;
        double lengthSquared;
        RandomInBall(x, y, z, lengthSquared);
        float length = Store(x87::fsqrt(lengthSquared));
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        double s = D(size);
        p->velocity.x = Store(D(direction.x) * s);
        p->velocity.y = Store(D(direction.y) * s);
        p->velocity.z = Store(D(direction.z) * s);
        double px = D(p->velocity.x) + D(origin.x);
        float py = Store(D(p->velocity.y) + D(origin.y));
        double pz = D(p->velocity.z) + D(origin.z);
        p->pos.y = py;
        p->pos.x = Store(px);
        p->pos.z = Store(pz);
        float ticks = static_cast<float>(p->life + 3);
        float nx = Store(-D(p->velocity.x));
        float ny = Store(-D(p->velocity.y));
        float nz = Store(-D(p->velocity.z));
        p->drift.x = Store(D(nx) / D(ticks));
        p->drift.y = Store(D(ny) / D(ticks));
        p->drift.z = Store(D(nz) / D(ticks));
        p->unknown_2C = Store(D(size) * D(GameFloat(0x0073C52C)));    // 0.05
    }
    p->color = 0xFFFFFF;
    SetBits(&p->size, 0);
    return 1;
}
EI_REPLACE_METHOD(0x004965E0, CParticleSource::EmitStartTrans);

// 00496840. StartTrans: the glow flickers at element 0's +0x18 while carried, then fades (by the
// life table, size times 0.6); the others come in, cycling four frames at +0x2C, fading out over
// their last ticks (capped at 4 once the carrier is gone).
int CParticleSource::UpdateStartTrans(Particle* p) {
    if (p->state != 0) {
        if (!attachedTo) {
            int life = --p->life;
            if (life < 0) return 0;
            p->color = (static_cast<std::uint32_t>(TransAlpha()[life]) << 24) | 0xFFFFFF;
            p->size = Store(D(p->size) * D(GameFloat(0x0073CDE0)));    // 0.6
            return 1;
        }
        p->color = 0xFFFFFFFF;
        p->size = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) *
                        D(*ElementFloat(elements, 0x18)));
        return 1;
    }
    int life = --p->life;
    if (life < 0) return 0;
    if (!attachedTo) {
        if (life >= 4) life = 4;
        p->life = life;
    }
    p->frame = (p->frame & ~3) | ((p->frame + 1) & 3);
    p->pos.x = Store(D(p->drift.x) + D(p->pos.x));
    p->pos.y = Store(D(p->drift.y) + D(p->pos.y));
    std::memcpy(&p->size, &p->unknown_2C, sizeof(float));
    p->pos.z = Store(D(p->drift.z) + D(p->pos.z));
    p->color = (static_cast<std::uint32_t>(TransAlpha()[p->life]) << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x00496840, CParticleSource::UpdateStartTrans);

// --- Nuke emission; its update and element tick are 00489190 / 004892E0 ---------------------------

namespace {

// base ^ exponent as VC6 inlines pow for this call: FLDLN2; FLD base; FYL2X; FIMUL exponent;
// FLDL2E; FMULP; then 2^x by FRNDINT / F2XM1 / FSCALE (left unrounded in ST0).
__declspec(naked) double __cdecl PowInline(double /*base*/, int /*exponent*/) {
    __asm {
        fldln2
        fld qword ptr [esp + 4]
        fyl2x
        fimul dword ptr [esp + 12]
        fldl2e
        fmulp st(1), st
        fld st(0)
        frndint
        fxch st(1)
        fsub st, st(1)
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        ret
    }
}

} // namespace

// 00488F90. Nuke: a puff at a random point of the disc of a fifth of the source's size, at a
// random height up to twice the size, drifting outwards and up (less as the blast ages: element
// 0's tick); later puffs live shorter, start at a later frame and are more opaque (4 ^ tick * 4,
// capped at 255).
int CParticleSource::EmitNuke(Particle* p, int) {
    int tick = elements[0].ticks;
    int rise = 11 - tick;
    float fifth = Store(D(size) * D(GameFloat(0x0073BAD4)));
    float reach = Store(Unsigned(Next()) * D(fifth) * EI_P_SCALE_1);
    float turn = Store(GameDouble(0x0073CAE8) + GameDouble(0x0073CAE8));    // 2 pi
    double angle = Unsigned(Next()) * D(turn) * EI_P_SCALE_1;
    volatile double s = x87::fsin(angle);    // kept as a double, as the original
    p->pos.x = Store(D(reach) * s + D(origin.x));
    volatile double c = x87::fcos(angle);
    p->pos.y = Store(D(reach) * c + D(origin.y));
    float top = Store(D(size) + D(size));
    p->pos.z = Store(Unsigned(Next()) * D(top) * EI_P_SCALE_1 + D(origin.z));
    p->size = Store(D(size) * D(GameFloat(0x0073BAD4)));
    p->drift.x = Store(s);
    p->drift.y = Store(c);
    p->drift.z = Store(static_cast<double>(rise) * D(size) * D(GameFloat(0x0073B850)));
    p->life = rise + 30;
    p->frame = elements[0].ticks / 3;
    std::uint32_t a = static_cast<std::uint32_t>(
        GameFtol(PowInline(D(GameFloat(0x0073CB24)), elements[0].ticks) * GameDouble(0x0073CCE8)));
    if (a > 0xFF) a = 0xFF;
    p->state = 0;
    p->color = (a << 24) | 0xFFFFFF;
    return 1;
}
EI_REPLACE_METHOD(0x00488F90, CParticleSource::EmitNuke);

// --- Tendril emission (EmitSmoke spread over the extent and laid on the ground) ------------------

// 0048ED60. A smoke puff (EmitSmoke), moved by one random amount in [-1, 1) times the extent in x
// and y, and set its own height above the ground there.
int CParticleSource::EmitTendril(Particle* p, int index) {
    if (!EmitSmoke(p, index)) return 0;
    double u = Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE);
    p->pos.x = Store(u * D(extent.x) + D(p->pos.x));
    p->pos.y = Store(u * D(extent.y) + D(p->pos.y));
    double ground = GroundHeight(p->pos.x, p->pos.y);
    p->pos.z = Store(D(p->pos.z) - D(origin.z) + ground);
    return 1;
}
EI_REPLACE_METHOD(0x0048ED60, CParticleSource::EmitTendril);

// --- Ground dust (0x203A / 0x203B) emission ------------------------------------------------------

namespace {

const float* DustAxisX() { return reinterpret_cast<const float*>(0x0078AAB8); }    // (+-0.707) by quadrant
const float* DustAxisY() { return reinterpret_cast<const float*>(0x0078AAC8); }

} // namespace

// 00493300. Ground dust: the first 128 particles of a tick run out on a circle (index * 0.049
// radians, step 0.1); the others along the four diagonals (by 32, step index * 1/310). All at the
// source, in element 0's colour, 6 ticks.
int CParticleSource::EmitGroundDust(Particle* p, int index) {
    float a, b;
    if (index >= 128) {
        int j = index - 128;
        int diagonal = j / 32;
        double fu = static_cast<double>(j & 31) * D(GameFloat(0x0073CF3C));
        float f = Store(fu);
        a = Store(fu * D(DustAxisX()[diagonal]));
        b = Store(D(f) * D(DustAxisY()[diagonal]));
    } else {
        double fu = static_cast<double>(index) * D(GameFloat(0x0073CF38));
        float f = Store(fu);
        a = Store(x87::fsin(fu) * GameDouble(0x0073CF30));
        b = Store(x87::fcos(D(f)) * GameDouble(0x0073CF30));
    }
    std::memcpy(&p->pos, &origin, sizeof(Vector3));
    SetBits(&p->size, 0);
    p->life = 6;
    p->velocity.x = a;
    p->frame = 15;
    p->velocity.y = b;
    SetBits(&p->velocity.z, 0);
    SetBits(&p->unknown_2C, 0x3D8F5C29);     // 0.07
    p->color = static_cast<std::uint32_t>(*ElementInt(elements, 0x30));
    SetBits(&p->unknown_30, 0x3F800000);
    return 1;
}
EI_REPLACE_METHOD(0x00493300, CParticleSource::EmitGroundDust);

// --- ShapeChange glow (0x2039) emission and element tick; the update is UpdateGlow ---------------

namespace {

// Not rewritten (forwarders through the objects' vtables and the original address): the unit's
// point `index` (vtable slot 52), the terrain's height (007AF5B8, slot 28) and the platforms'
// surface height over it (00560F00 on 007AF78C), all left unrounded in ST0.
Vector3* UnitPoint(void* unit, Vector3* out, int index) {
    using Fn = Vector3*(__thiscall*)(void*, Vector3*, int);
    return (*static_cast<Fn**>(unit))[52](unit, out, index);
}
double TerrainHeight(float x, float y) {
    void* terrain = reinterpret_cast<void*>(0x007AF5B8);
    using Fn = double(__thiscall*)(void*, float, float);
    return (*static_cast<Fn**>(terrain))[28](terrain, x, y);
}
double PlatformHeight(const Vector3* at, float ground) {
    using Fn = double(__thiscall*)(void*, const Vector3*, float);
    return reinterpret_cast<Fn>(0x00560F00)(reinterpret_cast<void*>(0x007AF78C), at, ground);
}

// FCOMP's "equal" (also taken when unordered).
bool SameOrNaN(float a, float b) { return a == b || x87::IsNaN(a) || x87::IsNaN(b); }

} // namespace

// 00492FC0. ShapeChange: on the first call one element: the last point (-1, -1, -1) and the colour
// by the alpha field (0 grey, 1 green, 2 orange). Then one glow per point of the carrying unit (up
// to +0x11C points, from the global offset 007AF61C), on the ground or platform under it, unless
// the point is where the last one was; the point's index goes in +0x4C and the life is -10 (set
// by UpdateGlow).
int CParticleSource::EmitGlow(Particle* p, int index) {
    void* unit = attachedTo;
    if (elementCount == 0) {
        SetElementCount(this, 1);
        float* mark = ElementFloat(elements, 0x18);
        SetBits(&mark[0], 0xBF800000);
        SetBits(&mark[1], 0xBF800000);
        SetBits(&mark[2], 0xBF800000);
        if (alpha == 0.0f || x87::IsNaN(alpha)) {
            *ElementInt(elements, 0x30) = 0xB0B0B0;
        } else if (Bits(&alpha) == 0x3F800000) {
            *ElementInt(elements, 0x30) = 0x40FF40;
        } else if (Bits(&alpha) == 0x40000000) {
            *ElementInt(elements, 0x30) = 0xFF8028;
        }
    }
    if (!unit) return 0;
    if (static_cast<double>(index) > D(unknown_11C)) return 0;
    Vector3 scratch;
    Vector3 point = *UnitPoint(unit, &scratch, index + *reinterpret_cast<const int*>(0x007AF61C));
    float* mark = ElementFloat(elements, 0x18);
    if (SameOrNaN(mark[0], point.x) && SameOrNaN(mark[1], point.y) && SameOrNaN(mark[2], point.z)) return 0;
    std::memcpy(mark, &point, sizeof(Vector3));
    Vector3 under;
    std::memcpy(&under.x, &point.x, sizeof(float));
    std::memcpy(&under.y, &point.y, sizeof(float));
    SetBits(&under.z, 0);
    float ground = Store(TerrainHeight(point.x, point.y));
    double top = PlatformHeight(&under, ground);
    p->pos.y = point.y;
    SetBits(&p->size, 0x3D8F5C29);     // 0.07
    p->pos.x = point.x;
    p->frame = 15;
    p->life = -10;
    p->pos.z = Store(top + D(GameFloat(0x0073CD18)));
    p->frameTimer = index;
    p->color = static_cast<std::uint32_t>(*ElementInt(elements, 0x30));
    return 1;
}
EI_REPLACE_METHOD(0x00492FC0, CParticleSource::EmitGlow);

// 004932F0. ShapeChange: one tick of emission only.
void CParticleSource::TickGlow(ParticleElement*) {
    flags_ &= ~1u;
}
EI_REPLACE_METHOD(0x004932F0, CParticleSource::TickGlow);

// --- Stun (0x204D) -------------------------------------------------------------------------------

namespace {

// Not rewritten (forwarder through the unit's vtable, slot 55): nonzero once the stun is over.
int UnitStunOver(void* unit) {
    using Fn = int(__thiscall*)(void*);
    return (*static_cast<Fn**>(unit))[55](unit);
}

} // namespace

// 00495FB0. Stun: a star at a random point of the ball of 0.8 times the carrier figure's radius
// (00485280) around the source, for 20 ticks, with a random size factor in +0x20. A unit carrier
// in state 3 (+0x264) gets four stars a tick living 5 ticks, else one; once the carrier is gone
// (nothing emitted) or its stun is over (vtable slot 55) the source stops and lets it go.
int CParticleSource::EmitStun(Particle* p, int) {
    Vector3 offset;
    float radius;
    AttachmentOffset(&offset, &radius);
    p->color = 0xFFFFFF;
    p->frame = 3;
    float x, y, z;
    for (int tries = 10;;) {
        x = SignedUnit();
        y = SignedUnit();
        double zu = Range(-1.0f, 1.0f);
        z = Store(zu);
        if (!(zu * D(z) + D(y) * D(y) + D(x) * D(x) >= D(EI_P_ONE))) break;
        if (!(--tries > 0)) break;
    }
    Vector3 v;
    v.Construct(x, y, z);
    double k = D(radius) * D(GameFloat(0x0073CCA8));    // 0.8
    float a = Store(D(v.x) * k);
    float b = Store(D(v.y) * k);
    double c = k * D(v.z);
    double px = D(a) + D(origin.x);
    float py = Store(D(b) + D(origin.y));
    double pz = c + D(origin.z);
    SetBits(&p->size, 0);
    p->pos.y = py;
    p->pos.z = Store(pz);
    p->pos.x = Store(px);
    p->life = 20;
    p->velocity.x = Store(Unsigned(Next()) * GameDouble(0x0073CE10) + D(GameFloat(0x0073C538)));
    void* unit = attachedTo;
    if (!unit) {
        flags_ &= ~5u;
        Attach(nullptr);
        return 0;
    }
    if (static_cast<const CUnit*>(unit)->moveMode == 3) {
        emitCount = 4;
        p->life = 5;
    } else {
        emitCount = 1;
    }
    if (UnitStunOver(unit)) {
        flags_ &= ~5u;
        Attach(nullptr);
    }
    return 1;
}
EI_REPLACE_METHOD(0x00495FB0, CParticleSource::EmitStun);

// 004961B0. Stun stars: hidden and dropped past their life; else flickering at a random size of
// the source's times +0x20. Without a carrier the source stops and is let go.
int CParticleSource::UpdateStun(Particle* p) {
    if (--p->life < 0) {
        p->color = 0xFFFFFF;
        SetBits(&p->size, 0);
        return 0;
    }
    p->color = 0xFFFFFFFF;
    p->size = Store((Unsigned(Next()) * GameDouble(0x0073CCB0) + D(GameFloat(0x0073CCA8))) * D(size) *
                    D(p->velocity.x));
    if (!attachedTo) {
        flags_ &= ~5u;
        Attach(nullptr);
    }
    return 1;
}
EI_REPLACE_METHOD(0x004961B0, CParticleSource::UpdateStun);
