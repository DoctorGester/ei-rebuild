// WorldServer: object and player lookups, the coarse height grid, shared objects, dirty flags and
// the id maps. See world.h.
#include "player_messages.h"

#include "ai_map.h"
#include "base_manager.h"
#include "combat.h"
#include "console.h"
#include "core_maths.h"
#include "game_strings.h"
#include "hash_tables_400.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_link.h"
#include "net_messages.h"
#include "object.h"
#include "party.h"
#include "player_manager.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "server_specific_manager.h"
#include "spell.h"
#include "unit.h"
#include "world.h"
#include "world_data.h"
#include "world_effect.h"
#include "ei/rebirth.h"
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

// The game server object of a multiplayer session (null or flagged inactive otherwise); its
// FindPlayer is the player record with that id, or null.
net::ServerSession* ActiveGameServer() {
    net::ServerSession* server = net::g_gameServer;
    if (!server || (server->flags7 & RefCounted::kDetached)) return nullptr;
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

// ---- Id maps -------------------------------------------------------------------------------------

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
        if (EngineObject* unit = FindUnit(static_cast<int>(current.objectId))) group->Insert(unit);
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
    if (weather != 0) {
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
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginPause) && ei::rebirth::PluginPause() == 3) return;
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

// ---- Removal ---------------------------------------------------------------------------------------

void WorldServer::RemoveObject(unsigned objectId) {
    auto found = objects.map.find(objectId);
    MapObject* const* slot = found != objects.map.end() ? &found->second : nullptr;
    // Rebirth's FixRemoveMapObject: an unknown id is logged and ignored. Vanilla reads through the null
    // pointer (and crashes).
    if (!slot && ei::rebirth::Enabled(ei::rebirth::Patch::FixRemoveMapObject)) return;
    MapObject* object = *static_cast<MapObject* const volatile*>(slot);
    if (!object) return;
    if (object->GetClassId() != ClassId::MagicTrap) aiMap.sectorMap.Remove(*slot);    // magic traps are not in it

    hash400::RemoveKey(updateMap.map, objectId);
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
    ScriptPurgeDeletedObjects(MainScript());     // what refers to removed objects dropped
    ScriptPurgeDeletedObjects(SecondScript());

    // Every world object forgets it (the walk stops at an entry without an object).
    for (auto node = objects.map.begin(); node != objects.map.end() && node->second; ++node)
        node->second->ForgetObject(object);
    // The missiles it shot are dropped; those aimed at it lose their target.
    for (auto node = projectiles.begin(); node != projectiles.end();) {
        auto current = node++;
        // 0 when the missile's owner left (the missile is then dropped).
        if (!(*current)->Forget(object)) projectiles.erase(current);
    }
    {
        PlayerIterator playerIt;
        for (Player* player = playerIt.First(PlayerList()); player; player = playerIt.Next())
            player->Server()->ForgetObject(object);
        object->Release();
        object->Release();
    }
}

double WorldServer::RawSurfaceHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    return aiMap.TopHeightAt(point);
}

double WorldServer::GroundHeightAt(float x, float y) { return GridHeightAt(x, y); }

double WorldServer::SurfaceHeightAt(float x, float y) { return RawSurfaceHeightAt(x, y); }

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kWorldServerClassName = "CWorldServer";
} // namespace

const char* WorldServer::ClassName() { return kWorldServerClassName; }
ClassId WorldServer::GetClassId() { return ClassId::WorldServer; }

bool WorldServer::IsServer() { return true; }

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
// FatalError texts.
const char* CantCreateText() { return "CWorldServer::Create.\n Can't create base world.\n"; }
const char* CantOpenText() { return "CWorldServer::Create.\n Can't opent message base resource.\n"; }

constexpr float kWindStrength = 0.4f;

} // namespace

void WorldServer::Create() {
    Destroy();
    World::Create();
    std::string folder = g_textFolder;
    bool opened;
    {
        std::string path = folder + kMessagesFileName;
        opened = messages.Open(path);
    }
    if (!opened) {
        game::FatalError(CantOpenText());
        return;
    }

    specificManager = new ServerSpecificManager();
    specificManager->AddRef();    // the reference is counted without testing the pointer
    specificManager->SetContext(this);
    hoover = new Hoover();
    hoover->AddRef();

    // The wind: along x, no strength yet (three dwords), then 0.4.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(windDirection, direction, sizeof(direction));
    windStrength = std::bit_cast<std::uint32_t>(kWindStrength);
    sessionKind = 0;
    scriptsActive = 0;
    exits.clear();
    climate = 1;
    cave = 0;
    loadedFresh = 1;
    keepsState = 1;
    zoneName = game::kEmptyText;
    gameResult = 0;
    mobFiles.clear();
}

void WorldServer::Destroy() {
    // Every player disconnected and released. The iterator drops its count of the player list when it goes out
    // of scope: before World::Destroy deletes the list.
    if (PlayerList()) {
        PlayerIterator it;
        for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
            player->Disconnect();
            PlayerList()->ReleasePlayer(player);
        }
    }

    if (hoover) {
        --hoover->refCount;
        if (hoover) delete hoover;     // the virtual destructor
        hoover = nullptr;
    }
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ResetState();
    messages.Close();

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
void DropPlayer(WorldServer* world, Player* player) {
    player->Disconnect();
    world->PlayerList()->ReleasePlayer(player);
    console::EndLine(console::g_netLogOut << worlddata::DisconnectingPlayerText() << player->playerId);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer computes it, with
// another margin: the server half's movement factor 3 and statistics B0 values 0 and 2).
void WakeAroundUnits(WorldServer* world, Player* player) {
    auto& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        Unit* unit = node->second;
        auto* controller = unit->ServerController();
        float perception = controller->moveFactors[3] * controller->stats.perception[0].current;
        float minimum = controller->stats.perception[2].current;
        float range = MaxKeepB(perception, minimum);   // the branch-free select
        float radius = range * worlddata::kHearingScale + worlddata::kWakeMargin;
        world->WakeUnitsAround(unit->localPosition, radius);
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
            DropPlayer(this, player);
    }

    if (WorldHandle* handle = worldHandle.Get()) {
        handle->Pump();
        AcceptNewPlayers(handle);
    }
    if (netlink::DriverSession* session = netSession.Get()) {
        net::ServerSession* server = net::g_gameServer;
        if (server && !(server->flags7 & RefCounted::kDetached)) session->PumpDrivers(server);
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
    ScriptGroup heroes;
    ScriptVars& globals = MainScript()->globals;
    auto& names = globals.groups.names;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (str::Compare(names[i], worlddata::HeroesGroupText()) == 0) {
            heroes = globals.groups.values[i];
            break;
        }
    }
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
        if (!SyncPlayer(player)) DropPlayer(this, player);

    ++updateCount;
}
