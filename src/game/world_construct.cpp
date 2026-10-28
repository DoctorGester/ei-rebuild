// The two worlds (g_serverWorld, g_clientWorld) and their constructors: CWorldServer and CWorldClient. Only
// their static initialisers (defined here on the names) call them. The members with C++ constructors (the
// strings, the zone header) are the compiler's; the others' constructors are written out in the original's
// order (the inlined ones) or called (the out-of-line ones).
#include "world.h"

#include "ai_map.h"
#include "core_streams.h"
#include "effects.h"
#include "ground_print.h"
#include "object_diplomacy.h"
#include "registry.h"
#include "world_class_slots.h"
#include "script.h"

#include "lib/mfc_slots.h"
#include "lib/static_object.h"
#include "game/game_slots.h"

#include <initializer_list>
#include <new>

namespace {

// The MFC lists' block size (CList(nBlockSize = 10)).
constexpr int kListBlockSize = 10;
// The client's flags after construction.
constexpr std::uint32_t kClientFlags = 0x3DC0;
constexpr std::uint32_t kClientDebugFlags = 0xB;
constexpr int kClientConstant228 = 10;

// CList's constructor as inlined: the instantiation's vtable, empty, no blocks.
template <typename T>
void ConstructList(mfc::CList<T>& list, void* vtable) {
    list.vtable = vtable;
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = kListBlockSize;
}

// The ground print lists' sentinels come from operator new (their nodes are larger than the node
// allocator's 128 bytes).
static_assert(sizeof(stlp::list<GroundPrint>::Node) > 128);

} // namespace

void ObjectIdMap::Construct() {
    map.InitBuckets(100);
    maxId = 0;
}

PlatformSet::PlatformSet() {
    ClearFields();
    ConstructList(list, mfc::impl::PlatformListClass::Vtable());
}


CWorldServer::CWorldServer() {
    exits_.Construct(mfc::impl::MapRecordItemsClass::Vtable());
    updateMap_.Construct();
    Messages()->Construct();
    MainScript()->Construct();
    SecondScript()->Construct();
    diplomacy_.Reset();
    for (MapObjectIdMap* map : {&objects_, &sentObjects_, &mapObjects_, &otherMapObjects_, &namedObjects_})
        map->Construct();
    timedObjects_.Construct();
    ConstructList(projectiles_, gameslots::RefListBClass::Vtable());
    ConstructList(counted_, gameslots::ItemObjectListClass::Vtable());
    ConstructList(sharedObjects_, gameslots::SharedObjectListClass::Vtable());
    ResetState();
}

CWorldClient::CWorldClient() {
    exits_.Construct(mfc::impl::MapRecordItemsClass::Vtable());
    countedA0_.Construct(gameslots::ClientObjectPtrArrayClass::Vtable());
    heroes_.Construct();
    objects_.Construct();
    ConstructList(dynamicObjects_, mfc::impl::ClientObjectListClass::Vtable());
    dynamicNodes_.InitBuckets(100);
    dynamicMaxId_ = 0;
    lineLists_.InitBuckets(100);
    lastLineListKey_ = 0;
    // Both sector grids at 1 x 1 (Create makes them 4 x 4).
    objectGrid_.unknown08_ = 1;
    objectGrid_.unknown0C_ = 1;
    objectGrid_.Allocate();
    secondGrid_.unknown08_ = 1;
    secondGrid_.unknown0C_ = 1;
    secondGrid_.Allocate();
    bloodPrints_.Construct();
    footprints_.Construct();
    scorchMarks_.Construct();
    handle_.ptr = nullptr;
    flags_ |= kClientFlags;
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
    bird_ = nullptr;
    for (int& value : constants7C4_) value = 0;
    cursor_ = nullptr;
    appName_ = "EvilIslands";
    windowClass_ = "EvilIslandsClass";
    windowTitle_ = "EvilIslands";
    pausedTitle_ = "EvilIslands Paused";
    constant6E8_ = 0;
}

// ---- The two worlds -------------------------------------------------------------------------------

// Zero at the start: built by their static initialisers in the list's order and destroyed by the
// exit functions those register; all defined on the names
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CWorldServer> g_serverWorld = /*007AFF60*/ {};
constinit lib::StaticObject<CWorldClient> g_clientWorld = /*007AF5B8*/ {};
CWorldServer* TheServerWorld() { return &*g_serverWorld; }
CWorldClient* TheClientWorld() { return &*g_clientWorld; }

namespace {
void __cdecl DestroyServerWorld() { g_serverWorld.Destroy(); }
void __cdecl DestroyClientWorld() { g_clientWorld.Destroy(); }
} // namespace

void __cdecl ConstructServerWorld() {
    g_serverWorld.Construct();
    libcrt::AtExit(DestroyServerWorld);
}
void __cdecl ConstructClientWorld() {
    g_clientWorld.Construct();
    libcrt::AtExit(DestroyClientWorld);
}
