// The particle manager's own members: create / destroy, making sources it holds, ticking every source and
// letting go of the held ones. See effects.h (CParticleManager) and docs/notes/particles.md.
#include "effects.h"

#include <list>

namespace {

// The source types that start without emitting while quietStart is set.
constexpr int kStoppedTypeA = 0x2001;
constexpr int kStoppedTypeB = 0x2003;
constexpr int kStoppedTypeC = 0x2004;

// The iterator destructor: the manager's count drops without a release.
void EndWalk(ObjectListIterator& it) {
    if (it.manager) --it.manager->refCount_;
}

template <typename Pick>
void ReleaseHeld(CParticleManager* manager, Pick drop) {
    ObjectListIterator it{nullptr, {}};
    CEngineObject* object = it.First(manager);
    std::list<int> ids;
    for (; object; object = it.Next()) {
        if (!drop(object)) continue;
        int id = object->id_;
        if (!manager->held_.contains(id)) continue;
        ids.push_back(id);
        object->Release();
    }
    for (auto& n : ids) {
        if (auto node = manager->held_.find(n); node != manager->held_.end()) manager->held_.erase(node);
    }
    EndWalk(it);
}

} // namespace

int CParticleManager::Create() {
    quietStart = 0;
    return CBaseManager::Create() != 0 ? 1 : 0;
}

// The held sources are released walking the table (a null value ends the walk, as in the original's
// for_each); the table is cleared (count and highest id 0); every pooled particle is deleted (taken off the list one by one,
// which frees the list's blocks with the last node).
void CParticleManager::Destroy() {
    for (auto node = held_.begin(); node != held_.end() && node->second; ++node)
        node->second->Release();
    held_.clear();
    maxHeldId_ = 0;
    while (!freeParticles.empty()) {
        Particle* p = freeParticles.front();
        freeParticles.pop_front();
        delete p;
    }
    CBaseManager::Destroy();
    quietStart = 0;
}

CParticleSource* CParticleManager::CreateSource(int type) {
    CParticleSource* source = CreateObjectAs<CParticleSource>(type, -1);
    source->AddRef();
    int id = source->id_;
    if (id > maxHeldId_) maxHeldId_ = id;
    held_[id] = source;
    if (quietStart != 0) {
        int kind = source->type_;
        if (kind == kStoppedTypeA || kind == kStoppedTypeC || kind == kStoppedTypeB) source->StopEmitting();
    }
    return source;
}

// The two callers share the walk: the ids of the held sources `drop` picks are listed while the sources are
// released, then they leave the table.

int CParticleManager::TickSources(char paused) {
    ReleaseHeld(this, [paused](CEngineObject* object) {
        if (paused != 0 && !(object->flags_ & 8)) return false;
        return object->RunTick() == 0;
    });
    return 0;
}

void CParticleManager::ReleaseUnheld() {
    ReleaseHeld(this, [](CEngineObject* object) { return object->refCount_ == 1; });
}
