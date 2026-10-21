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

EI_REWRITTEN(0x00405820, Map8::Construct);
EI_REWRITTEN(0x00405C40, Map8::Construct);
EI_REWRITTEN(0x00438B70, Map8::Construct);
EI_REWRITTEN(0x00439F20, Map8::Construct);
EI_REWRITTEN(0x0053B4B0, Map8::Construct);
EI_REWRITTEN(0x0053B650, Map8::Construct);
EI_REWRITTEN(0x0053B800, Map8::Construct);
EI_REWRITTEN(0x0053BA00, Map8::Construct);
EI_REWRITTEN(0x00609C10, Map8::Construct);
EI_REWRITTEN(0x006BBF90, Map8::Construct);
EI_REWRITTEN(0x006BC2B0, Map8::Construct);
// hash_map() / hash_set() (100 buckets): 8 instantiations, and the one with a dword after it.
EI_REWRITTEN(0x004639D0, Map8::ConstructDefault);
EI_REPLACE(0x004DC830, Map8::ConstructDefault);
EI_REWRITTEN(0x005725E0, Map8::ConstructDefault);
EI_REWRITTEN(0x00581790, Map8::ConstructDefault);
EI_REWRITTEN(0x005985E0, Map8::ConstructDefault);
EI_REWRITTEN(0x00598790, Map8::ConstructDefault);
EI_REWRITTEN(0x005BCCD0, Map8::ConstructDefault);
EI_REPLACE(0x0066B320, Map8::ConstructDefault);
EI_REWRITTEN(0x004067B0, Map8::Clear);
EI_REWRITTEN(0x00406E80, Map8::Clear);
EI_REWRITTEN(0x00407050, Map8::Clear);
EI_REWRITTEN(0x00407220, Map8::Clear);
EI_REWRITTEN(0x0040B330, Map8::Clear);
EI_REWRITTEN(0x0043B4D0, Map8::Clear);
EI_REWRITTEN(0x0043D590, Map8::Clear);
EI_REWRITTEN(0x0059D6D0, Map8::Clear);
EI_REWRITTEN(0x0059D7A0, Map8::Clear);
EI_REWRITTEN(0x006BD100, Map8::Clear);
EI_REWRITTEN(0x00405620, Map8::EraseIterator);
EI_REWRITTEN(0x00405720, Map8::EraseIterator);
EI_REWRITTEN(0x005BCE60, Map8::EraseIterator);
EI_REWRITTEN(0x006BD060, Map8::EraseKey);
EI_REWRITTEN(0x004065D0, Map8::FindOrInsert);
EI_REWRITTEN(0x004085E0, Map8::FindOrInsert);
EI_REWRITTEN(0x0040B2C0, Map8::FindOrInsert);
EI_REWRITTEN(0x006BCFF0, Map8::FindOrInsert);
EI_REWRITTEN(0x004055A0, Map8::Subscript);
EI_REWRITTEN(0x004056A0, Map8::Subscript);
EI_REWRITTEN(0x004075C0, Map8::Subscript);
EI_REWRITTEN(0x00407680, Map8::Subscript);
EI_REWRITTEN(0x0040A800, Map8::Subscript);
EI_REWRITTEN(0x00439EA0, Map8::Subscript);
EI_REWRITTEN(0x00404C30, Map8::Begin);
EI_REWRITTEN(0x00406280, Map8::Begin);
EI_REWRITTEN(0x0040A970, Map8::Begin);
EI_REWRITTEN(0x004614C0, Map8::Begin);
EI_REWRITTEN(0x0055AB50, Map8::Begin);
EI_REWRITTEN(0x0055ADF0, Map8::Begin);
EI_REWRITTEN(0x0055AE50, Map8::Begin);
EI_REWRITTEN(0x0069C160, Map8::Begin);
EI_REWRITTEN(0x006BC3E0, Map8::Begin);
EI_REWRITTEN(0x00405560, Map8::Find);
EI_REWRITTEN(0x004057A0, Map8::Find);
EI_REWRITTEN(0x00405AE0, Map8::Find);
EI_REWRITTEN(0x00405B20, Map8::Find);
EI_REWRITTEN(0x00405BB0, Map8::Find);
EI_REWRITTEN(0x00405D70, Map8::Find);
EI_REWRITTEN(0x0040B400, Map8::Find);
EI_REWRITTEN(0x0042A810, Map8::Find);
EI_REWRITTEN(0x0066B490, Map8::Find);
EI_REWRITTEN(0x0055C5C0, Map8::BucketOfKey);
EI_REWRITTEN(0x0040AE00, Map8::NextSize);
EI_REWRITTEN(0x00672EC0, Map8::NextSize);
EI_REWRITTEN(0x00406640, Map8::Resize);
EI_REWRITTEN(0x00406D10, Map8::Resize);
EI_REWRITTEN(0x00406EE0, Map8::Resize);
EI_REWRITTEN(0x004070B0, Map8::Resize);
EI_REWRITTEN(0x0040BAC0, Map8::Resize);
EI_REWRITTEN(0x0042AA90, Map8::Resize);
EI_REWRITTEN(0x0042B9C0, Map8::Resize);
EI_REWRITTEN(0x00405EB0, Map8::InitializeBuckets);
EI_REWRITTEN(0x00406300, Map8::InitializeBuckets);
EI_REWRITTEN(0x0040AA80, Map8::InitializeBuckets);
EI_REWRITTEN(0x00571050, stlp::ConstructTableWithDword);

// ~hashtable() (library.csv: "clear"): 12-byte nodes (2), 8-byte nodes (2).
EI_REWRITTEN(0x004A8B70, Map8::Destroy);
EI_REWRITTEN(0x005984A0, Map8::Destroy);
EI_REPLACE(0x004DB8A0, Set4::Destroy);
EI_REPLACE(0x00668B70, Set4::Destroy);
EI_REWRITTEN(0x004AB720, stlp::RecordNamed);
EI_REWRITTEN(0x004AB820, stlp::RecordNamed);
EI_REWRITTEN(0x0052CA40, stlp::RecordNamed);
EI_REWRITTEN(0x0052CB40, stlp::RecordNamed);
EI_REWRITTEN(0x0069D290, stlp::RecordNamed);
EI_REWRITTEN(0x0069D380, stlp::RecordNamed);
EI_REWRITTEN(0x006AD580, stlp::RecordNamed);
EI_REWRITTEN(0x00613820, stlp::RecordNamed);
EI_REWRITTEN(0x00613920, stlp::RecordNamed);
EI_REWRITTEN(0x004298D0, stlp::HashString);
EI_REWRITTEN(0x004FB090, Map8::Begin);
EI_REWRITTEN(0x005E0C00, Map8::Begin);
EI_REWRITTEN(0x00482DD0, Map8::ConstructIterator);
EI_REWRITTEN(0x005E0D50, Map8::ConstructIterator);
EI_REWRITTEN(0x005E0E00, Map8::SkipToNext);
EI_REWRITTEN(0x004073C0, Map8::SkipToNext);
EI_REWRITTEN(0x0055B500, Map8::SkipToNext);
EI_REWRITTEN(0x0055B9D0, Map8::SkipToNext);
EI_REWRITTEN(0x006BCFB0, Map8::SkipToNext);
EI_REWRITTEN(0x00404980, Map8::Increment);
EI_REWRITTEN(0x0040A700, Map8::PostIncrement);
EI_REWRITTEN(0x0040AD20, Map8::ConstructIterator);
EI_REWRITTEN(0x005BE450, Map8::ConstructIterator);
EI_REWRITTEN(0x006B0B10, Map8::ConstructIterator);
EI_REWRITTEN(0x006B0B30, Map8::ConstructIterator);
EI_REWRITTEN(0x00407420, Map8::NewNode);
EI_REWRITTEN(0x004074F0, Map8::NewNode);
EI_REWRITTEN(0x00408B20, Map8::NewNode);
EI_REWRITTEN(0x0040B440, Map8::NewNode);
EI_REWRITTEN(0x0042AA00, Map8::NewNode);
EI_REWRITTEN(0x0042B8A0, Map8::NewNode);
EI_REWRITTEN(0x004074B0, Map8::DeleteNode);
EI_REWRITTEN(0x00407580, Map8::DeleteNode);
EI_REWRITTEN(0x00407640, Map8::DeleteNode);
EI_REWRITTEN(0x00407700, Map8::DeleteNode);
EI_REWRITTEN(0x0066B400, Set4::InsertUnique);
EI_REWRITTEN(0x0066B900, Set4::Resize);
EI_REWRITTEN(0x0066BAD0, Set4::NewNode);
EI_REWRITTEN(0x0066BA70, Set4::Clear);
EI_REWRITTEN(0x00482DF0, Map8::Destroy);

EI_REWRITTEN(0x005BCDE0, Map8::Subscript);
