// CWorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
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

#include "lib/crt.h"
#include "lib/file_io.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kTextKeyIdMin = 100000000;                      // ids above this are scripted (text-key) units

// Save format versions (WorldStream::version_) at which fields were added.

// Progress screen stages LoadSave shows.
constexpr int kProgressStart = 0;
constexpr int kProgressZone = 1;
constexpr int kProgressManagers = 5;
constexpr int kProgressObjects = 6;
constexpr int kProgressDone = 7;

// ---- Local layouts ----------------------------------------------------------------------------

void ShowLoadProgress(int stage) { ShowLoadingMovie(stage); }

// Reads a string and assigns it to `target` (the temporary is destroyed at once).
void ReadStringInto(CStream* stream, std::string& target) {
    std::string storage;
    std::string* read = stream->ReadString(&storage);
    target = *read;
}

template <typename T>
T ReadAs(CStream* stream) {
    T value;
    stream->ReadValue(value);
    return value;
}

// The units saved standing where they cannot move (a cell they cannot leave) are put on a free
// cell nearby when they are scripted units (text-key ids), otherwise removed.
void RelocateStrandedUnits(CWorldServer* world) {
    std::vector<int> stranded;
    for (auto& node : world->objects_.map) {
        CUnit* unit = UnitOrNull(node.second);
        if (!unit) continue;
        if (world->aiMap_.CanLeaveCell(unit)) continue;
        int id = unit->id_;
        int index = std::ssize(stranded);
        stranded.resize(index + 1);
        stranded[index] = id;
    }
    for (int i = 0; i < std::ssize(stranded); ++i) {
        CUnit* unit = world->FindUnit(stranded[i]);
        if (unit && unit->id_ > kTextKeyIdMin) {
            world->aiMap_.sectorMap.Remove(unit);
            Vector3 from = unit->localPosition_;
            Vector3 to = unit->localPosition_;
            world->aiMap_.FindFreeCellNear(unit, &from, &to);
            unit->localPosition_ = to;
            world->aiMap_.sectorMap.Add(unit);
            unit->ServerController()->ResyncWithUnit();
        } else {
            world->RemoveObject(stranded[i]);
        }
    }
}

// The players' part of the save (in the player list's order).
void LoadPlayers(CWorldServer* world, CStream* stream) {
    PlayerIterator it;
    for (CPlayer* player = it.First(world->PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->Server();
        stream->ReadValue(player->partyId_);
        player->Party()->LoadParty(stream, 1);
        player->lastTick_ = world->tick_;
        stream->ReadValue(player->sessionKind_);
        CBaseManager* items = world->ItemFactory();
        for (;;) {
            int id = ReadAs<int>(stream);
            if (id == -1) break;
            CItem* object = FindItem(items, id);
            if (!object) continue;
            player->items_.push_back(object);
            ++object->refCount_;
        }
        stream->ReadValue(player->godMode_);
        if (stream->version_ >= kVersionPlayerLists) {
            std::list<std::uint32_t>& ids = player->recharging_;
            int count = ReadAs<int>(stream);
            for (int i = 0; i < count; ++i) ids.push_back(0);
            for (auto node = ids.begin(); node != ids.end();) {
                auto next = std::next(node);
                stream->ReadValue(*node);
                node = next;
            }
        }
        if (stream->version_ < kVersionPlayerByte) ReadAs<std::uint8_t>(stream);
        if (stream->version_ < kVersionPlayerGroup) {
            ScriptGroup group;
            group.Load(stream);
        }
        state->SendParty();
        state->SetPartyDeployed(1);
    }
}

// The players' part of the save (what LoadPlayers reads).
void SavePlayers(CWorldServer* world, CStream* stream) {
    PlayerIterator it;
    for (CPlayer* player = it.First(world->PlayerList()); player; player = it.Next()) {
        stream->WriteValue(player->partyId_);
        player->Party()->SaveParty(stream, 1);
        stream->WriteValue(player->sessionKind_);
        for (auto node = player->items_.begin(); node != player->items_.end();) {
            auto next = std::next(node);
            stream->WriteValue((*node)->id_);
            node = next;
        }
        stream->WriteValue(-1);
        stream->WriteValue(player->godMode_);
        std::list<std::uint32_t>& ids = player->recharging_;
        stream->WriteValue(std::ssize(ids));
        for (auto node = ids.begin(); node != ids.end();) {
            auto next = std::next(node);
            stream->WriteValue(*node);
            node = next;
        }
    }
}


constexpr const char* kCantLoadGraph = "CWorldServer::LoadMap.\n Can't load AIGraph.\n";
constexpr const char* kCantLoadMap = "CWorldServer::NewZone()\n Can't load map.\n";

// Whether the file `name` is neither there as named nor in the maps folder (Rebirth's Maps names it
// first: `site`).
bool MapFileMissing(std::string_view name, modfolders::MapsSite site) {
    const std::string file(name);
    if (lib::FileExists(file)) return false;
    std::string folder;
    modfolders::SelectMaps(name, site);
    MapsFolder(&folder);
    return !lib::FileExists(folder + file);
}

} // namespace


// The sum in the game's order ((elapsed * secondsPerTick + offset) + gtime); plain C++ maths, compared with
// CheckNear.
void CWorldServer::UpdatePlayerTimes(float offset) {
    const char* name = game::GTimeText();
    int now = tick_;
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        int elapsed = static_cast<int>(static_cast<unsigned>(now) - static_cast<unsigned>(player->lastTick_));
        player->lastTick_ = now;
        if (elapsed < 0) elapsed = 0;
        double current = player->Party()->variables_.Get(name);
        float value = static_cast<float>(static_cast<double>(elapsed) * secondsPerTick_ + offset + current);
        player->Server()->SetVariable(name, std::bit_cast<std::uint32_t>(value));
    }
}

void CWorldServer::LoadSavedMapObjects(CStream* stream) {
    for (;;) {
        int id = ReadAs<int>(stream);
        if (id == -1) return;
        CMapObject* object = objectManager_->FindMapObject(id);
        if (stream->version_ >= kVersionMapObjectName) {
            std::string name;
            ReadStringInto(stream, name);
            object->TakePrototype(name);
        }
        object->WriteFullState();
        object->Load(stream);
        int changed = ReadAs<int>(stream);
        if (!object) continue;   // (the object was already used above)

        ++object->refCount_;
        object->MarkChanged(-1);
        if (object->type_ != 0x3B) {
            object->SetUpAnimatedFigure(object->name);
            aiMap_.sectorMap.Add(object);
        }
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != kClassUnit)
            otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (!object->name2.empty())
            namedObjects_.Set(static_cast<unsigned>(object->id_), object);

        if (CUnit* unit = UnitOrNull(object)) {
            // The unit's six dwords come from its prototype's model record. A prototype or model out of range is
            // read through null.
            auto* prototype = unitdb::PrototypeRecord(unit->prototypeId_);
            auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
            std::memcpy(unit->actionSpeeds, model->actionSpeeds, 24);
        }

        // Every part is marked changed when any change was saved (the part's bit 1 << i is tested itself, never
        // `changed & bit`).
        int parts = static_cast<int>(std::ssize(object->netState_.fields_));
        for (int i = 0; i < parts; ++i)
            if (changed != 0 && (1u << (i & 31)) != 0) object->MarkChanged(i);

        CUnit* unit = UnitOrNull(object);
        if (unit && unit->ServerController()->brain.alwaysUpdated == 0)
            continue;   // a unit whose controller does not ask for updates
        AddToUpdateMap(object);
    }
}

int CWorldServer::LoadSave(CStream* stream, const char* map) {
    CMemoryStream savedGraph;   // the AI graph state as the zone files have it
    CMemoryStream graph;        // ... with the save's differences applied
    std::string mobName;
    std::string mapName;
    std::string zoneName;

    gameResult_ = 0;
    scriptsActive_ = 0;
    if (PlayerList()) {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
            gameResult_ = 1;
            scriptsActive_ = 1;
            player->Clear();
        }
    }
    aiMap_.BeginRefresh();
    loaded_ = 0;
    aiMap_.refreshUnits = false;

    ReadStringInto(stream, mobName);
    ReadStringInto(stream, mapName);
    ReadStringInto(stream, zoneName);
    if (map) mapName = map;
    zoneName_ = zoneName;
    SelectLoadingMovie(zoneName);
    ShowLoadProgress(kProgressStart);
    {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->dirty_ = 0xFFFFFFFFu;
    }

    stream->Read(windDirection_, 12);
    stream->ReadValue(windStrength_);
    stream->ReadValue(nightLevel_);
    for (WorldAlarm& alarm : alarms_) stream->ReadValue(alarm);
    stream->ReadValue(tick_);
    stream->ReadValue(weather_);
    stream->ReadValue(weatherStrength_);
    float time = ReadAs<float>(stream);
    float speed;
    if (stream->version_ >= kVersionTimeSpeedFloat) {
        stream->ReadValue(speed);
        if (stream->version_ >= kVersionRainBoltTick) stream->ReadValue(lastRainBoltTick_);
    } else {
        speed = static_cast<float>(ReadAs<int>(stream));
    }
    stream->ReadValue(sessionKind_);

    int result;
    if (!ReadAIGraph(mapName, mobName, &savedGraph)) {
        result = 0;
    } else {
        graph.ApplyDiff(&savedGraph, stream);
        ShowLoadProgress(kProgressZone);
        if (!LoadMap(mapName, mobName, &graph, 1)) {
            result = game::FatalError(game::CantLoadSaveMapText());
        } else {
            // The map objects' identities: id, type and prototype id.
            for (;;) {
                int id = ReadAs<int>(stream);
                if (id == -1) break;
                int type = ReadAs<int>(stream);
                std::uint32_t value18 = ReadAs<std::uint32_t>(stream);
                CEngineObject* object = objectManager_->CreateObject(type, id);
                object->prototypeId_ = value18;
            }
            timedObjectManager_->Load(stream);
            spellManager_->Load(stream);
            itemManager_->Load(stream);
            ShowLoadProgress(kProgressManagers);
            LoadSavedMapObjects(stream);
            timedObjectManager_->LoadObjects(stream);
            spellManager_->LoadObjects(stream);
            itemManager_->LoadObjects(stream);
            ShowLoadProgress(kProgressObjects);

            // Timed objects (spells, effects) by id.
            for (;;) {
                int id = ReadAs<int>(stream);
                if (id == -1) break;
                CEngineObject* object = timedObjectManager_->FindObject(id);
                ++object->refCount_;
                static_cast<CEffect*>(object)->MarkChanged();
                timedObjects_.Set(static_cast<unsigned>(object->id_), object);
            }

            // Missiles: a byte 1 before each one, 0 at the end. They find the world through the global server while
            // they load.
            g_loadingProjectileWorld = this;
            for (;;) {
                std::uint8_t more = ReadAs<std::uint8_t>(stream);
                if (!more) break;
                auto* missile = new CSpellEffect();
                missile->refs_ = 0;   // the count and flags are cleared before the null test
                missile->flags6_ = 0;
                missile->flags7_ = 0;
                missile->Load(stream);
                AssignCounted(projectiles_.emplace_back(), missile);
            }

            aiMap_.events.Load(stream);
            MainScript()->LoadState(stream);
            diplomacy_.ReadFrom(stream);
            stream->ReadValue(climate_);
            stream->ReadValue(cave_);
            if (stream->version_ >= kVersionCountdowns)   // the countdowns are a tween array
                countdowns_.LoadEntries(stream);

            // Counted references to items (added at the head of the list).
            for (;;) {
                int id = ReadAs<int>(stream);
                if (id < 0) break;
                CEngineObject* item = itemManager_->FindObject(id);
                if (!item) continue;
                auto node = counted_.emplace(counted_.begin());
                HeldObject<CEngineObject>::Of(&*node)->Assign(item);
            }

            if (PlayerList()) LoadPlayers(this, stream);
            stream->ReadValue(paused_);

            // The item manager's object records.
            if (stream->version_ >= kVersionPlayerLists) {
                for (;;) {
                    int id = -1;
                    stream->ReadValue(id);
                    if (id == -1) break;
                    std::uint32_t value = ReadAs<std::uint32_t>(stream);
                    CItem* object = FindItem(ItemFactory(), id);
                    if (!object) continue;
                    auto* record = new CItemLink();
                    record->value = value;
                    CItemLink* old = object->ServerLink();
                    object->link_ = record;
                    if (record) ++record->refs_;
                    if (old) old->Release();
                }
            }

            ShowLoadProgress(kProgressDone);
            zoneName_ = zoneName;
            SetTimeSpeed(speed);
            SetTime(time);
            loaded_ = 1;
            aiMap_.EndRefresh();
            aiMap_.refreshUnits = true;

            RelocateStrandedUnits(this);

            if (stream->version_ >= kVersionMobFiles) {
                int count = ReadAs<int>(stream);
                for (int i = 0; i < count; ++i) {
                    std::string storage;
                    std::string* name = stream->ReadString(&storage);
                    mobFiles_.push_back(name->c_str());
                }
            } else {
                mobFiles_.clear();
            }

            if (stream->version_ >= kVersionSharedObjects) {
                std::list<SharedObject>& shared = sharedObjects_;
                int count = ReadAs<int>(stream);
                for (int i = 0; i < count; ++i) shared.emplace_back();
                for (SharedObject& entry : shared) entry.Load(stream);
            }

            objectManager_->ReleaseUnused();
            timedObjectManager_->ReleaseUnused();
            spellManager_->ReleaseUnused();
            itemManager_->ReleaseUnused();
            result = 1;
        }
    }
    return result;
}

void CWorldServer::SaveWorld(CStream* stream) {
    // The AI graph state as the zone files have it (`base`) and as it is now (`current`); only
    // their differences are saved.
    CMemoryStream base;
    CMemoryStream current;

    if (sessionKind_ == 1 || sessionKind_ == 3) UpdatePlayerTimes(0.0f);
    objectManager_->ReleaseUnused();
    timedObjectManager_->ReleaseUnused();
    spellManager_->ReleaseUnused();
    itemManager_->ReleaseUnused();

    stream->WriteString(mobName_);
    stream->WriteString(mapName_);
    stream->WriteString(zoneName_);
    stream->Write(windDirection_, 12);
    stream->WriteValue(windStrength_);
    stream->WriteValue(nightLevel_);
    for (WorldAlarm& alarm : alarms_) stream->WriteValue(alarm);
    stream->WriteValue(tick_);
    stream->WriteValue(weather_);
    std::uint32_t e8;
    e8 = std::bit_cast<std::uint32_t>(weatherStrength_);
    stream->WriteValue(e8);
    // Stored by the callee as floats; written on as their bytes.
    float time;
    float speed;
    GetTime(&time, &speed);
    stream->WriteValue(time);
    stream->WriteValue(speed);
    stream->WriteValue(lastRainBoltTick_);
    stream->WriteValue(sessionKind_);

    ReadAIGraph(mapName_, mobName_, &base);
    aiMap_.regionMap.SaveBlocks(&current);
    stream->WriteDiff(&base, &current);

    // The map objects' identities (the walk stops at the first empty entry).
    for (auto node = mapObjects_.map.begin(); node != mapObjects_.map.end() && node->second; ++node) {
        CEngineObject* object = node->second;
        stream->WriteValue(object->id_);
        stream->WriteValue(object->type_);
        stream->WriteValue(object->prototypeId_);
    }
    stream->WriteValue(-1);
    timedObjectManager_->Save(stream);
    spellManager_->Save(stream);
    itemManager_->Save(stream);
    SaveMapObjects(stream);
    timedObjectManager_->SaveObjects(stream);
    spellManager_->SaveObjects(stream);
    itemManager_->SaveObjects(stream);

    for (auto& node : timedObjects_.map) stream->WriteValue(node.second->id_);
    stream->WriteValue(-1);

    for (auto node = projectiles_.begin(); node != projectiles_.end();) {
        auto next = std::next(node);
        CSpellEffect* missile = *node;
        stream->WriteValue(static_cast<std::uint8_t>(1));
        missile->Save(stream);
        node = next;
    }
    stream->WriteValue(static_cast<std::uint8_t>(0));

    aiMap_.events.Save(stream);
    MainScript()->SaveState(stream);
    diplomacy_.WriteTo(stream);
    stream->WriteValue(climate_);
    stream->WriteValue(cave_);
    countdowns_.Save(stream);

    for (auto node = counted_.begin(); node != counted_.end();) {
        auto next = std::next(node);
        stream->WriteValue((*node)->id_);
        node = next;
    }
    stream->WriteValue(-1);

    if (PlayerList()) SavePlayers(this, stream);
    stream->WriteValue(paused_);

    // The item manager's object records.
    {
        ObjectListIteratorOf<CItem> it{};
        for (CItem* item = it.First(ItemFactory()); item; item = it.Next()) {
            CItemLink* record = item->ServerLink();
            if (!record) continue;
            stream->WriteValue(item->id_);
            stream->WriteValue(record->value);
        }
        stream->WriteValue(-1);
        if (it.manager) --it.manager->refCount_;
    }

    stream->WriteValue(static_cast<int>(mobFiles_.size()));
    for (const std::string& file : mobFiles_) stream->WriteString(file);

    stream->WriteValue(static_cast<int>(sharedObjects_.size()));
    for (const SharedObject& entry : sharedObjects_) {
        stream->WriteValue(entry.objectId);
        stream->WriteValue(entry.player);
        stream->WriteValue(entry.money);
        stream->WriteValue(static_cast<int>(entry.objects.size()));
        for (CItem* object : entry.objects) stream->WriteValue(object->id_);
    }
}

void SharedObject::Load(CStream* stream) {
    SharedObject* entry = this;
    stream->ReadValue(entry->objectId);
    stream->ReadValue(entry->player);
    stream->ReadValue(entry->money);
    int count = ReadAs<int>(stream);
    for (int i = 0; i < count; ++i) {
        int id = ReadAs<int>(stream);
        CItem* object = FindItem(g_serverWorld->itemManager_, id);
        if (object) entry->objects.emplace_back(object);
    }
}

void CWorldServer::AddSharedObject(int id, std::uint32_t value, const std::list<CItem*>* objects) {
    // The session player record's serial in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::g_gameServer;
    if (session && !(session->flags7_ & CRefCounted::kDetached))
        if (net::PlayerMessage* record = session->FindPlayer(id)) player = record->serial_;

    // Pushing SharedObject() at the head: the new head copies an empty local (whose first three dwords the
    // original leaves unset; they are overwritten below).
    SharedObject& entry = sharedObjects_.emplace_front();
    entry.objectId = id;
    entry.player = player;
    entry.money = value;
    for (CItem* object : *objects) entry.objects.emplace_back(object);
}

void CWorldServer::PrepareSave() {
    for (auto node = mapObjects_.map.begin(); node != mapObjects_.map.end() && node->second; ++node) {
        CUnit* unit = UnitOrNull(node->second);
        if (!unit || unit->IsDead()) continue;
        CUnitController* controller = unit->Controller();
        unit->ServerController()->stats.RefillSlots();
        controller->RefreshVitals(0);
    }
}

int CWorldServer::NewZone(std::string_view map, std::string_view mob, std::optional<std::string_view> zone) {
    cave_ = 0;
    SelectLoadingMovie(zone);
    ShowLoadProgress(kProgressStart);
    if (MapFileMissing(map, modfolders::MapsSite::NewZoneMap)) return 0;
    if (MapFileMissing(mob, modfolders::MapsSite::NewZoneMob)) return 0;
    CMemoryStream graph;
    if (!ReadAIGraph(map, mob, &graph))
        return game::FatalError(kCantLoadGraph);
    mobFiles_.clear();
    aiMap_.BeginRefresh();
    loaded_ = 0;
    std::memset(alarms_, 0, sizeof(alarms_));
    aiMap_.refreshUnits = false;
    if (!LoadMap(map, mob, &graph, 0))
        return game::FatalError(kCantLoadMap);
    ShowLoadProgress(kProgressObjects);
    loaded_ = 1;
    aiMap_.EndRefresh();
    aiMap_.refreshUnits = true;
    ShowLoadProgress(kProgressDone);
    if (zone) zoneName_ = *zone;
    scriptsActive_ = 0;
    return 1;
}
