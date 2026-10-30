#pragma once

// The world: the server side that runs the game rules (WorldServer) and the client side that draws it and answers
// terrain queries (WorldClient), both derived from a common base. Class names come from the game's error strings.

#include "core_refcount.h"
#include "effects.h"   // RainSnow
#include "engine_object.h"
#include "game_singletons.h"
#include "ground_print.h"
#include "maths.h"
#include "object_diplomacy.h"
#include "party.h"
#include "player_messages.h"
#include "player_source.h"
#include "registry.h"
#include "unit_ai.h"   // AIEventTable (the server world's AI events)
#include "unit_looks.h"

#include "lib/bit_flags.h"
#include "lib/hash_map.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

inline constexpr Quaternion kNewUnitRotation = /*007C2DE0*/ {1.0f, 0.0f, 0.0f, 0.0f};


class ParticleSource;
class PlayerClient;
namespace netlink {
class NetLink;
class DriverSession;
}
namespace net {
struct NetAddress;
class VariableMessage;
}
class WorldHandle;
class ClientHandle;
struct SoundManager;
class SpellManager;
class LinkFigureLoader;
class Texture;
class TextureManager;
class MaterialManager;
namespace terrain { class Terrain; }
class Camera;
class Material;
struct LineBatch;
class Spell;
class SpellEffect;
class ParticleManager;
class Hoover;
class EffectMoshka;
namespace mapspec { class Spec; }

class WorldServer;

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

// The world's object maps plus the highest key ever inserted; walked in STLport's order (the timed objects are ticked
// and saved in it, the map objects saved by SaveMapObjects and collected for the clients' sync).
struct ObjectIdMap {
    lib::HashMap<unsigned, EngineObject*> map;
    int maxId = 0;

    void Set(const unsigned& key, EngineObject* const& value);
};

struct MapObjectIdMap {
    lib::HashMap<unsigned, MapObject*> map;
    int maxId = 0;

    void Set(const unsigned& key, MapObject* const& value) {
        if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
        map[key] = value;
    }
};

// Walks the world's player list; the list object is reference counted while an iterator holds it.
struct PlayerIterator : ObjectListIteratorOf<Player> {
    PlayerIterator() : ObjectListIteratorOf<Player>{{nullptr, {}}} {}
    PlayerIterator(const PlayerIterator&) = delete;
    PlayerIterator& operator=(const PlayerIterator&) = delete;

    // Drops the list's count without releasing it (the list must outlive the iterator).
    ~PlayerIterator() {
        if (manager) --manager->refCount;
    }
};

class FlyingHitPoints;

// A light colour of the client's tables (a is 1).
struct LightColour {
    float r, g, b, a;
};

class World : public ManagerContext {
public:
    World();
    ~World() override;

    // Folders, managers, the ISAAC generator and the countdowns set up. Returns 1.
    void Create() override;
    // Every object of the spell manager gets ClearState, then the managers are deleted whatever their counts.
    void Destroy() override;
    bool RunTick() override;

    World* ThisWorld() override { return this; }
    BaseManager* TimedObjectManager() override { return timedObjectManager; }
    BaseManager* GetFigureManager() override;   // figureManager (world_managers.cpp: FigureManager is complete there)
    ItemConstructor* GetItemConstructor() override { return itemConstructor; }
    SpellConstructor* GetSpellConstructor() override { return spellConstructor; }
    ItemManager* ItemFactory() override { return itemManager; }
    MapObjectManager* GetMapObjectManager() override { return objectManager; }
    PlayerManager* PlayerList() override { return players; }
    BaseManager* SpecificManager() override { return specificManager; }
    SpellManager* GetSpellManager() override { return spellManager; }
    LinkFigureLoader* FigureLoader() override { return figureLoader; }

    // The world's own virtuals; docs/notes/map_objects.md, "Virtual slot names".
    virtual const char* TexturePath() { return nullptr; }   // the client's "textures\"
    virtual bool IsServer() { return false; }                    // the server's 1
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
    virtual MaterialManager* GetMaterialManager() { return nullptr; }
    virtual TextureManager* GetTextureManager() { return nullptr; }
    virtual BaseManager* ClientLightManager() { return nullptr; }
    virtual ParticleManager* ClientParticleManager() { return nullptr; }
    virtual int Unused39() { return 0; }
    virtual MapObject* ObjectWithId(int id);
    // `object` moves to `position` (the client's sector map).
    virtual void MoveObject(MapObject*, const float*) {}

    // The game time: tick * secondsPerTick, the tick (above 2^24 in long games) taken whole, the product rounded once.
    float Elapsed() const;
    // The time of day in hours: fmod(Elapsed() + timeBase, 24).
    float TimeOfDay() const;
    // Whether the hour of the day is before 6 or from 22 on (a NaN counts as night).
    bool IsNight();
    // The time of day in hours and the time speed (1 / (seconds per tick * 15)), both stored as floats.
    struct Clock {
        float hours;
        float speed;
    };
    Clock GetTime();
    void SetWind(const Vector3& direction, float strength);
    Unit* FindUnit(int id);
    // How dark the time of day is: 1 before 8 o'clock (and for a NaN), 0 until 20, then rising to 1 by midnight.
    double NightLevel();
    // A new empty network session shared into the world's session pointer, the previous one destroyed.
    void OpenSession();
    void CloseSession();
    // Stores paused. SetServerPaused is the server's entry: EI_Plugin's Pause in mode 3 makes it a plain return.
    void SetPaused(std::uint8_t value);
    void SetServerPaused(std::uint8_t value);

    float timeBase;
    float secondsPerTick;
    std::uint8_t windDirection[12];       // three floats; saved and loaded as they are
    std::uint32_t windStrength;           // float bits, 0..1; saved and loaded
    Ref<LinkFigureLoader> figureLoader;    // the client's from SetUpGraphics; FigureLoader returns it
    PlayerManager* players;
    FigureManager* figureManager;       // FigureManager
    MapObjectManager* objectManager;    // map objects by id (its NewObject creates one)
    BaseManager* specificManager;       // SpecificManager
    BaseManager* timedObjectManager;    // the timed objects (spells, effects; TimedObjectManager)
    SpellManager* spellManager;         // the spells (SpellManager)
    SpellConstructor* spellConstructor;    // SpellConstructor
    ItemManager* itemManager;           // items (ItemFactory)
    ItemConstructor* itemConstructor;   // ItemConstructor
    // A counted helper World::Create shares in.
    Ref<WorldHandle> worldHandle;
    Ref<netlink::DriverSession> netSession;    // the network session (OpenSession)
    int tick;                             // game time in ticks
    int updateCount;                      // world updates run (the server's Update)
    // Paused: the server's Update skips the players and the tick while it is set; the client's Segment
    // skips its own ticks, and the client copies the server's flag from message 0x60, telling the HUD.
    std::uint8_t paused;
    WaterTweens countdowns;
};

// One of the five alarms InvokeAlarm raises (the script's IsAlarm / AlarmTime / AlarmPosX / AlarmPosY).
struct WorldAlarm {
    std::uint8_t active;
    int time;                             // tick it was raised
    float x, y;
};
static_assert(sizeof(WorldAlarm) == 16);

// The world the missiles find while they load.
inline WorldServer* g_loadingProjectileWorld = /*007C84EC*/ {nullptr};

// An entry of the world server's shared-object list: the loot a unit left.
struct SharedObject {
    int objectId = 0;
    int player = 0;
    std::uint32_t money = 0;                // gold the loot adds to the finder's party
    std::vector<HeldRef<Item>> objects;    // counted

    // Reads the entry: id, player, value, then object ids resolved by the item manager; each found
    // object is appended with a reference.
    void Load(ByteStream* stream);
};

class WorldServer : public World {
public:
    WorldServer();
    ~WorldServer() override;

    // The managers, the messages registry, the Hoover, the wind (1, 0, 0; strength 0.4) and the zone state
    // set up. Returns 1, or FatalError's result when the managers or the registry fail.
    void Create() override;
    void Destroy() override;
    // One world update: drops disconnected players and lets the others' units wake what they hear, accepts
    // new players, runs the players and the world tick (unless paused), rebuilds the local players' "Heroes"
    // group, synchronises every player (SyncPlayer; 0 drops it).
    void Update();
    bool RunTick() override {
        Update();
        return true;
    }
    const char* ClassName() override;
    ClassId GetClassId() override;
    Item* DuplicateItem(Item* item) override { return CopyItem(item); }
    bool IsServer() override;
    double GridHeightAt(float x, float y) override { return GridHeight(x, y); }
    // RawSurfaceHeightAt: the AI map's top height at (x, y, 0); the other two return GridHeightAt /
    // RawSurfaceHeightAt unclamped.
    double RawSurfaceHeightAt(float x, float y) override;
    double GroundHeightAt(float x, float y) override;
    double SurfaceHeightAt(float x, float y) override;
    void SetTime(float hours) override;
    void SetTimeSpeed(float speed) override;
    MapObject* ObjectWithId(int objectId) override { return FindObject(static_cast<unsigned>(objectId)); }

    MapObject* FindObject(unsigned id);
    // Creates the object a .mob record describes (base.kind): map objects and units (1, 5, 8, 9; 6 with a
    // figure and maybe a campfire; 7) go into the object maps, the update map and the sector map; sounds (3),
    // lights (2) and kind 4 become timed objects. Returns the new id, -1 when refused or for an unknown kind (the
    // original returned the message's address there; no caller reads it). Rebirth's FixAddMapObject takes index 0 for a unit without a prototype record.
    int CreateObject(mapspec::Spec* message);
    // Removes object `id` from the server: out of the sector map, the id maps and the command queues,
    // forgotten by every object, the missiles and the players' states, the maps' reference released. An
    // unknown id crashes the original (null read); Rebirth's FixRemoveMapObject ignores it.
    void RemoveObject(unsigned id);
    Player* GetPlayer(int id);
    // Ground height from the server's coarse height grid; 0 outside.
    float GridHeight(float x, float y);
    // Living mercenaries of player `id`, not counting the leader (0 without the player).
    int CountLivingMercs(int id);
    // Whether object `objectId` is on the shared-object list for player `playerId` (or for everyone).
    bool IsObjectShared(int objectId, int playerId);
    void CollectSharedObjects(int playerId, ScriptGroup* group);
    void MarkPlayersDirty(std::uint32_t mask);
    // SetWind with the strength clamped to 0..1 (NaN: 0), then the players marked dirty (8).
    void ChangeWind(const Vector3& direction, float strength);
    // Setters of cave, climate (any weather stops first) and weather / weatherStrength (the players
    // marked dirty).
    void SetCave(int value);
    void SetClimate(int value);
    void SetWeather(int weather, float strength);
    // Exit `index` (its target zone and entrance); null when out of range.
    const ZoneExit* FindZoneExit(int index) const;
    // Registers an object that lives for a limited time (spells, effects) in timedObjects.
    void AddTimedObject(Effect* object);
    void RemoveTimedObject(unsigned id);
    void AddToUpdateMap(MapObject* unit);
    // The updates an object stays in the update map before it may sleep (updateCountdown): 0x80 plus
    // 0..15, which the game takes from bits 5..8 of the object's address (in several functions), so it
    // follows the heap's layout. Ours takes the low 4 bits of the object's id instead, and harness runs
    // on the virtual clock patch the original to do the same (tests/verify/harness_heap.h), so that
    // every run and both sides agree.
    static int UpdateCountdownFor(const EngineObject* object);

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
    void WakeUnitsAround(const Vector3& position, float radius);
    // Raises alarm `index` (0..4) at `point` unless it is already raised (`force` raises it again; alarm 0 with
    // `force` clears all five first), tells every AI unit without a player and wakes those that react. Script
    // InvokeAlarm.
    void InvokeAlarm(int index, const float* point, char force);
    // Records how the game ended (only the first time); results 2 and 3 also replace the network session
    // and query it (Query).
    void SetGameResult(int result);
    // Sends the unit's message 0x0B (the ack `kind`) to every player with `unit` among its party units.
    void TellParties(EngineObject* unit, AckKind kind);
    void TellPartiesGone(EngineObject* unit);
    // The items the player holds put into `party` and released. Nothing without a player.
    void StorePlayerItems(Player* player, class Party* party);
    void ClearPlayerItems(Player* player);
    void SetWaterLevel(int key, float target, int steps);

    // Replaces the zone exits with copies of `source`'s records that lead somewhere (target not "");
    // an exit without an entrance must lead to "none".
    void SetZoneExits(const std::vector<ZoneExit>* source);
    // Script AddUnitToServer: creates a unit of prototype `prototype` named `name`, with id `id` (the name's text
    // key), at `position`.
    void AddUnitToServer(unsigned id, const char* prototype, const char* name, const Vector3& position);
    // Reads the objects of a .mob file (chunked). Returns 1, or what FatalError returns when the file
    // cannot be opened or read.
    int LoadMapObjects(std::string_view file, int keepIds);
    // Loads .mob file `file` once (the loaded names are kept).
    int LoadMobFile(std::string_view file);
    // Removes every map object from the server, releases the missiles and the counted objects and clears
    // the countdowns.
    void ClearMapObjects();
    // Writes every map object to a save stream (id, prototype name, the object's Save, two flag words); -1 ends the
    // list.
    void SaveMapObjects(ByteStream* stream);

    // Restores the world from a mission save, in the order SaveWorld writes it; units that cannot stand where they
    // were saved are moved to a free cell or removed. `map` replaces the saved map name when not null. Returns 1, 0
    // when the AI graph cannot be read, or what FatalError returns when the zone cannot be loaded.
    int LoadSave(ByteStream* stream, const char* map);
    // Writes what LoadSave reads.
    void SaveWorld(ByteStream* stream);
    // Reads the map objects SaveMapObjects wrote, registers them in the id maps and wakes them.
    void LoadSavedMapObjects(ByteStream* stream);
    // Adds the time each player missed since its last update (plus `offset`) to its "gtime" variable.
    void UpdatePlayerTimes(float offset);
    // Puts a new entry at the head of the shared-object list: `id`, the session player's field for that id in a
    // multiplayer game (else -1), `value`, and references to the objects of `objects`.
    void AddSharedObject(int id, std::uint32_t value, const std::vector<Item*>* objects);
    // Before a save: every living unit refills its controller's slots and runs the controller's matching
    // virtual with 0.
    void PrepareSave();
    // Reads the zone's AI graph into the memory stream `out`: the 0x31415926 chunk of the .mob file when
    // there is one, else the whole <map>.grp file. 1 when found.
    int ReadAIGraph(std::string_view map, std::string_view mob, ByteStream* out);
    // The zone's scripts from the .mob file's 0xA000 chunk (its 0xACCEECCB text packed and XORed,
    // decoded by the game's routine, or the plain 0xACCEECCA text) become the only line of the first
    // command queue.
    void LoadZoneScripts(std::string_view mob);
    // While a game runs, every new connection the lobby hands over becomes a player of this game's kind, fully dirty,
    // starting at startPosition ("Accepted player <id>" goes to the network log).
    void AcceptNewPlayers(PlayerSource* lobby);

    // Loads map `map` (when it changed), the AI layer state from `graph` and the zone's .mob objects (the
    // same .mob starts the zone over, another one empties the world first; from a save the loaded objects are
    // cleared again). Returns 1, 0 when the map or the objects cannot be loaded.
    int LoadMap(std::string_view map, std::string_view mob, ByteStream* graph, char fromSave);
    // Empties the world of the current zone.
    void ResetZone();
    // ResetZone, then the map name, diplomacy, both command queues and the AI map are reset and the game state
    // cleared.
    void ResetState();
    // Before every game state request: every player leaves and the zone's contents go, then the base class's
    // teardown; the game result and kind are cleared.
    void TeardownZone();
    // A new zone from `map` and `mob`: the loading movie for `zone` shown, the AI graph read (a fatal error
    // without it), the map loaded; the zone name kept when given, scriptsActive cleared. 0 when a file is
    // neither where its name says nor in the maps folder, else 1.
    int NewZone(std::string_view map, std::string_view mob, std::optional<std::string_view> zone);
    // Unless the zone's map and .mob are these already: NewZone(map, mob, null), then TeardownZone.
    void EnsureZone(std::string_view map, std::string_view mob);
    // `unit` used one of `item` (a scroll): the item's count drops, and the count of the party's item it was made
    // from when the unit's player has a record for the unit.
    void UseItemUp(class Unit* unit, class Item* item);
    // After a fresh zone is loaded: the map objects that are not part of the map (IsSynchronised) move to
    // sentObjects and are sent, with zoneHeader, through a temporary player.
    void SendFreshObjects();

    // Casts `spell` from `caster` (or from `from`): one missile at `target` / `to`, or, for a spell with several
    // targets or area flags, at the nearest living units within its radius the caster may hit, the requested target
    // first. Network clients cast nothing with a caster.
    void CastSpell(Spell* spell, Unit* caster, const Vector3& from, MapObject* target,
                   const Vector3& to, int extra);
    // One missile of `spell` from the caster (or `from`) to the target (or `to`, on the ground there);
    // kept on the missile list unless its first update ends it. A hostile spell turns the target unit
    // against the caster.
    void CreateMissile(Spell* spell, Unit* caster, Vector3 from, MapObject* target, const Vector3& to,
                       int extra);

    // `player` takes what `object` holds: a shared loot record (flags 0x20000000, removed afterwards) or what the
    // collector gathers; the gold is added to the player. False for no player, a living owned unit, or nothing to
    // take.
    bool Loot(Unit* unit, Player* player);
    // The local player (id 0) loots every dead unit, which is then removed. Nothing without a local player.
    void LootDeadUnits();

    // The weather, every 64 ticks: the day-time value; in single player rain starting / stopping,
    // lightning strikes in storms (climate 3), bolts in the sky while it rains; the day factor; every
    // object's weather virtual; the players' "gtime".
    void UpdateWeather();

    // For every player and the ten mercenary slots ("merc1".."merc10"), sets the briefing variables b.mercN.* from
    // whether the mercenary is in the party, was hired, and how many others are alive.
    void UpdateMercBriefings();
    // Briefing `name` ("a.b.<record>") is complete for player `playerId`: a network client asks the server; otherwise
    // the player gets the record's experience, gold and items, loses its quest items, its variables are set, and the
    // scripts get GSSetVar / #OnBriefingComplete (also on a network client with EI_Plugin's Briefings).
    void CompleteBriefing(int playerId, std::string_view name);
    // The end of CompleteBriefing: GSSetVar / #OnBriefingComplete for `name` queued, run at once in
    // kind-3 games.
    void QueueBriefingScript(int playerId, std::string_view name);
    // The mercenary side of a completed briefing `name`: per slot b.mercN.n8_N clears the variable, n10_N removes the
    // mercenary, n2_N removes it from the party, n1_N / n3_N hire it (amerc_N = 2); then UpdateMercBriefings.
    void ApplyMercBriefing(Player* player, std::string_view name);
    // Gives `amount` experience: with a player, shared equally by its living heroes (units and party
    // records); without one, shared by the players that have a single hero, in proportion to the sqrt of
    // two of each hero's controller stats. Zero (or NaN) does nothing.
    void AwardExperience(Player* player, float amount);
    // Brings the player's party records up to date with its living units (items, spells and statistics rebuilt from
    // the unit).
    void SyncPartyRecords(Player* player);
    // Before a zone change: every record of `records` notes its unit's leader, gets a placement (if `placements`), is
    // rebuilt from its unit, and the unit is removed (a dead one just loses its owner).
    void StoreParty(std::vector<PartyUnitRecord*>* records, Party* partyRecords, std::vector<UnitPlacement>* placements);
    // The player's party leaves the zone: gtime, its items, then (single player and kind 3, with
    // zoneLoaded) its records without a unit are dropped and the rest stored (StoreParty).
    void CollectParty(Player* player, std::vector<UnitPlacement>* placements);

    // Takes quest items (class 0x14) out of `items` and returns them: in single player only those whose
    // description names this zone or whose id an object of the zone asks for; otherwise all.
    std::vector<Item*> TakeZoneQuestItems(std::vector<Item*>* items);
    // Gives the player fresh copies of the items it brings into the zone (all of them on a client, the
    // zone's quest items otherwise; nothing in a network game).
    void RestorePlayerItems(Player* player);
    // The party arrives in the zone (single player and kind 3, with zoneLoaded): its units are created at the
    // entrance's centre, facing away from the map centre or along the exit's heading; `runScripts` runs the script
    // command queue five times.
    void Redeploy(Player* player, std::vector<UnitPlacement>* placements, char runScripts);
    // A new item like `item`, made by the item factory and filled by the item's fill virtual (the second one for
    // spells), with its count; one below its maximum goes on counted once.
    Item* CopyItem(Item* item);
    void AddCounted(EngineObject* item);
    Item* CreateItemNamed(const char* name);
    Item* CreateSpellNamed(const char* name);

    // Creates a unit for each record of `records` (an existing object with the record's id is removed first), at its
    // entry in `placements` or in a line from `position` along `rotation`, moved to a free cell, under `player`'s
    // control, with its items, spells and AI; heroes get their figure parts, followers their leaders, and with a
    // player the units join the "Heroes" script group.
    void CreateUnits(Player* player, std::vector<PartyUnitRecord*>* records, const Vector3& position, Quaternion rotation,
                     std::vector<UnitPlacement>* placements);
    // A quick item (potion) is used by `unit`: the effect object "st.tr.130" (model "unhuma", type 9, flags 0x3010)
    // is created at the unit's position and flagged new (flags |= 0x180000). `item` is not used.
    void UseQuickItem(Unit* unit);

    // Brings `player`'s view up to date (once per world update): the zone when it changed, weather, time, wind and
    // countdowns when dirty, then the map objects and timed objects it should see. Returns what the player state's
    // flush returns (0 drops the player), 1 for a player that is leaving.
    int SyncPlayer(Player* player);
    // Sends `player` the zone to load (tick, map and .mob names, exits, the zone's units) and, for a new
    // game, where it starts. 0 when the player state refused it.
    int SendZone(Player* player);

    // WorldDayTime's value: the script's IsNight reads it, the units' movement factors scale by it.
    float nightLevel;
    std::uint32_t weatherFactor;          // float bits: 1 dry, 0.9 in rain or snow (the units' fourth movement factor); the harness's digest reads it as bits
    WorldAlarm alarms[5];
    // The zone's weather kind (its record's climate): 3 storms with lightning strikes, 1 light rain,
    // other non-zero values heavier weather; 0 none. Single-player only.
    int climate;
    int cave;                             // the zone is a cave (its record's cave; sent with the zone)
    int weather;                          // 0 clear, 1 rain, 2 snow (the script's SetWeather)
    float weatherStrength;                // sent with the weather
    std::vector<ZoneExit> exits;
    std::uint8_t loaded;                  // 0 while LoadSave runs
    MapObjectIdMap updateMap;
    // CreateObject makes objects answering IsSynchronised, sounds, lights and particle sources only with
    // loadedFresh set, the other map objects only with keepsState set; a refused object is released.
    std::uint8_t loadedFresh;             // the zone is not loaded from a save
    std::uint8_t keepsState;
    std::string zoneName;                // the zone's map database name
    std::vector<std::string> mobFiles;     // .mob files loaded in this zone (LoadMobFile)
    int gameResult;                       // 0 while the game runs
    std::uint32_t lastRainBoltTick;       // the tick of the last bolt in the rain (one per 20 ticks); saved
    Hoover* hoover;                      // counted; made by Setup, deleted by Teardown
    // The server's messages: the login handler reads "SrvErrMsg" / "WrongClientVersion" from it.
    FileRegistry messages;
    int zoneLoaded;                       // set by LoadMap, cleared by the teardown and a .mob file that fails to load
    int scriptsActive;                    // non-zero: Tick runs the zone's script (set by Redeploy)
    Vector3 startPosition;                // where joining players start
    AIMap aiMap;
    // Its globals hold the "Heroes" group.
    Script commandsA;
    Script* MainScript() { return &commandsA; }
    Script commandsB;
    Script* SecondScript() { return &commandsB; }
    Diplomacy diplomacy;
    // The session kind: 1 single player, 3 no range test, 4 network; 1 or 3: SaveWorld updates the
    // players' "gtime" first. Joining players take it (Player::sessionKind). Also the campaign mode
    // (in the server world): GameFlow::StartZone stores its mode here (1 battle, 2 main menu, 3 base,
    // 4 edge, 8 the end of the game), StartFirstMap 7 for the command-line script.
    int sessionKind;
    std::string mobName;                 // the zone's .mob file
    std::string mapName;                 // the zone's map
    MapObjectIdMap objects;               // every world object by id
    // The map objects SendFreshObjects sent (not part of the map itself; IsSynchronised).
    MapObjectIdMap sentObjects;
    MapObjectIdMap mapObjects;            // objects saved with the zone (SaveMapObjects)
    MapObjectIdMap otherMapObjects;       // the map objects that are not units
    MapObjectIdMap namedObjects;          // objects with a second name
    ObjectIdMap timedObjects;
    // The zone header SendFreshObjects sends to a temporary player.
    MemoryStream zoneHeader;
    // A list: a missile's hit can cast the struck unit's item spells (CreateMissile appends) while Tick walks it.
    std::list<HeldRef<SpellEffect>> projectiles;
    std::vector<EngineObject*> counted; // counted references (released on clear)
    std::vector<SharedObject> sharedObjects;
};

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

// The client world's platform set: the engine's CBridgeManager (class 0x34) with the list of the platform faces
// (AddFace adds at the head).
class PlatformSet final : public EngineObject {
public:
    ~PlatformSet() override;
    void Create() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Max(height, z of every platform surface whose area contains the point); the point is projected
    // onto each platform plane along z.
    float SurfaceHeight(const Vector3& point, float height);
    // Whether (x, y) lies on some platform (with a 0.1 margin).
    bool Contains(float x, float y);
    // The highest platform surface above 0 at (x, y) (with a 0.5 margin): its z and its normal; 0 and
    // (0, 0, 1) when there is none.
    struct SurfacePlaneResult {
        Vector3 normal;
        float height;
    };
    SurfacePlaneResult SurfacePlane(float x, float y);
    // Adds the face with corner `a`, edges to `b` and `c` and `normal` when normal.z >= 0.8: origin a,
    // unit axes (b - a) / |b - a| and (c - a) / |c - a|, the two lengths, the normal; *out = the new
    // record (its id, centre and radius are the caller's), null when the face is too steep. Returns 1
    // when added.
    int AddFace(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& normal, Platform** out);
    // The platform item `item` (offset, radius, the box's corners, pivot) of object `id` placed at
    // `position` with the rotation quaternion `rotation` (w, x, y, z): the box's corners and the six
    // face normals rotated, and every face that points up added with the id, the centre
    // R (pivot + offset) + position and the radius.
    void AddBox(int id, const FootprintPart* item, const Vector3& position, const Quaternion* rotation);

    std::vector<Platform> list;                  // the faces, the newest first
};

// The unit set the server's SendZone fills and the client reads back as its unit looks (unit_looks.h): the figured
// records grouped by prototype (each group a hash_multimap by three packed 10-bit values), and the dressed records
// by prototype. All three hash a key to itself; Write walks them in the game's order (the zone message's bytes).
struct UnitSet {
    using FiguredGroup = lib::HashMultimap<std::uint32_t, FiguredUnitRecord>;   // by scale key

    // Adds `unit`'s looks: its prototype, the figure's scale and, for a hero (MapObjectType::Hero), the item ids of its 7 worn
    // pieces and 4 weapons (0xFFFFFFFF for none or an id out of the tables) and its active weapon; other units get
    // extraWeapon 0xFF and the texts their companion holds. A figured record of the same prototype, scale key and ids
    // gets modelNumber -1, else the record (modelNumber the unit's id) joins its prototype's group; then the same for
    // a dressed record.
    void Add(Unit* unit);
    // Writes the set to `stream`: the number of figured records; per record its head, the morph
    // coefficients, its name count and each name with its terminator; the number of dressed records and
    // per record a 16-bit prototype, two counted dword arrays, a byte and a dword.
    void Write(ByteStream* stream);

    lib::HashMap<std::uint32_t, FiguredGroup> figured;            // by prototype
    lib::HashMultimap<std::uint32_t, DressedUnitRecord> dressed;  // by prototype
};

// Reads the zone's two unit look lists from `stream`: a count, then per figured record a 16-bit
// prototype, two counted dword arrays, a byte, a dword, the three morph coefficients and a counted list
// of names; then a count and per dressed record the prototype, the two arrays, the byte and a dword.
// Both vectors have the counts' sizes.
UnitLooks ReadUnitLooks(ByteStream* stream);

struct SectorGrid {
    // Nothing adds to a cell while it is walked (CollectVisible files one object; a first pass only appends
    // units to the deferred vector); Move erases from one cell and appends to another.
    std::unique_ptr<std::vector<HeldRef<MapObject>>[]> cells;
    std::unique_ptr<std::vector<HeldRef<MapObject>>*[]> rows;  // row y: its cells
    int allocatedWidth, allocatedHeight;    // the cells allocated (Resize: when they change)
    std::uint32_t width;                  // cells along x
    std::uint32_t height;                 // cells along y
    // allocatedWidth x allocatedHeight empty cells, then the row pointers.
    void Allocate();
    // When the storage has another size: Free, the size taken, then Allocate.
    void Resize(int width, int height);
    // The cells destroyed (from the last; each releases its objects), then the row pointers.
    void Free();
    // Moves `object` to `position` (stored in the object): when the cell (x and y rounded to nearest, >> 4)
    // changes, out of the old cell and to the end of the new one's (cells outside the grid are
    // skipped).
    void Move(MapObject* object, const float* position);
};

// The worlds' counted helpers: player sources (player_source.h; Query false, the client's Segment calls Pump every
// tick). The server's hands its pending link over once (TakeNew).
class WorldHandle final : public PlayerSource {
public:
    ~WorldHandle() override;

    void OnShutdown() override;
    WorldHandle* Clone() override;
    bool Query() override { return false; }
    void TakeNew(std::vector<HeldRef<netlink::NetLink>>* joined) override;

    Ref<netlink::NetLink> pendingLink;      // handed over by TakeNew
};

// The client world's player source (Setup makes it): it has no state of its own.
class ClientHandle final : public PlayerSource {
public:
    void OnShutdown() override {}
    ClientHandle* Clone() override;
    bool Query() override { return false; }
    // The client takes no connections: every link of the list is released and the list emptied.
    void TakeNew(std::vector<HeldRef<netlink::NetLink>>* joined) override;
};

// A window rectangle of the frame (the client's screen list and hit vector).
struct ScreenEntry {
    ClassId classId;
    unsigned objectId;                    // the object, in the map object manager
    Rect rect;                            // on the screen
};
// An object whose screen rectangle is valid (flags 0x20) and that is not hidden (0x40) puts its class id, id and
// rectangle at the end of `queued`.
int QueueScreenEntry(MapObject* object, std::vector<ScreenEntry>* queued);

// The functor of the for_each over the screen entries: CollectBounds / CollectParts push to `hits` every entry of
// `entries` whose rectangle holds (x, y) and whose object (found in `manager`) answers the bounds virtual
// (CollectBounds) or 24 (CollectParts) for the point.
struct HitCollector {
    HitCollector* CollectBounds(const std::vector<ScreenEntry>& entries, std::vector<ScreenEntry>* hits, std::int32_t x, std::int32_t y,
                                MapObjectManager* manager);
    HitCollector* CollectParts(const std::vector<ScreenEntry>& entries, std::vector<ScreenEntry>* hits, std::int32_t x, std::int32_t y,
                               MapObjectManager* manager);
    std::vector<ScreenEntry>* hits;
};

class LightningManager;
// The visibility fog's manager ("CVisFogManager", class id 0x60): a counted object and a list.
struct ClientArrayManager : BaseManager {
    ClientArrayManager();
    ~ClientArrayManager() override;
    void Create() override;
    void Destroy() override;
    bool RunTick() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    EngineObject* NewObject(int type) override;

    int Draw(SceneRenderer* renderer);

    Ref<Texture> texture;                  // the fog texture (counted)
    std::vector<EngineObject*> list;     // counted
};
// The manager's Clear (its object and every listed one released, the list emptied); a new fog object,
// counted, at the list end.
void VisFogManagerDestroy(ClientArrayManager* manager);
EngineObject* VisFogManagerAdd(ClientArrayManager* manager);

// The bits of EngineObject::flags on the client world. The constructor sets the draw bits but DrawShadows (the
// shadow setting decides it at Create).
enum class WorldClientFlag : std::uint32_t {
    NoCamOption = 0x8,      // cleared by the command line's "-nocam"; nothing sets or reads it
    RecordingCamera = 0x10, // a camera file is being recorded (Camera::OpenFile; the command line's camera options
                            // are skipped meanwhile)
    MoviePathOption = 0x20, // cleared by the command line's "-moviepath_"; nothing sets or reads it
    DrawObjects = 0x40,     // the objects but units
    DrawUnits = 0x80,       // the units and the selection markers
    DrawTerrain = 0x100,    // the ground and the water
    DrawShadows = 0x200,
    DrawEffects = 0x400,    // the alpha pass: the client arrays, the particles, the lightning lists, the rain and snow
    Draw800 = 0x800,        // set by the constructor; nothing reads it
    DrawSky = 0x1000,
    DrawNumbers = 0x2000,   // the flying hit numbers
};
template <> inline constexpr bool lib::kBitFlags<WorldClientFlag> = true;

// WorldClient::EnvironmentFlags: what an environment speech (acks.db) asks of the surroundings.
enum class EnvironmentFlag : std::uint32_t {
    MoveMode3 = 0x1,        // the unit's moveMode2 (1 walk, 2 run; 0 and 3 not named)
    Run = 0x2,
    Walk = 0x4,
    MoveMode0 = 0x8,
    Gipat = 0x10,           // the zone's map database name
    Ingos = 0x20,
    Suslanger = 0x40,
    Day = 0x80,
    Night = 0x100,
    Outdoors = 0x200,
    Cave = 0x400,
    Rain = 0x800,
    Snow = 0x1000,
    NoWeather = 0x2000,
};
template <> inline constexpr bool lib::kBitFlags<EnvironmentFlag> = true;

// The drawing side.
class WorldClient : public World {
public:
    WorldClient();
    ~WorldClient() override;

    // Sets the client world up: the managers, the lights table (LightsGipat.ini), the terrain, the session,
    // the rain and snow layer, the platforms and both sector grids. Returns 1.
    void Create() override;
    // Tears the client world down in the reverse order, then deletes the owned objects (counted ones dropped first).
    void Destroy() override;
    // The client's tick. True; false when the session's view has lost its connection (the session is
    // disconnected, FatalError; Rebirth's DisableExitOnCrashReport keeps the session).
    bool RunTick() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    const char* TexturePath() override;
    double GridHeightAt(float x, float y) override { return ClampedGroundHeight(x, y); }
    // The height of whatever surface is on top at (x, y): the ground, or the water when the point is submerged.
    double RawSurfaceHeightAt(float x, float y) override;
    // GridHeightAt / RawSurfaceHeightAt raised to the platforms over the point.
    double GroundHeightAt(float x, float y) override;
    double SurfaceHeightAt(float x, float y) override;
    MaterialManager* GetMaterialManager() override;
    TextureManager* GetTextureManager() override;
    BaseManager* ClientLightManager() override;
    ParticleManager* ClientParticleManager() override;
    void MoveObject(MapObject* object, const float* position) override;
    virtual void MoveObject2(MapObject* object, const float* position);   // the second grid's move

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
    // Lays a ground print's decal into the three arrays: the quad's corners projected along `direction` from
    // `point`; the terrain under it adds its triangles unless the point lies on a platform, and the platform faces'
    // objects the decal can touch add the triangles of each part of their figure (once per face).
    void ProjectDecal(const Vector3& direction, const Vector3& point, float depth, const VertexPacket& quad,
                      std::vector<VertexPacket>* points, std::vector<DecalTexCoord>* texCoords,
                      std::vector<std::uint16_t>* indices);
    // The ambient sounds' time of day: "day" from 6 to 18 o'clock (and for a NaN hour), "morning" before 6 and
    // "evening" after 18 within 2..22, else "night".
    const char* AmbienceTimeOfDay() const;
    // Nearest living unit of the local player's group that `unit`'s side can see (the candidate's
    // relation masks); null when none or `unit` is null.
    Unit* NearestVisibleUnit(Unit* unit);
    PlayerClient* LocalView();
    bool IsLocalViewMode2();
    // A new texture / material of class `type` from the world's managers; null without the manager.
    Texture* NewTexture(int type);
    Material* NewMaterial(int type);
    // Takes `camera` as the view camera: the old one loses a reference (it is not released at zero), the
    // new one gains one; either may be null.
    void SetCamera(Camera* camera);
    void SetWorldValue(int value);
    // Message 0x73: the water levels read from `stream` and applied to the terrain. Returns 1.
    int ReadWaterLevels(ByteStream* stream);
    // FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // Index of the exit whose area contains `point` and that leads somewhere.
    bool FindExitAt(const float* point, int* index);
    float* ExitCentre(float* out, int index);
    // One ZoneExit particle source (0x2038) per exit that leads somewhere, in exitMarkers at the exit's index (the old
    // markers released), then UpdateExitMarkers.
    void CreateExitMarkers();
    // Whether the party can leave through `exit`: every living unit of `records` (unit id in the
    // record) stands in an exit (FindExitAt) and that exit is `exit`. Those that do are listed in
    // `inExit` (emptied first). 0 when no unit lives.
    bool PartyInExit(const std::vector<PartyUnitRecord*>* records, int exit, std::vector<PartyUnitRecord*>* inExit);
    // Makes the bird when there is none, resets it and has it follow the unit of the local party's first
    // record.
    void SummonBird();
    // A quest variable "q.<quest>.<name>" changed to `value`: every filed object whose name is <name>
    // (the text after the second dot) gets the visibility level `value`; a value of 2 (or NaN) with a
    // <name> without a dot summons the bird (as SummonBird).
    void OnQuestVariable(std::string_view name, float value);
    // The party variable of an object's name: "q.<the local player's zone>.<name>".
    std::string ObjectVariableName(std::string_view name);
    // Every filed object with a name gets the visibility level of its variable (ObjectVariableName) in the
    // local session's variables; run when the party variables arrive.
    void ApplyObjectVariables();
    // The EnvironmentFlag bits of the current environment for `unit`: its move mode, the island, day or night, a
    // cave or not, the weather.
    std::uint32_t EnvironmentFlags(Unit* unit);

    // Files `object` (null skipped) in the frame's draw lists when it has a model inside the view frustum;
    // with `animateUnits` 0 a unit only goes on the deferred list, `withScripted` lets heroes with a text-key
    // id through. Returns 1 when the object was queued for the model-touch list, else 0.
    int CollectVisible(MapObject* object, const Matrix4* view, float animTime, std::uint32_t now, char animateUnits,
                       char withScripted);
    // The ground point under screen pixel (x, y): inverts the current view matrix and iterates on the
    // terrain height (Terrain::HeightAt). Returns out (x, y, z).
    Vector3* PickGround(Vector3* out, int x, int y);
    Vector3* PickSurface(Vector3* out, int x, int y);

    // Files an object in the client's maps (objects, objectGrid, heroes for heroes with a text-key id; the
    // dynamic lists for IsSynchronised objects; platform items for classes 0x54 / 0x5D); units fade in
    // through their controller in a multiplayer session of kind 1. An id already filed is only referenced and
    // released. Returns 1.
    int AddObject(MapObject* object);
    // When an object with `object`'s id is filed (objects): its platform faces are removed and, for
    // classes 0x54 / 0x5D, its platform items added again where it now stands (as AddObject).
    void RefreshPlatforms(MapObject* object);
    // Files `object` in dynamicObjects and dynamicNodes (as AddObject does) unless its id is there already.
    bool QueueSynchronised(MapObject* object);
    // Every filed object whose kind has no shadows drops its shadow (after an options change).
    void DropShadows();
    // Empties the selection list, each listed object's highlight off when it was on.
    void Deselect();
    // Adds `object` (not null) to the selection list: its part bounds refreshed, the highlight on when it was off,
    // and its id appended unless it is listed already.
    void Select(MapObject* object);
    int DrawLineLists();
    // One more reference on every item and, when the item factory exists, on every spell.
    void ReferenceItemsAndSpells();
    // The reverse, used by the teardown: every item and spell released, each manager emptied.
    void ReleaseItemsAndSpells();
    // The items and spells replaced: all released, then (a block with data) the managers loaded and referenced again.
    // Returns 1.
    int LoadItemsAndSpells(ByteStream* block);
    // The block read from its start: the spells and then the items it lists (LoadManagerObjects). 0 when
    // the block fails.
    int LoadNewItemsAndSpells(ByteStream* block);
    // Takes the object `id` out of the maps (platform faces, dynamic lists, heroes, its grid cell and objects); the
    // object is released.
    void RemoveObject(int id);
    // Empties the maps, the session's player data and the managers' records.
    void ClearMaps();
    // The zone's leftovers dropped: the exit markers, the second grid's cells, the timed objects, the bird.
    void ResetZoneObjects();
    // The zone's state block (docs/notes/world.md, "Zone state block"). Returns 1, or the fatal error's
    // result when the map does not load.
    int LoadZone(ByteStream* block);
    // The local session takes a new link: the single-player loopback pair for `player`, or a driver for the game
    // client's first connection (false without an active client or connection). Each empties the maps and
    // reconnects the session.
    void SetPlayer(WorldHandle* player);
    bool JoinGameClient();
    // The player sources and the network session pumped, then the local player's link (when it is
    // active) asked HasIncoming / IncomingCount; false / 0 without one.
    bool PumpHasIncoming();
    int PumpIncomingCount();
    // The local view's owner disconnects. Without a session it reads through null, like the original.
    void DisconnectLocalPlayer();
    // The object at screen point `point`: the screen entries that contain it and whose object answers CollectBounds
    // or CollectParts go to hits, sorted front to back; the first entry whose object still exists wins (hitCursor
    // is left on it). Null if none.
    MapObject* PickAt(const Point* point);
    MapObject* PickAt2(const Point* point);
    // The unit at screen point `point`: from PickAt2's object on, the first unit among the sorted hits
    // (hitCursor moves past the others and those whose object is gone); failing that the same from
    // PickAt's. Null if none.
    Unit* UnitAt(const Point* point);
    // The same from PickAt2's object only (no second pick).
    Unit* UnitAt2(const Point* point);
    // Every object of the frame's window list that still exists and whose side half has a drawn part
    // meeting the screen rectangle `rect`.
    std::vector<MapObject*> UnitsInBox(const Rect* rect);
    // Marks the footprints within their scale times `range` of the point (x, y).
    void MarkFootprintsInSight(const float* point, float range);
    // The wind: stored, given to the terrain's waves and to the rain and snow layer.
    void ApplyWind(const Vector3& direction, float strength);
    // A number over the unit with id `id`; nothing for another object or without them.
    void ShowNumber(int id, int value, std::uint32_t kind);
    // Used in the client's game state 3: every unit has its controller load the face texture.
    void LoadUnitFaces();
    // The target zone of exit `index` (the original copies the exit out of the array and returns the
    // copy's text, which shares the element's buffer).
    const char* ExitTarget(int index);
    void RemoveUnitsOf(const std::vector<PartyUnitRecord*>* records);
    int SendFlag10(int value);
    // The camera looks at the point (x, y) at its own focus height, with its view applied and kept above
    // the objects. Returns 1.
    int CentreOn(const Vector3& point);

    // Creates the party's heroes from their records: the figure from the part names (the hair "hr.%.2d" by the
    // prototype's number), items, player; each unit is filed with AddObject. Armour of type 0 is put on only with
    // `allArmour`. Stops at the first unit that cannot be created or given a figure.
    void CreatePartyUnits(const std::vector<PartyUnitRecord*>* party, char allArmour);
    // Every object of the sector grid's cells around `point` whose (x, y) lies within radius +
    // hypot(rangeX, rangeY) of the point. The cell range (truncated
    // and clamped to the terrain) and the walk's cursor are left in rangeFirstX .. rangeY. The
    // original's z difference is the point's z minus itself (0, kept).
    std::vector<Object3D*> ObjectsInRange(const Vector3& point, float radius);
    // Reads the light colours from config\<file> (an .ini, LightsGipat.ini): each key of [sunlight],
    // [ambient] and [sky] names its hour in characters 4..5 ("hour07") and holds "r, g, b" (0..255,
    // scaled by 1/255, alpha 1); the hours between two keys in file order are filled by linear steps
    // from the earlier key (the first from hour 0). Hours are not range-checked. Returns 1.
    int LoadLights(const std::string& file);
    // The frame's light from the time of day: fog, sun direction and the sunlight, ambient and sky colours
    // interpolated between the hour's table entries; once per new hour the terrain's sun. A cave takes hour
    // 0's colours and a fixed sun. Returns 1 when a new hour began (or the renderer's flag bit 0 is clear:
    // nothing done), else 0; no caller reads it.
    int UpdateLighting();
    // With one marker per exit (exitMarkers, ZoneExit particle sources): a marker whose target zone's
    // party variable "z.<zone>" is 1 (or NaN) gets extent (1, 0, 0) (its particles turn red), the
    // others (0, 0, 0).
    void UpdateExitMarkers();
    // The graphics set up on `device`: the textures, platforms and sky, the digit sprites (numbers), a new
    // figure loader in figureLoader (counted, the old one released) and the sound file counts.
    void SetUpGraphics(TextureDevice* device);
    // Once (skyDome null): a new Sky with a SkyObject (class 0x44, id -666) "nask0" loading
    // "nask0sky.fig". Without the file the object is referenced and released again.
    void LoadSky();

    int nextClientObjectId;               // -100000 after Setup; NextClientObjectId counts it down
    std::vector<ZoneExit> exits;
    // The exits' particle markers, one per exit or null (counted; CreateExitMarkers / UpdateExitMarkers).
    std::vector<HeldRef<ParticleSource>> exitMarkers;
    std::vector<HeldRef<MapObject>> heroes;                  // heroes with a text-key id (counted)
    std::vector<MapObject*> objects;              // every filed object (one reference each)
    // The objects answering IsSynchronised, with their nodes by id in dynamicNodes and the highest id
    // in dynamicMaxId (Segment drops those whose update returns 0). A list: dynamicNodes keeps its iterators.
    std::list<MapObject*> dynamicObjects;
    std::unordered_map<std::uint32_t, std::list<MapObject*>::iterator> dynamicNodes;   // id -> its node in dynamicObjects
    int dynamicMaxId;
    std::vector<ScreenEntry> screenEntries;  // the window rectangles of the frame
    std::vector<ScreenEntry> hits;                      // the entries under the point (PickAt)
    ScreenEntry* hitCursor;               // PickAt's walk, left on the entry it returns
    lib::HashMap<unsigned, LineBatch*> lineLists;    // line lists by key (DrawLineLists: drawn in STLport's order)
    int lastLineListKey;                  // the last line list key handed out; 0 from the constructor
    SectorGrid objectGrid;                // the objects by sector (MoveObject moves them)
    std::uint32_t rangeFirstX, rangeFirstY, rangeLastX, rangeLastY, rangeX, rangeY;
    SectorGrid secondGrid;                // (MoveObject2 moves objects in it)
    terrain::Terrain* terrain;            // counted
    // The zone's allod in lower case ("gipath" after Setup; the zone load takes it from the map
    // database's record of the local player's zone, "gipat" without one). It names the terrain's
    // texture pages, the lights table and the music theme.
    std::string zoneName;
    MaterialManager* materialManager;    // counted
    TextureManager* textureManager;      // counted
    LightFactory* lightFactory;          // counted
    ParticleManager* particleManager;         // counted
    LightningManager* listManager;       // owned
    ClientArrayManager* arrayManager;     // owned
    struct MusicSystem* music;           // owned
    struct Sky* skyDome;                 // the sky (owned)
    EffectMoshka* bird;                  // the bird following the party (SummonBird), owned
    FlyingHitPoints* numbers;            // owned
    // Segment ticks the ground prints and colours the blood prints by the light.
    std::vector<BloodPrint> bloodPrints;
    std::vector<Footprint> footprints;
    std::vector<GroundPrint> scorchMarks;
    int lightLevelsCountdown;             // time left before the blood prints are lit again (Segment); 0 after Setup
    Ref<ClientHandle> handle;       // the client's player source
    SoundManager* sounds;                // the sound manager (owned; the client controller's hit sounds)
    TextManager* texts;                  // the text resources (counted)
    PlatformSet platforms;
    // The local session: the local Player; its connection is the local view (a PlayerClient), its
    // sessionKind 1 / 3 for kinds of multiplayer session.
    Player* session;
    Camera* camera;                      // the view camera (DrawPass, Render)
    std::string texturePath;             // <data path>textures\ (Setup)
    // The light colours by hour (0..23; r, g, b, 1) from the lights table's [sunlight], [ambient] and
    // [sky] sections (LoadLights).
    LightColour sunlight[24];
    LightColour ambient[24];
    LightColour sky[24];
    // This frame's light (UpdateLighting): the ambient colour (the table's, or 0.8 x the sunlight when
    // the table's is black; its fourth float is never written), a copy of it (four floats, the sky's
    // ambient) and the sunlight (its fourth float is whatever the original's stack held).
    LightColour frameAmbient;
    LightColour skyAmbient;
    LightColour frameSun;
    int lightingMark;                     // 0 after Setup, 0x80 once UpdateLighting runs outside a cave; never read
    // 1: a cave zone (the map's flag: LightsCave.ini, the sky "Sky01", light fixed at hour 0;
    // EnvironmentFlag::Cave instead of Outdoors).
    int cave;
    int partyDanger;                      // 0 after Setup; the party state's danger (message 0x60: 2 fighting)
    int ticksRun;                         // Segment's count; AddObject fades units in when >= 1
    float tickFraction;                   // added to the tick for the units' time (network client)
    // The rain / snow layer, built and destroyed with the world. Its `mode` (1 rain, 2 snow) picks the ground
    // prints' wet look and an environment flag.
    RainSnow rainSnow;
    std::vector<int> selectedIds;            // the selected objects' ids
    int worldValue;                       // -1; message 0x12 sets it (SetWorldValue); the exe never reads it
    // The window's names, set by the constructor and freed by the destructor only: "EvilIslands",
    // "EvilIslandsClass", "EvilIslands", "EvilIslands Paused".
    std::string appName;
    std::string windowClass;
    std::string windowTitle;
    std::string pausedTitle;
};

// Orders screen entries (their objects by objectId) by distance along the view's depth axis; dead and
// living objects are separated first.
struct DepthLess {
    const float* view;                    // the view matrix (row-major): row 2 the depth axis, its translation the offset
    MapObjectManager* manager;
    bool Less(const ScreenEntry* a, const ScreenEntry* b) const;
};

bool IsMultiplayerClient();
// Game clock: whole ticks + fraction.
float GameTime();
class Party* LocalPlayerData();
// The local player's party's zone (a copy).
std::string LocalPlayerZone();
float LocalPlayerVariable(std::string_view name);
// Float variable `name` of the local player: the client's view in multiplayer, player 0
// of the server otherwise (0 without that player).
float PlayerZeroVariable(std::string_view name);

struct GameFlow {
    // Saves the game into saves\<slot>\ (single player only): empties the folder, copies the visited zones' files
    // from saves\current, writes scenario.sav and, outside the cutscene zones, mission.sav. Without scenario.sav the
    // game goes back to the main menu. Returns 1 when saved.
    int SaveGame(const std::string& slot);
    // Fills the zone table once, while it is empty: for every "*.mq" file of the maps folder, its title is registered
    // as a quest folder (RegisterQuests); a zone the map database knows gets an entry {the title, the record's map
    // name, the allod}: 0 / 1 / 2 by the three map name lists, else 3.
    void BuildZoneTable();
    // Used each network step. The local player's "z.MPGame1" variable, set to 1 or removed: on a client unless the
    // client session's field is active; on the host when more than one player is in (zoneSwitchStarted) or unless the client's
    // field is active. Nothing without an active client session.
    void UpdateMultiplayerVariable();
    // The multiplayer start zone for the client session's game type: "bz2mpg".."bz4mpg" for
    // 1..3, else "bz1mpg".
    std::string MultiplayerZone();
    // Starts a multiplayer game: the party manager is reset, a single party record gets the unit id
    // 0x3B9AF5CE, the party outside a game is sent to the start zone with "z.MPGame1" = 1 and
    // redeployed; the flow loads that zone (state 4) and the client's game state request becomes kind 1.
    void BeginMultiplayerGame();
    // The local player has seen `briefing`: for a name ending in "_N" the zone's quests are loaded first
    // (with the name without "_N"), then BriefingVariable's variable is set to 1.
    void MarkBriefing(const char* briefing);
    // With an active client session: the local player's party is copied into the party outside a game,
    // redeployed by the party manager, its map transfer sent, then the flow's update.
    void ReturnLocalParty();
    // With an active client session: the flow's update, then an answered game state request (kind 3)
    // becomes a new one of kind 0.
    void UpdateAndAcknowledge();
    void RequestBriefing(std::string_view briefing);
    void ForgetBriefing(const char* briefing);
    void RecreateClientSession();
    int SaveGameThunk(const std::string& slot);
    // Clears the sessions-open flag, releases both network sessions (server, then client), sets the state to 1.
    void CloseSessions();
    // Used by the menus and the interface on leaving a network game. As CloseSessions without the
    // state: cleared, the server and the client session released.
    void DropSessions();
    // Used by the hero screens. DropSessions, then the resources go back to the local database
    // (UseResources(false)).
    void EndSession();
    // The client's variable `record` (an element of ClientSession::variables) changed: "q." variables
    // go to the quest log and the client world. Only the argument is read.
    static void OnVariableChanged(const net::VariableMessage* record);
    // Registers a quest folder once: the flow loads its quests, then with "<folder>\quest.reg" in the
    // texts the quest table reads it, the map database loads "<folder>\map.txt" and the folder joins the
    // list.
    void RegisterQuests(const std::string& folder);
    // The multiplayer zone switch, in two calls: the first (zoneSwitchStarted clear) resets the host's
    // connections, takes the party (player 0's records when its state has a party, else the outside
    // party) for multiplayerZone, asks for game state 3 and requests state 6 (host) or 5 (client, to its peer's
    // zone); the second (zoneSwitchStarted set) asks for state 1 and the start zone again, and the host drops its
    // client.
    void SwitchMultiplayerZone();
    // Hosts a multiplayer game: new server (game type) and client sessions, the client connects to
    // 127.0.0.1 with the password (20 bytes) as hello, then the server's name ("Server" when empty),
    // maxPlayers, password, listing (not `unlisted`) and masterKey are set (both between calls patched by
    // Rebirth).
    void StartServer(const char* name, int maxPlayers, int gameType, const char* password, bool unlisted, int masterKey);
    void StartClient();
    void JoinServer(const net::NetAddress* address, const char* password);
    // The flow's requests: the state (state) the flow's update acts on next, the zone or save name
    // (requestTarget) and, for 5 and 6, a copy of a party (requestParty).
    void RequestState3();
    void RequestLoad(std::string_view name);                              // 2
    void RequestZoneChange(std::string_view zone);                        // 4
    void RequestPartyZone5(const char* zone, Party* party);
    void RequestPartyZone6(const char* zone, Party* party);
    void DisconnectClient();
    // Starts `zone`: the current zone is torn down; mode 0 takes the mode from the zone record's kind
    // (0 -> 1, 1 -> 4, 2 -> 3), "endofgame" is mode 8, mode 2 loads the main menu zone (also when the
    // zone is unknown, after FatalError and closing the network sessions); otherwise the party enters
    // the zone and the clock follows its "gtime". The mode becomes the campaign mode (the server world's
    // sessionKind). Rebirth's AllowScriptsInMainMenu sets the server's scriptsActive in the menu.
    void StartZone(std::string_view zone, int mode);
    // A new game: "exec autoexec.cfg" on the console, the party manager's new game, saves\current
    // emptied, then the party's zone started (mode 0). Rebirth's AutoExec runs addon's AutoExec lines
    // first.
    void StartNewGame();
    // Acts on the pending request (state, then cleared): the zone being left is saved, the interface
    // closes its windows, the zone is torn down and the resource database follows the network state;
    // then 1 main menu, 2 load the save named in requestTarget (two ticks first), 3 new game (StartNewGame), 4 the zone in requestTarget, 5 / 6 multiplayer zone
    // changes with the party copy in requestParty. Ends with the network pump.
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
    int LoadGame(const std::string& slot);
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

    class Party* RequestParty() { return &requestParty; }

    // saves\current emptied, the multiplayer zone (multiplayerZone) named; a host without an active client
    // session starts it when there is a name.
    void SetNetworkZone(const char* zone);

    int state = 0;                        // the pending request (1..6; ProcessRequest runs it)
    std::string requestTarget;            // the request's zone or save name
    Party requestParty;
    std::string multiplayerZone;          // the multiplayer zone (SwitchMultiplayerZone)
    char zoneSwitchStarted = 0;           // set by SwitchMultiplayerZone, cleared by the game's start
};

extern GameFlow g_gameFlow;

// A multiplayer hero record (the saved parties' entries below, the hero screens' records): its party
// (Party) and file name.
struct MpHeroRecord {
    // A copy copies the party (Party's copy constructor and Assign, which read the source non-const).
    MpHeroRecord();
    MpHeroRecord(const MpHeroRecord& other);
    MpHeroRecord& operator=(const MpHeroRecord& other);
    ~MpHeroRecord();
    // Writes the party to `folder` + file: a file name with no '.' or with anything but digits before
    // the last one is deleted instead. Otherwise the old file is moved to `folder` + "temp.mp", the
    // party saved (with its variables) into a memory stream and that written as a save file (magic
    // 0x114, the save version), then the temporary file deleted.
    void WriteFile(const std::string& folder);
    // Reads the party from `folder` + file (magic 0x114, the versions kSaveOldestVersion..kSaveVersion):
    // false when the file does not read, else the party loaded (with its variables) and the memory
    // stream's ok flag.
    bool ReadFile(const std::string& folder);
    Party* GetParty() { return &party; }

    Party party;
    std::string file;
};

// The saved multiplayer parties.
struct SavedParties {
    // Rebuilds the list from the folder's "*.mp" files: each is read into party records; a loaded one
    // has its variables emptied and, with a record's field == 1, is appended. Rebirth's
    // PersistentGlobalVars keeps the "gvar." variables.
    void Reload();
    void SaveAll();
    void Select(const MpHeroRecord* hero);
    // Room for one more record the way the original's vector grew: doubled, the old records copied before the
    // new one is made. A record's copy loads its party again, which draws object ids and random numbers.
    void ReserveOne();
    int Count() const { return static_cast<int>(entries.size()); }
    MpHeroRecord* Hero(int index) { return &entries[index]; }

    std::string folder;
    std::vector<MpHeroRecord> entries;
    MpHeroRecord outsideGame;
};
extern SavedParties g_savedParties;
// The party of the record kept outside a running game (the multiplayer hero).
inline Party* GlobalParty() { return g_savedParties.outsideGame.GetParty(); }

int RequestZone(std::string_view zone, int entrance);

// With the briefing texts loaded: every quest text file loaded so far (g_questFiles) is unloaded and
// the list emptied.
void UnloadQuestTexts();
extern std::vector<std::string> g_questFiles;

// Loads the quest texts of `zone`: false without the briefing texts or when the maps folder has no
// "<zone>.mq". A running game server first lists the file's bytes under "<zone>.mq" for its clients
// (ShareFile); then a path not yet among the quest files joins them and the briefing texts add it as a
// source.
bool LoadZoneQuests(std::string_view zone);

bool NameInList(const char* const* list, std::string_view name);

void ClearSaveFolder(const std::string& path);

// For the zone table entries with `id`, grouped by their second name in order of first appearance: one
// random entry per group (never the one whose first name is `exclude` when there is a choice; one
// IsaacRandom draw per group) has its first name returned.
std::vector<std::string> PickZones(int id, const char* exclude);

// Reads mission.sav from a file stream (magic 0x102) and restores the world (LoadSave); old versions
// also carry the globals (< 0x52) and the input state (< 0x47). 0 when the file does not read.
int LoadMission(ByteStream* file);

// SaveFileWrite writes the header, at version > 0x6B a checksum, then a flag byte and the LZ-compressed data
// (or, when decompressing it does not give the data back, 0 and the raw data). SaveFileRead reads one back
// (versions up to kSaveLastUncompressedVersion uncompressed); false on a bad header or checksum.
void SaveFileWrite(ByteStream* file, unsigned magic, MemoryStream* data);
bool SaveFileRead(MemoryStream* out, unsigned magic, ByteStream* file);
inline constexpr int kSaveVersion = /*00790260*/ 116;
inline constexpr int kSaveLastUncompressedVersion = /*00790264*/ 100;
inline constexpr int kSaveOldestVersion = /*00790268*/ 66;

// The mercenary who speaks `briefing` (the second word of the first "#show" line of
// section "briefing <name>" that is not "Hero"), or "". The briefing's
// variable, "b.<speaker>.<briefing>" or, without a speaker, "b.elder.<briefing>".
std::string BriefingSpeakerName(const std::string& briefing);
std::string BriefingVariable(const std::string& briefing);
// The multiplayer peer's map (the client's session field or the server's), replaced by the first line of its "quest
// <name>" section when there is one; "" without a peer.
std::string SessionMapName();
// Chooses the loading movie for zone `zone` (may be null) by its allod in the map database:
// progres2.bik for gipat, progres3.bik for ingos, progres4.bik for suslanger, else progres.bik
// (Rebirth's FixProgressNames: progres2.bik for gipat2, progres5.bik for jigran); the zones gz1g and
// bz7g (case-insensitive) take progres1.bik.
void SelectLoadingMovie(std::optional<std::string_view> zone);

extern std::vector<MapObject*> g_touchedModels;
extern std::vector<MapObject*> g_shadowTrees;
extern std::vector<MapObject*> g_shadowUnits;
extern std::vector<MapObject*> g_shadowLevers;
extern std::vector<MapObject*> g_shadowOther;
extern std::vector<MapObject*> g_shadowBuildings;
extern std::vector<MapObject*> g_visibleTrees;
extern std::vector<MapObject*> g_visibleUnits;
extern std::vector<MapObject*> g_visibleLevers;
extern std::vector<MapObject*> g_visibleOther;
extern std::vector<MapObject*> g_visibleBuildings;
enum class FrameList {
    TouchedModels,   // models to keep loaded (by last use)
    ShadowTrees,
    ShadowUnits,
    ShadowLevers,
    ShadowOther,
    ShadowBuildings,
    VisibleTrees,
    VisibleUnits,
    VisibleLevers,
    VisibleOther,
    VisibleBuildings,
};
inline std::vector<MapObject*>& FrameObjects(FrameList list) {
    static constexpr std::vector<MapObject*>* kQueues[] = {&g_touchedModels, &g_shadowTrees,     &g_shadowUnits,  &g_shadowLevers,
                                              &g_shadowOther,   &g_shadowBuildings,     &g_visibleTrees,    &g_visibleUnits,
                                              &g_visibleLevers,     &g_visibleOther, &g_visibleBuildings};
    return *kQueues[static_cast<int>(list)];
}
extern std::vector<HeldRef<MapObject>> g_deferredUnits;   // world_client_view.cpp

// A lightning strike from `from` to `to`, as a script line on the queue: one time in four only a
// thunder sound, otherwise a point light at the middle, a sound and two bolts.
void ShowBolt(Vector3 from, Vector3 to);

// With `plain` 0 the players' gold, party records and, on the server, their units' controller statistics are
// XORed with a new random key, with 1 they are XORed back; also the party records outside a game. Nothing
// when the state is already `plain`.
void GuardPlayerData(char plain);
void GuardPlayers(PlayerManager* players, char units, std::uint32_t key);

// Reads objects up to a zero byte: after each mark an id, a class and a field of the object; an object
// registered under the id is released first (its count raised to 1 when 0), then a new one is created
// (NewObject), loaded (Load) and referenced. 1 at the end mark, 0 when a mark is read from a failed
// stream.
int LoadManagerObjects(BaseManager* manager, ByteStream* stream);
// The parts of it a list of a class derived from EngineObject walks itself: one object's entry, and the end
// mark.
void SaveListedObject(EngineObject* object, ByteStream* stream);
void SaveListedObjectsEnd(ByteStream* stream);

// The save screens: 1000000 plus the sizes (low dwords) of the files in saves\current.
int CurrentSaveSize();

// Every resource database (units with their sound file counts, skills, spell tables, items) emptied, then
// loaded from `name`.
void SwitchResourceDatabases(const std::string& name);
