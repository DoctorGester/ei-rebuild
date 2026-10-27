#include "spell.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <cstring>

using namespace itemdb;

namespace {
constexpr int kPointLight = 0x31;
constexpr float kZero = 0.0f;
constexpr float kOne = 1.0f;
constexpr float kLumaRed = 0.3f;
constexpr float kLumaGreen = 0.6f;
constexpr float kLumaBlue = 0.1f;
constexpr float kSplitDamage = 4.0f;
constexpr std::uint32_t kFltMaxBits = 0x7F7FFFFF;

// Prototype names of the races the target flags exclude (bits 2..5 of CSpell::targetFlags_).
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

int CSpell::Create() {
    speed_ = 0;
    range_ = 0;
    targetCount_ = 0;
    targetFlags_ = 0;
    manaCost_ = 0;
    complexity_ = 0;
    price_ = 0;
    prototypeField14_ = 0;
    skillNeeded_ = 1;
    std::memset(modifiers_, 0, sizeof(modifiers_));
    return 1;
}
EI_REWRITTEN(0x0068B320, CSpell::Create);

BOOL CSpell::InitFromPrototype(const itemdb::SpellRecord* prototype) {
    speed_ = prototype->speed;
    range_ = prototype->range;
    if (prototype->targetKind == 'u') {
        targetFlags_ = 0x10000000;
    } else if (prototype->targetKind == 't') {
        targetFlags_ = 0x20000000;
    }
    targetCount_ = prototype->targetCount;
    manaCost_ = prototype->mana;
    complexity_ = prototype->complexity;
    price_ = prototype->price;
    prototypeField14_ = prototype->value3C;
    skillNeeded_ = static_cast<std::uint32_t>(prototype->skill);
    effectPower_ = 0x10;
    prototypeIndex_ = static_cast<std::uint8_t>(prototypeId_ >> 16);
    damage_ = prototype->damage;
    radius_ = prototype->area;
    duration_ = prototype->duration;
    std::memset(modifiers_, 0, sizeof(modifiers_));
    return 1;
}
EI_REWRITTEN(0x0068B430, CSpell::InitFromPrototype);

itemdb::SpellRecord* CSpell::GetPrototype() {
    return SpellAt(prototypeId_ >> 16);
}
EI_REWRITTEN(0x0068BA20, CSpell::GetPrototype);

int CSpell::GetModifierCount() {
    for (int i = 0; i < 8; ++i) {
        if (modifiers_[i] == 0) return i;
    }
    return 8;
}
EI_REWRITTEN(0x0068BA60, CSpell::GetModifierCount);

bool CSpell::HasCastFlagModifier() {
    for (int i = 0; i < 8; ++i) {
        std::int8_t entry = modifiers_[i];
        if (entry == 0) break;
        // Modifier index = entry - 1, sign-extended then compared unsigned; faults when out of range.
        SpellModifierRecord* modifier = SpellModifierAt(static_cast<std::uint32_t>(entry - 1));
        int code;
        std::memcpy(&code, modifier->code, sizeof(code));
        if (code == 0x6369 || code == 0x7469) return true;   // "ic", "it"
    }
    return false;
}
EI_REWRITTEN(0x0068B7C0, CSpell::HasCastFlagModifier);

int CSpell::GetDamageType() {
    switch (prototypeId_ >> 16) {
    case 0: case 3: case 6: case 0x28: return 3;
    case 1: case 4: case 7: case 0x29: return 5;
    case 2: case 5: case 8: return 4;
    default: return 6;
    }
}
EI_REWRITTEN(0x0068B700, CSpell::GetDamageType);

bool CSpell::IsOffensive() {
    switch (prototypeId_ >> 16) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 0x11: case 0x13: case 0x19: case 0x1E: case 0x20: case 0x22: case 0x24: case 0x26:
        return true;
    case 0x28: case 0x29:   // Rebirth's new spells (its table runs to 0x29)
        return ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellSelfCasting);
    default:
        return false;
    }
}
EI_REWRITTEN(0x0068B770, CSpell::IsOffensive);
// Site #0 of FixSpellSelfCasting; site #1 is in CBattleScreen::SelectUnit.

bool CSpell::CanAffect(const CEngineObject* caster, CEngineObject* target) const {
    std::uint32_t flags = targetFlags_ & 0xFFFF;
    if (flags == 0) return true;
    if (!target || target->GetClassId() != kClassUnit) return false;

    // The caster is taken as a unit without a class test.
    const CUnit* unit = static_cast<const CUnit*>(target);
    if (caster) {
        const CUnit* source = static_cast<const CUnit*>(caster);
        bool hostile = Regards(source, unit) || Regards(unit, source);
        if ((flags & 1) && hostile) return false;     // friends only
        if ((flags & 2) && !hostile) return false;    // enemies only
    }

    const char* name = unit->name.GetString();   // the prototype name, e.g. "unhuma"
    if ((flags & 4) && (std::strcmp(name, kHumanMale) == 0 || std::strcmp(name, kHumanFemale) == 0)) return false;
    if ((flags & 8) && std::strcmp(name, kGoblin) == 0) return false;
    if ((flags & 0x10) && (std::strcmp(name, kOrcMale) == 0 || std::strcmp(name, kOrcFemale) == 0)) return false;
    if ((flags & 0x20) && std::strcmp(name, kMoli) == 0) return false;
    return true;
}
EI_REWRITTEN(0x0068B4C0, CSpell::CanAffect);

BOOL CSpellEffect::GetDamage(DamageInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->location = 6;
    out->multiplier = kOne;
    out->attackRoll = kFltMaxBits;
    out->backstabFactor = kOne;
    out->calledShot = 0;

    const CSpell* spell = Spell();
    switch (spell->prototypeId_ >> 16) {
    case 0: case 3: case 0x28:
        out->typeWeights[3] = kOne; out->amount = spell->damage_; out->hitKind = 0; break;
    case 1: case 4: case 0x29:
        out->typeWeights[5] = kOne; out->amount = spell->damage_; out->hitKind = 0; break;
    case 2:
        out->typeWeights[4] = kOne; out->amount = spell->damage_; out->hitKind = 0; break;
    case 5: case 8:
        out->typeWeights[4] = kOne; out->amount = spell->damage_; out->hitKind = 1; break;
    case 6: case 0x27:
        out->typeWeights[3] = kOne; out->amount = spell->damage_; out->hitKind = 1; break;
    case 7:
        out->typeWeights[5] = kOne; out->amount = spell->damage_; out->hitKind = 1; break;
    default:
        break;
    }

    int targets = static_cast<int>(spell->duration_);
    if (targets > 1) {
        out->multiplier = kSplitDamage / static_cast<float>(targets);
        out->amount = kSplitDamage / static_cast<float>(static_cast<int>(spell->duration_)) * out->amount;
    }
    return 1;
}
EI_REWRITTEN(0x00683950, CSpellEffect::GetDamage);

float CLight::GetIntensityAt(const Vector3* point) {
    if (type_ != kPointLight) {
        return color_[2] * kLumaBlue + color_[1] * kLumaGreen + color_[0] * kLumaRed;
    }
    float dx = point->x - localPosition_.x;
    float dy = point->y - localPosition_.y;
    float dz = point->z - localPosition_.z;
    float distanceSquared = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
    EI_NAN_AUDIT(distanceSquared, rangeSquared_);
    if (distanceSquared < rangeSquared_) {
        return (color_[2] * kLumaBlue + color_[1] * kLumaGreen + color_[0] * kLumaRed) *
               (kOne - distanceSquared * inverseRangeSquared_);
    }
    return kZero;
}
EI_REWRITTEN(0x0068D730, CLight::GetIntensityAt);

void CLight::SetLitColors(const float* source, float scale) {
    // source is a byte layout: colours and +0x40 (float indices 8 and 16).
    const float* a = source + 8;
    const float* b = source + 16;
    litColors_[0][0] = scale * a[0] * color_[0];
    litColors_[0][1] = a[1] * color_[1] * scale;
    litColors_[0][2] = scale * a[2] * color_[2];
    litColors_[0][3] = a[3];
    litColors_[1][0] = scale * b[0] * color_[0];
    litColors_[1][1] = b[1] * color_[1] * scale;
    litColors_[1][3] = kOne;
    litColors_[1][2] = scale * b[2] * color_[2];
}
EI_REWRITTEN(0x0068D280, CLight::SetLitColors);

char* __stdcall SpellCodeByName(const char* name) {
    const SpellTable& spells = TheSpellDatabase().spells;
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>(spells.Count()); ++i) {
        const SpellRecord* spell = spells.At(i);
        const char* spellName = spell->name;
        if (gamecrt::mbsicmp(spellName,
                            name) == 0) {
            // The code string's pointer.
            const char* code = spells.At(i)->code;
            return const_cast<char*>(code);
        }
    }
    return nullptr;
}
EI_REWRITTEN(0x006893F0, SpellCodeByName);

char* __stdcall SpellModifierCodeByName(const char* name) {
    const SpellModifierTable& modifiers = TheSpellDatabase().modifiers;
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>(modifiers.Count()); ++i) {
        const SpellModifierRecord* modifier = modifiers.At(i);
        const char* modifierName = modifier->name;
        if (gamecrt::mbsicmp(modifierName,
                            name) == 0) {
            // Out of range gives address 4 (null record + 4).
            return modifiers.At(i)->code;
        }
    }
    return nullptr;
}
EI_REWRITTEN(0x006894B0, SpellModifierCodeByName);
