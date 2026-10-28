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

#include <cstdarg>
#include <cstdlib>
#include <string>
#include <string_view>

namespace str {

// The texts compared as strcmp compares them, each up to its first zero byte (CString's Compare): below, at or
// above zero.
int Compare(std::string_view a, std::string_view b);

// The order of the maps that had MFC CString keys: _mbscmp below zero, on the text up to its first zero byte.
struct MbsLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

// std::atoi / std::atof of a std::string.
inline int ToInt(const std::string& s) { return std::atoi(s.c_str()); }
inline double ToDouble(const std::string& s) { return std::atof(s.c_str()); }

std::string Format(const char* format, ...);
// With a std::string among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasString<Args...>
std::string Format(const char* format, Args&&... args) {
    return Format(format, libprintf::Arg(std::forward<Args>(args))...);
}
// A std::string format.
template <class F, class... Args>
    requires libprintf::IsString<F>
std::string Format(F&& format, Args&&... args) {
    return Format(format.c_str(), libprintf::Arg(std::forward<Args>(args))...);
}
std::string FormatV(const char* format, va_list arguments);

int Find(const std::string& s, char c, int start = 0);
int Find(const std::string& s, const char* sub, int start = 0);
inline int Find(const std::string& s, const std::string& sub, int start = 0) { return Find(s, sub.c_str(), start); }
int ReverseFind(const std::string& s, char c);
int FindOneOf(const std::string& s, const char* set);

std::string Mid(const std::string& s, int first);
std::string Mid(const std::string& s, int first, int count);
std::string Left(const std::string& s, int count);
std::string Right(const std::string& s, int count);

void MakeLower(std::string& s);
// Removes every `c`; returns how many.
int Remove(std::string& s, char c);
void TrimRight(std::string& s);
void TrimRight(std::string& s, char target);
void TrimRight(std::string& s, const char* targets);
void TrimLeft(std::string& s);
void TrimLeft(std::string& s, char target);

// Room for at least `minLength` characters (the text kept); the characters to write into.
char* Buffer(std::string& s, int minLength);
// The text up to its first zero byte (after writing into Buffer).
void CutAtNull(std::string& s);
// The text resized to `length` characters; the characters to write into.
char* SetLength(std::string& s, int length);

} // namespace str
