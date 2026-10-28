// std::string helpers with CString's answers (str.h). The bodies follow MFC 4.2's (mfc_string.cpp).
#include "str.h"

#include "crt.h"

#include <cstring>
#include <mbstring.h>

namespace str {

namespace {

using uchar = unsigned char;

const uchar* U(const char* p) { return reinterpret_cast<const uchar*>(p); }
char* Inc(const char* p) { return const_cast<char*>(gamecrt::mbsinc(p)); }
// _tcschr with a char sign-extended first, as the library passed it.
bool Contains(const char* set, char c) { return _mbschr(U(set), static_cast<unsigned>(static_cast<int>(c))) != nullptr; }
bool IsSpace(char c) { return _ismbcspace(static_cast<unsigned>(static_cast<int>(c))) != 0; }
int Length(const std::string& s) { return static_cast<int>(s.size()); }
int Offset(const std::string& s, const uchar* found) {
    return found ? static_cast<int>(reinterpret_cast<const char*>(found) - s.c_str()) : -1;
}

// The text cut where `last` points (TrimRight's end), when it points into it.
void CutAt(std::string& s, const char* last) {
    if (last) s.resize(static_cast<std::size_t>(last - s.c_str()));
}

} // namespace

std::string __cdecl Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::string text = FormatV(format, arguments);
    va_end(arguments);
    return text;
}

int Find(const std::string& s, char c, int start) {
    if (start >= Length(s)) return -1;
    return Offset(s, _mbschr(U(s.c_str() + start), static_cast<uchar>(c)));
}

int Find(const std::string& s, const char* sub, int start) {
    if (start > Length(s)) return -1;
    return Offset(s, _mbsstr(U(s.c_str() + start), U(sub)));
}

int ReverseFind(const std::string& s, char c) { return Offset(s, _mbsrchr(U(s.c_str()), static_cast<uchar>(c))); }

int FindOneOf(const std::string& s, const char* set) { return Offset(s, _mbspbrk(U(s.c_str()), U(set))); }

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

void MakeLower(std::string& s) { libcrt::Mbslwr(reinterpret_cast<uchar*>(s.data())); }

// TrimRight: cut at the start of the last run of characters that match.
void TrimRight(std::string& s) {
    const char* last = nullptr;
    for (const char* p = s.c_str(); *p != '\0'; p = Inc(p)) {
        if (IsSpace(*p)) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    CutAt(s, last);
}

void TrimRight(std::string& s, char target) {
    const char* last = nullptr;
    for (const char* p = s.c_str(); *p != '\0'; p = Inc(p)) {
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
    for (const char* p = s.c_str(); *p != '\0'; p = Inc(p)) {
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
    while (IsSpace(*p)) p = Inc(p);
    s.erase(0, static_cast<std::size_t>(p - s.c_str()));
}

void TrimLeft(std::string& s, char target) {
    const char* p = s.c_str();
    while (*p == target) p = Inc(p);
    s.erase(0, static_cast<std::size_t>(p - s.c_str()));
}

char* Buffer(std::string& s, int minLength) {
    if (minLength > Length(s)) s.resize(static_cast<std::size_t>(minLength));
    return s.data();
}

char* SetLength(std::string& s, int length) {
    s.resize(static_cast<std::size_t>(length));
    return s.data();
}

} // namespace str
