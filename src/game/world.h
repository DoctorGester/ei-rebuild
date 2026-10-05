#pragma once

// The world: the server side that runs the game rules (CWorldServer, the global at 007AFF60) and
// the client side that draws it and answers terrain queries (CWorldClient, the global at 007AF5B8).
// Both derive from a common base whose methods live at 0055E000-0055FFFF. Class names come from
// the game's error strings ("CWorldServer::LoadMap", "CWorldClient::ToggleWireframe"); everything
// else is provisional. Only the members used by the rewritten functions are declared.

#include <cstddef>
#include <cstdint>

#include "core_streams.h"
#include "engine_object.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "maths.h"

using BOOL = int;

struct ScriptGroup;
class CBaseManager;
class CSectorMap;   // sector_map.h
class CSpellManager;   // spell.h
class Terrain;
struct Float4;   // chunk_stream.h: four floats passed by value

namespace world {
constexpr int kClassUnit = 0x50;   // CEngineObject::GetClassId() of units
}

// A player (one per human or AI side). Fields named only as far as used here.
struct WorldPlayer {
    std::uint8_t unknown_000[0x24];
    int id;                              // +0x024 player number (0 = the local player)
    std::uint8_t unknown_028[0xA0 - 0x28];
    std::uint8_t* mercs;                 // +0x0A0 0x784-byte records, unit id at +0x1C
    int mercCount;                       // +0x0A4
    std::uint8_t unknown_0A8[0xC4 - 0xA8];
    std::uint8_t variables[0x110 - 0xC4]; // +0x0C4 named float variables ("gtime", quest flags)
    int lastTick;                        // +0x110
    std::uint8_t unknown_114[0x130 - 0x114];
    std::uint32_t dirty;                 // +0x130 changes still to send to this player
};
static_assert(offsetof(WorldPlayer, mercs) == 0xA0);
static_assert(offsetof(WorldPlayer, variables) == 0xC4);
static_assert(offsetof(WorldPlayer, dirty) == 0x130);

// Walks the world's player list. The list object is reference counted (+4) while an iterator holds
// it; its MFC list head is at +0x38 and each node's data (+8) is a WorldPlayer.
struct PlayerIterator {
    CEngineObject* list = nullptr;
    void* next = nullptr;

    WorldPlayer* First(CEngineObject* players);   // 00404920
    WorldPlayer* Next();                          // 00404960
    // The original inlines this: drop the list's count without releasing it.
    ~PlayerIterator() {
        if (list) --list->refCount_;
    }
};
static_assert(sizeof(PlayerIterator) == 8);

// hash_map<unsigned, CEngineObject*> plus the highest key ever inserted (several instantiations
// share this shape: 005999E0 / 00599BD0 are its Set).
struct ObjectIdMap {
    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash> map;   // +0x00
    int maxId;                                                      // +0x14

    // 005999E0 and 00599BD0 (identical code, different template instantiations): map[key] = value
    // and raise maxId.
    void Set(const unsigned& key, CEngineObject* const& value);
};
static_assert(sizeof(ObjectIdMap) == 0x18);

// A zone exit as the world server keeps it (0x3C bytes); the client keeps the same records.
struct ZoneExit {
    std::uint8_t unknown_00[0x14];
    float x1, y1, x2, y2;                 // +0x14 rectangle
    std::uint8_t unknown_24[4];
    char* target;                         // +0x28 CString: target zone ("" = none)
    int entrance;                         // +0x2C entrance number in the target zone
    std::uint8_t unknown_30[0x3C - 0x30];

    // 004E59F0. Whether (x, y) lies inside the exit area; not rewritten.
    bool Contains(float x, float y) const;
    // 004608B0. Scalar deleting destructor (flags & 1: free the record too); returns this.
    ZoneExit* DeletingDestructor(unsigned flags);
};
static_assert(sizeof(ZoneExit) == 0x3C);
static_assert(offsetof(ZoneExit, target) == 0x28);

// MFC CArray<ZoneExit> (CWorldServer + 0xEC; the map database's zone records hold one at +0x48).
struct ZoneExitArray {
    void* vtable;
    ZoneExit* data;                       // +0x04
    int size;                             // +0x08
    int capacity;                         // +0x0C
    int growBy;                           // +0x10

    // 00573080. CArray::SetSize(size, growBy) (-1 keeps growBy); new records are constructed.
    void SetSize(int newSize, int newGrowBy);
};
static_assert(sizeof(ZoneExitArray) == 0x14);

// MFC CStringArray (vtable, data, size, capacity, growBy).
struct StringArray {
    void* vtable;
    char** data;                          // +0x04 CStrings
    int size;                             // +0x08
    int capacity;                         // +0x0C
    int growBy;                           // +0x10

    // 006FEBCE. CStringArray::SetAtGrow(index, text).
    void SetAtGrow(int index, const char* text);
};
static_assert(sizeof(StringArray) == 0x14);

// Members both world classes share (base class, 0055E000-0055FFFF).
class CWorld : public CEngineObject {
public:
    // Virtual slots after CEngineObject's 13 (server vtable 0073F928, client 0073F41C). Slots 26,
    // 29 and 30 are called by name; 13-25, 27 and 28 hold their places (the managers' getters, 24
    // the server's CopyItem; rewritten slots stay plain members, rewrite guide rule 4).
    virtual void Slot13();
    virtual void Slot14();
    virtual void Slot15();
    virtual void Slot16();
    virtual void Slot17();
    virtual void Slot18();
    virtual void Slot19();
    virtual void Slot20();
    virtual void Slot21();
    virtual CSpellManager* SpellManager();   // 22: 0055EBA0 (+0x4C)
    virtual void Slot23();
    virtual void Slot24();
    virtual void Slot25();
    virtual int IsServer();               // 26: 1 for the server (0058A380), 0 for the client (0055EB30)
    virtual void Slot27();                // 27: the terrain height (CWorldServer::GridHeight 00594220)
    virtual void Slot28();                // 28: the server's AI map height (0058A390), client 00564070
    // 29 / 30: the ground height and the height of the water surface at (x, y), in ST0 (server
    // 0058A3D0 / 0058A3F0 call slots 27 / 28; client 005618E0 / 00561930 also clamp, 00560F00).
    virtual double GroundHeightAt(float x, float y);
    virtual double SurfaceHeightAt(float x, float y);
    // 31 (world_time.cpp). 0055FB30: the time of day becomes `hours` (the base +0x1C is moved so
    // that fmod(tick * secondsPerTick + base, 24) = hours). The server's 0058EEA0 also updates the
    // players' times first (game kinds 1 and 3), marks them dirty (4) and updates the weather.
    virtual void SetTime(float hours);
    // 32 (world_time.cpp). 0055FB50: seconds per tick become 1 / speed (0: stopped; negative: the
    // setting 0079BE5C) times 1/15, keeping the time of day (slot 31). The server's 0058EEE0 as above.
    virtual void SetTimeSpeed(float speed);

    // 0055FCB0 (world_time.cpp). Stores the wind direction (+0x24) and strength (+0x30).
    void SetWind(const Vector3* direction, float strength);

    float timeBase_;                     // +0x1C
    float secondsPerTick_;                // +0x20
    std::uint8_t unknown24_[12];          // +0x24 wind direction (three floats); saved and loaded as they are
    std::uint32_t unknown30_;             // +0x30 wind strength (float bits, 0..1); saved and loaded
    void* unknown34_;                     // +0x34 (slot 23 returns it)
    CEngineObject* players_;              // +0x38 player list (vtable slot 20 returns it)
    CEngineObject* manager3C_;            // +0x3C (slot 15)
    CEngineObject* manager40_;            // +0x40 map objects by id (slot 19; its slot 23 creates one)
    CEngineObject* manager44_;            // +0x44 (slot 21)
    CEngineObject* manager48_;            // +0x48 the timed objects (spells, effects; slot 14)
    CEngineObject* manager4C_;            // +0x4C (slot 22)
    CEngineObject* manager50_;            // +0x50 (slot 17)
    CEngineObject* itemManager_;          // +0x54 items (slot 18)
    CEngineObject* manager58_;            // +0x58 (slot 16)
    std::uint8_t unknown_5C[4];
    void* unknown60_;                     // +0x60 replaced by 0055F410 when the game ends
    int tick_;                            // +0x64 game time in ticks
};
static_assert(offsetof(CWorld, unknown24_) == 0x24);
static_assert(offsetof(CWorld, manager40_) == 0x40);
static_assert(offsetof(CWorld, itemManager_) == 0x54);
static_assert(offsetof(CWorld, players_) == 0x38);
static_assert(offsetof(CWorld, unknown60_) == 0x60);
static_assert(offsetof(CWorld, tick_) == 0x64);

// One of the five alarms InvokeAlarm raises (CWorldServer + 0x8C, 16 bytes each; the script's
// IsAlarm / AlarmTime / AlarmPosX / AlarmPosY read them).
struct WorldAlarm {
    std::uint8_t active;                  // +0x00
    std::uint8_t unknown_01[3];
    int time;                             // +0x04 tick it was raised
    float x, y;                           // +0x08
};
static_assert(sizeof(WorldAlarm) == 16);

// A missile in flight (0xA8 bytes, constructor 0067E510; created by 0058E670). Reference counted
// through a 16-bit count at +4. Not rewritten.
struct Projectile {
    // 0067E6D0. Moves the missile one tick; 0 once it has hit or expired.
    int Update();
};
// The world server's missiles: an MFC CList of counted pointers (CWorldServer + 0x12E28).
struct ProjectileList {
    using Node = mfc::CTypedPtrList<Projectile>::Node;
    // 0059C810. Releases the node's missile and returns the node to the free list (the last one
    // frees every block).
    void FreeNode(Node* node);

    mfc::CTypedPtrList<Projectile> list;
};
static_assert(sizeof(ProjectileList) == 0x1C);

// The stream the world's save and load code reads and writes (the CMemoryStream holding
// mission.sav) is the engine's CStream (core_streams.h); version_ is the save format version.
using WorldStream = CStream;

// The game rules side (vtable 0073F928).
class CWorldServer : public CWorld {
public:
    // 00593AA0, vtable slot 40. World object with this id, or null.
    CEngineObject* FindObject(unsigned id);
    // 005927C0 (RET 4; world_units.cpp). Creates the object a creation message (+0x48 its kind)
    // describes: map objects and units (1, 5, 8, 9; 6 with a figure and maybe a campfire; 7) go
    // into the object maps, the update map and the sector map; sounds (3), lights (2) and kind 4
    // become timed objects. Returns the new id, -1 when refused, the message's address for an
    // unknown kind. Rebirth's FixAddMapObject takes index 0 for a unit without a prototype record.
    int CreateObject(std::uint8_t* message);
    // 005935C0. Removes object `id` from the server: out of the sector map and every id map, its
    // slot 5, flagged 1, the command queues, every object (slot 56), the missiles and the players'
    // states forget it, and the maps' reference is released. An unknown id crashes the original
    // (null read); Rebirth's FixRemoveMapObject ignores it.
    void RemoveObject(unsigned id);
    // 00594180. Player number `id`, or null.
    WorldPlayer* GetPlayer(int id);
    // 00594220, vtable slot 27. Ground height from the server's coarse height grid; 0 outside.
    float GridHeight(float x, float y);
    // 00591740. Living mercenaries of player `id`, not counting the leader (0 without the player).
    int CountLivingMercs(int id);
    // 005949D0. Whether object `objectId` is on the shared-object list for player `playerId` (or
    // for everyone). In multiplayer the player is looked up on the game server.
    bool IsObjectShared(int objectId, int playerId);
    // 00594A30. Fills `group` with the objects on that list for player `playerId`.
    void CollectSharedObjects(ScriptGroup* group, int playerId);
    // 0058B6D0. ORs `mask` into every player's dirty flags.
    void MarkPlayersDirty(std::uint32_t mask);
    // world_time.cpp. 0058EEA0 / 0058EEE0: slots 31 / 32 (see CWorld).
    void SetTime(float hours) override;
    void SetTimeSpeed(float speed) override;
    // 0058EE20 (world_time.cpp; script and .mob weather). SetWind with the strength clamped to
    // 0..1 (NaN: 0), then the players marked dirty (8).
    void ChangeWind(const Vector3* direction, float strength);
    // 0058CC70 / 0058CC80 / 0058EE00 / 005942C0. Setters (weather and similar map settings).
    void SetUnknownE0(int value);
    void SetUnknownDC(int value);
    void SetUnknownE4E8(int e4, float e8);
    void SetUnknown6C(std::uint8_t value);
    // 0058E4C0. Target zone and entrance of exit `index`; FALSE when out of range.
    BOOL GetZoneExit(int index, mfc::CString* target, int* entrance);
    // 0058E520. Registers an object that lives for a limited time (spells, effects): stamps the
    // current tick, adds a reference, marks it new and puts it in timedObjects_.
    void AddTimedObject(CEngineObject* object);
    // 0058E5D0. Flags object `id` deleted, releases it and removes it from timedObjects_.
    void RemoveTimedObject(unsigned id);
    // 0058D5D0. Puts a unit in the update map (+0x104) and sets its update slot.
    void AddToUpdateMap(CEngineObject* unit);

    // world_tick.cpp
    // 0058DCD0. One world tick: advances the tick count, runs every timed object (spells,
    // effects) and drops those that are done, advances the time wheel and the countdowns,
    // moves the missiles, runs the script command queues (the first only while +0x160 is set or
    // `force`) and then the units (0058D670).
    void Tick(char force);
    // 0058D670. Runs the objects: with +0x12D64 == 1 the units in the update map (+0x104), each
    // counting down to sleep (+0x174; out of the map when its controller is idle), else every map
    // object (+0x12DA0); then the counted objects (+0x12E44, dropped when their slot 6 returns 0).
    // Objects flagged 1 are removed (005935C0) at the end. Rebirth's FixSpellEffectsOverflow first
    // ticks the effects of sleeping objects (TickSleepingEffects).
    void UpdateUnits();
    // Rebirth's FixSpellEffectsOverflow #0 (no original address): counts the effects of sleeping
    // map objects down and wakes those with an expired one.
    void TickSleepingEffects();
    // 0058D3D0. Puts every unit in the sectors within `radius` of `position` (x, y) in the
    // update map.
    void WakeUnitsAround(const float* position, float radius);
    // 00593FC0. Raises alarm `index` (0..4) at `point` (x, y) unless it is already raised
    // (`force` raises it again; alarm 0 with `force` clears all five first), tells every AI unit
    // without a player, and puts those that react in the update map. Script InvokeAlarm.
    void InvokeAlarm(int index, const float* point, char force);
    // 0058DC90. Records how the game ended (+0x138, only the first time); results 2 and 3 also
    // replace +0x60 (0055F410) and call its slot 4.
    void SetGameResult(int result);

    // world_zone.cpp
    // 005915E0 (RET 4). Replaces the zone exits (+0xEC) with copies of `source`'s records that
    // lead somewhere (target not ""); an exit without an entrance must lead to "none".
    void SetZoneExits(const ZoneExitArray* source);
    // 005910A0 (RET 0x10). Script AddUnitToServer: builds a unit record for prototype
    // `prototype` named `name` (006628D0), gives it the id `id` (the name's text key) and creates
    // it at `position` with the default rotation (0058EF20).
    void AddUnitToServer(unsigned id, const char* prototype, const char* name, const Vector3* position);
    // 00596030 (RET 8). Reads the objects of a .mob file (chunked, 00481660). Returns 1, or what
    // FatalError returns when the file cannot be opened or read.
    int LoadMapObjects(const char* file, int keepIds);
    // 00595AB0 (RET 4). Loads .mob file `file` once (the loaded names are kept at +0x124).
    int LoadMobFile(const char* file);
    // 00596C60. Removes every map object (+0x12DA0) from the server, releases the missiles and
    // the counted objects at +0x12E44 and clears the countdowns.
    void ClearMapObjects();
    // 00596730 (RET 4). Writes every map object to a save stream: id, prototype name, the
    // object's own Save (slot 10) and its two flag words; -1 ends the list.
    void SaveMapObjects(void* stream);

    // world_save.cpp
    // 00596E70 (RET 8). Restores the world from a mission save: zone names, settings, alarms and
    // clock, the zone itself (LoadMap with the saved AI-graph state), the managers' objects, the
    // map objects, timed objects, missiles, time wheel, script state, players, shared objects;
    // units that cannot stand where they were saved are moved to a free cell or removed. `map`
    // replaces the saved map name when not null. Returns 1, 0 when the AI graph cannot be read,
    // or what FatalError returns when the zone cannot be loaded.
    int LoadSave(WorldStream* stream, const char* map);
    // 00597BD0 (RET 4). Writes what LoadSave reads.
    void SaveWorld(WorldStream* stream);
    // 00596900 (RET 4). Reads the map objects SaveMapObjects wrote, registers them in the id maps
    // and wakes them.
    void LoadSavedMapObjects(WorldStream* stream);
    // 0058D280 (RET 4). Adds the time each player missed since its last update (plus `offset`) to
    // its "gtime" variable.
    void UpdatePlayerTimes(float offset);
    // 005947E0 (RET 0x10). Puts a new entry at the head of the shared-object list (+0x12E60):
    // `id`, the session player's +0x30 for that id in a multiplayer game (else -1), `value`, and
    // references to the objects of `objects` (a CPtrList). The second argument is unused.
    void AddSharedObject(int id, int unused, std::uint32_t value, void* objects);
    // 005956E0. Before a save: every living unit among the map objects (the walk stops at an
    // empty entry) refills its controller's slots (00525AF0 on +0x1E0) and runs the controller's
    // slot 19 with 0.
    void PrepareSave();
    // 00596140 (RET 0xC; world_game.cpp). Reads the zone's AI graph into the memory stream `out`:
    // the 0x31415926 chunk of the .mob file when there is one, else the whole <map>.grp file.
    // 1 when found.
    int ReadAIGraph(const char* map, const char* mob, void* out);
    // 00596350 (RET 4; world_game.cpp). The zone's scripts from the .mob file's 0xA000 chunk (its
    // 0xACCEECCB text packed and XORed, decoded by 004D8E10, or the plain 0xACCEECCA text) become
    // the only line of the first command queue.
    void LoadZoneScripts(const char* mob);
    // 0058DAF0 (RET 4; world_game.cpp). While a game runs (+0x138), every new connection the
    // lobby hands over (its slot 6) becomes a player (0066C180 / 00665990) of this game's kind,
    // fully dirty, starting at startPosition_; "Accepted player <id>" goes to the network log.
    void AcceptNewPlayers(void* lobby);

    // world_game.cpp
    // 0057AA80 (RET 4). Copy-constructs the zone name (+0x120) into `out`; returns out.
    mfc::CString* ZoneNameInto(mfc::CString* out);

    // world_map.cpp
    // 00595C70 (RET 0x10). Loads map `map` (when it changed; CAIMap 005B43C0) and the AI layer
    // state from `graph`, then the zone: the same .mob again starts it over (queues, diplomacy,
    // map objects, missiles and timed objects cleared), another one first empties the
    // world (ResetZone 0058B250); every player is marked dirty; the .mob objects are loaded (fresh
    // zones start, 005951A0). From a save the loaded objects are cleared again (the save holds
    // them). Returns 1, 0 when the map or the objects cannot be loaded.
    int LoadMap(const char* map, const char* mob, void* graph, char fromSave);
    // 0058B250. Empties the world of the current zone: shared objects, .mob files, countdowns,
    // exits, command queues, missiles, counted objects, the players' zone data, every object
    // (slot 5 then the maps), timed objects, the zone names and three managers.
    void ResetZone();
    // 0058B600. ResetZone, then the map name, diplomacy, both command queues and the AI map are
    // reset and the game state cleared: +0x15C, the day-time value, alarms, the kind, the start
    // position, the day factor (1), loaded_ and +0x13C.
    void ResetState();
    // 005957E0. Before every game state request: every player leaves, the shared objects, .mob
    // files, script queues, diplomacy, map objects, missiles and timed objects go, then the
    // base class's teardown (0055F890); the game result and kind are cleared.
    void TeardownZone();
    // 005951A0. After a fresh zone is loaded: the map objects that are not part of the map
    // (slot 29) move from the map-object maps to +0x12D88 and are sent, with the zone header
    // (+0x12E00, reset first), through a temporary player.
    void SendFreshObjects();

    // world_update.cpp
    // 0058DFF0, vtable slot 6. One world update: drops disconnected players and lets the others'
    // units wake what they hear, accepts new players, runs the players and the world tick (unless
    // +0x6C), rebuilds the local players' "Heroes" group, synchronises every player (SyncPlayer;
    // 0 drops it) and counts the update (+0x68). Returns 1.
    int Update();

    // world_spells.cpp
    // 0058E850 (RET 0x18). Casts `spell` from `caster` (or from `from`): one missile at `target` /
    // `to`, or, for a spell with several targets (+0x38) or area flags, at the nearest living units
    // within its radius (+0x34) the caster may hit (its heroes group, friends or diplomacy), the
    // requested target first. Network clients cast nothing with a caster.
    void CastSpell(const void* spell, CEngineObject* caster, const Vector3* from, CEngineObject* target,
                   const Vector3* to, int extra);
    // 0058E670 (RET 0x20). One missile of `spell` from the caster (or `from`) to the target (or
    // `to`, on the ground there); kept on the missile list unless its first update ends it. A
    // hostile spell turns the target unit against the caster.
    void CreateMissile(const void* spell, CEngineObject* caster, Vector3 from, CEngineObject* target, const Vector3* to,
                       int extra);

    // world_loot.cpp
    // 005942D0 (RET 8, AL). `player` takes what `object` holds: a shared loot record
    // (flags_ 0x20000000, removed afterwards) or what 00461CB0 collects (in a multiplayer client
    // game quest items go to player 0); the gold is added to the player. False for no player, a
    // living owned unit, or nothing to take.
    bool Loot(CEngineObject* object, WorldPlayer* player);
    // 00593D60. The local player (id 0) loots every dead unit, which is then removed (both
    // through the global server 007AFF60). Nothing without a local player.
    void LootDeadUnits();

    // world_weather.cpp
    // 0058CCD0. The weather, every 64 ticks: the day-time value (+0x84); in single player rain
    // starting / stopping (+0xE4 / +0xE8, 007C2DFC delay), lightning strikes in storms (+0xDC 3),
    // bolts in the sky while it rains; the day factor (+0x88); every object's slot 57; the
    // players' "gtime".
    void UpdateWeather();

    // world_quests.cpp
    // 00591800. For every player and the ten mercenary slots ("merc1".."merc10"), sets the
    // briefing variables b.mercN.n1/n2/n3/n8/n9/n10/n11_N (and clears amerc_N) from whether the
    // mercenary is in the party, was hired (amerc_N), and how many others are alive.
    void UpdateMercBriefings();
    // 00592090 (RET 8). Briefing `name` ("a.b.<record>") is complete for player `playerId`: a
    // network client asks the server; otherwise the player gets the record's experience, gold
    // and items, loses its quest items, its variables are set, and the scripts get
    // GSSetVar / #OnBriefingComplete (run at once in kind-3 games).
    void CompleteBriefing(int playerId, const char* name);
    // 00591DE0 (RET 8). The mercenary side of a completed briefing `name`: for the ten slots,
    // b.mercN.n8_N clears that variable, n10_N removes the mercenary from the party and the
    // world, n2_N from the party, n1_N / n3_N hire it (amerc_N = 2, added to the party with its
    // unit's prototype or "Human Hero"); then UpdateMercBriefings.
    void ApplyMercBriefing(WorldPlayer* player, const char* name);
    // 005911C0 (RET 8). Gives `amount` experience: with a player, shared equally by its living
    // heroes (units and party records); without one, shared by the players that have a single
    // hero, in proportion to sqrt(+0x8E0 + +0x1E4) of each hero's controller. Zero (or NaN) does
    // nothing.
    void AwardExperience(WorldPlayer* player, float amount);
    // 00590C70 (RET 4). Brings the player's party records (+0x70) up to date with its living
    // units: the player's own items, then every record's quick, worn and belt items and spells
    // rebuilt from the unit (each item's +0x1C record pointing at its entry) and the statistics.
    void SyncPartyRecords(WorldPlayer* player);
    // 0058FD90 (RET 0xC). Before a zone change: every record of `records` (CArray of record
    // pointers) notes its unit's leader, gets a placement (if `placements`), is rebuilt from its
    // unit through `partyRecords`, and the unit is removed (a dead one just loses its owner).
    void StoreParty(void* records, void* partyRecords, void* placements);
    // 00590A40 (RET 8). The player's party leaves the zone: gtime, its items, then (single player
    // and kind 3, with +0x15C) its records without a unit are dropped and the rest stored
    // (StoreParty).
    void CollectParty(WorldPlayer* player, void* placements);

    // world_party.cpp
    // 005901E0 (RET 8). Moves quest items (class 0x14) from `itemArray` (a CArray of item
    // pointers) to `list` (CPtrList): in single player only those whose description names this
    // zone or whose id an object of the zone asks for (object slots 38/40); otherwise all.
    void TakeZoneQuestItems(void* itemArray, void* list);
    // 00590460 (RET 4). Gives the player fresh copies of the items it brings into the zone (all
    // of them on a client, the zone's quest items otherwise; nothing in a network game).
    void RestorePlayerItems(WorldPlayer* player);
    // 00590700 (RET 0xC). The party arrives in the zone (single player and kind 3, with +0x15C):
    // its units are created at the entrance's centre (the target zone's exit), facing away from
    // the map centre or along the exit's heading, the player state gets the start; `runScripts`
    // runs the script command queue five times.
    void Redeploy(WorldPlayer* player, void* placements, char runScripts);
    // 00594CE0 (slot 24, RET 4). A new item like `item`: made by the item factory (slot 18) from
    // its type (+0x10) and record (+0x18), filled by the item's slot 15 (with slot 22), with its
    // count (+0x28); one below its maximum (+0x2C) goes on counted_ once.
    CEngineObject* CopyItem(CEngineObject* item);
    // 00594F30 (RET 4). Puts `item` (an item whose state changed) on counted_, once; null is
    // ignored.
    void AddCounted(CEngineObject* item);
    // 00594AB0 (RET 4). The item manager's (007AFFB4) new item named `name` (006AC970).
    CEngineObject* CreateItemNamed(const char* name);
    // 00594BC0 (RET 4). The item manager's new spell named `name` (006AD1E0, with slot 22).
    CEngineObject* CreateSpellNamed(const char* name);

    // world_units.cpp
    // 0058EF20 (RET 0x20). Creates a unit for each record of `records` (a CArray of 0x784-byte unit
    // records; an existing object with the record's id is removed first), at its entry in
    // `placements` (CArray of 0x28-byte {id, position, rotation, two values}) or in a line from
    // `position` along `rotation`, moved to a free cell, under `player`'s control, with its items,
    // spells and AI; heroes get their figure parts. Followers are given their leaders; with a
    // player the units are added to the "Heroes" script group.
    void CreateUnits(void* player, void* records, const Vector3* position, Float4 rotation, void* placements);
    // 0058ECE0 (RET 8). A quick item (potion) is used by `unit`: the effect object "st.tr.130"
    // (model "unhuma", type 9, flags 0x3010) is created at the unit's position on the ground and
    // flagged new (00529D20: flags_ |= 0x180000). `item` is not used.
    void UseQuickItem(void* item, CEngineObject* unit);

    // world_sync.cpp
    // 0058B750 (RET 4). Brings `player`'s view up to date (once per world update): the zone when
    // it changed (SendZone), weather, time, wind and countdowns when dirty, then the map objects
    // and timed objects it should see compared with what it was sent. Returns what the player
    // state's flush returns (0 drops the player), 1 for a player that is leaving.
    int SyncPlayer(WorldPlayer* player);
    // 0058C890 (RET 4). Sends `player` the zone to load (tick, map and .mob names, exits, the
    // zone's units) and, for a new game, where it starts. 0 when the player state refused it.
    int SendZone(WorldPlayer* player);

    int updateCount_;                     // +0x068 world updates run (Update)
    std::uint8_t unknown6C_;              // +0x6C
    std::uint8_t unknown_06D[0x70 - 0x6D];
    std::uint8_t countdowns_[0x84 - 0x70]; // +0x070 countdown list (006D44E0 ticks it; a CArray)
    std::uint32_t unknown84_;             // +0x084 the day-time value (0058CCD0)
    std::uint32_t unknown88_;             // +0x088
    WorldAlarm alarms_[5];                // +0x08C
    int unknownDC_;                       // +0x0DC
    int unknownE0_;                       // +0x0E0
    int unknownE4_;                       // +0x0E4
    float unknownE8_;                     // +0x0E8
    void* exitsVtable_;                   // +0x0EC CArray<ZoneExit> (ZoneExitArray) from here
    ZoneExit* exits_;                     // +0x0F0
    int exitCount_;                       // +0x0F4
    int exitCapacity_;                    // +0x0F8
    int exitGrowBy_;                      // +0x0FC
    std::uint8_t loaded_;                 // +0x100 0 while LoadSave runs
    std::uint8_t unknown_101[0x104 - 0x101];
    ObjectIdMap updateMap_;               // +0x104
    std::uint8_t unknown_11C[0x120 - 0x11C];
    char* zoneName_;                      // +0x120 CString: the zone's map database name
    StringArray mobFiles_;                // +0x124 .mob files loaded in this zone (LoadMobFile)
    int gameResult_;                      // +0x138 0 while the game runs
    std::uint32_t unknown13C_;            // +0x13C saved and loaded
    std::uint8_t unknown_140[0x15C - 0x140];
    int unknown15C_;                      // +0x15C cleared when a .mob file fails to load
    int unknown160_;                      // +0x160 non-zero: Tick runs the first command queue
    float startPosition_[3];              // +0x164 where joining players start (0058DAF0)
    std::uint8_t aiMap_[0x290 - 0x170];   // +0x170 CAIMap (ai_map.h; the global 007B00D0); extent unknown
    float gridHeightScale_;               // +0x290
    CSectorMap* Sectors() { return reinterpret_cast<CSectorMap*>(sectorMap_); }
    std::uint8_t sectorMap_[0x280C - 0x294]; // +0x294 CSectorMap (sector_map.h; size unknown)
    std::uint8_t** gridRows_;             // +0x280C rows of 9-byte cells, height (u16) at +4
    int gridWidth_;                       // +0x2810
    int gridHeight_;                      // +0x2814
    std::uint8_t unknown_2818[0x2838 - 0x2818];
    std::uint8_t timeWheel_[0x12874 - 0x2838]; // +0x2838 time wheel (005C9530; size unknown)
    std::uint8_t commandsA_[0x12A6C - 0x12874]; // +0x12874 script command queue (004BF000)
    std::uint8_t commandsB_[0x12C64 - 0x12A6C]; // +0x12A6C script command queue (004BF000)
    std::uint8_t diplomacy_[0x12D64 - 0x12C64]; // +0x12C64 CDiplomacy (object_diplomacy.h)
    int unknown12D64_;                    // +0x12D64 1 or 3: SaveWorld updates the players' "gtime" first
    char* mobName_;                       // +0x12D68 CString: the zone's .mob file
    char* mapName_;                       // +0x12D6C CString: the zone's map
    ObjectIdMap objects_;                 // +0x12D70 every world object by id
    std::uint8_t unknown_12D88[0x12DA0 - 0x12D88];
    ObjectIdMap mapObjects_;              // +0x12DA0 objects saved with the zone (SaveMapObjects)
    ObjectIdMap otherMapObjects_;         // +0x12DB8 the map objects that are not units
    ObjectIdMap namedObjects_;            // +0x12DD0 objects with a second name (+0x1A8)
    ObjectIdMap timedObjects_;            // +0x12DE8
    std::uint8_t unknown_12E00[0x12E28 - 0x12E00];
    ProjectileList projectiles_;          // +0x12E28
    mfc::CTypedPtrList<CEngineObject> counted_; // +0x12E44 counted references (released on clear)
    std::uint8_t unknown_12E60[0x12E64 - 0x12E60];
    void* sharedObjects_;                 // +0x12E64 head of an MFC list of {object id, player}
};
static_assert(offsetof(CWorldServer, countdowns_) == 0x70);
static_assert(offsetof(CWorldServer, unknown84_) == 0x84);
static_assert(offsetof(CWorldServer, loaded_) == 0x100);
static_assert(offsetof(CWorldServer, zoneName_) == 0x120);
static_assert(offsetof(CWorldServer, unknown13C_) == 0x13C);
static_assert(offsetof(CWorldServer, aiMap_) == 0x170);
static_assert(offsetof(CWorldServer, diplomacy_) == 0x12C64);
static_assert(offsetof(CWorldServer, unknown12D64_) == 0x12D64);
static_assert(offsetof(CWorldServer, mapName_) == 0x12D6C);
static_assert(offsetof(CWorldServer, otherMapObjects_) == 0x12DB8);
static_assert(offsetof(CWorldServer, namedObjects_) == 0x12DD0);
static_assert(offsetof(CWorldServer, alarms_) == 0x8C);
static_assert(offsetof(CWorldServer, unknownDC_) == 0xDC);
static_assert(offsetof(CWorldServer, exits_) == 0xF0);
static_assert(offsetof(CWorldServer, gameResult_) == 0x138);
static_assert(offsetof(CWorldServer, unknown160_) == 0x160);
static_assert(offsetof(CWorldServer, sectorMap_) == 0x294);
static_assert(offsetof(CWorldServer, timeWheel_) == 0x2838);
static_assert(offsetof(CWorldServer, commandsA_) == 0x12874);
static_assert(offsetof(CWorldServer, commandsB_) == 0x12A6C);
static_assert(offsetof(CWorldServer, projectiles_) == 0x12E28);
static_assert(offsetof(CWorldServer, updateMap_) == 0x104);
static_assert(offsetof(CWorldServer, gridHeightScale_) == 0x290);
static_assert(offsetof(CWorldServer, gridRows_) == 0x280C);
static_assert(offsetof(CWorldServer, objects_) == 0x12D70);
static_assert(offsetof(CWorldServer, timedObjects_) == 0x12DE8);
static_assert(offsetof(CWorldServer, sharedObjects_) == 0x12E64);
static_assert(offsetof(CWorldServer, exitsVtable_) == 0xEC);
static_assert(offsetof(CWorldServer, mobFiles_) == 0x124);
static_assert(offsetof(CWorldServer, unknown15C_) == 0x15C);
static_assert(offsetof(CWorldServer, mapObjects_) == 0x12DA0);
static_assert(offsetof(CWorldServer, counted_) == 0x12E44);

// One walkable platform (bridge, deck) the client tests positions against. 0x4C bytes, stored in
// an MFC-style singly linked list (next pointer first, then the record).
struct Platform {
    std::uint8_t unknown_00[4];
    float x, y;                           // +0x04 centre of the bounding circle
    std::uint8_t unknown_0C[4];
    float radius;                         // +0x10
    float origin[3];                      // +0x14
    float axisU[3];                       // +0x20 local u axis
    float axisV[3];                       // +0x2C local v axis
    float normal[3];                      // +0x38
    float sizeU, sizeV;                   // +0x44 extent along u and v
};
static_assert(sizeof(Platform) == 0x4C);

struct PlatformNode {
    PlatformNode* next;
    std::uint8_t unknown_04[4];
    Platform platform;                    // +0x08
};

// The client world's platform set (CWorldClient + 0x1D4).
struct PlatformSet {
    // 00560F00. max(height, z of every platform surface whose area contains the point); the
    // point is projected onto each platform plane along z.
    float SurfaceHeight(const Vector3* point, float height);
    // 00561050. Whether (x, y) lies on some platform (with a 0.1 margin).
    bool Contains(float x, float y);
    // 00560650 (RET 0x14; world_client_maps.cpp). Adds the face with corner `a`, edges to `b` and
    // `c` and `normal` when normal.z >= 0.8: origin a, unit axes (b - a) / |b - a| and (c - a) /
    // |c - a|, the two lengths, the normal; *out = the new record (its id, centre and radius are the
    // caller's), null when the face is too steep. Returns 1 when added.
    int AddFace(const float* a, const float* b, const float* c, const float* normal, Platform** out);
    // 00560820 (RET 0x10; called by CWorldClient::AddObject). The platform item `item` (+4
    // offset, +0x10 radius, +0x14 / +0x20 the box's corners, +0x2C pivot) of object `id` placed at
    // `position` with the rotation quaternion `rotation` (w, x, y, z): the box's corners and the six
    // face normals rotated, and every face that points up added with the id, the centre
    // R (pivot + offset) + position and the radius.
    void AddBox(int id, const std::uint8_t* item, const float* position, const float* rotation);

    std::uint8_t unknown_00[0x20];
    PlatformNode* head;                   // +0x20 the last node added (nodes link backwards)
};

// The unit set the server's SendZone fills (world_sync.cpp; 005593D0 adds a unit): an STLport
// hash_map whose nodes hold a second hash_map of unit entries (node + 8), a word at +0x24 and a
// third hash_map at +0x14. Only the writer is rewritten (world_unit_set.cpp).
struct UnitSet {
    // 00559E60 (RET 4). Writes the set to `stream` (its slot 2 writes bytes): the number of unit
    // entries; per entry its head (00558FA0), the words at +0x24 / +0x28 / +0x2C, its string count
    // and each string with its terminator; the word +0x24; per third-map entry a 16-bit key, two
    // counted dword arrays, a byte and a dword.
    void Write(void* stream);
};

// 0055A190 (ECX stream, EDX first list, RET 4; called by the client's zone load 00567AB0). Reads
// the zone's two unit look lists from `stream` (slot 1 reads bytes): a count, then per 0x3C-byte
// record a 16-bit key, two counted dword arrays (+4, +0x10), a byte (+0x1C), four dwords (+0x20 ..
// +0x2C) and a counted list of strings (+0x30); then a count and per 0x24-byte record of `second`
// the key, the two arrays, the byte and a dword (+0x20). Both vectors are resized to the counts.
void __fastcall ReadUnitLooks(void* stream, void* first, void* second);

// The client's sector grids (CWorldClient +0x120 for the objects, +0x150 the second one;
// world_client_maps.cpp): an STLport list of counted pointers per 16x16 cell.
struct SectorListNode {
    SectorListNode* next;
    SectorListNode* prev;
    CEngineObject* value;
};
struct SectorGrid {
    SectorListNode** cells_;              // +0x00 the lists (a head pointer each)
    SectorListNode*** rows_;              // +0x04 row y: its lists
    int unknown08_, unknown0C_;           // +0x08 (00572450 resizes when they change)
    std::uint32_t width_;                 // +0x10 cells along x
    std::uint32_t height_;                // +0x14 cells along y
    // 00570860 (RET 8; CWorldClient slot 41 on +0x120, also 0067BEF0 / 0067CEF0). Moves `object`
    // to `position` (stored at object +0x1C): when the cell (FISTP of x and y, >> 4) changes, out
    // of the old cell's list and to the end of the new one's (cells outside the grid are skipped).
    void Move(CEngineObject* object, const float* position);
    // 00570BB0 (RET 8; slot 42 on +0x150). The same for the second grid's list instantiation.
    void Move2(CEngineObject* object, const float* position);
};
static_assert(offsetof(SectorGrid, width_) == 0x10);

// 00571D50 (thiscall on the returned functor, RET 0x14). for_each over list nodes [first, last):
// sets the byte at node +0xD when hypot(node +0x10 - x, node +0x14 - y) <= node +0x94 * scale.
// Writes {x, y, scale} to out and returns it.
float* __fastcall MarkWithinRadius(float* out, void*, std::uint8_t* first, std::uint8_t* last, float x, float y,
                                   float scale);
// 00571800 / 005718B0 (thiscall on the returned functor, RET 0x18). for_each over list nodes
// [first, last) of 0x18-byte entries (+4 object id, +8 RECT): every entry whose rectangle holds
// (x, y) and whose object (found in `manager`) has a +0x94 that answers slot 22 (00571800) or
// slot 24 (005718B0) for the point is pushed to the vector `hits`. *out = hits; returns out.
void** __fastcall CollectWindowsAt(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, long x,
                                   long y, CBaseManager* manager);
void** __fastcall CollectWindowsAt2(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, long x,
                                    long y, CBaseManager* manager);
// 005726B0 (RET 4; the predicate of CWorldClient::ClearMaps' remove_if, ECX unused). An object
// with a positive id is released and true; false otherwise.
bool __fastcall ReleaseIfIdentified(void* functor, void*, CEngineObject* object);
// 0056FC60 (RET 8). Lookup in a hash_map<unsigned, T*>: *value = the entry's value and 1, or 0.
using PendingMap = stlp::hash_map<std::uint32_t, void*, stlp::IntHash>;
int __fastcall PendingMapLookup(const PendingMap* map, void*, const std::uint32_t* key, void** value);
// 0056E400 / 0056E440 (RET 4). Record `index` of a vector of 0x50- / 0xB4-byte records, or null.
// The party records handed to CWorldClient::CreatePartyUnits: an MFC pointer array.
struct PartyRecordArray {
    void* vtable;
    void** data;                          // +0x04
    int count;                            // +0x08
};
struct RecordVector {
    const std::uint8_t* start;
    const std::uint8_t* finish;
    const std::uint8_t* eos;
};
const std::uint8_t* __fastcall RecordAt50(const RecordVector* records, void*, std::uint32_t index);
const std::uint8_t* __fastcall RecordAtB4(const RecordVector* records, void*, std::uint32_t index);

// The drawing side (vtable 0073F41C).
class CWorldClient : public CWorld {
public:
    // 005641F0, vtable slot 6 (world_client.cpp). The client's tick ("Segment"): autosave event
    // and sound volumes on frame 10, the terrain, sound and session updates, the objects and units,
    // effect lists, the lights' grey levels, the map state every 32 ticks. Returns 1; 0 when the
    // session's view has lost its connection (the session is disconnected, FatalError; Rebirth's
    // DisableExitOnCrashReport keeps the session).
    int Segment();
    // 00569E40 (world_client_draw.cpp). Draws the world view: view matrix, sky, terrain passes
    // around DrawPass, effects and particles, flying hit points. Returns 1. Rebirth's
    // LandscapeDrawRadius sets the terrain's draw distance (100 in the game).
    int Render();
    // 0056AFE0 (world_client_draw.cpp). The object passes: collects the visible objects of the
    // sectors around the camera (ObjectsDrawRadius widens the 100.0 margin), sorts and draws them,
    // the light effects, shadows and selection markers, then empties the frame's lists. Returns 0
    // without terrain, else 1.
    int DrawPass();
    // 00563F70, vtable slot 27. Ground height with the position clamped into the map.
    float ClampedGroundHeight(float x, float y);
    // 005640A0. Nearest living unit of the local player's group that `unit`'s side can see
    // (relation masks +0x244/+0x248 of the candidate); null when none or `unit` is null.
    CEngineObject* NearestVisibleUnit(CEngineObject* unit);
    // 00564AF0. Item `index` of the concatenation of two pointer arrays, at +0x188 of each.
    void* ListItem(int index);
    // 005699F0. The local view (session +0x20; its +0x1C is the local WorldPlayer), or null.
    std::uint8_t* LocalView();
    // 00566EC0. Whether the local view is in mode 2 (+0x20).
    bool IsLocalViewMode2();
    // 00566EF0. FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // 00567440. Index of the exit whose area contains `point` and that leads somewhere.
    BOOL FindExitAt(const float* point, int* index);
    // 005674C0. Centre of exit `index`'s rectangle.
    float* ExitCentre(float* out, int index);   // returns out
    // 0056D9F0. Flags describing the current environment for `unit`: unit state (+0x268), island
    // (gipat/ingos/suslanger), night (0x100) or day (0x80), and two world settings.
    std::uint32_t EnvironmentFlags(CEngineObject* unit);

    // world_client_view.cpp
    // 0056A990 (RET 0x18). Walks one object list (a map sector's, the deferred unit list at
    // 007AF5B4 or +0xB4), tests each object with a model against the view frustum and files it in
    // the frame's draw lists (world_client_view.cpp). `view` is the camera matrix (rows of 4 floats),
    // `animTime` the game time handed to units' slot 25, `now` the timeGetTime() of the frame.
    // With `animateUnits` 0 units are only queued on the deferred list; otherwise they are animated
    // (slot 25) and filed like other objects. `withScripted` lets heroes with a text-key id
    // through. Returns how many objects were queued for the model-touch list (007AF508).
    int CollectVisible(void* list, const float* view, float animTime, std::uint32_t now, char animateUnits,
                       char withScripted);
    // 0056BF30 (RET 0xC). The ground point under screen pixel (x, y): inverts the current view
    // matrix and iterates on the terrain height (Terrain::HeightAt). Returns out (x, y, z).
    float* PickGround(float* out, int x, int y);
    // 0056C350 (RET 0xC). Same with the client's surface height (slot 29: platforms included).
    float* PickSurface(float* out, int x, int y);

    // world_client_maps.cpp
    // 005647E0 (RET 4). Files an object in the client's maps: an object whose id is already in the
    // list +0xB8 is only referenced and released. Otherwise: objects answering slot 29 are added to
    // the MFC list +0xBC and the id map +0xD8 (id -> list node); classes 0x54 / 0x5D add their
    // platform items (flag 0x400) to +0x1D4; units get slot 43 of their +0x240 in a multiplayer
    // session of kind 1 (+0x6EC >= 1, unit +0x24C clear); then the list +0xB8 (with a
    // reference), the sector grid +0x120 (removed first, then appended) and, for heroes with a
    // text-key id, the list +0xB4. Returns 1.
    int AddObject(CEngineObject* object);
    // 00564BE0 (RET 4). Takes the object `id` out of the maps: the MFC list +0x1F0 entries for it,
    // its +0xBC node and +0xD8 entry, then (when it is in +0xB8) the +0xB4 entries of a hero with
    // a text-key id (and 00538EA0 on its +0x240 in a session of kind 3), its grid cell and +0xB8;
    // the object is released.
    void RemoveObject(int id);
    // 00564E70. Empties the maps: both sector grids' cells, the MFC lists +0x1F0 and +0xBC, the id
    // map +0xD8, the lists +0x7BC and +0xB4, the +0xB8 entries with a positive id (released), the
    // session's player data, +0x34's slot 5 and the managers +0x3C/+0x40/+0x44's records.
    void ClearMaps();
    // 00565790 / 005659D0 (RET 4). The object at screen point `point` (x, y): the screen entries of
    // the list +0xF0 that contain it and whose object's +0x94 answers slot 22 (00565790) or 24
    // (005659D0) go to the vector +0xF4, which is sorted front to back (DepthLess with the view
    // matrix); the first entry whose object still exists wins (+0x100 is left on it). Null if none.
    CEngineObject* PickAt(const long* point);
    CEngineObject* PickAt2(const long* point);
    // 00566F70 (RET 8). Creates the party's heroes from their records (+0x14 prototype, +0x18
    // second prototype, +0x1C id, +8 model, +0x10 skin, +0x20 stats, +0x724 position, +0x730 /
    // +0x740 / +0x750 weapons, quick items and armour as pointer arrays with their counts after):
    // the figure from the part names (the hair "hr.%.2d" by the prototype's number), items, player
    // (+0); each unit is filed with AddObject. Armour of type 0 is put on only with `allArmour`.
    // Stops at the first unit that cannot be created or given a figure.
    void CreatePartyUnits(const PartyRecordArray* party, char allArmour);
    // 0056DAF0 (RET 0xC; the camera's 0040E4C0). Appends to the plain pointer list `out` every
    // object of the sector grid's cells around `point` whose (x, y) (+0x1C plus +0x128) lies within
    // radius + hypot(+0x144, +0x148) of the point. The cell range (+0x138..+0x144, truncated and
    // clamped to the terrain) and the walk's cursor (+0x148 / +0x14C) are left in the client.
    // The original's z difference is the point's z minus itself (0, kept).
    void ObjectsInRange(const float* point, float radius, void* out);
    // 00563290 (slot 3; also 00456440 and 00561B00). Tears the client world down: the camera's
    // target, the objects at +0x1B4 / +0x1AC / +0x1B0, 00563190, +0xA0, the lists +0x1B8, +0x1BC,
    // +0x1C0, +0x6F4 and the platforms (slot 3), +0x1F0, the sector grid's cells, the lists +0x7BC,
    // +0xB4 and +0xB8 (released), the session (+0x20C), the counted +0x1C8, 0055F8E0, then the
    // owned objects +0x1A0 .. +0x1D0 (counted ones dropped first) deleted and cleared.
    // (Not declared as the Destroy override: a virtual member's address is a vtable thunk, and
    // hooking the slot's own code with it recurses.)
    void Teardown();
    // 00561E50 (slot 2; also 00456440). Sets the client world up: the managers (0055EE40), the
    // texture path, the lights table (LightsGipat.ini, 00563930), the owned objects +0x1CC, +0x1D0,
    // the terrain (+0x180, its constructor inlined), +0x44, +0x18C, +0x188, +0x190, +0x19C, the
    // session (+0x20C), the counted +0x1C8, the text resources, +0x1A0, +0x1A4, +0x1A8, +0x6F4 and the
    // platforms, flag 0x200, the cursor (+0x7D0), a reference on every player, room for 400 screen
    // entries (+0xF4), and both sector grids at 4 x 4. Returns 1.
    int Setup();

    std::uint8_t unknown_068[0x90 - 0x68];
    ZoneExit* exits_;                     // +0x090
    int exitCount_;                       // +0x094
    std::uint8_t unknown_098[0xD0 - 0x98];
    std::uint8_t** listA_;                // +0x0D0
    int listACount_;                      // +0x0D4
    std::uint8_t unknown_0D8[0xE0 - 0xD8];
    std::uint8_t** listB_;                // +0x0E0
    std::uint8_t unknown_0E4[0x180 - 0xE4];
    Terrain* terrain_;                    // +0x180
    char* zoneName_;                      // +0x184 CString
    std::uint8_t unknown_188[0x1D4 - 0x188];
    PlatformSet platforms_;               // +0x1D4
    std::uint8_t unknown_1F8[0x20C - 0x1F8];
    std::uint8_t* session_;               // +0x20C local session; its +0x20 is the local view
    std::uint8_t unknown_210[0x6E0 - 0x210];
    int unknown6E0_;                      // +0x6E0
    std::uint8_t unknown_6E4[0x7A8 - 0x6E4];
    int unknown7A8_;                      // +0x7A8
};
static_assert(offsetof(CWorldClient, exits_) == 0x90);
static_assert(offsetof(CWorldClient, terrain_) == 0x180);
static_assert(offsetof(CWorldClient, platforms_) == 0x1D4);
static_assert(offsetof(CWorldClient, session_) == 0x20C);
static_assert(offsetof(CWorldClient, unknown7A8_) == 0x7A8);

// 00560E10. out = rows * v for the three rows of a 4x4 matrix (row stride 16 bytes). Returns out.
struct MatrixRows {
    float m[12];
    Vector3* Transform(Vector3* out, const Vector3* v) const;
};

// Sorting predicates passed to STLport sort (operator() of empty or small functors).
// 00573A60. Orders objects (by id at +4 of each entry) by distance along a view plane; dead and
// living objects are separated first.
struct DepthLess {
    float* plane;                         // +0 plane at +0x20 (normal) and +0x2C (offset)
    void* manager;                        // +4 CBaseManager holding the objects
    bool Less(const int* a, const int* b);
};
// 00574300. Orders units by group rank, then sequence number, then +0x30.
struct UnitOrderLess {
    bool Less(CEngineObject* a, CEngineObject* b);
};
// 00589580. Orders entries by the unsigned value at +0x1C of the object they point to (+8).
struct EntryIdLess {
    BOOL Less(const std::uint8_t* a, const std::uint8_t* b);
};

// Game interface glue (00578000-00580000): small free functions the scripts and menus call.
// 0057F440. Whether a multiplayer client session (global at 0079B5D8) is active.
BOOL IsMultiplayerClient();
// 00578B40. Game clock: whole ticks + fraction.
float GameTime();
// 00578BA0. The local player's data block at +0x70.
std::uint8_t* LocalPlayerData();
// 00578A90. The local player's name (copy constructed into `out`, returned).
mfc::CString* __stdcall LocalPlayerName(mfc::CString* out);
// 00578B50. Float variable `name` of the local player as the client sees it.
float __stdcall LocalPlayerVariable(const char* name);
// 00578BE0. Float variable `name` of the local player: the client's view in multiplayer, player 0
// of the server otherwise (0 without that player).
float __stdcall PlayerZeroVariable(const char* name);

// The game flow object (007AFE20). Only the functions rewritten here are declared.
struct GameFlow {
    // 0057B3B0 (RET 4). Saves the game into saves\<slot>\ (single player only, 007B0098 == 1):
    // empties the folder, copies the visited zones' files from saves\current, writes scenario.sav
    // (the party records, input and scripts state) and, outside the cutscene zones, mission.sav
    // (the world, SaveWorld). Without scenario.sav the game goes back to the main menu. Returns 1
    // when saved.
    int SaveGame(const char* slot);
    // 0057F190 (each network step). The local player's "z.MPGame1" variable, set to 1 or
    // removed: on a client, set unless the client session's +0x25EC is active; on the host, set
    // when more than one player is in (with +0x80) or unless the client's +0x25F4 is active.
    // Nothing without an active client session.
    void UpdateMultiplayerVariable();
    // 0057E6B0 (RET 4). The multiplayer start zone for the client session's game type (+0x261C):
    // "bz2mpg".."bz4mpg" for 1..3, else "bz1mpg"; constructed into `out`, returned.
    mfc::CString* MultiplayerZoneInto(mfc::CString* out);
    // 0057F280. Starts a multiplayer game: the party manager (007AA5A0) is reset, a single party
    // record gets the unit id 0x3B9AF5CE, the party outside a game (007C82B0) is sent to the start
    // zone with "z.MPGame1" = 1 and redeployed; the flow loads that zone (state 4) and the
    // client's game state request becomes kind 1.
    void BeginMultiplayerGame();
    // 0057FF40 (RET 4; world_briefing.cpp). The local player has seen `briefing`: for a name
    // ending in "_N" the zone's quests are loaded first (005809D0 with the name without "_N"),
    // then BriefingVariableInto's variable is set to 1.
    void MarkBriefing(const char* briefing);
    // 0057F0F0. With an active client session: the local player's party is copied into the party
    // outside a game (007C82B0), redeployed by the party manager, its map transfer sent
    // (00662AC0 with 007C82A0), then the flow's update (005800C0).
    void ReturnLocalParty();
    // 0057F150. With an active client session: the flow's update, then an answered game state
    // request (kind 3) becomes a new one of kind 0.
    void UpdateAndAcknowledge();
    // 0057FB60 (RET 4). A multiplayer client asks the server to complete `briefing` (session
    // command 1).
    void RequestBriefing(const char* briefing);
    // 00580040 (RET 4; world_briefing.cpp). Removes the local player's variable of `briefing`.
    void ForgetBriefing(const char* briefing);
    // 0057F390. Replaces the game client session (0079B5D8) with a new one (0041BC40), releasing
    // the old one.
    void RecreateClientSession();
    // 00581610 (RET 4). SaveGame through its address (a thunk the menus call).
    int SaveGameThunk(const char* slot);
    // 00581660. Releases both network sessions (server 0079B5DC, then client 0079B5D8), clears
    // 007C7DD8 and sets the state to 1.
    void CloseSessions();
    // 00580840 (RET 4; world_briefing.cpp). A player variable changed (event +8: the record,
    // name at +0x0C, value at +0x18): "q." variables go to the quest log (007C7D50) and the
    // client world.
    void OnVariableChanged(const void* event);
    // 005810A0 (RET 4; world_briefing.cpp). Registers a quest folder once: the flow loads its
    // quests (005809D0), then with "<folder>\quest.reg" in the texts the quest table reads it,
    // the map database loads "<folder>\map.txt" and the folder joins the list at 007AFDC0.
    void RegisterQuests(const char* folder);
    // 0057EE10 (world_game.cpp). The multiplayer zone switch, in two calls: the first (+0x80
    // clear) resets the host's connections, takes the party (player 0's records when its state
    // has +0x70, else the outside party) for the zone at +0x7C, asks for game state 3 and
    // requests state 6 (host) or 5 (client, to its peer's zone); the second (+0x80 set) asks for
    // state 1 and the start zone again, and the host drops its client.
    void SwitchMultiplayerZone();
    // 0057E740 (RET 0x18; world_game.cpp). Hosts a multiplayer game: new server (game type) and
    // client sessions, the client connects to 127.0.0.1 with the password (20 bytes) as hello,
    // then the server's name ("Server" when empty), +0x25E0, password, listing (not `unlisted`)
    // and +0x25FC are set; between 0057F560 and 0057EB10 (both patched by Rebirth).
    void StartServer(const char* name, int value25E0, int gameType, const char* password, char unlisted, int value25FC);
    // The flow's requests (world_game.cpp): the state (+0) the flow's update acts on next, the
    // zone or save name (+4), +0x78 and, for 5 and 6, a copy of a party (+8, 00661300).
    void RequestState3();                                            // 005816C0
    void RequestLoad(const char* name, char flag);                   // 005816D0 (RET 8): 2, +0x78 = flag != 0
    void RequestZoneChange(const char* zone, int entrance);          // 00581700 (RET 8): 4, +0x78 = entrance
    void RequestPartyZone5(const char* zone, const void* party);     // 00581730 (RET 8)
    void RequestPartyZone6(const char* zone, const void* party);     // 00581760 (RET 8)
    // 0057C460 (RET 8). Starts `zone`: the current zone is torn down; mode 0 takes the mode from
    // the zone record's kind (0 -> 1, 1 -> 4, 2 -> 3), "endofgame" is mode 8, mode 2 loads the main
    // menu zone (also when the zone is unknown, after FatalError and closing the network
    // sessions); otherwise the party enters the zone (0057A060) and the clock follows its "gtime".
    // The mode becomes the campaign mode (007C2CC4). Rebirth's AllowScriptsInMainMenu sets the
    // server's +0x160 in the menu.
    void StartZone(const char* zone, int mode);
    // 0057BD10. A new game: "exec autoexec.cfg" on the console, the party manager's new game
    // (004A88A0), saves\current emptied, then the party's zone started (mode 0). Rebirth's
    // AutoExec runs addon's AutoExec lines first.
    void StartNewGame();
    // 0057C700. Acts on the pending request (+0, then cleared): the zone being left is saved
    // (0057A780), the interface closes its windows, the zone is torn down and the resource
    // database follows the network state; then 1 main menu, 2 load the save named at +4 (two
    // ticks first; autoplay with +0x78), 3 new game (StartNewGame), 4 zone +4, 5 / 6 multiplayer
    // zone changes with the party copy at +8. Ends with the network pump (004122D0).
    void ProcessRequest();
    // 0057ABA0 (RET 4). Loads the game saved in saves\<slot>\ (not in multiplayer, 007B0098 2 or
    // 3): "exec autoexec.cfg", saves\current emptied, scenario.sav read back (the party; by
    // version the input, scripts and globals), the visited zones of the current map copied back
    // (the others marked not visited), then the zone: from mission.sav (kinds 0, 2) or started
    // afresh. Without scenario.sav the main menu starts. Returns 1 when loaded. Rebirth's AutoExec
    // runs addon's AutoExec lines first.
    int LoadGame(const char* slot);
    // 0057EB10. After the sessions start: the party step 0057DEE0(1), the variables of the party
    // outside a game (007C82B0 + 0x54) emptied, its members reset (00661FB0), and the client's host
    // record (session +0x2600) takes the hero's and party's names (+0x18, +0xC; defaults "Human
    // Hero", "Bug"), +0x28 = 1. Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void AfterServerStart();
    // 005800C0. A multiplayer client's update from the server: the quest texts unloaded, the
    // local player's variables replaced ("constr.current" kept), the zone's quests, the game type's
    // construction variable and briefings (at least 1), the seen briefings, the quest folders of
    // "q." variables, then the server's variables applied in stamp order (007AFED0 / 007AFED4),
    // and the multiplayer flag variable. Rebirth's PersistentGlobalVars keeps the "gvar." ones.
    void UpdateFromServer();
};

// The saved multiplayer parties: +0 the folder (CString), +4 an STLport vector of 0x74-byte entries
// (the party records and the file name).
struct SavedPartyList {
    // 00662FB0 (world_game.cpp). Rebuilds the list from the folder's "*.mp" files: each is read
    // into party records (00662DD0); a loaded one has its variables emptied and, with records +0x34
    // == 1, is appended. Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void Reload();
};

// 00581630 (RET 8; world_game.cpp). Sets 007C7D94 and has the local view ask to go to `zone`
// (0066B1B0); returns what that returns.
int __stdcall RequestZone(const char* zone, int entrance);

// 00580F10 (world_briefing.cpp). With the briefing texts loaded (007AF788): every quest text file
// loaded so far (007AFDD0) is unloaded and the list emptied.
void UnloadQuestTexts();

// 0057F820 (ECX = list, EDX = name; world_game.cpp). Whether `name` is in `list` (an array of
// names ended by an empty one), compared case-insensitively.
bool __fastcall NameInList(const char* const* list, const char* name);

// 0057AAB0 (ECX = path ending in '\'; world_game.cpp). Creates the folder and deletes every file
// in it (CFileFind "*.*"; CreateDirectoryA / DeleteFileA through game.exe's imports).
void __fastcall ClearSaveFolder(const char* path);

// 0057F850 (RET 0xC; world_game.cpp). For the zone table entries (007AFDE0) with `id`, grouped by
// their second name in order of first appearance: one random entry per group (never the one
// whose first name is `exclude` when there is a choice; one IsaacRandom draw per group) has its
// first name appended to `out` (an STLport vector of strings).
void __stdcall PickZones(void* out, int id, const char* exclude);

// 00579E60 (RET 4; world_game.cpp). Reads mission.sav from a file stream (magic 0x102) and
// restores the world (LoadSave); old versions also carry the globals (< 0x52) and the input
// state (< 0x47). 0 when the file does not read.
int __stdcall LoadMission(void* file);

// world_savefile.cpp. 00579B80 (ECX = file stream, EDX = magic; RET 8): writes the header
// (004A5340), at version > 0x6B a checksum, then a flag byte and the LZ-compressed data (or, when
// decompressing it does not give the data back, 0 and the raw data). 00579830 (ECX = memory
// stream, EDX = magic; RET 0xC; AL): reads one back (versions up to 00790264 uncompressed);
// false on a bad header or checksum.
void __fastcall SaveFileWrite(void* file, unsigned magic, int version, void* data);
bool __fastcall SaveFileRead(void* out, unsigned magic, int minimum, int maximum, void* file);

// world_briefing.cpp. 0057FC90 (RET 8): the mercenary who speaks `briefing` (the second word of
// the first "#show" line of section "briefing <name>" that is not "Hero"), or "", constructed
// into `out`. 0057FDE0 (RET 8): the briefing's variable, "b.<speaker>.<briefing>" or, without a
// speaker, "b.elder.<briefing>".
mfc::CString* __stdcall BriefingSpeakerInto(mfc::CString* out, const char* briefing);
mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing);
// 005814C0 (RET 4). The multiplayer peer's map (the client's +0x25F4 or the server's +0x25A8,
// name at +0x0C), replaced by the first line of its "quest <name>" section when there is one;
// "" without a peer.
mfc::CString* __stdcall SessionMapNameInto(mfc::CString* out);
// 00595080. Chooses the loading movie (the name at 00791EE0) for zone `zone` (may be null) by its
// allod in the map database: progres2.bik for gipat, progres3.bik for ingos, progres4.bik for
// suslanger, else progres.bik (Rebirth's FixProgressNames: progres2.bik for gipat2, progres5.bik for
// jigran); the zones gz1g and bz7g (case-insensitive) take progres1.bik.
void __stdcall SelectLoadingMovie(const char* zone);

CWorldServer* WorldServer();   // 007AFF60
// 0079BE5C: the time speed setting (the options' value; a negative speed asks for it).
inline int TimeSpeedSetting() { return *reinterpret_cast<const int*>(0x0079BE5C); }
CWorldClient* WorldClient();   // 007AF5B8

// 00589CA0 (thiscall on a shared-object entry, RET 4; world_save.cpp). Reads an entry of the
// shared-object list: id, player, value, then object ids resolved by the item manager (007AFFB4);
// each found object is appended with a reference.
void __fastcall LoadSharedObject(void* entry, void*, WorldStream* stream);

// 00578E30 (thiscall on the client glue 007AFEA4, unused; RET 0x18; world_weather.cpp). A
// lightning strike from `from` to `to`, as a script line on the queue at 007C29CC: one time in
// four only a thunder sound, otherwise a point light at the middle, a sound and two bolts.
void __fastcall ShowBolt(void* glue, void*, Vector3 from, Vector3 to);

// world_guard.cpp. 005790E0 (thiscall on the client glue 007AFEA4, unused; RET 4): with `plain`
// 0 the players' gold, party records (0x704 bytes from +0x20) and, on the server, their units'
// controller statistics (0x704 bytes from +0x1E0) are XORed with a new random key (007AFECC),
// with 1 they are XORed back; also the party records outside a game (007C82B0). Nothing when
// the state (0079026C) is already `plain`. 005791C0 (RET 4) does one player list.
void __fastcall GuardPlayerData(void* glue, void*, char plain);
void __fastcall GuardPlayers(CEngineObject* players, char units, std::uint32_t key);

// ---- Client draw lists and their sorting (world_client_view.cpp) --------------------------------
// 0056AFE0 (the client's draw pass, patched by Rebirth) fills per-frame vectors of object pointers
// with CWorldClient::CollectVisible and sorts some of them with STLport's sort, instantiated for
// two orderings. The entry points keep STLport's internal calling convention: first in ECX, last
// in EDX, then a dummy value-type pointer, the depth limit and the (empty) comparator on the stack.

// 005740D0 (RET 0xC). STLport __introsort_loop with UnitOrderLess (model rank, +0xF0, view depth).
void __fastcall IntroSortByModel(CEngineObject** first, CEngineObject** last, void* valueType, int depthLimit,
                                 int comp);
// 00574390 (RET 4). STLport __final_insertion_sort with UnitOrderLess.
void __fastcall FinalSortByModel(CEngineObject** first, CEngineObject** last, int comp);
// 00573D50 (RET 0xC). __introsort_loop ordering objects by their model's last use (+0x94 -> +0xEC),
// most recent first.
void __fastcall IntroSortByModelUse(CEngineObject** first, CEngineObject** last, void* valueType, int depthLimit,
                                    int comp);
// 00573F10 (RET 4). __final_insertion_sort with the same ordering.
void __fastcall FinalSortByModelUse(CEngineObject** first, CEngineObject** last, int comp);
// 00573EE0. The comparator of that ordering: b's model used before a's (0/1 in EAX).
struct ModelUseLess {
    BOOL Less(CEngineObject* a, CEngineObject* b);
};
