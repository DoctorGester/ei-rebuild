// Checks for NamedFloatTable::Set / Remove (object_tables.cpp): ours only. After a set the name
// holds the value (or is gone for a zero value), after a removal the name is gone, and the count
// matches the nodes in the buckets.
#include "verify.h"

#include "../../src/game/object_tables.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

const NamedFloatTable::Node* Lookup(const NamedFloatTable* table, const char* name) {
    for (NamedFloatTable::Node* const* b = table->buckets; b != table->finish; ++b)
        for (const NamedFloatTable::Node* n = *b; n; n = n->next)
            if (std::strcmp(n->key, name) == 0) return n;
    return nullptr;
}
bool CountMatches(const NamedFloatTable* table) {
    std::uint32_t n = 0;
    for (NamedFloatTable::Node* const* b = table->buckets; b != table->finish; ++b)
        for (const NamedFloatTable::Node* node = *b; node; node = node->next) ++n;
    return n == table->count;
}

void __fastcall SetVerify(NamedFloatTable* table, void*, const char* name, float value) {
    table->Set(name, value);
    if (!*name) return;
    const NamedFloatTable::Node* n = Lookup(table, name);
    bool ok = value == 0.0f ? n == nullptr : n && std::memcmp(&n->value, &value, 4) == 0;
    ei::verify::Check("NamedFloatTable::Set", true, ok && CountMatches(table), name);
}
void __fastcall RemoveVerify(NamedFloatTable* table, void*, const char* name) {
    table->Remove(name);
    ei::verify::Check("NamedFloatTable::Remove", true, Lookup(table, name) == nullptr && CountMatches(table), name);
}
void __fastcall CopyPrefixedVerify(NamedFloatTable* table, void*, const char* prefix, NamedFloatTable* out) {
    table->CopyPrefixed(prefix, out);
    std::size_t length = std::strlen(prefix);
    std::uint32_t expected = 0;
    bool ok = CountMatches(out);
    for (NamedFloatTable::Node* const* b = table->buckets; b != table->finish; ++b)
        for (const NamedFloatTable::Node* n = *b; n; n = n->next) {
            if (std::strncmp(prefix, n->key, length) != 0) continue;
            ++expected;
            const NamedFloatTable::Node* copy = Lookup(out, n->key);
            ok = ok && copy && std::memcmp(&copy->value, &n->value, 4) == 0;
        }
    ei::verify::Check("NamedFloatTable::CopyPrefixed", true, ok && out->count == expected, prefix);
}
} // namespace

EI_VERIFY(0x00460EE0, CopyPrefixedVerify, 0, s_unused);
EI_VERIFY(0x00460BC0, SetVerify, 0, s_unused);
EI_VERIFY(0x00460DB0, RemoveVerify, 0, s_unused);
