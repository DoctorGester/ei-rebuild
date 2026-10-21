#pragma once

// Spells: prototypes in the spell database (item_db.h), CSpell instances built from them plus up
// to eight modifiers, and the spell effects that turn them into damage. Class names come from the
// game's own GetClassName strings where there is one; field names are provisional.

#include "base_manager.h"
#include "engine_object.h"
#include "resource_manager.h"
#include "item_db.h"
#include "maths.h"
#include "game_data.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CStream;              // core_streams.h
class CWorldServer;         // world.h
struct MapEffect;           // map_object.h

// A spell instance (vtable 00743A94). prototypeId_ of CEngineObject is the packed id: spell
// prototype index << 16.
class CSpell : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743A94);

    // spell_lifetime.cpp. 0068B1F0: the base fields, the effect record (MapEffect::Construct),
    // +0x30..+0x4C 0, +0x50 1, no modifiers; returns this. 0068B2C0: the inlined ClearModifiers
    // (slot 3), then the base's destructor body. 0068B2A0: slot 0 (operator delete with flag 1).
    CSpell* Construct();
    void Destruct();
    CSpell* DeletingDestruct(unsigned flags);

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
    // 0x22, 0x24, 0x26; with Rebirth's FixSpellSelfCasting also 0x28 and 0x29). Name provisional.
    bool IsOffensive();

    // 0068B4C0. Whether the spell may affect `target` when cast by `caster` (either may be null),
    // according to the target flags in the low 16 bits of flags_ (+0x3C).
    bool CanAffect(const CEngineObject* caster, CEngineObject* target) const;

    // 0068B350. Virtual slot 3 (Destroy; not named so to stay non-virtual): calls slot 5 and
    // clears the modifier list.
    void ClearModifiers();

    // 0068B370. Copies the parameters, the modifiers and the packed id into `target` (whose slot 3
    // runs first).
    void CopyTo(CSpell* target);

    // 0068B410. A new spell made by `manager` (CreateSpellObject(-1)) with this spell's contents.
    CSpell* Clone(class CSpellManager* manager);

    // 0068B860 / 0068B920 (vtable slots 9 / 10, Load / Save; spell_save.cpp): the effect record
    // (+0x1C), +0x30..+0x4C, the modifiers, flags_ and +0x50. Load clears the modifiers first.
    void LoadSpell(CStream* stream);
    void SaveSpell(CStream* stream);
    // +0x1C..+0x2F has the layout of an active effect record (map_object.h), which a buff copies.
    MapEffect& Effect() { return *reinterpret_cast<MapEffect*>(&prototypeIndex_); }
    const MapEffect& Effect() const { return *reinterpret_cast<const MapEffect*>(&prototypeIndex_); }

    std::uint8_t prototypeIndex_;   // +0x1C copy of the index byte
    std::uint8_t effectPower_;      // +0x1D the effect record's power, set to 0x10
    std::uint16_t unused_1E;
    std::uint32_t duration_;        // +0x20 prototype +0x38
    std::uint32_t damage_;          // +0x24 prototype +0x2C (float bits)
    std::uint32_t radius_;          // +0x28 prototype +0x28 (float bits; the area)
    std::uint32_t unused_2C;
    std::uint32_t speed_;           // +0x30 prototype +0x20 (float bits; missile speed)
    std::uint32_t range_;           // +0x34 prototype +0x24 (float bits)
    std::uint32_t targetCount_;     // +0x38 prototype +0x34
    std::uint32_t targetFlags_;     // +0x3C low 16 bits: target restrictions (see CanAffect)
    std::uint32_t manaCost_;        // +0x40 prototype +0x18 (float bits)
    std::uint32_t complexity_;      // +0x44 prototype +0x1C, +1 per modifier
    std::uint32_t price_;           // +0x48 prototype +0x0C (float, copied as a dword)
    std::uint32_t prototypeField14_;  // +0x4C prototype +0x3C, only loaded and saved
    std::uint32_t skillNeeded_;     // +0x50 prototype +0x44 plus the modifiers' +0x18
    std::int8_t modifiers_[8];      // +0x54 spell modifier index + 1 each, 0 ends the list
};
static_assert(offsetof(CSpell, prototypeIndex_) == 0x1C);
static_assert(offsetof(CSpell, targetFlags_) == 0x3C);
static_assert(sizeof(CSpell) == 0x5C);

// The units CSpell::CanAffect inspects (class id 0x50). Only the fields read here.
struct SpellTargetUnit {
    std::uint8_t unitHead[0xA4];    // CUnit's fields before +0xA4
    const char* prototypeName;      // +0xA4 e.g. "unhuma"
    std::uint8_t unitMiddle[0x244 - 0xA8];  // CUnit's fields +0xA8..+0x243
    std::uint32_t relationsA;       // +0x244 \ a unit regards side s as hostile when both have
    std::uint32_t relationsB;       // +0x248 / bit s set (provisional reading)
    std::uint32_t player;           // +0x24C CUnit::player
    std::uint32_t side;             // +0x250
};
static_assert(offsetof(SpellTargetUnit, side) == 0x250);

// The damage a hit deals, filled by the spell effects and the weapon code. 0x3C bytes.
struct DamageInfo {
    float typeWeights[7];       // +0x00 one weight per damage type
    std::uint32_t weaponValue;  // +0x1C the weapon's +0x4C (0 unarmed); not read here
    std::uint32_t attackRoll;   // +0x20 float bits against the defence; FLT_MAX for spells
    float amount;               // +0x24
    float multiplier;           // +0x28
    int location;               // +0x2C the body part aimed at; 6 (all) for spells
    int hitKind;                // +0x30 0/1 by spell; non-zero: no hit direction (OnDamaged)
    float backstabFactor;       // +0x34 above 1.0 for a backstab, else 1.0
    std::uint8_t calledShot;    // +0x38
    std::uint8_t unused_39[3];
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

    // spell_effect_start.cpp: the casting state (state 0) and the effect objects it creates.
    // 0067F870. Starts spell `type` (the prototype index): creates its sounds, particles, lights
    // and lightning, delivers the instant hits, and sets the next state (1 missile, 2 lasting,
    // 3 done; some failed casts leave it at 0) and the timer. Ends with UpdateFootprint(type, 1).
    void Start(int type);
    // 0067EF40. Replaces the visual (+0x90): the old one leaves the world's timed objects and is
    // released, the new one is counted and added to them.
    void SetVisual(CEngineObject* visual);
    // 0067EFC0. The walls (6 fire wall, 7 light wall) and the area clouds (8, 0x27) block the AI
    // map while they last: builds the footprint and stamps it (`stamp`) or clears it. Nothing
    // without a visual or for other types.
    void UpdateFootprint(int type, int stamp);
    // 0067F170. A missile (CEffectArrow, class 0x4E) from `from` at `target` with the flight
    // sound `sound` and a light of `color` / `range`; it becomes the visual.
    void CreateArrow(const Vector3* from, CEngineObject* target, int particleType, float power, const char* sound,
                     const Vector3* color, float range);
    // 0067F260. A particle source (class 0x4A; types 0x2010 and 0x2014 are walls, class 0x5A),
    // optionally attached to `attachTo`. `keep`: it becomes the visual, else it is only added to
    // the world's timed objects. Returns it.
    CEngineObject* CreateParticle(int keep, int particleType, const Vector3* position, float size, float alpha,
                                  const Vector3* velocity, CEngineObject* attachTo);
    // 0067F410. A lightning bolt (class 0x4D) from `from` to `target` (nothing without a target);
    // it becomes the visual. `flag` is a byte.
    void CreateLightning(const Vector3* from, CEngineObject* target, CEngineObject* source, float power, int flag);
    // 0067F580. A positional sound (class 0x48). `keep`: it becomes the sound (+0x94).
    void CreateSound(int keep, const char* name, const Vector3* position, const Vector3* velocity, float volume,
                     float radius);
    // 0067F660. A sound sequence (class 99): `first`, then `pattern` itself (count < 1) or
    // `pattern` formatted with 1..count. It becomes the sound (+0x94).
    void CreateSoundSequence(const Vector3* position, const Vector3* velocity, const char* first,
                             const char* pattern, int count, float volume, float radius);
    // 0067F7C0. A point light (class 0x4B, light flags 0x840); it becomes the light (+0x98).
    void CreatePointLight(const Vector3* position, const Vector3* color, float range, const Vector3* velocity);

    // spell_effect_update.cpp: the later states.
    // 0067E6D0. One tick: state 0 Start, 1 UpdateMissile, 2 UpdateLasting (all with the spell's
    // prototype index); any other state clears the footprint and drops the visual, sound and light
    // and returns 0 (the world then removes the effect). 0 also without a world; else 1.
    int Update();
    // 00681BB0. State 1: the fire arrow (0, 0x28) and acid ray (2) hit their target when the
    // missile reports a hit; the fireball (3) explodes when its timer runs out.
    void UpdateMissile(int type);
    // 00682650. State 2: the lasting effects (walls and clouds hit every 4th tick, fireworks scare,
    // clairvoyance and possession reveal, teleport moves the caster, link and vision fog end when
    // their units do); every case counts the timer down and ends the effect (state 3).
    void UpdateLasting(int type);
    // 0067EE00 / 0067EE40 / 0067EE80. Drop the visual / sound / light (leave the timed objects,
    // release).
    void ReleaseVisual();
    void ReleaseSound();
    void ReleaseLight();

    // spell_lifetime.cpp. 0067E510: everything but the reference count cleared, the spell
    // constructed; returns this. 0067E5D0: the footprint cleared when stamped, the visual removed
    // from the world's timed objects and dropped, the sound and light dropped, the spell destroyed,
    // the caster and target released; the base vtable 0073BB68 last. 0067E5B0: slot 0.
    CSpellEffect* Construct();
    void Destruct();
    CSpellEffect* DeletingDestruct(unsigned flags);
    // 0067ECB0 (RET 0x1C; A-H25). A cast begins: `spell` copied into the embedded spell, the
    // world, the position, the caster and the target (each counted; the one held before released),
    // the target point, state 0 and the byte +0xA4.
    void Init(CWorldServer* world, CSpell* spell, CEngineObject* caster, const Vector3* position, CEngineObject* target,
              const Vector3* targetPoint, std::uint8_t flagA4);
    // 0067EF00 (RET 4; A-H25). `object` leaves the world: 0 when it is the target (the world then
    // drops the effect); else 1, the caster released and cleared when it is `object`.
    BOOL Forget(CEngineObject* object);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0074399C);
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073BB68);

    // spell_effect_save.cpp (RET 4). 0067E960: the position, caster and target ids, the target
    // point, the state, the spell (its slot 10), the packed id, the visual / sound / light ids (-1
    // for none), the timer, the flags and the position again. 0067E7D0 reads them back in the world
    // of LoadingProjectileWorld (world.h), finding the three objects in its timed object manager,
    // and stamps the footprint (UpdateFootprint(type, 1)).
    void Load(CStream* stream);
    void Save(CStream* stream);

    std::uint8_t header[0x08];  // +0x00 vtable and reference count (the Projectile object)
    CWorldServer* world;        // +0x08
    std::uint32_t flags;        // +0x0C bit 0: the caster gets no credit for the hit
    Vector3 position;           // +0x10
    CEngineObject* target;      // +0x1C
    CEngineObject* caster;      // +0x20
    Vector3 targetPoint;        // +0x24
    int state;                  // +0x30 0 casting, 1 missile, 2 lasting, else ends
    std::uint8_t spell_[0x18];  // +0x34 the CSpell (vtable .. +0x17 of it)
    std::uint32_t spellId;      // +0x4C prototype index << 16
    std::uint32_t effectRecord; // +0x50 the spell's +0x1C (index byte, ...), a buff copies 0x14
    int targetCount;            // +0x54
    float damage;               // +0x58
    float radius;               // +0x5C
    std::uint32_t unused_60;
    float speed;                // +0x64 the spell's +0x30: missile speed
    std::uint8_t spellTail[0x90 - 0x68];  // +0x68 the spell's +0x34..+0x5B (Spell())
    CEngineObject* visual;      // +0x90 particle source, wall, lightning or missile (counted)
    CEngineObject* sound;       // +0x94 (counted)
    CEngineObject* light;       // +0x98 (counted)
    std::uint8_t footprintStamped;  // +0x9C
    std::uint8_t unused_9D[3];
    int timer;                  // +0xA0 ticks left in the next state
    std::uint8_t silentEnd;     // +0xA4 copied into the buff record; healing: skip when at full health
    std::uint8_t unused_A5[3];

    CSpell* Spell() { return reinterpret_cast<CSpell*>(spell_); }
};
static_assert(offsetof(CSpellEffect, world) == 0x08);
static_assert(offsetof(CSpellEffect, caster) == 0x20);
static_assert(offsetof(CSpellEffect, state) == 0x30);
static_assert(offsetof(CSpellEffect, spell_) == 0x34);
static_assert(offsetof(CSpellEffect, damage) == 0x58);
static_assert(offsetof(CSpellEffect, radius) == 0x5C);
static_assert(offsetof(CSpellEffect, speed) == 0x64);
static_assert(offsetof(CSpellEffect, visual) == 0x90);
static_assert(offsetof(CSpellEffect, timer) == 0xA0);
static_assert(sizeof(CSpellEffect) == 0xA8);

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

    // spell_manager.cpp. 0068BA80: the base fields, an empty modifier list (its sentinel
    // allocated), an empty code string with room for 8 bytes, the spell (CSpell::Construct inlined),
    // an empty name, then Reset inlined. Returns this. 0068BC50: the destructor body (the list
    // emptied, the spell's slot 3, the name, the spell's destructor, the string, the list's
    // sentinel, the base). 0068BC30: slot 0.
    CSpellContainer* Construct();
    void Destruct();
    CSpellContainer* DeletingDestruct(unsigned flags);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743AC8);

    char* prototype_;               // +0x1C spell prototype record
    SpellModifierNode* modifiers_;  // +0x20 list sentinel
    char* codes_;                   // +0x24 STLport string: modifier index + 1 per modifier
    char* codesEnd_;                // +0x28
    char* codesCapacity_;           // +0x2C
    std::uint8_t spell_[0x5C];      // +0x30 the CSpell (vtable 00743A94)
    mfc::CString name_;             // +0x8C (always empty)
};
static_assert(offsetof(CSpellContainer, modifiers_) == 0x20);
static_assert(offsetof(CSpellContainer, spell_) == 0x30);
static_assert(sizeof(CSpellContainer) == 0x90);
// 0068BC10 / 0068BC20 (slots 11 / 12, spell_manager.cpp): "CSpellContainer", class id 0x1A.
const char* __fastcall SpellContainerClassName(CSpellContainer*);
int __fastcall SpellContainerClassId(CSpellContainer*);

// The spell manager's "sequences" (+0xBC, keyed by the high half of a packed spell id): containers
// keyed by the low half, and the largest low half asked for. Nothing in the game fills them, so
// only CreateSpellObject with an id whose low half is not 0 reaches them (and then fails).
struct SpellSequence {
    // 006886F0 (spell_manager.cpp). The destructor body: every container released (the walk stops
    // at the first entry without one), then the table freed.
    void Destruct();

    stlp::hash_map<std::uint32_t, CSpellContainer*, stlp::IntHash> containers;
    std::uint32_t maxLow;
};
static_assert(sizeof(SpellSequence) == 0x18);
// 00689820 (spell_manager.cpp, A-H25). A walk over a sequence's containers one step on: the
// container there, or null at the end (and when the walk had ended already). The manager's
// Destroy 00688B40 (rewritten: it walks the table itself) and the unreferenced 00688D70 call it.
using SpellContainerWalk = stlp::hash_map<std::uint32_t, CSpellContainer*, stlp::IntHash>::Iterator;
CSpellContainer* __fastcall NextSpellContainer(SpellContainerWalk* walk);

// The spell manager (GetClassName "CSpellManager", 0xD4 bytes; a real virtual class: the compiler
// makes its vtable, C19, game.exe's 00743A14 is retired). Only what the construction code uses;
// the context (context_, +0xAC) gives the spell constructor (its slot 17).
class CSpellManager : public CResourceManager {
public:
    // 00688840 (party_lifetime.cpp). CResourceManager's constructor, an empty map with
    // NextPrime(100) buckets, +0xD0 0.
    CSpellManager();
    // 00688990. The sequences destroyed (006886F0 per sequence) and deleted, the map emptied and
    // freed, CResourceManager::Destroy, then CResourceManager's destructor.
    ~CSpellManager() override;
    const char* GetClassName() override;     // 11: 00688950 "CSpellManager"
    int GetClassId() override;               // 12: 00688960 (0x33)
    const char* GetFolderName() override;    // 25: 00688940 "spells"

    // The registrations (C20 retires them): 00688840 the constructor in place (returns this),
    // 00688990 the destructor, 00688970 (slot 0) the destructor, then operator delete when bit 0
    // of `flags` is set (returns this).
    CSpellManager* Construct();

    // 00688EB0. A new CSpellContainer (manager slot 23 with class 0x3003, key -1).
    CSpellContainer* NewContainer();

    // spell_manager.cpp.
    // 00688EC0. A new CSpell (slot 23, class 0x3002, key -1). For an id other than -1 the spell
    // takes it as its packed id: low half 0 initialises it from prototype (id >> 16) (null when out
    // of range); otherwise the sequence of the high half gives the container of the low half to
    // copy the spell from (FatalError without a sequence; a new entry has no container and faults).
    CSpell* CreateSpellObject(int id);
    // 2: 00688B30. CBaseManager::Create, as 0/1.
    int Create() override;
    // 3: 00688B40. Releases the containers of every sequence (each walk stops at the first entry
    // without one), deletes the sequences (their tables' bucket arrays leak), empties the map, then
    // CResourceManager::Destroy.
    void Destroy() override;
    // 14: 00688CD0. A new CSpell (0x3002) or CSpellContainer (0x3003), constructed; else null.
    CEngineObject* NewObject(int type) override;
    CEngineObject* NewObjectImpl(int type);   // body of the slot above (registered by member pointer)
    void Destruct();
    CSpellManager* DeletingDestruct(unsigned flags);

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

    // 00689570 / 00689610 (RET 4; A-H25). `names` resized to the spell / spell modifier record
    // count and filled with the records' names (+0x00). `this` is not used.
    void ListSpellNames(mfc::CStringArray* names);
    void ListModifierNames(mfc::CStringArray* names);

    // +0xBC: sequences by the high half of a packed id (keys with low half 0).
    stlp::hash_map<std::uint32_t, SpellSequence*, stlp::IntHash> sequences_;
    std::uint32_t maxSequenceKey_;   // +0xD0 the largest key asked for
};
static_assert(offsetof(CSpellManager, sequences_) == 0xBC);
// 00688950 / 00688960 (slots 11 / 12): "CSpellManager", class id 0x33.
const char* __fastcall SpellManagerClassName(CSpellManager*);
int __fastcall SpellManagerClassId(CSpellManager*);
const char* __fastcall SpellManagerFolder(CSpellManager*);   // 00688940 (slot 25) "spells"
static_assert(sizeof(CSpellManager) == 0xD4);

// The spell constructor (GetClassName "CSpellConstructor", vtable 007439C8, 0x24 bytes).
class CSpellConstructor : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x007439C8);

    // 00683FF0 (party_lifetime.cpp). Constructor body: CEngineObject's fields, no manager or
    // container, the vtable. Returns this.
    CSpellConstructor* Construct();

    // 006840A0 (spell_cast.cpp, A-H21). Destructor body: the vtable, the container released, the
    // manager's count dropped (+4), then the container member's release (null by then) and
    // CEngineObject's.
    void DestructBody();
    // 00684080 (slot 0, RET 4). DestructBody, then operator delete when bit 0 of `flags` is set.
    CSpellConstructor* DeletingDestruct(unsigned flags);

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
    BOOL CallAddModifier(char* modifier);

    CSpellManager* manager_;        // +0x1C counted (+4) but never released
    CSpellContainer* container_;    // +0x20
};
static_assert(sizeof(CSpellConstructor) == 0x24);
// 00684060 / 00684070 (spell_cast.cpp, A-H21): "CSpellConstructor", class id 0x1B.
const char* __fastcall SpellConstructorClassName(CSpellConstructor*);
int __fastcall SpellConstructorClassId(CSpellConstructor*);

// A light (vtable 00743AFC, GetClassName "CLight", class id 10). type_ 0x31 is a point light.
class CLight : public CEngineObject {
public:
    // 0068D730. Brightness (luminance of the colour) at `point`, falling off with the squared
    // distance for point lights.
    float GetIntensityAt(const Vector3* point);

    // 0068D280. Scales two colours of `source` (at +0x20 and +0x40) by the light's colour and
    // `scale` into litColors_.
    void SetLitColors(const float* source, float scale);

    // light.cpp (A-H23). 0068CED0: the base fields, identity rotations, everything from +0x94 to
    // +0xFC zero but +0xE4 / +0xE8 1.0, the range sqrt(FLT_MAX) with its square and inverse.
    // Returns this. 0068CFE0: the destructor body (CTransformObject's vtable, then the base's).
    // 0068CFC0: slot 0.
    CLight* Construct();
    void Destruct();
    CLight* DeletingDestruct(unsigned flags);
    // 0068D010 (RET 8). A point light (kind 1) of `color` at `position` (also kept at +0xC8);
    // flags_ bit 3 (changed). Returns 1.
    BOOL SetPoint(const Vector3* color, const Vector3* position);
    // 0068D0A0 (RET 8). A directional light (kind 3) of `color` along `direction`, which the light
    // also tilts towards (CTransformObject::TiltTowards); flags_ bit 3. Returns 1.
    BOOL SetDirectional(const Vector3* color, const Vector3* direction);
    // A-H25 (light.cpp; the terrain's vertex lighting 006C36B0 / 006C31C0). A colour (r, g, b in
    // x, y, z) lit by the light: `base` plus the light's colour times `scale` and a factor, each
    // channel clamped at 1; w is not written then (the original stores an unset local, which its
    // callers do not read). Unlit, `out` is `base` (all four).
    // 0068D310 (RET 0x10): the factor is minus the cosine between `normal` and direction_; unlit
    // when it is not above 0.
    // 0068D5A0 (RET 0x18): the factor is 1 - distance^2 / range^2 (unlit below 0), times 1 + the
    // cosine between `normal` and `toLight` when that is not above 0.
    void AddDirectional(Vector4* out, float scale, const Vector4* base, const Vector3* normal) const;
    void AddPoint(Vector4* out, float scale, const Vector4* base, const Vector3* normal, const Vector3* toLight,
                  float distance) const;
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743AFC);

    // The first 0x54 bytes are a CTransformObject (core_transform.h; its destructor's vtable is
    // stored), not declared as the base here yet.
    Vector3 position_;              // +0x1C
    Vector3 localPosition_;         // +0x28 the position in the space of the mesh being lit (005A7A70)
    Quaternion localRotation_;      // +0x34 identity
    Quaternion worldRotation_;      // +0x44 identity
    std::uint8_t unused_54[0x94 - 0x54];
    int kind_;                      // +0x94 1 point, 3 directional
    float color_[3];                // +0x98 r, g, b
    std::uint8_t unused_A4[0xC8 - 0xA4];
    Vector3 pointPosition_;         // +0xC8 SetPoint's position
    Vector3 direction_;             // +0xD4 a directional light's (the sun's) direction
    float range_;                   // +0xE0
    float unused_E4;               // +0xE4 1.0 from the constructor
    float unused_E8;               // +0xE8 1.0 from the constructor
    std::uint8_t unused_EC[0xFC - 0xEC];
    std::uint32_t unused_FC;
    float rangeSquared_;            // +0x100
    float inverseRangeSquared_;     // +0x104
    Vector3 localDirection_;        // +0x108 direction_ in the space of the mesh being lit (005A7A70)
    float litColors_[2][4];         // +0x114
};
static_assert(offsetof(CLight, kind_) == 0x94 && offsetof(CLight, pointPosition_) == 0xC8 &&
              offsetof(CLight, unused_FC) == 0xFC && sizeof(CLight) == 0x134);
static_assert(offsetof(CLight, color_) == 0x98);
static_assert(offsetof(CLight, rangeSquared_) == 0x100);
static_assert(offsetof(CLight, litColors_) == 0x114);
static_assert(offsetof(CLight, range_) == 0xE0);
static_assert(offsetof(CLight, localPosition_) == 0x28 && offsetof(CLight, direction_) == 0xD4 &&
              offsetof(CLight, localDirection_) == 0x108);

// 0068CFF0 / 0068D000 (slots 2 / 3, light.cpp): Create returns 1; Destroy clears flags_ bit 5.
// 0068CFA0 / 0068CFB0 (slots 11 / 12): "CLight", class id 10.
int __fastcall LightCreate(CLight*);
void __fastcall LightDestroy(CLight*);
const char* __fastcall LightClassName(CLight*);
int __fastcall LightClassId(CLight*);

// The light factory (GetClassName "CLightManager", class id 0x29, vtable 00743B50, 0xD8 bytes; the
// global pointer at 007AF748): CreateObject (slot 23) makes lights by class (0x2E: the renderer's
// default light, 00500B70; 0x31: point lights). Its +0xD4 list holds the ids of the lights held on
// (00511FB0 erases from it). light.cpp (A-H23).
class CLightFactory : public CBaseManager {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743B50);

    // 0068D850. CBaseManager's constructor body, +0xB0..+0xC0 and +0xCC 0, an empty list, +0xD0 1.
    // Returns this. 0068D990: the destructor body (the list freed, +0xCC 0, +0xC0 released, then
    // CBaseManager's). 0068D970: slot 0.
    CLightFactory* Construct();
    void Destruct();
    CLightFactory* DeletingDestruct(unsigned flags);
    // 14: 0068DA40. A new CLight (0x134 bytes) of type 0x2E or 0x31 (its type_); other types are
    // logged and give null.
    CEngineObject* NewObject(int type) override;
    CEngineObject* NewObjectImpl(int type);   // body of the slot above (registered by member pointer)
    // 23: 0068DAD0. CBaseManager::CreateObject, then flags_ bit 7 cleared (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    CEngineObject* CreateObjectImpl(int type, int id);   // body of the slot above (registered by member pointer)

    std::uint32_t unused_B0[4];    // +0xB0
    CEngineObject* held_;           // +0xC0 released by the destructor
    std::uint32_t unused_C4[2];
    std::uint32_t unused_CC;       // +0xCC
    std::uint32_t unused_D0;       // +0xD0 1
    stlp::list<int> litIds_;        // +0xD4
};
static_assert(offsetof(CLightFactory, held_) == 0xC0 && offsetof(CLightFactory, litIds_) == 0xD4 &&
              sizeof(CLightFactory) == 0xD8);
// 0068D950 / 0068D960 (slots 11 / 12): "CLightManager", class id 0x29.
const char* __fastcall LightManagerClassName(CLightFactory*);
int __fastcall LightManagerClassId(CLightFactory*);
inline CLightFactory* TheLightFactory() { return ei::GameData<CLightFactory* const>(0x007AF748); }

// 006893F0. The code (+0x04) of the spell prototype whose name matches (_mbsicmp); null if none.
char* __stdcall SpellCodeByName(const char* name);

// 006894B0. Address of the code (+0x04) of the spell modifier whose name matches; null if none.
char* __stdcall SpellModifierCodeByName(const char* name);
