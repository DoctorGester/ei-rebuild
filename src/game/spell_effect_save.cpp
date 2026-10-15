// Saving and loading a cast spell (CSpellEffect, the world server's missiles list) with the world's
// save game. See spell.h and docs/notes/items_spells.md ("Spell effects").
#include "spell.h"

#include "base_manager.h"
#include "core_streams.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

constexpr int kNoObject = -1;

AIRef& Ref(CEngineObject*& object) { return *reinterpret_cast<AIRef*>(&object); }

// One of the three counted effect objects by the id read (-1 none), looked up in the world's timed
// object manager; `id` keeps what the previous read left (the original's one stack slot).
void LoadEffectObject(CStream* stream, CWorld* world, std::uint32_t& id, CEngineObject*& field) {
    CBaseManager* manager = world->TimedObjectManager();
    stream->Read(&id, 4);
    CEngineObject* object = static_cast<int>(id) == kNoObject ? nullptr : manager->FindObject(id);
    CEngineObject* old = field;
    field = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

void SaveEffectObject(CStream* stream, const CEngineObject* object) {
    int id = object ? object->id_ : kNoObject;
    stream->Write(&id, 4);
}

} // namespace

// The packed id is read through the stream argument's stack slot and the object ids through a slot
// holding `this` (a read that writes nothing leaves those; kept).
void CSpellEffect::Load(CStream* stream) {
    world = LoadingProjectileWorld();
    stream->Read(&position, 12);
    Ref(caster).Load(stream, world);
    Ref(target).Load(stream, world);
    stream->Read(&targetPoint, 12);
    stream->Read(&state, 4);
    Spell()->Load(stream);
    auto packed = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&packed, 4);
    spellId = packed;
    CWorld* server = world;
    auto id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this));
    LoadEffectObject(stream, server, id, visual);
    LoadEffectObject(stream, server, id, sound);
    LoadEffectObject(stream, server, id, light);
    stream->Read(&timer, 4);
    stream->Read(&flags, 4);
    stream->Read(&position, 12);
    UpdateFootprint(static_cast<int>(spellId >> 16), 1);
}
EI_REPLACE_METHOD(0x0067E7D0, CSpellEffect::Load);

void CSpellEffect::Save(CStream* stream) {
    stream->Write(&position, 12);
    Ref(caster).Save(stream);
    Ref(target).Save(stream);
    stream->Write(&targetPoint, 12);
    stream->Write(&state, 4);
    Spell()->Save(stream);
    stream->Write(&spellId, 4);
    SaveEffectObject(stream, visual);
    SaveEffectObject(stream, sound);
    SaveEffectObject(stream, light);
    stream->Write(&timer, 4);
    stream->Write(&flags, 4);
    stream->Write(&position, 12);
}
EI_REPLACE_METHOD(0x0067E960, CSpellEffect::Save);
