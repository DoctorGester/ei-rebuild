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

    // 0068B350. Virtual slot 3 (Destroy; not named so to stay non-virtual): calls slot 5 and
    // clears the modifier list.
    void ClearModifiers();

    // 0068B370. Copies the parameters, the modifiers and the packed id into `target` (whose slot 3
    // runs first).
    void CopyTo(CSpell* target);

    // 0068B410. A new spell made by `manager` (CreateSpellObject(-1)) with this spell's contents.
    CSpell* Clone(class CSpellManager* manager);

    // Forwarder to the game's entry of CopyTo (so its verify wrapper sees the call).
    void CallCopyTo(CSpell* target);

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
// It embeds the CSpell it applies at +0x34, so spellId..radius are that spell's +0x18..+0x28.
struct CSpellEffect {
    // 00683950. Fills `out` with the damage this effect deals; split between the targets when
    // there are several. Returns 1.
    BOOL GetDamage(DamageInfo* out);

    // 006835B0. Delivers `hit` to `unit` (CUnit slot 31) when the spell may affect it (no caster,
    // or CSpell::CanAffect). The unit's slot gets (hit, attacker, caster); attacker is the caster
    // unless flags bit 0 is set.
    void HitUnit(DamageInfo* hit, CEngineObject* unit);

    // 00683600. Delivers this effect's damage to every unit within `radius` of `position` that
    // IsSpellTarget accepts and the spell may affect.
    void HitUnitsInRadius();

    // 00683730. The same for the units within `radius` that lie within 1 of the vertical plane
    // through `position` along `direction` (only x and y are used): the wall spells.
    void HitUnitsAlong(const Vector3* direction);

    // Forwarders to the game's entries (so the verify wrappers see the calls).
    BOOL CallGetDamage(DamageInfo* out);

    std::uint8_t unknown_00[0x08];
    char* world;                // +0x08 its sector map is at +0x170 + 0x124
    std::uint32_t flags;        // +0x0C bit 0: the caster gets no credit for the hit
    Vector3 position;           // +0x10
    CEngineObject* target;      // +0x1C
    CEngineObject* caster;      // +0x20
    std::uint8_t unknown_24[0x34 - 0x24];
    std::uint8_t spell_[0x18];  // +0x34 the CSpell (vtable .. +0x17 of it)
    std::uint32_t spellId;      // +0x4C prototype index << 16
    std::uint32_t unknown_50;
    int targetCount;            // +0x54
    float damage;               // +0x58
    float radius;               // +0x5C

    CSpell* Spell() { return reinterpret_cast<CSpell*>(spell_); }
};
static_assert(offsetof(CSpellEffect, world) == 0x08);
static_assert(offsetof(CSpellEffect, caster) == 0x20);
static_assert(offsetof(CSpellEffect, spell_) == 0x34);
static_assert(offsetof(CSpellEffect, damage) == 0x58);
static_assert(offsetof(CSpellEffect, radius) == 0x5C);

// 00681B80. __fastcall. Whether area spells hit `unit`: units with a size class (+0x324), and
// those whose prototype's +0x24 is below 0.5 (or NaN). Returns 0/1 in EAX.
int __fastcall IsSpellTarget(CEngineObject* unit);

// A node of the STLport list<char*> of modifier records a container collects.
struct SpellModifierNode {
    SpellModifierNode* next;
    SpellModifierNode* prev;
    char* modifier;             // spell modifier record (item_db.h, 0x24 bytes)
};

// A spell under construction (GetClassName "CSpellContainer", vtable 00743AC8, 0x90 bytes,
// created by the spell manager as class 0x3003): a prototype, the modifiers added so far, and the
// CSpell (at +0x30) that Build computes from them.
class CSpellContainer : public CEngineObject {
public:
    // 0068BD80. Virtual slot 2: forgets the prototype and the modifiers and clears the spell
    // (its slot 2). Returns 1.
    BOOL Reset();

    // 0068BDD0. Virtual slot 3: slot 5, then the same as Reset but the spell gets its slot 3.
    void Shutdown();

    // 0068BC00. Virtual slot 7: the name pointer at +0x8C.
    const char* NameField();

    // 0068BE30. Appends `modifier` (a modifier record) to the list. Null is a fatal error (its
    // result is returned). Returns 1.
    BOOL AddModifier(char* modifier);

    // 0068BEC0. Whether the prototype accepts a modifier of `modifier`'s kind (its first code
    // letter: a, d, e, r, t read the prototype's flag bytes +0x7A, +0x7C, +0x7B, +0x78, +0x79;
    // others are accepted). 0 without a prototype or modifier.
    int AllowsModifier(const char* modifier);

    // 0068BF60. Computes the spell (+0x30) from the prototype and the modifiers: area (a),
    // effect (e), mana (m), range (r) scale with the modifier's strength, duration (d) and t add
    // integers, f* add target flags and ic/it cast flags; every modifier adds its price, mana and
    // level. The area becomes a radius (sqrt(area / pi)); the modifier list is sorted. Returns 1.
    BOOL Build();

    CSpell* Spell() { return reinterpret_cast<CSpell*>(spell_); }

    char* prototype_;               // +0x1C spell prototype record
    SpellModifierNode* modifiers_;  // +0x20 list sentinel
    char* codes_;                   // +0x24 STLport string: modifier index + 1 per modifier
    char* codesEnd_;                // +0x28
    char* codesCapacity_;           // +0x2C
    std::uint8_t spell_[0x5C];      // +0x30 the CSpell (vtable 00743A94)
    const char* name_;              // +0x8C
};
static_assert(offsetof(CSpellContainer, modifiers_) == 0x20);
static_assert(offsetof(CSpellContainer, spell_) == 0x30);
static_assert(sizeof(CSpellContainer) == 0x90);

// The spell manager (GetClassName "CSpellManager", vtable 00743A14; a CBaseManager). Only what
// the construction code uses.
class CSpellManager : public CEngineObject {
public:
    // 00688EB0. A new CSpellContainer (manager slot 23 with class 0x3003, key -1).
    CSpellContainer* NewContainer();

    // 00688EC0 (not rewritten). A new CSpell (class 0x3002) with packed id `id` (-1: none).
    CSpell* CreateSpellObject(int id);

    // 00689310. Builds a spell from `prototype` and up to 8 modifier records of `modifiers` (an
    // STLport list<char*>, passed as a pointer to its sentinel pointer) through the context's
    // spell constructor. Returns the new spell, or a fatal error's result.
    CSpell* CreateSpell(char* prototype, SpellModifierNode** modifiers);

    // 00689030. Builds a spell from text such as "healing{e2;e2}": the trimmed text before '{' is
    // the prototype name, the ';'-separated names inside the braces are modifiers (empty ones
    // skipped). Names are looked up in the name maps with operator[] / find_or_insert, so an
    // unknown name is added to the map with a null record. Returns the new spell, or a fatal
    // error's result.
    CSpell* CreateSpellFromText(const char* text);

    std::uint8_t unknown_1C[0xAC - 0x1C];
    CEngineObject* context_;        // +0xAC its slot 17 gives the spell constructor
};
static_assert(offsetof(CSpellManager, context_) == 0xAC);

// The spell constructor (GetClassName "CSpellConstructor", vtable 007439C8, 0x24 bytes).
class CSpellConstructor : public CEngineObject {
public:
    // 00684120. Virtual slot 2: releases the container and forgets the manager. Returns 1.
    BOOL Reset();

    // 00684150. Virtual slot 3: the same, also dropping the manager's count (+4).
    void Detach();

    // 006841A0. Releases the container and counts a reference to `manager` (+4, no release).
    void SetManager(CSpellManager* manager);

    // 006841E0. Starts a spell from `prototype` in a new container. Returns 1, or a fatal error's
    // result without a manager or prototype.
    BOOL BeginConstruct(char* prototype);

    // 00684250. Builds the container's spell. Returns 1, or a fatal error's result.
    BOOL EndConstruct();

    // 006842A0. Adds `modifier` and rebuilds when the prototype accepts it. Returns 0/1.
    BOOL AddModifier(char* modifier);

    // Forwarders to the game's entries (so the verify wrappers see the calls).
    void CallSetManager(CSpellManager* manager);
    BOOL CallBeginConstruct(char* prototype);
    BOOL CallEndConstruct();
    BOOL CallAddModifier(char* modifier);

    CSpellManager* manager_;        // +0x1C counted (+4) but never released
    CSpellContainer* container_;    // +0x20
};
static_assert(sizeof(CSpellConstructor) == 0x24);

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
