#pragma once

// The game's binary registry files (*.reg: quests, item databases, settings), read through FileRegistry.
// More: docs/notes/items_spells.md, "Registry files".

#include <cstdint>
#include <string>
#include <vector>

#include "lib/bit_flags.h"
#include "game/resource.h"

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

class FileRegistry {
public:
    // Maps the file at `path`. Returns 0 if it cannot be opened or is not a registry.
    bool Open(const std::string& path);
    // Uses a registry image already in memory. Returns 0 if it is not a registry.
    bool Attach(const void* image);
    // Closes the mapped file (when it owns one) and forgets the data.
    void Close();

    // The section record (key null) or the value of `key` in `section`, or null. With
    // `isScalar`, an array type also accepts a single value of the element type (*isScalar is true
    // then; false for an array; for a scalar type it is true). A wrong type logs and gives null.
    const std::uint8_t* FindData(const char* section, const char* key, RegistryType type, bool* isScalar);

    // The names of all sections (in table order).
    std::vector<std::string> SectionNames();

    // An int value, a float one truncated (_ftol), or `defaultValue`.
    int GetInt(const char* section, const char* key, int defaultValue);
    // A float value, an int one (exact), or `defaultValue`, unrounded (declared
    // double so an int value is not rounded to float on return).
    double GetFloat(const char* section, const char* key, float defaultValue);
    // A string value, or `defaultValue`.
    std::string GetString(const char* section, const char* key, const std::string& defaultValue);
    // A string value into `buffer`, cut to *length - 1 characters, or `defaultValue`
    // (strncpy, so not terminated when too long); *length becomes the copied length.
    void GetStringBuffer(const char* section, const char* key, char* buffer, int* length, const char* defaultValue);

    // Int / float array (or a single value) into `out`. Without the key `out` is emptied and the result is 0.
    bool GetIntArray(const char* section, const char* key, std::vector<std::int32_t>* out);
    bool GetFloatArray(const char* section, const char* key, std::vector<float>* out);
    // A string array (or a single string) into `out`. Without the key `out` is emptied and the result is 0.
    bool GetStringArray(const char* section, const char* key, std::vector<std::string>* out);

    bool attached = false;        // true: the data is not ours (Attach)
    platform::MappedFile file;
    const std::uint8_t* data = nullptr;
};

constexpr std::uint32_t kRegistryMagic = 0x45AB3EFB;
