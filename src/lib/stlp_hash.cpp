// The game's STLport hashtable instantiations (stlp_hash.h), registered at their own addresses so the
// standalone image has them; the forwarders in src/game keep calling these addresses. Checked by
// tests/verify/verify_stlp_hash.cpp. Instantiations with the same instructions (addresses masked,
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
std::uint32_t __fastcall HashText(const char* s) { return HashChars(s); }

void* __fastcall RecordNamed(RecordsByName* self, void*, const char* name) {
    string key(name);
    StringKeyed<4> obj(key);
    void* record;
    std::memcpy(&record, self->byName.FindOrInsert(obj).mapped, sizeof(record));
    return record;
}

} // namespace stlp

namespace {

// Dword keys hashed as themselves: 12-byte nodes (dword to dword), 8-byte nodes (sets of dwords),
// 32-byte nodes (a dword key and 24 bytes).
using Map8 = stlp::HashtableAbi<stlp::DwordKeyTraits<8>>;
using Set4 = stlp::HashtableAbi<stlp::DwordKeyTraits<4>>;
using Map28 = stlp::HashtableAbi<stlp::DwordKeyTraits<28>>;
// String keys (__stl_hash_string), a 4-byte mapped value: 20-byte nodes (the 24-byte list).
using Str4 = stlp::HashtableAbi<stlp::StringKeyTraits<4>>;

} // namespace

// hashtable(n, hf, eql, a): 11 instantiations (the node is not touched).
EI_REPLACE(0x00405820, Map8::Construct);
EI_REPLACE(0x00405C40, Map8::Construct);
EI_REPLACE(0x00438B70, Map8::Construct);
EI_REPLACE(0x00439F20, Map8::Construct);
EI_REPLACE(0x0053B4B0, Map8::Construct);
EI_REPLACE(0x0053B650, Map8::Construct);
EI_REPLACE(0x0053B800, Map8::Construct);
EI_REPLACE(0x0053BA00, Map8::Construct);
EI_REPLACE(0x00609C10, Map8::Construct);
EI_REPLACE(0x006BBF90, Map8::Construct);
EI_REPLACE(0x006BC2B0, Map8::Construct);

// hash_map() / hash_set() (100 buckets): 8 instantiations, and the one with a dword after it.
EI_REPLACE(0x004639D0, Map8::ConstructDefault);
EI_REPLACE(0x004DC830, Map8::ConstructDefault);
EI_REPLACE(0x005725E0, Map8::ConstructDefault);
EI_REPLACE(0x00581790, Map8::ConstructDefault);
EI_REPLACE(0x005985E0, Map8::ConstructDefault);
EI_REPLACE(0x00598790, Map8::ConstructDefault);
EI_REPLACE(0x005BCCD0, Map8::ConstructDefault);
EI_REPLACE(0x0066B320, Map8::ConstructDefault);
EI_REPLACE(0x00571050, stlp::ConstructTableWithDword);

// _M_initialize_buckets(n) (library.csv: "resize"): 3 instantiations.
EI_REPLACE(0x00405EB0, Map8::InitializeBuckets);
EI_REPLACE(0x00406300, Map8::InitializeBuckets);
EI_REPLACE(0x0040AA80, Map8::InitializeBuckets);

// resize(hint): 7 instantiations of dword keys.
EI_REPLACE(0x00406640, Map8::Resize);
EI_REPLACE(0x00406D10, Map8::Resize);
EI_REPLACE(0x00406EE0, Map8::Resize);
EI_REPLACE(0x004070B0, Map8::Resize);
EI_REPLACE(0x0040BAC0, Map8::Resize);
EI_REPLACE(0x0042AA90, Map8::Resize);
EI_REPLACE(0x0042B9C0, Map8::Resize);

// find(key): 9 instantiations of dword keys.
EI_REPLACE(0x00405560, Map8::Find);
EI_REPLACE(0x004057A0, Map8::Find);
EI_REPLACE(0x00405AE0, Map8::Find);
EI_REPLACE(0x00405B20, Map8::Find);
EI_REPLACE(0x00405BB0, Map8::Find);
EI_REPLACE(0x00405D70, Map8::Find);
EI_REPLACE(0x0040B400, Map8::Find);
EI_REPLACE(0x0042A810, Map8::Find);
EI_REPLACE(0x0066B490, Map8::Find);
EI_REPLACE(0x0055C5C0, Map8::BucketOfKey);
EI_REPLACE(0x0040AE00, Map8::NextSize);  // __stl_next_prime
EI_REPLACE(0x00672EC0, Map8::NextSize);

// begin(): 9 instantiations.
EI_REPLACE(0x00404C30, Map8::Begin);
EI_REPLACE(0x00406280, Map8::Begin);
EI_REPLACE(0x0040A970, Map8::Begin);
EI_REPLACE(0x004614C0, Map8::Begin);
EI_REPLACE(0x0055AB50, Map8::Begin);
EI_REPLACE(0x0055ADF0, Map8::Begin);
EI_REPLACE(0x0055AE50, Map8::Begin);
EI_REPLACE(0x0069C160, Map8::Begin);
EI_REPLACE(0x006BC3E0, Map8::Begin);

// hash_map<dword, dword>::operator[]: 6 instantiations.
EI_REPLACE(0x004055A0, Map8::Subscript);
EI_REPLACE(0x004056A0, Map8::Subscript);
EI_REPLACE(0x004075C0, Map8::Subscript);
EI_REPLACE(0x00407680, Map8::Subscript);
EI_REPLACE(0x0040A800, Map8::Subscript);
EI_REPLACE(0x00439EA0, Map8::Subscript);

// find_or_insert(obj): 4 of 12-byte nodes, 1 of 32-byte nodes (004E3130 makes them).
EI_REPLACE(0x004065D0, Map8::FindOrInsert);
EI_REPLACE(0x004085E0, Map8::FindOrInsert);
EI_REPLACE(0x0040B2C0, Map8::FindOrInsert);
EI_REPLACE(0x006BCFF0, Map8::FindOrInsert);
EI_REPLACE(0x004E2E90, Map28::FindOrInsert);

// erase(const_iterator): 3 instantiations of 12-byte nodes.
EI_REPLACE(0x00405620, Map8::EraseIterator);
EI_REPLACE(0x00405720, Map8::EraseIterator);
EI_REPLACE(0x005BCE60, Map8::EraseIterator);

// clear(): 10 instantiations of nodes from the 16-byte free list.
EI_REPLACE(0x004067B0, Map8::Clear);
EI_REPLACE(0x00406E80, Map8::Clear);
EI_REPLACE(0x00407050, Map8::Clear);
EI_REPLACE(0x00407220, Map8::Clear);
EI_REPLACE(0x0040B330, Map8::Clear);
EI_REPLACE(0x0043B4D0, Map8::Clear);
EI_REPLACE(0x0043D590, Map8::Clear);
EI_REPLACE(0x0059D6D0, Map8::Clear);
EI_REPLACE(0x0059D7A0, Map8::Clear);
EI_REPLACE(0x006BD100, Map8::Clear);

// ~hashtable() (library.csv: "clear"): 12-byte nodes (2), 8-byte nodes (2).
EI_REPLACE(0x004A8B70, Map8::Destroy);
EI_REPLACE(0x005984A0, Map8::Destroy);
EI_REPLACE(0x004DB8A0, Set4::Destroy);
EI_REPLACE(0x00668B70, Set4::Destroy);

// hash_set<int> of the client's party ids: insert_unique, its resize and _M_new_node (8-byte
// nodes), clear.
EI_REPLACE(0x0066B400, Set4::InsertUnique);
EI_REPLACE(0x0066B900, Set4::Resize);
EI_REPLACE(0x0066BAD0, Set4::NewNode);
EI_REPLACE(0x0066BA70, Set4::Clear);

// _M_new_node(obj) / _M_delete_node(n): 12-byte nodes.
EI_REPLACE(0x00407420, Map8::NewNode);
EI_REPLACE(0x004074F0, Map8::NewNode);
EI_REPLACE(0x00408B20, Map8::NewNode);
EI_REPLACE(0x0040B440, Map8::NewNode);
EI_REPLACE(0x0042AA00, Map8::NewNode);
EI_REPLACE(0x0042B8A0, Map8::NewNode);
EI_REPLACE(0x004074B0, Map8::DeleteNode);
EI_REPLACE(0x00407580, Map8::DeleteNode);
EI_REPLACE(0x00407640, Map8::DeleteNode);
EI_REPLACE(0x00407700, Map8::DeleteNode);

// _Ht_iterator members of dword keys.
EI_REPLACE(0x004073C0, Map8::SkipToNext);
EI_REPLACE(0x0055B500, Map8::SkipToNext);
EI_REPLACE(0x0055B9D0, Map8::SkipToNext);
EI_REPLACE(0x006BCFB0, Map8::SkipToNext);
EI_REPLACE(0x00404980, Map8::Increment);
EI_REPLACE(0x0040A700, Map8::PostIncrement);
EI_REPLACE(0x0040AD20, Map8::ConstructIterator);
EI_REPLACE(0x005BE450, Map8::ConstructIterator);
EI_REPLACE(0x006B0B10, Map8::ConstructIterator);
EI_REPLACE(0x006B0B30, Map8::ConstructIterator);

// String keys with a 4-byte mapped value (each _M_new_node takes 0x14-byte nodes; each clear()
// pushes onto the 24-byte list).
EI_REPLACE(0x004298D0, stlp::HashString);
EI_REPLACE(0x00460BA0, stlp::HashText);
EI_REPLACE(0x004FB0F0, Str4::Find);
EI_REPLACE(0x005AD520, Str4::Find);
EI_REPLACE(0x006B07C0, Str4::Find);
EI_REPLACE(0x006B0870, Str4::Find);
EI_REPLACE(0x006B0930, Str4::Find);
EI_REPLACE(0x006BC0C0, Str4::Find);
EI_REPLACE(0x0069D0E0, Str4::SkipToNext);
EI_REPLACE(0x006BCCD0, Str4::EraseKey);

// hash_map<string, T>::operator[](const string&): 16 instantiations.
EI_REPLACE(0x004297A0, Str4::SubscriptKey);
EI_REPLACE(0x004ABAA0, Str4::SubscriptKey);
EI_REPLACE(0x004ABBD0, Str4::SubscriptKey);
EI_REPLACE(0x004ABD00, Str4::SubscriptKey);
EI_REPLACE(0x0053C650, Str4::SubscriptKey);
EI_REPLACE(0x0053C780, Str4::SubscriptKey);
EI_REPLACE(0x0053C8B0, Str4::SubscriptKey);
EI_REPLACE(0x0060AC20, Str4::SubscriptKey);
EI_REPLACE(0x00619FB0, Str4::SubscriptKey);
EI_REPLACE(0x00684B20, Str4::SubscriptKey);
EI_REPLACE(0x006A75E0, Str4::SubscriptKey);
EI_REPLACE(0x00464750, Str4::SubscriptKey);
EI_REPLACE(0x005335E0, Str4::SubscriptKey);
EI_REPLACE(0x006BBDF0, Str4::SubscriptKey);
EI_REPLACE(0x0064EE90, Str4::SubscriptKey);
EI_REPLACE(0x006AD670, Str4::SubscriptKey);

// hash_map<string, T*>::operator[](const char*) on a record table: 8 instantiations.
EI_REPLACE(0x004AB720, stlp::RecordNamed);
EI_REPLACE(0x004AB820, stlp::RecordNamed);
EI_REPLACE(0x0052CA40, stlp::RecordNamed);
EI_REPLACE(0x0052CB40, stlp::RecordNamed);
EI_REPLACE(0x0069D290, stlp::RecordNamed);
EI_REPLACE(0x0069D380, stlp::RecordNamed);
EI_REPLACE(0x006AD580, stlp::RecordNamed);
EI_REPLACE(0x00613820, stlp::RecordNamed);

// find_or_insert(obj): 15 instantiations.
EI_REPLACE(0x004ACC10, Str4::FindOrInsert);
EI_REPLACE(0x004CE110, Str4::FindOrInsert);
EI_REPLACE(0x004FBBD0, Str4::FindOrInsert);
EI_REPLACE(0x00514880, Str4::FindOrInsert);
EI_REPLACE(0x0053D340, Str4::FindOrInsert);
EI_REPLACE(0x005ADC10, Str4::FindOrInsert);
EI_REPLACE(0x00613B70, Str4::FindOrInsert);
EI_REPLACE(0x0069D1E0, Str4::FindOrInsert);
EI_REPLACE(0x0069E2A0, Str4::FindOrInsert);
EI_REPLACE(0x006A7870, Str4::FindOrInsert);
EI_REPLACE(0x006BCC20, Str4::FindOrInsert);
EI_REPLACE(0x004ACAB0, Str4::FindOrInsert);
EI_REPLACE(0x004ACB60, Str4::FindOrInsert);
EI_REPLACE(0x0052CF60, Str4::FindOrInsert);
EI_REPLACE(0x0069E1F0, Str4::FindOrInsert);

// resize(hint): 20 instantiations (004CEF00, 00514D50 and 004FC320 keep an exception handler).
EI_REPLACE(0x0042AFD0, Str4::Resize);
EI_REPLACE(0x004AEA90, Str4::Resize);
EI_REPLACE(0x004AEC40, Str4::Resize);
EI_REPLACE(0x004AEDF0, Str4::Resize);
EI_REPLACE(0x004AEFA0, Str4::Resize);
EI_REPLACE(0x004AF150, Str4::Resize);
EI_REPLACE(0x004AF300, Str4::Resize);
EI_REPLACE(0x00464F30, Str4::Resize);
EI_REPLACE(0x00514F70, Str4::Resize);
EI_REPLACE(0x0053DDF0, Str4::Resize);
EI_REPLACE(0x0053E050, Str4::Resize);
EI_REPLACE(0x005AE430, Str4::Resize);
EI_REPLACE(0x0060B740, Str4::Resize);
EI_REPLACE(0x0061A650, Str4::Resize);
EI_REPLACE(0x0069E090, Str4::Resize);
EI_REPLACE(0x0069EB70, Str4::Resize);
EI_REPLACE(0x006BD840, Str4::Resize);
EI_REPLACE(0x004CEF00, Str4::Resize);
EI_REPLACE(0x00514D50, Str4::Resize);
EI_REPLACE(0x004FC320, Str4::Resize);

// _M_new_node(obj): 20 instantiations.
EI_REPLACE(0x0042A650, Str4::NewNode);
EI_REPLACE(0x004ADBE0, Str4::NewNode);
EI_REPLACE(0x004ADD00, Str4::NewNode);
EI_REPLACE(0x004ADE20, Str4::NewNode);
EI_REPLACE(0x004ADF40, Str4::NewNode);
EI_REPLACE(0x004AE060, Str4::NewNode);
EI_REPLACE(0x004AE180, Str4::NewNode);
EI_REPLACE(0x00464E10, Str4::NewNode);
EI_REPLACE(0x00514C10, Str4::NewNode);
EI_REPLACE(0x0053D9D0, Str4::NewNode);
EI_REPLACE(0x0053DAF0, Str4::NewNode);
EI_REPLACE(0x0060B600, Str4::NewNode);
EI_REPLACE(0x0061A380, Str4::NewNode);
EI_REPLACE(0x004CE460, Str4::NewNode);
EI_REPLACE(0x004FBF60, Str4::NewNode);
EI_REPLACE(0x00514AF0, Str4::NewNode);
EI_REPLACE(0x005ADF10, Str4::NewNode);
EI_REPLACE(0x0069D880, Str4::NewNode);
EI_REPLACE(0x0069E5B0, Str4::NewNode);
EI_REPLACE(0x006BD240, Str4::NewNode);

// clear(): 13 instantiations.
EI_REPLACE(0x004AB2B0, Str4::Clear);
EI_REPLACE(0x0053C0D0, Str4::Clear);
EI_REPLACE(0x0053C280, Str4::Clear);
EI_REPLACE(0x0053C430, Str4::Clear);
EI_REPLACE(0x0053C5E0, Str4::Clear);
EI_REPLACE(0x006A6D00, Str4::Clear);
EI_REPLACE(0x006A6D70, Str4::Clear);
EI_REPLACE(0x006A6DE0, Str4::Clear);
EI_REPLACE(0x006A70A0, Str4::Clear);
EI_REPLACE(0x006A7340, Str4::Clear);
EI_REPLACE(0x006B0520, Str4::Clear);
EI_REPLACE(0x006B0590, Str4::Clear);
EI_REPLACE(0x006BCE00, Str4::Clear);
