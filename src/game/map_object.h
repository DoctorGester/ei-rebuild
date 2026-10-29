#pragma once

// The base class of every map object (units, trees, buildings, stones, levers, traps, torches ...); abstract.
// Each has a companion holding its side-specific state: a CMapObjectServer (class id 0x39) on the server, a
// CMapObjectClientSpecific (0x3A) on the client; both point back at the object.

#include "engine_object.h"
#include "map_object_figures.h"   // CObject3D, the base
#include "maths.h"
#include "net_delta.h"          // DeltaState

#include "lib/bit_flags.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

class CMapObjectServer;
class CMapObjectClientSpecific;
class CMapObjectCompanion;
struct DeltaState;
enum class SpellType : int;   // spell.h

// An active effect (an array): the record of a buff spell (the buffs CSpellEffect::Start applies) or of Teleport,
// which stuns its caster like Stun. Strengthen, Weaken, Enlarge and Shrink scale the object.
struct MapEffect {
    std::uint8_t type = 0;          // the spell's SpellType (spell.h)
    std::uint8_t power = 0;
    int ticksLeft = 0;              // counted down once per world update; removed at 0
    std::uint32_t value = 0;        // a float (EffectValue) or a sound handle
    std::uint32_t strength = 0;     // a float (saved; Rebirth compares it)
    std::uint8_t ending = 0;        // the client plays no end sound when set

    SpellType Type() const { return static_cast<SpellType>(type); }

    // One tick less; whether any are left (0 / 1).
    int Tick();
    // Type, power, ticksLeft, value and strength, read or written one by one (`ending` is not saved).
    void Load(CStream* stream);
    void Save(CStream* stream) const;
};

// The hit points, mana and protection rounded (written from the server companion's UnitVitals).
struct UnitVitalsPacked {
    int Reset();

    std::uint8_t unused_00[2];      // sent with the record; Reset zeroes the first
    std::int16_t hitPoints;
    std::int16_t maxHitPoints;
    std::uint16_t mana;             // (read unsigned)
    std::uint16_t maxMana;
    std::int16_t protection[7];
};
static_assert(sizeof(UnitVitalsPacked) == 0x18);

// The bits of CEngineObject::flags_ on the map objects (the word is saved and sent as the number). Bits that mean
// something else per class have one name each (the same number).
enum class MapObjectFlag : std::uint32_t {
    Removed = 0x1,              // gone from the world: its id is sent on
    Changed = 0x2,              // MarkChanged: the network state is not written yet
    Hidden = 0x8,               // not drawn (a spec with visible 0; the client's draw test sets and clears it)
    Selected = 0x10,            // client: selected (CWorldClient::Select; its figure lit and highlighted)
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
    Invisible = 0x10000,        // HideObject
    Synchronised = 0x20000,     // IsSynchronised
    Stunned = 0x40000,          // IsStunned
    RelationsChanged = 0x80000, // units: the hostile mask changed, PropagateHostility is due
    Armed = 0x80000,            // traps: ActivateTrap
    Disabled = 0x80000,         // levers
    ArmedFresh = 0x100000,      // traps: armed since the last update
    LootTaken = 0x100000,       // units: the loot was collected
    InParty = 0x400000,         // client units: in the local player's party (CPlayerClient::OnPartyState): their
                                // hits show their number, their footprints and attached particle sources show, the
                                // views that draw only the party draw them
    Blocked = 0x800000,         // units: BlockUnit; also set while the unit speaks
    Frozen = 0x1000000,         // units: no updates of orders and animation
    SeeAll = 0x2000000,         // units: sees every unit once
    ChoosingTarget = 0x4000000, // server units: while a player's unit chooses its target (ScaledValue caps the range)
    Preview = 0x4000000,        // client units: an interface figure (CreatePartyUnits, CUiModel), not moved along
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

// The virtuals' names and their evidence: docs/notes/map_objects.md ("Virtual slot names").
class CMapObject : public CObject3D {
public:
    ~CMapObject() override;
    void Create() override { CreateMapObject(); }
    void Destroy() override { DestroyMapObject(); }
    void ClearState() override {}
    bool RunTick() override {
        Update();
        return true;
    }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // An object of type SideObjectType::MapObject becomes the companion (AttachCompanion); null and others go to CObject3D's (which
    // takes SideObjectType::Object3D). Units override it.
    void AttachObject(CEngineObject* object) override;
    bool InvisibleSlot() override;
    // Resets the scale and grows or shrinks it by 0.4 per net level of enlarge/shrink (x, y, z) or, without
    // those, strengthen/weaken (x and, except for heroes, z). Components stay >= 0. Marks part 1 changed.
    virtual void RecomputeScale();
    // Empty (units override it).
    virtual void EffectsChanged();
    // flags_ 0x20000 (bit 17 as 0 / 1); units, levers, traps, torches 1. False ones leave the server's map-object
    // maps after the zone loads (SendFreshObjects). The world server creates such objects only with one flag set,
    // the others with another.
    virtual bool IsSynchronised();
    // The scale becomes the base scale (units override it).
    virtual void ApplyBaseScale();
    // A hit (DamageInfo) from `attacker` (units: to the controller's TakeHit; other objects ignore it).
    virtual void ReceiveHit(DamageInfo* hit, CUnit* attacker, CUnit* caster);
    virtual int StatsChanged();        // PackVitals (units override); 1
    // Nothing (units hand the argument to their controller's vitals virtual).
    virtual void ScaleEffectChanged(bool scaleEffect);
    // The prototype by name (units: their statistics). Here the vitals of `prototype` are packed into the
    // rounded vitals, from a local the original does not clear either.
    virtual void TakePrototype(std::string_view prototype);
    // Empty; units: a CUnitServer controller (class 0x3E) copies the statistics (CUnitServer::SetStats).
    virtual void SetStats(const UnitStats* stats);
    // Hands `data` to a CMapObjectServer companion.
    virtual void SetServerData(const UnitVitals* data);
    // Empty; units: the controller's size-class virtual.
    virtual void UpdateSizeClasses();
    virtual bool IsUsable();            // 0; traps 1, levers NeedsKey
    virtual bool IsDirectUseBlocked();  // 0; levers their directUseBlocked
    virtual std::uint32_t KeyId();     // the key it asks for: -123; levers override
    // A skill used on the object (traps Disarm, levers ApplyKey); here logs "Science is applied on wrong object"
    // and returns false.
    virtual bool ApplyScience(const LockKey* key, CEngineObject* user);
    // Whether `key` fits: 0; levers KeyOpens, traps KeyFits.
    virtual bool AcceptsKey(const LockKey* key);
    // True; levers a flag of theirs (a best guess). The AI map's MarkFootprint refreshes units' passage around
    // the object only when it is true.
    virtual bool KeepsMapFlag();
    // The spots a user of radius `userRadius` may use the object from: none; levers override it.
    virtual std::vector<Vector3> UseSpots(float userRadius);
    // Appends a copy of `effect`, marks part 3 changed and the object dirty, then calls the vitals virtual
    // (with whether the effect scales the object), SetStunned(1) for the stun types, the effects-changed
    // virtual and, for a scaling effect, RecomputeScale. Rebirth's FixSpellEffectsOverflow keeps at most 20
    // effects (a 21st is dropped) and, for an effect lasting more than one tick, removes older ones of the same
    // type, end flag and second float (within 0.001) that last no longer and are no stronger.
    virtual void AddEffect(const MapEffect* effect);
    // The effect array's SetSize(count, -1).
    void SetEffectCount(int count);
    virtual std::uint32_t SetStunned(int on);   // ignores it (0); units: the new flags_
    virtual int SetVisibilityMode(int mode);    // hands it to the companion
    virtual Vector3* EyePoint(Vector3* out);        // where the object sees from (GroundPoint)
    virtual std::uint32_t ReadNetState(int first);   // (units override)
    virtual void WriteNetState();      // (units override)
    // z = ground height at (x, y) + `offset`.
    virtual void PlaceOnGround(float offset);
    // Where the object is at world time `time` (units: along their path; other objects ignore the time).
    virtual Vector3* PositionAt(Vector3* out, int time);
    virtual Vector3* AimPoint(Vector3* out);        // GetPosition2
    virtual bool IsLargeUnit();                      // 0; units their own
    virtual bool IsDead();                           // non-zero when dead or gone
    // `object` is going away (the world server's RemoveObject tells every object). Empty (0); units: the
    // controller's matching virtual; magic traps override.
    virtual int ForgetObject(CEngineObject* object);
    virtual void UpdateMoveFactors();  // empty; units: the controller's UpdateMoveFactors
    virtual void RefreshWounds();      // empty; units: the controller's hit virtual
    // The speed the object moves at, unrounded: 0; units their forward speed.
    virtual double MoveSpeed();
    virtual float Radius();                         // 0.75; units their own
    // Units keep `name` as their script name; empty here.
    virtual void SetScriptName(std::string_view name);

    // Records that part `part` (0..15; negative: none) of the object changed, marks it dirty (flags_ bit 1) and
    // stamps it with the world's change counter.
    void MarkChanged(int part);
    // Has the object an active effect of type `type` (compared with MapEffect::type)?
    bool HasEffect(SpellType type);

    // The per-tick update: lets the companion update. CUnit's update calls it after its controller's.
    void Update();

    // Removes every effect (frees the array), marks part 3 changed and calls the vitals, stun (0) and scale
    // virtuals.
    void ClearEffects();

    // The largest MapEffect::value (as a float, at least 0) of the effects of type
    // `type` (compared with the whole low byte); 0 without one. NaN values win the comparison.
    float EffectValue(SpellType type);

    // scale = base scale.
    void ResetScale();

    // The position into `out`.
    float* GetPosition(float* out);
    float* GetPosition2(float* out);

    // The point at the object's offset, moved to the ground height there plus the offset's z.
    float* GroundPoint(float* out);

    // Replaces the companion: the old one is detached and released, the new one points back here and gains a
    // reference.
    void AttachCompanion(CEngineObject* companion);

    // Statistics changed: a CMapObjectServer companion packs its vitals into the rounded vitals. Returns 1, or 0
    // for another companion.
    int PackVitals();
    // A CMapObjectServer companion (class 0x39) gets the base of each protection (7 pairs) zeroed.
    void ClearProtectionBases();

    // Bits 16 (invisible: set by the client for effect 0x0F), 18 (stunned: effect 0x19) and 17 of flags_;
    // IsInvisible and the bit-17 test are virtuals.
    bool IsInvisible();
    bool IsStunned();
    std::uint32_t Flag20000();

    void CreateMapObject();
    void DestroyMapObject();

    // Load: the companion's Load, then animationSpeed, the position, the orientation, the name, the scale, the
    // effects (count, then MapEffect::Load each), flags_, the second name and, from version 0x4F, the base scale.
    void LoadMapObject(CStream* stream);
    // Save: the same fields (names with their terminators; the base scale always).
    void SaveMapObject(CStream* stream);

    // SetVisibilityMode with mode 0 for `level` 0 (or NaN), 1 for 1, else 2.
    int SetVisibilityLevel(float level);

    // The sector map's Remove. The companion's virtual (empty in both); returns 1.
    int NotifyCompanion();
    // The network state written in full / only its changed parts (the statistics, repacked first when part 0 or 4
    // changed): the write mode, the write virtual, the state finished; flags_ bit 1 cleared.
    void WriteFullState();
    void WriteChangedState();
    // The network state read from `reader`: BeginRead, ReadNetState with `first`, the state finished.
    std::uint32_t ReadState(int first, CMemoryStream* reader);
    std::string* SetSecondName(const char* text);

    CObject3D* Object3D() { return this; }


    CMapObjectCompanion* companion; // CMapObjectServer / CMapObjectClientSpecific
    // The companion as the server's CMapObjectServer (on the server; defined in unit.h).
    CMapObjectServer* ServerCompanion() const;
    // The companion as the client's CMapObjectClientSpecific (on the client; defined in unit.h).
    CMapObjectClientSpecific* ClientCompanion() const;
    // World updates until the object's effects are next looked at (5 after Create; the server world's update map
    // sets it).
    int updateCountdown;
    float animationSpeed;           // the animation's speed factor (CUnit::animationSpeed; 1 after Create)
    UnitVitalsPacked packedVitals;  // hit points, maximum, mana, maximum
    std::vector<MapEffect> effects; // (SetEffectCount is its SetSize)
    std::string name2;
    Vector3 baseScale;
    int sectorCellX;
    int sectorCellY;
    int changeStamp;                // the world's change counter when last changed
    DeltaState netState_;           // the network state; its mask_ is the parts changed since the last send (bit
                                    // per part: MarkChanged)

protected:
    CMapObject();
};

// A map object's companion (CMapObjectServer / CMapObjectClientSpecific derive from it).
class CMapObjectCompanion : public CEngineObject {
public:
    CMapObjectCompanion() {
        owner = nullptr;
    }
    ~CMapObjectCompanion() override;
    void Create() override;    // no map object
    void Destroy() override;  // no map object
    bool RunTick() override { return false; }

    // The object's effect list changed (`first`: it arrived with the object). The server's is empty.
    virtual void EffectsChanged(bool) {}
    virtual void Unused14() {}       // empty in both
    // A hit of `damage` with the hit-kind bits `kind` (UnitStatsFlag Backstab, Head, Electrical). Server: empty;
    // client: for a living MapObjectFlag::InParty object, a message with the object's id.
    virtual void Hit(int, int) {}
    // Whether the object is dead (server: hit points rounded to 16 bits <= 0; client: the unit's hit points
    // <= 0).
    virtual bool IsDead() = 0;
    virtual bool IsOverkilled() = 0;
    // The client's sets the object's light mode; the server's is empty.
    virtual void SetLightMode(int) {}

    CMapObject* owner;              // the map object (CMapObject::AttachCompanion sets it)
};
inline CMapObjectCompanion* CompanionSlots(CEngineObject* companion) {
    return static_cast<CMapObjectCompanion*>(companion);
}

// An engine object known to be a map object (a unit, a lever, a trap ...): its slots and fields.
inline CMapObject* ObjectSlots(CEngineObject* object) { return static_cast<CMapObject*>(object); }

// The companion's back pointer.
inline CMapObject*& CompanionOwner(CEngineObject* companion) { return CompanionSlots(companion)->owner; }
