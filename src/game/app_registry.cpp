// 00458570: registers an object type with a factory in a two-way type registry (called twice by
// WinMain): a prototype made by the factory gives the vtable; vtable -> id in the STLport
// hash_map at +0, id -> factory in the one at +0x14 (the same scheme as the network's
// TypeRegistry::Register, 0042A890). See docs/notes/app.md.
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/stlp.h"

#include <unknwn.h>

#include <cstdint>
#include <cstring>

namespace app {

namespace {

// The two maps (STLport hash_map<uint32, uint32>, 0x14 bytes each): vtable -> id, id -> factory.
using Table = stlp::hash_map<std::uint32_t, std::uint32_t, stlp::IntHash>;
static_assert(sizeof(Table) == 0x14);

} // namespace

void __fastcall TypeRegistryRegister(void* self, void*, std::uint32_t id, void* (__cdecl* factory)()) {
    auto* prototype = static_cast<CRefCounted*>(factory());
    if (prototype) ++prototype->refs_;
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype, 4);
    auto* tables = static_cast<Table*>(self);
    // operator[] as inlined: find_or_insert of {key, 0} (resize 0042B9C0 / 0040BAC0, new node
    // 0042B8A0 / 0040B440).
    tables[0].FindOrInsert({vtable, 0})->value = id;
    tables[1].FindOrInsert({id, 0})->value = reinterpret_cast<std::uint32_t>(factory);
    if (prototype && --prototype->refs_ == 0) delete prototype;   // slot 0 with 1
}
EI_REPLACE(0x00458570, TypeRegistryRegister);

// 00458540 / 00458550 / 004586E0: destructors of WinMain's locals, reached only when an exception
// unwinds it: two COM pointers (Release) and a counted object (deleted at its last reference).
void __fastcall ComPointerRelease(void** self) {
    if (void* object = *self) static_cast<IUnknown*>(object)->Release();
}
EI_REPLACE(0x00458540, ComPointerRelease);
void __fastcall ComPointerRelease2(void** self) { ComPointerRelease(self); }
EI_REPLACE(0x00458550, ComPointerRelease2);
void __fastcall CountedPointerRelease(void** self) {
    auto* object = static_cast<CRefCounted*>(*self);
    if (!object) return;
    if (--object->refs_ == 0) delete object;   // slot 0 with 1 (the original tests the pointer again)
}
EI_REPLACE(0x004586E0, CountedPointerRelease);
EI_REPLACE(0x00545B80, CountedPointerRelease);   // byte-identical copy (A-U11)

} // namespace app
