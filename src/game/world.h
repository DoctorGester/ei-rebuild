#pragma once

// The world: the server side that runs the game rules (CWorldServer) and the client side that draws it and answers
// terrain queries (CWorldClient), both derived from a common base. Class names come from the game's error strings.

#include <cstddef>
#include "options.h"
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai_map.h"
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "effects.h"        // CRainSnow
#include "engine_object.h"
#include "manager_context.h"
#include "ground_print.h"
#include "object_diplomacy.h"
#include "object_tables.h"
#include "player_manager.h"
#include "player_messages.h"
#include "player_source.h"
#include "registry.h"
#include "script.h"
#include "unit_ai.h"       // AIEventTable (the server world's AI events)
#include "unit_create.h"
#include "unit_looks.h"
#include "water_tweens.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/hash_map.h"
#include "maths.h"
#include "model.h"          // HeldTexture
#include "party_storage.h"
#include "game_data.h"
#include "game_singletons.h"

#undef GetClassName

inline constexpr Quaternion kNewUnitRotation = /*007C2DE0*/ {1.0f, 0.0f, 0.0f, 0.0f};

using BOOL = int;

class CParticleSource;
class CPlayerClient;
namespace netlink {
class NetLink;
class DriverSession;
}
namespace net {
struct NetAddress;
}
class CWorldHandle;
class CClientHandle;
struct CSoundManager;
class CSpellManager;
class CLinkFigureLoader;
class CTexture;
class CTextureManager;
class CMaterialManager;
class Terrain;
class CCamera;
class CMaterial;
struct LineBatch;
class CSpell;
class CSpellEffect;
class CParticleManager;
class CHoover;
class CEffectMoshka;
namespace mapspec { class Spec; }

class CWorldServer;

namespace world {
constexpr int kClassUnit = 0x50;   // CEngineObject::GetClassId() of units
}

// A hash map of counted objects emptied as ObjectIdMap::ReleaseAll does: every object up to the first empty entry
// released, the nodes freed (the buckets stay). The caller clears its highest id.
template <typename Map>
void ReleaseEach(Map& map) {
    for (auto& [id, object] : map) {
        if (!object) break;
        object->Release();
    }
    map.clear();
}

// The world's object maps (walked in STLport's order) plus the highest key ever inserted.
struct ObjectIdMap {
    lib::HashMap<unsigned, CEngineObject*> map;
    int maxId = 0;

    void Set(const unsigned& key, CEngineObject* const& value);
};

struct MapObjectIdMap {
    lib::HashMap<unsigned, CMapObject*> map;
    int maxId = 0;

    void Set(const unsigned& key, CMapObject* const& value) {
        if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
        map[key] = value;
    }
};

// Walks the world's player list; the list object is reference counted while an iterator holds it.
struct PlayerIterator : ObjectListIteratorOf<CPlayer> {
    PlayerIterator() : ObjectListIteratorOf<CPlayer>{{nullptr, {}}} {}

    // Drops the list's count without releasing it.
    ~PlayerIterator() {
        if (manager) --manager->refCount_;
    }
};

class CFlyingHitPoints;

// A light colour of the client's tables (a is 1).
struct LightColour {
    float r, g, b, a;
};
static_assert(sizeof(LightColour) == 0x10);

class CWorld : public CManagerContext {
public:
    CWorld();
    ~CWorld() override;

    // Folders, managers, the ISAAC generator and the countdowns set up. Returns 1.
    int Create() override;
    // Every object of the spell manager gets ClearState, then the managers are deleted whatever their counts.
    void Destroy() override;
    int RunTick() override;

    CWorld* ThisWorld() override { return this; }
    CBaseManager* TimedObjectManager() override { return manager48_; }
    CBaseManager* FigureManager() override;   // manager3C_ (world_class_slots.cpp: CFigureManager is complete there)
    CItemConstructor* ItemConstructor() override { return manager58_; }
    CSpellConstructor* SpellConstructor() override { return manager50_; }
    CItemManager* ItemFactory() override { return itemManager_; }
    CMapObjectManager* MapObjectManager() override { return manager40_; }
    CPlayerManager* PlayerList() override { return players_; }
    CBaseManager* SpecificManager() override { return manager44_; }
    CSpellManager* SpellManager() override { return manager4C_; }
    CLinkFigureLoader* FigureLoader() override { return figureLoader_; }

    // The world's own virtuals; docs/notes/map_objects.md, "Virtual slot names".
    virtual const char* TexturePath() { return nullptr; }   // the client's "textures\"
    virtual int IsServer() { return 0; }                    // the server's 1
    virtual double GridHeightAt(float x, float y) = 0;
    // The server's AI map height.
    virtual double RawSurfaceHeightAt(float x, float y) = 0;
    // The ground height and the water surface's height at (x, y): the server's call GridHeightAt / RawSurfaceHeightAt, the
    // client's also clamp.
    virtual double GroundHeightAt(float x, float y) = 0;
    virtual double SurfaceHeightAt(float x, float y) = 0;
    // The time of day becomes `hours` (the base is moved so that fmod(tick * secondsPerTick + base, 24)
    // = hours). The server's also updates the players' times first (game kinds 1 and 3), marks them
    // dirty (4) and updates the weather.
    virtual void SetTime(float hours);
    // Seconds per tick become 1 / speed (0: stopped; negative: the setting) times 1/15, keeping the time
    // of day (SetTime). The server's updates the players and weather as SetTime does.
    virtual void SetTimeSpeed(float speed);
    // The client's managers; null in the base.
    virtual CMaterialManager* MaterialManager() { return nullptr; }
    virtual CTextureManager* TextureManager() { return nullptr; }
    virtual CBaseManager* ClientLightManager() { return nullptr; }
    virtual CParticleManager* ClientParticleManager() { return nullptr; }
    virtual CEngineObject* ClientManager194() { return nullptr; }   // not identified
    virtual CEngineObject* ClientManager198() { return nullptr; }   // not identified
    virtual int Unused39() { return 0; }
    virtual CMapObject* ObjectWithId(int id);
    // `object` moves to `position` (the client's sector map).
    virtual void MoveObject(CMapObject*, const float*) {}

    // Whether the hour of the day is before 6 or from 22 on (a NaN counts as night).
    BOOL IsNight();
    // The time of day in hours and the time speed (1 / (seconds per tick * 15)), both stored as floats.
    void GetTime(float* hours, float* speed);
    void SetWind(const Vector3* direction, float strength);
    CUnit* FindUnit(int id);
    // How dark the time of day is: 1 before 8 o'clock (and for a NaN), 0 until 20, then rising to 1 by midnight.
    double NightLevel();
    // A new empty network session shared into the world's session pointer, the previous one destroyed.
    void OpenSession();
    void CloseSession();
    // Stores paused_. SetServerPaused is the server's entry: EI_Plugin's Pause in mode 3 makes it a plain return.
    void SetPaused(std::uint8_t value);
    void SetServerPaused(std::uint8_t value);

    float timeBase_;
    float secondsPerTick_;
    std::uint8_t windDirection_[12];      // three floats; saved and loaded as they are
    std::uint32_t windStrength_;          // float bits, 0..1; saved and loaded
    CLinkFigureLoader* figureLoader_;     // counted (the client's from SetUpGraphics; FigureLoader returns it)
    CPlayerManager* players_;
    CFigureManager* manager3C_;          // FigureManager
    CMapObjectManager* manager40_;       // map objects by id (its NewObject creates one)
    CBaseManager* manager44_;            // SpecificManager
    CBaseManager* manager48_;            // the timed objects (spells, effects; TimedObjectManager)
    CSpellManager* manager4C_;           // the spells (SpellManager)
    CSpellConstructor* manager50_;       // SpellConstructor
    CItemManager* itemManager_;          // items (ItemFactory)
    CItemConstructor* manager58_;        // ItemConstructor
    // A counted helper CWorld::Create shares in.
    RefPtrTo<CWorldHandle> shared5C_;
    RefPtrTo<netlink::DriverSession> netSession_;   // the network session (OpenSession)
    int tick_;                            // game time in ticks
    int updateCount_;                     // world updates run (the server's Update)
    // Paused: the server's Update skips the players and the tick while it is set; the client's Segment
    // skips its own ticks, and the client copies the server's flag from message 0x60, telling the HUD.
    std::uint8_t paused_;
    std::uint8_t unused_06D[0x70 - 0x6D];
    WaterTweens countdowns_;
};
static_assert(offsetof(CWorld, shared5C_) == 0x5C);
static_assert(offsetof(CWorld, updateCount_) == 0x68);
static_assert(offsetof(CWorld, paused_) == 0x6C);
static_assert(offsetof(CWorld, windDirection_) == 0x24);
static_assert(offsetof(CWorld, manager40_) == 0x40);
static_assert(offsetof(CWorld, itemManager_) == 0x54);
static_assert(offsetof(CWorld, players_) == 0x38);
static_assert(offsetof(CWorld, netSession_) == 0x60);
static_assert(offsetof(CWorld, tick_) == 0x64);

// One of the five alarms InvokeAlarm raises (the script's IsAlarm / AlarmTime / AlarmPosX / AlarmPosY).
struct WorldAlarm {
    std::uint8_t active;
    std::uint8_t unused_01[3];
    int time;                             // tick it was raised
    float x, y;
};
static_assert(sizeof(WorldAlarm) == 16);

// The world the missiles find while they load.
inline CWorldServer* g_loadingProjectileWorld = /*007C84EC*/ {nullptr};
// The world server's missiles (the spells being applied), counted.
using ProjectileList = std::list<CSpellEffect*>;

using WorldStream = CStream;

// An entry of the world server's shared-object list: the loot a unit left.
struct SharedObject {
    int objectId = 0;
    int player = 0;
    std::uint32_t money = 0;                // gold the loot adds to the finder's party
    std::vector<HeldPtr<CItem>> objects;    // counted

    // Reads the entry: id, player, value, then object ids resolved by the item manager; each found
    // object is appended with a reference.
    void Load(WorldStream* stream);
};
using SharedObjectList = std::list<SharedObject>;

using RecordPtrArray = std::vector<PartyUnitRecord*>;

class CWorldServer : public CWorld {
public:
    CWorldServer();
    ~CWorldServer() override;

    // The managers, the messages registry, the CHoover, the wind (1, 0, 0; strength 0.4) and the zone state
    // set up. Returns 1, or FatalError's result when the managers or the registry fail.
    int Create() override;
    void Destroy() override;
    // One world update: drops disconnected players and lets the others' units wake what they hear, accepts
    // new players, runs the players and the world tick (unless paused_), rebuilds the local players' "Heroes"
    // group, synchronises every player (SyncPlayer; 0 drops it). Returns 1.
    int Update();
    int RunTick() override { return Update(); }
    const char* GetClassName() override;
    int GetClassId() override;
    CItem* DuplicateItem(CItem* item) override { return CopyItem(item); }
    int IsServer() override;
    double GridHeightAt(float x, float y) override { return GridHeight(x, y); }
    // RawSurfaceHeightAt: the AI map's top height at (x, y, 0); the other two return GridHeightAt /
    // RawSurfaceHeightAt unclamped.
    double RawSurfaceHeightAt(float x, float y) override;
    double GroundHeightAt(float x, float y) override;
    double SurfaceHeightAt(float x, float y) override;
    void SetTime(float hours) override;
    void SetTimeSpeed(float speed) override;
    CMapObject* ObjectWithId(int id) override { return FindObject(static_cast<unsigned>(id)); }

    CMapObject* FindObject(unsigned id);
    // Creates the object a .mob record describes (base.kind): map objects and units (1, 5, 8, 9; 6 with a
    // figure and maybe a campfire; 7) go into the object maps, the update map and the sector map; sounds (3),
    // lights (2) and kind 4 become timed objects. Returns the new id, -1 when refused, the message's address
    // for an unknown kind. Rebirth's FixAddMapObject takes index 0 for a unit without a prototype record.
    int CreateObject(mapspec::Spec* message);
    // Removes object `id` from the server: out of the sector map, the id maps and the command queues,
    // forgotten by every object, the missiles and the players' states, the maps' reference released. An
    // unknown id crashes the original (null read); Rebirth's FixRemoveMapObject ignores it.
    void RemoveObject(unsigned id);
    CPlayer* GetPlayer(int id);
    // Ground height from the server's coarse height grid; 0 outside.
    float GridHeight(float x, float y);
    // Living mercenaries of player `id`, not counting the leader (0 without the player).
    int CountLivingMercs(int id);
    // Whether object `objectId` is on the shared-object list for player `playerId` (or for everyone).
    bool IsObjectShared(int objectId, int playerId);
    void CollectSharedObjects(int playerId, ScriptGroup* group);
    void MarkPlayersDirty(std::uint32_t mask);
    // SetWind with the strength clamped to 0..1 (NaN: 0), then the players marked dirty (8).
    void ChangeWind(const Vector3* direction, float strength);
    // Setters of cave_, climate_ (any weather stops first) and weather_ / weatherStrength_ (the players
    // marked dirty).
    void SetCave(int value);
    void SetClimate(int value);
    void SetWeather(int weather, float strength);
    // Target zone and entrance of exit `index`; FALSE when out of range.
    BOOL GetZoneExit(int index, std::string* target, int* entrance);
    // Registers an object that lives for a limited time (spells, effects) in timedObjects_.
    void AddTimedObject(CEffect* object);
    void RemoveTimedObject(unsigned id);
    void AddToUpdateMap(CMapObject* unit);
    // The updates an object stays in the update map before it may sleep (updateCountdown): 0x80 plus
    // 0..15, which the game takes from bits 5..8 of the object's address (in several functions), so it
    // follows the heap's layout. Ours takes the low 4 bits of the object's id instead, and harness runs
    // on the virtual clock patch the original to do the same (tests/verify/harness_heap.h), so that
    // every run and both sides agree.
    static int UpdateCountdownFor(const CEngineObject* object);

    // One world tick: runs every timed object, advances the time wheel and countdowns, moves the missiles, runs the
    // script command queues and then the units.
    void Tick(char force);
    // Runs the objects: in the update-map mode the units in the update map, each counting down to sleep, else every
    // map object; then the counted objects. Rebirth's FixSpellEffectsOverflow first ticks the effects of sleeping
    // objects (TickSleepingEffects).
    void UpdateUnits();
    // Rebirth's FixSpellEffectsOverflow #0: counts the effects of sleeping map objects down and wakes
    // those with an expired one.
    void TickSleepingEffects();
    // Puts every unit in the sectors within `radius` of `position` (x, y) in the update map.
    void WakeUnitsAround(const Vector3* position, float radius);
    // Raises alarm `index` (0..4) at `point` unless it is already raised (`force` raises it again; alarm 0 with
    // `force` clears all five first), tells every AI unit without a player and wakes those that react. Script
    // InvokeAlarm.
    void InvokeAlarm(int index, const float* point, char force);
    // Records how the game ended (only the first time); results 2 and 3 also replace the network session
    // and query it (Query).
    void SetGameResult(int result);
    // Sends the unit's message 0x0B with `value` to every player with `unit` among its party units.
    void TellParties(CEngineObject* unit, std::uint32_t value);
    void TellPartiesGone(CEngineObject* unit);
    // The items the player holds put into `party` and released. Nothing without a player.
    void StorePlayerItems(CPlayer* player, class CParty* party);
    void ClearPlayerItems(CPlayer* player);
    void SetWaterLevel(int key, float target, int steps);

    // Replaces the zone exits with copies of `source`'s records that lead somewhere (target not "");
    // an exit without an entrance must lead to "none".
    void SetZoneExits(const ZoneExits* source);
    // Script AddUnitToServer: creates a unit of prototype `prototype` named `name`, with id `id` (the name's text
    // key), at `position`.
    void AddUnitToServer(unsigned id, const char* prototype, const char* name, const Vector3* position);
    // Reads the objects of a .mob file (chunked). Returns 1, or what FatalError returns when the file
    // cannot be opened or read.
    int LoadMapObjects(const char* file, int keepIds);
    // Loads .mob file `file` once (the loaded names are kept).
    int LoadMobFile(const char* file);
    // Removes every map object from the server, releases the missiles and the counted objects and clears
    // the countdowns.
    void ClearMapObjects();
    // Writes every map object to a save stream (id, prototype name, the object's Save, two flag words); -1 ends the
    // list.
    void SaveMapObjects(CStream* stream);

    // Restores the world from a mission save, in the order SaveWorld writes it; units that cannot stand where they
    // were saved are moved to a free cell or removed. `map` replaces the saved map name when not null. Returns 1, 0
    // when the AI graph cannot be read, or what FatalError returns when the zone cannot be loaded.
    int LoadSave(WorldStream* stream, const char* map);
    // Writes what LoadSave reads.
    void SaveWorld(WorldStream* stream);
    // Reads the map objects SaveMapObjects wrote, registers them in the id maps and wakes them.
    void LoadSavedMapObjects(WorldStream* stream);
    // Adds the time each player missed since its last update (plus `offset`) to its "gtime" variable.
    void UpdatePlayerTimes(float offset);
    // Puts a new entry at the head of the shared-object list: `id`, the session player's field for that id in a
    // multiplayer game (else -1), `value`, and references to the objects of `objects`. `unused` is ignored.
    void AddSharedObject(int id, int unused, std::uint32_t value, const std::list<CItem*>* objects);
    // Before a save: every living unit refills its controller's slots and runs the controller's matching
    // virtual with 0.
    void PrepareSave();
    // Reads the zone's AI graph into the memory stream `out`: the 0x31415926 chunk of the .mob file when
    // there is one, else the whole <map>.grp file. 1 when found.
    int ReadAIGraph(const char* map, const char* mob, CStream* out);
    // The zone's scripts from the .mob file's 0xA000 chunk (its 0xACCEECCB text packed and XORed,
    // decoded by the game's routine, or the plain 0xACCEECCA text) become the only line of the first
    // command queue.
    void LoadZoneScripts(const char* mob);
    // While a game runs, every new connection the lobby hands over becomes a player of this game's kind, fully dirty,
    // starting at startPosition_ ("Accepted player <id>" goes to the network log).
    void AcceptNewPlayers(CPlayerSource* lobby);

    std::string* ZoneNameInto(std::string* out);

    // Loads map `map` (when it changed), the AI layer state from `graph` and the zone's .mob objects (the
    // same .mob starts the zone over, another one empties the world first; from a save the loaded objects are
    // cleared again). Returns 1, 0 when the map or the objects cannot be loaded.
    int LoadMap(const char* map, const char* mob, CStream* graph, char fromSave);
    // Empties the world of the current zone.
    void ResetZone();
    // ResetZone, then the map name, diplomacy, both command queues and the AI map are reset and the game state
    // cleared.
    void ResetState();
    // Before every game state request: every player leaves and the zone's contents go, then the base class's
    // teardown; the game result and kind are cleared.
    void TeardownZone();
    // A new zone from `map` and `mob`: the loading movie for `zone` shown, the AI graph read (a fatal error
    // without it), the map loaded; the zone name kept when given, scriptsActive_ cleared. 0 when a file is
    // neither where its name says nor in the maps folder, else 1.
    int NewZone(const char* map, const char* mob, const char* zone);
    // Unless the zone's map and .mob are these already: NewZone(map, mob, null), then TeardownZone.
    void EnsureZone(const char* map, const char* mob);
    // `unit` used one of `item` (a scroll): the item's count drops, and the count of the party's item it was made
    // from when the unit's player has a record for the unit.
    void UseItemUp(class CUnit* unit, class CItem* item);
    // After a fresh zone is loaded: the map objects that are not part of the map (IsSynchronised) move to
    // sentObjects_ and are sent, with zoneHeader_, through a temporary player.
    void SendFreshObjects();

    // Casts `spell` from `caster` (or from `from`): one missile at `target` / `to`, or, for a spell with several
    // targets or area flags, at the nearest living units within its radius the caster may hit, the requested target
    // first. Network clients cast nothing with a caster.
    void CastSpell(CSpell* spell, CUnit* caster, const Vector3* from, CMapObject* target,
                   const Vector3* to, int extra);
    // One missile of `spell` from the caster (or `from`) to the target (or `to`, on the ground there);
    // kept on the missile list unless its first update ends it. A hostile spell turns the target unit
    // against the caster.
    void CreateMissile(CSpell* spell, CUnit* caster, Vector3 from, CMapObject* target, const Vector3* to,
                       int extra);

    // `player` takes what `object` holds: a shared loot record (flags_ 0x20000000, removed afterwards) or what the
    // collector gathers; the gold is added to the player. False for no player, a living owned unit, or nothing to
    // take.
    bool Loot(CUnit* unit, CPlayer* player);
    // The local player (id 0) loots every dead unit, which is then removed. Nothing without a local player.
    void LootDeadUnits();

    // The weather, every 64 ticks: the day-time value; in single player rain starting / stopping,
    // lightning strikes in storms (climate_ 3), bolts in the sky while it rains; the day factor; every
    // object's weather virtual; the players' "gtime".
    void UpdateWeather();

    // For every player and the ten mercenary slots ("merc1".."merc10"), sets the briefing variables b.mercN.* from
    // whether the mercenary is in the party, was hired, and how many others are alive.
    void UpdateMercBriefings();
    // Briefing `name` ("a.b.<record>") is complete for player `playerId`: a network client asks the server; otherwise
    // the player gets the record's experience, gold and items, loses its quest items, its variables are set, and the
    // scripts get GSSetVar / #OnBriefingComplete (also on a network client with EI_Plugin's Briefings).
    void CompleteBriefing(int playerId, const char* name);
    // The end of CompleteBriefing: GSSetVar / #OnBriefingComplete for `name` queued, run at once in
    // kind-3 games.
    void QueueBriefingScript(int playerId, const char* name);
    // The mercenary side of a completed briefing `name`: per slot b.mercN.n8_N clears the variable, n10_N removes the
    // mercenary, n2_N removes it from the party, n1_N / n3_N hire it (amerc_N = 2); then UpdateMercBriefings.
    void ApplyMercBriefing(CPlayer* player, const char* name);
    // Gives `amount` experience: with a player, shared equally by its living heroes (units and party
    // records); without one, shared by the players that have a single hero, in proportion to the sqrt of
    // two of each hero's controller stats. Zero (or NaN) does nothing.
    void AwardExperience(CPlayer* player, float amount);
    // Brings the player's party records up to date with its living units (items, spells and statistics rebuilt from
    // the unit).
    void SyncPartyRecords(CPlayer* player);
    // Before a zone change: every record of `records` notes its unit's leader, gets a placement (if `placements`), is
    // rebuilt from its unit, and the unit is removed (a dead one just loses its owner).
    void StoreParty(RecordPtrArray* records, CParty* partyRecords, std::vector<UnitPlacement>* placements);
    // The player's party leaves the zone: gtime, its items, then (single player and kind 3, with
    // zoneLoaded_) its records without a unit are dropped and the rest stored (StoreParty).
    void CollectParty(CPlayer* player, std::vector<UnitPlacement>* placements);

    // Moves quest items (class 0x14) from `items` to `list`: in single player only those whose description
    // names this zone or whose id an object of the zone asks for; otherwise all.
    void TakeZoneQuestItems(std::vector<CItem*>* items, std::list<CItem*>* list);
    // Gives the player fresh copies of the items it brings into the zone (all of them on a client, the
    // zone's quest items otherwise; nothing in a network game).
    void RestorePlayerItems(CPlayer* player);
    // The party arrives in the zone (single player and kind 3, with zoneLoaded_): its units are created at the
    // entrance's centre, facing away from the map centre or along the exit's heading; `runScripts` runs the script
    // command queue five times.
    void Redeploy(CPlayer* player, std::vector<UnitPlacement>* placements, char runScripts);
    // A new item like `item`, made by the item factory and filled by the item's fill virtual (the second one for
    // spells), with its count; one below its maximum goes on counted_ once.
    CItem* CopyItem(CItem* item);
    void AddCounted(CEngineObject* item);
    CItem* CreateItemNamed(const char* name);
    CItem* CreateSpellNamed(const char* name);

    // Creates a unit for each record of `records` (an existing object with the record's id is removed first), at its
    // entry in `placements` or in a line from `position` along `rotation`, moved to a free cell, under `player`'s
    // control, with its items, spells and AI; heroes get their figure parts, followers their leaders, and with a
    // player the units join the "Heroes" script group.
    void CreateUnits(CPlayer* player, RecordPtrArray* records, const Vector3* position, Quaternion rotation,
                     std::vector<UnitPlacement>* placements);
    // A quick item (potion) is used by `unit`: the effect object "st.tr.130" (model "unhuma", type 9, flags 0x3010)
    // is created at the unit's position and flagged new (flags_ |= 0x180000). `item` is not used.
    void UseQuickItem(CItem* item, CUnit* unit);

    // Brings `player`'s view up to date (once per world update): the zone when it changed, weather, time, wind and
    // countdowns when dirty, then the map objects and timed objects it should see. Returns what the player state's
    // flush returns (0 drops the player), 1 for a player that is leaving.
    int SyncPlayer(CPlayer* player);
    // Sends `player` the zone to load (tick, map and .mob names, exits, the zone's units) and, for a new
    // game, where it starts. 0 when the player state refused it.
    int SendZone(CPlayer* player);

    // WorldDayTime's value: the script's IsNight reads it, the units' movement factors scale by it.
    float nightLevel_;
    std::uint32_t weatherFactor_;         // float bits: 1 dry, 0.9 in rain or snow (the units' fourth movement factor)
    WorldAlarm alarms_[5];
    // The zone's weather kind (its record's setting2C): 3 storms with lightning strikes, 1 light rain,
    // other non-zero values heavier weather; 0 none. Single-player only.
    int climate_;
    int cave_;                            // the zone is a cave (its record's setting30; sent with the zone)
    int weather_;                         // 0 clear, 1 rain, 2 snow (the script's SetWeather)
    float weatherStrength_;               // sent with the weather
    ZoneExits exits_;
    std::uint8_t loaded_;                 // 0 while LoadSave runs
    std::uint8_t unused_101[0x104 - 0x101];
    MapObjectIdMap updateMap_;
    // SendZone also sends the player its start (at its first unit) when set; nothing in the game sets it.
    std::uint8_t sendsStartPosition_;
    // CreateObject makes objects answering IsSynchronised, sounds, lights and particle sources only with
    // loadedFresh_ set, the other map objects only with keepsState_ set; a refused object is released.
    std::uint8_t loadedFresh_;            // the zone is not loaded from a save
    std::uint8_t keepsState_;
    std::uint8_t unused_11F;
    std::string zoneName_;               // the zone's map database name
    std::vector<std::string> mobFiles_;    // .mob files loaded in this zone (LoadMobFile)
    int gameResult_;                      // 0 while the game runs
    std::uint32_t lastRainBoltTick_;      // the tick of the last bolt in the rain (one per 20 ticks); saved
    CHoover* hoover_;                     // counted; made by Setup, deleted by Teardown
    // The server's messages: the login handler reads "SrvErrMsg" / "WrongClientVersion" from it.
    lib::InPlace<CFileRegistry> messages_;
    CFileRegistry* Messages() { return messages_.Get(); }
    int zoneLoaded_;                      // set by LoadMap, cleared by the teardown and a .mob file that fails to load
    int scriptsActive_;                   // non-zero: Tick runs the zone's script (set by Redeploy)
    Vector3 startPosition_;               // where joining players start
    CAIMap aiMap_;
    CAIMap* AIMap() { return &aiMap_; }
    CSectorMap* Sectors() { return &aiMap_.sectorMap; }
    AIEventTable* AIEvents() { return aiMap_.Events(); }
    // Its globals_ hold the "Heroes" group.
    lib::InPlace<CScript> commandsA_;
    CScript* MainScript() { return commandsA_.Get(); }
    lib::InPlace<CScript> commandsB_;
    CScript* SecondScript() { return commandsB_.Get(); }
    CDiplomacy diplomacy_;
    // The session kind: 1 single player, 3 no range test, 4 network; 1 or 3: SaveWorld updates the
    // players' "gtime" first. Joining players take it (CPlayer::sessionKind100_). Also the campaign mode
    // (in the server world): GameFlow::StartZone stores its mode here (1 battle, 2 main menu, 3 base,
    // 4 edge, 8 the end of the game), StartFirstMap 7 for the command-line script.
    int sessionKind_;
    std::string mobName_;                // the zone's .mob file
    std::string mapName_;                // the zone's map
    MapObjectIdMap objects_;              // every world object by id
    // The map objects SendFreshObjects sent (not part of the map itself; IsSynchronised).
    MapObjectIdMap sentObjects_;
    MapObjectIdMap mapObjects_;           // objects saved with the zone (SaveMapObjects)
    MapObjectIdMap otherMapObjects_;      // the map objects that are not units
    MapObjectIdMap namedObjects_;         // objects with a second name
    ObjectIdMap timedObjects_;
    // The zone header SendFreshObjects sends to a temporary player.
    CMemoryStream zoneHeader_;
    ProjectileList projectiles_;
    std::list<CEngineObject*> counted_; // counted references (released on clear)
    SharedObjectList sharedObjects_;
};
static_assert(offsetof(CWorldServer, countdowns_) == 0x70);

// One walkable platform (bridge, deck) the client tests positions against.
struct Platform {
    int id;                               // the object it belongs to (RemoveObject drops its faces)
    float x, y;                           // centre of the bounding circle
    float z;
    float radius;
    Vector3 origin;
    Vector3 axisU;                        // local u axis
    Vector3 axisV;                        // local v axis
    Vector3 normal;
    float sizeU, sizeV;                   // extent along u and v
};
static_assert(sizeof(Platform) == 0x4C);

using PlatformList = std::list<Platform>;
using PlatformNode = PlatformList::iterator;

// The client world's platform set: the engine's CBridgeManager (class 0x34) with the CList of the platform faces
// (AddFace adds at the head).
class PlatformSet final : public CEngineObject {
public:
    PlatformSet();
    ~PlatformSet() override;
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;

    // Max(height, z of every platform surface whose area contains the point); the point is projected
    // onto each platform plane along z.
    float SurfaceHeight(const Vector3* point, float height);
    // Whether (x, y) lies on some platform (with a 0.1 margin).
    bool Contains(float x, float y);
    // The highest platform surface above 0 at (x, y) (with a 0.5 margin): *height its z and *normal its
    // normal; 0 and (0, 0, 1) when there is none.
    void SurfacePlane(float x, float y, Vector3* normal, float* height);
    // Adds the face with corner `a`, edges to `b` and `c` and `normal` when normal.z >= 0.8: origin a,
    // unit axes (b - a) / |b - a| and (c - a) / |c - a|, the two lengths, the normal; *out = the new
    // record (its id, centre and radius are the caller's), null when the face is too steep. Returns 1
    // when added.
    int AddFace(const Vector3* a, const Vector3* b, const Vector3* c, const Vector3* normal, Platform** out);
    // The platform item `item` (offset, radius, the box's corners, pivot) of object `id` placed at
    // `position` with the rotation quaternion `rotation` (w, x, y, z): the box's corners and the six
    // face normals rotated, and every face that points up added with the id, the centre
    // R (pivot + offset) + position and the radius.
    void AddBox(int id, const FootprintPart* item, const Vector3* position, const Quaternion* rotation);

    PlatformList list;                    // the faces, the newest first
};
static_assert(offsetof(PlatformSet, list) == 0x1C);

// The unit set the server's SendZone fills and the client reads back as its unit looks (unit_looks.h): the figured
// records grouped by prototype (each group a hash_multimap by three packed 10-bit values), and the dressed records
// by prototype. All three hash a key to itself; Write walks them in the game's order (the zone message's bytes).
struct UnitSet {
    using FiguredGroup = lib::HashMultimap<std::uint32_t, FiguredUnitRecord>;   // by scale key

    // Adds `unit`'s looks: its prototype, the figure's scale and, for a hero (type_ 0x32), the item ids of its 7 worn
    // pieces and 4 weapons (0xFFFFFFFF for none or an id out of the tables) and its active weapon; other units get
    // extraWeapon 0xFF and the texts their companion holds. A figured record of the same prototype, scale key and ids
    // gets modelNumber -1, else the record (modelNumber the unit's id) joins its prototype's group; then the same for
    // a dressed record.
    void Add(CUnit* unit);
    // Writes the set to `stream`: the number of figured records; per record its head, the morph
    // coefficients, its name count and each name with its terminator; the number of dressed records and
    // per record a 16-bit prototype, two counted dword arrays, a byte and a dword.
    void Write(CStream* stream);

    lib::HashMap<std::uint32_t, FiguredGroup> figured;            // by prototype
    lib::HashMultimap<std::uint32_t, DressedUnitRecord> dressed;  // by prototype
};

// Reads the zone's two unit look lists from `stream`: a count, then per figured record a 16-bit
// prototype, two counted dword arrays, a byte, a dword, the three morph coefficients and a counted list
// of names; then a count and per dressed record the prototype, the two arrays, the byte and a dword.
// Both vectors are resized to the counts.
void ReadUnitLooks(CStream* stream, std::vector<FiguredUnitRecord>* figured, std::vector<DressedUnitRecord>* dressed);

// The client's sector grids: a list of held objects per 16x16 cell (also the heroes and the deferred units).
using SectorList = std::list<HeldPtr<CMapObject>>;
// The client's filed objects, map objects only (every object filed is one the player client created through
// the map object manager, or a unit). Each holds one reference, taken and dropped by the client.
using MapObjectList = std::list<CMapObject*>;
struct SectorGrid {
    std::unique_ptr<SectorList[]> cells_;
    std::unique_ptr<SectorList*[]> rows_;   // row y: its lists
    int unknown08_, unknown0C_;           // (resized when they change)
    std::uint32_t width_;                 // cells along x
    std::uint32_t height_;                // cells along y
    // unknown08_ x unknown0C_ empty cells, then the row pointers.
    void Allocate();
    // When the storage has another size: Free, the size taken, then Allocate.
    void Resize(int width, int height);
    // The cells destroyed (from the last; each releases its objects), then the row pointers.
    void Free();
    // Moves `object` to `position` (stored in the object): when the cell (x and y rounded to nearest, >> 4)
    // changes, out of the old cell's list and to the end of the new one's (cells outside the grid are
    // skipped).
    void Move(CMapObject* object, const float* position);
};

// The client's pending map from an object id to its node in the pending list.
struct PendingIndex : std::unordered_map<std::uint32_t, std::list<CMapObject*>::iterator> {
    int Lookup(const std::uint32_t* key, std::list<CMapObject*>::iterator* value) const;
};
using PendingMap = PendingIndex;
template <std::size_t N>
struct OpaqueRecord {
    std::uint8_t bytes[N];
};

// The worlds' counted helpers: player sources (player_source.h; Query false, the client's Segment calls Pump every
// tick). The server's hands its pending link over once (TakeNew).
class CWorldHandle final : public CPlayerSource {
public:
    CWorldHandle() : pendingLink_(nullptr) {}
    ~CWorldHandle() override;

    int OnShutdown() override;
    CWorldHandle* Clone() override;
    bool Query() override { return false; }
    void TakeNew(JoinedLinks* joined) override;

    netlink::NetLink* pendingLink_;     // counted; handed over by TakeNew
};

// The client world's player source (Setup makes it): it has no state of its own.
class CClientHandle final : public CPlayerSource {
public:
    int OnShutdown() override { return 0; }
    CClientHandle* Clone() override;
    bool Query() override { return false; }
    // The client takes no connections: every link of the list is released and the list emptied.
    void TakeNew(JoinedLinks* joined) override;
};

// A window rectangle of the frame (the client's screen list and hit vector).
struct ScreenEntry {
    int classId;
    unsigned objectId;                    // the object, in the map object manager
    tagRECT rect;                         // on the screen
};
static_assert(sizeof(ScreenEntry) == 0x18);
using ScreenEntryNode = std::list<ScreenEntry>::iterator;
// An object whose screen rectangle is valid (flags 0x20) and that is not hidden (0x40) puts its class id, id and
// rectangle at the end of `queued`.
int QueueScreenEntry(CMapObject* object, std::list<ScreenEntry>* queued);

// The entries the pickers find.
using ScreenHits = std::vector<ScreenEntry>;
// The functor of the for_each over the screen entries: CollectBounds / CollectParts push to `hits` every entry whose
// rectangle holds (x, y) and whose object (found in `manager`) answers the bounds virtual (CollectBounds) or 24
// (CollectParts) for the point.
struct HitCollector {
    HitCollector* CollectBounds(ScreenEntryNode first, ScreenEntryNode last, ScreenHits* hits, long x, long y,
                                CMapObjectManager* manager);
    HitCollector* CollectParts(ScreenEntryNode first, ScreenEntryNode last, ScreenHits* hits, long x, long y,
                               CMapObjectManager* manager);
    ScreenHits* hits;
};

class CLightningManager;
// The visibility fog's manager ("CVisFogManager", class id 0x60): a counted object and a CList.
struct ClientArrayManager : CBaseManager {
    ClientArrayManager();
    ~ClientArrayManager() override;
    int Create() override;
    void Destroy() override;
    int RunTick() override;
    const char* GetClassName() override;
    int GetClassId() override;
    CEngineObject* NewObject(int type) override;

    int Draw(CSceneRenderer* renderer);

    HeldTexture texture;                  // the fog texture (counted)
    std::list<CEngineObject*> list;       // counted
};
// The manager's Clear (its object and every listed one released, the list emptied); a new fog object,
// counted, at the list end.
void VisFogManagerDestroy(ClientArrayManager* manager);
CEngineObject* VisFogManagerAdd(ClientArrayManager* manager);

// The drawing side.
class CWorldClient : public CWorld {
public:
    CWorldClient();
    ~CWorldClient() override;

    // Sets the client world up: the managers, the lights table (LightsGipat.ini), the terrain, the session,
    // the rain and snow layer, the platforms, the cursor and both sector grids. Returns 1.
    int Create() override;
    // Tears the client world down in the reverse order, then deletes the owned objects (counted ones dropped first).
    void Destroy() override;
    // The client's tick. Returns 1; 0 when the session's view has lost its connection (the session is
    // disconnected, FatalError; Rebirth's DisableExitOnCrashReport keeps the session).
    int Segment();
    int RunTick() override { return Segment(); }
    const char* GetClassName() override;
    int GetClassId() override;
    const char* TexturePath() override;
    double GridHeightAt(float x, float y) override { return ClampedGroundHeight(x, y); }
    // The height of whatever surface is on top at (x, y): the ground, or the water when the point is submerged.
    double RawSurfaceHeightAt(float x, float y) override;
    // GridHeightAt / RawSurfaceHeightAt raised to the platforms over the point.
    double GroundHeightAt(float x, float y) override;
    double SurfaceHeightAt(float x, float y) override;
    CMaterialManager* MaterialManager() override;
    CTextureManager* TextureManager() override;
    CBaseManager* ClientLightManager() override;
    CParticleManager* ClientParticleManager() override;
    CEngineObject* ClientManager194() override;
    CEngineObject* ClientManager198() override;
    void MoveObject(CMapObject* object, const float* position) override;
    virtual void MoveObject2(CMapObject* object, const float* position);   // the second grid's move

    void BuildUnitSkins(std::vector<FiguredUnitRecord>* others, std::vector<DressedUnitRecord>* dressed);
    // Draws the world view. Returns 1. Rebirth's LandscapeDrawRadius sets the terrain's draw distance (100 in
    // the game).
    int Render();
    // The object passes over the sectors around the camera (Rebirth's ObjectsDrawRadius widens the 100.0
    // margin). Returns 0 without terrain, else 1.
    int DrawPass();
    float ClampedGroundHeight(float x, float y);
    // A blood print at (x, y) when the setting is on and prints.db's blood entry for the ground there
    // has an alpha and a life (`tile`: the texture quarter and, in the high half, the kind; `fresh` 1
    // for a hit, 0 for a death), coloured by this frame's sunlight.
    void AddBloodPrint(float x, float y, float halfSize, float fullSize, std::uint32_t tile, int fresh);
    // A footprint. A blood print reached (its half width plus the footprint's mean half size; not a
    // fresh one) gives its kind; with a kind the blood entry, else the footprint entry, gives alpha and
    // life. Returns the blood print's kind, else -1 (also when footprints are off).
    int AddFootprint(float x, float y, float halfWidth, float halfLength, float angle, std::uint32_t tile, float scale,
                     int visible, int bloodKind);
    // A scorch mark of `alpha` (made when it is positive and the scorch entry has a life).
    void AddScorchMark(float x, float y, float halfWidth, float halfLength, float angle, float alpha);
    // Lays a decal on the ground into the three arrays (shadows, ground prints): the quad `corners` projected along
    // `direction` from `point`; the terrain under it adds its triangles unless `skipOnPlatform` is set and the point
    // lies on a platform; with `objects`, the platform faces' objects the decal can touch add the triangles of each
    // part of their figure (once per face).
    void ProjectDecal(const Vector3* direction, const Vector3* point, float depth, VertexPacket* const* corners,
                      std::vector<VertexPacket>* points, std::vector<DecalTexCoord>* texCoords,
                      std::vector<std::uint16_t>* indices, std::uint8_t skipOnPlatform, std::uint8_t objects);
    // The ambient sounds' time of day into `out`: "day" from 6 to 18 o'clock, "morning" before 6 and
    // "evening" after 18 within 2..22, else "night". Returns out.
    std::string* AmbienceTimeOfDay(std::string* out);
    // Nearest living unit of the local player's group that `unit`'s side can see (the candidate's
    // relation masks); null when none or `unit` is null.
    CUnit* NearestVisibleUnit(CUnit* unit);
    CPlayerClient* LocalView();
    bool IsLocalViewMode2();
    // A new texture / material of class `type` from the world's managers; null without the manager.
    CTexture* NewTexture(int type);
    CMaterial* NewMaterial(int type);
    // Takes `camera` as the view camera: the old one loses a reference (it is not released at zero), the
    // new one gains one; either may be null.
    void SetCamera(CCamera* camera);
    void SetWorldValue(int value);
    // Message 0x73: the water levels read from `stream` and applied to the terrain. Returns 1.
    int ReadWaterLevels(CStream* stream);
    // FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // Index of the exit whose area contains `point` and that leads somewhere.
    BOOL FindExitAt(const float* point, int* index);
    float* ExitCentre(float* out, int index);
    // One ZoneExit particle source (0x2038) per exit that leads somewhere, in countedA0_ at the exit's index (the old
    // markers released), then UpdateExitMarkers.
    void CreateExitMarkers();
    // Whether the party can leave through `exit`: every living unit of `records` (unit id in the
    // record) stands in an exit (FindExitAt) and that exit is `exit`. Those that do are listed in
    // `inExit` (emptied first). 0 when no unit lives.
    BOOL PartyInExit(const RecordPtrArray* records, int exit, RecordPtrArray* inExit);
    // Makes the bird when there is none, resets it and has it follow the unit of the local party's first
    // record.
    void SummonBird();
    // A quest variable "q.<quest>.<name>" changed to `value`: every filed object whose name is <name>
    // (the text after the second dot) gets the visibility level `value`; a value of 2 (or NaN) with a
    // <name> without a dot summons the bird (as SummonBird).
    void OnQuestVariable(const char* name, float value);
    // The party variable of an object's name: "q.<the local player's zone>.<name>".
    std::string ObjectVariableName(const char* name);
    // Every filed object with a name gets the visibility level of its variable (ObjectVariableName) in the
    // local session's variables; run when the party variables arrive.
    void ApplyObjectVariables();
    // Flags describing the current environment for `unit`: unit state, island (gipat/ingos/suslanger),
    // night (0x100) or day (0x80), and two world settings.
    std::uint32_t EnvironmentFlags(CUnit* unit);

    // Files each object of `list` with a model inside the view frustum in the frame's draw lists; with
    // `animateUnits` 0 units only go on the deferred list, `withScripted` lets heroes with a text-key id
    // through. Returns how many objects were queued for the model-touch list.
    int CollectVisible(SectorList* list, const Matrix4* view, float animTime, std::uint32_t now,
                       char animateUnits, char withScripted);
    // The ground point under screen pixel (x, y): inverts the current view matrix and iterates on the
    // terrain height (Terrain::HeightAt). Returns out (x, y, z).
    Vector3* PickGround(Vector3* out, int x, int y);
    Vector3* PickSurface(Vector3* out, int x, int y);

    // Files an object in the client's maps (objects_, objectGrid_, heroes_ for heroes with a text-key id; the
    // dynamic lists for IsSynchronised objects; platform items for classes 0x54 / 0x5D); units fade in
    // through their controller in a multiplayer session of kind 1. An id already filed is only referenced and
    // released. Returns 1.
    int AddObject(CMapObject* object);
    // When an object with `object`'s id is filed (objects_): its platform faces are removed and, for
    // classes 0x54 / 0x5D, its platform items added again where it now stands (as AddObject).
    void RefreshPlatforms(CMapObject* object);
    // Files `object` in dynamicObjects_ and dynamicNodes_ (as AddObject does) unless its id is there already.
    bool QueueSynchronised(CMapObject* object);
    // Every filed object whose kind has no shadows drops its shadow (after an options change).
    void DropShadows();
    // Takes `object` out of the selection list, its highlight off when it was on; with null every listed
    // object's highlight goes off and the list empties.
    void Deselect(CMapObject* object);
    // Adds `object` (not null) to the selection list: its part bounds refreshed, the highlight on when it was off,
    // and its id appended unless it is listed already.
    void Select(CMapObject* object);
    int DrawLineLists();
    // One more reference on every item and, when the item factory exists, on every spell.
    void ReferenceItemsAndSpells();
    // The reverse, used by the teardown: every item and spell released, each manager emptied.
    void ReleaseItemsAndSpells();
    // The items and spells replaced: all released, then (a block with data) the managers loaded and referenced again.
    // Returns 1.
    int LoadItemsAndSpells(CStream* block);
    // The block read from its start: the spells and then the items it lists (LoadManagerObjects). 0 when
    // the block fails.
    int LoadNewItemsAndSpells(CStream* block);
    // Takes the object `id` out of the maps (platform faces, dynamic lists, heroes_, its grid cell and objects_); the
    // object is released.
    void RemoveObject(int id);
    // Empties the maps, the session's player data and the managers' records.
    void ClearMaps();
    // The zone's leftovers dropped: the exit markers, the second grid's cells, the timed objects, the bird.
    void ResetZoneObjects();
    // The zone's state block (docs/notes/world.md, "Zone state block"). Returns 1, or the fatal error's
    // result when the map does not load.
    int LoadZone(CStream* block);
    // The local session takes a new link: the single-player loopback pair for `player`, a replay of the recording
    // `path`, or a driver for the game client's first connection (false without an active client or connection). Each
    // empties the maps and reconnects the session.
    void SetPlayer(CWorldHandle* player);
    bool JoinGameClient(const char* unused);
    // The player sources and the network session pumped, then the local player's link (when it is
    // active) asked HasIncoming / IncomingCount; false / 0 without one.
    bool PumpHasIncoming();
    int PumpIncomingCount();
    // The local view's owner disconnects. Without a session it reads through null, like the original.
    void DisconnectLocalPlayer();
    // The object at screen point `point`: the screen entries that contain it and whose object answers CollectBounds
    // or CollectParts go to hits_, sorted front to back; the first entry whose object still exists wins (hitCursor_
    // is left on it). Null if none.
    CMapObject* PickAt(const struct tagPOINT* point);
    CMapObject* PickAt2(const struct tagPOINT* point);
    // The unit at screen point `point`: from PickAt2's object on, the first unit among the sorted hits
    // (hitCursor_ moves past the others and those whose object is gone); failing that the same from
    // PickAt's. Null if none.
    CUnit* UnitAt(const struct tagPOINT* point);
    // The same from PickAt2's object only (no second pick).
    CUnit* UnitAt2(const struct tagPOINT* point);
    // Every object of the frame's window list that still exists and whose side half has a drawn part
    // meeting the screen rectangle `rect` is appended to `out`.
    void UnitsInBox(const tagRECT* rect, std::list<CMapObject*>* out);
    // Marks the footprints within their scale times `range` of the point (x, y).
    void MarkFootprintsInSight(const float* point, float range);
    // The wind: stored, given to the terrain's waves and to the rain and snow layer.
    void ApplyWind(const Vector3* direction, float strength);
    // A number over the unit with id `id`; nothing for another object or without them.
    void ShowNumber(int id, int value, std::uint32_t kind);
    // Used in the client's game state 3: every unit has its controller load the face texture.
    void LoadUnitFaces();
    // The target zone of exit `index` (the original copies the exit out of the array and returns the
    // copy's text, which shares the element's buffer).
    const char* ExitTarget(int index);
    void RemoveUnitsOf(const RecordPtrArray* records);
    int SendFlag10(int value);
    // The camera looks at the point (x, y) at its own focus height, with its view applied and kept above
    // the objects. Returns 1.
    int CentreOn(const Vector3* point);

    // The debug display's switches (the console's debug commands; bits 0-3 of debugFlags_): the frame
    // rate, the render counters, the managers' lines and the world's. Switching one off also takes its
    // lines out of the debug info panel.
    void ShowFps();
    void HideFps();
    void ShowRenderInfo();
    void HideRenderInfo();
    void ShowManagers();
    void HideManagers();
    void ShowWorldData();
    void HideWorldData();
    // Creates the party's heroes from their records: the figure from the part names (the hair "hr.%.2d" by the
    // prototype's number), items, player; each unit is filed with AddObject. Armour of type 0 is put on only with
    // `allArmour`. Stops at the first unit that cannot be created or given a figure.
    void CreatePartyUnits(const RecordPtrArray* party, char allArmour);
    // Appends to the plain pointer list `out` every object of the sector grid's cells around `point`
    // whose (x, y) lies within radius + hypot(rangeX_, rangeY_) of the point. The cell range (truncated
    // and clamped to the terrain) and the walk's cursor are left in rangeFirstX_ .. rangeY_. The
    // original's z difference is the point's z minus itself (0, kept).
    void ObjectsInRange(const Vector3* point, float radius, std::list<CObject3D*>* out);
    // Reads the light colours from config\<file> (an .ini, LightsGipat.ini): each key of [sunlight],
    // [ambient] and [sky] names its hour in characters 4..5 ("hour07") and holds "r, g, b" (0..255,
    // scaled by 1/255, alpha 1); the hours between two keys in file order are filled by linear steps
    // from the earlier key (the first from hour 0). Hours are not range-checked. Returns 1.
    int LoadLights(const char* file);
    // The frame's light from the time of day: fog, sun direction and the sunlight, ambient and sky colours
    // interpolated between the hour's table entries; once per new hour the terrain's sun. A cave takes hour
    // 0's colours and a fixed sun. Returns 1 when a new hour began (or the renderer's flag bit 0 is clear:
    // nothing done), else 0; no caller reads it.
    int UpdateLighting();
    // With one marker per exit (countedA0_, ZoneExit particle sources): a marker whose target zone's
    // party variable "z.<zone>" is 1 (or NaN) gets extent (1, 0, 0) (its particles turn red), the
    // others (0, 0, 0).
    void UpdateExitMarkers();
    // The graphics set up on `device`: the textures, platforms and sky, the digit sprites (numbers_), a new
    // figure loader in figureLoader_ (counted, the old one released) and the sound file counts.
    void SetUpGraphics(CTextureDevice* device);
    // Once (skyDome_ null): a new CSky with a CSkyObject (class 0x44, id -666) "nask0" loading
    // "nask0sky.fig". Without the file the object is referenced and released again.
    void LoadSky();

    CPlayer* Session() const { return session_; }

    int nextClientObjectId_;              // -100000 after Setup; NextClientObjectId counts it down
    std::uint32_t debugFlags_;            // the debug display's switches (bits 0-3: ShowFps ...)
    ZoneExits exits_;
    // The exits' particle markers, one per exit or null (counted; CreateExitMarkers / UpdateExitMarkers).
    std::vector<HeldPtr<CParticleSource>> countedA0_;
    SectorList heroes_;                   // heroes with a text-key id (counted)
    MapObjectList objects_;               // every filed object (one reference each)
    // The objects answering IsSynchronised, with their nodes by id in dynamicNodes_ and the highest id
    // in dynamicMaxId_ (Segment drops those whose update returns 0).
    std::list<CMapObject*> dynamicObjects_;
    PendingMap dynamicNodes_;             // id -> its node in dynamicObjects_
    int dynamicMaxId_;
    std::list<ScreenEntry> screenEntries_;   // the window rectangles of the frame
    ScreenHits hits_;                     // the entries under the point (PickAt)
    ScreenEntry* hitCursor_;              // PickAt's walk, left on the entry it returns
    std::uint8_t unused_104[0x108 - 0x104];
    lib::HashMap<unsigned, LineBatch*> lineLists_;   // line lists by key (DrawLineLists: drawn in STLport's order)
    int lastLineListKey_;                 // the last line list key handed out; 0 from the constructor
    SectorGrid objectGrid_;               // the objects by sector (MoveObject moves them)
    std::uint32_t rangeFirstX_, rangeFirstY_, rangeLastX_, rangeLastY_, rangeX_, rangeY_;
    SectorGrid secondGrid_;               // (MoveObject2 moves objects in it)
    std::uint8_t unused_168[0x180 - 0x168];
    Terrain* terrain_;                    // counted
    // The zone's allod in lower case ("gipath" after Setup; the zone load takes it from the map
    // database's record of the local player's zone, "gipat" without one). It names the terrain's
    // texture pages, the lights table and the music theme.
    std::string zoneName_;
    CMaterialManager* manager188_;        // counted
    CTextureManager* textureManager_;     // counted
    CLightFactory* manager190_;           // counted
    CEngineObject* manager194_;           // (ClientManager194 returns it; not identified)
    CEngineObject* manager198_;           // (ClientManager198 returns it; not identified)
    CParticleManager* particleManager_;        // counted
    CLightningManager* listManager_;      // owned
    ClientArrayManager* arrayManager_;    // owned
    struct CMusicSystem* music_;          // owned
    struct CSky* skyDome_;                // the sky (owned)
    CEffectMoshka* bird_;                 // the bird following the party (SummonBird), owned
    CFlyingHitPoints* numbers_;           // owned
    // Segment ticks the ground prints and colours the blood prints by the light.
    std::list<BloodPrint> bloodPrints_;
    std::list<Footprint> footprints_;
    std::list<GroundPrint> scorchMarks_;
    int lightLevelsCountdown_;            // time left before the blood prints are lit again (Segment); 0 after Setup
    RefPtrTo<CClientHandle> handle_;      // the client's player source
    CSoundManager* sounds_;               // the sound manager (owned; the client controller's hit sounds)
    CTextManager* texts_;                 // the text resources (counted)
    PlatformSet platforms_;
    // The local session: the local CPlayer; its connection is the local view (a CPlayerClient), its
    // sessionKind100_ 1 / 3 for kinds of multiplayer session.
    CPlayer* session_;
    CCamera* camera_;                     // the view camera (DrawPass, Render)
    std::uint8_t unused_214[0x224 - 0x214];
    std::string texturePath_;            // <data path>textures\ (Setup)
    int constant228_;                     // 10 from the constructor; nothing in the exe reads it
    // The light colours by hour (0..23; r, g, b, 1) from the lights table's [sunlight], [ambient] and
    // [sky] sections (LoadLights).
    LightColour sunlight_[24];
    LightColour ambient_[24];
    LightColour sky_[24];
    // This frame's light (UpdateLighting): the ambient colour (the table's, or 0.8 x the sunlight when
    // the table's is black; its fourth float is never written), a copy of it (four floats, the sky's
    // ambient) and the sunlight (its fourth float is whatever the original's stack held).
    LightColour frameAmbient_;
    LightColour skyAmbient_;
    LightColour frameSun_;
    int lightingMark_;                    // 0 after Setup, 0x80 once UpdateLighting runs outside a cave; never read
    // 1: a cave zone (the map's flag: LightsCave.ini, the sky "Sky01", light fixed at hour 0;
    // EnvironmentFlags 0x400 instead of 0x200).
    int cave_;
    int partyDanger_;                     // 0 after Setup; the party state's danger (message 0x60: 2 fighting)
    int constant6E8_;                     // 0 from the constructor; nothing in the exe reads it
    int ticksRun_;                        // Segment's count; AddObject fades units in when >= 1
    float tickFraction_;                  // added to the tick for the units' time (network client)
    // The rain / snow layer, built and destroyed with the world. Its `mode` (1 rain, 2 snow) picks the ground
    // prints' wet look and an environment flag.
    CRainSnow rainSnow_;
    std::list<int> list7BC_;             // the selected objects' ids
    int worldValue_;                      // -1; message 0x12 sets it (SetWorldValue); the exe never reads it
    int constants7C4_[3];                 // 0 from the constructor; nothing in the exe reads them
    void* cursor_;                        // HCURSOR wincurs\Df0000.cur
    // The window's names, set by the constructor and freed by the destructor only: "EvilIslands",
    // "EvilIslandsClass", "EvilIslands", "EvilIslands Paused".
    std::string appName_;
    std::string windowClass_;
    std::string windowTitle_;
    std::string pausedTitle_;
};

// Orders screen entries (their objects by objectId) by distance along the view's depth axis; dead and
// living objects are separated first.
struct DepthLess {
    float* view;                          // the view matrix (row-major): row 2 the depth axis, its translation the offset
    CMapObjectManager* manager;
    bool Less(const ScreenEntry* a, const ScreenEntry* b);
};
// The draw lists' order (DrawPass): by the first part set's texture, then the figure, then the view
// depth. The original compares the texture and the figure by address; ours by name
// (address_order::DrawBefore).
struct UnitOrderLess {
    bool Less(CMapObject* a, CMapObject* b);
};
// Orders the client's variable nodes (ClientSession::variables_) by their stamp
// (VariableMessage::serial_).
struct EntryIdLess {
    BOOL Less(const stlp::list<RefPtr>::Node* a, const stlp::list<RefPtr>::Node* b);
};
// STLport's sort of the changed variables' nodes with EntryIdLess (the flow's update).
void StampIntroSort(stlp::list<RefPtr>::Node** first, stlp::list<RefPtr>::Node** last, int valueType, int depthLimit,
                               const void* comp);
void StampInsertionSort(stlp::list<RefPtr>::Node** first, stlp::list<RefPtr>::Node** last, const void* comp);

BOOL IsMultiplayerClient();
// Game clock: whole ticks + fraction.
float GameTime();
class CParty* LocalPlayerData();
// The local player's party's zone (a copy).
std::string LocalPlayerZone();
float LocalPlayerVariable(const char* name);
// Float variable `name` of the local player: the client's view in multiplayer, player 0
// of the server otherwise (0 without that player).
float PlayerZeroVariable(const char* name);

struct GameFlow {
    // Saves the game into saves\<slot>\ (single player only): empties the folder, copies the visited zones' files
    // from saves\current, writes scenario.sav and, outside the cutscene zones, mission.sav. Without scenario.sav the
    // game goes back to the main menu. Returns 1 when saved.
    int SaveGame(const char* slot);
    // Fills the zone table once, while it is empty: for every "*.mq" file of the maps folder, its title is registered
    // as a quest folder (RegisterQuests); a zone the map database knows gets an entry {the title, the record's map
    // name, the allod}: 0 / 1 / 2 by the three map name lists, else 3.
    void BuildZoneTable();
    // Used each network step. The local player's "z.MPGame1" variable, set to 1 or removed: on a client unless the
    // client session's field is active; on the host when more than one player is in (flag80_) or unless the client's
    // field is active. Nothing without an active client session.
    void UpdateMultiplayerVariable();
    // The multiplayer start zone for the client session's game type: "bz2mpg".."bz4mpg" for
    // 1..3, else "bz1mpg"; into `out`, returned.
    std::string* MultiplayerZoneInto(std::string* out);
    // Starts a multiplayer game: the party manager is reset, a single party record gets the unit id
    // 0x3B9AF5CE, the party outside a game is sent to the start zone with "z.MPGame1" = 1 and
    // redeployed; the flow loads that zone (state 4) and the client's game state request becomes kind 1.
    void BeginMultiplayerGame();
    // The local player has seen `briefing`: for a name ending in "_N" the zone's quests are loaded first
    // (with the name without "_N"), then BriefingVariableInto's variable is set to 1.
    void MarkBriefing(const char* briefing);
    // With an active client session: the local player's party is copied into the party outside a game,
    // redeployed by the party manager, its map transfer sent, then the flow's update.
    void ReturnLocalParty();
    // With an active client session: the flow's update, then an answered game state request (kind 3)
    // becomes a new one of kind 0.
    void UpdateAndAcknowledge();
    void RequestBriefing(const char* briefing);
    void ForgetBriefing(const char* briefing);
    void RecreateClientSession();
    int SaveGameThunk(const char* slot);
    // Clears the sessions-open flag, releases both network sessions (server, then client), sets the state to 1.
    void CloseSessions();
    // Used by the menus and the interface on leaving a network game. As CloseSessions without the
    // state: cleared, the server and the client session released.
    void DropSessions();
    // Used by the hero screens. DropSessions, then the resources go back to the local database
    // (UseResources(false)).
    void EndSession();
    // The client's variable at `node` (an element of ClientSession::variables_) changed: "q." variables
    // go to the quest log and the client world. Only the argument is read.
    static void OnVariableChanged(const stlp::list<RefPtr>::Node* node);
    // Registers a quest folder once: the flow loads its quests, then with "<folder>\quest.reg" in the
    // texts the quest table reads it, the map database loads "<folder>\map.txt" and the folder joins the
    // list.
    void RegisterQuests(const char* folder);
    // The multiplayer zone switch, in two calls: the first (flag80_ clear) resets the host's
    // connections, takes the party (player 0's records when its state has a party, else the outside
    // party) for zone7C_, asks for game state 3 and requests state 6 (host) or 5 (client, to its peer's
    // zone); the second (flag80_ set) asks for state 1 and the start zone again, and the host drops its
    // client.
    void SwitchMultiplayerZone();
    // Hosts a multiplayer game: new server (game type) and client sessions, the client connects to
    // 127.0.0.1 with the password (20 bytes) as hello, then the server's name ("Server" when empty),
    // value25E0, password, listing (not `unlisted`) and value25FC are set (both between calls patched by
    // Rebirth).
    void StartServer(const char* name, int value25E0, int gameType, const char* password, char unlisted, int value25FC);
    void StartClient();
    void JoinServer(const net::NetAddress* address, const char* password);
    // The flow's requests: the state (state_) the flow's update acts on next, the zone or save name
    // (requestTarget_), requestArg_ and, for 5 and 6, a copy of a party (requestParty_).
    void RequestState3();
    void RequestLoad(const char* name, char flag);                   // 2, requestArg_ = flag != 0
    void RequestZoneChange(const char* zone, int entrance);          // 4, requestArg_ = entrance
    void RequestPartyZone5(const char* zone, CParty* party);
    void RequestPartyZone6(const char* zone, CParty* party);
    void DisconnectClient();
    // Starts `zone`: the current zone is torn down; mode 0 takes the mode from the zone record's kind
    // (0 -> 1, 1 -> 4, 2 -> 3), "endofgame" is mode 8, mode 2 loads the main menu zone (also when the
    // zone is unknown, after FatalError and closing the network sessions); otherwise the party enters
    // the zone and the clock follows its "gtime". The mode becomes the campaign mode (the server world's
    // sessionKind_). Rebirth's AllowScriptsInMainMenu sets the server's scriptsActive_ in the menu.
    void StartZone(const char* zone, int mode);
    // A new game: "exec autoexec.cfg" on the console, the party manager's new game, saves\current
    // emptied, then the party's zone started (mode 0). Rebirth's AutoExec runs addon's AutoExec lines
    // first.
    void StartNewGame();
    // Acts on the pending request (state_, then cleared): the zone being left is saved, the interface
    // closes its windows, the zone is torn down and the resource database follows the network state;
    // then 1 main menu, 2 load the save named in requestTarget_ (two ticks first; autoplay with
    // requestArg_), 3 new game (StartNewGame), 4 the zone in requestTarget_, 5 / 6 multiplayer zone
    // changes with the party copy in requestParty_. Ends with the network pump.
    void ProcessRequest();
    // The resource database follows `network`: "databaseLMP.res" in a network game, else "database.res".
    // When the name differs from the last one ("database.res" at first) the databases are switched and
    // the difficulty is saved and cleared (network) or taken back from it.
    void UseResources(bool network);
    // Loads the game saved in saves\<slot>\ (not in multiplayer, or kind 3): "exec autoexec.cfg",
    // saves\current emptied, scenario.sav read back (the party; by version the input, scripts and
    // globals), the visited zones of the current map copied back (the others marked not visited), then
    // the zone: from mission.sav (kinds 0, 2) or started afresh. Without scenario.sav the main menu
    // starts. Returns 1 when loaded. Rebirth's AutoExec runs addon's AutoExec lines first.
    int LoadGame(const char* slot);
    // After the sessions start: the party step (1), the variables of the party outside a game emptied,
    // its members reset, and the client's host record takes the hero's and party's
    // names (defaults "Human Hero", "Bug"). Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void AfterServerStart();
    // A multiplayer client's update from the server: the quest texts unloaded, the local player's
    // variables replaced ("constr.current" kept), the zone's quests, the game type's construction
    // variable and briefings (at least 1), the seen briefings, the quest folders of "q." variables, then
    // the server's variables applied in stamp order, and the multiplayer flag variable. Rebirth's
    // PersistentGlobalVars keeps the "gvar." ones.
    void UpdateFromServer();

    class CParty* RequestParty() { return requestParty_.Get(); }

    GameFlow* Construct();
    void Destruct();
    // saves\current emptied, the multiplayer zone (zone7C_) named; a host without an active client
    // session starts it when there is a name.
    void SetNetworkZone(const char* zone);

    int state_;                           // the pending request (1..6; ProcessRequest runs it)
    std::string requestTarget_;           // the request's zone or save name
    PartyStorage requestParty_;
    int requestArg_;                      // the entrance (4) or the load flag (2)
    std::string zone7C_;                 // the multiplayer zone (SwitchMultiplayerZone)
    char flag80_;                         // set by SwitchMultiplayerZone, cleared by the game's start
};
static_assert(offsetof(GameFlow, requestTarget_) == 0x04);

extern lib::StaticObject<GameFlow> g_gameFlow;

// A multiplayer hero record (the saved parties' entries below, the hero screens' records): its party
// (CParty) and file name.
struct MpHeroRecord {
    // A copy copies the party (CParty's copy constructor and Assign, which read the source non-const).
    MpHeroRecord();
    MpHeroRecord(const MpHeroRecord& other);
    MpHeroRecord& operator=(const MpHeroRecord& other);
    ~MpHeroRecord();
    // Writes the party to `folder` + file: a file name with no '.' or with anything but digits before
    // the last one is deleted instead. Otherwise the old file is moved to `folder` + "temp.mp", the
    // party saved (with its variables) into a memory stream and that written as a save file (magic
    // 0x114, the save version), then the temporary file deleted.
    void WriteFile(const char* folder);
    // Reads the party from `folder` + file (magic 0x114, the versions kSaveOldestVersion..kSaveVersion):
    // false when the file does not read, else the party loaded (with its variables) and the memory
    // stream's ok flag.
    bool ReadFile(const char* folder);
    CParty* Party() { return party.Get(); }

    PartyStorage party;
    std::string file;
};

// The saved multiplayer parties.
struct SavedParties {
    // Rebuilds the list from the folder's "*.mp" files: each is read into party records; a loaded one
    // has its variables emptied and, with a record's field == 1, is appended. Rebirth's
    // PersistentGlobalVars keeps the "gvar." variables.
    void Reload();
    void SaveAll();
    void Destruct();
    SavedParties* Construct();
    void Select(const MpHeroRecord* hero);
    // Room for one more record the way the original's vector grew: doubled, the old records copied before the
    // new one is made. A record's copy loads its party again, which draws object ids and random numbers.
    void ReserveOne();

    std::string folder_;
    std::vector<MpHeroRecord> entries_;
    MpHeroRecord outsideGame_;
    MpHeroRecord* OutsideGame() { return &outsideGame_; }
};
extern lib::StaticObject<SavedParties> g_savedParties;
inline SavedParties& TheSavedParties() { return *g_savedParties; }

int RequestZone(const char* zone, int entrance);

// With the briefing texts loaded: every quest text file loaded so far (g_questFiles) is unloaded and
// the list emptied.
void UnloadQuestTexts();
extern std::vector<std::string> g_questFiles;

// Loads the quest texts of `zone`: false without the briefing texts or when the maps folder has no
// "<zone>.mq". A running game server first lists the file's bytes under "<zone>.mq" for its clients
// (ShareFile); then a path not yet among the quest files joins them and the briefing texts add it as a
// source.
bool LoadZoneQuests(const char* zone);

bool NameInList(const char* const* list, const char* name);

void ClearSaveFolder(const char* path);

// For the zone table entries with `id`, grouped by their second name in order of first appearance: one
// random entry per group (never the one whose first name is `exclude` when there is a choice; one
// IsaacRandom draw per group) has its first name appended to `out` (an STLport vector of strings).
void PickZones(std::vector<std::string>* out, int id, const char* exclude);

// Reads mission.sav from a file stream (magic 0x102) and restores the world (LoadSave); old versions
// also carry the globals (< 0x52) and the input state (< 0x47). 0 when the file does not read.
int LoadMission(CStream* file);

// SaveFileWrite writes the header, at version > 0x6B a checksum, then a flag byte and the LZ-compressed data
// (or, when decompressing it does not give the data back, 0 and the raw data). SaveFileRead reads one back
// (versions up to kSaveLastUncompressedVersion uncompressed); false on a bad header or checksum.
void SaveFileWrite(CStream* file, unsigned magic, int version, CMemoryStream* data);
bool SaveFileRead(CMemoryStream* out, unsigned magic, int minimum, int maximum, CStream* file);
inline constexpr int kSaveVersion = /*00790260*/ 116;
inline constexpr int kSaveLastUncompressedVersion = /*00790264*/ 100;
inline constexpr int kSaveOldestVersion = /*00790268*/ 66;

// The mercenary who speaks `briefing` (the second word of the first "#show" line of
// section "briefing <name>" that is not "Hero"), or "", into `out`. The briefing's
// variable, "b.<speaker>.<briefing>" or, without a speaker, "b.elder.<briefing>".
std::string* BriefingSpeakerInto(std::string* out, const char* briefing);
std::string* BriefingVariableInto(std::string* out, const char* briefing);
// The multiplayer peer's map (the client's session field or the server's), replaced by the first line of its "quest
// <name>" section when there is one; "" without a peer.
std::string* SessionMapNameInto(std::string* out);
// Chooses the loading movie for zone `zone` (may be null) by its allod in the map database:
// progres2.bik for gipat, progres3.bik for ingos, progres4.bik for suslanger, else progres.bik
// (Rebirth's FixProgressNames: progres2.bik for gipat2, progres5.bik for jigran); the zones gz1g and
// bz7g (case-insensitive) take progres1.bik.
void SelectLoadingMovie(const char* zone);

inline int TimeSpeedSetting() { return options::TheOptions()->defaultTimeScale_; }
inline float FogStartDay() { return options::TheOptions()->fogDayStartDistance_; }
inline float FogStartNight() { return options::TheOptions()->fogNightStartDistance_; }
inline float FarClipDistance() { return options::TheOptions()->farClipDistance_; }

// The client's per-frame draw lists (CollectVisible files the visible objects, DrawPass sorts, draws and
// empties them), the globals in this order; then the deferred unit list (counted pointers).
using FrameQueue = std::vector<CMapObject*>;
extern FrameQueue g_touchedModels;
extern FrameQueue g_shadow52;
extern FrameQueue g_shadowUnits;
extern FrameQueue g_shadow5D;
extern FrameQueue g_shadowOther;
extern FrameQueue g_shadow54;
extern FrameQueue g_visible52;
extern FrameQueue g_visibleUnits;
extern FrameQueue g_visible5D;
extern FrameQueue g_visibleOther;
extern FrameQueue g_visible54;
enum class FrameList {
    TouchedModels,   // models to keep loaded (by last use)
    Shadow52,
    ShadowUnits,
    Shadow5D,
    ShadowOther,
    Shadow54,
    Visible52,
    VisibleUnits,
    Visible5D,
    VisibleOther,
    Visible54,
};
inline FrameQueue& FrameObjects(FrameList list) {
    static constexpr FrameQueue* kQueues[] = {&g_touchedModels, &g_shadow52,     &g_shadowUnits,  &g_shadow5D,
                                              &g_shadowOther,   &g_shadow54,     &g_visible52,    &g_visibleUnits,
                                              &g_visible5D,     &g_visibleOther, &g_visible54};
    return *kQueues[static_cast<int>(list)];
}
extern lib::StaticObject<SectorList> g_deferredUnits;
inline SectorList* FrameDeferredUnits() { return &*g_deferredUnits; }
// The id the next object only the client makes gets (client figures such as a bow shot's arrow); each
// use counts it down.
inline int& NextClientObjectId() { return TheClientWorld()->nextClientObjectId_; }
inline CItemManager* WorldItemManager() { return TheServerWorld()->itemManager_; }
inline CMapObjectManager* WorldObjectManager() { return TheServerWorld()->manager40_; }

// A lightning strike from `from` to `to`, as a script line on the queue: one time in four only a
// thunder sound, otherwise a point light at the middle, a sound and two bolts.
void ShowBolt(Vector3 from, Vector3 to);

// With `plain` 0 the players' gold, party records and, on the server, their units' controller statistics are
// XORed with a new random key, with 1 they are XORed back; also the party records outside a game. Nothing
// when the state is already `plain`.
void GuardPlayerData(char plain);
void GuardPlayers(CPlayerManager* players, char units, std::uint32_t key);

// ---- Client draw lists and their sorting --------------------------------
// STLport's sort instantiated for two orderings; the entry points keep STLport's signature (first, last, a
// dummy value-type pointer, the depth limit, the empty comparator).

// STLport __introsort_loop with UnitOrderLess (model rank, a model field, view depth).
void IntroSortByModel(CMapObject** first, CMapObject** last, void* valueType, int depthLimit,
                                 int comp);
// STLport __final_insertion_sort with UnitOrderLess.
void FinalSortByModel(CMapObject** first, CMapObject** last, int comp);
// __introsort_loop ordering objects by their model's last use, most recent first.
void IntroSortByModelUse(CMapObject** first, CMapObject** last, void* valueType, int depthLimit,
                                    int comp);
// __final_insertion_sort with the same ordering.
void FinalSortByModelUse(CMapObject** first, CMapObject** last, int comp);
// The comparator of that ordering: b's model used before a's.
struct ModelUseLess {
    BOOL Less(CMapObject* a, CMapObject* b);
};

// Reads objects up to a zero byte: after each mark an id, a class and a field of the object; an object
// registered under the id is released first (its count raised to 1 when 0), then a new one is created
// (NewObject), loaded (Load) and referenced. 1 at the end mark, 0 when a mark is read from a failed
// stream.
int LoadManagerObjects(CBaseManager* manager, CStream* stream);
// The parts of it a list of a class derived from CEngineObject walks itself: one object's entry, and the end
// mark.
void SaveListedObject(CEngineObject* object, CStream* stream);
void SaveListedObjectsEnd(CStream* stream);

// The save screens: 1000000 plus the sizes (low dwords) of the files in saves\current.
int CurrentSaveSize();

// Every resource database (units with their sound file counts, skills, spell tables, items) destroyed
// and constructed again in place, then loaded from `name`.
void SwitchResourceDatabases(const char* name);
