// WorldServer::LoadMap: loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md.
#include "game/ai/ai_map.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"
#include "engine/models/figure_manager.h"
#include "game/app/singletons.h"
#include "engine/core/id_tables.h"
#include "game/objects/map_object.h"
#include "engine/mods/mod_folders.h"
#include "engine/core/coder.h"
#include "game/net/net_link.h"
#include "game/world/diplomacy.h"
#include "game/players/player_manager.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/spells/spell.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "engine/mods/maps_folder.h"
#include "engine/data/resource.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

// The timed objects are ended (ClearState) and released, then the map is emptied (the walk stops at the
// first empty entry).
void EndTimedObjects(WorldServer* world) {
    auto& map = world->timedObjects.map;
    for (auto node = map.begin(); node != map.end() && node->second; ++node) {
        EngineObject* object = node->second;
        object->ClearState();
        object->Release();
    }
    world->timedObjects.map.clear();    // nodes freed, buckets kept
    world->timedObjects.maxId = 0;
}

} // namespace

int WorldServer::LoadMap(std::string_view map, std::string_view mob, ByteStream* graph, char fromSave) {
    AIMap* currentAiMap = &aiMap;
    if (text::CompareIgnoringCase(mapName, map) != 0) {
        currentAiMap->Reset();
        currentAiMap->BeginRefresh();
        bool failed = currentAiMap->Load(MapPath(map)) == 0;
        if (failed) return 0;
        mapName = map;
        mobName.clear();
    }
    graph->Seek(0);
    if (graph->Size() != 0 && !aiMap.regionMap.LoadBlocks(graph)) return 0;

    if (text::CompareIgnoringCase(mobName, mob) == 0) {
        // The same zone again: start over (the casts before the scripts that run them).
        projectiles.clear();
        MainScript()->Reset();
        SecondScript()->Reset();
        diplomacy.Reset();
        ClearMapObjects();
        EndTimedObjects(this);
        keepsState = 0;
        loadedFresh = fromSave == 0;
        aiMap.footprintTick = 0;
    } else {
        ResetZone();
        keepsState = 1;
        loadedFresh = fromSave == 0;
    }

    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty = kPlayerDirtyAll;
    zoneLoaded = 1;
    // A fresh zone starts clear; a save's load (a slot, a revisited zone, a multiplayer zone) keeps the weather
    // LoadSave read, where the original cleared it too (docs/notes/world.md, "Weather across a load"). The zone's
    // first weather message has no fade, so the client's rain / snow layer is at the zone's weather at once.
    if (!fromSave) weather = 0;
    weatherStrength = 0.0f;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) player->dirty |= PlayerDirty::Weather;

    if (keepsState || loadedFresh) {
        mobName = mob;
        LoadMapObjects(mob, loadedFresh == 0);
        if (!loadedFresh) {
            MainScript()->Reset();
            diplomacy.Reset();
        }
        if (keepsState) SendFreshObjects();
    }
    if (fromSave) {
        ClearMapObjects();
        EndTimedObjects(this);
    }
    loadedFresh = 1;
    keepsState = 1;
    return 1;
}

// ---- ResetZone --------------------------------------------------------------------------------

void WorldServer::ResetZone() {
    sharedObjects.clear();
    mobFiles.clear();
    countdowns.entries.clear();
    exits.clear();
    projectiles.clear();   // the casts before the scripts that run them
    MainScript()->Reset();
    SecondScript()->Reset();
    ReleaseAndClear(counted);

    if (PlayerList()) {
        PlayerIterator it;
        for (Player* player = it.First(PlayerList()); player; player = it.Next())
            player->Clear();
    }

    for (auto& [objectId, object] : objects.map) {
        if (!object) break;
        object->ClearState();
    }
    ReleaseEach(objects.map);
    objects.maxId = 0;
    MapObjectIdMap* const maps[] = {&sentObjects, &mapObjects, &otherMapObjects, &updateMap, &namedObjects};
    for (MapObjectIdMap* map : maps) {
        map->map.clear();
        map->maxId = 0;
    }
    projectiles.clear();
    EndTimedObjects(this);
    mobName.clear();
    zoneName.clear();
    if (figureManager) figureManager->FreeSpares();
    if (objectManager) objectManager->FreeSpares();
    if (specificManager) specificManager->FreeSpares();
}

// ---- TeardownZone -----------------------------------------------------------------------------

void WorldServer::TeardownZone() {
    // Every player leaves (the list is walked from its start again after each one).
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player;
         player = it.First(PlayerList())) {
        player->Disconnect();
        player->Release();
    }
    zoneLoaded = 0;

    sharedObjects.clear();
    mobFiles.clear();
    aiMap.refreshUnits = false;    // refreshes off
    // The missiles released (16-bit count, deleted at zero), before the scripts that run them.
    projectiles.clear();
    MainScript()->Reset();
    SecondScript()->Reset();
    diplomacy.Reset();
    ClearMapObjects();

    EndTimedObjects(this);
    CloseSession();
    gameState = GameState::None;
    zoneMode = ZoneMode::None;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

void WorldServer::SendFreshObjects() {
    std::vector<MapObject*> fresh;
    ByteStream* header = &zoneHeader;
    header->Clear();

    // The map objects that are not part of the map itself: also kept in sentObjects.
    for (auto node = objects.map.begin(); node != objects.map.end() && node->second; ++node) {
        MapObject* object = node->second;
        unsigned objectId = static_cast<unsigned>(object->id);
        if (!mapObjects.map.contains(objectId)) continue;
        if (object->IsSynchronised()) continue;
        sentObjects.Set(static_cast<unsigned>(object->id), object);
        fresh.push_back(object);
    }

    // They stop being map objects and are sent to a temporary player, which is then dropped.
    Player* temporary = players->CreateSessionPlayer();
    PlayerServer* state = temporary->Server();
    std::vector<MapObject*> newObjects;
    for (MapObject* object : fresh) {
        unsigned objectId = static_cast<unsigned>(object->id);
        idtable::RemoveKey(mapObjects.map, objectId);
        objectId = static_cast<unsigned>(object->id);
        if (idtable::Find(otherMapObjects.map, objectId)) {
            objectId = static_cast<unsigned>(object->id);
            idtable::RemoveKey(otherMapObjects.map, objectId);
        }
        newObjects.push_back(object);
    }
    state->SendObjectsAdded(newObjects);
    state->WriteTo(header);
    temporary->Release();
    lib::FreeStorage(newObjects);
}

void WorldServer::ResetState() {
    ResetZone();
    mapName.clear();
    diplomacy.Reset();
    MainScript()->Reset();
    SecondScript()->Reset();
    aiMap.Reset();
    zoneLoaded = 0;
    nightLevel = 0.0f;
    for (WorldAlarm& alarm : alarms) {
        alarm.active = 0;
        alarm.time = 0;
        alarm.y = 0.0f;
        alarm.x = 0.0f;
    }
    zoneMode = ZoneMode::None;
    startPosition = {};
    weatherFactor = 1.0f;
    loaded = 0;
    lastRainBoltTick = 0;
}

void WorldServer::EnsureZone(std::string_view map, std::string_view mob) {
    if (str::Compare(mapName, map) == 0 && str::Compare(mobName, mob) == 0) return;
    NewZone(map, mob, std::nullopt);
    TeardownZone();
}
