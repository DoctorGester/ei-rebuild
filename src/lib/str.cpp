// std::string helpers with CString's answers (str.h). The bodies follow MFC 4.2's (strcore.cpp, strex.cpp).
#include "str.h"

#include "text_case.h"

#include <cstring>

namespace str {

int Compare(std::string_view a, std::string_view b) {
    return a.substr(0, a.find('\0')).compare(b.substr(0, b.find('\0')));
}

bool MbsLess::operator()(const std::string& a, const std::string& b) const {
    return std::strcmp(a.c_str(), b.c_str()) < 0;
}

namespace {

// The byte searches stand for the multibyte CRT's _mbschr / _mbsstr / _mbsrchr / _mbspbrk / _ismbcspace, which the
// text (UTF-8, no lead bytes) made the same: strchr finds the char's byte (_tcschr's sign-extended char too), the
// white space is ASCII's.
bool Contains(const char* set, char c) { return std::strchr(set, c) != nullptr; }
int Length(const std::string& s) { return static_cast<int>(s.size()); }
int Offset(const std::string& s, const char* found) { return found ? static_cast<int>(found - s.c_str()) : -1; }

// The text cut where `last` points (TrimRight's end), when it points into it.
void CutAt(std::string& s, const char* last) {
    if (last) s.resize(static_cast<std::size_t>(last - s.c_str()));
}

} // namespace

std::string Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::string text = FormatV(format, arguments);
    va_end(arguments);
    return text;
}

int Find(const std::string& s, char c, int start) {
    if (start >= Length(s)) return -1;
    return Offset(s, std::strchr(s.c_str() + start, c));
}

int Find(const std::string& s, const char* sub, int start) {
    if (start > Length(s)) return -1;
    return Offset(s, std::strstr(s.c_str() + start, sub));
}

int ReverseFind(const std::string& s, char c) { return Offset(s, std::strrchr(s.c_str(), c)); }

int FindOneOf(const std::string& s, const char* set) { return Offset(s, std::strpbrk(s.c_str(), set)); }

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

// TrimRight: cut at the start of the last run of characters that match.
void TrimRight(std::string& s) {
    const char* last = nullptr;
    for (const char* p = s.c_str(); *p != '\0'; ++p) {
        if (text::IsSpace(*p)) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    CutAt(s, last);
}

void TrimRight(std::string& s, char target) {
    const char* last = nullptr;
    for (const char* p = s.c_str(); *p != '\0'; ++p) {
        if (*p == target) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    CutAt(s, last);
}

void TrimRight(std::string& s, const char* targets) {
    const char* last = nullptr;
    for (const char* p = s.c_str(); *p != '\0'; ++p) {
        if (Contains(targets, *p)) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    CutAt(s, last);
}

void TrimLeft(std::string& s) {
    const char* p = s.c_str();
    while (text::IsSpace(*p)) ++p;
    s.erase(0, static_cast<std::size_t>(p - s.c_str()));
}

void TrimLeft(std::string& s, char target) {
    const char* p = s.c_str();
    while (*p == target) ++p;
    s.erase(0, static_cast<std::size_t>(p - s.c_str()));
}

char* Buffer(std::string& s, int minLength) {
    if (minLength > Length(s)) s.resize(static_cast<std::size_t>(minLength));
    return s.data();
}

void CutAtNull(std::string& s) { s.resize(std::strlen(s.c_str())); }

char* SetLength(std::string& s, int length) {
    s.resize(static_cast<std::size_t>(length));
    return s.data();
}

} // namespace str
