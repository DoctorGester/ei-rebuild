#pragma once

// CIniFile (provisional name): the game's .ini settings reader and
// writer over GetPrivateProfile* / WritePrivateProfileString. The object is just the file path.
// Lists are comma-separated values ("1, 2, 3"), each item trimmed.

#include <cstdint>

#include "../lib/mfc.h"
#include "registry.h"

namespace console {
class CStringArray;
}

class CIniFile {
public:
    CIniFile* Construct();                                  // empty path
    CIniFile* ConstructWithPath(const char* path);
    BOOL SetPath(const char* path);                          // returns 1
    void Destruct();                                         // the path's ~CString

    // The key names of `section` (the part before '=', trimmed) into `out` (emptied
    // first). Returns 0 for an empty or unreadable section.
    BOOL GetKeyNames(const char* section, console::CStringArray* out);

    unsigned GetInt(const char* section, const char* key, int defaultValue);
    unsigned GetInt2(const char* section, const char* key, int defaultValue);
    double GetFloat(const char* section, const char* key, float defaultValue);
    // Atof of the value, the default written with "%g"; rounded to double.
    double GetDouble(const char* section, const char* key, double defaultValue);
    // The value (at most 1023 characters), or `defaultValue`.
    mfc::CString GetString(const char* section, const char* key, const char* defaultValue);
    // Appends the items of the value split at `separator` (trimmed) to `out`. Returns 0
    // when the value is empty.
    BOOL GetList(const char* section, const char* key, mfc::CStringArray* out, char separator);

    // Lists of integers (atoi of each item) into the different array types. Each returns 0, `out`
    // untouched, when the value is empty; otherwise `out` holds exactly the items.
    BOOL GetIntArrayCopy(const char* section, const char* key, DwordArray* out);
    BOOL GetByteArray(const char* section, const char* key, lib::AlignedArray<std::uint8_t>* out);
    BOOL GetShortArray(const char* section, const char* key, lib::AlignedArray<std::uint16_t>* out);
    BOOL GetIntArray(const char* section, const char* key, DwordArray* out);
    BOOL GetMfcByteArray(const char* section, const char* key, mfc::CArray<std::uint8_t>* out);
    BOOL GetMfcWordArray(const char* section, const char* key, mfc::CArray<std::uint16_t>* out);
    BOOL GetMfcUIntArray(const char* section, const char* key, mfc::CArray<std::uint32_t>* out);
    BOOL GetMfcDWordArray(const char* section, const char* key, mfc::CArray<std::uint32_t>* out);

    BOOL WriteString(const char* section, const char* key, mfc::CString value);
    BOOL WriteIntForward(const char* section, const char* key, int value);             // -> WriteInt
    BOOL WriteInt(const char* section, const char* key, int value);                    // "%d"
    BOOL WriteUInt(const char* section, const char* key, unsigned value);              // "%u"
    BOOL WriteFloat(const char* section, const char* key, float value);                // "%f"
    BOOL WriteDouble(const char* section, const char* key, double value);              // "%lf"
    // The items joined with `separator`; 0 (nothing written) for an empty list.
    BOOL WriteList(const char* section, const char* key, const console::CStringArray* items, char separator);

    mfc::CString path_;
};
static_assert(sizeof(CIniFile) == 4);
