// The particle manager's own members: create / destroy, making sources it holds, ticking every source and
// letting go of the held ones. See effects.h (CParticleManager) and docs/notes/particles.md.
#include "effects.h"

#include <list>

namespace {

// The iterator destructor: the manager's count drops without a release.
void EndWalk(ObjectListIterator& it) {
    if (it.manager) --it.manager->refCount;
}

template <typename Pick>
void ReleaseHeld(CParticleManager* manager, Pick drop) {
    ObjectListIterator it{nullptr, {}};
    CEngineObject* object = it.First(manager);
    std::list<int> ids;
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

void CParticleManager::Create() {
    quietStart = 0;
    CBaseManager::Create();
}

// The held sources are released walking the table (a null value ends the walk, as in the original's
// for_each); the table is cleared (count and highest id 0); every pooled particle is deleted (taken off the list one by one,
// which frees the list's blocks with the last node).
void CParticleManager::Destroy() {
    for (auto node = held.begin(); node != held.end() && node->second; ++node)
        node->second->Release();
    held.clear();
    maxHeldId = 0;
    while (!freeParticles.empty()) {
        Particle* p = freeParticles.front();
        freeParticles.pop_front();
        delete p;
    }
    CBaseManager::Destroy();
    quietStart = 0;
}

CParticleSource* CParticleManager::CreateSource(int newType) {
    CParticleSource* source = CreateObjectAs<CParticleSource>(newType, -1);
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

int CParticleManager::TickSources(char paused) {
    ReleaseHeld(this, [paused](CEngineObject* object) {
        if (paused != 0 && !(object->flags & ParticleSourceFlag::TicksWhilePaused)) return false;
        return !object->RunTick();
    });
    return 0;
}

void CParticleManager::ReleaseUnheld() {
    ReleaseHeld(this, [](CEngineObject* object) { return object->refCount == 1; });
}
