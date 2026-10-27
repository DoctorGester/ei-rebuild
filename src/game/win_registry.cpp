// CWinRegistry (006972A0-006988A0). See win_registry.h.
#include "win_registry.h"

#include "settings_file.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "ei/hook.h"

#include <cstring>
#include <new>
#include <string>

namespace {

// Writes one value of `subkey`: 1 (the settings file always takes it).
BOOL SetValue(void* root, const char* subkey, const char* name, DWORD type, const void* data, DWORD size) {
    return settings_file::Set(root, subkey, name, type, data, size) == 0;
}

// Reads a 4-byte value of `subkey`: false when the subkey or the value cannot be read; `type`
// receives the value's type.
bool QueryValue(void* root, const char* subkey, const char* name, DWORD* type, void* data) {
    DWORD size = 4;
    return settings_file::Query(root, subkey, name, type, data, &size) == 0;
}

// The REG_DWORD readers (00698570 / 00698610 are the same code).
std::uint32_t GetDWordValue(void* root, const char* subkey, const char* name, std::uint32_t defaultValue) {
    DWORD type;
    std::uint32_t value;   // left as it was when the value is shorter (not reproduced: the original's is uninitialised)
    if (!QueryValue(root, subkey, name, &type, &value)) return defaultValue;
    if (type != REG_DWORD) {
        game::DebugLog("CWinRegistry::GetDWordInt().\nRegistry data type is not DWORD.\n");
        return defaultValue;
    }
    return value;
}

// The REG_DWORD writers (00697CA0 / 00697D40 are the same code).
BOOL SetDWordValue(void* root, const char* subkey, const char* name, std::uint32_t value) {
    return SetValue(root, subkey, name, REG_DWORD, &value, 4);
}

} // namespace

CWinRegistry* CWinRegistry::Construct() {
    key_ = nullptr;
    return this;
}
EI_REWRITTEN(0x006972A0, CWinRegistry::Construct);

void CWinRegistry::Destroy() {
    Close();
}
EI_REWRITTEN(0x006972B0, CWinRegistry::Destroy);

BOOL CWinRegistry::Open(const char* path, std::uint32_t /*root: HKEY_CURRENT_USER for every caller*/) {
    Close();
    key_ = settings_file::CreateKey(path);
    return 1;
}
EI_REWRITTEN(0x00697340, CWinRegistry::Open);

void CWinRegistry::Close() {
    if (!key_) return;
    settings_file::Flush();   // what the key's writes changed
    key_ = nullptr;
}
EI_REWRITTEN(0x006973A0, CWinRegistry::Close);

BOOL CWinRegistry::SetDWord(const char* subkey, const char* name, std::uint32_t value) {
    return SetDWordValue(key_, subkey, name, value);
}
EI_REWRITTEN(0x00697CA0, CWinRegistry::SetDWord);

BOOL CWinRegistry::SetInt(const char* subkey, const char* name, int value) {
    return SetDWordValue(key_, subkey, name, static_cast<std::uint32_t>(value));
}
EI_REWRITTEN(0x00697D40, CWinRegistry::SetInt);

BOOL CWinRegistry::SetFloat(const char* subkey, const char* name, std::uint32_t bits) {
    return SetValue(key_, subkey, name, REG_BINARY, &bits, 4);
}
EI_REWRITTEN(0x00697E80, CWinRegistry::SetFloat);

BOOL CWinRegistry::SetStringInt(const char* subkey, const char* name, mfc::CString value) {
    return SetValue(key_, subkey, name, REG_SZ, value.GetString(), static_cast<DWORD>(value.GetLength() + 1));
}
EI_REWRITTEN(0x00697460, CWinRegistry::SetStringInt);

BOOL CWinRegistry::SetString(const char* subkey, const char* name, mfc::CString value) {
    return SetStringInt(subkey, name, value);
}
EI_REWRITTEN(0x00697FC0, CWinRegistry::SetString);

std::uint32_t CWinRegistry::GetDWord(const char* subkey, const char* name, std::uint32_t defaultValue) {
    return GetDWordValue(key_, subkey, name, defaultValue);
}
EI_REWRITTEN(0x00698570, CWinRegistry::GetDWord);

int CWinRegistry::GetInt(const char* subkey, const char* name, int defaultValue) {
    return static_cast<int>(GetDWordValue(key_, subkey, name, static_cast<std::uint32_t>(defaultValue)));
}
EI_REWRITTEN(0x00698610, CWinRegistry::GetInt);

double CWinRegistry::GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits) {
    // The value is read into the stack slot of `subkey`, so a shorter value keeps the pointer's
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
    std::memcpy(&value, &bits, 4);
    return value;
}
EI_REWRITTEN(0x00698750, CWinRegistry::GetFloat);

mfc::CString CWinRegistry::GetStringInt(const char* subkey, const char* name, mfc::CString defaultValue) {
    char buffer[0x100];
    DWORD size = sizeof(buffer);
    DWORD type;
    char* longValue = nullptr;
    LONG status = settings_file::Query(key_, subkey, name, &type, buffer, &size);
    if (status == ERROR_MORE_DATA) {
        longValue = static_cast<char*>(gamecrt::malloc(size));
        status = settings_file::Query(key_, subkey, name, &type, longValue, &size);
    }
    if (status == 0 && type != REG_SZ) game::DebugLog("CWinRegistry::GetStringInt().\nRegistry data type is not SZ.\n");
    if (status != 0 || type != REG_SZ) {
        if (longValue) gamecrt::free(longValue);
        return defaultValue;
    }
    std::string text;
    if (longValue) {
        text = longValue;
        gamecrt::free(longValue);
    } else {
        text = buffer;
    }
    return text;
}
EI_REWRITTEN(0x006976B0, CWinRegistry::GetStringInt);

mfc::CString CWinRegistry::GetString(const char* subkey, const char* name, mfc::CString defaultValue) {
    return GetStringInt(subkey, name, defaultValue);
}
EI_REWRITTEN(0x006988A0, CWinRegistry::GetString);
