#pragma once

// Spells: prototypes in the spell database (item_db.h), CSpell instances built from them plus up to eight
// modifiers, and the spell effects that turn them into damage.

#include "base_manager.h"
#include "core_refcount.h"
#include "core_transform.h"   // CTransformObject (CLight)
#include "engine_object.h"
#include "resource_manager.h"
#include "item_db.h"
#include "maths.h"
#include "game_data.h"
#include "lib/static_object.h"
#include "game_singletons.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CStream;
class CWorldServer;
class CUnit;
class CMapObject;
struct MapEffect;
class CEffect;
class CEffectParticle;
class CEffectPointLight;

// A spell instance ("CSpell", class id 0x4F). prototypeId_ of CEngineObject is the packed id: spell
// prototype index << 16.
class CSpell : public CEngineObject {
public:
    CSpell();
    ~CSpell() override;

    int Create() override;
    void Destroy() override;
    // Load / Save (spell_save.cpp): the effect record, the parameters, the modifiers, flags_ and the last
    // field. Load clears the modifiers first.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* GetClassName() override;
    int GetClassId() override;             // class id 0x4F

    BOOL InitFromPrototype(const itemdb::SpellRecord* prototype);

    // The spell's prototype record, or null if the index is out of range.
    itemdb::SpellRecord* GetPrototype();

    // Number of modifiers (the list ends at the first zero, at most 8).
    int GetModifierCount();

    // Whether any modifier is "ic" or "it" (the ones that set cast flags 0x20000 and
    // 0x10000 when the spell is constructed).
    bool HasCastFlagModifier();

    // The elemental damage type the spell deals (3, 4 or 5), 6 for none.
    int GetDamageType();

    // True for the attack spells (prototype indices 0-8, 0x11, 0x13, 0x19, 0x1E, 0x20, 0x22, 0x24, 0x26; with
    // Rebirth's FixSpellSelfCasting also 0x28 and 0x29).
    bool IsOffensive();

    // Whether the spell may affect `target` when cast by `caster` (either may be null),
    // according to the target flags in the low 16 bits of flags_.
    bool CanAffect(const CEngineObject* caster, CEngineObject* target) const;

    // Copies the parameters, the modifiers and the packed id into `target` (whose Destroy runs first).
    void CopyTo(CSpell* target);

    // A new spell made by `manager` (CreateSpellObject(-1)) with this spell's contents.
    CSpell* Clone(class CSpellManager* manager);

    // The fields from prototypeIndex_ on have the layout of an active effect record (map_object.h), which a buff copies.
    MapEffect& Effect() { return *reinterpret_cast<MapEffect*>(&prototypeIndex_); }
    const MapEffect& Effect() const { return *reinterpret_cast<const MapEffect*>(&prototypeIndex_); }

    std::uint8_t prototypeIndex_;   // copy of the index byte
    std::uint8_t effectPower_;      // the effect record's power, set to 0x10
    std::uint16_t unused_1E;
    std::uint32_t duration_;        // prototype +0x38
    float damage_;                  // prototype +0x2C
    float radius_;                  // prototype +0x28 (the area; Build makes it a radius)
    std::uint32_t unused_2C;
    float speed_;                   // prototype +0x20 (missile speed)
    float range_;                   // prototype +0x24
    std::uint32_t targetCount_;     // prototype +0x34
    std::uint32_t targetFlags_;     // low 16 bits: target restrictions (see CanAffect)
    float manaCost_;                // prototype +0x18
    std::uint32_t complexity_;      // prototype +0x1C, +1 per modifier
    float price_;                   // prototype +0x0C
    std::uint32_t prototypeField14_;  // prototype +0x3C, only loaded and saved
    std::uint32_t skillNeeded_;     // prototype +0x44 plus the modifiers' +0x18
    std::int8_t modifiers_[8];      // spell modifier index + 1 each, 0 ends the list
};
static_assert(offsetof(CSpell, prototypeIndex_) == 0x1C);
static_assert(offsetof(CSpell, targetFlags_) == 0x3C);
static_assert(sizeof(CSpell) == 0x5C);

// The damage a hit deals, filled by the spell effects and the weapon code. 0x3C bytes.
struct DamageInfo {
    float typeWeights[7];       // one weight per damage type
    std::uint32_t weaponValue;  // the weapon's +0x4C (0 unarmed); not read here
    std::uint32_t attackRoll;   // float bits against the defence; FLT_MAX for spells
    float amount;
    float multiplier;
    int location;               // the body part aimed at; 6 (all) for spells
    int hitKind;                // 0/1 by spell; non-zero: no hit direction (OnDamaged)
    float backstabFactor;       // above 1.0 for a backstab, else 1.0
    std::uint8_t calledShot;
    std::uint8_t unused_39[3];
};
static_assert(sizeof(DamageInfo) == 0x3C);

// A spell being applied (created by CWorldServer::CastSpell): the world server's missiles (ProjectileList,
// world.h). It embeds the CSpell it applies (Spell()): the effect reads the spell's packed id, effect record,
// damage, radius (area), missile speed and duration (also the number of targets the damage is split between,
// and the lasting effects' ticks). A counted object (CRefCounted's virtuals).
class CSpellEffect final : public CRefCounted {
public:
    CSpellEffect();
    ~CSpellEffect() override;

    // Fills `out` with the damage this effect deals; split between the targets when there are several.
    BOOL GetDamage(DamageInfo* out);

    // Delivers `hit` to `unit` (CUnit's hit virtual) when the spell may affect it (no caster, or
    // CSpell::CanAffect). The unit's virtual gets (hit, attacker, caster); attacker is the caster unless flags
    // bit 0 is set.
    void HitUnit(DamageInfo* hit, CEngineObject* unit);

    // Delivers this effect's damage to every unit within `radius` of `position` that
    // IsSpellTarget accepts and the spell may affect.
    void HitUnitsInRadius();

    // The same for the units within `radius` that lie within 1 of the vertical plane
    // through `position` along `direction` (only x and y are used): the wall spells.
    void HitUnitsAlong(const Vector3* direction);

    // spell_effect_start.cpp: the casting state (state 0) and the effect objects it creates. Starts spell
    // `type` (the prototype index): creates its sounds, particles, lights and lightning, delivers the instant
    // hits, and sets the next state (1 missile, 2 lasting, 3 done; some failed casts leave it at 0) and the
    // timer. Ends with UpdateFootprint(type, 1).
    void Start(int type);
    // Replaces the visual: the old one leaves the world's timed objects and is
    // released, the new one is counted and added to them.
    void SetVisual(CEffect* visual);
    // The walls (6 fire wall, 7 light wall) and the area clouds (8, 0x27) block the AI
    // map while they last: builds the footprint and stamps it (`stamp`) or clears it. Nothing
    // without a visual or for other types.
    void UpdateFootprint(int type, int stamp);
    // A missile (CEffectArrow, class 0x4E) from `from` at `target` with the flight
    // sound `sound` and a light of `color` / `range`; it becomes the visual.
    void CreateArrow(const Vector3* from, CEngineObject* target, int particleType, float power, const char* sound,
                     const Vector3* color, float range);
    // A particle source (class 0x4A; types 0x2010 and 0x2014 are walls, class 0x5A),
    // optionally attached to `attachTo`. `keep`: it becomes the visual, else it is only added to
    // the world's timed objects. Returns it.
    CEffectParticle* CreateParticle(int keep, int particleType, const Vector3* position, float size, float alpha,
                                    const Vector3* velocity, CEngineObject* attachTo);
    // A lightning bolt (class 0x4D) from `from` to `target` (nothing without a target);
    // it becomes the visual. `flag` is a byte.
    void CreateLightning(const Vector3* from, CMapObject* target, CEngineObject* source, float power, int flag);
    // A positional sound (class 0x48). `keep`: it becomes the sound.
    void CreateSound(int keep, const char* name, const Vector3* position, const Vector3* velocity, float volume,
                     float radius);
    // A sound sequence (class 99): `first`, then `pattern` itself (count < 1) or
    // `pattern` formatted with 1..count. It becomes the sound.
    void CreateSoundSequence(const Vector3* position, const Vector3* velocity, const char* first,
                             const char* pattern, int count, float volume, float radius);
    // A point light (class 0x4B, light flags 0x840); it becomes the light.
    void CreatePointLight(const Vector3* position, const Vector3* color, float range, const Vector3* velocity);

    // spell_effect_update.cpp: the later states.
    // One tick: state 0 Start, 1 UpdateMissile, 2 UpdateLasting (all with the spell's
    // prototype index); any other state clears the footprint and drops the visual, sound and light
    // and returns 0 (the world then removes the effect). 0 also without a world; else 1.
    int Update();
    // State 1: the fire arrow (0, 0x28) and acid ray (2) hit their target when the
    // missile reports a hit; the fireball (3) explodes when its timer runs out.
    void UpdateMissile(int type);
    // State 2: the lasting effects (walls and clouds hit every 4th tick, fireworks scare,
    // clairvoyance and possession reveal, teleport moves the caster, link and vision fog end when
    // their units do); every case counts the timer down and ends the effect (state 3).
    void UpdateLasting(int type);
    // Drop the visual / sound / light (leave the timed objects, release).
    void ReleaseVisual();
    void ReleaseSound();
    void ReleaseLight();

    // A cast begins: `spell` copied into the embedded spell, the world, the position, the caster and the
    // target (each counted; the one held before released), the target point, state 0 and the trailing byte.
    void Init(CWorldServer* world, CSpell* spell, CUnit* caster, const Vector3* position, CMapObject* target,
              const Vector3* targetPoint, std::uint8_t flagA4);
    // `object` leaves the world: 0 when it is the target (the world then drops the effect); else 1, the
    // caster released and cleared when it is `object`.
    BOOL Forget(CEngineObject* object);

    // Save (spell_effect_save.cpp): the position, caster and target ids, the target point, the state, the
    // spell (its Save), the packed id, the visual / sound / light ids (-1 for none), the timer, the flags
    // and the position again. Load reads them back in the world of g_loadingProjectileWorld (world.h),
    // finding the three objects in its timed object manager, and stamps the footprint
    // (UpdateFootprint(type, 1)).
    void Load(CStream* stream);
    void Save(CStream* stream);

    CWorldServer* world;
    std::uint32_t flags;        // bit 0: the caster gets no credit for the hit
    Vector3 position;
    AIRefTo<CMapObject> target; // a unit or another map object (counted)
    AIRefTo<CUnit> caster;      // the casting unit (counted; null for traps and scripts)
    Vector3 targetPoint;
    int state;                  // 0 casting, 1 missile, 2 lasting, else ends
    // The CSpell (Spell()), constructed and destroyed in the constructor's and destructor's order.
    lib::AbstractInPlace<CSpell> spell_;
    CEffect* visual;            // particle source, wall, lightning or missile (counted; Visual<T>)
    CEffect* sound;             // a sound or a sound sequence (counted)
    CEffectPointLight* light;   // (counted)
    std::uint8_t footprintStamped;
    std::uint8_t unused_9D[3];
    int timer;                  // ticks left in the next state
    std::uint8_t silentEnd;     // copied into the buff record; healing: skip when at full health
    std::uint8_t unused_A5[3];

    // The visual as the effect class its spell type creates (CEffectParticle, CEffectWall,
    // CEffectLightning, CEffectMissile).
    template <class T> T* Visual() const { return static_cast<T*>(visual); }

    CSpell* Spell() { return spell_.Get(); }
    const CSpell* Spell() const { return spell_.Get(); }
};
static_assert(offsetof(CSpellEffect, world) == 0x08);
static_assert(offsetof(CSpellEffect, caster) == 0x20);
static_assert(offsetof(CSpellEffect, state) == 0x30);
static_assert(offsetof(CSpellEffect, spell_) == 0x34);
static_assert(offsetof(CSpellEffect, visual) == 0x90);
static_assert(offsetof(CSpellEffect, timer) == 0xA0);
static_assert(sizeof(CSpellEffect) == 0xA8);

// Whether area spells hit `unit`: units with a size class, and those whose prototype's +0x24 is
// below 0.5 (or NaN).
int __fastcall IsSpellTarget(CUnit* unit);

// The STLport list<SpellModifierRecord*> of modifier records a container collects (the list
// object is the pointer to its sentinel) and its node (next, prev, the record).
using SpellModifierList = stlp::list<itemdb::SpellModifierRecord*>;
using SpellModifierNode = SpellModifierList::Node;

// A spell under construction ("CSpellContainer", class id 0x1A, created by the spell manager as class 0x3003):
// a prototype, the modifiers added so far, and the CSpell that Build computes from them.
class CSpellContainer : public CEngineObject {
public:
    // (spell_manager.cpp) Destruction order: the list emptied, the spell's Destroy, the string, the list's
    // sentinel; the name and the spell after it, where the game destroys them before the string.
    CSpellContainer();
    ~CSpellContainer() override;

    // Forgets the prototype and the modifiers and clears the spell (its Create).
    int Create() override;
    // ClearState, then the same as Create but the spell gets its Destroy.
    void Destroy() override;
    const char* GetName() override;
    const char* GetClassName() override;
    int GetClassId() override;             // class id 0x1A

    // Appends `modifier` to the list. Null is a fatal error (its result is returned). Returns 1.
    BOOL AddModifier(itemdb::SpellModifierRecord* modifier);

    // Whether the prototype accepts a modifier of `modifier`'s kind (its first code letter: a, d, e, r, t read
    // the prototype's flag bytes; others are accepted). 0 without a prototype or modifier.
    int AllowsModifier(const itemdb::SpellModifierRecord* modifier);

    // Computes the spell from the prototype and the modifiers: area (a), effect (e), mana (m), range (r) scale
    // with the modifier's strength, duration (d) and t add integers, f* add target flags and ic/it cast
    // flags; every modifier adds its price, mana and level. The area becomes a radius (sqrt(area / pi)); the
    // modifier list is sorted. Returns 1.
    BOOL Build();

    CSpell* Spell() { return &spell_; }

    const itemdb::SpellRecord* prototype_;
    SpellModifierList modifiers_;
    char* codes_;                   // STLport string: modifier index + 1 per modifier
    char* codesEnd_;
    char* codesCapacity_;
    CSpell spell_;                  // (Spell())
    mfc::CString name_;             // (always empty)
};

// The spell manager's "sequences" (keyed by the high half of a packed spell id): containers keyed by the low
// half, and the largest low half asked for. Nothing in the game fills them, so only CreateSpellObject with an
// id whose low half is not 0 reaches them (and then fails).
struct SpellSequence {
    // The destructor body: every container released (the walk stops at the first entry without one), then
    // the table freed.
    void Destruct();

    stlp::hash_map<std::uint32_t, CSpellContainer*, stlp::IntHash> containers;
    std::uint32_t maxLow;
};
static_assert(sizeof(SpellSequence) == 0x18);
// A walk over a sequence's containers one step on: the container there, or null at the end (and when the
// walk had ended already).
using SpellContainerWalk = stlp::hash_map<std::uint32_t, CSpellContainer*, stlp::IntHash>::Iterator;

// The spell manager ("CSpellManager"). Only what the construction code uses; context_ gives the spell constructor.
class CSpellManager : public CResourceManager {
public:
    CSpellManager();
    ~CSpellManager() override;
    const char* GetClassName() override;
    int GetClassId() override;               // class id 0x33
    const char* GetFolderName() override;    // "spells"

    // A new CSpellContainer (manager's creator with class 0x3003, key -1).
    CSpellContainer* NewContainer();

    // spell_manager.cpp. A new CSpell (class 0x3002, key -1). For an id other than -1 the spell takes it as
    // its packed id: low half 0 initialises it from prototype (id >> 16) (null when out of range);
    // otherwise the sequence of the high half gives the container of the low half to copy the spell from
    // (FatalError without a sequence; a new entry has no container and faults).
    CSpell* CreateSpellObject(int id);
    int Create() override;
    // Releases the containers of every sequence (each walk stops at the first entry
    // without one), deletes the sequences (their tables' bucket arrays leak), empties the map, then
    // CResourceManager::Destroy.
    void Destroy() override;
    // A new CSpell (0x3002) or CSpellContainer (0x3003), constructed; else null.
    CEngineObject* NewObject(int type) override;

    // Builds a spell from `prototype` and up to 8 modifier records of `modifiers` (an
    // STLport list<char*>, passed as a pointer to its sentinel pointer) through the context's
    // spell constructor. Returns the new spell, or a fatal error's result.
    CSpell* CreateSpell(const itemdb::SpellRecord* prototype, const SpellModifierList* modifiers);

    // Builds a spell from text such as "healing{e2;e2}": the trimmed text before '{' is
    // the prototype name, the ';'-separated names inside the braces are modifiers (empty ones
    // skipped). Names are looked up in the name maps with operator[] / find_or_insert, so an
    // unknown name is added to the map with a null record. Returns the new spell, or a fatal
    // error's result.
    CSpell* CreateSpellFromText(const char* text);

    // `names` resized to the spell / spell modifier record count and filled with the records' names. `this` is
    // not used.
    void ListSpellNames(mfc::CStringArray* names);
    void ListModifierNames(mfc::CStringArray* names);

    // Sequences by the high half of a packed id (keys with low half 0).
    stlp::hash_map<std::uint32_t, SpellSequence*, stlp::IntHash> sequences_;
    std::uint32_t maxSequenceKey_;   // the largest key asked for
};
static_assert(offsetof(CSpellManager, sequences_) == 0xBC);
static_assert(sizeof(CSpellManager) == 0xD4);

// The spell constructor ("CSpellConstructor", class id 0x1B).
class CSpellConstructor : public CEngineObject {
public:
    CSpellConstructor();
    ~CSpellConstructor() override;

    // Releases the container and forgets the manager. Returns 1.
    int Create() override;
    // The same, also dropping the manager's count (+4).
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;             // class id 0x1B

    // Releases the container and counts a reference to `manager` (+4, no release).
    void SetManager(CSpellManager* manager);

    // Starts a spell from `prototype` in a new container. Returns 1, or a fatal error's
    // result without a manager or prototype.
    BOOL BeginConstruct(const itemdb::SpellRecord* prototype);

    // Builds the container's spell. Returns 1, or a fatal error's result.
    BOOL EndConstruct();

    // Adds `modifier` and rebuilds when the prototype accepts it. Returns 0/1.
    BOOL AddModifier(itemdb::SpellModifierRecord* modifier);


    CSpellManager* manager_;        // counted (+4) but never released
    CSpellContainer* container_;
};

// A light ("CLight", class id 10). type_ 0x31 is a point light. A transform object: localPosition_ is the
// light's position (a point light's, and where a directional light is drawn from); worldPosition_ holds the
// position in the space of the mesh being lit (the mesh drawer writes it), localRotation_ / worldRotation_
// start identity.
class CLight : public CTransformObject {
public:
    // Brightness (luminance of the colour) at `point`, falling off with the squared
    // distance for point lights.
    float GetIntensityAt(const Vector3* point);

    // Scales two colours of `source` by the light's colour and `scale` into litColors_.
    void SetLitColors(const float* source, float scale);

    // Constructor (light.cpp): the base fields, identity rotations, everything zero but two 1.0 fields, the
    // range sqrt(FLT_MAX) with its square and inverse.
    CLight();
    ~CLight() override;
    int Create() override;                  // returns 1
    void Destroy() override;                // flags_ bit 5 cleared
    const char* GetClassName() override;
    int GetClassId() override;              // class id 10
    // A point light (kind 1) of `color` at `position` (also kept);
    // flags_ bit 3 (changed). Returns 1.
    BOOL SetPoint(const Vector3* color, const Vector3* position);
    // A directional light (kind 3) of `color` along `direction`, which the light
    // also tilts towards (CTransformObject::TiltTowards); flags_ bit 3. Returns 1.
    BOOL SetDirectional(const Vector3* color, const Vector3* direction);
    // light.cpp; the terrain's vertex lighting. A colour (r, g, b in x, y, z) lit by the light: `base` plus
    // the light's colour times `scale` and a factor, each channel clamped at 1; w is not written then (an
    // unset local is stored, which the callers do not read). Unlit, `out` is `base` (all four). The factor
    // is minus the cosine between `normal` and direction_; unlit when it is not above 0. The factor is 1 -
    // distance^2 / range^2 (unlit below 0), times 1 + the cosine between `normal` and `toLight` when that
    // is not above 0.
    void AddDirectional(Vector4* out, float scale, const Vector4* base, const Vector3* normal) const;
    void AddPoint(Vector4* out, float scale, const Vector4* base, const Vector3* normal, const Vector3* toLight,
                  float distance) const;

    std::uint8_t unused_54[0x94 - 0x54];
    int kind_;                      // 1 point, 3 directional
    float color_[3];                // r, g, b
    std::uint8_t unused_A4[0xC8 - 0xA4];
    Vector3 pointPosition_;         // SetPoint's position
    Vector3 direction_;             // a directional light's (the sun's) direction
    float range_;
    float unused_E4;               // 1.0 from the constructor
    float unused_E8;               // 1.0 from the constructor
    std::uint8_t unused_EC[0xFC - 0xEC];
    std::uint32_t unused_FC;
    float rangeSquared_;
    float inverseRangeSquared_;
    Vector3 localDirection_;        // direction_ in the space of the mesh being lit
    float litColors_[2][4];
};
static_assert(offsetof(CLight, kind_) == 0x94 && offsetof(CLight, pointPosition_) == 0xC8 &&
              offsetof(CLight, unused_FC) == 0xFC && sizeof(CLight) == 0x134);
static_assert(offsetof(CLight, color_) == 0x98);
static_assert(offsetof(CLight, rangeSquared_) == 0x100);
static_assert(offsetof(CLight, litColors_) == 0x114);
static_assert(offsetof(CLight, range_) == 0xE0);
static_assert(offsetof(CLight, unused_54) == 0x54 && offsetof(CLight, direction_) == 0xD4 &&
              offsetof(CLight, localDirection_) == 0x108);


// The light factory (GetClassName "CLightManager", class id 0x29): CreateObject makes lights by class (0x2E:
// the renderer's default light; 0x31: point lights). Its list holds the ids of the lights held on (light.cpp).
class CLightFactory : public CBaseManager {
public:
    CLightFactory();
    ~CLightFactory() override;
    const char* GetClassName() override;
    int GetClassId() override;               // class id 0x29

    // A new CLight of type 0x2E or 0x31 (its type_); other types are
    // logged and give null.
    CEngineObject* NewObject(int type) override;
    // CBaseManager::CreateObject, then flags_ bit 7 cleared (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    // The light registered under `id` (every object the factory makes is a CLight), or null.
    CLight* LightWithId(unsigned id) const { return FindObjectAs<CLight>(id); }
    CLight* CreateLight(int type, int id = -1) { return CreateObjectAs<CLight>(type, id); }

    std::uint32_t unused_B0[4];
    CEngineObject* held_;           // released by the destructor
    std::uint32_t unused_C4[2];
    std::uint32_t unused_CC;
    std::uint32_t unused_D0;       // 1
    stlp::list<int> litIds_;
};
static_assert(offsetof(CLightFactory, held_) == 0xC0 && offsetof(CLightFactory, litIds_) == 0xD4 &&
              sizeof(CLightFactory) == 0xD8);

// The code of the spell prototype whose name matches (_mbsicmp); null if none.
char* __stdcall SpellCodeByName(const char* name);

// Address of the code of the spell modifier whose name matches; null if none.
char* __stdcall SpellModifierCodeByName(const char* name);
