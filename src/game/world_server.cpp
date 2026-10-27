// CWorldServer: object and player lookups, the coarse height grid, shared objects, dirty flags and
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

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "game_lists.h"
#include "game_data.h"

namespace {

// The game server object of a multiplayer session (null or flagged inactive otherwise); its
// FindPlayer is the player record with that id, or null.
net::ServerSession* ActiveGameServer() {
    net::ServerSession* server = net::GameServer();
    if (!server || (server->flags7_ & CRefCounted::kDetached)) return nullptr;
    return server;
}

// Player index used by the shared-object list: -1 when not in a multiplayer game or unknown.
int SharedListPlayer(int playerId) {
    if (net::ServerSession* server = ActiveGameServer()) {
        if (net::PlayerMessage* record = server->FindPlayer(playerId)) return record->serial_;
    }
    return -1;
}

constexpr int kClassNoSector = 0x61;     // objects of this class are not in the sector map

template <typename Map>
void EraseId(Map& map, unsigned id) {
    if (auto* node = map.map.Find(id)) map.map.Erase(node);
}

} // namespace

// ---- Id maps -------------------------------------------------------------------------------------

void ObjectIdMap::Set(const unsigned& key, CEngineObject* const& value) {
    if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
    map[key] = value;
}

// ---- Lookups -------------------------------------------------------------------------------------

CMapObject* CWorldServer::FindObject(unsigned id) {
    auto* node = objects_.map.Find(id);
    return node ? node->value : nullptr;
}

CPlayer* CWorldServer::GetPlayer(int id) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (player->playerId_ == id) return player;
    return nullptr;
}

float CWorldServer::GridHeight(float x, float y) {
    // Cell size 1.0 at half-cell offset: (1 / (1 * 0.5)) * v - 0.5, rounded through a float.
    int cx = x87::fistp(static_cast<float>(2.0f * x - 0.5f));
    int cy = x87::fistp(static_cast<float>(2.0f * y - 0.5f));
    const CAIMap* map = AIMap();
    if (cx < 0 || cy < 0 || cx >= map->cells.width || cy >= map->cells.height) return 0.0f;
    int raw = map->cells[cy][cx].groundLevel;
    return static_cast<float>(raw) / map->heightScale;
}

int CWorldServer::CountLivingMercs(int id) {
    CPlayer* player = GetPlayer(id);
    if (!player) return 0;
    int living = 0;
    for (int i = 0; i < player->Party()->members_.size; ++i) {
        unsigned unitId = player->Party()->members_.data[i].unitId;
        CUnit* unit = FindUnit(static_cast<int>(unitId));
        if (unit && unit->IsDead() == 0) ++living;
    }
    return living - 1;
}

bool CWorldServer::IsObjectShared(int objectId, int playerId) {
    int player = SharedListPlayer(playerId);
    for (auto* node = sharedObjects_.head; node; node = node->next)
        if (node->data.objectId == objectId && (node->data.player == player || node->data.player == -1)) return true;
    return false;
}

void CWorldServer::CollectSharedObjects(int playerId, ScriptGroup* group) {
    group->SetSize(0, -1);
    int player = SharedListPlayer(playerId);
    for (auto* node = sharedObjects_.head; node;) {
        const SharedObject& current = node->data;
        node = node->next;
        if (current.player != player && current.player != -1) continue;   // -1: every player
        if (CEngineObject* unit = FindUnit(static_cast<int>(current.objectId))) group->Insert(unit);
    }
}

// ---- State ---------------------------------------------------------------------------------------

void CWorldServer::MarkPlayersDirty(std::uint32_t mask) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty_ |= mask;
}

void CWorldServer::SetCave(int value) { cave_ = value; }

void CWorldServer::SetClimate(int value) {
    if (weather_ != 0) {
        weather_ = 0;
        weatherStrength_ = 20.0f;
        MarkPlayersDirty(2);
    }
    climate_ = value;
}

void CWorldServer::SetWeather(int weather, float strength) {
    weather_ = weather;
    weatherStrength_ = strength;
    MarkPlayersDirty(2);
}

void CWorld::SetPaused(std::uint8_t value) { paused_ = value; }

void CWorld::SetServerPaused(std::uint8_t value) {
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginPause) && ei::rebirth::PluginPause() == 3) return;
    SetPaused(value);
}

BOOL CWorldServer::GetZoneExit(int index, mfc::CString* target, int* entrance) {
    if (index < 0 || index >= exits_.size) return 0;
    *target = exits_[index].target;
    *entrance = exits_[index].entrance;
    return 1;
}

void CWorldServer::AddTimedObject(CEngineObject* object) {
    // The timed objects are the world's effects (world_effect.h): the tick they began.
    auto* effect = static_cast<CEffect*>(object);
    effect->startTick_ = tick_;
    ++object->refCount_;
    effect->MarkChanged();   // flags_ |= 2, stamped with the server time
    unsigned id = static_cast<unsigned>(object->id_);
    timedObjects_.Set(id, object);
}

void CWorldServer::RemoveTimedObject(unsigned id) {
    auto* node = timedObjects_.map.Find(id);
    if (!node) return;
    CEngineObject* object = node->value;
    object->flags_ |= 1;
    object->Release();
    // Looked up again: releasing may have changed the map.
    if (auto* again = timedObjects_.map.Find(id)) timedObjects_.map.Erase(again);
}

int CWorldServer::UpdateCountdownFor(const CEngineObject* object) { return (object->id_ & 0xF) + 0x80; }

void CWorldServer::AddToUpdateMap(CMapObject* unit) {
    // The unit's update slot.
    unit->updateCountdown = UpdateCountdownFor(unit);
    unsigned id = static_cast<unsigned>(unit->id_);
    updateMap_.Set(id, unit);
}

// ---- Removal ---------------------------------------------------------------------------------------

void CWorldServer::RemoveObject(unsigned id) {
    auto* found = objects_.map.Find(id);
    CMapObject* const* slot = found ? &found->value : nullptr;
    // Rebirth's FixRemoveMapObject: an unknown id is logged and ignored. Vanilla reads through the null
    // pointer (and crashes).
    if (!slot && ei::rebirth::Enabled(ei::rebirth::Patch::FixRemoveMapObject)) return;
    CMapObject* object = *static_cast<CMapObject* const volatile*>(slot);
    if (!object) return;
    if (object->GetClassId() != kClassNoSector) Sectors()->Remove(*slot);

    hash400::ObjectsRemoveKey(updateMap_.map, id);
    EraseId(objects_, id);
    if (namedObjects_.map.Find(id)) EraseId(namedObjects_, id);
    if (sentObjects_.map.Find(id)) {
        EraseId(sentObjects_, id);
    } else {
        EraseId(mapObjects_, id);
        if (otherMapObjects_.map.Find(id)) EraseId(otherMapObjects_, id);
    }

    object->ClearState();   // it leaves the server
    CUnit* unit = UnitOrNull(object);
    ++object->refCount_;   // held to the end, released twice there (with the maps' reference)
    if (unit) unit->SetPlayer(nullptr);
    object->flags_ |= 1;
    ScriptPurgeDeletedObjects(MainScript());     // what refers to removed objects dropped
    ScriptPurgeDeletedObjects(SecondScript());

    // Every world object forgets it (the walk stops at an entry without an object).
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node))
        node->value->ForgetObject(object);
    // The missiles it shot are dropped; those aimed at it lose their target.
    for (auto* node = projectiles_.head; node;) {
        auto* current = node;
        node = node->next;
        // 0 when the missile's owner left (the missile is then dropped).
        if (!current->data->Forget(object))
            mfc::impl::RemoveAtAs<gamelists::Refs>(&projectiles_, current);
    }
    {
        PlayerIterator players;
        for (CPlayer* player = players.First(PlayerList()); player; player = players.Next())
            player->Server()->ForgetObject(object);
        object->Release();
        object->Release();
    }
}

void CWorldServer::RemoveAllUnits() {
    auto* node = objects_.map.First();
    if (!node) return;
    // The next object is read before the current one is removed.
    CEngineObject* object = node->value;
    do {
        unsigned id = static_cast<unsigned>(object->id_);
        bool isUnit = object->GetClassId() == world::kClassUnit;
        node = objects_.map.After(node);
        object = node ? node->value : nullptr;
        if (isUnit) RemoveObject(static_cast<int>(id));
    } while (node);
}
EI_REWRITTEN(0x005939E0, CWorldServer::RemoveAllUnits);

CEngineObject* ObjectMapIterator::First(ObjectIdMap* objects) {
    map = objects;
    node = objects->map.First();
    table = objects;
    return node ? node->value : nullptr;
}
EI_REWRITTEN(0x0059AF20, ObjectMapIterator::First);
EI_REWRITTEN(0x0059AFF0, ObjectMapIterator::First);

CEngineObject* ObjectMapIterator::Next() {
    if (!node) return nullptr;
    node = table->map.After(node);
    return node ? node->value : nullptr;
}
EI_REWRITTEN(0x0059AF80, ObjectMapIterator::Next);
EI_REWRITTEN(0x0059B050, ObjectMapIterator::Next);

double CWorldServer::RawSurfaceHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    return AIMap()->TopHeightAt(&point);
}
EI_REWRITTEN(0x0058A390, CWorldServer::RawSurfaceHeightAt);

double CWorldServer::GroundHeightAt(float x, float y) { return GridHeightAt(x, y); }
EI_REWRITTEN(0x0058A3D0, CWorldServer::GroundHeightAt);

double CWorldServer::SurfaceHeightAt(float x, float y) { return RawSurfaceHeightAt(x, y); }
EI_REWRITTEN(0x0058A3F0, CWorldServer::SurfaceHeightAt);

EI_REWRITTEN(0x005935C0, CWorldServer::RemoveObject);
EI_REWRITTEN(0x00593AA0, CWorldServer::FindObject);
EI_REWRITTEN(0x00594180, CWorldServer::GetPlayer);
EI_REWRITTEN(0x00594220, CWorldServer::GridHeight);
EI_REWRITTEN(0x00591740, CWorldServer::CountLivingMercs);
EI_REWRITTEN(0x005949D0, CWorldServer::IsObjectShared);
EI_REWRITTEN(0x00594A30, CWorldServer::CollectSharedObjects);
EI_REWRITTEN(0x0058B6D0, CWorldServer::MarkPlayersDirty);
EI_REWRITTEN(0x0058CC70, CWorldServer::SetCave);
EI_REWRITTEN(0x0058CC80, CWorldServer::SetClimate);
EI_REWRITTEN(0x0058EE00, CWorldServer::SetWeather);
EI_REWRITTEN(0x005942C0, CWorld::SetServerPaused);
EI_REWRITTEN(0x00566350, CWorld::SetPaused);
EI_REWRITTEN(0x0058E4C0, CWorldServer::GetZoneExit);
EI_REWRITTEN(0x0058E520, CWorldServer::AddTimedObject);
EI_REWRITTEN(0x0058E5D0, CWorldServer::RemoveTimedObject);
EI_REWRITTEN(0x0058D5D0, CWorldServer::AddToUpdateMap);
EI_REWRITTEN(0x005999E0, ObjectIdMap::Set);
EI_REWRITTEN(0x00599BD0, ObjectIdMap::Set);
