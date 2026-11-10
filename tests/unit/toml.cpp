// lib/toml.h: what our writer writes reads back through tomlc17 as it was written (strings with every control
// character, keys that need quotes, floats by their bits, integers, arrays, inline tables, arrays of tables).
#include "../../src/lib/toml.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("ei_unit_check: toml: %s\n", what.c_str());
}

// `value` written as a float, read back: the same bits (a NaN: a NaN).
void CheckFloat(float value) {
    toml::Document document;
    const std::string text = "v = " + toml::Float(value) + "\n";
    if (!document.Parse(text, "float")) {
        Check(false, "float " + text + ": " + document.Error());
        return;
    }
    const float back = static_cast<float>(document.Root()["v"].Float());
    const bool same = value != value ? back != back : std::bit_cast<std::uint32_t>(back) == std::bit_cast<std::uint32_t>(value);
    Check(document.Root()["v"].IsFloat() && same, "float " + text);
}

} // namespace

int RunTomlChecks() {
    // Every byte below 0x80 in a string and in a key, and UTF-8 above it.
    std::string all;
    for (int c = 1; c < 0x80; ++c) all.push_back(static_cast<char>(c));
    all += "\xD0\x9F\xD1\x80\xD0\xB8";   // "При"
    const std::string withZero = std::string("a\0b", 3);
    const std::string lines = "first\r\nsecond \"quoted\" \\ back\r\n\tthird\n";
    toml::Writer writer;
    writer.Pair("plain", toml::String(all));
    writer.Pair(all, toml::Int(-7));
    writer.Pair("zero", toml::String(withZero));
    writer.Pair("lines", toml::Text(lines));
    writer.Pair("one line", toml::Text("no line end"));
    writer.Pair("hex", toml::Hex(0xFFFFFFFFu));
    writer.Pair("min", toml::Int(-2147483647 - 1));
    writer.Pair("yes", toml::Bool(true));
    writer.Pair("empty", toml::Array({}));
    writer.Pair("lists", toml::Array({toml::Int(1), toml::Int(2)}, true));
    writer.Pair("inline", toml::InlineTable({{"a b", toml::String("x")}, {"c", toml::Float(0.5f)}}));
    writer.Table(toml::Key("section.with dots"));
    writer.Pair("k", toml::Int(3));
    writer.ArrayTable("item");
    writer.Pair("n", toml::Int(1));
    writer.ArrayTable("item");
    writer.Pair("n", toml::Int(2));

    toml::Document document;
    if (!document.Parse(writer.text, "written")) {
        Check(false, "the written document: " + document.Error() + "\n" + writer.text);
    } else {
        const toml::Value root = document.Root();
        Check(root["plain"].String() == all, "a string of every character");
        Check(root[all.c_str()].Int() == -7, "a key of every character");
        Check(root["zero"].String() == withZero, "a string with a zero byte");
        Check(root["lines"].String() == lines, "a multi-line text");
        Check(root["one line"].String() == "no line end", "a one-line text");
        Check(root["hex"].Int() == 0xFFFFFFFFll, "a hexadecimal number");
        Check(root["min"].Int() == -2147483647ll - 1, "the least 32-bit number");
        Check(root["yes"].Bool(), "a boolean");
        Check(root["empty"].IsArray() && root["empty"].Size() == 0, "an empty array");
        Check(root["lists"].Size() == 2 && root["lists"].At(1).Int() == 2 && !root["lists"].At(2).Exists(),
              "a multi-line array");
        Check(root["inline"]["a b"].String() == "x" && root["inline"]["c"].Float() == 0.5, "an inline table");
        Check(root["section.with dots"]["k"].Int() == 3, "a quoted table name");
        Check(root["item"].Size() == 2 && root["item"].At(1)["n"].Int() == 2, "an array of tables");
        Check(root.KeyAt(0) == "plain" && root.KeyAt(1) == all, "the keys in the document's order");
        std::uint32_t numbers[3] = {9, 9, 9};
        Check(toml::ReadArray(root["lists"], numbers, 3) && numbers[0] == 1 && numbers[1] == 2 && numbers[2] == 0,
              "an array read into numbers");
    }
    toml::Document broken;
    Check(!broken.Parse("a = = 1\n", "broken") && broken.Error().find("broken") == 0, "a broken document's error");

    // Floats: special values, edges, and a spread of bit patterns.
    for (const float value : {0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 1e-45f, 1.17549435e-38f, 3.40282347e+38f, 16777217.0f,
                              std::bit_cast<float>(0x7F800000u), std::bit_cast<float>(0xFF800000u),
                              std::bit_cast<float>(0x7FC00000u)})
        CheckFloat(value);
    std::uint32_t bits = 0x12345678u;
    for (int i = 0; i < 200000; ++i) {
        bits = bits * 1664525u + 1013904223u;
        CheckFloat(std::bit_cast<float>(bits));
    }
    std::printf("ei_unit_check: toml: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
