#pragma once

// Spells: prototypes in the spell database (item_db.h), CSpell instances built from them plus up to eight
// modifiers, and the spell effects that turn them into damage.

#include "base_manager.h"
#include "core_refcount.h"
#include "core_transform.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "item_db.h"
#include "maths.h"
#include "resource_manager.h"

#include "lib/bit_flags.h"

#include <list>
#include <string>
#include <unordered_map>
#include <vector>

class CStream;
class CWorldServer;
class CUnit;
class CMapObject;
struct MapEffect;
class CEffect;
class CEffectParticle;
class CEffectPointLight;

// The spell types: the spell prototype index (the order of the spell database in database.res, CSpell's
// prototypeId_ >> 16), also an active effect's type (MapEffect::type: a buff copies its spell's). Names from the
// prototypes' names (the code name where it differs), the sound folders under magic\ and the Start* functions. The
// 1.06 database and Rebirth's both have all 42. Shapechange and Comprehend start nothing; Teleport's record on the
// caster stuns it like Stun's (the client draws no particles for it). MapEffect::Type() reads the byte.
enum class SpellType : int {
    FireArrow = 0x00,               // "arrow"
    Lightning = 0x01,               // "Lightning Arc"
    AcidRay = 0x02,
    Fireball = 0x03,
    InvokeLightning = 0x04,         // "inv_lit": the light strike
    AcidColumn = 0x05,
    FireWall = 0x06,
    LightningWall = 0x07,           // "litnwall" ("Smtg Lightning"): the light wall
    AcidFog = 0x08,                 // the poison cloud
    ProtectionFromFire = 0x09,      // "prot_fire", sounds in magic\Fireshield
    ProtectionFromLightning = 0x0A, // "prot_electro", magic\Lightsheild
    ProtectionFromAcid = 0x0B,      // "prot_acid", magic\PoisonShield
    EagleSight = 0x0C,
    Infravision = 0x0D,
    DetectLife = 0x0E,
    Invisibility = 0x0F,
    Silence = 0x10,
    Lichdom = 0x11,
    Fireworks = 0x12,
    Stench = 0x13,
    Clairvoyance = 0x14,            // "clairvoyence"
    Possession = 0x15,
    Shapechange = 0x16,
    VisionFog = 0x17,
    Healing = 0x18,
    Stun = 0x19,
    Antimagic = 0x1A,
    Link = 0x1B,
    Teleport = 0x1C,
    Strengthen = 0x1D,              // "strength"
    Weaken = 0x1E,                  // "weak"
    Regeneration = 0x1F,
    Feeblemind = 0x20,
    Speed = 0x21,
    Slow = 0x22,
    Comprehend = 0x23,
    Charm = 0x24,
    Enlarge = 0x25,
    Shrink = 0x26,
    Campfire = 0x27,                // StartAtGround: the effect stands at the ground point
    RickMagic = 0x28,               // "rick_magic", school fire: a fire arrow with the RikArrowSmoke trail
    CurseMagic = 0x29,              // "curse_magic", school lightning: lightning (CreateLightning's flag 1)
};

// The bits of CSpell::targetFlags_ (saved as the number). The low 16 bits restrict the targets (CanAffect; the
// modifiers "f?" set them; a spell with none and one target is a single missile); "it" / "ic" (the cast-flag
// modifiers, HasCastFlagModifier) say when an item casts it; the prototype's targetKind sets the aim. The races
// are the unit prototypes' names (spell.cpp; the goblin's reading is provisional).
enum class SpellTargetFlag : std::uint32_t {
    FriendsOnly = 0x1,          // "ff": not at a unit either side regards as hostile
    EnemiesOnly = 0x2,          // "fe"
    NoHumans = 0x4,             // "fh": not at "unhuma" / "unhufe"
    NoGoblins = 0x8,            // "fg": not at "unmogo"
    NoOrcs = 0x10,              // "fo": not at "unorma" / "unorfe"
    NoMoli = 0x20,              // "fl": not at "unmoli"
    ItemTriggered = 0x10000,    // "it": a weapon casts it at what it hits, armour at its wearer when hit (healing:
                                // when not)
    ItemContinuous = 0x20000,   // "ic": an item's lasting spell while it is worn (CUnitServer::RefreshItemSpells)
    AtUnits = 0x10000000,       // targetKind 'u'
    AtPoint = 0x20000000,       // targetKind 't': aimed at a point; the missile flies at no object
};
template <> inline constexpr bool lib::kBitFlags<SpellTargetFlag> = true;
inline constexpr std::uint32_t kSpellTargetRestrictions = 0xFFFF;   // the low 16 bits

// A spell instance ("CSpell", class id 0x4F). prototypeId_ of CEngineObject is the packed id: spell
// prototype index << 16.
class CSpell : public CEngineObject {
public:
    CSpell();
    ~CSpell() override;

    void Create() override;
    void Destroy() override;
    // The effect record, the parameters, the modifiers, flags_ and the last field. Load clears the modifiers
    // first.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void InitFromPrototype(const itemdb::SpellRecord* prototype);

    // Null if the index is out of range.
    itemdb::SpellRecord* GetPrototype();

    // The prototype index of the packed id.
    SpellType Type() const { return static_cast<SpellType>(prototypeId_ >> 16); }

    // At most 8.
    int GetModifierCount();

    // "ic" or "it": the modifiers that set cast flags 0x20000 and 0x10000 when the spell is constructed.
    bool HasCastFlagModifier();

    // The elemental damage type the spell deals (3, 4 or 5), 6 for none.
    int GetDamageType();

    // True for the attack spells (FireArrow to AcidFog, Lichdom, Stench, Stun, Weaken, Feeblemind, Slow, Charm,
    // Shrink; with Rebirth's FixSpellSelfCasting also RickMagic and CurseMagic).
    bool IsOffensive();

    // By the target flags in the low 16 bits of flags_; either may be null.
    bool CanAffect(const CUnit* caster, CMapObject* target) const;

    // `target`'s Destroy runs first.
    void CopyTo(CSpell* target);

    CSpell* Clone(class CSpellManager* manager);

    // The spell's first fields are an active effect record (map_object.h), which a buff copies: its type, power,
    // ticks (duration_), value (damage_) and strength (radius_). Effect() builds the record, SetEffect() stores one
    // (its `ending` is not kept).
    MapEffect Effect() const;
    void SetEffect(const MapEffect& record);

    std::uint8_t prototypeIndex_;
    std::uint8_t effectPower_;      // the effect record's power, set to 0x10
    std::uint32_t duration_;
    float damage_;
    float radius_;                  // the prototype's area; Build makes it a radius
    float speed_;                   // missile speed
    float range_;
    std::uint32_t targetCount_;
    std::uint32_t targetFlags_;     // SpellTargetFlag bits
    float manaCost_;
    std::uint32_t complexity_;      // +1 per modifier
    float price_;
    std::uint32_t castTime_;  // only loaded and saved
    std::uint32_t skillNeeded_;     // the prototype's plus the modifiers'
    std::int8_t modifiers_[8];      // spell modifier index + 1 each, 0 ends the list
};

// The damage a hit deals, filled by the spell effects and the weapon code.
struct DamageInfo {
    float typeWeights[7];       // one weight per damage type
    std::uint32_t weaponValue;  // 0 unarmed; not read here
    float attackRoll;           // against the defence; FLT_MAX for spells
    float amount;
    float multiplier;
    int location;               // the body part aimed at; 6 (all) for spells
    int hitKind;                // 0/1 by spell; non-zero: no hit direction (OnDamaged)
    float backstabFactor;       // above 1.0 for a backstab, else 1.0
    std::uint8_t calledShot;
    std::uint8_t unused_39[3];
};
static_assert(sizeof(DamageInfo) == 0x3C);

// A spell being applied (created by CWorldServer::CastSpell): the world server's missiles. It embeds the
// CSpell it applies; the spell's duration is also the number of targets the damage is split between, and the
// lasting effects' ticks.
class CSpellEffect final : public CRefCounted {
public:
    CSpellEffect();
    ~CSpellEffect() override;

    // Split between the targets when there are several.
    void GetDamage(DamageInfo* out);

    // When the spell may affect `unit` (no caster, or CSpell::CanAffect). The attacker is the caster unless
    // flags bit 0 is set.
    void HitUnit(DamageInfo* hit, CMapObject* unit);

    // Every unit within `radius` of `position` that IsSpellTarget accepts and the spell may affect.
    void HitUnitsInRadius();

    // The units within `radius` that lie within 1 of the vertical plane through `position` along
    // `direction` (only x and y are used): the wall spells.
    void HitUnitsAlong(const Vector3* direction);

    // The casting state of spell `type` (the prototype index): its effect objects, the instant hits, the
    // next state (1 missile, 2 lasting, 3 done; some failed casts leave it at 0) and the timer.
    void Start(SpellType type);
    // The old visual leaves the world's timed objects and is released.
    void SetVisual(CEffect* visual);
    // The walls (FireWall, LightningWall) and the areas (AcidFog, Campfire) block the AI map while they last.
    // Nothing without a visual or for other types.
    void UpdateFootprint(SpellType type, int stamp);
    // It becomes the visual.
    void CreateArrow(const Vector3* from, CEngineObject* target, int particleType, float power, const char* sound,
                     const Vector3* color, float range);
    // Types 0x2010 and 0x2014 are walls. `keep`: it becomes the visual, else it is only added to the world's
    // timed objects.
    CEffectParticle* CreateParticle(int keep, int particleType, const Vector3* position, float size, float alpha,
                                    const Vector3* velocity, CEngineObject* attachTo);
    // Nothing without a target; it becomes the visual.
    void CreateLightning(const Vector3* from, CMapObject* target, CEngineObject* source, float power, int flag);
    // A one-shot sound effect (the effect does not keep it).
    void CreateSound(const char* name, const Vector3* position, const Vector3* velocity, float volume, float radius);
    // `first`, then `pattern` itself (count < 1) or `pattern` formatted with 1..count. It becomes the sound.
    void CreateSoundSequence(const Vector3* position, const Vector3* velocity, const char* first,
                             const char* pattern, int count);
    // Light flags Specular | Strong; it becomes the light.
    void CreatePointLight(const Vector3* position, const Vector3* color, float range, const Vector3* velocity);

    // One tick. Returns 0 when the effect has ended (the world then removes it) or without a world.
    int Update();
    // State 1: the fire arrow (FireArrow, RickMagic) and the acid ray hit their target when the missile reports a
    // hit; the fireball explodes when its timer runs out.
    void UpdateMissile(SpellType type);
    // State 2: walls and clouds hit every 4th tick; every case counts the timer down and ends the effect.
    void UpdateLasting(SpellType type);
    void ReleaseVisual();
    void ReleaseSound();
    void ReleaseLight();

    void Init(CWorldServer* world, CSpell* spell, CUnit* caster, const Vector3* position, CMapObject* target,
              const Vector3* targetPoint, std::uint8_t newSilentEnd);
    // 0 when `object` is the target (the world then drops the effect).
    bool Forget(CEngineObject* object);

    // The position, caster and target ids, the target point, the state, the spell, the packed id, the visual
    // / sound / light ids (-1 for none), the timer, the flags and the position again. Load finds the objects
    // in g_loadingProjectileWorld and stamps the footprint.
    void Load(CStream* stream);
    void Save(CStream* stream);

    CWorldServer* world = nullptr;
    std::uint32_t flags = 0;    // bit 0: the caster gets no credit for the hit
    Vector3 position{};
    Ref<CMapObject> target; // a unit or another map object
    Ref<CUnit> caster;      // null for traps and scripts
    Vector3 targetPoint;
    int state;                  // 0 casting, 1 missile, 2 lasting, else ends
    CSpell spell_;
    CEffect* visual = nullptr;            // particle source, wall, lightning or missile (counted)
    CEffect* sound = nullptr;             // a sound or a sound sequence (counted)
    CEffectPointLight* light = nullptr;   // counted
    std::uint8_t footprintStamped = 0;
    int timer = 0;                        // ticks left in the next state
    std::uint8_t silentEnd = 0;           // copied into the buff record; healing: skip when at full health

    // The visual as the effect class its spell type creates.
    template <class T> T* Visual() const { return static_cast<T*>(visual); }

    CSpell* Spell() { return &spell_; }
    const CSpell* Spell() const { return &spell_; }
};

// Whether area spells hit `unit`: units with a size class, and those whose prototype's height is below 0.5
// (or NaN).
bool IsSpellTarget(CUnit* unit);

// A spell under construction (created by the spell manager as SpellObjectType::Container): a prototype, the modifiers added
// so far, and the CSpell that Build computes from them.
class CSpellContainer : public CEngineObject {
public:
    // The name and the spell are destroyed after the string, where the game destroys them before it.
    CSpellContainer();
    ~CSpellContainer() override;

    void Create() override;
    // As Create, but the spell gets its Destroy.
    void Destroy() override;
    const char* GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Null is a fatal error (its result is returned). Returns 1.
    bool AddModifier(itemdb::SpellModifierRecord* modifier);

    // By the modifier's first code letter: a, d, e, r, t read the prototype's flag bytes; others are
    // accepted. 0 without a prototype or modifier.
    bool AllowsModifier(const itemdb::SpellModifierRecord* modifier);

    // Area (a), effect (e), mana (m), range (r) scale with the modifier's strength, duration (d) and t add
    // integers, f* add target flags and ic/it cast flags; every modifier adds its price, mana and level. The
    // area becomes a radius (sqrt(area / pi)); the modifier list is sorted.
    void Build();


    const itemdb::SpellRecord* prototype_;
    std::list<itemdb::SpellModifierRecord*> modifiers_;
    std::string codes_;             // modifier index + 1 per modifier
    CSpell spell_;
    std::string name_;             // always empty
};

// The spell manager's "sequences" (keyed by the high half of a packed spell id): containers keyed by the low
// half. Nothing in the game fills them, so only CreateSpellObject with an id whose low half is not 0 reaches
// them (and then fails).
struct SpellSequence {
    // The containers released; the walk stops at the first entry without one.
    ~SpellSequence();

    std::unordered_map<std::uint32_t, CSpellContainer*> containers;   // only empty entries ever: no order to keep
    std::uint32_t maxLow;
};

// The spell manager's object types (CreateObject / type_).
enum class SpellObjectType : int {
    Spell = 0x3002,      // CSpell
    Container = 0x3003,  // CSpellContainer
};

// The spell manager; context_ gives the spell constructor.
class CSpellManager : public CResourceManager {
public:
    CSpellManager();
    ~CSpellManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    const char* GetFolderName() override;    // "spells"

    CSpellContainer* NewContainer();

    // For an id other than -1 the spell takes it as its packed id: low half 0 initialises it from prototype
    // (id >> 16) (null when out of range); otherwise the sequence of the high half gives the container to
    // copy the spell from (FatalError without a sequence; a new entry has no container and faults).
    CSpell* CreateSpellObject(int id);
    // The sequences' tables' bucket arrays leak.
    void Destroy() override;
    // A CSpell (0x3002) or CSpellContainer (0x3003); else null.
    CEngineObject* NewObject(int type) override;

    // Up to 8 modifiers, through the context's spell constructor. Returns the new spell, or a fatal error's
    // result.
    CSpell* CreateSpell(const itemdb::SpellRecord* prototype, const std::list<itemdb::SpellModifierRecord*>* modifiers);

    // Text such as "healing{e2;e2}": the trimmed text before '{' is the prototype name, the ';'-separated
    // names inside the braces are modifiers (empty ones skipped). An unknown name is added to the name map
    // with a null record. Returns the new spell, or a fatal error's result.
    CSpell* CreateSpellFromText(std::string_view text);

    // `this` is not used.
    void ListSpellNames(std::vector<std::string>* names);
    void ListModifierNames(std::vector<std::string>* names);

    // By the high half of a packed id. Its entries are only the null ones CreateSpellObject's lookup adds, so the
    // destructor's walk (which stops at a null entry) deletes nothing in any order.
    std::unordered_map<std::uint32_t, SpellSequence*> sequences_;
    std::uint32_t maxSequenceKey_;   // the largest key asked for
};

class CSpellConstructor : public CEngineObject {
public:
    CSpellConstructor();
    ~CSpellConstructor() override;

    // Releases the container and forgets the manager. Returns 1.
    void Create() override;
    // Also drops the manager's count.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Counts a reference to `manager` that is never released.
    void SetManager(CSpellManager* manager);

    // Returns 1, or a fatal error's result without a manager or prototype.
    bool BeginConstruct(const itemdb::SpellRecord* prototype);

    // Returns 1, or a fatal error's result.
    bool EndConstruct();

    // Rebuilds when the prototype accepts it.
    bool AddModifier(itemdb::SpellModifierRecord* modifier);

    CSpellManager* manager_;        // counted but never released
    Ref<CSpellContainer> container_;
};

// The bits of CEngineObject::flags_ on a light. A point light effect's lightFlags_ (saved) are ored in: the spells'
// and missiles' lights take Specular | Strong.
enum class LightFlag : std::uint32_t {
    Changed = 0x8,          // SetPoint / SetDirectional; nothing reads it
    Active = 0x20,          // cleared by Destroy; nothing sets or reads it
    Specular = 0x40,        // lights the terrain water's specular and the GPU's specular (a quest light, the spells')
    Hidden = 0x80,          // not lighting: the objects' and the terrain's lighting skip it (a torch's or an object's
                            // light turned off; the factory's CreateObject clears it)
    Moving = 0x100,         // a moving effect's light, an object's own light: an object it reaches is not drawn
                            // static
    Held = 0x200,           // held on by an object's light mode 1: hiding and showing leave it alone
    Off = 0x400,            // casts no shadow: a point light effect of negative range, an object's own light
    Strong = 0x800,         // counts ten times in a shadow's light direction
};
template <> inline constexpr bool lib::kBitFlags<LightFlag> = true;

// A light. type_ 0x31 is a point light. localPosition_ is the light's position (where a directional light is
// drawn from); worldPosition_ holds it in the space of the mesh being lit.
class CLight : public CTransformObject {
public:
    // Luminance of the colour, falling off with the squared distance for point lights.
    float GetIntensityAt(const Vector3* point);

    // The range is sqrt(FLT_MAX).
    CLight();
    ~CLight() override;
    void Create() override;
    void Destroy() override;                // LightFlag::Active cleared
    const char* ClassName() override;
    ClassId GetClassId() override;
    // Kind 1; LightFlag::Changed.
    void SetPoint(const Vector3* color, const Vector3* position);
    // Kind 3; the light also tilts towards `direction`; LightFlag::Changed.
    void SetDirectional(const Vector3* color, const Vector3* direction);
    // The terrain water layer's specular lighting: `base` plus the light's colour times `scale` and a factor,
    // each channel clamped at 1; w is then not written (the callers do not read it). Unlit, `out` is `base`. The
    // factor: 1 - distance^2 / range^2 (unlit below 0), times 1 + the cosine between `normal` and `toLight` when
    // that is not above 0.
    void AddPoint(Vector4* out, float scale, const Vector4* base, const Vector3* normal, const Vector3* toLight,
                  float distance) const;

    int kind_;                      // 1 point, 3 directional
    float color_[3];                // r, g, b
    Vector3 pointPosition_;         // SetPoint's position
    Vector3 direction_;             // a directional light's (the sun's) direction
    float range_;
    float rangeSquared_;
    float inverseRangeSquared_;
    Vector3 localDirection_;        // direction_ in the space of the mesh being lit
    float litColors_[2][4];
};

// The light factory ("CLightManager"): lights of class 0x2E (the renderer's default light) and 0x31 (point
// lights).
class CLightFactory : public CBaseManager {
public:
    CLightFactory();
    ~CLightFactory() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Other types are logged and give null.
    CEngineObject* NewObject(int type) override;
    // LightFlag::Hidden cleared (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    CLight* LightWithId(unsigned id) const { return FindObjectAs<CLight>(id); }
    CLight* CreateLight(int type, int id = -1) { return CreateObjectAs<CLight>(type, id); }

    CEngineObject* held_;           // released by the destructor
    std::list<int> litIds_;
};

// Case-insensitive; null if none.
char* SpellCodeByName(std::string_view name);
char* SpellModifierCodeByName(std::string_view name);
