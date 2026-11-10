#pragma once

// TOML: documents read with tomlc17 (third_party/tomlc17) and written with our own small writer. The record files
// written as TOML (docs/notes/mods.md, "Records as TOML") use them. Text is UTF-8 both ways.

#include "tomlc17/tomlc17.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace toml {

// A value of a parsed document (a view: valid while its Document lives). A missing one (a key not there) is
// !Exists() and answers every question with nothing.
class Value {
public:
    Value() = default;
    explicit Value(const toml_datum_t& datum) : datum(datum) {}

    bool Exists() const { return datum.type != TOML_UNKNOWN; }
    bool IsTable() const { return datum.type == TOML_TABLE; }
    bool IsArray() const { return datum.type == TOML_ARRAY; }
    bool IsString() const { return datum.type == TOML_STRING; }
    bool IsInt() const { return datum.type == TOML_INT64; }
    bool IsFloat() const { return datum.type == TOML_FP64; }
    bool IsBool() const { return datum.type == TOML_BOOLEAN; }
    // The line it was read from (0: none).
    int Line() const { return datum.lineno; }

    // A table's value of `key` (missing when there is none or this is not a table).
    Value operator[](const char* key) const { return Value(toml_get(datum, key)); }
    // The number of an array's elements or of a table's keys (0 for anything else).
    int Size() const;
    // An array's element `i` (missing past its end or when this is not an array).
    Value At(int i) const { return IsArray() && i >= 0 && i < datum.u.arr.size ? Value(datum.u.arr.elem[i]) : Value(); }
    // A table's key `i` and its value, in the document's order.
    std::string_view KeyAt(int i) const { return {datum.u.tab.key[i], static_cast<std::size_t>(datum.u.tab.len[i])}; }
    Value ValueAt(int i) const { return Value(datum.u.tab.value[i]); }

    std::string_view String() const {
        return IsString() ? std::string_view(datum.u.str.ptr, static_cast<std::size_t>(datum.u.str.len)) : std::string_view();
    }
    std::int64_t Int() const { return IsInt() ? datum.u.int64 : 0; }
    double Float() const { return IsFloat() ? datum.u.fp64 : 0.0; }
    bool Bool() const { return IsBool() && datum.u.boolean; }

private:
    toml_datum_t datum{};
};

// The values a record loader reads into its fields: true when `value` is there and of a type the field takes (an
// integer for a float, a float for an integer, truncated), else the field keeps what it had.
bool Read(Value value, std::string* out);
bool Read(Value value, std::int32_t* out);
bool Read(Value value, std::uint32_t* out);
bool Read(Value value, std::uint8_t* out);
bool Read(Value value, float* out);
// An array of numbers into `out`, at most `count` of them; the rest of `out` zero. False when `value` is not an array.
bool ReadArray(Value value, std::uint32_t* out, int count);

// A parsed document.
class Document {
public:
    Document() = default;
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Parses `text`; false on an error, which Error() then holds ("<name>: line n: ...", as tomlc17 says it).
    bool Parse(std::string_view text, std::string_view name);
    const std::string& Error() const { return error; }
    // The top table (missing before a successful Parse).
    Value Root() const;

private:
    toml_result_t result{};
    bool parsed = false;
    std::string error;
};

// ---- Writing ------------------------------------------------------------------------------------------------------

// A key: bare when it is only ASCII letters, digits, '_' and '-', else a quoted string.
std::string Key(std::string_view key);
// A basic string: '"' and '\' escaped, control characters as \b \t \n \f \r or \uXXXX.
std::string String(std::string_view text);
// A string that holds line ends as a multi-line basic string (its LF kept as line ends, every other escape as
// String's), else as String.
std::string Text(std::string_view text);
std::string Int(std::int64_t value);
// Unsigned as hexadecimal ("0x1A").
std::string Hex(std::uint32_t value);
// The shortest decimal that reads back (through a double) to exactly `value`; always a float to TOML (".0" added to
// an integral one); inf, -inf and nan as TOML spells them (a NaN's payload is not kept).
std::string Float(float value);
std::string Bool(bool value);
// An array of value texts: on one line, or with `multiline` one element a line, each followed by a comma.
std::string Array(const std::vector<std::string>& items, bool multiline = false);
// An inline table of (key, value text) pairs: "{ a = 1, b = "x" }".
std::string InlineTable(const std::vector<std::pair<std::string, std::string>>& pairs);

// A document written line by line.
class Writer {
public:
    // A comment line ("# " and the text).
    void Comment(std::string_view text);
    // "key = value" (the value a text of the functions above).
    void Pair(std::string_view key, const std::string& value);
    // "[name]" and "[[name]]", a blank line before each but at the document's start; the name as it is (a Key, or
    // keys joined with '.').
    void Table(std::string_view name);
    void ArrayTable(std::string_view name);

    std::string text;
};

} // namespace toml
