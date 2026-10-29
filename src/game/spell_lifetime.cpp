// Constructor and destructor bodies of the spell objects: the effect record (MapEffect), the spell
// (CSpell) and the cast spell (SpellEffect). See spell.h.
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
    record.type = prototypeIndex;
    record.power = effectPower;
    record.ticksLeft = static_cast<int>(duration);
    record.value = std::bit_cast<std::uint32_t>(damage);
    record.strength = std::bit_cast<std::uint32_t>(radius);
    return record;
}

void CSpell::SetEffect(const MapEffect& record) {
    prototypeIndex = record.type;
    effectPower = record.power;
    duration = static_cast<std::uint32_t>(record.ticksLeft);
    damage = std::bit_cast<float>(record.value);
    radius = std::bit_cast<float>(record.strength);
}

CSpell::CSpell() {
    prototypeIndex = 0;
    effectPower = 0;
    duration = 0;
    damage = 0;
    radius = 0;
    speed = 0;
    range = 0;
    targetCount = 0;
    targetFlags = 0;
    manaCost = 0;
    complexity = 0;
    price = 0;
    castTime = 0;
    skillNeeded = 1;
    std::memset(modifiers, 0, sizeof(modifiers));
}

CSpell::~CSpell() { CSpell::Destroy(); }

const char* CSpell::ClassName() { return "CSpell"; }
ClassId CSpell::GetClassId() { return ClassId::Spell; }

SpellEffect::SpellEffect() = default;

SpellEffect::~SpellEffect() {
    if (footprintStamped) UpdateFootprint(Spell()->Type(), 0);
    if (visual) world->RemoveTimedObject(static_cast<unsigned>(visual->id));
    Drop(visual);
    ReleaseSound();
    ReleaseLight();
}

// --- The cast's start values and the object it forgets ------------------------------------

void SpellEffect::Init(WorldServer* newWorld, CSpell* newSpell, CUnit* newCaster, const Vector3& newPosition,
                        CMapObject* newTarget, const Vector3& newTargetPoint, std::uint8_t newSilentEnd) {
    newSpell->CopyTo(Spell());
    world = newWorld;
    position = newPosition;
    caster.Assign(newCaster);
    target.Assign(newTarget);
    targetPoint = newTargetPoint;
    state = 0;
    silentEnd = newSilentEnd;
}

bool SpellEffect::Forget(EngineObject* object) {
    if (target == object) return false;
    if (caster == object && caster) caster.Reset();
    return true;
}

// --- An effect's tick --------------------------------------------------------------

int MapEffect::Tick() {
    --ticksLeft;
    return ticksLeft > 0 ? 1 : 0;
}
