// 00458570: registers an object type with a factory in a two-way type registry (called twice by
// WinMain): a prototype made by the factory gives the vtable; vtable -> id in the STLport
// hash_map at +0, id -> factory in the one at +0x14 (the same scheme as the network's
// TypeRegistry::Register, 0042A890). See docs/notes/app.md.
#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

namespace app {

namespace {

struct Node {
    Node* next;
    std::uint32_t key;
    std::uint32_t value;
};
struct Table {                   // STLport hashtable<pair<const uint32, uint32>> (0x14 bytes)
    std::uint32_t functors;
    Node** buckets;
    Node** bucketsEnd;
    Node** bucketsEos;
    std::uint32_t count;
};
struct Pair {
    std::uint32_t key;
    std::uint32_t value;
};

// The two instantiations' resize and node creation (library).
void __fastcall ByVtableResize(Table* a0, void*, std::uint32_t a2) { ByVtableResizeOriginal((void*)a0, nullptr, a2); }
Node* __fastcall ByVtableNewNode(Table* a0, void*, const Pair* a2) { return (Node*)ByVtableNewNodeOriginal((void*)a0, nullptr, (const void*)a2); }
__declspec(naked) void __fastcall ByIdResize(Table*, void*, std::uint32_t) EI_ORIGINAL(0x0040BAC0)
__declspec(naked) Node* __fastcall ByIdNewNode(Table*, void*, const Pair*) EI_ORIGINAL(0x0040B440)

using Resize = void(__fastcall*)(Table*, void*, std::uint32_t);
using NewNode = Node*(__fastcall*)(Table*, void*, const Pair*);

// operator[] as inlined: the node with `key`, made (value 0) and put first in its bucket if
// missing.
Node* FindOrInsert(Table& t, std::uint32_t key, Resize resize, NewNode newNode) {
    Pair pair{key, 0};
    resize(&t, nullptr, t.count + 1);
    std::uint32_t bucket = key % static_cast<std::uint32_t>(t.bucketsEnd - t.buckets);
    Node* head = t.buckets[bucket];
    for (Node* n = head; n; n = n->next)
        if (n->key == key) return n;
    Node* n = newNode(&t, nullptr, &pair);
    n->next = head;
    t.buckets[bucket] = n;
    ++t.count;
    return n;
}

} // namespace

void __fastcall TypeRegistryRegister(void* self, void*, std::uint32_t id, void* (__cdecl* factory)()) {
    auto* prototype = static_cast<std::uint8_t*>(factory());
    if (prototype) ++*reinterpret_cast<std::int16_t*>(prototype + 4);
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype, 4);
    auto* tables = static_cast<Table*>(self);
    FindOrInsert(tables[0], vtable, ByVtableResize, ByVtableNewNode)->value = id;
    FindOrInsert(tables[1], id, ByIdResize, ByIdNewNode)->value = reinterpret_cast<std::uint32_t>(factory);
    if (prototype && --*reinterpret_cast<std::int16_t*>(prototype + 4) == 0) ei::VCall<void*>(prototype, 0, 1);
}
EI_REPLACE(0x00458570, TypeRegistryRegister);

// 00458540 / 00458550 / 004586E0: destructors of WinMain's locals, reached only when an exception
// unwinds it: two COM pointers (Release) and a counted object (deleted at its last reference).
void __fastcall ComPointerRelease(void** self) {
    if (void* object = *self) {
        using Release = unsigned long(__stdcall*)(void*);
        reinterpret_cast<Release>((*static_cast<void***>(object))[2])(object);
    }
}
EI_REPLACE(0x00458540, ComPointerRelease);
void __fastcall ComPointerRelease2(void** self) { ComPointerRelease(self); }
EI_REPLACE(0x00458550, ComPointerRelease2);
void __fastcall CountedPointerRelease(void** self) {
    auto* object = static_cast<std::uint8_t*>(*self);
    if (!object) return;
    if (--*reinterpret_cast<std::int16_t*>(object + 4) == 0 && object) ei::VCall<void*>(object, 0, 1);
}
EI_REPLACE(0x004586E0, CountedPointerRelease);
EI_REPLACE(0x00545B80, CountedPointerRelease);   // byte-identical copy (A-U11)

} // namespace app
