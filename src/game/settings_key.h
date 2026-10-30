#pragma once

// SettingsKey (the game's CWinRegistry, its Windows registry wrapper): one open key of the settings file
// (settings_file.h) that the options loader and writer use; every getter and setter reads or writes one value of
// `subkey` below it. Nothing reads or writes the registry: key is a settings_file::CreateKey handle, and Close
// writes the file when a value changed. A value of another type goes to the game's debug message (CWinRegistry's
// texts) and gives the default.

#include <cstdint>
#include <string>

class SettingsKey {
public:
    SettingsKey() = default;
    ~SettingsKey() { Close(); }
    SettingsKey(const SettingsKey&) = delete;
    SettingsKey& operator=(const SettingsKey&) = delete;
    // Closes the key, then opens the settings file's `path` (the original's HKEY_CURRENT_USER key). Returns 1.
    bool Open(const char* path);
    void Close();

    // The writers: 1 (the settings file takes every value).
    bool SetInt(const char* subkey, const char* name, int value);   // kTypeDword
    // The float's bits as a 4-byte kTypeBinary (bits passed as the game pushes them).
    bool SetFloat(const char* subkey, const char* name, std::uint32_t bits);
    bool SetString(const char* subkey, const char* name, const std::string& value);   // kTypeString

    // The readers: the value, or `defaultValue` when the subkey or value is missing or of
    // another type (the last also logged).
    int GetInt(const char* subkey, const char* name, int defaultValue);   // kTypeDword
    // A 4-byte kTypeBinary returned as a float; the default passed as bits, as the game pushes it.
    double GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits);
    std::string GetString(const char* subkey, const char* name, const std::string& defaultValue);   // kTypeString

    void* key = nullptr;    // the settings file's key, null when closed
};
