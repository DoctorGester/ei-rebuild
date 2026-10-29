// ParticleSource's per-frame tick and the particle pool. Each tick: copy the world's wind, follow the attachment
// (or the source's own position) and derive the motion since the last tick with a basis (side, up) around it,
// run the element callback on every element, the update callback on every particle (rebuilding the bounding
// box), then emit new particles.
#include "effects.h"
#include "map_object_figures.h"

#include "random.h"

#include "lib/bytes.h"
#include "lib/vecmath.h"
#include "lib/nan_audit.h"
#include "world.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <list>

namespace {

// Constants the original reads from memory at run time.
#define EI_ZERO 0.0f   // the z of the up axis the side is crossed with
#define EI_ONE 1.0f
#define EI_HALF 0.5f
#define EI_STILL 0.001f   // below this the source is not moving
#define EI_DEGENERATE 1e-10f

constexpr float kFarBits = 100000.0f;
constexpr float kNearBits = -100000.0f;

void SetBits(float& f, std::uint32_t bits) { f = std::bit_cast<float>(bits); }

// v *= 1 / length.
void Normalize(Vector3& v, float length) {
    float scale = EI_ONE / length;
    v.x = scale * v.x;
    v.y = scale * v.y;
    v.z = scale * v.z;
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
    ParticleSource* s;

    // Motion since the last tick, from the attachment point plus the local position (or the
    // position alone); the emission point moves there.
    void Follow() {
        if (s->attachedTo) {
            Vector3 point;
            const Vector3* a = s->AttachmentPoint(&point, s->attachedTo);
            float wx = a->x + s->localPosition.x;
            float wy = a->y + s->localPosition.y;
            float wz = a->z + s->localPosition.z;
            s->motion.x = wx - s->origin.x;
            s->motion.y = wy - s->origin.y;
            s->motion.z = wz - s->origin.z;
            s->origin.x = wx;
            s->origin.y = wy;
            s->origin.z = wz;
        } else {
            s->motion.x = s->localPosition.x - s->origin.x;
            s->motion.y = s->localPosition.y - s->origin.y;
            s->motion.z = s->localPosition.z - s->origin.z;
            s->origin = s->localPosition;
        }
    }

    // side = normalize(motion x (0, 0, 1)) (another perpendicular when that is degenerate),
    // up = normalize(motion x side); both zero when the source barely moved.
    void BuildBasis() {
        const Vector3& m = s->motion;
        float distance = LengthXYZ(m);
        s->distance = distance;
        EI_NAN_AUDIT(distance);
        if (distance <= EI_STILL) {
            s->side = {};
            s->up = {};
            return;
        }
        float zero = EI_ZERO;
        s->side.x = m.y - zero;
        s->side.y = zero - m.x;
        s->side.z = 0.0f;
        float length = LengthXYZ(s->side);
        EI_NAN_AUDIT(length);
        if (length < EI_DEGENERATE) {
            float mx = m.x;
            s->side.x = m.y - m.z;
            s->side.y = -mx;
            s->side.z = mx;
            length = LengthZYX(s->side);
        }
        Normalize(s->side, length);
        const Vector3& v = s->side;
        float ux = m.y * v.z - m.z * v.y;
        float uy = m.z * v.x - v.z * m.x;
        float uz = m.x * v.y - m.y * v.x;
        s->up.x = ux;
        s->up.y = uy;
        s->up.z = uz;
        Normalize(s->up, LengthXYZ(s->up));
    }

    void RunElements() {
        for (int i = 0; i < std::ssize(s->elements); ++i)
            (s->*s->elementFn)(s->elements.data() + i);
    }

    // Updates every particle. A particle the previous tick rejected is unlinked and returned to
    // the pool first, but (as in the original) is still updated and counted in the bounds.
    void UpdateParticles() {
        ParticleManager* pool = s->Manager();
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
        ParticleManager* pool = s->Manager();
        Particle* p = pool->AcquireParticle();
        if (!p) return;
        if ((s->*s->emitFn)(p, index)) s->AddParticle(p);
        else pool->ReleaseParticle(p);
    }

    // max(emitCount, distance / emitSpacing) tries, each emitting with emitChance percent; a
    // source above 50% that emitted nothing gets one particle anyway.
    void Emit() {
        int tries = std::lround(s->distance / s->emitSpacing);
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
        float cx = (s->boundsMax.x + s->boundsMin.x) * EI_HALF;
        float cy = (s->boundsMax.y + s->boundsMin.y) * EI_HALF;
        float cz = (s->boundsMax.z + s->boundsMin.z) * EI_HALF;
        s->clip.center.x = cx;
        s->clip.center.y = cy;
        s->clip.center.z = cz;
        s->clip.boxMin.x = s->boundsMin.x - cx;
        s->clip.boxMin.y = s->boundsMin.y - cy;
        s->clip.boxMin.z = s->boundsMin.z - cz;
        s->clip.boxMax.x = s->boundsMax.x - cx;
        s->clip.boxMax.y = s->boundsMax.y - cy;
        s->clip.boxMax.z = s->boundsMax.z - cz;
        s->clip.flags |= 2;
    }
};

} // namespace

// --- Particle pool --------------------------------------------------------------------------------

// The head of the free list, or a fresh 0x64-byte particle when it is empty.
Particle* ParticleManager::AcquireParticle() {
    if (freeParticles.empty()) return new Particle;
    Particle* p = freeParticles.front();
    freeParticles.pop_front();
    return p;
}

void ParticleManager::ReleaseParticle(Particle* p) { freeParticles.push_back(p); }

// A newly emitted particle: alive, its "prev" fields equal to the current ones, inside
// the bounds, appended to the list.
void ParticleSource::AddParticle(Particle* p) {
    p->dead = 0;
    SavePrevious(p);
    GrowBounds(boundsMin, boundsMax, p->pos);
    particles.push_back(p);
}

// Returns 1 while the source is still needed: a free source while it keeps
// alive (ParticleSourceFlag::KeepAlive) or has particles, an attached one while it emits or has particles.
bool ParticleSource::RunTick() {
    if (hiddenTicks > 0x38 && (flags & ParticleSourceFlag::IdlesOffScreen)) return true;
    const WorldClient* world = &g_clientWorld;
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
