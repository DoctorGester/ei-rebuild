// str::BoundedLength and str::CopyInto (src/lib/str.h): text read from a packet or a file stops at its zero byte or
// at the end of the bytes there are, and text copied into a fixed buffer is cut to fit with its terminator. Also
// libprintf::Arg: a std::string and a std::string_view (not zero-terminated) for a %s. And str's number readers and
// searches against the C functions they replaced (atoi, strtof, strchr, strstr, strrchr, strpbrk, strcmp).
#include "../../src/lib/str.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void Check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("ei_unit_check: bounded text: %s\n", what);
}

// A random text of `length` characters from `alphabet`.
std::string Random(std::mt19937& random, const char* alphabet, int length) {
    const std::size_t count = std::strlen(alphabet);
    std::string text;
    for (int i = 0; i < length; ++i) text += alphabet[random() % count];
    return text;
}

// str::ToInt, str::ToFloat, Find, ReverseFind, FindOneOf, TrimRight, CutAtNull and MbsLess: the same answers as the C
// functions on texts of every shape (numbers with signs, spaces, exponents, junk; searches in short texts).
void CheckAgainstC() {
    std::mt19937 random(12345);
    const char* numbers[] = {"", " ", "-", "+", "+-5", "-+5", "12abc", "  42", "\t-7x", "2147483647", "2147483648",
                             "-2147483648", "-2147483649", "99999999999999999999999", "1.5", "-.5", ".", "1e5", "1e",
                             "1e-50", "1e50", "1e-400", "1e400", "-1e400", "0x10", "-0x1.8p1", "0x", "0xg", "inf", "-inf",
                             "+1.25", "  3.25abc", "0.1", "123456789.123456789", "3.4028235e38", "3.5e38",
                             "1.17549435e-38", "1e-45", "-0", "1,5", "5."};
    std::vector<std::string> texts(std::begin(numbers), std::end(numbers));
    for (int i = 0; i < 20000; ++i)
        texts.push_back(Random(random, "0123456789.-+eE \tx", 1 + static_cast<int>(random() % 12)));
    for (int i = 0; i < 20000; ++i) {
        // Printed floats: the digits a settings file or a script holds.
        char printed[64];
        const float value = static_cast<float>(static_cast<std::int32_t>(random())) / static_cast<float>(1 + random() % 100000);
        std::snprintf(printed, sizeof(printed), i % 2 ? "%f" : "%g", static_cast<double>(value));
        texts.push_back(printed);
    }
    int badInt = 0, badFloat = 0;
    std::string firstInt, firstFloat;
    for (const std::string& text : texts) {
        // atoi's answer: strtol's, clamped to the int range (the Windows CRT's long is that big already).
        const long wide = std::strtol(text.c_str(), nullptr, 10);
        const long clamped = wide > INT32_MAX ? INT32_MAX : wide < INT32_MIN ? INT32_MIN : wide;
        if (str::ToInt(text) != static_cast<int>(clamped) && ++badInt == 1) firstInt = text;
        const float expected = std::strtof(text.c_str(), nullptr);
        const float got = str::ToFloat(text);
        if (std::memcmp(&expected, &got, sizeof(float)) != 0 && ++badFloat == 1) firstFloat = text;
    }
    Check(badInt == 0, ("ToInt differs from atoi, first on \"" + firstInt + "\"").c_str());
    Check(badFloat == 0, ("ToFloat differs from strtof, first on \"" + firstFloat + "\"").c_str());
    Check(!str::ParseInt("abc") && !str::ParseInt("") && !str::ParseInt("-") && str::ParseInt(" -12x") == -12,
          "ParseInt says none without digits");

    int badSearch = 0;
    for (int i = 0; i < 20000; ++i) {
        const std::string s = Random(random, "ab.\n \r", static_cast<int>(random() % 8));
        const std::string sub = Random(random, "ab.", static_cast<int>(random() % 3));
        const char c = "ab.\n"[random() % 4];
        const int start = static_cast<int>(random() % 9);
        auto offset = [&](const char* found) { return found ? static_cast<int>(found - s.c_str()) : -1; };
        const int findChar = start >= static_cast<int>(s.size()) ? -1 : offset(std::strchr(s.c_str() + start, c));
        const int findSub = start > static_cast<int>(s.size()) ? -1 : offset(std::strstr(s.c_str() + start, sub.c_str()));
        badSearch += str::Find(s, c, start) != findChar;
        badSearch += str::Find(s, sub, start) != findSub;
        badSearch += str::ReverseFind(s, c) != offset(std::strrchr(s.c_str(), c));
        badSearch += str::FindOneOf(s, sub) != offset(std::strpbrk(s.c_str(), sub.c_str()));
        badSearch += (str::Compare(s, sub) < 0) != (std::strcmp(s.c_str(), sub.c_str()) < 0);
        badSearch += str::MbsLess{}(s, sub) != (std::strcmp(s.c_str(), sub.c_str()) < 0);
        // TrimRight: from the start of the last run of `sub`'s characters, when it reaches the end.
        std::string trimmed = s;
        str::TrimRight(trimmed, sub);
        std::string expected = s;
        while (!expected.empty() && !sub.empty() && sub.find(expected.back()) != std::string::npos) expected.pop_back();
        badSearch += trimmed != expected;
    }
    Check(badSearch == 0, "Find / ReverseFind / FindOneOf / Compare / TrimRight differ from the C functions");

    // Zero bytes inside a text: the searches and CutAtNull stop at the first.
    std::string zero("ab\0cd", 5);
    Check(str::Find(zero, 'c') == -1 && str::Find(zero, '\0') == 2 && str::ReverseFind(zero, 'a') == 0,
          "searches stop at a zero byte");
    str::CutAtNull(zero);
    Check(zero == "ab", "CutAtNull cuts at the zero byte");
    std::string plain = "abc";
    str::CutAtNull(plain);
    Check(plain == "abc", "CutAtNull keeps a text without one");
}

} // namespace

int RunBoundedTextChecks() {
    CheckAgainstC();
    // A terminated text, one without a terminator in its room, an empty room, a zero first byte.
    const char packet[] = {'a', 'b', 'c', '\0', 'd', 'e'};
    Check(str::BoundedLength(packet, sizeof(packet)) == 3, "length up to the zero byte");
    Check(str::BoundedLength(packet + 4, 2) == 2, "no zero byte: the whole room");
    Check(str::BoundedLength(packet, 2) == 2, "zero byte past the room");
    Check(str::BoundedLength(packet, 0) == 0, "empty room");
    Check(str::BoundedLength(packet + 3, 3) == 0, "zero first byte");

    char name[8];
    std::memset(name, 'x', sizeof(name));
    str::CopyInto(name, "short");
    Check(std::strcmp(name, "short") == 0, "a text that fits is copied whole");
    str::CopyInto(name, std::string(300, 'n'));
    Check(std::strlen(name) == 7 && name[7] == '\0', "a long text is cut with its terminator");
    str::CopyInto(name, std::string("ab\0cd", 5));
    Check(std::strcmp(name, "ab") == 0, "a text stops at its zero byte");
    str::CopyInto(name, "");
    Check(name[0] == '\0', "an empty text");
    char one[1] = {'x'};
    str::CopyInto(one, "abc");
    Check(one[0] == '\0', "a one-byte buffer holds the terminator");

    // A std::string_view cut out of a longer text, handed to a printf-style function for a %s: only its own bytes.
    const std::string whole = "abcdef";
    const std::string_view view(whole.data() + 1, 3);
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%s:%s", 5, libprintf::Arg(libprintf::Owned(whole)),
                  libprintf::Arg(libprintf::Owned(view)));
    Check(std::strcmp(text, "5:abcdef:bcd") == 0, "a std::string_view is handed on by its length");

    std::printf("ei_unit_check: bounded text: %d checks, %d failed\n", checks, failures);
    return failures;
}
