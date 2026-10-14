// 005A9C90 (thiscall): the figure model's destructor (the model of 005AA5F0, 0x20 bytes: three
// parts at +0, +4, +8 and a hash_map<string, object*> at +0xC: buckets vector +0x10..+0x18,
// element count +0x1C). Every mapped object is deleted (slot 0 with 1), the map cleared (each
// node's pair destroyed, the node returned to the STLport node allocator), each part released
// (slot 3) and deleted (slot 0 with 1), then the map's own destructor runs inline: a second
// clear (empty by then) and the buckets freed. See docs/notes/models.md.
#include "lib/crt.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstdint>

namespace {

struct Node {
    Node* next;
    std::uint8_t key[0xC];     // STLport string
    void* value;
};
struct Table {                 // at model + 0xC
    std::uint32_t hasher;
    Node** begin;
    Node** end;
    Node** capacity;
    int count;                 // +0x10; the node allocator (empty) shares its address
};

// 004298D0: the string's hash (thiscall). 005AE5B0 (RET 4): destroys a node's pair.
__declspec(naked) std::uint32_t __fastcall HashString(const void*) EI_ORIGINAL(0x004298D0)
__declspec(naked) void __fastcall DestroyPair(void*, void*, int) EI_ORIGINAL(0x005AE5B0)
// 00404D70: STLport deallocate(p, bytes). 005AE150 (RET 8): the node allocator's deallocate(p, n).
void __fastcall Deallocate(void* a0, std::size_t a1) { StlNodeFreeOriginal(a0, a1); }
__declspec(naked) void __fastcall NodeDeallocate(void*, void*, Node*, int) EI_ORIGINAL(0x005AE150)
// 00406590: deallocates `count` pointers (the buckets).
void __fastcall DeallocatePointers(void* a0, int a1) { BucketsDeallocateOriginal(a0, (std::size_t)a1); }

// The figure manager's helpers: the model destructor (through its original address), 00674F40
// (the base's clean-up), 005AE5D0 (RET 4) the pair destructor, 004FC080 (ECX node, EDX count)
// the node allocator's deallocate, 00674DF0 the base destructor.
__declspec(naked) void __fastcall ManagerCleanUp(void*) EI_ORIGINAL(0x00674F40)
__declspec(naked) void __fastcall DestroyNamePair(void*, void*, int) EI_ORIGINAL(0x005AE5D0)
__declspec(naked) void __fastcall NodeFree(void*, int) EI_ORIGINAL(0x004FC080)
__declspec(naked) void __fastcall ManagerBaseDestroy(void*) EI_ORIGINAL(0x00674DF0)

std::uint32_t BucketCount(const Table* t) { return static_cast<std::uint32_t>(t->end - t->begin); }

} // namespace

void __fastcall DestroyFigureModel(char* model) {
    auto* table = reinterpret_cast<Table*>(model + 0xC);
    // Every value, in iteration order.
    Node* node = nullptr;
    for (std::uint32_t b = 0; b < BucketCount(table); ++b)
        if (table->begin[b]) {
            node = table->begin[b];
            break;
        }
    while (node) {
        if (node->value) ei::VCall<void>(node->value, 0, 1);
        if (node->next) {
            node = node->next;
            continue;
        }
        std::uint32_t b = HashString(node->key) % BucketCount(table);
        node = nullptr;
        while (++b < BucketCount(table))
            if ((node = table->begin[b]) != nullptr) break;
    }
    // clear().
    for (std::uint32_t b = 0; b < BucketCount(table); ++b) {
        for (Node* n = table->begin[b]; n;) {
            Node* next = n->next;
            DestroyPair(n->key, nullptr, 0);
            if (n) Deallocate(n, sizeof(Node));
            n = next;
        }
        table->begin[b] = nullptr;
    }
    table->count = 0;

    void** parts = reinterpret_cast<void**>(model);
    for (int i = 0; i < 3; ++i) ei::VCall<void>(parts[i], 3);
    for (int i = 0; i < 3; ++i)
        if (parts[i]) ei::VCall<void>(parts[i], 0, 1);

    // The hash_map's destructor.
    for (std::uint32_t b = 0; b < BucketCount(table); ++b) {
        for (Node* n = table->begin[b]; n;) {
            Node* next = n->next;
            DestroyPair(n->key, nullptr, 0);
            NodeDeallocate(&table->count, nullptr, n, 1);
            n = next;
        }
        table->begin[b] = nullptr;
    }
    table->count = 0;
    DeallocatePointers(table->begin, static_cast<int>(table->capacity - table->begin));
}

EI_REPLACE(0x005A9C90, DestroyFigureModel);

// 005AA1C0 (thiscall): the figure manager's destructor: its vtable (0073FB88), every cached model
// destroyed and freed, the base's clean-up (00674F40), the model map cleared and its buckets
// freed (the map's inline destructor), then the base destructor (00674DF0).
void __fastcall DestroyFigureManager(char* manager) {
    *reinterpret_cast<std::uint32_t*>(manager) = 0x0073FB88;
    auto* table = reinterpret_cast<Table*>(manager + 0xBC);
    Node* node = nullptr;
    for (std::uint32_t b = 0; b < BucketCount(table); ++b)
        if (table->begin[b]) {
            node = table->begin[b];
            break;
        }
    while (node) {
        if (void* model = node->value) {
            FigureModelDestructOriginal(model);
            gamecrt::operator_delete(model);
        }
        if (node->next) {
            node = node->next;
            continue;
        }
        std::uint32_t b = HashString(node->key) % BucketCount(table);
        node = nullptr;
        while (++b < BucketCount(table))
            if ((node = table->begin[b]) != nullptr) break;
    }
    ManagerCleanUp(manager);
    for (std::uint32_t b = 0; b < BucketCount(table); ++b) {
        for (Node* n = table->begin[b]; n;) {
            Node* next = n->next;
            DestroyNamePair(n->key, nullptr, 0);
            NodeFree(n, 1);
            n = next;
        }
        table->begin[b] = nullptr;
    }
    table->count = 0;
    if (table->begin) Deallocate(table->begin, static_cast<std::size_t>(table->capacity - table->begin) * 4);
    ManagerBaseDestroy(manager);
}
EI_REPLACE(0x005AA1C0, DestroyFigureManager);
