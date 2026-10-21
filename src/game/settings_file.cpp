// The standalone's settings file. See settings_file.h.
#include "settings_file.h"

#include "system_text.h"

#include "ei/hook.h"
#include "hook/log.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>

namespace settings_file {
namespace {

bool Same(const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; }

// ---- The text ----

std::string Quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + '"';
}

// A string the quoted form holds: one terminator at its end, no other control character.
bool PlainString(const std::string& data) {
    if (data.empty() || data.back() != '\0') return false;
    for (std::size_t i = 0; i + 1 < data.size(); ++i)
        if (static_cast<unsigned char>(data[i]) < 0x20) return false;
    return true;
}

// The 4 bytes as "float:<value>" when the text reads back to the same bits (not for NaNs and infinities).
bool FloatText(const std::string& data, std::string* out) {
    if (data.size() != 4) return false;
    std::uint32_t bits;
    std::memcpy(&bits, data.data(), 4);
    if ((bits & 0x7F800000u) == 0x7F800000u) return false;
    float value;
    std::memcpy(&value, &bits, 4);
    std::array<char, 32> text;
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    if (written.ec != std::errc()) return false;
    float back = 0;
    const auto read = std::from_chars(text.data(), written.ptr, back);
    std::uint32_t backBits;
    std::memcpy(&backBits, &back, 4);
    if (read.ec != std::errc() || read.ptr != written.ptr || backBits != bits) return false;
    *out = "float:" + std::string(text.data(), written.ptr);
    return true;
}

// `value` as `digits` lower-case hex digits.
void AppendHex(std::string& out, std::uint32_t value, int digits) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) out += kDigits[(value >> shift) & 0xF];
}

std::string ValueText(const Value& v) {
    std::string out;
    if (v.type == REG_DWORD && v.data.size() == 4) {
        std::uint32_t dword;
        std::memcpy(&dword, v.data.data(), 4);
        out = "dword:";
        AppendHex(out, dword, 8);
        return out;
    }
    if (v.type == REG_SZ && PlainString(v.data)) return Quoted(v.data.substr(0, v.data.size() - 1));
    if (v.type == REG_BINARY && FloatText(v.data, &out)) return out;
    if (v.type == REG_BINARY) {
        out = "hex:";
    } else {
        std::array<char, 16> type;
        out = "hex(" + std::string(type.data(), std::to_chars(type.data(), type.data() + type.size(), v.type, 16).ptr) + "):";
    }
    for (std::size_t i = 0; i < v.data.size(); ++i) {
        if (i) out += ',';
        AppendHex(out, static_cast<unsigned char>(v.data[i]), 2);
    }
    return out;
}

// The quoted text at `p` (\\ and \" escaped); `p` moves past its closing quote.
bool ParseQuoted(const char*& p, std::string* out) {
    if (*p != '"') return false;
    ++p;
    out->clear();
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        *out += *p++;
    }
    if (*p != '"') return false;
    ++p;
    return true;
}

bool ParseHex(const char* p, std::string* out) {
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        unsigned byte = 0;
        const auto read = std::from_chars(p, p + 2, byte, 16);
        if (read.ec != std::errc() || read.ptr != p + 2) return false;
        *out += static_cast<char>(byte);
        p += 2;
    }
    return true;
}

bool StartsWith(const char* p, const char* prefix) { return _strnicmp(p, prefix, std::strlen(prefix)) == 0; }

bool ParseValue(const char* p, Value* v) {
    const char* end = p + std::strlen(p);
    if (*p == '"') {
        if (!ParseQuoted(p, &v->data) || *p) return false;
        v->type = REG_SZ;
        v->data += '\0';
        return true;
    }
    if (StartsWith(p, "dword:")) {
        std::uint32_t dword = 0;
        const auto read = std::from_chars(p + 6, end, dword, 16);
        if (read.ec != std::errc() || read.ptr != end || end - (p + 6) > 8) return false;
        v->type = REG_DWORD;
        v->data.assign(reinterpret_cast<const char*>(&dword), 4);
        return true;
    }
    if (StartsWith(p, "float:")) {
        float value = 0;
        const auto read = std::from_chars(p + 6, end, value);
        if (read.ec != std::errc() || read.ptr != end) return false;
        v->type = REG_BINARY;
        v->data.assign(reinterpret_cast<const char*>(&value), 4);
        return true;
    }
    if (StartsWith(p, "hex:")) {
        v->type = REG_BINARY;
        return ParseHex(p + 4, &v->data);
    }
    if (StartsWith(p, "hex(")) {
        const char* close = std::strchr(p, ')');
        if (!close || close[1] != ':') return false;
        const auto read = std::from_chars(p + 4, close, v->type, 16);
        if (read.ec != std::errc() || read.ptr != close) return false;
        return ParseHex(close + 2, &v->data);
    }
    return false;
}

constexpr const char* kHeader =
    "; Evil Islands' settings for ei_standalone.exe, in place of its registry keys under HKEY_CURRENT_USER.\r\n"
    "; [key below HKEY_CURRENT_USER], then \"name\"=value: dword:<hex> (REG_DWORD), \"text\" (REG_SZ, \\\\ and \\\"\r\n"
    "; escaped), float:<number> (a 4-byte REG_BINARY float), hex:<bytes> (REG_BINARY), hex(<type>):<bytes>.\r\n"
    "; @ is a key's default value. The game rewrites this file when it saves its options.\r\n";

// ---- The process's settings ----

struct Store {
    Document document;
    std::wstring file;            // empty until loaded: the one next to the exe, or the pinned one
    bool loaded = false;
    bool pinned = false;
    bool dirty = false;
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

// The trees game.exe and Rebirth keep their settings in, read from the registry when the file is missing.
constexpr const char* kImportedTrees[] = {"Software\\Nival Interactive\\EvilIslands", "Software\\Gipat.Ru\\EI_Starter"};

// Copies the key `path` and every key below it from the registry (read only), each value as the game's
// ANSI call reads it (text::sys: UTF-8 with EI_UTF8).
void Import(Document& document, const std::string& path, int* values) {
    HKEY key;
    if (text::sys::RegOpenKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    document.AddKey(path);
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
        if (text::sys::RegQueryValueExA(key, name.data(), nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &size) !=
            ERROR_SUCCESS)
            continue;
        data.resize(size);
        document.Set(path, name.data(), type, std::move(data));
        ++*values;
    }
    std::vector<std::string> subkeys;
    for (DWORD i = 0;; ++i) {
        DWORD nameSize = static_cast<DWORD>(name.size());
        const LSTATUS status = RegEnumKeyExA(key, i, name.data(), &nameSize, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_SUCCESS) subkeys.push_back(path + "\\" + name.data());
    }
    RegCloseKey(key);
    for (const std::string& sub : subkeys) Import(document, sub, values);
}

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
                s.pinned ? " (pinned, read only)" : "");
        return;
    }
    if (s.pinned) {
        ei::Log("settings: the pinned file %ls cannot be read", s.file.c_str());
        return;
    }
    int values = 0;
    for (const char* tree : kImportedTrees) Import(s.document, tree, &values);
    ei::Log("settings: %ls missing: %d values imported from the registry", s.file.c_str(), values);
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

// ---- Document ----

Key* Document::FindKey(const std::string& path) {
    for (Key& key : keys)
        if (Same(key.path, path.c_str())) return &key;
    return nullptr;
}

Key& Document::AddKey(const std::string& path) {
    if (Key* key = FindKey(path)) return *key;
    keys.push_back(Key{path, {}});
    return keys.back();
}

Value* Document::FindValue(Key& key, const char* name) {
    for (Value& value : key.values)
        if (Same(value.name, name)) return &value;
    return nullptr;
}

void Document::Set(const std::string& path, const char* name, unsigned long type, std::string data) {
    Key& key = AddKey(path);
    if (Value* value = FindValue(key, name)) {
        value->type = type;
        value->data = std::move(data);
        return;
    }
    key.values.push_back(Value{name, type, std::move(data)});
}

std::string Document::Text() const {
    std::string out = kHeader;
    for (const Key& key : keys) {
        out += "\r\n[" + key.path + "]\r\n";
        for (const Value& value : key.values)
            out += (value.name.empty() ? std::string("@") : Quoted(value.name)) + "=" + ValueText(value) + "\r\n";
    }
    return out;
}

void Document::Parse(const std::string& text, std::vector<std::string>* problems) {
    Key* key = nullptr;
    std::string line;
    int number = 0;
    for (std::size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0; at < text.size();) {   // past a UTF-8 BOM
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(at, end - at);
        at = end + 1;
        ++number;
        while (!part.empty() && (part.back() == '\r' || part.back() == ' ' || part.back() == '\t')) part.pop_back();
        const std::size_t first = part.find_first_not_of(" \t");
        part = first == std::string::npos ? std::string() : part.substr(first);
        // Hex bytes continue on the next line after a backslash, as reg export writes them.
        if (!part.empty() && part.back() == '\\') {
            part.pop_back();
            line += part;
            continue;
        }
        line += part;
        if (line.empty() || line[0] == ';') {
            line.clear();
            continue;
        }
        bool ok = true;
        if (line[0] == '[' && line.back() == ']') {
            key = &AddKey(JoinPath("", line.substr(1, line.size() - 2).c_str()));
        } else if (key) {
            const char* p = line.c_str();
            Value value;
            if (*p == '@') {
                ++p;
            } else {
                ok = ParseQuoted(p, &value.name);
            }
            while (ok && (*p == ' ' || *p == '\t')) ++p;
            ok = ok && *p++ == '=';
            while (ok && (*p == ' ' || *p == '\t')) ++p;
            ok = ok && ParseValue(p, &value);
            if (ok) Set(key->path, value.name.c_str(), value.type, std::move(value.data));
        } else {
            ok = false;
        }
        if (!ok) problems->push_back("line " + std::to_string(number) + " not understood: " + line);
        line.clear();
    }
}

std::string JoinPath(const char* path, const char* sub) {
    std::string out;
    for (const char* part : {path, sub}) {
        for (const char* p = part; p && *p;) {
            const char* end = std::strchr(p, '\\');
            if (!end) end = p + std::strlen(p);
            if (end != p) {
                if (!out.empty()) out += '\\';
                out.append(p, end);
            }
            p = *end ? end + 1 : end;
        }
    }
    return out;
}

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
    if (s.pinned) return ERROR_SUCCESS;   // a harness run writes no settings
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
    if (s.loaded && !s.pinned && s.dirty) Write(s);
}

long ReadValue(const char* path, const char* name, unsigned long* type, void* data, unsigned long* size) {
    if (ei::Standalone()) return Query(nullptr, path, name, type, data, size);
    HKEY key;
    const LSTATUS status = text::sys::RegOpenKeyExA(HKEY_CURRENT_USER, path, 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS) return status;
    const LSTATUS read = text::sys::RegQueryValueExA(key, name, nullptr, type, static_cast<BYTE*>(data), size);
    RegCloseKey(key);
    return read;
}

void AddReadFilter(ReadFilter filter) {
    Locked lock;
    S().filters.push_back(filter);
}

void Pin(const char* file, MissingHandler missing) {
    Locked lock;
    Store& s = S();
    s.document.keys.clear();
    s.file.assign(static_cast<std::size_t>(MultiByteToWideChar(CP_ACP, 0, file, -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_ACP, 0, file, -1, s.file.data(), static_cast<int>(s.file.size()));
    if (!s.file.empty()) s.file.pop_back();   // the terminator
    s.pinned = true;
    s.loaded = false;
    s.dirty = false;
    s.missing = missing;
}

} // namespace settings_file
