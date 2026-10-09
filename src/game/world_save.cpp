// CWorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
// back, LoadSavedMapObjects restores the map objects SaveMapObjects wrote, and UpdatePlayerTimes
// brings the players' "gtime" up to date before a save. See world.h and docs/notes/world.md.
#include "world.h"

#include "ai_map.h"
#include "base_manager.h"
#include "core_streams.h"
#include "game_strings.h"
#include "item.h"
#include "map_object.h"
#include "net_delta.h"
#include "net_messages.h"
#include "net_session.h"
#include "object.h"
#include "script.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "originals_script.h"

#include <cstring>

namespace {

// ---- Constants --------------------------------------------------------------------------------

// kMemoryStreamVtable (0073BD90) and kStreamVtable (0073BDB0, its CStream base): core_streams.h.
// The CArray<int> vtable is mfc::DWordArrayVtable, the script group's scriptvtable::kRefs (set
// after 004C2030), the item link record's CItemLink::Vtable() (item.h).
constexpr std::uint32_t kStreamLimit = 2000000000;            // CStream +0x08 as constructed
constexpr int kTextKeyIdMin = 100000000;                      // ids above this are scripted (text-key) units
constexpr int kMissileSize = 0xA8;                            // operator new size of a Projectile

// Save format versions (WorldStream::version_) at which fields were added.
constexpr int kVersionTimeSpeedFloat = 0x4C;   // the time speed is a float (an int before)
constexpr int kVersionUnknown13C = 0x74;
constexpr int kVersionCountdowns = 0x4E;
constexpr int kVersionPlayerLists = 0x55;      // a player's id list (+0x114); the object records
constexpr int kVersionPlayerByte = 0x49;       // before: a byte that is read and dropped
constexpr int kVersionPlayerGroup = 0x46;      // before: a script group that is read and dropped
constexpr int kVersionMobFiles = 0x60;
constexpr int kVersionSharedObjects = 0x6E;
constexpr int kVersionMapObjectName = 0x59;

// Progress screen stages LoadSave shows (005DA400).
constexpr int kProgressStart = 0;
constexpr int kProgressZone = 1;
constexpr int kProgressManagers = 5;
constexpr int kProgressObjects = 6;
constexpr int kProgressDone = 7;

constexpr int kSharedObjectNodeSize = 0x28;

// ---- Local layouts ----------------------------------------------------------------------------

// MFC CArray<int> (vtable 0073C3D0; SetSize 0059C660, DwordArraySetSizeOriginal).
using IdArray = mfc::CArray<int>;

// MFC CArray of object pointers (SharedObject::objects, world.h).
using ObjectPtrArray = mfc::CArray<CEngineObject*>;

// The shared-object list is CWorldServer::sharedObjects_ (+0x12E60, world.h).
using SharedObjectNode = SharedObjectList::Node;
static_assert(sizeof(SharedObjectNode) == kSharedObjectNodeSize);

// A player's MFC list of plain dwords (WorldPlayer::ids, AddTail 0059CC60) and of objects
// (WorldPlayer::items, AddTail 004C98F0); only head and the node layout are read here.
using DwordList = mfc::CList<std::uint32_t>;
using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// The item link record of an item of the item manager (CItem::unknown_1C).
CItemLink* LinkOf(CEngineObject* item) {
    return reinterpret_cast<CItemLink*>(static_cast<std::uintptr_t>(static_cast<CItem*>(item)->unknown_1C));
}

// The memory stream locals of LoadSave / SaveWorld are built in place.
void InitMemoryStream(CMemoryStream* stream) {
    std::memset(stream, 0, sizeof(*stream));
    ei::SetVtable(stream, kMemoryStreamVtable);
    stream->version_ = kStreamLimit;
    stream->ok_ = 1;
    stream->data_ = nullptr;
    stream->Reset();
}

void DestroyMemoryStream(CMemoryStream* stream) {
    ei::SetVtable(stream, kMemoryStreamVtable);
    if (stream->data_) gamecrt::operator_delete(stream->data_);
    ei::SetVtable(stream, kStreamVtable);
}

// A CMemoryStream local: built in place, its buffer freed when it goes out of scope.
struct LocalMemoryStream {
    CMemoryStream stream;
    LocalMemoryStream() { InitMemoryStream(&stream); }
    ~LocalMemoryStream() { DestroyMemoryStream(&stream); }
    LocalMemoryStream(const LocalMemoryStream&) = delete;
    LocalMemoryStream& operator=(const LocalMemoryStream&) = delete;
};

void WriteString(WorldStream* stream, const char* text) {
    stream->Write(text, static_cast<unsigned>(std::strlen(text) + 1));
}

// A dword written through a stack copy, as the original writes most scalars.
template <typename T>
void WriteCopy(WorldStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// 00461190 (RET 4). Reads a zero-terminated string; constructs the CString at `out` and returns it.
__declspec(naked) mfc::CString* __fastcall ReadString(WorldStream*, void*, void* /*out*/) EI_ORIGINAL(0x00461190)
// 00595080 (RET 4). Picks the loading movie for the zone. Patched by Rebirth: original address.
__declspec(naked) void __fastcall PickLoadingMovie(CWorldServer*, void*, const char* /*zone*/) EI_ORIGINAL(0x00595080)
// 005DA400. Shows the loading screen at `stage` (ECX).
__declspec(naked) void __fastcall ShowLoadProgress(int /*stage*/) EI_ORIGINAL(0x005DA400)
// 00665200. Empties a player before it is loaded.
void __fastcall PlayerClear(WorldPlayer* a0, void*) { PlayerClearOriginal((void*)a0); }
// 005B8B30 / 005B8B80. CAIMap::BeginRefresh / EndRefresh (ai_footprint.cpp).
__declspec(naked) void __fastcall AIMapBeginRefresh(void*, void*) EI_ORIGINAL(0x005B8B30)
__declspec(naked) void __fastcall AIMapEndRefresh(void*, void*) EI_ORIGINAL(0x005B8B80)
// 005BA830 / 005BA9D0. CAIMap::CanLeaveCell / FindFreeCellNear (ai_map.cpp).
__declspec(naked) bool __fastcall AIMapCanLeaveCell(void*, void*, CEngineObject*) EI_ORIGINAL(0x005BA830)
__declspec(naked) bool __fastcall AIMapFindFreeCellNear(void*, void*, CEngineObject*, const Vector3* /*from*/,
                                                        Vector3* /*out*/) EI_ORIGINAL(0x005BA9D0)
// 00596140 (RET 0xC). Loads the zone's AI graph state into `out` (map, .mob); non-zero when found.
__declspec(naked) int __fastcall LoadAIGraph(CWorldServer*, void*, const char* /*map*/, const char* /*mob*/,
                                             CMemoryStream* /*out*/) EI_ORIGINAL(0x00596140)
// 004A50D0 (RET 8). Rebuilds a memory stream from `base` and the differences stored in `stream`.
__declspec(naked) void __fastcall MemoryStreamApplyDiff(CMemoryStream*, void*, CMemoryStream* /*base*/,
                                                        WorldStream*) EI_ORIGINAL(0x004A50D0)
// 004A4DA0 (RET 8). Writes the differences between `base` and `current` to the stream (ECX).
__declspec(naked) void __fastcall StreamWriteDiff(WorldStream*, void*, CMemoryStream* /*base*/,
                                                  CMemoryStream* /*current*/) EI_ORIGINAL(0x004A4DA0)
// 005B1380 (RET 4). Saves the AI layer at +0x2468 into a memory stream.
__declspec(naked) void __fastcall AILayerSave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x005B1380)
// 00595C70 (RET 0x10). LoadMap: loads the terrain when the map changed, then the zone (the AI
// graph state in `graph`); 0 on failure.
__declspec(naked) int __fastcall LoadMapAt(CWorldServer*, void*, const char* /*map*/, const char* /*mob*/,
                                         CMemoryStream* /*graph*/, char /*fromSave*/) EI_ORIGINAL(0x00595C70)
// 00596900. LoadSavedMapObjects, through its address (rewritten here) so its verify sees the call.
__declspec(naked) void __fastcall LoadSavedMapObjectsAt(CWorldServer*, void*, WorldStream*) EI_ORIGINAL(0x00596900)
// 00596730. SaveMapObjects (world_zone.cpp), through its address.
__declspec(naked) void __fastcall SaveMapObjectsAt(CWorldServer*, void*, WorldStream*) EI_ORIGINAL(0x00596730)
// 0058D280. UpdatePlayerTimes, through its address.
// 004047E0 / 00404860 (RET 4). A manager's changed objects, read / written by id.
__declspec(naked) void __fastcall ManagerLoadObjects(CEngineObject*, void*, WorldStream*) EI_ORIGINAL(0x004047E0)
__declspec(naked) void __fastcall ManagerSaveObjects(CEngineObject*, void*, WorldStream*) EI_ORIGINAL(0x00404860)
// 004048F0. Releases the manager's objects nobody holds (reference count 0).
__declspec(naked) void __fastcall ManagerReleaseUnused(CEngineObject*, void*) EI_ORIGINAL(0x004048F0)
// 00403F40 (RET 4). CBaseManager::FindObject (base_manager.cpp).
// 00675CA0. Marks an object new (flags_ |= 2, stamp).
void __fastcall MarkNew(CEngineObject* a0, void*) { ObjectMarkNewOriginal((void*)a0); }
// 0067E510 / 0067E7D0 / 0067E960. Missile constructor (returns this), Load and Save.
__declspec(naked) void __fastcall ProjectileLoad(Projectile*, void*, WorldStream*) EI_ORIGINAL(0x0067E7D0)
__declspec(naked) void __fastcall ProjectileSave(Projectile*, void*, WorldStream*) EI_ORIGINAL(0x0067E960)
// 0059C7A0 (RET 8). New node of the missile list (its counted pointer constructed empty).
__declspec(naked) ProjectileList::Node* __fastcall ProjectileNewNode(ProjectileList*, void*, ProjectileList::Node* /*prev*/,
                                                                   ProjectileList::Node* /*next*/) EI_ORIGINAL(0x0059C7A0)
// 00419CD0 (RET 4). RefPtr::Assign (core_refcount.cpp).
__declspec(naked) void __fastcall RefPtrAssign(void* /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x00419CD0)
// 0040A3A0. CRefObject::Release.
void __fastcall RefRelease(void* a0, void*) { RefObjectRelease(a0); }
// 00429EA0 (RET 8) / 00429B50 (RET 4). New node of the counted list (+0x12E44) and the counted
// pointer's assignment (reference added, old one released through slot 4).
mfc::CTypedPtrList<CEngineObject>::Node* __fastcall CountedNewNode(mfc::CTypedPtrList<CEngineObject>* a0, void*, mfc::CTypedPtrList<CEngineObject>::Node* a2, mfc::CTypedPtrList<CEngineObject>::Node* a3) { return (mfc::CTypedPtrList<CEngineObject>::Node*)ListNewNode429EA0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
__declspec(naked) void __fastcall CountedAssign(void* /*slot*/, void*, CEngineObject*) EI_ORIGINAL(0x00429B50)
// Load / Save of the time wheel (+0x2838), the first command queue (+0x12874), the diplomacy
// tables (+0x12C64) and the countdowns (+0x70).
__declspec(naked) void __fastcall TimeWheelLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x005CAE30)
__declspec(naked) void __fastcall TimeWheelSave(void*, void*, WorldStream*) EI_ORIGINAL(0x005CAFA0)
__declspec(naked) void __fastcall CommandQueueLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x004C0D00)
__declspec(naked) void __fastcall CommandQueueSave(void*, void*, WorldStream*) EI_ORIGINAL(0x004C0F90)
__declspec(naked) void __fastcall DiplomacyRead(void*, void*, WorldStream*) EI_ORIGINAL(0x004557C0)
__declspec(naked) void __fastcall DiplomacyWrite(void*, void*, WorldStream*) EI_ORIGINAL(0x004557F0)
__declspec(naked) void __fastcall CountdownsLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x006D4660)
__declspec(naked) void __fastcall CountdownsSave(void*, void*, WorldStream*) EI_ORIGINAL(0x006D47E0)
// 006618C0 / 00661C10 (RET 8). A player's party records, read / written.
__declspec(naked) void __fastcall PlayerRecordsLoad(void*, void*, WorldStream*, int) EI_ORIGINAL(0x006618C0)
__declspec(naked) void __fastcall PlayerRecordsSave(void*, void*, WorldStream*, int) EI_ORIGINAL(0x00661C10)
// 004C2030 / 004B3360 (RET 4) / 004C2290. A script group's constructor, Load and destructor.
void __fastcall ScriptGroupConstruct(void* a0, void*) { ScriptGroupConstructOriginal(a0); }
__declspec(naked) void __fastcall ScriptGroupLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x004B3360)
inline void __fastcall ScriptGroupDestruct(void* a0, void*) { ScriptGroupDestructOriginal((void*)a0); }
// 0066EF80 / 0066D260 (RET 4). The player state's refresh after a load and SetFlag70.
void __fastcall PlayerStateRefresh(void* a0, void*) { PlayerStateRefreshOriginal(a0); }
__declspec(naked) void __fastcall PlayerStateSetFlag70(void*, void*, int /*byte*/) EI_ORIGINAL(0x0066D260)
// 0066EE10 (RET 8). CPlayerServer::SetVariable (player_messages.cpp).
__declspec(naked) void __fastcall PlayerStateSetVariable(void*, void*, const char*, float) EI_ORIGINAL(0x0066EE10)
// 00460AF0 (RET 4). Named float table Get (object_tables.cpp); the value is left in ST0.
double __fastcall VariableGet(void* a0, void*, const char* a2) { return (double)NamedTableGet(a0, nullptr, a2); }
// 0055FB00 (RET 4). CWorld::FindUnit.
// 0055FBD0 (RET 8). The time of day and the time speed (as LoadSave hands them to slots 31 / 32).
// 005C1010 / 005C0DF0 (RET 4). CSectorMap::Remove / Add (sector_map.cpp) on +0x294.
// 00554450. The unit controller's reposition after a move.
void __fastcall ControllerReposition(void* a0, void*) { ControllerRepositionOriginal(a0); }
// 005935C0 (RET 4). RemoveObjectFromServer. Patched by Rebirth: original address.
// 006FEBFA / 006FE9B8. CStringArray::SetAtGrow(int, const CString&) / SetSize(int, int).
void __fastcall StringArraySetAtGrow(mfc::CStringArray* a0, void*, int a2, const mfc::CString* a3) { StringArraySetAtGrowStringOriginal((void*)a0, nullptr, a2, a3); }
// 0050EA40 / 0050E9F0 (RET 4) / 005179E0 (RET 4). Map object: prepares the record array before
// Load, MarkChanged(part), CObject3D's animated figure setup for its figure name.
void __fastcall MapObjectBeginLoad(CEngineObject* a0, void*) { ObjectPrepareOriginal((void*)a0); }
void __fastcall MapObjectSetupFigure(CEngineObject* a0, void*, const char* a2) { ObjectSetupFigureOriginal((void*)a0, nullptr, a2); }

} // namespace

namespace {
__declspec(naked) void __fastcall ObjectPtrArrayConstruct(ObjectPtrArray*) EI_ORIGINAL(0x005985C0)
void ObjectPtrArrayDestruct(ObjectPtrArray* a) { CountedPtrArrayDestructOriginal(a); }
__declspec(naked) SharedObjectNode* __fastcall SharedObjectsNewNode(SharedObjectList*, void*, SharedObjectNode* /*prev*/,
                                                                     SharedObjectNode* /*next*/) EI_ORIGINAL(0x0059CA60)
__declspec(naked) void __fastcall DwordListAddTail(DwordList*, void*, std::uint32_t) EI_ORIGINAL(0x0059CC60)
} // namespace
__declspec(naked) void SharedObject::CopyFrom(const SharedObject*) EI_ORIGINAL(0x0059B510)
__declspec(naked) void SharedObject::Load(WorldStream*) EI_ORIGINAL(0x00589CA0)

namespace {


// Reads a string and assigns it to `target` (the temporary is destroyed at once).
void ReadStringInto(WorldStream* stream, mfc::CString& target) {
    alignas(4) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* read = ReadString(stream, nullptr, storage);
    target = *read;
    read->~CString();
}

template <typename T>
T ReadValue(WorldStream* stream) {
    T value;
    stream->Read(&value, sizeof(T));
    return value;
}

} // namespace

// ---- UpdatePlayerTimes ------------------------------------------------------------------------

// The sum in the original's order ((elapsed * secondsPerTick + offset) + gtime); plain C++ maths
// (rewrite guide rule 7), compared with CheckNear.
void CWorldServer::UpdatePlayerTimes(float offset) {
    const char* name = game::GTimeText();
    int now = tick_;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        int elapsed = static_cast<int>(static_cast<unsigned>(now) - static_cast<unsigned>(player->lastTick));
        player->lastTick = now;
        if (elapsed < 0) elapsed = 0;
        double current = VariableGet(player->variables, nullptr, name);
        float value = static_cast<float>(static_cast<double>(elapsed) * secondsPerTick_ + offset + current);
        PlayerStateSetVariable(player->server, nullptr, name, value);
    }
}

// ---- LoadSavedMapObjects ----------------------------------------------------------------------

void CWorldServer::LoadSavedMapObjects(WorldStream* stream) {
    for (;;) {
        int id = ReadValue<int>(stream);
        if (id == -1) return;
        CEngineObject* object = ManagerFindObject(manager40_, nullptr, id);
        auto* mapObject = static_cast<CMapObject*>(object);
        if (stream->version_ >= kVersionMapObjectName) {
            mfc::CString name;
            ReadStringInto(stream, name);
            ObjectSlots(object)->TakePrototype(static_cast<const char*>(name));
        }
        MapObjectBeginLoad(object, nullptr);
        object->Load(stream);
        int changed = ReadValue<int>(stream);
        if (!object) continue;   // (the object was already used above)

        ++object->refCount_;
        UnitMarkChanged(object, nullptr, -1);
        if (object->type_ != 0x3B) {
            MapObjectSetupFigure(object, nullptr, mapObject->name);
            SectorMapAdd(Sectors(), nullptr, object);
        }
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit)
            otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (*mapObject->name2 != '\0')
            namedObjects_.Set(static_cast<unsigned>(object->id_), object);

        if (object->GetClassId() == world::kClassUnit) {
            // The unit's six dwords at +0x4B0 come from its prototype's model record (+0x2C). A
            // prototype or model out of range is read through null, as in the original.
            auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(
                unitdb::Prototypes().At(object->unknown_18, unitdb::kPrototypeSize));
            auto* model = reinterpret_cast<const UnitModelRecord*>(
                unitdb::Models().At(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
            std::memcpy(static_cast<CUnit*>(object)->resistances, model->unknown_2C, 24);
        }

        // Every part is marked changed when any change was saved (the original tests the part's
        // bit 1 << i itself, never `changed & bit`).
        int parts = mapObject->NetState()->fieldCount_;
        for (int i = 0; i < parts; ++i)
            if (changed != 0 && (1u << (i & 31)) != 0) UnitMarkChanged(object, nullptr, i);

        if (object->GetClassId() == world::kClassUnit &&
            reinterpret_cast<UnitController*>(static_cast<CUnit*>(object)->controller)->Brain()->unknown_37 == 0)
            continue;   // a unit whose controller does not ask for updates (its brain's +0x37)
        AddToUpdateMap(object);
    }
}

// ---- LoadSave ---------------------------------------------------------------------------------

namespace {

// The units saved standing where they cannot move (a cell they cannot leave) are put on a free
// cell nearby when they are scripted units (text-key ids), otherwise removed.
void RelocateStrandedUnits(CWorldServer* world) {
    IdArray stranded;
    stranded.vtable = mfc::DWordArrayVtable();
    stranded.data = nullptr;
    stranded.growBy = 0;
    stranded.maxSize = 0;
    stranded.size = 0;
    for (auto* node = world->objects_.map.First(); node; node = world->objects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (object->GetClassId() != world::kClassUnit) continue;
        if (AIMapCanLeaveCell(world->AIMap(), nullptr, object)) continue;
        int id = object->id_;
        int index = stranded.size;
        DwordArraySetSizeOriginal(&stranded, nullptr, index + 1, -1);
        stranded.data[index] = id;
    }
    for (int i = 0; i < stranded.size; ++i) {
        CEngineObject* unit = WorldFindUnit(world, nullptr, stranded.data[i]);
        if (unit && unit->id_ > kTextKeyIdMin) {
            SectorMapRemove(world->Sectors(), nullptr, unit);
            float* position = &static_cast<CUnit*>(unit)->positionX;   // x, y, z
            Vector3 from;
            Vector3 to;
            std::memcpy(&from, position, sizeof(Vector3));
            std::memcpy(&to, position, sizeof(Vector3));
            AIMapFindFreeCellNear(world->AIMap(), nullptr, unit, &from, &to);
            std::memcpy(position, &to, sizeof(Vector3));
            SectorMapAdd(world->Sectors(), nullptr, unit);
            ControllerReposition(static_cast<CUnit*>(unit)->controller, nullptr);
        } else {
            WorldRemoveObject(world, nullptr, stranded.data[i]);
        }
    }
    stranded.vtable = mfc::DWordArrayVtable();
    if (stranded.data) gamecrt::operator_delete(stranded.data);
}

// The players' part of the save (in the player list's order).
void LoadPlayers(CWorldServer* world, WorldStream* stream) {
    // The original keeps this iterator on the heap.
    auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
    if (it) it->list = nullptr;
    for (WorldPlayer* player = it->First(world->PlayerList()); player; player = it->Next()) {
        CPlayerServer* state = player->server;
        stream->Read(&player->unknownFC, 4);
        PlayerRecordsLoad(player->party, nullptr, stream, 1);
        player->lastTick = world->tick_;
        stream->Read(&player->unknown100, 4);
        CEngineObject* items = world->ItemFactory();
        for (;;) {
            int id = ReadValue<int>(stream);
            if (id == -1) break;
            CEngineObject* object = ManagerFindObject(items, nullptr, id);
            if (!object) continue;
            PtrListAddTailOriginal(&player->items, nullptr, object);
            ++object->refCount_;
        }
        stream->Read(&player->unknown108, 1);
        if (stream->version_ >= kVersionPlayerLists) {
            DwordList& ids = player->ids;
            int count = ReadValue<int>(stream);
            for (int i = 0; i < count; ++i) DwordListAddTail(&ids, nullptr, 0);
            for (auto* node = ids.head; node;) {
                auto* next = node->next;
                stream->Read(&node->data, 4);
                node = next;
            }
        }
        if (stream->version_ < kVersionPlayerByte) ReadValue<std::uint8_t>(stream);
        if (stream->version_ < kVersionPlayerGroup) {
            ScriptGroup group;
            ScriptGroupConstruct(&group, nullptr);
            group.vtable = scriptvtable::kRefs;
            ScriptGroupLoad(&group, nullptr, stream);
            ScriptGroupDestruct(&group, nullptr);
        }
        PlayerStateRefresh(state, nullptr);
        PlayerStateSetFlag70(state, nullptr, 1);
    }
    if (it) {
        if (it->list) {
            --it->list->refCount_;
            it->list = nullptr;
        }
        gamecrt::operator_delete(it);
    }
}

} // namespace

int CWorldServer::LoadSave(WorldStream* stream, const char* map) {
    LocalMemoryStream savedGraph;   // the AI graph state as the zone files have it
    LocalMemoryStream graph;        // ... with the save's differences applied
    mfc::CString mobName;
    mfc::CString mapName;
    mfc::CString zoneName;

    gameResult_ = 0;
    unknown160_ = 0;
    if (PlayerList()) {
        PlayerIterator it;
        for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
            gameResult_ = 1;
            unknown160_ = 1;
            PlayerClear(player, nullptr);
        }
    }
    AIMapBeginRefresh(AIMap(), nullptr);
    loaded_ = 0;
    AIMap()->refreshUnits = false;   // +0x172

    ReadStringInto(stream, mobName);
    ReadStringInto(stream, mapName);
    ReadStringInto(stream, zoneName);
    if (map) mapName = map;
    *reinterpret_cast<mfc::CString*>(&zoneName_) = zoneName;
    PickLoadingMovie(this, nullptr, zoneName);
    ShowLoadProgress(kProgressStart);
    {
        PlayerIterator it;
        for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->dirty = 0xFFFFFFFFu;
    }

    stream->Read(unknown24_, 12);
    stream->Read(&unknown30_, 4);
    stream->Read(&unknown84_, 4);
    for (WorldAlarm& alarm : alarms_) stream->Read(&alarm, 16);
    stream->Read(&tick_, 4);
    stream->Read(&unknownE4_, 4);
    stream->Read(&unknownE8_, 4);
    float time = ReadValue<float>(stream);
    float speed;
    if (stream->version_ >= kVersionTimeSpeedFloat) {
        stream->Read(&speed, 4);
        if (stream->version_ >= kVersionUnknown13C) stream->Read(&unknown13C_, 4);
    } else {
        speed = static_cast<float>(ReadValue<int>(stream));
    }
    stream->Read(&unknown12D64_, 4);

    int result;
    if (!LoadAIGraph(this, nullptr, mapName, mobName, &savedGraph.stream)) {
        result = 0;
    } else {
        MemoryStreamApplyDiff(&graph.stream, nullptr, &savedGraph.stream, stream);
        ShowLoadProgress(kProgressZone);
        if (!LoadMapAt(this, nullptr, mapName, mobName, &graph.stream, 1)) {
            result = game::FatalError(game::CantLoadSaveMapText());
        } else {
            // The map objects' identities: id, type and +0x18 (created by the manager at +0x40).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id == -1) break;
                int type = ReadValue<int>(stream);
                std::uint32_t value18 = ReadValue<std::uint32_t>(stream);
                CEngineObject* object = static_cast<CBaseManager*>(manager40_)->CreateObject(type, id);   // slot 23
                object->unknown_18 = value18;
            }
            manager48_->Load(stream);
            manager4C_->Load(stream);
            itemManager_->Load(stream);
            ShowLoadProgress(kProgressManagers);
            LoadSavedMapObjectsAt(this, nullptr, stream);
            ManagerLoadObjects(manager48_, nullptr, stream);
            ManagerLoadObjects(manager4C_, nullptr, stream);
            ManagerLoadObjects(itemManager_, nullptr, stream);
            ShowLoadProgress(kProgressObjects);

            // Timed objects (spells, effects) by id.
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id == -1) break;
                CEngineObject* object = ManagerFindObject(manager48_, nullptr, id);
                ++object->refCount_;
                MarkNew(object, nullptr);
                timedObjects_.Set(static_cast<unsigned>(object->id_), object);
            }

            // Missiles: a byte 1 before each one, 0 at the end. They find the world through
            // the global at 007C84EC while they load.
            LoadingProjectileWorld() = this;
            for (;;) {
                std::uint8_t more = ReadValue<std::uint8_t>(stream);
                if (!more) break;
                void* memory = gamecrt::operator_new(kMissileSize);
                static_cast<Projectile*>(memory)->references = 0;   // written before the null test
                Projectile* missile = memory ? ProjectileConstruct(memory, nullptr) : nullptr;
                ProjectileLoad(missile, nullptr, stream);
                auto& list = projectiles_;
                ProjectileList::Node* node = ProjectileNewNode(&projectiles_, nullptr, list.tail, nullptr);
                RefPtrAssign(&node->data, nullptr, missile);
                if (list.tail) list.tail->next = node;
                else list.head = node;
                list.tail = node;
            }

            TimeWheelLoad(timeWheel_, nullptr, stream);
            CommandQueueLoad(commandsA_, nullptr, stream);
            DiplomacyRead(diplomacy_, nullptr, stream);
            stream->Read(&unknownDC_, 4);
            stream->Read(&unknownE0_, 4);
            if (stream->version_ >= kVersionCountdowns) CountdownsLoad(countdowns_, nullptr, stream);

            // Counted references to items (added at the head of the list).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id < 0) break;
                CEngineObject* item = ManagerFindObject(itemManager_, nullptr, id);
                if (!item) continue;
                auto* node = CountedNewNode(&counted_, nullptr, nullptr, counted_.head);
                CountedAssign(&node->data, nullptr, item);
                if (counted_.head) counted_.head->prev = node;
                else counted_.tail = node;
                counted_.head = node;
            }

            if (PlayerList()) LoadPlayers(this, stream);
            stream->Read(&unknown6C_, 1);

            // The item manager's object records.
            if (stream->version_ >= kVersionPlayerLists) {
                for (;;) {
                    int id = -1;
                    stream->Read(&id, 4);
                    if (id == -1) break;
                    std::uint32_t value = ReadValue<std::uint32_t>(stream);
                    CEngineObject* object = ManagerFindObject(ItemFactory(), nullptr, id);
                    if (!object) continue;
                    auto* record = static_cast<CItemLink*>(gamecrt::operator_new(sizeof(CItemLink)));
                    // The count's whole dword is cleared, before the null test.
                    record->references = 0;
                    record->unknown_06 = 0;
                    if (record) record->vtable = CItemLink::Vtable();
                    record->value = value;
                    CItemLink* old = LinkOf(object);
                    static_cast<CItem*>(object)->unknown_1C = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(record));
                    if (record) ++record->references;
                    if (old) RefRelease(old, nullptr);
                }
            }

            ShowLoadProgress(kProgressDone);
            *reinterpret_cast<mfc::CString*>(&zoneName_) = zoneName;
            SetTimeSpeed(speed);   // slot 32 (virtual)
            SetTime(time);   // slot 31 (virtual)
            loaded_ = 1;
            AIMapEndRefresh(AIMap(), nullptr);
            AIMap()->refreshUnits = true;

            RelocateStrandedUnits(this);

            if (stream->version_ >= kVersionMobFiles) {
                int count = ReadValue<int>(stream);
                for (int i = 0; i < count; ++i) {
                    alignas(4) unsigned char storage[sizeof(mfc::CString)];
                    mfc::CString* name = ReadString(stream, nullptr, storage);
                    StringArraySetAtGrow(&mobFiles_, nullptr, mobFiles_.size, name);
                    name->~CString();
                }
            } else {
                StringArraySetSize(&mobFiles_, nullptr, 0, -1);
            }

            if (stream->version_ >= kVersionSharedObjects) {
                SharedObjectList& shared = sharedObjects_;
                int count = ReadValue<int>(stream);
                for (int i = 0; i < count; ++i) {
                    // The element is copied from a local whose first three dwords the original
                    // leaves unset; Load overwrites them.
                    SharedObject empty;
                    empty.objectId = 0;
                    empty.player = 0;
                    empty.unknown_08 = 0;
                    ObjectPtrArrayConstruct(&empty.objects);
                    SharedObjectNode* node = SharedObjectsNewNode(&shared, nullptr, shared.tail, nullptr);
                    node->data.CopyFrom(&empty);
                    if (shared.tail) shared.tail->next = node;
                    else shared.head = node;
                    shared.tail = node;
                    ObjectPtrArrayDestruct(&empty.objects);
                }
                for (SharedObjectNode* node = shared.head; node;) {
                    SharedObjectNode* next = node->next;
                    node->data.Load(stream);
                    node = next;
                }
            }

            ManagerReleaseUnused(manager40_, nullptr);
            ManagerReleaseUnused(manager48_, nullptr);
            ManagerReleaseUnused(manager4C_, nullptr);
            ManagerReleaseUnused(itemManager_, nullptr);
            result = 1;
        }
    }
    return result;   // the strings and the two memory streams are destroyed on the way out
}

// ---- SaveWorld --------------------------------------------------------------------------------

namespace {

// The players' part of the save (what LoadPlayers reads).
void SavePlayers(CWorldServer* world, WorldStream* stream) {
    // The original keeps this iterator on the heap.
    auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
    if (it) it->list = nullptr;
    for (WorldPlayer* player = it->First(world->PlayerList()); player; player = it->Next()) {
        stream->Write(&player->unknownFC, 4);
        PlayerRecordsSave(player->party, nullptr, stream, 1);
        stream->Write(&player->unknown100, 4);
        for (auto* node = player->items.head; node;) {
            auto* next = node->next;
            WriteCopy(stream, node->data->id_);
            node = next;
        }
        WriteCopy(stream, -1);
        WriteCopy(stream, player->unknown108);
        DwordList& ids = player->ids;
        WriteCopy(stream, ids.count);
        for (auto* node = ids.head; node;) {
            auto* next = node->next;
            stream->Write(&node->data, 4);
            node = next;
        }
    }
    if (it) {
        if (it->list) {
            --it->list->refCount_;
            it->list = nullptr;
        }
        gamecrt::operator_delete(it);
    }
}

// A memory stream local of SaveWorld: CStream's constructor (0047A200), then the memory stream's
// vtable and an empty buffer.
void ConstructSaveStream(CMemoryStream* stream) {
    std::memset(stream, 0, sizeof(*stream));
    stream->ConstructBase();
    ei::SetVtable(stream, kMemoryStreamVtable);
    stream->data_ = nullptr;
    stream->Reset();
}

} // namespace

void CWorldServer::SaveWorld(WorldStream* stream) {
    // The AI graph state as the zone files have it (`base`) and as it is now (`current`); only
    // their differences are saved.
    CMemoryStream base;
    ConstructSaveStream(&base);
    CMemoryStream current;
    ConstructSaveStream(&current);

    if (unknown12D64_ == 1 || unknown12D64_ == 3) UpdatePlayerTimesAt(this, nullptr, 0.0f);
    ManagerReleaseUnused(manager40_, nullptr);
    ManagerReleaseUnused(manager48_, nullptr);
    ManagerReleaseUnused(manager4C_, nullptr);
    ManagerReleaseUnused(itemManager_, nullptr);

    WriteString(stream, mobName_);
    WriteString(stream, mapName_);
    WriteString(stream, zoneName_);
    stream->Write(unknown24_, 12);
    WriteCopy(stream, unknown30_);
    WriteCopy(stream, unknown84_);
    for (WorldAlarm& alarm : alarms_) stream->Write(&alarm, 16);
    WriteCopy(stream, tick_);
    stream->Write(&unknownE4_, 4);
    std::uint32_t e8;
    std::memcpy(&e8, &unknownE8_, 4);
    WriteCopy(stream, e8);
    // Stored by the callee with FSTP; copied on as dwords.
    std::uint32_t time;
    std::uint32_t speed;
    WorldTimeSettings(this, nullptr, reinterpret_cast<float*>(&time), reinterpret_cast<float*>(&speed));
    WriteCopy(stream, time);
    WriteCopy(stream, speed);
    WriteCopy(stream, unknown13C_);
    stream->Write(&unknown12D64_, 4);

    LoadAIGraph(this, nullptr, mapName_, mobName_, &base);
    AILayerSave(aiLayer_, nullptr, &current);
    StreamWriteDiff(stream, nullptr, &base, &current);

    // The map objects' identities (the walk stops at the first empty entry).
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        WriteCopy(stream, object->id_);
        WriteCopy(stream, object->type_);
        WriteCopy(stream, object->unknown_18);
    }
    WriteCopy(stream, -1);
    manager48_->Save(stream);
    manager4C_->Save(stream);
    itemManager_->Save(stream);
    SaveMapObjectsAt(this, nullptr, stream);
    ManagerSaveObjects(manager48_, nullptr, stream);
    ManagerSaveObjects(manager4C_, nullptr, stream);
    ManagerSaveObjects(itemManager_, nullptr, stream);

    for (auto* node = timedObjects_.map.First(); node; node = timedObjects_.map.After(node))
        WriteCopy(stream, node->value->id_);
    WriteCopy(stream, -1);

    for (auto* node = projectiles_.head; node;) {
        auto* next = node->next;
        Projectile* missile = node->data;
        WriteCopy(stream, static_cast<std::uint8_t>(1));
        ProjectileSave(missile, nullptr, stream);
        node = next;
    }
    WriteCopy(stream, static_cast<std::uint8_t>(0));

    TimeWheelSave(timeWheel_, nullptr, stream);
    CommandQueueSave(commandsA_, nullptr, stream);
    DiplomacyWrite(diplomacy_, nullptr, stream);
    stream->Write(&unknownDC_, 4);
    stream->Write(&unknownE0_, 4);
    CountdownsSave(countdowns_, nullptr, stream);

    for (auto* node = counted_.head; node;) {
        auto* next = node->next;
        WriteCopy(stream, node->data->id_);
        node = next;
    }
    WriteCopy(stream, -1);

    if (PlayerList()) SavePlayers(this, stream);
    WriteCopy(stream, unknown6C_);

    // The item manager's object records.
    {
        PlayerIterator it;
        for (auto* item = reinterpret_cast<CEngineObject*>(it.First(ItemFactory())); item;
             item = reinterpret_cast<CEngineObject*>(it.Next())) {
            CItemLink* record = LinkOf(item);
            if (!record) continue;
            WriteCopy(stream, item->id_);
            WriteCopy(stream, record->value);
        }
        WriteCopy(stream, -1);
    }

    WriteCopy(stream, mobFiles_.size);
    for (int i = 0; i < mobFiles_.size; ++i) WriteString(stream, mobFiles_.data[i]);

    SharedObjectList& shared = sharedObjects_;
    WriteCopy(stream, shared.count);
    for (SharedObjectNode* node = shared.head; node;) {
        SharedObjectNode* next = node->next;
        WriteCopy(stream, node->data.objectId);
        WriteCopy(stream, node->data.player);
        WriteCopy(stream, node->data.unknown_08);
        int count = node->data.objects.size;
        WriteCopy(stream, count);
        for (int i = 0; i < count; ++i) WriteCopy(stream, node->data.objects.data[i]->id_);
        node = next;
    }

    ei::SetVtable(&current, kMemoryStreamVtable);
    if (current.data_) gamecrt::operator_delete(current.data_);
    ei::SetVtable(&base, kMemoryStreamVtable);
    if (base.data_) gamecrt::operator_delete(base.data_);
    ei::SetVtable(&current, kStreamVtable);
}

// ---- The shared-object list -------------------------------------------------------------------

namespace {

// 0059B8F0 (RET 4). CArray<CRefPtr>::Copy.
__declspec(naked) void __fastcall ObjectPtrArrayCopy(ObjectPtrArray*, void*, const ObjectPtrArray*) EI_ORIGINAL(0x0059B8F0)
// 0059C8C0 (RET 8). CArray<CRefPtr>::SetSize(size, growBy); the same code the original inlines
// in LoadSharedObject.
__declspec(naked) void __fastcall ObjectPtrArraySetSize(ObjectPtrArray*, void*, int, int) EI_ORIGINAL(0x0059C8C0)
// 0059C890 (RET 8). Releases `count` counted pointers from `data` (CArray DestructElements).
void __stdcall CountedRangeRelease(CEngineObject** a0, int a1) { CountedRangeReleaseOriginal(a0, a1); }
// 00403F40 (RET 4). The item manager's (007AFFB4, WorldItemManager) object by id, or null.

// SetAtGrow(size, object) on a CArray<CRefPtr>: the new slot holds a reference.
void Append(ObjectPtrArray& array, CEngineObject* object) {
    int index = array.size;
    ObjectPtrArraySetSize(&array, nullptr, index + 1, -1);
    CEngineObject* old = array.data[index];
    array.data[index] = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

} // namespace

void __fastcall LoadSharedObject(void* entryArg, void*, WorldStream* stream) {
    auto* entry = static_cast<SharedObject*>(entryArg);
    stream->Read(&entry->objectId, 4);
    stream->Read(&entry->player, 4);
    stream->Read(&entry->unknown_08, 4);
    int count = ReadValue<int>(stream);
    for (int i = 0; i < count; ++i) {
        int id = ReadValue<int>(stream);
        CEngineObject* object = ManagerFindObject(WorldItemManager(), nullptr, id);
        if (object) Append(entry->objects, object);   // the original inlines the SetSize
    }
}

void CWorldServer::AddSharedObject(int id, int /*unused*/, std::uint32_t value, void* objectsArg) {
    // The entry's player: the session player record's +0x30 in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::GameServer();
    if (session && !(session->flags7_ & CRefCounted::kDetached))
        if (auto* record = static_cast<net::PlayerMessage*>(session->FindPlayer(id))) player = record->serial_;   // 0041A2E0

    // AddHead(SharedObject()): the new head copies an empty local (whose first three dwords the
    // original leaves unset; they are overwritten below).
    SharedObjectList& shared = sharedObjects_;
    ObjectPtrArray empty{SharedObject::ObjectsVtable(), nullptr, 0, 0, 0};
    SharedObjectNode* node = SharedObjectsNewNode(&shared, nullptr, nullptr, shared.head);
    ObjectPtrArrayCopy(&node->data.objects, nullptr, &empty);
    if (shared.head) shared.head->prev = node;
    else shared.tail = node;
    shared.head = node;
    empty.vtable = SharedObject::ObjectsBaseVtable();
    if (empty.data) {
        CountedRangeRelease(empty.data, empty.size);
        gamecrt::operator_delete(empty.data);
    }

    SharedObject& entry = shared.head->data;
    entry.objectId = id;
    entry.player = player;
    entry.unknown_08 = value;
    auto* objects = static_cast<mfc::CTypedPtrList<CEngineObject>*>(objectsArg);
    for (auto* it = objects->head; it;) {
        auto* next = it->next;
        Append(entry.objects, it->data);
        it = next;
    }
}

// ---- PrepareSave --------------------------------------------------------------------------------

namespace {

// 00525AF0. UnitStats::RefillSlots (unit_vitals.cpp), on the controller's stats (+0x1E0).
__declspec(naked) void __fastcall StatsRefillSlots(UnitStats*, void*) EI_ORIGINAL(0x00525AF0)

} // namespace

void CWorldServer::PrepareSave() {
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CEngineObject* unit = node->value;
        if (unit->GetClassId() != world::kClassUnit || ObjectSlots(unit)->IsDead()) continue;   // slot 55
        CEngineObject* controller = static_cast<CUnit*>(unit)->controller;
        StatsRefillSlots(&reinterpret_cast<UnitController*>(controller)->stats, nullptr);
        static_cast<CUnitController*>(controller)->RefreshVitals(0);   // slot 19 (+0x4C)
    }
}

EI_REPLACE_METHOD(0x005956E0, CWorldServer::PrepareSave);
EI_REPLACE(0x00589CA0, LoadSharedObject);
EI_REPLACE_METHOD(0x005947E0, CWorldServer::AddSharedObject);
EI_REPLACE_METHOD(0x00596E70, CWorldServer::LoadSave);
EI_REPLACE_METHOD(0x00597BD0, CWorldServer::SaveWorld);
EI_REPLACE_METHOD(0x00596900, CWorldServer::LoadSavedMapObjects);
EI_REPLACE_METHOD(0x0058D280, CWorldServer::UpdatePlayerTimes);
