// Registers an object type with a factory in a two-way type registry (called twice by WinMain): a prototype
// made by the factory gives the vtable; vtable -> id in one map, id -> factory in the other (the same scheme as
// the network's TypeRegistry::Register). See docs/notes/app.md.
#include "chunk_archive.h"
#include "core_refcount.h"
#include "lib/bytes.h"


#include <unknwn.h>

#include <cstdint>
#include <cstring>

namespace app {

void TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, std::uint32_t id,
                          ChunkArchive::Factory factory) {
    RefCounted* prototype = factory();
    if (prototype) prototype->AddRef();
    std::uint32_t vtable;
    lib::LoadBytes(vtable, prototype);
    registry->classIds[vtable] = static_cast<std::int32_t>(id);
    registry->factories[id] = factory;
    if (prototype) prototype->Release();
}

} // namespace app
