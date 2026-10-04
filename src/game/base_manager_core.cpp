// CBaseManager construction and destruction (00402B20-00403010) and the small default slots of the
// engine object and manager vtables (0073B8E0, 0073B870). See base_manager.h and
// docs/notes/unmapped_410_470.md ("Fifth round").
#include "base_manager.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

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
// 004031E0: CBaseManager::Destroy (slot 3), called directly by the destructor; not rewritten yet.
__declspec(naked) void __fastcall BaseManagerDestroy(CBaseManager* /*manager*/) EI_ORIGINAL(0x004031E0)
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
    BaseManagerDestroy(this);
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
