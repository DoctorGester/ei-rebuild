// The standalone's settings file. See settings_file.h.
#include "settings_file.h"

#include "system_text.h"

#include "ei/hook.h"
#include "hook/log.h"

#include <string>
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>

namespace settings_file {
namespace {

// ---- The process's settings ----

struct Store {
    Document document;
    std::wstring file;            // empty until loaded: the one next to the exe, or the pinned one
    bool loaded = false;
    bool pinned = false;          // a harness run's file
    bool readOnly = false;        // pinned and read only: nothing written, nothing imported
    bool dirty = false;
    RegistrySource* importSource = nullptr;   // the harness's, in place of the registry
    MissingHandler missing = nullptr;
    std::vector<ReadFilter> filters;
};
Store& S() {
    static Store* store = new Store;   // never destroyed: the game may still read at exit
    return *store;
}
SRWLOCK g_lock = SRWLOCK_INIT;

struct Locked {
    Locked() { AcquireSRWLockExclusive(&g_lock); }
    ~Locked() { ReleaseSRWLockExclusive(&g_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};

std::wstring FileNextToExe() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    path.resize(path.find_last_of(L'\\') + 1);
    for (const char* c = kFileName; *c; ++c) path += static_cast<wchar_t>(*c);
    return path;
}

bool ReadWhole(const std::wstring& file, std::string* text) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(handle, &size) && size.QuadPart < (1 << 24);
    if (ok) {
        text->resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(handle, text->data(), static_cast<DWORD>(text->size()), &read, nullptr) && read == text->size();
    }
    CloseHandle(handle);
    return ok;
}

// The file written whole beside itself, then moved over it.
void Write(Store& s) {
    const std::string text = s.document.Text();
    const std::wstring temporary = s.file + L".tmp";
    HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    bool ok = handle != INVALID_HANDLE_VALUE;
    if (ok) {
        ok = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
        CloseHandle(handle);
    }
    ok = ok && MoveFileExW(temporary.c_str(), s.file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        ei::Log("settings: cannot write %ls (error %lu)", s.file.c_str(), GetLastError());
        DeleteFileW(temporary.c_str());
        return;
    }
    s.dirty = false;
}

// HKEY_CURRENT_USER as the game's ANSI calls read it (text::sys: UTF-8 with EI_UTF8); read only.
class Registry : public RegistrySource {
public:
    bool ReadKey(const std::string& path, std::vector<Value>* values, std::vector<std::string>* subkeys) override {
        HKEY key;
        if (text::sys::RegOpenKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
        DWORD longestName = 0, longestKey = 0;
        RegQueryInfoKeyA(key, nullptr, nullptr, nullptr, nullptr, &longestKey, nullptr, nullptr, &longestName, nullptr,
                         nullptr, nullptr);
        std::vector<char> name(static_cast<std::size_t>((std::max)(longestName, longestKey)) + 1);
        for (DWORD i = 0;; ++i) {
            DWORD nameSize = static_cast<DWORD>(name.size());
            const LSTATUS status = RegEnumValueA(key, i, name.data(), &nameSize, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS) continue;
            DWORD type = 0, size = 0;
            if (text::sys::RegQueryValueExA(key, name.data(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS) continue;
            std::string data(size, '\0');
            if (text::sys::RegQueryValueExA(key, name.data(), nullptr, &type, reinterpret_cast<BYTE*>(data.data()),
                                            &size) != ERROR_SUCCESS)
                continue;
            data.resize(size);
            values->push_back(Value{name.data(), type, std::move(data)});
        }
        for (DWORD i = 0;; ++i) {
            DWORD nameSize = static_cast<DWORD>(name.size());
            const LSTATUS status = RegEnumKeyExA(key, i, name.data(), &nameSize, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status == ERROR_SUCCESS) subkeys->push_back(name.data());
        }
        RegCloseKey(key);
        return true;
    }
};

void Load(Store& s) {
    if (s.loaded) return;
    s.loaded = true;
    if (s.file.empty()) s.file = FileNextToExe();
    std::string text;
    if (ReadWhole(s.file, &text)) {
        std::vector<std::string> problems;
        s.document.Parse(text, &problems);
        for (const std::string& problem : problems) ei::Log("settings: %ls: %s", s.file.c_str(), problem.c_str());
        std::size_t values = 0;
        for (const Key& key : s.document.keys) values += key.values.size();
        ei::Log("settings: %ls: %zu keys, %zu values%s", s.file.c_str(), s.document.keys.size(), values,
                s.readOnly ? " (pinned, read only)" : s.pinned ? " (pinned)" : "");
        return;
    }
    if (s.readOnly) {
        ei::Log("settings: the pinned file %ls cannot be read", s.file.c_str());
        return;
    }
    Registry registry;
    const int values = ImportTrees(s.document, s.importSource ? *s.importSource : static_cast<RegistrySource&>(registry));
    ei::Log("settings: %ls missing: %d values imported from %s", s.file.c_str(), values,
            s.importSource ? "the harness's stand-in" : "the registry");
    Write(s);
}

// RegQueryValueEx's way of giving `bytes`.
long Fill(unsigned long valueType, const std::string& bytes, unsigned long* type, void* data, unsigned long* size) {
    if (type) *type = valueType;
    if (!size) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    const auto needed = static_cast<unsigned long>(bytes.size());
    if (!data) {
        *size = needed;
        return ERROR_SUCCESS;
    }
    if (*size < needed) {
        *size = needed;
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    *size = needed;
    return ERROR_SUCCESS;
}

const char* PathOf(const void* key) { return key ? static_cast<const Key*>(key)->path.c_str() : ""; }

} // namespace

// ---- The process's settings ----

void* CreateKey(const char* path) {
    Locked lock;
    Store& s = S();
    Load(s);
    return &s.document.AddKey(JoinPath(path, nullptr));
}

long Query(const void* key, const char* sub, const char* name, unsigned long* type, void* data, unsigned long* size) {
    if (!name) name = "";
    Locked lock;
    Store& s = S();
    Load(s);
    const std::string path = JoinPath(PathOf(key), sub);
    Key* found = s.document.FindKey(path);
    const Value* value = found ? Document::FindValue(*found, name) : nullptr;
    if (!value) {
        if (s.missing) s.missing(path.c_str(), name);
        return ERROR_FILE_NOT_FOUND;
    }
    std::string bytes = value->data;
    for (ReadFilter filter : s.filters) filter(name, value->type, bytes);
    return Fill(value->type, bytes, type, data, size);
}

long Set(const void* key, const char* sub, const char* name, unsigned long type, const void* data, unsigned long size) {
    if (!name) name = "";
    Locked lock;
    Store& s = S();
    Load(s);
    if (s.readOnly) return ERROR_SUCCESS;   // a harness run writes no settings
    const std::string path = JoinPath(PathOf(key), sub);
    std::string bytes(static_cast<const char*>(data), size);
    Key& target = s.document.AddKey(path);
    if (const Value* value = Document::FindValue(target, name); value && value->type == type && value->data == bytes)
        return ERROR_SUCCESS;
    s.document.Set(path, name, type, std::move(bytes));
    s.dirty = true;
    return ERROR_SUCCESS;
}

void Flush() {
    Locked lock;
    Store& s = S();
    if (s.loaded && !s.readOnly && s.dirty) Write(s);
}

long ReadValue(const char* path, const char* name, unsigned long* type, void* data, unsigned long* size) {
    return Query(nullptr, path, name, type, data, size);
}

void AddReadFilter(ReadFilter filter) {
    Locked lock;
    S().filters.push_back(filter);
}

void SetImportSource(RegistrySource* source) {
    Locked lock;
    S().importSource = source;
}

void Pin(const char* file, MissingHandler missing, bool readOnly) {
    Locked lock;
    Store& s = S();
    s.document.keys.clear();
    s.file.assign(static_cast<std::size_t>(MultiByteToWideChar(CP_ACP, 0, file, -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_ACP, 0, file, -1, s.file.data(), static_cast<int>(s.file.size()));
    if (!s.file.empty()) s.file.pop_back();   // the terminator
    s.pinned = true;
    s.readOnly = readOnly;
    s.loaded = false;
    s.dirty = false;
    s.missing = missing;
}

} // namespace settings_file
