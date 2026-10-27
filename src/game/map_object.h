#pragma once

// The base class of every map object (units, trees, buildings, stones, levers, traps, torches ...). Abstract
// (two virtuals are pure). It derives from the 3D object base (0x170 bytes: position, orientation, figure
// parts, scale), which derives from CTransformObject and CEngineObject. CUnit's own fields follow. Each map
// object has a companion holding its side-specific state: a CMapObjectServer (class id 0x39) on the server, a
// CMapObjectClientSpecific (0x3A) on the client; both point back at the object (unit.h declares the two
// classes). CMapObject derives from CObject3D (map_object_figures.h); the virtuals past CEngineObject's are
// declared here (CObject3D declares none of its own).

#include "engine_object.h"
#include "map_object_figures.h"   // CObject3D, the base
#include "maths.h"

#include "lib/mfc.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CStream;
class CMapObjectServer;
class CMapObjectClientSpecific;
class CMapObjectCompanion;  // below
struct DeltaState;
class CMemoryStream;        // core_streams.h (net_delta.h: BitStream)
class Terrain;

// An active effect (CArray).
// Effect types seen: 0x09-0x0E shields and sights, 0x0F invisibility, 0x10 silence, 0x11 lichdom,
// 0x13 stench, 0x19 stun, 0x1A antimagic, 0x1C, 0x1D strengthen, 0x1E weaken, 0x1F regeneration,
// 0x20 feeble mind, 0x21 speed, 0x22 slow, 0x25 enlarge, 0x26 shrink.
struct MapEffect {
    std::uint8_t type;
    std::uint8_t power;
    std::uint8_t unused_02[2];
    int ticksLeft;                  // counted down once per world update; removed at 0
    std::uint32_t value;            // a float (EffectValue) or a sound handle
    std::uint32_t strength;         // a float (saved; Rebirth compares it, map_object.cpp)
    std::uint8_t ending;            // the client plays no end sound when set
    std::uint8_t unused_11[3];

    // Type, power, ticksLeft, value and ending cleared (strength not).
    MapEffect* Construct();
    // One tick less; whether any are left (0 / 1).
    int Tick();
    // Type, power, ticksLeft, value and strength, read or written one by one (`ending` is not saved).
    void Load(CStream* stream);
    void Save(CStream* stream) const;
};
static_assert(sizeof(MapEffect) == 0x14);

// The hit points, mana and protection rounded (0x18 bytes in CMapObject; written from the server companion's
// UnitVitals, unit.h).
struct UnitVitalsPacked {
    // Zeroes it. Returns 0.
    int Reset();

    std::uint8_t spare00;           // 0 after Reset; nothing reads it
    std::uint8_t unused_01;
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
struct AttackParams;
struct UnitStats;
struct LockKey;

// The spots a use order may go to (the `out` of the use-spots virtual): an MFC CArray<Vector3>.
using UseSpotArray = mfc::CArray<Vector3>;
static_assert(sizeof(UseSpotArray) == 0x14);

// The game builds most map objects with Construct (which stores the base vtable); a class whose constructor
// is C++ (the torches, levers and traps) gets the compiler's table, built from the same bodies: every
// virtual is defined here (the CObject3D ones among them, two overridden). The bodies registered by address
// keep their names (IsInvisible, ResetScale, the stubs of map_object_stub_slots.cpp, ...); the virtuals call
// them. Names and evidence: docs/notes/map_objects.md ("Virtual slot names").
class CMapObject : public CObject3D {
public:
    int ScaleEffectChangedDefault(int /*scaleEffect*/);
    bool AcceptsAnyAnimation(const void* /*animation*/);
    int AcceptsNoKey(const void* /*key*/);
    void IgnoreArgument(int);
    void SetScriptPosition(const Vector3* point);

    ~CMapObject() override;
    int Create() override { return CreateMapObject(); }
    void Destroy() override { DestroyMapObject(); }
    void ClearState() override {}
    int RunTick() override { return Update(); }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // AttachCompanion (units override it). map_object.cpp: an object of type 0x47 becomes the companion (as
    // AttachCompanion, but a null or other object goes to the 3D object's attach, which takes type 0x46 objects).
    // Returns the companion's new reference count, or 0.
    std::uintptr_t AttachObject(CEngineObject* object) override;
    bool InvisibleSlot() override;
    // Resets the scale and grows or shrinks it by 0.4 per net level of enlarge/shrink (x, y, z)
    // or, without those, strengthen/weaken (x and, except for heroes, z). Components stay >= 0. Marks part 1
    // changed. Returns what the original leaves in EAX (0, or the world when the object is referenced).
    virtual std::uintptr_t RecomputeScale();
    // Empty (units override it). Callers use EAX (0 here).
    virtual std::uintptr_t EffectsChanged();
    // flags_ 0x20000 (bit 17 as 0 / 1); units, levers, traps, torches 1. False ones leave the server's map-object
    // maps after the zone loads (SendFreshObjects). The world server creates such objects only with one flag set,
    // the others with another.
    virtual bool IsSynchronised();
    // The scale becomes the base scale (units override it). Returns the z component's bits.
    virtual std::uint32_t ApplyBaseScale();
    // A hit (DamageInfo) from `attacker` (units: to the controller's TakeHit; other objects ignore it).
    virtual void ReceiveHit(DamageInfo* hit, CEngineObject* attacker, CEngineObject* caster);
    virtual int StatsChanged();        // PackVitals (units override); 1
    // Returns 1 (units hand the argument to their controller's vitals virtual).
    virtual std::uintptr_t ScaleEffectChanged(int scaleEffect);
    // The prototype by name (units: their statistics). map_object_stub_slots.cpp: the vitals of the prototype
    // `prototype` (the owner's BuildFromPrototype asked for no statistics) packed into the rounded vitals. The
    // vitals are a local the original does not clear either.
    virtual void TakePrototype(const char* prototype);
    // Empty; units: a CUnitServer controller (class 0x3E) copies the statistics (CUnitServer::SetStats).
    virtual std::uintptr_t SetStats(const UnitStats* stats);
    // Hands `data` to a CMapObjectServer companion. Returns what the original leaves in EAX.
    virtual std::uintptr_t SetServerData(const void* data);
    // Empty; units: the controller's size-class virtual.
    virtual std::uintptr_t UpdateSizeClasses();
    virtual int IsUsable();            // 0; traps 1, levers NeedsKey
    virtual int IsDirectUseBlocked();  // 39: 0; levers +0x260. Best guess
    virtual std::uint32_t KeyId();     // the key it asks for: -123; levers override
    // A skill used on the object (traps Disarm, levers ApplyKey). object_small_slots.cpp: logs "Science is
    // applied on wrong object" and returns false (AL). Both arguments unread.
    virtual bool ApplyScience(int first, int second);
    // Whether `key` fits: 0; levers KeyOpens, traps KeyFits.
    virtual int AcceptsKey(const LockKey* key);
    // 43 (0xAC): AL 1; levers +0x261. Best guess. The AI map's MarkFootprint
    // refreshes units' passage around the object only when it is true.
    virtual bool KeepsMapFlag();
    // The spots a user of radius `userRadius` may use the object from, into the CArray<Vector3> `out`: FreeArray
    // (none); levers Lever::CollectUseSpots.
    virtual void UseSpots(UseSpotArray* out, float userRadius);
    // Appends a copy of `effect` (the array grown by SetSize), marks part 3 changed and the
    // object dirty (stamped with the world's counter when referenced), then calls the vitals virtual with the
    // argument's dword whose low byte says whether the effect scales the object (strengthen, weaken, enlarge,
    // shrink), the stun virtual (1) for the stun types 0x19 / 0x1C, the scale-change virtual, and the reset-scale
    // virtual for a scaling effect. Returns what the last call leaves in EAX (its low byte 0 without the scale
    // call). Rebirth's FixSpellEffectsOverflow keeps at most 20 effects (a 21st is dropped) and, for an effect
    // lasting more than one tick, removes older ones of the same type, end flag and second float (within 0.001)
    // that last no longer and are no stronger.
    virtual std::uintptr_t AddEffect(const MapEffect* effect);
    // The effect array's SetSize(count, -1) (CArray<MapEffect>, mfc::impl::Plain20's).
    void SetEffectCount(int count);
    virtual std::uint32_t SetStunned(int on);   // ignores it (0); units: the new flags_
    virtual int SetVisibilityMode(int mode);    // hands it to the companion (class_slots.cpp)
    virtual Vector3* EyePoint(Vector3* out);        // where the object sees from (GroundPoint); returns `out`
    virtual std::uint32_t ReadNetState(int first);   // (units override)
    virtual void WriteNetState();      // (units override)
    // The fields 0-3 written into / read from the DeltaState; the reader's result says whether every field's
    // snapshot was consumed. Z = ground height at (x, y) + `offset`.
    virtual void PlaceOnGround(float offset);
    // 52 (0xD0): where the object is at world time `time` (units: along their path; other objects
    // ignore the time, GetPosition). Returns `out`.
    virtual Vector3* PositionAt(Vector3* out, int time);
    virtual Vector3* AimPoint(Vector3* out);        // GetPosition2
    virtual int IsLargeUnit();                      // 54 (0xD8): 0; units +0x278
    virtual int IsDead();                           // 55 (0xDC): non-zero when dead or gone
    // `object` is going away (the world server's RemoveObject tells every object). Empty (0); units: the
    // controller's matching virtual; magic traps override.
    virtual int ForgetObject(CEngineObject* object);
    virtual void UpdateMoveFactors();  // empty; units: the controller's UpdateMoveFactors
    virtual void RefreshWounds();      // empty; units: the controller's hit virtual
    // 59 (0xEC): the speed the object moves at, in ST0 unrounded: 0; units their forward speed.
    virtual double MoveSpeed();
    virtual float Radius();                         // 60 (0xF0): 0.75; units' +0x328, ST0
    // 61 (0xF4): units only keep `name` in their CString +0x238 (the script name); a
    // map object's table ends at 60 (empty here).
    virtual void SetScriptName(const char* name);

    // Records that part `part` (0..15; negative: none) of the object changed, marks it dirty (flags_ bit 1) and
    // stamps it with the world's change counter. Returns what the original leaves in EAX (0, or the world when
    // the object is referenced).
    std::uintptr_t MarkChanged(int part);
    // Has the object an active effect of type `type` (compared with MapEffect::type)?
    int HasEffect(std::uint32_t type);

    // The fields the constructor sets past the 3D object's and the second
    // name, and the destructor's work but for the second name and the effects.
    void SetUpMapObject();
    void TearDownMapObject();

    // --- Rewritten (map_object.cpp) ---

    // The per-tick update: lets the companion update. Returns 1. CUnit's update calls it after its controller's.
    int Update();

    // Removes every effect (frees the array), marks part 3 changed and calls the vitals, stun (0) and scale
    // virtuals. Returns what the scale virtual leaves in EAX.
    std::uintptr_t ClearEffects();

    // The largest MapEffect::value (as a float, at least 0) of the effects of type
    // `type` (compared with the whole low byte); 0 without one. NaN values win the comparison.
    float EffectValue(std::uint32_t type);

    // scale = base scale. Returns the z component's bits (EAX).
    std::uint32_t ResetScale();

    // Copy the position to `out`; return `out`. The first (position) takes a second argument it never reads.
    float* GetPosition(float* out, int unused);
    float* GetPosition2(float* out);

    // The point at the object's offset, moved to the ground height there plus the offset's z. Returns `out`.
    float* GroundPoint(float* out);

    // Replaces the companion: the old one is detached (its +0x1C cleared) and
    // released, the new one points back here and gains a reference.
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

    // Create (another name so it is no override): the 3D object's Create, the companion
    // detached and released, the packed vitals reset (animationSpeed 1 between two resets), 4 network fields
    // (SetSize) reset, changeStamp -1, the effects freed, a state byte 5, the second name emptied, the base scale
    // 0. Returns 1.
    int CreateMapObject();
    // Destroy: the effects emptied (SetSize to 0), the companion detached and released, then the 3D object's
    // Destroy.
    void DestroyMapObject();

    // Load: the companion's Load, then animationSpeed, the position, the orientation, the
    // name, the scale, the effects (count, then MapEffect::Load each), flags_, the second name and, from version
    // 0x4F, the base scale.
    void LoadMapObject(CStream* stream);
    // Save: the same fields (names with their terminators; the base scale always).
    void SaveMapObject(CStream* stream);

    // The hit virtual: the companion's matching virtual with the first two arguments (the
    // third is not read). Both companions return 0.
    int CompanionQuery(int first, int second, int unused);
    // Frees the data of the MFC array `array` (operator delete) and empties it (data, size, capacity). The second
    // argument is unread.
    void FreeArray(UseSpotArray* array, int unused);

    // The visibility mode virtual with mode 0 for `level` 0 (or NaN), 1 for 1, else 2. Returns its EAX.
    int SetVisibilityLevel(float level);

    // The virtuals a plain map object leaves empty are free functions there; these read
    // the object. Whether the object is dead: the companion's dead virtual (a tail call).
    int CompanionIsDead();
    // The sector map's Remove. The companion's virtual (empty in both); returns 1.
    int NotifyCompanion();
    // The network state written in full / only its changed parts (the statistics, repacked first when part 0 or 4
    // changed): the write mode, the write virtual, the state finished; flags_ bit 1 cleared.
    void WriteFullState();
    void WriteChangedState();
    // The network state read from `reader`: BeginRead, the read virtual with `first`, the state finished. Returns
    // its EAX.
    std::uint32_t ReadState(int first, CMemoryStream* reader);
    // The second name becomes `text`. Returns the string.
    mfc::CString* SetSecondName(const char* text);

    // The object as its CObject3D base (map_object_figures.h).
    CObject3D* Object3D() { return this; }

    // The packed vitals and the network state.
    UnitVitalsPacked* PackedVitals() { return &packedVitals; }
    DeltaState* NetState() { return reinterpret_cast<DeltaState*>(netState); }


    // The world the object lives in (via its owner), or null.
    CWorld* World();

    // +0x000-0x16C: CObject3D (position +0x1C, orientation +0x34, the side half `attached` +0x94,
    // name +0xA4 (the prototype name for units), scale +0xC4, bounds centre +0x128 (GroundPoint),
    // radius +0x134, half box `above` +0x144).
    CMapObjectCompanion* companion; // CMapObjectServer / CMapObjectClientSpecific
    // The companion as the server's CMapObjectServer (on the server; defined in unit.h).
    CMapObjectServer* ServerCompanion() const;
    // The companion as the client's CMapObjectClientSpecific (on the client; defined in unit.h).
    CMapObjectClientSpecific* ClientCompanion() const;
    // +0x174 world updates until the object's effects are next looked at (5 after Create; the
    // server world's update map sets it, world_units.cpp / world_server.cpp).
    int updateCountdown;
    float animationSpeed;           // the animation's speed factor (CUnit::animationSpeed; 1 after Create)
    UnitVitalsPacked packedVitals;  // hit points +0x17E, maximum +0x180, mana +0x182, maximum +0x184
    void* effectsVtable;            // CArray<MapEffect> (SetEffectCount is its SetSize)
    MapEffect* effects;             // |
    int effectCount;                // |
    int effectCapacity;             // |
    int effectGrowBy;               // /
    mfc::CString name2;
    Vector3 baseScale;
    int sectorCellX;
    int sectorCellY;
    int changeStamp;                // world + 0x68 when last changed
    // +0x1C4 the network state (DeltaState, net_delta.h, up to +0x238: its field records, masks
    // and two memory streams);  NetState(). Its mask is changedParts.
    std::uint8_t netState[0x1DC - 0x1C4];
    std::uint16_t changedParts;     // bit per part changed (MarkChanged)
    std::uint8_t unused_1DE[0x238 - 0x1DE];    // two memory streams and +0x210

protected:
    CMapObject();
};
static_assert(sizeof(CObject3D) == 0x170);
static_assert(offsetof(CMapObject, companion) == 0x170);
static_assert(offsetof(CMapObject, effects) == 0x198);
static_assert(offsetof(CMapObject, name2) == 0x1A8 && offsetof(CMapObject, baseScale) == 0x1AC);
static_assert(offsetof(CMapObject, changeStamp) == 0x1C0);
static_assert(offsetof(CMapObject, changedParts) == 0x1DC);
static_assert(sizeof(CMapObject) == 0x238);   // CUnit's fields start at +0x238

// The virtual the verify wrapper of RecomputeScale reads to tell plain objects from units.
namespace mapslot {
constexpr int kResetScale = 30;           // 0x78
} // namespace mapslot

// CMapObjectVirtuals is the name callers outside the map object files reach the virtuals by (CUnit derives
// from CMapObject).
using CMapObjectVirtuals = CMapObject;

// A map object's companion (CMapObjectServer / CMapObjectClientSpecific, unit.h, derive from it). CompanionSlots
// takes an object handed over as a companion.
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

    // The base's empty bodies the companions' tables pointed at (registered at their addresses).
    void HitNothing(int, int);
    void IgnoreArgument(int);
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
    virtual int IsOverkilled() = 0;  // see unit.h
    // Client and server (the server's is empty). Neither sets a result; the objects' visibility virtual passes
    // EAX on, which nothing reads, so it is void.
    virtual void SetLightMode(int) {}

    CMapObject* owner;              // the map object (CMapObject::AttachCompanion sets it)
};
static_assert(offsetof(CMapObjectCompanion, owner) == 0x1C);
inline CMapObjectCompanion* CompanionSlots(CEngineObject* companion) {
    return static_cast<CMapObjectCompanion*>(companion);
}

// An engine object known to be a map object (a unit, a lever, a trap ...): its slots and fields.
inline CMapObject* ObjectSlots(CEngineObject* object) { return static_cast<CMapObject*>(object); }

// The companion's back pointer (+0x1C of CMapObjectServer / CMapObjectClientSpecific).
inline CMapObject*& CompanionOwner(CEngineObject* companion) { return CompanionSlots(companion)->owner; }

// CTorchObject::CreateTorch (torch Create): torch.h, map_object_create.cpp.
