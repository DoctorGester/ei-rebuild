#pragma once

// Spells: prototypes in the spell database (item_db.h), CSpell instances built from them plus up
// to eight modifiers, and the spell effects that turn them into damage. Class names come from the
// game's own GetClassName strings where there is one; field names are provisional.

#include "engine_object.h"
#include "item_db.h"
#include "maths.h"

// A spell instance (vtable 00743A94). unknown_18 of CEngineObject is the packed id: spell
// prototype index << 16.
class CSpell : public CEngineObject {
public:
    // 0068B320. Virtual slot 2: clears the parameters and the modifier list. Returns 1.
    BOOL Clear();   // vtable slot 2; deliberately not named Create (that would override CEngineObject::Create)

    // 0068B430. Copies the parameters from a spell prototype record. Returns 1.
    BOOL InitFromPrototype(const char* prototype);

    // 0068BA20. The spell's prototype record, or null if the index is out of range.
    char* GetPrototype();

    // 0068BA60. Number of modifiers (the list ends at the first zero, at most 8).
    int GetModifierCount();

    // 0068B7C0. Whether any modifier is "ic" or "it" (the ones that set cast flags 0x20000 and
    // 0x10000 when the spell is constructed).
    bool HasCastFlagModifier();

    // 0068B700. The elemental damage type the spell deals (3, 4 or 5), 6 for none.
    int GetDamageType();

    // 0068B770. True for the attack spells (prototype indices 0-8, 0x11, 0x13, 0x19, 0x1E, 0x20,
    // 0x22, 0x24, 0x26). Name provisional.
    bool IsOffensive();

    // 0068B4C0. Whether the spell may affect `target` when cast by `caster` (either may be null),
    // according to the target flags in the low 16 bits of flags_ (+0x3C).
    bool CanAffect(const CEngineObject* caster, CEngineObject* target);

    std::uint8_t prototypeIndex_;   // +0x1C copy of the index byte
    std::uint8_t unknown_1D;        // +0x1D set to 0x10
    std::uint16_t unknown_1E;
    std::uint32_t unknown_20;       // +0x20 prototype +0x38
    std::uint32_t unknown_24;       // +0x24 prototype +0x2C
    std::uint32_t unknown_28;       // +0x28 prototype +0x28
    std::uint32_t unknown_2C;
    std::uint32_t unknown_30;       // +0x30 prototype +0x20
    std::uint32_t unknown_34;       // +0x34 prototype +0x24
    std::uint32_t unknown_38;       // +0x38 prototype +0x34
    std::uint32_t targetFlags_;     // +0x3C low 16 bits: target restrictions (see CanAffect)
    std::uint32_t unknown_40;       // +0x40 prototype +0x18
    std::uint32_t unknown_44;       // +0x44 prototype +0x1C
    std::uint32_t price_;           // +0x48 prototype +0x0C (float, copied as a dword)
    std::uint32_t unknown_4C;       // +0x4C prototype +0x3C
    std::uint32_t unknown_50;       // +0x50 prototype +0x44
    std::int8_t modifiers_[8];      // +0x54 spell modifier index + 1 each, 0 ends the list
};
static_assert(offsetof(CSpell, prototypeIndex_) == 0x1C);
static_assert(offsetof(CSpell, targetFlags_) == 0x3C);
static_assert(sizeof(CSpell) == 0x5C);

// The units CSpell::CanAffect inspects (class id 0x50). Only the fields read here.
struct SpellTargetUnit {
    std::uint8_t unknown_00[0xA4];
    const char* prototypeName;      // +0xA4 e.g. "unhuma"
    std::uint8_t unknown_A8[0x244 - 0xA8];
    std::uint32_t relationsA;       // +0x244 \ a unit regards side s as hostile when both have
    std::uint32_t relationsB;       // +0x248 / bit s set (provisional reading)
    std::uint32_t unknown_24C;
    std::uint32_t side;             // +0x250
};
static_assert(offsetof(SpellTargetUnit, side) == 0x250);

// The damage a hit deals, filled by the spell effects and the weapon code. 0x3C bytes.
struct DamageInfo {
    float typeWeights[7];       // +0x00 one weight per damage type
    std::uint32_t unknown_1C;
    std::uint32_t unknown_20;   // +0x20 FLT_MAX for spells
    float amount;               // +0x24
    float multiplier;           // +0x28
    int unknown_2C;             // +0x2C 6 for spells
    int unknown_30;             // +0x30 0/1 depending on the spell
    float unknown_34;           // +0x34 1.0
    std::uint8_t unknown_38;
    std::uint8_t unknown_39[3];
};
static_assert(sizeof(DamageInfo) == 0x3C);

// A spell being applied (the object built by 00682650 and friends). Only the fields read here.
struct CSpellEffect {
    // 00683950. Fills `out` with the damage this effect deals; split between the targets when
    // there are several. Returns 1.
    BOOL GetDamage(DamageInfo* out);

    std::uint8_t unknown_00[0x4C];
    std::uint32_t spellId;      // +0x4C prototype index << 16
    std::uint32_t unknown_50;
    int targetCount;            // +0x54
    float damage;               // +0x58
};
static_assert(offsetof(CSpellEffect, damage) == 0x58);

// A light (vtable 00743AFC, GetClassName "CLight", class id 10). type_ 0x31 is a point light.
class CLight : public CEngineObject {
public:
    // 0068D730. Brightness (luminance of the colour) at `point`, falling off with the squared
    // distance for point lights.
    float GetIntensityAt(const Vector3* point);

    // 0068D280. Scales two colours of `source` (at +0x20 and +0x40) by the light's colour and
    // `scale` into litColors_.
    void SetLitColors(const float* source, float scale);

    Vector3 position_;              // +0x1C
    std::uint8_t unknown_28[0x98 - 0x28];
    float color_[3];                // +0x98 r, g, b
    std::uint8_t unknown_A4[0x100 - 0xA4];
    float rangeSquared_;            // +0x100
    float inverseRangeSquared_;     // +0x104
    std::uint8_t unknown_108[0x114 - 0x108];
    float litColors_[2][4];         // +0x114
};
static_assert(offsetof(CLight, color_) == 0x98);
static_assert(offsetof(CLight, rangeSquared_) == 0x100);
static_assert(offsetof(CLight, litColors_) == 0x114);

// 006893F0. The code (+0x04) of the spell prototype whose name matches (_mbsicmp); 0 if none.
int __stdcall SpellCodeByName(const char* name);

// 006894B0. Address of the code (+0x04) of the spell modifier whose name matches; null if none.
char* __stdcall SpellModifierCodeByName(const char* name);
