#pragma once

// The zone file's object records (the .mob objects; 00469630-0046F160): chunk readers / writers
// (Chunk, chunk_stream.h) that are also the map editor's live objects: Spec and the classes on it,
// each with game.exe's 12 slots (0 OnChunk, 1 File, then Spec's virtuals; 11 is ObjectSpec's
// WriteFields or EffectSpec's SetVisible). The record classes (kind at +0x48): the base (0); objects
// (1); flames (6); magic traps (7); levers (8); units (5); lights (2), sounds (3) and particle sources
// (4) on the effect records' base. Names are provisional; see docs/notes/objects.md ("Rewritten").
#include "chunk_stream.h"
#include "lock_key.h"
#include "maths.h"
#include "unit_record.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CStream;   // core_streams.h
class CEngineObject;   // engine_object.h
class CMapObject;      // map_object.h
class CTransformObject;   // core_transform.h
class CUnit;           // unit.h

// ---- The world objects' parts of the trap and lever records ---------------------------------------
// The fields after a record's ObjectSpec (+0x8C), which the world object's SetUp reads in place.

// A circle of a trap's area (0xC bytes).
struct TrapCircle {
    float x;
    float y;
    float radius;
};
static_assert(sizeof(TrapCircle) == 0xC);

// A point a trap casts at (zero-filled when added: 0046F110).
struct TrapPoint {
    float x;
    float y;
};
static_assert(sizeof(TrapPoint) == 8);

// A trap's area: an MFC CArray of circles (SetSize 0046EAD0; the record's vtable 0073C68C).
using TrapArea = mfc::CArray<TrapCircle>;
static_assert(sizeof(TrapArea) == 0x14);

// A trap's points: an MFC CArray of {x, y} pairs (SetSize 0052C8E0; the record's vtable 0073C680).
using TrapPoints = mfc::CArray<TrapPoint>;
static_assert(sizeof(TrapPoints) == 0x14);

// The MagicTrap part of a zone object record (mapspec::TrapSpec::trap; 005927C0 passes it to
// MagicTrap::SetUp).
struct MagicTrapRecord {
    std::uint32_t sides;        // +0x00 (-1); the zone loader makes it the players the
                                // owner (+0x6E) is hostile to (relation 2), one bit each
    mfc::CString spell;         // +0x04 the spell's text
    Vector3 position;           // +0x08 the position when the zone loaded
    TrapArea area;              // +0x14
    TrapPoints points;          // +0x28
    std::int32_t period;        // +0x3C (15)
    std::uint8_t singleTarget;  // +0x40
};
static_assert(offsetof(MagicTrapRecord, area) == 0x14);
static_assert(offsetof(MagicTrapRecord, period) == 0x3C);
static_assert(sizeof(MagicTrapRecord) == 0x44);

// The lever part of a zone object record (mapspec::LeverSpec::lever), what Lever::SetUp reads.
struct LeverSetup {
    LockKey lock;                   // +0x00 ({1, 0, 0}; chunk BBAC0006, a 12-byte blob)
    std::int32_t state;             // +0x0C (chunk BBAC0002, a byte)
    std::int32_t stateCount;        // +0x10 (2; chunk BBAC0003, a byte)
    std::uint8_t flag260;           // +0x14 (chunk BBAC0007)
    std::uint8_t keepsMapFlag;      // +0x15 (chunk BBAC0008)
    std::uint8_t cycling;           // +0x16 (chunk BBAC0004)
};
static_assert(offsetof(LeverSetup, cycling) == 0x16);
static_assert(sizeof(LeverSetup) == 0x18);

namespace mapspec {

// An MFC CArray of plain elements (0x14 bytes).
using PlainArray = mfc::CArray<std::uint8_t>;

// A unit record's behaviour (alarm) records (map_spec_unit.cpp; the brain applies one, 005D6630).
// A waypoint (0x14 bytes).
struct Waypoint {
    Vector3 position;            // +0x00
    float time;                  // +0x0C the look's turning time (AILook::time; chunk BBBE0003)
    std::int16_t lookWait;       // +0x10 the wait after the look (AILook::wait; chunk BBBE0002)
    std::uint16_t stopFirst;     // +0x12 bit 0: AILook::stopFirst (chunk BBBE0004)
};
static_assert(sizeof(Waypoint) == 0x14);

// A patrol (0x20 bytes): where it starts and its waypoints (CArray, vtable 0073C3A0).
struct Patrol {
    Vector3 position;
    mfc::CArray<Waypoint> waypoints;   // +0x0C
};
static_assert(sizeof(Patrol) == 0x20);

// CArray<Patrol> (a behaviour's patrols, vtable 0073C37C); map_spec_unit.cpp and record_arrays.cpp.
struct Patrols : mfc::CArray<Patrol> {
    // 0046E680 (RET 4; map_spec_unit.cpp). The patrols become a copy of `from` (each patrol copied
    // through a temporary returned by value, as the original does).
    void CopyFrom(const Patrols& from);
    // 0046EAB0 / 0046EFD0 (RET 8; record_arrays.cpp). By-value GetAt: element `index` copy-constructed
    // into `result` (the position, then the waypoints: storage for exactly the source's count, zero
    // filled, the waypoints copied). Returns result.
    Patrol* CopyAt(Patrol* result, int index) const;
};

// A unit's behaviour record (0x4C bytes; constructor 0045D5B0, destructor 0046C290).
struct Behaviour {
    mapspec::Behaviour* DeleteScalar(unsigned flags);
    void Read(CStream* stream);
    void Write(CStream* stream) const;
    std::int32_t type;           // +0x00 (3) 1 guard, 2 patrol, 3 sentry, 4 party, 5 guard at the alarm point
    Patrols patrols;       // +0x04 (vtable 0073C37C)
    Vector3 guard;               // +0x18 the unit's position when saved
    std::uint32_t loop;          // +0x24 (1) a patrol loops; 0: its points are walked back too
    std::uint32_t flags;         // +0x28 bit 3: the unit is always updated (the brain's +0x37)
    float guardRadius;           // +0x2C (10)
    float field0A;               // +0x30 (-1) chunk BBBC000A, only read and written with the record
    std::uint32_t alarmKind;     // +0x34 the alarm raised when the countdown ends (low byte)
    std::uint32_t enabled;       // +0x38 (1 for the first record) HearAlarm applies a record other than the
                                 // first only when set
    std::uint32_t callRadius;    // +0x3C float bits (from 0073C378)
    std::uint32_t alertMode;     // +0x40
    std::uint32_t alarmDelay;    // +0x44 (75) the alarm countdown (at least 1)
    std::uint32_t alarm;         // +0x48 the reaction modes 0..3 (4 while reading: unset)
};
static_assert(offsetof(Behaviour, guard) == 0x18);
static_assert(sizeof(Behaviour) == 0x4C);
// map_spec_unit.cpp. 0045D5B0: the constructor (no patrols, type 3, loop, radius 10, delay 75; returns
// this); 0046C290: the destructor body.
Behaviour* __fastcall BehaviourConstruct(Behaviour* self);
void __fastcall BehaviourDestruct(Behaviour* self);

// The base record (game.exe's vtable 0073C5C4, 0x4C bytes; map_spec.cpp). The records are read and
// written chunk by chunk (no record is a block of file bytes). They live on the game's heap.
class Spec : public Chunk {
public:
    // No parent chunk, id -1, at the origin unrotated, named and commented "none", kind 0.
    Spec();

    void OnChunk() override {}                              // 0: 00469E20 (empty)
    // The world object with the record's id.
    virtual void* WorldObject();
    virtual void Nothing(int) {}                            // 3: 00469DA0 (empty, RET 4)
    // The type name in `out` (a CString returned by value): "error".
    virtual mfc::CString* TypeName(mfc::CString* out);
    // Moves the record, then Apply.
    virtual void Move(float dx, float dy, float dz);
    // Removes the world object (here: Apply).
    virtual void Remove();
    // 7: 00469DF0 (empty). Makes or updates the world object.
    virtual void Apply() {}
    // Refreshes the world object (here: Apply).
    virtual void Refresh();
    // 9: 00469E10 (empty). Writes the record as a chunk of its own.
    virtual void Save() {}
    // 10: 00469E50 (deleting: 00469E30).
    virtual ~Spec();

    // On the game's heap (map_spec.cpp): null without memory, and then no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    std::int32_t id;             // +0x18 the object's id (-1)
    CEngineObject* object;       // +0x1C the world object made from the record (null: none)
    Vector3 position;            // +0x20
    Quaternion rotation;         // +0x2C (1, 0, 0, 0)
    mfc::CString name;           // +0x3C
    mfc::CString comment;        // +0x40
    std::int32_t spareId;        // +0x44 (-1) only set and copied (ObjectSpec::Assign)
    std::int32_t kind;           // +0x48 which record this is: 0 the base, 1 an object, 2 a light, 3 a sound,
                                 // 4 a particle source, 5 a unit, 6 a flame, 7 a magic trap, 8 a lever
};

// An object record (game.exe's vtable 0073C5F0, 0x8C bytes; chunk B001).
class ObjectSpec : public Spec {
public:
    // Kind 1, no model, textures "none", player -1, scale 0.5, visible, no parts.
    ObjectSpec();
    // 0046AEB0 (deleting: 0046A6F0). The quick item effect's record (UseQuickItem, game.exe's twin
    // vtable 0073FA58) is one too: its 0058EDF0 / 0058EDD0 are the same code.
    ~ObjectSpec() override;

    void OnChunk() override;                                 // 0: 0046AB20, the chunk just read, by its id
    mfc::CString* TypeName(mfc::CString* out) override;      // 4: 0046B090 (RET 4), the player number ("%i")
    void Remove() override;                                  // 6: 0046AF70, removes the world object, then Apply
    // Makes the world object, or updates it from the record (position, rotation, scale,
    // visibility) while it is out of the editor's lists.
    void Apply() override;
    void Refresh() override;
    void Save() override;                                    // 9: 0046A920, the record as chunk B001
    // The common fields (also called directly by the subclasses' Save).
    virtual void WriteFields();

    // Copies `from`: the base fields but its kind and chunk state, then the object's.
    void Assign(const ObjectSpec* from);

    // The world object an object record makes is a map object (a unit for a unit record).
    CMapObject* MapObject() const;
    CUnit* Unit() const;   // map_spec_unit.cpp

    mfc::CString model;          // +0x4C (from 0079B298, empty)
    std::int32_t parentId;       // +0x50
    mfc::CString texture;        // +0x54
    mfc::CString primaryTexture; // +0x58
    std::int32_t player;         // +0x5C (-1); the type name formats it
    Vector3 scale;               // +0x60 (0.5 each; read and written y, x, z)
    std::uint8_t shadow;         // +0x6C
    std::uint8_t visible;        // +0x6D (1)
    std::uint8_t side;           // +0x6E the owner's player number (chunk B011; CUnit::SetSide, the traps' hostile players)
    std::uint8_t unused_6F;
    mfc::CString parentIname;    // +0x70
    mfc::CString skinTexture;    // +0x74 (chunk B008) the texture swapped onto the figure ("none": none)
    mfc::CStringArray parts;      // +0x78
};

// A flame (game.exe's vtable 0073C620, 0xA0 bytes; chunk BBBF0000).
class FlameSpec : public ObjectSpec {
public:
    // Kind 6, volume 0.7, the sound "nature\fire\small.wav".
    FlameSpec();
    ~FlameSpec() override;          // 0046B120 (deleting: 0046B1F0)

    void OnChunk() override;
    void Apply() override;          // 7: 0046B310, the world object is always made again
    void Save() override;           // 9: 0046B340, chunk BBBF0000 with the flame's fields, then the common ones

    float volume;                // +0x8C (0.7)
    Vector3 offset;              // +0x90
    mfc::CString sound;          // +0x9C ("nature\fire\small.wav")
};

// A magic trap (game.exe's vtable 0073C650, 0xD0 bytes; chunk BBAB0000).
class TrapSpec : public ObjectSpec {
public:
    // Kind 7, every side, no area or points, period 15.
    TrapSpec();
    // 0046B570 (deleting: 0046B550). The arrays' destructors inlined: their blocks freed.
    ~TrapSpec() override;

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // 4: 0046B520 (RET 4), "MagicTrap"
    // Chunk BBAB0000: the trap's fields (the arrays as a count and the raw elements), then
    // the common ones.
    void Save() override;

    MagicTrapRecord trap;        // +0x8C
};

// A lever (game.exe's vtable 0073C6B0, 0xA4 bytes; chunk BBAC0000).
class LeverSpec : public ObjectSpec {
public:
    // Kind 8, the key {1, 0, 0}, two states.
    LeverSpec();
    ~LeverSpec() override;          // 0046BDC0 (deleting: 0046BDA0)

    void OnChunk() override;
    void Save() override;           // 9: 0046BE80, chunk BBAC0000

    LeverSetup lever;            // +0x8C
};

// The zone's player table (0x1014 bytes; constructor 0046E5E0 DiplomacyConstruct): the relation
// of every pair of the 32 players (1 by default, 0 with itself, 2 hostile) and their names.
struct ZoneDiplomacy {
    std::int32_t relations[32][32];
    mfc::CStringArray names;      // +0x1000
};
static_assert(offsetof(ZoneDiplomacy, names) == 0x1000);
static_assert(sizeof(ZoneDiplomacy) == 0x1014);

// The effect records' base (game.exe's vtable 0073C728, 0x54 bytes; code in map_spec_effect.cpp).
class EffectSpec : public Spec {
public:
    ~EffectSpec() override;         // 0046D370 (deleting: 0046D350)

    // Effects are managed under -3001 - id.
    void* WorldObject() override;
    // Moves the record, not below the terrain, then Apply.
    void Move(float dx, float dy, float dz) override;
    // 11: 0046D3F0 (the sound record's 0046E490 is the same code; RET 4). Shows or hides the world
    // object (flag 0x40 hides).
    virtual void SetVisible(std::uint8_t newVisible);

    // The world object an effect record makes (the effect's carrier: a transform object).
    CTransformObject* Transform() const;

    void* effect;                // +0x4C the effect the world object runs
    std::uint8_t visible;        // +0x50 (1)
    std::uint8_t unused_51[3];

protected:
    // The records' constructors begin with this (inlined in each; the sound record's 0046DE70 stores
    // 0073C728 for it): no world object or effect, visible.
    EffectSpec() : effect(nullptr), visible(1) {}
};

// A particle source (game.exe's vtable 0073C758; chunk DD01).
class ParticleSpec : public EffectSpec {
public:
    ParticleSpec();                 // Kind 4, no type, scale 1.
    ~ParticleSpec() override;       // 0046D5A0 (deleting: 0046D580)

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // 4: 0046D550 (RET 4), "Particle source"
    void Apply() override;          // 7: 0046D870, makes the source (when it has a type) or updates its effect
    void Save() override;           // 9: 0046D680, chunk DD01

    std::int32_t type;           // +0x54 (0: none made)
    float scale;                 // +0x58 (1)
};

// A light (game.exe's vtable 0073C788; chunk AA01).
class LightSpec : public EffectSpec {
public:
    LightSpec();                    // Kind 2, white, shadows, radius 10.
    ~LightSpec() override;          // 0046DA20 (deleting: 0046DA00)

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // 4: 0046D9D0 (RET 4), "Light"
    void Apply() override;          // 7: 0046DD70, makes the light or updates its effect (colour, radius, position)
    void Save() override;           // 9: 0046DB20, chunk AA01

    Vector3 colour;              // +0x54 (1, 1, 1)
    std::uint8_t shadows;        // +0x60 (1)
    std::uint8_t unused_61[3];
    float radius;                // +0x64 (10)
};

// A sound (game.exe's vtable 0073C7B8; chunk CC01).
class SoundSpec : public EffectSpec {
public:
    SoundSpec();                    // Kind 3, no sounds, volume 10, radius 15, pauses 1500-3000.
    ~SoundSpec() override;          // 0046DF70 (deleting: 0046DF50)

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // 4: 0046DF20 (RET 4), "Sound"
    // Makes the sound (when it has sounds); otherwise moves the world object and makes its
    // effect again.
    void Apply() override;
    void Save() override;           // 9: 0046E090, chunk CC01

    mfc::CStringArray sounds;     // +0x54 (none made without any)
    std::int32_t volume;         // +0x68 (10; chunk CC04) the sound's volume (CreateObject, 005927C0)
    std::int32_t radius;         // +0x6C (15; chunk CC0B) the sound's radius
    std::int32_t unused_70;      // +0x70 (1)
    std::int32_t minInterval;    // +0x74 (1500; chunk CC06) an ambient sound's shortest pause
    std::int32_t maxInterval;    // +0x78 (3000; chunk CC07) and longest
    std::int32_t unused_7C;      // +0x7C
    std::int32_t unused_80;      // +0x80
    std::uint8_t ambient;        // +0x84 (chunk CC0D) set: an ambient sound (class 0x49), else a plain one (0x48)
    std::uint8_t field0E;        // +0x85 chunk CC0E, only read and written with the record
    std::uint8_t unused_86[2];
};

// A unit record (game.exe's vtable 0073C6E0, 0x324 bytes; chunk BBBB0000; code in map_spec_unit.cpp).
// The world server's CreateUnits fills one on the stack for each unit it creates.
class UnitSpec : public ObjectSpec {
public:
    // Kind 5, five behaviour records (the first enabled), the prototype "none", no statistics.
    UnitSpec();
    ~UnitSpec() override;           // 0046C140 (deleting: 0046C120)

    void OnChunk() override;        // 0: 0046D080, the chunk just read, by its id
    void Apply() override;          // 7: 0046C4D0, makes the unit, or moves, turns and rescales it
    void Save() override;           // 9: 0046CF10, chunk BBBB0000

    std::int32_t behaviours;     // +0x08C records read so far
    Behaviour behaviour[5];      // +0x090
    mfc::CString prototype;      // +0x20C ("none")
    unitrecord::UnitRecord record;   // +0x210 the statistics and item lists (applied to the unit by 00477B00)
};

// Record constructors (each returns this), by .mob chunk id.
ZoneDiplomacy* __fastcall DiplomacyConstruct(ZoneDiplomacy* self);
// The records of a .mob file by id (00481660's local): an STLport hash_map<int, record*> (100
// buckets to start) and the largest id read.
struct MobRecords {
    stlp::hash_map<std::int32_t, Spec*, stlp::IntHash> byId;
    std::int32_t maxId;          // +0x14
};
static_assert(offsetof(MobRecords, maxId) == 0x14);

// A .mob file's root chunk (mob_reader.cpp; CWorldServer::LoadMapObjects opens it).
class MobFile : public RootChunk {
public:
    // Reads the file's top-level chunks: the scripts (0xACCEECCA plain,
    // 0xACCEECCB encoded) into `scripts`, the weather (0xABD0: wind and time of day, applied to the
    // server), the records (0xB000) into `records`, the players (0xDDDDDDD1) into `diplomacy`.
    // Returns the last 0xC000 (true) / 0xD000 (false) seen, true without either (AL).
    bool ReadChunks(MobRecords* records, ZoneDiplomacy* diplomacy, mfc::CString* scripts);
    // Reads the file, applies its player relations, makes every record's world
    // object (traps learn their hostile players, units their alarm records), frees the records, then
    // loads the scripts unless `keepIds`' low byte is set. Returns true (AL). The first argument
    // (the world) is not used.
    bool Load(void* unused, int keepIds);
};

} // namespace mapspec
