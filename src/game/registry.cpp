// CFileRegistry: lookups in the game's binary registry files (see registry.h for the format).
#include "registry.h"
#include "asset_text.h"
#include "game_strings.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/text_case.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

const char* const kNotRegistry = "CFileRegistry::IsValid().\nThat is not registry file.\n";   // "CFileRegistry::IsValid().\nThat is not registry file.\n"
const char* const kWrongType = "CFileRegistry::FindData().\nWrong type of entry.\n";     // "CFileRegistry::FindData().\nWrong type of entry.\n"

// _ftol of a float: the game's _ftol (truncates).
int FloatToInt(float value) { return static_cast<int>(libcrt::Ftol(value)); }

std::uint16_t U16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
std::uint32_t U32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
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
BOOL GetDwordArray(CFileRegistry* registry, const char* section, const char* key, unsigned type, std::vector<T>* out) {
    int isScalar;
    const std::uint8_t* value = registry->FindData(section, key, type, &isScalar);
    if (!value) {
        out->clear();
        return 0;
    }
    int count;
    if (isScalar) {
        count = 1;
    } else {
        count = U16(value);
        value += 2;
        if (count == 0) {
            out->clear();
            return 1;
        }
    }
    out->resize(count);
    for (int i = 0; i < count; ++i) (*out)[i] = static_cast<T>(U32(value + 4 * i));
    return 1;
}

} // namespace


CFileRegistry* CFileRegistry::Construct() {
    file_.Construct();
    attached_ = 0;
    data_ = nullptr;
    return this;
}

void CFileRegistry::Destroy() {
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
    file_.Close();                   // the mapped file's destructor
}

BOOL CFileRegistry::Open(const std::string& path) {
    if (!file_.Open(path, 0)) return 0;
    attached_ = 0;
    data_ = static_cast<const std::uint8_t*>(file_.view_);
    if (U32(data_) == kRegistryMagic) return 1;
    game::DebugLog(kNotRegistry);
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
    return 0;
}

BOOL CFileRegistry::Attach(const void* image) {
    attached_ = 1;
    data_ = static_cast<const std::uint8_t*>(image);
    if (U32(data_) == kRegistryMagic) return 1;
    game::DebugLog(kNotRegistry);
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
    return 0;
}

void CFileRegistry::Close() {
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
}

const std::uint8_t* CFileRegistry::FindData(const char* section, const char* key, unsigned type, int* isScalar) {
    const std::uint8_t* root = data_;
    if (!root) return nullptr;

    // The section: chained 6-byte slots after the u16 count at +4.
    std::uint32_t slot = HomeSlot(section, U16(root + 4));
    std::uint32_t sectionLength = static_cast<std::uint32_t>(std::strlen(section));
    const std::uint8_t* record;
    for (;;) {
        const std::uint8_t* entry = root + 6 + 6 * (slot & 0xFFFF);
        record = data_ + U32(entry + 2);
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

BOOL CFileRegistry::GetSectionNames(std::vector<std::string>* out) {
    const std::uint8_t* root = data_;
    int count = U16(root + 4);
    out->resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* record = data_ + U32(root + 8 + 6 * i);
        (*out)[i].assign(Chars(record + 4), U16(record + 2));
    }
    return 1;
}

int CFileRegistry::GetInt(const char* section, const char* key, int defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegInt, nullptr))
        return static_cast<int>(U32(value));
    if (const std::uint8_t* value = FindData(section, key, kRegFloat, nullptr))
        return FloatToInt(F32(value));
    return defaultValue;
}

int CFileRegistry::GetInt2(const char* section, const char* key, int defaultValue) {
    return GetInt(section, key, defaultValue);
}

double CFileRegistry::GetFloat(const char* section, const char* key, float defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegFloat, nullptr))
        return F32(value);
    if (const std::uint8_t* value = FindData(section, key, kRegInt, nullptr))
        return static_cast<int>(U32(value));
    return defaultValue;
}

std::string CFileRegistry::GetString(const char* section, const char* key, const std::string& defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegString, nullptr)) {
        std::string text(Chars(value + 2), U16(value));
        text::FromAssetText(&text);
        std::string result = text;
        return result;
    }
    return defaultValue;
}

void CFileRegistry::GetStringBuffer(const char* section, const char* key, char* buffer, int* length,
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

BOOL CFileRegistry::GetIntArray(const char* section, const char* key, std::vector<std::int32_t>* out) {
    return GetDwordArray(this, section, key, kRegArray | kRegInt, out);
}

BOOL CFileRegistry::GetFloatArray(const char* section, const char* key, std::vector<std::uint32_t>* out) {
    return GetDwordArray(this, section, key, kRegArray | kRegFloat, out);
}

BOOL CFileRegistry::GetStringArray(const char* section, const char* key, std::vector<std::string>* out) {
    int isScalar;
    const std::uint8_t* value = FindData(section, key, kRegArray | kRegString, &isScalar);
    if (!value) {
        out->clear();
        return 0;
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
    return 1;
}
