#include "object_tables.h"
#include "game_strings.h"

#include "core_streams.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstring>
#include <new>

namespace {

const unsigned char* Bytes(const char* s) { return reinterpret_cast<const unsigned char*>(s); }


// STLport __stl_hash_string (00460BA0): h = 5 h + c over signed chars.
std::uint32_t HashString(const char* s) {
    std::uint32_t h = 0;
    for (; *s; ++s) h = h * 5 + static_cast<std::uint32_t>(static_cast<int>(static_cast<signed char>(*s)));
    return h;
}

} // namespace

int __fastcall TextKeyFromName(const char* name) {
    std::uint32_t h = 0;
    for (const unsigned char* p = Bytes(name); *p; ++p)
        h = static_cast<std::uint32_t>(gamecrt::toupper(*p)) + h * 5;
    return static_cast<int>(h % 1000000000u + 1000000000u);
}
EI_REPLACE(0x00461360, TextKeyFromName);

BOOL __fastcall IsTextKey(int id) {
    return id >= 1000000000 && id < 2000000000 ? 1 : 0;
}
EI_REPLACE(0x004613A0, IsTextKey);

MapRecord* CMapDatabase::FindMap(const char* name) {
    for (int i = 0; i < maps.size; ++i)
        if (gamecrt::mbsicmp(Bytes(maps.data[i].name), Bytes(name)) == 0) return &maps.data[i];
    return nullptr;
}
EI_REPLACE_METHOD(0x0045E850, CMapDatabase::FindMap);

AllodRecord* CMapDatabase::FindAllod(const char* name) {
    for (int i = 0; i < allods.size; ++i)
        if (gamecrt::mbsicmp(Bytes(allods.data[i].name), Bytes(name)) == 0) return &allods.data[i];
    return nullptr;
}
EI_REPLACE_METHOD(0x0045E910, CMapDatabase::FindAllod);

float NamedFloatTable::Get(const char* name) {
    constexpr float kZero = 0.0f;   // 0073B858
    Node* node;
    {
        mfc::CString key(name);
        const char* k = key;
        // Bucket count as STLport's vector size: (finish - start) >> 2, signed, then unsigned.
        auto bucketCount = static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<std::uintptr_t>(finish) -
                                                                 reinterpret_cast<std::uintptr_t>(buckets)) >> 2);
        node = buckets[HashString(k) % bucketCount];
        while (node && gamecrt::mbscmp(Bytes(node->key), Bytes(k)) != 0) node = node->next;
    }
    return node ? node->value : kZero;
}
EI_REPLACE_METHOD(0x00460AF0, NamedFloatTable::Get);

namespace {

// STLport's hashtable internals (library): resize (00461750), _M_new_node (004615B0), the node
// allocator's deallocate (00461710: 12 bytes back to the pool's free list at 0079B228).
struct KeyValue {
    mfc::CString key;
    float value;
};
__declspec(naked) void __fastcall TableResize(NamedFloatTable*, void*, std::uint32_t) EI_ORIGINAL(0x00461750)
__declspec(naked) NamedFloatTable::Node* __fastcall TableNewNode(NamedFloatTable*, void*, const KeyValue*) EI_ORIGINAL(0x004615B0)
__declspec(naked) void __fastcall NodeDeallocate(NamedFloatTable::Node*, int) EI_ORIGINAL(0x00461710)
// clear (00461540), operator[] (004613C0: returns the value's address), _M_delete_node (004616A0).
__declspec(naked) float* __fastcall TableIndex(NamedFloatTable*, void*, const mfc::CString*) EI_ORIGINAL(0x004613C0)
void TableClear(NamedFloatTable* table) { FloatTableClearOriginal(table); }

std::uint32_t BucketCount(const NamedFloatTable* table) {
    return static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<std::uintptr_t>(table->finish) -
                                                       reinterpret_cast<std::uintptr_t>(table->buckets)) >> 2);
}

void FreeNode(NamedFloatTable* table, NamedFloatTable::Node* node) {
    reinterpret_cast<mfc::CString*>(&node->key)->~CString();
    NodeDeallocate(node, 1);
    --table->count;
}

// hashtable::erase(key) as STLport writes it: the rest of the chain first, then its head.
void Erase(NamedFloatTable* table, const char* key) {
    std::uint32_t bucket = HashString(key) % BucketCount(table);
    NamedFloatTable::Node* first = table->buckets[bucket];
    if (!first) return;
    NamedFloatTable::Node* prev = first;
    for (NamedFloatTable::Node* cur = first->next; cur;) {
        if (gamecrt::mbscmp(Bytes(cur->key), Bytes(key)) == 0) {
            prev->next = cur->next;
            FreeNode(table, cur);
            cur = prev->next;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
    if (gamecrt::mbscmp(Bytes(first->key), Bytes(key)) == 0) {
        table->buckets[bucket] = first->next;
        FreeNode(table, first);
    }
}

// operator[] as the callers inline it: {key, 0} copied, resize(count + 1), the bucket searched,
// a new node at the chain's head when the key is absent. Returns the value's address.
float* Index(NamedFloatTable* table, const mfc::CString& key) {
    NamedFloatTable::Node* node;
    {
        KeyValue entry{key, 0.0f};
        TableResize(table, nullptr, table->count + 1);
        std::uint32_t bucket = HashString(entry.key) % BucketCount(table);
        NamedFloatTable::Node* first = table->buckets[bucket];
        node = first;
        while (node && gamecrt::mbscmp(Bytes(node->key), Bytes(entry.key)) != 0) node = node->next;
        if (!node) {
            node = TableNewNode(table, nullptr, &entry);
            node->next = first;
            table->buckets[bucket] = node;
            ++table->count;
        }
    }
    return &node->value;
}

// Iteration in bucket order (STLport's iterator: the chain, then the next non-empty bucket after
// the node's own, found by hashing its key again).
NamedFloatTable::Node* First(NamedFloatTable* table) {
    for (std::uint32_t i = 0; i < BucketCount(table); ++i)
        if (table->buckets[i]) return table->buckets[i];
    return nullptr;
}
NamedFloatTable::Node* Next(NamedFloatTable* table, NamedFloatTable::Node* node) {
    if (node->next) return node->next;
    std::uint32_t bucket = HashString(node->key) % BucketCount(table);
    while (++bucket < BucketCount(table))
        if (table->buckets[bucket]) return table->buckets[bucket];
    return nullptr;
}

// FCOMP against 0 sets C3 for equal and for unordered operands.
bool ZeroOrNaN(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits &= 0x7FFFFFFFu;
    return bits == 0 || bits > 0x7F800000u;
}

} // namespace

void NamedFloatTable::Set(const char* name, float value) {
    if (!*name) return;
    mfc::CString key(name);
    if (ZeroOrNaN(value)) {
        Erase(this, key);
        return;
    }
    *Index(this, key) = value;
}
EI_REPLACE_METHOD(0x00460BC0, NamedFloatTable::Set);

void NamedFloatTable::Remove(const char* name) {
    mfc::CString key(name);
    Erase(this, key);
}
EI_REPLACE_METHOD(0x00460DB0, NamedFloatTable::Remove);

void NamedFloatTable::CopyPrefixed(const char* prefix, NamedFloatTable* out) {
    std::size_t length = std::strlen(prefix);
    TableClear(out);
    for (Node* node = First(this); node; node = Next(this, node))
        if (Strncmp(prefix, node->key, length) == 0) *TableIndex(out, nullptr, reinterpret_cast<const mfc::CString*>(&node->key)) = node->value;
}
EI_REPLACE_METHOD(0x00460EE0, NamedFloatTable::CopyPrefixed);

void NamedFloatTable::Load(CStream* stream) {
    // clear() inlined: each chain's nodes freed in order (004616A0), then the count zeroed.
    for (std::uint32_t i = 0; i < BucketCount(this); ++i) {
        for (Node* node = buckets[i]; node;) {
            Node* next = node->next;
            FloatTableDeleteNodeOriginal(this, nullptr, node);
            node = next;
        }
        buckets[i] = nullptr;
    }
    count = 0;
    for (;;) {
        mfc::CString key;
        {
            alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
            mfc::CString* read = stream->ReadString(raw);
            key = *read;
            read->~CString();
        }
        if (gamecrt::mbscmp(Bytes(key), Bytes(game::EmptyText())) == 0) break;
        float value;
        stream->Read(&value, 4);
        *Index(this, key) = value;
    }
}
EI_REPLACE_METHOD(0x00460FE0, NamedFloatTable::Load);

void NamedFloatTable::Save(CStream* stream) {
    for (Node* node = First(this); node; node = Next(this, node)) {
        stream->Write(node->key, static_cast<std::uint32_t>(std::strlen(node->key) + 1));
        float value = node->value;
        stream->Write(&value, 4);
    }
    mfc::CString end;
    stream->Write(static_cast<const char*>(end), static_cast<std::uint32_t>(std::strlen(end) + 1));
}
EI_REPLACE_METHOD(0x00461260, NamedFloatTable::Save);

void* __fastcall FindCommandByKey(int key) {
    const auto* keys = reinterpret_cast<const signed char*>(0x0079BF70);
    for (int slot = 0; slot < 0x200; ++slot) {
        if (keys[slot] != key) continue;
        for (std::uintptr_t entry = 0x007857B0; entry < 0x00787070; entry += 0x48)
            if (*reinterpret_cast<const int*>(entry) == slot) return reinterpret_cast<void*>(entry + 8);
    }
    return nullptr;
}
EI_REPLACE(0x00462F70, FindCommandByKey);
