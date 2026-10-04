// Checks for hash_tables_400.cpp.
//  - ObjectsLookup / ObjectsFind: side by side (they only read).
//  - SetAt, RemoveKey, operator[]: ours only (they insert and erase nodes); afterwards the key's
//    node holds the value / is gone / is the one returned, and SetAt keeps the highest key.
//  - The registries' constructor and destructors: ours only; the destructors leave both tables
//    empty. (The class registry is built before the hooks are installed and destroyed at exit.)
#include "verify.h"

#include <cstdint>

namespace hash400 {
struct Table {
    std::uint8_t functors[4];
    void** buckets;
    void** bucketsEnd;
    void** bucketsEos;
    std::uint32_t count;
    std::int32_t maxKey;
};
void __fastcall ObjectsSetAt(Table* t, void*, const std::int32_t* key, const std::uint32_t* value);
std::uint32_t __fastcall ObjectsRemoveKey(Table* t, void*, const std::uint32_t* key);
int __fastcall ObjectsLookup(Table* t, void*, const std::uint32_t* key, std::uint32_t* value);
std::uint32_t __fastcall PositionsRemoveKey(Table* t, void*, const std::uint32_t* key);
std::uint32_t* __fastcall ObjectsFind(Table* t, void*, const std::uint32_t* key);
std::uint32_t* __fastcall PositionAt(Table* t, void*, const std::int32_t* key);
std::uint32_t* __fastcall SpareCountAt(Table* t, void*, const std::int32_t* key);
void* __fastcall ClassRegistryConstruct(Table* self);
void __fastcall ClassRegistryDestruct(Table* self);
void __fastcall TypeRegistryDestruct(Table* self);
} // namespace hash400

namespace misc400 {
void* __fastcall AlignedWords(int count);
void* __fastcall AlignedDwords(int count);
}

namespace {
void* s_unused;
using hash400::Table;

// The value slot of `key` (a plain walk of its bucket), or null.
std::uint32_t* Slot(const Table* t, std::uint32_t key) {
    auto n = static_cast<std::uint32_t>((reinterpret_cast<std::intptr_t>(t->bucketsEnd) - reinterpret_cast<std::intptr_t>(t->buckets)) >> 2);
    if (n == 0) return nullptr;
    for (auto** node = static_cast<void**>(t->buckets[key % n]); node; node = static_cast<void**>(node[0]))
        if (reinterpret_cast<std::uintptr_t>(node[1]) == key) return reinterpret_cast<std::uint32_t*>(&node[2]);
    return nullptr;
}
Table* Second(Table* t) { return reinterpret_cast<Table*>(reinterpret_cast<std::uint8_t*>(t) + 0x14); }

void __fastcall SetAtVerify(Table* t, void*, const std::int32_t* key, const std::uint32_t* value) {
    std::int32_t k = *key;
    std::uint32_t v = *value;
    hash400::ObjectsSetAt(t, nullptr, key, value);
    std::uint32_t* slot = Slot(t, static_cast<std::uint32_t>(k));
    ei::verify::Check("ObjectsSetAt", true, slot && *slot == v && t->maxKey >= k, "key %d", k);
}
std::uint32_t __fastcall RemoveKeyVerify(Table* t, void*, const std::uint32_t* key) {
    std::uint32_t k = *key;
    std::uint32_t* before = Slot(t, k);
    std::uint32_t expected = before ? *before : 0;
    std::uint32_t result = hash400::ObjectsRemoveKey(t, nullptr, key);
    ei::verify::Check("ObjectsRemoveKey", true, result == expected && !Slot(t, k), "key %u", k);
    return result;
}
std::uint32_t __fastcall PositionsRemoveKeyVerify(Table* t, void*, const std::uint32_t* key) {
    std::uint32_t k = *key;
    std::uint32_t* before = Slot(t, k);
    std::uint32_t expected = before ? *before : 0;
    std::uint32_t result = hash400::PositionsRemoveKey(t, nullptr, key);
    ei::verify::Check("PositionsRemoveKey", true, result == expected && !Slot(t, k), "key %u", k);
    return result;
}
// 00404F80 / 00406950: MOV EAX,[ESP+4] (4), PUSH ESI (1).
int(__fastcall* s_lookup)(Table*, void*, const std::uint32_t*, std::uint32_t*);
int __fastcall LookupVerify(Table* t, void*, const std::uint32_t* key, std::uint32_t* value) {
    std::uint32_t expectedValue = *value, actualValue = *value;
    int expected = s_lookup(t, nullptr, key, &expectedValue);
    int actual = hash400::ObjectsLookup(t, nullptr, key, &actualValue);
    ei::verify::Check("ObjectsLookup", true, expected == actual && expectedValue == actualValue, "key %u", *key);
    *value = actualValue;
    return actual;
}
std::uint32_t*(__fastcall* s_find)(Table*, void*, const std::uint32_t*);
std::uint32_t* __fastcall FindVerify(Table* t, void*, const std::uint32_t* key) {
    std::uint32_t* expected = s_find(t, nullptr, key);
    std::uint32_t* actual = hash400::ObjectsFind(t, nullptr, key);
    ei::verify::Check("ObjectsFind", expected, actual, "key %u", *key);
    return actual;
}
std::uint32_t* __fastcall PositionAtVerify(Table* t, void*, const std::int32_t* key) {
    std::int32_t k = *key;
    std::uint32_t* result = hash400::PositionAt(t, nullptr, key);
    ei::verify::Check("PositionAt", true, result == Slot(t, static_cast<std::uint32_t>(k)) && t->maxKey >= k, "key %d", k);
    return result;
}
std::uint32_t* __fastcall SpareCountAtVerify(Table* t, void*, const std::int32_t* key) {
    std::int32_t k = *key;
    std::uint32_t* result = hash400::SpareCountAt(t, nullptr, key);
    ei::verify::Check("SpareCountAt", true, result == Slot(t, static_cast<std::uint32_t>(k)) && t->maxKey >= k, "key %d", k);
    return result;
}
void* __fastcall ClassRegistryConstructVerify(Table* self) {
    void* result = hash400::ClassRegistryConstruct(self);
    ei::verify::Check("ClassRegistryConstruct", true, result == self && self->count == 0 && Second(self)->count == 0, "");
    return result;
}
void __fastcall ClassRegistryDestructVerify(Table* self) {
    hash400::ClassRegistryDestruct(self);
    ei::verify::Check("ClassRegistryDestruct", true, self->count == 0 && Second(self)->count == 0, "");
}
void __fastcall TypeRegistryDestructVerify(Table* self) {
    hash400::TypeRegistryDestruct(self);
    ei::verify::Check("TypeRegistryDestruct", true, self->count == 0 && Second(self)->count == 0, "");
}
bool Aligned(void* start) {
    auto a = reinterpret_cast<std::uintptr_t>(start);
    std::uintptr_t block;
    std::memcpy(&block, static_cast<std::uint8_t*>(start) - 4, 4);
    return (a & 0x1F) == 0 && block <= a - 4 && a - block <= 0x23;
}
void* __fastcall AlignedWordsVerify(int count) {
    void* start = misc400::AlignedWords(count);
    ei::verify::Check("AlignedWords", true, Aligned(start), "%d", count);
    return start;
}
void* __fastcall AlignedDwordsVerify(int count) {
    void* start = misc400::AlignedDwords(count);
    ei::verify::Check("AlignedDwords", true, Aligned(start), "%d", count);
    return start;
}
} // namespace

EI_VERIFY(0x00404E80, SetAtVerify, 0, s_unused);
EI_VERIFY(0x00404F20, RemoveKeyVerify, 0, s_unused);
EI_VERIFY(0x00404F80, LookupVerify, 5, s_lookup);
EI_VERIFY(0x00405350, PositionsRemoveKeyVerify, 0, s_unused);
EI_VERIFY(0x00406950, FindVerify, 5, s_find);
EI_VERIFY(0x00406A10, PositionAtVerify, 0, s_unused);
EI_VERIFY(0x00406AA0, SpareCountAtVerify, 0, s_unused);
EI_VERIFY(0x00408EA0, ClassRegistryConstructVerify, 0, s_unused);
EI_VERIFY(0x00408D60, ClassRegistryDestructVerify, 0, s_unused);
EI_VERIFY(0x004368F0, TypeRegistryDestructVerify, 0, s_unused);
EI_VERIFY(0x00406B30, AlignedWordsVerify, 0, s_unused);
EI_VERIFY(0x00406B50, AlignedDwordsVerify, 0, s_unused);
