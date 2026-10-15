#pragma once

// The base class of every map object (units, trees, buildings, stones, levers, traps, torches ...):
// vtable 0073DAA0 (abstract: slots 11 / 12 are _purecall), constructor 0050E2D0, destructor body
// 0050E660. It derives from the 3D object base (vtable 0073DDD4, constructor 00515BD0, 0x170
// bytes: position, orientation, figure parts, scale), which derives from CTransformObject
// (0073BA38) and CEngineObject. CUnit's own fields start at +0x238.
//
// Each map object has a companion at +0x170 holding its side-specific state: a CMapObjectServer
// (class id 0x39, vtable 0073DD6C) on the server, a CMapObjectClientSpecific (0x3A, 0073DBEC) on the
// client; both point back at the object through their +0x1C (unit.h declares the two classes).
//
// CMapObject derives from CObject3D (map_object_figures.h), which holds the first 0x170 bytes; the
// virtual slots past CEngineObject's are declared here (CObject3D declares none of its own).

#include "engine_object.h"
#include "map_object_figures.h"   // CObject3D, the base
#include "maths.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CStream;              // core_streams.h
struct DeltaState;          // net_delta.h
class CMemoryStream;        // core_streams.h (net_delta.h: BitStream)

// An active effect (0x14 bytes; CArray at +0x194).
// Effect types seen: 0x09-0x0E shields and sights, 0x0F invisibility, 0x10 silence, 0x11 lichdom,
// 0x13 stench, 0x19 stun, 0x1A antimagic, 0x1C, 0x1D strengthen, 0x1E weaken, 0x1F regeneration,
// 0x20 feeble mind, 0x21 speed, 0x22 slow, 0x25 enlarge, 0x26 shrink.
struct MapEffect {
    std::uint8_t type;              // +0x00
    std::uint8_t power;             // +0x01
    std::uint8_t unused_02[2];
    int ticksLeft;                  // +0x04 counted down once per world update; removed at 0
    std::uint32_t value;            // +0x08 a float (EffectValue) or a sound handle
    std::uint32_t strength;         // +0x0C a float (saved; Rebirth compares it, map_object.cpp)
    std::uint8_t ending;            // +0x10 the client plays no end sound when set
    std::uint8_t unused_11[3];

    // 00688510 (spell_lifetime.cpp): type, power, ticksLeft, value and ending cleared (strength
    // not). Returns this.
    MapEffect* Construct();
    // 00688530 (spell_lifetime.cpp, A-H25; the server object's update). One tick less; whether
    // any are left (0 / 1).
    int Tick();
    // 00688550 / 006885A0 (spell_save.cpp; RET 4): type, power, ticksLeft, value and strength,
    // read or written one by one (`ending` is not saved).
    void Load(CStream* stream);
    void Save(CStream* stream) const;
};
static_assert(sizeof(MapEffect) == 0x14);

// The hit points, mana and protection rounded (CMapObject + 0x17C, 0x18 bytes; 00522C50 writes it from
// the server companion's UnitVitals, unit.h).
struct UnitVitalsPacked {
    // 005233B0. Zeroes it. Returns 0 (EAX).
    int Reset();

    std::uint8_t spare00;           // +0x00 0 after Reset; nothing reads it
    std::uint8_t unused_01;
    std::int16_t hitPoints;         // +0x02
    std::int16_t maxHitPoints;      // +0x04
    std::uint16_t mana;             // +0x06 (read unsigned)
    std::uint16_t maxMana;          // +0x08
    std::int16_t protection[7];     // +0x0A
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

struct DamageInfo;   // spell.h
struct AttackParams; // combat.h
struct UnitStats;    // unit.h
struct LockKey;      // unit.h

// The game builds map objects (constructor 0050E2D0 stores the vtable): novtable, and the virtual
// slots past CEngineObject's are declared in vtable order (0073DAA0; CUnit's 0073EA8C has the same
// slots). Slots declared `void Name()` keep their place in the order only (their real arguments are
// not declared; name them properly when calling them). The rewritten slots that are declared here
// are registered through __fastcall hooks (rewrite guide rule 4); the others are members of another
// name registered by address (IsInvisible for slot 21, ResetScale for 30, ...), or the shared stub
// bodies of map_object_stub_slots.cpp. Names and evidence: docs/notes/map_objects.md ("Virtual slot names").
class __declspec(novtable) CMapObject : public CObject3D {
public:
    int ScaleEffectChangedDefault(int /*scaleEffect*/);
    bool AcceptsAnyAnimation(const void* /*animation*/);
    int AcceptsNoKey(const void* /*key*/);
    void IgnoreArgument(int);
    void SetScriptPosition(const Vector3* point);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073DAA0);

    virtual void CopyTransform();      // 13: CTransformObject 00401490
    virtual void CombineRotation();    // 14: CObject3D 00516370 (CombineRotation3D)
    virtual void UpdateWorld();        // 15: CTransformObject 0040CC30
    virtual void SaveTransform();      // 16: 00516350 (00401510)
    virtual void LoadTransform();      // 17: 00516360 (00401540)
    // 18 (0x48): whether the object may play the animation entry (FigureAnimation, map_object_figures.h);
    // 0050E400 for CObject3D / CMapObject, CUnit 00532DA0 (AcceptsTarget: its flags +0x18 for heroes).
    virtual bool AcceptsAnimation(const void* animation);
    // 19 (RET 4): units 0052E870 AttachCompanion. 0050E890 (map_object.cpp): an object of type 0x47
    // becomes the companion (as AttachCompanion, but a null or other object goes to the 3D object's
    // slot 19, 00516D50, which takes type 0x46 objects at +0x94). Returns the companion's new
    // reference count, or what 00516D50 leaves.
    virtual std::uintptr_t AttachObject(CEngineObject* object);
    std::uintptr_t AttachObjectImpl(CEngineObject* object);   // body of the slot above (registered by member pointer)
    virtual void CopyFigure();         // 20: CObject3D 005161E0
    virtual bool InvisibleSlot();   // 21 (0x54): 0050FA10 IsInvisible
    // 22 / 23 / 24 (0x58 / 0x5C / 0x60, RET 4): draw the figure / draw it and collect its screen
    // rectangle / draw its shadow with the scene context (CObject3D 00516460 / 00516420 / 00516490:
    // the side half's slots 19 / 20; 1 without one).
    virtual int DrawFigure(void* context);
    virtual int DrawCollectDirty(void* context);
    virtual int DrawFigureShadow(void* context);
    virtual void AdvanceTime(float time);   // 25: 0050E410 empty; units 0052FC40
    // 26: loads the figure (CObject3D 00517500, units 0052E910). Returns 1 (or the loader's EAX).
    virtual std::uintptr_t LoadFigures();
    // 27: units 00532A80. 0050F2D0 (map_object.cpp): resets the scale (slot 30) and grows or shrinks
    // it by 0.4 per net level of enlarge/shrink (x, y, z) or, without those, strengthen/weaken (x
    // and, except for heroes, z). Components stay >= 0. Marks part 1 changed. Returns what the
    // original leaves in EAX (0, or the world when the object is referenced).
    virtual std::uintptr_t RecomputeScale();
    // 28: 0050E420 empty (units 00532AB0). Callers use EAX.
    virtual std::uintptr_t EffectsChanged();
    // 29 (AL): flags_ 0x20000 (0050E430, bit 17 as 0 / 1); units (004467E0), levers, traps,
    // torches 1. False ones leave the server's map-object maps after the zone loads
    // (SendFreshObjects). The world server creates such objects only with its +0x11D set, the
    // others with +0x11E (005927C0).
    virtual bool IsSynchronised();
    // 30 (0x78): the scale becomes the base scale (+0x1AC; 0050E440, units 00532AA0). Returns the z
    // component's bits.
    virtual std::uint32_t ApplyBaseScale();
    // 31 (0x7C): a hit (DamageInfo) from `attacker` (units: to the controller's TakeHit; other
    // objects ignore it, 0050E460).
    virtual void ReceiveHit(DamageInfo* hit, CEngineObject* attacker, CEngineObject* caster);
    virtual int StatsChanged();        // 32: 0050E970 PackVitals (units 0052EB50); 1
    // 33 (0x84): 0050E960; units 0052EB30 hand the argument to
    // their controller's slot 19.
    virtual std::uintptr_t ScaleEffectChanged(int scaleEffect);
    // 34 (0x88, RET 4): the prototype by name (units 0052EB70: their statistics). 0050E9B0
    // (map_object_stub_slots.cpp): the vitals of the prototype `prototype` (the owner's
    // BuildFromPrototype, 005136D0, asked for no statistics) packed into +0x17C. The vitals are a
    // local the original does not clear either.
    virtual void TakePrototype(const char* prototype);
    void TakePrototypeImpl(const char* prototype);   // body of the slot above (registered by member pointer)
    // 35 (0x8C): 0050E480 empty; units 00530A20: a CUnitServer controller (class 0x3E) copies the
    // statistics (00552F40, CUnitServer::SetStats).
    virtual std::uintptr_t SetStats(const UnitStats* stats);
    // 36 (RET 4): 0050F050 (map_object.cpp) hands `data` (0x58 bytes) to a CMapObjectServer companion.
    // Returns what the original leaves in EAX.
    virtual std::uintptr_t SetServerData(const void* data);
    std::uintptr_t SetServerDataImpl(const void* data);   // body of the slot above (registered by member pointer)
    // 37 (0x94): 0050E490 empty; units 00530A50, the controller's slot 23 (CUnitServer 00552FB0).
    virtual std::uintptr_t UpdateSizeClasses();
    virtual int IsUsable();            // 38: 0 (0050F0C0); traps 1, levers NeedsKey 0052BB00
    virtual int IsDirectUseBlocked();  // 39: 0 (0050F0D0); levers +0x260 (0052B2D0). Best guess
    virtual std::uint32_t KeyId();     // 40: the key it asks for: -123 (0050F0E0); levers 0052BBC0
    // 41 (a skill used on the object; traps Disarm, levers ApplyKey). 0050F0F0 (object_small_slots.cpp):
    // logs "Science is applied on wrong object" (0044C470) and returns false (AL). Both arguments unread.
    virtual bool ApplyScience(int first, int second);
    bool ApplyScienceImpl(int first, int second);   // body of the slot above (registered by member pointer)
    // 42 (0xA8): whether `key` fits: 0 (0050F110, RET 4); levers KeyOpens, traps KeyFits.
    virtual int AcceptsKey(const LockKey* key);
    // 43 (0xAC): AL 1 (004467B0); levers +0x261 (0052B2C0). Best guess. The AI map's MarkFootprint
    // (005B6A80) refreshes units' passage around the object only when it is true.
    virtual bool KeepsMapFlag();
    // 44 (0xB0, RET 8): the spots a user of radius `userRadius` may use the object from, into the
    // CArray<Vector3> `out`: 0050F120 FreeArray (none); levers 0052BBE0 Lever::CollectUseSpots.
    virtual void UseSpots(void* out, float userRadius);
    // 45: 0050F1E0 (map_object.cpp): appends a copy of `effect` (the array grown by SetSize 0050FE70),
    // marks part 3 changed and the object dirty (stamped with the world's +0x68 when referenced),
    // then calls slot 33 with the argument's dword whose low byte says whether the effect scales
    // the object (strengthen, weaken, enlarge, shrink), slot 46 (1) for the stun types 0x19 / 0x1C,
    // slot 28, and slot 27 for a scaling effect. Returns what the last call leaves in EAX (its low
    // byte 0 without slot 27). Rebirth's FixSpellEffectsOverflow keeps at most 20 effects (a 21st
    // is dropped) and, for an effect lasting more than one tick, removes older ones of the same type,
    // end flag and +0x0C (within 0.001) that last no longer and are no stronger.
    virtual std::uintptr_t AddEffect(const MapEffect* effect);
    std::uintptr_t AddEffectImpl(const MapEffect* effect);   // body of the slot above (registered by member pointer)
    virtual std::uint32_t SetStunned(int on);   // 46 (RET 4): 0050E4A0 ignores it; units 0052F7F0 (the new flags_)
    virtual int SetVisibilityMode(int mode);    // 47 (0xBC): 004467C0 hands it to the companion
    virtual Vector3* EyePoint(Vector3* out);        // 48 (0xC0): where the object sees from; returns `out`
    virtual std::uint32_t ReadNetState(int first);   // 49 (RET 4): 0050EC30 (units 0052EF90)
    virtual void WriteNetState();      // 50: 0050EAF0 (units 0052EC70)
    // 51 (RET 4; units 0052F740). 0050F020 (map_object.cpp): z = ground height at (x, y) + `offset`.
    virtual void PlaceOnGround(float offset);
    void PlaceOnGroundImpl(float offset);   // body of the slot above (registered by member pointer)
    // 52 (0xD0): where the object is at world time `time` (units: along their path; other objects
    // ignore the time, GetPosition). Returns `out`.
    virtual Vector3* PositionAt(Vector3* out, int time);
    virtual Vector3* AimPoint(Vector3* out);        // 53 (0xD4)
    virtual int IsLargeUnit();                      // 54 (0xD8): 0; units +0x278 (00446890)
    virtual int IsDead();                           // 55 (0xDC): non-zero when dead or gone
    // 56 (RET 4; the world server's RemoveObject tells every object): `object` is going away.
    // Empty (0050E510); units: the controller's slot 42; magic traps 0052AC90.
    virtual int ForgetObject(CEngineObject* object);
    virtual void UpdateMoveFactors();  // 57: empty; units: the controller's slot 53
    virtual void RefreshWounds();      // 58: 0; units: the controller's slot 31
    // 59 (0xEC): the speed the object moves at, in ST0 unrounded: 0 (0050E540); units their forward
    // speed (004468B0).
    virtual double MoveSpeed();
    virtual float Radius();                         // 60 (0xF0): units' +0x328 (004468A0), ST0
    // 61 (0xF4): units (00446830) keep `name` in their CString +0x238 (the script name).
    virtual void SetScriptName(const char* name);

    // 0050E9F0. Records that part `part` (0..15; negative: none) of the object changed, marks it
    // dirty (flags_ bit 1) and stamps it with the world's change counter. Returns what the original
    // leaves in EAX (0, or the world when the object is referenced). It touches nothing beyond +0x1DE.
    std::uintptr_t MarkChanged(int part);
    // 0050F630. Has the object an active effect of type `type` (compared with MapEffect::type)?
    int HasEffect(std::uint32_t type);

    // map_object_lifetime.cpp.
    // 0050E2D0. The constructor: the 3D object (00515BD0), the effect array (vtable 0073DBA0),
    // the second name, the network record array and its two memory streams (+0x1C4), then the
    // vtable 0073DAA0, no companion, packed vitals cleared, no sector cell, animationSpeed 1. Returns
    // this.
    CMapObject* Construct();
    // 0050E660. The destructor body: the streams' buffers, the network records (00510340), the
    // second name and the effects freed, then the 3D object's destructor body.
    void DestructBody();

    // --- Rewritten (map_object.cpp) ---

    // 0050F1C0. Virtual slot 6, the per-tick update: lets the companion (+0x170) update (its slot
    // 6). Returns 1. CUnit's slot 6 (0052F7D0) calls it after its controller's update.
    int Update();

    // 0050F5B0. Removes every effect (frees the array), marks part 3 changed and calls slots 33
    // (1), 46 (0) and 27. Returns what slot 27 leaves in EAX.
    std::uintptr_t ClearEffects();

    // 0050F690. The largest MapEffect::value (as a float, at least 0) of the effects of type
    // `type` (compared with the whole low byte); 0 without one. NaN values win the comparison.
    float EffectValue(std::uint32_t type);

    // 0050E440. Virtual slot 30: scale = base scale (+0x1AC). Returns the z component's bits (EAX).
    std::uint32_t ResetScale();

    // 0050E4B0 / 0050E4D0. Virtual slots 52 / 53: copy the position to `out`; return `out`. Slot
    // 52 takes a second argument it never reads (RET 8).
    float* GetPosition(float* out, int unused);
    float* GetPosition2(float* out);

    // 0050F730. Virtual slot 48: the point at the object's offset (+0x128), moved to the ground
    // height there plus the offset's z. Returns `out`.
    float* GroundPoint(float* out);

    // 0050E8F0. Replaces the companion (+0x170): the old one is detached (its +0x1C cleared) and
    // released, the new one points back here and gains a reference.
    void AttachCompanion(CEngineObject* companion);

    // 0050E970. Virtual slot 32 (statistics changed): a CMapObjectServer companion packs its
    // vitals (00522C50) into +0x17C. Returns 1, or 0 for another companion.
    int PackVitals();
    // map_object_vitals.cpp (A-U5). 0050F080 (the server's unit setup, 0058EF20): a CMapObjectServer
    // companion (class 0x39) gets the base of each protection (vitals +0x20, 7 pairs) zeroed.
    void ClearProtectionBases();

    // 0050FA10 / 0050FA20 / 0050E430. Bits 16 (invisible: set by the client for effect 0x0F),
    // 18 (stunned: effect 0x19) and 17 of flags_. 0050FA10 and 0050E430 are virtual slots 21 / 29.
    std::uint32_t IsInvisible();
    std::uint32_t IsStunned();
    std::uint32_t Flag20000();

    // map_object_create.cpp (A-U).
    // 0050E770. Virtual slot 2 (Create; another name so it is no override): the 3D object's
    // Create (00515F70), the companion detached and released, the packed vitals reset (animationSpeed 1
    // between two resets), 4 network fields (SetSize 0050FA30) reset (0047FB70), changeStamp -1,
    // the effects freed, +0x174 = 5, the second name emptied, the base scale 0. Returns 1.
    int CreateMapObject();
    // 0050E840. Virtual slot 3 (Destroy): the effects emptied (SetSize 0050FE70 to 0), the
    // companion detached and released, then the 3D object's Destroy (00516080).
    void DestroyMapObject();

    // map_object_save.cpp (A-U2).
    // 0050F7A0. Virtual slot 9 (Load): the companion's Load, then animationSpeed, the position, the
    // orientation, the name, the scale, the effects (count, then MapEffect::Load 00688550 each),
    // flags_, the second name and, from version 0x4F, the base scale.
    void LoadMapObject(CStream* stream);
    // 0050F900. Virtual slot 10 (Save): the same fields (names with their terminators; the base
    // scale always).
    void SaveMapObject(CStream* stream);

    // object_small_slots.cpp (A-U2). 0050E460, virtual slot 31: the companion's slot 15 with the
    // first two arguments (the third is not read). Both companions return 0.
    int CompanionQuery(int first, int second, int unused);
    // 0050F120. Virtual slot 44: frees the data of the MFC array `array` (operator delete) and
    // empties it (data, size, capacity). The second argument is unread.
    void FreeArray(void* array, int unused);

    // unit_vision.cpp (A-U3). 0050F170 (RET 4): slot 47 with mode 0 for `level` 0 (or NaN), 1 for
    // 1, else 2. Returns slot 47's EAX.
    int SetVisibilityLevel(float level);

    // map_object_stub_slots.cpp (A-U12). The slots a plain map object leaves empty are free
    // functions there; these read the object.
    // 0050E560 (slot 0, RET 4). DestructBody, then operator delete with flags bit 0. Returns this.
    CMapObject* DeleteMapObject(unsigned flags);
    // 0050E500 (slot 55). Whether the object is dead: the companion's slot 17 (a tail call).
    int CompanionIsDead();
    // 0050E930 (the sector map's Remove). The companion's slot 14 (empty in both); returns 1.
    int NotifyCompanion();
    // 0050EA40 / 0050EA70. The network state written in full / only its changed parts (the
    // statistics, slot 32, repacked first when part 0 or 4 changed): the write mode, slot 50, the
    // state finished (0047FE90); flags_ bit 1 cleared.
    void WriteFullState();
    void WriteChangedState();
    // 0050EAB0 (RET 8). The network state read from `reader`: BeginRead, slot 49 with `first`, the
    // state finished. Returns slot 49's EAX.
    std::uint32_t ReadState(int first, CMemoryStream* reader);
    // 0050F150 (RET 4). The second name (+0x1A8) becomes `text`. Returns the string.
    mfc::CString* SetSecondName(const char* text);
    // 00516350 / 00516360 (slots 16 / 17, RET 4; CObject3D's too). The transform written to /
    // read from the MFC file (CTransformObject 00401510 / 00401540, called directly).
    void SaveTransformSlot(void* file);
    void LoadTransformSlot(void* file);

    // The object as its CObject3D base (map_object_figures.h).
    CObject3D* Object3D() { return this; }

    // The packed vitals (+0x17C) and the network state (+0x1C4).
    UnitVitalsPacked* PackedVitals() { return &packedVitals; }
    DeltaState* NetState() { return reinterpret_cast<DeltaState*>(netState); }

    // --- Not rewritten (forwarders) ---

    // 00455950. The world the object lives in (owner_ + 0xAC, its slot 13), or null.
    void* World();

    // +0x000-0x16C: CObject3D (position +0x1C, orientation +0x34, the side half `attached` +0x94,
    // name +0xA4 (the prototype name for units), scale +0xC4, bounds centre +0x128 (GroundPoint),
    // radius +0x134, half box `above` +0x144).
    CEngineObject* companion;       // +0x170 CMapObjectServer / CMapObjectClientSpecific
    // +0x174 world updates until the object's effects are next looked at (5 after Create; the
    // server world's update map sets it, world_units.cpp / world_server.cpp).
    int updateCountdown;
    float animationSpeed;           // +0x178 the animation's speed factor (CUnit::animationSpeed; 1 after Create)
    UnitVitalsPacked packedVitals;  // +0x17C hit points +0x17E, maximum +0x180, mana +0x182, maximum +0x184
    void* effectsVtable;            // +0x194 \ CArray<MapEffect> (vtable 0073DBA0; SetSize 0050FE70)
    MapEffect* effects;             // +0x198 |
    int effectCount;                // +0x19C |
    int effectCapacity;             // +0x1A0 |
    int effectGrowBy;               // +0x1A4 /
    char* name2;                    // +0x1A8 CString
    Vector3 baseScale;              // +0x1AC
    int sectorCellX;                // +0x1B8
    int sectorCellY;                // +0x1BC
    int changeStamp;                // +0x1C0 world + 0x68 when last changed
    // +0x1C4 the network state (DeltaState, net_delta.h, up to +0x238: its field records, masks
    // and two memory streams at +0x1E8 / +0x210); NetState(). Its mask is changedParts.
    std::uint8_t netState[0x1DC - 0x1C4];
    std::uint16_t changedParts;     // +0x1DC bit per part changed (MarkChanged)
    std::uint8_t unused_1DE[0x238 - 0x1DE];    // two memory streams at +0x1E8 and +0x210
};
static_assert(sizeof(CObject3D) == 0x170);
static_assert(offsetof(CMapObject, companion) == 0x170);
static_assert(offsetof(CMapObject, effects) == 0x198);
static_assert(offsetof(CMapObject, baseScale) == 0x1AC);
static_assert(offsetof(CMapObject, changeStamp) == 0x1C0);
static_assert(offsetof(CMapObject, changedParts) == 0x1DC);
static_assert(sizeof(CMapObject) == 0x238);   // CUnit's fields start at +0x238
// net_state.cpp. 0050EAF0 / 0050EC30 (slots 50 / 49): the map object's network state writer and reader
// (fields 0-3; the reader's result says whether every field's snapshot was consumed).
void __fastcall MapObjectWriteNetState(std::uint8_t* object);
std::uint32_t __fastcall MapObjectReadNetState(std::uint8_t* object, void*, int first);

// The light manager (*007AF738; camera.h's TheTerrain reads the same global): the map object files
// hand it to the light calls (add, remove, aim, IsLit) as their `this`.
inline void* LightManager() { return *reinterpret_cast<void* const*>(0x007AF738); }

// The slot the verify wrapper of RecomputeScale reads to tell plain objects from units.
namespace mapslot {
constexpr int kResetScale = 30;           // 0x78
} // namespace mapslot

// CMapObjectVirtuals is the name callers outside the map object files reach the slots by (CUnit,
// vtable 0073EA8C, derives from CMapObject).
using CMapObjectVirtuals = CMapObject;

// A map object's companion (CMapObjectServer / CMapObjectClientSpecific) as the base class calls it.
// The two classes (unit.h) do not derive from it; CompanionSlots reaches a companion through it.
class __declspec(novtable) CMapObjectCompanion : public CEngineObject {
public:
    void HitNothing(int, int);
    int QueryNothing(int, int);
    void IgnoreArgument(int);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073DC54);

    virtual void EffectsChanged(int first);   // 13: client 005117D0, server 005106D0 (empty, RET 4)
    virtual void Unused14();         // 14: empty in both (client 005104B0, server 00515810)
    virtual int Query(int first, int second);   // 15: 005104C0 in both (0)
    // 16: a hit of `damage` with the hit-kind bits `kind` (1, 2, 4). Server 005106F0 (empty, RET 8);
    // client 005117F0: for a living, visible-flag (0x400000) object, 0056CE40 with the object's id.
    virtual void Hit(int damage, int kind);
    // 17 (0x44): whether the object is dead (server 005156A0: hit points rounded to 16 bits <= 0;
    // client 005104D0: the unit's hit points <= 0).
    virtual int IsDead();
    virtual int IsOverkilled();      // 18: server 005156C0, client 005104F0 (unit.h)
    // 19 (0x4C, RET 4): client 00511A00 (CMapObjectClientSpecific::SetLightMode), server 00510700
    // (empty). Neither sets a result; the objects' slot 47 (004467C0) passes EAX on as it is.
    virtual std::uintptr_t SetLightMode(int mode);

    // map_object_companion_slots.cpp: the base's own bodies of CEngineObject's slots (other names,
    // so they are no overrides). 00510730: the destructor body (the base's vtable, no map object,
    // the engine object's). 00510710 (slot 0, RET 4): that, then operator delete with flags bit 0;
    // returns this. 00510750 / 00510760 (slots 2 / 3): no map object (Create returns 1). 005106E0
    // (slot 6, the per-tick update): 0.
    void DestructCompanion();
    CMapObjectCompanion* DeleteCompanion(unsigned flags);
    int CreateCompanion();
    void DestroyCompanion();
    int UpdateCompanion();

    CMapObject* owner;              // +0x1C the map object (CMapObject::AttachCompanion sets it)
};
static_assert(offsetof(CMapObjectCompanion, owner) == 0x1C);
inline CMapObjectCompanion* CompanionSlots(CEngineObject* companion) {
    return static_cast<CMapObjectCompanion*>(companion);
}

// A unit's server half (CUnit::controller, +0x240: CUnitServer, class 0x3E) as callers reach it
// through its vtable (the slots as unit.h's CUnitController names them).
class CUnitServerVirtuals : public CEngineObject {
public:
    virtual void FiguresLoaded() = 0;      // 13
    virtual void RestartAnimation() = 0;   // 14
    virtual void Unused15() = 0;           // 15: empty
    virtual void Path() = 0;               // 16
    virtual void Unused17() = 0;           // 17: empty
    virtual void PlayerChanged() = 0;      // 18
    virtual int RefreshVitals(char withHitPoints) = 0;   // 19: 005533B0 (1), client 00533BD0 (0)
    // 20 (0x50): 00553F40 CUnitServer::TakeHit(hit, attacker, source); either unit may be null.
    virtual int TakeHitSlot(DamageInfo* hit, CEngineObject* attacker, CEngineObject* source) = 0;
    virtual int Heal(float amount) = 0;     // 21: 005543B0 (1)
    virtual void RepackStats() = 0;        // 22: 00553500
    virtual void UpdateSizeClasses() = 0;  // 23: 00552FB0
    virtual int Level() = 0;               // 24: 00556760
    // 25 (0x64, RET 4): 00553610 CUnitServer::TakeUpWeapon(slot); 1 when the weapon was taken up.
    virtual int TakeUpWeapon(int slot) = 0;
    virtual bool AddArmourLayerOf(std::uint32_t key) = 0;    // 26: 00553560
    virtual bool RebuildArmourLayers(int unused) = 0;        // 27: 005535C0
    virtual int ClearOuterArmourLayers() = 0;                // 28: 005535E0
    virtual int RefreshEquipment() = 0;                      // 29: 005535F0
    virtual void RefreshModel() = 0;                         // 30: base 00533C40
    virtual void RefreshWounds() = 0;                        // 31: base 00533C50
    virtual void SetArmourModels(const std::uint32_t* ids, std::uint32_t count) = 0;  // 32: base 00533C60
    virtual void SetWeaponModels(const std::uint32_t* ids, std::uint32_t count) = 0;  // 33: base 00533C70
    virtual bool WeaponAdded(std::uint32_t itemId) = 0;      // 34: base 00533C80
    virtual void RemoveWeaponModel() = 0;                    // 35: base 00533C90
    virtual int HasOtherArmour() = 0;                        // 36: 005567A0
    virtual int HasMainArmour() = 0;                         // 37: 005567E0
    virtual void ForgetClaims() = 0;                         // 38: 00552CD0
    virtual int TameStage() = 0;                             // 39: 005460A0
    virtual void AdvanceTime(float time) = 0;                // 40: base 00533CC0
    virtual void MotionChanged() = 0;                        // 41: base 00533CD0
    virtual void OnObjectDeleted(CEngineObject* object) = 0; // 42: 00552670
    virtual void RestartFadeIn() = 0;                        // 43: base 00533CF0
    virtual void Resettle() = 0;                             // 44: 00553260
    virtual void UpdateMoveFactors() = 0;                    // 45: 005536B0
    virtual void StatChanged(std::uint32_t stat, float change, std::uint8_t flags) = 0;   // 46: base 00533D20 (RET 0xC)
    virtual void Wound(float lost) = 0;                      // 47: base 00533D30 (RET 4)
    virtual void Unused48() = 0;                             // 48: base 00533D40 (empty)
    virtual void UpdateSpellBlockFlag() = 0;                 // 49: 00553220
    virtual void GetAttackParams(AttackParams* out) = 0;     // 50 (0xC8): 00555A00
};
inline CUnitServerVirtuals* ServerSlots(CEngineObject* controller) { return static_cast<CUnitServerVirtuals*>(controller); }

// An object (a map object or a unit) seen through its virtual slots.
inline CMapObject* ObjectSlots(CEngineObject* object) { return static_cast<CMapObject*>(object); }

// The companion's back pointer (+0x1C of CMapObjectServer / CMapObjectClientSpecific).
inline CMapObject*& CompanionOwner(CEngineObject* companion) { return CompanionSlots(companion)->owner; }

// CTorchObject::CreateTorch (0052BFC0, torch Create): torch.h, map_object_create.cpp.
