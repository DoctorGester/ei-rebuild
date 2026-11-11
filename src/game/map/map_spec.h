#pragma once

// The zone file's object records (the .mob objects): chunk readers / writers that are also the map editor's
// live objects. See docs/notes/objects.md.
#include "game/map/chunk_stream.h"
#include "game/objects/lock_key.h"
#include "lib/vecmath.h"
#include "game/units/unit_record.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "lib/bit_flags.h"
#include "lib/hash_map.h"

#include <vector>

class ByteStream;
class EngineObject;
class MapObject;

// ---- The world objects' parts of the trap and lever records ---------------------------------------
// The fields after a record's ObjectSpec, which the world object's SetUp reads in place.

// A circle of a trap's area.
struct TrapCircle {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
};
static_assert(sizeof(TrapCircle) == 0xC);   // saved as its 12 bytes
constexpr bool NoPadding(const TrapCircle*) {
    return lib::FieldsFill<&TrapCircle::x, &TrapCircle::y, &TrapCircle::radius>();
}


// The MagicTrap part of a zone object record (mapspec::TrapSpec::trap; passed to MagicTrap::SetUp).
struct MagicTrapRecord {
    std::uint32_t sides = 0;    // (-1); the zone loader makes it the players the
                                // owner is hostile to (relation 2), one bit each
    std::string spell;         // the spell's text
    Vector3 position;           // the position when the zone loaded
    std::vector<TrapCircle> area;
    std::vector<Vector2> points;
    std::int32_t period = 0;
    std::uint8_t singleTarget = 0;
};

// The lever part of a zone object record (mapspec::LeverSpec::lever), what Lever::SetUp reads.
struct LeverSetup {
    LockKey lock;                   // ({1, 0, 0}; chunk BBAC0006, a 12-byte blob)
    std::int32_t state = 0;         // (chunk BBAC0002, a byte)
    std::int32_t stateCount = 0;    // (2; chunk BBAC0003, a byte)
    std::uint8_t directUseBlocked = 0;  // (chunk BBAC0007)
    std::uint8_t keepsMapFlag = 0;  // (chunk BBAC0008)
    std::uint8_t cycling = 0;       // (chunk BBAC0004)
};

// The bits of mapspec::Behaviour::flags. Before format 0x53 (kVersionUnitAlarm) the reaction (Behaviour::alarm) was
// a bit each; the first set one counts, none is 2 (mapspec::Reaction).
enum class BehaviourFlag : std::uint32_t {
    OldFight = 0x1,             // reaction 0
    OldRevenge = 0x2,           // 1
    OldWatch = 0x4,             // 2
    AlwaysUpdated = 0x8,        // chunk BBBC000D: the unit is always updated
    OldWatchFearful = 0x10,     // 3
};
template <> inline constexpr bool lib::kBitFlags<BehaviourFlag> = true;

namespace mapspec {

// A waypoint of a unit record's patrol.
struct Waypoint {
    Vector3 position;
    float time = 0.0f;           // the look's turning time (AILook::time; chunk BBBE0003)
    std::int16_t lookWait = 0;   // the wait after the look (AILook::wait; chunk BBBE0002)
    std::uint16_t stopFirst = 0;  // bit 0: AILook::stopFirst (chunk BBBE0004)
};
static_assert(sizeof(Waypoint) == 0x14);   // saved as its 20 bytes

// A patrol: where it starts and its waypoints.
struct Patrol {
    Vector3 position;
    std::vector<Waypoint> waypoints;
};

// What a behaviour record makes the unit do (Behaviour::type; AIBrain::ApplyBehaviour). Any other value adds no mode
// of its own.
enum class BehaviourType : std::int32_t {
    None = 0,
    Guard = 1,
    Patrol = 2,
    Sentry = 3,
    Party = 4,
    GuardAlarm = 5,     // guards the world alarm point of its slot
};

// How a unit with a behaviour reacts to what it senses (Behaviour::alarm): the modes ApplyBehaviour adds.
enum class Reaction : std::int32_t {
    Fight = 0,
    Revenge = 1,        // aggression only against the enemy a script set (UMRevenge)
    Watch = 2,
    WatchFearful = 3,
    Unset = 4,          // while a record is read: no chunk set it
};

// When a unit counts as alerted (Behaviour::alertMode, AIBrain::alertMode; the alarm countdown runs only while it
// is). The numbers are the saved values.
enum class AlertMode : std::int32_t {
    OnSight = 0,        // a living hostile unit in sight
    OnSeen = 1,         // AIBrain::SetSeen: a hit, a call, a script
    OnSightOrMode = 2,  // in sight, or an alert mode (suspection, aggression, corpse watching, fear) takes charge
};

// A unit's behaviour (alarm) record; the brain applies one.
struct Behaviour {
    void Read(ByteStream* stream);
    void Write(ByteStream* stream) const;
    BehaviourType type = BehaviourType::Sentry;
    std::vector<Patrol> patrols;
    Vector3 guard{};             // the unit's position when saved
    std::uint32_t loop = 1;      // a patrol loops; 0: its points are walked back too
    std::uint32_t flags = 0;     // BehaviourFlag bits
    float guardRadius = 10.0f;
    float unreadChunk = -1.0f;   // chunk BBBC000A; only read
    std::uint32_t alarmKind = 0; // the alarm raised when the countdown ends (low byte)
    std::uint32_t enabled = 0;   // (1 for the first record) HearAlarm applies a record other than the
                                 // first only when set
    float callRadius = 10.0f;
    AlertMode alertMode = AlertMode::OnSight;
    std::uint32_t alarmDelay = 75;   // the alarm countdown (at least 1)
    Reaction alarm = Reaction::Fight;
};

// Which record a Spec is. QuickEffect is an object record no zone file holds: the effect object a quick item makes
// (WorldServer::UseQuickItem).
enum class SpecKind : std::int32_t { Base, Object, Light, Sound, Particle, Unit, Flame, Trap, Lever, QuickEffect };

// The base record. The records are read chunk by chunk (no record is a block of file bytes).
class Spec : public Chunk {
public:
    Spec();

    void OnChunk() override {}
    virtual ~Spec();

    std::int32_t id = -1;        // the object's id (-1)
    Vector3 position;
    Quaternion rotation;         // (1, 0, 0, 0)
    std::string name;
    std::string comment;
    std::int32_t spareId = -1;   // (-1) only set and copied (ObjectSpec::Assign)
    SpecKind kind = SpecKind::Base;
};

// An object record (chunk B001).
class ObjectSpec : public Spec {
public:
    ObjectSpec();
    ~ObjectSpec() override;

    void OnChunk() override;                                 // the chunk just read, by its id

    MapObject* object = nullptr;   // the map object made from the record (a unit for a unit record; null: none)
    std::string model;          // (empty)
    std::int32_t parentId = 0;
    std::string texture;
    std::string primaryTexture;
    std::int32_t player = -1;    // (-1); the type name formats it
    Vector3 scale;               // (0.5 each; read y, x, z)
    std::uint8_t shadow = 0;
    std::uint8_t visible = 1;
    std::uint8_t side = 0;       // the owner's player number (chunk B011; Unit::SetSide, the traps' hostile players)
    std::string parentIname;
    std::string skinTexture;    // (chunk B008) the texture swapped onto the figure ("none": none)
    std::vector<std::string> parts;
};

// A flame (chunk BBBF0000).
class FlameSpec : public ObjectSpec {
public:
    FlameSpec();
    ~FlameSpec() override;

    void OnChunk() override;

    float volume = 0.7f;         // (0.7)
    Vector3 offset;
    std::string sound;          // ("nature\fire\small.wav")
    std::string effect = "CampFire";   // TOML only (`effect`): its particle effect's name
};

// A magic trap (chunk BBAB0000).
class TrapSpec : public ObjectSpec {
public:
    TrapSpec();
    ~TrapSpec() override;

    void OnChunk() override;

    MagicTrapRecord trap;
};

// A lever (chunk BBAC0000).
class LeverSpec : public ObjectSpec {
public:
    LeverSpec();
    ~LeverSpec() override;

    void OnChunk() override;

    LeverSetup lever;
};

// The zone's player table: the relation of every pair of the 32 players (1
// by default, 0 with itself, 2 hostile) and their names.
struct ZoneDiplomacy {
    ZoneDiplomacy();

    std::int32_t relations[32][32] = {};
    std::vector<std::string> names;
};

// The effect records' base.
class EffectSpec : public Spec {
public:
    ~EffectSpec() override;

protected:
    EffectSpec() = default;
};

// A particle source (chunk DD01).
class ParticleSpec : public EffectSpec {
public:
    ParticleSpec();                 // Kind Particle, no effect, scale 1.
    ~ParticleSpec() override;

    void OnChunk() override;

    std::string effect;          // its effect's name (the .mob's type by its name; empty: none shown)
    float scale = 1.0f;
};

// A light (chunk AA01).
class LightSpec : public EffectSpec {
public:
    LightSpec();                    // Kind Light, white, shadows, radius 10.
    ~LightSpec() override;

    void OnChunk() override;

    Vector3 colour;              // (1, 1, 1)
    std::uint8_t shadows = 1;
    float radius = 10.0f;
};

// A sound (chunk CC01).
class SoundSpec : public EffectSpec {
public:
    SoundSpec();                    // Kind Sound, no sounds, volume 10, radius 15, pauses 1500-3000.
    ~SoundSpec() override;

    void OnChunk() override;

    std::vector<std::string> sounds;   // (none made without any)
    std::int32_t volume = 10;    // (10; chunk CC04) the sound's volume
    std::int32_t radius = 15;    // (15; chunk CC0B) the sound's radius
    std::int32_t minInterval = 1500;  // (1500; chunk CC06) an ambient sound's shortest pause
    std::int32_t maxInterval = 3000;  // (3000; chunk CC07) and longest
    std::uint8_t ambient = 0;    // (chunk CC0D) set: an ambient sound (class 0x49), else a plain one (0x48)
    std::uint8_t unreadChunk = 0;  // chunk CC0E; only read and written with the record
};

// A unit record (chunk BBBB0000). The world server's CreateUnits fills one for each unit it creates.
class UnitSpec : public ObjectSpec {
public:
    UnitSpec();
    ~UnitSpec() override;

    void OnChunk() override;        // the chunk just read, by its id

    std::int32_t behaviours = 0;  // records read so far
    Behaviour behaviour[5];
    std::string prototype;      // ("none")
    unitrecord::UnitRecord record;   // the statistics and item lists (applied to the unit by the world server)
};

// The records of a .mob file (a local of the zone loader).
struct MobRecords {
    // Takes `spec`, filed under its id (a later record of an id takes the earlier one's place in byId).
    void Add(std::unique_ptr<Spec> spec);

    lib::HashMap<std::int32_t, Spec*> byId;   // walked in STLport's order: the objects are created in it
    std::vector<std::unique_ptr<Spec>> read;  // every record, in the order read
};

// The weather of a .mob (chunk 0xABD0: the wind and the time of day, applied to the server).
struct MobWeather {
    Vector3 wind = {0.0f, 1.0f, 0.0f};
    float windStrength = 0.0f;
    float time = 0.0f;
};

// What a .mob file holds, read from its chunks or from its TOML (docs/notes/mods.md, "Map objects as TOML").
struct MobContents {
    MobRecords records;
    ZoneDiplomacy diplomacy;
    std::vector<MobWeather> weather;   // each applied, in order
    bool appliesRelations = true;      // the last of chunks 0xC000 (true) / 0xD000 (false); true without either
    std::string scripts;
    // The .lua that stands for the scripts (docs/notes/mods.md, "Scripts in Lua"; WorldServer::LoadMapObjects finds
    // it): its text and its name in Lua's messages; `luaName` empty when there is none. A TOML's `script` key names
    // its file (`scriptFile`, empty when the TOML has none).
    std::string scriptFile;
    std::string lua;
    std::string luaName;
};

// A .mob file's root chunk (WorldServer::LoadMapObjects opens it).
class MobFile : public RootChunk {
public:
    // Reads the file's top-level chunks: the scripts (0xACCEECCA plain, 0xACCEECCB encoded), the weather (0xABD0),
    // the records (0xB000), the players (0xDDDDDDD1) and the relations flag; with `objects` false the scripts alone.
    MobContents ReadChunks(bool objects = true);
};

// Applies a .mob's contents to the server world: the weather, the player relations (when it applies them), every
// record's world object (traps learn their hostile players and position, units their alarm records), then the
// scripts unless `keepIds`' low byte is set (a script error goes to the console under `name`).
void LoadMob(MobContents& contents, int keepIds, std::string_view name);

} // namespace mapspec
