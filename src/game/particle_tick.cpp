// CParticleSource's per-frame tick (vtable slot 6) and the particle pool (00496A20-00498870).
// Names are provisional.
//
// Each tick: copy the world's wind, follow the attachment (or the source's own position) and
// derive the motion since the last tick with a basis (side, up) around it, run the element
// callback on every element, the update callback on every particle (rebuilding the bounding box),
// then emit new particles. The callbacks are __thiscall on the source (effects.h).
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "effects.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstring>

namespace {

inline double D(float f) { return f; }

// Constants the original reads from memory at run time.
float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }
double GameDouble(std::uintptr_t address) { return ei::GameData<const volatile double>(address); }
#define EI_ZERO GameFloat(0x0073B858)          // 0.0 (the z of the up axis the side is crossed with)
#define EI_ONE GameFloat(0x0073B84C)           // 1.0
#define EI_HALF GameFloat(0x0073B86C)          // 0.5
#define EI_STILL GameDouble(0x0073BA88)        // 0.001: below this the source is not moving
#define EI_DEGENERATE GameDouble(0x0073CE00)   // 1e-10

constexpr std::uint32_t kFarBits = 0x47C35000;     // 100000.0f
constexpr std::uint32_t kNearBits = 0xC7C35000;    // -100000.0f

IsaacRandom& GlobalRandom() { return ei::GameData<IsaacRandom>(0x007A9D40); }

// The world's wind (007AF5DC: direction, then strength), copied as four dwords.
const void* WorldWind() { return ei::GamePointer<const void>(0x007AF5DC); }

// x87 FCOMP; FNSTSW; C0: "a < b or unordered".
bool LessOrUnordered(double a, double b) { return !(a >= b); }

void SetBits(float& f, std::uint32_t bits) { std::memcpy(&f, &bits, sizeof(f)); }

// sqrt((x*x + y*y) + z*z), all on the FPU.
double LengthDouble(const Vector3& v) { return x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(v)); }

// sqrt((y*y + z*z) + x*x): the order the original uses after rebuilding a degenerate side.
double LengthYZX(const Vector3& v) { return x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(v)); }

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
    low.x = LessOrUnordered(p.x, low.x) ? p.x : low.x;
    low.y = LessOrUnordered(p.y, low.y) ? p.y : low.y;
    low.z = LessOrUnordered(p.z, low.z) ? p.z : low.z;
    high.x = LessOrUnordered(high.x, p.x) ? p.x : high.x;
    high.y = LessOrUnordered(high.y, p.y) ? p.y : high.y;
    high.z = LessOrUnordered(high.z, p.z) ? p.z : high.z;
}

// Saves the fields the update callback may change, so a rejected update can be undone (dword
// copies, as in the original).
void SavePrevious(Particle* p) {
    std::memcpy(&p->prevPos, &p->pos, sizeof(Vector3));
    p->prevColor = p->color;
    std::memcpy(&p->prevSize, &p->size, sizeof(float));
    p->prevFrame = p->frame;
}

void RestorePrevious(Particle* p) {
    std::memcpy(&p->pos, &p->prevPos, sizeof(Vector3));
    p->frame = p->prevFrame;
    std::memcpy(&p->size, &p->prevSize, sizeof(float));
    p->color = p->prevColor;
}

} // namespace

// --- Particle pool --------------------------------------------------------------------------------

// 00498870. The head of the free list, or a fresh 0x64-byte particle when it is empty.
Particle* CParticleManager::AcquireParticle() {
    if (freeParticles.count == 0) return static_cast<Particle*>(gamecrt::operator_new(sizeof(Particle)));
    mfc::CTypedPtrList<Particle>::Node* node = freeParticles.head;
    Particle* p = node->data;
    freeParticles.head = node->next;
    if (node->next) node->next->prev = nullptr;
    else freeParticles.tail = nullptr;
    freeParticles.FreeNode(node);
    return p;
}
EI_REPLACE_METHOD(0x00498870, CParticleManager::AcquireParticle);

// 004987C0. Appends the particle to the free list.
void CParticleManager::ReleaseParticle(Particle* p) { freeParticles.AddTail(p); }
EI_REPLACE_METHOD(0x004987C0, CParticleManager::ReleaseParticle);

// --- Source -------------------------------------------------------------------------------------

// 00496A20. A newly emitted particle: alive, its "prev" fields equal to the current ones, inside
// the bounds, appended to the list.
void CParticleSource::AddParticle(Particle* p) {
    p->dead = 0;
    SavePrevious(p);
    GrowBounds(boundsMin, boundsMax, p->pos);
    particles.AddTail(p);
}
EI_REPLACE_METHOD(0x00496A20, CParticleSource::AddParticle);

namespace {

// The tick's steps, as helpers on the source.
struct TickSteps {
    CParticleSource* s;

    // Motion since the last tick, from the attachment point plus the local position (or the
    // position alone); the emission point moves there.
    void Follow() {
        if (s->attachedTo) {
            Vector3 point;
            const Vector3* a = s->AttachmentPoint(&point, s->attachedTo);
            float wx = static_cast<float>(D(a->x) + D(s->position.x));
            float wy = static_cast<float>(D(a->y) + D(s->position.y));
            float wz = static_cast<float>(D(a->z) + D(s->position.z));
            s->motion.x = static_cast<float>(D(wx) - D(s->origin.x));
            s->motion.y = static_cast<float>(D(wy) - D(s->origin.y));
            s->motion.z = static_cast<float>(D(wz) - D(s->origin.z));
            s->origin.x = wx;
            s->origin.y = wy;
            s->origin.z = wz;
        } else {
            s->motion.x = static_cast<float>(D(s->position.x) - D(s->origin.x));
            s->motion.y = static_cast<float>(D(s->position.y) - D(s->origin.y));
            s->motion.z = static_cast<float>(D(s->position.z) - D(s->origin.z));
            std::memcpy(&s->origin, &s->position, sizeof(Vector3));
        }
    }

    // side = normalize(motion x (0, 0, 1)) (another perpendicular when that is degenerate),
    // up = normalize(motion x side); both zero when the source barely moved.
    void BuildBasis() {
        const Vector3& m = s->motion;
        double distance = LengthDouble(m);
        s->distance = static_cast<float>(distance);
        if (!(distance > EI_STILL)) {
            std::memset(&s->side, 0, sizeof(Vector3));
            std::memset(&s->up, 0, sizeof(Vector3));
            return;
        }
        double zero = EI_ZERO;
        s->side.x = static_cast<float>(D(m.y) - zero);
        s->side.y = static_cast<float>(zero - D(m.x));
        SetBits(s->side.z, 0);
        double length = LengthDouble(s->side);
        if (LessOrUnordered(length, EI_DEGENERATE)) {
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
        for (int i = 0; i < s->elementCount; ++i)
            (s->*s->elementFn)(s->elements + i);
    }

    // Updates every particle. A particle the previous tick rejected is unlinked and returned to
    // the pool first, but (as in the original) is still updated and counted in the bounds.
    void UpdateParticles() {
        CParticleManager* pool = static_cast<CParticleManager*>(s->owner_);
        mfc::CTypedPtrList<Particle>::Node* node = s->particles.head;
        while (node) {
            mfc::CTypedPtrList<Particle>::Node* next = node->next;
            Particle* p = node->data;
            if (p->dead) {
                s->particles.RemoveAt(node);
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
        CParticleManager* pool = static_cast<CParticleManager*>(s->owner_);
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
        if (!(s->flags_ & 1)) return;
        if ((s->flags_ & 2) && !s->attachedTo) return;
        int emitted = 0;
        if (tries > 0) {
            for (int i = 0; i < tries; ++i) {
                if (s->particles.count >= s->maxParticles) break;
                std::uint32_t roll = GlobalRandom().Next();
                if (static_cast<int>(roll % 100) < s->emitChance) EmitOne(emitted++);
            }
            if (emitted != 0) return;
        }
        if (s->emitChance > 50 && tries > 0 && s->particles.count < s->maxParticles) EmitOne(0);
    }

    // Centre and half extents of the bounds.
    void FinishBounds() {
        float cx = static_cast<float>((D(s->boundsMax.x) + D(s->boundsMin.x)) * D(EI_HALF));
        float cy = static_cast<float>((D(s->boundsMax.y) + D(s->boundsMin.y)) * D(EI_HALF));
        float cz = static_cast<float>((D(s->boundsMax.z) + D(s->boundsMin.z)) * D(EI_HALF));
        s->boundsCenter.x = cx;
        s->boundsCenter.y = cy;
        s->boundsCenter.z = cz;
        s->boundsLow.x = static_cast<float>(D(s->boundsMin.x) - D(cx));
        s->boundsLow.y = static_cast<float>(D(s->boundsMin.y) - D(cy));
        s->boundsLow.z = static_cast<float>(D(s->boundsMin.z) - D(cz));
        s->boundsHigh.x = static_cast<float>(D(s->boundsMax.x) - D(cx));
        s->boundsHigh.y = static_cast<float>(D(s->boundsMax.y) - D(cy));
        s->boundsHigh.z = static_cast<float>(D(s->boundsMax.z) - D(cz));
        s->boundsFlags |= 2;
    }
};

} // namespace

// 00496EB0 (slot 6). Returns 1 while the source is still needed: a free source while it keeps
// alive (flag 4) or has particles, an attached one while it emits or has particles.
int CParticleSource::Tick() {
    if (hiddenTicks > 0x38 && (flags_ & 0x20)) return 1;
    std::memcpy(&windDirection, WorldWind(), sizeof(Vector3) + sizeof(float));
    SetBits(boundsMin.x, kFarBits);
    SetBits(boundsMin.y, kFarBits);
    SetBits(boundsMin.z, kFarBits);
    SetBits(boundsMax.x, kNearBits);
    SetBits(boundsMax.y, kNearBits);
    SetBits(boundsMax.z, kNearBits);
    if (attachedTo && (static_cast<const std::uint8_t*>(attachedTo)[8] & 1)) Attach(nullptr);

    TickSteps steps{this};
    steps.Follow();
    steps.BuildBasis();
    steps.RunElements();
    steps.UpdateParticles();
    steps.Emit();
    steps.FinishBounds();

    if (!attachedTo) {
        if (flags_ & 4) return 1;
        return particles.count != 0;
    }
    if (flags_ & 1) return 1;
    return particles.count != 0;
}
EI_REPLACE_METHOD(0x00496EB0, CParticleSource::Tick);
