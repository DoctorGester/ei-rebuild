#include "spell.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"

#include <cstring>

using namespace itemdb;

namespace {
constexpr int kClassUnit = 0x50;
constexpr int kPointLight = 0x31;
constexpr float kZero = 0.0f;              // 0073B858
constexpr float kOne = 1.0f;               // 0073B84C
constexpr float kLumaRed = 0.3f;           // 0073C54C
constexpr float kLumaGreen = 0.6f;         // 0073CDE0
constexpr float kLumaBlue = 0.1f;          // 0073B850
constexpr float kSplitDamage = 4.0f;       // 0073BAA0
constexpr std::uint32_t kFltMaxBits = 0x7F7FFFFF;

// Prototype names of the races the target flags exclude (bits 2..5 of CSpell::targetFlags_).
constexpr const char* kHumanMale = "unhuma";      // 007908C8
constexpr const char* kHumanFemale = "unhufe";    // 00792EA4
constexpr const char* kGoblin = "unmogo";         // 00794DF0 (provisional reading of the names)
constexpr const char* kOrcMale = "unorma";        // 007959D8
constexpr const char* kOrcFemale = "unorfe";      // 007959D0
constexpr const char* kMoli = "unmoli";           // 007959C8

const SpellTargetUnit* AsUnit(const CEngineObject* o) { return reinterpret_cast<const SpellTargetUnit*>(o); }

// `a` regards `b`'s side as hostile. The shift count is taken mod 32, like SHL.
bool Regards(const SpellTargetUnit* a, const SpellTargetUnit* b) {
    return (a->relationsA & ((1u << (b->side & 31)) & a->relationsB)) != 0;
}
} // namespace

BOOL CSpell::Clear() {
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
    return 1;
}
EI_REPLACE_METHOD(0x0068B320, CSpell::Clear);

BOOL CSpell::InitFromPrototype(const char* prototype) {
    auto dword = [prototype](int offset) { return *reinterpret_cast<const std::uint32_t*>(prototype + offset); };
    unknown_30 = dword(0x20);
    unknown_34 = dword(0x24);
    std::uint32_t kind = dword(0x30);
    if (kind == 0x75) {            // 'u'
        targetFlags_ = 0x10000000;
    } else if (kind == 0x74) {     // 't'
        targetFlags_ = 0x20000000;
    }
    unknown_38 = dword(0x34);
    unknown_40 = dword(0x18);
    unknown_44 = dword(0x1C);
    price_ = dword(field::kSpellPrice);
    unknown_4C = dword(0x3C);
    unknown_50 = dword(0x44);
    unknown_1D = 0x10;
    prototypeIndex_ = static_cast<std::uint8_t>(unknown_18 >> 16);
    unknown_24 = dword(0x2C);
    unknown_28 = dword(0x28);
    unknown_20 = dword(0x38);
    std::memset(modifiers_, 0, sizeof(modifiers_));
    return 1;
}
EI_REPLACE_METHOD(0x0068B430, CSpell::InitFromPrototype);

char* CSpell::GetPrototype() {
    return Spells().At(unknown_18 >> 16, kSpellSize);
}
EI_REPLACE_METHOD(0x0068BA20, CSpell::GetPrototype);

int CSpell::GetModifierCount() {
    for (int i = 0; i < 8; ++i) {
        if (modifiers_[i] == 0) return i;
    }
    return 8;
}
EI_REPLACE_METHOD(0x0068BA60, CSpell::GetModifierCount);

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
EI_REPLACE_METHOD(0x0068B7C0, CSpell::HasCastFlagModifier);

int CSpell::GetDamageType() {
    switch (unknown_18 >> 16) {
    case 0: case 3: case 6: case 0x28: return 3;
    case 1: case 4: case 7: case 0x29: return 5;
    case 2: case 5: case 8: return 4;
    default: return 6;
    }
}
EI_REPLACE_METHOD(0x0068B700, CSpell::GetDamageType);

bool CSpell::IsOffensive() {
    switch (unknown_18 >> 16) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 0x11: case 0x13: case 0x19: case 0x1E: case 0x20: case 0x22: case 0x24: case 0x26:
        return true;
    case 0x28: case 0x29:   // Rebirth's new spells (its table runs to 0x29)
        return ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellSelfCasting);
    default:
        return false;
    }
}
EI_REPLACE_METHOD(0x0068B770, CSpell::IsOffensive);
// Site #0 of FixSpellSelfCasting; site #1 is in CBattleScreen::SelectUnit (00638EF0, original).
EI_REBIRTH_IMPLEMENTS(0x0068B770, ei::rebirth::Patch::FixSpellSelfCasting);

bool CSpell::CanAffect(const CEngineObject* caster, CEngineObject* target) {
    std::uint32_t flags = targetFlags_ & 0xFFFF;
    if (flags == 0) return true;
    if (!target || target->GetClassId() != kClassUnit) return false;

    const SpellTargetUnit* unit = AsUnit(target);
    if (caster) {
        const SpellTargetUnit* source = AsUnit(caster);
        bool hostile = Regards(source, unit) || Regards(unit, source);
        if ((flags & 1) && hostile) return false;     // friends only
        if ((flags & 2) && !hostile) return false;    // enemies only
    }

    const char* name = unit->prototypeName;
    if ((flags & 4) && (std::strcmp(name, kHumanMale) == 0 || std::strcmp(name, kHumanFemale) == 0)) return false;
    if ((flags & 8) && std::strcmp(name, kGoblin) == 0) return false;
    if ((flags & 0x10) && (std::strcmp(name, kOrcMale) == 0 || std::strcmp(name, kOrcFemale) == 0)) return false;
    if ((flags & 0x20) && std::strcmp(name, kMoli) == 0) return false;
    return true;
}
EI_REPLACE_METHOD(0x0068B4C0, CSpell::CanAffect);

BOOL CSpellEffect::GetDamage(DamageInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->unknown_2C = 6;
    out->multiplier = kOne;
    out->unknown_20 = kFltMaxBits;
    out->unknown_34 = kOne;
    out->unknown_38 = 0;

    switch (spellId >> 16) {
    case 0: case 3: case 0x28:
        out->typeWeights[3] = kOne; out->amount = damage; out->unknown_30 = 0; break;
    case 1: case 4: case 0x29:
        out->typeWeights[5] = kOne; out->amount = damage; out->unknown_30 = 0; break;
    case 2:
        out->typeWeights[4] = kOne; out->amount = damage; out->unknown_30 = 0; break;
    case 5: case 8:
        out->typeWeights[4] = kOne; out->amount = damage; out->unknown_30 = 1; break;
    case 6: case 0x27:
        out->typeWeights[3] = kOne; out->amount = damage; out->unknown_30 = 1; break;
    case 7:
        out->typeWeights[5] = kOne; out->amount = damage; out->unknown_30 = 1; break;
    default:
        break;
    }

    int targets = targetCount;
    if (targets > 1) {
        out->multiplier = kSplitDamage / static_cast<float>(targets);
        out->amount = kSplitDamage / static_cast<float>(targetCount) * out->amount;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00683950, CSpellEffect::GetDamage);

float CLight::GetIntensityAt(const Vector3* point) {
    if (type_ != kPointLight) {
        return color_[2] * kLumaBlue + color_[1] * kLumaGreen + color_[0] * kLumaRed;
    }
    float dx = point->x - position_.x;
    float dy = point->y - position_.y;
    float dz = point->z - position_.z;
    float distanceSquared = dz * dz + dy * dy + dx * dx;
    if (!(distanceSquared >= rangeSquared_)) {
        return (color_[2] * kLumaBlue + color_[1] * kLumaGreen + color_[0] * kLumaRed) *
               (kOne - distanceSquared * inverseRangeSquared_);
    }
    return kZero;
}
EI_REPLACE_METHOD(0x0068D730, CLight::GetIntensityAt);

void CLight::SetLitColors(const float* source, float scale) {
    // source is a byte layout: colours at +0x20 and +0x40 (float indices 8 and 16).
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
EI_REPLACE_METHOD(0x0068D280, CLight::SetLitColors);

int __stdcall SpellCodeByName(const char* name) {
    const RecordTable& spells = Spells();
    for (std::uint32_t i = 0; static_cast<int>(i) < static_cast<int>((spells.end - spells.begin) / kSpellSize); ++i) {
        auto* spell = reinterpret_cast<const SpellRecord*>(spells.At(i, kSpellSize));
        const char* spellName = spell->name;
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(spellName),
                             reinterpret_cast<const unsigned char*>(name)) == 0) {
            // The code string's pointer, as the original returns it.
            const char* code = reinterpret_cast<const SpellRecord*>(spells.At(i, kSpellSize))->code;
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(code));
        }
    }
    return 0;
}
EI_REPLACE(0x006893F0, SpellCodeByName);

char* __stdcall SpellModifierCodeByName(const char* name) {
    const RecordTable& modifiers = SpellModifiers();
    for (std::uint32_t i = 0;
         static_cast<int>(i) < static_cast<int>((modifiers.end - modifiers.begin) / kSpellModifierSize); ++i) {
        auto* modifier = reinterpret_cast<const SpellModifierRecord*>(modifiers.At(i, kSpellModifierSize));
        const char* modifierName = modifier->name;
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(modifierName),
                             reinterpret_cast<const unsigned char*>(name)) == 0) {
            // Out of range gives address 4 (null record + 4), as in the original.
            return reinterpret_cast<SpellModifierRecord*>(modifiers.At(i, kSpellModifierSize))->code;
        }
    }
    return nullptr;
}
EI_REPLACE(0x006894B0, SpellModifierCodeByName);
