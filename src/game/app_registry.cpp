// Registers an object type with a factory in a two-way type registry (called twice by WinMain): a prototype
// made by the factory gives the vtable; vtable -> id in the STLport hash_map at +0, id -> factory in the one
// at +0x14 (the same scheme as the network's TypeRegistry::Register). See docs/notes/app.md.
#include "chunk_archive.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/stlp.h"

#include <unknwn.h>

#include <cstdint>
#include <cstring>

namespace app {

namespace {

} // namespace

void __fastcall TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, void*, std::uint32_t id,
                                     ChunkArchive::Factory factory) {
    CRefCounted* prototype = factory();
    if (prototype) ++prototype->refs_;
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype, 4);
    registry->classIds.FindOrInsert({vtable, 0})->value = id;
    registry->factories.FindOrInsert({id, nullptr})->value = factory;
    if (prototype && --prototype->refs_ == 0) delete prototype;
}
EI_REWRITTEN(0x00458570, TypeRegistryRegister);

// Destructors of WinMain's locals, reached only when an exception unwinds it: two COM pointers
// (Release) and a counted object (deleted at its last reference).
void __fastcall ComPointerRelease(void** self) {
    if (void* object = *self) static_cast<IUnknown*>(object)->Release();
}
EI_REWRITTEN(0x00458540, ComPointerRelease);

} // namespace app
