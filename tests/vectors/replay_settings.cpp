// The standalone's settings file without the game (ST1a): Document::Text / Parse round trips of every kind of value,
// the format's rules (case, comments, continuation lines, bad lines) and the defaults a missing file is written
// with. Not recorded vectors: ei_vectors runs these after the replays and counts their failures (replay_main.cpp).
// The file save (.tmp, then moved) and the defaults in a running game are the harness scenarios settings_save and
// settings_defaults (tests/scenarios, tools/settings_file_check.py).
#include "../../src/game/settings_file.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using settings_file::Document;
using settings_file::Key;
using settings_file::Value;

int g_checks = 0;
int g_failures = 0;

void Fail(int line, const char* what, const std::string& detail) {
    ++g_failures;
    if (g_failures <= 20) std::printf("  settings file check (line %d): %s%s%s\n", line, what, detail.empty() ? "" : ": ", detail.c_str());
}

#define CHECK(cond)                                  \
    do {                                             \
        ++g_checks;                                  \
        if (!(cond)) Fail(__LINE__, #cond, "");      \
    } while (0)
#define CHECK_MSG(cond, detail)                      \
    do {                                             \
        ++g_checks;                                  \
        if (!(cond)) Fail(__LINE__, #cond, (detail)); \
    } while (0)

constexpr const char* kKey = "Software\\Test\\Key";

std::string Bytes32(std::uint32_t bits) { return std::string(reinterpret_cast<const char*>(&bits), 4); }

std::string Hex(const std::string& data) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i) out += ',';
        out += kDigits[static_cast<unsigned char>(data[i]) >> 4];
        out += kDigits[static_cast<unsigned char>(data[i]) & 15];
    }
    return out;
}

std::string Printable(const std::string& s) {
    std::string out;
    for (char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == '\x7f') ? '?' : c;
    return out;
}

// The document's text read back; the parse must leave no problem.
Document Reread(const Document& document, std::string* text = nullptr) {
    const std::string written = document.Text();
    if (text) *text = written;
    Document back;
    std::vector<std::string> problems;
    back.Parse(written, &problems);
    CHECK_MSG(problems.empty(), problems.empty() ? "" : problems[0]);
    return back;
}

bool SameValue(const Value& a, const Value& b) { return a.name == b.name && a.type == b.type && a.data == b.data; }

// Same keys and values, in the same order, spelt the same.
bool SameDocument(const Document& a, const Document& b) {
    if (a.keys.size() != b.keys.size()) return false;
    for (std::size_t k = 0; k < a.keys.size(); ++k) {
        if (a.keys[k].path != b.keys[k].path || a.keys[k].values.size() != b.keys[k].values.size()) return false;
        for (std::size_t v = 0; v < a.keys[k].values.size(); ++v)
            if (!SameValue(a.keys[k].values[v], b.keys[k].values[v])) return false;
    }
    return true;
}

// The text line of the value `name` of the document (without its line break); empty when there is none.
std::string LineOf(const std::string& text, const std::string& quotedName) {
    const std::string at = "\n" + quotedName + "=";
    const std::size_t start = text.find(at);
    if (start == std::string::npos) return std::string();
    const std::size_t end = text.find("\r\n", start + 1);
    return text.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
}

const Value* Find(Document& document, const char* path, const char* name) {
    Key* key = document.FindKey(path);
    return key ? Document::FindValue(*key, name) : nullptr;
}

// Sets `data` as `name` of kKey with `type`, reads the text back and requires the same value; returns the text
// line of the value.
std::string RoundTripOne(std::uint32_t type, const std::string& data, const char* name = "v") {
    Document document;
    document.Set(kKey, name, type, data);
    std::string text;
    Document back = Reread(document, &text);
    const Value* value = Find(back, kKey, name);
    CHECK_MSG(value && value->type == type && value->data == data,
              std::string("type ") + std::to_string(type) + " data " + Hex(data) + " came back changed");
    CHECK(SameDocument(document, back));
    CHECK(back.Text() == text);   // and writes the same text again
    std::string quoted = "\"";
    for (const char* c = name; *c; ++c) {
        if (*c == '\\' || *c == '"') quoted += '\\';
        quoted += *c;
    }
    quoted += '"';
    return LineOf(text, *name ? quoted : std::string("@"));
}

void Dwords() {
    for (std::uint32_t dword : {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u, 0x000000ffu}) {
        const std::string line = RoundTripOne(REG_DWORD, Bytes32(dword));
        char expected[40];
        std::snprintf(expected, sizeof(expected), "\"v\"=dword:%08x", dword);
        CHECK_MSG(line == expected, "got " + line);
    }
    // A REG_DWORD that is not four bytes keeps its bytes (hex(4)).
    for (std::size_t size : {0u, 1u, 3u, 5u, 8u}) {
        const std::string line = RoundTripOne(REG_DWORD, std::string(size, '\x5a'));
        CHECK_MSG(line.rfind("\"v\"=hex(4):", 0) == 0, "REG_DWORD of " + std::to_string(size) + " bytes: " + line);
    }
    // The other four-byte integer type.
    CHECK(RoundTripOne(REG_DWORD_BIG_ENDIAN, Bytes32(0x01020304)).rfind("\"v\"=hex(5):", 0) == 0);
}

void Strings() {
    const std::vector<std::string> plain = {
        "", "plain", "C:\\Games\\EI\\", "quote\"d", "\\\\server\\share", "ends\\", "\\", "\"", "a=b", "[not a section]",
        "; not a comment", "  padded  ", "dword:00000001", "float:1", "hex:01", "hex(7):61", "@", "tab-free \x7f del",
        "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB (utf-8)", "\xE9\xF0\xFF high bytes", std::string(300, 'x'),
        "1920x1080", "a\\\"b\\\\\"c"};
    for (const std::string& text : plain) {
        const std::string line = RoundTripOne(REG_SZ, text + '\0');
        CHECK_MSG(line.rfind("\"v\"=\"", 0) == 0, "REG_SZ " + Printable(text) + " written as " + Printable(line));
    }
    // What the quoted form cannot hold goes to hex(1): control characters, a second terminator, no terminator.
    const std::vector<std::string> raw = {std::string("a\tb\0", 4),   std::string("a\nb\0", 4), std::string("a\rb\0", 4),
                                          std::string("\x01\0", 2),   std::string("a\0b\0", 4), std::string("abc", 3),
                                          std::string("a\0b", 3),     std::string(),            std::string("\0\0", 2)};
    for (const std::string& data : raw) {
        const std::string line = RoundTripOne(REG_SZ, data);
        CHECK_MSG(line.rfind("\"v\"=hex(1):", 0) == 0, "REG_SZ " + Hex(data) + " written as " + Printable(line));
    }
    // The empty string is the quoted form.
    CHECK(RoundTripOne(REG_SZ, std::string("\0", 1)) == "\"v\"=\"\"");
    // A REG_EXPAND_SZ is hex(2) whatever it holds.
    CHECK(RoundTripOne(REG_EXPAND_SZ, std::string("%PATH%\0", 7)) == "\"v\"=hex(2):25,50,41,54,48,25,00");
}

void Floats() {
    struct Case {
        std::uint32_t bits;
        const char* text;   // the float: text; null: hex (not a number)
    };
    const Case cases[] = {
        {0x00000000u, "float:0"},   {0x80000000u, "float:-0"},   {0x3f800000u, "float:1"},    {0xbf800000u, "float:-1"},
        {0x3dcccccdu, "float:0.1"}, {0x42b40000u, "float:90"},   {0x7f7fffffu, nullptr},       {0xff7fffffu, nullptr},
        {0x00800000u, nullptr},     {0x7f800000u, nullptr},      {0xff800000u, nullptr},       {0x7fc00000u, nullptr},
        {0xffc00000u, nullptr},     {0x7f800001u, nullptr},      {0x7fffffffu, nullptr},       {0xffffffffu, nullptr},
        {0x00000001u, nullptr},     {0x80000001u, nullptr},      {0x007fffffu, nullptr},       {0x807fffffu, nullptr},
        {0x4b800000u, nullptr},     {0x33800000u, nullptr},
    };
    for (const Case& c : cases) {
        const std::string line = RoundTripOne(REG_BINARY, Bytes32(c.bits));
        const bool number = (c.bits & 0x7f800000u) != 0x7f800000u;
        char name[32];
        std::snprintf(name, sizeof(name), "%08x", c.bits);
        if (c.text) {
            CHECK_MSG(line == std::string("\"v\"=") + c.text, std::string(name) + " written as " + line);
        } else if (!number) {   // a NaN or infinity is bytes, not a number
            CHECK_MSG(line == "\"v\"=hex:" + Hex(Bytes32(c.bits)), std::string(name) + " written as " + line);
        } else {                // any other number: float: when its text reads back to the bits, else bytes
            CHECK_MSG(line.rfind("\"v\"=float:", 0) == 0 || line.rfind("\"v\"=hex:", 0) == 0, std::string(name) + " written as " + line);
        }
    }
    // Every exponent with a few mantissas and both signs, then a pseudo-random sweep of bit patterns: the bits
    // read back whatever the text form is (the writer falls back to bytes when the text does not round trip).
    std::vector<std::uint32_t> sweep;
    for (std::uint32_t sign = 0; sign < 2; ++sign)
        for (std::uint32_t exponent = 0; exponent < 256; ++exponent)
            for (std::uint32_t mantissa : {0u, 1u, 2u, 0x400000u, 0x7ffffeu, 0x7fffffu, 0x123456u})
                sweep.push_back(sign << 31 | exponent << 23 | mantissa);
    std::uint32_t state = 0x9e3779b9u;
    for (int i = 0; i < 20000; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        sweep.push_back(state);
    }
    int asText = 0, asBytes = 0;
    for (std::size_t from = 0; from < sweep.size(); from += 1500) {
        Document document;
        const std::size_t to = (std::min)(sweep.size(), from + 1500);
        for (std::size_t i = from; i < to; ++i) document.Set(kKey, ("f" + std::to_string(i)).c_str(), REG_BINARY, Bytes32(sweep[i]));
        std::string text;
        Document back = Reread(document, &text);
        CHECK(SameDocument(document, back));
        for (std::size_t i = from; i < to; ++i) {
            const std::string line = LineOf(text, "\"f" + std::to_string(i) + "\"");
            if (line.find("=float:") != std::string::npos) ++asText;
            else ++asBytes;
        }
    }
    CHECK(asText > 10000);          // the numbers are written as numbers
    CHECK(asBytes >= 14);           // and at least the NaNs and infinities as bytes
}

void Binaries() {
    CHECK(RoundTripOne(REG_BINARY, std::string()) == "\"v\"=hex:");
    CHECK(RoundTripOne(REG_BINARY, std::string("\x01", 1)) == "\"v\"=hex:01");
    CHECK(RoundTripOne(REG_BINARY, std::string("\x01\x02\x03", 3)) == "\"v\"=hex:01,02,03");
    CHECK(RoundTripOne(REG_BINARY, std::string("\x01\x02\x03\x04\x05", 5)) == "\"v\"=hex:01,02,03,04,05");
    std::string all;
    for (int i = 0; i < 256; ++i) all += static_cast<char>(i);
    RoundTripOne(REG_BINARY, all);
    RoundTripOne(REG_BINARY, all + all + all);
}

void OtherTypes() {
    CHECK(RoundTripOne(REG_MULTI_SZ, std::string("one\0two\0\0", 9)) == "\"v\"=hex(7):6f,6e,65,00,74,77,6f,00,00");
    CHECK(RoundTripOne(REG_MULTI_SZ, std::string("\0\0", 2)) == "\"v\"=hex(7):00,00");
    CHECK(RoundTripOne(REG_MULTI_SZ, std::string()) == "\"v\"=hex(7):");
    RoundTripOne(REG_MULTI_SZ, std::string("\xD0\x96\0b\0\0", 6));
    CHECK(RoundTripOne(REG_QWORD, std::string("\x01\x02\x03\x04\x05\x06\x07\x08", 8)) == "\"v\"=hex(b):01,02,03,04,05,06,07,08");
    CHECK(RoundTripOne(REG_NONE, std::string("ab", 2)) == "\"v\"=hex(0):61,62");
    CHECK(RoundTripOne(REG_LINK, std::string("x", 1)) == "\"v\"=hex(6):78");
    CHECK(RoundTripOne(0xdeadbeefu, std::string("z", 1)) == "\"v\"=hex(deadbeef):7a");
    CHECK(RoundTripOne(REG_RESOURCE_LIST, std::string("\xff\x00", 2)) == "\"v\"=hex(8):ff,00");
}

void Names() {
    // The default value is "@", a value named "@" is quoted; both are kept apart.
    Document document;
    document.Set(kKey, "", REG_SZ, std::string("default\0", 8));
    document.Set(kKey, "@", REG_DWORD, Bytes32(7));
    document.Set(kKey, "quote\"back\\slash", REG_DWORD, Bytes32(1));
    document.Set(kKey, "a=b", REG_DWORD, Bytes32(2));
    document.Set(kKey, "[bracket]", REG_DWORD, Bytes32(3));
    document.Set(kKey, ";semi", REG_DWORD, Bytes32(4));
    document.Set(kKey, "  spaced name  ", REG_DWORD, Bytes32(5));
    document.Set(kKey, "\xD0\x98\xD0\xBC\xD1\x8F", REG_DWORD, Bytes32(6));
    std::string text;
    Document back = Reread(document, &text);
    CHECK(SameDocument(document, back));
    CHECK(LineOf(text, "@") == "@=\"default\"");
    CHECK(LineOf(text, "\"@\"") == "\"@\"=dword:00000007");
    CHECK(Find(back, kKey, "") && Find(back, kKey, "@") && Find(back, kKey, "")->data != Find(back, kKey, "@")->data);
    CHECK(LineOf(text, "\"quote\\\"back\\\\slash\"") == "\"quote\\\"back\\\\slash\"=dword:00000001");
    // Value names compare without case; the first spelling stays.
    CHECK(Find(back, kKey, "A=B") && Find(back, kKey, "A=B")->name == "a=b");
    document.Set(kKey, "A=B", REG_DWORD, Bytes32(99));
    CHECK(document.keys[0].values.size() == 8);
    CHECK(Find(document, kKey, "a=b")->data == Bytes32(99) && Find(document, kKey, "a=b")->name == "a=b");
    // Another type replaces the type too.
    document.Set(kKey, "a=b", REG_SZ, std::string("s\0", 2));
    CHECK(Find(document, kKey, "a=b")->type == REG_SZ);
    // Value order is the order of setting.
    Document ordered;
    for (const char* name : {"z", "a", "m", "B"}) ordered.Set(kKey, name, REG_DWORD, Bytes32(1));
    Document orderedBack = Reread(ordered);
    CHECK(orderedBack.keys[0].values.size() == 4 && orderedBack.keys[0].values[0].name == "z" &&
          orderedBack.keys[0].values[1].name == "a" && orderedBack.keys[0].values[3].name == "B");
}

void Keys() {
    const char* text =
        "[Software\\Nival Interactive\\EvilIslands]\r\n"
        "\"Name\"=dword:00000001\r\n"
        "[SOFTWARE\\nival interactive\\EVILISLANDS]\r\n"
        "\"NAME\"=dword:00000002\r\n"
        "\"other\"=\"x\"\r\n"
        "[Software\\Nival Interactive\\EvilIslands\\Sub]\r\n"
        "\"s\"=dword:00000003\r\n";
    Document document;
    std::vector<std::string> problems;
    document.Parse(text, &problems);
    CHECK(problems.empty());
    CHECK(document.keys.size() == 2);   // the second header is the first key again
    CHECK(document.keys[0].path == "Software\\Nival Interactive\\EvilIslands");   // the first spelling stays
    CHECK(document.keys[0].values.size() == 2);
    const Value* name = Find(document, "SOFTWARE\\NIVAL INTERACTIVE\\evilislands", "name");
    CHECK(name && name->name == "Name" && name->data == Bytes32(2));   // the last value wins, the first spelling stays
    CHECK(document.FindKey("software\\nival interactive\\evilislands\\sub") != nullptr);
    CHECK(document.FindKey("software\\nival interactive") == nullptr);   // a parent is not made
    // Set and AddKey meet the same key without case.
    document.Set("software\\NIVAL INTERACTIVE\\evilislands", "other", REG_SZ, std::string("y\0", 2));
    CHECK(document.keys.size() == 2 && document.keys[0].values.size() == 2 && Find(document, "Software\\Nival Interactive\\EvilIslands", "OTHER")->data == std::string("y\0", 2));
    Key& again = document.AddKey("SOFTWARE\\nival interactive\\evilislands\\SUB");
    CHECK(&again == &document.keys[1] && document.keys.size() == 2);
    // Paths are joined by one backslash; empty parts and outer backslashes go.
    CHECK(settings_file::JoinPath("a\\b", "c\\d") == "a\\b\\c\\d");
    CHECK(settings_file::JoinPath("\\a\\\\b\\", "\\c\\") == "a\\b\\c");
    CHECK(settings_file::JoinPath("", "x") == "x" && settings_file::JoinPath("x", nullptr) == "x" && settings_file::JoinPath("", "").empty());
    Document odd;
    odd.Parse("[\\Software\\\\Odd\\]\n\"v\"=dword:1\n", &problems);
    CHECK(odd.keys.size() == 1 && odd.keys[0].path == "Software\\Odd");
    // Key paths with spaces, and with ']' inside, round trip.
    Document paths;
    paths.Set("Software\\Nival Interactive\\Evil Islands (x86)", "a", REG_DWORD, Bytes32(1));
    paths.Set("Software\\odd]key", "b", REG_DWORD, Bytes32(2));
    paths.AddKey("Software\\Empty");
    CHECK(SameDocument(paths, Reread(paths)));
}

void Comments() {
    // Comments, blank lines, indentation, trailing blanks, any line break, a BOM, no final line break.
    const std::string text =
        "\xEF\xBB\xBF; header comment\n"
        "\n"
        "   ; an indented comment\r\n"
        "[Software\\C]  \r\n"
        "; between a header and its values\n"
        "  \"a\"=dword:00000005 \t \n"
        "\t\n"
        "\"b\" = \"two words\" \n"
        "@=\"default\"\n"
        "\"c\"=hex:01,\\\n"
        "  02,03,\\\r\n"
        "  04\n"
        "\"d\"=float:2.5";
    Document document;
    std::vector<std::string> problems;
    document.Parse(text, &problems);
    CHECK_MSG(problems.empty(), problems.empty() ? "" : problems[0]);
    CHECK(document.keys.size() == 1 && document.keys[0].values.size() == 5);
    const Value* a = Find(document, "Software\\C", "a");
    CHECK(a && a->type == REG_DWORD && a->data == Bytes32(5));
    const Value* b = Find(document, "Software\\C", "b");
    CHECK(b && b->type == REG_SZ && b->data == std::string("two words\0", 10));
    const Value* def = Find(document, "Software\\C", "");
    CHECK(def && def->data == std::string("default\0", 8));
    const Value* c = Find(document, "Software\\C", "c");
    CHECK(c && c->type == REG_BINARY && c->data == std::string("\x01\x02\x03\x04", 4));
    const float two = 2.5f;
    const Value* d = Find(document, "Software\\C", "d");
    CHECK(d && d->type == REG_BINARY && d->data == std::string(reinterpret_cast<const char*>(&two), 4));
    // The written text starts with comments and no more than that before the first key.
    const std::string written = document.Text();
    CHECK(written.rfind("; ", 0) == 0 && written.find("\r\n[Software\\C]\r\n") != std::string::npos);
    // The comment lines are not part of any value: a comment character after a value is part of a string only.
    Document inString;
    inString.Parse("[K]\n\"s\"=\"a ; b\"\n", &problems);
    CHECK(Find(inString, "K", "s") && Find(inString, "K", "s")->data == std::string("a ; b\0", 6));
    // Uppercase hex digits and prefixes read the same.
    Document upper;
    upper.Parse("[K]\n\"x\"=HEX(7):6F,6E,00,00\n\"y\"=DWORD:0000000A\n\"z\"=FLOAT:1.5\n\"w\"=hex(B):0A,0B,0C,0D,0E,0F,10,11\n", &problems);
    CHECK(Find(upper, "K", "x") && Find(upper, "K", "x")->type == REG_MULTI_SZ && Find(upper, "K", "x")->data == std::string("on\0\0", 4));
    CHECK(Find(upper, "K", "y") && Find(upper, "K", "y")->data == Bytes32(10));
    CHECK(Find(upper, "K", "w") && Find(upper, "K", "w")->type == REG_QWORD && Find(upper, "K", "w")->data.size() == 8);
    const float oneAndHalf = 1.5f;
    CHECK(Find(upper, "K", "z") && Find(upper, "K", "z")->data == std::string(reinterpret_cast<const char*>(&oneAndHalf), 4));
    // dword: with fewer digits is the number; with more than eight is a problem.
    Document few;
    few.Parse("[K]\n\"x\"=dword:1\n", &problems);
    CHECK(Find(few, "K", "x") && Find(few, "K", "x")->data == Bytes32(1));
}

void BadLines() {
    const std::string text =
        "\"early\"=dword:00000001\n"            // a value before any section
        "[K]\n"
        "\"a\"=bogus\n"
        "\"b\"=dword:zz\n"
        "\"c\"=dword:123456789\n"               // nine digits
        "\"d\"=hex:0\n"                         // half a byte
        "\"e\"=hex:zz\n"
        "\"f\"=\"unterminated\n"
        "x=dword:00000001\n"                    // a name without quotes
        "\"g\"=hex(zz):01\n"
        "\"h\"=\"text\" trailing\n"
        "\"i\"\n"
        "[unclosed\n"
        "\"ok\"=dword:00000007\n"
        "\"float\"=float:abc\n"
        "\"fine\"=\"yes\"\n";
    Document document;
    std::vector<std::string> problems;
    document.Parse(text, &problems);
    CHECK_MSG(problems.size() == 13, "problems: " + std::to_string(problems.size()));
    CHECK(document.keys.size() == 1);
    CHECK(document.keys[0].values.size() == 2);   // only the good lines
    CHECK(Find(document, "K", "ok") && Find(document, "K", "ok")->data == Bytes32(7));
    CHECK(Find(document, "K", "fine") != nullptr);
    CHECK(!problems.empty() && problems[0].find("line 1") != std::string::npos);
    // Nothing breaks on empty or odd input.
    Document empty;
    empty.Parse("", &problems);
    CHECK(empty.keys.empty());
    empty.Parse("\r\n\r\n;only comments\r\n", &problems);
    CHECK(empty.keys.empty());
    empty.Parse("[]\n\"v\"=dword:1\n", &problems);   // an empty key path
    CHECK(empty.keys.size() == 1 && empty.keys[0].path.empty());
}

void Whole() {
    // A document of every kind of value, in two keys: its text reads back to the same, twice.
    Document document;
    document.Set("Software\\One", "dword", REG_DWORD, Bytes32(0xfeedbeefu));
    document.Set("Software\\One", "string", REG_SZ, std::string("C:\\dir\\\"q\"\0", 12));
    document.Set("Software\\One", "float", REG_BINARY, Bytes32(0x80000000u));
    document.Set("Software\\One", "nan", REG_BINARY, Bytes32(0x7fc00001u));
    document.Set("Software\\One", "multi", REG_MULTI_SZ, std::string("a\0b\0\0", 5));
    document.Set("Software\\One", "", REG_SZ, std::string("d\0", 2));
    document.AddKey("Software\\Two");
    document.Set("Software\\Two\\Deep", "bytes", REG_BINARY, std::string("\x00\xff\x7f", 3));
    std::string first, second;
    Document back = Reread(document, &first);
    Document again = Reread(back, &second);
    CHECK(first == second && SameDocument(document, back) && SameDocument(back, again));
    CHECK(first.find('\r') != std::string::npos && first.find("\n\n") == std::string::npos);
    CHECK(first.size() >= 2 && first.substr(first.size() - 2) == "\r\n");
}

// ---- the defaults ----

// DefaultDocument: the values the game and Rebirth read, under both option trees, at the desktop's size; written and
// read back whole.
void Defaults() {
    Document document = settings_file::DefaultDocument(2560, 1440);
    auto dword = [&](const std::string& path, const char* name, std::uint32_t expected) {
        const Value* value = Find(document, path.c_str(), name);
        CHECK_MSG(value && value->type == REG_DWORD && value->data == Bytes32(expected), path + "\\" + name);
    };
    for (const char* tree : settings_file::kOptionTrees) {
        const std::string root = tree;
        const Value* resolution = Find(document, (root + "\\General Settings").c_str(), "VideoResolution");
        CHECK_MSG(resolution && resolution->type == REG_SZ && resolution->data == std::string("2560x1440\0", 10), root);
        dword(root + "\\General Settings", "FullScreen", 1);
        dword(root + "\\General Settings", "VideoColorDepthInBits", 32);
        dword(root + "\\General Settings", "Audio", 1);
        for (const char* volume : {"AudioStreaming", "AudioSFX", "AudioSpeech"}) dword(root + "\\General Settings", volume, 100);
        dword(root + "\\General Settings", "Filtering", 2);
        dword(root + "\\General Settings", "TextureQuality", 0);
        dword(root + "\\General Settings", "Mipmapping", 1);
        dword(root + "\\Shadow Settings", "ShadowQuality", 2);
        for (const char* shadow : {"HeroShadow", "UnitShadow", "BuildingShadow", "FloraShadow", "OtherShadow"})
            dword(root + "\\Shadow Settings", shadow, 1);
        for (const char* feature : {"EnableWaterWaves", "EnableSelfShadowing", "EnableDynamicLights"})
            dword(root + "\\Terrain Settings", feature, 1);
        dword(root + "\\Terrain Settings", "LightingFreq", 1000);
        // The LOD distances as the game reads its floats: four-byte REG_BINARY.
        const Value* lod1 = Find(document, (root + "\\Terrain Settings").c_str(), "LOD1");
        const Value* lod2 = Find(document, (root + "\\Terrain Settings").c_str(), "LOD2");
        CHECK(lod1 && lod1->type == REG_BINARY && lod1->data == Bytes32(0x42b40000u));   // 90
        CHECK(lod2 && lod2->type == REG_BINARY && lod2->data == Bytes32(0x43020000u));   // 130
        // The install's camera and game settings: floats as four-byte REG_BINARY, the rest as the install has them.
        const Value* rotate = Find(document, (root + "\\Camera Settings").c_str(), "CameraDefaultRotateSpeed");
        CHECK(rotate && rotate->type == REG_BINARY && rotate->data == Bytes32(0x3e19999au));   // 0.15
        const Value* pitch = Find(document, (root + "\\Camera Settings").c_str(), "CameraDefaultPitchSpeed");
        CHECK(pitch && pitch->type == REG_BINARY && pitch->data == Bytes32(0x3ac49ba6u));      // 0.0015
        const Value* shortcut = Find(document, (root + "\\Camera Settings").c_str(), "CameraShortcut 3 Pitch");
        CHECK(shortcut && shortcut->type == REG_BINARY && shortcut->data == Bytes32(0x42340000u));   // 45
        dword(root + "\\Camera Settings", "CameraBorderScrollArea", 1);
        const Value* coefficient = Find(document, (root + "\\Game Specific Settings").c_str(), "ObjectsLightingCoeff");
        CHECK(coefficient && coefficient->type == REG_BINARY && coefficient->data == Bytes32(0x40000000u));   // 2
        dword(root + "\\Game Specific Settings", "Hero Light B", 116);
        dword(root + "\\Game Specific Settings", "Quest Light Radius", 5);
        const Value* master = Find(document, (root + "\\Network Settings").c_str(), "Master Server Name");
        CHECK(master && master->type == REG_SZ && master->data == std::string("a3master.nival.com\0", 19));
        // Left to the game's own defaults: the paths (".\" from the working folder), the tutorial.
        CHECK(document.FindKey(root + "\\Path Settings") == nullptr);
        CHECK(Find(document, (root + "\\Game Specific Settings").c_str(), "ShowTutorial") == nullptr);
    }
    // Rebirth's tree by the name its NivalRegistry patch opens (app_main.cpp: "Gipat.ru"), names without case.
    CHECK(Find(document, "Software\\Gipat.ru\\EI_Starter\\EvilIslands\\General Settings", "VIDEORESOLUTION") != nullptr);
    dword("Software\\Nival Interactive\\EvilIslands\\Standalone", "VSync", 1);
    dword("Software\\Nival Interactive\\EvilIslands\\Standalone", "Msaa", 8);
    dword("Software\\Nival Interactive\\EvilIslands\\Standalone", "Anisotropy", 16);
    CHECK(Find(document, "Software\\Nival Interactive\\EvilIslands\\Standalone", "PerfOverlay") == nullptr);
    dword("Software\\Gipat.Ru\\EI_Starter\\Settings", "WideScreen", 1);
    dword("Software\\Gipat.Ru\\EI_Starter\\Settings", "ObjectsDrawRadius", 5376);
    dword("Software\\Gipat.Ru\\EI_Starter\\Settings", "LandscapeDrawRadius", 5376);
    CHECK(Find(document, "Software\\Gipat.Ru\\EI_Starter\\Settings", "AddonPath") == nullptr);   // no mod selected
    // Written and read back: the same document, the floats as numbers, the text stable.
    std::string first, second;
    Document back = Reread(document, &first);
    CHECK(SameDocument(document, back));
    Reread(back, &second);
    CHECK(first == second && first.find("\"LOD2\"=float:130\r\n") != std::string::npos);
    CHECK(first.find("\"VideoResolution\"=\"2560x1440\"\r\n") != std::string::npos);
    // Another desktop changes the resolution only.
    Document other = settings_file::DefaultDocument(1920, 1080);
    const Value* resolution = Find(other, "Software\\Nival Interactive\\EvilIslands\\General Settings", "VideoResolution");
    CHECK(resolution && resolution->data == std::string("1920x1080\0", 10) && other.keys.size() == document.keys.size());
}

} // namespace

// replay_main.cpp: the number of failed checks.
int RunSettingsFileChecks() {
    g_checks = g_failures = 0;
    Dwords();
    Strings();
    Floats();
    Binaries();
    OtherTypes();
    Names();
    Keys();
    Comments();
    BadLines();
    Whole();
    Defaults();
    std::printf("ei_vectors: settings file: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
