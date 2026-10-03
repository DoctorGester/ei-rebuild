// CParticleSource per-type callbacks (+0x1E0 emit, +0x1E4 update, +0x1E8 element) not covered by
// effects.cpp / particle_emitters.cpp. Names are provisional; each notes its original address
// and the types (see Create, particle_create.cpp) that use it.
#include "particle_common.h"

#include "core_maths.h"
#include "object_maths.h"

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
        const float* toward = reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(&swirl) + 0x18);
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
    *speed = Store(x87::fsqrt(D(m.x) * D(m.x) + D(m.y) * D(m.y) + D(m.z) * D(m.z)));
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
    double vx = D(p->velocity.x);
    double vy = D(p->velocity.y);
    double vz = D(p->velocity.z);
    double shell = D(radius) * D(EI_SHELL);
    double scale = shell / x87::fsqrt(vx * vx + vy * vy + vz * vz);
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
        float t = Store(-((D(m.z) * D(p->velocity.z) + D(m.y) * D(p->velocity.y) + D(m.x) * D(p->velocity.x)) / speed));
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
    std::memcpy(&r, reinterpret_cast<const std::uint8_t*>(&ring) + 0x0C, sizeof(float));
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
