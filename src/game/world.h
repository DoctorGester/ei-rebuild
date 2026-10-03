#pragma once

// The world: the server side that runs the game rules (CWorldServer, the global at 007AFF60) and
// the client side that draws it and answers terrain queries (CWorldClient, the global at 007AF5B8).
// Both derive from a common base whose methods live at 0055E000-0055FFFF. Class names come from
// the game's error strings ("CWorldServer::LoadMap", "CWorldClient::ToggleWireframe"); everything
// else is provisional. Only the members used by the rewritten functions are declared.

#include <cstddef>
#include <cstdint>

#include "engine_object.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "maths.h"

using BOOL = int;

struct ScriptGroup;
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
    float timeBase_;                      // +0x1C
    float secondsPerTick_;                // +0x20
    std::uint8_t unknown24_[12];          // +0x24 saved and loaded as they are
    std::uint32_t unknown30_;             // +0x30 saved and loaded
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
// mission.sav): slot 1 Read, slot 2 Write; +0x08 the save format version.
struct WorldStream {
    virtual void Slot0() = 0;
    virtual int Read(void* buffer, unsigned size) = 0;          // 1
    virtual void Write(const void* buffer, unsigned size) = 0;   // 2

    int unknown_04;
    int version;            // +0x08
};

// The game rules side (vtable 0073F928).
class CWorldServer : public CWorld {
public:
    // 00593AA0, vtable slot 40. World object with this id, or null.
    CEngineObject* FindObject(unsigned id);
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

    // world_units.cpp
    // 0058EF20 (RET 0x20). Creates a unit for each record of `records` (a CArray of 0x784-byte unit
    // records; an existing object with the record's id is removed first), at its entry in
    // `placements` (CArray of 0x28-byte {id, position, rotation, two values}) or in a line from
    // `position` along `rotation`, moved to a free cell, under `player`'s control, with its items,
    // spells and AI; heroes get their figure parts. Followers are given their leaders; with a
    // player the units are added to the "Heroes" script group.
    void CreateUnits(void* player, void* records, const Vector3* position, Float4 rotation, void* placements);

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
    std::uint8_t unknown_164[0x170 - 0x164];
    std::uint8_t aiMap_[0x290 - 0x170];   // +0x170 CAIMap (ai_map.h; the global 007B00D0); extent unknown
    float gridHeightScale_;               // +0x290
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

    std::uint8_t unknown_00[0x20];
    PlatformNode* head;                   // +0x20
};

// The drawing side (vtable 0073F41C).
class CWorldClient : public CWorld {
public:
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
};

CWorldServer* WorldServer();   // 007AFF60
CWorldClient* WorldClient();   // 007AF5B8

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
