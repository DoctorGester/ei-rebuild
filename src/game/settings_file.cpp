// The standalone's settings file. See settings_file.h.
#include "settings_file.h"

#include "ei/hook.h"
#include "hook/log.h"

#include <string>
#include <windows.h>

#include <cstring>

namespace settings_file {
namespace {

// ---- The process's settings ----

struct Store {
    Document document;
    std::wstring file;            // empty until loaded: the one next to the exe or the per-user one, or the pinned one
    bool loaded = false;
    bool pinned = false;          // a harness run's file
    bool readOnly = false;        // pinned and read only: nothing written, no defaults
    bool inMemory = false;        // the file could not be read or made: the defaults, nothing written this session
    bool dirty = false;
    int desktopWidth = 0;         // the harness's desktop for the defaults (0: the primary monitor's)
    int desktopHeight = 0;
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

std::wstring WithFileName(std::wstring folder) {
    for (const char* c = kFileName; *c; ++c) folder += static_cast<wchar_t>(*c);
    return folder;
}

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
    return WithFileName(path);
}

// %LOCALAPPDATA%\EvilIslands\ei_settings.ini (empty without LOCALAPPDATA).
std::wstring FileForUser() {
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (n == 0) return {};
    std::wstring folder(n, L'\0');
    folder.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), n));
    if (folder.empty()) return {};
    if (folder.back() != L'\\') folder += L'\\';
    return WithFileName(folder + L"EvilIslands\\");
}

enum class Read { read, missing, failed };

// The whole file, shared with any writer (an editor or a sync tool holding it open). `missing` only when the file
// or its folder does not exist.
Read ReadWhole(const std::wstring& file, std::string* text) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return Read::missing;
        ei::Log("settings: %ls cannot be read (error %lu)", file.c_str(), error);
        return Read::failed;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(handle, &size) && size.QuadPart < (1 << 24);
    if (ok) {
        text->resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(handle, text->data(), static_cast<DWORD>(text->size()), &read, nullptr) && read == text->size();
    }
    if (!ok) ei::Log("settings: %ls cannot be read (error %lu, %lld bytes)", file.c_str(), GetLastError(), size.QuadPart);
    CloseHandle(handle);
    return ok ? Read::read : Read::failed;
}

// The file written whole beside itself (its folder made when missing), then moved over it.
bool Write(Store& s) {
    const std::string text = s.document.Text();
    const std::wstring temporary = s.file + L".tmp";
    CreateDirectoryW(s.file.substr(0, s.file.find_last_of(L'\\')).c_str(), nullptr);
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
        return false;
    }
    s.dirty = false;
    return true;
}

// The defaults for the primary monitor's mode in pixels (whatever the process's DPI awareness), or the harness's
// desktop. Without a size (no desktop): for 1024x768, kept in memory only, so a start with a desktop writes its own.
void UseDefaults(Store& s, int* width, int* height) {
    *width = s.desktopWidth;
    *height = s.desktopHeight;
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if ((*width <= 0 || *height <= 0) &&
        GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) &&
        EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        *width = static_cast<int>(mode.dmPelsWidth);
        *height = static_cast<int>(mode.dmPelsHeight);
    }
    if (*width <= 0 || *height <= 0) {
        ei::Log("settings: no desktop size (%dx%d): the defaults for 1024x768, not written this session", *width, *height);
        *width = 1024;
        *height = 768;
        s.inMemory = true;
    }
    s.document = DefaultDocument(*width, *height);
}

// The first of the places that holds a file, read: the pinned file, else the one next to the exe, then the per-user
// one. With none, the defaults, written to the first place that takes them. A file that exists but cannot be read
// is never written over: the defaults are kept in memory, and nothing is written this session.
void Load(Store& s) {
    if (s.loaded) return;
    s.loaded = true;
    std::vector<std::wstring> places;
    if (s.pinned) places.push_back(s.file);
    else {
        places.push_back(FileNextToExe());
        if (std::wstring user = FileForUser(); !user.empty()) places.push_back(std::move(user));
    }
    int width = 0, height = 0;
    for (const std::wstring& place : places) {
        s.file = place;
        std::string text;
        const Read read = ReadWhole(place, &text);
        if (read == Read::missing) continue;
        if (read == Read::failed) {
            if (s.readOnly) return;
            UseDefaults(s, &width, &height);
            s.inMemory = true;
            ei::Log("settings: %ls left as it is: the defaults (full screen at %dx%d) are used and nothing is written "
                    "this session", place.c_str(), width, height);
            return;
        }
        std::vector<std::string> problems;
        s.document.Parse(text, &problems);
        for (const std::string& problem : problems) ei::Log("settings: %ls: %s", place.c_str(), problem.c_str());
        std::size_t values = 0;
        for (const Key& key : s.document.keys) values += key.values.size();
        ei::Log("settings: %ls: %zu keys, %zu values%s", place.c_str(), s.document.keys.size(), values,
                s.readOnly ? " (pinned, read only)" : s.pinned ? " (pinned)" : "");
        return;
    }
    if (s.readOnly) {
        ei::Log("settings: the pinned file %ls cannot be read", s.file.c_str());
        return;
    }
    UseDefaults(s, &width, &height);
    if (s.inMemory) return;
    for (const std::wstring& place : places) {
        s.file = place;
        if (Write(s)) {
            ei::Log("settings: %ls missing: written with the defaults (full screen at %dx%d)", place.c_str(), width, height);
            return;
        }
    }
    s.inMemory = true;
    ei::Log("settings: no settings file can be written: the defaults (full screen at %dx%d) are kept in memory this "
            "session", width, height);
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
    if (s.loaded && !s.readOnly && !s.inMemory && s.dirty) Write(s);
}

long ReadValue(const char* path, const char* name, unsigned long* type, void* data, unsigned long* size) {
    return Query(nullptr, path, name, type, data, size);
}

void AddReadFilter(ReadFilter filter) {
    Locked lock;
    S().filters.push_back(filter);
}

void SetDesktopSize(int width, int height) {
    Locked lock;
    S().desktopWidth = width;
    S().desktopHeight = height;
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
    s.inMemory = false;
    s.dirty = false;
    s.missing = missing;
}

} // namespace settings_file
