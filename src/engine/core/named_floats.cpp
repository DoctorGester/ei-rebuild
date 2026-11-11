// NamedFloatTable's members. See named_floats.h.
#include "engine/core/named_floats.h"

#include "engine/core/core_streams.h"

#include <string>

float NamedFloatTable::Get(std::string_view name) {
    const auto node = find(std::string(name));
    return node != end() ? node->second : 0.0f;
}

void NamedFloatTable::Set(std::string_view name, float value) {
    if (name.empty()) return;
    std::string key(name);
    if (value == 0.0f) {
        erase(key);
        return;
    }
    (*this)[key] = value;
}

void NamedFloatTable::Remove(std::string_view name) { erase(std::string(name)); }

void NamedFloatTable::CopyPrefixed(std::string_view prefix, NamedFloatTable* out) {
    out->clear();
    for (const auto& [key, value] : *this)
        if (key.starts_with(prefix)) (*out)[key] = value;
}

void NamedFloatTable::Load(ByteStream* stream) {
    clear();
    for (;;) {
        std::string key = stream->ReadString();
        if (key.empty()) break;
        float value;
        stream->ReadValue(value);
        (*this)[key] = value;
    }
}

void NamedFloatTable::Save(ByteStream* stream) {
    for (const auto& [name, value] : *this) {
        stream->WriteString(name);
        stream->WriteValue(value);
    }
    stream->WriteString("");   // the end
}
