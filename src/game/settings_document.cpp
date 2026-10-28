// The settings file's text and the import's tree walk (no registry, no game): Document, its reg-export-style
// lines, JoinPath, ImportTrees. See settings_file.h; tests/vectors/replay_settings.cpp checks them without the game.
#include "settings_file.h"

#include <string>
#include <windows.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>

namespace settings_file {
namespace {

bool Same(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

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

int ImportTree(Document& document, RegistrySource& source, const std::string& path) {
    std::vector<Value> values;
    std::vector<std::string> subkeys;
    if (!source.ReadKey(path, &values, &subkeys)) return 0;
    document.AddKey(path);
    int count = static_cast<int>(values.size());
    for (Value& value : values) document.Set(path, value.name, value.type, std::move(value.data));
    for (const std::string& sub : subkeys) count += ImportTree(document, source, path + "\\" + sub);
    return count;
}

} // namespace


Key* Document::FindKey(const std::string& path) {
    for (Key& key : keys)
        if (Same(key.path, path)) return &key;
    return nullptr;
}

Key& Document::AddKey(const std::string& path) {
    if (Key* key = FindKey(path)) return *key;
    keys.push_back(Key{path, {}});
    return keys.back();
}

Value* Document::FindValue(Key& key, const std::string& name) {
    for (Value& value : key.values)
        if (Same(value.name, name)) return &value;
    return nullptr;
}

void Document::Set(const std::string& path, const std::string& name, unsigned long type, std::string data) {
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
            if (ok) Set(key->path, value.name, value.type, std::move(value.data));
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

// ---- The import ----

bool DocumentSource::ReadKey(const std::string& path, std::vector<Value>* values, std::vector<std::string>* subkeys) {
    const std::string below = path + "\\";
    bool exists = false;
    for (const Key& key : document_.keys) {
        if (Same(key.path, path)) {
            exists = true;
            values->insert(values->end(), key.values.begin(), key.values.end());
        } else if (_strnicmp(key.path.c_str(), below.c_str(), below.size()) == 0) {
            exists = true;   // a key exists when one below it does
            const std::string name = key.path.substr(below.size(), key.path.find('\\', below.size()) - below.size());
            bool listed = false;
            for (const std::string& known : *subkeys) listed = listed || Same(known, name);
            if (!listed) subkeys->push_back(name);
        }
    }
    return exists;
}

int ImportTrees(Document& document, RegistrySource& source) {
    int count = 0;
    for (const char* tree : kImportedTrees) count += ImportTree(document, source, tree);
    return count;
}

} // namespace settings_file
