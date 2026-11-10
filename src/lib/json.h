#pragma once

// JSON: a small reader of whole documents into a tree, and the writer's pieces. The figure models as glTF read their
// `extras` with it (cgltf hands them over as JSON text; docs/notes/mods.md, "Models as glTF") and write their whole
// JSON with it. Text is UTF-8; a number is read as a double (a float written by Float reads back to the same bits).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json {

// A value of a parsed document. A missing one (a key not there, an element past the end) is !Exists() and answers
// every question with nothing.
class Value {
public:
    enum class Type { Missing, Null, Bool, Number, String, Array, Object };

    bool Exists() const { return type != Type::Missing; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    double Number() const { return type == Type::Number ? number : 0.0; }
    float Float() const { return static_cast<float>(Number()); }
    // The number truncated (0 for anything else).
    std::int64_t Int() const { return type == Type::Number ? static_cast<std::int64_t>(number) : 0; }
    bool Bool() const { return type == Type::Bool && number != 0.0; }
    const std::string& String() const { return text; }

    // The elements of an array, the keys of an object (0 for anything else).
    int Size() const { return static_cast<int>(type == Type::Object ? members.size() : elements.size()); }
    const Value& At(int i) const;
    // An object's value of `key` (the first of that name).
    const Value& operator[](std::string_view key) const;
    std::string_view KeyAt(int i) const { return members[static_cast<std::size_t>(i)].first; }
    const Value& ValueAt(int i) const { return members[static_cast<std::size_t>(i)].second; }

private:
    friend class Parser;
    Type type = Type::Missing;
    double number = 0.0;
    std::string text;
    std::vector<Value> elements;
    std::vector<std::pair<std::string, Value>> members;
};

// `text` parsed into `*out`; false with `*error` set ("offset n: ...") when it is not one JSON value.
bool Parse(std::string_view text, Value* out, std::string* error);

// ---- Writing ------------------------------------------------------------------------------------------------------

// A string: '"' and '\' escaped, control characters as \b \t \n \f \r or \uXXXX.
std::string String(std::string_view text);
// The shortest decimal that reads back (through a double) to exactly `value`; inf, -inf and nan, which JSON has no
// numbers for, as the strings "inf", "-inf" and "nan" (Parse's reader of a float takes them: FloatOf).
std::string Float(float value);
// A float value as Float writes it: a number, or one of those strings (0 for anything else).
float FloatOf(const Value& value);

} // namespace json
