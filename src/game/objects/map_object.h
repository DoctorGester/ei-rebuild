#pragma once

// The base class of every map object (units, trees, buildings, stones, levers, traps, torches ...); abstract.
// Each has a companion holding its side-specific state: a MapObjectServer (class id 0x39) on the server, a
// MapObjectClientSpecific (0x3A) on the client; both point back at the object.

#include "engine/core/engine_object.h"
#include "game/net/net_delta.h"          // DeltaState
#include "game/objects/object3d.h"   // Object3D, the base
#include "game/spells/effect_stats.h"

#include "lib/bit_flags.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class MapObjectServer;
class MapObjectClientSpecific;
struct DeltaState;
enum class SpellType : int;   // spell_db.h

// An active effect (an array): what a spell's cast:Apply puts on the object (the buffs, Buff.lua) or Teleport's
// record, which stuns its caster like Stun. Strengthen, Weaken, Enlarge and Shrink scale the object.
struct MapEffect {
    std::uint8_t type = 0;          // the place of the spell record that made it (spell_db.h)
    std::uint8_t power = 0;
    int ticksLeft = 0;              // counted down once per world update; removed at 0
    float value = 0.0f;             // the spell's damage (its kind's OnStats sees the strongest)
    float strength = 0.0f;          // the spell's radius (saved; Rebirth compares it)
    std::uint8_t ending = 0;        // the client plays no end sound when set

    // What it does: the activeEffect of the spell record at `type` (its `effect`, else its behaviour;
    // itemdb::ActiveEffectAt).
    SpellType Behaviour() const;
    // The place of the record whose OnStats gives it its stats (itemdb::EffectKindAt; -1: none).
    int Kind() const;

    // One tick less; whether any are left (0 / 1).
    int Tick();
    // Type, power, ticksLeft, value and strength, read or written one by one (`ending` is not saved).
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;
};

// The hit points, mana and protection rounded (written from the server companion's UnitVitals).
struct UnitVitalsPacked {
    int Reset();

    std::uint8_t unused_00[2] = {};  // sent with the record; Reset zeroes the first
    std::int16_t hitPoints = 0;
    std::int16_t maxHitPoints = 0;
    std::uint16_t mana = 0;         // (read unsigned)
    std::uint16_t maxMana = 0;
    std::int16_t protection[7] = {};
};
static_assert(sizeof(UnitVitalsPacked) == 0x18);   // sent as its 24 bytes

// The bits of EngineObject::flags on the map objects (the word is saved and sent as the number) and the item
// constructor. Bits that mean something else per class have one name each (the same number).
enum class MapObjectFlag : std::uint32_t {
    Removed = 0x1,              // gone from the world: its id is sent on
    Changed = 0x2,              // MarkChanged: the network state is not written yet
    Hidden = 0x8,               // not drawn (a spec with visible 0; the client's draw test sets and clears it)
    Constructing = 0x8,         // the item constructor: from BeginCreateItem to EndCreateItem (nothing reads it)
    Selected = 0x10,            // client: selected (WorldClient::Select; its figure lit and highlighted)
    Shown = 0x20,               // client: in view this frame (the view pass), a part drawn through its drawer (the
                                // draw); screen picking takes only such objects
    NotDrawn = 0x40,            // never drawn (traps)
    FiguresLoaded = 0x80,       // the figure is loaded
    BoxMarked = 0x100,          // client party units: inside the selection box being dragged (their figure lit)
    Stationary = 0x200,         // client objects but units: the figure's parts are FigureMeshFlag::Stationary; only
                                // such objects are drawn static and have their models kept loaded
    Static = 0x400,             // client: drawn static (every part kept its lighting; no object matrix pushed)
    NoEdgeFade = 0x800,         // client: drawn at full light, not darkened by the terrain's edge fade (the
                                // interface's figures)
    Detached = 0x1000,          // its parts are passed to the figure manager
    InSectorMap = 0x8000,       // placed in the sector map (set when it is placed or moves; a unit's update clears it; the
                                // word is saved)
    Invisible = 0x10000,        // HideObject
    Synchronised = 0x20000,     // IsSynchronised
    Stunned = 0x40000,          // IsStunned
    RelationsChanged = 0x80000, // units: the hostile mask changed, PropagateHostility is due
    Armed = 0x80000,            // traps: ActivateTrap
    Disabled = 0x80000,         // levers
    LootTaken = 0x100000,       // units: the loot was collected
    InParty = 0x400000,         // client units: in the local player's party (PlayerClient::OnPartyState): their
                                // hits show their number, their footprints and attached particle sources show, the
                                // views that draw only the party draw them
    Blocked = 0x800000,         // units: BlockUnit; also set while the unit speaks
    Frozen = 0x1000000,         // units: no updates of orders and animation
    SeeAll = 0x2000000,         // units: sees every unit once
    ChoosingTarget = 0x4000000, // server units: while a player's unit chooses its target (ScaledValue caps the range)
    Preview = 0x4000000,        // client units: an interface figure (CreatePartyUnits, UiModel), not moved along
                                // its path
    Planned = 0x8000000,        // units: set on the target while a cast is planned
    NoCasting = 0x10000000,     // units: no spells; the client turns such a unit at once along its path
    Shared = 0x20000000,        // on the world's shared list (a shared loot record or unit)
    HelmetHidden = 0x40000000,  // units: the helmet (armour slot 0) is sent as none (the briefing's leader while
                                // speaking)
};
template <> inline constexpr bool lib::kBitFlags<MapObjectFlag> = true;

struct DamageInfo;
struct UnitVitals;
struct UnitStats;
struct LockKey;

class MapObject;

// A map object's companion (MapObjectServer / MapObjectClientSpecific derive from it).
class MapObjectCompanion : public EngineObject {
public:
    ~MapObjectCompanion() override;
    void Create() override;    // no map object
    void Destroy() override;  // no map object
    bool RunTick() override { return false; }

    // The object's effect list changed (`first`: it arrived with the object). The server's is empty.
    virtual void EffectsChanged(bool) {}
    // A hit of `damage` with the hit-kind bits `kind` (UnitStatsFlag Backstab, Head, Electrical). Server: empty;
    // client: for a living MapObjectFlag::InParty object, a message with the object's id.
    virtual void Hit(int, int) {}
    // Whether the object is dead (server: hit points rounded to 16 bits <= 0; client: the unit's hit points
    // <= 0).
    virtual bool IsDead() = 0;
    virtual bool IsOverkilled() = 0;
    // The client's sets the object's light mode; the server's is empty.
    virtual void SetLightMode(int) {}
    // This half as the server's or the client's: null on the other side.
    virtual MapObjectServer* ServerSide() { return nullptr; }
    virtual MapObjectClientSpecific* ClientSide() { return nullptr; }

    MapObject* owner = nullptr;    // the map object (MapObject::SetCompanion sets it)
};

// The virtuals' names and their evidence: docs/notes/map_objects.md ("Virtual slot names").
class MapObject : public Object3D {
public:
    ~MapObject() override;
    void Create() override;
    void Destroy() override;
    void ClearState() override {}
    bool RunTick() override {
        Update();
        return true;
    }
    // Load: the companion's Load, then animationSpeed, the position, the orientation, the name, the scale, the
    // effects (count, then MapEffect::Load each), flags, the second name and, from version 0x4F, the base scale.
    // Save: the same fields (names with their terminators; the base scale always).
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    // An object of type SideObjectType::MapObject becomes the companion (SetCompanion); null and others go to
    // Object3D's (which takes SideObjectType::Object3D). Units override it.
    void AttachObject(EngineObject* object) override;
    bool InvisibleSlot() override;
    // Resets the scale and grows or shrinks it by 0.4 per step of the effects' Size + Girth (x, y, z) or, with Size
    // 0, of Girth (x and, except for heroes, z). Components stay >= 0. Marks part 1 changed.
    virtual void RecomputeScale();
    // Empty (units override it).
    virtual void EffectsChanged();
    // flags 0x20000 (bit 17 as 0 / 1); units, levers, traps, torches 1. False ones leave the server's map-object
    // maps after the zone loads (SendFreshObjects). The world server creates such objects only with one flag set,
    // the others with another.
    virtual bool IsSynchronised();
    // The scale becomes the base scale (units override it).
    virtual void ApplyBaseScale();
    // A hit (DamageInfo) from `attacker` (units: to the controller's TakeHit; other objects ignore it).
    virtual void ReceiveHit(DamageInfo* hit, Unit* attacker, Unit* caster);
    // A MapObjectServer companion packs its vitals into the rounded vitals; returns 1, or 0 for another companion
    // (units override it and repack their statistics too).
    virtual int StatsChanged();
    // Nothing (units hand the argument to their controller's vitals virtual).
    virtual void ScaleEffectChanged(bool scaleEffect);
    // The prototype by name (units: their statistics). Here the vitals of `prototype` are packed into the
    // rounded vitals, from a local the original does not clear either.
    virtual void TakePrototype(std::string_view prototype);
    // Empty; units: a UnitServer controller (class 0x3E) copies the statistics (UnitServer::SetStats).
    virtual void SetStats(const UnitStats* stats);
    // Hands `data` to a MapObjectServer companion.
    virtual void SetServerData(const UnitVitals* data);
    // Empty; units: the controller's size-class virtual.
    virtual void UpdateSizeClasses();
    virtual bool IsUsable();            // 0; traps 1, levers NeedsKey
    virtual bool IsDirectUseBlocked();  // 0; levers their directUseBlocked
    virtual std::uint32_t KeyId();     // the key it asks for: -123; levers override
    // A skill used on the object (traps Disarm, levers ApplyKey); here logs "Science is applied on wrong object"
    // and returns false.
    virtual bool ApplyScience(const LockKey* key, EngineObject* user);
    // Whether `key` fits: 0; levers KeyOpens, traps KeyFits.
    virtual bool AcceptsKey(const LockKey* key);
    // True; levers a flag of theirs (a best guess). The AI map's MarkFootprint refreshes units' passage around
    // the object only when it is true.
    virtual bool KeepsMapFlag();
    // The spots a user of radius `userRadius` may use the object from: none; levers override it.
    virtual std::vector<Vector3> UseSpots(float userRadius);
    // Appends a copy of `effect`, marks part 3 changed and the object dirty, then calls the vitals virtual
    // (with whether the effect scales the object: ScalesWith), SetStunned(1) when the effects stun, the
    // effects-changed virtual and, for a scaling effect, RecomputeScale. An effect lasting more than one tick then removes the older
    // ones it renews: the same type, end flag and strength (within 0.001), lasting no longer and no stronger. At most
    // 255 effects (the network's count is a byte).
    virtual void AddEffect(const MapEffect* effect);
    // The effect array's SetSize(count, -1).
    void SetEffectCount(int count);
    virtual std::uint32_t SetStunned(int on);   // ignores it (0); units: the new flags
    virtual int SetVisibilityMode(int mode);    // hands it to the companion
    virtual Vector3 EyePoint();        // where the object sees from (GroundPoint)
    virtual bool ReadNetState(bool update);   // (units override)
    virtual void WriteNetState();      // (units override)
    // z = ground height at (x, y) + `offset`.
    virtual void PlaceOnGround(float offset);
    // Where the object is at world time `time` (units: along their path; other objects ignore the time).
    virtual Vector3 PositionAt(int time);
    virtual Vector3 AimPoint();
    virtual bool IsLargeUnit();                      // 0; units their own
    virtual bool IsDead();                           // non-zero when dead or gone
    // `object` is going away (the world server's RemoveObject tells every object). Empty (0); units: the
    // controller's matching virtual; magic traps override.
    virtual int ForgetObject(EngineObject* object);
    virtual void UpdateMoveFactors();  // empty; units: the controller's UpdateMoveFactors
    virtual void RefreshWounds();      // empty; units: the controller's hit virtual
    // The speed the object moves at: 0; units their forward speed.
    virtual float MoveSpeed();
    virtual float Radius();                         // 0.75; units their own
    // Units keep `name` as their script name; empty here.
    virtual void SetScriptName(std::string_view name);

    // Records that field `part` of the object changed (NetField::None: none), marks it dirty (flags bit 1) and
    // stamps it with the world's change counter.
    void MarkChanged(NetField part);
    // Has the object an active effect of the behaviour `type` (MapEffect::Behaviour())?
    bool HasEffect(SpellType type);

    // The per-tick update: lets the companion update. Unit's update calls it after its controller's.
    void Update();

    // Removes every effect (frees the array), marks part 3 changed and calls the vitals, stun (0) and scale
    // virtuals.
    void ClearEffects();
    // After an effect left the array (it ran out, or RemoveEffects; `scales`: ScalesWith it, asked before): the scale
    // recomputed for a scaling one, part 3 marked changed, the scale and effects-changed virtuals, unstunned when the
    // effects left do not stun.
    void EffectRemoved(bool scales);
    // Every effect of the spell record `record` (MapEffect::type) removed, each as one that ran out; how many.
    int RemoveEffects(std::uint8_t record);

    // What the active effects add to the object's stats (effect_stats.h): a server object's by its kinds' OnStats,
    // none on the client.
    const EffectStats& EffectTotals();
    // Whether the kind of `effect` (one of the object's) changes its figure or its hit points' factor: its OnStats
    // touches Might, Frailty, Girth or Size.
    bool ScalesWith(const MapEffect& effect);

    // The point at the object's offset, moved to the ground height there plus the offset's z.
    Vector3 GroundPoint();

    // A MapObjectServer companion (class 0x39) gets the base of each protection (7 pairs) zeroed.
    void ClearProtectionBases();

    // Bit 18 of flags (stunned: the effects' Stunned); bit 16 (invisible: set by the client for effect 0x0F) is
    // InvisibleSlot and bit 17 IsSynchronised.
    bool IsStunned();

    // SetVisibilityMode with mode 0 for `level` 0 (or NaN), 1 for 1, else 2.
    int SetVisibilityLevel(float level);

    // The network state written in full / only its changed parts (the statistics, repacked first when part 0 or 4
    // changed): the write mode, the write virtual, the state finished; flags bit 1 cleared.
    void WriteFullState();
    void WriteChangedState();
    // The network state read from `reader`: BeginRead, ReadNetState with `first`, the state finished.
    bool ReadState(bool update, MemoryStream* reader);

    Object3D* AsObject3D() { return this; }


    // `companion` (or null) replaces the companion: the old one is detached and released, the new one points back
    // here and gains a reference.
    void SetCompanion(MapObjectCompanion* companion);

    Ref<MapObjectCompanion> companion; // MapObjectServer / MapObjectClientSpecific
    // The companion as the server's MapObjectServer (on the server; defined in unit.h).
    MapObjectServer* ServerCompanion() const;
    // The server companion's vitals (defined in unit.h).
    UnitVitals& Vitals() const;
    // The companion as the client's MapObjectClientSpecific (on the client; defined in unit.h).
    MapObjectClientSpecific* ClientCompanion() const;
    // World updates until the object's effects are next looked at (5 after Create; the server world's update map
    // sets it).
    int updateCountdown = 0;
    float animationSpeed = 1.0f;    // the animation's speed factor (Unit::animationSpeed; 1 after Create)
    UnitVitalsPacked packedVitals;  // hit points, maximum, mana, maximum
    std::vector<MapEffect> effects; // (SetEffectCount is its SetSize)
    std::string name2;
    Vector3 baseScale;
    int sectorCellX = -1;
    int sectorCellY = -1;
    int changeStamp = 0;            // the world's change counter when last changed
    DeltaState netState;            // the network state; its mask is the parts changed since the last send (bit
                                    // per part: MarkChanged)

protected:
    MapObject();
};

// The map object classes without members of their own (objects/map_object.cpp): CTree, CStone, CBuilding and
// CStuffObject, all 0x238 bytes and made by the map object factory. Real
// virtual classes. Each overrides only the destructor, class name and class id.
template <ClassId Id>
class PlainMapObject : public MapObject {
public:
    PlainMapObject();
    ~PlainMapObject() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
};

// Kept: the map object kinds are the class names (one instance of PlainMapObject per class id).
// Constructor, destructor, deleting destructor and two more virtuals.
using CTree = PlainMapObject<ClassId::Tree>;
using CStone = PlainMapObject<ClassId::Stone>;
using CBuilding = PlainMapObject<ClassId::Building>;
using CStuffObject = PlainMapObject<ClassId::StuffObject>;
