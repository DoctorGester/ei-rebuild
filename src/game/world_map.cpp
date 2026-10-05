// CWorldServer::LoadMap (00595C70): loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md (seventh round).
#include "world.h"
#include "game_strings.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
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
__declspec(naked) bool __fastcall AILayerLoad(void*, void*, void* /*stream*/) EI_ORIGINAL(0x005B1180)
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

constexpr int kSlotStreamSeek = 3;
constexpr int kSlotStreamSize = 4;
constexpr int kSlotTimedEnd = 5;   // a timed object's slot 5 (+0x14): end it

// The timed objects are ended (slot 5) and released, then the map is emptied (the walk stops at
// the first empty entry).
void EndTimedObjects(CWorldServer* world) {
    auto& map = world->timedObjects_.map;
    for (auto* node = map.First(); node && node->value; node = map.After(node)) {
        CEngineObject* object = node->value;
        ei::VCall<void>(object, kSlotTimedEnd);
        object->Release();
    }
    TimedMapClear(&world->timedObjects_, nullptr);
    world->timedObjects_.maxId = 0;
}

void ClearMissiles(CWorldServer* world) {
    auto& list = world->projectiles_.list;
    for (auto* node = list.head; node; node = node->next)
        if (node->data) RefRelease(node->data, nullptr);
    list.RemoveAll();
}

bool* LoadedFresh(CWorldServer* world) { return reinterpret_cast<bool*>(reinterpret_cast<std::uint8_t*>(world) + 0x11D); }
bool* KeepsState(CWorldServer* world) { return reinterpret_cast<bool*>(reinterpret_cast<std::uint8_t*>(world) + 0x11E); }

} // namespace

int CWorldServer::LoadMap(const char* map, const char* mob, void* graph, char fromSave) {
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
    ei::VCall<void>(graph, kSlotStreamSeek, 0);
    if (ei::VCall<unsigned>(graph, kSlotStreamSize) != 0 &&
        !AILayerLoad(reinterpret_cast<std::uint8_t*>(this) + 0x2468, nullptr, graph))
        return 0;

    std::uint8_t* raw = reinterpret_cast<std::uint8_t*>(this);
    if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(mobName_), reinterpret_cast<const unsigned char*>(mob)) == 0) {
        // The same zone again: start over.
        QueueReset(commandsA_, nullptr);
        QueueReset(commandsB_, nullptr);
        DiplomacyReset(diplomacy_, nullptr);
        ClearMapObjectsAt(this, nullptr);
        ClearMissiles(this);
        EndTimedObjects(this);
        *KeepsState(this) = false;
        *LoadedFresh(this) = fromSave == 0;
        *reinterpret_cast<int*>(raw + 0x2804) = 0;
    } else {
        ResetZoneAt(this, nullptr);
        *KeepsState(this) = true;
        *LoadedFresh(this) = fromSave == 0;
    }

    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next())
        player->dirty = 0xFFFFFFFFu;
    if (it.list) --it.list->refCount_;
    unknown15C_ = 1;
    unknownE4_ = 0;
    unknownE8_ = 0.0f;
    it.list = nullptr;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next()) player->dirty |= 2;
    if (it.list) {
        --it.list->refCount_;
        it.list = nullptr;
    }

    if (*KeepsState(this) || *LoadedFresh(this)) {
        *reinterpret_cast<mfc::CString*>(&mobName_) = mob;
        int loaded = LoadMapObjectsAt(this, nullptr, mob, *LoadedFresh(this) == 0);
        if (!loaded) return loaded;
        if (!*LoadedFresh(this)) {
            QueueReset(commandsA_, nullptr);
            DiplomacyReset(diplomacy_, nullptr);
        }
        if (*KeepsState(this)) StartFreshZone(this, nullptr);
    }
    if (fromSave) {
        ClearMapObjectsAt(this, nullptr);
        EndTimedObjects(this);
    }
    *LoadedFresh(this) = true;
    *KeepsState(this) = true;
    return 1;
}

// ---- ResetZone --------------------------------------------------------------------------------

namespace {

// 0059DDE0 (RET 4) / 0059DD90 (RET 4). Destructors of a shared-object element and of a missile's
// counted pointer (flags 0: in place).
__declspec(naked) void* __fastcall SharedObjectDestruct(void*, void*, unsigned) EI_ORIGINAL(0x0059DDE0)
__declspec(naked) void* __fastcall MissilePtrDestruct(void*, void*, unsigned) EI_ORIGINAL(0x0059DD90)
// 0059C890 (__stdcall). Releases a range of counted object pointers (slot 4).
__declspec(naked) void __stdcall CountedRangeRelease(void*, int) EI_ORIGINAL(0x0059C890)
__declspec(naked) void __fastcall CountdownsClear(void*, void*) EI_ORIGINAL(0x006D43E0)
void __fastcall PlayerClear(WorldPlayer* a0, void*) { PlayerClearOriginal((void*)a0); }
// 0047B670. The object map's clear (every object id hash map; nodes freed).
__declspec(naked) void __fastcall ObjectMapClear(void*, void*) EI_ORIGINAL(0x0047B670)
// 0059D6D0. The other id maps' clear.
__declspec(naked) void __fastcall IdMapClear(void*, void*) EI_ORIGINAL(0x0059D6D0)
// 00404410. A manager's clear.
__declspec(naked) void __fastcall ManagerClear(CEngineObject*, void*) EI_ORIGINAL(0x00404410)

constexpr int kSlotObjectEnd = 5;

template <typename T>
void FreeListBlocks(mfc::CTypedPtrList<T>& list) {
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(list.blocks);
    list.blocks = nullptr;
}

void ClearMissilePointers(CWorldServer* world) {
    auto& list = world->projectiles_.list;
    for (auto* node = list.head; node; node = node->next) MissilePtrDestruct(&node->data, nullptr, 0);
    FreeListBlocks(list);
}

} // namespace

void CWorldServer::ResetZone() {
    auto& shared = *reinterpret_cast<mfc::CTypedPtrList<void>*>(reinterpret_cast<std::uint8_t*>(this) + 0x12E60);
    for (auto* node = shared.head; node; node = node->next) SharedObjectDestruct(&node->data, nullptr, 0);
    FreeListBlocks(shared);
    StringArraySetSize(&mobFiles_, nullptr, 0, -1);
    CountdownsClear(countdowns_, nullptr);
    reinterpret_cast<ZoneExitArray*>(&exitsVtable_)->SetSize(0, -1);
    QueueReset(commandsA_, nullptr);
    QueueReset(commandsB_, nullptr);
    ClearMissilePointers(this);
    for (auto* node = counted_.head; node; node = node->next) CountedRangeRelease(&node->data, 1);
    FreeListBlocks(counted_);

    if (ei::VCall<CEngineObject*>(this, 20)) {
        auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
        if (it) it->list = nullptr;
        for (WorldPlayer* player = it->First(ei::VCall<CEngineObject*>(this, 20)); player; player = it->Next())
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
        ei::VCall<void>(node->value, kSlotObjectEnd);
    ObjectMapClear(&objects_, nullptr);
    ObjectIdMap* const maps[] = {reinterpret_cast<ObjectIdMap*>(reinterpret_cast<std::uint8_t*>(this) + 0x12D88),
                                 &mapObjects_, &otherMapObjects_, &updateMap_, &namedObjects_};
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
__declspec(naked) void __fastcall ObjectPtrArrayDestruct(void*, void*) EI_ORIGINAL(0x0059A100)
// 0055F890. CWorld's own teardown (base class).
__declspec(naked) void __fastcall WorldBaseTeardown(CWorldServer*, void*) EI_ORIGINAL(0x0055F890)

} // namespace

void CWorldServer::TeardownZone() {
    // Every player leaves (the list is walked from its start again after each one).
    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player;
         player = it.First(ei::VCall<CEngineObject*>(this, 20))) {
        PlayerDisconnect(player, nullptr);
        PlayerListRelease(ei::VCall<CEngineObject*>(this, 20), nullptr, player);
        if (it.list) --it.list->refCount_;
        it.list = nullptr;
    }
    if (it.list) --it.list->refCount_;
    it.list = nullptr;
    unknown15C_ = 0;

    auto& shared = *reinterpret_cast<mfc::CTypedPtrList<void>*>(reinterpret_cast<std::uint8_t*>(this) + 0x12E60);
    for (auto* node = shared.head; node; node = node->next)
        ObjectPtrArrayDestruct(reinterpret_cast<std::uint8_t*>(node) + 0x14, nullptr);
    shared.count = 0;
    shared.free = nullptr;
    shared.tail = nullptr;
    shared.head = nullptr;
    mfc::PlexFreeDataChain(shared.blocks);
    shared.blocks = nullptr;
    StringArraySetSize(&mobFiles_, nullptr, 0, -1);
    aiMap_[2] = 0;   // CAIMap +0x172: refreshes off
    QueueReset(commandsA_, nullptr);
    QueueReset(commandsB_, nullptr);
    DiplomacyReset(diplomacy_, nullptr);
    ClearMapObjectsAt(this, nullptr);

    // The missiles' counted pointers, released inline (16-bit count at +4, slot 0 deletes).
    auto& missiles = projectiles_.list;
    for (auto* node = missiles.head; node; node = node->next) {
        auto* missile = reinterpret_cast<std::uint8_t*>(node->data);
        if (missile && --*reinterpret_cast<std::int16_t*>(missile + 4) == 0 && missile)
            ei::VCall<void>(missile, 0, 1u);
    }
    missiles.count = 0;
    missiles.free = nullptr;
    missiles.tail = nullptr;
    missiles.head = nullptr;
    mfc::PlexFreeDataChain(missiles.blocks);
    missiles.blocks = nullptr;

    EndTimedObjects(this);
    WorldBaseTeardown(this, nullptr);
    gameResult_ = 0;
    unknown12D64_ = 0;
}

// ---- SendFreshObjects -------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kObjectListVtable = 0x0073F530;

__declspec(naked) mfc::CTypedPtrList<CEngineObject>::Node* __fastcall ObjectListNewNode(
    mfc::CTypedPtrList<CEngineObject>*, void*, mfc::CTypedPtrList<CEngineObject>::Node*,
    mfc::CTypedPtrList<CEngineObject>::Node*) EI_ORIGINAL(0x00573290)
__declspec(naked) void __fastcall ObjectListRemoveAll(mfc::CTypedPtrList<CEngineObject>*, void*) EI_ORIGINAL(0x00570470)
// 00599A80 (RET 4) / 00599AE0 (RET 4). An id map's erase(key) / lookup (pointer to the value or
// null).
__declspec(naked) CEngineObject** __fastcall IdMapLookup(ObjectIdMap*, void*, const unsigned*) EI_ORIGINAL(0x00599AE0)
// 0059CCE0 (RET 8). The aligned object array's SetAtGrow.
__declspec(naked) void __fastcall AlignedArraySetAtGrow(void*, void*, int, CEngineObject*) EI_ORIGINAL(0x0059CCE0)
// 0066C180. The player list creates a temporary player (returned).
WorldPlayer* __fastcall PlayerListCreateTemporary(CEngineObject* a0, void*) { return (WorldPlayer*)PlayerCreateOriginal((void*)a0); }
// 0066D7F0 (RET 4) / 0066D230 (RET 4). The player state: objects added / the zone header stream.
__declspec(naked) void __fastcall StateObjectsAdded(void*, void*, void* /*objects*/) EI_ORIGINAL(0x0066D7F0)
__declspec(naked) void __fastcall StateSendHeader(void*, void*, void* /*stream*/) EI_ORIGINAL(0x0066D230)

constexpr int kSlotObjectStatic = 29;   // object slot 29 (+0x74, AL): part of the map itself
constexpr int kSlotHeaderReset = 6;     // the header object at +0x12E00: slot 6 (+0x18)

struct AlignedObjects {
    CEngineObject** data;
    int size;
    int capacity;
    int growBy;
};

} // namespace

void CWorldServer::SendFreshObjects() {
    mfc::CTypedPtrList<CEngineObject> fresh;
    fresh.vtable = reinterpret_cast<void*>(kObjectListVtable);
    fresh.head = nullptr;
    fresh.tail = nullptr;
    fresh.count = 0;
    fresh.free = nullptr;
    fresh.blocks = nullptr;
    fresh.blockSize = 10;
    void* header = reinterpret_cast<std::uint8_t*>(this) + 0x12E00;
    ei::VCall<void>(header, kSlotHeaderReset);

    // The map objects that are not part of the map itself (slot 29): also kept in +0x12D88.
    auto& created = *reinterpret_cast<ObjectIdMap*>(reinterpret_cast<std::uint8_t*>(this) + 0x12D88);
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node)) {
        CEngineObject* object = node->value;
        unsigned id = static_cast<unsigned>(object->id_);
        if (!mapObjects_.map.Find(id)) continue;
        if (ei::VCall<bool>(object, kSlotObjectStatic)) continue;
        created.Set(static_cast<unsigned>(object->id_), object);
        auto* entry = ObjectListNewNode(&fresh, nullptr, fresh.tail, nullptr);
        entry->data = object;
        if (fresh.tail) fresh.tail->next = entry;
        else fresh.head = entry;
        fresh.tail = entry;
    }

    // They stop being map objects and are sent to a temporary player, which is then dropped.
    WorldPlayer* temporary = PlayerListCreateTemporary(players_, nullptr);
    void* state = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(temporary) + 0x20);
    AlignedObjects objects{nullptr, 0, 0, 0};
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
    PlayerListRelease(ei::VCall<CEngineObject*>(this, 20), nullptr, temporary);
    if (objects.data) gamecrt::operator_delete(reinterpret_cast<void**>(objects.data)[-1]);
    fresh.vtable = reinterpret_cast<void*>(kObjectListVtable);
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
