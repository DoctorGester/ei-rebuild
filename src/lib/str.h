#pragma once

// The game's text as std::string: what MFC's CString did that std::string has no member for, with
// CString's answers (the game's code was written against them).
//   - Find / ReverseFind / FindOneOf return an int position, -1 when absent (strchr, strstr, strrchr, strpbrk on the
//     text up to its first zero byte: CString's _mbschr ... on UTF-8, which has no lead bytes).
//   - Mid / Left / Right clamp a start or count outside the text instead of throwing.
//   - Format prints with the game CRT's printf (crt_format.cpp: VC6's float digits, "1.#INF", three-digit
//     exponents; the rest the host's) and keeps the text up to the first zero byte.
//   - MakeLower, Remove and the trims step through the text a byte at a time; the white space of TrimLeft /
//     TrimRight is ASCII's (text::IsSpace, what _ismbcspace answered).
//   - Buffer / CutAtNull / SetLength stand for GetBuffer / ReleaseBuffer(-1) / GetBufferSetLength: the
//     characters written into the buffer, then the length up to the terminator.
// The usual operations are std::string's own: size(), empty(), clear(), c_str(), +, +=, ==.

#include "printf_args.h"

#include <charconv>
#include <cstdarg>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace str {

// The texts compared as strcmp compares them, each up to its first zero byte (CString's Compare): below, at or
// above zero.
int Compare(std::string_view a, std::string_view b);

// The order of the maps that had MFC CString keys: _mbscmp below zero, on the text up to its first zero byte.
struct MbsLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

// The hash of std::unordered_map's std::string keys that also takes a std::string_view (lookups without a copy;
// with std::equal_to<>). The same values as std::hash<std::string>, so the maps keep their order.
struct ViewHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

// What std::atoi / std::strtof answer for the text: leading white space, an optional sign, the number up to the first
// character that is not part of it (0 when there is none); an int out of range clamps to the int range, a float out of
// range is infinite (too large) or zero (too small).
int ToInt(std::string_view text);
// The same number, none (nullopt) when the text holds no digits.
std::optional<int> ParseInt(std::string_view text);
float ToFloat(std::string_view text);
// std::from_chars for a float or double, also where the library has none (Apple's libc++): there strtof or strtod in
// the C locale the game keeps, refusing what from_chars refuses (a leading space or plus; in the general format the "x" of a "0x").
std::from_chars_result FloatFromChars(std::string_view text, float& value,
                                      std::chars_format format = std::chars_format::general);
std::from_chars_result FloatFromChars(std::string_view text, double& value,
                                      std::chars_format format = std::chars_format::general);

EI_PRINTF_FORMAT(1, 2) std::string Format(const char* format, ...);
// With a std::string among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasClass<Args...>
std::string Format(const char* format, Args&&... args) {
    return Format(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}
// A std::string format.
template <class F, class... Args>
    requires libprintf::IsString<F>
std::string Format(F&& format, Args&&... args) {
    return Format(format.c_str(), libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}
EI_PRINTF_FORMAT(1, 0) std::string FormatV(const char* format, va_list arguments);

int Find(const std::string& s, char c, int start = 0);
int Find(const std::string& s, std::string_view sub, int start = 0);
int ReverseFind(const std::string& s, char c);
int FindOneOf(const std::string& s, std::string_view set);

std::string Mid(const std::string& s, int first);
std::string Mid(const std::string& s, int first, int count);
std::string Left(const std::string& s, int count);
std::string Right(const std::string& s, int count);

void MakeLower(std::string& s);
// Removes every `c`; returns how many.
int Remove(std::string& s, char c);
void TrimRight(std::string& s);
void TrimRight(std::string& s, char target);
void TrimRight(std::string& s, std::string_view targets);
void TrimLeft(std::string& s);
void TrimLeft(std::string& s, char target);

// The lines of a text file's bytes as a text-mode read gave them: cut at each '\n', a '\r' before it dropped; a
// final '\n' starts no empty last line.
std::vector<std::string> Lines(std::string_view text);

// Room for at least `minLength` characters (the text kept); the characters to write into.
// The text up to its first zero byte (after writing into Buffer).
void CutAtNull(std::string& s);
// The text resized to `length` characters; the characters to write into.
char* SetLength(std::string& s, int length);

// The length of the zero-terminated text at `text`, at most `room` bytes (the whole room when no zero byte is in
// it): text from a file or a packet, where nothing promises the terminator.
inline std::size_t BoundedLength(const char* text, std::size_t room) {
    const std::size_t zero = std::string_view(text, room).find('\0');
    return zero == std::string_view::npos ? room : zero;
}

// `text` up to its first zero byte copied into the fixed buffer `to` of `size` bytes (size > 0), cut to fit with
// its terminator.
inline void CopyInto(char* to, std::size_t size, std::string_view text) {
    const std::size_t length = BoundedLength(text.data(), text.size() < size ? text.size() : size - 1);
    text.copy(to, length);
    to[length] = '\0';
}
template <std::size_t N>
void CopyInto(char (&to)[N], std::string_view text) {
    CopyInto(to, N, text);
}

} // namespace str
