// TOML documents: the reader over tomlc17 and our writer (toml.h).
#include "lib/toml.h"

#include "lib/str.h"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <system_error>

namespace toml {

int Value::Size() const {
    if (IsArray()) return datum.u.arr.size;
    if (IsTable()) return datum.u.tab.size;
    return 0;
}

bool Read(Value value, std::string* out) {
    if (!value.IsString()) return false;
    *out = value.String();
    return true;
}

bool Read(Value value, std::int32_t* out) {
    if (value.IsInt()) *out = static_cast<std::int32_t>(value.Int());
    else if (value.IsFloat() && std::isfinite(value.Float())) *out = static_cast<std::int32_t>(value.Float());
    else return false;
    return true;
}

bool Read(Value value, std::uint32_t* out) {
    std::int32_t read = 0;
    if (!Read(value, &read)) return false;
    *out = static_cast<std::uint32_t>(read);
    return true;
}

bool Read(Value value, std::uint8_t* out) {
    std::int32_t read = 0;
    if (!Read(value, &read)) return false;
    *out = static_cast<std::uint8_t>(read);
    return true;
}

bool Read(Value value, float* out) {
    if (value.IsFloat()) *out = static_cast<float>(value.Float());
    else if (value.IsInt()) *out = static_cast<float>(value.Int());
    else return false;
    return true;
}

bool ReadArray(Value value, std::uint32_t* out, int count) {
    for (int i = 0; i < count; ++i) out[i] = 0;
    for (int i = 0; i < count; ++i) Read(value.At(i), &out[i]);
    return value.IsArray();
}

Document::~Document() {
    if (parsed) toml_free(result);
}

bool Document::Parse(std::string_view text, std::string_view name) {
    if (parsed) toml_free(result);
    error.clear();
    const std::string source(name);
    result = toml_parse_named(text.data(), static_cast<int>(text.size()), source.c_str());
    parsed = true;
    if (result.ok) return true;
    error = source + ": " + result.errmsg;
    return false;
}

Value Document::Root() const { return parsed && result.ok ? Value(result.toptab) : Value(); }

// ---- Writing ------------------------------------------------------------------------------------------------------

namespace {

bool IsBareKeyCharacter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// `c` escaped as a basic string writes it, or empty when it stands as it is.
std::string_view Escape(char c, char* buffer) {
    switch (c) {
    case '"': return "\\\"";
    case '\\': return "\\\\";
    case '\b': return "\\b";
    case '\t': return "\\t";
    case '\n': return "\\n";
    case '\f': return "\\f";
    case '\r': return "\\r";
    default: break;
    }
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7F) {
        std::snprintf(buffer, 8, "\\u%04X", byte);
        return buffer;
    }
    return {};
}

void AppendEscaped(std::string* out, std::string_view text, bool keepLineEnds) {
    char buffer[8];
    for (const char c : text) {
        const std::string_view escaped = keepLineEnds && c == '\n' ? std::string_view() : Escape(c, buffer);
        if (escaped.empty()) out->push_back(c);
        else out->append(escaped);
    }
}

// The shortest text of `value` (std::to_chars) when it reads back through a double to the same bits; else the exact
// double's.
std::string ShortestFloat(float value) {
    char buffer[64];
    auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    double back = 0.0;
    str::FloatFromChars(std::string_view(buffer, written.ptr), back);
    if (std::bit_cast<std::uint32_t>(static_cast<float>(back)) != std::bit_cast<std::uint32_t>(value))
        written = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<double>(value));
    return std::string(buffer, written.ptr);
}

} // namespace

std::string Key(std::string_view key) {
    bool bare = !key.empty();
    for (const char c : key) bare = bare && IsBareKeyCharacter(c);
    return bare ? std::string(key) : String(key);
}

std::string String(std::string_view text) {
    std::string out = "\"";
    AppendEscaped(&out, text, false);
    out += '"';
    return out;
}

std::string Text(std::string_view text) {
    if (text.find('\n') == std::string_view::npos) return String(text);
    // The line end after the opening quotes is not part of the string.
    std::string out = "\"\"\"\n";
    AppendEscaped(&out, text, true);
    out += "\"\"\"";
    return out;
}

std::string Int(std::int64_t value) { return std::to_string(value); }

std::string Hex(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%X", value);
    return buffer;
}

std::string Float(float value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    std::string text = ShortestFloat(value);
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

std::string Bool(bool value) { return value ? "true" : "false"; }

std::string Array(const std::vector<std::string>& items, bool multiline) {
    if (items.empty()) return "[]";
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (multiline) out += "\n    ";
        else if (i != 0) out += ", ";
        out += items[i];
        if (multiline) out += ',';
    }
    if (multiline) out += '\n';
    out += ']';
    return out;
}

std::string InlineTable(const std::vector<std::pair<std::string, std::string>>& pairs) {
    if (pairs.empty()) return "{}";
    std::string out = "{ ";
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (i != 0) out += ", ";
        out += Key(pairs[i].first) + " = " + pairs[i].second;
    }
    out += " }";
    return out;
}

void Writer::Comment(std::string_view comment) {
    text += "# ";
    text += comment;
    text += '\n';
}

void Writer::Pair(std::string_view key, const std::string& value) {
    text += Key(key);
    text += " = ";
    text += value;
    text += '\n';
}

void Writer::Table(std::string_view name) {
    if (!text.empty()) text += '\n';
    text += '[';
    text += name;
    text += "]\n";
}

void Writer::ArrayTable(std::string_view name) {
    if (!text.empty()) text += '\n';
    text += "[[";
    text += name;
    text += "]]\n";
}

} // namespace toml
