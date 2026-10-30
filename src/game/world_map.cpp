// WorldServer::LoadMap: loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md.
#include "ai_map.h"

#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "figure_manager.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "hash_tables_400.h"
#include "map_object.h"
#include "mod_folders.h"
#include "model.h"   // TweenArray (countdowns)
#include "net_coder.h"
#include "net_link.h"
#include "object_diplomacy.h"
#include "player_manager.h"
#include "player_messages.h"
#include "spell.h"
#include "world.h"
#include "world_data.h"
#include "world_map_files.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <bit>
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
        bool failed;
        {
            std::string path = MapPath(map);
            failed = currentAiMap->Load(path.c_str()) == 0;
        }
        if (failed) return 0;
        mapName = map;
        mobName = game::kEmptyText;
    }
    graph->Seek(0);
    if (graph->Size() != 0 && !aiMap.regionMap.LoadBlocks(graph)) return 0;

    if (text::CompareIgnoringCase(mobName, mob) == 0) {
        // The same zone again: start over.
        ScriptReset(MainScript());
        ScriptReset(SecondScript());
        diplomacy.Reset();
        ClearMapObjects();
        projectiles.clear();
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
    if (it.manager) --it.manager->refCount;
    zoneLoaded = 1;
    weather = 0;
    weatherStrength = 0.0f;
    it.manager = nullptr;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) player->dirty |= PlayerDirty::Weather;
    if (it.manager) {
        --it.manager->refCount;
        it.manager = nullptr;
    }

    if (keepsState || loadedFresh) {
        mobName = mob;
        int loadedCount = LoadMapObjects(mob, loadedFresh == 0);
        if (!loadedCount) return loadedCount;
        if (!loadedFresh) {
            ScriptReset(MainScript());
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
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    projectiles.clear();
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
    mobName = game::kEmptyText;
    zoneName = game::kEmptyText;
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
        PlayerList()->ReleasePlayer(player);
        if (it.manager) --it.manager->refCount;
        it.manager = nullptr;
    }
    if (it.manager) --it.manager->refCount;
    it.manager = nullptr;
    zoneLoaded = 0;

    sharedObjects.clear();
    mobFiles.clear();
    aiMap.refreshUnits = false;    // refreshes off
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    diplomacy.Reset();
    ClearMapObjects();

    // The missiles released (16-bit count, deleted at zero).
    projectiles.clear();

    EndTimedObjects(this);
    CloseSession();
    gameResult = 0;
    sessionKind = 0;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

void WorldServer::SendFreshObjects() {
    std::vector<EngineObject*> fresh;
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
    std::vector<EngineObject*> newObjects;
    for (EngineObject* object : fresh) {
        unsigned objectId = static_cast<unsigned>(object->id);
        hash400::RemoveKey(mapObjects.map, objectId);
        objectId = static_cast<unsigned>(object->id);
        if (hash400::Find(otherMapObjects.map, objectId)) {
            objectId = static_cast<unsigned>(object->id);
            hash400::RemoveKey(otherMapObjects.map, objectId);
        }
        newObjects.push_back(object);
    }
    state->SendObjectsAdded(&newObjects);
    state->WriteTo(header);
    PlayerList()->ReleasePlayer(temporary);
    lib::FreeStorage(newObjects);
}

void WorldServer::ResetState() {
    ResetZone();
    mapName = game::kEmptyText;
    diplomacy.Reset();
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    aiMap.Reset();
    zoneLoaded = 0;
    nightLevel = 0.0f;
    for (WorldAlarm& alarm : alarms) {
        alarm.active = 0;
        alarm.time = 0;
        alarm.y = 0.0f;
        alarm.x = 0.0f;
    }
    sessionKind = 0;
    startPosition.x = 0.0f;
    weatherFactor = std::bit_cast<std::uint32_t>(1.0f);
    loaded = 0;
    startPosition.y = 0.0f;
    lastRainBoltTick = 0;
    startPosition.z = 0.0f;
}

void WorldServer::EnsureZone(std::string_view map, std::string_view mob) {
    if (str::Compare(mapName, map) == 0 && str::Compare(mobName, mob) == 0) return;
    NewZone(map, mob, std::nullopt);
    TeardownZone();
}

// --- Files -------------------------------------------------------------------------------------------

// The maps folder and its files (MapsFolder, MapPath, MapFileExists, WriteMapFile), with Rebirth's Maps in
// the standalone image (mod_folders.h). See world_map_files.h and docs/notes/world.md.

namespace {

const char* MapsFolderName() { return "maps\\"; }

// Rebirth's Maps (modfolders::SelectMaps): addon names the file before each maps-folder use.
bool ModMaps() { return ei::rebirth::Enabled(ei::rebirth::Patch::Maps); }

} // namespace

std::string MapsFolder() {
    // Rebirth's Maps: the folder the last site selected.
    std::string folder(ModMaps() ? modfolders::MapsRoot(g_dataFolder) : g_dataFolder);
    return folder + MapsFolderName();
}

std::string MapPath(std::string_view name) {
    if (name.size() > 1 && name[1] == ':') return std::string(name);
    modfolders::SelectMaps(name, modfolders::MapsSite::MapPath);
    std::string path = MapsFolder();
    path += name;
    return path;
}

bool MapFileExists(const char* name) {
    platform::FileFind find;
    modfolders::SelectMaps(name, modfolders::MapsSite::FileExists);
    std::string path = MapsFolder() + name;
    return find.FindFile(path) != 0;
}

void WriteMapFile(const char* name, GameBuffer* buffer) {
    netlink::FileStream file;
    {
        modfolders::SelectMaps(name, modfolders::MapsSite::WriteFile);
        std::string path = MapsFolder() + name;
        file.Open(path, netlink::FileStream::kOpenWrite, 0);
    }
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    file.Write(buffer->begin, static_cast<std::uint32_t>(buffer->base + (buffer->high - buffer->begin)));
    file.Close();
}
