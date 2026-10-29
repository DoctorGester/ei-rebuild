// CParticleSource's per-frame tick and the particle pool. Each tick: copy the world's wind, follow the attachment
// (or the source's own position) and derive the motion since the last tick with a basis (side, up) around it,
// run the element callback on every element, the update callback on every particle (rebuilding the bounding
// box), then emit new particles. `double` holds what the original keeps on the FPU stack.
#include "effects.h"
#include "map_object_figures.h"

#include "random.h"

#include "ei/x87.h"
#include "lib/bytes.h"
#include "lib/vecmath.h"
#include "lib/nan_audit.h"
#include "world.h"

#include <bit>
#include <cstring>
#include <list>

namespace {

inline double D(float f) { return f; }

// Constants the original reads from memory at run time.
#define EI_ZERO 0.0f   // the z of the up axis the side is crossed with
#define EI_ONE 1.0f
#define EI_HALF 0.5f
#define EI_STILL 0.001   // below this the source is not moving
#define EI_DEGENERATE 1e-10

constexpr float kFarBits = 100000.0f;
constexpr float kNearBits = -100000.0f;

void SetBits(float& f, std::uint32_t bits) { f = std::bit_cast<float>(bits); }

// sqrt((x*x + y*y) + z*z), all on the FPU.
double LengthDouble(const Vector3& v) { return std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(v)); }

// sqrt((y*y + z*z) + x*x): the order the original uses after rebuilding a degenerate side.
double LengthYZX(const Vector3& v) { return std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(v)); }

// v *= 1 / length, the reciprocal kept on the FPU.
void Normalize(Vector3& v, double length) {
    double scale = D(EI_ONE) / length;
    v.x = static_cast<float>(scale * D(v.x));
    v.y = static_cast<float>(scale * D(v.y));
    v.z = static_cast<float>(scale * D(v.z));
}

// The bounding-box fold of the tick and AddParticle: the minimum takes the particle when it is
// smaller (or unordered), the maximum when the current maximum is smaller (or unordered).
void GrowBounds(Vector3& low, Vector3& high, const Vector3& p) {
    low.x = p.x >= low.x ? low.x : p.x;
    low.y = p.y >= low.y ? low.y : p.y;
    low.z = p.z >= low.z ? low.z : p.z;
    high.x = high.x >= p.x ? high.x : p.x;
    high.y = high.y >= p.y ? high.y : p.y;
    high.z = high.z >= p.z ? high.z : p.z;
}

// Saves the fields the update callback may change, so a rejected update can be undone (dword copies).
void SavePrevious(Particle* p) {
    p->prevPos = p->pos;
    p->prevColor = p->color;
    p->prevSize = p->size;
    p->prevFrame = p->frame;
}

void RestorePrevious(Particle* p) {
    p->pos = p->prevPos;
    p->frame = p->prevFrame;
    p->size = p->prevSize;
    p->color = p->prevColor;
}

// The tick's steps, as helpers on the source.
struct TickSteps {
    CParticleSource* s;

    // Motion since the last tick, from the attachment point plus the local position (or the
    // position alone); the emission point moves there.
    void Follow() {
        if (s->attachedTo) {
            Vector3 point;
            const Vector3* a = s->AttachmentPoint(&point, s->attachedTo);
            float wx = static_cast<float>(D(a->x) + D(s->localPosition.x));
            float wy = static_cast<float>(D(a->y) + D(s->localPosition.y));
            float wz = static_cast<float>(D(a->z) + D(s->localPosition.z));
            s->motion.x = static_cast<float>(D(wx) - D(s->origin.x));
            s->motion.y = static_cast<float>(D(wy) - D(s->origin.y));
            s->motion.z = static_cast<float>(D(wz) - D(s->origin.z));
            s->origin.x = wx;
            s->origin.y = wy;
            s->origin.z = wz;
        } else {
            s->motion.x = static_cast<float>(D(s->localPosition.x) - D(s->origin.x));
            s->motion.y = static_cast<float>(D(s->localPosition.y) - D(s->origin.y));
            s->motion.z = static_cast<float>(D(s->localPosition.z) - D(s->origin.z));
            s->origin = s->localPosition;
        }
    }

    // side = normalize(motion x (0, 0, 1)) (another perpendicular when that is degenerate),
    // up = normalize(motion x side); both zero when the source barely moved.
    void BuildBasis() {
        const Vector3& m = s->motion;
        double distance = LengthDouble(m);
        s->distance = static_cast<float>(distance);
        EI_NAN_AUDIT(distance);
        if (distance <= EI_STILL) {
            s->side = {};
            s->up = {};
            return;
        }
        double zero = EI_ZERO;
        s->side.x = static_cast<float>(D(m.y) - zero);
        s->side.y = static_cast<float>(zero - D(m.x));
        s->side.z = 0.0f;
        double length = LengthDouble(s->side);
        EI_NAN_AUDIT(length);
        if (length < EI_DEGENERATE) {
            float mx = m.x;
            s->side.x = static_cast<float>(D(m.y) - D(m.z));
            s->side.y = static_cast<float>(-D(mx));
            s->side.z = mx;
            length = LengthYZX(s->side);
        }
        Normalize(s->side, length);
        const Vector3& v = s->side;
        float ux = static_cast<float>(D(m.y) * D(v.z) - D(m.z) * D(v.y));
        float uy = static_cast<float>(D(m.z) * D(v.x) - D(v.z) * D(m.x));
        float uz = static_cast<float>(D(m.x) * D(v.y) - D(m.y) * D(v.x));
        s->up.x = ux;
        s->up.y = uy;
        s->up.z = uz;
        Normalize(s->up, LengthDouble(s->up));
    }

    void RunElements() {
        for (int i = 0; i < std::ssize(s->elements); ++i)
            (s->*s->elementFn)(s->elements.data() + i);
    }

    // Updates every particle. A particle the previous tick rejected is unlinked and returned to
    // the pool first, but (as in the original) is still updated and counted in the bounds.
    void UpdateParticles() {
        CParticleManager* pool = s->Manager();
        auto node = s->particles.begin();
        while (node != s->particles.end()) {
            auto next = std::next(node);
            Particle* p = *node;
            if (p->dead) {
                s->particles.erase(node);
                pool->ReleaseParticle(p);
            }
            SavePrevious(p);
            if (!(s->*s->updateFn)(p)) {
                p->dead = 1;
                RestorePrevious(p);
            }
            GrowBounds(s->boundsMin, s->boundsMax, p->pos);
            node = next;
        }
    }

    // One emission attempt: a particle from the pool, kept when the emit callback accepts it.
    void EmitOne(int index) {
        CParticleManager* pool = s->Manager();
        Particle* p = pool->AcquireParticle();
        if (!p) return;
        if ((s->*s->emitFn)(p, index)) s->AddParticle(p);
        else pool->ReleaseParticle(p);
    }

    // max(emitCount, distance / emitSpacing) tries, each emitting with emitChance percent; a
    // source above 50% that emitted nothing gets one particle anyway.
    void Emit() {
        int tries = x87::fistp(static_cast<float>(D(s->distance) / D(s->emitSpacing)));
        if (tries <= s->emitCount) tries = s->emitCount;
        if (!(s->flags & ParticleSourceFlag::Emitting)) return;
        if ((s->flags & ParticleSourceFlag::AttachedOnly) && !s->attachedTo) return;
        int emitted = 0;
        if (tries > 0) {
            for (int i = 0; i < tries; ++i) {
                if (std::ssize(s->particles) >= s->maxParticles) break;
                std::uint32_t roll = g_clientRandom.Next();
                if (static_cast<int>(roll % 100) < s->emitChance) EmitOne(emitted++);
            }
            if (emitted != 0) return;
        }
        if (s->emitChance > 50 && tries > 0 && std::ssize(s->particles) < s->maxParticles) EmitOne(0);
    }

    // Centre and half extents of the bounds.
    void FinishBounds() {
        float cx = static_cast<float>((D(s->boundsMax.x) + D(s->boundsMin.x)) * D(EI_HALF));
        float cy = static_cast<float>((D(s->boundsMax.y) + D(s->boundsMin.y)) * D(EI_HALF));
        float cz = static_cast<float>((D(s->boundsMax.z) + D(s->boundsMin.z)) * D(EI_HALF));
        s->clip.center.x = cx;
        s->clip.center.y = cy;
        s->clip.center.z = cz;
        s->clip.boxMin.x = static_cast<float>(D(s->boundsMin.x) - D(cx));
        s->clip.boxMin.y = static_cast<float>(D(s->boundsMin.y) - D(cy));
        s->clip.boxMin.z = static_cast<float>(D(s->boundsMin.z) - D(cz));
        s->clip.boxMax.x = static_cast<float>(D(s->boundsMax.x) - D(cx));
        s->clip.boxMax.y = static_cast<float>(D(s->boundsMax.y) - D(cy));
        s->clip.boxMax.z = static_cast<float>(D(s->boundsMax.z) - D(cz));
        s->clip.flags |= 2;
    }
};

} // namespace

// --- Particle pool --------------------------------------------------------------------------------

// The head of the free list, or a fresh 0x64-byte particle when it is empty.
Particle* CParticleManager::AcquireParticle() {
    if (freeParticles.empty()) return new Particle;
    Particle* p = freeParticles.front();
    freeParticles.pop_front();
    return p;
}

void CParticleManager::ReleaseParticle(Particle* p) { freeParticles.push_back(p); }

// A newly emitted particle: alive, its "prev" fields equal to the current ones, inside
// the bounds, appended to the list.
void CParticleSource::AddParticle(Particle* p) {
    p->dead = 0;
    SavePrevious(p);
    GrowBounds(boundsMin, boundsMax, p->pos);
    particles.push_back(p);
}

// Returns 1 while the source is still needed: a free source while it keeps
// alive (ParticleSourceFlag::KeepAlive) or has particles, an attached one while it emits or has particles.
bool CParticleSource::RunTick() {
    if (hiddenTicks > 0x38 && (flags & ParticleSourceFlag::IdlesOffScreen)) return true;
    const CWorldClient* world = &g_clientWorld;
    lib::LoadBytes(windDirection, world->windDirection);
    windStrength = std::bit_cast<float>(world->windStrength);
    boundsMin.x = kFarBits;
    boundsMin.y = kFarBits;
    boundsMin.z = kFarBits;
    boundsMax.x = kNearBits;
    boundsMax.y = kNearBits;
    boundsMax.z = kNearBits;
    if (attachedTo && (attachedTo->flags & MapObjectFlag::Removed)) Attach(nullptr);

    TickSteps steps{this};
    steps.Follow();
    steps.BuildBasis();
    steps.RunElements();
    steps.UpdateParticles();
    steps.Emit();
    steps.FinishBounds();

    if (!attachedTo) {
        if (flags & ParticleSourceFlag::KeepAlive) return true;
        return !particles.empty();
    }
    if (flags & ParticleSourceFlag::Emitting) return true;
    return !particles.empty();
}
