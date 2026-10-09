// The particle manager's own members (00497FC0-00498630): create / destroy, making sources it holds,
// ticking every source and letting go of the held ones. See effects.h (CParticleManager) and
// docs/notes/particles.md.
#include "effects.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"

namespace {

// The source types that start without emitting while unknown_E4 is set.
constexpr int kStoppedTypeA = 0x2001;
constexpr int kStoppedTypeB = 0x2003;
constexpr int kStoppedTypeC = 0x2004;

CParticleSource* AsSource(CEngineObject* object) { return reinterpret_cast<CParticleSource*>(object); }
CEngineObject* AsObject(CParticleSource* source) { return reinterpret_cast<CEngineObject*>(source); }

// The original's inlined iterator destructor: the manager's count drops without a release.
void EndWalk(ObjectListIterator& it) {
    if (it.manager) --it.manager->refCount_;
}

} // namespace

// 00497FC0 (slot 2).
int CParticleManager::Create() {
    unknown_E4 = 0;
    return CBaseManager::Create() != 0 ? 1 : 0;
}

// 00497FE0 (slot 3). The held sources are released walking the table (a null value ends the walk,
// as in the original's for_each); the table is cleared (count and highest id 0); every pooled
// particle is deleted (RemoveHead, which frees the list's blocks with the last node).
void CParticleManager::Destroy() {
    for (HeldMap::Node* node = held_.First(); node && node->value; node = held_.After(node))
        AsObject(node->value)->Release();
    held_.Clear();
    maxHeldId_ = 0;
    while (freeParticles.count != 0) gamecrt::operator_delete(freeParticles.RemoveHead());
    CBaseManager::Destroy();
    unknown_E4 = 0;
}

// 00498400.
CParticleSource* CParticleManager::CreateSource(int type) {
    CEngineObject* object = CreateObject(type, -1);
    ++object->refCount_;
    int id = object->id_;
    if (id > maxHeldId_) maxHeldId_ = id;
    CParticleSource* source = AsSource(object);
    held_[id] = source;
    if (unknown_E4 != 0) {
        int kind = object->type_;
        if (kind == kStoppedTypeA || kind == kStoppedTypeC || kind == kStoppedTypeB) source->StopEmitting();
    }
    return source;
}

// 00498480 and 00498630 share the walk: the ids of the held sources `drop` picks are listed while
// the sources are released, then they leave the table. The id list is a local stlp::list (its
// sentinel and nodes come from the node pool in the original's order).
namespace {

template <typename Pick>
void ReleaseHeld(CParticleManager* manager, Pick drop) {
    ObjectListIterator it{nullptr, nullptr};
    CEngineObject* object = it.First(manager);
    stlp::list<int> ids;
    ids.Construct();
    for (; object; object = it.Next()) {
        if (!drop(object)) continue;
        int id = object->id_;
        if (!manager->held_.Find(id)) continue;
        ids.PushBack(id);
        object->Release();
    }
    for (stlp::list<int>::Node* n = ids.First(); n != ids.End(); n = n->next) {
        if (CParticleManager::HeldMap::Node* node = manager->held_.Find(n->data)) manager->held_.Erase(node);
    }
    ids.Destroy();
    EndWalk(it);
}

} // namespace

// 00498480. With `paused` set only sources with flag 8 tick; a source whose tick returned 0 is
// released when held.
int CParticleManager::TickSources(char paused) {
    ReleaseHeld(this, [paused](CEngineObject* object) {
        if (paused != 0 && !(object->flags_ & 8)) return false;
        return object->RunTick() == 0;
    });
    return 0;
}

// 00498630.
void CParticleManager::ReleaseUnheld() {
    ReleaseHeld(this, [](CEngineObject* object) { return object->refCount_ == 1; });
}

namespace {
// The virtual slots' entry points (rewrite-guide rule 4).
int __fastcall ParticleManagerCreateHook(CParticleManager* self) { return self->CParticleManager::Create(); }
void __fastcall ParticleManagerDestroyHook(CParticleManager* self) { self->CParticleManager::Destroy(); }
} // namespace
EI_REPLACE(0x00497FC0, ParticleManagerCreateHook);
EI_REPLACE(0x00497FE0, ParticleManagerDestroyHook);
EI_REPLACE_METHOD(0x00498400, CParticleManager::CreateSource);
EI_REPLACE_METHOD(0x00498480, CParticleManager::TickSources);
EI_REPLACE_METHOD(0x00498630, CParticleManager::ReleaseUnheld);
