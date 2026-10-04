// The string table at 0079B930 (vtable 0073BFA4 on the abstract 0073BFAC): an STLport
// map<string, string> at +4 with a setter (slot 0) and a remover (slot 1); 004447D0 returns the
// table. The map's algorithms are library code. The constructor (00444740) runs from a static
// initialiser before the hooks are installed and is not rewritten. See
// docs/notes/unmapped_410_470.md ("Ninth round").
#include "ei/hook.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace strmap {

namespace {

struct StringRep {               // STLport basic_string
    char* start;
    char* finish;
    char* eos;
};
struct Node {                    // _Rb_tree node (0x28 bytes)
    std::uint32_t color;
    Node* parent;
    Node* left;
    Node* right;
    StringRep key;               // +0x10
    StringRep value;             // +0x1C
};
static_assert(sizeof(Node) == 0x28);
struct Tree {                    // the map at +4 of the table
    Node* header;
    std::uint32_t count;
};

// STLport (library).
__declspec(naked) Node** __fastcall LowerBound(Tree*, void*, Node** /*out*/, const StringRep*) EI_ORIGINAL(0x004456E0)
__declspec(naked) Node** __fastcall Find(Tree*, void*, Node** /*out*/, const StringRep*) EI_ORIGINAL(0x00445620)
__declspec(naked) bool __fastcall Less(const StringRep* /*a*/, const StringRep* /*b*/) EI_ORIGINAL(0x00445780)
__declspec(naked) void __fastcall StringReserve(StringRep*, void*, const void* /*alloc*/, std::size_t) EI_ORIGINAL(0x00421800)
__declspec(naked) void* __fastcall PairConstruct(void* /*pair*/, void*, const StringRep*, const StringRep*) EI_ORIGINAL(0x00445170)
__declspec(naked) Node** __fastcall InsertHint(Tree*, void*, Node** /*out*/, Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x004450B0)
__declspec(naked) void __fastcall PairDestruct(void*) EI_ORIGINAL(0x00445040)
__declspec(naked) void __fastcall StringDestruct(StringRep*) EI_ORIGINAL(0x0040C720)
__declspec(naked) void __fastcall StringErase(StringRep*, void*, char* /*first*/, char* /*last*/) EI_ORIGINAL(0x0040B3C0)
__declspec(naked) void __fastcall StringAppend(StringRep*, void*, const char* /*first*/, const char* /*last*/, int /*tag*/) EI_ORIGINAL(0x0040C280)
__declspec(naked) void __fastcall Erase(Tree*, void*, Node* /*position*/) EI_ORIGINAL(0x00445010)
__declspec(naked) void __fastcall EraseSubtree(Tree*, void*, Node*) EI_ORIGINAL(0x00445310)
__declspec(naked) void __fastcall PairDelete(void*, void*, unsigned) EI_ORIGINAL(0x004457F0)
__declspec(naked) void __fastcall NodeFree(Node*, std::size_t) EI_ORIGINAL(0x004452D0)

Tree& TreeOf(void* self) { return *reinterpret_cast<Tree*>(static_cast<std::uint8_t*>(self) + 4); }

void ReleaseString(StringRep& s) {
    if (s.start) stlp::deallocate(s.start, static_cast<std::size_t>(s.eos - s.start));
}

} // namespace

// 004447D0.
void* __cdecl StringTable() { return reinterpret_cast<void*>(0x0079B930); }
EI_REPLACE(0x004447D0, StringTable);

// 004447E0 (slot 0): table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void __fastcall StringTableSet(void* self, void*, const char* key, const char* value) {
    StringRep k{};
    new (&k) stlp::string(key, key + std::strlen(key));
    Tree& tree = TreeOf(self);
    Node* it;
    LowerBound(&tree, nullptr, &it, &k);
    if (it == tree.header || Less(&k, &it->key)) {
        StringRep empty;
        char alloc;
        StringReserve(&empty, nullptr, &alloc, 8);
        *empty.start = 0;
        alignas(4) std::uint8_t pair[sizeof(StringRep) * 2];
        PairConstruct(pair, nullptr, &k, &empty);
        Node* inserted;
        InsertHint(&tree, nullptr, &inserted, it, pair);
        it = inserted;
        PairDestruct(pair);
        StringDestruct(&empty);
    }
    StringRep& s = it->value;
    std::size_t length = std::strlen(value);
    std::size_t size = static_cast<std::size_t>(s.finish - s.start);
    if (length <= size) {
        std::memcpy(s.start, value, length);
        StringErase(&s, nullptr, s.start + length, s.finish);
    } else {
        std::memcpy(s.start, value, size);
        StringAppend(&s, nullptr, value + size, value + length, 0);
    }
    ReleaseString(k);
}
EI_REPLACE(0x004447E0, StringTableSet);

// 004449A0 (slot 1): removes `key`.
void __fastcall StringTableRemove(void* self, void*, const char* key) {
    StringRep k{};
    new (&k) stlp::string(key, key + std::strlen(key));
    Tree& tree = TreeOf(self);
    Node* it;
    Find(&tree, nullptr, &it, &k);
    ReleaseString(k);
    if (it != tree.header) Erase(&tree, nullptr, it);
}
EI_REPLACE(0x004449A0, StringTableRemove);

// 00444690: the table's destructor (at exit): every node destroyed and freed, then the header.
void __fastcall StringTableDestruct(void* self) {
    Tree& tree = TreeOf(self);
    if (tree.count) {
        Node* node = tree.header->parent;
        while (node) {
            EraseSubtree(&tree, nullptr, node->right);
            Node* left = node->left;
            PairDelete(&node->key, nullptr, 0);
            NodeFree(node, 1);
            node = left;
        }
        tree.header->left = tree.header;
        tree.header->parent = nullptr;
        tree.header->right = tree.header;
        tree.count = 0;
    }
    if (Node* header = tree.header) {
        *reinterpret_cast<void**>(header) = *reinterpret_cast<void**>(0x0079B234);
        *reinterpret_cast<void**>(0x0079B234) = header;
    }
}
EI_REPLACE(0x00444690, StringTableDestruct);

} // namespace strmap
