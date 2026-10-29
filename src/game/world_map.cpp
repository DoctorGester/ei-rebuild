// CWorldServer::LoadMap: loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "figure_manager.h"
#include "game_strings.h"
#include "hash_tables_400.h"
#include "map_object.h"
#include "model.h"   // TweenArray (countdowns_)
#include "object_diplomacy.h"
#include "player_manager.h"
#include "player_messages.h"
#include "spell.h"
#include "world.h"
#include "world_map_files.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <bit>
#include <list>
#include <string>
#include <vector>

namespace {

// The timed objects are ended (ClearState) and released, then the map is emptied (the walk stops at the
// first empty entry).
void EndTimedObjects(CWorldServer* world) {
    auto& map = world->timedObjects_.map;
    for (auto node = map.begin(); node != map.end() && node->second; ++node) {
        CEngineObject* object = node->second;
        object->ClearState();
        object->Release();
    }
    world->timedObjects_.map.clear();   // nodes freed, buckets kept
    world->timedObjects_.maxId = 0;
}

} // namespace

int CWorldServer::LoadMap(std::string_view map, std::string_view mob, CStream* graph, char fromSave) {
    CAIMap* aiMap = &aiMap_;
    if (text::CompareIgnoringCase(mapName_, map) != 0) {
        aiMap->Reset();
        aiMap->BeginRefresh();
        bool failed;
        {
            std::string path;
            MapPath(&path, map);
            failed = aiMap->Load(path.c_str()) == 0;
        }
        if (failed) return 0;
        mapName_ = map;
        mobName_ = game::kEmptyText;
    }
    graph->Seek(0);
    if (graph->Size() != 0 && !aiMap_.regionMap.LoadBlocks(graph)) return 0;

    if (text::CompareIgnoringCase(mobName_, mob) == 0) {
        // The same zone again: start over.
        ScriptReset(MainScript());
        ScriptReset(SecondScript());
        diplomacy_.Reset();
        ClearMapObjects();
        projectiles_.clear();
        EndTimedObjects(this);
        keepsState_ = 0;
        loadedFresh_ = fromSave == 0;
        aiMap_.footprintTick = 0;
    } else {
        ResetZone();
        keepsState_ = 1;
        loadedFresh_ = fromSave == 0;
    }

    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty_ = kPlayerDirtyAll;
    if (it.manager) --it.manager->refCount_;
    zoneLoaded_ = 1;
    weather_ = 0;
    weatherStrength_ = 0.0f;
    it.manager = nullptr;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) player->dirty_ |= PlayerDirty::Weather;
    if (it.manager) {
        --it.manager->refCount_;
        it.manager = nullptr;
    }

    if (keepsState_ || loadedFresh_) {
        mobName_ = mob;
        int loaded = LoadMapObjects(mob, loadedFresh_ == 0);
        if (!loaded) return loaded;
        if (!loadedFresh_) {
            ScriptReset(MainScript());
            diplomacy_.Reset();
        }
        if (keepsState_) SendFreshObjects();
    }
    if (fromSave) {
        ClearMapObjects();
        EndTimedObjects(this);
    }
    loadedFresh_ = 1;
    keepsState_ = 1;
    return 1;
}

// ---- ResetZone --------------------------------------------------------------------------------

void CWorldServer::ResetZone() {
    sharedObjects_.clear();
    mobFiles_.clear();
    countdowns_.clear();
    exits_.clear();
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    projectiles_.clear();
    for (CEngineObject* object : counted_)
        if (object) object->Release();
    counted_.clear();

    if (PlayerList()) {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->Clear();
    }

    for (auto& [id, object] : objects_.map) {
        if (!object) break;
        object->ClearState();
    }
    ReleaseEach(objects_.map);
    objects_.maxId = 0;
    MapObjectIdMap* const maps[] = {&sentObjects_, &mapObjects_, &otherMapObjects_, &updateMap_, &namedObjects_};
    for (MapObjectIdMap* map : maps) {
        map->map.clear();
        map->maxId = 0;
    }
    projectiles_.clear();
    EndTimedObjects(this);
    mobName_ = game::kEmptyText;
    zoneName_ = game::kEmptyText;
    if (figureManager_) figureManager_->FreeSpares();
    if (objectManager_) objectManager_->FreeSpares();
    if (specificManager_) specificManager_->FreeSpares();
}

// ---- TeardownZone -----------------------------------------------------------------------------

void CWorldServer::TeardownZone() {
    // Every player leaves (the list is walked from its start again after each one).
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player;
         player = it.First(PlayerList())) {
        player->Disconnect();
        PlayerList()->ReleasePlayer(player);
        if (it.manager) --it.manager->refCount_;
        it.manager = nullptr;
    }
    if (it.manager) --it.manager->refCount_;
    it.manager = nullptr;
    zoneLoaded_ = 0;

    sharedObjects_.clear();
    mobFiles_.clear();
    aiMap_.refreshUnits = false;   // refreshes off
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    diplomacy_.Reset();
    ClearMapObjects();

    // The missiles released (16-bit count, deleted at zero).
    projectiles_.clear();

    EndTimedObjects(this);
    CloseSession();
    gameResult_ = 0;
    sessionKind_ = 0;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

void CWorldServer::SendFreshObjects() {
    std::list<CEngineObject*> fresh;
    CStream* header = &zoneHeader_;
    header->Clear();

    // The map objects that are not part of the map itself: also kept in sentObjects_.
    for (auto node = objects_.map.begin(); node != objects_.map.end() && node->second; ++node) {
        CMapObject* object = node->second;
        unsigned id = static_cast<unsigned>(object->id_);
        if (!mapObjects_.map.contains(id)) continue;
        if (object->IsSynchronised()) continue;
        sentObjects_.Set(static_cast<unsigned>(object->id_), object);
        auto entry = fresh.emplace(fresh.end());
        *entry = object;
    }

    // They stop being map objects and are sent to a temporary player, which is then dropped.
    CPlayer* temporary = players_->CreateSessionPlayer();
    CPlayerServer* state = temporary->Server();
    std::vector<CEngineObject*> objects;
    for (auto node = fresh.begin(); node != fresh.end();) {
        auto next = std::next(node);
        CEngineObject* object = *node;
        unsigned id = static_cast<unsigned>(object->id_);
        hash400::RemoveKey(mapObjects_.map, id);
        id = static_cast<unsigned>(object->id_);
        if (hash400::Find(otherMapObjects_.map, id)) {
            id = static_cast<unsigned>(object->id_);
            hash400::RemoveKey(otherMapObjects_.map, id);
        }
        objects.push_back(object);
        node = next;
    }
    state->SendObjectsAdded(&objects);
    state->WriteTo(header);
    PlayerList()->ReleasePlayer(temporary);
    lib::FreeStorage(objects);
    fresh.clear();
}

void CWorldServer::ResetState() {
    ResetZone();
    mapName_ = game::kEmptyText;
    diplomacy_.Reset();
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    aiMap_.Reset();
    zoneLoaded_ = 0;
    nightLevel_ = 0.0f;
    for (WorldAlarm& alarm : alarms_) {
        alarm.active = 0;
        alarm.time = 0;
        alarm.y = 0.0f;
        alarm.x = 0.0f;
    }
    sessionKind_ = 0;
    startPosition_.x = 0.0f;
    weatherFactor_ = std::bit_cast<std::uint32_t>(1.0f);
    loaded_ = 0;
    startPosition_.y = 0.0f;
    lastRainBoltTick_ = 0;
    startPosition_.z = 0.0f;
}

void CWorldServer::EnsureZone(std::string_view map, std::string_view mob) {
    if (str::Compare(mapName_, map) == 0 && str::Compare(mobName_, mob) == 0) return;
    NewZone(map, mob, std::nullopt);
    TeardownZone();
}
