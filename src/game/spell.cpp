#include "spell.h"
#include "unit.h"

#include "ei/rebirth.h"
#include "lib/bytes.h"
#include "lib/nan_audit.h"
#include "lib/text_case.h"

#include <bit>
#include <cfloat>
#include <cstring>

using namespace itemdb;

namespace {
constexpr int kPointLight = 0x31;
constexpr float kLumaRed = 0.3f;
constexpr float kLumaGreen = 0.6f;
constexpr float kLumaBlue = 0.1f;
constexpr float kSplitDamage = 4.0f;

// Prototype names of the races the target flags exclude (SpellTargetFlag NoHumans .. NoMoli).
constexpr const char* kHumanMale = "unhuma";
constexpr const char* kHumanFemale = "unhufe";
constexpr const char* kGoblin = "unmogo";         // provisional reading of the names
constexpr const char* kOrcMale = "unorma";
constexpr const char* kOrcFemale = "unorfe";
constexpr const char* kMoli = "unmoli";

// `a` regards `b`'s side as hostile. The shift count is taken mod 32, like SHL.
bool Regards(const CUnit* a, const CUnit* b) {
    return (a->relationsA & ((1u << (b->side & 31)) & a->relationsB)) != 0;
}
} // namespace

void CSpell::Create() {
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

void CSpell::InitFromPrototype(const itemdb::SpellRecord* prototype) {
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

itemdb::SpellRecord* CSpell::GetPrototype() {
    return SpellAt(prototypeId >> 16);
}

int CSpell::GetModifierCount() {
    for (int i = 0; i < 8; ++i) {
        if (modifiers[i] == 0) return i;
    }
    return 8;
}

bool CSpell::HasCastFlagModifier() {
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

int CSpell::GetDamageType() {
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

bool CSpell::IsOffensive() {
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

bool CSpell::CanAffect(const CUnit* caster, CMapObject* target) const {
    std::uint32_t restricted = targetFlags & kSpellTargetRestrictions;
    if (restricted == 0) return true;
    if (!target || target->GetClassId() != ClassId::Unit) return false;

    // The target is a unit (tested above).
    const CUnit* unit = static_cast<const CUnit*>(target);
    if (caster) {
        const CUnit* source = caster;
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

void SpellEffect::GetDamage(DamageInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->location = 6;
    out->multiplier = 1.0f;
    out->attackRoll = FLT_MAX;
    out->backstabFactor = 1.0f;
    out->calledShot = 0;

    const CSpell* record = Spell();
    switch (record->Type()) {
    case SpellType::FireArrow: case SpellType::Fireball: case SpellType::RickMagic:
        out->typeWeights[3] = 1.0f; out->amount = record->damage; out->hitKind = 0; break;
    case SpellType::Lightning: case SpellType::InvokeLightning: case SpellType::CurseMagic:
        out->typeWeights[5] = 1.0f; out->amount = record->damage; out->hitKind = 0; break;
    case SpellType::AcidRay:
        out->typeWeights[4] = 1.0f; out->amount = record->damage; out->hitKind = 0; break;
    case SpellType::AcidColumn: case SpellType::AcidFog:
        out->typeWeights[4] = 1.0f; out->amount = record->damage; out->hitKind = 1; break;
    case SpellType::FireWall: case SpellType::Campfire:
        out->typeWeights[3] = 1.0f; out->amount = record->damage; out->hitKind = 1; break;
    case SpellType::LightningWall:
        out->typeWeights[5] = 1.0f; out->amount = record->damage; out->hitKind = 1; break;
    default:
        break;
    }

    int targets = static_cast<int>(record->duration);
    if (targets > 1) {
        out->multiplier = kSplitDamage / static_cast<float>(targets);
        out->amount = kSplitDamage / static_cast<float>(static_cast<int>(record->duration)) * out->amount;
    }
}

float CLight::GetIntensityAt(const Vector3& point) {
    if (type != kPointLight) {
        return color[2] * kLumaBlue + color[1] * kLumaGreen + color[0] * kLumaRed;
    }
    float dx = point.x - localPosition.x;
    float dy = point.y - localPosition.y;
    float dz = point.z - localPosition.z;
    float distanceSquared = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
    EI_NAN_AUDIT(distanceSquared, rangeSquared);
    if (distanceSquared < rangeSquared) {
        return (color[2] * kLumaBlue + color[1] * kLumaGreen + color[0] * kLumaRed) *
               (1.0f - distanceSquared * inverseRangeSquared);
    }
    return 0.0f;
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

// --- An effect's tick ----------------------------------------------------------------------------

int MapEffect::Tick() {
    --ticksLeft;
    return ticksLeft > 0 ? 1 : 0;
}
