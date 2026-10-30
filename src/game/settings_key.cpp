// SettingsKey. See settings_key.h.
#include "settings_key.h"

#include "settings_file.h"

#include "lib/crt.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

// Writes one value of `subkey`: 1 (the settings file always takes it).
bool SetValue(void* root, const char* subkey, const char* name, std::uint32_t type, const void* data, std::uint32_t size) {
    return settings_file::Set(root, subkey, name, type, data, size) == 0;
}

// Reads a 4-byte value of `subkey`: false when the subkey or the value cannot be read; `type`
// receives the value's type.
bool QueryValue(void* root, const char* subkey, const char* name, std::uint32_t* type, void* data) {
    std::uint32_t size = 4;
    return settings_file::Query(root, subkey, name, type, data, &size) == 0;
}

} // namespace

bool SettingsKey::Open(const char* path) {
    Close();
    key = settings_file::CreateKey(path);
    return true;
}

void SettingsKey::Close() {
    if (!key) return;
    settings_file::Flush();   // what the key's writes changed
    key = nullptr;
}

bool SettingsKey::SetInt(const char* subkey, const char* name, int value) {
    return SetValue(key, subkey, name, settings_file::kTypeDword, &value, 4);
}

bool SettingsKey::SetFloat(const char* subkey, const char* name, std::uint32_t bits) {
    return SetValue(key, subkey, name, settings_file::kTypeBinary, &bits, 4);
}

bool SettingsKey::SetString(const char* subkey, const char* name, const std::string& value) {
    return SetValue(key, subkey, name, settings_file::kTypeString, value.c_str(), static_cast<std::uint32_t>(static_cast<int>(value.size()) + 1));
}

int SettingsKey::GetInt(const char* subkey, const char* name, int defaultValue) {
    std::uint32_t type;
    int value;   // left as it was when the value is shorter (not reproduced: the game's is uninitialised)
    if (!QueryValue(key, subkey, name, &type, &value)) return defaultValue;
    if (type != settings_file::kTypeDword) {
        game::DebugLog("CWinRegistry::GetDWordInt().\nRegistry data type is not unsigned long.\n");
        return defaultValue;
    }
    return value;
}

double SettingsKey::GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits) {
    // The value is read into the storage of `subkey`, so a shorter value keeps the pointer's
    // remaining bytes.
    std::uint32_t bits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(subkey));
    std::uint32_t type;
    if (!QueryValue(key, subkey, name, &type, &bits)) {
        bits = defaultBits;
    } else if (type != settings_file::kTypeBinary) {
        game::DebugLog("CWinRegistry::GetBinInt().\nRegistry data type is not BINARY.\n");
        bits = defaultBits;
    }
    float value;
    value = std::bit_cast<float>(bits);
    return value;
}

std::string SettingsKey::GetString(const char* subkey, const char* name, const std::string& defaultValue) {
    char buffer[0x100];
    std::uint32_t size = sizeof(buffer);
    std::uint32_t type;
    std::vector<char> longValue;
    long status = settings_file::Query(key, subkey, name, &type, buffer, &size);
    if (status == settings_file::kMoreData) {
        longValue.resize(size);
        status = settings_file::Query(key, subkey, name, &type, longValue.data(), &size);
    }
    if (status == 0 && type != settings_file::kTypeString) game::DebugLog("CWinRegistry::GetStringInt().\nRegistry data type is not SZ.\n");
    if (status != 0 || type != settings_file::kTypeString) return defaultValue;
    return longValue.empty() ? std::string(buffer) : std::string(longValue.data());
}
