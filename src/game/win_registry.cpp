// CWinRegistry (006972A0-006988A0). See win_registry.h.
#include "win_registry.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "ei/hook.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders

namespace {

constexpr REGSAM kAllAccess = 0xF003F;   // KEY_ALL_ACCESS
char kNoClass[] = "";                    // the key class RegCreateKeyExA gets

HKEY Handle(void* key) { return static_cast<HKEY>(key); }

// Creates (or opens) `subkey` below `root` for writing; logs `failure` when it cannot.
bool CreateKey(void* root, const char* subkey, HKEY* key, const char* failure) {
    DWORD disposition;
    if (gameapi::RegCreateKeyExA(Handle(root), subkey, 0, kNoClass, 0, kAllAccess, nullptr, key, &disposition) != 0) {
        game::DebugLog(failure);
        return false;
    }
    return true;
}

// Writes one value into an open key and closes it; logs `failure` when the value cannot be set.
BOOL SetValue(HKEY key, const char* name, DWORD type, const void* data, DWORD size, const char* failure) {
    if (gameapi::RegSetValueExA(key, name, 0, type, static_cast<const BYTE*>(data), size) != 0) {
        game::DebugLog(failure);
        gameapi::RegCloseKey(key);
        return 0;
    }
    gameapi::RegCloseKey(key);
    return 1;
}

// Reads a 4-byte value of `subkey`: false when the subkey or the value cannot be read; `type`
// receives the value's type.
bool QueryValue(void* root, const char* subkey, const char* name, DWORD* type, void* data) {
    HKEY key;
    if (gameapi::RegOpenKeyExA(Handle(root), subkey, 0, kAllAccess, &key) != 0) return false;
    DWORD size = 4;
    LONG status = gameapi::RegQueryValueExA(key, name, nullptr, type, static_cast<BYTE*>(data), &size);
    gameapi::RegCloseKey(key);
    return status == 0;
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
    HKEY key;
    if (!CreateKey(root, subkey, &key, "CWinRegistry::SetDWordInt().\nUnable to open key.\n")) return 0;
    return SetValue(key, name, REG_DWORD, &value, 4, "CWinRegistry::SetDWordInt().\nUnable to access registry.\n");
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

BOOL CWinRegistry::Open(const char* path, std::uint32_t root) {
    Close();
    DWORD disposition;
    HKEY key;
    if (gameapi::RegCreateKeyExA(reinterpret_cast<HKEY>(static_cast<std::uintptr_t>(root)), path, 0, kNoClass, 0,
                                 kAllAccess, nullptr, &key, &disposition) != 0) {
        key_ = nullptr;
        return 0;
    }
    key_ = key;
    return 1;
}
EI_REWRITTEN(0x00697340, CWinRegistry::Open);

void CWinRegistry::Close() {
    if (key_) {
        gameapi::RegCloseKey(Handle(key_));
        key_ = nullptr;
    }
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
    HKEY key;
    if (!CreateKey(key_, subkey, &key, "CWinRegistry::SetBinInt().\nUnable to open key.\n")) return 0;
    return SetValue(key, name, REG_BINARY, &bits, 4, "CWinRegistry::SetBinInt().\nUnable to access registry.\n");
}
EI_REWRITTEN(0x00697E80, CWinRegistry::SetFloat);

BOOL CWinRegistry::SetStringInt(const char* subkey, const char* name, mfc::CString value) {
    HKEY key;
    if (!CreateKey(key_, subkey, &key, "CWinRegistry::SetStringInt().\nUnable to open key.\n")) return 0;
    return SetValue(key, name, REG_SZ, static_cast<const char*>(value), static_cast<DWORD>(value.GetLength() + 1),
                    "CWinRegistry::SetStringInt().\nUnable to access registry.\n");
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
    HKEY key;
    if (gameapi::RegOpenKeyExA(Handle(key_), subkey, 0, kAllAccess, &key) != 0) return defaultValue;
    char buffer[0x100];
    DWORD size = sizeof(buffer);
    DWORD type;
    char* longValue = nullptr;
    LONG status = gameapi::RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size);
    if (status == ERROR_MORE_DATA) {
        longValue = static_cast<char*>(gamecrt::malloc(size));
        status = gameapi::RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE*>(longValue), &size);
    }
    gameapi::RegCloseKey(key);
    if (status == 0 && type != REG_SZ) game::DebugLog("CWinRegistry::GetStringInt().\nRegistry data type is not SZ.\n");
    if (status != 0 || type != REG_SZ) {
        if (longValue) gamecrt::free(longValue);
        return defaultValue;
    }
    mfc::CString text;
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
