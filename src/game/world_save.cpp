// WorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
// back, LoadSavedMapObjects restores the map objects SaveMapObjects wrote, and UpdatePlayerTimes
// brings the players' "gtime" up to date before a save. See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "base_manager.h"
#include "bink_video.h"
#include "combat.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_strings.h"
#include "item.h"
#include "item_manager.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "mod_folders.h"
#include "model.h"
#include "net_delta.h"
#include "net_messages.h"
#include "net_session.h"
#include "object.h"
#include "object_diplomacy.h"
#include "party.h"
#include "player_manager.h"
#include "player_messages.h"
#include "save_versions.h"
#include "script.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"
#include "world_effect.h"
#include "world_map_files.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "platform/file.h"
#include "lib/pc24.h"

#include <bit>
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

template <typename T>
T ReadAs(ByteStream* stream) {
    T value;
    stream->ReadValue(value);
    return value;
}

// The units saved standing where they cannot move (a cell they cannot leave) are put on a free
// cell nearby when they are scripted units (text-key ids), otherwise removed.
void RelocateStrandedUnits(WorldServer* world) {
    std::vector<int> stranded;
    for (auto& node : world->objects.map) {
        CUnit* unit = UnitOrNull(node.second);
        if (!unit) continue;
        if (world->aiMap.CanLeaveCell(unit)) continue;
        int id = unit->id;
        int index = lib::Count(stranded);
        stranded.resize(index + 1);
        stranded[index] = id;
    }
    for (int i = 0; i < std::ssize(stranded); ++i) {
        CUnit* unit = world->FindUnit(stranded[i]);
        if (unit && unit->id > kTextKeyIdMin) {
            world->aiMap.sectorMap.Remove(unit);
            Vector3 from = unit->localPosition;
            Vector3 to = unit->localPosition;
            world->aiMap.FindFreeCellNear(unit, from, &to);
            unit->localPosition = to;
            world->aiMap.sectorMap.Add(unit);
            unit->ServerController()->ResyncWithUnit();
        } else {
            world->RemoveObject(stranded[i]);
        }
    }
}

// The players' part of the save (in the player list's order).
void LoadPlayers(WorldServer* world, ByteStream* stream) {
    PlayerIterator it;
    for (CPlayer* player = it.First(world->PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        stream->ReadValue(player->partyId);
        player->Party()->LoadParty(stream, 1);
        player->lastTick = world->tick;
        stream->ReadValue(player->sessionKind);
        BaseManager* items = world->ItemFactory();
        for (;;) {
            int id = ReadAs<int>(stream);
            if (id == -1) break;
            Item* object = FindItem(items, id);
            if (!object) continue;
            player->items.push_back(object);
            object->AddRef();
        }
        stream->ReadValue(player->godMode);
        if (stream->version >= kVersionPlayerLists) {
            // `count` zeros appended, then every id read from the head (those already listed too).
            std::vector<std::uint32_t>& ids = player->recharging;
            int count = ReadAs<int>(stream);
            for (int i = 0; i < count; ++i) ids.push_back(0);
            for (std::uint32_t& id : ids) stream->ReadValue(id);
        }
        if (stream->version < kVersionPlayerByte) ReadAs<std::uint8_t>(stream);
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
    for (CPlayer* player = it.First(world->PlayerList()); player; player = it.Next()) {
        stream->WriteValue(player->partyId);
        player->Party()->SaveParty(stream, 1);
        stream->WriteValue(player->sessionKind);
        for (Item* item : player->items) stream->WriteValue(item->id);
        stream->WriteValue(-1);
        stream->WriteValue(player->godMode);
        const std::vector<std::uint32_t>& ids = player->recharging;
        stream->WriteValue(static_cast<std::int32_t>(std::ssize(ids)));
        for (std::uint32_t id : ids) stream->WriteValue(id);
    }
}


constexpr const char* kCantLoadGraph = "CWorldServer::LoadMap.\n Can't load AIGraph.\n";
constexpr const char* kCantLoadMap = "CWorldServer::NewZone()\n Can't load map.\n";

// Whether the file `name` is neither there as named nor in the maps folder (Rebirth's Maps names it
// first: `site`).
bool MapFileMissing(std::string_view name, modfolders::MapsSite site) {
    const std::string file(name);
    if (platform::FileExists(file)) return false;
    modfolders::SelectMaps(name, site);
    return !platform::FileExists(MapsFolder() + file);
}

} // namespace


// The sum in the game's order ((elapsed * secondsPerTick + offset) + gtime); plain C++ maths, compared with
// CheckNear.
void WorldServer::UpdatePlayerTimes(float offset) {
    const char* name = game::GTimeText();
    int now = tick;
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        int elapsed = static_cast<int>(static_cast<unsigned>(now) - static_cast<unsigned>(player->lastTick));
        player->lastTick = now;
        if (elapsed < 0) elapsed = 0;
        float current = player->Party()->variables.Get(name);
        float value = pc24::Mul(static_cast<double>(elapsed), secondsPerTick) + offset + current;
        player->Server()->SetVariable(name, std::bit_cast<std::uint32_t>(value));
    }
}

void WorldServer::LoadSavedMapObjects(ByteStream* stream) {
    for (;;) {
        int objectId = ReadAs<int>(stream);
        if (objectId == -1) return;
        CMapObject* object = objectManager->FindMapObject(objectId);
        if (stream->version >= kVersionMapObjectName) {
            std::string name;
            name = stream->ReadString();
            object->TakePrototype(name);
        }
        object->WriteFullState();
        object->Load(stream);
        int changed = ReadAs<int>(stream);
        if (!object) continue;   // (the object was already used above)

        object->AddRef();
        object->MarkChanged(-1);
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

        if (CUnit* unit = UnitOrNull(object)) {
            // The unit's six dwords come from its prototype's model record. A prototype or model out of range is
            // read through null.
            auto* prototype = unitdb::PrototypeRecord(unit->prototypeId);
            auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
            std::memcpy(unit->actionSpeeds, model->actionSpeeds, 24);
        }

        // Every part is marked changed when any change was saved (the part's bit 1 << i is tested itself, never
        // `changed & bit`).
        int parts = static_cast<int>(std::ssize(object->netState.fields));
        for (int i = 0; i < parts; ++i)
            if (changed != 0 && (1u << (i & 31)) != 0) object->MarkChanged(i);

        CUnit* unit = UnitOrNull(object);
        if (unit && !unit->ServerController()->brain.alwaysUpdated)
            continue;   // a unit whose controller does not ask for updates
        AddToUpdateMap(object);
    }
}

int WorldServer::LoadSave(ByteStream* stream, const char* map) {
    MemoryStream savedGraph;   // the AI graph state as the zone files have it
    MemoryStream graph;        // ... with the save's differences applied
    std::string newMobName;
    std::string newMapName;
    std::string newZoneName;

    gameResult = 0;
    scriptsActive = 0;
    if (PlayerList()) {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
            gameResult = 1;
            scriptsActive = 1;
            player->Clear();
        }
    }
    aiMap.BeginRefresh();
    loaded = 0;
    aiMap.refreshUnits = false;

    newMobName = stream->ReadString();
    newMapName = stream->ReadString();
    newZoneName = stream->ReadString();
    if (map) newMapName = map;
    zoneName = newZoneName;
    SelectLoadingMovie(newZoneName);
    ShowLoadProgress(kProgressStart);
    {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->dirty = kPlayerDirtyAll;
    }

    stream->Read(windDirection, 12);
    stream->ReadValue(windStrength);
    stream->ReadValue(nightLevel);
    for (WorldAlarm& alarm : alarms) stream->ReadValue(alarm);
    stream->ReadValue(tick);
    stream->ReadValue(weather);
    stream->ReadValue(weatherStrength);
    float time = ReadAs<float>(stream);
    float speed;
    if (stream->version >= kVersionTimeSpeedFloat) {
        stream->ReadValue(speed);
        if (stream->version >= kVersionRainBoltTick) stream->ReadValue(lastRainBoltTick);
    } else {
        speed = static_cast<float>(ReadAs<int>(stream));
    }
    stream->ReadValue(sessionKind);

    int result;
    if (!ReadAIGraph(newMapName, newMobName, &savedGraph)) {
        result = 0;
    } else {
        graph.ApplyDiff(&savedGraph, stream);
        ShowLoadProgress(kProgressZone);
        if (!LoadMap(newMapName, newMobName, &graph, 1)) {
            result = game::FatalError(game::CantLoadSaveMapText());
        } else {
            // The map objects' identities: id, type and prototype id.
            for (;;) {
                int objectId = ReadAs<int>(stream);
                if (objectId == -1) break;
                int objectType = ReadAs<int>(stream);
                std::uint32_t value18 = ReadAs<std::uint32_t>(stream);
                EngineObject* object = objectManager->CreateObject(objectType, objectId);
                object->prototypeId = value18;
            }
            timedObjectManager->Load(stream);
            spellManager->Load(stream);
            itemManager->Load(stream);
            ShowLoadProgress(kProgressManagers);
            LoadSavedMapObjects(stream);
            timedObjectManager->LoadObjects(stream);
            spellManager->LoadObjects(stream);
            itemManager->LoadObjects(stream);
            ShowLoadProgress(kProgressObjects);

            // Timed objects (spells, effects) by id.
            for (;;) {
                int objectId = ReadAs<int>(stream);
                if (objectId == -1) break;
                EngineObject* object = timedObjectManager->FindObject(objectId);
                object->AddRef();
                static_cast<CEffect*>(object)->MarkChanged();
                timedObjects.Set(static_cast<unsigned>(object->id), object);
            }

            // Missiles: a byte 1 before each one, 0 at the end. They find the world through the global server while
            // they load.
            g_loadingProjectileWorld = this;
            for (;;) {
                std::uint8_t more = ReadAs<std::uint8_t>(stream);
                if (!more) break;
                auto* missile = new SpellEffect();
                missile->refs = 0;    // the count and flags are cleared before the null test
                missile->flags6 = 0;
                missile->flags7 = 0;
                missile->Load(stream);
                projectiles.emplace_back(missile);
            }

            aiMap.events.Load(stream);
            MainScript()->LoadState(stream);
            diplomacy.ReadFrom(stream);
            stream->ReadValue(climate);
            stream->ReadValue(cave);
            if (stream->version >= kVersionCountdowns)    // the countdowns are a tween array
                countdowns.LoadEntries(stream);

            // Counted references to items (added at the head of the list).
            for (;;) {
                int objectId = ReadAs<int>(stream);
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
                    int objectId = -1;
                    stream->ReadValue(objectId);
                    if (objectId == -1) break;
                    std::uint32_t value = ReadAs<std::uint32_t>(stream);
                    Item* object = FindItem(ItemFactory(), objectId);
                    if (!object) continue;
                    auto* record = new ItemLink();
                    record->value = value;
                    object->link.Assign(record);
                }
            }

            ShowLoadProgress(kProgressDone);
            zoneName = newZoneName;
            SetTimeSpeed(speed);
            SetTime(time);
            loaded = 1;
            aiMap.EndRefresh();
            aiMap.refreshUnits = true;

            RelocateStrandedUnits(this);

            if (stream->version >= kVersionMobFiles) {
                int count = ReadAs<int>(stream);
                for (int i = 0; i < count; ++i) {
                    mobFiles.push_back(stream->ReadString());
                }
            } else {
                mobFiles.clear();
            }

            if (stream->version >= kVersionSharedObjects) {
                std::vector<SharedObject>& shared = sharedObjects;
                int count = ReadAs<int>(stream);
                for (int i = 0; i < count; ++i) shared.emplace_back();
                for (SharedObject& entry : shared) entry.Load(stream);
            }

            objectManager->ReleaseUnused();
            timedObjectManager->ReleaseUnused();
            spellManager->ReleaseUnused();
            itemManager->ReleaseUnused();
            result = 1;
        }
    }
    return result;
}

void WorldServer::SaveWorld(ByteStream* stream) {
    // The AI graph state as the zone files have it (`base`) and as it is now (`current`); only
    // their differences are saved.
    MemoryStream base;
    MemoryStream current;

    if (sessionKind == 1 || sessionKind == 3) UpdatePlayerTimes(0.0f);
    objectManager->ReleaseUnused();
    timedObjectManager->ReleaseUnused();
    spellManager->ReleaseUnused();
    itemManager->ReleaseUnused();

    stream->WriteString(mobName);
    stream->WriteString(mapName);
    stream->WriteString(zoneName);
    stream->Write(windDirection, 12);
    stream->WriteValue(windStrength);
    stream->WriteValue(nightLevel);
    for (WorldAlarm& alarm : alarms) stream->WriteValue(alarm);
    stream->WriteValue(tick);
    stream->WriteValue(weather);
    std::uint32_t e8;
    e8 = std::bit_cast<std::uint32_t>(weatherStrength);
    stream->WriteValue(e8);
    // Stored by the callee as floats; written on as their bytes.
    float time;
    float speed;
    GetTime(&time, &speed);
    stream->WriteValue(time);
    stream->WriteValue(speed);
    stream->WriteValue(lastRainBoltTick);
    stream->WriteValue(sessionKind);

    ReadAIGraph(mapName, mobName, &base);
    aiMap.regionMap.SaveBlocks(&current);
    stream->WriteDiff(&base, &current);

    // The map objects' identities (the walk stops at the first empty entry).
    for (auto node = mapObjects.map.begin(); node != mapObjects.map.end() && node->second; ++node) {
        EngineObject* object = node->second;
        stream->WriteValue(object->id);
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

    for (auto& node : timedObjects.map) stream->WriteValue(node.second->id);
    stream->WriteValue(-1);

    for (SpellEffect* missile : projectiles) {
        stream->WriteValue(static_cast<std::uint8_t>(1));
        missile->Save(stream);
    }
    stream->WriteValue(static_cast<std::uint8_t>(0));

    aiMap.events.Save(stream);
    MainScript()->SaveState(stream);
    diplomacy.WriteTo(stream);
    stream->WriteValue(climate);
    stream->WriteValue(cave);
    countdowns.Save(stream);

    for (EngineObject* object : counted) stream->WriteValue(object->id);
    stream->WriteValue(-1);

    if (PlayerList()) SavePlayers(this, stream);
    stream->WriteValue(paused);

    // The item manager's object records.
    {
        ObjectListIteratorOf<Item> it{};
        for (Item* item = it.First(ItemFactory()); item; item = it.Next()) {
            ItemLink* record = item->ServerLink();
            if (!record) continue;
            stream->WriteValue(item->id);
            stream->WriteValue(record->value);
        }
        stream->WriteValue(-1);
        if (it.manager) --it.manager->refCount;
    }

    stream->WriteValue(static_cast<int>(mobFiles.size()));
    for (const std::string& file : mobFiles) stream->WriteString(file);

    stream->WriteValue(static_cast<int>(sharedObjects.size()));
    for (const SharedObject& entry : sharedObjects) {
        stream->WriteValue(entry.objectId);
        stream->WriteValue(entry.player);
        stream->WriteValue(entry.money);
        stream->WriteValue(static_cast<int>(entry.objects.size()));
        for (Item* object : entry.objects) stream->WriteValue(object->id);
    }
}

void SharedObject::Load(ByteStream* stream) {
    SharedObject* entry = this;
    stream->ReadValue(entry->objectId);
    stream->ReadValue(entry->player);
    stream->ReadValue(entry->money);
    int count = ReadAs<int>(stream);
    for (int i = 0; i < count; ++i) {
        int id = ReadAs<int>(stream);
        Item* object = FindItem(g_serverWorld.itemManager, id);
        if (object) entry->objects.emplace_back(object);
    }
}

void WorldServer::AddSharedObject(int newId, std::uint32_t value, const std::vector<Item*>* newObjects) {
    // The session player record's serial in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::g_gameServer;
    if (session && !(session->flags7 & RefCounted::kDetached))
        if (net::PlayerMessage* record = session->FindPlayer(newId)) player = record->serial;

    // Pushing SharedObject() at the head: the new head copies an empty local (whose first three dwords the
    // original leaves unset; they are overwritten below).
    SharedObject& entry = *sharedObjects.emplace(sharedObjects.begin());
    entry.objectId = newId;
    entry.player = player;
    entry.money = value;
    for (Item* object : *newObjects) entry.objects.emplace_back(object);
}

void WorldServer::PrepareSave() {
    for (auto node = mapObjects.map.begin(); node != mapObjects.map.end() && node->second; ++node) {
        CUnit* unit = UnitOrNull(node->second);
        if (!unit || unit->IsDead()) continue;
        UnitController* controller = unit->Controller();
        unit->ServerController()->stats.RefillSlots();
        controller->RefreshVitals(false);
    }
}

int WorldServer::NewZone(std::string_view map, std::string_view mob, std::optional<std::string_view> zone) {
    cave = 0;
    SelectLoadingMovie(zone);
    ShowLoadProgress(kProgressStart);
    if (MapFileMissing(map, modfolders::MapsSite::NewZoneMap)) return 0;
    if (MapFileMissing(mob, modfolders::MapsSite::NewZoneMob)) return 0;
    MemoryStream graph;
    if (!ReadAIGraph(map, mob, &graph))
        return game::FatalError(kCantLoadGraph);
    mobFiles.clear();
    aiMap.BeginRefresh();
    loaded = 0;
    std::memset(alarms, 0, sizeof(alarms));
    aiMap.refreshUnits = false;
    if (!LoadMap(map, mob, &graph, 0))
        return game::FatalError(kCantLoadMap);
    ShowLoadProgress(kProgressObjects);
    loaded = 1;
    aiMap.EndRefresh();
    aiMap.refreshUnits = true;
    ShowLoadProgress(kProgressDone);
    if (zone) zoneName = *zone;
    scriptsActive = 0;
    return 1;
}
