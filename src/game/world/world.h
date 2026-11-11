#pragma once

// The world: the server side that runs the game rules (WorldServer) and the client side that draws it and answers
// terrain queries (WorldClient), both derived from a common base. Class names come from the game's error strings.

#include "game/saves/client_visuals.h"
#include "engine/core/core_refcount.h"
#include "game/effects/effects.h"   // RainSnow
#include "engine/core/engine_object.h"
#include "game/app/singletons.h"
#include "game/terrain/ground_print.h"
#include "lib/vecmath.h"
#include "game/world/diplomacy.h"
#include "game/map/map_database.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player_source.h"
#include "engine/data/registry.h"
#include "game/ai/unit_ai.h"   // AIEventTable (the server world's AI events)
#include "game/units/unit_looks.h"
#include "engine/core/named_floats.h"

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
// The weather checks after a change (a roll's or a script's SetWeather) before rain may start or stop again.
inline constexpr int kWeatherChangeDelay = 10;


class IsaacRandom;
class ParticleSource;
class Player;
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
class Spell;
class SpellEffect;
class ParticleManager;
class VisibilityFog;
class Hoover;
class Effect;
class EffectMoshka;
struct MusicSystem;
struct Sky;
namespace mapspec { class Spec; }

class WorldServer;

// A hash map of counted objects emptied as the world's object maps are: every object up to the first empty entry
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
struct EffectIdMap {
    lib::HashMap<unsigned, Effect*> map;
    int maxId = 0;

    void Set(const unsigned& key, Effect* const& value);
};

struct MapObjectIdMap {
    lib::HashMap<unsigned, MapObject*> map;
    int maxId = 0;

    void Set(const unsigned& key, MapObject* const& value) {
        if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
        map[key] = value;
    }
};

// Walks the world's player list.
using PlayerIterator = ObjectListIteratorOf<Player>;

class FlyingHitPoints;

// A light colour of the client's tables (a is 1).
struct LightColour {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
};

class World : public ManagerContext {
public:
    explicit World(ContextKind kind);
    ~World() override;

    // Folders, managers, the ISAAC generator and the countdowns set up.
    void Create() override;
    // Every object of the spell manager gets ClearState, then the managers are deleted.
    void Destroy() override;
    bool RunTick() override;

    ItemConstructor* GetItemConstructor() override { return itemConstructor.get(); }
    SpellConstructor* GetSpellConstructor() override { return spellConstructor.get(); }
    ItemManager* ItemFactory() override { return itemManager.get(); }
    PlayerManager* PlayerList() override { return players.get(); }
    SpellManager* GetSpellManager() override { return spellManager.get(); }
    FigureManager* GetFigureManager() override { return figureManager.get(); }
    BaseManager* SpecificManager() override { return specificManager.get(); }
    LinkFigureLoader* FigureLoader() override { return figureLoader.Get(); }
    Item* DuplicateItem(Item* item) override { return MakeCopyOf(item); }

    // The world's own virtuals; docs/notes/map_objects.md, "Virtual slot names".
    bool IsServer() const { return contextKind == ContextKind::Server; }
    virtual float GridHeightAt(float x, float y) = 0;
    // The server's AI map height.
    virtual float RawSurfaceHeightAt(float x, float y) = 0;
    // The ground height and the water surface's height at (x, y): the server's call GridHeightAt / RawSurfaceHeightAt, the
    // client's also clamp.
    virtual float GroundHeightAt(float x, float y) = 0;
    virtual float SurfaceHeightAt(float x, float y) = 0;
    // The time of day becomes `hours` (the base is moved so that fmod(tick * secondsPerTick + base, 24)
    // = hours). The server's also updates the players' times first (game kinds 1 and 3), marks them
    // dirty (4) and updates the weather.
    virtual void SetTime(float hours);
    // Seconds per tick become 1 / speed (0: stopped; negative: the setting) times 1/15, keeping the time
    // of day (SetTime). The server's updates the players and weather as SetTime does.
    virtual void SetTimeSpeed(float speed);
    // The client's particle manager; null on the server's world (world_client.cpp; the texture and material managers:
    // ManagerContext's getters).
    ParticleManager* ClientParticleManager();
    virtual MapObject* ObjectWithId(int id);
    // `object` moves to `position` (the client's sector map).
    virtual void MoveObject(MapObject*, const float*) {}

    // The game time: tick * secondsPerTick.
    float Elapsed() const;
    // The time of day in hours: fmod(Elapsed() + timeBase, 24).
    float TimeOfDay() const;
    // Whether the hour of the day is before 6 or from 22 on (a NaN counts as night).
    bool IsNight();
    // The time of day in hours and the time speed (1 / (seconds per tick * 15)), both stored as floats.
    struct Clock {
        float hours = 0.0f;
        float speed = 0.0f;
    };
    Clock GetTime();
    void SetWind(const Vector3& direction, float strength);
    Unit* FindUnit(int id);
    // How dark the time of day is: 1 before 8 o'clock (and for a NaN), 0 until 20, then rising to 1 by midnight.
    float NightLevel();
    // A new empty network session shared into the world's session pointer, the previous one destroyed.
    void OpenSession();
    void CloseSession();
    // Stores paused. SetServerPaused is the server's entry: a pause is a plain return in a hosted game whose pause mode
    // is Off (net::PauseMode; EI_Plugin's Pause 3 returned for a resume too, so a world paused before stayed paused).
    void SetPaused(std::uint8_t value);
    void SetServerPaused(std::uint8_t value);

    float timeBase = 0.0f;
    float secondsPerTick = 0.0f;
    Vector3 windDirection;                // saved and loaded
    float windStrength = 0.0f;            // 0..1; saved and loaded
    Ref<LinkFigureLoader> figureLoader;    // the client's from SetUpGraphics; FigureLoader returns it
    // The managers (Create makes them, Destroy deletes them in its order).
    std::unique_ptr<PlayerManager> players;
    std::unique_ptr<FigureManager> figureManager;
    std::unique_ptr<MapObjectManager> objectManager;    // map objects by id (its NewObject creates one)
    std::unique_ptr<BaseManager> specificManager;       // the side halves (the client's or the server's)
    std::unique_ptr<BaseManager> timedObjectManager;    // the timed objects (spells, effects)
    std::unique_ptr<SpellManager> spellManager;
    std::unique_ptr<SpellConstructor> spellConstructor;
    std::unique_ptr<ItemManager> itemManager;
    std::unique_ptr<ItemConstructor> itemConstructor;
    // A counted helper World::Create shares in.
    Ref<WorldHandle> worldHandle;
    Ref<netlink::DriverSession> netSession;    // the network session (OpenSession)
    int tick = 0;                         // game time in ticks
    int updateCount = 0;                  // world updates run (the server's Update)
    // Paused: the server's Update skips the players and the tick while it is set; the client's Segment
    // skips its own ticks, and the client copies the server's flag from message 0x60, telling the HUD.
    std::uint8_t paused = 0;
    WaterTweens countdowns;
};

// One of the five alarms InvokeAlarm raises (the script's IsAlarm / AlarmTime / AlarmPosX / AlarmPosY).
struct WorldAlarm {
    std::uint8_t active = 0;
    std::uint8_t unused[3] = {};          // saved: the alarm is written whole
    int time = 0;                         // tick it was raised
    float x = 0.0f, y = 0.0f;
};
static_assert(sizeof(WorldAlarm) == 16);   // saved as its 16 bytes
constexpr bool NoPadding(const WorldAlarm*) {
    return lib::FieldsFill<&WorldAlarm::active, &WorldAlarm::unused, &WorldAlarm::time, &WorldAlarm::x,
                           &WorldAlarm::y>();
}

// The world the missiles find while they load.
inline WorldServer* g_loadingProjectileWorld = /*007C84EC*/ {nullptr};

// An entry of the world server's shared-object list: the loot a unit left.
struct SharedObject {
    int objectId = 0;
    int player = 0;
    std::uint32_t money = 0;                // gold the loot adds to the finder's party
    std::vector<HeldRef<Item>> objects;    // counted

    // The entry: id, player, value, then a count and the object ids. Load resolves them by the item manager;
    // each found object is appended with a reference.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;
};

// What the server world runs (WorldServer::gameState; the saves do not hold it).
enum class GameState {
    None = 0,        // no zone loaded yet
    Playing = 1,     // a single player game (a campaign zone, "endofgame" too)
    Network = 2,     // a network game
    Network3 = 3,    // handled like Network; nothing sets it
    MainMenu = 4,    // the main menu zone
};

// The campaign mode of the zone (WorldServer::zoneMode; the saves hold the number). Battle and Base are the
// modes of a single player game: their players keep their own time, the leader's death ends the game, and
// Base skips the range test.
enum class ZoneMode {
    None = 0,        // nothing loaded; as a StartZone argument: the mode comes from the zone record's kind
    Battle = 1,      // a zone record of kind 0, and every network zone
    MainMenu = 2,
    Base = 3,        // a zone record of kind 2
    Edge = 4,        // a zone record of kind 1
    Script = 7,      // StartFirstMap: the command-line script's zone
    EndOfGame = 8,   // the zone "endofgame"
};
inline bool IsSinglePlayerMode(ZoneMode mode) { return mode == ZoneMode::Battle || mode == ZoneMode::Base; }

// How far a unit hears: max(perception * movement factor 3, the minimum perception) * 2 + `margin` (the server half's
// statistics B0 values 0 and 2). SyncPlayer sends the units in range, the wake-up wakes them.
float HearingRadius(class Unit* unit, float margin);

class WorldServer : public World {
public:
    WorldServer();
    ~WorldServer() override;

    // The managers, the messages registry, the Hoover, the wind (1, 0, 0; strength 0.4) and the zone state
    // set up (a registry that cannot be opened ends the game).
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
    float GridHeightAt(float x, float y) override { return GridHeight(x, y); }
    // RawSurfaceHeightAt: the AI map's top height at (x, y, 0); the other two return GridHeightAt /
    // RawSurfaceHeightAt unclamped.
    float RawSurfaceHeightAt(float x, float y) override;
    float GroundHeightAt(float x, float y) override;
    float SurfaceHeightAt(float x, float y) override;
    void SetTime(float hours) override;
    void SetTimeSpeed(float speed) override;
    MapObject* ObjectWithId(int objectId) override { return FindObject(static_cast<unsigned>(objectId)); }

    MapObject* FindObject(unsigned id);
    // Creates the object a .mob record describes (base.kind): map objects, units and levers; flames (with a
    // figure and maybe a campfire) and traps go into the object maps (FileMapObject; the flames, traps and levers into
    // the update map too, all but the traps into the sector map); sounds, lights and particle sources become timed
    // objects. Returns the new id, -1 when refused or for an unknown kind (the
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
    // Setters of cave, climate (a change of climate stops any weather first) and weather / weatherStrength (the
    // players marked dirty).
    void SetCave(int value);
    void SetClimate(int value);
    void SetWeather(int weather, float strength);
    // Exit `index` (its target zone and entrance); null when out of range.
    const ZoneExit* FindZoneExit(int index) const;
    // Registers an object that lives for a limited time (spells, effects) in timedObjects.
    void AddTimedObject(Effect* object);
    void RemoveTimedObject(unsigned id);
    void AddToUpdateMap(MapObject* unit);
    // Files a map object CreateObject made: the object maps (the other-objects map for all but units), the update map
    // when `updated`, the sector map when `inSectorMap`; then writes its changed state when one is pending.
    void FileMapObject(MapObject* object, bool updated, bool inSectorMap);
    // The updates an object stays in the update map before it may sleep (updateCountdown): 0x80 plus
    // 0..15, which the game takes from bits 5..8 of the object's address (in several functions), so it
    // follows the heap's layout. Ours takes the low 4 bits of the object's id instead, so that every run
    // agrees.
    static int UpdateCountdownFor(const EngineObject* object);

    // One world tick: runs every timed object, advances the time wheel and countdowns, moves the missiles, runs the
    // script command queues and then the units.
    void Tick(char force);
    // Runs the objects: in the update-map mode the units in the update map, each counting down to sleep, else every
    // map object; then the counted objects. The effects of sleeping objects are ticked first (TickSleepingEffects).
    void UpdateUnits();
    // Counts the effects of sleeping map objects down and wakes those with an expired one (Rebirth's
    // FixSpellEffectsOverflow #0).
    void TickSleepingEffects();
    // Puts every unit in the sectors within `radius` of `position` (x, y) in the update map.
    void WakeUnitsAround(const Vector3& position, float radius);
    // Takes `unit` out of the sector map, puts it on the free cell nearest to its own place (in the region of
    // `from`; it stays when there is none), and files it again.
    void MoveToFreeCell(class Unit* unit, const Vector3& from);
    // Raises alarm `index` (0..4) at `point` unless it is already raised (`force` raises it again; alarm 0 with
    // `force` clears all five first), tells every AI unit without a player and wakes those that react. Script
    // InvokeAlarm.
    void InvokeAlarm(int index, const float* point, char force);
    // Records the game's state (only the first time); Network and Network3 also replace the network session
    // and query it (Query).
    void SetGameState(GameState state);
    // Sends the unit's message 0x0B (the ack `kind`) to every player with `unit` among its party units.
    void TellParties(EngineObject* unit, AckKind kind);
    void TellPartiesGone(EngineObject* unit);
    // The items the player holds put into `party` and released. Nothing without a player.
    void StorePlayerItems(Player* player, class Party* party);
    void ClearPlayerItems(Player* player);
    void SetWaterLevel(int key, float target, int steps);

    // Replaces the zone exits with copies of `source`'s records that lead somewhere (target not "");
    // an exit without an entrance must lead to "none".
    void SetZoneExits(const std::vector<ZoneExit>& source);
    // Script AddUnitToServer: creates a unit of prototype `prototype` named `name`, with id `id` (the name's text
    // key), at `position`. A prototype the units database lacks: a warning, no unit.
    void AddUnitToServer(unsigned id, std::string_view prototype, std::string_view name, const Vector3& position);
    // Script AddUnitToParty: the record `record` (just made by AddMember) takes the texture names of the map unit
    // with its id (the unit's server-side parentIname and skinTexture), so the unit is drawn textured when the
    // party arrives in the next zone. No unit with that id: the names stay empty. A null record (AddMember refused the
    // prototype) is ignored.
    void TakeLookFromUnit(PartyUnitRecord* record);
    // Reads the objects of a .mob file (chunked); a file that cannot be opened or read ends the game.
    void LoadMapObjects(std::string_view file, int keepIds);
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
    // when the AI graph cannot be read; a zone that cannot be loaded ends the game.
    int LoadSave(ByteStream* stream, const char* map);
    // Writes what LoadSave reads.
    void SaveWorld(ByteStream* stream);
    // Reads the map objects SaveMapObjects wrote, registers them in the id maps and wakes them.
    void LoadSavedMapObjects(ByteStream* stream);
    // Adds the time each player missed since its last update (plus `offset`) to its "gtime" variable.
    void UpdatePlayerTimes(float offset);
    // The players' times move on by `offset` (UpdatePlayerTimes) unless it is negative or a NaN.
    void PassTime(float offset);
    // Player 0's connection, when there is one, sends "constr.current" = `value`.
    void SetConstructionVariable(float value);
    // The unit with id `id` moves to height `z` (its sector map entry follows, its placement is marked changed)
    // unless it is there already (or NaN).
    void SetUnitHeight(int unitId, float z);
    // The console's script command: loads `text` into the second command script (Script::Load) and runs it
    // (Script::Tick) when it loaded and `run` is set; the script's last error is copied into `error` when
    // given. Returns Load's result.
    int RunCommandScript(const std::string& text, bool run, ScriptErrorInfo* error);
    // Puts a new entry at the head of the shared-object list: `id`, the session player's field for that id in a
    // multiplayer game (else -1), `value`, and references to the objects of `objects`.
    void AddSharedObject(int id, std::uint32_t value, const std::vector<Item*>& objects);
    // Before a save: every living unit refills its controller's slots and runs the controller's matching
    // virtual with 0.
    void PrepareSave();
    // Reads the zone's AI graph into the memory stream `out`: the 0x31415926 chunk of the .mob file when
    // there is one, else the whole <map>.grp file; `out` stays empty when neither is there (a mod's new zone:
    // NewZone and LoadSave then build the graph after the load's refresh).
    void ReadAIGraph(std::string_view map, std::string_view mob, ByteStream* out);
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
    // A new zone from `map` and `mob`: the loading movie for `zone` shown, the AI graph read (the game ends
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

    // The weather's rolls, every 64 ticks before UpdateWeather (Tick only): in a battle zone rain starting /
    // stopping, lightning strikes in storms (climate 3), bolts in the sky while it rains. SetTime and SetTimeSpeed
    // do not roll, so a load (LoadSave's and StartZone's) keeps the saved weather (docs/notes/world.md).
    void RollWeather();
    // The weather's effects, every 64 ticks and after the clock is set: the day-time value, the day factor, every
    // object's weather virtual, the players' "gtime".
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
    // Ours: the player's companions (its records after the first) up to date with their living units as
    // SyncPartyRecords does, each then whole again (RestoreSlots); the player's items are left alone.
    void SyncCompanions(Player* player);
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
    Item* CreateItemNamed(std::string_view name);
    Item* CreateSpellNamed(std::string_view name);

    // Creates a unit for each record of `records` (an existing object with the record's id is removed first), at its
    // entry in `placements` or in a line from `position` along `rotation`, moved to a free cell, under `player`'s
    // control, with its items, spells and AI; heroes get their figure parts, followers their leaders, and with a
    // player the units join the "Heroes" script group.
    void CreateUnits(Player* player, std::vector<PartyUnitRecord*>* records, const Vector3& position, Quaternion rotation,
                     std::vector<UnitPlacement>* placements);
    // A quick item (potion) is used by `unit`: the effect object "st.tr.130" (model "unhuma", type 9, flags 0x3010)
    // is created at the unit's position and flagged new (flags |= 0x180000).
    void UseQuickItem(Unit* unit);

    // Brings `player`'s view up to date (once per world update): the zone when it changed, weather, time, wind and
    // countdowns when dirty, then the map objects and timed objects it should see. Returns what the player state's
    // flush returns (0 drops the player), 1 for a player that is leaving.
    int SyncPlayer(Player* player);
    // Sends `player` the zone to load (tick, map and .mob names, exits, the zone's units) and, for a new
    // game, where it starts. 0 when the player state refused it.
    void SendZone(Player* player);

    // WorldDayTime's value: the script's IsNight reads it, the units' movement factors scale by it.
    float nightLevel = 0.0f;
    float weatherFactor = 1.0f;           // 1 dry, 0.9 in rain or snow (the units' fourth movement factor)
    WorldAlarm alarms[5] = {};
    // The zone's weather kind (its record's climate): 3 storms with lightning strikes, 1 light rain,
    // other non-zero values heavier weather; 0 none. Single-player only.
    int climate = 0;
    int cave = 0;                         // the zone is a cave (its record's cave; sent with the zone)
    int weather = 0;                      // 0 clear, 1 rain, 2 snow (the script's SetWeather)
    float weatherStrength = 0.0f;         // sent with the weather
    // UpdateWeather's checks left before rain may start or stop again (10 after a change, counting down below 0 while
    // nothing changes). Not in mission.sav: a slot's visuals.sav keeps it (client_visuals.h, WeatherDelay).
    int weatherDelay = /*007C2DFC*/ 0;
    std::vector<ZoneExit> exits;
    std::uint8_t loaded = 0;              // 0 while LoadSave runs
    MapObjectIdMap updateMap;
    // CreateObject makes objects answering IsSynchronised, sounds, lights and particle sources only with
    // loadedFresh set, the other map objects only with keepsState set; a refused object is released.
    std::uint8_t loadedFresh = 0;         // the zone is not loaded from a save
    std::uint8_t keepsState = 0;
    std::string zoneName;                // the zone's map database name
    std::vector<std::string> mobFiles;     // .mob files loaded in this zone (LoadMobFile)
    GameState gameState = GameState::None;    // None until SetGameState
    std::uint32_t lastRainBoltTick = 0;   // the tick of the last bolt in the rain (one per 20 ticks); saved
    std::unique_ptr<Hoover> hoover;       // made by Create, deleted by Destroy
    // The server's messages: the login handler reads "SrvErrMsg" / "WrongClientVersion" from it.
    FileRegistry messages;
    int zoneLoaded = 0;                   // set by LoadMap, cleared by the teardown and a .mob file that fails to load
    int scriptsActive = 0;                // non-zero: Tick runs the zone's script (set by Redeploy)
    Vector3 startPosition = {};           // where joining players start
    AIMap aiMap;
    // Its globals hold the "Heroes" group.
    Script commandsA;
    Script* MainScript() { return &commandsA; }
    Script commandsB;
    Script* SecondScript() { return &commandsB; }
    Diplomacy diplomacy;
    // The campaign mode: GameFlow::StartZone stores its mode here. Battle or Base: SaveWorld updates the
    // players' "gtime" first. Joining players take it (Player::sessionKind).
    ZoneMode zoneMode = ZoneMode::None;
    std::string mobName;                 // the zone's .mob file
    std::string mapName;                 // the zone's map
    MapObjectIdMap objects;               // every world object by id
    // The map objects SendFreshObjects sent (not part of the map itself; IsSynchronised).
    MapObjectIdMap sentObjects;
    MapObjectIdMap mapObjects;            // objects saved with the zone (SaveMapObjects)
    MapObjectIdMap otherMapObjects;       // the map objects that are not units
    MapObjectIdMap namedObjects;          // objects with a second name
    EffectIdMap timedObjects;
    // The zone header SendFreshObjects sends to a temporary player.
    MemoryStream zoneHeader;
    // A list: a missile's hit can cast the struck unit's item spells (CreateMissile appends) while Tick walks it.
    std::list<HeldRef<SpellEffect>> projectiles;
    std::vector<EngineObject*> counted; // counted references (released on clear)
    std::vector<SharedObject> sharedObjects;
};

// SyncPlayer's difference of two object sets (world_object_sets.cpp; instantiated for MapObject and Effect). Sorts
// `current`, then fills `added` (only in `current`, referenced once more), `gone` (only in `sent`, released) and `same`
// (in both); when anything differs `sent` becomes a copy of `current`.
template <class T>
void DiffObjectSets(std::vector<T*>* sent, std::vector<T*>* current, std::vector<T*>* added, std::vector<T*>* gone,
                    std::vector<T*>* same);

// One walkable platform (bridge, deck) the client tests positions against.
struct Platform {
    int id = 0;                           // the object it belongs to (RemoveObject drops its faces)
    Vector3 centre;                       // of the bounding circle (x, y)
    float radius = 0.0f;
    Vector3 origin;
    Vector3 axisU;                        // local u axis
    Vector3 axisV;                        // local v axis
    Vector3 normal;
    float sizeU = 0.0f, sizeV = 0.0f;     // extent along u and v
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
        float height = 0.0f;
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

// A block of sector cells, both corners included.
struct CellRange {
    std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct SectorGrid {
    // Nothing adds to a cell while it is walked (CollectCells files its objects; a first pass only appends
    // units to the deferred vector); Move erases from one cell and appends to another.
    std::unique_ptr<std::vector<HeldRef<MapObject>>[]> cells;
    std::unique_ptr<std::vector<HeldRef<MapObject>>*[]> rows;  // row y: its cells
    int allocatedWidth = 0, allocatedHeight = 0;  // the cells allocated (Resize: when they change)
    std::uint32_t width = 0;              // cells along x
    std::uint32_t height = 0;             // cells along y
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
    // Calls visit(MapObject*) for every object of the cells x0..x1, y0..y1 (inclusive), row by row.
    template <class Visit>
    void ForEachObject(const CellRange& range, Visit&& visit) {
        for (std::uint32_t y = range.y0; y <= range.y1; ++y)
            for (std::uint32_t x = range.x0; x <= range.x1; ++x)
                for (MapObject* object : rows[y][x]) visit(object);
    }
};

// The worlds' counted helpers: player sources (player_source.h). The server's hands its pending link over once (TakeNew).
class WorldHandle final : public PlayerSource {
public:
    ~WorldHandle() override;

    void OnShutdown() override;
    WorldHandle* Clone() override;
    void TakeNew(std::vector<HeldRef<netlink::NetLink>>* joined) override;

    Ref<netlink::NetLink> pendingLink;      // handed over by TakeNew
};

// The client world's player source (Setup makes it): it has no state of its own.
class ClientHandle final : public PlayerSource {
public:
    void OnShutdown() override {}
    ClientHandle* Clone() override;
    // The client takes no connections: every link of the list is released and the list emptied.
    void TakeNew(std::vector<HeldRef<netlink::NetLink>>* joined) override;
};

// A window rectangle of the frame (the client's screen list).
struct ScreenEntry {
    ClassId classId = {};
    unsigned objectId = 0;                // the object, in the map object manager
    Rect rect;                            // on the screen
};
// An object whose screen rectangle is valid (flags 0x20) and that is not hidden (0x40) puts its class id, id and
// rectangle at the end of `queued`.
int QueueScreenEntry(MapObject* object, std::vector<ScreenEntry>* queued);

// What a screen pick tests an object's side half with: its bounds or its drawn parts.
enum class HitTest { Bounds, Parts };

// An object under a screen pick's point (WorldClient::hits) and what Pick sorts it by: living objects before dead
// ones, then by depth (Object3DSpecific::PickDepth).
struct PickHit {
    unsigned objectId = 0;
    bool dead = false;
    float depth = 0.0f;
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
    std::vector<VisibilityFog*> list;    // counted
};
// The manager's Clear (its object and every listed one released, the list emptied); a new fog object,
// counted, at the list end.
void VisFogManagerDestroy(ClientArrayManager* manager);
VisibilityFog* VisFogManagerAdd(ClientArrayManager* manager);

// The bits of EngineObject::flags on the client world (the constructor sets the draw bits).
enum class WorldClientFlag : std::uint32_t {
    RecordingCamera = 0x10, // a camera file is being recorded (Camera::OpenFile; the command line's camera options
                            // are skipped meanwhile)
    DrawObjects = 0x40,     // the objects but units
    DrawUnits = 0x80,       // the units and the selection markers
    DrawTerrain = 0x100,    // the ground and the water
    DrawEffects = 0x400,    // the alpha pass: the client arrays, the particles, the lightning lists, the rain and snow
    DrawSky = 0x1000,
    DrawNumbers = 0x2000,   // the flying hit numbers
};
template <> inline constexpr bool lib::kBitFlags<WorldClientFlag> = true;

// WorldClient::EnvironmentFlags: what an environment speech (acks.db) asks of the surroundings.
enum class EnvironmentFlag : std::uint32_t {
    MoveMode3 = 0x1,        // the unit's moveMode2 (the data's names for 1 and 2 are Walk and Run; 0 and 3 are not named)
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
    // the rain and snow layer, the platforms and both sector grids.
    void Create() override;
    // Tears the client world down in the reverse order, then deletes the owned objects (counted ones dropped first).
    void Destroy() override;
    // The client's tick. True; false when the session's view has lost its connection or met a packet it cannot parse
    // (a warning; the session is disconnected).
    bool RunTick() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    float GridHeightAt(float x, float y) override { return ClampedGroundHeight(x, y); }
    // The height of whatever surface is on top at (x, y): the ground, or the water when the point is submerged.
    float RawSurfaceHeightAt(float x, float y) override;
    // GridHeightAt / RawSurfaceHeightAt raised to the platforms over the point.
    float GroundHeightAt(float x, float y) override;
    float SurfaceHeightAt(float x, float y) override;
    void MoveObject(MapObject* object, const float* position) override;
    TextureManager* GetTextureManager() override { return textureManager.get(); }
    MaterialManager* GetMaterialManager() override { return materialManager.get(); }
    LightFactory* GetLightFactory() override { return lightFactory.get(); }
    float SceneTime() override { return static_cast<float>(tick) + tickFraction; }

    void BuildUnitSkins(std::vector<FiguredUnitRecord>* others, std::vector<DressedUnitRecord>* dressed);
    // Draws the world view, the whole map's terrain clip-tested.
    void Render();
    // The object passes over every sector of the map, the objects clip-tested. Returns 0 without terrain, else 1.
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
                      std::vector<VertexPacket>* points, std::vector<TexCoord>* texCoords,
                      std::vector<std::uint16_t>* indices);
    // The ambient sounds' time of day: "day" from 6 to 18 o'clock (and for a NaN hour), "morning" before 6 and
    // "evening" after 18 within 2..22, else "night".
    const char* AmbienceTimeOfDay() const;
    // Nearest living unit of the local player's group that `unit`'s side can see (the candidate's
    // relation masks); null when none or `unit` is null.
    Unit* NearestVisibleUnit(Unit* unit);
    PlayerClient* LocalView();
    bool IsLocalViewSendingCamera();
    // A new texture / material of class `type` from the world's managers; null without the manager.
    Texture* NewTexture(int type);
    Material* NewMaterial(int type);
    // Loads the camera path file `name` and, when that works, plays it from now (Camera::kPlaying): the time offset
    // is the client time (tick + fraction) less the first key's, the key index 0.
    void PlayCameraPath(std::string_view name);
    // Message 0x73: the water levels read from `stream` and applied to the terrain.
    void ReadWaterLevels(ByteStream* stream);
    // FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // Index of the exit whose area contains `point` and that leads somewhere.
    std::optional<int> FindExitAt(const Vector3& point);
    Vector2 ExitCentre(int index);
    // One ZoneExit particle source (0x2038) per exit that leads somewhere, in exitMarkers at the exit's index (the old
    // markers released), then UpdateExitMarkers.
    void CreateExitMarkers();
    // Whether the party can leave through `exit`: every living unit of `records` (unit id in the
    // record) stands in an exit (FindExitAt) and that exit is `exit`. Those that do are listed in
    // `inExit` (emptied first). 0 when no unit lives.
    bool PartyInExit(const std::vector<PartyUnitRecord*>& records, int exit, std::vector<PartyUnitRecord*>* inExit);
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
    // id through.
    void CollectVisible(MapObject* object, const Matrix4* view, float animTime, char animateUnits, char withScripted);
    // CollectVisible's first pass over the object grid's cells in `range`, the same verdicts decided a cell at a
    // time where the cell's boxes clear the view's planes (world_client_view.cpp); files the cells' casters
    // (frame_lists.h, CasterCell).
    void CollectCells(const CellRange& range, const Matrix4* view);
    // The ground point under screen pixel (x, y): inverts the current view matrix and iterates on the
    // terrain height (Terrain::HeightAt).
    Vector3 PickGround(int x, int y);
    Vector3 PickSurface(int x, int y);

    // Files an object in the client's maps (objects, objectGrid, heroes for heroes with a text-key id; the
    // dynamic lists for IsSynchronised objects; platform items for classes 0x54 / 0x5D); units fade in
    // through their controller in a multiplayer session of kind 1. An id already filed is only referenced and
    // released.
    void AddObject(MapObject* object);
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
    // A base person's mark (the briefing's people with a topic open): its figure lit (MapObjectFlag::BoxMarked)
    // or not; a selected one is left as it is.
    void MarkPerson(MapObject* object, bool marked);
    // One more reference on every item and, when the item factory exists, on every spell.
    void ReferenceItemsAndSpells();
    // The reverse, used by the teardown: every item and spell released, each manager emptied.
    void ReleaseItemsAndSpells();
    // The items and spells replaced: all released, then (a block with data) the managers loaded and referenced again.
    // Then KeepItemsAndSpells' result.
    bool LoadItemsAndSpells(ByteStream* block);
    // The block read from its start: the spells and then the items it lists (LoadManagerObjects; a failed read
    // ends it). Then KeepItemsAndSpells' result.
    bool LoadNewItemsAndSpells(ByteStream* block);
    // Both block loaders' test: true when every item and spell names records of this game's databases
    // (HasEveryRecord); else the managers are emptied (ReleaseItemsAndSpells; a server with another items.idb or
    // spells.sdb) and the handler makes the session leave (PlayerClient::LeaveAfterTick).
    bool KeepItemsAndSpells();
    // Takes the object `id` out of the maps (platform faces, dynamic lists, heroes, its grid cell and objects); the
    // object is released.
    void RemoveObject(int id);
    // Empties the maps, the session's player data and the managers' records.
    void ClearMaps();
    // The zone's leftovers dropped: the exit markers, the second grid's cells, the timed objects, the bird.
    void ResetZoneObjects();
    // The zone's state block (docs/notes/world.md, "Zone state block"). Returns 1; a map that does not load ends the
    // game.
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
    // The object at screen point `point`: the screen entries that contain it and whose object passes `test` go to
    // hits, sorted front to back by the depth of what is drawn under the point (living objects first); the first
    // entry whose object still exists wins (hitCursor is left on it). Null if none.
    MapObject* Pick(const Point& point, HitTest test);
    // The unit at screen point `point`: from the picked object on, the first unit among the sorted hits
    // (hitCursor moves past the others and those whose object is gone). Null if none.
    Unit* UnitAt(const Point& point, HitTest test);
    // The same by the parts, failing that by the bounds.
    Unit* UnitAt(const Point& point);
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
    const std::string& ExitTarget(int index);
    void RemoveUnitsOf(const std::vector<PartyUnitRecord*>& records);
    void SendPause(int value);
    // The camera looks at the point (x, y) at its own focus height, with its view applied and kept above
    // the objects.
    void CentreOn(const Vector3& point);

    // Creates the party's heroes from their records: the figure from the part names (the hair "hr.%.2d" by the
    // prototype's number), items, player; each unit is filed with AddObject. Armour of type 0 is put on only with
    // `allArmour`. Stops at the first unit that cannot be created or given a figure.
    void CreatePartyUnits(const std::vector<PartyUnitRecord*>& party, char allArmour);
    // Every object of the sector grid's cells around `point` whose (x, y) lies within radius +
    // hypot(rangeX, rangeY) of the point (the cells' range truncated and clamped to the terrain). The
    // original's z difference is the point's z minus itself (0, kept).
    std::vector<Object3D*> ObjectsInRange(const Vector3& point, float radius);
    // Reads the light colours from config\<file> (an .ini, LightsGipat.ini): each key of [sunlight],
    // [ambient] and [sky] names its hour in characters 4..5 ("hour07") and holds "r, g, b" (0..255,
    // scaled by 1/255, alpha 1); the hours between two keys in file order are filled by linear steps
    // from the earlier key (the first from hour 0). Hours are not range-checked.
    void LoadLights(const std::string& file);
    // The frame's light from the time of day: fog, sun direction and the sunlight, ambient and sky colours
    // interpolated between the hour's table entries; once per new hour the terrain's sun. A cave takes hour
    // 0's colours and a fixed sun. Does nothing when the renderer's flag bit 0 is clear.
    void UpdateLighting();
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

    int nextClientObjectId = 0;           // -100000 after Setup; NextClientObjectId counts it down
    std::vector<ZoneExit> exits;
    // The exits' particle markers, one per exit or null (counted; CreateExitMarkers / UpdateExitMarkers).
    std::vector<HeldRef<ParticleSource>> exitMarkers;
    std::vector<HeldRef<MapObject>> heroes;                  // heroes with a text-key id (counted)
    std::vector<MapObject*> objects;              // every filed object (one reference each)
    // The objects answering IsSynchronised, with their nodes by id in dynamicNodes and the highest id
    // in dynamicMaxId (Segment drops those whose update returns 0). A list: dynamicNodes keeps its iterators.
    std::list<MapObject*> dynamicObjects;
    std::unordered_map<std::uint32_t, std::list<MapObject*>::iterator> dynamicNodes;   // id -> its node in dynamicObjects
    int dynamicMaxId = 0;
    std::vector<ScreenEntry> screenEntries;  // the window rectangles of the frame
    std::vector<PickHit> hits;            // the objects under the point (Pick)
    PickHit* hitCursor = nullptr;         // Pick's walk, left on the entry it returns
    SectorGrid objectGrid;                // the objects by sector (MoveObject moves them)
    SectorGrid secondGrid;
    // The client's own managers and parts (Create makes them, Destroy deletes them in its order).
    std::unique_ptr<terrain::Terrain> terrain;
    // The zone's allod in lower case ("gipath" after Setup; the zone load takes it from the map
    // database's record of the local player's zone, "gipat" without one). It names the terrain's
    // texture pages, the lights table and the music theme.
    std::string zoneName;
    std::unique_ptr<MaterialManager> materialManager;
    std::unique_ptr<TextureManager> textureManager;
    std::unique_ptr<LightFactory> lightFactory;
    std::unique_ptr<ParticleManager> particleManager;
    std::unique_ptr<LightningManager> listManager;
    std::unique_ptr<ClientArrayManager> arrayManager;
    std::unique_ptr<MusicSystem> music;
    std::unique_ptr<Sky> skyDome;                 // the sky (LoadSky)
    std::unique_ptr<EffectMoshka> bird;           // the bird following the party (SummonBird)
    std::unique_ptr<FlyingHitPoints> numbers;     // the damage numbers (SetUpGraphics)
    // Segment ticks the ground prints.
    std::vector<BloodPrint> bloodPrints;
    std::vector<Footprint> footprints;
    std::vector<GroundPrint> scorchMarks;
    // The zone LoadZone loaded: its map database name (the local player's zone then) and its map as the server
    // named it.
    std::string shownZone;
    std::string shownMap;
    // The picture kept for the zone LoadZone loads (its side file in saves\current, client_visuals.h), restored there.
    ClientVisuals keptVisuals;
    Ref<ClientHandle> handle;       // the client's player source
    std::unique_ptr<SoundManager> sounds;   // the client controller's hit sounds too
    std::unique_ptr<TextManager> texts;     // the text resources
    PlatformSet platforms;
    // The local session: the local Player; its connection is the local view (a PlayerClient), its
    // sessionKind 1 / 3 for kinds of multiplayer session.
    Player* session = nullptr;
    Camera* camera = nullptr;             // the view camera (DrawPass, Render)
    // The light colours by hour (0..23; r, g, b, 1) from the lights table's [sunlight], [ambient] and
    // [sky] sections (LoadLights).
    LightColour sunlight[24] = {};
    LightColour ambient[24] = {};
    LightColour sky[24] = {};
    // This frame's light (UpdateLighting): the ambient colour (the table's, or 0.8 x the sunlight when
    // the table's is black; its fourth float is never written) and the sunlight (its fourth float is whatever the original's stack held).
    LightColour frameAmbient = {};
    LightColour frameSun = {};
    // The daylight colours: the sun and ambient colour of the table's noon (a cave's hour 0, the light it keeps), the
    // ambient colour taken as UpdateLighting takes it. A ground print's own colour is its colour in them (by day the
    // game's); the renderer lights it by the ground's light over the ground's light in them.
    void DaylightColours(LightColour* sun, LightColour* ambientColour) const;
    // 1: a cave zone (the map's flag: LightsCave.ini, the sky "Sky01", light fixed at hour 0;
    // EnvironmentFlag::Cave instead of Outdoors).
    int cave = 0;
    int partyDanger = 0;                  // the party state's danger (message 0x60: 2 fighting)
    int ticksRun = 0;                     // Segment's count; AddObject fades units in when >= 1
    float tickFraction = 0.0f;            // added to the tick for the units' time (network client)
    // The rain / snow layer, built and destroyed with the world. Its `mode` (1 rain, 2 snow) picks the ground
    // prints' wet look and an environment flag.
    RainSnow rainSnow;
    std::vector<int> selectedIds;            // the selected objects' ids
};

// The scene's camera while the world's draw runs (the scene stack's top it pushes first: projection x
// view; world_client_draw.cpp), else null. The figure parts lit on the GPU take their model matrix from it.
const Matrix4* SceneCamera();
// The hour (floored) the terrain's sun was last set for (world_client_lights.cpp); -1 makes the next
// UpdateLighting set it.
extern float g_lastLitHour;

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
// For the quest and briefing screens: player zero's variables whose names start with `prefix` copied into
// a table (NamedFloatTable::CopyPrefixed), PlayerZeroVariable's player; empty when there is none.
NamedFloatTable PlayerZeroVariables(std::string_view prefix);

// A lightning strike from `from` to `to`, as a script line on the queue: one time in four only a
// thunder sound, otherwise a point light at the middle, a sound and two bolts. `picks` draws the choices: the
// server's rain bolts draw g_random (the world's tick decides when), the main menu's g_clientRandom.
void ShowBolt(Vector3 from, Vector3 to, IsaacRandom& picks);

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
