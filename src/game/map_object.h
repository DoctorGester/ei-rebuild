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
// Names are provisional. The virtual interface is not declared (the derived classes override
// most slots); slots are called through ei::VCall, and the rewritten slots are plain members.

#include "engine_object.h"

#include <cstddef>
#include <cstdint>

class CStream;              // core_streams.h
struct DeltaState;          // net_delta.h
struct UnitVitalsPacked;    // unit.h
class CObject3D;            // map_object_figures.h

// An active effect (0x14 bytes; CArray at +0x194, the same record as UnitEffect in unit.h).
// Effect types seen: 0x09-0x0E shields and sights, 0x0F invisibility, 0x10 silence, 0x11 lichdom,
// 0x13 stench, 0x19 stun, 0x1A antimagic, 0x1C, 0x1D strengthen, 0x1E weaken, 0x1F regeneration,
// 0x20 feeble mind, 0x21 speed, 0x22 slow, 0x25 enlarge, 0x26 shrink.
struct MapEffect {
    std::uint8_t type;              // +0x00
    std::uint8_t power;             // +0x01
    std::uint8_t unknown_02[2];
    int ticksLeft;                  // +0x04 counted down once per world update; removed at 0
    std::uint32_t value;            // +0x08 a float (EffectValue) or a sound handle
    std::uint32_t unknown_0C;
    std::uint8_t ending;            // +0x10 the client plays no end sound when set
    std::uint8_t unknown_11[3];
};
static_assert(sizeof(MapEffect) == 0x14);

// Effect types with a special meaning for the base class.
namespace mapeffect {
constexpr int kStun = 0x19;
constexpr int kStun2 = 0x1C;           // also keeps flag 0x1000000 (with kStun)
constexpr int kStrengthen = 0x1D;      // scale up (x and z)
constexpr int kWeaken = 0x1E;          // scale down
constexpr int kEnlarge = 0x25;         // scale up (x, y and z)
constexpr int kShrink = 0x26;          // scale down
} // namespace mapeffect

class CMapObject : public CEngineObject {
public:
    // map_object_lifetime.cpp.
    // 0050E2D0. The constructor: the 3D object (00515BD0), the effect array (vtable 0073DBA0),
    // the second name, the network record array and its two memory streams (+0x1C4), then the
    // vtable 0073DAA0, no companion, packed vitals cleared, no sector cell, +0x178 = 1. Returns
    // this.
    CMapObject* Construct();
    // 0050E660. The destructor body: the streams' buffers, the network records (00510340), the
    // second name and the effects freed, then the 3D object's destructor body.
    void DestructBody();

    // --- Rewritten (map_object.cpp) ---

    // 0050F1C0. Virtual slot 6, the per-tick update: lets the companion (+0x170) update (its slot
    // 6). Returns 1. CUnit's slot 6 (0052F7D0) calls it after its controller's update.
    int Update();

    // 0050F1E0. Virtual slot 45: appends a copy of `effect` (the array grown by SetSize 0050FE70),
    // marks part 3 changed and the object dirty (stamped with the world's +0x68 when referenced),
    // then calls slot 33 with the argument's dword whose low byte says whether the effect scales
    // the object (strengthen, weaken, enlarge, shrink), slot 46 (1) for the stun types 0x19 / 0x1C,
    // slot 28, and slot 27 for a scaling effect. Returns what the last call leaves in EAX (its low
    // byte 0 without slot 27). Rebirth's FixSpellEffectsOverflow keeps at most 20 effects (a 21st
    // is dropped) and, for an effect lasting more than one tick, removes older ones of the same type,
    // end flag and +0x0C (within 0.001) that last no longer and are no stronger.
    std::uintptr_t AddEffect(const MapEffect* effect);

    // 0050F5B0. Removes every effect (frees the array), marks part 3 changed and calls slots 33
    // (1), 46 (0) and 27. Returns what slot 27 leaves in EAX.
    std::uintptr_t ClearEffects();

    // 0050F690. The largest MapEffect::value (as a float, at least 0) of the effects of type
    // `type` (compared with the whole low byte); 0 without one. NaN values win the comparison.
    float EffectValue(std::uint32_t type);

    // 0050F2D0. Virtual slot 27: resets the scale (slot 30) and grows or shrinks it by 0.4 per
    // net level of enlarge/shrink (x, y, z) or, without those, strengthen/weaken (x and, except
    // for heroes, z). Components stay >= 0. Marks part 1 changed. Returns what the original
    // leaves in EAX (0, or the world when the object is referenced).
    std::uintptr_t RecomputeScale();

    // 0050E440. Virtual slot 30: scale = base scale (+0x1AC). Returns the z component's bits (EAX).
    std::uint32_t ResetScale();

    // 0050E4B0 / 0050E4D0. Virtual slots 52 / 53: copy the position to `out`; return `out`. Slot
    // 52 takes a second argument it never reads (RET 8).
    float* GetPosition(float* out, int unused);
    float* GetPosition2(float* out);

    // 0050F730. Virtual slot 48: the point at the object's offset (+0x128), moved to the ground
    // height there plus the offset's z. Returns `out`.
    float* GroundPoint(float* out);

    // 0050F020. Virtual slot 51: z = ground height at (x, y) + `offset`.
    void PlaceOnGround(float offset);

    // 0050F050. Virtual slot 36: hands `data` (0x58 bytes) to a CMapObjectServer companion.
    // Returns what the original leaves in EAX.
    std::uintptr_t SetServerData(const void* data);

    // 0050E8F0. Replaces the companion (+0x170): the old one is detached (its +0x1C cleared) and
    // released, the new one points back here and gains a reference.
    void AttachCompanion(CEngineObject* companion);

    // 0050E890. Virtual slot 19: an object of type 0x47 becomes the companion (as AttachCompanion,
    // but a null or other object goes to the 3D object's slot 19, 00516D50, which takes type 0x46
    // objects at +0x94). Returns the companion's new reference count, or what 00516D50 leaves.
    std::uintptr_t AttachObject(CEngineObject* object);

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
    // Create (00515F70), the companion detached and released, the packed vitals reset (+0x178 = 1
    // between two resets), 4 network fields (SetSize 0050FA30) reset (0047FB70), changeStamp -1,
    // the effects freed, +0x174 = 5, the second name emptied, the base scale 0. Returns 1.
    int CreateMapObject();
    // 0050E840. Virtual slot 3 (Destroy): the effects emptied (SetSize 0050FE70 to 0), the
    // companion detached and released, then the 3D object's Destroy (00516080).
    void DestroyMapObject();

    // map_object_save.cpp (A-U2).
    // 0050F7A0. Virtual slot 9 (Load): the companion's Load, then +0x178, the position, the
    // orientation, the name, the scale, the effects (count, then MapEffect::Load 00688550 each),
    // flags_, the second name and, from version 0x4F, the base scale.
    void LoadMapObject(CStream* stream);
    // 0050F900. Virtual slot 10 (Save): the same fields (names with their terminators; the base
    // scale always).
    void SaveMapObject(CStream* stream);

    // object_small_slots.cpp (A-U2). 0050E460, virtual slot 31: the companion's slot 15 with the
    // first two arguments (the third is not read). Both companions return 0.
    int CompanionQuery(int first, int second, int unused);
    // 0050F0F0. Virtual slot 41 (a skill used on the object): logs "Science is applied on wrong
    // object" (0044C470) and returns false (AL); units override it. Both arguments unread.
    bool ApplyScience(int first, int second);
    // 0050F120. Virtual slot 44: frees the data of the MFC array `array` (operator delete) and
    // empties it (data, size, capacity). The second argument is unread.
    void FreeArray(void* array, int unused);

    // unit_vision.cpp (A-U3). 0050F170 (RET 4): slot 47 with mode 0 for `level` 0 (or NaN), 1 for
    // 1, else 2. Returns slot 47's EAX.
    int SetVisibilityLevel(float level);

    // The object as the CObject3D it derives from in the original (map_object_figures.h).
    CObject3D* Object3D() { return reinterpret_cast<CObject3D*>(this); }

    // The packed vitals (+0x17C) and the network state (+0x1C4).
    UnitVitalsPacked* PackedVitals() { return reinterpret_cast<UnitVitalsPacked*>(packedVitals); }
    DeltaState* NetState() { return reinterpret_cast<DeltaState*>(netState); }

    // --- Not rewritten (forwarders) ---

    // 00455950. The world the object lives in (owner_ + 0xAC, its slot 13), or null.
    void* World();
    // 0050E9F0 / 0050F630 (unit.cpp: CUnit::MarkChanged / CUnit::HasEffect, reached through their
    // game addresses).
    std::uintptr_t MarkChanged(int part);
    int HasEffect(std::uint32_t type);

    float positionX;                // +0x01C
    float positionY;                // +0x020
    float positionZ;                // +0x024
    std::uint8_t unknown_028[0x34 - 0x28];
    float orientation[4];           // +0x034 a quaternion
    std::uint8_t unknown_044[0xA4 - 0x44];
    char* name;                     // +0x0A4 CString (the prototype name for units)
    std::uint8_t unknown_0A8[0xC4 - 0xA8];
    float scale[3];                 // +0x0C4
    std::uint8_t unknown_0D0[0x128 - 0xD0];
    float pointOffset[3];           // +0x128 (GroundPoint)
    float radius;                   // +0x134 (CObject3D::radius)
    std::uint8_t unknown_138[0x144 - 0x138];
    float extent[3];                // +0x144 half the bounding box (units: box maximum - centre)
    std::uint8_t unknown_150[0x170 - 0x150];
    CEngineObject* companion;       // +0x170 CMapObjectServer / CMapObjectClientSpecific
    int unknown_174;                // +0x174 (5 after Create)
    float unknown_178;              // +0x178 (1 after Create)
    std::uint8_t packedVitals[0x18];    // +0x17C a UnitVitalsPacked (unit.h): PackedVitals()
    void* effectsVtable;            // +0x194 \ CArray<MapEffect> (vtable 0073DBA0; SetSize 0050FE70)
    MapEffect* effects;             // +0x198 |
    int effectCount;                // +0x19C |
    int effectCapacity;             // +0x1A0 |
    int effectGrowBy;               // +0x1A4 /
    char* name2;                    // +0x1A8 CString
    float baseScale[3];             // +0x1AC
    int sectorCellX;                // +0x1B8
    int sectorCellY;                // +0x1BC
    int changeStamp;                // +0x1C0 world + 0x68 when last changed
    // +0x1C4 the network state (DeltaState, net_delta.h, up to +0x238: its field records, masks
    // and two memory streams at +0x1E8 / +0x210); NetState(). Its mask is changedParts.
    std::uint8_t netState[0x1DC - 0x1C4];
    std::uint16_t changedParts;     // +0x1DC bit per part changed (MarkChanged)
    std::uint8_t unknown_1DE[0x238 - 0x1DE];   // two memory streams at +0x1E8 and +0x210
};
static_assert(offsetof(CMapObject, positionX) == 0x1C);
static_assert(offsetof(CMapObject, orientation) == 0x34);
static_assert(offsetof(CMapObject, name) == 0xA4);
static_assert(offsetof(CMapObject, extent) == 0x144);
static_assert(offsetof(CMapObject, scale) == 0xC4);
static_assert(offsetof(CMapObject, pointOffset) == 0x128);
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

// Map object vtable slots called by the rewritten code (byte offset / 4).
namespace mapslot {
constexpr int kUpdate = 6;                // also the companions' update
constexpr int kRecomputeScale = 27;       // 0x6C
constexpr int kEffectsChanged = 28;       // 0x70
constexpr int kResetScale = 30;           // 0x78
constexpr int kScaleEffectChanged = 33;   // 0x84 (argument: a scale effect was involved)
constexpr int kSetStunned = 46;           // 0xB8 (CUnit::SetFlag1000000 for units)
} // namespace mapslot

struct Vector3;
struct DamageInfo;   // spell.h
struct UnitStats;    // unit.h
struct LockKey;      // unit.h

// The map objects' virtual slots past CEngineObject's, as callers reach them through the vtable
// (CMapObject 0073DAA0, CUnit 0073EA8C, ...). The rewritten slot bodies stay plain members of the
// classes; this only declares the dispatch. Slots declared `void Name()` keep their place in the
// order only (their real arguments are not declared; name them properly when calling them).
// Names and evidence: docs/notes/drafts/DR-C2.md.
class CMapObjectVirtuals : public CEngineObject {
public:
    virtual void CopyTransform() = 0;      // 13: CTransformObject 00401490
    virtual void CombineRotation() = 0;    // 14: CObject3D 00516370 (CombineRotation3D)
    virtual void UpdateWorld() = 0;        // 15: CTransformObject 0040CC30
    virtual void SaveTransform() = 0;      // 16: 00516350 (00401510)
    virtual void LoadTransform() = 0;      // 17: 00516360 (00401540)
    // 18 (0x48): whether the object may play the animation entry (FigureAnimation, map_object_figures.h);
    // 0050E400 for CObject3D / CMapObject, CUnit 00532DA0 (AcceptsTarget: its flags +0x18 for heroes).
    virtual bool AcceptsAnimation(const void* animation) = 0;
    virtual void AttachObject() = 0;       // 19: 0050E890 (units 0052E870 AttachCompanion)
    virtual void CopyFigure() = 0;         // 20: CObject3D 005161E0
    virtual bool InvisibleSlot() = 0;   // 21 (0x54): 0050FA10 IsInvisible
    virtual void DrawFigure() = 0;         // 22: CObject3D 00516460
    virtual void DrawCollectDirty() = 0;   // 23: CObject3D 00516420
    virtual void DrawFigureShadow() = 0;   // 24: CObject3D 00516490
    virtual void AdvanceTime() = 0;        // 25: (float time) 0050E410 empty; units 0052FC40
    virtual void LoadFigures() = 0;        // 26: CObject3D 00517500 (units 0052E910)
    virtual void RecomputeScale() = 0;     // 27: 0050F2D0 (units 00532A80)
    virtual void EffectsChanged() = 0;     // 28: 0050E420 empty; units 00532AB0
    // 29 (AL): flags_ 0x20000 (0050E430, bit 17 as 0 / 1); units (004467E0), levers, traps,
    // torches 1. False ones leave the server's map-object maps after the zone loads
    // (SendFreshObjects). The world server creates such objects only with its +0x11D set, the
    // others with +0x11E (005927C0).
    virtual bool IsSynchronised() = 0;
    // 30 (0x78): the scale becomes the base scale (+0x1AC; 0050E440, units 00532AA0). Returns the z
    // component's bits.
    virtual std::uint32_t ApplyBaseScale() = 0;
    // 31 (0x7C): a hit (DamageInfo) from `attacker` (units: to the controller's TakeHit; other
    // objects ignore it, 0050E460).
    virtual void ReceiveHit(DamageInfo* hit, CEngineObject* attacker, CEngineObject* caster) = 0;
    virtual void StatsChanged() = 0;       // 32: 0050E970 PackVitals (units 0052EB50)
    // 33 (0x84, mapslot::kScaleEffectChanged): 0050E960; units 0052EB30 hand the argument to
    // their controller's slot 19.
    virtual void ScaleEffectChanged(int scaleEffect) = 0;
    // 34 (0x88): the prototype by name (units 0052EB70: their statistics; 0050E9B0 for others).
    virtual void TakePrototype(const char* prototype) = 0;
    // 35 (0x8C): 0050E480 empty; units 00530A20: a CUnitServer controller (class 0x3E) copies the
    // statistics (00552F40, CUnitServer::SetStats).
    virtual void SetStats(const UnitStats* stats) = 0;
    virtual void SetServerData() = 0;      // 36: 0050F050
    // 37 (0x94): 0050E490 empty; units 00530A50, the controller's slot 23 (CUnitServer 00552FB0).
    virtual void UpdateSizeClasses() = 0;
    virtual int IsUsable() = 0;            // 38: 0 (0050F0C0); traps 1, levers NeedsKey 0052BB00
    virtual int IsDirectUseBlocked() = 0;  // 39: 0 (0050F0D0); levers +0x260 (0052B2D0). Best guess
    virtual void KeyId() = 0;              // 40: -123 (0050F0E0); levers 0052BBC0
    virtual void ApplyScience() = 0;       // 41: 0050F0F0; traps Disarm, levers ApplyKey
    // 42 (0xA8): whether `key` fits: 0 (0050F110, RET 4); levers KeyOpens, traps KeyFits.
    virtual int AcceptsKey(const LockKey* key) = 0;
    virtual void KeepsMapFlag() = 0;       // 43: AL 1 (004467B0); levers +0x261 (0052B2C0). Best guess
    virtual void UseSpots() = 0;           // 44: 0050F120 FreeArray (none); levers 0052BBE0
    virtual void AddEffect() = 0;          // 45: 0050F1E0
    virtual void SetStunned() = 0;         // 46: 0050E4A0; units 0052F7F0 SetFlag1000000
    virtual int SetVisibilityMode(int mode) = 0;    // 47 (0xBC): 004467C0 hands it to the companion
    virtual Vector3* EyePoint(Vector3* out) = 0;        // 48 (0xC0): where the object sees from; returns `out`
    virtual void ReadNetState() = 0;       // 49: 0050EC30 (units 0052EF90)
    virtual void WriteNetState() = 0;      // 50: 0050EAF0 (units 0052EC70)
    virtual void PlaceOnGround() = 0;      // 51: 0050F020 (units 0052F740)
    // 52 (0xD0): where the object is at world time `time` (units: along their path; other objects
    // ignore the time, GetPosition). Returns `out`.
    virtual Vector3* PositionAt(Vector3* out, int time) = 0;
    virtual Vector3* AimPoint(Vector3* out) = 0;        // 53 (0xD4)
    virtual int IsLargeUnit() = 0;                      // 54 (0xD8): 0; units +0x278 (00446890)
    virtual int IsDead() = 0;                           // 55 (0xDC): non-zero when dead or gone
    virtual void ForgetObject() = 0;       // 56: (object) empty; units: the controller's slot 42
    virtual void UpdateMoveFactors() = 0;  // 57: empty; units: the controller's slot 53
    virtual void RefreshWounds() = 0;      // 58: 0; units: the controller's slot 31
    virtual void MoveSpeed() = 0;          // 59: ST0 0 (0050E540); units: forward speed (004468B0)
    virtual float Radius() = 0;                         // 60 (0xF0): units' +0x328 (004468A0), ST0
    // 61 (0xF4): units (00446830) keep `name` in their CString +0x238 (the script name).
    virtual void SetScriptName(const char* name) = 0;
};

// A map object's companion (CMapObjectServer / CMapObjectClientSpecific) as the base class calls it.
class CMapObjectCompanion : public CEngineObject {
public:
    virtual void EffectsChanged();   // 13: client 005117D0, server 005106D0 (empty, RET 4)
    virtual void Unused14();         // 14: empty in both (client 005104B0, server 00515810)
    virtual int Query(int first, int second);   // 15: 005104C0 in both (0)
};

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
};
inline CUnitServerVirtuals* ServerSlots(CEngineObject* controller) { return static_cast<CUnitServerVirtuals*>(controller); }

// An object seen through its virtual slots.
inline CMapObjectVirtuals* ObjectSlots(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object); }

// The companion's back pointer (+0x1C of CMapObjectServer / CMapObjectClientSpecific).
inline CMapObject*& CompanionOwner(CEngineObject* companion) {
    return *reinterpret_cast<CMapObject**>(reinterpret_cast<char*>(companion) + 0x1C);
}

// map_object_create.cpp (U6). 0052BFC0, virtual slot 2
// (Create) of CTorchObject (class id 0x3A, 0x23C bytes, vtable 0073E834, constructor 0052BEE0):
// the map object's Create (0050E770), the object at +0x238 released and cleared, 5 network fields
// (the DeltaState at +0x1C4, SetSize 0050FA30 inlined), then DeltaState::Reset. Returns 1.
int __fastcall TorchCreate(CMapObject* torch);
