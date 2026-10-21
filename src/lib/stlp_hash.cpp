// The game's STLport hashtable instantiations (stlp_hash.h) registered at their own addresses: the ones something
// still reaches by game.exe's address (tools/registration_readers.py, C20c); our code calls the others directly.
// Instantiations with the same instructions (addresses masked,
// node sizes and free lists kept) share one shim; each node size was read from the instantiation's
// _M_new_node or from the free list its clear() pushes to.
#include "stlp_hash.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace stlp {

TableWithDword* __fastcall ConstructTableWithDword(TableWithDword* self) {
    HashtableAbi<DwordKeyTraits<8>>::ConstructDefault(&self->table);
    self->extra = 0;
    return self;
}

std::uint32_t __fastcall HashString(const string* s) { return s->Hash(); }

void* __fastcall RecordNamed(RecordsByName* self, void*, const char* name) {
    string key(name);
    StringKeyed<4> obj(key);
    void* record;
    std::memcpy(&record, self->byName.FindOrInsert(obj).mapped, sizeof(record));
    return record;
}

} // namespace stlp

namespace {

// Dword keys hashed as themselves: 12-byte nodes (dword to dword), 8-byte nodes (sets of dwords).
using Map8 = stlp::HashtableAbi<stlp::DwordKeyTraits<8>>;
using Set4 = stlp::HashtableAbi<stlp::DwordKeyTraits<4>>;

} // namespace

// hash_map() / hash_set() (100 buckets): 8 instantiations, and the one with a dword after it.
EI_REPLACE(0x004DC830, Map8::ConstructDefault);
EI_REPLACE(0x0066B320, Map8::ConstructDefault);

// ~hashtable() (library.csv: "clear"): 12-byte nodes (2), 8-byte nodes (2).
EI_REPLACE(0x004DB8A0, Set4::Destroy);
EI_REPLACE(0x00668B70, Set4::Destroy);

