// Saving and loading a cast spell (CSpellEffect, the world server's missiles list) with the world's
// save game. See spell.h and docs/notes/items_spells.md ("Spell effects").
#include "spell.h"

#include "base_manager.h"
#include "core_streams.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include <cstdint>

namespace {

constexpr int kNoObject = -1;

// One of the three counted effect objects by the id read (-1 none), looked up in the world's timed object
// manager; `id` keeps what the previous read left.
template <class T>
void LoadEffectObject(CStream* stream, CWorld* world, std::uint32_t& id, T*& field) {
    CBaseManager* manager = world->TimedObjectManager();
    stream->ReadValue(id);
    T* object = static_cast<int>(id) == kNoObject ? nullptr : manager->FindObjectAs<T>(id);
    T* old = field;
    field = object;
    if (object) object->AddRef();
    if (old) old->Release();
}

void SaveEffectObject(CStream* stream, const CEngineObject* object) {
    int id = object ? object->id_ : kNoObject;
    stream->WriteValue(id);
}

} // namespace

void CSpellEffect::Load(CStream* stream) {
    world = g_loadingProjectileWorld;
    stream->ReadValue(position);
    LoadRef(caster, stream, world);
    LoadRef(target, stream, world);
    stream->ReadValue(targetPoint);
    stream->ReadValue(state);
    Spell()->Load(stream);
    std::uint32_t packed = 0;
    stream->ReadValue(packed);
    Spell()->prototypeId_ = packed;
    CWorld* server = world;
    std::uint32_t id = 0;
    LoadEffectObject(stream, server, id, visual);
    LoadEffectObject(stream, server, id, sound);
    LoadEffectObject(stream, server, id, light);
    stream->ReadValue(timer);
    stream->ReadValue(flags);
    stream->ReadValue(position);
    UpdateFootprint(Spell()->Type(), 1);
}

void CSpellEffect::Save(CStream* stream) {
    stream->WriteValue(position);
    SaveRef(caster, stream);
    SaveRef(target, stream);
    stream->WriteValue(targetPoint);
    stream->WriteValue(state);
    Spell()->Save(stream);
    stream->WriteValue(Spell()->prototypeId_);
    SaveEffectObject(stream, visual);
    SaveEffectObject(stream, sound);
    SaveEffectObject(stream, light);
    stream->WriteValue(timer);
    stream->WriteValue(flags);
    stream->WriteValue(position);
}
