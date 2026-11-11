// WorldServer: object and player lookups, the coarse height grid, shared objects, dirty flags and
// the id maps. See world.h.
#include "game/app/singletons.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"

#include "game/ai/ai_map.h"
#include "game/core/base_manager.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/core/core_maths.h"
#include "game/core/id_tables.h"
#include "game/objects/map_object.h"
#include "game/net/net_session.h"
#include "game/net/net_link.h"
#include "game/net/net_messages.h"
#include "game/world/hoover.h"
#include "game/players/party.h"
#include "game/players/player_manager.h"
#include "game/data/registry.h"
#include "game/script/script.h"
#include "game/ai/sector_map.h"
#include "game/objects/side_managers.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/map/map_database.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr float kWakeMargin = 20.0f;     // added to the hearing radius when units wake others

// The game server object of a multiplayer session (null or flagged inactive otherwise); its
// FindPlayer is the player record with that id, or null.
net::ServerSession* ActiveGameServer() {
    net::ServerSession* server = net::g_gameServer;
    if (!IsLive(server)) return nullptr;
    return server;
}

// Player index used by the shared-object list: -1 when not in a multiplayer game or unknown.
int SharedListPlayer(int playerId) {
    if (net::ServerSession* server = ActiveGameServer()) {
        if (net::PlayerMessage* record = server->FindPlayer(playerId)) return record->serial;
    }
    return -1;
}

template <typename Map>
void EraseId(Map& map, unsigned id) {
    map.map.erase(id);
}

} // namespace

// ---- Lookups -------------------------------------------------------------------------------------

MapObject* WorldServer::FindObject(unsigned objectId) {
    auto node = objects.map.find(objectId);
    return node != objects.map.end() ? node->second : nullptr;
}

Player* WorldServer::GetPlayer(int playerId) {
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next())
        if (player->playerId == playerId) return player;
    return nullptr;
}

float WorldServer::GridHeight(float x, float y) {
    // Cell size 1.0 at half-cell offset: (1 / (1 * 0.5)) * v - 0.5, rounded through a float.
    int cx = lib::RoundEven(2.0f * x - 0.5f);
    int cy = lib::RoundEven(2.0f * y - 0.5f);
    const AIMap* map = &aiMap;
    if (cx < 0 || cy < 0 || cx >= map->cells.width || cy >= map->cells.height) return 0.0f;
    int raw = map->cells[cy][cx].groundLevel;
    return static_cast<float>(raw) / map->heightScale;
}

int WorldServer::CountLivingMercs(int unitId) {
    Player* player = GetPlayer(unitId);
    if (!player) return 0;
    int living = 0;
    for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) {
        unsigned memberId = player->GetParty()->members[i].unitId;
        Unit* unit = FindUnit(static_cast<int>(memberId));
        if (unit && unit->IsDead() == 0) ++living;
    }
    return living - 1;
}

bool WorldServer::IsObjectShared(int objectId, int playerId) {
    int player = SharedListPlayer(playerId);
    for (const SharedObject& entry : sharedObjects)
        if (entry.objectId == objectId && (entry.player == player || entry.player == -1)) return true;
    return false;
}

void WorldServer::CollectSharedObjects(int playerId, ScriptGroup* group) {
    group->Clear();
    int player = SharedListPlayer(playerId);
    for (const SharedObject& current : sharedObjects) {
        if (current.player != player && current.player != -1) continue;   // -1: every player
        if (Unit* unit = FindUnit(static_cast<int>(current.objectId))) group->Insert(unit);
    }
}

// ---- State ---------------------------------------------------------------------------------------

void WorldServer::MarkPlayersDirty(std::uint32_t mask) {
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty |= mask;
}

void WorldServer::SetCave(int value) { cave = value; }

void WorldServer::SetClimate(int value) {
    // The zone's own climate set again after a revisit's load keeps the weather it was left with.
    if (value != climate && weather != 0) {
        weather = 0;
        weatherStrength = 20.0f;
        MarkPlayersDirty(lib::Bits(PlayerDirty::Weather));
    }
    climate = value;
}

void WorldServer::SetWeather(int newWeather, float strength) {
    weather = newWeather;
    weatherStrength = strength;
    MarkPlayersDirty(lib::Bits(PlayerDirty::Weather));
}

void World::SetPaused(std::uint8_t value) { paused = value; }

void World::SetServerPaused(std::uint8_t value) {
    const net::ServerSession* server = ActiveGameServer();
    if (value != 0 && server && server->pauseMode == net::PauseMode::Off) return;
    SetPaused(value);
}

const ZoneExit* WorldServer::FindZoneExit(int index) const {
    if (index < 0 || index >= std::ssize(exits)) return nullptr;
    return &exits[index];
}

void WorldServer::AddTimedObject(Effect* effect) {
    // The timed objects are the world's effects (world_effect.h): the tick they began.
    effect->startTick = tick;
    effect->AddRef();
    effect->MarkChanged();   // flags |= 2, stamped with the server time
    unsigned effectId = static_cast<unsigned>(effect->id);
    timedObjects.Set(effectId, effect);
}

void WorldServer::RemoveTimedObject(unsigned objectId) {
    auto node = timedObjects.map.find(objectId);
    if (node == timedObjects.map.end()) return;
    EngineObject* object = node->second;
    object->flags |= MapObjectFlag::Removed;
    object->Release();
    // Looked up again: releasing may have changed the map.
    if (auto again = timedObjects.map.find(objectId); again != timedObjects.map.end()) timedObjects.map.erase(again);
}

int WorldServer::UpdateCountdownFor(const EngineObject* object) { return (object->id & 0xF) + 0x80; }

void WorldServer::AddToUpdateMap(MapObject* unit) {
    // The unit's update slot.
    unit->updateCountdown = UpdateCountdownFor(unit);
    unsigned unitId = static_cast<unsigned>(unit->id);
    updateMap.Set(unitId, unit);
}

void WorldServer::FileMapObject(MapObject* object, bool updated, bool inSectorMap) {
    const unsigned objectId = static_cast<unsigned>(object->id);
    objects.Set(objectId, object);
    mapObjects.Set(objectId, object);
    if (object->GetClassId() != ClassId::Unit) otherMapObjects.Set(objectId, object);
    if (updated) AddToUpdateMap(object);
    if (inSectorMap) aiMap.sectorMap.Add(object);
    if (object->flags & MapObjectFlag::Changed) object->WriteChangedState();
}

// ---- Removal ---------------------------------------------------------------------------------------

void WorldServer::RemoveObject(unsigned objectId) {
    auto found = objects.map.find(objectId);
    MapObject* const* slot = found != objects.map.end() ? &found->second : nullptr;
    // An unknown id is ignored (Rebirth's FixRemoveMapObject; the original read through the null pointer).
    if (!slot) return;
    MapObject* object = *slot;
    if (!object) return;
    if (object->GetClassId() != ClassId::MagicTrap) aiMap.sectorMap.Remove(*slot);    // magic traps are not in it

    idtable::RemoveKey(updateMap.map, objectId);
    EraseId(objects, objectId);
    if (namedObjects.map.contains(objectId)) EraseId(namedObjects, objectId);
    if (sentObjects.map.contains(objectId)) {
        EraseId(sentObjects, objectId);
    } else {
        EraseId(mapObjects, objectId);
        if (otherMapObjects.map.contains(objectId)) EraseId(otherMapObjects, objectId);
    }

    object->ClearState();   // it leaves the server
    Unit* unit = UnitOrNull(object);
    object->AddRef();   // held to the end, released twice there (with the maps' reference)
    if (unit) unit->SetPlayer(nullptr);
    object->flags |= MapObjectFlag::Removed;
    MainScript()->PurgeDeletedObjects();     // what refers to removed objects dropped
    SecondScript()->PurgeDeletedObjects();

    // Every world object forgets it (the walk stops at an entry without an object).
    for (auto node = objects.map.begin(); node != objects.map.end() && node->second; ++node)
        node->second->ForgetObject(object);
    // The missiles it shot are dropped; those aimed at it lose their target.
    for (auto node = projectiles.begin(); node != projectiles.end();) {
        auto current = node++;
        // 0 when the missile's owner left (the missile is then dropped).
        if (!(*current)->Forget(object)) projectiles.erase(current);
    }
    PlayerIterator playerIt;
    for (Player* player = playerIt.First(PlayerList()); player; player = playerIt.Next())
        player->Server()->ForgetObject(object);
    object->Release();
    object->Release();
}

float WorldServer::RawSurfaceHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    return aiMap.TopHeightAt(point);
}

float WorldServer::GroundHeightAt(float x, float y) { return GridHeightAt(x, y); }

float WorldServer::SurfaceHeightAt(float x, float y) { return RawSurfaceHeightAt(x, y); }

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kWorldServerClassName = "CWorldServer";
} // namespace

const char* WorldServer::ClassName() { return kWorldServerClassName; }
ClassId WorldServer::GetClassId() { return ClassId::WorldServer; }

// --- The water level, which other code reaches through the world --------------------------------------

void WorldServer::SetWaterLevel(int key, float target, int steps) {
    countdowns.SetTarget(key, target, steps);
}

// --- Life --------------------------------------------------------------------------------------------

// WorldServer's own Create (Setup) and Destroy (Teardown). WinMain calls both on the global server: Setup at
// start-up, Teardown at exit. See world.h and docs/notes/world.md.

namespace {

// The messages registry's file name.
constexpr const char* kMessagesFileName = /*0073F918*/ "SMessBase.reg";
// The fatal error texts.
constexpr const char* kCantOpenText = "CWorldServer::Create.\n Can't opent message base resource.\n";

constexpr float kWindStrength = 0.4f;

} // namespace

void WorldServer::Create() {
    Destroy();
    World::Create();
    if (!messages.Open(g_textFolder + kMessagesFileName)) game::FatalExit("%s", kCantOpenText);

    specificManager = std::make_unique<ServerSpecificManager>();
    specificManager->SetContext(this);
    hoover = std::make_unique<Hoover>();

    // The wind: along x, 0.4.
    windDirection = {1.0f, 0.0f, 0.0f};
    windStrength = kWindStrength;
    zoneMode = ZoneMode::None;
    scriptsActive = 0;
    exits.clear();
    climate = 1;
    cave = 0;
    loadedFresh = 1;
    keepsState = 1;
    zoneName.clear();
    gameState = GameState::None;
    mobFiles.clear();
}

void WorldServer::Destroy() {
    // Every player disconnected and released. The iterator drops its count of the player list when it goes out
    // of scope: before World::Destroy deletes the list.
    if (PlayerList()) {
        PlayerIterator it;
        for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
            player->Disconnect();
            player->Release();
        }
    }

    hoover.reset();
    projectiles.clear();   // the casts before the scripts that run them
    MainScript()->Reset();
    SecondScript()->Reset();
    ResetState();
    messages.Close();

    // Fresh object maps, as at the program's start: a cleared one keeps the buckets it grew to, and with them the
    // order its walk takes (which unit thinks first) at the next start (a switch of mods).
    for (MapObjectIdMap* map : {&objects, &sentObjects, &mapObjects, &otherMapObjects, &updateMap, &namedObjects}) {
        map->map = decltype(map->map)();
        map->maxId = 0;
    }
    timedObjects.map = decltype(timedObjects.map)();
    timedObjects.maxId = 0;
    exits = {};
    World::Destroy();
}

// --- World update ------------------------------------------------------------------------------------

// WorldServer::Update: one world update. See world.h and docs/notes/world.md.

namespace {

// -1 while no Possession spell has taken a unit.
bool NoPendingPossession(const PlayerServer* state) { return static_cast<int>(state->possessedId) < 0; }

// ---- Helpers ----------------------------------------------------------------------------------

// Disconnects a player (its connection and units).
// A player that lost its connection leaves the game.
void DropPlayer(Player* player) {
    player->Disconnect();
    player->Release();
    console::EndLine(console::g_netLogOut << "Disconnecting player " << player->playerId);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer computes it, with
// another margin).
void WakeAroundUnits(WorldServer* world, Player* player) {
    auto& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        Unit* unit = node->second;
        world->WakeUnitsAround(unit->localPosition, HearingRadius(unit, kWakeMargin));
    }
}

} // namespace

void WorldServer::Update() {
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        netlink::NetLink* link = player->link;
        if (link && link->Connected() && player->connection->Receive() != 0)
            WakeAroundUnits(this, player);
        else
            DropPlayer(player);
    }

    if (WorldHandle* handle = worldHandle.Get()) {
        AcceptNewPlayers(handle);
    }
    if (netlink::DriverSession* session = netSession.Get()) {
        net::ServerSession* server = net::g_gameServer;
        if (IsLive(server)) session->PumpDrivers(server);
        AcceptNewPlayers(session);
    }

    if (!paused) {
        for (Player* player = it.First(PlayerList()); player; player = it.Next())
            player->Tick();
        Tick(static_cast<char>(0));
    }
    for (Player* player = it.First(PlayerList()); player; player = it.Next())
        player->Server()->UpdatePartyUnits();

    // The local players' heroes: the global "Heroes" group plus each one's own heroes, then given
    // back to every local player.
    ScriptGroup heroes = MainScript()->GlobalGroup("Heroes");
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        if (!NoPendingPossession(state)) continue;
        ScriptGroup sum;
        sum.Union(state->partyUnits, heroes);
        heroes = std::move(sum);
    }
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        if (!NoPendingPossession(state)) continue;
        state->partyUnits = heroes;
    }

    for (Player* player = it.First(PlayerList()); player; player = it.Next())
        if (!SyncPlayer(player)) DropPlayer(player);

    ++updateCount;
}

// --- The id maps -------------------------------------------------------------------------------------

// The world's timed objects by id (EffectIdMap, world.h). Main thread. See docs/notes/core.md.
void EffectIdMap::Set(const unsigned& key, Effect* const& value) {
    if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
    map[key] = value;
}
