// The particle manager's own members: create / destroy, making sources it holds, ticking every source and
// letting go of the held ones. See effects.h (ParticleManager) and docs/notes/particles.md.
#include "effects.h"


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
