// The particle manager's own members: create / destroy, making sources it holds, ticking every source and
// letting go of the held ones. See effects.h (ParticleManager) and docs/notes/effects.md, "Particle sources".
#include "engine/particles/particle_source.h"
#include "engine/particles/particle_data.h"
#include "engine/particles/particle_host.h"

#include "engine/core/engine_globals.h"
#include "engine/core/engine_object.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/core/random.h"
#include "engine/models/figure_instance.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/intro_sort.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

template <typename Pick>
void ReleaseHeld(ParticleManager* manager, Pick drop) {
    ObjectListIterator it;
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
    const int shown = ShownParticleType(newType);
    if (shown == kNoParticleType) return nullptr;
    ParticleSource* source = CreateObjectAs<ParticleSource>(shown, -1);
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
        if (object->flags & ParticleSourceFlag::OwnClock) return false;
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

// Particle sources that follow an object: Attach and StopAndClear. The object a source follows is a figure
// (attachedTo); its list of sources and where a part of it is are the host's (particle_host.h).

namespace {

particlehost::Host* g_host = nullptr;

} // namespace

void particlehost::Use(Host& host) { g_host = &host; }

particlehost::Host& particlehost::Current() { return *g_host; }

Vector3 ParticleSource::CarrierPoint() const {
    if (!(flags & ParticleSourceFlag::OnPart)) return attachedTo->localPosition;
    return particlehost::Current().PartPoint(*attachedTo, attachPoint);
}

void ParticleSource::ReleaseAllParticles() {
    for (Particle* p : particles) Manager()->ReleaseParticle(p);
    particles.clear();
}

void ParticleSource::Attach(FigureInstance* object) {
    FigureInstance* old = attachedTo;
    if (old == object) {
        if (!old) origin = localPosition;
        return;
    }
    if (old) {
        if (!object) {
            // Let go where the source is: the attachment point becomes part of the position.
            localPosition = CarrierPoint() + localPosition;
        }
        attachedTo = nullptr;
        // Drops the source and calls Attach(null) again (nothing to do then but the origin).
        particlehost::Current().LetGo(*old, this);
    }
    attachedTo = object;
    if (!object) {
        origin = localPosition;
        return;
    }
    particlehost::Current().Follow(*object, this);
    origin = CarrierPoint();
}

void ParticleSource::StopAndClear() {
    ReleaseAllParticles();
    Stop();
}

void ParticleSource::ScaleSize(float factor) {
    spreadMin *= factor;
    spreadMax *= factor;
    size = factor * size;
    baseSize = factor * baseSize;
    height = factor * height;
    emitSpacing = factor * emitSpacing;
}

void ParticleSource::StopEmitting() { flags = (flags & ~ParticleSourceFlag::Emitting) | ParticleSourceFlag::KeepAlive; }

void ParticleSource::StartEmitting() { flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive; }

void ParticleSource::Stop() {
    flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    Attach(nullptr);
}

// --- Particle draw -----------------------------------------------------------------------------------

// The particle manager's draw pass and its draw order: every source's depth along the camera's view axis goes
// into an array, which is sorted far-to-near with the game's sort (lib/intro_sort.h: sources at the same depth
// keep its order) and drawn in that order.

namespace {

// The particles' draw order, reused by every frame.
std::vector<ParticleDepth> g_drawOrder;

} // namespace

// The attachment point plus the local position, or the local position alone.
Vector3 ParticleSource::WorldPosition() {
    if (attachedTo) {
        return CarrierPoint() + localPosition;
    }
    return localPosition;
}

// Rebuilds the draw order (depth = position . camera view axis, iterating the manager's
// sources until the first empty entry), sorts it far to near and draws each source that is not
// hidden (ParticleSourceFlag::NotDrawn) and not on a hidden unit, switching to additive blending for the sources
// with additiveBlend. Returns 0 as soon as a source fails to draw. The manager is referenced meanwhile.
int ParticleManager::DrawAll(SceneRenderer* renderer) {
    ++refCount;
    std::vector<ParticleDepth>& order = g_drawOrder;
    order.clear();
    Vector3 axis = g_sceneRenderer.view.rotation.AxisZ();

    // The manager's objects are its sources (it makes no other class).
    for (auto node = objectList.begin(); node != objectList.end() && *node; ++node) {
        auto* source = static_cast<ParticleSource*>(*node);
        order.push_back({Dot(axis, source->WorldPosition()), source});
    }

    // Far (greater depth) first; false for NaN.
    lib::IntroSort(order.data(), order.data() + order.size(),
                   [](const ParticleDepth& a, const ParticleDepth& b) { return a.depth > b.depth; });

    bool additive = false;
    for (int i = 0; i < std::ssize(order); ++i) {
        ParticleSource* s = order[i].source;
        if (s->flags & ParticleSourceFlag::NotDrawn) continue;
        if (s->attachedTo && particlehost::Current().Hidden(*s->attachedTo)) continue;
        if (s->additiveBlend && !additive) {
            renderer->SetBlend(render::Blend::SourceAlpha, render::Blend::One);
            additive = true;
        }
        if (!s->additiveBlend && additive) {
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
// arrays to the renderer.

namespace {

// The draw's work arrays, reused by every source.
std::vector<SpritePoint> g_vertices;
std::vector<ParticleSprite> g_sprites;

constexpr int kReserve = 0x1000;

// How far the frame is between the last tick and the next, clamped to [0, 1] (NaN gives 1).
float Interpolation() {
    float fraction = particlehost::Current().TickFraction();
    float clamped = MinKeepA(1.0f, fraction);
    return MaxKeepB(clamped, 0.0f);
}

// One colour channel (at `shift`) between the previous and the current colour, truncated.
int MixChannel(std::uint32_t current, std::uint32_t previous, int shift, float t, float u) {
    return static_cast<int>(((current >> shift) & 0xFF) * t + ((previous >> shift) & 0xFF) * u);
}

} // namespace

// Off screen (the renderer's clip test on the bounds says outside) only counts the hidden ticks. Otherwise the arrays are reserved for 4096 particles, sized to this source's, filled, and drawn.
int ParticleSource::Draw(SceneRenderer* renderer) {
    if (particles.empty()) return 1;
    if (renderer->pipeline->ClipTest(&clip) == TransformPipeline::ClipResult::Outside) {
        ++hiddenTicks;
        return 1;
    }
    hiddenTicks = 0;
    float t = Interpolation();
    g_sprites.resize(kReserve);
    g_vertices.resize(kReserve);
    g_sprites.resize(particles.size());
    g_vertices.resize(particles.size());
    renderer->BindLoadedTexture(0, texture);

    ParticleSprite* sprite = g_sprites.data();
    SpritePoint* vertex = g_vertices.data();
    float u = 1.0f - t;
    for (const Particle* p : particles) {
        vertex->position = u * p->prevPos + t * p->pos;
        vertex->size = u * p->prevSize + t * p->size;
        auto channel = [&](int shift) {
            return static_cast<std::uint32_t>(MixChannel(p->color, p->prevColor, shift, t, u));
        };
        sprite->color = channel(24) << 24 | channel(16) << 16 | channel(8) << 8 | channel(0);
        // A frame past the grid (a link spark is emitted on a frame up to frameCount, one past the last): the
        // original read the heap after the frames there, here the first frame.
        sprite->frame = frames.data() + (p->prevFrame < std::ssize(frames) ? p->prevFrame : 0);
        ++sprite;
        ++vertex;
    }
    return renderer->DrawSprites(additiveBlend, g_vertices, &g_sprites);
}

// --- Particle tick -----------------------------------------------------------------------------------

// ParticleSource's per-frame tick and the particle pool. Each tick: copy the world's wind, follow the attachment
// (or the source's own position) and derive the motion since the last tick with a basis (side, up) around it,
// step the effect's state (particle_data.h), update every particle (rebuilding the bounding box), then emit new
// particles.

namespace {

// v *= 1 / length.
void Normalize(Vector3& v, float length) {
    float scale = 1.0f / length;
    v *= scale;
}

// The bounding-box fold of the tick and AddParticle: the minimum takes the particle when it is
// smaller (or unordered), the maximum when the current maximum is smaller (or unordered).
void GrowBounds(Vector3& low, Vector3& high, const Vector3& p) {
    low = MinKeepA(p, low);
    high = MaxKeepB(high, p);
}

// Saves the fields the type's update may change, so a rejected update can be undone.
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

// The tick's steps.

// Motion since the last tick, from the attachment point plus the local position (or the
// position alone); the emission point moves there.
void Follow(ParticleSource& s) {
    if (s.attachedTo) {
        Vector3 at = s.CarrierPoint() + s.localPosition;
        s.motion = at - s.origin;
        s.origin = at;
    } else {
        s.motion = s.localPosition - s.origin;
        s.origin = s.localPosition;
    }
}

// side = normalize(motion x (0, 0, 1)) (another perpendicular when that is degenerate),
// up = normalize(motion x side); both zero when the source barely moved.
void BuildBasis(ParticleSource& s) {
    const Vector3& m = s.motion;
    float distance = Length(m);
    s.distance = distance;
    if (distance <= 0.001f) {
        s.side = {};
        s.up = {};
        return;
    }
    // (0 - m.x, not -m.x: the same value, but +0 for m.x = 0.)
    s.side = {m.y, 0.0f - m.x, 0.0f};
    float length = Length(s.side);
    if (length < 1e-10f) {
        s.side = {m.y - m.z, -m.x, m.x};
        length = Length(s.side);
    }
    Normalize(s.side, length);
    s.up = Cross(m, s.side);
    Normalize(s.up, Length(s.up));
}

// Updates every particle. A particle the previous tick rejected is erased and returned to the
// pool first, but (as in the original) is still updated and counted in the bounds.
void UpdateParticles(ParticleSource& s) {
    ParticleManager* pool = s.Manager();
    auto node = s.particles.begin();
    while (node != s.particles.end()) {
        Particle* p = *node;
        if (p->dead) {
            node = s.particles.erase(node);
            pool->ReleaseParticle(p);
        } else {
            ++node;
        }
        SavePrevious(p);
        if (!effectdata::UpdateData(s, *p)) {
            p->dead = 1;
            RestorePrevious(p);
        }
        GrowBounds(s.boundsMin, s.boundsMax, p->pos);
    }
}

// One emission attempt: a particle from the pool, kept when the effect's emission accepts it.
void EmitOne(ParticleSource& s, int index) {
    ParticleManager* pool = s.Manager();
    Particle* p = pool->AcquireParticle();
    if (!p) return;
    if (effectdata::EmitData(s, *p, index)) s.AddParticle(p);
    else pool->ReleaseParticle(p);
}

// max(emitCount, distance / emitSpacing) tries, each emitting with emitChance percent; a
// source above 50% that emitted nothing gets one particle anyway. The effect's queued particles come first, emitting or
// not.
void Emit(ParticleSource& s) {
    effectdata::EmitQueued(s);
    if (!(s.flags & ParticleSourceFlag::Emitting)) return;
    if ((s.flags & ParticleSourceFlag::AttachedOnly) && !s.attachedTo) return;
    int tries = !s.data->emitting ? 0 : std::lround(s.distance / s.emitSpacing);
    if (tries <= s.emitCount) tries = s.emitCount;
    int emitted = 0;
    if (tries > 0) {
        for (int i = 0; i < tries; ++i) {
            if (std::ssize(s.particles) >= s.maxParticles) break;
            std::uint32_t roll = g_particleRandom.Next();
            if (static_cast<int>(roll % 100) < s.emitChance) EmitOne(s, emitted++);
        }
        if (emitted != 0) return;
    }
    if (s.emitChance > 50 && tries > 0 && std::ssize(s.particles) < s.maxParticles) EmitOne(s, 0);
}

// Centre and half extents of the bounds.
void FinishBounds(ParticleSource& s) {
    s.clip.center = (s.boundsMax + s.boundsMin) * 0.5f;
    s.clip.boxMin = s.boundsMin - s.clip.center;
    s.clip.boxMax = s.boundsMax - s.clip.center;
    s.clip.flags |= lib::Bits(BoundsFlag::Box);
}

} // namespace

// --- Particle pool --------------------------------------------------------------------------------

// The head of the free list, or a fresh particle when it is empty.
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
    if (hiddenTicks >= kIdleAfterHidden && (flags & ParticleSourceFlag::IdlesOffScreen)) return true;
    const particlehost::Wind wind = particlehost::Current().CurrentWind();
    windDirection = wind.direction;
    windStrength = wind.strength;
    boundsMin = {kBoundsFar, kBoundsFar, kBoundsFar};
    boundsMax = {-kBoundsFar, -kBoundsFar, -kBoundsFar};
    if (attachedTo && particlehost::Current().Removed(*attachedTo)) Attach(nullptr);

    Follow(*this);
    BuildBasis(*this);
    effectdata::TickData(*this);
    UpdateParticles(*this);
    Emit(*this);
    FinishBounds(*this);

    if (!attachedTo) {
        if (flags & ParticleSourceFlag::KeepAlive) return true;
        return !particles.empty();
    }
    if (flags & ParticleSourceFlag::Emitting) return true;
    return !particles.empty();
}
