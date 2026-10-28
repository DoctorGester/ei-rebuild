#include "object_tables.h"
#include "game_strings.h"

#include "core_streams.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

int TextKeyFromName(const char* name) {
    std::uint32_t h = 0;
    for (const char* p = name; *p; ++p)
        h = static_cast<std::uint32_t>(libcrt::Toupper(static_cast<unsigned char>(*p))) + h * 5;
    return static_cast<int>(h % 1000000000u + 1000000000u);
}

BOOL IsTextKey(int id) {
    return id >= 1000000000 && id < 2000000000 ? 1 : 0;
}

MapRecord* CMapDatabase::FindMap(const char* name) {
    for (int i = 0; i < maps.size; ++i)
        if (gamecrt::mbsicmp(maps.data[i].name, name) == 0) return &maps.data[i];
    return nullptr;
}

AllodRecord* CMapDatabase::FindAllod(const char* name) {
    for (int i = 0; i < allods.size; ++i)
        if (gamecrt::mbsicmp(allods.data[i].name, name) == 0) return &allods.data[i];
    return nullptr;
}

float NamedFloatTable::Get(const char* name) {
    constexpr float kZero = 0.0f;
    const_iterator node;
    {
        std::string key = name;
        node = find(key);
    }
    return node != end() ? node->second : kZero;
}

void NamedFloatTable::Set(const char* name, float value) {
    if (!*name) return;
    std::string key = name;
    EI_NAN_AUDIT(value);
    if (value == 0.0f) {
        erase(key);
        return;
    }
    (*this)[key] = value;
}

void NamedFloatTable::Remove(const char* name) {
    std::string key = name;
    erase(key);
}

void NamedFloatTable::CopyPrefixed(const char* prefix, NamedFloatTable* out) {
    std::size_t length = std::strlen(prefix);
    out->clear();
    for (const auto& [key, value] : *this)
        if (gamecrt::strncmp(prefix, key.c_str(), length) == 0) (*out)[key] = value;
}

void NamedFloatTable::Load(CStream* stream) {
    clear();
    for (;;) {
        std::string key;
        {
            std::string raw;
            std::string* read = stream->ReadString(&raw);
            key = *read;
        }
        if (gamecrt::mbscmp(key, game::EmptyText()) == 0) break;
        float value;
        stream->Read(&value, 4);
        (*this)[key] = value;
    }
}

void NamedFloatTable::Save(CStream* stream) {
    for (const auto& [name, value] : *this) {
        stream->WriteString(name);
        stream->Write(&value, 4);
    }
    stream->WriteString("");   // the end
}

void* FindCommandByKey(int key) {
    const signed char* keys = g_keyActions;
    for (int slot = 0; slot < 0x200; ++slot) {
        if (keys[slot] != key) continue;
        for (UiKeyName& entry : g_keyNames)
            if (entry.scan_ == slot) return entry.name_;
    }
    return nullptr;
}
