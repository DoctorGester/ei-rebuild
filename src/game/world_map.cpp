// CWorldServer::LoadMap (00595C70): loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md.
#include "world.h"
#include "game_strings.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "core_streams.h"
#include "map_object.h"
#include "lib/containers.h"
#include "originals_gameplay.h"
#include "originals_script.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_core.h"

namespace {


// 005B5E10 / 005B8B30. CAIMap: forget the map / start collecting refreshes.
__declspec(naked) void __fastcall AIMapReset(void*, void*) EI_ORIGINAL(0x005B5E10)
__declspec(naked) void __fastcall AIMapBeginRefresh(void*, void*) EI_ORIGINAL(0x005B8B30)
// 005B43C0 (RET 4). CAIMap: loads the terrain map file; 0 on failure. Patched by Rebirth.
__declspec(naked) int __fastcall AIMapLoad(void*, void*, const char* /*path*/) EI_ORIGINAL(0x005B43C0)
// 00595BA0 (RET 8). The full path of a map file (CString constructed into `out`). Patched by Rebirth.
char** __fastcall ServerMapPath(CWorldServer* a0, void*, char** a2, const char* a3) { return (char**)ServerMapPathOriginal((void*)a0, nullptr, (void*)a2, a3); }
// 005B1180 (RET 4). The AI layer (+0x2468) reads its state from the graph stream (AL).
__declspec(naked) bool __fastcall AILayerLoad(void*, void*, CStream* /*stream*/) EI_ORIGINAL(0x005B1180)
// 004BE6E0. A script command queue's reset.
// 004554B0. CDiplomacy::Reset (object_diplomacy.cpp).
__declspec(naked) void __fastcall DiplomacyReset(void*, void*) EI_ORIGINAL(0x004554B0)
// 0059D7A0. The timed-object map's clear (nodes freed, buckets kept).
__declspec(naked) void __fastcall TimedMapClear(void*, void*) EI_ORIGINAL(0x0059D7A0)
// 0040A3A0. CRefObject::Release (the missiles).
void __fastcall RefRelease(void* a0, void*) { RefObjectRelease(a0); }
// Rewritten elsewhere, through their addresses: ClearMapObjects / LoadMapObjects (world_zone).
__declspec(naked) void __fastcall ClearMapObjectsAt(CWorldServer*, void*) EI_ORIGINAL(0x00596C60)
__declspec(naked) int __fastcall LoadMapObjectsAt(CWorldServer*, void*, const char*, int) EI_ORIGINAL(0x00596030)
// 0058B250. ResetZone (rewritten below), through its address.
__declspec(naked) void __fastcall ResetZoneAt(CWorldServer*, void*) EI_ORIGINAL(0x0058B250)
// 005951A0. A fresh zone's start (players placed, scripts started).
__declspec(naked) void __fastcall StartFreshZone(CWorldServer*, void*) EI_ORIGINAL(0x005951A0)

// The timed objects are ended (slot 5, ClearState) and released, then the map is emptied (the
// walk stops at the first empty entry).
void EndTimedObjects(CWorldServer* world) {
    auto& map = world->timedObjects_.map;
    for (auto* node = map.First(); node && node->value; node = map.After(node)) {
        CEngineObject* object = node->value;
        object->ClearState();
        object->Release();
    }
    TimedMapClear(&world->timedObjects_, nullptr);
    world->timedObjects_.maxId = 0;
}

void ClearMissiles(CWorldServer* world) {
    auto& list = world->projectiles_;
    for (auto* node = list.head; node; node = node->next)
        if (node->data) RefRelease(node->data, nullptr);
    list.RemoveAll();
}

} // namespace

int CWorldServer::LoadMap(const char* map, const char* mob, CStream* graph, char fromSave) {
    void* aiMap = aiMap_;
    if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(mapName_), reinterpret_cast<const unsigned char*>(map)) != 0) {
        AIMapReset(aiMap, nullptr);
        AIMapBeginRefresh(aiMap, nullptr);
        bool failed;
        {
            char* path = nullptr;
            ServerMapPath(this, nullptr, &path, map);
            failed = AIMapLoad(aiMap, nullptr, path) == 0;
            reinterpret_cast<mfc::CString*>(&path)->~CString();
        }
        if (failed) return 0;
        *reinterpret_cast<mfc::CString*>(&mapName_) = map;
        *reinterpret_cast<mfc::CString*>(&mobName_) = game::EmptyText();
    }
    graph->Seek(0);
    if (graph->Size() != 0 && !AILayerLoad(aiLayer_, nullptr, graph)) return 0;

    if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(mobName_), reinterpret_cast<const unsigned char*>(mob)) == 0) {
        // The same zone again: start over.
        QueueReset(commandsA_, nullptr);
        QueueReset(commandsB_, nullptr);
        DiplomacyReset(diplomacy_, nullptr);
        ClearMapObjectsAt(this, nullptr);
        ClearMissiles(this);
        EndTimedObjects(this);
        keepsState_ = 0;
        loadedFresh_ = fromSave == 0;
        occluderReplanTime_ = 0;
    } else {
        ResetZoneAt(this, nullptr);
        keepsState_ = 1;
        loadedFresh_ = fromSave == 0;
    }

    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty = 0xFFFFFFFFu;
    if (it.list) --it.list->refCount_;
    unknown15C_ = 1;
    unknownE4_ = 0;
    unknownE8_ = 0.0f;
    it.list = nullptr;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) player->dirty |= 2;
    if (it.list) {
        --it.list->refCount_;
        it.list = nullptr;
    }

    if (keepsState_ || loadedFresh_) {
        *reinterpret_cast<mfc::CString*>(&mobName_) = mob;
        int loaded = LoadMapObjectsAt(this, nullptr, mob, loadedFresh_ == 0);
        if (!loaded) return loaded;
        if (!loadedFresh_) {
            QueueReset(commandsA_, nullptr);
            DiplomacyReset(diplomacy_, nullptr);
        }
        if (keepsState_) StartFreshZone(this, nullptr);
    }
    if (fromSave) {
        ClearMapObjectsAt(this, nullptr);
        EndTimedObjects(this);
    }
    loadedFresh_ = 1;
    keepsState_ = 1;
    return 1;
}

// ---- ResetZone --------------------------------------------------------------------------------

namespace {

// 0059DDE0 (RET 4) / 0059DD90 (RET 4). Destructors of a shared-object element and of a missile's
// counted pointer (flags 0: in place).
__declspec(naked) void* __fastcall SharedObjectDestruct(void*, void*, unsigned) EI_ORIGINAL(0x0059DDE0)
__declspec(naked) void* __fastcall MissilePtrDestruct(void*, void*, unsigned) EI_ORIGINAL(0x0059DD90)
// 0059C890 (__stdcall). Releases a range of counted object pointers (slot 4).
void __stdcall CountedRangeRelease(void* a0, int a1) { CountedRangeReleaseOriginal(a0, a1); }
__declspec(naked) void __fastcall CountdownsClear(void*, void*) EI_ORIGINAL(0x006D43E0)
void __fastcall PlayerClear(WorldPlayer* a0, void*) { PlayerClearOriginal((void*)a0); }
// 0059D6D0. The other id maps' clear.
__declspec(naked) void __fastcall IdMapClear(void*, void*) EI_ORIGINAL(0x0059D6D0)
// 00404410. A manager's clear.
__declspec(naked) void __fastcall ManagerClear(CEngineObject*, void*) EI_ORIGINAL(0x00404410)

void ClearMissilePointers(CWorldServer* world) {
    auto& list = world->projectiles_;
    for (auto* node = list.head; node; node = node->next) MissilePtrDestruct(&node->data, nullptr, 0);
    list.RemoveAll();   // the elements were destroyed above; this frees the blocks
}

} // namespace

void CWorldServer::ResetZone() {
    SharedObjectList& shared = sharedObjects_;
    for (auto* node = shared.head; node; node = node->next) SharedObjectDestruct(&node->data, nullptr, 0);
    shared.RemoveAll();
    StringArraySetSize(&mobFiles_, nullptr, 0, -1);
    CountdownsClear(countdowns_, nullptr);
    ZoneExitsSetSizeOriginal(&exitsVtable_, nullptr, 0, -1);
    QueueReset(commandsA_, nullptr);
    QueueReset(commandsB_, nullptr);
    ClearMissilePointers(this);
    for (auto* node = counted_.head; node; node = node->next) CountedRangeRelease(&node->data, 1);
    counted_.RemoveAll();

    if (PlayerList()) {
        auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
        if (it) it->list = nullptr;
        for (WorldPlayer* player = it->First(PlayerList()); player; player = it->Next())
            PlayerClear(player, nullptr);
        if (it) {
            if (it->list) {
                --it->list->refCount_;
                it->list = nullptr;
            }
            gamecrt::operator_delete(it);
        }
    }

    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node))
        node->value->ClearState();
    ObjectMapClearOriginal(&objects_);
    ObjectIdMap* const maps[] = {&sentObjects_, &mapObjects_, &otherMapObjects_, &updateMap_, &namedObjects_};
    for (ObjectIdMap* map : maps) {
        IdMapClear(map, nullptr);
        map->maxId = 0;
    }
    ClearMissilePointers(this);
    EndTimedObjects(this);
    *reinterpret_cast<mfc::CString*>(&mobName_) = game::EmptyText();
    *reinterpret_cast<mfc::CString*>(&zoneName_) = game::EmptyText();
    if (manager3C_) ManagerClear(manager3C_, nullptr);
    if (manager40_) ManagerClear(manager40_, nullptr);
    if (manager44_) ManagerClear(manager44_, nullptr);
}

// ---- TeardownZone -----------------------------------------------------------------------------

namespace {

void __fastcall PlayerDisconnect(WorldPlayer* a0, void*) { PlayerDisconnectOriginal((void*)a0); }
// 0059A100. The shared-object element's object array destructor.
void __fastcall ObjectPtrArrayDestruct(void* a0, void*) { CountedPtrArrayDestructOriginal(a0); }

} // namespace

void CWorldServer::TeardownZone() {
    // Every player leaves (the list is walked from its start again after each one).
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player;
         player = it.First(PlayerList())) {
        PlayerDisconnect(player, nullptr);
        PlayerListRelease(PlayerList(), nullptr, player);
        if (it.list) --it.list->refCount_;
        it.list = nullptr;
    }
    if (it.list) --it.list->refCount_;
    it.list = nullptr;
    unknown15C_ = 0;

    SharedObjectList& shared = sharedObjects_;
    for (auto* node = shared.head; node; node = node->next)
        ObjectPtrArrayDestruct(&node->data.objects, nullptr);
    shared.RemoveAll();
    StringArraySetSize(&mobFiles_, nullptr, 0, -1);
    aiMap_[2] = 0;   // CAIMap +0x172: refreshes off
    QueueReset(commandsA_, nullptr);
    QueueReset(commandsB_, nullptr);
    DiplomacyReset(diplomacy_, nullptr);
    ClearMapObjectsAt(this, nullptr);

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
    unknown12D64_ = 0;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

namespace {

mfc::CTypedPtrList<CEngineObject>::Node* __fastcall ObjectListNewNode(
    mfc::CTypedPtrList<CEngineObject>* a0, void*, mfc::CTypedPtrList<CEngineObject>::Node* a2,
    mfc::CTypedPtrList<CEngineObject>::Node* a3) {
    return (mfc::CTypedPtrList<CEngineObject>::Node*)PtrListNewNode573290Original((void*)a0, nullptr, (void*)a2, (void*)a3);
}
__declspec(naked) void __fastcall ObjectListRemoveAll(mfc::CTypedPtrList<CEngineObject>*, void*) EI_ORIGINAL(0x00570470)
// 00599A80 (RET 4) / 00599AE0 (RET 4). An id map's erase(key) / lookup (pointer to the value or
// null).
__declspec(naked) CEngineObject** __fastcall IdMapLookup(ObjectIdMap*, void*, const unsigned*) EI_ORIGINAL(0x00599AE0)
// 0059CCE0 (RET 8). The aligned object array's SetAtGrow.
__declspec(naked) void __fastcall AlignedArraySetAtGrow(lib::AlignedArray<CEngineObject*>*, void*, int,
                                                         CEngineObject*) EI_ORIGINAL(0x0059CCE0)
// 0066C180. The player list creates a temporary player (returned).
WorldPlayer* __fastcall PlayerListCreateTemporary(CEngineObject* a0, void*) { return (WorldPlayer*)PlayerCreateOriginal((void*)a0); }
// 0066D7F0 (RET 4) / 0066D230 (RET 4). The player state: objects added / the zone header stream.
void __fastcall StateObjectsAdded(void* a0, void*, void* a2) { StateObjectsAddedOriginal(a0, nullptr, a2); }
__declspec(naked) void __fastcall StateSendHeader(void*, void*, void* /*stream*/) EI_ORIGINAL(0x0066D230)

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
        auto* entry = ObjectListNewNode(&fresh, nullptr, fresh.tail, nullptr);
        entry->data = object;
        if (fresh.tail) fresh.tail->next = entry;
        else fresh.head = entry;
        fresh.tail = entry;
    }

    // They stop being map objects and are sent to a temporary player, which is then dropped.
    WorldPlayer* temporary = PlayerListCreateTemporary(players_, nullptr);
    CPlayerServer* state = temporary->server;
    lib::AlignedArray<CEngineObject*> objects{nullptr, 0, 0, 0};
    for (auto* node = fresh.head; node;) {
        auto* next = node->next;
        CEngineObject* object = node->data;
        unsigned id = static_cast<unsigned>(object->id_);
        IdMapEraseOriginal(&mapObjects_, nullptr, &id);
        id = static_cast<unsigned>(object->id_);
        if (IdMapLookup(&otherMapObjects_, nullptr, &id)) {
            id = static_cast<unsigned>(object->id_);
            IdMapEraseOriginal(&otherMapObjects_, nullptr, &id);
        }
        AlignedArraySetAtGrow(&objects, nullptr, objects.size, object);
        node = next;
    }
    StateObjectsAdded(state, nullptr, &objects);
    StateSendHeader(state, nullptr, header);
    PlayerListRelease(PlayerList(), nullptr, temporary);
    if (objects.data) lib::AlignedArray<CEngineObject*>::Free(objects.data);
    fresh.vtable = ObjectPtrListVtable();
    ObjectListRemoveAll(&fresh, nullptr);
}

void CWorldServer::ResetState() {
    ResetZoneAt(this, nullptr);
    *reinterpret_cast<mfc::CString*>(&mapName_) = game::EmptyText();
    DiplomacyReset(diplomacy_, nullptr);
    QueueReset(commandsA_, nullptr);
    QueueReset(commandsB_, nullptr);
    AIMapReset(aiMap_, nullptr);
    unknown15C_ = 0;
    unknown84_ = 0;
    for (WorldAlarm& alarm : alarms_) {
        alarm.active = 0;
        alarm.time = 0;
        alarm.y = 0.0f;
        alarm.x = 0.0f;
    }
    unknown12D64_ = 0;
    startPosition_[0] = 0.0f;
    unknown88_ = 0x3F800000;   // 1.0f
    loaded_ = 0;
    startPosition_[1] = 0.0f;
    unknown13C_ = 0;
    startPosition_[2] = 0.0f;
}

EI_REPLACE_METHOD(0x0058B600, CWorldServer::ResetState);
EI_REPLACE_METHOD(0x00595C70, CWorldServer::LoadMap);
EI_REPLACE_METHOD(0x005957E0, CWorldServer::TeardownZone);
EI_REPLACE_METHOD(0x005951A0, CWorldServer::SendFreshObjects);
EI_REPLACE_METHOD(0x0058B250, CWorldServer::ResetZone);
