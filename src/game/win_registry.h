#pragma once

// CWinRegistry (the name from its own messages): the Windows registry
// wrapper the options loader and writer use. The object is one open key; every
// getter and setter reads or writes one value of `subkey` below it. The keys and values are the settings
// file's (settings_file.h), never the registry's: key_ is a settings_file::CreateKey handle, and Close
// writes the file when a value changed. A value of another type goes to the game's debug message
// and gives the default.

#include <cstdint>
#include <string>

class CWinRegistry {
public:
    CWinRegistry() = default;
    ~CWinRegistry() { Close(); }
    CWinRegistry(const CWinRegistry&) = delete;
    CWinRegistry& operator=(const CWinRegistry&) = delete;
    // Closes the key, then opens the settings file's `path` (`root`, an HKEY, is
    // HKEY_CURRENT_USER for every caller). Returns 1.
    bool Open(const char* path, std::uint32_t root);
    void Close();

    // The writers: 1 (the settings file takes every value).
    bool SetDWord(const char* subkey, const char* name, std::uint32_t value);    // REG_DWORD
    bool SetInt(const char* subkey, const char* name, int value);                // same code
    // The float's bits as a 4-byte REG_BINARY (bits passed as the game pushes them).
    bool SetFloat(const char* subkey, const char* name, std::uint32_t bits);
    bool SetString(const char* subkey, const char* name, const std::string& value);   // REG_SZ

    // The readers: the value, or `defaultValue` when the subkey or value is missing or of
    // another type (the last also logged).
    std::uint32_t GetDWord(const char* subkey, const char* name, std::uint32_t defaultValue);
    int GetInt(const char* subkey, const char* name, int defaultValue);
    // A 4-byte REG_BINARY returned as a float; the default passed as bits, as the game pushes it.
    double GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits);
    std::string GetString(const char* subkey, const char* name, const std::string& defaultValue);   // REG_SZ

    void* key_ = nullptr;   // the settings file's key, null when closed
};
