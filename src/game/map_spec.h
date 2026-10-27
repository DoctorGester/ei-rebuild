#pragma once

// The zone file's object records (the .mob objects): chunk readers / writers (Chunk, chunk_stream.h) that are
// also the map editor's live objects: Spec and the classes on it, each with the game's 12 virtuals (OnChunk,
// File, then Spec's virtuals; the last is ObjectSpec's WriteFields or EffectSpec's SetVisible). The record
// classes (kind at +0x48): the base (0); objects (1); flames (6); magic traps (7); levers (8); units (5);
// lights (2), sounds (3) and particle sources (4) on the effect records' base. Names are provisional; see
// docs/notes/objects.md ("Rewritten").
#include "chunk_stream.h"
#include "lock_key.h"
#include "maths.h"
#include "unit_record.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CStream;
class CEngineObject;
class CMapObject;
class CTransformObject;
class CUnit;

// ---- The world objects' parts of the trap and lever records ---------------------------------------
// The fields after a record's ObjectSpec (+0x8C), which the world object's SetUp reads in place.

// A circle of a trap's area (0xC bytes).
struct TrapCircle {
    float x;
    float y;
    float radius;
};
static_assert(sizeof(TrapCircle) == 0xC);

// A point a trap casts at (zero-filled when added).
struct TrapPoint {
    float x;
    float y;
};
static_assert(sizeof(TrapPoint) == 8);

// A trap's area: an MFC CArray of circles.
using TrapArea = mfc::CArray<TrapCircle>;
static_assert(sizeof(TrapArea) == 0x14);

// A trap's points: an MFC CArray of {x, y} pairs.
using TrapPoints = mfc::CArray<TrapPoint>;
static_assert(sizeof(TrapPoints) == 0x14);

// The MagicTrap part of a zone object record (mapspec::TrapSpec::trap; passed to MagicTrap::SetUp).
struct MagicTrapRecord {
    std::uint32_t sides;        // (-1); the zone loader makes it the players the
                                // owner (+0x6E) is hostile to (relation 2), one bit each
    mfc::CString spell;         // the spell's text
    Vector3 position;           // the position when the zone loaded
    TrapArea area;
    TrapPoints points;
    std::int32_t period;
    std::uint8_t singleTarget;
};
static_assert(offsetof(MagicTrapRecord, area) == 0x14);
static_assert(offsetof(MagicTrapRecord, period) == 0x3C);
static_assert(sizeof(MagicTrapRecord) == 0x44);

// The lever part of a zone object record (mapspec::LeverSpec::lever), what Lever::SetUp reads.
struct LeverSetup {
    LockKey lock;                   // ({1, 0, 0}; chunk BBAC0006, a 12-byte blob)
    std::int32_t state;             // (chunk BBAC0002, a byte)
    std::int32_t stateCount;        // (2; chunk BBAC0003, a byte)
    std::uint8_t flag260;           // (chunk BBAC0007)
    std::uint8_t keepsMapFlag;      // (chunk BBAC0008)
    std::uint8_t cycling;           // (chunk BBAC0004)
};
static_assert(offsetof(LeverSetup, cycling) == 0x16);
static_assert(sizeof(LeverSetup) == 0x18);

namespace mapspec {

// An MFC CArray of plain elements (0x14 bytes).
using PlainArray = mfc::CArray<std::uint8_t>;

// A unit record's behaviour (alarm) records (map_spec_unit.cpp; the brain applies one).
// A waypoint (0x14 bytes).
struct Waypoint {
    Vector3 position;
    float time;                  // the look's turning time (AILook::time; chunk BBBE0003)
    std::int16_t lookWait;       // the wait after the look (AILook::wait; chunk BBBE0002)
    std::uint16_t stopFirst;     // bit 0: AILook::stopFirst (chunk BBBE0004)
};
static_assert(sizeof(Waypoint) == 0x14);

// A patrol (0x20 bytes): where it starts and its waypoints (CArray).
struct Patrol {
    Vector3 position;
    mfc::CArray<Waypoint> waypoints;
};
static_assert(sizeof(Patrol) == 0x20);

// CArray<Patrol> (a behaviour's patrols); map_spec_unit.cpp and record_arrays.cpp.
struct Patrols : mfc::CArray<Patrol> {
    // map_spec_unit.cpp. The patrols become a copy of `from` (each patrol copied through a temporary returned by
    // value, as the original does).
    void CopyFrom(const Patrols& from);
    // record_arrays.cpp. By-value GetAt: element `index` copy-constructed into `result` (the position, then the
    // waypoints: storage for exactly the source's count, zero filled, the waypoints copied). Returns result.
    Patrol* CopyAt(Patrol* result, int index) const;
};

// A unit's behaviour record (0x4C bytes).
struct Behaviour {
    mapspec::Behaviour* DeleteScalar(unsigned flags);
    void Read(CStream* stream);
    void Write(CStream* stream) const;
    std::int32_t type;           // (3) 1 guard, 2 patrol, 3 sentry, 4 party, 5 guard at the alarm point
    Patrols patrols;
    Vector3 guard;               // the unit's position when saved
    std::uint32_t loop;          // (1) a patrol loops; 0: its points are walked back too
    std::uint32_t flags;         // bit 3: the unit is always updated (the brain's +0x37)
    float guardRadius;
    float field0A;               // (-1) chunk BBBC000A, only read and written with the record
    std::uint32_t alarmKind;     // the alarm raised when the countdown ends (low byte)
    std::uint32_t enabled;       // (1 for the first record) HearAlarm applies a record other than the
                                 // first only when set
    std::uint32_t callRadius;    // float bits
    std::uint32_t alertMode;
    std::uint32_t alarmDelay;    // (75) the alarm countdown (at least 1)
    std::uint32_t alarm;         // the reaction modes 0..3 (4 while reading: unset)
};
static_assert(offsetof(Behaviour, guard) == 0x18);
static_assert(sizeof(Behaviour) == 0x4C);
// map_spec_unit.cpp: the constructor (no patrols, type 3, loop, radius 10, delay 75; returns this) and the
// destructor body.
Behaviour* __fastcall BehaviourConstruct(Behaviour* self);
void __fastcall BehaviourDestruct(Behaviour* self);

// The base record (0x4C bytes; map_spec.cpp). The records are read and written chunk by chunk (no record is a
// block of file bytes). They live on the game's heap.
class Spec : public Chunk {
public:
    // No parent chunk, id -1, at the origin unrotated, named and commented "none", kind 0.
    Spec();

    void OnChunk() override {}                              // (empty)
    // The world object with the record's id.
    virtual void* WorldObject();
    virtual void Nothing(int) {}                            // (empty)
    // The type name in `out` (a CString returned by value): "error".
    virtual mfc::CString* TypeName(mfc::CString* out);
    // Moves the record, then Apply.
    virtual void Move(float dx, float dy, float dz);
    // Removes the world object (here: Apply).
    virtual void Remove();
    // Empty. Makes or updates the world object.
    virtual void Apply() {}
    // Refreshes the world object (here: Apply).
    virtual void Refresh();
    // Empty. Writes the record as a chunk of its own.
    virtual void Save() {}
    // The deleting destructor.
    virtual ~Spec();

    // On the game's heap (map_spec.cpp): null without memory, and then no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    std::int32_t id;             // the object's id (-1)
    CEngineObject* object;       // the world object made from the record (null: none)
    Vector3 position;
    Quaternion rotation;         // (1, 0, 0, 0)
    mfc::CString name;
    mfc::CString comment;
    std::int32_t spareId;        // (-1) only set and copied (ObjectSpec::Assign)
    std::int32_t kind;           // which record this is: 0 the base, 1 an object, 2 a light, 3 a sound,
                                 // 4 a particle source, 5 a unit, 6 a flame, 7 a magic trap, 8 a lever
};

// An object record (0x8C bytes; chunk B001).
class ObjectSpec : public Spec {
public:
    // Kind 1, no model, textures "none", player -1, scale 0.5, visible, no parts.
    ObjectSpec();
    // The deleting destructor. The quick item effect's record (UseQuickItem) is one too: its destructors are the
    // same code.
    ~ObjectSpec() override;

    void OnChunk() override;                                 // the chunk just read, by its id
    mfc::CString* TypeName(mfc::CString* out) override;      // the player number ("%i")
    void Remove() override;                                  // removes the world object, then Apply
    // Makes the world object, or updates it from the record (position, rotation, scale,
    // visibility) while it is out of the editor's lists.
    void Apply() override;
    void Refresh() override;
    void Save() override;                                    // the record as chunk B001
    // The common fields (also called directly by the subclasses' Save).
    virtual void WriteFields();

    // Copies `from`: the base fields but its kind and chunk state, then the object's.
    void Assign(const ObjectSpec* from);

    // The world object an object record makes is a map object (a unit for a unit record).
    CMapObject* MapObject() const;
    CUnit* Unit() const;   // map_spec_unit.cpp

    mfc::CString model;          // (empty)
    std::int32_t parentId;
    mfc::CString texture;
    mfc::CString primaryTexture;
    std::int32_t player;         // (-1); the type name formats it
    Vector3 scale;               // (0.5 each; read and written y, x, z)
    std::uint8_t shadow;
    std::uint8_t visible;
    std::uint8_t side;           // the owner's player number (chunk B011; CUnit::SetSide, the traps' hostile players)
    std::uint8_t unused_6F;
    mfc::CString parentIname;
    mfc::CString skinTexture;    // (chunk B008) the texture swapped onto the figure ("none": none)
    mfc::CStringArray parts;
};

// A flame (0xA0 bytes; chunk BBBF0000).
class FlameSpec : public ObjectSpec {
public:
    // Kind 6, volume 0.7, the sound "nature\fire\small.wav".
    FlameSpec();
    ~FlameSpec() override;          // (and the deleting destructor)

    void OnChunk() override;
    void Apply() override;          // the world object is always made again
    void Save() override;           // chunk BBBF0000 with the flame's fields, then the common ones

    float volume;                // (0.7)
    Vector3 offset;
    mfc::CString sound;          // ("nature\fire\small.wav")
};

// A magic trap (0xD0 bytes; chunk BBAB0000).
class TrapSpec : public ObjectSpec {
public:
    // Kind 7, every side, no area or points, period 15.
    TrapSpec();
    // The deleting destructor. The arrays' destructors inlined: their blocks freed.
    ~TrapSpec() override;

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // "MagicTrap"
    // Chunk BBAB0000: the trap's fields (the arrays as a count and the raw elements), then
    // the common ones.
    void Save() override;

    MagicTrapRecord trap;
};

// A lever (0xA4 bytes; chunk BBAC0000).
class LeverSpec : public ObjectSpec {
public:
    // Kind 8, the key {1, 0, 0}, two states.
    LeverSpec();
    ~LeverSpec() override;          // (and the deleting destructor)

    void OnChunk() override;
    void Save() override;           // chunk BBAC0000

    LeverSetup lever;
};

// The zone's player table (0x1014 bytes; DiplomacyConstruct): the relation of every pair of the 32 players (1
// by default, 0 with itself, 2 hostile) and their names.
struct ZoneDiplomacy {
    std::int32_t relations[32][32];
    mfc::CStringArray names;
};
static_assert(offsetof(ZoneDiplomacy, names) == 0x1000);
static_assert(sizeof(ZoneDiplomacy) == 0x1014);

// The effect records' base (0x54 bytes; code in map_spec_effect.cpp).
class EffectSpec : public Spec {
public:
    ~EffectSpec() override;         // (and the deleting destructor)

    // Effects are managed under -3001 - id.
    void* WorldObject() override;
    // Moves the record, not below the terrain, then Apply.
    void Move(float dx, float dy, float dz) override;
    // SetVisible (the sound record's is the same code). Shows or hides the world object (flag 0x40 hides).
    virtual void SetVisible(std::uint8_t newVisible);

    // The world object an effect record makes (the effect's carrier: a transform object).
    CTransformObject* Transform() const;

    void* effect;                // the effect the world object runs
    std::uint8_t visible;
    std::uint8_t unused_51[3];

protected:
    // The records' constructors begin with this (inlined in each): no world object or effect, visible.
    EffectSpec() : effect(nullptr), visible(1) {}
};

// A particle source (chunk DD01).
class ParticleSpec : public EffectSpec {
public:
    ParticleSpec();                 // Kind 4, no type, scale 1.
    ~ParticleSpec() override;       // (and the deleting destructor)

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // "Particle source"
    void Apply() override;          // makes the source (when it has a type) or updates its effect
    void Save() override;           // chunk DD01

    std::int32_t type;           // (0: none made)
    float scale;
};

// A light (chunk AA01).
class LightSpec : public EffectSpec {
public:
    LightSpec();                    // Kind 2, white, shadows, radius 10.
    ~LightSpec() override;          // (and the deleting destructor)

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // "Light"
    void Apply() override;          // makes the light or updates its effect (colour, radius, position)
    void Save() override;           // chunk AA01

    Vector3 colour;              // (1, 1, 1)
    std::uint8_t shadows;
    std::uint8_t unused_61[3];
    float radius;
};

// A sound (chunk CC01).
class SoundSpec : public EffectSpec {
public:
    SoundSpec();                    // Kind 3, no sounds, volume 10, radius 15, pauses 1500-3000.
    ~SoundSpec() override;          // (and the deleting destructor)

    void OnChunk() override;
    mfc::CString* TypeName(mfc::CString* out) override;      // "Sound"
    // Makes the sound (when it has sounds); otherwise moves the world object and makes its
    // effect again.
    void Apply() override;
    void Save() override;           // chunk CC01

    mfc::CStringArray sounds;     // (none made without any)
    std::int32_t volume;         // (10; chunk CC04) the sound's volume
    std::int32_t radius;         // (15; chunk CC0B) the sound's radius
    std::int32_t unused_70;
    std::int32_t minInterval;    // (1500; chunk CC06) an ambient sound's shortest pause
    std::int32_t maxInterval;    // (3000; chunk CC07) and longest
    std::int32_t unused_7C;
    std::int32_t unused_80;
    std::uint8_t ambient;        // (chunk CC0D) set: an ambient sound (class 0x49), else a plain one (0x48)
    std::uint8_t field0E;        // chunk CC0E, only read and written with the record
    std::uint8_t unused_86[2];
};

// A unit record (0x324 bytes; chunk BBBB0000; code in map_spec_unit.cpp). The world server's CreateUnits
// fills one on the stack for each unit it creates.
class UnitSpec : public ObjectSpec {
public:
    // Kind 5, five behaviour records (the first enabled), the prototype "none", no statistics.
    UnitSpec();
    ~UnitSpec() override;           // (and the deleting destructor)

    void OnChunk() override;        // the chunk just read, by its id
    void Apply() override;          // makes the unit, or moves, turns and rescales it
    void Save() override;           // chunk BBBB0000

    std::int32_t behaviours;     // records read so far
    Behaviour behaviour[5];
    mfc::CString prototype;      // ("none")
    unitrecord::UnitRecord record;   // the statistics and item lists (applied to the unit by the world server)
};

// Record constructors (each returns this), by .mob chunk id.
ZoneDiplomacy* __fastcall DiplomacyConstruct(ZoneDiplomacy* self);
// The records of a .mob file by id (a local of the zone loader): an STLport hash_map<int, record*> (100
// buckets to start) and the largest id read.
struct MobRecords {
    stlp::hash_map<std::int32_t, Spec*, stlp::IntHash> byId;
    std::int32_t maxId;
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
