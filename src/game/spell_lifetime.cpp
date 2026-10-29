// Constructor and destructor bodies of the spell objects: the effect record (MapEffect), the spell
// (CSpell) and the cast spell (CSpellEffect). See spell.h.
#include "map_object.h"
#include "spell.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include <bit>
#include <cstring>

namespace {

// A counted pointer's destructor and the counted pointer assignment of null: the old object loses a
// reference.
template <class T>
void Drop(T*& field) {
    T* old = field;
    field = nullptr;
    if (old) old->Release();
}

} // namespace

MapEffect CSpell::Effect() const {
    MapEffect record;
    record.type = prototypeIndex_;
    record.power = effectPower_;
    record.ticksLeft = static_cast<int>(duration_);
    record.value = std::bit_cast<std::uint32_t>(damage_);
    record.strength = std::bit_cast<std::uint32_t>(radius_);
    return record;
}

void CSpell::SetEffect(const MapEffect& record) {
    prototypeIndex_ = record.type;
    effectPower_ = record.power;
    duration_ = static_cast<std::uint32_t>(record.ticksLeft);
    damage_ = std::bit_cast<float>(record.value);
    radius_ = std::bit_cast<float>(record.strength);
}

CSpell::CSpell() {
    prototypeIndex_ = 0;
    effectPower_ = 0;
    duration_ = 0;
    damage_ = 0;
    radius_ = 0;
    speed_ = 0;
    range_ = 0;
    targetCount_ = 0;
    targetFlags_ = 0;
    manaCost_ = 0;
    complexity_ = 0;
    price_ = 0;
    castTime_ = 0;
    skillNeeded_ = 1;
    std::memset(modifiers_, 0, sizeof(modifiers_));
}

CSpell::~CSpell() { CSpell::Destroy(); }

const char* CSpell::ClassName() { return "CSpell"; }
ClassId CSpell::GetClassId() { return ClassId::Spell; }

CSpellEffect::CSpellEffect() = default;

CSpellEffect::~CSpellEffect() {
    if (footprintStamped) UpdateFootprint(Spell()->Type(), 0);
    if (visual) world->RemoveTimedObject(static_cast<unsigned>(visual->id_));
    Drop(visual);
    ReleaseSound();
    ReleaseLight();
}

// --- The cast's start values and the object it forgets ------------------------------------

void CSpellEffect::Init(CWorldServer* newWorld, CSpell* spell, CUnit* newCaster, const Vector3* newPosition,
                        CMapObject* newTarget, const Vector3* newTargetPoint, std::uint8_t newSilentEnd) {
    spell->CopyTo(Spell());
    world = newWorld;
    position = *newPosition;
    caster.Assign(newCaster);
    target.Assign(newTarget);
    targetPoint = *newTargetPoint;
    state = 0;
    silentEnd = newSilentEnd;
}

bool CSpellEffect::Forget(CEngineObject* object) {
    if (target == object) return false;
    if (caster == object && caster) caster.Reset();
    return true;
}

// --- An effect's tick --------------------------------------------------------------

int MapEffect::Tick() {
    --ticksLeft;
    return ticksLeft > 0 ? 1 : 0;
}
