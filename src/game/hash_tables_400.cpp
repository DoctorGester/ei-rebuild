// Hash tables (STLport hashtables of one-dword keys and values):
// - the id tables of CBaseManager: MFC-style SetAt / Lookup / RemoveKey, find and
// operator[] over a hashtable that also keeps the highest key inserted;
// - the class registry of the chunk archive (vtable -> class id, then class id -> factory) and the session's
// type registry: constructor and destructors, which the compiler inlined from the
// hashtable's.
// See docs/notes/core.md.
#include "hash_tables_400.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

namespace hash400 {

namespace {

using Node = Table::Map::Node;

// operator[] as these instantiations inline it: the highest key kept, then find_or_insert of {key, 0}.
Node* FindOrInsert(Table* t, const std::int32_t* key) {
    if (*key > t->maxKey) t->maxKey = *key;
    return t->map.FindOrInsert({static_cast<std::uint32_t>(*key), 0});
}

// RemoveKey: the value of `key`'s node (0 when there is none), the node erased.
std::uint32_t RemoveKey(Table* t, const std::uint32_t* key) {
    Node* node = t->map.Find(*key);
    if (!node) return 0;
    std::uint32_t value = node->value;
    t->map.Erase(node);
    return value;
}

} // namespace

// --- CBaseManager's id tables --------------------------------------------------------------------

void Table::ObjectsSetAt(const std::int32_t* key, const std::uint32_t* value) {
    FindOrInsert(this, key)->value = *value;
}
std::uint32_t Table::ObjectsRemoveKey(const std::uint32_t* key) { return RemoveKey(this, key); }
int Table::ObjectsLookup(const std::uint32_t* key, std::uint32_t* value) {
    Node* node = map.Find(*key);
    if (!node) return 0;
    *value = node->value;
    return 1;
}
// RemoveKey of the id -> list position table (+0x50; also the client world's pending table).
std::uint32_t Table::PositionsRemoveKey(const std::uint32_t* key) { return RemoveKey(this, key); }
std::uint32_t* Table::ObjectsFind(const std::uint32_t* key) {
    Node* node = map.Find(*key);
    return node ? &node->value : nullptr;
}
std::uint32_t* Table::PositionAt(const std::int32_t* key) {
    return &FindOrInsert(this, key)->value;
}
std::uint32_t* Table::SpareCountAt(const std::int32_t* key) {
    return &FindOrInsert(this, key)->value;
}

// --- The registries ------------------------------------------------------------------------------

// The chunk archive's class registry (vtable -> class id, then class id -> factory), built by a static
// initialiser. The empty functors are copied from an uninitialised stack byte in the original (0 here).
void* __fastcall ClassRegistryConstruct(ChunkArchive::ClassRegistry* registry) {
    registry->classIds.Construct(100);
    registry->factories.Construct(100);
    return registry;
}
// Its destructor (registered with atexit by the initialiser): the factories, then the class ids.
void __fastcall ClassRegistryDestruct(ChunkArchive::ClassRegistry* registry) {
    registry->factories.Destroy();
    registry->classIds.Destroy();
}
// The session type registry's destructor (reached from the session constructor's unwind
// funclets): byType (+0x14), then byVtable.
void __fastcall TypeRegistryDestruct(net::TypeRegistry* registry) {
    registry->byType.Destroy();
    registry->byVtable.Destroy();
}

} // namespace hash400

EI_REWRITTEN(0x00599A80, hash400::Table::ObjectsRemoveKey);
EI_REWRITTEN(0x00599AE0, hash400::Table::ObjectsFind);
EI_REWRITTEN(0x00406A10, hash400::Table::PositionAt);
EI_REWRITTEN(0x00406AA0, hash400::Table::SpareCountAt);
EI_REWRITTEN(0x00404E80, hash400::Table::ObjectsSetAt);
EI_REWRITTEN(0x00404F20, hash400::Table::ObjectsRemoveKey);
EI_REWRITTEN(0x00404F80, hash400::Table::ObjectsLookup);
EI_REWRITTEN(0x00405350, hash400::Table::PositionsRemoveKey);
EI_REWRITTEN(0x00406950, hash400::Table::ObjectsFind);
EI_REWRITTEN(0x00408EA0, hash400::ClassRegistryConstruct);
EI_REWRITTEN(0x00408D60, hash400::ClassRegistryDestruct);
EI_REWRITTEN(0x004368F0, hash400::TypeRegistryDestruct);
