#pragma once

// The game's registry files (*.reg: quests, item databases, settings), read through FileRegistry: the binary format
// or its TOML (docs/notes/mods.md, "Records as TOML"). More: docs/notes/items_spells.md, "Registry files".

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lib/bit_flags.h"
#include "engine/data/resource.h"

namespace toml {
class Value;
}

enum class RegistryType : std::uint8_t {
    Int = 0,
    Float = 1,
    String = 2,
    Array = 0x80,       // with one of the above
    IntArray = Array | Int,
    FloatArray = Array | Float,
    StringArray = Array | String,
};
constexpr bool EnableBitFlags(RegistryType) { return true; }

// A key's value: Int and Float values as their bits in `words`, String values (UTF-8) in `strings`; one for a
// scalar type, an array's elements for an array type.
struct RegistryValue {
    std::string name;
    RegistryType type = RegistryType::Int;
    // An empty array read from TOML, which names no element type: any array type is asked of it.
    bool anyArray = false;
    std::vector<std::uint32_t> words;
    std::vector<std::string> strings;
};

struct RegistrySection {
    std::string name;
    std::vector<RegistryValue> values;
};

class FileRegistry {
public:
    // Reads the registry at `path` (FindRecordFile: the file and its TOML files over it). Returns 0 when there is
    // neither, or the file is not a registry.
    bool Open(const std::string& path);
    // Reads the registry image of `size` bytes in memory. Returns 0 if it is not a registry.
    bool Attach(const void* image, std::size_t size);
    // Reads `file`: the original's image, then each TOML file over it (bottom first). Returns 0 when there is
    // neither, or the original is not a registry.
    bool Load(const RecordFile& file);
    // Forgets the sections.
    void Close();

    // The value of `key` in `section`, or null (names compared without case; the first of a name). With
    // `isScalar`, an array type also accepts a single value of the element type (*isScalar is true
    // then; false for an array; for a scalar type it is true). A wrong type logs and gives null.
    const RegistryValue* Find(std::string_view section, std::string_view key, RegistryType type, bool* isScalar);

    // The names of all sections (in the file's order).
    std::vector<std::string> SectionNames();

    // An int value, a float one truncated (_ftol), or `defaultValue`.
    int GetInt(std::string_view section, std::string_view key, int defaultValue);
    // A float value, an int one (exact), or `defaultValue`, unrounded (declared
    // double so an int value is not rounded to float on return).
    double GetFloat(std::string_view section, std::string_view key, float defaultValue);
    // A string value, or `defaultValue`.
    std::string GetString(std::string_view section, std::string_view key, const std::string& defaultValue);
    // A string value into `buffer`, cut to *length - 1 characters, or `defaultValue`
    // (strncpy, so not terminated when too long); *length becomes the copied length.
    void GetStringBuffer(std::string_view section, std::string_view key, char* buffer, int* length, const char* defaultValue);

    // Int / float array (or a single value) into `out`. Without the key `out` is emptied and the result is 0.
    bool GetIntArray(std::string_view section, std::string_view key, std::vector<std::int32_t>* out);
    bool GetFloatArray(std::string_view section, std::string_view key, std::vector<float>* out);
    // A string array (or a single string) into `out`. Without the key `out` is emptied and the result is 0.
    bool GetStringArray(std::string_view section, std::string_view key, std::vector<std::string>* out);

    // TOML (docs/notes/mods.md, "Records as TOML"): a table per section, a key per value (an integer, a float, a
    // string or an array of one of them). Merge: each section and key of `root` over the ones of its name (or
    // appended), a value that is none of those logged (with `path`) and left out. ToToml: the sections as such.
    void MergeToml(const toml::Value& root, const std::string& path);
    std::string ToToml() const;

    std::vector<RegistrySection> sections;
};

constexpr std::uint32_t kRegistryMagic = 0x45AB3EFB;
