// CBaseManager (00402B20-004048F0) and the object list iterator. See base_manager.h and
// docs/notes/core.md.
#include "base_manager.h"
#include "hash_tables_400.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"   // ei::SetVtable
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "lib/mfc_list.h"

namespace {

using ObjectMap = CBaseManager::ObjectMap;
using ObjectList = CBaseManager::ObjectList;
using ObjectNode = ObjectList::Node;
using PositionMap = CBaseManager::PositionMap;
using SpareMap = CBaseManager::SpareMap;
using SpareCountMap = CBaseManager::SpareCountMap;

// The tables' iterator and {key, value} pair, as stlp::hash_map lays them out.
template <typename Map>
using MapIterator = typename Map::Iterator;
using ObjectPair = ObjectMap::Pair;
using CountPair = SpareCountMap::Pair;
// Destroy's list<int> of ids: the sentinel node of a circular list.
using IdList = stlp::list<int>;
using IdNode = IdList::Node;

// The manager's object lists (objectList_ and the spare lists) are CTypedPtrLists with this vtable.
// Id written after the last object by SaveObjects; an object with this id is followed by a 1 byte.
constexpr int kEndOfObjects = -0x521;
// 00783C00: the fatal "out of ids" message ("CBaseManager::RegisterInstant ...").
const char* OutOfIdsMessage() { return reinterpret_cast<const char*>(0x00783C00); }

// --- STLport and MFC instantiations of the containers (library) ---------------------------------

// Constructors: the objects and spares tables' (n buckets); the pieces of the positions and spare
// counts tables, which the constructor builds by hand (bucket vector, count proxy, resize).
__declspec(naked) void* __fastcall ObjectsConstruct(ObjectMap*, void*, std::size_t /*n*/, const void* /*hash*/, const void* /*equal*/, const void* /*alloc*/) EI_ORIGINAL(0x00405820)
__declspec(naked) void* __fastcall SparesConstruct(SpareMap*, void*, std::size_t /*n*/, const void*, const void*, const void*) EI_ORIGINAL(0x00405C40)
__declspec(naked) void* __fastcall PositionBucketsConstruct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00406240)
__declspec(naked) void* __fastcall PositionCountConstruct(void* /*proxy*/, void*, const void* /*alloc*/, const void* /*value*/) EI_ORIGINAL(0x00406260)
__declspec(naked) void __fastcall PositionsResize(PositionMap*, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00405EB0)
__declspec(naked) void* __fastcall SpareCountBucketsConstruct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00406530)
__declspec(naked) void* __fastcall SpareCountCountConstruct(void* /*proxy*/, void*, const void* /*alloc*/, const void* /*value*/) EI_ORIGINAL(0x00406550)
__declspec(naked) void __fastcall SpareCountsResize(SpareCountMap*, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00406300)
// Node deletion and bucket vector destruction (the destructor's).
__declspec(naked) void __fastcall SpareCountsDeleteNode(SpareCountMap*, void*, SpareCountMap::Node*) EI_ORIGINAL(0x00407700)
__declspec(naked) void __fastcall SparesDeleteNode(SpareMap*, void*, SpareMap::Node*) EI_ORIGINAL(0x00407640)
__declspec(naked) void __fastcall PositionsDeleteNode(PositionMap*, void*, PositionMap::Node*) EI_ORIGINAL(0x00407580)
__declspec(naked) void __fastcall ObjectsDeleteNode(ObjectMap*, void*, ObjectMap::Node*) EI_ORIGINAL(0x004074B0)
__declspec(naked) void __fastcall SpareCountBucketsDestroy(void* /*vector*/) EI_ORIGINAL(0x00405E00)
__declspec(naked) void __fastcall SpareBucketsDestroy(void* /*vector*/) EI_ORIGINAL(0x00405DB0)
__declspec(naked) void __fastcall PositionBucketsDestroy(void* /*vector*/) EI_ORIGINAL(0x00405BF0)
__declspec(naked) void __fastcall ObjectBucketsDestroy(void* /*vector*/) EI_ORIGINAL(0x00405E50)
// The objects table's iteration (begin, ++), find (the value's address or null) and clear; the
// other tables' clear; resize (Create's sizes).
__declspec(naked) MapIterator<ObjectMap>* __fastcall ObjectsBegin(ObjectMap*, void*, MapIterator<ObjectMap>* /*out*/) EI_ORIGINAL(0x00404C30)
__declspec(naked) MapIterator<ObjectMap>* __fastcall ObjectsNext(MapIterator<ObjectMap>*) EI_ORIGINAL(0x00404980)
__declspec(naked) CEngineObject** __fastcall ObjectsFind(ObjectMap*, void*, const int* /*key*/) EI_ORIGINAL(0x00406950)
__declspec(naked) void __fastcall ObjectsClear(ObjectMap*) EI_ORIGINAL(0x004067B0)
void __fastcall PositionsClear(PositionMap* a0) { HashClearOriginal((void*)a0); }
__declspec(naked) void __fastcall SparesClear(SpareMap*) EI_ORIGINAL(0x00407050)
__declspec(naked) void __fastcall SpareCountsClear(SpareCountMap*) EI_ORIGINAL(0x00407220)
void __fastcall ObjectsResize(ObjectMap* a0, void*, std::size_t a2) { ObjectsResizeOriginal((void*)a0, nullptr, (std::uint32_t)a2); }
void __fastcall PositionsResizeBig(PositionMap* a0, void*, std::size_t a2) { PositionsResizeOriginal((void*)a0, nullptr, (std::uint32_t)a2); }
__declspec(naked) void __fastcall SparesResize(SpareMap*, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00406EE0)
void __fastcall SpareCountsResizeBig(SpareCountMap* a0, void*, std::size_t a2) { SpareCountsResizeOriginal((void*)a0, nullptr, (std::uint32_t)a2); }
// Node deallocation through a table's count proxy (its node allocator).
__declspec(naked) void __fastcall SparesFreeNode(void* /*proxy*/, void*, SpareMap::Node*, int /*n*/) EI_ORIGINAL(0x00407DA0)
__declspec(naked) void __fastcall SpareCountsFreeNode(void* /*proxy*/, void*, SpareCountMap::Node*, int /*n*/) EI_ORIGINAL(0x00408020)
// Destroy's list<int>: constructor, end, insert, node deallocation, clear.
IdList* __fastcall IdsConstruct(IdList* a0, void*, const void* a2) { return (IdList*)ListConstruct404A70Original(a0, nullptr, a2); }
IdNode** __fastcall IdsEnd(IdList* a0, void*, IdNode** a2) { return (IdNode**)ListEnd4049D0Original(a0, nullptr, (void**)a2); }
IdNode** __fastcall IdsInsert(IdList* a0, void*, IdNode** a2, IdNode* a3, const int* a4) { return (IdNode**)ListInsert404A40Original(a0, nullptr, (void**)a2, a3, a4); }
__declspec(naked) void __fastcall IdsFreeNode(IdList*, void*, IdNode*, int /*n*/) EI_ORIGINAL(0x00404D10)
__declspec(naked) void __fastcall IdsClear(IdList*) EI_ORIGINAL(0x00405500)
// The positions table: find, erase(iterator), operator[] (two copies), remove by key (returning the
// value).
__declspec(naked) MapIterator<PositionMap>* __fastcall PositionsFind(PositionMap*, void*, MapIterator<PositionMap>* /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405BB0)
__declspec(naked) ObjectNode** __fastcall PositionAt2(PositionMap*, void*, const int* /*key*/) EI_ORIGINAL(0x004056A0)
// The objects table: finds into an iterator (three copies), MFC-style Lookup / SetAt, operator[],
// remove by key, erase(iterator), find-or-insert of a pair.
__declspec(naked) MapIterator<ObjectMap>* __fastcall ObjectsFindIt(ObjectMap*, void*, MapIterator<ObjectMap>* /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405560)
__declspec(naked) MapIterator<ObjectMap>* __fastcall ObjectsFindIt2(ObjectMap*, void*, MapIterator<ObjectMap>* /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405B20)
__declspec(naked) MapIterator<ObjectMap>* __fastcall ObjectsFindIt3(ObjectMap*, void*, MapIterator<ObjectMap>* /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405AE0)
__declspec(naked) int __fastcall ObjectsLookup(ObjectMap*, void*, const int* /*key*/, CEngineObject** /*value*/) EI_ORIGINAL(0x00404F80)
__declspec(naked) void __fastcall ObjectsSetAt(ObjectMap*, void*, const int* /*key*/, CEngineObject* const* /*value*/) EI_ORIGINAL(0x00404E80)
__declspec(naked) CEngineObject** __fastcall ObjectsAt(ObjectMap*, void*, const int* /*key*/) EI_ORIGINAL(0x004055A0)
__declspec(naked) CEngineObject* __fastcall ObjectsRemoveKey(ObjectMap*, void*, const int* /*key*/) EI_ORIGINAL(0x00404F20)
__declspec(naked) ObjectPair* __fastcall ObjectsFindOrInsert(ObjectMap*, void*, const ObjectPair*) EI_ORIGINAL(0x004065D0)
// The spares table: finds into an iterator (two copies), operator[], begin, the next non-empty
// bucket after a node (returns its head).
__declspec(naked) MapIterator<SpareMap>* __fastcall SparesFind(SpareMap*, void*, MapIterator<SpareMap>* /*out*/, const int* /*key*/) EI_ORIGINAL(0x004057A0)
__declspec(naked) MapIterator<SpareMap>* __fastcall SparesFind2(SpareMap*, void*, MapIterator<SpareMap>* /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405D70)
__declspec(naked) ObjectList** __fastcall SparesAt(SpareMap*, void*, const int* /*key*/) EI_ORIGINAL(0x004075C0)
__declspec(naked) MapIterator<SpareMap>* __fastcall SparesBegin(SpareMap*, void*, MapIterator<SpareMap>* /*out*/) EI_ORIGINAL(0x00406280)
__declspec(naked) SpareMap::Node* __fastcall SparesNextBucket(MapIterator<SpareMap>*) EI_ORIGINAL(0x004073C0)
// The spare counts table: operator[] (two copies), make_pair, insert of a pair.
__declspec(naked) int* __fastcall SpareCountAt(SpareCountMap*, void*, const int* /*key*/) EI_ORIGINAL(0x00406AA0)
__declspec(naked) int* __fastcall SpareCountAt2(SpareCountMap*, void*, const int* /*key*/) EI_ORIGINAL(0x00407680)
__declspec(naked) CountPair* __fastcall MakeCountPair(CountPair*, void*, const int* /*key*/, const int* /*value*/) EI_ORIGINAL(0x00407DE0)
__declspec(naked) CountPair* __fastcall SpareCountInsert(SpareCountMap*, void*, const CountPair*) EI_ORIGINAL(0x004085E0)

// --- Helpers ------------------------------------------------------------------------------------

// The positions and spare counts tables are built by hand (the hashtable constructor inlined):
// functors, empty bucket vector, count 0, 100 buckets; then the table's highest key is zeroed. The
// original copies the three empty functor bytes from an uninitialised stack byte; they are never
// read, so they are zeroed here.
template <typename Map, typename Buckets, typename Count, typename Resize>
void BuildTable(Map& table, int& maxKey, Buckets buckets, Count count, Resize resize) {
    char tag = 0;
    std::uint8_t* functors = table.Functors();
    functors[0] = 0;
    functors[1] = 0;
    functors[2] = 0;
    buckets(table.BucketVector(), nullptr, &tag);
    std::uint32_t zero = 0;
    count(table.CountProxy(), nullptr, &tag, &zero);
    resize(&table, nullptr, 100);
    maxKey = 0;
}

// A table's destruction: every node deleted, then its bucket vector destroyed.
template <typename Map, typename DeleteNode, typename DestroyBuckets>
void DestroyTable(Map& table, DeleteNode deleteNode, DestroyBuckets destroyBuckets) {
    table.ClearWith([&table, deleteNode](typename Map::Node* node) { deleteNode(&table, nullptr, node); });
    destroyBuckets(table.BucketVector());
}

// Create's reset of a table: emptied (each node freed by `freeNode`), its highest key zeroed, sized
// for `size` elements, the highest key zeroed again.
template <typename Map, typename FreeNode, typename Resize>
void ResetTable(Map& table, int& maxKey, FreeNode freeNode, Resize resize, std::size_t size) {
    table.ClearWith(freeNode);
    maxKey = 0;
    resize(&table, nullptr, size);
    maxKey = 0;
}

// The objects table's walk as Destroy does it: from begin, until the end or an entry without an
// object.
template <typename F>
void ForEachObject(ObjectMap& objects, F f) {
    MapIterator<ObjectMap> it;
    ObjectsBegin(&objects, nullptr, &it);
    while (it.node) {
        CEngineObject* object = it.node->value;
        if (!object) return;
        f(object);
        ObjectsNext(&it);
    }
}

// CPtrList::AddTail as the manager's code inlines it, with the list's own NewNode.
ObjectNode* AddTail(ObjectList* list, CEngineObject* object) {
    ObjectNode* node = list->NewNode(list->tail, nullptr);
    node->data = object;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
    return node;
}

// Every node of a table freed through the table's node allocator (`this` is its count proxy),
// the buckets emptied, the count zeroed.
template <typename Map, typename FreeNode>
void FreeNodes(Map& table, FreeNode freeNode) {
    table.ClearWith([&table, freeNode](typename Map::Node* node) { freeNode(table.CountProxy(), nullptr, node, 1); });
}

// One of the objects table's finds into an iterator (the callers use different copies).
using ObjectFind = MapIterator<ObjectMap>*(__fastcall*)(ObjectMap*, void*, MapIterator<ObjectMap>*, const int*);

// The id of an object registered without one: maxId_ + 1 while that stays inside [idLow_,
// idHigh_), else a random one in that range that `find` does not find (100000 tries; then the
// fatal "out of ids" message and -1).
int NextFreeId(CBaseManager* m, ObjectFind find) {
    int limit = m->idHigh_;
    int current = m->maxId_;
    int low = m->idLow_;
    int id;
    if (current < limit && current >= low) {
        id = current + 1;
    } else {
        int range = limit - low;
        id = -1;
        for (int tries = 0; tries < 100000; ++tries) {
            int r = gamecrt::rand() << 15;
            r += gamecrt::rand();
            int candidate = r % range + low;
            if (candidate == 0) continue;
            int key = candidate;
            MapIterator<ObjectMap> hit;
            find(&m->objects_, nullptr, &hit, &key);
            if (!hit.node) {
                id = candidate;
                break;
            }
        }
    }
    if (id == -1) game::FatalError(OutOfIdsMessage());
    return id;
}

// The positions entry of `id` (the id's node in the object list) erased and the node removed from
// the list (inlined RemoveAt).
void ForgetPosition(CBaseManager* m, int id) {
    int key = id;
    MapIterator<PositionMap> found;
    PositionsFind(&m->positions_, nullptr, &found, &key);
    if (!found.node) return;
    ObjectNode* node = found.node->value;
    HashEraseOriginal(&m->positions_, nullptr, found.node, found.table);
    if (node) m->objectList_.RemoveAt(node);
}

} // namespace

// --- Construction and destruction ---------------------------------------------------------------

CBaseManager* CBaseManager::ConstructBase() {
    ei::SetVtable(this, CEngineObject::kVtableAddress);   // the inlined CEngineObject constructor
    ClearFields();
    char hash = 0, equal = 0, alloc = 0;
    ObjectsConstruct(&objects_, nullptr, 100, &hash, &equal, &alloc);
    maxId_ = 0;
    objectList_.vtable = mfc::impl::TempListClass::Vtable();   // CPtrList(10)
    objectList_.count = 0;
    objectList_.free = nullptr;
    objectList_.tail = nullptr;
    objectList_.head = nullptr;
    objectList_.blocks = nullptr;
    objectList_.blockSize = 10;
    BuildTable(positions_, maxPositionId_, PositionBucketsConstruct, PositionCountConstruct, PositionsResize);
    SparesConstruct(&spares_, nullptr, 100, &hash, &equal, &alloc);
    maxSpareType_ = 0;
    BuildTable(spareCounts_, maxSpareCountType_, SpareCountBucketsConstruct, SpareCountCountConstruct,
               SpareCountsResize);
    idLow_ = 0;
    context_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    idHigh_ = 2000000000;
    reservedA_ = -1;
    reservedB_ = -1;
    lastId_ = -1;
    return this;
}
EI_REPLACE_METHOD(0x00402B20, CBaseManager::ConstructBase);

void CBaseManager::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    CBaseManager::Destroy();
    DestroyTable(spareCounts_, SpareCountsDeleteNode, SpareCountBucketsDestroy);
    DestroyTable(spares_, SparesDeleteNode, SpareBucketsDestroy);
    DestroyTable(positions_, PositionsDeleteNode, PositionBucketsDestroy);
    objectList_.vtable = mfc::impl::TempListClass::Vtable();   // ~CPtrList
    objectList_.RemoveAll();
    DestroyTable(objects_, ObjectsDeleteNode, ObjectBucketsDestroy);
    DestructBase();
}
EI_REPLACE_METHOD(0x00402DF0, CBaseManager::DestructBody);

CBaseManager* CBaseManager::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00402DD0, CBaseManager::DeletingDestruct);

// --- Slots 2, 3, 13, 15 -------------------------------------------------------------------------

int CBaseManager::Create() {
    idLow_ = 0;
    idHigh_ = 2000000000;
    ObjectsClear(&objects_);
    maxId_ = 0;
    ObjectsResize(&objects_, nullptr, 0x400);
    maxId_ = 0;
    ResetTable(positions_, maxPositionId_,
               [this](PositionMap::Node* n) { PositionsDeleteNode(&positions_, nullptr, n); }, PositionsResizeBig, 0x400);
    ResetTable(spares_, maxSpareType_, [this](SpareMap::Node* n) { SparesFreeNode(spares_.CountProxy(), nullptr, n, 1); },
               SparesResize, 0x100);
    ResetTable(spareCounts_, maxSpareCountType_,
               [](SpareCountMap::Node* n) { stlp::deallocate(n, sizeof(SpareCountMap::Node)); }, SpareCountsResizeBig, 0x100);
    reservedA_ = 0;
    reservedB_ = 300;
    return 1;
}

// Every object gets a reference, then its own Destroy (slot 3); then every object still registered
// under the ids collected is released (slot 4); the object list is emptied; every object left is
// deleted (slot 0); then the tables are cleared.
void CBaseManager::Destroy() {
    ForEachObject(objects_, [](CEngineObject* o) { ++o->refCount_; });
    char alloc = 0;
    IdList ids;
    IdsConstruct(&ids, nullptr, &alloc);
    ForEachObject(objects_, [&ids](CEngineObject* o) {
        int id = o->id_;
        IdNode* end;
        IdsEnd(&ids, nullptr, &end);
        IdNode* added;
        IdsInsert(&ids, nullptr, &added, end, &id);
        o->Destroy();
    });
    IdNode* head = ids.head;
    for (IdNode* n = head->next; n != head; n = n->next) {
        CEngineObject** found = ObjectsFind(&objects_, nullptr, &n->data);
        if (found) (*found)->Release();
    }
    for (IdNode* n = head->next; n != head;) {
        IdNode* dead = n;
        n = n->next;
        IdsFreeNode(&ids, nullptr, dead, 1);
    }
    head->next = head;
    head->prev = head;
    objectList_.RemoveAll();
    ForEachObject(objects_, [](CEngineObject* o) { delete o; });
    ObjectsClear(&objects_);
    maxId_ = 0;
    PositionsClear(&positions_);
    maxPositionId_ = 0;
    FreeSpares();
    SparesClear(&spares_);
    maxSpareType_ = 0;
    SpareCountsClear(&spareCounts_);
    maxSpareCountType_ = 0;
    context_ = nullptr;
    IdsClear(&ids);
    IdsFreeNode(&ids, nullptr, ids.head, 1);
}

BOOL CBaseManager::InitImpl(void* context) {
    context_ = context;
    if (Create()) return 1;
    context_ = nullptr;
    return 0;
}

// Every object gets a reference, then is destroyed (slot 3), then released (slot 4; objects nothing
// else holds go back to the spares). Then the objects table, the object list and the positions are
// emptied.
void CBaseManager::DestroyAll() {
    ForEachObject(objects_, [](CEngineObject* o) { ++o->refCount_; });
    ForEachObject(objects_, [](CEngineObject* o) { o->Destroy(); });
    ForEachObject(objects_, [](CEngineObject* o) { o->Release(); });
    maxId_ = 0;
    ObjectsClear(&objects_);
    objectList_.RemoveAll();
    maxPositionId_ = 0;
    PositionsClear(&positions_);
}

// --- Slots 16-22: no archives -------------------------------------------------------------------
// 18 (00402D80) and 22 (00402DC0) are shorter than the hook's jump and stay original.

BOOL CBaseManager::AddArchiveImpl(const char*) { return 0; }
BOOL CBaseManager::RemoveArchiveImpl(const char*) { return 0; }
BOOL CBaseManager::RemoveAllArchives() { return 0; }
void* CBaseManager::GetFileImpl(const char*, std::uint32_t*) { return nullptr; }
bool CBaseManager::CloseFileImpl(void*) { return false; }
BOOL CBaseManager::HasFileImpl(const char*) { return 0; }
void CBaseManager::FreeAllFiles() {}

// --- Slots 23, 24: objects in and out -----------------------------------------------------------

CEngineObject* CBaseManager::CreateObjectImpl(int type, int id) {
    int key = type;
    CEngineObject* object = nullptr;
    if (*SpareCountAt(&spareCounts_, nullptr, &key) > 0) {
        int typeKey = key;
        MapIterator<SpareMap> found;
        SparesFind(&spares_, nullptr, &found, &typeKey);
        if (found.node) {
            ObjectList* spares = found.node->value;
            if (spares->count != 0) object = spares->RemoveHead();
        }
    }
    if (!object) {
        object = NewObject(type);
        if (!object) return nullptr;
    }
    object->flags_ = 0;
    object->type_ = type;
    if (id == -1) {
        int chosen = NextFreeId(this, ObjectsFindIt);
        CEngineObject* existing = nullptr;
        if (!ObjectsLookup(&objects_, nullptr, &chosen, &existing)) {
            existing = nullptr;
            objectList_.AddTail(object);
            ObjectNode* tail = objectList_.tail;
            hash400::PositionAt(positions_, chosen) = tail;
        }
        ObjectsSetAt(&objects_, nullptr, &chosen, &object);
        object->id_ = chosen;
        lastId_ = chosen;
    } else {
        int wanted = id;
        MapIterator<ObjectMap> hit;
        ObjectsFindIt(&objects_, nullptr, &hit, &wanted);
        if (!hit.node) {
            ObjectNode* position = AddTail(&objectList_, object);
            hash400::PositionAt(positions_, id) = position;
        }
        if (id > maxId_) maxId_ = id;
        int slotKey = id;
        *ObjectsAt(&objects_, nullptr, &slotKey) = object;
        object->id_ = id;
        lastId_ = id;
    }
    object->SetOwner(this);
    object->Create();
    return object;
}

int CBaseManager::ReleaseObjectImpl(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    ForgetPosition(this, object->id_);
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

namespace {
// The virtual slots' entry points (rewrite-guide rule 4).
int __fastcall CreateHook(CBaseManager* self) { return self->CBaseManager::Create(); }
void __fastcall DestroyHook(CBaseManager* self) { self->CBaseManager::Destroy(); }
void __fastcall DestroyAllHook(CBaseManager* self) { self->CBaseManager::DestroyAll(); }
} // namespace
EI_REPLACE(0x00403020, CreateHook);
EI_REPLACE(0x004031E0, DestroyHook);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE(0x0066C100, DestroyHook);
EI_REPLACE(0x0067E160, DestroyHook);
EI_REPLACE_METHOD(0x004046E0, CBaseManager::LoadImpl);
EI_REPLACE_METHOD(0x00404760, CBaseManager::SaveImpl);
EI_REPLACE_METHOD(0x004031B0, CBaseManager::InitImpl);
EI_REPLACE(0x004034E0, DestroyAllHook);
EI_REPLACE_METHOD(0x00402D60, CBaseManager::AddArchiveImpl);
EI_REPLACE_METHOD(0x00402D70, CBaseManager::RemoveArchiveImpl);
EI_REPLACE_METHOD(0x00402D90, CBaseManager::GetFileImpl);
EI_REPLACE_METHOD(0x00402DA0, CBaseManager::CloseFileImpl);
EI_REPLACE_METHOD(0x00402DB0, CBaseManager::HasFileImpl);
EI_REPLACE_METHOD(0x00403730, CBaseManager::CreateObjectImpl);
EI_REPLACE_METHOD(0x00403AE0, CBaseManager::ReleaseObjectImpl);

// --- Registration (non-virtual) -----------------------------------------------------------------

CEngineObject* CBaseManager::FindObject(unsigned id) const {
    auto* node = objects_.Find(id);
    return node ? node->value : nullptr;
}
EI_REPLACE_METHOD(0x00403F40, CBaseManager::FindObject);

void CBaseManager::SetContext(void* context) { context_ = context; }
EI_REPLACE_METHOD(0x00403720, CBaseManager::SetContext);

CEngineObject* CBaseManager::Unregister(int id) {
    int key = id;
    MapIterator<ObjectMap> found;
    ObjectsFindIt2(&objects_, nullptr, &found, &key);
    if (!found.node) return nullptr;
    CEngineObject* object = found.node->value;
    key = id;
    ObjectsFindIt3(&objects_, nullptr, &found, &key);
    if (found.node) ObjectsEraseOriginal(&objects_, nullptr, found.node, found.table);
    ForgetPosition(this, id);
    return object;
}
EI_REPLACE_METHOD(0x004039B0, CBaseManager::Unregister);

int CBaseManager::RegisterInstant(CEngineObject* object) {
    int id = NextFreeId(this, ObjectsFindIt2);
    int key = id;
    MapIterator<ObjectMap> found;
    ObjectsFindIt2(&objects_, nullptr, &found, &key);
    if (!found.node) {
        ObjectNode* position = AddTail(&objectList_, object);
        if (id > maxPositionId_) maxPositionId_ = id;
        int positionKey = id;
        *PositionAt2(&positions_, nullptr, &positionKey) = position;
    }
    if (id > maxId_) maxId_ = id;
    ObjectPair pair = {static_cast<unsigned>(id), nullptr};
    ObjectsFindOrInsert(&objects_, nullptr, &pair)->value = object;
    object->id_ = id;
    lastId_ = id;
    object->SetOwner(this);
    return id;
}
EI_REPLACE_METHOD(0x00403BD0, CBaseManager::RegisterInstant);

int CBaseManager::RemoveObject(CEngineObject* object) {
    int id = object->id_;
    int type = object->type_;
    int key = id;
    MapIterator<ObjectMap> found;
    ObjectsFindIt2(&objects_, nullptr, &found, &key);
    if (found.node && found.node->value == object) {
        key = id;
        CEngineObject* value = nullptr;
        if (ObjectsLookup(&objects_, nullptr, &key, &value)) {
            ObjectsRemoveKey(&objects_, nullptr, &key);
            if (auto* position = hash400::PositionsRemoveKey(positions_, key))
                objectList_.RemoveAt(position);
        }
    }
    int typeKey = type;
    MapIterator<SpareMap> spare;
    SparesFind2(&spares_, nullptr, &spare, &typeKey);
    if (spare.node) {
        AddTail(spare.node->value, object);
        object->id_ = -1;
        int count = *SpareCountAt(&spareCounts_, nullptr, &type) + 1;
        if (type > maxSpareCountType_) maxSpareCountType_ = type;
        int countKey = type;
        *SpareCountAt2(&spareCounts_, nullptr, &countKey) = count;
        return 0;
    }
    // The first spare of this type: a new list (CPtrList(10)).
    auto* list = static_cast<ObjectList*>(gamecrt::operator_new(sizeof(ObjectList)));
    if (list) {
        list->count = 0;
        list->free = nullptr;
        list->tail = nullptr;
        list->head = nullptr;
        list->blocks = nullptr;
        list->vtable = mfc::impl::TempListClass::Vtable();
        list->blockSize = 10;
    }
    if (type > maxSpareType_) maxSpareType_ = type;
    int listKey = type;
    *SparesAt(&spares_, nullptr, &listKey) = list;
    AddTail(list, object);
    object->id_ = -1;
    if (type > maxSpareCountType_) maxSpareCountType_ = type;
    int countKey = type;
    int count = *SpareCountAt2(&spareCounts_, nullptr, &countKey) + 1;
    if (type > maxSpareCountType_) maxSpareCountType_ = type;
    int pairKey = type;
    int zero = 0;
    CountPair pair;
    MakeCountPair(&pair, nullptr, &pairKey, &zero);
    SpareCountInsert(&spareCounts_, nullptr, &pair)->value = count;
    return 0;
}
EI_REPLACE_METHOD(0x00404040, CBaseManager::RemoveObject);

// Each type's list deletes its objects (slot 0), is emptied (RemoveAll, inlined) and deleted (its
// slot 1); then the spares and spare counts tables are emptied.
void CBaseManager::FreeSpares() {
    MapIterator<SpareMap> it;
    SparesBegin(&spares_, nullptr, &it);
    while (it.node) {
        ObjectList* list = it.node->value;
        if (!list) break;
        for (ObjectNode* n = list->head; n;) {
            ObjectNode* next = n->next;
            delete n->data;
            n = next;
        }
        list->RemoveAll();
        reinterpret_cast<mfc::CObjectVirtuals*>(list)->DeletingDestructor(1u);   // its CObject slot 1
        SpareMap::Node* next = it.node->next;
        if (!next) next = SparesNextBucket(&it);
        it.node = next;
    }
    maxSpareType_ = 0;
    FreeNodes(spares_, SparesFreeNode);
    maxSpareCountType_ = 0;
    FreeNodes(spareCounts_, SpareCountsFreeNode);
}
EI_REPLACE_METHOD(0x00404410, CBaseManager::FreeSpares);

// --- Saving and loading -------------------------------------------------------------------------

// The spares are freed; then `count` records {id, type, prototypeId_} each create an object (slot 23).
void CBaseManager::LoadImpl(void* stream) {
    auto* in = static_cast<CStream*>(stream);
    FreeSpares();
    int count;
    in->Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        int id, type, extra;
        in->Read(&id, 4);
        in->Read(&type, 4);
        in->Read(&extra, 4);
        CEngineObject* object = CreateObject(type, id);
        object->prototypeId_ = static_cast<std::uint32_t>(extra);
    }
}

// The object list's count, then {id, type, prototypeId_} per object.
void CBaseManager::SaveImpl(void* stream) {
    auto* out = static_cast<CStream*>(stream);
    int count = objectList_.count;
    out->Write(&count, 4);
    for (ObjectNode* node = objectList_.head; node;) {
        ObjectNode* next = node->next;
        CEngineObject* object = node->data;
        int value = object->id_;
        out->Write(&value, 4);
        value = object->type_;
        out->Write(&value, 4);
        value = static_cast<int>(object->prototypeId_);
        out->Write(&value, 4);
        node = next;
    }
}

// Records {id, object data (slot 9)} until the end mark followed by a 0 byte (with a 1 byte the
// mark is an object id after all); stops at an id not registered (FindObject inlined).
void CBaseManager::LoadObjects(void* stream) {
    auto* in = static_cast<CStream*>(stream);
    for (;;) {
        int id;
        in->Read(&id, 4);
        if (id == kEndOfObjects) {
            char more;
            in->Read(&more, 1);
            if (!more) return;
        }
        CEngineObject* object = FindObject(static_cast<unsigned>(id));
        if (!object) return;
        object->Load(stream);
    }
}
EI_REPLACE_METHOD(0x004047E0, CBaseManager::LoadObjects);

// {id, [1 byte if the id is the end mark], object data (slot 10)} per object in list order, then
// the end mark and a 0 byte.
void CBaseManager::SaveObjects(void* stream) {
    auto* out = static_cast<CStream*>(stream);
    for (ObjectNode* node = objectList_.head; node;) {
        ObjectNode* next = node->next;
        CEngineObject* object = node->data;
        int id = object->id_;
        out->Write(&id, 4);
        if (object->id_ == kEndOfObjects) {
            char one = 1;
            out->Write(&one, 1);
        }
        object->Save(stream);
        node = next;
    }
    int mark = kEndOfObjects;
    out->Write(&mark, 4);
    char zero = 0;
    out->Write(&zero, 1);
}
EI_REPLACE_METHOD(0x00404860, CBaseManager::SaveObjects);

void CBaseManager::ReleaseUnused() {
    for (ObjectNode* node = objectList_.head; node;) {
        CEngineObject* object = node->data;
        node = node->next;
        if (object->refCount_ == 0) {
            object->refCount_ = 1;
            object->Release();
        }
    }
}
EI_REPLACE_METHOD(0x004048F0, CBaseManager::ReleaseUnused);

// --- ObjectListIterator -------------------------------------------------------------------------

// The reference moves from the previous manager to `list`; returns the list's first object (the
// position then names the next node), or null.
CEngineObject* ObjectListIterator::First(CBaseManager* list) {
    if (manager) --manager->refCount_;
    manager = list;
    if (list) ++list->refCount_;
    ObjectNode* node = manager->objectList_.head;
    position = node;
    if (!node) return nullptr;
    position = node->next;
    return node->data;
}
EI_REPLACE_METHOD(0x00404920, ObjectListIterator::First);

// The object at the position (moving it on), or null at the end.
CEngineObject* ObjectListIterator::Next() {
    ObjectNode* node = position;
    if (!node) return nullptr;
    position = node->next;
    return node->data;
}
EI_REPLACE_METHOD(0x00404960, ObjectListIterator::Next);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x004BAA30, ObjectListIterator::Next);
