#include "object_tables.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"

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
