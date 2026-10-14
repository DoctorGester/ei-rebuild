#pragma once

// The zone file's object records (the .mob objects; 00469630-0046F160): chunk readers / writers
// (Chunk, chunk_stream.h) that are also the map editor's live objects. Every record type has a
// 12-slot vtable:
//   0 OnChunk (reads the current chunk's value by its id), 1 File (Chunk), 2 the world object by
//   id, 3 (empty), 4 the type name (CString by value), 5 Move(dx, dy, dz), 6 remove the world
//   object, 7 make or update the world object, 8 refresh it, 9 Save (a chunk of its own), 10 the
//   deleting destructor, 11 WriteFields (the common fields, also called directly by the
//   subclasses' Save).
// The record classes (vtables, kind at +0x48): the base 0073C5C4 (0); objects 0073C5F0 (1); flames
// 0073C620 (6); magic traps 0073C650 (7); levers 0073C6B0 (8); units 0073C6E0 (5). Names are
// provisional; see docs/notes/objects.md ("Sixth round").
#include "chunk_stream.h"
#include "maths.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

namespace mapspec {

// An MFC CArray of plain elements (0x14 bytes).
using PlainArray = mfc::CArray<std::uint8_t>;

// A unit record's behaviour (alarm) records (map_spec_unit.cpp; the brain applies one, 005D6630).
// A waypoint (0x14 bytes).
struct Waypoint {
    Vector3 position;            // +0x00
    float wait;                  // +0x0C
    std::int16_t unknown_10;     // +0x10
    std::uint16_t unknown_12;    // +0x12
};
static_assert(sizeof(Waypoint) == 0x14);

// A patrol (0x20 bytes): where it starts and its waypoints (CArray, vtable 0073C3A0).
struct Patrol {
    Vector3 position;
    mfc::CArray<Waypoint> waypoints;   // +0x0C
};
static_assert(sizeof(Patrol) == 0x20);

// A unit's behaviour record (0x4C bytes; constructor 0045D5B0, destructor 0046C290).
struct Behaviour {
    std::int32_t type;           // +0x00 (3) 1 guard, 2 patrol, 3 sentry, 4 party, 5 guard at the alarm point
    mfc::CArray<Patrol> patrols;       // +0x04 (vtable 0073C37C)
    Vector3 guard;               // +0x18 the unit's position when saved
    std::uint32_t loop;          // +0x24 (1) a patrol loops; 0: its points are walked back too
    std::uint32_t flags;         // +0x28 bit 3: the unit is always updated (the brain's +0x37)
    float guardRadius;           // +0x2C (10)
    float unknown_30;            // +0x30 (-1)
    std::uint32_t alarmKind;     // +0x34 the alarm raised when the countdown ends (low byte)
    std::uint32_t unknown_38;    // +0x38
    std::uint32_t callRadius;    // +0x3C float bits (from 0073C378)
    std::uint32_t alertMode;     // +0x40
    std::uint32_t alarmDelay;    // +0x44 (75) the alarm countdown (at least 1)
    std::uint32_t alarm;         // +0x48 the reaction modes 0..3 (4 while reading: unset)
};
static_assert(offsetof(Behaviour, guard) == 0x18);
static_assert(sizeof(Behaviour) == 0x4C);
// map_spec_unit.cpp. 0045D5B0: the constructor (no patrols, type 3, loop, radius 10, delay 75; returns
// this); 0046C290: the destructor body; 0046E680 (RET 4): the patrols become a copy of `from`.
Behaviour* __fastcall BehaviourConstruct(Behaviour* self);
void __fastcall BehaviourDestruct(Behaviour* self);
void __fastcall PatrolsCopy(mfc::CArray<Patrol>* self, void* edx, const mfc::CArray<Patrol>* from);

// A trap's target record (8 bytes, zero-filled when added: 0046F110).
struct TargetRecord {
    std::uint8_t bytes[8];
};
using TargetArray = mfc::CArray<TargetRecord>;

class SpecRecord;

// The base record (vtable 0073C5C4, 0x4C bytes). Its code is in map_spec.cpp; the slot versions are
// plain members (the record structs have no C++ virtuals), registered at the original addresses.
// Calls through the vtable go through Record() (SpecRecord below).
struct Spec {
    // 00469CA0. Constructor; returns this.
    Spec* SpecConstruct();
    // 00469E50. The destructor body.
    void SpecDestruct();
    // 00469E30 (slot 10, RET 4). SpecDestruct, then operator delete when bit 0 of `flags` is set;
    // returns this.
    Spec* SpecDelete(unsigned flags);
    // 00469DB0 (slot 4, RET 4). The type name in `out`: "error".
    mfc::CString* SpecTypeName(mfc::CString* out);
    // 00469DE0 / 00469E00 (slots 6 and 8). Slot 7 (apply).
    void SpecRemove();
    void SpecRefresh();
    // 00469F20 (slot 2). The world object with the record's id.
    void* SpecWorldObject();
    // 00469F40 (slot 5, RET 0xC). Moves the record, then slot 7.
    void SpecMove(float dx, float dy, float dz);

    // The record seen through its vtable.
    SpecRecord* Record() { return reinterpret_cast<SpecRecord*>(this); }

    Chunk chunk;                 // +0x00
    std::int32_t id;             // +0x18 the object's id (-1)
    void* object;                // +0x1C the world object made from the record (null: none)
    Vector3 position;            // +0x20
    Quaternion rotation;         // +0x2C (1, 0, 0, 0)
    mfc::CString name;           // +0x3C
    mfc::CString comment;        // +0x40
    std::int32_t unknown_44;     // +0x44 (-1)
    std::int32_t kind;           // +0x48
};
static_assert(offsetof(Spec, position) == 0x20);
static_assert(offsetof(Spec, name) == 0x3C);
static_assert(sizeof(Spec) == 0x4C);

// An object record (vtable 0073C5F0, 0x8C bytes).
struct ObjectSpec {
    // 0046A5C0 (0xB001). Constructor; returns this.
    ObjectSpec* ObjectSpecConstruct();
    // 0046AEB0 (and 0058EDF0, the quick item effect's record, vtable 0073FA58). The destructor body.
    void ObjectSpecDestruct();
    // 0046A6F0 (and 0058EDD0; slot 10, RET 4). ObjectSpecDestruct, then operator delete when bit 0 of
    // `flags` is set; returns this.
    ObjectSpec* ObjectSpecDelete(unsigned flags);
    // 0046A710 (RET 4). Copies `from`: the base fields but its kind and chunk state, then the object's.
    void ObjectSpecAssign(const ObjectSpec* from);
    // 0046A940 (slot 11). The common fields.
    void ObjectSpecWriteFields();
    // 0046A920 (slot 9). The record as chunk B001.
    void ObjectSpecSave();
    // 0046AB20 (slot 0). The chunk just read, by its id.
    void ObjectSpecOnChunk();
    // 0046AF70 (slot 6). Removes the world object, then slot 7.
    void ObjectSpecRemove();
    // 0046AFA0 (slot 8).
    void ObjectSpecRefresh();
    // 0046AFB0 (slot 7). Makes the world object, or updates it from the record (position, rotation,
    // scale, visibility) while it is out of the editor's lists.
    void ObjectSpecApply();
    // 0046B090 (slot 4, RET 4). The type name in `out`: the player number ("%i").
    mfc::CString* ObjectSpecTypeName(mfc::CString* out);

    Spec base;                   // +0x00
    mfc::CString model;          // +0x4C (from 0079B298, empty)
    std::int32_t parentId;       // +0x50
    mfc::CString texture;        // +0x54
    mfc::CString primaryTexture; // +0x58
    std::int32_t player;         // +0x5C (-1); the type name formats it
    Vector3 scale;               // +0x60 (0.5 each; read and written y, x, z)
    std::uint8_t shadow;         // +0x6C
    std::uint8_t visible;        // +0x6D (1)
    std::uint8_t unknown_6E;     // +0x6E
    std::uint8_t unknown_6F;
    mfc::CString parentIname;    // +0x70
    mfc::CString unknown_74;     // +0x74
    mfc::CStringArray parts;      // +0x78
};
static_assert(offsetof(ObjectSpec, model) == 0x4C);
static_assert(offsetof(ObjectSpec, scale) == 0x60);
static_assert(offsetof(ObjectSpec, parts) == 0x78);
static_assert(sizeof(ObjectSpec) == 0x8C);

// A flame (vtable 0073C620, 0xA0 bytes).
struct FlameSpec {
    // 0046B210 (0xBBBF0000). Constructor; returns this.
    FlameSpec* FlameSpecConstruct();
    // 0046B120. The destructor body.
    void FlameSpecDestruct();
    // 0046B1F0 (slot 10, RET 4). Destructor, then operator delete when bit 0 of `flags` is set.
    FlameSpec* FlameSpecDelete(unsigned flags);
    // 0046B310 (slot 7). The world object is always made again.
    void FlameSpecApply();
    // 0046B340 (slot 9). Chunk BBBF0000 with the flame's fields, then the common ones.
    void FlameSpecSave();
    // 0046B3C0 (slot 0).
    void FlameSpecOnChunk();

    ObjectSpec object;           // +0x00
    float volume;                // +0x8C (0.7)
    Vector3 offset;              // +0x90
    mfc::CString sound;          // +0x9C ("nature\fire\small.wav")
};
static_assert(sizeof(FlameSpec) == 0xA0);

// A magic trap (vtable 0073C650, 0xD0 bytes).
struct TrapSpec {
    // 0046B4A0 (0xBBAB0000). Constructor; returns this.
    TrapSpec* TrapSpecConstruct();
    // 0046B570. The destructor body (the arrays' destructors inlined: their blocks freed).
    void TrapSpecDestruct();
    // 0046B550 (slot 10, RET 4). Destructor, then operator delete when bit 0 of `flags` is set.
    TrapSpec* TrapSpecDelete(unsigned flags);
    // 0046B520 (slot 4, RET 4). The type name in `out`: "MagicTrap".
    mfc::CString* TrapSpecTypeName(mfc::CString* out);
    // 0046B680 (slot 9). Chunk BBAB0000: the trap's fields (the arrays as a count and the raw
    // elements), then the common ones.
    void TrapSpecSave();
    // 0046B7D0 (slot 0).
    void TrapSpecOnChunk();

    ObjectSpec object;           // +0x00
    std::int32_t diplomacy;      // +0x8C (-1); the zone loader (00481660) makes it the players the
                                 // owner (+0x6E) is hostile to (relation 2), one bit each
    mfc::CString spell;          // +0x90
    Vector3 origin;              // +0x94 the position when the zone loaded (00481660)
    PlainArray areas;            // +0xA0 12-byte elements (vtable 0073C68C)
    TargetArray targets;         // +0xB4 8-byte elements (vtable 0073C680)
    std::int32_t interval;       // +0xC8 (15)
    std::uint8_t castOnce;       // +0xCC
    std::uint8_t unknown_CD[3];
};
static_assert(offsetof(TrapSpec, areas) == 0xA0);
static_assert(offsetof(TrapSpec, interval) == 0xC8);
static_assert(sizeof(TrapSpec) == 0xD0);

// A lever (vtable 0073C6B0, 0xA4 bytes).
struct LeverSpec {
    // 0046BD40 (0xBBAC0000). Constructor; returns this.
    LeverSpec* LeverSpecConstruct();
    // 0046BDC0. The destructor body (the object record's).
    void LeverSpecDestruct();
    // 0046BDA0 (slot 10, RET 4). Destructor, then operator delete when bit 0 of `flags` is set.
    LeverSpec* LeverSpecDelete(unsigned flags);
    // 0046BE80 (slot 9). Chunk BBAC0000.
    void LeverSpecSave();
    // 0046BF20 (slot 0).
    void LeverSpecOnChunk();

    ObjectSpec object;           // +0x00
    std::uint32_t stats[3];      // +0x8C (1, 0, 0): a 12-byte blob
    std::uint32_t state;         // +0x98
    std::uint32_t states;        // +0x9C (2)
    std::uint8_t cycled;         // +0xA0
    std::uint8_t castOnce;       // +0xA1
    std::uint8_t unknown_A2;     // +0xA2
    std::uint8_t unknown_A3;
};
static_assert(sizeof(LeverSpec) == 0xA4);

// The record's vtable from slot 2 on (see the top of this file), for calls through it (the deleting
// destructor, slot 7 from the slots that end with it, slot 11 from Save).
class __declspec(novtable) SpecRecord : public Chunk {
public:
    virtual void* WorldObject();                // 2
    virtual void Nothing();                     // 3 (empty)
    virtual void TypeNameSlot();                // 4 (CString by value; not called through here)
    virtual void MoveSlot();                    // 5
    virtual void RemoveObject();                // 6
    virtual void ApplyObject();                 // 7
    virtual void RefreshObject();               // 8
    virtual void SaveSlot();                    // 9
    virtual void* DeleteRecord(unsigned flags); // 10: the deleting destructor (flags 1 frees)
    virtual void WriteFields();                 // 11
};

// The zone's player table (0x1014 bytes; constructor 0046E5E0 DiplomacyConstruct): the relation
// of every pair of the 32 players (1 by default, 0 with itself, 2 hostile) and their names.
struct ZoneDiplomacy {
    std::int32_t relations[32][32];
    mfc::CStringArray names;      // +0x1000
};
static_assert(offsetof(ZoneDiplomacy, names) == 0x1000);
static_assert(sizeof(ZoneDiplomacy) == 0x1014);

// The effect records' base (vtable 0073C728, 0x54 bytes; code in map_spec_effect.cpp).
struct EffectSpec {
    // Members of the effect records (map_spec_effect.cpp), registered at the original addresses.
    // 0046D370 / 0046D5A0 / 0046DA20: the destructor bodies (the base record's); 0046D350 / 0046D580 /
    // 0046DA00 (slot 10, RET 4): EffectSpecDestruct, then operator delete when bit 0 of `flags` is set.
    void EffectSpecDestruct();
    EffectSpec* EffectSpecDelete(unsigned flags);
    // 0046D3D0 (slot 2). Effects are managed under -3001 - id.
    void* EffectSpecWorldObject();
    // 0046D3F0 / 0046E490 (slot 11, RET 4). Shows or hides the world object (flag 0x40 hides).
    void EffectSpecSetVisible(std::uint8_t newVisible);
    // 0046D420 (slot 5, RET 0xC). Moves the record, not below the terrain, then slot 7.
    void EffectSpecMove(float dx, float dy, float dz);

    Spec base;                   // +0x00
    void* effect;                // +0x4C the effect the world object runs
    std::uint8_t visible;        // +0x50 (1)
    std::uint8_t unknown_51[3];
};
static_assert(sizeof(EffectSpec) == 0x54);

struct ParticleSpec {            // 0073C758
    // 0046D520. Constructor; returns this.
    ParticleSpec* ParticleSpecConstruct();
    // 0046D5A0. The destructor body; 0046D580 (slot 10, RET 4): that, then operator delete when bit 0 of
    // `flags` is set.
    void ParticleSpecDestruct();
    ParticleSpec* ParticleSpecDelete(unsigned flags);
    // 0046D550 (slot 4, RET 4). The type name in `out`: "Particle source".
    mfc::CString* ParticleSpecTypeName(mfc::CString* out);
    // 0046D680 (slot 9). Chunk DD01.
    void ParticleSpecSave();
    // 0046D730 (slot 0).
    void ParticleSpecOnChunk();
    // 0046D870 (slot 7). Makes the source (when it has a type) or updates its effect.
    void ParticleSpecApply();

    EffectSpec effect;
    std::int32_t type;           // +0x54 (0: none made)
    float scale;                 // +0x58 (1)
};
static_assert(sizeof(ParticleSpec) == 0x5C);

struct LightSpec {               // 0073C788
    // 0046D960. Constructor; returns this.
    LightSpec* LightSpecConstruct();
    // 0046DA20. The destructor body; 0046DA00 (slot 10, RET 4): that, then operator delete when bit 0 of
    // `flags` is set.
    void LightSpecDestruct();
    LightSpec* LightSpecDelete(unsigned flags);
    // 0046D9D0 (slot 4, RET 4). The type name in `out`: "Light".
    mfc::CString* LightSpecTypeName(mfc::CString* out);
    // 0046DB20 (slot 9). Chunk AA01.
    void LightSpecSave();
    // 0046DBF0 (slot 0).
    void LightSpecOnChunk();
    // 0046DD70 (slot 7). Makes the light or updates its effect (colour, radius, position).
    void LightSpecApply();

    EffectSpec effect;
    Vector3 colour;              // +0x54 (1, 1, 1)
    std::uint8_t shadows;        // +0x60 (1)
    std::uint8_t unknown_61[3];
    float radius;                // +0x64 (10)
};
static_assert(sizeof(LightSpec) == 0x68);

struct SoundSpec {               // 0073C7B8
    // 0046DE70. Constructor; returns this.
    SoundSpec* SoundSpecConstruct();
    // 0046DF70. The destructor body; 0046DF50 (slot 10, RET 4): that, then operator delete when bit 0 of
    // `flags` is set.
    void SoundSpecDestruct();
    SoundSpec* SoundSpecDelete(unsigned flags);
    // 0046DF20 (slot 4, RET 4). The type name in `out`: "Sound".
    mfc::CString* SoundSpecTypeName(mfc::CString* out);
    // 0046E090 (slot 9). Chunk CC01.
    void SoundSpecSave();
    // 0046E190 (slot 0).
    void SoundSpecOnChunk();
    // 0046E380 (slot 7). Makes the sound (when it has sounds); otherwise moves the world object and makes
    // its effect again.
    void SoundSpecApply();

    EffectSpec effect;
    mfc::CStringArray sounds;     // +0x54 (none made without any)
    std::int32_t unknown_68;     // +0x68 (10) the ambient sound's volume (CreateObject, 005927C0)
    std::int32_t unknown_6C;     // +0x6C (15) the ambient sound's radius
    std::int32_t unknown_70;     // +0x70 (1)
    std::int32_t unknown_74;     // +0x74 (1500)
    std::int32_t unknown_78;     // +0x78 (3000)
    std::int32_t unknown_7C;     // +0x7C
    std::int32_t unknown_80;     // +0x80
    std::uint8_t unknown_84;     // +0x84 set: an ambient sound (class 0x49), else a plain one (0x48)
    std::uint8_t unknown_85;     // +0x85
    std::uint8_t unknown_86[2];
};
static_assert(offsetof(SoundSpec, unknown_68) == 0x68);
static_assert(offsetof(SoundSpec, unknown_84) == 0x84);
static_assert(sizeof(SoundSpec) == 0x88);

// A unit record (vtable 0073C6E0, 0x324 bytes; code in map_spec_unit.cpp). The world server's
// CreateUnits (0058EF20) fills one on the stack for each unit it creates.
struct UnitSpec {
    ObjectSpec object;           // +0x000
    std::int32_t behaviours;     // +0x08C records read so far
    Behaviour behaviour[5];      // +0x090
    mfc::CString prototype;      // +0x20C ("none")
    std::uint8_t stats[0xAC];    // +0x210 (applied to the unit by 00477B00)
    mfc::CStringArray lists[5];   // +0x2BC ... +0x30C (chunks BBBB0005 ... BBBB0009); [1]: the item
                                 // names the unit wears (CreateObject, 005927C0)
    std::uint32_t unknown_320;   // +0x320
};
static_assert(offsetof(UnitSpec, behaviour) == 0x90);
static_assert(offsetof(UnitSpec, prototype) == 0x20C);
static_assert(offsetof(UnitSpec, stats) == 0x210);
static_assert(offsetof(UnitSpec, lists) == 0x2BC);
static_assert(sizeof(UnitSpec) == 0x324);

// Record constructors (each returns this), by .mob chunk id.
UnitSpec* __fastcall UnitSpecConstruct(UnitSpec* self);             // 0046C010 (0xBBBB0000, 0x324)
ZoneDiplomacy* __fastcall DiplomacyConstruct(ZoneDiplomacy* self);  // 0046E5E0
// The records of a .mob file by id (00481660's local): an STLport hash_map<int, record*> (100
// buckets to start) and the largest id read.
struct MobRecords {
    stlp::hash_map<std::int32_t, Spec*, stlp::IntHash> byId;
    std::int32_t maxId;          // +0x14
};
static_assert(offsetof(MobRecords, maxId) == 0x14);

// mob_reader.cpp. 00481F00 (thiscall on the file's root chunk, RET 0xC): reads the .mob file's
// top-level chunks: the scripts (0xACCEECCA plain, 0xACCEECCB encoded) into `scripts`, the weather
// (0xABD0: wind and time of day, applied to the server), the records (0xB000) into `records`, the
// players (0xDDDDDDD1) into `diplomacy`. Returns the last 0xC000 (true) / 0xD000 (false) seen,
// true without either (AL).
bool __fastcall ReadMobChunks(Chunk* root, void*, MobRecords* records, ZoneDiplomacy* diplomacy,
                              mfc::CString* scripts);
// 00481660 (thiscall on the root chunk, RET 8): reads the file, applies its player relations,
// makes every record's world object (traps learn their hostile players, units their alarm
// records), frees the records, then loads the scripts unless `keepIds`' low byte is set. Returns
// true (AL). The first argument is not used.
bool __fastcall LoadMobFile(Chunk* root, void*, void* unused, int keepIds);

// The unit record's five behaviour (alarm) records (+0x90, 0x4C bytes each).
const void* UnitSpecBehaviours(const UnitSpec* spec);

} // namespace mapspec
