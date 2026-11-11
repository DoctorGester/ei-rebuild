// std::string helpers with CString's answers (str.h). The bodies follow MFC 4.2's (strcore.cpp, strex.cpp).
#include "str.h"

#include "text_case.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace str {

int Compare(std::string_view a, std::string_view b) {
    return a.substr(0, a.find('\0')).compare(b.substr(0, b.find('\0')));
}

bool MbsLess::operator()(const std::string& a, const std::string& b) const { return Compare(a, b) < 0; }

namespace {

// The byte searches stand for the multibyte CRT's _mbschr / _mbsstr / _mbsrchr / _mbspbrk / _ismbcspace, which the
// text (UTF-8, no lead bytes) made the same: the char's byte is found (_tcschr's sign-extended char too), the white
// space is ASCII's. Each looks at the text up to its first zero byte, as the C functions did.
int Length(const std::string& s) { return static_cast<int>(s.size()); }
std::string_view UpToNull(std::string_view text) { return text.substr(0, text.find('\0')); }
int Position(std::size_t found, int start = 0) {
    return found == std::string_view::npos ? -1 : static_cast<int>(found) + start;
}

// The text cut at index `last` (TrimRight's end), when there is one.
void CutAt(std::string& s, std::size_t last) {
    if (last != std::string_view::npos) s.resize(last);
}

// TrimRight: the start of the last run of characters that match, in the text up to its first zero byte.
template <class Match>
std::size_t LastRun(const std::string& s, Match match) {
    const std::string_view text = UpToNull(s);
    std::size_t last = std::string_view::npos;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (match(text[i])) {
            if (last == std::string_view::npos) last = i;
        } else {
            last = std::string_view::npos;
        }
    }
    return last;
}

bool IsSign(char c) { return c == '+' || c == '-'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

} // namespace

std::string Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::string text = FormatV(format, arguments);
    va_end(arguments);
    return text;
}

// strchr on the text from `start`: a zero byte is found too (at the end of the text).
int Find(const std::string& s, char c, int start) {
    if (start >= Length(s)) return -1;
    const std::string_view text = UpToNull(std::string_view(s).substr(static_cast<std::size_t>(start)));
    return c == '\0' ? start + static_cast<int>(text.size()) : Position(text.find(c), start);
}

int Find(const std::string& s, std::string_view sub, int start) {
    if (start > Length(s)) return -1;
    const std::string_view text = UpToNull(std::string_view(s).substr(static_cast<std::size_t>(start)));
    return Position(text.find(UpToNull(sub)), start);
}

int ReverseFind(const std::string& s, char c) {
    const std::string_view text = UpToNull(s);
    return c == '\0' ? static_cast<int>(text.size()) : Position(text.rfind(c));
}

int FindOneOf(const std::string& s, std::string_view set) { return Position(UpToNull(s).find_first_of(UpToNull(set))); }

int ToInt(std::string_view text) { return ParseInt(text).value_or(0); }

std::optional<int> ParseInt(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && text::IsSpace(text[i])) ++i;
    const bool negative = i < text.size() && text[i] == '-';
    if (i < text.size() && IsSign(text[i])) ++i;
    if (i == text.size() || !IsDigit(text[i])) return std::nullopt;
    // The digits as far as they go; a number that overflows 64 bits is far beyond the int range too.
    std::uint64_t magnitude = 0;
    std::size_t end = i;
    while (end < text.size() && IsDigit(text[end])) ++end;
    const auto read = std::from_chars(text.data() + i, text.data() + end, magnitude);
    if (read.ec == std::errc::result_out_of_range) magnitude = UINT64_MAX;
    constexpr std::uint64_t kMost = INT32_MAX;
    if (negative) return magnitude > kMost ? INT32_MIN : -static_cast<int>(magnitude);
    return magnitude > kMost ? INT32_MAX : static_cast<int>(magnitude);
}

namespace {

template <typename Number>
std::from_chars_result FromChars(std::string_view text, Number& value, std::chars_format format) {
    const char* first = text.data();
#if defined(__cpp_lib_to_chars)
    return std::from_chars(first, first + text.size(), value, format);
#else
    const std::from_chars_result none{first, std::errc::invalid_argument};
    const std::size_t sign = !text.empty() && text.front() == '-' ? 1 : 0;
    if (text.size() == sign || text[sign] == '+' || text[sign] == '-' || text::IsSpace(text[sign])) return none;
    const bool hex = format == std::chars_format::hex;
    // strtof takes a "0x" as hexadecimal: the hex format's digits get one, the general format stops at its "x".
    if (!hex && text.size() > sign + 1 && text[sign] == '0' && (text[sign + 1] == 'x' || text[sign + 1] == 'X')) {
        value = sign ? -Number(0) : Number(0);
        return {first + sign + 1, std::errc()};
    }
    const std::string prefix = hex ? "0x" : "";
    const std::string terminated = std::string(text.substr(0, sign)) + prefix + std::string(text.substr(sign));
    char* end = nullptr;
    errno = 0;
    const Number number = std::is_same_v<Number, float> ? std::strtof(terminated.c_str(), &end)
                                                       : std::strtod(terminated.c_str(), &end);
    if (end == terminated.c_str() || (hex && end <= terminated.c_str() + sign + prefix.size())) return none;
    const char* ptr = first + (end - terminated.c_str()) - prefix.size();
    // Out of range is infinite or zero (value untouched, as from_chars leaves it); subnormals are in range.
    if (errno == ERANGE && (number == Number(0) || std::isinf(number))) return {ptr, std::errc::result_out_of_range};
    value = number;
    return {ptr, std::errc()};
#endif
}

}   // namespace

std::from_chars_result FloatFromChars(std::string_view text, float& value, std::chars_format format) {
    return FromChars(text, value, format);
}

std::from_chars_result FloatFromChars(std::string_view text, double& value, std::chars_format format) {
    return FromChars(text, value, format);
}

float ToFloat(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && text::IsSpace(text[i])) ++i;
    // One sign, which from_chars gets without (it takes no plus sign); a second sign makes no number.
    const bool negative = i < text.size() && text[i] == '-';
    if (i < text.size() && IsSign(text[i])) ++i;
    if (i < text.size() && IsSign(text[i])) return 0.0f;
    text = UpToNull(text.substr(i));
    // strtof reads a hexadecimal float after "0x" (from_chars wants the digits alone, in the hex format).
    const auto isHexDigit = [](char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
                     (isHexDigit(text[2]) || text[2] == '.');
    if (hex) text.remove_prefix(2);
    float number = 0.0f;
    const auto read = FloatFromChars(text, number, hex ? std::chars_format::hex : std::chars_format::general);
    if (read.ec == std::errc::result_out_of_range) {
        // Too large is infinite, too small is zero, as strtof answered: a negative exponent says which.
        const std::size_t exponent = text.find_first_of(hex ? "pP" : "eE");
        const bool tooSmall = exponent != std::string_view::npos && exponent + 1 < text.size() && text[exponent + 1] == '-';
        number = tooSmall ? 0.0f : std::numeric_limits<float>::infinity();
    } else if (read.ec != std::errc()) {
        return 0.0f;   // no number: zero, whatever the sign
    }
    return negative ? -number : number;
}

std::string Mid(const std::string& s, int first) { return Mid(s, first, Length(s) - first); }

std::string Mid(const std::string& s, int first, int count) {
    if (first < 0) first = 0;
    if (count < 0) count = 0;
    const int length = Length(s);
    if (first + count > length) count = length - first;
    if (first > length) count = 0;
    if (count <= 0) return std::string();
    return s.substr(static_cast<std::size_t>(first), static_cast<std::size_t>(count));
}

std::string Left(const std::string& s, int count) {
    if (count < 0) count = 0;
    if (count >= Length(s)) return s;
    return s.substr(0, static_cast<std::size_t>(count));
}

std::string Right(const std::string& s, int count) {
    if (count < 0) count = 0;
    const int length = Length(s);
    if (count >= length) return s;
    return s.substr(static_cast<std::size_t>(length - count));
}

void MakeLower(std::string& s) { text::LowerInPlace(s.data()); }

int Remove(std::string& s, char c) {
    char* source = s.data();
    char* dest = s.data();
    char* end = s.data() + s.size();
    while (source < end) {
        if (*source != c) {
            *dest = *source;
            ++dest;
        }
        ++source;
    }
    const int count = static_cast<int>(source - dest);
    s.resize(s.size() - static_cast<std::size_t>(count));
    return count;
}

void TrimRight(std::string& s) {
    CutAt(s, LastRun(s, [](char c) { return text::IsSpace(c); }));
}

void TrimRight(std::string& s, char target) {
    CutAt(s, LastRun(s, [target](char c) { return c == target; }));
}

void TrimRight(std::string& s, std::string_view targets) {
    targets = UpToNull(targets);
    CutAt(s, LastRun(s, [targets](char c) { return targets.find(c) != std::string_view::npos; }));
}

void TrimLeft(std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && text::IsSpace(s[i])) ++i;
    s.erase(0, i);
}

void TrimLeft(std::string& s, char target) {
    std::size_t i = 0;
    while (i < s.size() && s[i] == target) ++i;
    s.erase(0, i);
}

std::vector<std::string> Lines(std::string_view text) {
    std::vector<std::string> lines;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (end != std::string_view::npos && line.ends_with('\r')) line.remove_suffix(1);
        lines.emplace_back(line);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    }
    return lines;
}

// The text up to its first zero byte (find answers npos, the whole text, when there is none).
void CutAtNull(std::string& s) { s.resize(std::min(s.find('\0'), s.size())); }

char* SetLength(std::string& s, int length) {
    s.resize(static_cast<std::size_t>(length));
    return s.data();
}

} // namespace str
