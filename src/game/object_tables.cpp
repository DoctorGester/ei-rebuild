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

} // namespace

int __fastcall TextKeyFromName(const char* name) {
    std::uint32_t h = 0;
    for (const unsigned char* p = Bytes(name); *p; ++p)
        h = static_cast<std::uint32_t>(libcrt::Toupper(*p)) + h * 5;
    return static_cast<int>(h % 1000000000u + 1000000000u);
}
EI_REPLACE(0x00461360, TextKeyFromName);

BOOL __fastcall IsTextKey(int id) {
    return id >= 1000000000 && id < 2000000000 ? 1 : 0;
}
EI_REPLACE(0x004613A0, IsTextKey);

MapRecord* CMapDatabase::FindMap(const char* name) {
    for (int i = 0; i < maps.size; ++i)
        if (libcrt::Mbsicmp(Bytes(maps.data[i].name), Bytes(name)) == 0) return &maps.data[i];
    return nullptr;
}
EI_REPLACE_METHOD(0x0045E850, CMapDatabase::FindMap);

AllodRecord* CMapDatabase::FindAllod(const char* name) {
    for (int i = 0; i < allods.size; ++i)
        if (libcrt::Mbsicmp(Bytes(allods.data[i].name), Bytes(name)) == 0) return &allods.data[i];
    return nullptr;
}
EI_REPLACE_METHOD(0x0045E910, CMapDatabase::FindAllod);

float NamedFloatTable::Get(const char* name) {
    constexpr float kZero = 0.0f;   // 0073B858
    const Node* node;
    {
        mfc::CString key(name);
        node = Find(key);
    }
    return node ? node->value.mapped : kZero;
}
EI_REPLACE_METHOD(0x00460AF0, NamedFloatTable::Get);

namespace {

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
        EraseKey(key);
        return;
    }
    (*this)[key] = value;
}
EI_REPLACE_METHOD(0x00460BC0, NamedFloatTable::Set);

void NamedFloatTable::Remove(const char* name) {
    mfc::CString key(name);
    EraseKey(key);
}
EI_REPLACE_METHOD(0x00460DB0, NamedFloatTable::Remove);

void NamedFloatTable::CopyPrefixed(const char* prefix, NamedFloatTable* out) {
    std::size_t length = std::strlen(prefix);
    out->Clear();
    for (const Node* node = First(); node; node = Next(node))
        if (Strncmp(prefix, node->value.key, length) == 0) (*out)[node->value.key] = node->value.mapped;
}
EI_REPLACE_METHOD(0x00460EE0, NamedFloatTable::CopyPrefixed);

void NamedFloatTable::Load(CStream* stream) {
    Clear();   // inlined in the original: each chain's nodes freed in order (004616A0), the count zeroed
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
        (*this)[key] = value;
    }
}
EI_REPLACE_METHOD(0x00460FE0, NamedFloatTable::Load);

void NamedFloatTable::Save(CStream* stream) {
    for (const Node* node = First(); node; node = Next(node)) {
        const char* key = node->value.key;
        stream->Write(key, static_cast<std::uint32_t>(std::strlen(key) + 1));
        float value = node->value.mapped;
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
