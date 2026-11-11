#include "game/spells/spell.h"

#include "game/core/core_streams.h"
#include "game/objects/map_object.h"
#include "game/spells/spell_db.h"
#include "game/units/unit.h"
#include "lib/crt.h"
#include "lib/text_case.h"

#include <cfloat>
#include <cstdint>

using namespace itemdb;

namespace {

// Prototype names of the races the target flags exclude (SpellTargetFlag NoHumans .. NoMoli).
constexpr const char* kHumanMale = "unhuma";
constexpr const char* kHumanFemale = "unhufe";
constexpr const char* kGoblin = "unmogo";         // provisional reading of the names
constexpr const char* kOrcMale = "unorma";
constexpr const char* kOrcFemale = "unorfe";
constexpr const char* kMoli = "unmoli";

// `unit` regards `other`'s side as hostile. The shift count is taken mod 32.
bool Regards(const Unit* unit, const Unit* other) {
    return (unit->relationsA & ((1u << (other->side & 31)) & unit->relationsB)) != 0;
}
} // namespace

bool SpellHostile(const Unit* caster, const Unit* unit) { return Regards(caster, unit) || Regards(unit, caster); }

void Spell::Create() {
    speed = 0;
    range = 0;
    targetCount = 0;
    targetFlags = 0;
    manaCost = 0;
    complexity = 0;
    price = 0;
    castTime = 0;
    skillNeeded = 1;
    modifiers = {};
}

void Spell::InitFromPrototype(const itemdb::SpellRecord* prototype) {
    speed = prototype->speed;
    range = prototype->range;
    if (prototype->targetKind == 'u') {
        targetFlags = lib::Bits(SpellTargetFlag::AtUnits);
    } else if (prototype->targetKind == 't') {
        targetFlags = lib::Bits(SpellTargetFlag::AtPoint);
    }
    targetCount = prototype->targetCount;
    manaCost = prototype->mana;
    complexity = prototype->complexity;
    price = prototype->price;
    castTime = prototype->castTime;
    skillNeeded = static_cast<std::uint32_t>(prototype->skill);
    effectPower = 0x10;
    prototypeIndex = static_cast<std::uint8_t>(prototypeId >> 16);
    damage = prototype->damage;
    radius = prototype->area;
    duration = prototype->duration;
    modifiers = {};
}

itemdb::SpellRecord* Spell::GetPrototype() const {
    return SpellAt(prototypeId >> 16);
}

bool Spell::HasRecords() {
    if (!GetPrototype()) return false;
    for (int i = 0; i < GetModifierCount(); ++i) {
        if (!SpellModifierAt(static_cast<std::uint32_t>(modifiers[i] - 1))) return false;
    }
    return true;
}

void Spell::RequireRecords() {
    if (HasRecords()) return;
    game::FatalExit("Spell %d names spell %u or a modifier which the spell database does not have: the save or the "
                    "other player uses another database.",
                    id, prototypeId >> 16);
}

int Spell::GetModifierCount() {
    for (int i = 0; i < 8; ++i) {
        if (modifiers[i] == 0) return i;
    }
    return 8;
}

bool Spell::HasCastFlagModifier() {
    for (int i = 0; i < 8; ++i) {
        std::int8_t entry = modifiers[i];
        if (entry == 0) break;
        const SpellModifierRecord* modifier = SpellModifierAt(static_cast<std::uint32_t>(entry - 1));
        // "ic" or "it", then 0 0.
        const char* code = modifier->code;
        if (code[0] == 'i' && (code[1] == 'c' || code[1] == 't') && code[2] == 0 && code[3] == 0) return true;
    }
    return false;
}

bool Spell::CanAffect(const Unit* caster, MapObject* target) const {
    std::uint32_t restricted = targetFlags & kSpellTargetRestrictions;
    if (restricted == 0) return true;
    const Unit* unit = UnitOrNull(target);
    if (!unit) return false;

    if (caster) {
        bool hostile = SpellHostile(caster, unit);
        if ((restricted & SpellTargetFlag::FriendsOnly) && hostile) return false;
        if ((restricted & SpellTargetFlag::EnemiesOnly) && !hostile) return false;
    }

    const std::string& name = unit->name;   // the prototype name, e.g. "unhuma"
    if ((restricted & SpellTargetFlag::NoHumans) && (name == kHumanMale || name == kHumanFemale)) return false;
    if ((restricted & SpellTargetFlag::NoGoblins) && name == kGoblin) return false;
    if ((restricted & SpellTargetFlag::NoOrcs) && (name == kOrcMale || name == kOrcFemale)) return false;
    if ((restricted & SpellTargetFlag::NoMoli) && name == kMoli) return false;
    return true;
}

const std::string* SpellCodeByName(std::string_view name) {
    const SpellTable& spells = g_spellDatabase.spells;
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>(spells.Count()); ++i) {
        const SpellRecord* spell = spells.At(i);
        if (text::CompareIgnoringCase(spell->name, name) == 0) {
            return &spell->code;
        }
    }
    return nullptr;
}

const char* SpellModifierCodeByName(std::string_view name) {
    SpellModifierTable& modifiers = g_spellDatabase.modifiers;
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>(modifiers.Count()); ++i) {
        const SpellModifierRecord* modifier = modifiers.At(i);
        if (text::CompareIgnoringCase(modifier->name, name) == 0) return modifier->code;
    }
    return nullptr;
}

// --- The spell's effect record, lifetime, class name and id ---------------------------------------

MapEffect Spell::EffectRecord() const {
    MapEffect record;
    record.type = prototypeIndex;
    record.power = effectPower;
    record.ticksLeft = static_cast<int>(duration);
    record.value = damage;
    record.strength = radius;
    return record;
}

void Spell::SetEffect(const MapEffect& record) {
    prototypeIndex = record.type;
    effectPower = record.power;
    duration = static_cast<std::uint32_t>(record.ticksLeft);
    damage = record.value;
    radius = record.strength;
}

Spell::Spell() = default;

Spell::~Spell() { Spell::Destroy(); }

const char* Spell::ClassName() { return "CSpell"; }
ClassId Spell::GetClassId() { return ClassId::Spell; }

// --- An effect's behaviour and tick --------------------------------------------------------------

SpellType MapEffect::Behaviour() const { return ActiveEffectAt(type); }

int MapEffect::Kind() const { return itemdb::EffectKindAt(type); }

int MapEffect::Tick() {
    --ticksLeft;
    return ticksLeft > 0 ? 1 : 0;
}

void Spell::Destroy() {
    ClearState();
    modifiers = {};
}

void Spell::CopyTo(Spell* target) {
    target->Destroy();
    static_cast<SpellValues&>(*target) = *this;
    for (std::size_t i = 0; i < modifiers.size(); ++i) {
        if (modifiers[i] == 0) break;
        target->modifiers[i] = modifiers[i];
    }
    target->prototypeId = prototypeId;
}

Spell* Spell::Clone(SpellManager* manager) {
    Spell* spell = manager->CreateSpellObject();
    CopyTo(spell);
    return spell;
}

// --- Saving and loading ------------------------------------------------------------------------------

// Saving and loading a spell (Spell's Load / Save) and an active effect record (MapEffect, which
// the map objects' Load / Save call for each of their effects).

void MapEffect::Load(ByteStream* stream) {
    stream->ReadValue(type);
    stream->ReadValue(power);
    stream->ReadValue(ticksLeft);
    stream->ReadValue(value);
    stream->ReadValue(strength);
}

void MapEffect::Save(ByteStream* stream) const {
    stream->WriteValue(type);
    stream->WriteValue(power);
    stream->WriteValue(ticksLeft);
    stream->WriteValue(value);
    stream->WriteValue(strength);
}

void Spell::Load(ByteStream* stream) {
    modifiers = {};
    MapEffect record = EffectRecord();
    record.Load(stream);
    SetEffect(record);
    stream->ReadValue(speed);
    stream->ReadValue(range);
    stream->ReadValue(targetCount);
    stream->ReadValue(targetFlags);
    stream->ReadValue(manaCost);
    stream->ReadValue(complexity);
    stream->ReadValue(price);
    stream->ReadValue(castTime);
    stream->Read(modifiers.data(), static_cast<std::uint32_t>(modifiers.size()));
    std::uint32_t stateFlags = 0;
    stream->ReadValue(stateFlags);
    flags = stateFlags;
    stream->ReadValue(skillNeeded);
}

void Spell::Save(ByteStream* stream) {
    EffectRecord().Save(stream);
    stream->WriteValue(speed);
    stream->WriteValue(range);
    stream->WriteValue(targetCount);
    stream->WriteValue(targetFlags);
    stream->WriteValue(manaCost);
    stream->WriteValue(complexity);
    stream->WriteValue(price);
    stream->WriteValue(castTime);
    stream->Write(modifiers.data(), static_cast<std::uint32_t>(modifiers.size()));
    stream->WriteValue(flags);
    stream->WriteValue(skillNeeded);
}
