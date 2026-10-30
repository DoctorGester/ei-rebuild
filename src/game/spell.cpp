#include "spell.h"

#include "core_streams.h"
#include "map_object.h"
#include "unit.h"
#include "ei/rebirth.h"
#include "lib/bytes.h"
#include "lib/nan_audit.h"
#include "lib/text_case.h"

#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstring>

using namespace itemdb;

namespace {

// Prototype names of the races the target flags exclude (SpellTargetFlag NoHumans .. NoMoli).
constexpr const char* kHumanMale = "unhuma";
constexpr const char* kHumanFemale = "unhufe";
constexpr const char* kGoblin = "unmogo";         // provisional reading of the names
constexpr const char* kOrcMale = "unorma";
constexpr const char* kOrcFemale = "unorfe";
constexpr const char* kMoli = "unmoli";

// `a` regards `b`'s side as hostile. The shift count is taken mod 32, like SHL.
bool Regards(const Unit* unit, const Unit* other) {
    return (unit->relationsA & ((1u << (other->side & 31)) & unit->relationsB)) != 0;
}
} // namespace

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
    std::memset(modifiers, 0, sizeof(modifiers));
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
    std::memset(modifiers, 0, sizeof(modifiers));
}

itemdb::SpellRecord* Spell::GetPrototype() {
    return SpellAt(prototypeId >> 16);
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
        // Modifier index = entry - 1, sign-extended then compared unsigned; faults when out of range.
        SpellModifierRecord* modifier = SpellModifierAt(static_cast<std::uint32_t>(entry - 1));
        int code;
        lib::LoadBytes(code, modifier->code);
        if (code == 0x6369 || code == 0x7469) return true;   // "ic", "it"
    }
    return false;
}

int Spell::GetDamageType() {
    switch (Type()) {
    case SpellType::FireArrow: case SpellType::Fireball: case SpellType::FireWall: case SpellType::RickMagic:
        return 3;
    case SpellType::Lightning: case SpellType::InvokeLightning: case SpellType::LightningWall:
    case SpellType::CurseMagic:
        return 5;
    case SpellType::AcidRay: case SpellType::AcidColumn: case SpellType::AcidFog:
        return 4;
    default:
        return 6;
    }
}

bool Spell::IsOffensive() {
    switch (Type()) {
    case SpellType::FireArrow: case SpellType::Lightning: case SpellType::AcidRay: case SpellType::Fireball:
    case SpellType::InvokeLightning: case SpellType::AcidColumn: case SpellType::FireWall:
    case SpellType::LightningWall: case SpellType::AcidFog:
    case SpellType::Lichdom: case SpellType::Stench: case SpellType::Stun: case SpellType::Weaken:
    case SpellType::Feeblemind: case SpellType::Slow: case SpellType::Charm: case SpellType::Shrink:
        return true;
    case SpellType::RickMagic: case SpellType::CurseMagic:
        return ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellSelfCasting);
    default:
        return false;
    }
}
// Site #0 of FixSpellSelfCasting; site #1 is in BattleScreen::SelectUnit.

bool Spell::CanAffect(const Unit* caster, MapObject* target) const {
    std::uint32_t restricted = targetFlags & kSpellTargetRestrictions;
    if (restricted == 0) return true;
    if (!target || target->GetClassId() != ClassId::Unit) return false;

    // The target is a unit (tested above).
    const Unit* unit = static_cast<const Unit*>(target);
    if (caster) {
        const Unit* source = caster;
        bool hostile = Regards(source, unit) || Regards(unit, source);
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

char* SpellCodeByName(std::string_view name) {
    const SpellTable& spells = g_spellDatabase.spells;
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>(spells.Count()); ++i) {
        const SpellRecord* spell = spells.At(i);
        if (text::CompareIgnoringCase(spell->name, name) == 0) {
            // The code string's pointer.
            const char* code = spells.At(i)->code.c_str();
            return const_cast<char*>(code);
        }
    }
    return nullptr;
}

char* SpellModifierCodeByName(std::string_view name) {
    SpellModifierTable& modifiers = g_spellDatabase.modifiers;
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>(modifiers.Count()); ++i) {
        const SpellModifierRecord* modifier = modifiers.At(i);
        if (text::CompareIgnoringCase(modifier->name, name) == 0) {
            // Out of range gives address 4 (null record + 4).
            return modifiers.At(i)->code;
        }
    }
    return nullptr;
}

// --- The spell's effect record, lifetime, class name and id ---------------------------------------

MapEffect Spell::EffectRecord() const {
    MapEffect record;
    record.type = prototypeIndex;
    record.power = effectPower;
    record.ticksLeft = static_cast<int>(duration);
    record.value = std::bit_cast<std::uint32_t>(damage);
    record.strength = std::bit_cast<std::uint32_t>(radius);
    return record;
}

void Spell::SetEffect(const MapEffect& record) {
    prototypeIndex = record.type;
    effectPower = record.power;
    duration = static_cast<std::uint32_t>(record.ticksLeft);
    damage = std::bit_cast<float>(record.value);
    radius = std::bit_cast<float>(record.strength);
}

Spell::Spell() {
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

Spell::~Spell() { Spell::Destroy(); }

const char* Spell::ClassName() { return "CSpell"; }
ClassId Spell::GetClassId() { return ClassId::Spell; }

// --- An effect's tick ----------------------------------------------------------------------------

int MapEffect::Tick() {
    --ticksLeft;
    return ticksLeft > 0 ? 1 : 0;
}

void Spell::Destroy() {
    ClearState();
    std::memset(modifiers, 0, sizeof(modifiers));
}

void Spell::CopyTo(Spell* target) {
    target->Destroy();
    std::memcpy(&target->prototypeIndex, &prototypeIndex, 0x54 - 0x1C);
    std::memset(target->modifiers, 0, sizeof(target->modifiers));
    for (int i = 0; i < 8; ++i) {
        if (modifiers[i] == 0) break;
        target->modifiers[i] = modifiers[i];
    }
    target->prototypeId = prototypeId;
}

Spell* Spell::Clone(SpellManager* manager) {
    Spell* spell = manager->CreateSpellObject(-1);
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
    std::memset(modifiers, 0, sizeof(modifiers));
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
    stream->Read(modifiers, sizeof(modifiers));
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
    stream->Write(modifiers, sizeof(modifiers));
    stream->WriteValue(flags);
    stream->WriteValue(skillNeeded);
}
