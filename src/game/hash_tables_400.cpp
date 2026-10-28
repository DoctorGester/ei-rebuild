// Hash tables of one-dword keys and values: CBaseManager's id tables (MFC-style SetAt / Lookup / RemoveKey over
// a hashtable that also keeps the highest key), the chunk archive's class registry and the session's type
// registry. See docs/notes/core.md.
#include "hash_tables_400.h"

#include "base_manager.h"
#include "world.h"

namespace hash400 {

// --- The registries ------------------------------------------------------------------------------

// The chunk archive's class registry (vtable -> class id, then class id -> factory), built by a static
// initialiser. The empty functors are copied from an uninitialised stack byte in the original (0 here).
void* ClassRegistryConstruct(ChunkArchive::ClassRegistry* registry) {
    registry->classIds.Construct(100);
    registry->factories.Construct(100);
    return registry;
}
// Its destructor (registered with atexit by the initialiser): the factories, then the class ids.
void ClassRegistryDestruct(ChunkArchive::ClassRegistry* registry) {
    registry->factories.Destroy();
    registry->classIds.Destroy();
}

} // namespace hash400
