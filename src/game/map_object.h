#pragma once

// The base class of every map object (units, trees, buildings, stones, levers, traps, torches ...); abstract.
// Each has a companion holding its side-specific state: a CMapObjectServer (class id 0x39) on the server, a
// CMapObjectClientSpecific (0x3A) on the client; both point back at the object.

#include "engine_object.h"
#include "map_object_figures.h"   // CObject3D, the base
#include "maths.h"
#include "net_delta.h"          // DeltaState

#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

class CMapObjectServer;
class CMapObjectClientSpecific;
class CMapObjectCompanion;
struct DeltaState;

// An active effect (an array).
// Effect types seen: 0x09-0x0E shields and sights, 0x0F invisibility, 0x10 silence, 0x11 lichdom,
// 0x13 stench, 0x19 stun, 0x1A antimagic, 0x1C, 0x1D strengthen, 0x1E weaken, 0x1F regeneration,
// 0x20 feeble mind, 0x21 speed, 0x22 slow, 0x25 enlarge, 0x26 shrink.
struct MapEffect {
    std::uint8_t type;
    std::uint8_t power;
    int ticksLeft;                  // counted down once per world update; removed at 0
    std::uint32_t value;            // a float (EffectValue) or a sound handle
    std::uint32_t strength;         // a float (saved; Rebirth compares it)
    std::uint8_t ending;            // the client plays no end sound when set

    // Type, power, ticksLeft, value and ending cleared (strength not).
    MapEffect* Construct();
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

// Effect types with a special meaning for the base class.
namespace mapeffect {
constexpr int kStun = 0x19;
constexpr int kStun2 = 0x1C;           // also keeps flag 0x1000000 (with kStun)
constexpr int kStrengthen = 0x1D;      // scale up (x and z)
constexpr int kWeaken = 0x1E;          // scale down
constexpr int kEnlarge = 0x25;         // scale up (x, y and z)
constexpr int kShrink = 0x26;          // scale down
} // namespace mapeffect

struct DamageInfo;
struct UnitVitals;
struct UnitStats;
struct LockKey;

// The virtuals' names and their evidence: docs/notes/map_objects.md ("Virtual slot names").
class CMapObject : public CObject3D {
public:
    int ScaleEffectChangedDefault(int /*scaleEffect*/);
    int AcceptsNoKey(const void* /*key*/);

    ~CMapObject() override;
    int Create() override { return CreateMapObject(); }
    void Destroy() override { DestroyMapObject(); }
    void ClearState() override {}
    int RunTick() override { return Update(); }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // An object of type 0x47 becomes the companion (AttachCompanion); null and others go to CObject3D's (which
    // takes type 0x46). Units override it.
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
    virtual void ScaleEffectChanged(int scaleEffect);
    // The prototype by name (units: their statistics). Here the vitals of `prototype` are packed into the
    // rounded vitals, from a local the original does not clear either.
    virtual void TakePrototype(std::string_view prototype);
    // Empty; units: a CUnitServer controller (class 0x3E) copies the statistics (CUnitServer::SetStats).
    virtual void SetStats(const UnitStats* stats);
    // Hands `data` to a CMapObjectServer companion.
    virtual void SetServerData(const UnitVitals* data);
    // Empty; units: the controller's size-class virtual.
    virtual void UpdateSizeClasses();
    virtual int IsUsable();            // 0; traps 1, levers NeedsKey
    virtual int IsDirectUseBlocked();  // 0; levers their directUseBlocked
    virtual std::uint32_t KeyId();     // the key it asks for: -123; levers override
    // A skill used on the object (traps Disarm, levers ApplyKey); here logs "Science is applied on wrong object"
    // and returns false.
    virtual bool ApplyScience(const LockKey* key, CEngineObject* user);
    // Whether `key` fits: 0; levers KeyOpens, traps KeyFits.
    virtual int AcceptsKey(const LockKey* key);
    // True; levers a flag of theirs (a best guess). The AI map's MarkFootprint refreshes units' passage around
    // the object only when it is true.
    virtual bool KeepsMapFlag();
    // The spots a user of radius `userRadius` may use the object from, into the array `out`: FreeArray
    // (none); levers Lever::CollectUseSpots.
    virtual void UseSpots(std::vector<Vector3>* out, float userRadius);
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
    virtual int IsLargeUnit();                      // 0; units their own
    virtual int IsDead();                           // non-zero when dead or gone
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
    int HasEffect(std::uint32_t type);

    void SetUpMapObject();

    // The per-tick update: lets the companion update. Returns 1. CUnit's update calls it after its controller's.
    int Update();

    // Removes every effect (frees the array), marks part 3 changed and calls the vitals, stun (0) and scale
    // virtuals.
    void ClearEffects();

    // The largest MapEffect::value (as a float, at least 0) of the effects of type
    // `type` (compared with the whole low byte); 0 without one. NaN values win the comparison.
    float EffectValue(std::uint32_t type);

    // scale = base scale.
    void ResetScale();

    // The position into `out`.
    float* GetPosition(float* out, int unused);
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
    std::uint32_t IsInvisible();
    std::uint32_t IsStunned();
    std::uint32_t Flag20000();

    int CreateMapObject();
    void DestroyMapObject();

    // Load: the companion's Load, then animationSpeed, the position, the orientation, the name, the scale, the
    // effects (count, then MapEffect::Load each), flags_, the second name and, from version 0x4F, the base scale.
    void LoadMapObject(CStream* stream);
    // Save: the same fields (names with their terminators; the base scale always).
    void SaveMapObject(CStream* stream);

    // The companion's Query with the first two arguments (both companions return 0).
    int CompanionQuery(int first, int second, int unused);
    // Frees the data of `array` and empties it.
    void FreeArray(std::vector<Vector3>* array, int unused);

    // SetVisibilityMode with mode 0 for `level` 0 (or NaN), 1 for 1, else 2.
    int SetVisibilityLevel(float level);

    // Whether the object is dead: the companion's IsDead.
    int CompanionIsDead();
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


    // The world the object lives in (via its owner), or null.
    CWorld* World();

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
        ClearFields();
        owner = nullptr;
    }
    ~CMapObjectCompanion() override;
    int Create() override;    // no map object; 1
    void Destroy() override;  // no map object
    int RunTick() override { return UpdateCompanion(); }

    int UpdateCompanion();

    // The object's effect list changed (`first`: it arrived with the object). The server's is empty.
    virtual void EffectsChanged(bool) {}
    virtual void Unused14() {}       // empty in both
    virtual int Query(int first, int second);   // (0) in both
    // A hit of `damage` with the hit-kind bits `kind` (1, 2, 4). Server: empty; client: for a living,
    // visible-flag (0x400000) object, a message with the object's id.
    virtual void Hit(int, int) {}
    // Whether the object is dead (server: hit points rounded to 16 bits <= 0; client: the unit's hit points
    // <= 0).
    virtual int IsDead() = 0;
    virtual int IsOverkilled() = 0;
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
