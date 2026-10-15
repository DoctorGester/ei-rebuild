// Checks for the game CRT's formatted text (src/lib/crt_format.cpp, S4a), all against the original.
// - sprintf, vsprintf: the original's _output (006F17D4, not hooked) into a scratch string FILE first,
//   ours into the caller's buffer; the count and the bytes with the terminator.
// - fprintf, printf: the original's _output into scratch and ours (FormatInto) into scratch, the count
//   and the bytes; then ours writes (one fwrite).
// - atof: the original through its trampoline; the bits of the result.
// - A start-up check runs both on a fixed battery: %e / %f / %g / %E / %G at precisions 0..20 with flags
//   and widths on special and pseudo-random doubles, the integer, character and string conversions with
//   their size prefixes, and atof on the formatted numbers and on hand-picked and random digit strings.
// Reached by every scenario (sprintf builds file names and interface text; atof reads the map database,
// scripts and settings); vsprintf through CString::Format; fprintf and printf only in debug paths.
#include "verify.h"

#include "../../src/lib/crt.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using ei::verify::Check;

// The VC6 FILE as _output uses it: a string FILE is _IOWRT | _IOSTRG with _cnt bytes of room.
struct StringFile {
    char* ptr;
    int count;
    char* base;
    int flag;
    int file;
    int charBuffer;
    int bufferSize;
    char* tempName;
};
constexpr int kStringFileFlags = 0x42;

int(__cdecl* const s_output)(StringFile*, const char*, va_list) =
    reinterpret_cast<int(__cdecl*)(StringFile*, const char*, va_list)>(0x006F17D4);

// The original's _output into `room` bytes, then the terminator when it still fits (as _vsnprintf).
int OriginalFormat(char* buffer, std::size_t room, const char* format, va_list args) {
    StringFile file{buffer, static_cast<int>(room), buffer, kStringFileFlags, 0, 0, 0, nullptr};
    const int written = s_output(&file, format, args);
    if (--file.count >= 0) *file.ptr = '\0';
    return written;
}

constexpr std::size_t kRoom = 1 << 16;

std::vector<char>& Scratch(int which) {
    thread_local std::vector<char> scratch[2] = {std::vector<char>(kRoom + 1), std::vector<char>(kRoom + 1)};
    return scratch[which];
}

// Ours already formatted `actual` (count `actualCount`); the original into scratch must match.
void CompareText(const char* name, const char* format, va_list args, const char* actual, int actualCount) {
    char* expected = Scratch(0).data();
    const int expectedCount = OriginalFormat(expected, kRoom, format, args);
    if (expectedCount < 0) return;   // longer than the scratch: not compared
    const bool same = expectedCount == actualCount && std::memcmp(expected, actual, static_cast<std::size_t>(actualCount)) == 0;
    Check(name, true, same, "\"%.60s\": %d \"%.80s\", ours %d \"%.80s\"", format, expectedCount, expected, actualCount,
          actualCount >= 0 ? actual : "");
}

int(__cdecl* s_unusedSprintf)(char*, const char*, ...);
int __cdecl SprintfVerify(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char* expected = Scratch(1).data();
    const int expectedCount = OriginalFormat(expected, kRoom, format, args);
    const int actual = libcrt::Vsprintf(buffer, format, args);
    if (expectedCount >= 0) {
        Check("sprintf", true,
              expectedCount == actual && std::memcmp(expected, buffer, static_cast<std::size_t>(actual) + 1) == 0,
              "\"%.60s\": %d \"%.80s\", ours %d \"%.80s\"", format, expectedCount, expected, actual, buffer);
    }
    va_end(args);
    return actual;
}
EI_VERIFY(0x006E8715, SprintfVerify, 0, s_unusedSprintf);

int(__cdecl* s_unusedVsprintf)(char*, const char*, va_list);
int __cdecl VsprintfVerify(char* buffer, const char* format, va_list args) {
    char* expected = Scratch(1).data();
    const int expectedCount = OriginalFormat(expected, kRoom, format, args);
    const int actual = libcrt::Vsprintf(buffer, format, args);
    if (expectedCount >= 0) {
        Check("vsprintf", true,
              expectedCount == actual && std::memcmp(expected, buffer, static_cast<std::size_t>(actual) + 1) == 0,
              "\"%.60s\": %d \"%.80s\", ours %d \"%.80s\"", format, expectedCount, expected, actual, buffer);
    }
    return actual;
}
EI_VERIFY(0x006ECC47, VsprintfVerify, 0, s_unusedVsprintf);

void CompareStream(const char* name, const char* format, va_list args) {
    char* actual = Scratch(1).data();
    const int count = libcrt::FormatInto(actual, kRoom, format, args);
    if (count >= 0) CompareText(name, format, args, actual, count);
}

int(__cdecl* s_unusedFprintf)(gamecrt::CrtFile*, const char*, ...);
int __cdecl FprintfVerify(gamecrt::CrtFile* file, const char* format, ...) {
    va_list args;
    va_start(args, format);
    CompareStream("fprintf", format, args);
    const int written = libcrt::Vfprintf(file, format, args);
    va_end(args);
    return written;
}
EI_VERIFY(0x006EAEB0, FprintfVerify, 0, s_unusedFprintf);

int(__cdecl* s_unusedPrintf)(const char*, ...);
int __cdecl PrintfVerify(const char* format, ...) {
    va_list args;
    va_start(args, format);
    CompareStream("printf", format, args);
    const int written = libcrt::Vfprintf(libcrt::StandardOutput(), format, args);
    va_end(args);
    return written;
}
EI_VERIFY(0x006E955F, PrintfVerify, 0, s_unusedPrintf);

std::uint64_t Bits(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

double(__cdecl* s_atof)(const char*);
double __cdecl AtofVerify(const char* text) {
    const double expected = s_atof(text);
    const double actual = libcrt::Atof(text);
    Check("atof", Bits(expected), Bits(actual), "\"%.60s\": %016llX, ours %016llX", text, Bits(expected), Bits(actual));
    return actual;
}
// 006E911A: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes).
EI_VERIFY(0x006E911A, AtofVerify, 6, s_atof);

// ---- Start-up battery ----

void Both(const char* name, const char* format, ...) {
    va_list args;
    va_start(args, format);
    CompareStream(name, format, args);
    va_end(args);
}

double FromBits(std::uint64_t bits) {
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

struct Lcg {
    std::uint64_t state;
    std::uint32_t Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 32);
    }
};

std::vector<double> TestDoubles() {
    std::vector<double> values = {0.0, 1.0, 0.5, 1.5, 2.5, 0.1, 0.2, 0.3, 1.0 / 3, 2.0 / 3, 1e-5, 9.9999e-5, 1e-4, 0.05,
                                  0.15, 0.25, 0.35, 0.45, 0.95, 9.5, 99.5, 999.5, 0.0005, 0.00049999999999999999,
                                  123456789.0, 1e15, 1e16, 1e17, 1e21, 1e22, 1e23, 1e100, 1e-100, 1e300, 1e-300,
                                  3.14159265358979, 2.718281828459045, 9.999999999999999, 99999999999999.99,
                                  0.000123456789, 65535.0, 4294967295.0, 1.0e-7, 5e-7, 4.5, 5.5, 1234.5678, 0.9999995,
                                  0.99999949, 9.9999995e10};
    // The ends of the range and the special values, by their bits.
    for (std::uint64_t bits : {0x8000000000000000ull, 0x7FEFFFFFFFFFFFFFull, 0x0010000000000000ull, 0x000FFFFFFFFFFFFFull,
                               0x0000000000000001ull, 0x0000000000000123ull, 0x7FF0000000000000ull, 0xFFF0000000000000ull,
                               0x7FF8000000000000ull, 0xFFF8000000000000ull, 0x7FF4000000000000ull, 0x7FF8000000000001ull,
                               0xFFF8000000000001ull, 0x3FF0000000000001ull, 0x3FEFFFFFFFFFFFFFull, 0x433FFFFFFFFFFFFFull,
                               0x4340000000000001ull})
        values.push_back(FromBits(bits));
    const std::size_t fixed = values.size();
    for (std::size_t i = 0; i < fixed; ++i) values.push_back(-values[i]);
    Lcg random{12345};
    for (int i = 0; i < 1500; ++i) {
        // Any finite double, and numbers near the interface's range.
        std::uint64_t bits = static_cast<std::uint64_t>(random.Next()) << 32 | random.Next();
        if ((bits >> 52 & 0x7FF) == 0x7FF) bits ^= 0x4000000000000000ull;
        values.push_back(FromBits(bits));
        values.push_back(static_cast<double>(static_cast<int>(random.Next() % 2000001) - 1000000) / 1000.0);
        values.push_back(static_cast<double>(random.Next()) * 1e-6 * ((random.Next() & 1) ? 1 : -1));
        const int exponent = static_cast<int>(random.Next() % 80) - 40;
        double scale = 1.0;
        for (int e = 0; e < (exponent < 0 ? -exponent : exponent); ++e) scale *= 10.0;
        const double mantissa = static_cast<double>(random.Next() % 100000) / 100.0;
        values.push_back(exponent < 0 ? mantissa / scale : mantissa * scale);
    }
    return values;
}

void FloatBattery(const std::vector<double>& values) {
    static const char* const kFormats[] = {
        "%f",     "%.0f",   "%.1f",   "%.2f",    "%.3f",    "%.4f",     "%.6f",   "%.9f",   "%.10f",   "%.15f",
        "%.17f",  "%.20f",  "%e",     "%.0e",    "%.1e",    "%.3e",     "%.8e",   "%.14e",  "%.16e",   "%.17e",
        "%.20e",  "%E",     "%g",     "%.0g",    "%.1g",    "%.2g",     "%.3g",   "%.5g",   "%.10g",   "%.15g",
        "%.16g",  "%.17g",  "%.18g",  "%.20g",   "%G",      "%.3G",     "%#g",    "%#.0f",  "%#.0e",   "%#.3g",
        "%#.0g",  "%#.1g",  "%+f",    "% f",     "%010.3f", "%-12.4e|", "%12g|",  "%+08.2f", "%-+9.1f|", "% 012.5g",
        "%5.1f%%", "x=%7.3f|", "%.100f", "%.150e", "%.120g", "%#12.4G|", "%*.*f", "%-*.*e|",
    };
    for (const char* format : kFormats) {
        const bool stars = std::strchr(format, '*') != nullptr;
        for (double value : values) {
            if (stars) {
                Both("sprintf start-up", format, 14, 5, value);
            } else {
                Both("sprintf start-up", format, value);
            }
        }
    }
}

void IntegerBattery() {
    static const int kInts[] = {0, 1, -1, 7, 10, 255, 256, -255, 65535, 65536, -32768, 32767, 2147483647, -2147483647 - 1,
                                123456789, -987654321, 0x7F, 0x80, 0xFF};
    static const char* const kFormats[] = {
        "%d",    "%i",   "%u",    "%x",    "%X",    "%o",    "%#x",   "%#X",  "%#o",   "%p",   "%c",   "%05d",
        "%+d",   "% d",  "%-6d|", "%hd",   "%hu",   "%hx",   "%ld",   "%lu",  "%.0d",  "%.5d", "%5.3d", "%-8.3x|",
        "%08X",  "%#08x", "%#.0o", "%+.0d", "%10u|", "%-+5d|", "%Fd",  "%Nd",  "%Ld",   "%y",   "%I32d", "%5",
        "%-",    "%.d",   "%#5o|", "%+05i", "%hc",  "%lc",   "%C",    "%hC",  "%wc",
    };
    for (const char* format : kFormats) {
        for (int value : kInts) Both("sprintf start-up", format, value);
    }
    static const long long kInt64s[] = {0, 1, -1, 4294967296ll, -4294967296ll, 9223372036854775807ll,
                                        -9223372036854775807ll - 1, 1234567890123456789ll};
    for (const char* format : {"%I64d", "%I64u", "%I64x", "%I64X", "%#I64o", "%-24I64d|", "%025I64d", "%.30I64u"}) {
        for (long long value : kInt64s) Both("sprintf start-up", format, value);
    }
    for (int width : {-12, -1, 0, 3, 20}) {
        Both("sprintf start-up", "%*d|%-*s|", width, 42, width, "ab");
        Both("sprintf start-up", "%.*s|%.*d", width, "abcdef", width, 7);
    }
    int counted = 0;
    short countedShort = 0;
    Both("sprintf start-up", "abc%n%hn def", &counted, &countedShort);
}

void TextBattery() {
    static const char* const kStrings[] = {"", "a", "hello", "Evil Islands", "\xC0\xE1\xE2", nullptr};
    for (const char* format : {"%s", "%10s|", "%-10s|", "%.3s", "%5.2s|", "%hs", "[%s]", "%%%s%%"}) {
        for (const char* s : kStrings) Both("sprintf start-up", format, s);
    }
    static const wchar_t* const kWide[] = {L"", L"abc", L"\x0416\x0438\x0437\x043D\x044C", L"x\x00FF", nullptr};
    for (const char* format : {"%S", "%ls", "%ws", "%-8S|", "%.2ls"}) {
        for (const wchar_t* s : kWide) Both("sprintf start-up", format, s);
    }
    struct CountedString {
        short length;
        short maximum;
        const void* buffer;
    };
    const CountedString narrow{5, 6, "hello"};
    const CountedString wide{6, 8, L"abc"};
    const CountedString empty{0, 0, nullptr};
    Both("sprintf start-up", "%Z|%Z|%wZ|%wZ", &narrow, &empty, &wide, &empty);
    Both("sprintf start-up", "%Z", static_cast<const CountedString*>(nullptr));
    Both("sprintf start-up", "%d %s %.2f %c %x %e %s", 5, "x", 2.345, 'q', 0xBEEF, 1e-9, "end");
    Both("sprintf start-up", "%I64", 1);
    Both("sprintf start-up", "%I6x", 1);
}

void AtofOne(const char* text) {
    const double expected = s_atof(text);
    const double actual = libcrt::Atof(text);
    Check("atof start-up", Bits(expected), Bits(actual), "\"%.60s\": %016llX, ours %016llX", text, Bits(expected),
          Bits(actual));
}

void AtofBattery(const std::vector<double>& values) {
    // Record mode (EI_VECTORS=1, tests/vectors/record_crt_format.cpp): the recorder is the only hook at
    // 006E911A, so this wrapper has no trampoline to the original.
    if (!s_atof) return;
    static const char* const kTexts[] = {
        "", " ", "abc", "1", "-1", "+1", ".5", "-.5", "+.5e+3", "1.", "1e10", "1E-10", "1d5", "1D+5", "1e", "1e+", "1e-",
        "  \t\n12", "\r\n-7.25x", "0x1A", "1e5000", "-1e5000", "1e-5000", "1e-400", "1e-320", "1e308", "2e308",
        "4.9406564584124654e-324", "2.4703282292062327e-324", "2.4703282292062328e-324", "1.7976931348623157e308",
        "1.7976931348623158e308", "1.7976931348623159e308", "123456789012345678901234567890",
        "0.000000000000000000000000000001234567890123456789012345", "9007199254740993", "9007199254740992.5",
        "1.0000000000000000000000001", "99999999999999999999999999", "999999999999999999999994",
        "999999999999999999999995", "0.1", "0.2", "0.3", "1.15", "-0", "-0.0e5", "000000000000000000123", "0.000",
        "1e0005", "1e-0005", "5e-324", "2.2250738585072011e-308", "2.2250738585072014e-308", "1.5.5", "--1", "+-1",
        "1e+-5", "12345678901234567890123456789e-10", "3.402823466e+38", "1.175494351e-38", "0.30000000000000004",
        "1234567.8901234567890123456", "7.9e-5", "6.02214076e23", "-", "+", ".", "e5", "\xA0" "5",
    };
    for (const char* text : kTexts) AtofOne(text);
    char text[512];
    for (double value : values) {
        for (const char* format : {"%.17g", "%.25e", "%.30f", "%g", "%.3f"}) {
            if (libcrt::Sprintf(text, format, value) < 400) AtofOne(text);
        }
    }
    Lcg random{777};
    for (int i = 0; i < 3000; ++i) {
        char* p = text;
        if (random.Next() & 1) *p++ = '-';
        const int digits = 1 + static_cast<int>(random.Next() % 32);
        const int point = static_cast<int>(random.Next() % (digits + 2));
        for (int d = 0; d < digits; ++d) {
            if (d == point) *p++ = '.';
            *p++ = static_cast<char>('0' + random.Next() % 10);
        }
        if (random.Next() % 3 == 0) {
            p += std::snprintf(p, 16, "e%d", static_cast<int>(random.Next() % 700) - 350);
        }
        *p = '\0';
        AtofOne(text);
    }
}

void CrtFormatStartupCheck() {
    const std::vector<double> values = TestDoubles();
    FloatBattery(values);
    IntegerBattery();
    TextBattery();
    AtofBattery(values);
}
EI_STARTUP_CHECK(CrtFormatStartupCheck);

} // namespace
