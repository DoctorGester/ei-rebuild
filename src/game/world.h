#pragma once

// The world: the server side that runs the game rules (CWorldServer) and
// the client side that draws it and answers terrain queries (CWorldClient).
// Both derive from a common base. Class names come from
// the game's error strings ("CWorldServer::LoadMap", "CWorldClient::ToggleWireframe"); everything
// else is provisional. Only the members used by the rewritten functions are declared.

#include <cstddef>
#include "options.h"
#include <cstdint>

#include "ai_map.h"
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
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
#include "maths.h"
#include "party_storage.h"
#include "game_data.h"
#include "game_singletons.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// New units' orientation (a static initialiser: the identity; world_party.cpp, world_zone.cpp).
inline constexpr Quaternion kNewUnitRotation = /*007C2DE0*/ {1.0f, 0.0f, 0.0f, 0.0f};

using BOOL = int;

struct ScriptGroup;
class CItem;       // item.h
struct CItemManager;   // item.h
class CParty;      // party.h
class CParticleSource;   // effects.h
class CAIMap;       // ai_map.h
class CPlayerServer;   // player_messages.h
class CPlayerConnection;   // player_messages.h
class CPlayerClient;   // player_client.h
class CScript;   // script.h
namespace netlink {
class NetLink;   // net_link.h
class DriverSession;   // net_link.h
}
namespace net {
struct NetAddress;   // net_classes.h
}
class CWorldHandle;    // below
class CClientHandle;   // below
class CBaseManager;
struct CSoundManager;   // sound.h
class CSectorMap;   // sector_map.h
class CSpellManager;   // spell.h
class CLinkFigureLoader;   // model.h
class CTexture;   // texture.h
class CTextureManager;   // texture.h
class CMaterialManager;   // material.h
class Terrain;
class CCamera;   // camera.h
class CMaterial;   // material.h
class CStream;   // core_streams.h
class CUnit;     // unit.h
class CMapObject;   // map_object.h
class CObject3D;    // map_object_figures.h
class CSpell;   // spell.h
class CSpellEffect;   // spell.h
class CParticleManager;   // effects.h
struct FootprintPart;   // ai_map.h: a figure part's box
class CFileRegistry;   // registry.h
class CHoover;   // object.h
class CEffectMoshka;   // world_effect.h
namespace mapspec { class Spec; }   // map_spec.h

class CWorldServer;

namespace world {
constexpr int kClassUnit = 0x50;   // CEngineObject::GetClassId() of units
}

// hash_map<unsigned, CEngineObject*> plus the highest key ever inserted (several instantiations
// share this shape).
struct ObjectIdMap {
    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash> map;
    int maxId;

    // map[key] = value, raising maxId.
    void Set(const unsigned& key, CEngineObject* const& value);
    // Releases every object up to the first empty entry, frees
    // the nodes (the buckets stay) and clears maxId. The player's maps of sent objects use it too.
    void ReleaseAll();
    // The constructor as the worlds inline it (world_construct.cpp): 100 buckets (193), maxId 0.
    void Construct();
};
static_assert(sizeof(ObjectIdMap) == 0x18);

// The server world's maps of map objects (objects_, sentObjects_, mapObjects_, otherMapObjects_,
// namedObjects_, updateMap_): ObjectIdMap's shape with CMapObject* values. The release walk is
// ObjectMapClearOriginal.
struct MapObjectIdMap {
    stlp::hash_map<unsigned, CMapObject*, stlp::IntHash> map;
    int maxId;

    void Set(const unsigned& key, CMapObject* const& value) {
        if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
        map[key] = value;
    }
    void Construct() {
        map.InitBuckets(100);
        maxId = 0;
    }
};
static_assert(sizeof(MapObjectIdMap) == 0x18);

// Walks the world's player list. The list object is reference counted while an iterator holds it;
// each node's data is a CPlayer.
struct PlayerIterator : ObjectListIterator {
    PlayerIterator() : ObjectListIterator{nullptr, nullptr} {}

    CPlayer* First(CPlayerManager* players) {
        return static_cast<CPlayer*>(ObjectListIterator::First(players));
    }
    CPlayer* Next() { return static_cast<CPlayer*>(ObjectListIterator::Next()); }
    // Drops the list's count without releasing it.
    ~PlayerIterator() {
        if (manager) --manager->refCount_;
    }
};
static_assert(sizeof(PlayerIterator) == 8);

// Walks an object map's values (world_server.cpp; two byte-identical instantiations): the current
// node, then the map twice. First gives the first object or null, Next the one after it (null at
// the end and for an iterator already at its end).
struct ObjectMapIterator {
    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>::Node* node;
    ObjectIdMap* table;
    ObjectIdMap* map;

    CEngineObject* First(ObjectIdMap* objects);
    CEngineObject* Next();
};
static_assert(sizeof(ObjectMapIterator) == 12);

// The client's flying numbers: the "numbers" texture (counted) and the list of the numbers shown.
class CFlyingHitPoints;     // ui_flying_hit_points.h

// A light colour of the client's tables: r, g, b and 1.
struct LightColour {
    float r, g, b, a;
};
static_assert(sizeof(LightColour) == 0x10);

// Members both world classes share (base class).
class CWorld : public CManagerContext {
public:
    // The engine object's fields, no figure loader, helper, session or managers, the clock at 0 and the
    // empty countdown array.
    CWorld();
    // Destroy (the managers), then the members last to first (the countdown array's storage, the session
    // and the shared helper as RefPtr::Destroy, the figure loader's reference).
    ~CWorld() override;

    // The world's folders from the configured paths, the clock and wind cleared, the managers made,
    // counted and attached (ManagerContext::ThisWorld with the world, or the base), the counted helper,
    // the ISAAC generator seeded and the countdowns set up. Returns 1.
    int Create() override;
    // Every object of the spell manager gets ClearState, then the managers are deleted whatever their
    // counts (the first manager only at its last reference) and the shared pointers dropped.
    void Destroy() override;
    int RunTick() override;   // 1

    // The manager context's getters (slots 13-23; their short functions keep the registrations).
    CWorld* ThisWorld() override { return this; }
    CBaseManager* TimedObjectManager() override { return manager48_; }
    CBaseManager* FigureManager() override { return manager3C_; }
    CItemConstructor* ItemConstructor() override { return manager58_; }
    CSpellConstructor* SpellConstructor() override { return manager50_; }
    CItemManager* ItemFactory() override { return itemManager_; }
    CMapObjectManager* MapObjectManager() override { return manager40_; }
    CPlayerManager* PlayerList() override { return players_; }
    CBaseManager* SpecificManager() override { return manager44_; }
    CSpellManager* SpellManager() override { return manager4C_; }
    CLinkFigureLoader* FigureLoader() override { return figureLoader_; }

    // The world's own virtuals (the client adds MoveObject's override). Names and evidence:
    // docs/notes/map_objects.md ("Virtual slot names"). Some names avoid free helpers of the same
    // name (World, ItemManager, LightManager, ...) that the members' bodies call.
    virtual const char* TexturePath() { return nullptr; }   // the client's "textures\"
    virtual int IsServer() { return 0; }                    // the server's 1
    // The terrain height at (x, y), in ST0: server CWorldServer::GridHeight.
    virtual double GridHeightAt(float x, float y) = 0;
    // The server's AI map height; in ST0.
    virtual double RawSurfaceHeightAt(float x, float y) = 0;
    // The ground height and the height of the water surface at (x, y), in ST0 (the server's call
    // GridHeightAt / RawSurfaceHeightAt; the client's also clamp).
    virtual double GroundHeightAt(float x, float y) = 0;
    virtual double SurfaceHeightAt(float x, float y) = 0;
    // The time of day becomes `hours` (the base is moved so that fmod(tick * secondsPerTick + base, 24)
    // = hours). The server's also updates the players' times first (game kinds 1 and 3), marks them
    // dirty (4) and updates the weather.
    virtual void SetTime(float hours);
    // Seconds per tick become 1 / speed (0: stopped; negative: the setting) times 1/15, keeping the time
    // of day (SetTime). The server's updates the players and weather as SetTime does.
    virtual void SetTimeSpeed(float speed);
    // The client's managers (CLightManager among them), null in the base. MaterialManager registers part
    // materials; the client's tick walks ClientParticleManager's objects.
    virtual CMaterialManager* MaterialManager() { return nullptr; }
    virtual CTextureManager* TextureManager() { return nullptr; }
    virtual CBaseManager* ClientLightManager() { return nullptr; }
    virtual CParticleManager* ClientParticleManager() { return nullptr; }
    virtual CEngineObject* ClientManager194() { return nullptr; }   // not identified
    virtual CEngineObject* ClientManager198() { return nullptr; }   // not identified
    virtual int Unused39() { return 0; }
    // The server's FindObject (world_create.cpp). The map object manager's object with this id, or null.
    virtual CMapObject* ObjectWithId(int id);
    // `object` moves to `position` (the client's sector map).
    virtual void MoveObject(CEngineObject*, const float*) {}

    // Whether the hour of the day is before 6 or from 22 on (a NaN counts as night).
    BOOL IsNight();
    // The time of day in hours and the time speed (1 / (seconds per tick * 15)), both stored as floats.
    void GetTime(float* hours, float* speed);
    // Stores the wind direction and strength.
    void SetWind(const Vector3* direction, float strength);
    // The world object with this id (ObjectWithId) when it is a unit (class 0x50), else null.
    CUnit* FindUnit(int id);
    // How dark the time of day is, in ST0: 1 before 8 o'clock (and for a NaN), 0 until 20, then rising
    // to 1 by midnight ((hours - 20) / 4).
    double NightLevel();
    // A new empty network session (netlink::DriverSession) shared into the world's session pointer, the
    // previous one destroyed (RefPtr::ShareAssign, inlined).
    void OpenSession();
    // The session destroyed (RefPtr::Destroy, inlined) and the pointer cleared.
    void CloseSession();
    // Stores paused_. SetPaused is the server's copy from the pause request and the client's from the
    // pause message (byte-identical code, world_server.cpp). SetServerPaused is the server's entry:
    // EI_Plugin's Pause in mode 3 makes it a plain return.
    void SetPaused(std::uint8_t value);
    void SetServerPaused(std::uint8_t value);

    float timeBase_;
    float secondsPerTick_;
    std::uint8_t windDirection_[12];      // three floats; saved and loaded as they are
    std::uint32_t windStrength_;          // float bits, 0..1; saved and loaded
    CLinkFigureLoader* figureLoader_;     // counted (the client's from SetUpGraphics; FigureLoader returns it)
    CPlayerManager* players_;            // player list (PlayerList returns it)
    CBaseManager* manager3C_;            // FigureManager
    CMapObjectManager* manager40_;       // map objects by id (MapObjectManager; its NewObject creates one)
    CBaseManager* manager44_;            // SpecificManager
    CBaseManager* manager48_;            // the timed objects (spells, effects; TimedObjectManager)
    CSpellManager* manager4C_;           // the spells (SpellManager)
    CSpellConstructor* manager50_;       // SpellConstructor
    CItemManager* itemManager_;          // items (ItemFactory)
    CItemConstructor* manager58_;        // ItemConstructor
    // A counted helper (0xC bytes) CWorld::Create shares in.
    RefPtrTo<CWorldHandle> shared5C_;
    RefPtrTo<netlink::DriverSession> netSession_;   // the network session (OpenSession)
    int tick_;                            // game time in ticks
    int updateCount_;                     // world updates run (the server's Update)
    // Paused: the server's Update skips the players and the tick while it is set; the client's Segment
    // skips its own ticks, and the client copies the server's flag from message 0x60, telling the HUD.
    std::uint8_t paused_;
    std::uint8_t unused_06D[0x70 - 0x6D];
    WaterTweens countdowns_;   // the water level tweens (water_tweens.h)
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

// One of the five alarms InvokeAlarm raises (the script's IsAlarm / AlarmTime / AlarmPosX / AlarmPosY
// read them).
struct WorldAlarm {
    std::uint8_t active;
    std::uint8_t unused_01[3];
    int time;                             // tick it was raised
    float x, y;
};
static_assert(sizeof(WorldAlarm) == 16);

// The world the missiles find while they load (LoadSave sets it).
inline CWorldServer* g_loadingProjectileWorld = /*007C84EC*/ {nullptr};
// The world server's missiles (the spells being applied, spell.h's CSpellEffect): an MFC CList of
// counted pointers. Its FreeNode (world_tick.cpp) releases the node's missile.
using ProjectileList = mfc::CTypedPtrList<CSpellEffect>;

// The stream the world's save and load code reads and writes (the CMemoryStream holding
// mission.sav) is the engine's CStream (core_streams.h); version_ is the save format version.
using WorldStream = CStream;

// CArray<T*> of counted references to engine objects of class T (game_arrays.cpp; instantiated for
// CEngineObject and CItem).
template <class T>
struct CountedPtrs : mfc::CArray<T*> {
    // SetSize to the source's size, then each pointer replaced by the source's (a counted temporary from
    // CopyAt; the new one referenced before the old one is released).
    void CopyFrom(const CountedPtrs& source);
    // By-value GetAt: element `index` into `out` with one more reference. Returns out.
    T** CopyAt(T** out, int index) const;
};
using EngineObjectPtrs = CountedPtrs<CEngineObject>;

// An element of the world server's shared-object list (0x20 bytes; world_save.cpp): object id, player,
// a third dword and an MFC CArray of counted object pointers (the destructor releases the objects and
// frees the data). The objects are items (the loot a unit left: AddSharedObject, Load through the item
// manager).
// Load through the item manager).
struct SharedObject {
    int objectId;
    int player;
    std::uint32_t money;                    // gold the loot adds to the finder's party
    CountedPtrs<CItem> objects;

    // The three dwords, then the array by its Copy (the objects counted). Returns this.
    SharedObject* CopyFrom(const SharedObject* from);
    // The body (the array's destructor, every object released), then operator delete when bit 0 of the
    // flags is set. Returns this.
    SharedObject* DeletingDestruct(unsigned flags);
    // Reads the entry: id, player, value, then object ids resolved by the item manager; each found
    // object is appended with a reference.
    void Load(WorldStream* stream);
};
static_assert(sizeof(SharedObject) == 0x20);
// MFC CList<SharedObject> (its NewNode constructs the element).
using SharedObjectList = mfc::CList<SharedObject>;

// The unit / party records (0x784 bytes) handed around as MFC CArray<record*>: CWorldClient::
// CreatePartyUnits, CWorldServer's unit creation, the hero screens.
struct PartyUnitRecord;   // unit.h
using RecordPtrArray = mfc::CArray<PartyUnitRecord*>;

class CWorldServer : public CWorld {
public:
    // The members' constructors in order (the zone exits, the update map, the .mob list, the messages
    // registry, the AI map, both scripts, the diplomacy, the id maps, the three lists), then ResetState.
    CWorldServer();
    // Destroy, then the members last to first: the lists, the id maps, the diplomacy (its Reset), both
    // scripts (the second's destructor inlined), the AI map, the messages registry, the .mob list, the
    // update map, the zone exits.
    ~CWorldServer() override;

    // world_server_life.cpp
    // Destroy first, then the managers (CWorld::Create), the messages registry (text folder +
    // "SMessBase.reg"), the side-specific manager (a CServerSpecificManager given this world as
    // context), the CHoover, the wind (1, 0, 0; strength 0.4) and the zone state cleared. Returns 1, or
    // FatalError's result when the managers or the registry fail.
    int Create() override;
    // Every player disconnected and released, the CHoover deleted, both script queues reset, ResetState,
    // the messages registry closed, the zone exits freed, then the base's Destroy.
    void Destroy() override;
    // world_update.cpp
    // One world update: drops disconnected players and lets the others' units wake what they hear,
    // accepts new players, runs the players and the world tick (unless paused_), rebuilds the local
    // players' "Heroes" group, synchronises every player (SyncPlayer; 0 drops it) and counts the update.
    // Returns 1. A plain member as well: the main loop and the harness reach it by its address.
    int Update();
    int RunTick() override { return Update(); }
    const char* GetClassName() override;
    int GetClassId() override;
    CItem* DuplicateItem(CItem* item) override { return CopyItem(item); }
    int IsServer() override;
    double GridHeightAt(float x, float y) override { return GridHeight(x, y); }
    // The server's height virtuals (world_server.cpp; ST0). RawSurfaceHeightAt: the AI map's top height
    // at (x, y, 0). GroundHeightAt / SurfaceHeightAt: GridHeightAt / RawSurfaceHeightAt as they are.
    double RawSurfaceHeightAt(float x, float y) override;
    double GroundHeightAt(float x, float y) override;
    double SurfaceHeightAt(float x, float y) override;
    // world_time.cpp. See CWorld::SetTime / SetTimeSpeed.
    void SetTime(float hours) override;
    void SetTimeSpeed(float speed) override;
    CMapObject* ObjectWithId(int id) override { return FindObject(static_cast<unsigned>(id)); }

    // World object with this id, or null.
    CMapObject* FindObject(unsigned id);
    // Creates the object a .mob record (base.kind its kind) describes: map objects and units (1, 5, 8, 9;
    // 6 with a figure and maybe a campfire; 7) go into the object maps, the update map and the sector
    // map; sounds (3), lights (2) and kind 4 become timed objects. Returns the new id, -1 when refused,
    // the message's address for an unknown kind. Rebirth's FixAddMapObject takes index 0 for a unit
    // without a prototype record.
    int CreateObject(mapspec::Spec* message);
    // Removes object `id` from the server: out of the sector map and every id map, its ClearState,
    // flagged 1, the command queues, every object's ForgetObject, the missiles and the players' states forget
    // it, and the maps' reference is released. An unknown id crashes the original (null read); Rebirth's
    // FixRemoveMapObject ignores it.
    void RemoveObject(unsigned id);
    // Player number `id`, or null.
    CPlayer* GetPlayer(int id);
    // Ground height from the server's coarse height grid; 0 outside.
    float GridHeight(float x, float y);
    // Living mercenaries of player `id`, not counting the leader (0 without the player).
    int CountLivingMercs(int id);
    // Whether object `objectId` is on the shared-object list for player `playerId` (or for everyone). In
    // multiplayer the player is looked up on the game server.
    bool IsObjectShared(int objectId, int playerId);
    // Fills `group` with the objects on that list for player `playerId`.
    void CollectSharedObjects(int playerId, ScriptGroup* group);
    // ORs `mask` into every player's dirty flags.
    void MarkPlayersDirty(std::uint32_t mask);
    // SetWind with the strength clamped to 0..1 (NaN: 0), then the players marked dirty (8).
    void ChangeWind(const Vector3* direction, float strength);
    // Setters of cave_, climate_ (any weather stops first) and weather_ / weatherStrength_ (the players
    // marked dirty).
    void SetCave(int value);
    void SetClimate(int value);
    void SetWeather(int weather, float strength);
    // Target zone and entrance of exit `index`; FALSE when out of range.
    BOOL GetZoneExit(int index, mfc::CString* target, int* entrance);
    // Registers an object that lives for a limited time (spells, effects): stamps the current tick, adds
    // a reference, marks it new and puts it in timedObjects_.
    void AddTimedObject(CEngineObject* object);
    // Flags object `id` deleted, releases it and removes it from timedObjects_.
    void RemoveTimedObject(unsigned id);
    // Puts a unit in the update map and sets its update slot.
    void AddToUpdateMap(CMapObject* unit);
    // The updates an object stays in the update map before it may sleep (updateCountdown): 0x80 plus
    // 0..15, which the game takes from bits 5..8 of the object's address (in several functions), so it
    // follows the heap's layout. Ours takes the low 4 bits of the object's id instead, and harness runs
    // on the virtual clock patch the original to do the same (tests/verify/harness_heap.h), so that
    // every run and both sides agree.
    static int UpdateCountdownFor(const CEngineObject* object);

    // world_tick.cpp
    // One world tick: advances the tick count, runs every timed object (spells, effects) and drops those
    // that are done, advances the time wheel and the countdowns, moves the missiles, runs the script
    // command queues (the first only while the pause-free flag is set or `force`) and then the units.
    void Tick(char force);
    // Runs the objects: in the update-map mode the units in the update map, each counting down to sleep
    // (out of the map when its controller is idle), else every map object; then the counted objects
    // (dropped when their RunTick returns 0). Objects flagged 1 are removed at the end. Rebirth's
    // FixSpellEffectsOverflow first ticks the effects of sleeping objects (TickSleepingEffects).
    void UpdateUnits();
    // Rebirth's FixSpellEffectsOverflow #0: counts the effects of sleeping map objects down and wakes
    // those with an expired one.
    void TickSleepingEffects();
    // Puts every unit in the sectors within `radius` of `position` (x, y) in the update map.
    void WakeUnitsAround(const Vector3* position, float radius);
    // Raises alarm `index` (0..4) at `point` (x, y) unless it is already raised (`force` raises it again;
    // alarm 0 with `force` clears all five first), tells every AI unit without a player, and puts those
    // that react in the update map. Script InvokeAlarm.
    void InvokeAlarm(int index, const float* point, char force);
    // Records how the game ended (only the first time); results 2 and 3 also replace the network session
    // and query it (Query).
    void SetGameResult(int result);
    // Every player with `unit` among its party units (CPlayerServer::IsPartyUnit) is sent the unit's
    // message 0x0B with `value` (SendObject0B).
    void TellParties(CEngineObject* unit, std::uint32_t value);
    // Every player with `unit` among its party units is sent the unit's message 0x43 (SendObject43).
    void TellPartiesGone(CEngineObject* unit);
    // The items the player holds (CPlayer::items_) put into `party` (CParty::AddItem(item, 1, 1)) and
    // released, the list emptied. Nothing without a player.
    void StorePlayerItems(CPlayer* player, class CParty* party);
    // Every item of the player's party taken out (CParty::RemoveItem(first, 1)) until none is left.
    void ClearPlayerItems(CPlayer* player);
    // The water level `key` moves to `target` over `steps` ticks (TweenSetTarget).
    void SetWaterLevel(int key, float target, int steps);
    // (No caller in the exe; Rebirth's binaries refer to it.) Every unit (class 0x50) of the object map
    // removed (RemoveObject by id; the walk steps on before each removal).
    void RemoveAllUnits();

    // world_zone.cpp
    // Replaces the zone exits with copies of `source`'s records that lead somewhere (target not "");
    // an exit without an entrance must lead to "none".
    void SetZoneExits(const ZoneExits* source);
    // Script AddUnitToServer: builds a unit record for prototype `prototype` named `name`, gives it the
    // id `id` (the name's text key) and creates it at `position` with the default rotation.
    void AddUnitToServer(unsigned id, const char* prototype, const char* name, const Vector3* position);
    // Reads the objects of a .mob file (chunked). Returns 1, or what FatalError returns when the file
    // cannot be opened or read.
    int LoadMapObjects(const char* file, int keepIds);
    // Loads .mob file `file` once (the loaded names are kept).
    int LoadMobFile(const char* file);
    // Removes every map object from the server, releases the missiles and the counted objects and clears
    // the countdowns.
    void ClearMapObjects();
    // Writes every map object to a save stream: id, prototype name, the object's own Save and its two
    // flag words; -1 ends the list.
    void SaveMapObjects(CStream* stream);

    // world_save.cpp
    // Restores the world from a mission save: zone names, settings, alarms and
    // clock, the zone itself (LoadMap with the saved AI-graph state), the managers' objects, the
    // map objects, timed objects, missiles, time wheel, script state, players, shared objects;
    // units that cannot stand where they were saved are moved to a free cell or removed. `map`
    // replaces the saved map name when not null. Returns 1, 0 when the AI graph cannot be read,
    // or what FatalError returns when the zone cannot be loaded.
    int LoadSave(WorldStream* stream, const char* map);
    // Writes what LoadSave reads.
    void SaveWorld(WorldStream* stream);
    // Reads the map objects SaveMapObjects wrote, registers them in the id maps
    // and wakes them.
    void LoadSavedMapObjects(WorldStream* stream);
    // Adds the time each player missed since its last update (plus `offset`) to its "gtime" variable.
    void UpdatePlayerTimes(float offset);
    // Puts a new entry at the head of the shared-object list: `id`, the session player's field for that
    // id in a multiplayer game (else -1), `value`, and references to the objects of `objects` (a
    // CPtrList). The second argument is unused.
    void AddSharedObject(int id, int unused, std::uint32_t value, const mfc::CTypedPtrList<CItem>* objects);
    // Before a save: every living unit among the map objects (the walk stops at an empty entry) refills
    // its controller's slots and runs the controller's matching virtual with 0.
    void PrepareSave();
    // Reads the zone's AI graph into the memory stream `out`: the 0x31415926 chunk of the .mob file when
    // there is one, else the whole <map>.grp file. 1 when found.
    int ReadAIGraph(const char* map, const char* mob, CStream* out);
    // The zone's scripts from the .mob file's 0xA000 chunk (its 0xACCEECCB text packed and XORed,
    // decoded by the game's routine, or the plain 0xACCEECCA text) become the only line of the first
    // command queue.
    void LoadZoneScripts(const char* mob);
    // While a game runs, every new connection the lobby hands over (TakeNew) becomes a player of this
    // game's kind, fully dirty, starting at startPosition_; "Accepted player <id>" goes to the network
    // log.
    void AcceptNewPlayers(CPlayerSource* lobby);

    // world_game.cpp
    // Copy-constructs the zone name into `out`; returns out.
    mfc::CString* ZoneNameInto(mfc::CString* out);

    // world_map.cpp
    // Loads map `map` (when it changed; CAIMap) and the AI layer state from `graph`, then the zone: the
    // same .mob again starts it over (queues, diplomacy, map objects, missiles and timed objects
    // cleared), another one first empties the world (ResetZone); every player is marked dirty; the .mob
    // objects are loaded (fresh zones start). From a save the loaded objects are cleared again (the save
    // holds them). Returns 1, 0 when the map or the objects cannot be loaded.
    int LoadMap(const char* map, const char* mob, CStream* graph, char fromSave);
    // Empties the world of the current zone: shared objects, .mob files, countdowns, exits, command
    // queues, missiles, counted objects, the players' zone data, every object (ClearState, then the maps),
    // timed objects, the zone names and three managers.
    void ResetZone();
    // ResetZone, then the map name, diplomacy, both command queues and the AI map are reset and the game
    // state cleared: zoneLoaded_, the day-time value, alarms, the kind, the start position, the day
    // factor (1), loaded_ and lastRainBoltTick_.
    void ResetState();
    // Before every game state request: every player leaves, the shared objects, .mob files, script
    // queues, diplomacy, map objects, missiles and timed objects go, then the base class's teardown; the
    // game result and kind are cleared.
    void TeardownZone();
    // A new zone from the map `map` and the object file `mob`: the loading movie picked for `zone` and
    // its first frame shown; 0 when a file is neither where its name says nor in the maps folder (null
    // names are not looked for). The AI graph is read (ReadAIGraph; without it a fatal error, whose
    // result is returned), the .mob list and the alarms are emptied and the map loaded between the AI
    // map's BeginRefresh and EndRefresh (LoadMap; a failure is a fatal error too). The zone name is kept
    // when given, scriptsActive_ cleared. Returns 1.
    int NewZone(const char* map, const char* mob, const char* zone);
    // Unless the zone's map and .mob are these already: NewZone(map, mob, null), then TeardownZone.
    void EnsureZone(const char* map, const char* mob);
    // `unit` used one of `item` (a scroll): the item's count drops, and so does the count of the party's
    // item it was made from (the item's link field) when the unit's player has a record for the unit.
    void UseItemUp(class CUnit* unit, class CItem* item);
    // After a fresh zone is loaded: the map objects that are not part of the map (IsSynchronised) move
    // from the map-object maps to sentObjects_ and are sent, with zoneHeader_ (reset first), through a
    // temporary player.
    void SendFreshObjects();

    // world_spells.cpp
    // Casts `spell` from `caster` (or from `from`): one missile at `target` / `to`, or, for a spell with
    // several targets or area flags, at the nearest living units within its radius the caster may hit
    // (its heroes group, friends or diplomacy), the requested target first. Network clients cast nothing
    // with a caster.
    void CastSpell(CSpell* spell, CUnit* caster, const Vector3* from, CMapObject* target,
                   const Vector3* to, int extra);
    // One missile of `spell` from the caster (or `from`) to the target (or `to`, on the ground there);
    // kept on the missile list unless its first update ends it. A hostile spell turns the target unit
    // against the caster.
    void CreateMissile(CSpell* spell, CUnit* caster, Vector3 from, CMapObject* target, const Vector3* to,
                       int extra);

    // world_loot.cpp
    // `player` takes what `object` holds: a shared loot record (flags_ 0x20000000, removed afterwards) or
    // what the collector gathers (in a multiplayer client game quest items go to player 0); the gold is
    // added to the player. False for no player, a living owned unit, or nothing to take.
    bool Loot(CUnit* unit, CPlayer* player);
    // The local player (id 0) loots every dead unit, which is then removed (both through the global
    // server). Nothing without a local player.
    void LootDeadUnits();

    // world_weather.cpp
    // The weather, every 64 ticks: the day-time value; in single player rain starting / stopping,
    // lightning strikes in storms (climate_ 3), bolts in the sky while it rains; the day factor; every
    // object's weather virtual; the players' "gtime".
    void UpdateWeather();

    // world_quests.cpp
    // For every player and the ten mercenary slots ("merc1".."merc10"), sets the briefing variables
    // b.mercN.n1/n2/n3/n8/n9/n10/n11_N (and clears amerc_N) from whether the mercenary is in the party,
    // was hired (amerc_N), and how many others are alive.
    void UpdateMercBriefings();
    // Briefing `name` ("a.b.<record>") is complete for player `playerId`: a network client asks the
    // server; otherwise the player gets the record's experience, gold and items, loses its quest items,
    // its variables are set, and the scripts get GSSetVar / #OnBriefingComplete (run at once in kind-3
    // games; also on a network client with EI_Plugin's Briefings).
    void CompleteBriefing(int playerId, const char* name);
    // The end of CompleteBriefing: GSSetVar / #OnBriefingComplete for `name` queued, run at once in
    // kind-3 games.
    void QueueBriefingScript(int playerId, const char* name);
    // The mercenary side of a completed briefing `name`: for the ten slots, b.mercN.n8_N clears that
    // variable, n10_N removes the mercenary from the party and the world, n2_N from the party, n1_N /
    // n3_N hire it (amerc_N = 2, added to the party with its unit's prototype or "Human Hero"); then
    // UpdateMercBriefings.
    void ApplyMercBriefing(CPlayer* player, const char* name);
    // Gives `amount` experience: with a player, shared equally by its living heroes (units and party
    // records); without one, shared by the players that have a single hero, in proportion to the sqrt of
    // two of each hero's controller stats. Zero (or NaN) does nothing.
    void AwardExperience(CPlayer* player, float amount);
    // Brings the player's party records up to date with its living units: the player's own items, then
    // every record's quick, worn and belt items and spells rebuilt from the unit (each item's record
    // pointer pointing at its entry) and the statistics.
    void SyncPartyRecords(CPlayer* player);
    // Before a zone change: every record of `records` (CArray of record pointers) notes its unit's
    // leader, gets a placement (if `placements`), is rebuilt from its unit through `partyRecords`, and
    // the unit is removed (a dead one just loses its owner).
    void StoreParty(RecordPtrArray* records, CParty* partyRecords, mfc::CArray<UnitPlacement>* placements);
    // The player's party leaves the zone: gtime, its items, then (single player and kind 3, with
    // zoneLoaded_) its records without a unit are dropped and the rest stored (StoreParty).
    void CollectParty(CPlayer* player, mfc::CArray<UnitPlacement>* placements);

    // world_party.cpp
    // Moves quest items (class 0x14) from `itemArray` (a CArray of item pointers) to `list` (CPtrList):
    // in single player only those whose description names this zone or whose id an object of the zone
    // asks for (object slots 38/40); otherwise all.
    void TakeZoneQuestItems(lib::AlignedArray<CItem*>* items, mfc::CTypedPtrList<CItem>* list);
    // Gives the player fresh copies of the items it brings into the zone (all of them on a client, the
    // zone's quest items otherwise; nothing in a network game).
    void RestorePlayerItems(CPlayer* player);
    // The party arrives in the zone (single player and kind 3, with zoneLoaded_): its units are created
    // at the entrance's centre (the target zone's exit), facing away from the map centre or along the
    // exit's heading, the player state gets the start; `runScripts` runs the script command queue five
    // times.
    void Redeploy(CPlayer* player, mfc::CArray<UnitPlacement>* placements, char runScripts);
    // A new item like `item`: made by the item factory from its type and record, filled by the item's
    // its fill virtual (with the second one for spells), with its count; one below its maximum goes on counted_ once.
    CItem* CopyItem(CItem* item);
    // Puts `item` (an item whose state changed) on counted_, once; null is ignored.
    void AddCounted(CEngineObject* item);
    // The item manager's new item named `name`.
    CItem* CreateItemNamed(const char* name);
    // The item manager's new spell named `name` (filled like CopyItem's).
    CItem* CreateSpellNamed(const char* name);

    // world_units.cpp
    // Creates a unit for each record of `records` (a CArray of 0x784-byte unit records; an existing
    // object with the record's id is removed first), at its entry in `placements` (CArray of 0x28-byte
    // {id, position, rotation, two values}) or in a line from `position` along `rotation`, moved to a
    // free cell, under `player`'s control, with its items, spells and AI; heroes get their figure parts.
    // Followers are given their leaders; with a player the units are added to the "Heroes" script group.
    void CreateUnits(CPlayer* player, RecordPtrArray* records, const Vector3* position, Quaternion rotation,
                     mfc::CArray<UnitPlacement>* placements);
    // A quick item (potion) is used by `unit`: the effect object "st.tr.130" (model "unhuma", type 9,
    // flags 0x3010) is created at the unit's position on the ground and flagged new (flags_ |= 0x180000).
    // `item` is not used.
    void UseQuickItem(CItem* item, CUnit* unit);

    // world_sync.cpp
    // Brings `player`'s view up to date (once per world update): the zone when it changed (SendZone),
    // weather, time, wind and countdowns when dirty, then the map objects and timed objects it should
    // see compared with what it was sent. Returns what the player state's flush returns (0 drops the
    // player), 1 for a player that is leaving.
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
    // Set: SendZone also sends the player its start (at its first unit). Cleared by the reset; nothing
    // in the game sets it.
    std::uint8_t sendsStartPosition_;
    // (LoadMap, world_map.cpp: LoadedFresh / KeepsState): CreateObject makes objects answering map
    // IsSynchronised, sounds, lights and particle sources only with loadedFresh_ set,
    // the other map objects only with keepsState_ set; a refused object is released.
    std::uint8_t loadedFresh_;            // the zone is not loaded from a save
    std::uint8_t keepsState_;
    std::uint8_t unused_11F;
    mfc::CString zoneName_;               // the zone's map database name
    mfc::CStringArray mobFiles_;           // .mob files loaded in this zone (LoadMobFile)
    int gameResult_;                      // 0 while the game runs
    std::uint32_t lastRainBoltTick_;      // the tick of the last bolt in the rain (one per 20 ticks); saved
    CHoover* hoover_;                     // a CHoover (object.h), counted; made by Setup, deleted by Teardown
    // A CFileRegistry (registry.h) with the server's messages: the login handler reads "SrvErrMsg" /
    // "WrongClientVersion" from it.
    lib::InPlace<CFileRegistry> messages_;
    CFileRegistry* Messages() { return messages_.Get(); }
    int zoneLoaded_;                      // set by LoadMap, cleared by the teardown and a .mob file that fails to load
    int scriptsActive_;                   // non-zero: Tick runs the zone's script (set by Redeploy)
    Vector3 startPosition_;               // where joining players start
    // The AI map (ai_map.h), including its height scale, sector map, region map (what SaveWorld saves),
    // footprint tick and cell grid.
    CAIMap aiMap_;
    CAIMap* AIMap() { return &aiMap_; }
    CSectorMap* Sectors() { return &aiMap_.sectorMap; }
    AIEventTable aiEvents_;         // the AI events (unit_ai.h: time wheel, grid)
    AIEventTable* AIEvents() { return &aiEvents_; }
    // The zone's CScript (script.h): its globals_ hold the "Heroes" group.
    lib::InPlace<CScript> commandsA_;
    CScript* MainScript() { return commandsA_.Get(); }
    lib::InPlace<CScript> commandsB_;     // the second script
    CScript* SecondScript() { return commandsB_.Get(); }
    CDiplomacy diplomacy_;                // object_diplomacy.h
    // The session kind: 1 single player, 3 no range test, 4 network; 1 or 3: SaveWorld updates the
    // players' "gtime" first. Joining players take it (CPlayer::sessionKind100_). Also the campaign mode
    // (in the server world): GameFlow::StartZone stores its mode here (1 battle, 2 main menu, 3 base,
    // 4 edge, 8 the end of the game), StartFirstMap 7 for the command-line script.
    int sessionKind_;
    mfc::CString mobName_;                // the zone's .mob file
    mfc::CString mapName_;                // the zone's map
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
    mfc::CTypedPtrList<CEngineObject> counted_; // counted references (released on clear)
    SharedObjectList sharedObjects_;      // (world_save.cpp; AddSharedObject, IsObjectShared)
};
static_assert(offsetof(CWorldServer, countdowns_) == 0x70);
static_assert(offsetof(CWorldServer, nightLevel_) == 0x84);
static_assert(offsetof(CWorldServer, loaded_) == 0x100);
static_assert(offsetof(CWorldServer, zoneName_) == 0x120);
static_assert(offsetof(CWorldServer, hoover_) == 0x140);
static_assert(offsetof(CWorldServer, loadedFresh_) == 0x11D);
static_assert(offsetof(CWorldServer, keepsState_) == 0x11E);
static_assert(offsetof(CWorldServer, lastRainBoltTick_) == 0x13C);
static_assert(offsetof(CWorldServer, aiMap_) == 0x170);
static_assert(offsetof(CWorldServer, diplomacy_) == 0x12C64);
static_assert(offsetof(CWorldServer, sessionKind_) == 0x12D64);
static_assert(offsetof(CWorldServer, mobName_) == 0x12D68 && offsetof(CWorldServer, mapName_) == 0x12D6C);
static_assert(offsetof(CWorldServer, sentObjects_) == 0x12D88);
static_assert(offsetof(CWorldServer, zoneHeader_) == 0x12E00);
static_assert(offsetof(CWorldServer, otherMapObjects_) == 0x12DB8);
static_assert(offsetof(CWorldServer, namedObjects_) == 0x12DD0);
static_assert(offsetof(CWorldServer, alarms_) == 0x8C);
static_assert(offsetof(CWorldServer, climate_) == 0xDC);
static_assert(offsetof(CWorldServer, exits_) == 0xEC);
static_assert(offsetof(CWorldServer, gameResult_) == 0x138);
static_assert(offsetof(CWorldServer, scriptsActive_) == 0x160);
static_assert(offsetof(CWorldServer, aiEvents_) == 0x2838);
static_assert(offsetof(CWorldServer, commandsA_) == 0x12874);
static_assert(offsetof(CWorldServer, commandsB_) == 0x12A6C);
static_assert(offsetof(CWorldServer, projectiles_) == 0x12E28);
static_assert(offsetof(CWorldServer, updateMap_) == 0x104);
static_assert(offsetof(CWorldServer, objects_) == 0x12D70);
static_assert(offsetof(CWorldServer, timedObjects_) == 0x12DE8);
static_assert(offsetof(CWorldServer, sharedObjects_) == 0x12E60);
static_assert(offsetof(CWorldServer, mobFiles_) == 0x124);
static_assert(offsetof(CWorldServer, zoneLoaded_) == 0x15C);
static_assert(offsetof(CWorldServer, mapObjects_) == 0x12DA0);
static_assert(offsetof(CWorldServer, counted_) == 0x12E44);

// One walkable platform (bridge, deck) the client tests positions against. 0x4C bytes, the element of
// the platform set's MFC CList (node: next, prev, then the record).
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

using PlatformList = mfc::CList<Platform>;
using PlatformNode = PlatformList::Node;
static_assert(sizeof(PlatformNode) == 0x54);   // carves nodes of 0x54 bytes

// The client world's platform set (0x38 bytes): the engine's CBridgeManager (class 0x34; the client's
// Create calls its Create, Destroy its Destroy) with the CList of the platform faces (AddFace adds at
// the head; the list's own NewNode, RemoveAt, RemoveAll).
class PlatformSet final : public CEngineObject {
public:
    // The engine object's fields and the empty face list (inlined in the client's constructor).
    PlatformSet();
    // The face list emptied.
    ~PlatformSet() override;
    // The face list emptied (its blocks freed); returns 1.
    int Create() override;
    // The face list emptied.
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;

    // Max(height, z of every platform surface whose area contains the point); the point is projected
    // onto each platform plane along z.
    float SurfaceHeight(const Vector3* point, float height);
    // Whether (x, y) lies on some platform (with a 0.1 margin).
    bool Contains(float x, float y);
    // Used by CParticleSource::TickGroundPlane (the particles' ground plane). The highest platform
    // surface above 0 at (x, y) (with a 0.5 margin): *height its z and *normal its normal; 0 and
    // (0, 0, 1) when there is none.
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
static_assert(sizeof(PlatformSet) == 0x38);

// The unit set the server's SendZone fills (world_sync.cpp) and the client reads back as its unit looks
// (unit_looks.h): the figured records grouped by prototype (each group an STLport hash_map keyed by
// three packed 10-bit values), and the dressed records by prototype. All three maps hash a key to
// itself. The writer is world_unit_set.cpp, Add world_unit_set_add.cpp. The maps are game_hashes.h's
// tables (gamehash::FiguredGroupsTable, gamehash::DressedTable); their element policies are
// record_vectors.h's and game_hashes.h's.
namespace recordvec { struct FiguredUnitElement; struct DressedUnitElement; }
namespace gamehash { struct FiguredTableElement; }
struct UnitSet {
    using FiguredTable = stlp::hashtable<stlp::DwordObjectTraits<FiguredUnitRecord, recordvec::FiguredUnitElement>>;
    using FiguredGroups = stlp::hashtable<stlp::DwordObjectTraits<FiguredTable, gamehash::FiguredTableElement>>;
    using DressedTable = stlp::hashtable<stlp::DwordObjectTraits<DressedUnitRecord, recordvec::DressedUnitElement>>;

    // Adds `unit`'s looks: its prototype (the low word of prototypeId_), the figure's scale and, for a
    // hero (type_ 0x32), the item ids of its 7 worn pieces and 4 weapons (0xFFFFFFFF for none or an id
    // out of the tables) and its active weapon; other units get extraWeapon 0xFF and the texts their
    // companion holds (see the .cpp). A figured record of the same prototype, scale key and ids gets
    // modelNumber -1, else the record (modelNumber the unit's id) joins its prototype's group; then the
    // same for a dressed record of the same prototype and ids.
    void Add(CUnit* unit);
    // Writes the set to `stream`: the number of figured records; per record its head, the morph
    // coefficients, its name count and each name with its terminator; the number of dressed records and
    // per record a 16-bit prototype, two counted dword arrays, a byte and a dword.
    void Write(CStream* stream);

    FiguredGroups figured;                // by prototype: each group's records by scale key
    DressedTable dressed;                 // by prototype
};
static_assert(offsetof(UnitSet, dressed) == 0x14);
static_assert(sizeof(UnitSet) == 0x28);

// The record helpers the unit add was compiled with (world_unit_set_add.cpp).
namespace unitset {
// A record head's two id lists empty. Returns this.
DressedUnitRecord* __fastcall ConstructHead(DressedUnitRecord* self);
// A record head's destructor: the extra weapons' storage, then the items'.
void __fastcall DestroyHead(DressedUnitRecord* self);
// Equal(first, last, other) of two id ranges.
bool __fastcall EqualIds(const std::uint32_t* first, const std::uint32_t* last, const std::uint32_t* other);
// The scalar deleting destructor of a CString (the name vector's element loop calls it with 0),
// ScalarDeleting<&DestructName>::Delete. Returns this.
void __fastcall DestructName(mfc::CString* name);
} // namespace unitset

// Reads the zone's two unit look lists from `stream`: a count, then per figured record a 16-bit
// prototype, two counted dword arrays, a byte, a dword, the three morph coefficients and a counted list
// of names; then a count and per dressed record the prototype, the two arrays, the byte and a dword.
// Both vectors are resized to the counts.
void __fastcall ReadUnitLooks(CStream* stream, stlp::vector<FiguredUnitRecord>* figured,
                              stlp::vector<DressedUnitRecord>* dressed);

// The client's sector grids (the objects' and a second one; world_client_maps.cpp): an STLport list of
// counted pointers per 16x16 cell.
struct SectorCellObjects : stlp::list<CEngineObject*> {
    // The cells' vector deleting destructor, on the first list of `this` array. Bit 1 of `flags`: it is
    // a new[] block (its count in the dword before it), every list destroyed from the last; else the one
    // list. Bit 0: the memory freed. Returns the block.
    SectorCellObjects* VectorDelete(unsigned flags);
};
using SectorList = SectorCellObjects;
using SectorListNode = SectorList::Node;
// The client's filed objects: the same plain STLport list, of map objects only (every object filed is
// one the player client created through the map object manager, or a unit).
using MapObjectList = stlp::list<CMapObject*>;
// A cell's list destroyed (world_client_maps.cpp; STLport's list<counted pointer>::~list inlined by the
// client's destructor for its heroes): each object released (Release) and its node returned to the
// pool, the sentinel last.
void DestroySectorList(stlp::list<CEngineObject*>* list);
// The same out of line, which only the exit function calls (on the deferred unit list).
void __fastcall SectorListDestruct(stlp::list<CEngineObject*>* list);
struct SectorGrid {
    SectorList* cells_;                   // the lists
    SectorList** rows_;                   // row y: its lists
    int unknown08_, unknown0C_;           // (resized when they change)
    std::uint32_t width_;                 // cells along x
    std::uint32_t height_;                // cells along y
    // The storage for unknown08_ x unknown0C_ cells: a new[] block of empty lists (the count before
    // them), then the row pointers.
    // unknown08_ x unknown0C_ cells: a new[] block of empty lists (the count before them), then the
    // row pointers.
    void Allocate();
    // When the storage has another size: the lists destroyed and the storage freed
    // (SectorList::VectorDelete, flags 3), the size taken, then Allocate.
    void Resize(int width, int height);
    // Moves `object` to `position` (stored in the object): when the cell (FISTP of x and y, >> 4)
    // changes, out of the old cell's list and to the end of the new one's (cells outside the grid are
    // skipped).
    void Move(CEngineObject* object, const float* position);
    // The same for the second grid's list instantiation.
    void Move2(CEngineObject* object, const float* position);
};
static_assert(offsetof(SectorGrid, width_) == 0x10);

// The functor of the for_each over the footprints (world_client_maps.cpp): {x, y, scale}, handed back
// by value.
struct RadiusFunctor {
    // For_each over the footprint list's nodes [first, last): a footprint is made visible when
    // hypot(position - (x, y)) <= its scale_ * scale. Writes {x, y, scale} to this and returns it.
    RadiusFunctor* Mark(stlp::list<Footprint>::Node* first, stlp::list<Footprint>::Node* last, float x, float y,
                        float scale);
    float x, y, scale;
};
// The predicate of CWorldClient::ClearMaps' remove_if (an empty functor).
struct ReleaseIdentified {
    // An object with a positive id is released and true; false otherwise.
    bool Test(CEngineObject* object);
    std::uint8_t unused;
};
// hash_map<unsigned, node*> (the client's pending map: an object id to its node in the client's
// pending list).
struct PendingIndex : stlp::hash_map<std::uint32_t, mfc::CList<CEngineObject*>::Node*, stlp::IntHash> {
    // *value = the entry's value and 1, or 0.
    int Lookup(const std::uint32_t* key, mfc::CList<CEngineObject*>::Node** value) const;
};
using PendingMap = PendingIndex;
// A list element whose layout is not needed here (only its size, for the node's).
template <std::size_t N>
struct OpaqueRecord {
    std::uint8_t bytes[N];
};


// The worlds' counted helpers (the server's 0xC bytes, the client's 8): player sources
// (player_source.h; Query false, the client's Segment calls Pump every tick). The server's hands its
// pending link over once (TakeNew). world_sessions.cpp.
class CWorldHandle final : public CPlayerSource {
public:
    CWorldHandle() : pendingLink_(nullptr) {}
    ~CWorldHandle() override;           // the link released

    int OnShutdown() override;
    CWorldHandle* Clone() override;
    bool Query() override { return false; }
    void TakeNew(JoinedLinks* joined) override;

    netlink::NetLink* pendingLink_;     // counted; handed over by TakeNew
};

// The client world's player source (Setup makes it): it has no state of its own. world_sessions.cpp.
class CClientHandle final : public CPlayerSource {
public:
    // The deleting destructor is the counted value's.
    int OnShutdown() override { return 0; }
    CClientHandle* Clone() override;
    bool Query() override { return false; }
    // The client takes no connections: every link of the list is released and the list emptied (its
    // blocks freed).
    void TakeNew(JoinedLinks* joined) override;
};

// A window rectangle of the frame (the client's screen list and hit vector; 0x18 bytes).
struct ScreenEntry {
    int classId;                          // the object's class id
    unsigned objectId;                    // the object, in the map object manager
    std::int32_t rect[4];                 // RECT on the screen
};
static_assert(sizeof(ScreenEntry) == 0x18);
using ScreenEntryNode = stlp::list<ScreenEntry>::Node;
// Called through a mem_fun1 by the frame's draw pass (true when queued). An object whose screen
// rectangle is valid (flags 0x20) and that is not hidden (0x40) puts its class id, id and that
// rectangle (CObject3D::dirtyRect) at the end of `queued`.
int __fastcall QueueScreenEntry(CEngineObject* object, stlp::list<ScreenEntry>* queued);

// The STLport vector<ScreenEntry> the pickers fill. Its _M_insert_overflow is the instantiation's own
// (world_client_maps.cpp); the collectors inline push_back.
struct ScreenHits : stlp::vector<ScreenEntry> {
    // _M_insert_overflow (plain elements).
    void InsertOverflow(ScreenEntry* position, const ScreenEntry* value, int count);
    void PushBack(const ScreenEntry* entry) {
        ScreenEntry*& finish = Finish();
        if (finish != EndOfStorage()) {
            if (finish) *finish = *entry;
            ++finish;
        } else {
            InsertOverflow(finish, entry, 1);
        }
    }
};
static_assert(sizeof(ScreenHits) == 12);
// The functor of the for_each over the screen entries: the hits vector it fills, handed back by value.
// CollectBounds / CollectParts: for_each over list nodes [first, last) of the entries: every entry
// whose rectangle holds (x, y) and whose object (found in `manager`) answers the bounds virtual
// (CollectBounds) or 24 (CollectParts) for the point is pushed to the vector `hits`. this->hits =
// hits; returns this.
struct HitCollector {
    HitCollector* CollectBounds(ScreenEntryNode* first, ScreenEntryNode* last, ScreenHits* hits, long x, long y,
                                CBaseManager* manager);
    HitCollector* CollectParts(ScreenEntryNode* first, ScreenEntryNode* last, ScreenHits* hits, long x, long y,
                               CBaseManager* manager);
    ScreenHits* hits;
};

// The client's rain / snow layer (0xC8 bytes; drawn by the client's draw): an engine object (Teardown
// calls its Destroy) that keeps the world.
class CRainSnow;            // effects.h

class CLightningManager;   // lightning.h
class CSceneRenderer;   // model.h
// The visibility fog's manager ("CVisFogManager", class id 0x60, 0xD0 bytes; vis_fog_manager.cpp; a
// real virtual class): a counted object and a CList.
struct ClientArrayManager : CBaseManager {
    // CBaseManager's constructor, no texture, an empty list (blocks of 10). Inlined in the client
    // world's Setup (world_client_maps.cpp, which also holds the destructor).
    ClientArrayManager();
    // Destroy's body (the object and every listed one released), the list's and the pointer's own
    // destructors, then CBaseManager's destructor.
    ~ClientArrayManager() override;
    int Create() override;
    void Destroy() override;
    int RunTick() override;
    const char* GetClassName() override;
    int GetClassId() override;
    // CBaseManager's Init.
    CEngineObject* NewObject(int type) override;

    // Every fog drawn.
    int Draw(CSceneRenderer* renderer);

    CEngineObject* current;               // counted
    mfc::CList<CEngineObject*> list;      // counted
};
static_assert(sizeof(ClientArrayManager) == 0xD0);
// vis_fog_manager.cpp: the manager's Clear (its object and every listed one released, the list
// emptied); a new fog object, counted, at the list end.
void __fastcall VisFogManagerDestroy(ClientArrayManager* manager);
CEngineObject* __fastcall VisFogManagerAdd(ClientArrayManager* manager);
// The client's particle manager, CParticleManager (effects.h).

// The drawing side.
class CWorldClient : public CWorld {
public:
    // The members' constructors in order (the zone exits, the counted array, the lists and maps, both
    // sector grids at 1 x 1, the ground print lists, the platform set, the rain and snow layer
    // (cleared), the selection list), the flags 0x3DC0, the fields it zeroes and the window's names.
    CWorldClient();
    // Destroy, then the members last to first (the lists, the rain and snow layer, the platforms, the
    // handle, the ground prints, both sector grids set to 1 x 1 and freed, the maps, the counted array,
    // the exits).
    ~CWorldClient() override;

    // Sets the client world up: the managers (CWorld::Create), the texture path, the lights table
    // (LightsGipat.ini), the owned objects (sounds_, texts_), the terrain, the managers, the session,
    // the counted handle_, the text resources, the list / array / music managers, the rain and snow
    // layer and the platforms, flag 0x200, the cursor, a reference on every player, room for 400 screen
    // entries, and both sector grids at 4 x 4. Returns 1.
    int Create() override;
    // Tears the client world down: the camera's target, the objects numbers_ / skyDome_ / bird_, the
    // countedA0_, the ground print lists, the rain and snow layer and the platforms (Destroy), the
    // platform faces, the sector grid's cells, list7BC_, heroes_ and objects_ (released), the session,
    // handle_, CWorld::Destroy, then the owned objects (counted ones dropped first) deleted and cleared.
    void Destroy() override;
    // The client's tick ("Segment"): autosave event and sound volumes on frame 10, the terrain, sound
    // and session updates, the objects and units, effect lists, the lights' grey levels, the map state
    // every 32 ticks. Returns 1; 0 when the session's view has lost its connection (the session is
    // disconnected, FatalError; Rebirth's DisableExitOnCrashReport keeps the session). A plain member as
    // well: the main loop and the harness reach it by its address.
    int Segment();
    int RunTick() override { return Segment(); }
    // world_class_slots.cpp: the class name and id ("CWorldClient" / 7), TexturePath, the managers and
    // the grid moves.
    const char* GetClassName() override;
    int GetClassId() override;
    const char* TexturePath() override;
    // GridHeightAt is ClampedGroundHeight.
    double GridHeightAt(float x, float y) override { return ClampedGroundHeight(x, y); }
    // The height of whatever surface is on top at (x, y): the ground, or the water when the point is
    // submerged (ST0; the sum rounded to float).
    double RawSurfaceHeightAt(float x, float y) override;
    // The client's ground / surface heights (ST0; world_client_maps.cpp): GridHeightAt / RawSurfaceHeightAt
    // at (x, y), stored as a float and raised to the platforms over the point (PlatformSet::SurfaceHeight).
    double GroundHeightAt(float x, float y) override;
    double SurfaceHeightAt(float x, float y) override;
    CMaterialManager* MaterialManager() override;
    CTextureManager* TextureManager() override;
    CBaseManager* ClientLightManager() override;
    CParticleManager* ClientParticleManager() override;
    CEngineObject* ClientManager194() override;
    CEngineObject* ClientManager198() override;
    void MoveObject(CEngineObject* object, const float* position) override;
    virtual void MoveObject2(CEngineObject* object, const float* position);   // the second grid's move

    void BuildUnitSkins(stlp::vector<FiguredUnitRecord>* others, stlp::vector<DressedUnitRecord>* dressed);
    // Draws the world view: view matrix, sky, terrain passes around DrawPass, effects and particles,
    // flying hit points. Returns 1. Rebirth's LandscapeDrawRadius sets the terrain's draw distance (100
    // in the game).
    int Render();
    // The object passes: collects the visible objects of the sectors around the camera
    // (ObjectsDrawRadius widens the 100.0 margin), sorts and draws them, the light effects, shadows and
    // selection markers, then empties the frame's lists. Returns 0 without terrain, else 1.
    int DrawPass();
    // Ground height with the position clamped into the map.
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
    // Lays a decal on the ground into the three arrays (shadows, ground prints). The static builder
    // starts on the quad `corners` projected along `direction` from `point`; the terrain under it adds
    // its triangles unless `skipOnPlatform` is set and the point lies on a platform; with `objects`,
    // every platform face's object that the decal can touch adds the triangles of each part of its
    // figure (transformed by the object's and the part's matrices, on the scene pipeline's stack). An
    // object with several faces adds its parts once per face.
    void ProjectDecal(const Vector3* direction, const Vector3* point, float depth, VertexPacket* const* corners,
                      lib::AlignedArray<VertexPacket>* points, lib::AlignedArray<DecalTexCoord>* texCoords,
                      lib::AlignedArray<std::uint16_t>* indices, std::uint8_t skipOnPlatform, std::uint8_t objects);
    // The ambient sounds' time of day into `out`: "day" from 6 to 18 o'clock, "morning" before 6 and
    // "evening" after 18 within 2..22, else "night". Returns out.
    mfc::CString* AmbienceTimeOfDay(mfc::CString* out);
    // Nearest living unit of the local player's group that `unit`'s side can see (the candidate's
    // relation masks); null when none or `unit` is null.
    CUnit* NearestVisibleUnit(CUnit* unit);
    // The local view (the session's connection; its player_ is the local CPlayer), or null.
    CPlayerClient* LocalView();
    // Whether the local view is in mode 2.
    bool IsLocalViewMode2();
    // world_small_calls.cpp
    // A new texture / material of class `type` from the world's managers (textureManager_,
    // manager188_); null without the manager.
    CTexture* NewTexture(int type);
    CMaterial* NewMaterial(int type);
    // Takes `camera` as the view camera: the old one loses a reference (it is not released at zero), the
    // new one gains one; either may be null.
    void SetCamera(CCamera* camera);
    // The dword of message 0x12 (CPlayerClient::OnWorldValue) stored in worldValue_.
    void SetWorldValue(int value);
    // Message 0x73: the water levels read from `stream` and applied to the terrain (TweenLoad). Returns
    // 1.
    int ReadWaterLevels(CStream* stream);
    // FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // Index of the exit whose area contains `point` and that leads somewhere.
    BOOL FindExitAt(const float* point, int* index);
    // Centre of exit `index`'s rectangle.
    float* ExitCentre(float* out, int index);   // returns out
    // One ZoneExit particle source (0x2038) per exit that leads somewhere, in countedA0_ at the exit's
    // index (others null; the old markers released): at the rectangle's centre, half extents and the
    // angle's cos / sin, ticked 20 times; then UpdateExitMarkers.
    void CreateExitMarkers();
    // Whether the party can leave through `exit`: every living unit of `records` (unit id in the
    // record) stands in an exit (FindExitAt) and that exit is `exit`. Those that do are listed in
    // `inExit` (emptied first). 0 when no unit lives.
    BOOL PartyInExit(const RecordPtrArray* records, int exit, RecordPtrArray* inExit);
    // Makes the bird (bird_, a CEffectMoshka) when there is none, resets it and has it follow
    // the unit of the local party's first record.
    void SummonBird();
    // A quest variable "q.<quest>.<name>" changed to `value`: every filed object whose name is <name>
    // (the text after the second dot) gets the visibility level `value`; a value of 2 (or NaN) with a
    // <name> without a dot summons the bird (as SummonBird).
    void OnQuestVariable(const char* name, float value);
    // The party variable of an object's name: "q.<local player's name>.<name>", constructed in `out`;
    // returns `out`.
    mfc::CString* ObjectVariableName(mfc::CString* out, const char* name);
    // Used when the client's party variables arrived. Every filed object with a name gets the
    // visibility level of its variable (ObjectVariableName) in the local session's variables.
    void ApplyObjectVariables();
    // Flags describing the current environment for `unit`: unit state, island (gipat/ingos/suslanger),
    // night (0x100) or day (0x80), and two world settings.
    std::uint32_t EnvironmentFlags(CUnit* unit);

    // world_client_view.cpp
    // Walks one object list (a map sector's, or the deferred unit list), tests each object with a model
    // against the view frustum and files it in the frame's draw lists. `view` is the camera matrix
    // (M * v), `animTime` the game time handed to units' animation virtual, `now` the timeGetTime() of the frame.
    // With `animateUnits` 0 units are only queued on the deferred list; otherwise they are animated
    // (animation virtual) and filed like other objects. `withScripted` lets heroes with a text-key id through.
    // Returns how many objects were queued for the model-touch list.
    int CollectVisible(stlp::list<CEngineObject*>* list, const Matrix4* view, float animTime, std::uint32_t now,
                       char animateUnits, char withScripted);
    // The ground point under screen pixel (x, y): inverts the current view matrix and iterates on the
    // terrain height (Terrain::HeightAt). Returns out (x, y, z).
    Vector3* PickGround(Vector3* out, int x, int y);
    // Same with the client's surface height (SurfaceHeightAt: platforms included).
    Vector3* PickSurface(Vector3* out, int x, int y);

    // world_client_maps.cpp
    // Files an object in the client's maps: an object whose id is already in objects_ is only referenced
    // and released. Otherwise: objects answering IsSynchronised are added to dynamicObjects_ and
    // dynamicNodes_ (id -> list node); classes 0x54 / 0x5D add their platform items (flag 0x400) to the
    // platform set; units fade in through their controller in a multiplayer session of kind 1 (ticksRun_
    // >= 1, unit field clear); then objects_ (with a reference), objectGrid_ (removed first, then
    // appended) and, for heroes with a text-key id, heroes_. Returns 1.
    int AddObject(CMapObject* object);
    // When an object with `object`'s id is filed (objects_): its platform faces are removed and, for
    // classes 0x54 / 0x5D, its platform items added again where it now stands (as AddObject).
    void RefreshPlatforms(CMapObject* object);
    // Files `object` in dynamicObjects_ and dynamicNodes_ (as AddObject does) unless its id is there
    // already. Returns whether it was added (callers do not read the result).
    bool QueueSynchronised(CEngineObject* object);
    // Called by the options' OK after a shadow switch changed. Every filed object whose kind has no
    // shadows drops its shadow.
    void DropShadows();
    // Takes `object` out of the selection list list7BC_, its highlight off (flags 0x110 cleared, side
    // half's highlight virtuals with 0) when it was on; with null every listed object's highlight goes off and
    // the list empties.
    void Deselect(CEngineObject* object);
    // Adds `object` (not null) to the selection list: its part bounds refreshed
    // (CObject3D::UpdatePartBounds), the highlight on (flag 0x10; side half's highlight with 1 unless flag
    // 0x100 is set, then the other one with 1) when it was off, and its id appended unless it is listed
    // already.
    void Select(CEngineObject* object);
    // Draws every line list of lineLists_ (with the scene pipeline and the renderer). Returns 1.
    int DrawLineLists();
    // One more reference on every item (the item manager's) and, when the item factory exists (tested
    // again, not the spell manager), on every spell (the spell manager's).
    void ReferenceItemsAndSpells();
    // Used by the teardown. The reverse: every item and then every spell released (Release), each manager
    // then emptied (DestroyAll).
    void ReleaseItemsAndSpells();
    // The items and spells replaced: all released, then (a block with data) the spell and item managers
    // loaded (Load, then their objects) and referenced again. Returns 1.
    int LoadItemsAndSpells(CStream* block);
    // The block read from its start: the spells and then the items it lists (LoadManagerObjects). 0 when
    // the block fails.
    int LoadNewItemsAndSpells(CStream* block);
    // Takes the object `id` out of the maps: the platform-set face entries for it, its dynamicObjects_
    // node and dynamicNodes_ entry, then (when it is in objects_) the heroes_ entries of a hero with a
    // text-key id (and its controller's slot in a session of kind 3), its grid cell and objects_; the
    // object is released.
    void RemoveObject(int id);
    // Empties the maps: both sector grids' cells, the platform faces and dynamicObjects_, dynamicNodes_,
    // list7BC_ and heroes_, the objects_ entries with a positive id (released), the session's player
    // data, the terrain manager's ClearState and the managers' records.
    void ClearMaps();
    // The zone's leftovers dropped: the counted array countedA0_ emptied (its own SetSize releases
    // them), the second grid's cells cleared (counted elements released), every timed object released,
    // the bird reset.
    void ResetZoneObjects();
    // Patched by Rebirth, so hooked in the vanilla launch only: the player's client reaches it through
    // its address. The zone's state block: the flow acknowledges the server, sounds stop, then from
    // `block` the tick, the map and .mob names, the exits, the cave flag and the unit looks; the local
    // server makes sure it has the zone; the ground prints, ResetZoneObjects, ClearMaps, the items and
    // spells and the idle particle sources go; the terrain loads the map (a name without a drive from
    // the maps folder) with the allod of the local player's zone, the sector grids take its size (cells
    // of 16), the sky, lights table, exit markers, lighting and music follow the cave flag, the local
    // server's pending messages are appended to the view's, the unit skins are built and the view
    // resumes. Loading movie steps 8-11, then closed. Returns 1, or the result of the fatal error when
    // the map does not load.
    int LoadZone(CStream* block);
    // The local session takes a new link: the single-player loopback pair for `player` (CWorld's
    // teardown first), a replay of the recording `path`, or a driver for the game client's first
    // connection (false without an active client or connection; the argument is unused). Each empties
    // the maps (ClearMaps, ResetZoneObjects) and reconnects the session (CPlayer::Connect null, then the
    // link).
    void SetPlayer(CWorldHandle* player);
    void PlayRecording(const char* path);
    bool JoinGameClient(const char* unused);
    // The player sources and the network session pumped, then the local player's link (when it is
    // active) asked HasIncoming / IncomingCount; false / 0 without one.
    bool PumpHasIncoming();
    int PumpIncomingCount();
    // The local view's owner (the session's connection's player) disconnects (CPlayer::Disconnect).
    // Without a session it reads through null, like the original.
    void DisconnectLocalPlayer();
    // The object at screen point `point` (x, y): the screen entries of screenEntries_ that contain it
    // and whose object answers CollectBounds or CollectParts go to hits_, which is sorted front to back (DepthLess with
    // the view matrix); the first entry whose object still exists wins (hitCursor_ is left on it). Null
    // if none.
    CEngineObject* PickAt(const struct tagPOINT* point);
    CEngineObject* PickAt2(const struct tagPOINT* point);
    // The unit at screen point `point`: from PickAt2's object on, the first unit among the sorted hits
    // (hitCursor_ moves past the others and those whose object is gone); failing that the same from
    // PickAt's. Null if none.
    CUnit* UnitAt(const struct tagPOINT* point);
    // The same from PickAt2's object only (no second pick).
    CUnit* UnitAt2(const struct tagPOINT* point);
    // Every object of the frame's window list that still exists and whose side half has a drawn part
    // meeting the screen rectangle `rect` is appended to `out`.
    void UnitsInBox(const long* rect, stlp::list<CEngineObject*>* out);
    // Marks the footprints within their scale times `range` of the point (x, y): MarkWithinRadius over
    // the list.
    void MarkFootprintsInSight(const float* point, float range);
    // The wind: stored (CWorld::SetWind), given to the terrain's waves (a copy of the direction) and to
    // the rain and snow layer.
    void ApplyWind(const Vector3* direction, float strength);
    // A number over the unit with id `id` (the flying hit points); nothing for another object or without
    // them.
    void ShowNumber(int id, int value, std::uint32_t kind);
    // Used in the client's game state 3. Every unit among objects_ has its controller load the face
    // texture.
    void LoadUnitFaces();
    // The target zone of exit `index` (the original copies the exit out of the array and returns the
    // copy's text, which shares the element's buffer).
    const char* ExitTarget(int index);
    // Removes the units of a record array (each record's unit id) from the world.
    void RemoveUnitsOf(const RecordPtrArray* records);
    // The local client sends its message 0x10 with `value`.
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
    // Creates the party's heroes from their records (prototype, second prototype, id, model, skin,
    // stats, position, weapons, quick items and armour as pointer arrays with their counts after): the
    // figure from the part names (the hair "hr.%.2d" by the prototype's number), items, player; each
    // unit is filed with AddObject. Armour of type 0 is put on only with `allArmour`. Stops at the first
    // unit that cannot be created or given a figure.
    void CreatePartyUnits(const RecordPtrArray* party, char allArmour);
    // Appends to the plain pointer list `out` every object of the sector grid's cells around `point`
    // whose (x, y) lies within radius + hypot(rangeX_, rangeY_) of the point. The cell range (truncated
    // and clamped to the terrain) and the walk's cursor are left in rangeFirstX_ .. rangeY_. The
    // original's z difference is the point's z minus itself (0, kept).
    void ObjectsInRange(const Vector3* point, float radius, stlp::list<CObject3D*>* out);
    // Reads the light colours from config\<file> (an .ini, LightsGipat.ini): each key of [sunlight],
    // [ambient] and [sky] names its hour in characters 4..5 ("hour07") and holds "r, g, b" (0..255,
    // scaled by 1/255, alpha 1); the hours between two keys in file order are filled by linear steps
    // from the earlier key (the first from hour 0). Hours are not range-checked. Returns 1.
    int LoadLights(const char* file);
    // The frame's light from the time of day (the exit markers first): fog start, sun direction (the
    // hour turns it once around +z, its elevation peaks at noon), sunlight, ambient and sky colours
    // interpolated between the hour's table entries; the renderer's sun, ambient and fog colour, the
    // sky's lighting; once per new hour the terrain's sun. A cave takes hour 0's colours and a fixed
    // sun. Returns 1 when a new hour began (or the renderer's flag bit 0 is clear: nothing done), else
    // 0; no caller reads it.
    int UpdateLighting();
    // With one marker per exit (countedA0_, ZoneExit particle sources): a marker whose target zone's
    // party variable "z.<zone>" is 1 (or NaN) gets extent (1, 0, 0) (its particles turn red), the
    // others (0, 0, 0).
    void UpdateExitMarkers();
    // The texture manager gets `device`; the platforms and sky; the digit sprites (numbers_) with the
    // "numbers" texture (kind 6; flag 0x40000); a new figure loader for the figures resource (the
    // CString) in figureLoader_ (counted, the old one released); then the unit database counts the
    // sound files of sounds_.
    void SetUpGraphics(void* device);
    // Once (skyDome_ null): a new CSky; the figure manager makes a CSkyObject (class 0x44, id -666)
    // named "nask0" that loads "nask0sky.fig" from the figure resources and becomes the sky's object
    // (flag 0x10), with a material of type 0x12. Without the file the object is referenced and released
    // again.
    void LoadSky();

    // The local session as the maps read it.
    CPlayer* Session() const { return session_; }

    int nextClientObjectId_;              // -100000 after Setup; NextClientObjectId counts it down
    std::uint32_t debugFlags_;            // the debug display's switches (bits 0-3: ShowFps ...)
    ZoneExits exits_;
    // The exits' particle markers, one per exit or null (MFC CArray of counted pointers; its own
    // SetSize releases the dropped ones; CreateExitMarkers / UpdateExitMarkers).
    CountedPtrs<CParticleSource> countedA0_;
    SectorList heroes_;                   // heroes with a text-key id (counted)
    MapObjectList objects_;               // every filed object (one reference each)
    // The objects answering IsSynchronised, with their nodes by id in dynamicNodes_ and the highest id
    // in dynamicMaxId_ (Segment drops those whose update returns 0).
    mfc::CList<CEngineObject*> dynamicObjects_;
    PendingMap dynamicNodes_;             // id -> its node in dynamicObjects_
    int dynamicMaxId_;
    stlp::list<ScreenEntry> screenEntries_;   // the window rectangles of the frame
    ScreenHits hits_;                     // the entries under the point (PickAt)
    ScreenEntry* hitCursor_;              // PickAt's walk, left on the entry it returns
    std::uint8_t unused_104[0x108 - 0x104];
    stlp::hash_map<unsigned, void*, stlp::IntHash> lineLists_;   // line lists by key (DrawLineLists)
    int lastLineListKey_;                 // the last line list key handed out; 0 from the constructor
    SectorGrid objectGrid_;               // the objects by sector (MoveObject moves them)
    // ObjectsInRange's cell range and its walk's cursor.
    std::uint32_t rangeFirstX_, rangeFirstY_, rangeLastX_, rangeLastY_, rangeX_, rangeY_;
    SectorGrid secondGrid_;               // (MoveObject2 moves objects in it)
    std::uint8_t unused_168[0x180 - 0x168];
    Terrain* terrain_;                    // counted
    // The zone's allod in lower case ("gipath" after Setup; the zone load takes it from the map
    // database's record of the local player's zone, "gipat" without one). It names the terrain's
    // texture pages, the lights table and the music theme.
    mfc::CString zoneName_;
    CMaterialManager* manager188_;        // counted
    CTextureManager* textureManager_;     // counted
    CBaseManager* manager190_;            // counted
    CEngineObject* manager194_;           // (ClientManager194 returns it; not identified)
    CEngineObject* manager198_;           // (ClientManager198 returns it; not identified)
    CParticleManager* particleManager_;        // counted
    CLightningManager* listManager_;      // owned (lightning.h)
    ClientArrayManager* arrayManager_;    // owned
    struct CMusicSystem* music_;          // owned (sound.h)
    struct CSky* skyDome_;                // the sky (owned; sky_object.h)
    CEffectMoshka* bird_;                 // the bird following the party (SummonBird), owned
    CFlyingHitPoints* numbers_;           // owned
    // The ground prints (ground_print.h; element destructors). Segment ticks them and colours
    // the blood prints by the light.
    stlp::list<BloodPrint> bloodPrints_;
    stlp::list<Footprint> footprints_;
    stlp::list<GroundPrint> scorchMarks_;
    // Erase(pos) of the three lists: the node unlinked, its print destroyed (GroundPrint::Destruct), the
    // node deleted (operator delete: the nodes are over 128 bytes); *out the node that followed it.
    // Return out.
    static void** __fastcall EraseBloodPrint(stlp::list<BloodPrint>*, void*, void** out, stlp::list<BloodPrint>::Node* pos);
    static void** __fastcall EraseFootprint(stlp::list<Footprint>*, void*, void** out, stlp::list<Footprint>::Node* pos);
    static void** __fastcall EraseScorchMark(stlp::list<GroundPrint>*, void*, void** out,
                                             stlp::list<GroundPrint>::Node* pos);
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
    mfc::CString texturePath_;            // <data path>textures\ (Setup)
    int constant228_;                     // 10 from the constructor; nothing in the exe reads it
    // The light colours by hour (0..23; r, g, b, 1) from the lights table's [sunlight], [ambient] and
    // [sky] sections (LoadLights).
    // [sunlight], [ambient] and [sky] sections (LoadLights).
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
    // The rain / snow layer, built and destroyed in place with the world (world_construct.cpp,
    // world_destructors.cpp); RainSnow() is the object. Its `mode` (1 rain, 2 snow) picks the ground
    // prints' wet look and an environment flag.
    alignas(4) std::uint8_t rainSnow_[0xC8];
    CRainSnow* RainSnow() { return reinterpret_cast<CRainSnow*>(rainSnow_); }
    const CRainSnow* RainSnow() const { return reinterpret_cast<const CRainSnow*>(rainSnow_); }
    stlp::list<int> list7BC_;             // the selected objects' ids (nodes of 0xC bytes)
    int worldValue_;                      // -1; message 0x12 sets it (SetWorldValue); the exe never reads it
    int constants7C4_[3];                 // 0 from the constructor; nothing in the exe reads them
    void* cursor_;                        // HCURSOR wincurs\Df0000.cur
    // The window's names, set by the constructor and freed by the destructor only: "EvilIslands",
    // "EvilIslandsClass", "EvilIslands", "EvilIslands Paused".
    mfc::CString appName_;
    mfc::CString windowClass_;
    mfc::CString windowTitle_;
    mfc::CString pausedTitle_;
};
static_assert(offsetof(CWorldClient, nextClientObjectId_) == 0x84);
static_assert(offsetof(CWorldClient, countedA0_) == 0xA0);
static_assert(offsetof(CWorldClient, heroes_) == 0xB4);
static_assert(offsetof(CWorldClient, objects_) == 0xB8);
static_assert(offsetof(CWorldClient, dynamicObjects_) == 0xBC);
static_assert(offsetof(CWorldClient, dynamicNodes_) == 0xD8);
static_assert(offsetof(CWorldClient, dynamicMaxId_) == 0xEC);
static_assert(offsetof(CWorldClient, screenEntries_) == 0xF0);
static_assert(offsetof(CWorldClient, hits_) == 0xF4);
static_assert(offsetof(CWorldClient, hitCursor_) == 0x100);
static_assert(offsetof(CWorldClient, rangeFirstX_) == 0x138);
static_assert(offsetof(CWorldClient, rangeY_) == 0x14C);
static_assert(offsetof(CWorldClient, secondGrid_) == 0x150);
static_assert(offsetof(CWorldClient, manager188_) == 0x188);
static_assert(offsetof(CWorldClient, particleManager_) == 0x19C);
static_assert(offsetof(CWorldClient, bird_) == 0x1B0);
static_assert(offsetof(CWorldClient, bloodPrints_) == 0x1B8 && offsetof(CWorldClient, scorchMarks_) == 0x1C0);
static_assert(offsetof(CWorldClient, handle_) == 0x1C8);
static_assert(offsetof(CWorldClient, sounds_) == 0x1CC);
static_assert(offsetof(CWorldClient, texts_) == 0x1D0);
static_assert(offsetof(CWorldClient, texturePath_) == 0x224);
static_assert(offsetof(CWorldClient, lightingMark_) == 0x6DC);
static_assert(offsetof(CWorldClient, frameAmbient_) == 0x6AC && offsetof(CWorldClient, frameSun_) == 0x6CC);
static_assert(offsetof(CWorldClient, ticksRun_) == 0x6EC);
static_assert(offsetof(CWorldClient, rainSnow_) == 0x6F4);
static_assert(offsetof(CWorldClient, list7BC_) == 0x7BC);
static_assert(offsetof(CWorldClient, cursor_) == 0x7D0);
static_assert(offsetof(CWorldClient, exits_) == 0x8C);
static_assert(offsetof(CWorldClient, objectGrid_) == 0x120);
static_assert(offsetof(CWorldClient, terrain_) == 0x180 && offsetof(CWorldClient, zoneName_) == 0x184);
static_assert(offsetof(CWorldClient, platforms_) == 0x1D4);
static_assert(offsetof(CWorldClient, session_) == 0x20C);
static_assert(offsetof(CWorldClient, camera_) == 0x210);
static_assert(offsetof(CWorldClient, textureManager_) == 0x18C);
static_assert(offsetof(CWorldClient, numbers_) == 0x1B4);
static_assert(offsetof(CWorldClient, sunlight_) == 0x22C);
static_assert(offsetof(CWorldClient, sky_) == 0x52C);
static_assert(offsetof(CWorldClient, constant228_) == 0x228 && offsetof(CWorldClient, constant6E8_) == 0x6E8);
static_assert(offsetof(CWorldClient, lastLineListKey_) == 0x11C && offsetof(CWorldClient, constants7C4_) == 0x7C4);
static_assert(offsetof(CWorldClient, appName_) == 0x7D4 && sizeof(CWorldClient) == 0x7E4);

// MatrixRows (the first three rows of a 4x4 matrix; Transform) is in src/lib/vecmath.h.

// Sorting predicates passed to STLport sort (operator() of empty or small functors).
// Orders screen entries (their objects by objectId) by distance along the view's depth axis; dead and
// living objects are separated first.
struct DepthLess {
    float* view;                          // the view matrix (row-major 4x4): row 2 the depth axis, its
                                          // translation the offset
    CBaseManager* manager;                // the manager holding the objects
    bool Less(const ScreenEntry* a, const ScreenEntry* b);
};
// The draw lists' order (DrawPass): by the first part set's texture, then the figure, then the view
// depth. The original compares the texture and the figure by address; ours by name
// (address_order::DrawBefore).
struct UnitOrderLess {
    bool Less(CEngineObject* a, CEngineObject* b);
};
// Orders the client's variable nodes (ClientSession::variables_) by their stamp
// (VariableMessage::serial_).
struct EntryIdLess {
    BOOL Less(const stlp::list<RefPtr>::Node* a, const stlp::list<RefPtr>::Node* b);
};
// world_stamp_sort.cpp. STLport's sort of the changed variables' nodes with EntryIdLess (the flow's
// update): __introsort_loop, __final_insertion_sort; first, last, then a dummy value-type pointer, the
// depth limit and the comparator.
void __fastcall StampIntroSort(stlp::list<RefPtr>::Node** first, stlp::list<RefPtr>::Node** last, int valueType, int depthLimit,
                               const void* comp);
void __fastcall StampInsertionSort(stlp::list<RefPtr>::Node** first, stlp::list<RefPtr>::Node** last, const void* comp);

// Game interface glue: small free functions the scripts and menus call.
// Whether a multiplayer client session is active.
BOOL IsMultiplayerClient();
// Game clock: whole ticks + fraction.
float GameTime();
// The local player's data block.
class CParty* LocalPlayerData();
// The local player's name (copy constructed into `out`, returned).
mfc::CString* __stdcall LocalPlayerName(mfc::CString* out);
// Float variable `name` of the local player as the client sees it.
float __stdcall LocalPlayerVariable(const char* name);
// Float variable `name` of the local player: the client's view in multiplayer, player 0
// of the server otherwise (0 without that player).
float __stdcall PlayerZeroVariable(const char* name);

// The game flow object. Only the functions rewritten here are declared.
struct GameFlow {
    // Saves the game into saves\<slot>\ (single player only): empties the folder, copies the visited
    // zones' files from saves\current, writes scenario.sav (the party records, input and scripts state)
    // and, outside the cutscene zones, mission.sav (the world, SaveWorld). Without scenario.sav the game
    // goes back to the main menu. Returns 1 when saved.
    int SaveGame(const char* slot);
    // Fills the zone table once, while it is empty: for every "*.mq" file of the maps
    // folder, its title in lower case is registered as a quest folder (RegisterQuests) and the
    // quest texts loaded so far are unloaded again; a zone the map database knows gets an entry
    // {the title, the record's map name, the allod}: 0 / 1 / 2 by the three map name lists
    // (zone2..zone9, zone12 / zone13, zone17 / zone18), else 3.
    void BuildZoneTable();
    // Used each network step. The local player's "z.MPGame1" variable, set to 1 or removed: on a client,
    // set unless the client session's field at +0x25EC is active; on the host, set when more than one
    // player is in (flag80_) or unless the client's +0x25F4 is active. Nothing without an active client
    // session.
    void UpdateMultiplayerVariable();
    // The multiplayer start zone for the client session's game type (+0x261C): "bz2mpg".."bz4mpg" for
    // 1..3, else "bz1mpg"; constructed into `out`, returned.
    mfc::CString* MultiplayerZoneInto(mfc::CString* out);
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
    // A multiplayer client asks the server to complete `briefing` (session command 1).
    void RequestBriefing(const char* briefing);
    // Removes the local player's variable of `briefing`.
    void ForgetBriefing(const char* briefing);
    // Replaces the game client session with a new one, releasing the old one.
    void RecreateClientSession();
    // SaveGame as a thunk the menus call.
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
    static void __stdcall OnVariableChanged(const stlp::list<RefPtr>::Node* node);
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
    // Joins as a client only: the server session dropped, a new client session, then the second.
    void StartClient();
    // The client session joins the server at `address` with the password (20 bytes, zero-padded) as its
    // hello.
    void JoinServer(const net::NetAddress* address, const char* password);
    // The flow's requests (world_game.cpp): the state (state_) the flow's update acts on next, the zone
    // or save name (requestTarget_), requestArg_ and, for 5 and 6, a copy of a party (requestParty_).
    void RequestState3();
    void RequestLoad(const char* name, char flag);                   // 2, requestArg_ = flag != 0
    void RequestZoneChange(const char* zone, int entrance);          // 4, requestArg_ = entrance
    void RequestPartyZone5(const char* zone, CParty* party);
    void RequestPartyZone6(const char* zone, CParty* party);
    // The client world's local player disconnects (CWorldClient::DisconnectLocalPlayer).
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
    // its members reset, and the client's host record (session +0x2600) takes the hero's and party's
    // names (defaults "Human Hero", "Bug"). Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void AfterServerStart();
    // A multiplayer client's update from the server: the quest texts unloaded, the local player's
    // variables replaced ("constr.current" kept), the zone's quests, the game type's construction
    // variable and briefings (at least 1), the seen briefings, the quest folders of "q." variables, then
    // the server's variables applied in stamp order, and the multiplayer flag variable. Rebirth's
    // PersistentGlobalVars keeps the "gvar." ones.
    void UpdateFromServer();

    // The party of a request (party_storage.h; RequestPartyZone5 / 6 copy one in).
    class CParty* RequestParty() { return requestParty_.Get(); }

    // The constructor body (the flow's static initialiser calls it): empty names, the party constructed,
    // no request; returns this. The destructor body (its atexit function calls it): the names and the
    // party.
    GameFlow* Construct();
    void Destruct();
    // saves\current emptied, the multiplayer zone (zone7C_) named; a host without an active client
    // session starts it when there is a name.
    void SetNetworkZone(const char* zone);

    int state_;                           // the pending request (1..6; ProcessRequest runs it)
    mfc::CString requestTarget_;          // the request's zone or save name
    PartyStorage requestParty_;           // a CParty: RequestParty()
    int requestArg_;                      // the entrance (4) or the load flag (2)
    mfc::CString zone7C_;                 // the multiplayer zone (SwitchMultiplayerZone)
    char flag80_;                         // set by SwitchMultiplayerZone, cleared by the game's start
};
static_assert(offsetof(GameFlow, requestTarget_) == 0x04);
static_assert(offsetof(GameFlow, requestArg_) == 0x78 && offsetof(GameFlow, zone7C_) == 0x7C);
static_assert(offsetof(GameFlow, flag80_) == 0x80);
static_assert(sizeof(GameFlow) == 0x84);

// The game flow (world_game.cpp): built by its static initialiser in the list's order,
// destroyed by the exit function it registers.
extern lib::StaticObject<GameFlow> g_gameFlow;

constexpr int kMpHeroSize = 0x74;
// A multiplayer hero record (the saved parties' entries below, the hero screens' records): its party
// (CParty, 0x70 bytes) and file name.
struct MpHeroRecord {
    // Assignment: the party, the file name. Returns this.
    MpHeroRecord* Assign(const MpHeroRecord* other);
    // Constructor: the party, an empty file name. Returns this.
    MpHeroRecord* Construct();
    // Destructor: the file name, then the party.
    void Destruct();
    // Scalar deleting destructor (the vector's element destructor with flag 0). Returns this.
    MpHeroRecord* DeletingDestruct(unsigned flags);
    // Copy: [first, last) assigned (Assign) to dest onwards; returns the end of the destination.
    static MpHeroRecord* __fastcall CopyRange(const MpHeroRecord* first, const MpHeroRecord* last, MpHeroRecord* dest);
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

    PartyStorage party;              // CParty (party_storage.h)
    mfc::CString file;
};
static_assert(sizeof(MpHeroRecord) == kMpHeroSize);

// The saved multiplayer parties: the folder and an STLport vector of 0x74-byte entries.
struct SavedParties {
    // Rebuilds the list from the folder's "*.mp" files: each is read into party records; a loaded one
    // has its variables emptied and, with a record's field == 1, is appended. Rebirth's
    // PersistentGlobalVars keeps the "gvar." variables.
    void Reload();
    // With a folder name: the folder created and emptied, then every entry written into it
    // (MpHeroRecord::WriteFile).
    void SaveAll();
    // The party outside a game as an MpHeroRecord (file name, then the party), every entry, the vector's
    // storage back to the node allocator, the folder name.
    void Destruct();
    // An empty folder name and vector, the party outside a game as an MpHeroRecord (the party, an empty
    // file name). Returns this.
    SavedParties* Construct();
    // `hero` becomes the party outside a game (MpHeroRecord's assignment).
    void Select(const MpHeroRecord* hero);

    mfc::CString folder_;
    stlp::vector<MpHeroRecord> entries_;
    // The party outside a game (the hero screens' current hero) and a file name.
    MpHeroRecord outsideGame_;
    MpHeroRecord* OutsideGame() { return &outsideGame_; }
};
static_assert(offsetof(SavedParties, outsideGame_) == 0x10);
static_assert(offsetof(SavedParties, entries_) == 0x04);
static_assert(sizeof(SavedParties) == 0x84);
// The saved parties (world_game.cpp), built empty by its static initialiser.
extern lib::StaticObject<SavedParties> g_savedParties;
inline SavedParties& TheSavedParties() { return *g_savedParties; }

// Sets and has the local view ask to go to `zone`; returns what that returns.
int __stdcall RequestZone(const char* zone, int entrance);

// With the briefing texts loaded: every quest text file loaded so far (g_questFiles) is unloaded and
// the list emptied.
void UnloadQuestTexts();
// The quest text files loaded so far (world_briefing.cpp; the files' paths).
extern stlp::vector<stlp::string> g_questFiles;

// Loads the quest texts of `zone`: false without the briefing texts or when the maps folder has no
// "<zone>.mq" (CFileFind). A running game server first lists the file's bytes under "<zone>.mq" for its
// clients (ShareFile); then a path not yet among the quest files joins them and the briefing texts add
// it as a source. Rebirth patches it: hooked in the vanilla launch only.
bool __stdcall LoadZoneQuests(const char* zone);

// Whether `name` is in `list` (an array of names ended by an empty one), compared case-insensitively.
bool __fastcall NameInList(const char* const* list, const char* name);

// Creates the folder and deletes every file in it (CFileFind "*.*"; CreateDirectoryA / DeleteFileA).
void __fastcall ClearSaveFolder(const char* path);

// For the zone table entries with `id`, grouped by their second name in order of first appearance: one
// random entry per group (never the one whose first name is `exclude` when there is a choice; one
// IsaacRandom draw per group) has its first name appended to `out` (an STLport vector of strings).
void __stdcall PickZones(stlp::vector<stlp::string>* out, int id, const char* exclude);

// Reads mission.sav from a file stream (magic 0x102) and restores the world (LoadSave); old versions
// also carry the globals (< 0x52) and the input state (< 0x47). 0 when the file does not read.
int __stdcall LoadMission(CStream* file);

// world_savefile.cpp. SaveFileWrite writes the header, at version > 0x6B a checksum, then a flag byte
// and the LZ-compressed data (or, when decompressing it does not give the data back, 0 and the raw
// data). SaveFileRead (memory stream, magic) reads one back (versions up to
// kSaveLastUncompressedVersion uncompressed); false on a bad header or checksum.
void __fastcall SaveFileWrite(CStream* file, unsigned magic, int version, CMemoryStream* data);
bool __fastcall SaveFileRead(CMemoryStream* out, unsigned magic, int minimum, int maximum, CStream* file);
// The save version written, which is also the newest read, and the oldest version read.
inline constexpr int kSaveVersion = /*00790260*/ 116;
// The newest save version stored uncompressed (SaveFileRead reads those as they are).
inline constexpr int kSaveLastUncompressedVersion = /*00790264*/ 100;
inline constexpr int kSaveOldestVersion = /*00790268*/ 66;

// world_briefing.cpp. The mercenary who speaks `briefing` (the second word of the first "#show" line of
// section "briefing <name>" that is not "Hero"), or "", constructed into `out`. The briefing's
// variable, "b.<speaker>.<briefing>" or, without a speaker, "b.elder.<briefing>".
mfc::CString* __stdcall BriefingSpeakerInto(mfc::CString* out, const char* briefing);
mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing);
// The multiplayer peer's map (the client's session field at +0x25F4 or the server's at +0x25A8, name at
// +0x0C), replaced by the first line of its "quest <name>" section when there is one; "" without a
// peer.
mfc::CString* __stdcall SessionMapNameInto(mfc::CString* out);
// Chooses the loading movie for zone `zone` (may be null) by its allod in the map database:
// progres2.bik for gipat, progres3.bik for ingos, progres4.bik for suslanger, else progres.bik
// (Rebirth's FixProgressNames: progres2.bik for gipat2, progres5.bik for jigran); the zones gz1g and
// bz7g (case-insensitive) take progres1.bik.
void __stdcall SelectLoadingMovie(const char* zone);

// The time speed setting (the options' value; a negative speed asks for it).
inline int TimeSpeedSetting() { return options::TheOptions()->defaultTimeScale_; }
// The display options' distances: the fog start by day, at night, the far clip (the transform
// pipeline's depth range).
inline float FogStartDay() { return options::TheOptions()->fogDayStartDistance_; }
inline float FogStartNight() { return options::TheOptions()->fogNightStartDistance_; }
inline float FarClipDistance() { return options::TheOptions()->farClipDistance_; }

// The client's per-frame draw lists (world_client_view.cpp files the visible objects in them,
// world_client_draw.cpp sorts, draws and empties them): STLport vectors of object pointers, the globals
// in this order (defined in world_client_view.cpp). The deferred unit list follows (an STLport list of
// counted object pointers; nodes of 12 bytes).
using FrameQueue = stlp::vector<CEngineObject*>;
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
extern lib::StaticObject<stlp::list<CEngineObject*>> g_deferredUnits;   // static_lists.cpp
inline stlp::list<CEngineObject*>* FrameDeferredUnits() { return &*g_deferredUnits; }
// The id the next object only the client makes gets (client figures such as a bow shot's arrow); each
// use counts it down.
inline int& NextClientObjectId() { return TheClientWorld()->nextClientObjectId_; }
// The world server's item manager (the item factory) and the manager that owns its objects (units,
// buildings, ...).
inline CItemManager* WorldItemManager() { return TheServerWorld()->itemManager_; }
inline CBaseManager* WorldObjectManager() { return TheServerWorld()->manager40_; }


// A lightning strike from `from` to `to`, as a script line on the queue: one time in four only a
// thunder sound, otherwise a point light at the middle, a sound and two bolts.
void __stdcall ShowBolt(Vector3 from, Vector3 to);

// world_guard.cpp. With `plain` 0 the players' gold, party records (0x704 bytes) and, on the server,
// their units' controller statistics (0x704 bytes) are XORed with a new random key, with 1 they are
// XORed back; also the party records outside a game. Nothing when the state is already `plain`. One
// helper does one player list.
void __stdcall GuardPlayerData(char plain);
void __fastcall GuardPlayers(CPlayerManager* players, char units, std::uint32_t key);

// ---- Client draw lists and their sorting (world_client_view.cpp) --------------------------------
// The client's draw pass (patched by Rebirth) fills per-frame vectors of object pointers with
// CWorldClient::CollectVisible and sorts some of them with STLport's sort, instantiated for two
// orderings. The entry points keep STLport's internal calling convention: first, last, then a dummy
// value-type pointer, the depth limit and the (empty) comparator on the stack.

// STLport __introsort_loop with UnitOrderLess (model rank, a model field, view depth).
void __fastcall IntroSortByModel(CEngineObject** first, CEngineObject** last, void* valueType, int depthLimit,
                                 int comp);
// STLport __final_insertion_sort with UnitOrderLess.
void __fastcall FinalSortByModel(CEngineObject** first, CEngineObject** last, int comp);
// __introsort_loop ordering objects by their model's last use, most recent first.
void __fastcall IntroSortByModelUse(CEngineObject** first, CEngineObject** last, void* valueType, int depthLimit,
                                    int comp);
// __final_insertion_sort with the same ordering.
void __fastcall FinalSortByModelUse(CEngineObject** first, CEngineObject** last, int comp);
// The comparator of that ordering: b's model used before a's.
struct ModelUseLess {
    BOOL Less(CEngineObject* a, CEngineObject* b);
};

// Reads objects up to a zero byte: after each mark an id, a class and a field of the object; an object
// registered under the id is released first (its count raised to 1 when 0), then a new one is created
// (NewObject), loaded (Load) and referenced. 1 at the end mark, 0 when a mark is read from a failed
// stream.
int __fastcall LoadManagerObjects(CBaseManager* manager, CStream* stream);
// Writes what LoadManagerObjects reads: per listed object a 1, its id, type and field, then its own
// Save; a 0 at the end.
void __fastcall SaveListedObjects(mfc::CList<CEngineObject*>* list, CStream* stream);

// The save screens: 1000000 plus the sizes (low dwords) of the files in saves\current.
int CurrentSaveSize();

// Every resource database (units with their sound file counts, skills, spell tables, items) destroyed
// and constructed again in place, then loaded from `name`.
void __fastcall SwitchResourceDatabases(const char* name);
