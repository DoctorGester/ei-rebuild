// CParticleSource per-type callbacks (+0x1E0 emit, +0x1E4 update, +0x1E8 element) not covered by
// effects.cpp / particle_emitters.cpp. Names are provisional; each notes its original address
// and the types (see Create, particle_create.cpp) that use it.
#include "particle_common.h"

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
