// Hash tables of one-dword keys and values: CBaseManager's id tables (MFC-style SetAt / Lookup / RemoveKey over
// a hashtable that also keeps the highest key), the chunk archive's class registry and the session's type
// registry. See docs/notes/core.md.
#include "hash_tables_400.h"

#include "base_manager.h"
#include "world.h"

#include "ei/hook.h"

namespace hash400 {

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

} // namespace hash400

namespace {
using MapObjectIds = decltype(MapObjectIdMap::map);
} // namespace
// CBaseManager's id tables (SetAt, RemoveKey, Lookup and find of the objects; RemoveKey of the positions, also the
// client world's dynamic nodes'; operator[] of the positions and the spare counts) and the world's object maps.
EI_REWRITTEN(0x00404E80, hash400::SetAt<CBaseManager::ObjectMap>);
EI_REWRITTEN(0x00404F20, hash400::RemoveKey<CBaseManager::ObjectMap>);
EI_REWRITTEN(0x00404F80, hash400::Lookup<CBaseManager::ObjectMap>);
EI_REWRITTEN(0x00406950, hash400::Find<CBaseManager::ObjectMap>);
EI_REWRITTEN(0x00405350, hash400::RemoveKey<CBaseManager::PositionMap>);
EI_REWRITTEN(0x00406A10, hash400::Entry<CBaseManager::PositionMap>);
EI_REWRITTEN(0x00406AA0, hash400::Entry<CBaseManager::SpareCountMap>);
EI_REWRITTEN(0x00599A80, hash400::RemoveKey<MapObjectIds>);
EI_REWRITTEN(0x00599AE0, hash400::Find<MapObjectIds>);
EI_REWRITTEN(0x00408EA0, hash400::ClassRegistryConstruct);
EI_REWRITTEN(0x00408D60, hash400::ClassRegistryDestruct);
