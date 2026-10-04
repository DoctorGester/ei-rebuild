// Checks for app_registry.cpp: ours only. After a registration the id is found under the factory
// prototype's vtable in the first map, and the factory under the id in the second (looked up
// directly in the STLport buckets).
#include "verify.h"

#include <cstdint>

namespace app {
void __fastcall TypeRegistryRegister(void* self, void*, std::uint32_t id, void* (__cdecl* factory)());
}

namespace {
void* s_unused;

struct Node {
    Node* next;
    std::uint32_t key;
    std::uint32_t value;
};
struct Table {
    std::uint32_t functors;
    Node** buckets;
    Node** bucketsEnd;
    Node** bucketsEos;
    std::uint32_t count;
};

const Node* Find(const Table& t, std::uint32_t key) {
    std::uint32_t n = static_cast<std::uint32_t>(t.bucketsEnd - t.buckets);
    if (n == 0) return nullptr;
    for (const Node* node = t.buckets[key % n]; node; node = node->next)
        if (node->key == key) return node;
    return nullptr;
}

void __fastcall RegisterVerify(void* self, void*, std::uint32_t id, void* (__cdecl* factory)()) {
    app::TypeRegistryRegister(self, nullptr, id, factory);
    auto* tables = static_cast<const Table*>(self);
    const Node* byId = Find(tables[1], id);
    bool ok = byId && byId->value == reinterpret_cast<std::uint32_t>(factory);
    ei::verify::Check("TypeRegistryRegister", true, ok, "");
}
} // namespace

EI_VERIFY(0x00458570, RegisterVerify, 0, s_unused);
