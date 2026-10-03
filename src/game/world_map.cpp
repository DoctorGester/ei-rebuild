// CWorldServer::LoadMap (00595C70): loads a zone's map and objects (from NewZone, LoadSave,
// EnterZone). See world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

const char* EmptyText() { return reinterpret_cast<const char*>(0x0079B298); }

// 005B5E10 / 005B8B30. CAIMap: forget the map / start collecting refreshes.
__declspec(naked) void __fastcall AIMapReset(void*, void*) EI_ORIGINAL(0x005B5E10)
__declspec(naked) void __fastcall AIMapBeginRefresh(void*, void*) EI_ORIGINAL(0x005B8B30)
// 005B43C0 (RET 4). CAIMap: loads the terrain map file; 0 on failure. Patched by Rebirth.
__declspec(naked) int __fastcall AIMapLoad(void*, void*, const char* /*path*/) EI_ORIGINAL(0x005B43C0)
// 00595BA0 (RET 8). The full path of a map file (CString constructed into `out`). Patched by Rebirth.
__declspec(naked) char** __fastcall ServerMapPath(CWorldServer*, void*, char** /*out*/, const char*) EI_ORIGINAL(0x00595BA0)
// 005B1180 (RET 4). The AI layer (+0x2468) reads its state from the graph stream (AL).
__declspec(naked) bool __fastcall AILayerLoad(void*, void*, void* /*stream*/) EI_ORIGINAL(0x005B1180)
// 004BE6E0. A script command queue's reset.
__declspec(naked) void __fastcall CommandQueueReset(void*, void*) EI_ORIGINAL(0x004BE6E0)
// 004554B0. CDiplomacy::Reset (object_diplomacy.cpp).
__declspec(naked) void __fastcall DiplomacyReset(void*, void*) EI_ORIGINAL(0x004554B0)
// 0059D7A0. The timed-object map's clear (nodes freed, buckets kept).
__declspec(naked) void __fastcall TimedMapClear(void*, void*) EI_ORIGINAL(0x0059D7A0)
// 0040A3A0. CRefObject::Release (the missiles).
__declspec(naked) void __fastcall RefRelease(void*, void*) EI_ORIGINAL(0x0040A3A0)
// Rewritten elsewhere, through their addresses: ClearMapObjects / LoadMapObjects (world_zone).
__declspec(naked) void __fastcall ClearMapObjectsAt(CWorldServer*, void*) EI_ORIGINAL(0x00596C60)
__declspec(naked) int __fastcall LoadMapObjectsAt(CWorldServer*, void*, const char*, int) EI_ORIGINAL(0x00596030)
// 0058B250. Saves the current zone's state (saves\current) before another .mob is loaded.
__declspec(naked) void __fastcall SaveZoneState(CWorldServer*, void*) EI_ORIGINAL(0x0058B250)
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
        *reinterpret_cast<mfc::CString*>(&mobName_) = EmptyText();
    }
    ei::VCall<void>(graph, kSlotStreamSeek, 0);
    if (ei::VCall<unsigned>(graph, kSlotStreamSize) != 0 &&
        !AILayerLoad(reinterpret_cast<std::uint8_t*>(this) + 0x2468, nullptr, graph))
        return 0;

    std::uint8_t* raw = reinterpret_cast<std::uint8_t*>(this);
    if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(mobName_), reinterpret_cast<const unsigned char*>(mob)) == 0) {
        // The same zone again: start over.
        CommandQueueReset(commandsA_, nullptr);
        CommandQueueReset(commandsB_, nullptr);
        DiplomacyReset(diplomacy_, nullptr);
        ClearMapObjectsAt(this, nullptr);
        ClearMissiles(this);
        EndTimedObjects(this);
        *KeepsState(this) = false;
        *LoadedFresh(this) = fromSave == 0;
        *reinterpret_cast<int*>(raw + 0x2804) = 0;
    } else {
        SaveZoneState(this, nullptr);
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
            CommandQueueReset(commandsA_, nullptr);
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
__declspec(naked) void __fastcall StringArraySetSize(void*, void*, int, int) EI_ORIGINAL(0x006FE9B8)
__declspec(naked) void __fastcall CountdownsClear(void*, void*) EI_ORIGINAL(0x006D43E0)
__declspec(naked) void __fastcall PlayerClear(WorldPlayer*, void*) EI_ORIGINAL(0x00665200)
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
    CommandQueueReset(commandsA_, nullptr);
    CommandQueueReset(commandsB_, nullptr);
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
    *reinterpret_cast<mfc::CString*>(&mobName_) = EmptyText();
    *reinterpret_cast<mfc::CString*>(&zoneName_) = EmptyText();
    if (manager3C_) ManagerClear(manager3C_, nullptr);
    if (manager40_) ManagerClear(manager40_, nullptr);
    if (manager44_) ManagerClear(manager44_, nullptr);
}

EI_REPLACE_METHOD(0x00595C70, CWorldServer::LoadMap);
EI_REPLACE_METHOD(0x0058B250, CWorldServer::ResetZone);
