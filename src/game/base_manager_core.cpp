// CBaseManager construction and destruction (00402B20-00403010) and the small default slots of the
// engine object and manager vtables (0073B8E0, 0073B870). See base_manager.h and
// docs/notes/unmapped_410_470.md ("Fifth round").
#include "base_manager.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

int __fastcall BaseManagerRemoveObject(CBaseManager* self, void*, CEngineObject* object);
void __fastcall BaseManagerFreeRecords(CBaseManager* self);

namespace {

// STLport instantiations of the manager's containers (library): hashtable constructors, the
// manual construction pieces (bucket vector, allocator proxy, initial resize), node deletion and
// bucket vector destruction.
__declspec(naked) void* __fastcall ObjectsConstruct(void* /*table*/, void*, std::size_t /*n*/, const void*, const void*, const void*) EI_ORIGINAL(0x00405820)
__declspec(naked) void* __fastcall Table68Construct(void* /*table*/, void*, std::size_t /*n*/, const void*, const void*, const void*) EI_ORIGINAL(0x00405C40)
__declspec(naked) void* __fastcall Buckets50Construct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00406240)
__declspec(naked) void* __fastcall Proxy50Construct(void* /*proxy*/, void*, const void* /*alloc*/, const void* /*value*/) EI_ORIGINAL(0x00406260)
__declspec(naked) void __fastcall Table50Resize(void* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00405EB0)
__declspec(naked) void* __fastcall Buckets80Construct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00406530)
__declspec(naked) void* __fastcall Proxy80Construct(void* /*proxy*/, void*, const void* /*alloc*/, const void* /*value*/) EI_ORIGINAL(0x00406550)
__declspec(naked) void __fastcall Table80Resize(void* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00406300)
__declspec(naked) void __fastcall DeleteNode80(void* /*table*/, void*, void* /*node*/) EI_ORIGINAL(0x00407700)
__declspec(naked) void __fastcall DeleteNode68(void* /*table*/, void*, void* /*node*/) EI_ORIGINAL(0x00407640)
__declspec(naked) void __fastcall DeleteNode50(void* /*table*/, void*, void* /*node*/) EI_ORIGINAL(0x00407580)
__declspec(naked) void __fastcall DeleteNode1C(void* /*table*/, void*, void* /*node*/) EI_ORIGINAL(0x004074B0)
__declspec(naked) void __fastcall Buckets80Destroy(void* /*vector*/) EI_ORIGINAL(0x00405E00)
__declspec(naked) void __fastcall Buckets68Destroy(void* /*vector*/) EI_ORIGINAL(0x00405DB0)
__declspec(naked) void __fastcall Buckets50Destroy(void* /*vector*/) EI_ORIGINAL(0x00405BF0)
__declspec(naked) void __fastcall Buckets1CDestroy(void* /*vector*/) EI_ORIGINAL(0x00405E50)
// MFC CPlex::FreeDataChain (the CPtrList's blocks).
__declspec(naked) void __fastcall PlexFreeDataChain(void* /*plex*/) EI_ORIGINAL(0x006FE90F)
// The objects table's iteration (begin into {node, table}, ++), find (pointer to the value or
// null), clear; the other tables' clear; the id list used by Destroy (list<int>: constructor,
// end, insert, node deallocation, clear).
__declspec(naked) void* __fastcall ObjectsBegin(void* /*table*/, void*, void** /*out*/) EI_ORIGINAL(0x00404C30)
__declspec(naked) void** __fastcall ObjectsNext(void** /*it*/) EI_ORIGINAL(0x00404980)
__declspec(naked) CEngineObject** __fastcall ObjectsFind(void* /*table*/, void*, const void* /*key*/) EI_ORIGINAL(0x00406950)
__declspec(naked) void __fastcall ObjectsClear(void* /*table*/) EI_ORIGINAL(0x004067B0)
__declspec(naked) void __fastcall Table50Clear(void* /*table*/) EI_ORIGINAL(0x00406E80)
__declspec(naked) void __fastcall Table68Clear(void* /*table*/) EI_ORIGINAL(0x00407050)
__declspec(naked) void __fastcall Table80Clear(void* /*table*/) EI_ORIGINAL(0x00407220)
__declspec(naked) void* __fastcall IdsConstruct(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00404A70)
__declspec(naked) void** __fastcall IdsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004049D0)
__declspec(naked) void* __fastcall IdsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const int* /*value*/) EI_ORIGINAL(0x00404A40)
__declspec(naked) void __fastcall IdsFreeNode(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00404D10)
__declspec(naked) void __fastcall IdsClear(void* /*list*/) EI_ORIGINAL(0x00405500)
__declspec(naked) void __fastcall ObjectsResize(void* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00406640)
__declspec(naked) void __fastcall Table50ResizeBig(void* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00406D10)
__declspec(naked) void __fastcall Table68Resize(void* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x00406EE0)
__declspec(naked) void __fastcall Table80ResizeBig(void* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x004070B0)
__declspec(naked) void __fastcall Table68FreeNode(void* /*allocator*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00407DA0)
// The +0x50 table (id -> list position): find into {node, table}, erase by iterator.
__declspec(naked) void* __fastcall Table50Find(void* /*table*/, void*, void** /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405BB0)
__declspec(naked) void __fastcall Table50Erase(void* /*table*/, void*, void* /*node*/, void* /*owner*/) EI_ORIGINAL(0x00405720)
// 00455970: CEngineObject::SetOwner (returns the previous owner); 00404040: removes an object
// from the manager (not rewritten yet).
__declspec(naked) void* __fastcall SetObjectOwner(CEngineObject* /*object*/, void*, void* /*owner*/) EI_ORIGINAL(0x00455970)
// CreateObject's containers: +0x80 type -> spare count (operator[]), +0x68 type -> CPtrList* of
// spare objects (find), the objects table (MFC-style Lookup / SetAt, find, operator[]), the
// pointer list (AddTail, NewNode, RemoveHead), +0x50 id -> list position (operator[]).
__declspec(naked) int* __fastcall SpareCountAt(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x00406AA0)
__declspec(naked) void* __fastcall SpareListFind(void* /*table*/, void*, void** /*out*/, const int* /*key*/) EI_ORIGINAL(0x004057A0)
__declspec(naked) void* __fastcall PtrListRemoveHead(void* /*list*/) EI_ORIGINAL(0x00404FE0)
__declspec(naked) int __fastcall ObjectsLookup(void* /*table*/, void*, const int* /*key*/, CEngineObject** /*value*/) EI_ORIGINAL(0x00404F80)
__declspec(naked) void __fastcall ObjectsSetAt(void* /*table*/, void*, const int* /*key*/, CEngineObject* const* /*value*/) EI_ORIGINAL(0x00404E80)
__declspec(naked) void* __fastcall ObjectsFindIt(void* /*table*/, void*, void** /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405560)
__declspec(naked) CEngineObject** __fastcall ObjectsAt(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x004055A0)
__declspec(naked) void* __fastcall PtrListAddTail(void* /*list*/, void*, void* /*data*/) EI_ORIGINAL(0x00405040)
__declspec(naked) void** __fastcall PtrListNewNode(void* /*list*/, void*, void* /*prev*/, void* /*next*/) EI_ORIGINAL(0x004069A0)
__declspec(naked) void** __fastcall PositionAt(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x00406A10)
__declspec(naked) int __cdecl GameRand() EI_ORIGINAL(0x006E77F7)
// RemoveObject's: another find of the objects table, remove-by-key of the objects / +0x50 tables
// (returning the value), CPtrList::RemoveAt, find / operator[] of +0x68, operator[] / pair / insert
// of +0x80.
__declspec(naked) void* __fastcall ObjectsFindIt2(void* /*table*/, void*, void** /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405B20)
__declspec(naked) CEngineObject* __fastcall ObjectsRemoveKey(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x00404F20)
__declspec(naked) void* __fastcall PositionRemoveKey(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x00405350)
__declspec(naked) void __fastcall PtrListRemoveAt(void* /*list*/, void*, void* /*position*/) EI_ORIGINAL(0x004050C0)
__declspec(naked) void* __fastcall SpareListFind2(void* /*table*/, void*, void** /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405D70)
__declspec(naked) void** __fastcall SpareListAt(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x004075C0)
__declspec(naked) int* __fastcall SpareCountAt2(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x00407680)
__declspec(naked) int* __fastcall MakeTypePair(int* /*pair*/, void*, const int* /*key*/, const int* /*value*/) EI_ORIGINAL(0x00407DE0)
__declspec(naked) int* __fastcall SpareCountInsert(void* /*table*/, void*, const int* /*pair*/) EI_ORIGINAL(0x004085E0)
// FreeRecords': begin of +0x68, the next non-empty bucket after a node (returns its head), node
// deallocation of +0x80. Unregister's: another find of the objects table and erase(iterator).
// RegisterInstant's: operator[] of +0x50, find-or-insert of a {key, value} pair in the objects table.
__declspec(naked) void* __fastcall Table68Begin(void* /*table*/, void*, void** /*out*/) EI_ORIGINAL(0x00406280)
__declspec(naked) void* __fastcall Table68NextBucket(void** /*it*/) EI_ORIGINAL(0x004073C0)
__declspec(naked) void __fastcall Table80FreeNode(void* /*allocator*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00408020)
__declspec(naked) void* __fastcall ObjectsFindIt3(void* /*table*/, void*, void** /*out*/, const int* /*key*/) EI_ORIGINAL(0x00405AE0)
__declspec(naked) void __fastcall ObjectsErase(void* /*table*/, void*, void* /*node*/, void* /*owner*/) EI_ORIGINAL(0x00405620)
__declspec(naked) void** __fastcall PositionAt2(void* /*table*/, void*, const int* /*key*/) EI_ORIGINAL(0x004056A0)
__declspec(naked) int* __fastcall ObjectsFindOrInsert(void* /*table*/, void*, const int* /*pair*/) EI_ORIGINAL(0x004065D0)
// 00455930: the engine object's base destructor body.
__declspec(naked) void __fastcall EngineObjectDestructBase(void* /*object*/) EI_ORIGINAL(0x00455930)

constexpr std::uintptr_t kEngineObjectVtable = 0x0073B8E0;
constexpr std::uintptr_t kBaseManagerVtable = 0x0073B870;
constexpr std::uintptr_t kPtrListVtable = 0x0073B8D4;

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, 4); }
std::uint32_t& At(void* object, std::size_t offset) {
    return *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(object) + offset);
}

// The hashtable members built by hand (+0x50 and +0x80): functors, bucket vector, allocator proxy
// of the element count, 100 buckets, count 0.
template <typename Buckets, typename Proxy, typename Resize>
void BuildTable(void* table, Buckets buckets, Proxy proxy, Resize resize) {
    char tag = 0;
    auto* bytes = static_cast<std::uint8_t*>(table);
    bytes[0] = 0;
    bytes[1] = 0;
    bytes[2] = 0;
    buckets(bytes + 4, nullptr, &tag);
    std::uint32_t zero = 0;
    proxy(bytes + 0x10, nullptr, &tag, &zero);
    resize(table, nullptr, 100);
    At(table, 0x14) = 0;
}

// Empties a hashtable member: every node of every bucket deleted, the buckets cleared, count 0,
// then its bucket vector destroyed.
template <typename Delete, typename Destroy>
void DestroyTable(void* table, Delete deleteNode, Destroy destroyBuckets) {
    auto*& begin = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(table) + 4);
    auto*& end = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(table) + 8);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(end - begin); ++i) {
        for (void* node = begin[i]; node;) {
            void* next = *static_cast<void**>(node);
            deleteNode(table, nullptr, node);
            node = next;
        }
        begin[i] = nullptr;
    }
    At(table, 0x10) = 0;
    destroyBuckets(static_cast<std::uint8_t*>(table) + 4);
}

} // namespace

CBaseManager* CBaseManager::ConstructBase() {
    // The engine object part (inlined CEngineObject constructor).
    SetVtable(this, kEngineObjectVtable);
    refCount_ = 0;
    id_ = -1;
    unknown_18 = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
    char hash = 0, equal = 0, alloc = 0;
    ObjectsConstruct(&objects_, nullptr, 100, &hash, &equal, &alloc);
    maxId_ = 0;
    // MFC CPtrList at +0x34 (block size 10).
    std::uint8_t* self = reinterpret_cast<std::uint8_t*>(this);
    SetVtable(self + 0x34, kPtrListVtable);
    At(this, 0x40) = 0;
    At(this, 0x44) = 0;
    At(this, 0x3C) = 0;
    At(this, 0x38) = 0;
    At(this, 0x48) = 0;
    At(this, 0x4C) = 10;
    BuildTable(self + 0x50, Buckets50Construct, Proxy50Construct, Table50Resize);
    Table68Construct(self + 0x68, nullptr, 100, &hash, &equal, &alloc);
    At(this, 0x7C) = 0;
    BuildTable(self + 0x80, Buckets80Construct, Proxy80Construct, Table80Resize);
    At(this, 0x98) = 0;
    owner_AC = nullptr;
    SetVtable(this, kBaseManagerVtable);
    At(this, 0x9C) = 2000000000;
    At(this, 0xA0) = 0xFFFFFFFFu;
    At(this, 0xA4) = 0xFFFFFFFFu;
    At(this, 0xA8) = 0xFFFFFFFFu;
    return this;
}
EI_REPLACE_METHOD(0x00402B20, CBaseManager::ConstructBase);

void CBaseManager::DestructBody() {
    SetVtable(this, kBaseManagerVtable);
    CBaseManager::Destroy();
    std::uint8_t* self = reinterpret_cast<std::uint8_t*>(this);
    DestroyTable(self + 0x80, DeleteNode80, Buckets80Destroy);
    DestroyTable(self + 0x68, DeleteNode68, Buckets68Destroy);
    DestroyTable(self + 0x50, DeleteNode50, Buckets50Destroy);
    // CPtrList::~CPtrList (RemoveAll: the nodes need no destruction; the blocks are freed).
    SetVtable(self + 0x34, kPtrListVtable);
    for (void* node = reinterpret_cast<void*>(At(this, 0x38)); node; node = *static_cast<void**>(node)) {
    }
    At(this, 0x40) = 0;
    At(this, 0x44) = 0;
    At(this, 0x3C) = 0;
    At(this, 0x38) = 0;
    PlexFreeDataChain(reinterpret_cast<void*>(At(this, 0x48)));
    At(this, 0x48) = 0;
    DestroyTable(&objects_, DeleteNode1C, Buckets1CDestroy);
    EngineObjectDestructBase(this);
}
EI_REPLACE_METHOD(0x00402DF0, CBaseManager::DestructBody);

// 00402DD0 (slot 0 of 0073B870): the destructor body, then operator delete when flags bit 0 is set.
CBaseManager* __fastcall BaseManagerDeletingDestructor(CBaseManager* self, void*, unsigned flags) {
    self->DestructBody();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00402DD0, BaseManagerDeletingDestructor);

// 00402D20 (slot 0 of 0073B8E0): the engine object's deleting destructor.
CEngineObject* __fastcall EngineObjectDeletingDestructor(CEngineObject* self, void*, unsigned flags) {
    EngineObjectDestructBase(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00402D20, EngineObjectDeletingDestructor);

// 00402D40 (slot 1): copies the flags, kind and +0x18 of another object.
void CEngineObject::CopyFrom(const CEngineObject& other) {
    flags_ = other.flags_;
    type_ = other.type_;
    unknown_18 = other.unknown_18;
}
namespace {
void __fastcall CopyFromHook(CEngineObject* self, void*, const CEngineObject* other) { self->CEngineObject::CopyFrom(*other); }
// 00402CE0 / 00402CF0 (slots 7 / 8): the default name and description strings.
const char* __fastcall DefaultName(void*) { return reinterpret_cast<const char*>(0x00783BF0); }
const char* __fastcall DefaultDescription(void*) { return reinterpret_cast<const char*>(0x00783BF8); }
} // namespace
EI_REPLACE(0x00402D40, CopyFromHook);
EI_REPLACE(0x00402CE0, DefaultName);
EI_REPLACE(0x00402CF0, DefaultDescription);

namespace {

// The objects table's walk as Destroy does it: from begin, until the end or an entry without an
// object.
template <typename F>
void ForEachObject(CBaseManager* m, F f) {
    void* it[2];
    ObjectsBegin(&m->objects_, nullptr, it);
    while (it[0]) {
        auto* object = *reinterpret_cast<CEngineObject**>(static_cast<std::uint8_t*>(it[0]) + 8);
        if (!object) return;
        f(object);
        ObjectsNext(it);
    }
}

} // namespace

// 004031E0 (slot 3). Destroys every object: each gets a reference, then its own Destroy (slot 3);
// then every object still registered under the ids collected is released (slot 4); the pointer
// list is emptied; every object left is deleted (slot 0); then the tables are cleared.
void CBaseManager::Destroy() {
    ForEachObject(this, [](CEngineObject* o) { ++o->refCount_; });
    char alloc = 0;
    void* ids;
    IdsConstruct(&ids, nullptr, &alloc);
    ForEachObject(this, [&ids](CEngineObject* o) {
        int id = o->id_;
        void* end;
        IdsEnd(&ids, nullptr, &end);
        void* added;
        IdsInsert(&ids, nullptr, &added, end, &id);
        o->Destroy();
    });
    void* head = ids;
    for (void* n = *static_cast<void**>(head); n != head; n = *static_cast<void**>(n)) {
        CEngineObject** found = ObjectsFind(&objects_, nullptr, static_cast<std::uint8_t*>(n) + 8);
        if (found) (*found)->Release();
    }
    for (void* n = *static_cast<void**>(head); n != head;) {
        void* dead = n;
        n = *static_cast<void**>(n);
        IdsFreeNode(&ids, nullptr, dead, 1);
    }
    static_cast<void**>(head)[0] = head;
    static_cast<void**>(head)[1] = head;
    // CPtrList::RemoveAll.
    for (void* node = reinterpret_cast<void*>(At(this, 0x38)); node; node = *static_cast<void**>(node)) {
    }
    At(this, 0x40) = 0;
    At(this, 0x44) = 0;
    At(this, 0x3C) = 0;
    At(this, 0x38) = 0;
    PlexFreeDataChain(reinterpret_cast<void*>(At(this, 0x48)));
    At(this, 0x48) = 0;
    ForEachObject(this, [](CEngineObject* o) { delete o; });
    std::uint8_t* self = reinterpret_cast<std::uint8_t*>(this);
    ObjectsClear(&objects_);
    maxId_ = 0;
    Table50Clear(self + 0x50);
    At(this, 0x64) = 0;
    BaseManagerFreeRecords(this);
    Table68Clear(self + 0x68);
    At(this, 0x7C) = 0;
    Table80Clear(self + 0x80);
    At(this, 0x94) = 0;
    owner_AC = nullptr;
    IdsClear(&ids);
    IdsFreeNode(&ids, nullptr, ids, 1);
}
namespace {
void __fastcall DestroyHook(CBaseManager* self) { self->CBaseManager::Destroy(); }
} // namespace
EI_REPLACE(0x004031E0, DestroyHook);

// Bucket-by-bucket emptying used by Create (each node freed by `freeNode`), then the element
// count (+0x10) and +0x14 cleared, the table resized for `size` and +0x14 cleared again.
template <typename FreeNode, typename Resize>
static void ResetTable(void* table, FreeNode freeNode, Resize resize, std::size_t size) {
    auto*& begin = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(table) + 4);
    auto*& end = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(table) + 8);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(end - begin); ++i) {
        for (void* node = begin[i]; node;) {
            void* next = *static_cast<void**>(node);
            freeNode(node);
            node = next;
        }
        begin[i] = nullptr;
    }
    At(table, 0x10) = 0;
    At(table, 0x14) = 0;
    resize(table, nullptr, size);
    At(table, 0x14) = 0;
}

int CBaseManager::Create() {
    std::uint8_t* self = reinterpret_cast<std::uint8_t*>(this);
    At(this, 0x98) = 0;
    At(this, 0x9C) = 2000000000;
    ObjectsClear(&objects_);
    maxId_ = 0;
    ObjectsResize(&objects_, nullptr, 0x400);
    maxId_ = 0;
    void* t50 = self + 0x50;
    ResetTable(t50, [t50](void* n) { DeleteNode50(t50, nullptr, n); }, Table50ResizeBig, 0x400);
    void* t68 = self + 0x68;
    ResetTable(t68, [t68](void* n) { Table68FreeNode(static_cast<std::uint8_t*>(t68) + 0x10, nullptr, n, 1); }, Table68Resize, 0x100);
    void* t80 = self + 0x80;
    ResetTable(t80, [](void* n) { stlp::deallocate(n, 0xC); }, Table80ResizeBig, 0x100);
    At(this, 0xA0) = 0;
    At(this, 0xA4) = 300;
    return 1;
}
namespace {
int __fastcall CreateHook(CBaseManager* self) { return self->CBaseManager::Create(); }
} // namespace
EI_REPLACE(0x00403020, CreateHook);

// 004031B0 (slot 13): Init(owner). Keeps the owner at +0xAC and creates (slot 2); a failed create
// clears the owner again. Returns 1 / 0.
int __fastcall BaseManagerInit(CBaseManager* self, void*, void* owner) {
    self->owner_AC = owner;
    if (self->Create()) return 1;
    self->owner_AC = nullptr;
    return 0;
}
EI_REPLACE(0x004031B0, BaseManagerInit);

// The +0x50 entry of `id` (the id's node in the pointer list) erased and that node removed from the
// list (CPtrList::RemoveAt, inlined: {next, prev, data} nodes; the list's blocks are freed when it
// becomes empty).
static void ForgetPosition(CBaseManager* self, int id) {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
    int key = id;
    void* found[2];
    Table50Find(base + 0x50, nullptr, found, &key);
    if (!found[0]) return;
    auto** node = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(found[0]) + 8);
    Table50Erase(base + 0x50, nullptr, found[0], found[1]);
    if (!node) return;
    if (node == reinterpret_cast<void**>(At(self, 0x38))) At(self, 0x38) = reinterpret_cast<std::uint32_t>(node[0]);
    else static_cast<void**>(node[1])[0] = node[0];
    if (node == reinterpret_cast<void**>(At(self, 0x3C))) At(self, 0x3C) = reinterpret_cast<std::uint32_t>(node[1]);
    else static_cast<void**>(node[0])[1] = node[1];
    node[0] = reinterpret_cast<void*>(At(self, 0x44));
    At(self, 0x44) = reinterpret_cast<std::uint32_t>(node);
    if (--At(self, 0x40) == 0) {
        for (void* n = reinterpret_cast<void*>(At(self, 0x38)); n; n = *static_cast<void**>(n)) {
        }
        At(self, 0x40) = 0;
        At(self, 0x44) = 0;
        At(self, 0x3C) = 0;
        At(self, 0x38) = 0;
        PlexFreeDataChain(reinterpret_cast<void*>(At(self, 0x48)));
        At(self, 0x48) = 0;
    }
}

// 00403AE0 (slot 24): an object without references leaves the manager: its id's entry in the +0x50
// table and its node in the pointer list go, then it is destroyed (slot 3), loses its owner and is
// removed (00404040).
void __fastcall BaseManagerReleaseObject(CBaseManager* self, void*, CEngineObject* object) {
    if (!object || object->refCount_ != 0) return;
    ForgetPosition(self, object->id_);
    object->Destroy();
    SetObjectOwner(object, nullptr, nullptr);
    BaseManagerRemoveObject(self, nullptr, object);
}
EI_REPLACE(0x00403AE0, BaseManagerReleaseObject);

// 00403730 (slot 23): CreateObject(type, id). A spare object of the type when there is one (slot
// 14 makes a new one otherwise; null when that fails), registered under `id` or, for -1, under the
// next id (maxId + 1 while inside [+0x98, +0x9C)) or a random free one in that range (100000 tries;
// then the fatal "out of ids" message). New ids join the pointer list. The object gets the manager
// as owner and is created (slot 2).
CEngineObject* __fastcall BaseManagerCreateObject(CBaseManager* self, void*, int type, int id) {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
    int key = type;
    CEngineObject* object = nullptr;
    if (*SpareCountAt(base + 0x80, nullptr, &key) > 0) {
        int typeKey = key;
        void* found[2];
        SpareListFind(base + 0x68, nullptr, found, &typeKey);
        if (found[0]) {
            void* spares = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(found[0]) + 8);
            if (At(spares, 0xC) != 0) object = static_cast<CEngineObject*>(PtrListRemoveHead(spares));
        }
    }
    if (!object) {
        object = ei::VCall<CEngineObject*>(self, 14, type);
        if (!object) return nullptr;
    }
    object->flags_ = 0;
    object->type_ = type;
    if (id == -1) {
        int limit = static_cast<int>(At(self, 0x9C));
        int current = self->maxId_;
        int low = static_cast<int>(At(self, 0x98));
        int chosen;
        if (current < limit && current >= low) {
            chosen = current + 1;
        } else {
            int range = limit - low;
            chosen = -1;
            for (int tries = 0; tries < 100000; ++tries) {
                int r = GameRand() << 15;
                r += GameRand();
                int candidate = r % range + low;
                if (candidate == 0) continue;
                void* hit[2];
                ObjectsFindIt(&self->objects_, nullptr, hit, &candidate);
                if (!hit[0]) {
                    chosen = candidate;
                    break;
                }
            }
        }
        if (chosen == -1) game::FatalError(reinterpret_cast<const char*>(0x00783C00));
        CEngineObject* existing = nullptr;
        if (!ObjectsLookup(&self->objects_, nullptr, &chosen, &existing)) {
            existing = nullptr;
            PtrListAddTail(base + 0x34, nullptr, object);
            void* tail = reinterpret_cast<void*>(At(self, 0x3C));
            *PositionAt(base + 0x50, nullptr, &chosen) = tail;
        }
        ObjectsSetAt(&self->objects_, nullptr, &chosen, &object);
        object->id_ = chosen;
        At(self, 0xA8) = static_cast<std::uint32_t>(chosen);
    } else {
        int wanted = id;
        void* hit[2];
        ObjectsFindIt(&self->objects_, nullptr, hit, &wanted);
        if (!hit[0]) {
            void** node = PtrListNewNode(base + 0x34, nullptr, reinterpret_cast<void*>(At(self, 0x3C)), nullptr);
            node[2] = object;
            if (void** tail = reinterpret_cast<void**>(At(self, 0x3C))) tail[0] = node;
            else At(self, 0x38) = reinterpret_cast<std::uint32_t>(node);
            At(self, 0x3C) = reinterpret_cast<std::uint32_t>(node);
            void* position = reinterpret_cast<void*>(At(self, 0x3C));
            *PositionAt(base + 0x50, nullptr, &id) = position;
        }
        if (id > self->maxId_) self->maxId_ = id;
        int slotKey = id;
        *ObjectsAt(&self->objects_, nullptr, &slotKey) = object;
        object->id_ = id;
        At(self, 0xA8) = static_cast<std::uint32_t>(id);
    }
    SetObjectOwner(object, nullptr, self);
    object->Create();
    return object;
}
EI_REPLACE(0x00403730, BaseManagerCreateObject);

// A node {next, prev, data} for `data` at the tail of an MFC CPtrList (AddTail, inlined).
static void LinkTail(void* list, void* data) {
    void** node = PtrListNewNode(list, nullptr, reinterpret_cast<void*>(At(list, 8)), nullptr);
    node[2] = data;
    if (void** tail = reinterpret_cast<void**>(At(list, 8))) tail[0] = node;
    else At(list, 4) = reinterpret_cast<std::uint32_t>(node);
    At(list, 8) = reinterpret_cast<std::uint32_t>(node);
}

// 00404040: RemoveObject(object). An object registered under its id leaves the objects table, the
// +0x50 table and the pointer list; then it becomes a spare of its type (a CPtrList per type at
// +0x68, created on first use; the type's spare count at +0x80 goes up) with id -1. Returns 0.
int __fastcall BaseManagerRemoveObject(CBaseManager* self, void*, CEngineObject* object) {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
    int id = object->id_;
    int type = object->type_;
    int key = id;
    void* found[2];
    ObjectsFindIt2(&self->objects_, nullptr, found, &key);
    if (found[0] && *reinterpret_cast<CEngineObject**>(static_cast<std::uint8_t*>(found[0]) + 8) == object) {
        key = id;
        CEngineObject* value = nullptr;
        if (ObjectsLookup(&self->objects_, nullptr, &key, &value)) {
            ObjectsRemoveKey(&self->objects_, nullptr, &key);
            if (void* position = PositionRemoveKey(base + 0x50, nullptr, &key)) PtrListRemoveAt(base + 0x34, nullptr, position);
        }
    }
    int typeKey = type;
    void* spare[2];
    SpareListFind2(base + 0x68, nullptr, spare, &typeKey);
    if (spare[0]) {
        void* list = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(spare[0]) + 8);
        LinkTail(list, object);
        object->id_ = -1;
        int count = *SpareCountAt(base + 0x80, nullptr, &type) + 1;
        if (type > static_cast<int>(At(self, 0x94))) At(self, 0x94) = static_cast<std::uint32_t>(type);
        int countKey = type;
        *SpareCountAt2(base + 0x80, nullptr, &countKey) = count;
        return 0;
    }
    // First spare of this type: a new CPtrList (block size 10).
    auto* list = static_cast<std::uint8_t*>(gamecrt::operator_new(0x1C));
    if (list) {
        At(list, 0xC) = 0;
        At(list, 0x10) = 0;
        At(list, 8) = 0;
        At(list, 4) = 0;
        At(list, 0x14) = 0;
        SetVtable(list, kPtrListVtable);
        At(list, 0x18) = 10;
    }
    if (type > static_cast<int>(At(self, 0x7C))) At(self, 0x7C) = static_cast<std::uint32_t>(type);
    int listKey = type;
    *SpareListAt(base + 0x68, nullptr, &listKey) = list;
    LinkTail(list, object);
    object->id_ = -1;
    if (type > static_cast<int>(At(self, 0x94))) At(self, 0x94) = static_cast<std::uint32_t>(type);
    int countKey = type;
    int count = *SpareCountAt2(base + 0x80, nullptr, &countKey) + 1;
    if (type > static_cast<int>(At(self, 0x94))) At(self, 0x94) = static_cast<std::uint32_t>(type);
    int pairKey = type;
    int zero = 0;
    int pair[2];
    MakeTypePair(pair, nullptr, &pairKey, &zero);
    SpareCountInsert(base + 0x80, nullptr, pair)[1] = count;
    return 0;
}
EI_REPLACE(0x00404040, BaseManagerRemoveObject);

// 004034E0 (slot 15): every object gets a reference, then is destroyed (slot 3), then released
// (slot 4; objects nothing else holds go back to the spares). Then the objects table, the pointer
// list and the +0x50 table are emptied.
void __fastcall BaseManagerDestroyAll(CBaseManager* self) {
    ForEachObject(self, [](CEngineObject* o) { ++o->refCount_; });
    ForEachObject(self, [](CEngineObject* o) { o->Destroy(); });
    ForEachObject(self, [](CEngineObject* o) { o->Release(); });
    self->maxId_ = 0;
    ObjectsClear(&self->objects_);
    // CPtrList::RemoveAll.
    for (void* node = reinterpret_cast<void*>(At(self, 0x38)); node; node = *static_cast<void**>(node)) {
    }
    At(self, 0x40) = 0;
    At(self, 0x44) = 0;
    At(self, 0x3C) = 0;
    At(self, 0x38) = 0;
    PlexFreeDataChain(reinterpret_cast<void*>(At(self, 0x48)));
    At(self, 0x48) = 0;
    At(self, 0x64) = 0;
    Table50Clear(reinterpret_cast<std::uint8_t*>(self) + 0x50);
}
EI_REPLACE(0x004034E0, BaseManagerDestroyAll);

// 004039B0: Unregister(id). The object registered under `id` leaves the objects table, the +0x50
// table and the pointer list (it stays alive and keeps its id); returns it, or null.
CEngineObject* __fastcall BaseManagerUnregister(CBaseManager* self, void*, int id) {
    int key = id;
    void* found[2];
    ObjectsFindIt2(&self->objects_, nullptr, found, &key);
    if (!found[0]) return nullptr;
    CEngineObject* object = *reinterpret_cast<CEngineObject**>(static_cast<std::uint8_t*>(found[0]) + 8);
    key = id;
    ObjectsFindIt3(&self->objects_, nullptr, found, &key);
    if (found[0]) ObjectsErase(&self->objects_, nullptr, found[0], found[1]);
    ForgetPosition(self, id);
    return object;
}
EI_REPLACE(0x004039B0, BaseManagerUnregister);

// 00403BD0: RegisterInstant(object). Registers an object made elsewhere under the next id (maxId + 1
// while inside [+0x98, +0x9C)) or a random free one in that range (100000 tries, then the fatal
// "CBaseManager::RegisterInstant" message and id -1). A new id joins the pointer list. The object
// gets the id and the manager as owner; returns the id.
int __fastcall BaseManagerRegisterInstant(CBaseManager* self, void*, CEngineObject* object) {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
    int limit = static_cast<int>(At(self, 0x9C));
    int current = self->maxId_;
    int low = static_cast<int>(At(self, 0x98));
    int id;
    if (current < limit && current >= low) {
        id = current + 1;
    } else {
        int range = limit - low;
        id = -1;
        for (int tries = 0; tries < 100000; ++tries) {
            int r = GameRand() << 15;
            r += GameRand();
            int candidate = r % range + low;
            if (candidate == 0) continue;
            int key = candidate;
            void* hit[2];
            ObjectsFindIt2(&self->objects_, nullptr, hit, &key);
            if (!hit[0]) {
                id = candidate;
                break;
            }
        }
    }
    if (id == -1) game::FatalError(reinterpret_cast<const char*>(0x00783C00));
    int key = id;
    void* found[2];
    ObjectsFindIt2(&self->objects_, nullptr, found, &key);
    if (!found[0]) {
        void** node = PtrListNewNode(base + 0x34, nullptr, reinterpret_cast<void*>(At(self, 0x3C)), nullptr);
        node[2] = object;
        if (void** tail = reinterpret_cast<void**>(At(self, 0x3C))) tail[0] = node;
        else At(self, 0x38) = reinterpret_cast<std::uint32_t>(node);
        At(self, 0x3C) = reinterpret_cast<std::uint32_t>(node);
        void* position = reinterpret_cast<void*>(At(self, 0x3C));
        if (id > static_cast<int>(At(self, 0x64))) At(self, 0x64) = static_cast<std::uint32_t>(id);
        int positionKey = id;
        *PositionAt2(base + 0x50, nullptr, &positionKey) = position;
    }
    if (id > self->maxId_) self->maxId_ = id;
    int pair[2] = {id, 0};
    ObjectsFindOrInsert(&self->objects_, nullptr, pair)[1] = reinterpret_cast<int>(object);
    object->id_ = id;
    At(self, 0xA8) = static_cast<std::uint32_t>(id);
    SetObjectOwner(object, nullptr, self);
    return id;
}
EI_REPLACE(0x00403BD0, BaseManagerRegisterInstant);

// Every node of every bucket of a hashtable member freed by `freeNode`, the buckets cleared.
template <typename FreeNode>
static void FreeBuckets(void* table, FreeNode freeNode) {
    auto*& begin = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(table) + 4);
    auto*& end = *reinterpret_cast<void***>(static_cast<std::uint8_t*>(table) + 8);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(end - begin); ++i) {
        for (void* node = begin[i]; node;) {
            void* next = *static_cast<void**>(node);
            freeNode(node);
            node = next;
        }
        begin[i] = nullptr;
    }
}

// 00404410: frees the spares. Each type's CPtrList (+0x68) deletes its objects (slot 0), is emptied
// (RemoveAll, inlined) and deleted (its slot 1); then the +0x68 and +0x80 tables are emptied.
void __fastcall BaseManagerFreeRecords(CBaseManager* self) {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
    void* it[2];
    Table68Begin(base + 0x68, nullptr, it);
    while (it[0]) {
        auto* list = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(it[0]) + 8);
        if (!list) break;
        for (void** n = reinterpret_cast<void**>(At(list, 4)); n;) {
            void** next = static_cast<void**>(n[0]);
            if (n[2]) ei::VCall<void*>(n[2], 0, 1u);
            n = next;
        }
        for (void* n = reinterpret_cast<void*>(At(list, 4)); n; n = *static_cast<void**>(n)) {
        }
        At(list, 0xC) = 0;
        At(list, 0x10) = 0;
        At(list, 8) = 0;
        At(list, 4) = 0;
        PlexFreeDataChain(reinterpret_cast<void*>(At(list, 0x14)));
        At(list, 0x14) = 0;
        ei::VCall<void*>(list, 1, 1u);
        void* next = *static_cast<void**>(it[0]);
        if (!next) next = Table68NextBucket(it);
        it[0] = next;
    }
    At(self, 0x7C) = 0;
    FreeBuckets(base + 0x68, [base](void* n) { Table68FreeNode(base + 0x78, nullptr, n, 1); });
    At(self, 0x78) = 0;
    At(self, 0x94) = 0;
    FreeBuckets(base + 0x80, [base](void* n) { Table80FreeNode(base + 0x90, nullptr, n, 1); });
    At(self, 0x90) = 0;
}
EI_REPLACE(0x00404410, BaseManagerFreeRecords);

namespace {
// The archive stream's slots (CArchiveStream-like: 1 Read(buffer, size), 2 Write(buffer, size)).
constexpr int kStreamRead = 1;
constexpr int kStreamWrite = 2;
// Id written after the last object by SaveObjects; an object with this id is followed by a 1 byte.
constexpr int kEndOfObjects = -0x521;

void StreamRead(void* stream, void* buffer, unsigned size) { ei::VCall<int>(stream, kStreamRead, buffer, size); }
void StreamWrite(void* stream, const void* buffer, unsigned size) {
    ei::VCall<int>(stream, kStreamWrite, buffer, size);
}
} // namespace

// 004046E0 (slot 9): Load(stream). The spares are freed; then `count` records {id, type, +0x18} each
// create an object (slot 23, CreateObject(type, id)) that gets the +0x18 value.
void CBaseManager::Load(void* stream) {
    BaseManagerFreeRecords(this);
    int count;
    StreamRead(stream, &count, 4);
    for (int i = 0; i < count; ++i) {
        int id, type, extra;
        StreamRead(stream, &id, 4);
        StreamRead(stream, &type, 4);
        StreamRead(stream, &extra, 4);
        auto* object = ei::VCall<CEngineObject*>(this, 23, type, id);
        object->unknown_18 = static_cast<std::uint32_t>(extra);
    }
}

// 00404760 (slot 10): Save(stream). The pointer list's count, then {id, type, +0x18} per object.
void CBaseManager::Save(void* stream) {
    int count = static_cast<int>(At(this, 0x40));
    StreamWrite(stream, &count, 4);
    for (void** node = reinterpret_cast<void**>(At(this, 0x38)); node;) {
        void** next = static_cast<void**>(node[0]);
        auto* object = static_cast<CEngineObject*>(node[2]);
        int value = object->id_;
        StreamWrite(stream, &value, 4);
        value = object->type_;
        StreamWrite(stream, &value, 4);
        value = static_cast<int>(object->unknown_18);
        StreamWrite(stream, &value, 4);
        node = next;
    }
}
namespace {
void __fastcall LoadHook(CBaseManager* self, void*, void* stream) { self->CBaseManager::Load(stream); }
void __fastcall SaveHook(CBaseManager* self, void*, void* stream) { self->CBaseManager::Save(stream); }
} // namespace
EI_REPLACE(0x004046E0, LoadHook);
EI_REPLACE(0x00404760, SaveHook);

// 004047E0: LoadObjects(stream). Records {id, object data (slot 9)} until the end mark followed by a
// 0 byte (with a 1 byte the mark is an object id after all); stops at an id not registered.
void __fastcall BaseManagerLoadObjects(CBaseManager* self, void*, void* stream) {
    for (;;) {
        int id;
        StreamRead(stream, &id, 4);
        if (id == kEndOfObjects) {
            char more;
            StreamRead(stream, &more, 1);
            if (!more) return;
        }
        // The objects table's find, inlined: bucket id % bucket count, nodes {next, key, value}.
        auto** buckets = reinterpret_cast<void***>(At(self, 0x20));
        std::uint32_t count = static_cast<std::uint32_t>(static_cast<int>(At(self, 0x24) - At(self, 0x20)) >> 2);
        void** node = buckets[static_cast<std::uint32_t>(id) % count];
        while (node && reinterpret_cast<int>(node[1]) != id) node = static_cast<void**>(node[0]);
        if (!node) return;
        auto* object = static_cast<CEngineObject*>(node[2]);
        if (!object) return;
        object->Load(stream);
    }
}
EI_REPLACE(0x004047E0, BaseManagerLoadObjects);

// 00404860: SaveObjects(stream). {id, [1 byte if the id is the end mark], object data (slot 10)}
// per object in list order, then the end mark and a 0 byte.
void __fastcall BaseManagerSaveObjects(CBaseManager* self, void*, void* stream) {
    for (void** node = reinterpret_cast<void**>(At(self, 0x38)); node;) {
        void** next = static_cast<void**>(node[0]);
        auto* object = static_cast<CEngineObject*>(node[2]);
        int id = object->id_;
        StreamWrite(stream, &id, 4);
        if (object->id_ == kEndOfObjects) {
            char one = 1;
            StreamWrite(stream, &one, 1);
        }
        object->Save(stream);
        node = next;
    }
    int mark = kEndOfObjects;
    StreamWrite(stream, &mark, 4);
    char zero = 0;
    StreamWrite(stream, &zero, 1);
}
EI_REPLACE(0x00404860, BaseManagerSaveObjects);

// 004048F0: every object without references gets one and releases it (slot 4), so the ones nobody
// took go back to the spares.
void __fastcall BaseManagerReleaseUnused(CBaseManager* self) {
    for (void** node = reinterpret_cast<void**>(At(self, 0x38)); node;) {
        auto* object = static_cast<CEngineObject*>(node[2]);
        node = static_cast<void**>(node[0]);
        if (object->refCount_ == 0) {
            object->refCount_ = 1;
            object->Release();
        }
    }
}
EI_REPLACE(0x004048F0, BaseManagerReleaseUnused);

// 00403720: SetContext(context): the manager's +0xAC (as Init keeps it).
void __fastcall BaseManagerSetContext(CBaseManager* self, void*, void* context) { self->owner_AC = context; }
EI_REPLACE(0x00403720, BaseManagerSetContext);

// The manager vtable's empty defaults (slots 16, 17, 19, 20, 21); slots 5, 6, 18 and 22 are shorter
// than the hook's jump and stay original.
int __fastcall BaseManagerSlot16(CBaseManager*, void*, int) { return 0; }
int __fastcall BaseManagerSlot17(CBaseManager*, void*, int) { return 0; }
int __fastcall BaseManagerSlot19(CBaseManager*, void*, int, int) { return 0; }
bool __fastcall BaseManagerSlot20(CBaseManager*, void*, int) { return false; }
int __fastcall BaseManagerSlot21(CBaseManager*, void*, int) { return 0; }
EI_REPLACE(0x00402D60, BaseManagerSlot16);
EI_REPLACE(0x00402D70, BaseManagerSlot17);
EI_REPLACE(0x00402D90, BaseManagerSlot19);
EI_REPLACE(0x00402DA0, BaseManagerSlot20);
EI_REPLACE(0x00402DB0, BaseManagerSlot21);
