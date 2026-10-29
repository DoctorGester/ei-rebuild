// CWinRegistry. See win_registry.h.
#include "win_registry.h"

#include "settings_file.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include <bit>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

// Writes one value of `subkey`: 1 (the settings file always takes it).
bool SetValue(void* root, const char* subkey, const char* name, DWORD type, const void* data, DWORD size) {
    return settings_file::Set(root, subkey, name, type, data, size) == 0;
}

// Reads a 4-byte value of `subkey`: false when the subkey or the value cannot be read; `type`
// receives the value's type.
bool QueryValue(void* root, const char* subkey, const char* name, DWORD* type, void* data) {
    DWORD size = 4;
    return settings_file::Query(root, subkey, name, type, data, &size) == 0;
}

// The REG_DWORD readers (two copies of the same code).
std::uint32_t GetDWordValue(void* root, const char* subkey, const char* name, std::uint32_t defaultValue) {
    DWORD type;
    std::uint32_t value;   // left as it was when the value is shorter (not reproduced: the game's is uninitialised)
    if (!QueryValue(root, subkey, name, &type, &value)) return defaultValue;
    if (type != REG_DWORD) {
        game::DebugLog("CWinRegistry::GetDWordInt().\nRegistry data type is not DWORD.\n");
        return defaultValue;
    }
    return value;
}

// The REG_DWORD writers (two copies of the same code).
bool SetDWordValue(void* root, const char* subkey, const char* name, std::uint32_t value) {
    return SetValue(root, subkey, name, REG_DWORD, &value, 4);
}

} // namespace

bool CWinRegistry::Open(const char* path, std::uint32_t /*root: HKEY_CURRENT_USER for every caller*/) {
    Close();
    key_ = settings_file::CreateKey(path);
    return true;
}

void CWinRegistry::Close() {
    if (!key_) return;
    settings_file::Flush();   // what the key's writes changed
    key_ = nullptr;
}

bool CWinRegistry::SetDWord(const char* subkey, const char* name, std::uint32_t value) {
    return SetDWordValue(key_, subkey, name, value);
}

bool CWinRegistry::SetInt(const char* subkey, const char* name, int value) {
    return SetDWordValue(key_, subkey, name, static_cast<std::uint32_t>(value));
}

bool CWinRegistry::SetFloat(const char* subkey, const char* name, std::uint32_t bits) {
    return SetValue(key_, subkey, name, REG_BINARY, &bits, 4);
}

bool CWinRegistry::SetString(const char* subkey, const char* name, const std::string& value) {
    return SetValue(key_, subkey, name, REG_SZ, value.c_str(), static_cast<DWORD>(static_cast<int>(value.size()) + 1));
}

std::uint32_t CWinRegistry::GetDWord(const char* subkey, const char* name, std::uint32_t defaultValue) {
    return GetDWordValue(key_, subkey, name, defaultValue);
}

int CWinRegistry::GetInt(const char* subkey, const char* name, int defaultValue) {
    return static_cast<int>(GetDWordValue(key_, subkey, name, static_cast<std::uint32_t>(defaultValue)));
}

double CWinRegistry::GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits) {
    // The value is read into the storage of `subkey`, so a shorter value keeps the pointer's
    // remaining bytes.
    std::uint32_t bits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(subkey));
    DWORD type;
    if (!QueryValue(key_, subkey, name, &type, &bits)) {
        bits = defaultBits;
    } else if (type != REG_BINARY) {
        game::DebugLog("CWinRegistry::GetBinInt().\nRegistry data type is not BINARY.\n");
        bits = defaultBits;
    }
    float value;
    value = std::bit_cast<float>(bits);
    return value;
}

std::string CWinRegistry::GetString(const char* subkey, const char* name, const std::string& defaultValue) {
    char buffer[0x100];
    DWORD size = sizeof(buffer);
    DWORD type;
    std::vector<char> longValue;
    LONG status = settings_file::Query(key_, subkey, name, &type, buffer, &size);
    if (status == ERROR_MORE_DATA) {
        longValue.resize(size);
        status = settings_file::Query(key_, subkey, name, &type, longValue.data(), &size);
    }
    if (status == 0 && type != REG_SZ) game::DebugLog("CWinRegistry::GetStringInt().\nRegistry data type is not SZ.\n");
    if (status != 0 || type != REG_SZ) return defaultValue;
    return longValue.empty() ? std::string(buffer) : std::string(longValue.data());
}
