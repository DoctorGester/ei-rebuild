// Constructor and destructor bodies of the spell objects: the effect record (MapEffect), the spell
// (CSpell) and the cast spell (CSpellEffect). See spell.h.
#include "spell.h"

#include "map_object.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

using ei::SetVtable;

namespace {

// A counted pointer's destructor and the counted pointer assignment of null (00683A90): the old
// object loses a reference (slot 4).
void Drop(CEngineObject*& field) {
    CEngineObject* old = field;
    field = nullptr;
    if (old) old->Release();
}

} // namespace

MapEffect* MapEffect::Construct() {
    type = 0;
    power = 0;
    ticksLeft = 0;
    value = 0;
    ending = 0;
    return this;
}
EI_REPLACE_METHOD(0x00688510, MapEffect::Construct);

// --- CSpell --------------------------------------------------------------------------------------

CSpell* CSpell::Construct() {
    ClearFields();
    Effect().Construct();
    SetVtable(this, kVtableAddress);
    unknown_30 = 0;
    unknown_34 = 0;
    unknown_38 = 0;
    targetFlags_ = 0;
    unknown_40 = 0;
    unknown_44 = 0;
    price_ = 0;
    unknown_4C = 0;
    unknown_50 = 1;
    std::memset(modifiers_, 0, sizeof(modifiers_));
    return this;
}
EI_REPLACE_METHOD(0x0068B1F0, CSpell::Construct);

void CSpell::Destruct() {
    SetVtable(this, kVtableAddress);
    ClearModifiers();
    DestructBase();
}
EI_REPLACE_METHOD(0x0068B2C0, CSpell::Destruct);

CSpell* CSpell::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0068B2A0, CSpell::DeletingDestruct);

// --- CSpellEffect --------------------------------------------------------------------------------

CSpellEffect* CSpellEffect::Construct() {
    target = nullptr;
    caster = nullptr;
    Spell()->Construct();
    visual = nullptr;
    sound = nullptr;
    light = nullptr;
    position = Vector3{0.0f, 0.0f, 0.0f};
    SetVtable(this, kVtableAddress);
    timer = 0;
    world = nullptr;
    flags = 0;
    unknown_A4 = 0;
    footprintStamped = 0;
    return this;
}
EI_REPLACE_METHOD(0x0067E510, CSpellEffect::Construct);

void CSpellEffect::Destruct() {
    SetVtable(this, kVtableAddress);
    if (footprintStamped) UpdateFootprint(static_cast<int>(spellId >> 16), 0);
    if (visual) reinterpret_cast<CWorldServer*>(world)->RemoveTimedObject(static_cast<unsigned>(visual->id_));
    Drop(visual);
    ReleaseSound();
    ReleaseLight();
    // The three counted pointers' destructors (empty by now).
    if (light) light->Release();
    if (sound) sound->Release();
    if (visual) visual->Release();
    Spell()->Destruct();
    if (caster) caster->Release();
    caster = nullptr;
    if (target) target->Release();
    target = nullptr;
    SetVtable(this, kBaseVtableAddress);
}
EI_REPLACE_METHOD(0x0067E5D0, CSpellEffect::Destruct);

CSpellEffect* CSpellEffect::DeletingDestruct(unsigned deleteFlags) {
    Destruct();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0067E5B0, CSpellEffect::DeletingDestruct);
