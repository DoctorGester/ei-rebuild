// CWorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
// back, LoadSavedMapObjects restores the map objects SaveMapObjects wrote, and UpdatePlayerTimes
// brings the players' "gtime" up to date before a save. See world.h and docs/notes/world.md
// (seventh round).
#include "world.h"

#include "core_streams.h"
#include "net_session.h"
#include "object.h"

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

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// ---- Constants --------------------------------------------------------------------------------

// kMemoryStreamVtable (0073BD90) and kStreamVtable (0073BDB0, its CStream base): core_streams.h.
constexpr std::uint32_t kStreamLimit = 2000000000;            // CStream +0x08 as constructed
constexpr std::uintptr_t kIdArrayVtable = 0x0073C3D0;         // CArray<int>
constexpr std::uintptr_t kScriptGroupVtable = 0x0073D110;     // ScriptGroup (set after 004C2030)
constexpr std::uintptr_t kObjectRecordVtable = 0x0073FA88;    // the 12-byte record at object +0x1C
constexpr std::uintptr_t kGTimeName = 0x0073F920;             // "gtime"
constexpr std::uintptr_t kCantLoadMap = 0x00790BB8;           // "CWorldServer::LoadSave(): Can't load map..."
constexpr int kTextKeyIdMin = 100000000;                      // ids above this are scripted (text-key) units

// Save format versions (WorldStream::version) at which fields were added.
constexpr int kVersionTimeSpeedFloat = 0x4C;   // the time speed is a float (an int before)
constexpr int kVersionUnknown13C = 0x74;
constexpr int kVersionCountdowns = 0x4E;
constexpr int kVersionPlayerLists = 0x55;      // a player's id list (+0x114); the object records
constexpr int kVersionPlayerByte = 0x49;       // before: a byte that is read and dropped
constexpr int kVersionPlayerGroup = 0x46;      // before: a script group that is read and dropped
constexpr int kVersionMobFiles = 0x60;
constexpr int kVersionSharedObjects = 0x6E;
constexpr int kVersionMapObjectName = 0x59;

// Vtable slots.
constexpr int kSlotSetTime = 31;           // 0058EEA0
constexpr int kSlotSetTimeSpeed = 32;      // 0058EEE0
constexpr int kSlotCreateObject = 23;      // manager +0x40: create an object of a type with an id
constexpr int kSlotSetSecondName = 34;     // map object slot 34 (+0x88)

// Progress screen stages LoadSave shows (005DA400).
constexpr int kProgressStart = 0;
constexpr int kProgressZone = 1;
constexpr int kProgressManagers = 5;
constexpr int kProgressObjects = 6;
constexpr int kProgressDone = 7;

constexpr int kSharedObjectNodeSize = 0x28;

// ---- Local layouts ----------------------------------------------------------------------------

// MFC CArray<int> (vtable 0073C3D0).
struct IdArray {
    void* vtable;
    int* data;
    int size;
    int capacity;
    int growBy;

    void SetSize(int newSize, int newGrowBy);   // 0059C660 (RET 8)
};
static_assert(sizeof(IdArray) == 0x14);

// The 12-byte record an object keeps at +0x1C (vtable 0073FA88, 16-bit count at +4), restored for
// the item manager's objects.
struct ObjectRecord {
    void* vtable;
    std::int16_t refs;
    std::uint16_t unknown_06;
    std::uint32_t value;   // +0x08
};
static_assert(sizeof(ObjectRecord) == 0xC);

// MFC CArray of object pointers (vtable 0073FAC8).
struct ObjectPtrArray {
    void* vtable;
    CEngineObject** data;
    int size;
    int capacity;
    int growBy;

    void Construct();   // 005985C0
    void Destruct();    // 0059A100 (releases the objects through slot 4, frees the data)
};
static_assert(sizeof(ObjectPtrArray) == 0x14);

// An element of the shared-object list (+0x12E60, 0x20 bytes): object id, player, a third dword
// and an array of objects.
struct SharedObject {
    int objectId;
    int player;
    std::uint32_t unknown_08;
    ObjectPtrArray objects;     // +0x0C

    void CopyFrom(const SharedObject* from);  // 0059B510 (RET 4)
    void Load(WorldStream* stream);           // 00589CA0 (RET 4)
};
static_assert(sizeof(SharedObject) == 0x20);

struct SharedObjectNode {
    SharedObjectNode* next;
    SharedObjectNode* prev;
    SharedObject value;
};
static_assert(sizeof(SharedObjectNode) == kSharedObjectNodeSize);

// MFC CList<SharedObject> at +0x12E60.
struct SharedObjectList {
    void* vtable;
    SharedObjectNode* head;
    SharedObjectNode* tail;
    int count;
    void* free;
    void* blocks;
    int blockSize;

    SharedObjectNode* NewNode(SharedObjectNode* prev, SharedObjectNode* next);   // 0059CA60 (RET 8)
};
static_assert(sizeof(SharedObjectList) == 0x1C);

SharedObjectList& SharedObjects(CWorldServer* world) { return At<SharedObjectList>(world, 0x12E60); }

// A player's MFC list of plain dwords (+0x114, AddTail 0059CC60) and of objects (+0xE0, AddTail
// 004C98F0); only head and the node layout are read here.
struct DwordList {
    struct Node {
        Node* next;
        Node* prev;
        std::uint32_t value;
    };
    void* vtable;
    Node* head;
    Node* tail;
    int count;

    void AddTail(std::uint32_t value);   // 0059CC60 (RET 4)
};
struct ObjectList {
    struct Node {
        Node* next;
        Node* prev;
        CEngineObject* object;
    };
    void* vtable;
    Node* head;
    Node* tail;
    int count;

    void AddTail(CEngineObject* object);   // 004C98F0 (RET 4)
};

// Player fields saved with the world.
constexpr int kPlayerState = 0x20;          // CPlayerServer*
constexpr int kPlayerRecords = 0x70;        // the party records (006618C0 / 00661C10)
constexpr int kPlayerObjects = 0xE0;        // ObjectList
constexpr int kPlayerFC = 0xFC;
constexpr int kPlayer100 = 0x100;
constexpr int kPlayer108 = 0x108;           // a byte
constexpr int kPlayerIds = 0x114;           // DwordList

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
__declspec(naked) void __fastcall PlayerClear(WorldPlayer*, void*) EI_ORIGINAL(0x00665200)
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
__declspec(naked) mfc::CTypedPtrList<CEngineObject>::Node* __fastcall CountedNewNode(
    mfc::CTypedPtrList<CEngineObject>*, void*, mfc::CTypedPtrList<CEngineObject>::Node* /*prev*/,
    mfc::CTypedPtrList<CEngineObject>::Node* /*next*/) EI_ORIGINAL(0x00429EA0)
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
__declspec(naked) void __fastcall ScriptGroupConstruct(void*, void*) EI_ORIGINAL(0x004C2030)
__declspec(naked) void __fastcall ScriptGroupLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x004B3360)
inline void __fastcall ScriptGroupDestruct(void* a0, void*) { ScriptGroupDestructOriginal((void*)a0); }
// 0066EF80 / 0066D260 (RET 4). The player state's refresh after a load and SetFlag70.
__declspec(naked) void __fastcall PlayerStateRefresh(void*, void*) EI_ORIGINAL(0x0066EF80)
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
void __fastcall StringArraySetAtGrow(StringArray* a0, void*, int a2, const mfc::CString* a3) { StringArraySetAtGrowStringOriginal((void*)a0, nullptr, a2, a3); }
// 0050EA40 / 0050E9F0 (RET 4) / 005179E0 (RET 4). Map object: prepares the record array before
// Load, MarkChanged(part), CObject3D's animated figure setup for its figure name.
__declspec(naked) void __fastcall MapObjectBeginLoad(CEngineObject*, void*) EI_ORIGINAL(0x0050EA40)
__declspec(naked) void __fastcall MapObjectSetupFigure(CEngineObject*, void*, const char* /*figure*/)
    EI_ORIGINAL(0x005179E0)

} // namespace

void IdArray::SetSize(int n, int g) { DwordArraySetSizeOriginal(this, nullptr, n, g); }
__declspec(naked) void ObjectPtrArray::Construct() EI_ORIGINAL(0x005985C0)
__declspec(naked) void ObjectPtrArray::Destruct() EI_ORIGINAL(0x0059A100)
__declspec(naked) void SharedObject::CopyFrom(const SharedObject*) EI_ORIGINAL(0x0059B510)
__declspec(naked) void SharedObject::Load(WorldStream*) EI_ORIGINAL(0x00589CA0)
__declspec(naked) SharedObjectNode* SharedObjectList::NewNode(SharedObjectNode*, SharedObjectNode*) EI_ORIGINAL(0x0059CA60)
__declspec(naked) void DwordList::AddTail(std::uint32_t) EI_ORIGINAL(0x0059CC60)
__declspec(naked) void ObjectList::AddTail(CEngineObject*) EI_ORIGINAL(0x004C98F0)

namespace {

void* AIMap(CWorldServer* world) { return world->aiMap_; }
std::uint8_t& AIMapUpdates(CWorldServer* world) { return world->aiMap_[2]; }   // +0x172
void* SectorMap(CWorldServer* world) { return world->sectorMap_; }

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

// The sum keeps the original's order ((elapsed * secondsPerTick + offset) + gtime): no
// reassociation.
#pragma float_control(precise, on, push)
void CWorldServer::UpdatePlayerTimes(float offset) {
    const char* name = reinterpret_cast<const char*>(kGTimeName);
    int now = tick_;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next()) {
        int elapsed = static_cast<int>(static_cast<unsigned>(now) - static_cast<unsigned>(player->lastTick));
        player->lastTick = now;
        if (elapsed < 0) elapsed = 0;
        double current = VariableGet(player->variables, nullptr, name);
        float value = static_cast<float>(static_cast<double>(elapsed) * secondsPerTick_ + offset + current);
        PlayerStateSetVariable(At<void*>(player, kPlayerState), nullptr, name, value);
    }
}
#pragma float_control(pop)

// ---- LoadSavedMapObjects ----------------------------------------------------------------------

void CWorldServer::LoadSavedMapObjects(WorldStream* stream) {
    for (;;) {
        int id = ReadValue<int>(stream);
        if (id == -1) return;
        CEngineObject* object = ManagerFindObject(manager40_, nullptr, id);
        if (stream->version >= kVersionMapObjectName) {
            mfc::CString name;
            ReadStringInto(stream, name);
            ei::VCall<void>(object, kSlotSetSecondName, static_cast<const char*>(name));
        }
        MapObjectBeginLoad(object, nullptr);
        object->Load(stream);
        int changed = ReadValue<int>(stream);
        if (!object) continue;   // (the object was already used above)

        ++object->refCount_;
        UnitMarkChanged(object, nullptr, -1);
        if (object->type_ != 0x3B) {
            MapObjectSetupFigure(object, nullptr, At<const char*>(object, 0xA4));
            SectorMapAdd(AsSectorMap(SectorMap(this)), nullptr, object);
        }
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit)
            otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (*At<const char*>(object, 0x1A8) != '\0')
            namedObjects_.Set(static_cast<unsigned>(object->id_), object);

        if (object->GetClassId() == world::kClassUnit) {
            // The unit's six dwords at +0x4B0 come from its prototype's model record.
            char* prototypes = *reinterpret_cast<char**>(0x007AF358);
            char* prototypesEnd = *reinterpret_cast<char**>(0x007AF35C);
            unsigned index = static_cast<unsigned>(At<int>(object, 0x18));
            unsigned count = static_cast<unsigned>((prototypesEnd - prototypes) / 0x120);
            char* prototype = index < count ? prototypes + index * 0x120 : nullptr;
            char* models = *reinterpret_cast<char**>(0x007AF398);
            char* modelsEnd = *reinterpret_cast<char**>(0x007AF39C);
            unsigned model = At<unsigned>(prototype, 8);
            unsigned modelCount = static_cast<unsigned>((modelsEnd - models) / 0x168);
            char* record = model < modelCount ? models + model * 0x168 : nullptr;
            std::memcpy(&At<std::uint8_t>(object, 0x4B0), record + 0x2C, 24);
        }

        // Every part is marked changed when any change was saved (the original tests the part's
        // bit 1 << i itself, never `changed & bit`).
        int parts = At<int>(object, 0x1CC);
        for (int i = 0; i < parts; ++i)
            if (changed != 0 && (1u << (i & 31)) != 0) UnitMarkChanged(object, nullptr, i);

        if (object->GetClassId() == world::kClassUnit && At<std::uint8_t>(At<void*>(object, 0x240), 0x9B7) == 0)
            continue;   // a unit whose controller does not ask for updates
        AddToUpdateMap(object);
    }
}

// ---- LoadSave ---------------------------------------------------------------------------------

namespace {

// The units saved standing where they cannot move (a cell they cannot leave) are put on a free
// cell nearby when they are scripted units (text-key ids), otherwise removed.
void RelocateStrandedUnits(CWorldServer* world) {
    IdArray stranded;
    stranded.vtable = reinterpret_cast<void*>(kIdArrayVtable);
    stranded.data = nullptr;
    stranded.growBy = 0;
    stranded.capacity = 0;
    stranded.size = 0;
    for (auto* node = world->objects_.map.First(); node; node = world->objects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (object->GetClassId() != world::kClassUnit) continue;
        if (AIMapCanLeaveCell(AIMap(world), nullptr, object)) continue;
        int id = object->id_;
        int index = stranded.size;
        stranded.SetSize(index + 1, -1);
        stranded.data[index] = id;
    }
    for (int i = 0; i < stranded.size; ++i) {
        CEngineObject* unit = WorldFindUnit(world, nullptr, stranded.data[i]);
        if (unit && unit->id_ > kTextKeyIdMin) {
            SectorMapRemove(AsSectorMap(SectorMap(world)), nullptr, unit);
            Vector3& position = At<Vector3>(unit, 0x1C);
            Vector3 from;
            Vector3 to;
            std::memcpy(&from, &position, sizeof(Vector3));
            std::memcpy(&to, &position, sizeof(Vector3));
            AIMapFindFreeCellNear(AIMap(world), nullptr, unit, &from, &to);
            std::memcpy(&position, &to, sizeof(Vector3));
            SectorMapAdd(AsSectorMap(SectorMap(world)), nullptr, unit);
            ControllerReposition(At<void*>(unit, 0x240), nullptr);
        } else {
            WorldRemoveObject(world, nullptr, stranded.data[i]);
        }
    }
    stranded.vtable = reinterpret_cast<void*>(kIdArrayVtable);
    if (stranded.data) gamecrt::operator_delete(stranded.data);
}

// The players' part of the save (in the player list's order).
void LoadPlayers(CWorldServer* world, WorldStream* stream) {
    // The original keeps this iterator on the heap.
    auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
    if (it) it->list = nullptr;
    for (WorldPlayer* player = it->First(ei::VCall<CEngineObject*>(world, 20)); player; player = it->Next()) {
        void* state = At<void*>(player, kPlayerState);
        stream->Read(&At<std::uint8_t>(player, kPlayerFC), 4);
        PlayerRecordsLoad(&At<std::uint8_t>(player, kPlayerRecords), nullptr, stream, 1);
        player->lastTick = world->tick_;
        stream->Read(&At<std::uint8_t>(player, kPlayer100), 4);
        CEngineObject* items = ei::VCall<CEngineObject*>(world, 18);
        for (;;) {
            int id = ReadValue<int>(stream);
            if (id == -1) break;
            CEngineObject* object = ManagerFindObject(items, nullptr, id);
            if (!object) continue;
            At<ObjectList>(player, kPlayerObjects).AddTail(object);
            ++object->refCount_;
        }
        stream->Read(&At<std::uint8_t>(player, kPlayer108), 1);
        if (stream->version >= kVersionPlayerLists) {
            auto& ids = At<DwordList>(player, kPlayerIds);
            int count = ReadValue<int>(stream);
            for (int i = 0; i < count; ++i) ids.AddTail(0);
            for (auto* node = ids.head; node;) {
                auto* next = node->next;
                stream->Read(&node->value, 4);
                node = next;
            }
        }
        if (stream->version < kVersionPlayerByte) ReadValue<std::uint8_t>(stream);
        if (stream->version < kVersionPlayerGroup) {
            alignas(4) std::uint8_t group[0x14];
            ScriptGroupConstruct(group, nullptr);
            *reinterpret_cast<void**>(group) = reinterpret_cast<void*>(kScriptGroupVtable);
            ScriptGroupLoad(group, nullptr, stream);
            ScriptGroupDestruct(group, nullptr);
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
    if (ei::VCall<CEngineObject*>(this, 20)) {
        PlayerIterator it;
        for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next()) {
            gameResult_ = 1;
            unknown160_ = 1;
            PlayerClear(player, nullptr);
        }
    }
    AIMapBeginRefresh(AIMap(this), nullptr);
    loaded_ = 0;
    AIMapUpdates(this) = 0;

    ReadStringInto(stream, mobName);
    ReadStringInto(stream, mapName);
    ReadStringInto(stream, zoneName);
    if (map) mapName = map;
    *reinterpret_cast<mfc::CString*>(&zoneName_) = zoneName;
    PickLoadingMovie(this, nullptr, zoneName);
    ShowLoadProgress(kProgressStart);
    {
        PlayerIterator it;
        for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next())
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
    if (stream->version >= kVersionTimeSpeedFloat) {
        stream->Read(&speed, 4);
        if (stream->version >= kVersionUnknown13C) stream->Read(&unknown13C_, 4);
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
            result = game::FatalError(reinterpret_cast<const char*>(kCantLoadMap));
        } else {
            // The map objects' identities: id, type and +0x18 (created by the manager at +0x40).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id == -1) break;
                int type = ReadValue<int>(stream);
                std::uint32_t value18 = ReadValue<std::uint32_t>(stream);
                void* object = ei::VCall<void*>(manager40_, kSlotCreateObject, type, id);
                At<std::uint32_t>(object, 0x18) = value18;
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
            *reinterpret_cast<CWorldServer**>(0x007C84EC) = this;
            for (;;) {
                std::uint8_t more = ReadValue<std::uint8_t>(stream);
                if (!more) break;
                void* memory = gamecrt::operator_new(0xA8);
                At<std::uint32_t>(memory, 4) = 0;   // written before the null test
                Projectile* missile = memory ? ProjectileConstruct(memory, nullptr) : nullptr;
                ProjectileLoad(missile, nullptr, stream);
                auto& list = projectiles_.list;
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
            if (stream->version >= kVersionCountdowns) CountdownsLoad(countdowns_, nullptr, stream);

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

            if (ei::VCall<CEngineObject*>(this, 20)) LoadPlayers(this, stream);
            stream->Read(&unknown6C_, 1);

            // The item manager's object records.
            if (stream->version >= kVersionPlayerLists) {
                for (;;) {
                    int id = -1;
                    stream->Read(&id, 4);
                    if (id == -1) break;
                    std::uint32_t value = ReadValue<std::uint32_t>(stream);
                    CEngineObject* object = ManagerFindObject(ei::VCall<CEngineObject*>(this, 18), nullptr, id);
                    if (!object) continue;
                    auto* record = static_cast<ObjectRecord*>(gamecrt::operator_new(sizeof(ObjectRecord)));
                    At<std::uint32_t>(record, 4) = 0;   // written before the null test
                    if (record) record->vtable = reinterpret_cast<void*>(kObjectRecordVtable);
                    record->value = value;
                    void* old = At<void*>(object, 0x1C);
                    At<void*>(object, 0x1C) = record;
                    if (record) ++record->refs;
                    if (old) RefRelease(old, nullptr);
                }
            }

            ShowLoadProgress(kProgressDone);
            *reinterpret_cast<mfc::CString*>(&zoneName_) = zoneName;
            ei::VCall<void>(this, kSlotSetTimeSpeed, speed);
            ei::VCall<void>(this, kSlotSetTime, time);
            loaded_ = 1;
            AIMapEndRefresh(AIMap(this), nullptr);
            AIMapUpdates(this) = 1;

            RelocateStrandedUnits(this);

            if (stream->version >= kVersionMobFiles) {
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

            if (stream->version >= kVersionSharedObjects) {
                SharedObjectList& shared = SharedObjects(this);
                int count = ReadValue<int>(stream);
                for (int i = 0; i < count; ++i) {
                    // The element is copied from a local whose first three dwords the original
                    // leaves unset; Load overwrites them.
                    SharedObject empty;
                    empty.objectId = 0;
                    empty.player = 0;
                    empty.unknown_08 = 0;
                    empty.objects.Construct();
                    SharedObjectNode* node = shared.NewNode(shared.tail, nullptr);
                    node->value.CopyFrom(&empty);
                    if (shared.tail) shared.tail->next = node;
                    else shared.head = node;
                    shared.tail = node;
                    empty.objects.Destruct();
                }
                for (SharedObjectNode* node = shared.head; node;) {
                    SharedObjectNode* next = node->next;
                    node->value.Load(stream);
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
    for (WorldPlayer* player = it->First(ei::VCall<CEngineObject*>(world, 20)); player; player = it->Next()) {
        stream->Write(&At<std::uint8_t>(player, kPlayerFC), 4);
        PlayerRecordsSave(&At<std::uint8_t>(player, kPlayerRecords), nullptr, stream, 1);
        stream->Write(&At<std::uint8_t>(player, kPlayer100), 4);
        for (auto* node = At<ObjectList>(player, kPlayerObjects).head; node;) {
            auto* next = node->next;
            WriteCopy(stream, node->object->id_);
            node = next;
        }
        WriteCopy(stream, -1);
        WriteCopy(stream, At<std::uint8_t>(player, kPlayer108));
        auto& ids = At<DwordList>(player, kPlayerIds);
        WriteCopy(stream, ids.count);
        for (auto* node = ids.head; node;) {
            auto* next = node->next;
            stream->Write(&node->value, 4);
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
    AILayerSave(&At<std::uint8_t>(this, 0x2468), nullptr, &current);
    StreamWriteDiff(stream, nullptr, &base, &current);

    // The map objects' identities (the walk stops at the first empty entry).
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        WriteCopy(stream, object->id_);
        WriteCopy(stream, object->type_);
        WriteCopy(stream, At<std::uint32_t>(object, 0x18));
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

    for (auto* node = projectiles_.list.head; node;) {
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

    if (ei::VCall<CEngineObject*>(this, 20)) SavePlayers(this, stream);
    WriteCopy(stream, unknown6C_);

    // The item manager's object records.
    {
        PlayerIterator it;
        for (auto* item = reinterpret_cast<CEngineObject*>(it.First(ei::VCall<CEngineObject*>(this, 18))); item;
             item = reinterpret_cast<CEngineObject*>(it.Next())) {
            auto* record = At<ObjectRecord*>(item, 0x1C);
            if (!record) continue;
            WriteCopy(stream, item->id_);
            WriteCopy(stream, record->value);
        }
        WriteCopy(stream, -1);
    }

    WriteCopy(stream, mobFiles_.size);
    for (int i = 0; i < mobFiles_.size; ++i) WriteString(stream, mobFiles_.data[i]);

    SharedObjectList& shared = SharedObjects(this);
    WriteCopy(stream, shared.count);
    for (SharedObjectNode* node = shared.head; node;) {
        SharedObjectNode* next = node->next;
        WriteCopy(stream, node->value.objectId);
        WriteCopy(stream, node->value.player);
        WriteCopy(stream, node->value.unknown_08);
        int count = node->value.objects.size;
        WriteCopy(stream, count);
        for (int i = 0; i < count; ++i) WriteCopy(stream, node->value.objects.data[i]->id_);
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

// 0041A2E0 (RET 4). The game server session's player record with +0x2C == id, or null.
__declspec(naked) std::uint8_t* __fastcall SessionFindPlayer(void*, void*, int) EI_ORIGINAL(0x0041A2E0)
// 0059B8F0 (RET 4). CArray<CRefPtr>::Copy.
__declspec(naked) void __fastcall ObjectPtrArrayCopy(ObjectPtrArray*, void*, const ObjectPtrArray*) EI_ORIGINAL(0x0059B8F0)
// 0059C8C0 (RET 8). CArray<CRefPtr>::SetSize(size, growBy); the same code the original inlines
// in LoadSharedObject.
__declspec(naked) void __fastcall ObjectPtrArraySetSize(ObjectPtrArray*, void*, int, int) EI_ORIGINAL(0x0059C8C0)
// 0059C890 (RET 8). Releases `count` counted pointers from `data` (CArray DestructElements).
__declspec(naked) void __stdcall CountedRangeRelease(CEngineObject**, int) EI_ORIGINAL(0x0059C890)
// 00403F40 (RET 4). The item manager's (007AFFB4) object by id, or null.

constexpr std::uintptr_t kObjectPtrArrayVtable = 0x0073FAC8;
constexpr std::uintptr_t kObjectPtrArrayBaseVtable = 0x0073FABC;   // set by the destructor
constexpr std::uintptr_t kItemManager = 0x007AFFB4;                 // pointer

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
        CEngineObject* object = ManagerFindObject(*reinterpret_cast<void**>(kItemManager), nullptr, id);
        if (object) Append(entry->objects, object);   // the original inlines the SetSize
    }
}

void CWorldServer::AddSharedObject(int id, int /*unused*/, std::uint32_t value, void* objectsArg) {
    // The entry's player: the session player record's +0x30 in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::GameServer();
    if (session && !(session->flags7_ & CRefCounted::kDetached))
        if (std::uint8_t* record = SessionFindPlayer(session, nullptr, id)) player = At<int>(record, 0x30);

    // AddHead(SharedObject()): the new head copies an empty local (whose first three dwords the
    // original leaves unset; they are overwritten below).
    SharedObjectList& shared = SharedObjects(this);
    ObjectPtrArray empty{reinterpret_cast<void*>(kObjectPtrArrayVtable), nullptr, 0, 0, 0};
    SharedObjectNode* node = shared.NewNode(nullptr, shared.head);
    ObjectPtrArrayCopy(&node->value.objects, nullptr, &empty);
    if (shared.head) shared.head->prev = node;
    else shared.tail = node;
    shared.head = node;
    empty.vtable = reinterpret_cast<void*>(kObjectPtrArrayBaseVtable);
    if (empty.data) {
        CountedRangeRelease(empty.data, empty.size);
        gamecrt::operator_delete(empty.data);
    }

    SharedObject& entry = shared.head->value;
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
__declspec(naked) void __fastcall StatsRefillSlots(void*, void*) EI_ORIGINAL(0x00525AF0)

constexpr int kSlotUnitDead = 55;         // unit slot 55 (+0xDC): non-zero when dead
constexpr int kSlotControllerReset = 19;  // controller slot 19 (+0x4C), called with 0

} // namespace

void CWorldServer::PrepareSave() {
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CEngineObject* unit = node->value;
        if (unit->GetClassId() != world::kClassUnit || ei::VCall<int>(unit, kSlotUnitDead)) continue;
        void* controller = At<void*>(unit, 0x240);
        StatsRefillSlots(static_cast<std::uint8_t*>(controller) + 0x1E0, nullptr);
        ei::VCall<void>(controller, kSlotControllerReset, 0);
    }
}

EI_REPLACE_METHOD(0x005956E0, CWorldServer::PrepareSave);
EI_REPLACE(0x00589CA0, LoadSharedObject);
EI_REPLACE_METHOD(0x005947E0, CWorldServer::AddSharedObject);
EI_REPLACE_METHOD(0x00596E70, CWorldServer::LoadSave);
EI_REPLACE_METHOD(0x00597BD0, CWorldServer::SaveWorld);
EI_REPLACE_METHOD(0x00596900, CWorldServer::LoadSavedMapObjects);
EI_REPLACE_METHOD(0x0058D280, CWorldServer::UpdatePlayerTimes);
