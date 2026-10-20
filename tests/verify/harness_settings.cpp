// The game settings a harness run reads: the registry snapshot and the pinned install files. See
// harness_settings.h.
#include "harness_settings.h"

#include "harness_starter.h"

#include "../../src/hook/log.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace ei::verify::settings {
namespace {

using ei::Log;

struct Value {
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
    bool absent = false;   // "name"=-: read, absent on purpose
};

struct Key {
    bool absent = false;   // [-key]
    std::map<std::string, Value> values;   // by lower-case name
};

char g_folder[MAX_PATH];             // config\harness

// The keys answered here (lower case, below a root's name).
constexpr const char* kTrees[] = {"software\\nival interactive", "software\\gipat.ru"};
constexpr const char* kCurrentUser = "hkey_current_user";
constexpr const char* kLocalMachine = "hkey_local_machine";

// The module's state, made on first use: the harness's static constructor installs it before this file's
// globals would be constructed.
constexpr std::size_t kHandles = 1024;
struct State {
    std::map<std::string, Key> keys;   // by lower-case full path ("hkey_current_user\software\...")
    std::set<std::string> problemSet;
    std::vector<std::string> problems;
    std::vector<std::string> open = std::vector<std::string>(kHandles);   // a handle's key path
    std::vector<bool> used = std::vector<bool>(kHandles);
};
State& S() {
    static State state;
    return state;
}
SRWLOCK g_lock = SRWLOCK_INIT;

void Problem(const std::string& what) {
    AcquireSRWLockExclusive(&g_lock);
    const bool fresh = S().problemSet.insert(what).second;
    if (fresh) S().problems.push_back(what);
    ReleaseSRWLockExclusive(&g_lock);
    if (fresh) Log("harness: SETTINGS: %s", what.c_str());
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- the snapshot file (the .reg format reg export writes, in ASCII) ----

// The quoted string at `p` (escapes \\ and \"); `p` moves past its closing quote.
bool ParseQuoted(const char*& p, std::string* out) {
    if (*p != '"') return false;
    ++p;
    out->clear();
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        out->push_back(*p++);
    }
    if (*p != '"') return false;
    ++p;
    return true;
}

bool ParseHexBytes(const char* p, std::vector<BYTE>* out) {
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        unsigned byte;
        if (!std::isxdigit(static_cast<unsigned char>(p[0])) || !std::isxdigit(static_cast<unsigned char>(p[1])) ||
            sscanf_s(p, "%2x", &byte) != 1)
            return false;
        out->push_back(static_cast<BYTE>(byte));
        p += 2;
    }
    return true;
}

bool ParseValue(const char* p, Value* v) {
    if (std::strcmp(p, "-") == 0) {
        v->absent = true;
        return true;
    }
    if (*p == '"') {
        std::string text;
        if (!ParseQuoted(p, &text) || *p) return false;
        v->type = REG_SZ;
        v->data.assign(text.begin(), text.end());
        v->data.push_back(0);
        return true;
    }
    if (_strnicmp(p, "dword:", 6) == 0) {
        unsigned long dword;
        if (sscanf_s(p + 6, "%8lx", &dword) != 1) return false;
        v->type = REG_DWORD;
        v->data.resize(4);
        std::memcpy(v->data.data(), &dword, 4);
        return true;
    }
    if (_strnicmp(p, "hex:", 4) == 0) {
        v->type = REG_BINARY;
        return ParseHexBytes(p + 4, &v->data);
    }
    unsigned type;
    int used = 0;
    if (sscanf_s(p, "hex(%x):%n", &type, &used) == 1 && used > 0) {
        v->type = type;
        return ParseHexBytes(p + used, &v->data);
    }
    return false;
}

void Load() {
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%s\\registry.reg", g_folder);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        Problem(std::string("cannot read the registry snapshot ") + path);
        return;
    }
    std::string text;
    char chunk[4096];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) text.append(chunk, n);
    std::fclose(f);

    Key* key = nullptr;
    int lineNumber = 0, values = 0;
    std::string line;
    for (std::size_t at = 0; at <= text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(at, end - at);
        at = end + 1;
        ++lineNumber;
        while (!part.empty() && (part.back() == '\r' || part.back() == ' ' || part.back() == '\t')) part.pop_back();
        std::size_t first = part.find_first_not_of(" \t");
        part = first == std::string::npos ? std::string() : part.substr(first);
        // A value's hex bytes continue on the next line after a backslash.
        if (!part.empty() && part.back() == '\\') {
            part.pop_back();
            line += part;
            continue;
        }
        line += part;
        if (line.empty() || line[0] == ';' || line.rfind("Windows Registry Editor", 0) == 0) {
            line.clear();
            continue;
        }
        bool ok = true;
        if (line[0] == '[' && line.back() == ']') {
            const bool absent = line[1] == '-';
            key = &S().keys[Lower(line.substr(absent ? 2 : 1, line.size() - (absent ? 3 : 2)))];
            key->absent = absent;
        } else if (key) {
            const char* p = line.c_str();
            std::string name;
            if (*p == '@') {
                ++p;
            } else {
                ok = ParseQuoted(p, &name);
            }
            Value v;
            ok = ok && *p == '=' && ParseValue(p + 1, &v);
            if (ok) {
                key->values[Lower(name)] = v;
                ++values;
            }
        } else {
            ok = false;
        }
        if (!ok) Problem(std::string("registry.reg line ") + std::to_string(lineNumber) + " not understood: " + line);
        line.clear();
    }
    Log("harness: registry snapshot %s: %u keys, %d values; the user's settings are not read", path,
        unsigned(S().keys.size()), values);
}

bool KeyKnown(const std::string& path) {
    auto it = S().keys.lower_bound(path);
    if (it != S().keys.end() && it->first == path) return !it->second.absent;
    // An intermediate key: one of the snapshot's lies below it.
    const std::string below = path + "\\";
    return it != S().keys.end() && it->first.compare(0, below.size(), below) == 0;
}

bool KeyAbsent(const std::string& path) {
    auto it = S().keys.find(path);
    return it != S().keys.end() && it->second.absent;
}

// ---- handles ----

// Handles of this module's keys: far above any kernel handle value (the handle table ends below 2^26).
constexpr std::uintptr_t kHandleBase = 0x7EB00000;

bool IsOurs(HKEY key) {
    const std::uintptr_t v = reinterpret_cast<std::uintptr_t>(key);
    return v >= kHandleBase && v < kHandleBase + kHandles * 4 && (v & 3) == 0;
}

std::string PathOf(HKEY key) {
    const std::size_t i = (reinterpret_cast<std::uintptr_t>(key) - kHandleBase) / 4;
    AcquireSRWLockShared(&g_lock);
    std::string path = S().used[i] ? S().open[i] : std::string();
    ReleaseSRWLockShared(&g_lock);
    return path;
}

HKEY NewHandle(const std::string& path) {
    AcquireSRWLockExclusive(&g_lock);
    HKEY handle = nullptr;
    for (std::size_t i = 0; i < kHandles; ++i) {
        if (S().used[i]) continue;
        S().used[i] = true;
        S().open[i] = path;
        handle = reinterpret_cast<HKEY>(kHandleBase + i * 4);
        break;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!handle) Problem("more than 1024 registry keys open at once");
    return handle;
}

void FreeHandle(HKEY key) {
    const std::size_t i = (reinterpret_cast<std::uintptr_t>(key) - kHandleBase) / 4;
    AcquireSRWLockExclusive(&g_lock);
    S().used[i] = false;
    S().open[i].clear();
    ReleaseSRWLockExclusive(&g_lock);
}

// The full lower-case path of `sub` below `root`, and whether it is one of the trees answered here
// (`any`: whatever the root, for game.exe's creates).
bool Resolve(HKEY root, const char* sub, std::string* path, bool any = false) {
    if (IsOurs(root)) {
        *path = PathOf(root);
    } else if (root == HKEY_CURRENT_USER) {
        *path = kCurrentUser;
    } else if (root == HKEY_LOCAL_MACHINE) {
        *path = kLocalMachine;
    } else if (any) {
        char name[32];
        std::snprintf(name, sizeof(name), "key%p", static_cast<void*>(root));
        *path = name;
    } else {
        return false;
    }
    // Each part of `sub` (empty parts, from doubled or outer backslashes, are skipped), lower case.
    for (const char* p = sub; p && *p;) {
        const char* end = std::strchr(p, '\\');
        if (!end) end = p + std::strlen(p);
        if (end != p) *path += '\\' + Lower(std::string(p, end));
        p = *end ? end + 1 : end;
    }
    if (IsOurs(root) || any) return true;
    const std::size_t rootEnd = path->find('\\');
    if (rootEnd == std::string::npos) return false;
    const std::string below = path->substr(rootEnd + 1);
    for (const char* tree : kTrees) {
        const std::size_t n = std::strlen(tree);
        if (below.compare(0, n, tree) == 0 && (below.size() == n || below[n] == '\\')) return true;
    }
    return false;
}

std::string Narrow(const wchar_t* text) {
    if (!text) return std::string();
    char buffer[512] = {};
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, sizeof(buffer) - 1, nullptr, nullptr);
    return buffer;
}

LSTATUS OpenOurs(const std::string& path, bool create, PHKEY out, LPDWORD disposition) {
    if (!out) return ERROR_INVALID_PARAMETER;
    const bool known = KeyKnown(path);
    if (!known && !create) {
        if (!KeyAbsent(path)) Problem("registry key not in the snapshot: " + path);
        return ERROR_FILE_NOT_FOUND;
    }
    if (!known) Log("harness: registry key %s created for the run only (not in the snapshot)", path.c_str());
    HKEY handle = NewHandle(path);
    if (!handle) return ERROR_NOT_ENOUGH_MEMORY;
    *out = handle;
    if (disposition) *disposition = known ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
    return ERROR_SUCCESS;
}

// Copies an answer the way RegQueryValueEx fills its outputs.
LSTATUS Fill(DWORD answerType, const void* answer, DWORD answerSize, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (type) *type = answerType;
    if (!size) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!data) {
        *size = answerSize;
        return ERROR_SUCCESS;
    }
    if (*size < answerSize) {
        *size = answerSize;
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, answer, answerSize);
    *size = answerSize;
    return ERROR_SUCCESS;
}

// The value `name` of our key `key`: the starter's answer, else the snapshot's. `wide`: strings as UTF-16.
LSTATUS QueryOurs(HKEY key, const std::string& name, bool wide, LPDWORD type, LPBYTE data, LPDWORD size) {
    DWORD answerType, answerSize, dword;
    const BYTE* answer;
    std::vector<BYTE> bytes;
    if (starter::Answer(name.c_str(), &answerType, &answer, &answerSize, &dword)) {
        bytes.assign(answer, answer + answerSize);
    } else {
        const std::string path = PathOf(key);
        auto k = S().keys.find(path);
        const Value* v = nullptr;
        if (k != S().keys.end()) {
            auto it = k->second.values.find(Lower(name));
            if (it != k->second.values.end()) v = &it->second;
        }
        if (!v) {
            Problem("registry value not in the snapshot: " + path + "\\" + (name.empty() ? "@" : name));
            return ERROR_FILE_NOT_FOUND;
        }
        if (v->absent) return ERROR_FILE_NOT_FOUND;
        answerType = v->type;
        bytes = v->data;
    }
    if (wide && (answerType == REG_SZ || answerType == REG_EXPAND_SZ) && !bytes.empty()) {
        std::vector<wchar_t> text(bytes.size());
        const int chars = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()),
                                              static_cast<int>(bytes.size()), text.data(), static_cast<int>(text.size()));
        bytes.assign(reinterpret_cast<const BYTE*>(text.data()), reinterpret_cast<const BYTE*>(text.data() + chars));
    }
    return Fill(answerType, bytes.data(), static_cast<DWORD>(bytes.size()), type, data, size);
}

// ---- the hooks (advapi32's stubs' slots; the originals are kernelbase's) ----

decltype(&::RegOpenKeyExA) g_openA;
decltype(&::RegOpenKeyExW) g_openW;
decltype(&::RegCreateKeyExA) g_createA;
decltype(&::RegCreateKeyExW) g_createW;
decltype(&::RegQueryValueExA) g_queryA;
decltype(&::RegQueryValueExW) g_queryW;
decltype(&::RegCloseKey) g_close;
decltype(&::RegSetValueExA) g_setA;
decltype(&::RegSetValueExW) g_setW;
decltype(&::RegDeleteValueA) g_deleteA;
decltype(&::RegDeleteValueW) g_deleteW;
decltype(&::RegEnumValueA) g_enumValueA;
decltype(&::RegEnumValueW) g_enumValueW;
decltype(&::RegEnumKeyExA) g_enumKeyA;
decltype(&::RegEnumKeyExW) g_enumKeyW;
decltype(&::RegGetValueA) g_getA;
decltype(&::RegGetValueW) g_getW;

LSTATUS WINAPI OpenA(HKEY root, LPCSTR sub, DWORD options, REGSAM sam, PHKEY out) {
    std::string path;
    if (!Resolve(root, sub, &path)) return g_openA(root, sub, options, sam, out);
    return OpenOurs(path, false, out, nullptr);
}
LSTATUS WINAPI OpenW(HKEY root, LPCWSTR sub, DWORD options, REGSAM sam, PHKEY out) {
    std::string path;
    const std::string narrow = Narrow(sub);
    if (!Resolve(root, sub ? narrow.c_str() : nullptr, &path)) return g_openW(root, sub, options, sam, out);
    return OpenOurs(path, false, out, nullptr);
}
LSTATUS WINAPI CreateA(HKEY root, LPCSTR sub, DWORD reserved, LPSTR cls, DWORD options, REGSAM sam,
                       const LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disposition) {
    std::string path;
    if (!Resolve(root, sub, &path)) return g_createA(root, sub, reserved, cls, options, sam, sa, out, disposition);
    return OpenOurs(path, true, out, disposition);
}
LSTATUS WINAPI CreateW(HKEY root, LPCWSTR sub, DWORD reserved, LPWSTR cls, DWORD options, REGSAM sam,
                       const LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disposition) {
    std::string path;
    const std::string narrow = Narrow(sub);
    if (!Resolve(root, sub ? narrow.c_str() : nullptr, &path))
        return g_createW(root, sub, reserved, cls, options, sam, sa, out, disposition);
    return OpenOurs(path, true, out, disposition);
}
// game.exe's own import (the original's CWinRegistry and the image's): whatever the key, nothing is created.
LSTATUS WINAPI GameCreate(HKEY root, LPCSTR sub, DWORD, LPSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY out,
                          LPDWORD disposition) {
    std::string path;
    Resolve(root, sub, &path, true);
    return OpenOurs(path, true, out, disposition);
}
LSTATUS WINAPI QueryA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (!IsOurs(key)) return g_queryA(key, name, reserved, type, data, size);
    return QueryOurs(key, name ? name : "", false, type, data, size);
}
LSTATUS WINAPI QueryW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (!IsOurs(key)) return g_queryW(key, name, reserved, type, data, size);
    return QueryOurs(key, Narrow(name), true, type, data, size);
}
LSTATUS WINAPI Close(HKEY key) {
    if (!IsOurs(key)) return g_close(key);
    FreeHandle(key);
    return ERROR_SUCCESS;
}

void Dropped(const char* call, HKEY key) {
    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= 4) Log("harness: %s on %s dropped (the run writes no settings)", call, PathOf(key).c_str());
}
LSTATUS WINAPI SetA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size) {
    if (!IsOurs(key)) return g_setA(key, name, reserved, type, data, size);
    Dropped("RegSetValueExA", key);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI SetW(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size) {
    if (!IsOurs(key)) return g_setW(key, name, reserved, type, data, size);
    Dropped("RegSetValueExW", key);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI DeleteA(HKEY key, LPCSTR name) {
    if (!IsOurs(key)) return g_deleteA(key, name);
    Dropped("RegDeleteValueA", key);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI DeleteW(HKEY key, LPCWSTR name) {
    if (!IsOurs(key)) return g_deleteW(key, name);
    Dropped("RegDeleteValueW", key);
    return ERROR_SUCCESS;
}

LSTATUS Unanswered(const char* call, const std::string& path) {
    Problem(std::string(call) + " on " + path + " is not answered by the snapshot");
    return ERROR_ACCESS_DENIED;
}
LSTATUS WINAPI EnumValueA(HKEY key, DWORD index, LPSTR name, LPDWORD nameSize, LPDWORD reserved, LPDWORD type,
                          LPBYTE data, LPDWORD size) {
    if (!IsOurs(key)) return g_enumValueA(key, index, name, nameSize, reserved, type, data, size);
    return Unanswered("RegEnumValueA", PathOf(key));
}
LSTATUS WINAPI EnumValueW(HKEY key, DWORD index, LPWSTR name, LPDWORD nameSize, LPDWORD reserved, LPDWORD type,
                          LPBYTE data, LPDWORD size) {
    if (!IsOurs(key)) return g_enumValueW(key, index, name, nameSize, reserved, type, data, size);
    return Unanswered("RegEnumValueW", PathOf(key));
}
LSTATUS WINAPI EnumKeyA(HKEY key, DWORD index, LPSTR name, LPDWORD nameSize, LPDWORD reserved, LPSTR cls,
                        LPDWORD clsSize, PFILETIME time) {
    if (!IsOurs(key)) return g_enumKeyA(key, index, name, nameSize, reserved, cls, clsSize, time);
    return Unanswered("RegEnumKeyExA", PathOf(key));
}
LSTATUS WINAPI EnumKeyW(HKEY key, DWORD index, LPWSTR name, LPDWORD nameSize, LPDWORD reserved, LPWSTR cls,
                        LPDWORD clsSize, PFILETIME time) {
    if (!IsOurs(key)) return g_enumKeyW(key, index, name, nameSize, reserved, cls, clsSize, time);
    return Unanswered("RegEnumKeyExW", PathOf(key));
}
LSTATUS WINAPI GetA(HKEY key, LPCSTR sub, LPCSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD size) {
    std::string path;
    if (!Resolve(key, sub, &path)) return g_getA(key, sub, name, flags, type, data, size);
    return Unanswered("RegGetValueA", path);
}
LSTATUS WINAPI GetW(HKEY key, LPCWSTR sub, LPCWSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD size) {
    std::string path;
    const std::string narrow = Narrow(sub);
    if (!Resolve(key, sub ? narrow.c_str() : nullptr, &path)) return g_getW(key, sub, name, flags, type, data, size);
    return Unanswered("RegGetValueW", path);
}

template <class T>
void PatchSlot(T* slot, T replacement, T& original) {
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

// advapi32's export `name` is a stub (MOV EDI,EDI; PUSH EBP; MOV EBP,ESP; POP EBP; JMP [slot]) that jumps
// through advapi32's own import of kernelbase's: every module's call comes through that slot.
template <class T>
bool HookExport(HMODULE advapi, const char* name, T replacement, T& original) {
    auto* code = reinterpret_cast<const BYTE*>(GetProcAddress(advapi, name));
    static const BYTE kStub[] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC, 0x5D, 0xFF, 0x25};
    if (!code || std::memcmp(code, kStub, sizeof(kStub)) != 0) {
        Problem(std::string("advapi32's ") + name + " is not the expected stub: its callers read the user's registry");
        return false;
    }
    T* slot;
    std::memcpy(&slot, code + sizeof(kStub), sizeof(slot));
    PatchSlot(slot, replacement, original);
    return true;
}

constexpr std::uintptr_t kGameRegCreateKeyExASlot = 0x0073B010;   // game.exe import table
decltype(&::RegCreateKeyExA) g_gameCreate;

} // namespace

void Install() {
    char folder[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("EI_HARNESS_SETTINGS", folder, sizeof(folder));
    if (n == 0 || n >= sizeof(folder)) {
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&Install), &self);
        GetModuleFileNameA(self, folder, sizeof(folder));
        if (char* slash = std::strrchr(folder, '\\')) *slash = '\0';
        strcat_s(folder, "\\..\\..\\config\\harness");
    }
    if (!GetFullPathNameA(folder, MAX_PATH, g_folder, nullptr)) strcpy_s(g_folder, folder);
    Load();
    for (const char* file : {"vanilla\\keyboard.ini", "vanilla\\addrbook.ini", "rebirth\\keyboard.ini", "rebirth\\addrbook.ini"}) {
        char path[MAX_PATH];
        std::snprintf(path, sizeof(path), "%s\\%s", g_folder, file);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) Problem(std::string("pinned install file missing: ") + path);
    }

    HMODULE advapi = GetModuleHandleA("advapi32.dll");
    if (!advapi) advapi = LoadLibraryA("advapi32.dll");
    if (!advapi) {
        Problem("advapi32.dll not loaded: the registry is not answered");
        return;
    }
    HookExport(advapi, "RegOpenKeyExA", &OpenA, g_openA);
    HookExport(advapi, "RegOpenKeyExW", &OpenW, g_openW);
    HookExport(advapi, "RegCreateKeyExA", &CreateA, g_createA);
    HookExport(advapi, "RegCreateKeyExW", &CreateW, g_createW);
    HookExport(advapi, "RegQueryValueExA", &QueryA, g_queryA);
    HookExport(advapi, "RegQueryValueExW", &QueryW, g_queryW);
    HookExport(advapi, "RegCloseKey", &Close, g_close);
    HookExport(advapi, "RegSetValueExA", &SetA, g_setA);
    HookExport(advapi, "RegSetValueExW", &SetW, g_setW);
    HookExport(advapi, "RegDeleteValueA", &DeleteA, g_deleteA);
    HookExport(advapi, "RegDeleteValueW", &DeleteW, g_deleteW);
    HookExport(advapi, "RegEnumValueA", &EnumValueA, g_enumValueA);
    HookExport(advapi, "RegEnumValueW", &EnumValueW, g_enumValueW);
    HookExport(advapi, "RegEnumKeyExA", &EnumKeyA, g_enumKeyA);
    HookExport(advapi, "RegEnumKeyExW", &EnumKeyW, g_enumKeyW);
    HookExport(advapi, "RegGetValueA", &GetA, g_getA);
    HookExport(advapi, "RegGetValueW", &GetW, g_getW);
    PatchSlot(reinterpret_cast<decltype(&::RegCreateKeyExA)*>(kGameRegCreateKeyExASlot), &GameCreate, g_gameCreate);
}

bool PinnedFile(const char* relative, char (&out)[260]) {
    if (!relative || (_stricmp(relative, "config\\keyboard.ini") != 0 && _stricmp(relative, "config\\addrbook.ini") != 0))
        return false;
    const char* install = starter::RebirthInstall() ? "rebirth" : "vanilla";
    if (std::snprintf(out, sizeof(out), "%s\\%s\\%s", g_folder, install, relative + 7) >= static_cast<int>(sizeof(out)))
        return false;
    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= 4) Log("harness: %s read from the pinned copy %s", relative, out);
    return true;
}

std::string Failure() {
    AcquireSRWLockShared(&g_lock);
    std::string text;
    for (const std::string& p : S().problems) text += (text.empty() ? "settings: " : "; ") + p;
    ReleaseSRWLockShared(&g_lock);
    return text;
}

} // namespace ei::verify::settings
