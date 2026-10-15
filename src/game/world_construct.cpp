// The two worlds' compiler-generated constructors (S3b): CWorldServer 00589ED0 (the global at
// 007AFF60) and CWorldClient 005612F0 (007AF5B8). Only their static initialisers call them, before
// the hooks are installed, so the hook build checks them at start-up against what the originals
// built (tests/verify/verify_global_constructors.cpp); the standalone image runs them. Each is the
// base constructor, then the members' constructors in the original's order (the inlined ones written
// out, the out-of-line ones the game's own registered rewrites), the vtable, and the fields set
// after it.
#include "world.h"

#include "ai_map.h"
#include "core_streams.h"
#include "effects.h"
#include "ground_print.h"
#include "object_diplomacy.h"
#include "originals_world.h"
#include "registry.h"
#include "script.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <initializer_list>
#include <new>

namespace {

// The instantiations' vtables the constructors write.
constexpr std::uintptr_t kZoneExitArrayVtable = std::uintptr_t(0x0073C400);     // CArray<ZoneExit> (both worlds)
constexpr std::uintptr_t kServerVtable = std::uintptr_t(0x0073F928);
constexpr std::uintptr_t kProjectileListVtable = std::uintptr_t(0x0073F9D0);
constexpr std::uintptr_t kCountedListVtable = std::uintptr_t(0x0073BE74);
constexpr std::uintptr_t kSharedObjectListVtable = std::uintptr_t(0x0073FA10);
constexpr std::uintptr_t kClientVtable = std::uintptr_t(0x0073F41C);
constexpr std::uintptr_t kCountedArrayVtable = std::uintptr_t(0x0073F53C);     // the client's +0xA0
constexpr std::uintptr_t kPlatformListVtable = std::uintptr_t(0x0073F548);
constexpr std::uintptr_t kPlatformSetVtable = std::uintptr_t(0x0073F4FC);       // CBridgeManager
constexpr std::uintptr_t kByteArrayVtable = std::uintptr_t(0x00746560);         // CByteArray
constexpr std::uintptr_t kWordArrayVtable = std::uintptr_t(0x00745F78);         // CWordArray

// The MFC lists' block size (CList(nBlockSize = 10)).
constexpr int kListBlockSize = 10;
// The client's flags after construction.
constexpr std::uint32_t kClientFlags = 0x3DC0;
constexpr std::uint32_t kClientDebugFlags = 0xB;
constexpr int kClientConstant228 = 10;

// CList's constructor as inlined (or 0059A340 / 0056FD70): the instantiation's vtable, empty, no
// blocks.
template <typename T>
void ConstructList(mfc::CList<T>& list, std::uintptr_t vtable) {
    list.vtable = reinterpret_cast<void*>(vtable);
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = kListBlockSize;
}

// An engine object's fields as its (inlined) constructor sets them, with the class's vtable.
void ConstructEngineObject(CEngineObject* object, std::uintptr_t vtable) {
    object->ClearFields();
    ei::SetVtable(object, vtable);
}

// The ground print lists' sentinels come from operator new (their nodes are larger than the node
// allocator's 128 bytes): 0xA4, 0x9C and 0x94 bytes.
static_assert(sizeof(stlp::list<BloodPrint>::Node) == 0xA4);
static_assert(sizeof(stlp::list<Footprint>::Node) == 0x9C);
static_assert(sizeof(stlp::list<GroundPrint>::Node) == 0x94);

} // namespace

void ObjectIdMap::Construct() {
    map.InitBuckets(100);
    maxId = 0;
}

// ---- CWorldServer (00589ED0) ------------------------------------------------------------------------

CWorldServer* CWorldServer::Construct() {
    ConstructBase();
    reinterpret_cast<ZoneExitArray*>(&exitsVtable_)->Construct(kZoneExitArrayVtable);
    updateMap_.Construct();
    new (&zoneName_) mfc::CString();
    mobFiles_.Construct();
    Messages()->Construct();
    ConstructAIMap(AIMap());
    MainScript()->Construct();
    SecondScript()->Construct();
    diplomacy_.Reset();
    new (&mobName_) mfc::CString();
    new (&mapName_) mfc::CString();
    for (ObjectIdMap* map : {&objects_, &sentObjects_, &mapObjects_, &otherMapObjects_, &namedObjects_, &timedObjects_})
        map->Construct();
    zoneHeader_.ConstructQueueStream();
    ConstructList(projectiles_, kProjectileListVtable);
    ConstructList(counted_, kCountedListVtable);
    ConstructList(sharedObjects_, kSharedObjectListVtable);
    ei::SetVtable(this, kServerVtable);
    ResetState();
    return this;
}
EI_REPLACE_METHOD(0x00589ED0, CWorldServer::Construct);

// ---- CWorldClient (005612F0) ------------------------------------------------------------------------

CWorldClient* CWorldClient::Construct() {
    ConstructBase();
    reinterpret_cast<ZoneExitArray*>(&exitsVtable_)->Construct(kZoneExitArrayVtable);
    countedA0_.Construct(kCountedArrayVtable);
    heroes_.Construct();
    objects_.Construct();
    ConstructList(dynamicObjects_, reinterpret_cast<std::uintptr_t>(ObjectPtrListVtable()));
    dynamicNodes_.InitBuckets(100);
    dynamicMaxId_ = 0;
    screenEntries_.Construct();
    hits_ = {};
    lineLists_.InitBuckets(100);
    lastLineListKey_ = 0;
    // Both sector grids at 1 x 1 (Setup makes them 4 x 4).
    objectGrid_.unknown08_ = 1;
    objectGrid_.unknown0C_ = 1;
    objectGrid_.Allocate();
    secondGrid_.unknown08_ = 1;
    secondGrid_.unknown0C_ = 1;
    secondGrid_.Allocate();
    new (&zoneName_) mfc::CString();
    bloodPrints_.Construct();
    footprints_.Construct();
    scorchMarks_.Construct();
    handle_.ptr = nullptr;

    ConstructEngineObject(platforms_.Engine(), kPlatformSetVtable);
    ConstructList(platforms_.list, kPlatformListVtable);
    new (&texturePath_) mfc::CString();

    CRainSnow* rain = RainSnow();
    ConstructEngineObject(rain, CRainSnow::kRainSnowVtable);
    rain->texture = nullptr;
    rain->projected.Construct();
    rain->positions.Construct();
    rain->outcodes.Construct(kByteArrayVtable);
    rain->frames.Construct(kByteArrayVtable);
    rain->vertices.Construct();
    rain->indices.Construct(kWordArrayVtable);
    rain->offsets.Construct();
    rain->world = nullptr;
    rain->sound = 0;
    rain->Clear();

    list7BC_.Construct();
    for (mfc::CString* name : {&appName_, &windowClass_, &windowTitle_, &pausedTitle_}) new (name) mfc::CString();
    flags_ |= kClientFlags;
    ei::SetVtable(this, kClientVtable);
    debugFlags_ = kClientDebugFlags;
    worldValue_ = -1;
    camera_ = nullptr;
    constant228_ = kClientConstant228;
    manager188_ = nullptr;
    particleManager_ = nullptr;
    textureManager_ = nullptr;
    manager190_ = nullptr;
    manager194_ = nullptr;
    manager198_ = nullptr;
    listManager_ = nullptr;
    arrayManager_ = nullptr;
    sounds_ = nullptr;
    texts_ = nullptr;
    terrain_ = nullptr;
    session_ = nullptr;
    skyDome_ = nullptr;
    owned1B0_ = nullptr;
    for (int& value : constants7C4_) value = 0;
    cursor_ = nullptr;
    appName_ = "EvilIslands";
    windowClass_ = "EvilIslandsClass";
    windowTitle_ = "EvilIslands";
    pausedTitle_ = "EvilIslands Paused";
    constant6E8_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x005612F0, CWorldClient::Construct);
