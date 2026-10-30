// The particle manager's own members: create / destroy, making sources it holds, ticking every source and
// letting go of the held ones. See effects.h (ParticleManager) and docs/notes/particles.md.
#include "effects.h"

#include "camera.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "model.h"
#include "random.h"
#include "texture.h"
#include "unit.h"
#include "world.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/intro_sort.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// The iterator destructor: the manager's count drops without a release.
void EndWalk(ObjectListIterator& it) {
    if (it.manager) --it.manager->refCount;
}

template <typename Pick>
void ReleaseHeld(ParticleManager* manager, Pick drop) {
    ObjectListIterator it{nullptr, {}};
    EngineObject* object = it.First(manager);
    std::vector<int> ids;
    for (; object; object = it.Next()) {
        if (!drop(object)) continue;
        int id = object->id;
        if (!manager->held.contains(id)) continue;
        ids.push_back(id);
        object->Release();
    }
    for (auto& n : ids) {
        if (auto node = manager->held.find(n); node != manager->held.end()) manager->held.erase(node);
    }
    EndWalk(it);
}

} // namespace

void ParticleManager::Create() {
    quietStart = 0;
    BaseManager::Create();
}

// The held sources are released walking the table (a null value ends the walk, as in the original's
// for_each); the table is cleared (count and highest id 0); every pooled particle is deleted (taken off the list one by one,
// which frees the list's blocks with the last node).
void ParticleManager::Destroy() {
    for (auto node = held.begin(); node != held.end() && node->second; ++node)
        node->second->Release();
    held.clear();
    maxHeldId = 0;
    while (!freeParticles.empty()) {
        Particle* p = freeParticles.front();
        freeParticles.pop_front();
        delete p;
    }
    BaseManager::Destroy();
    quietStart = 0;
}

ParticleSource* ParticleManager::CreateSource(int newType) {
    ParticleSource* source = CreateObjectAs<ParticleSource>(newType, -1);
    source->AddRef();
    int sourceId = source->id;
    if (sourceId > maxHeldId) maxHeldId = sourceId;
    held[sourceId] = source;
    if (quietStart != 0) {
        // CampFire, Fire and Smoke start without emitting.
        const ParticleType kind = ParticleTypeOf(source->type);
        if (kind == ParticleType::CampFire || kind == ParticleType::Smoke || kind == ParticleType::Fire) source->StopEmitting();
    }
    return source;
}

// The two callers share the walk: the ids of the held sources `drop` picks are listed while the sources are
// released, then they leave the table.

int ParticleManager::TickSources(char paused) {
    ReleaseHeld(this, [paused](EngineObject* object) {
        if (paused != 0 && !(object->flags & ParticleSourceFlag::TicksWhilePaused)) return false;
        return !object->RunTick();
    });
    return 0;
}

void ParticleManager::ReleaseUnheld() {
    ReleaseHeld(this, [](EngineObject* object) { return object->refCount == 1; });
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kParticleManagerClassName = "CParticleManager";
} // namespace

const char* ParticleManager::ClassName() { return kParticleManagerClassName; }
ClassId ParticleManager::GetClassId() { return ClassId::ParticleManager; }

// The manager's own part of its constructor (the client world's manager set-up calls it after the base
// manager's): quietStart cleared.
void ParticleManagerClearQuietStart(ParticleManager* manager) {
    manager->quietStart = 0;
}

// --- Particle attach ---------------------------------------------------------------------------------

// Particle sources that follow an object (A-S4): Attach, the attachment point
// and its part lookup, the attachment offset and StopAndClear
//. Names are provisional.
// The object a source follows is a Object3D (attachedTo, +0x94). With ParticleSourceFlag::OnPart a source on a
// unit sits on the part attachPoint names; otherwise on top of the object's bounds.

namespace {

// The part each attachPoint 1..12 asks for (0 is the unit's effect point). 2 is the head raised
// by 1; 7 asks for "bd" twice (the fallback part).
const char* const kPartNames[13] = {nullptr, "hd", "hd", "lh2", "rh2", "ll2", "rl2", "bd",
                                    "body00", "body01", "body02", "body03", "body04"};

} // namespace

void ParticleSource::ReleaseAllParticles() {
    for (Particle* p : particles) Manager()->ReleaseParticle(p);
    particles.clear();
}

Vector3 ParticleSource::PartPoint(Object3D* object, const char* prefix) {
    FigureMesh* part = object->FindPartByPrefix(prefix);
    if (!part) part = object->FindPartByPrefix("bd");
    if (part) {
        Vector3 point;
        object->PartWorldPosition(&point.x, part);
        return point;
    }
    return {object->localPosition.x, object->localPosition.y, object->localPosition.z + object->bounds.boxMax.z};
}

Vector3 ParticleSource::AttachmentPoint(Object3D* object) {
    if (!object) return {0.0f, 0.0f, 0.0f};
    if (!(flags & ParticleSourceFlag::OnPart)) return object->localPosition;
    const auto point = static_cast<std::uint32_t>(attachPoint);
    if (object->GetClassId() == ClassId::Unit && point <= 12) {
        if (point == 0) return static_cast<Unit*>(object)->EffectPoint();
        Vector3 at = PartPoint(object, kPartNames[point]);
        if (point == 2) at.z = at.z + 1.0f;
        return at;
    }
    float rise = lib::Has(object->bounds.flags, BoundsFlag::Sphere) ? object->bounds.radius * 1.6f
                                                                  : object->bounds.boxMax.z - object->bounds.boxMin.z;
    return {object->localPosition.x, object->localPosition.y, rise + object->localPosition.z};
}

ParticleSource::Attachment ParticleSource::AttachmentOffset() {
    Object3D* object = attachedTo;
    if (!object) return {{0.0f, 0.0f, 0.0f}, 0.1f};
    object->ComputeBounds();
    float radius;
    if (lib::Has(object->bounds.flags, BoundsFlag::Sphere)) {
        radius = object->bounds.radius;
    } else {
        float dz = object->bounds.boxMax.z - object->bounds.boxMin.z;
        float dx = object->bounds.boxMax.x - object->bounds.boxMin.x;
        float dy = object->bounds.boxMax.y - object->bounds.boxMin.y;
        radius = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)) * 0.5f;
    }
    return {{0.0f, 0.0f, object->bounds.center.z}, radius};
}

void ParticleSource::Attach(Object3D* object) {
    Object3D* old = attachedTo;
    if (old == object) {
        if (!old) origin = localPosition;
        return;
    }
    attachedTo = nullptr;
    if (old) {
        if (!object) {
            // Let go where the source is: the attachment point becomes part of the position.
            Vector3 point = AttachmentPoint(old);
            float x = point.x + localPosition.x;
            float y = localPosition.y + point.y;
            float z = localPosition.z + point.z;
            localPosition.x = x;
            localPosition.y = y;
            localPosition.z = z;
        }
        // Drops the source and calls Attach(null) again (nothing to do then but the origin).
        old->RemoveEffectSource(this);
    }
    attachedTo = object;
    if (!object) {
        origin = localPosition;
        return;
    }
    object->ComputeBounds();
    // Adds the source and calls Attach(object) again (already attached: nothing).
    attachedTo->AddEffectSource(this);
    origin = AttachmentPoint(attachedTo);
}

void ParticleSource::StopAndClear() {
    ReleaseAllParticles();
    flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    Attach(nullptr);
}

// --- Particle draw -----------------------------------------------------------------------------------

// The particle manager's draw pass and its draw order: every source's depth along the camera's view axis goes
// into an array, which is sorted far-to-near with the game's sort (lib/intro_sort.h: sources at the same depth
// keep its order) and drawn in that order. Names are provisional.

namespace {

// The particles' draw order, reused by every frame.
std::vector<ParticleDepth> g_drawOrder;
// The orientation of the camera the frame shows (camera::FrameView).
const Quaternion& CameraRotation() { return camera::FrameView().rotation; }

// A source attached to a unit (class 0x50) is drawn only while the unit has MapObjectFlag::InParty.
bool AttachedToHiddenUnit(const ParticleSource* s) {
    EngineObject* object = s->attachedTo;
    if (!object) return false;
    return object->GetClassId() == ClassId::Unit && !(object->flags & MapObjectFlag::InParty);
}

} // namespace

// The attachment point plus the local position, or the local position alone.
Vector3 ParticleSource::WorldPosition() {
    if (attachedTo) {
        Vector3 a = AttachmentPoint(attachedTo);
        float z = a.z + localPosition.z;
        float y = a.y + localPosition.y;
        float x = a.x + localPosition.x;
        return {x, y, z};
    }
    return localPosition;
}

// Rebuilds the draw order (depth = position . camera view axis, iterating the manager's
// sources until the first empty entry), sorts it far to near and draws each source that is not
// hidden (ParticleSourceFlag::NotDrawn) and not on a hidden unit, switching to additive blending for sources with
// +0x150 == 1. Returns 0 as soon as a source fails to draw. The manager is held (+4) meanwhile.
int ParticleManager::DrawAll(SceneRenderer* renderer) {
    ++refCount;
    std::vector<ParticleDepth>& order = g_drawOrder;
    lib::FreeStorage(order);
    Vector3 axis = CameraRotation().AxisZ();

    // The manager's objects are its sources.
    auto sourceOf = [](EngineObject* object) { return static_cast<ParticleSource*>(object); };
    auto node = objectList.begin();
    ParticleSource* source = node != objectList.end() ? sourceOf(*node) : nullptr;
    while (source) {
        ++node;
        Vector3 p = source->WorldPosition();
        ParticleDepth entry;
        entry.source = source;
        entry.depth = DotUnrounded<Sum3::ZYX>(axis, p);
        lib::SetAtGrow(order, lib::Count(order), entry);
        source = node != objectList.end() ? sourceOf(*node) : nullptr;
    }

    // Far (greater depth) first; false for NaN.
    lib::IntroSort(order.data(), order.data() + order.size(),
                   [](const ParticleDepth& a, const ParticleDepth& b) { return a.depth > b.depth; });

    bool additive = false;
    for (int i = 0; i < std::ssize(order); ++i) {
        ParticleSource* s = order[i].source;
        if (s->flags & ParticleSourceFlag::NotDrawn) continue;
        if (AttachedToHiddenUnit(s)) continue;
        int blend;
        blend = std::bit_cast<std::int32_t>(s->additiveBlend);
        if (blend == 1 && !additive) {
            renderer->SetBlend(render::Blend::SourceAlpha, render::Blend::One);
            additive = true;
        }
        blend = std::bit_cast<std::int32_t>(s->additiveBlend);
        if (blend == 0 && additive) {
            renderer->SetBlend(render::Blend::SourceAlpha, render::Blend::InvSourceAlpha);
            additive = false;
        }
        if (!s->Draw(renderer)) {
            --refCount;
            return 0;
        }
    }
    --refCount;
    return 1;
}

// --- Particle render ---------------------------------------------------------------------------------

// Particle drawing: each visible source fills the shared vertex array (position and size interpolated between
// the last two ticks) and sprite array (interpolated colour, texture frame), binds its texture and hands both
// arrays to the renderer. Names are provisional.

namespace {

#define EI_ONE 1.0f

// The draw's work arrays, reused by every source.
std::vector<ParticleVertex> g_vertices;
std::vector<ParticleSprite> g_sprites;

constexpr int kReserve = 0x1000;

// How far the frame is between the last tick and the next, clamped to [0, 1] (NaN gives 1).
float Interpolation() {
    float fraction = g_clientWorld.tickFraction;
    float clamped = MinKeepA(1.0f, fraction);
    return MaxKeepB(clamped, 0.0f);
}

// One colour channel (at `shift`) between the previous and the current colour, truncated.
int MixChannel(std::uint32_t current, std::uint32_t previous, int shift, float t, float u) {
    return static_cast<int>(((current >> shift) & 0xFF) * t + ((previous >> shift) & 0xFF) * u);
}

} // namespace

int SceneRenderer::BindLoadedTexture(int stage, Texture* texture) {
    EnsureTextureLoaded(texture);
    return SetTexture(stage, texture);
}

// Off screen (the renderer's clip test on the bounds says 2) only counts the hidden
// ticks. Otherwise the arrays are reserved for 0x1000 particles, sized to this source's, filled,
// and drawn; 0 when the texture cannot be bound.
int ParticleSource::Draw(SceneRenderer* renderer) {
    if (particles.empty()) return 1;
    if (renderer->pipeline->ClipTest(&clip) == 2) {
        ++hiddenTicks;
        return 1;
    }
    hiddenTicks = 0;
    float t = Interpolation();
    g_sprites.resize(kReserve);
    g_vertices.resize(kReserve);
    g_sprites.resize(particles.size());
    g_vertices.resize(particles.size());
    if (!renderer->BindLoadedTexture(0, texture)) return 0;

    ParticleSprite* sprite = g_sprites.data();
    auto cursor = particles.begin();
    if (cursor != particles.end()) {
        float u = EI_ONE - t;
        ParticleVertex* vertex = g_vertices.data();
        do {
            const Particle* p = *cursor++;
            vertex->x = u * p->prevPos.x + t * p->pos.x;
            vertex->y = u * p->prevPos.y + t * p->pos.y;
            vertex->z = u * p->prevPos.z + t * p->pos.z;
            vertex->size = u * p->prevSize + t * p->size;
            std::uint32_t now = p->color;
            std::uint32_t before = p->prevColor;
            std::uint32_t argb = static_cast<std::uint32_t>(MixChannel(now, before, 16, t, u));
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 24, t, u)) << 8;
            argb <<= 8;
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 8, t, u));
            argb <<= 8;
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 0, t, u));
            sprite->color = argb;
            // A frame past the grid (a link spark is emitted on a frame up to frameCount, one past the last): the
            // original read the heap after the frames there, here the first frame.
            sprite->frame = frames.data() + (p->prevFrame < std::ssize(frames) ? p->prevFrame : 0);
            ++sprite;
            ++vertex;
        } while (cursor != particles.end());
    }
    int blend;
    blend = std::bit_cast<std::int32_t>(additiveBlend);
    return renderer->DrawSprites(blend != 0, &g_vertices, &g_sprites);
}

// --- Particle tick -----------------------------------------------------------------------------------

// ParticleSource's per-frame tick and the particle pool. Each tick: copy the world's wind, follow the attachment
// (or the source's own position) and derive the motion since the last tick with a basis (side, up) around it,
// run the element callback on every element, the update callback on every particle (rebuilding the bounding
// box), then emit new particles.

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
    low.x = MinKeepA(p.x, low.x);
    low.y = MinKeepA(p.y, low.y);
    low.z = MinKeepA(p.z, low.z);
    high.x = MaxKeepB(high.x, p.x);
    high.y = MaxKeepB(high.y, p.y);
    high.z = MaxKeepB(high.z, p.z);
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
            Vector3 a = s->AttachmentPoint(s->attachedTo);
            float wx = a.x + s->localPosition.x;
            float wy = a.y + s->localPosition.y;
            float wz = a.z + s->localPosition.z;
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

    // Updates every particle. A particle the previous tick rejected is erased and returned to the
    // pool first, but (as in the original) is still updated and counted in the bounds. The erase comes
    // before the update: UpdateGlow reads the particle count.
    void UpdateParticles() {
        ParticleManager* pool = s->Manager();
        auto node = s->particles.begin();
        while (node != s->particles.end()) {
            Particle* p = *node;
            if (p->dead) {
                node = s->particles.erase(node);
                pool->ReleaseParticle(p);
            } else {
                ++node;
            }
            SavePrevious(p);
            if (!(s->*s->updateFn)(p)) {
                p->dead = 1;
                RestorePrevious(p);
            }
            GrowBounds(s->boundsMin, s->boundsMax, p->pos);
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
