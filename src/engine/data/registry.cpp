// FileRegistry: the game's registry files read into sections (see registry.h), from the binary format or TOML.
#include "engine/data/registry.h"
#include "engine/core/asset_text.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/rounding.h"
#include "lib/text_case.h"
#include "engine/data/resource.h"
#include "lib/toml.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const char* const kNotRegistry = "CFileRegistry::IsValid().\nThat is not registry file.\n";   // "FileRegistry::IsValid().\nThat is not registry file.\n"
const char* const kWrongType = "CFileRegistry::FindData().\nWrong type of entry.\n";     // "FileRegistry::FindData().\nWrong type of entry.\n"

// The binary file's layout. Sections and keys are found in hash tables whose slots chain on collisions (every slot
// holds an entry: the table's size is their count); names and strings are stored inline, without a terminator.
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
struct RegistrySectionHead {
    std::uint16_t keySlots = 0;
    std::uint16_t nameLength = 0;
};
// A key's entry in a section: the name's characters follow, then the value (an int or a float; a string or an
// array with its 16-bit length or count first).
struct RegistryKeyHead {
    std::uint8_t type = 0;              // RegistryType
    std::uint16_t nameLength = 0;
};
#pragma pack(pop)
static_assert(sizeof(RegistryHeader) == 6 && sizeof(RegistrySlot) == 6 && sizeof(RegistrySectionHead) == 4 &&
              sizeof(RegistryKeyHead) == 3);

// The binary image's reader: false for a read that falls outside the image.
class ImageReader {
public:
    ImageReader(const void* image, std::size_t size) : data(static_cast<const std::uint8_t*>(image)), size(size) {}

    template <class T>
    bool Load(std::size_t offset, T* out) {
        if (offset > size || size - offset < sizeof(T)) return false;
        *out = lib::Load<T>(data + offset);
        return true;
    }
    bool Text(std::size_t offset, std::size_t length, std::string* out) {
        if (offset > size || size - offset < length) return false;
        out->assign(reinterpret_cast<const char*>(data + offset), length);
        return true;
    }

    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// The value at `offset` (after its name) of type `type` into `value`.
bool ReadValue(ImageReader& image, std::size_t offset, RegistryValue* value) {
    const bool array = lib::Has(value->type, RegistryType::Array);
    std::uint16_t count = 1;
    if (array) {
        if (!image.Load(offset, &count)) return false;
        offset += 2;
    }
    for (int i = 0; i < count; ++i) {
        if ((value->type & ~RegistryType::Array) == RegistryType::String) {
            std::uint16_t length = 0;
            std::string text;
            if (!image.Load(offset, &length) || !image.Text(offset + 2, length, &text)) return false;
            value->strings.push_back(text::FromAssetText(text));
            offset += 2 + length;
        } else {
            std::uint32_t word = 0;
            if (!image.Load(offset, &word)) return false;
            value->words.push_back(word);
            offset += 4;
        }
    }
    return true;
}

// The sections of the binary image, in slot order (each key in its section's slot order).
bool ReadImage(ImageReader& image, std::vector<RegistrySection>* sections) {
    RegistryHeader header;
    if (!image.Load(0, &header) || header.magic != kRegistryMagic) return false;
    for (std::size_t i = 0; i < header.sectionSlots; ++i) {
        RegistrySlot slot;
        RegistrySectionHead head;
        RegistrySection& section = sections->emplace_back();
        if (!image.Load(sizeof(RegistryHeader) + sizeof(RegistrySlot) * i, &slot) || !image.Load(slot.offset, &head) ||
            !image.Text(slot.offset + sizeof(head), head.nameLength, &section.name))
            return false;
        const std::size_t keyTable = slot.offset + sizeof(head) + head.nameLength;
        for (std::size_t k = 0; k < head.keySlots; ++k) {
            RegistrySlot keySlot;
            RegistryKeyHead key;
            RegistryValue& value = section.values.emplace_back();
            if (!image.Load(keyTable + sizeof(RegistrySlot) * k, &keySlot)) return false;
            const std::size_t at = std::size_t{slot.offset} + keySlot.offset;
            if (!image.Load(at, &key) || !image.Text(at + sizeof(key), key.nameLength, &value.name)) return false;
            value.type = static_cast<RegistryType>(key.type);
            if (!ReadValue(image, at + sizeof(key) + key.nameLength, &value)) return false;
        }
    }
    return true;
}

// The first of `items` named `name` (without case), or null.
template <class T>
T* Named(std::vector<T>& items, std::string_view name) {
    for (T& item : items)
        if (item.name.size() == name.size() && text::CompareIgnoringCase(item.name.data(), name.data(), name.size(), false) == 0)
            return &item;
    return nullptr;
}

// GetIntArray / GetFloatArray: the values are copied bit for bit.
template <typename T>
bool GetDwordArray(FileRegistry* registry, std::string_view section, std::string_view key, RegistryType type, std::vector<T>* out) {
    const RegistryValue* value = registry->Find(section, key, type, nullptr);
    if (!value) {
        out->clear();
        return false;
    }
    out->resize(value->words.size());
    for (std::size_t i = 0; i < value->words.size(); ++i) (*out)[i] = std::bit_cast<T>(value->words[i]);
    return true;
}

// A TOML value as a registry value (its name already set); false when it is none of the registry's types.
bool FromToml(const toml::Value& from, RegistryValue* value) {
    auto word = [](const toml::Value& number, bool asFloat) {
        if (!asFloat) return static_cast<std::uint32_t>(number.Int());
        return std::bit_cast<std::uint32_t>(static_cast<float>(number.IsInt() ? static_cast<double>(number.Int()) : number.Float()));
    };
    if (from.IsString()) {
        value->type = RegistryType::String;
        value->strings.emplace_back(from.String());
    } else if (from.IsBool()) {
        value->type = RegistryType::Int;
        value->words.push_back(from.Bool() ? 1u : 0u);
    } else if (from.IsInt() || from.IsFloat()) {
        value->type = from.IsFloat() ? RegistryType::Float : RegistryType::Int;
        value->words.push_back(word(from, from.IsFloat()));
    } else if (from.IsArray()) {
        bool strings = false, numbers = false, floats = false;
        for (int i = 0; i < from.Size(); ++i) {
            const toml::Value element = from.At(i);
            strings = strings || element.IsString();
            numbers = numbers || element.IsInt() || element.IsFloat();
            floats = floats || element.IsFloat();
            if (!element.IsString() && !element.IsInt() && !element.IsFloat()) return false;
        }
        if (strings && numbers) return false;
        value->type = strings ? RegistryType::StringArray : floats ? RegistryType::FloatArray : RegistryType::IntArray;
        value->anyArray = from.Size() == 0;
        for (int i = 0; i < from.Size(); ++i) {
            if (strings) value->strings.emplace_back(from.At(i).String());
            else value->words.push_back(word(from.At(i), floats));
        }
    } else {
        return false;
    }
    return true;
}

// The keys that hold 0 or 1 as a switch, written as false / true (`section`, `key`): a mod's config.reg's [MOD]
// flags, for the starter's list (Single and Multi: mods::ModEntry's label; GameSaves and ForceShowTutorial: read by
// nothing of ours, docs/notes/mods.md "A mod's config.reg"). Any registry value read from TOML takes true or false as
// the integer 1 or 0.
constexpr std::pair<std::string_view, std::string_view> kSwitches[] = {
    {"MOD", "Single"}, {"MOD", "Multi"}, {"MOD", "GameSaves"}, {"MOD", "ForceShowTutorial"}};

bool IsSwitch(std::string_view section, const RegistryValue& value) {
    if (value.type != RegistryType::Int || value.words.size() != 1 || value.words.front() > 1) return false;
    for (const auto& [switchSection, key] : kSwitches)
        if (text::CompareIgnoringCase(section, switchSection) == 0 && text::CompareIgnoringCase(value.name, key) == 0)
            return true;
    return false;
}

// The value's TOML text.
std::string ToToml(const RegistryValue& value) {
    std::vector<std::string> items;
    const RegistryType element = value.type & ~RegistryType::Array;
    if (element == RegistryType::String) {
        for (const std::string& s : value.strings) items.push_back(toml::String(s));
    } else {
        for (const std::uint32_t w : value.words)
            items.push_back(element == RegistryType::Float ? toml::Float(std::bit_cast<float>(w))
                                                           : toml::Int(static_cast<std::int32_t>(w)));
    }
    if (lib::Has(value.type, RegistryType::Array)) return toml::Array(items);
    return items.empty() ? std::string() : items.front();
}

} // namespace

bool FileRegistry::Open(const std::string& path) {
    const RecordFile file = FindRecordFile(path);
    if (!file.Any()) {
        game::DebugLog("CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
        return false;
    }
    return Load(file);
}

bool FileRegistry::Attach(const void* image, std::size_t size) {
    Close();
    ImageReader reader(image, size);
    if (ReadImage(reader, &sections)) return true;
    game::DebugLog(kNotRegistry);
    Close();
    return false;
}

bool FileRegistry::Load(const RecordFile& file) {
    Close();
    if (!file.Any()) return false;
    if (file.found && !Attach(file.original.data(), file.original.size())) return false;
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    return true;
}

void FileRegistry::Close() { sections.clear(); }

const RegistryValue* FileRegistry::Find(std::string_view sectionName, std::string_view key, RegistryType type, bool* isScalar) {
    RegistrySection* section = Named(sections, sectionName);
    if (!section) return nullptr;
    const RegistryValue* value = Named(section->values, key);
    if (!value) return nullptr;

    if (isScalar) *isScalar = !lib::Has(type, RegistryType::Array);
    if (value->type != type && !(value->anyArray && lib::Has(type, RegistryType::Array))) {
        if (value->type != (type & ~RegistryType::Array)) {
            game::DebugLog(kWrongType);
            return nullptr;
        }
        if (isScalar) *isScalar = true;
    }
    return value;
}

std::vector<std::string> FileRegistry::SectionNames() {
    std::vector<std::string> names;
    for (const RegistrySection& section : sections) names.push_back(section.name);
    return names;
}

int FileRegistry::GetInt(std::string_view section, std::string_view key, int defaultValue) {
    if (const RegistryValue* value = Find(section, key, RegistryType::Int, nullptr))
        return static_cast<int>(value->words.front());
    if (const RegistryValue* value = Find(section, key, RegistryType::Float, nullptr))
        return lib::Truncate(std::bit_cast<float>(value->words.front()));
    return defaultValue;
}

double FileRegistry::GetFloat(std::string_view section, std::string_view key, float defaultValue) {
    if (const RegistryValue* value = Find(section, key, RegistryType::Float, nullptr))
        return std::bit_cast<float>(value->words.front());
    if (const RegistryValue* value = Find(section, key, RegistryType::Int, nullptr))
        return static_cast<int>(value->words.front());
    return defaultValue;
}

std::string FileRegistry::GetString(std::string_view section, std::string_view key, const std::string& defaultValue) {
    if (const RegistryValue* value = Find(section, key, RegistryType::String, nullptr)) return value->strings.front();
    return defaultValue;
}

void FileRegistry::GetStringBuffer(std::string_view section, std::string_view key, char* buffer, int* length,
                                    const char* defaultValue) {
    if (const RegistryValue* value = Find(section, key, RegistryType::String, nullptr)) {
        // The UTF-8 text is cut to the buffer at a character's end (the original cut the asset's bytes, by their
        // 16-bit length).
        const std::string& string = value->strings.front();
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
    const RegistryValue* value = Find(section, key, RegistryType::StringArray, nullptr);
    if (!value) {
        out->clear();
        return false;
    }
    *out = value->strings;
    return true;
}

void FileRegistry::MergeToml(const toml::Value& root, const std::string& path) {
    for (int i = 0; i < root.Size(); ++i) {
        const std::string_view sectionName = root.KeyAt(i);
        const toml::Value table = root.ValueAt(i);
        if (IsReplaceKey(sectionName, table)) continue;
        if (!table.IsTable()) {
            game::Warning("registry: %s line %d: %s is not a section", path, table.Line(), std::string(sectionName));
            continue;
        }
        RegistrySection* section = Named(sections, sectionName);
        if (!section) {
            section = &sections.emplace_back();
            section->name = sectionName;
        }
        for (int k = 0; k < table.Size(); ++k) {
            RegistryValue value;
            value.name = table.KeyAt(k);
            if (!FromToml(table.ValueAt(k), &value)) {
                game::Warning("registry: %s line %d: %s is not a number, a string or an array of one", path,
                              table.ValueAt(k).Line(), value.name);
                continue;
            }
            if (RegistryValue* old = Named(section->values, value.name)) *old = std::move(value);
            else section->values.push_back(std::move(value));
        }
    }
}

std::string FileRegistry::ToToml() const {
    toml::Writer writer;
    for (const RegistrySection& section : sections) {
        writer.Table(toml::Key(section.name));
        for (const RegistryValue& value : section.values)
            writer.Pair(value.name, IsSwitch(section.name, value) ? toml::Bool(value.words.front() != 0) : ::ToToml(value));
    }
    return writer.text;
}
