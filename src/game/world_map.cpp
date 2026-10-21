// CWorldServer::LoadMap (00595C70): loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "game_lists.h"
#include "base_manager.h"
#include "hash_tables_400.h"
#include "object_diplomacy.h"
#include "player_manager.h"
#include "player_messages.h"
#include "world.h"
#include "model.h"   // TweenArray (countdowns_)
#include "game_strings.h"
#include "game_arrays.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "core_streams.h"
#include "map_object.h"
#include "lib/containers.h"
#include "lib/mfc_array.h"
#include "originals_gameplay.h"
#include "originals_script.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_core.h"
#include "originals_ai.h"

namespace {


// 005B43C0 (RET 4). CAIMap: loads the terrain map file; 0 on failure. Patched by Rebirth.
__declspec(naked) int __fastcall AIMapLoad(void*, void*, const char* /*path*/) EI_ORIGINAL(0x005B43C0)
// 00595BA0 (RET 8). The full path of a map file (CString constructed into `out`). Patched by Rebirth.
char** __fastcall ServerMapPath(CWorldServer* a0, void*, char** a2, const char* a3) { return (char**)ServerMapPathOriginal((void*)a0, nullptr, (void*)a2, a3); }
// 0040A3A0. CRefObject::Release (the missiles).

// The timed objects are ended (slot 5, ClearState) and released, then the map is emptied (the
// walk stops at the first empty entry).
void EndTimedObjects(CWorldServer* world) {
    auto& map = world->timedObjects_.map;
    for (auto* node = map.First(); node && node->value; node = map.After(node)) {
        CEngineObject* object = node->value;
        object->ClearState();
        object->Release();
    }
    world->timedObjects_.map.Clear();   // 0059D7A0: nodes freed, buckets kept
    world->timedObjects_.maxId = 0;
}

void ClearMissiles(CWorldServer* world) {
    auto& list = world->projectiles_;
    for (auto* node = list.head; node; node = node->next)
        if (node->data) RefObjectRelease(node->data);
    list.RemoveAll();
}

} // namespace

int CWorldServer::LoadMap(const char* map, const char* mob, CStream* graph, char fromSave) {
    void* aiMap = aiMap_;
    if (gamecrt::mbsicmp(mapName_, map) != 0) {
        ResetAIMap(static_cast<CAIMap*>(aiMap));   // 005B5E10
        static_cast<CAIMap*>(aiMap)->BeginRefresh();
        bool failed;
        {
            char* path = nullptr;
            ServerMapPath(this, nullptr, &path, map);
            failed = AIMapLoad(aiMap, nullptr, path) == 0;
            reinterpret_cast<mfc::CString*>(&path)->~CString();
        }
        if (failed) return 0;
        mapName_ = map;
        mobName_ = game::EmptyText();
    }
    graph->Seek(0);
    if (graph->Size() != 0 && !reinterpret_cast<CRegionMap*>(aiLayer_)->LoadBlocks(graph)) return 0;

    if (gamecrt::mbsicmp(mobName_, mob) == 0) {
        // The same zone again: start over.
        ScriptReset(MainScript());
        ScriptReset(SecondScript());
        diplomacy_.Reset();
        ClearMapObjects();
        ClearMissiles(this);
        EndTimedObjects(this);
        keepsState_ = 0;
        loadedFresh_ = fromSave == 0;
        occluderReplanTime_ = 0;
    } else {
        ResetZone();
        keepsState_ = 1;
        loadedFresh_ = fromSave == 0;
    }

    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty = 0xFFFFFFFFu;
    if (it.manager) --it.manager->refCount_;
    zoneLoaded_ = 1;
    weather_ = 0;
    weatherStrength_ = 0.0f;
    it.manager = nullptr;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) player->dirty |= 2;
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

// 0059DD90 (RET 4) with flags 0: a missile's counted pointer released in place (RefPtrRelease).
// 0059C890 (__stdcall). Releases a range of counted object pointers (slot 4).
void __stdcall CountedRangeRelease(void* a0, int a1) { gamearrays::ObjectPtrs::DestructElements(static_cast<CEngineObject**>(a0), a1); }

void ClearMissilePointers(CWorldServer* world) {
    auto& list = world->projectiles_;
    for (auto* node = list.head; node; node = node->next) RefPtrRelease(reinterpret_cast<RefPtr*>(&node->data));
    list.RemoveAll();   // the elements were destroyed above; this frees the blocks
}

} // namespace

void CWorldServer::ResetZone() {
    SharedObjectList& shared = sharedObjects_;
    for (auto* node = shared.head; node; node = node->next) static_cast<SharedObject*>(&node->data)->DeletingDestruct(0);
    shared.RemoveAll();
    mobFiles_.SetSize(0, -1);
    countdowns_.RemoveAll();
    gamearrays::ExitsSetSize(&exitsVtable_, 0);
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ClearMissilePointers(this);
    for (auto* node = counted_.head; node; node = node->next) CountedRangeRelease(&node->data, 1);
    counted_.RemoveAll();

    if (PlayerList()) {
        auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
        if (it) it->manager = nullptr;
        for (WorldPlayer* player = it->First(PlayerList()); player; player = it->Next())
            PlayerClearOriginal((void*)player);
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
    ObjectMapClearOriginal(&objects_);
    ObjectIdMap* const maps[] = {&sentObjects_, &mapObjects_, &otherMapObjects_, &updateMap_, &namedObjects_};
    for (ObjectIdMap* map : maps) {
        map->map.Clear();   // 0059D6D0
        map->maxId = 0;
    }
    ClearMissilePointers(this);
    EndTimedObjects(this);
    mobName_ = game::EmptyText();
    zoneName_ = game::EmptyText();
    if (manager3C_) static_cast<CBaseManager*>(manager3C_)->FreeSpares();
    if (manager40_) static_cast<CBaseManager*>(manager40_)->FreeSpares();
    if (manager44_) static_cast<CBaseManager*>(manager44_)->FreeSpares();
}

// ---- TeardownZone -----------------------------------------------------------------------------

namespace {

} // namespace

void CWorldServer::TeardownZone() {
    // Every player leaves (the list is walked from its start again after each one).
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player;
         player = it.First(PlayerList())) {
        PlayerDisconnectOriginal((void*)player);
        static_cast<CPlayerManager*>(PlayerList())->ReleasePlayer(reinterpret_cast<CEngineObject*>(player));
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
    aiMap_[2] = 0;   // CAIMap +0x172: refreshes off
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    diplomacy_.Reset();
    ClearMapObjects();

    // The missiles' counted pointers, released inline (16-bit count, slot 0 deletes).
    auto& missiles = projectiles_;
    for (auto* node = missiles.head; node; node = node->next) {
        Projectile* missile = node->data;
        if (missile && --missile->referenceCount == 0 && missile) missile->DeletingDestructor(1);
    }
    missiles.RemoveAll();

    EndTimedObjects(this);
    CloseSession();
    gameResult_ = 0;
    sessionKind_ = 0;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

namespace {

// 0066D7F0 (RET 4) / 0066D230 (RET 4). The player state: objects added / the zone header stream.

} // namespace

void CWorldServer::SendFreshObjects() {
    mfc::CTypedPtrList<CEngineObject> fresh;
    fresh.vtable = ObjectPtrListVtable();
    fresh.head = nullptr;
    fresh.tail = nullptr;
    fresh.count = 0;
    fresh.free = nullptr;
    fresh.blocks = nullptr;
    fresh.blockSize = 10;
    CStream* header = &zoneHeader_;
    header->Clear();

    // The map objects that are not part of the map itself (slot 29): also kept in sentObjects_.
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node)) {
        CEngineObject* object = node->value;
        unsigned id = static_cast<unsigned>(object->id_);
        if (!mapObjects_.map.Find(id)) continue;
        if (static_cast<CMapObjectVirtuals*>(object)->IsSynchronised()) continue;
        sentObjects_.Set(static_cast<unsigned>(object->id_), object);
        auto* entry = fresh.NewNode(fresh.tail, nullptr);
        entry->data = object;
        if (fresh.tail) fresh.tail->next = entry;
        else fresh.head = entry;
        fresh.tail = entry;
    }

    // They stop being map objects and are sent to a temporary player, which is then dropped.
    WorldPlayer* temporary = (WorldPlayer*)PlayerCreateOriginal((void*)players_);
    CPlayerServer* state = temporary->server;
    lib::AlignedArray<CEngineObject*> objects{nullptr, 0, 0, 0};
    for (auto* node = fresh.head; node;) {
        auto* next = node->next;
        CEngineObject* object = node->data;
        unsigned id = static_cast<unsigned>(object->id_);
        hash400::ObjectsRemoveKey(mapObjects_.map, id);
        id = static_cast<unsigned>(object->id_);
        if (hash400::ObjectsFind(otherMapObjects_.map, id)) {
            id = static_cast<unsigned>(object->id_);
            hash400::ObjectsRemoveKey(otherMapObjects_.map, id);
        }
        objects.SetAtGrow(objects.size, object);
        node = next;
    }
    state->SendObjectsAdded(&objects);
    static_cast<CPlayerServer*>(state)->WriteTo(header);
    static_cast<CPlayerManager*>(PlayerList())->ReleasePlayer(reinterpret_cast<CEngineObject*>(temporary));
    if (objects.data) lib::AlignedArray<CEngineObject*>::Free(objects.data);
    fresh.vtable = ObjectPtrListVtable();
    fresh.RemoveAll();
}

void CWorldServer::ResetState() {
    ResetZone();
    mapName_ = game::EmptyText();
    diplomacy_.Reset();
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ResetAIMap(reinterpret_cast<CAIMap*>(aiMap_));
    zoneLoaded_ = 0;
    nightLevel_ = 0;
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

EI_REPLACE_METHOD(0x0058B600, CWorldServer::ResetState);
EI_REPLACE_METHOD(0x00595C70, CWorldServer::LoadMap);
EI_REPLACE_METHOD(0x005957E0, CWorldServer::TeardownZone);

void CWorldServer::EnsureZone(const char* map, const char* mob) {
    if (GameMbscmp(mapName_, map) == 0 && GameMbscmp(mobName_, mob) == 0) return;
    WorldNewZoneOriginal(this, nullptr, map, mob, nullptr);   // patched by Rebirth: stays the original
    TeardownZone();
}
EI_REPLACE_METHOD(0x00595B50, CWorldServer::EnsureZone);
EI_REPLACE_METHOD(0x005951A0, CWorldServer::SendFreshObjects);
EI_REPLACE_METHOD(0x0058B250, CWorldServer::ResetZone);
