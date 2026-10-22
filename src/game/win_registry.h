#pragma once

// CWinRegistry (game.exe 006972A0-006988A0; the name from its own messages): the Windows registry
// wrapper the options loader (0045B210) and writer (0045C560) use. The object is one open key; every
// getter and setter reads or writes one value of `subkey` below it. The keys and values are the settings
// file's (settings_file.h, ST1), never the registry's: key_ is a settings_file::CreateKey handle, and Close
// writes the file when a value changed. A value of another type goes to the game's debug message (0044C470)
// and gives the default.

#include <cstdint>

#include "../lib/mfc.h"

class CWinRegistry {
public:
    CWinRegistry* Construct();                                    // 006972A0 (returns this)
    void Destroy();                                               // 006972B0 (closes the key)
    // 00697340. Closes the key, then opens the settings file's `path` (`root`, an HKEY, is
    // HKEY_CURRENT_USER for every caller). Returns 1.
    mfc::BOOL Open(const char* path, std::uint32_t root);
    void Close();                                                 // 006973A0

    // The writers: 1 (the settings file takes every value).
    mfc::BOOL SetDWord(const char* subkey, const char* name, std::uint32_t value);    // 00697CA0 (REG_DWORD)
    mfc::BOOL SetInt(const char* subkey, const char* name, int value);                // 00697D40 (same code)
    // 00697E80. The float's bits as a 4-byte REG_BINARY (bits passed as the original pushes them).
    mfc::BOOL SetFloat(const char* subkey, const char* name, std::uint32_t bits);
    mfc::BOOL SetString(const char* subkey, const char* name, mfc::CString value);     // 00697FC0
    mfc::BOOL SetStringInt(const char* subkey, const char* name, mfc::CString value);  // 00697460 (REG_SZ)

    // The readers: the value, or `defaultValue` when the subkey or value is missing or of
    // another type (the last also logged).
    std::uint32_t GetDWord(const char* subkey, const char* name, std::uint32_t defaultValue);  // 00698570
    int GetInt(const char* subkey, const char* name, int defaultValue);                         // 00698610
    // 00698750. A 4-byte REG_BINARY returned in ST0 (FLD of the float); the default passed as
    // bits, as the original pushes it.
    double GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits);
    mfc::CString GetString(const char* subkey, const char* name, mfc::CString defaultValue);    // 006988A0
    mfc::CString GetStringInt(const char* subkey, const char* name, mfc::CString defaultValue); // 006976B0 (REG_SZ)

    void* key_;    // +0x00 the settings file's key, null when closed
};
static_assert(sizeof(CWinRegistry) == 4);
