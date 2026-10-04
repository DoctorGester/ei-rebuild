// Checks for string_map.cpp: ours only. After a set the key holds the value; after a removal the
// key is gone (both looked up by walking the red-black tree with the strings' bytes).
#include "verify.h"

#include <cstdint>
#include <cstring>

namespace strmap {
void __fastcall StringTableSet(void* self, void*, const char* key, const char* value);
void __fastcall StringTableRemove(void* self, void*, const char* key);
} // namespace strmap

namespace {
void* s_unused;

struct StringRep {
    char* start;
    char* finish;
    char* eos;
};
struct Node {
    std::uint32_t color;
    Node* parent;
    Node* left;
    Node* right;
    StringRep key;
    StringRep value;
};

int Compare(const StringRep& s, const char* text) {
    std::size_t a = static_cast<std::size_t>(s.finish - s.start), b = std::strlen(text);
    int c = std::memcmp(s.start, text, a < b ? a : b);
    return c != 0 ? c : (a < b ? -1 : a > b ? 1 : 0);
}
const Node* Lookup(void* self, const char* key) {
    Node* header = *reinterpret_cast<Node**>(static_cast<std::uint8_t*>(self) + 4);
    for (const Node* n = header->parent; n;) {
        int c = Compare(n->key, key);
        if (c == 0) return n;
        n = c > 0 ? n->left : n->right;
    }
    return nullptr;
}

void __fastcall SetVerify(void* self, void*, const char* key, const char* value) {
    strmap::StringTableSet(self, nullptr, key, value);
    const Node* n = Lookup(self, key);
    ei::verify::Check("StringTableSet", true, n && Compare(n->value, value) == 0, key);
}
void __fastcall RemoveVerify(void* self, void*, const char* key) {
    strmap::StringTableRemove(self, nullptr, key);
    ei::verify::Check("StringTableRemove", true, Lookup(self, key) == nullptr, key);
}
} // namespace

EI_VERIFY(0x004447E0, SetVerify, 0, s_unused);
EI_VERIFY(0x004449A0, RemoveVerify, 0, s_unused);
