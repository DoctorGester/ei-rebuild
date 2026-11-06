// FileRegistry: lookups in the game's binary registry files (see registry.h for the format).
#include "game/data/registry.h"
#include "game/core/asset_text.h"

#include "game/console/console.h"
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

// The file's layout. Sections and keys are found in hash tables whose slots chain on collisions; names and
// strings are stored inline, without a terminator.
#pragma pack(push, 1)
// The file starts with this; the section table's slots follow.
struct RegistryHeader {
    std::uint32_t magic = 0;            // kRegistryMagic
    std::uint16_t sectionSlots = 0;
};
// A table slot: the next slot of the chain (0xFFFF: the end) and the entry's offset (a section's from the file
// start, a key's from its section record).
struct RegistrySlot {
    std::uint16_t next = 0;
    std::uint32_t offset = 0;
};
// A section record: the name's characters follow, then the key table (its slots).
struct RegistrySection {
    std::uint16_t keySlots = 0;
    std::uint16_t nameLength = 0;
};
// A key's entry in a section: the name's characters follow, then the value (an int or a float; a string or an
// array with its 16-bit length or count first).
struct RegistryKey {
    std::uint8_t type = 0;              // RegistryType
    std::uint16_t nameLength = 0;
};
#pragma pack(pop)
static_assert(sizeof(RegistryHeader) == 6 && sizeof(RegistrySlot) == 6 && sizeof(RegistrySection) == 4 &&
              sizeof(RegistryKey) == 3);

// The text bytes at `p`.
const char* Chars(const std::uint8_t* p) { return reinterpret_cast<const char*>(p); }

// The home slot of `name` in a table of `slots` entries.
std::uint32_t HomeSlot(std::string_view name, std::uint16_t slots) {
    std::uint32_t sum = 0;
    for (const char c : name) sum += static_cast<std::uint32_t>(text::ToLower(c));
    return sum % slots;
}

// A table's slot `index`, the table starting at `table`.
RegistrySlot SlotAt(const std::uint8_t* table, std::uint32_t index) {
    return lib::Load<RegistrySlot>(table + sizeof(RegistrySlot) * index);
}

// GetIntArray / GetFloatArray: the values are copied bit for bit.
template <typename T>
bool GetDwordArray(FileRegistry* registry, std::string_view section, std::string_view key, RegistryType type, std::vector<T>* out) {
    bool isScalar;
    const std::uint8_t* value = registry->FindData(section, key, type, &isScalar);
    if (!value) {
        out->clear();
        return false;
    }
    int count = 1;
    if (!isScalar) {
        count = lib::Load<std::uint16_t>(value);
        value += 2;
    }
    out->resize(count);
    for (int i = 0; i < count; ++i) (*out)[i] = lib::Load<T>(value + 4 * i);
    return true;
}

} // namespace

bool FileRegistry::Open(const std::string& path) {
    if (!file.Open(path)) {
        game::DebugLog("CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
        return false;
    }
    attached = false;
    data = static_cast<const std::uint8_t*>(file.Data());
    if (lib::Load<RegistryHeader>(data).magic == kRegistryMagic) return true;
    game::DebugLog(kNotRegistry);
    Close();
    return false;
}

bool FileRegistry::Attach(const void* image) {
    attached = true;
    data = static_cast<const std::uint8_t*>(image);
    if (lib::Load<RegistryHeader>(data).magic == kRegistryMagic) return true;
    game::DebugLog(kNotRegistry);
    Close();
    return false;
}

void FileRegistry::Close() {
    if (!attached) file.Close();
    attached = false;
    data = nullptr;
}

const std::uint8_t* FileRegistry::FindData(std::string_view section, std::string_view key, RegistryType type, bool* isScalar) {
    if (!data) return nullptr;

    // The section: the slots after the header.
    const std::uint8_t* sectionTable = data + sizeof(RegistryHeader);
    std::uint32_t slot = HomeSlot(section, lib::Load<RegistryHeader>(data).sectionSlots);
    const auto sectionLength = static_cast<std::uint16_t>(section.size());
    const std::uint8_t* record;
    for (;;) {
        const RegistrySlot entry = SlotAt(sectionTable, slot);
        record = data + entry.offset;
        const auto head = lib::Load<RegistrySection>(record);
        if (head.nameLength == sectionLength &&
            text::CompareIgnoringCase(Chars(record + sizeof(head)), section.data(), sectionLength, false) == 0)
            break;
        slot = entry.next;
        if (slot == 0xFFFF) return nullptr;
    }
    // The key: the section's own table follows its name.
    const std::uint8_t* keyTable = record + sizeof(RegistrySection) + sectionLength;
    slot = HomeSlot(key, lib::Load<RegistrySection>(record).keySlots);
    const auto keyLength = static_cast<std::uint16_t>(key.size());
    const std::uint8_t* value;
    for (;;) {
        const RegistrySlot entry = SlotAt(keyTable, slot);
        value = record + entry.offset;
        const auto head = lib::Load<RegistryKey>(value);
        if (head.nameLength == keyLength &&
            text::CompareIgnoringCase(Chars(value + sizeof(head)), key.data(), keyLength, false) == 0)
            break;
        slot = entry.next;
        if (slot == 0xFFFF) return nullptr;
    }

    const auto found = static_cast<RegistryType>(lib::Load<RegistryKey>(value).type);
    if (isScalar) *isScalar = !lib::Has(type, RegistryType::Array);
    if (found != type) {
        if (found != (type & ~RegistryType::Array)) {
            game::DebugLog(kWrongType);
            return nullptr;
        }
        if (isScalar) *isScalar = true;
    }
    return value + sizeof(RegistryKey) + keyLength;
}

std::vector<std::string> FileRegistry::SectionNames() {
    const auto count = lib::Load<RegistryHeader>(data).sectionSlots;
    std::vector<std::string> names(count);
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* record = data + SlotAt(data + sizeof(RegistryHeader), i).offset;
        const auto head = lib::Load<RegistrySection>(record);
        names[i].assign(Chars(record + sizeof(head)), head.nameLength);
    }
    return names;
}

int FileRegistry::GetInt(std::string_view section, std::string_view key, int defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, RegistryType::Int, nullptr))
        return static_cast<int>(lib::Load<std::uint32_t>(value));
    if (const std::uint8_t* value = FindData(section, key, RegistryType::Float, nullptr))
        return lib::Truncate(lib::Load<float>(value));
    return defaultValue;
}

double FileRegistry::GetFloat(std::string_view section, std::string_view key, float defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, RegistryType::Float, nullptr))
        return lib::Load<float>(value);
    if (const std::uint8_t* value = FindData(section, key, RegistryType::Int, nullptr))
        return static_cast<int>(lib::Load<std::uint32_t>(value));
    return defaultValue;
}

std::string FileRegistry::GetString(std::string_view section, std::string_view key, const std::string& defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, RegistryType::String, nullptr)) {
        std::string text(Chars(value + 2), lib::Load<std::uint16_t>(value));
        text::FromAssetText(&text);
        std::string result = text;
        return result;
    }
    return defaultValue;
}

void FileRegistry::GetStringBuffer(std::string_view section, std::string_view key, char* buffer, int* length,
                                    const char* defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, RegistryType::String, nullptr)) {
        // The UTF-8 text is cut to the buffer at a character's end (the original cut the asset's bytes, by their
        // 16-bit length).
        const std::string string =
            text::FromAssetText(std::string_view(Chars(value + 2), lib::Load<std::uint16_t>(value)));
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

bool FileRegistry::GetIntArray(std::string_view section, std::string_view key, std::vector<std::int32_t>* out) {
    return GetDwordArray(this, section, key, RegistryType::IntArray, out);
}

bool FileRegistry::GetFloatArray(std::string_view section, std::string_view key, std::vector<float>* out) {
    return GetDwordArray(this, section, key, RegistryType::FloatArray, out);
}

bool FileRegistry::GetStringArray(std::string_view section, std::string_view key, std::vector<std::string>* out) {
    bool isScalar;
    const std::uint8_t* value = FindData(section, key, RegistryType::StringArray, &isScalar);
    if (!value) {
        out->clear();
        return false;
    }
    int count = 1;
    if (!isScalar) {
        count = lib::Load<std::uint16_t>(value);
        value += 2;
    }
    out->resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::uint16_t length = lib::Load<std::uint16_t>(value);
        (*out)[i] = text::FromAssetText(std::string_view(Chars(value + 2), length));
        value += length + 2;
    }
    return true;
}
