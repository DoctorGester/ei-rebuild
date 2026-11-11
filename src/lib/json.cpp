// JSON read and written: see json.h.
#include "lib/json.h"

#include "lib/str.h"
#include "lib/toml.h"   // toml::Float: the shortest text of a float

#include <charconv>
#include <cmath>
#include <cstdio>

namespace json {

namespace {

const Value kMissing;

} // namespace

const Value& Value::At(int i) const {
    return type == Type::Array && i >= 0 && i < static_cast<int>(elements.size()) ? elements[static_cast<std::size_t>(i)]
                                                                                   : kMissing;
}

const Value& Value::operator[](std::string_view key) const {
    if (type != Type::Object) return kMissing;
    for (const auto& [name, value] : members)
        if (name == key) return value;
    return kMissing;
}

// The recursive reader (a friend of Value).
class Parser {
public:
    explicit Parser(std::string_view text) : text(text) {}

    bool Document(Value* out, std::string* error) {
        const bool ok = ValueAt(out, 0) && (Space(), at == text.size());
        if (!ok) *error = "offset " + std::to_string(at) + ": " + (problem.empty() ? "text after the value" : problem);
        return ok;
    }

private:
    // Deep enough for any glTF's extras; a deeper document is refused rather than overflowing the stack.
    static constexpr int kMostDepth = 64;

    std::string_view text;
    std::size_t at = 0;
    std::string problem;

    bool Fail(const char* what) {
        if (problem.empty()) problem = what;
        return false;
    }
    void Space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) ++at;
    }
    // Past an array's or object's closing bracket.
    bool Close() {
        ++at;
        return true;
    }
    bool Word(std::string_view word) {
        if (text.substr(at, word.size()) != word) return Fail("not a value");
        at += word.size();
        return true;
    }

    bool ValueAt(Value* out, int depth) {
        if (depth > kMostDepth) return Fail("nested too deep");
        Space();
        if (at == text.size()) return Fail("no value");
        const char c = text[at];
        if (c == '{') return ObjectAt(out, depth);
        if (c == '[') return ArrayAt(out, depth);
        if (c == '"') {
            out->type = Value::Type::String;
            return StringAt(&out->text);
        }
        if (c == 't' || c == 'f') {
            out->type = Value::Type::Bool;
            out->number = c == 't' ? 1.0 : 0.0;
            return Word(c == 't' ? "true" : "false");
        }
        if (c == 'n') {
            out->type = Value::Type::Null;
            return Word("null");
        }
        return NumberAt(out);
    }

    bool NumberAt(Value* out) {
        const std::size_t start = at;
        if (at < text.size() && text[at] == '-') ++at;
        while (at < text.size() && ((text[at] >= '0' && text[at] <= '9') || text[at] == '.' || text[at] == 'e' ||
                                    text[at] == 'E' || text[at] == '+' || text[at] == '-'))
            ++at;
        // from_chars takes no plus sign at the start, which JSON has none of either.
        const auto read = str::FloatFromChars(text.substr(start, at - start), out->number);
        if (read.ec != std::errc() || read.ptr != text.data() + at || at == start) return Fail("not a number");
        out->type = Value::Type::Number;
        return true;
    }

    // Four hexadecimal digits at `at`.
    bool Hex4(unsigned* code) {
        if (at + 4 > text.size()) return Fail("short \\u escape");
        *code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[at++];
            const unsigned digit = c >= '0' && c <= '9' ? static_cast<unsigned>(c - '0')
                                   : c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a' + 10)
                                   : c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A' + 10)
                                                          : 16u;
            if (digit == 16u) return Fail("bad \\u escape");
            *code = *code * 16 + digit;
        }
        return true;
    }

    static void AppendUtf8(std::string* out, unsigned code) {
        if (code < 0x80) {
            out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out->push_back(static_cast<char>(0xE0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out->push_back(static_cast<char>(0xF0 | (code >> 18)));
            out->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool StringAt(std::string* out) {
        ++at;   // the quote
        while (at < text.size() && text[at] != '"') {
            const char c = text[at++];
            if (static_cast<unsigned char>(c) < 0x20) return Fail("a control character in a string");
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (at == text.size()) break;
            const char e = text[at++];
            switch (e) {
            case '"': case '\\': case '/': out->push_back(e); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                unsigned code = 0;
                if (!Hex4(&code)) return false;
                // A surrogate pair is one character; a lone surrogate is kept as its own code.
                if (code >= 0xD800 && code < 0xDC00 && text.substr(at, 2) == "\\u") {
                    const std::size_t pair = at;
                    at += 2;
                    unsigned low = 0;
                    if (!Hex4(&low)) return false;
                    if (low >= 0xDC00 && low < 0xE000) code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    else at = pair;
                }
                AppendUtf8(out, code);
                break;
            }
            default: return Fail("bad escape");
            }
        }
        if (at == text.size()) return Fail("an unterminated string");
        ++at;
        return true;
    }

    bool ArrayAt(Value* out, int depth) {
        out->type = Value::Type::Array;
        ++at;
        Space();
        if (at < text.size() && text[at] == ']') return Close();
        for (;;) {
            if (!ValueAt(&out->elements.emplace_back(), depth + 1)) return false;
            Space();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            if (at < text.size() && text[at] == ']') return Close();
            return Fail("no ',' or ']' in an array");
        }
    }

    bool ObjectAt(Value* out, int depth) {
        out->type = Value::Type::Object;
        ++at;
        Space();
        if (at < text.size() && text[at] == '}') return Close();
        for (;;) {
            Space();
            if (at == text.size() || text[at] != '"') return Fail("no key in an object");
            auto& member = out->members.emplace_back();
            if (!StringAt(&member.first)) return false;
            Space();
            if (at == text.size() || text[at] != ':') return Fail("no ':' after a key");
            ++at;
            if (!ValueAt(&member.second, depth + 1)) return false;
            Space();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            if (at < text.size() && text[at] == '}') return Close();
            return Fail("no ',' or '}' in an object");
        }
    }
};

bool Parse(std::string_view text, Value* out, std::string* error) {
    *out = Value();
    return Parser(text).Document(out, error);
}

std::string String(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            } else {
                out.push_back(c);
            }
        }
    }
    out += '"';
    return out;
}

std::string Float(float value) {
    if (std::isnan(value)) return "\"nan\"";
    if (std::isinf(value)) return value < 0 ? "\"-inf\"" : "\"inf\"";
    return toml::Float(value);
}

float FloatOf(const Value& value) {
    if (value.IsNumber()) return value.Float();
    if (!value.IsString()) return 0.0f;
    if (value.String() == "inf") return INFINITY;
    if (value.String() == "-inf") return -INFINITY;
    if (value.String() == "nan") return NAN;
    return 0.0f;
}

} // namespace json
