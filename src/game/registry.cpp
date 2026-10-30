// FileRegistry: lookups in the game's binary registry files (see registry.h for the format).
#include "registry.h"
#include "asset_text.h"
#include "game_strings.h"

#include "console.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "lib/rounding.h"

namespace {

const char* const kNotRegistry = "CFileRegistry::IsValid().\nThat is not registry file.\n";   // "FileRegistry::IsValid().\nThat is not registry file.\n"
const char* const kWrongType = "CFileRegistry::FindData().\nWrong type of entry.\n";     // "FileRegistry::FindData().\nWrong type of entry.\n"

std::uint16_t U16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
std::uint32_t U32(const std::uint8_t* p) {
    std::uint32_t v;
    lib::LoadBytes(v, p);
    return v;
}
float F32(const std::uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
// The text bytes at `p` (names and strings are stored inline, without a terminator).
const char* Chars(const std::uint8_t* p) { return reinterpret_cast<const char*>(p); }

// The home slot of `name` in a table of `slots` entries.
std::uint32_t HomeSlot(const char* name, std::uint16_t slots) {
    std::uint32_t sum = 0;
    for (const char* p = name; *p; ++p) sum += static_cast<std::uint32_t>(text::ToLower(*p));
    return sum % slots;
}

// GetIntArray / GetFloatArray: the values are copied bit for bit.
template <typename T>
bool GetDwordArray(FileRegistry* registry, const char* section, const char* key, unsigned type, std::vector<T>* out) {
    int isScalar;
    const std::uint8_t* value = registry->FindData(section, key, type, &isScalar);
    if (!value) {
        out->clear();
        return false;
    }
    int count;
    if (isScalar) {
        count = 1;
    } else {
        count = U16(value);
        value += 2;
        if (count == 0) {
            out->clear();
            return true;
        }
    }
    out->resize(count);
    for (int i = 0; i < count; ++i) (*out)[i] = static_cast<T>(U32(value + 4 * i));
    return true;
}

} // namespace


bool FileRegistry::Open(const std::string& path) {
    if (!file.Open(path, 0)) return false;
    attached = 0;
    data = static_cast<const std::uint8_t*>(file.mapping.View());
    if (U32(data) == kRegistryMagic) return true;
    game::DebugLog(kNotRegistry);
    if (!attached) file.Close();
    attached = 0;
    data = nullptr;
    return false;
}

bool FileRegistry::Attach(const void* image) {
    attached = 1;
    data = static_cast<const std::uint8_t*>(image);
    if (U32(data) == kRegistryMagic) return true;
    game::DebugLog(kNotRegistry);
    if (!attached) file.Close();
    attached = 0;
    data = nullptr;
    return false;
}

void FileRegistry::Close() {
    if (!attached) file.Close();
    attached = 0;
    data = nullptr;
}

const std::uint8_t* FileRegistry::FindData(const char* section, const char* key, unsigned type, int* isScalar) {
    const std::uint8_t* root = data;
    if (!root) return nullptr;

    // The section: chained 6-byte slots after the u16 count at +4.
    std::uint32_t slot = HomeSlot(section, U16(root + 4));
    std::uint32_t sectionLength = static_cast<std::uint32_t>(std::strlen(section));
    const std::uint8_t* record;
    for (;;) {
        const std::uint8_t* entry = root + 6 + 6 * (slot & 0xFFFF);
        record = data + U32(entry + 2);
        if (U16(record + 2) == static_cast<std::uint16_t>(sectionLength) &&
            text::CompareIgnoringCase(Chars(record + 4), section, sectionLength & 0xFFFF, false) == 0)
            break;
        slot = U16(entry);
        if (slot == 0xFFFF) return nullptr;
    }
    if (!key) return record;

    // The key: the section's own table follows its name.
    const std::uint8_t* keyTable = record + 4 + (sectionLength & 0xFFFF);
    slot = HomeSlot(key, U16(record));
    std::uint32_t keyLength = static_cast<std::uint32_t>(std::strlen(key));
    const std::uint8_t* value;
    for (;;) {
        const std::uint8_t* entry = keyTable + 6 * (slot & 0xFFFF);
        value = record + U32(entry + 2);
        if (U16(value + 1) == static_cast<std::uint16_t>(keyLength) &&
            text::CompareIgnoringCase(Chars(value + 3), key, keyLength & 0xFFFF, false) == 0)
            break;
        slot = U16(entry);
        if (slot == 0xFFFF) return nullptr;
    }

    std::uint8_t wanted = static_cast<std::uint8_t>(type);
    if (isScalar) *isScalar = (wanted & 0x80) ? 0 : 1;
    if (value[0] != wanted) {
        if (value[0] != (wanted & 0x7F)) {
            game::DebugLog(kWrongType);
            return nullptr;
        }
        if (isScalar) *isScalar = 1;
    }
    return value + 3 + (keyLength & 0xFFFF);
}

std::vector<std::string> FileRegistry::SectionNames() {
    const std::uint8_t* root = data;
    int count = U16(root + 4);
    std::vector<std::string> names(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* record = data + U32(root + 8 + 6 * i);
        names[i].assign(Chars(record + 4), U16(record + 2));
    }
    return names;
}

int FileRegistry::GetInt(const char* section, const char* key, int defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegInt, nullptr))
        return static_cast<int>(U32(value));
    if (const std::uint8_t* value = FindData(section, key, kRegFloat, nullptr))
        return lib::Truncate(F32(value));
    return defaultValue;
}

double FileRegistry::GetFloat(const char* section, const char* key, float defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegFloat, nullptr))
        return F32(value);
    if (const std::uint8_t* value = FindData(section, key, kRegInt, nullptr))
        return static_cast<int>(U32(value));
    return defaultValue;
}

std::string FileRegistry::GetString(const char* section, const char* key, const std::string& defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegString, nullptr)) {
        std::string text(Chars(value + 2), U16(value));
        text::FromAssetText(&text);
        std::string result = text;
        return result;
    }
    return defaultValue;
}

void FileRegistry::GetStringBuffer(const char* section, const char* key, char* buffer, int* length,
                                    const char* defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegString, nullptr)) {
        // The UTF-8 text is cut to the buffer at a character's end (the original cut the asset's bytes, by their
        // 16-bit length).
        const std::string string = text::FromAssetText(std::string_view(Chars(value + 2), U16(value)));
        const int count = static_cast<int>(text::FittingBytes(string, static_cast<std::size_t>(*length - 1)));
        #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
        std::strncpy(buffer, string.data(), static_cast<std::size_t>(count));
        buffer[count] = '\0';
        *length = count;
        return;
    }
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    std::strncpy(buffer, defaultValue, static_cast<std::size_t>(*length));
    *length = static_cast<int>(std::strlen(buffer));
}

bool FileRegistry::GetIntArray(const char* section, const char* key, std::vector<std::int32_t>* out) {
    return GetDwordArray(this, section, key, kRegArray | kRegInt, out);
}

bool FileRegistry::GetFloatArray(const char* section, const char* key, std::vector<std::uint32_t>* out) {
    return GetDwordArray(this, section, key, kRegArray | kRegFloat, out);
}

bool FileRegistry::GetStringArray(const char* section, const char* key, std::vector<std::string>* out) {
    int isScalar;
    const std::uint8_t* value = FindData(section, key, kRegArray | kRegString, &isScalar);
    if (!value) {
        out->clear();
        return false;
    }
    int count;
    if (isScalar) {
        count = 1;
    } else {
        count = U16(value);
        value += 2;
    }
    out->resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::uint16_t length = U16(value);
        (*out)[i] = text::FromAssetText(std::string_view(Chars(value + 2), length));
        value += length + 2;
    }
    return true;
}
