// Checks for the particle manager's own members (particle_manager.cpp). All of them change shared
// state (sources ticked, created and released, tables resized, pooled particles freed), so only
// ours runs and the result is checked for consistency:
// - held_ is consistent: every node's key is its source's id and the walk sees Size() nodes;
// - Create / Destroy (world set-up and teardown: newgame, every scenario's exit): the flag and the
//   tables are cleared;
// - CreateSource (particles, effects, spells_cast): the new source is held under its id;
// - TickSources (every client frame): returns 0, the manager's count is back, held_ only shrinks;
// - ReleaseUnheld (the client's zone load: newgame, zone_*): exactly the held sources with one
//   reference left were dropped.
#include "verify.h"

#include "../../src/game/effects.h"

#include <cstring>

namespace {

using HeldNode = CParticleManager::HeldMap::Node;

// -1 when a node's key is not its source's id or the walk does not see Size() nodes, else the
// number of held sources with exactly one reference.
int HeldConsistency(const CParticleManager* m) {
    int walked = 0;
    int single = 0;
    for (const HeldNode* n = m->held_.First(); n; n = m->held_.After(n)) {
        const auto* source = reinterpret_cast<const CEngineObject*>(n->value);
        if (!source || source->id_ != n->key) return -1;
        if (source->refCount_ == 1) ++single;
        ++walked;
    }
    return walked == static_cast<int>(m->held_.Size()) ? single : -1;
}

void* s_unusedCreate;
int __fastcall CreateVerify(CParticleManager* self) {
    int r = self->CParticleManager::Create();
    ei::verify::Check("CParticleManager::Create", true, (r == 0 || r == 1) && self->quietStart == 0, "result %d", r);
    return r;
}

void* s_unusedDestroy;
void __fastcall DestroyVerify(CParticleManager* self) {
    int held = static_cast<int>(self->held_.Size());
    self->CParticleManager::Destroy();
    bool ok = self->held_.Size() == 0 && self->maxHeldId_ == 0 && self->quietStart == 0 &&
              self->freeParticles.count == 0 && !self->freeParticles.head && !self->freeParticles.blocks &&
              self->objectList_.count == 0;
    ei::verify::Check("CParticleManager::Destroy", true, ok, "held %d before", held);
}

void* s_unusedCreateSource;
CParticleSource* __fastcall CreateSourceVerify(CParticleManager* self, void*, int type) {
    int held = static_cast<int>(self->held_.Size());
    CParticleSource* source = self->CParticleManager::CreateSource(type);
    const auto* object = reinterpret_cast<const CEngineObject*>(source);
    const HeldNode* node = self->held_.Find(object->id_);
    bool ok = node && node->value == source && self->maxHeldId_ >= object->id_ && object->type_ == type &&
              self->held_.Size() >= static_cast<unsigned>(held) && HeldConsistency(self) >= 0;
    ei::verify::Check("CParticleManager::CreateSource", true, ok, "type %X id %d held %d -> %d", type, object->id_,
                      held, static_cast<int>(self->held_.Size()));
    return source;
}

void* s_unusedTick;
int __fastcall TickSourcesVerify(CParticleManager* self, void*, char paused) {
    int refs = self->refCount_;
    int held = static_cast<int>(self->held_.Size());
    int r = self->CParticleManager::TickSources(paused);
    int after = static_cast<int>(self->held_.Size());
    ei::verify::Check("CParticleManager::TickSources", true,
                      r == 0 && self->refCount_ == refs && after <= held && HeldConsistency(self) >= 0,
                      "paused %d held %d -> %d refs %d -> %d", paused, held, after, refs, self->refCount_);
    return r;
}

void* s_unusedReleaseUnheld;
void __fastcall ReleaseUnheldVerify(CParticleManager* self) {
    int refs = self->refCount_;
    int held = static_cast<int>(self->held_.Size());
    int single = HeldConsistency(self);
    self->CParticleManager::ReleaseUnheld();
    int after = static_cast<int>(self->held_.Size());
    ei::verify::Check("CParticleManager::ReleaseUnheld", true,
                      single >= 0 && after == held - single && self->refCount_ == refs && HeldConsistency(self) >= 0,
                      "held %d -> %d, %d with one reference", held, after, single);
}

// CParticleSource's slots 11 / 12 (MOV EAX,imm32: 5): both, compared by content / value. Slot 0
// (004982F0): ours only, the result.
const char*(__fastcall* s_className)(CParticleSource*);
const char* __fastcall ClassNameVerify(CParticleSource* self) {
    const char* expected = s_className(self);
    const char* actual = ParticleSourceClassName(self);
    ei::verify::Check("ParticleSourceClassName", true, std::strcmp(expected, actual) == 0, "%s", actual);
    return actual;
}
int(__fastcall* s_classId)(CParticleSource*);
int __fastcall ClassIdVerify(CParticleSource* self) {
    int expected = s_classId(self);
    int actual = ParticleSourceClassId(self);
    ei::verify::Check("ParticleSourceClassId", expected, actual, "id %d / %d", expected, actual);
    return actual;
}
void* s_unusedDeleting;
CParticleSource* __fastcall DeletingVerify(CParticleSource* self, void*, int flags) {
    CParticleSource* result = self->CParticleSource::DeletingDestruct(flags);
    ei::verify::Check("CParticleSource::DeletingDestruct", true, result == self, "flags %d", flags);
    return result;
}

} // namespace

EI_VERIFY(0x004982D0, ClassNameVerify, 5, s_className);
EI_VERIFY(0x004982E0, ClassIdVerify, 5, s_classId);
EI_VERIFY(0x004982F0, DeletingVerify, 0, s_unusedDeleting);
EI_VERIFY(0x00497FC0, CreateVerify, 0, s_unusedCreate);
EI_VERIFY(0x00497FE0, DestroyVerify, 0, s_unusedDestroy);
EI_VERIFY(0x00498400, CreateSourceVerify, 0, s_unusedCreateSource);
EI_VERIFY(0x00498480, TickSourcesVerify, 0, s_unusedTick);
EI_VERIFY(0x00498630, ReleaseUnheldVerify, 0, s_unusedReleaseUnheld);
