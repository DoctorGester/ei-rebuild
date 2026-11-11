// WorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
// back, LoadSavedMapObjects restores the map objects SaveMapObjects wrote, and UpdatePlayerTimes
// brings the players' "gtime" up to date before a save. See world.h and docs/notes/world.md.
#include "game/ai/ai_map.h"
#include "engine/data/resource.h"
#include "engine/core/base_manager.h"
#include "game/app/movies.h"
#include "game/combat/combat.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "engine/mods/mod_folders.h"
#include "game/net/net_delta.h"
#include "game/net/net_messages.h"
#include "game/net/net_session.h"
#include "game/world/hoover.h"
#include "game/world/diplomacy.h"
#include "game/players/party.h"
#include "game/players/player_manager.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/saves/save_versions.h"
#include "game/script/script.h"
#include "game/ai/sector_map.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "engine/mods/maps_folder.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kTextKeyIdMin = 100000000;                      // ids above this are scripted (text-key) units

// Progress screen stages LoadSave shows.
constexpr int kProgressStart = 0;
constexpr int kProgressZone = 1;
constexpr int kProgressManagers = 5;
constexpr int kProgressObjects = 6;
constexpr int kProgressDone = 7;

// ---- Local layouts ----------------------------------------------------------------------------

void ShowLoadProgress(int stage) { ShowLoadingMovie(stage); }

// The units saved standing where they cannot move (a cell they cannot leave) are put on a free
// cell nearby when they are scripted units (text-key ids), otherwise removed.
void RelocateStrandedUnits(WorldServer* world) {
    std::vector<int> stranded;
    for (auto& node : world->objects.map) {
        Unit* unit = UnitOrNull(node.second);
        if (!unit) continue;
        if (world->aiMap.CanLeaveCell(unit)) continue;
        stranded.push_back(unit->id);
    }
    for (int i = 0; i < std::ssize(stranded); ++i) {
        Unit* unit = world->FindUnit(stranded[i]);
        if (unit && unit->id > kTextKeyIdMin) {
            world->MoveToFreeCell(unit, unit->localPosition);
            unit->ServerController()->ResyncWithUnit();
        } else {
            world->RemoveObject(stranded[i]);
        }
    }
}

// The players' part of the save (in the player list's order).
void LoadPlayers(WorldServer* world, ByteStream* stream) {
    PlayerIterator it;
    for (Player* player = it.First(world->PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        stream->ReadValue(player->partyId);
        player->GetParty()->LoadParty(stream, true);
        RequireEveryRecord(player->GetParty());
        player->lastTick = world->tick;
        stream->ReadValue(player->sessionKind);
        BaseManager* items = world->ItemFactory();
        for (;;) {
            const int id = stream->ReadId();
            if (id == kNoObjectId) break;
            Item* object = items->FindObjectAs<Item>(id);
            if (!object) continue;
            player->items.push_back(object);
            object->AddRef();
        }
        stream->ReadValue(player->godMode);
        if (stream->version >= kVersionPlayerLists) {
            // `count` zeros appended, then every id read from the head (those already listed too).
            std::vector<std::uint32_t>& ids = player->recharging;
            for (int count = stream->ReadCount(); count > 0; --count) ids.push_back(0);
            for (std::uint32_t& id : ids) stream->ReadValue(id);
        }
        if (stream->version < kVersionPlayerByte) stream->Skip(1);
        if (stream->version < kVersionPlayerGroup) {
            ScriptGroup group;
            group.Load(stream);
        }
        state->SendParty();
        state->SetPartyDeployed(1);
    }
}

// The players' part of the save (what LoadPlayers reads).
void SavePlayers(WorldServer* world, ByteStream* stream) {
    PlayerIterator it;
    for (Player* player = it.First(world->PlayerList()); player; player = it.Next()) {
        stream->WriteValue(player->partyId);
        player->GetParty()->SaveParty(stream, true);
        stream->WriteValue(player->sessionKind);
        for (const Item* item : player->items) stream->WriteId(item);
        stream->WriteValue(kNoObjectId);
        stream->WriteValue(player->godMode);
        const std::vector<std::uint32_t>& ids = player->recharging;
        stream->WriteCount(ids.size());
        for (std::uint32_t id : ids) stream->WriteValue(id);
    }
}


constexpr const char* kCantLoadMap = "CWorldServer::NewZone()\n Can't load map.\n";

// Whether the file `name` (an archive also as its folder) is neither there as named nor in the maps folder (Rebirth's
// Maps names it first: `site`).
bool MapFileMissing(std::string_view name, modfolders::MapsSite site) {
    const std::string file(name);
    if (ResourceExists(file)) return false;
    modfolders::SelectMaps(name, site);
    return !ResourceExists(MapsFolder() + file);
}

// A zone without a graph file (a mod's new zone; ReadAIGraph left the graph empty): once the refresh has measured the
// cells, the whole map refreshed again with its blocks measured, as the editor made the files' graphs.
void BuildAIGraph(AIMap& map) { map.RefreshArea({0, 0}, {map.cells.width - 1, map.cells.height - 1}); }

} // namespace


// The sum in the game's order ((elapsed * secondsPerTick + offset) + gtime); plain C++ maths, compared with
// CheckNear.
void WorldServer::UpdatePlayerTimes(float offset) {
    const char* name = "gtime";
    int now = tick;
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        int elapsed = static_cast<int>(static_cast<unsigned>(now) - static_cast<unsigned>(player->lastTick));
        player->lastTick = now;
        if (elapsed < 0) elapsed = 0;
        float current = player->GetParty()->variables.Get(name);
        float value = (static_cast<float>(elapsed) * secondsPerTick) + offset + current;
        player->Server()->SetVariable(name, value);
    }
}

void WorldServer::PassTime(float offset) {
    if (offset >= 0.0f) UpdatePlayerTimes(offset);
}

void WorldServer::LoadSavedMapObjects(ByteStream* stream) {
    for (;;) {
        const int objectId = stream->ReadId();
        if (objectId == kNoObjectId) return;
        MapObject* object = objectManager->FindMapObject(objectId);
        if (stream->version >= kVersionMapObjectName) object->TakePrototype(stream->ReadString());
        object->WriteFullState();
        object->Load(stream);
        int changed = stream->ReadAs<int>();

        object->AddRef();
        object->MarkChanged(NetField::None);
        if (object->MapType() != MapObjectType::MagicTrap) {
            object->SetUpAnimatedFigure(object->name);
            aiMap.sectorMap.Add(object);
        }
        objects.Set(static_cast<unsigned>(object->id), object);
        mapObjects.Set(static_cast<unsigned>(object->id), object);
        if (object->GetClassId() != ClassId::Unit)
            otherMapObjects.Set(static_cast<unsigned>(object->id), object);
        if (!object->name2.empty())
            namedObjects.Set(static_cast<unsigned>(object->id), object);

        if (Unit* unit = UnitOrNull(object)) {
            // The unit's six dwords come from its prototype's model record; a prototype or model out of range
            // leaves them as they are (the original read them through null).
            auto* prototype = unitdb::PrototypeRecord(unit->prototypeId);
            auto* model = prototype ? unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex)) : nullptr;
            if (model) std::memcpy(unit->actionSpeeds, model->actionSpeeds, 24);
        }

        // Every part is marked changed when any change was saved (not just the parts whose bit is set).
        if (changed != 0) {
            const int parts = static_cast<int>(std::ssize(object->netState.fields));
            for (int i = 0; i < parts; ++i) object->MarkChanged(static_cast<NetField>(i));
        }

        // A unit whose controller does not ask for updates stays out of the update map.
        Unit* unit = UnitOrNull(object);
        if (!unit || unit->ServerController()->brain.alwaysUpdated) AddToUpdateMap(object);
    }
}

int WorldServer::LoadSave(ByteStream* stream, const char* map) {
    gameState = GameState::None;
    scriptsActive = 0;
    if (PlayerList()) {
        PlayerIterator it;
        for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
            gameState = GameState::Playing;
            scriptsActive = 1;
            player->Clear();
        }
    }
    aiMap.BeginRefresh();
    loaded = 0;
    aiMap.refreshUnits = false;

    const std::string newMobName = stream->ReadString();
    std::string newMapName = stream->ReadString();
    const std::string newZoneName = stream->ReadString();
    if (map) newMapName = map;
    zoneName = newZoneName;
    SelectLoadingMovie(newZoneName);
    ShowLoadProgress(kProgressStart);
    {
        PlayerIterator it;
        for (Player* player = it.First(PlayerList()); player; player = it.Next())
            player->dirty = kPlayerDirtyAll;
    }

    stream->ReadValue(windDirection);
    stream->ReadValue(windStrength);
    stream->ReadValue(nightLevel);
    for (WorldAlarm& alarm : alarms) stream->ReadValue(alarm);
    stream->ReadValue(tick);
    stream->ReadValue(weather);
    stream->ReadValue(weatherStrength);
    const float time = stream->ReadAs<float>();
    float speed;
    if (stream->version >= kVersionTimeSpeedFloat) {
        stream->ReadValue(speed);
        if (stream->version >= kVersionRainBoltTick) stream->ReadValue(lastRainBoltTick);
    } else {
        speed = static_cast<float>(stream->ReadAs<int>());
    }
    stream->ReadValue(zoneMode);

    // The AI graph state as the zone files have it, then with the save's differences applied.
    MemoryStream savedGraph;
    ReadAIGraph(newMapName, newMobName, &savedGraph);
    MemoryStream graph;
    graph.ApplyDiff(&savedGraph, stream);
    ShowLoadProgress(kProgressZone);
    if (!LoadMap(newMapName, newMobName, &graph, 1)) game::FatalExit("%s", "CWorldServer::LoadSave()\n Can't load map.\n");

    // The map objects' identities: id, type and prototype id.
    for (;;) {
        const int objectId = stream->ReadId();
        if (objectId == kNoObjectId) break;
        int objectType = stream->ReadAs<int>();
        EngineObject* object = objectManager->CreateObject(objectType, objectId);
        object->prototypeId = stream->ReadAs<std::uint32_t>();
    }
    timedObjectManager->Load(stream);
    spellManager->Load(stream);
    itemManager->Load(stream);
    // The packed ids are tested before the map objects load: a unit's load reads its weapon's type. Then again
    // after the objects' own data (an item reads its packed id again, a spell its modifiers).
    RequireEveryRecord(this);
    ShowLoadProgress(kProgressManagers);
    LoadSavedMapObjects(stream);
    timedObjectManager->LoadObjects(stream);
    spellManager->LoadObjects(stream);
    itemManager->LoadObjects(stream);
    RequireEveryRecord(this);
    ShowLoadProgress(kProgressObjects);

    // Timed objects (spells, effects) by id.
    for (;;) {
        const int objectId = stream->ReadId();
        if (objectId == kNoObjectId) break;
        auto* object = timedObjectManager->FindObjectAs<Effect>(objectId);
        object->AddRef();
        object->MarkChanged();
        timedObjects.Set(static_cast<unsigned>(object->id), object);
    }

    // Missiles: a byte 1 before each one, 0 at the end. They find the world through the global server while
    // they load.
    g_loadingProjectileWorld = this;
    while (stream->ReadAs<std::uint8_t>()) {
        auto* missile = new SpellEffect();
        missile->Load(stream);
        projectiles.emplace_back(missile);
    }

    aiMap.events.Load(stream);
    MainScript()->LoadState(stream);
    for (SpellEffect* missile : projectiles) missile->ResumeScripted();
    diplomacy.Load(stream);
    stream->ReadValue(climate);
    stream->ReadValue(cave);
    if (stream->version >= kVersionCountdowns)    // the countdowns are a tween array
        countdowns.Load(stream);

    // Counted references to items (added at the head of the list).
    for (;;) {
        const int objectId = stream->ReadId();
        if (objectId < 0) break;
        EngineObject* item = itemManager->FindObject(objectId);
        if (!item) continue;
        item->AddRef();
        counted.insert(counted.begin(), item);
    }

    if (PlayerList()) LoadPlayers(this, stream);
    stream->ReadValue(paused);

    // The item manager's object records.
    if (stream->version >= kVersionPlayerLists) {
        for (;;) {
            const int objectId = stream->ReadId();
            if (objectId == kNoObjectId) break;
            const auto value = stream->ReadAs<std::uint32_t>();
            Item* object = ItemFactory()->FindObjectAs<Item>(objectId);
            if (!object) continue;
            auto* record = new ItemLink();
            record->value = value;
            object->serverLink.Assign(record);
        }
    }

    ShowLoadProgress(kProgressDone);
    zoneName = newZoneName;
    SetTimeSpeed(speed);
    SetTime(time);
    loaded = 1;
    aiMap.EndRefresh();
    aiMap.refreshUnits = true;
    if (graph.Size() == 0) BuildAIGraph(aiMap);

    RelocateStrandedUnits(this);

    if (stream->version >= kVersionMobFiles) {
        for (int count = stream->ReadCount(); count > 0; --count) mobFiles.push_back(stream->ReadString());
    } else {
        mobFiles.clear();
    }

    if (stream->version >= kVersionSharedObjects) {
        for (int count = stream->ReadCount(); count > 0; --count) sharedObjects.emplace_back();
        for (SharedObject& entry : sharedObjects) entry.Load(stream);
    }

    objectManager->ReleaseUnused();
    timedObjectManager->ReleaseUnused();
    spellManager->ReleaseUnused();
    itemManager->ReleaseUnused();
    return 1;
}

void WorldServer::SaveWorld(ByteStream* stream) {
    // The AI graph state as the zone files have it (`base`) and as it is now (`current`); only
    // their differences are saved.
    MemoryStream base;
    MemoryStream current;

    if (IsSinglePlayerMode(zoneMode)) UpdatePlayerTimes(0.0f);
    objectManager->ReleaseUnused();
    timedObjectManager->ReleaseUnused();
    spellManager->ReleaseUnused();
    itemManager->ReleaseUnused();

    stream->WriteString(mobName);
    stream->WriteString(mapName);
    stream->WriteString(zoneName);
    stream->WriteValue(windDirection);
    stream->WriteValue(windStrength);
    stream->WriteValue(nightLevel);
    for (WorldAlarm& alarm : alarms) stream->WriteValue(alarm);
    stream->WriteValue(tick);
    stream->WriteValue(weather);
    stream->WriteValue(weatherStrength);
    const Clock clock = GetTime();
    stream->WriteValue(clock.hours);
    stream->WriteValue(clock.speed);
    stream->WriteValue(lastRainBoltTick);
    stream->WriteValue(zoneMode);

    // A zone without a graph file has an empty base: nothing is written, and its load builds the graph again.
    ReadAIGraph(mapName, mobName, &base);
    aiMap.regionMap.SaveBlocks(&current);
    stream->WriteDiff(&base, &current);

    // The map objects' identities (the walk stops at the first empty entry).
    for (auto node = mapObjects.map.begin(); node != mapObjects.map.end() && node->second; ++node) {
        EngineObject* object = node->second;
        stream->WriteId(object);
        stream->WriteValue(object->type);
        stream->WriteValue(object->prototypeId);
    }
    stream->WriteValue(-1);
    timedObjectManager->Save(stream);
    spellManager->Save(stream);
    itemManager->Save(stream);
    SaveMapObjects(stream);
    timedObjectManager->SaveObjects(stream);
    spellManager->SaveObjects(stream);
    itemManager->SaveObjects(stream);

    for (auto& node : timedObjects.map) stream->WriteId(node.second);
    stream->WriteValue(kNoObjectId);

    for (SpellEffect* missile : projectiles) {
        stream->WriteValue(static_cast<std::uint8_t>(1));
        missile->Save(stream);
    }
    stream->WriteValue(static_cast<std::uint8_t>(0));

    aiMap.events.Save(stream);
    MainScript()->SaveState(stream);
    diplomacy.Save(stream);
    stream->WriteValue(climate);
    stream->WriteValue(cave);
    countdowns.Save(stream);

    for (const EngineObject* object : counted) stream->WriteId(object);
    stream->WriteValue(kNoObjectId);

    if (PlayerList()) SavePlayers(this, stream);
    stream->WriteValue(paused);

    // The item manager's object records.
    {
        ObjectListIteratorOf<Item> it;
        for (Item* item = it.First(ItemFactory()); item; item = it.Next()) {
            ItemLink* record = item->serverLink.Get();
            if (!record) continue;
            stream->WriteId(item);
            stream->WriteValue(record->value);
        }
        stream->WriteValue(kNoObjectId);
    }

    stream->WriteCount(mobFiles.size());
    for (const std::string& file : mobFiles) stream->WriteString(file);

    stream->WriteCount(sharedObjects.size());
    for (const SharedObject& entry : sharedObjects) entry.Save(stream);
}

void SharedObject::Load(ByteStream* stream) {
    stream->ReadValue(objectId);
    stream->ReadValue(player);
    stream->ReadValue(money);
    for (int count = stream->ReadCount(); count > 0; --count) {
        Item* object = g_serverWorld.itemManager.get()->FindObjectAs<Item>(stream->ReadId());
        if (object) objects.emplace_back(object);
    }
}

void SharedObject::Save(ByteStream* stream) const {
    stream->WriteValue(objectId);
    stream->WriteValue(player);
    stream->WriteValue(money);
    stream->WriteCount(objects.size());
    for (const Item* object : objects) stream->WriteId(object);
}

void WorldServer::AddSharedObject(int newId, std::uint32_t value, const std::vector<Item*>& newObjects) {
    // The session player record's serial in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::g_gameServer;
    if (IsLive(session))
        if (net::PlayerMessage* record = session->FindPlayer(newId)) player = record->serial;

    // Pushing SharedObject() at the head: the new head copies an empty local (whose first three dwords the
    // original leaves unset; they are overwritten below).
    SharedObject& entry = *sharedObjects.emplace(sharedObjects.begin());
    entry.objectId = newId;
    entry.player = player;
    entry.money = value;
    for (Item* object : newObjects) entry.objects.emplace_back(object);
}

void WorldServer::PrepareSave() {
    for (auto node = mapObjects.map.begin(); node != mapObjects.map.end() && node->second; ++node) {
        Unit* unit = UnitOrNull(node->second);
        if (!unit || unit->IsDead()) continue;
        UnitController* controller = unit->controller;
        unit->ServerController()->stats.RefillSlots();
        controller->RefreshVitals(false);
    }
}

int WorldServer::NewZone(std::string_view map, std::string_view mob, std::optional<std::string_view> zone) {
    cave = 0;
    SelectLoadingMovie(zone);
    ShowLoadProgress(kProgressStart);
    if (MapFileMissing(map, modfolders::MapsSite::NewZoneMap)) return 0;
    // The objects may be the .mob's TOML alone (a mod's new zone; docs/notes/mods.md, "Map objects as TOML").
    if (MapFileMissing(mob, modfolders::MapsSite::NewZoneMob) &&
        MapFileMissing(std::string(mob) + ".toml", modfolders::MapsSite::NewZoneMob))
        return 0;
    MemoryStream graph;
    ReadAIGraph(map, mob, &graph);
    mobFiles.clear();
    aiMap.BeginRefresh();
    loaded = 0;
    std::memset(alarms, 0, sizeof(alarms));
    aiMap.refreshUnits = false;
    if (!LoadMap(map, mob, &graph, 0))
        game::FatalExit(kCantLoadMap);
    ShowLoadProgress(kProgressObjects);
    loaded = 1;
    aiMap.EndRefresh();
    aiMap.refreshUnits = true;
    if (graph.Size() == 0) BuildAIGraph(aiMap);
    ShowLoadProgress(kProgressDone);
    if (zone) zoneName = *zone;
    scriptsActive = 0;
    return 1;
}
