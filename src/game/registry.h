#pragma once

// The game's binary registry files (*.reg: quests, item databases, settings), read through
// CFileRegistry (game.exe 00694520-006959C0). Names come from the game's own messages
// ("CFileRegistry::FindData().\nWrong type of entry.").
//
// File layout (all offsets from the start of the file):
//   +0x00 u32 magic 0x45AB3EFB
//   +0x04 u16 section count = size of the section hash table
//   +0x06 the section table, 6 bytes per slot: u16 next slot in the chain (0xFFFF ends it),
//         u32 offset of the section
// A section: u16 key count (its hash table size), u16 name length, the name (no terminator),
// then its key table (6-byte slots like the section table, offsets from the section).
// A key: u8 type, u16 name length, the name, then the value. Types: 0 int, 1 float, 2 string
// (u16 length + characters), 0x80/0x81/0x82 arrays of them (u16 count first).
// A name's home slot is (sum of tolower(c) over its signed characters) % table size; names
// compare case-insensitively over their stored length.

#include <cstdint>

#include "../lib/containers.h"
#include "../lib/mfc.h"
#include "resource.h"
#include "string_arg.h"

namespace console {
class CStringArray;
}

// The int / float arrays the registry fills: engine aligned arrays of dwords (lib::AlignedArray;
// the inlined SetSize allocates through 00453A10 / 004D9ED0).
using DwordArray = lib::AlignedArray<std::uint32_t>;

enum RegistryType : std::uint8_t {
    kRegInt = 0,
    kRegFloat = 1,
    kRegString = 2,
    kRegArray = 0x80,       // with one of the above
};


class CFileRegistry {
public:
    // 00694520. The constructor: nothing open.
    CFileRegistry* Construct();
    // 00694540. The destructor body: closes the file (when it owns one).
    void Destroy();
    // 006945A0. Maps the file at `path`. Returns 0 if it cannot be opened or is not a registry.
    BOOL Open(const char* path);
    // 00694610. Uses a registry image already in memory. Returns 0 if it is not a registry.
    BOOL Attach(const void* image);
    // 00694660. Whether the data starts with the registry magic; closes it when not.
    BOOL IsValid();
    // 006946B0. Closes the mapped file (when it owns one) and forgets the data.
    void Close();

    // 006946D0. The section record (key null) or the value of `key` in `section`, or null. With
    // `isScalar`, an array type also accepts a single value of the element type (*isScalar is 1
    // then; 0 for an array; for a scalar type it is 1). A wrong type logs and gives null.
    const std::uint8_t* FindData(const char* section, const char* key, unsigned type, int* isScalar);

    // 00694880. The names of all sections into `out` (in table order). Returns 1.
    BOOL GetSectionNames(console::CStringArray* out);

    // 00694B90 / 00694BE0 (identical copies). An int value, a float one truncated (_ftol), or
    // `defaultValue`.
    int GetInt(const char* section, const char* key, int defaultValue);
    int GetInt2(const char* section, const char* key, int defaultValue);
    // 00694C80. A float value, an int one (exact, FILD), or `defaultValue`, in ST0 (declared
    // double so an int value is not rounded to float on return).
    double GetFloat(const char* section, const char* key, float defaultValue);
    // 00694D20. A string value, or `defaultValue`.
    mfc::CString GetString(const char* section, const char* key, mfc::CString defaultValue);
    // 00694DC0. A string value into `buffer`, cut to *length - 1 characters, or `defaultValue`
    // (strncpy, so not terminated when too long); *length becomes the copied length.
    void GetStringBuffer(const char* section, const char* key, char* buffer, int* length, const char* defaultValue);

    // 00695410 / 00695600. An int / float array (or a single value) into `out`. Without the key
    // `out` is emptied and the result is 0.
    BOOL GetIntArray(const char* section, const char* key, DwordArray* out);
    BOOL GetFloatArray(const char* section, const char* key, DwordArray* out);
    // 006958E0. A string array (or a single string) into `out`. Without the key `out` is emptied
    // and the result is 0.
    BOOL GetStringArray(const char* section, const char* key, console::CStringArray* out);

    BOOL attached_;               // +0x00 1: the data is not ours (Attach)
    CMappedFile file_;            // +0x04
    const std::uint8_t* data_;    // +0x14
};
static_assert(sizeof(CFileRegistry) == 0x18);

constexpr std::uint32_t kRegistryMagic = 0x45AB3EFB;
