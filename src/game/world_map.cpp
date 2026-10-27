// CWorldServer::LoadMap: loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "core_refcount.h"
#include "game_lists.h"
#include "base_manager.h"
#include "hash_tables_400.h"
#include "object_diplomacy.h"
#include "player_manager.h"
#include "player_messages.h"
#include "world.h"
#include "spell.h"
#include "model.h"   // TweenArray (countdowns_)
#include "game_strings.h"
#include "game_arrays.h"
#include "world_map_files.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "core_streams.h"
#include "map_object.h"
#include "lib/containers.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"

namespace {



// The timed objects are ended (ClearState) and released, then the map is emptied (the walk stops at the
// first empty entry).
void EndTimedObjects(CWorldServer* world) {
    auto& map = world->timedObjects_.map;
    for (auto* node = map.First(); node && node->value; node = map.After(node)) {
        CEngineObject* object = node->value;
        object->ClearState();
        object->Release();
    }
    world->timedObjects_.map.Clear();   // nodes freed, buckets kept
    world->timedObjects_.maxId = 0;
}

void ClearMissiles(CWorldServer* world) {
    auto& list = world->projectiles_;
    for (auto* node = list.head; node; node = node->next)
        ReleaseCounted(node->data);
    list.RemoveAll();
}

} // namespace

int CWorldServer::LoadMap(const char* map, const char* mob, CStream* graph, char fromSave) {
    CAIMap* aiMap = AIMap();
    if (gamecrt::mbsicmp(mapName_.c_str(), map) != 0) {
        aiMap->Reset();
        aiMap->BeginRefresh();
        bool failed;
        {
            mfc::CString path(mfc::CString::kUnset);   // MapPath constructs it
            MapPath(&path, map);
            failed = aiMap->Load(path) == 0;
        }
        if (failed) return 0;
        mapName_ = map;
        mobName_ = game::EmptyText();
    }
    graph->Seek(0);
    if (graph->Size() != 0 && !AIMap()->Regions()->LoadBlocks(graph)) return 0;

    if (gamecrt::mbsicmp(mobName_.c_str(), mob) == 0) {
        // The same zone again: start over.
        ScriptReset(MainScript());
        ScriptReset(SecondScript());
        diplomacy_.Reset();
        ClearMapObjects();
        ClearMissiles(this);
        EndTimedObjects(this);
        keepsState_ = 0;
        loadedFresh_ = fromSave == 0;
        AIMap()->footprintTick = 0;
    } else {
        ResetZone();
        keepsState_ = 1;
        loadedFresh_ = fromSave == 0;
    }

    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty_ = 0xFFFFFFFFu;
    if (it.manager) --it.manager->refCount_;
    zoneLoaded_ = 1;
    weather_ = 0;
    weatherStrength_ = 0.0f;
    it.manager = nullptr;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) player->dirty_ |= 2;
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

namespace {

// Releases a range of counted object pointers.
void __stdcall CountedRangeRelease(void* a0, int a1) { gamearrays::ObjectPtrs::DestructElements(static_cast<CEngineObject**>(a0), a1); }

void ClearMissilePointers(CWorldServer* world) {
    auto& list = world->projectiles_;
    for (auto* node = list.head; node; node = node->next) ReleaseCounted(node->data);
    list.RemoveAll();   // the elements were destroyed above; this frees the blocks
}

} // namespace

void CWorldServer::ResetZone() {
    SharedObjectList& shared = sharedObjects_;
    for (auto* node = shared.head; node; node = node->next) node->data.DeletingDestruct(0);
    shared.RemoveAll();
    mobFiles_.SetSize(0, -1);
    countdowns_.RemoveAll();
    exits_.SetSize(0);
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ClearMissilePointers(this);
    for (auto* node = counted_.head; node; node = node->next) CountedRangeRelease(&node->data, 1);
    counted_.RemoveAll();

    if (PlayerList()) {
        auto* it = gamecrt::operator_new_as<PlayerIterator>();
        if (it) it->manager = nullptr;
        for (CPlayer* player = it->First(PlayerList()); player; player = it->Next())
            player->Clear();
        if (it) {
            if (it->manager) {
                --it->manager->refCount_;
                it->manager = nullptr;
            }
            gamecrt::operator_delete(it);
        }
    }

    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node))
        node->value->ClearState();
    ReleaseEach(objects_.map);
    objects_.maxId = 0;
    MapObjectIdMap* const maps[] = {&sentObjects_, &mapObjects_, &otherMapObjects_, &updateMap_, &namedObjects_};
    for (MapObjectIdMap* map : maps) {
        map->map.Clear();
        map->maxId = 0;
    }
    ClearMissilePointers(this);
    EndTimedObjects(this);
    mobName_ = game::EmptyText();
    zoneName_ = game::EmptyText();
    if (manager3C_) manager3C_->FreeSpares();
    if (manager40_) manager40_->FreeSpares();
    if (manager44_) manager44_->FreeSpares();
}

// ---- TeardownZone -----------------------------------------------------------------------------

namespace {

} // namespace

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

    SharedObjectList& shared = sharedObjects_;
    for (auto* node = shared.head; node; node = node->next)
        mfc::impl::DestructArrayOf<gamearrays::ObjectPtrArrayBaseClass>(mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(node->data.objects));
    shared.RemoveAll();
    mobFiles_.SetSize(0, -1);
    AIMap()->refreshUnits = false;   // refreshes off
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    diplomacy_.Reset();
    ClearMapObjects();

    // The missiles' counted pointers, released inline (16-bit count, deleted at zero).
    auto& missiles = projectiles_;
    for (auto* node = missiles.head; node; node = node->next) {
        CSpellEffect* missile = node->data;
        if (missile && --missile->refs_ == 0) delete missile;
    }
    missiles.RemoveAll();

    EndTimedObjects(this);
    CloseSession();
    gameResult_ = 0;
    sessionKind_ = 0;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

namespace {


} // namespace

void CWorldServer::SendFreshObjects() {
    mfc::CTypedPtrList<CEngineObject> fresh;
    fresh.vtable = mfc::impl::ClientObjectListClass::Vtable();
    fresh.head = nullptr;
    fresh.tail = nullptr;
    fresh.count = 0;
    fresh.free = nullptr;
    fresh.blocks = nullptr;
    fresh.blockSize = 10;
    CStream* header = &zoneHeader_;
    header->Clear();

    // The map objects that are not part of the map itself: also kept in sentObjects_.
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node)) {
        CMapObject* object = node->value;
        unsigned id = static_cast<unsigned>(object->id_);
        if (!mapObjects_.map.Find(id)) continue;
        if (object->IsSynchronised()) continue;
        sentObjects_.Set(static_cast<unsigned>(object->id_), object);
        auto* entry = fresh.NewNode(fresh.tail, nullptr);
        entry->data = object;
        if (fresh.tail) fresh.tail->next = entry;
        else fresh.head = entry;
        fresh.tail = entry;
    }

    // They stop being map objects and are sent to a temporary player, which is then dropped.
    CPlayer* temporary = players_->CreateSessionPlayer();
    CPlayerServer* state = temporary->Server();
    lib::AlignedArray<CEngineObject*> objects{nullptr, 0, 0, 0};
    for (auto* node = fresh.head; node;) {
        auto* next = node->next;
        CEngineObject* object = node->data;
        unsigned id = static_cast<unsigned>(object->id_);
        hash400::RemoveKey(mapObjects_.map, id);
        id = static_cast<unsigned>(object->id_);
        if (hash400::Find(otherMapObjects_.map, id)) {
            id = static_cast<unsigned>(object->id_);
            hash400::RemoveKey(otherMapObjects_.map, id);
        }
        objects.SetAtGrow(objects.size, object);
        node = next;
    }
    state->SendObjectsAdded(&objects);
    state->WriteTo(header);
    PlayerList()->ReleasePlayer(temporary);
    if (objects.data) lib::AlignedArray<CEngineObject*>::Free(objects.data);
    fresh.vtable = mfc::impl::ClientObjectListClass::Vtable();
    fresh.RemoveAll();
}

void CWorldServer::ResetState() {
    ResetZone();
    mapName_ = game::EmptyText();
    diplomacy_.Reset();
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    AIMap()->Reset();
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
    weatherFactor_ = 0x3F800000;   // 1.0f
    loaded_ = 0;
    startPosition_.y = 0.0f;
    lastRainBoltTick_ = 0;
    startPosition_.z = 0.0f;
}

EI_REWRITTEN(0x0058B600, CWorldServer::ResetState);
EI_REWRITTEN(0x00595C70, CWorldServer::LoadMap);
EI_REWRITTEN(0x005957E0, CWorldServer::TeardownZone);

void CWorldServer::EnsureZone(const char* map, const char* mob) {
    if (gamecrt::mbscmp(mapName_.c_str(), map) == 0 && gamecrt::mbscmp(mobName_.c_str(), mob) == 0) return;
    NewZone(map, mob, nullptr);
    TeardownZone();
}
EI_REWRITTEN(0x00595B50, CWorldServer::EnsureZone);
EI_REWRITTEN(0x005951A0, CWorldServer::SendFreshObjects);
EI_REWRITTEN(0x0058B250, CWorldServer::ResetZone);
