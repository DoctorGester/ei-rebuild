#pragma once

// CWinRegistry (game.exe 006972A0-006988A0; the name from its own messages): the Windows registry
// wrapper the options loader (0045B210) and writer (0045C560) use. The object is one open key; every
// getter and setter opens `subkey` below it, reads or writes one value and closes it again.
// Failures go to the game's debug message (0044C470) and give the default / 0.

#include <cstdint>

#include "../lib/mfc.h"

class CWinRegistry {
public:
    CWinRegistry* Construct();                                    // 006972A0 (returns this)
    void Destroy();                                               // 006972B0 (closes the key)
    // 00697340. Closes the key, then creates / opens `path` below `root` (an HKEY) with full
    // access. Returns 1 on success; on failure the key stays null.
    mfc::BOOL Open(const char* path, std::uint32_t root);
    void Close();                                                 // 006973A0

    // The writers: 1 on success, 0 when the subkey cannot be created or the value not written.
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

    void* key_;    // +0x00 HKEY, null when closed
};
static_assert(sizeof(CWinRegistry) == 4);
