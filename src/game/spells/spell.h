#pragma once

// Spells: prototypes in the spell database (spell_db.h), Spell instances built from them plus up to eight
// modifiers, and the spell effects that turn them into damage.

#include "engine/core/core_refcount.h"
#include "engine/core/engine_object.h"
#include "game/app/singletons.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "lib/vecmath.h"
#include "engine/data/resource_manager.h"

#include "lib/bit_flags.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class ByteStream;
class WorldServer;
class Unit;
class MapObject;
struct MapEffect;
class Effect;
class EffectArrow;
class EffectLightning;
class EffectParticle;
class EffectPointLight;
class EffectSoundSeq;
class ScriptSpellHost;
enum class ParticleType : int;

// The bits of Spell::targetFlags (saved as the number). The low 16 bits restrict the targets (CanAffect; the
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
    ItemContinuous = 0x20000,   // "ic": an item's lasting spell while it is worn (UnitServer::RefreshItemSpells)
    AtUnits = 0x10000000,       // targetKind 'u'
    AtPoint = 0x20000000,       // targetKind 't': aimed at a point; the missile flies at no object
};
template <> inline constexpr bool lib::kBitFlags<SpellTargetFlag> = true;
inline constexpr std::uint32_t kSpellTargetRestrictions = 0xFFFF;   // the low 16 bits

// The numbers of a spell that a copy takes along (Spell::CopyTo); the spell's first fields are an active effect
// record (map_object.h), which a buff copies: its type, power, ticks (duration), value (damage) and strength
// (radius).
struct SpellValues {
    std::uint8_t prototypeIndex = 0;
    std::uint8_t effectPower = 0;   // the effect record's power, set to 0x10
    std::uint32_t duration = 0;
    float damage = 0.0f;
    float radius = 0.0f;            // the prototype's area; Build makes it a radius
    float speed = 0.0f;             // missile speed
    float range = 0.0f;
    std::uint32_t targetCount = 0;
    std::uint32_t targetFlags = 0;  // SpellTargetFlag bits
    float manaCost = 0.0f;
    std::uint32_t complexity = 0;   // +1 per modifier
    float price = 0.0f;
    std::uint32_t castTime = 0;     // only loaded and saved
    std::uint32_t skillNeeded = 1;  // the prototype's plus the modifiers'
};

// A spell instance ("CSpell", class id 0x4F). prototypeId of EngineObject is the packed id: spell
// prototype index << 16.
class Spell : public EngineObject, public SpellValues {
public:
    Spell();
    ~Spell() override;

    void Create() override;
    void Destroy() override;
    // The effect record, the parameters, the modifiers, flags and the last field. Load clears the modifiers
    // first.
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void InitFromPrototype(const itemdb::SpellRecord* prototype);

    // The prototype record. Every spell has one and a record for each modifier: the spells come from the spell
    // constructor (records of the database), copies (CopyTo) and the loaders, which check what they load
    // (HasRecords; HasEveryRecord in item_manager.h), so the readers take the records unchecked.
    itemdb::SpellRecord* GetPrototype() const;
    // The prototype and every modifier name a record of the spell database; false for a spell of a save, a
    // party or a player made with another database.
    bool HasRecords();
    // Ends the game (game::FatalExit) without the records (HasRecords).
    void RequireRecords();

    // What it does: its record's behaviour (for the game's records their place, prototypeId >> 16).
    SpellType Behaviour() const { return itemdb::BehaviourAt(prototypeId >> 16); }
    // How the game's tables use it (itemdb::UseOf).
    itemdb::SpellUse Use() const { return itemdb::UseOf(*GetPrototype()); }

    // At most 8.
    int GetModifierCount();

    // "ic" or "it": the modifiers that set cast flags 0x20000 and 0x10000 when the spell is constructed.
    bool HasCastFlagModifier();

    // By the target flags in the low 16 bits of flags; either may be null.
    bool CanAffect(const Unit* caster, MapObject* target) const;

    // `target`'s Destroy runs first; then it takes the values, the modifiers up to the first 0, and the packed id.
    void CopyTo(Spell* target);

    Spell* Clone(class SpellManager* manager);

    // EffectRecord() builds the active effect record of the values' first fields, SetEffect() stores one (its
    // `ending` is not kept).
    MapEffect EffectRecord() const;
    void SetEffect(const MapEffect& record);

    std::array<std::int8_t, 8> modifiers{};     // spell modifier index + 1 each, 0 ends the list
};

// The damage a hit deals, filled by the spell effects and the weapon code.
struct DamageInfo {
    float typeWeights[7] = {};  // one weight per damage type
    float& Weight(DamageType type) { return typeWeights[static_cast<int>(type)]; }
    float Weight(DamageType type) const { return typeWeights[static_cast<int>(type)]; }
    std::uint32_t weaponValue = 0;  // 0 unarmed; not read here
    float attackRoll = 0.0f;    // against the defence; FLT_MAX for spells
    float amount = 0.0f;
    float multiplier = 0.0f;
    int location = 0;           // the body part aimed at; 6 (all) for spells
    int hitKind = 0;            // 0/1 by spell; non-zero: no hit direction (OnDamaged)
    float backstabFactor = 0.0f;  // above 1.0 for a backstab, else 1.0
    std::uint8_t calledShot = 0;
    std::uint8_t unused_39[3] = {};
};
constexpr bool NoPadding(const DamageInfo*) {
    return lib::FieldsFill<&DamageInfo::typeWeights, &DamageInfo::weaponValue, &DamageInfo::attackRoll,
                           &DamageInfo::amount, &DamageInfo::multiplier, &DamageInfo::location, &DamageInfo::hitKind,
                           &DamageInfo::backstabFactor, &DamageInfo::calledShot, &DamageInfo::unused_39>();
}

// Either unit regards the other's side as hostile (CanAffect's "ff" / "fe").
bool SpellHostile(const Unit* caster, const Unit* unit);
// Whether area spells hit `unit`: units with a size class, and those whose prototype's height is below 0.5 (or NaN).
bool AcceptsAreaSpells(Unit* unit);

// An effect object a scripted cast made (ScriptSpellHost's bindings): in the world's timed objects until it goes.
struct SpellOwned {
    Ref<Effect> object;
    int ticks = 0;      // the ticks it has left; 0: it lasts as long as the cast
    int payload = 0;    // a missile's key in its cast's missiles (its OnHit payload); 0 for the others
};
// The AI map's cells a cast blocks (SpellEffect::Block): a disc of `radius` around `at`, or a wall from at - along to
// at + along (AlongDirection: a half width of 0.5).
struct SpellFootprint {
    Vector3 at;
    Vector3 along;
    float radius = 0.0f;
    bool disc = false;
};
// A scripted cast's missile that arrived: its payload key, the object it flew at (-1: gone) and where it ended.
struct SpellArrival {
    int payload = 0;
    int targetId = -1;
    Vector3 point;
};

// A spell being applied (created by WorldServer::CastSpell): the world server's missiles. It embeds the
// Spell it applies; the spell's duration is also the number of targets the damage is split between, and the
// lasting effects' ticks.
class SpellEffect final : public RefCounted {
public:
    SpellEffect();
    ~SpellEffect() override;

    // A hit shared by `targets` (more than 1): its amount and multiplier x 4 / targets, as the area spells' split.
    static void SplitHit(DamageInfo* hit, int targets);
    // A spell's hit: no defence roll (attackRoll FLT_MAX), every body part (location 6), `amount` of `type` (its
    // weight 1); a non-zero `hitKind` has no direction (the walls and clouds burn so).
    static DamageInfo TypedHit(DamageType type, float amount, int hitKind);

    // When the spell may affect `unit` (no caster, or Spell::CanAffect). The attacker is the caster unless
    // flags bit 0 is set.
    void HitUnit(DamageInfo* hit, MapObject* unit);

    // The casting state of the behaviour `type` (Spell::Behaviour()): its effect objects, the instant hits, the
    // next state (2 lasting, 3 done, 4 scripted; some failed casts leave it at 0) and the timer.
    void Start(SpellType type);
    // The old visual leaves the world's timed objects and is released.
    void SetVisual(Effect* visual);
    // The AI map's cells of `shape` blocked (a scripted cast's cast:Block, as the walls and the poison cloud block
    // theirs while they stand), the footprint held before cleared first; Unblock clears the one held.
    void Block(const SpellFootprint& shape);
    void Unblock();
    // A particle effect of this type is a wall (EffectWall, with a direction): Tendril, Link.
    static bool WallParticle(int particleType);
    // A spell missile homing on `target` (its particle `particleType`, an original type or a data effect's id), not yet
    // in the world; with a `launcher` the client starts it at the launcher's effect point. Null when the manager makes
    // none.
    EffectArrow* NewArrow(const Vector3& from, EngineObject* target, int particleType, float size, const char* sound,
                          const Vector3& color, float range, Unit* launcher);
    // Types 0x2010 and 0x2014 are walls. `keep`: it becomes the visual, else it is only added to the world's
    // timed objects.
    EffectParticle* CreateParticle(int keep, ParticleType particleType, const Vector3& position, float size, float alpha,
                                    const Vector3& velocity, EngineObject* attachTo);
    // CreateParticle's object (`particleType` an original type or a data effect's id), not yet in the world.
    EffectParticle* NewParticle(int particleType, const Vector3& position, float size, float alpha,
                                const Vector3& velocity, EngineObject* attachTo);
    // Where a lightning at `object` ends: above its ground by its height x 1.2.
    Vector3 LightningEnd(MapObject* object);
    // A bolt from `from` to `to` that follows the objects of the ids (-1: none), not yet in the world.
    EffectLightning* NewLightning(const Vector3& from, const Vector3& to, int fromObject, int toObject, float width,
                                  int flag);
    // A one-shot sound effect (the effect does not keep it).
    void CreateSound(const char* name, const Vector3& position, const Vector3& velocity, float volume, float radius);
    // `first`, then `pattern` itself (count < 1) or `pattern` formatted with 1..count. It becomes the sound.
    void CreateSoundSequence(const Vector3& position, const Vector3& velocity, const char* first,
                             const char* pattern, int count);
    // CreateSoundSequence's object (`first`, then `names` at random), not yet in the world.
    EffectSoundSeq* NewSoundSequence(const Vector3& position, const Vector3& velocity, const char* first,
                                     std::vector<std::string> names);
    // A point light (flags Specular | Strong), not yet in the world.
    EffectPointLight* NewPointLight(const Vector3& position, const Vector3& color, float range, const Vector3& velocity);
    // The active effect a cast puts on a unit by default (cast:Apply; Buff.lua's buffs): the spell's, its strength
    // trunc(power x 16), the end sound as silentEnd says.
    MapEffect BuffRecord();
    // `unit` to `point` (the sector map's Move) unless `check` and the AI map does not let it stand there (false).
    bool MoveUnit(Unit* unit, const Vector3& point, bool check);

    // A scripted cast's object: into the world's timed objects, then held in `owned` (SpellOwned).
    void Own(Effect* object, int ticks, int payload);
    // One tick of the owned objects: those whose ticks run out go, those that left the world are dropped, and the
    // missiles that arrived go, appended to *arrived in the list's order.
    void TickOwned(std::vector<SpellArrival>* arrived);
    // An owned object with ticks left, or a missile in flight.
    bool OwnsPending() const;
    // Every owned object out of the world.
    void ReleaseOwned();

    // One tick. Returns 0 when the effect has ended (the world then removes it) or without a world.
    int Update();
    // State 2: the lasting spells still in code (the cut possession and link); every case counts the timer and ends
    // the effect.
    void UpdateLasting(SpellType type);
    void ReleaseVisual();
    void ReleaseSound();
    void ReleaseLight();

    void Init(WorldServer* world, Spell* spell, Unit* caster, const Vector3& position, MapObject* target,
              const Vector3& targetPoint, std::uint8_t newSilentEnd);
    // 0 when `object` is the target (the world then drops the effect); a scripted cast lives on without it.
    bool Forget(EngineObject* object);

    // The position, caster and target ids, the target point, the state, the spell, the packed id, the visual
    // / sound / light ids (-1 for none), the timer, the flags and the position again; a scripted cast then its key
    // and its owned objects (a count, then each one's id, ticks and payload key). Load finds the objects in
    // g_loadingProjectileWorld; a scripted cast's footprint is in its Lua entry (stamped again by Adopt).
    void Load(ByteStream* stream);
    void Save(ByteStream* stream);
    // After the world's script state loaded: a scripted cast goes on in the restored state's entry of its key, else
    // it ends (said on the console); a scripted spell's cast in the missile or lasting state (a save from before the
    // spell moved onto its script) goes to its script's OnResume (ScriptSpellHost::Resume), else it ends at its next
    // update.
    void ResumeScripted();

    WorldServer* world = nullptr;
    std::uint32_t flags = 0;    // bit 0: the caster gets no credit for the hit
    Vector3 position{};
    Ref<MapObject> target; // a unit or another map object
    Ref<Unit> caster;      // null for traps and scripts
    Vector3 targetPoint;
    int state = 0;              // 0 casting, 1 missile (only a save's), 2 lasting, 4 scripted, else ends
    Spell spell;
    Ref<Effect> visual;              // particle source, wall, lightning or missile
    Ref<Effect> sound;               // a sound or a sound sequence
    Ref<EffectPointLight> light;
    SpellFootprint footprint;           // the cells it blocks while footprintStamped
    std::uint8_t footprintStamped = 0;
    int timer = 0;                        // ticks left in the next state
    std::uint8_t silentEnd = 0;           // an item's periodic cast: the buff record's end flag, the cast's `silent`
    // A scripted cast's key in its host's casts, and the host while the cast lives there (null once it ended, or
    // when the host went with its zone's Lua state).
    int castKey = 0;
    ScriptSpellHost* host = nullptr;
    std::vector<SpellOwned> owned;      // a scripted cast's objects

    // The visual as the effect class its spell's behaviour creates.
    template <class T> T* Visual() const { return static_cast<T*>(visual.Get()); }

    Spell* HeldSpell() { return &spell; }
    const Spell* HeldSpell() const { return &spell; }
};

// A spell under construction (created by the spell manager as SpellObjectType::Container): a prototype, the modifiers added
// so far, and the Spell that Build computes from them.
class SpellContainer : public EngineObject {
public:
    // The name and the spell are destroyed after the string, where the game destroys them before it.
    SpellContainer();
    ~SpellContainer() override;

    void Create() override;
    // As Create, but the spell gets its Destroy.
    void Destroy() override;
    std::string_view GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Null ends the game. Returns 1.
    bool AddModifier(itemdb::SpellModifierRecord* modifier);

    // By the modifier's first code letter: a, d, e, r, t read the prototype's flag bytes; others are
    // accepted. 0 without a prototype or modifier.
    bool AllowsModifier(const itemdb::SpellModifierRecord* modifier);

    // Area (a), effect (e), mana (m), range (r) scale with the modifier's strength, duration (d) and t add
    // integers, f* add target flags and ic/it cast flags; every modifier adds its price, mana and level. The
    // area becomes a radius (sqrt(area / pi)); the modifier list is sorted.
    void Build();


    const itemdb::SpellRecord* prototype = nullptr;
    std::vector<itemdb::SpellModifierRecord*> modifiers;
    std::string codes;              // modifier index + 1 per modifier
    Spell spell;
    std::string name;              // always empty
};

// The spell manager's object types (CreateObject / type).
enum class SpellObjectType : int {
    Spell = 0x3002,      // Spell
    Container = 0x3003,  // SpellContainer
};

// The spell manager; context gives the spell constructor.
class SpellManager : public ResourceManager {
public:
    SpellManager() = default;
    ~SpellManager() override = default;
    const char* ClassName() override;
    ClassId GetClassId() override;

    SpellContainer* NewContainer();

    // A new empty spell under a free id (Spell::Clone fills it).
    Spell* CreateSpellObject();
    // A Spell (0x3002) or SpellContainer (0x3003); else null.
    EngineObject* NewObject(int type) override;

    // Up to 8 modifiers, through the context's spell constructor. Returns the new spell.
    Spell* CreateSpell(const itemdb::SpellRecord* prototype, const std::vector<itemdb::SpellModifierRecord*>& modifiers);

    // Text such as "healing{e2;e2}": the trimmed text before '{' is the prototype name, the ';'-separated
    // names inside the braces are modifiers (empty ones skipped). An unknown name is added to the name map
    // with a null record. Returns the new spell; null, after a warning, for an unknown prototype name (a script's,
    // a map's or a database's text naming a spell spells.sdb lacks).
    Spell* CreateSpellFromText(std::string_view text);

    // `this` is not used.
    std::vector<std::string> ListSpellNames();
    std::vector<std::string> ListModifierNames();
};

class SpellConstructor : public EngineObject {
public:
    ~SpellConstructor() override;

    // Releases the container and forgets the manager. Returns 1.
    void Create() override;
    // Also drops the manager's count.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Counts a reference to `manager` that is never released.
    void SetManager(SpellManager* manager);

    // A new container for `prototype` (without a manager or a prototype the game ends).
    void BeginConstruct(const itemdb::SpellRecord* prototype);

    // Builds the container's spell (without a manager or a container the game ends).
    void EndConstruct();

    // Rebuilds when the prototype accepts it.
    bool AddModifier(itemdb::SpellModifierRecord* modifier);

    SpellManager* manager = nullptr;  // counted but never released
    Ref<SpellContainer> container;
};

// Case-insensitive; null if none.
const std::string* SpellCodeByName(std::string_view name);
const char* SpellModifierCodeByName(std::string_view name);
