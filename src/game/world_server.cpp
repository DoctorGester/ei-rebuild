// WorldServer: object and player lookups, the coarse height grid, shared objects, dirty flags and
// the id maps. See world.h.
#include "player_messages.h"
#include "sector_map.h"
#include "unit.h"
#include "party.h"
#include "world.h"

#include "ai_map.h"
#include "hash_tables_400.h"
#include "core_maths.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_messages.h"
#include "spell.h"
#include "script.h"
#include "world_effect.h"

#include "ei/rebirth.h"
#include "lib/rounding.h"
#include "game_data.h"
#include "player_manager.h"

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

void ObjectIdMap::Set(const unsigned& key, EngineObject* const& value) {
    if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
    map[key] = value;
}

// ---- Lookups -------------------------------------------------------------------------------------

CMapObject* WorldServer::FindObject(unsigned objectId) {
    auto node = objects.map.find(objectId);
    return node != objects.map.end() ? node->second : nullptr;
}

CPlayer* WorldServer::GetPlayer(int playerId) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
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
    CPlayer* player = GetPlayer(unitId);
    if (!player) return 0;
    int living = 0;
    for (int i = 0; i < std::ssize(player->Party()->members); ++i) {
        unsigned memberId = player->Party()->members[i].unitId;
        CUnit* unit = FindUnit(static_cast<int>(memberId));
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
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
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

void CWorld::SetPaused(std::uint8_t value) { paused = value; }

void CWorld::SetServerPaused(std::uint8_t value) {
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginPause) && ei::rebirth::PluginPause() == 3) return;
    SetPaused(value);
}

const ZoneExit* WorldServer::FindZoneExit(int index) const {
    if (index < 0 || index >= std::ssize(exits)) return nullptr;
    return &exits[index];
}

void WorldServer::AddTimedObject(CEffect* effect) {
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

void WorldServer::AddToUpdateMap(CMapObject* unit) {
    // The unit's update slot.
    unit->updateCountdown = UpdateCountdownFor(unit);
    unsigned unitId = static_cast<unsigned>(unit->id);
    updateMap.Set(unitId, unit);
}

// ---- Removal ---------------------------------------------------------------------------------------

void WorldServer::RemoveObject(unsigned objectId) {
    auto found = objects.map.find(objectId);
    CMapObject* const* slot = found != objects.map.end() ? &found->second : nullptr;
    // Rebirth's FixRemoveMapObject: an unknown id is logged and ignored. Vanilla reads through the null
    // pointer (and crashes).
    if (!slot && ei::rebirth::Enabled(ei::rebirth::Patch::FixRemoveMapObject)) return;
    CMapObject* object = *static_cast<CMapObject* const volatile*>(slot);
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
    CUnit* unit = UnitOrNull(object);
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
        for (CPlayer* player = playerIt.First(PlayerList()); player; player = playerIt.Next())
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
