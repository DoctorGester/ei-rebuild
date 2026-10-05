// Hash tables of 00400000-00440000 (STLport hashtables of one-dword keys and values):
//  - the id tables of CBaseManager (base_manager_core.cpp): MFC-style SetAt / Lookup / RemoveKey,
//    find and operator[] over a hashtable that also keeps the highest key inserted (+0x14);
//  - the class registry of the chunk archive (0079B270: vtable -> class id, then class id ->
//    factory) and the session's type registry (net_classes.h): constructor and destructors, which
//    the compiler inlined from the hashtable's.
// The tables' own resize, node allocation and erase steps (library) are called through their
// original addresses; each instantiation has its own copies of them. See
// docs/notes/unmapped_410_470.md ("Eighth round").
#include "hash_tables_400.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "originals_library.h"

namespace hash400 {

struct Node {
    Node* next;
    std::uint32_t key;
    std::uint32_t value;
};

using Resize = void(__fastcall*)(Table*, void*, std::uint32_t);
using NewNode = Node*(__fastcall*)(Table*, void*, const std::uint32_t*);
using Erase = void(__fastcall*)(Table*, void*, Node*, Table*);
using FreeNode = void(__fastcall*)(void* /*allocator*/, void*, Node*, int);

namespace {

// Library members of each instantiation.
void __fastcall ObjectsResize(Table* a0, void*, std::uint32_t a2) { ObjectsResizeOriginal((void*)a0, nullptr, a2); }
__declspec(naked) Node* __fastcall ObjectsNewNode(Table*, void*, const std::uint32_t*) EI_ORIGINAL(0x00407420)
void __fastcall ObjectsErase(Table* a0, void*, Node* a2, Table* a3) { ObjectsEraseOriginal((void*)a0, nullptr, (void*)a2, (const void*)a3); }
void __fastcall PositionsResize(Table* a0, void*, std::uint32_t a2) { PositionsResizeOriginal((void*)a0, nullptr, a2); }
__declspec(naked) Node* __fastcall PositionsNewNode(Table*, void*, const std::uint32_t*) EI_ORIGINAL(0x004074F0)
void __fastcall PositionsErase(Table* a0, void*, Node* a2, Table* a3) { HashEraseOriginal((void*)a0, nullptr, (void*)a2, (const void*)a3); }
void __fastcall SpareCountsResize(Table* a0, void*, std::uint32_t a2) { SpareCountsResizeOriginal((void*)a0, nullptr, a2); }
__declspec(naked) Node* __fastcall SpareCountsNewNode(Table*, void*, const std::uint32_t*) EI_ORIGINAL(0x00408B20)
__declspec(naked) void __fastcall FactoriesFreeNode(void*, void*, Node*, int) EI_ORIGINAL(0x0040B5A0)
__declspec(naked) void __fastcall ByVtableFreeNode(void*, void*, Node*, int) EI_ORIGINAL(0x0040B560)
__declspec(naked) void __fastcall ByTypeFreeNode(void*, void*, Node*, int) EI_ORIGINAL(0x0043E0A0)
void __fastcall NodeFree(void* a0, std::uint32_t a1) { StlNodeFreeOriginal(a0, (std::size_t)a1); }
// The class registry's construction steps (library): bucket vector constructors, initial buckets
// (first table), the bucket count for a hint, reserve and fill-insert (second table).
__declspec(naked) void __fastcall ClassIdsBucketsConstruct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0040ADE0)
__declspec(naked) void __fastcall ClassIdsInitBuckets(Table*, void*, std::uint32_t /*hint*/) EI_ORIGINAL(0x0040AA80)
__declspec(naked) void __fastcall FactoriesBucketsConstruct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0040AE50)
__declspec(naked) std::uint32_t __fastcall FactoriesBucketCount(Table*, void*, std::uint32_t /*hint*/) EI_ORIGINAL(0x0040AE00)
__declspec(naked) void __fastcall FactoriesBucketsReserve(void* /*vector*/, void*, std::uint32_t /*n*/) EI_ORIGINAL(0x0040AF00)
__declspec(naked) void __fastcall FactoriesBucketsFill(void* /*vector*/, void*, void** /*pos*/, std::uint32_t /*n*/, void* const* /*value*/) EI_ORIGINAL(0x0040B150)

// The bucket count as the original computes it (byte size shifted, then divided unsigned).
std::uint32_t Buckets(const Table* t) {
    return static_cast<std::uint32_t>((reinterpret_cast<std::intptr_t>(t->bucketsEnd) - reinterpret_cast<std::intptr_t>(t->buckets)) >> 2);
}
Node* Head(const Table* t, std::uint32_t key) { return static_cast<Node*>(t->buckets[key % Buckets(t)]); }
Node* Find(const Table* t, std::uint32_t key) {
    for (Node* n = Head(t, key); n; n = n->next)
        if (n->key == key) return n;
    return nullptr;
}

// operator[] as these instantiations inline it: the highest key kept, the table resized for one
// more element, a {key, 0} node put at the head of its bucket when the key is new.
Node* FindOrInsert(Table* t, const std::int32_t* key, Resize resize, NewNode newNode) {
    if (*key > t->maxKey) t->maxKey = *key;
    std::uint32_t pair[2] = {static_cast<std::uint32_t>(*key), 0};
    resize(t, nullptr, t->count + 1);
    std::uint32_t bucket = pair[0] % Buckets(t);
    Node* head = static_cast<Node*>(t->buckets[bucket]);
    for (Node* n = head; n; n = n->next)
        if (n->key == pair[0]) return n;
    Node* node = newNode(t, nullptr, pair);
    node->next = head;
    t->buckets[bucket] = node;
    ++t->count;
    return node;
}

// RemoveKey: the value of `key`'s node (0 when there is none), the node erased.
std::uint32_t RemoveKey(Table* t, const std::uint32_t* key, Erase erase) {
    Node* node = Find(t, *key);
    if (!node) return 0;
    std::uint32_t value = node->value;
    erase(t, nullptr, node, t);
    return value;
}

// A hashtable's destructor as inlined: every chain freed and its bucket cleared, the count reset,
// then the bucket vector freed.
void Destroy(Table* t, FreeNode freeNode) {
    if (Buckets(t) != 0) {
        std::uint32_t i = 0;
        do {
            for (Node* n = static_cast<Node*>(t->buckets[i]); n;) {
                Node* next = n->next;
                freeNode(&t->count, nullptr, n, 1);
                n = next;
            }
            t->buckets[i] = nullptr;
            ++i;
        } while (i < Buckets(t));
    }
    t->count = 0;
    if (t->buckets) {
        auto bytes = static_cast<std::uint32_t>((reinterpret_cast<std::intptr_t>(t->bucketsEos) - reinterpret_cast<std::intptr_t>(t->buckets)) >> 2) * 4;
        NodeFree(t->buckets, bytes);
    }
}

} // namespace

// --- CBaseManager's id tables --------------------------------------------------------------------

// 00404E80: the objects table's SetAt (key -> object).
void __fastcall ObjectsSetAt(Table* t, void*, const std::int32_t* key, const std::uint32_t* value) {
    FindOrInsert(t, key, ObjectsResize, ObjectsNewNode)->value = *value;
}
// 00404F20: the objects table's RemoveKey (returns the object or null).
std::uint32_t __fastcall ObjectsRemoveKey(Table* t, void*, const std::uint32_t* key) { return RemoveKey(t, key, ObjectsErase); }
// 00404F80: the objects table's Lookup: 1 with *value set, or 0.
int __fastcall ObjectsLookup(Table* t, void*, const std::uint32_t* key, std::uint32_t* value) {
    Node* node = Find(t, *key);
    if (!node) return 0;
    *value = node->value;
    return 1;
}
// 00405350: RemoveKey of the id -> list position table (+0x50; also the client world's pending table).
std::uint32_t __fastcall PositionsRemoveKey(Table* t, void*, const std::uint32_t* key) { return RemoveKey(t, key, PositionsErase); }
// 00406950: the objects table's find: the value's address, or null.
std::uint32_t* __fastcall ObjectsFind(Table* t, void*, const std::uint32_t* key) {
    Node* node = Find(t, *key);
    return node ? &node->value : nullptr;
}
// 00406A10 / 00406AA0: operator[] of the position table (+0x50) and of the spare counts (+0x80).
std::uint32_t* __fastcall PositionAt(Table* t, void*, const std::int32_t* key) {
    return &FindOrInsert(t, key, PositionsResize, PositionsNewNode)->value;
}
std::uint32_t* __fastcall SpareCountAt(Table* t, void*, const std::int32_t* key) {
    return &FindOrInsert(t, key, SpareCountsResize, SpareCountsNewNode)->value;
}

// --- The registries ------------------------------------------------------------------------------

// 00408EA0: the chunk archive's class registry (0079B270: vtable -> class id, then at +0x14 class
// id -> factory), built by the static initialiser 00408D30 before the hooks are installed. The
// empty functors are copied from an uninitialised stack byte in the original (0 here).
void* __fastcall ClassRegistryConstruct(ChunkArchive::ClassRegistry* registry) {
    Table* self = AsTable(&registry->classIds);
    std::uint8_t functors = 0;
    self->functors[0] = self->functors[1] = self->functors[2] = functors;
    ClassIdsBucketsConstruct(&self->buckets, nullptr, &functors);
    self->count = 0;
    ClassIdsInitBuckets(self, nullptr, 100);
    Table* factories = AsTable(&registry->factories);
    factories->functors[0] = factories->functors[2] = factories->functors[1] = functors;
    FactoriesBucketsConstruct(&factories->buckets, nullptr, &functors);
    factories->count = 0;
    std::uint32_t n = FactoriesBucketCount(factories, nullptr, 100);
    FactoriesBucketsReserve(&factories->buckets, nullptr, n);
    void* none = nullptr;
    FactoriesBucketsFill(&factories->buckets, nullptr, factories->bucketsEnd, n, &none);
    factories->count = 0;
    return registry;
}
// 00408D60: its destructor (registered with atexit by 00408D30): the factories, then the class ids.
void __fastcall ClassRegistryDestruct(ChunkArchive::ClassRegistry* registry) {
    Destroy(AsTable(&registry->factories), FactoriesFreeNode);
    Destroy(AsTable(&registry->classIds), ByVtableFreeNode);
}
// 004368F0: the session type registry's destructor (reached from the session constructor's unwind
// funclets): byType (+0x14), then byVtable.
void __fastcall TypeRegistryDestruct(net::TypeRegistry* registry) {
    Destroy(AsTable(&registry->byType), ByTypeFreeNode);
    Destroy(AsTable(&registry->byVtable), ByVtableFreeNode);
}

} // namespace hash400

EI_REPLACE(0x00404E80, hash400::ObjectsSetAt);
EI_REPLACE(0x00404F20, hash400::ObjectsRemoveKey);
EI_REPLACE(0x00404F80, hash400::ObjectsLookup);
EI_REPLACE(0x00405350, hash400::PositionsRemoveKey);
EI_REPLACE(0x00406950, hash400::ObjectsFind);
EI_REPLACE(0x00406A10, hash400::PositionAt);
EI_REPLACE(0x00406AA0, hash400::SpareCountAt);
EI_REPLACE(0x00408EA0, hash400::ClassRegistryConstruct);
EI_REPLACE(0x00408D60, hash400::ClassRegistryDestruct);
EI_REPLACE(0x004368F0, hash400::TypeRegistryDestruct);
