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

    // 0050FA10 / 0050FA20 / 0050E430. Bits 16 (invisible: set by the client for effect 0x0F),
    // 18 (stunned: effect 0x19) and 17 of flags_. 0050FA10 and 0050E430 are virtual slots 21 / 29.
    std::uint32_t IsInvisible();
    std::uint32_t IsStunned();
    std::uint32_t Flag20000();

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
    std::uint8_t unknown_134[0x170 - 0x134];
    CEngineObject* companion;       // +0x170 CMapObjectServer / CMapObjectClientSpecific
    int unknown_174;                // +0x174 (5 after Create)
    float unknown_178;              // +0x178 (1 after Create)
    std::uint8_t unknown_17C[0x194 - 0x17C];
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
    std::uint8_t unknown_1C4[0x1DC - 0x1C4];   // a CArray of 0xA0-byte records (+0x1C4)
    std::uint16_t changedParts;     // +0x1DC bit per part changed (MarkChanged)
    std::uint8_t unknown_1DE[0x238 - 0x1DE];   // two memory streams at +0x1E8 and +0x210
};
static_assert(offsetof(CMapObject, positionX) == 0x1C);
static_assert(offsetof(CMapObject, orientation) == 0x34);
static_assert(offsetof(CMapObject, name) == 0xA4);
static_assert(offsetof(CMapObject, scale) == 0xC4);
static_assert(offsetof(CMapObject, pointOffset) == 0x128);
static_assert(offsetof(CMapObject, companion) == 0x170);
static_assert(offsetof(CMapObject, effects) == 0x198);
static_assert(offsetof(CMapObject, baseScale) == 0x1AC);
static_assert(offsetof(CMapObject, changeStamp) == 0x1C0);
static_assert(offsetof(CMapObject, changedParts) == 0x1DC);
static_assert(sizeof(CMapObject) == 0x238);   // CUnit's fields start at +0x238

// Map object vtable slots called by the rewritten code (byte offset / 4).
namespace mapslot {
constexpr int kUpdate = 6;                // also the companions' update
constexpr int kRecomputeScale = 27;       // 0x6C
constexpr int kEffectsChanged = 28;       // 0x70
constexpr int kResetScale = 30;           // 0x78
constexpr int kScaleEffectChanged = 33;   // 0x84 (argument: a scale effect was involved)
constexpr int kSetStunned = 46;           // 0xB8 (CUnit::SetFlag1000000 for units)
} // namespace mapslot

// The companion's back pointer (+0x1C of CMapObjectServer / CMapObjectClientSpecific).
inline CMapObject*& CompanionOwner(CEngineObject* companion) {
    return *reinterpret_cast<CMapObject**>(reinterpret_cast<char*>(companion) + 0x1C);
}
