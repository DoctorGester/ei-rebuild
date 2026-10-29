#pragma once

// The game's binary registry files (*.reg: quests, item databases, settings), read through CFileRegistry.
// More: docs/notes/comments-c22i.md, "registry.h".

#include <cstdint>
#include <string>
#include <vector>

#include "resource.h"

enum RegistryType : std::uint8_t {
    kRegInt = 0,
    kRegFloat = 1,
    kRegString = 2,
    kRegArray = 0x80,       // with one of the above
};

class CFileRegistry {
public:
    // The constructor: nothing open.
    CFileRegistry* Construct();
    // The destructor body: closes the file (when it owns one).
    void Destroy();
    // Maps the file at `path`. Returns 0 if it cannot be opened or is not a registry.
    bool Open(const std::string& path);
    // Uses a registry image already in memory. Returns 0 if it is not a registry.
    bool Attach(const void* image);
    // Closes the mapped file (when it owns one) and forgets the data.
    void Close();

    // The section record (key null) or the value of `key` in `section`, or null. With
    // `isScalar`, an array type also accepts a single value of the element type (*isScalar is 1
    // then; 0 for an array; for a scalar type it is 1). A wrong type logs and gives null.
    const std::uint8_t* FindData(const char* section, const char* key, unsigned type, int* isScalar);

    // The names of all sections into `out` (in table order).
    void GetSectionNames(std::vector<std::string>* out);

    // Identical copies. An int value, a float one truncated (_ftol), or `defaultValue`.
    int GetInt(const char* section, const char* key, int defaultValue);
    int GetInt2(const char* section, const char* key, int defaultValue);
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
    bool GetFloatArray(const char* section, const char* key, std::vector<std::uint32_t>* out);
    // A string array (or a single string) into `out`. Without the key `out` is emptied and the result is 0.
    bool GetStringArray(const char* section, const char* key, std::vector<std::string>* out);

    bool attached_;               // 1: the data is not ours (Attach)
    CMappedFile file_;
    const std::uint8_t* data_;
};

constexpr std::uint32_t kRegistryMagic = 0x45AB3EFB;
