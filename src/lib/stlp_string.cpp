// STLport basic_string<char> members (stlp_string.h) registered at the library addresses. Checked
// in game by verify_stlp_string.cpp until S21d.
#include "stlp_string.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>

namespace stlp {

void StringAbi::RangeInitialize(const char* first, const char* last) {
    InitRange(first, last);
}

void StringAbi::RangeInitializeTagged(const char* first, const char* last, std::uint32_t) {
    InitRange(first, last);
}

string* StringAbi::CopyConstruct(const string* from) {
    InitRange(from->start_, from->finish_);
    return this;
}

void __fastcall StringAbi::ConstructAt(string* p, const string* from) {
    if (p) p->InitRange(from->start_, from->finish_);
}

string* StringAbi::ConstructFromText(const char* text, const void*) {
    InitRange(text, text + std::strlen(text));
    return this;
}

string* __fastcall StringAbi::ConstructDefault(string* self) {
    self->start_ = self->finish_ = allocate_as<char>(8);
    self->eos_ = self->start_ + 8;
    *self->finish_ = 0;
    return self;
}

string* StringAbi::ConstructWithCapacity(const void*, std::size_t n) {
    start_ = finish_ = n ? allocate_as<char>(n) : nullptr;
    eos_ = start_ + n;
    return this;
}

string* StringAbi::Append(const char* first, const char* last, std::uint32_t) {
    string::Append(first, last);
    return this;
}

string* StringAbi::AppendRange(const char* first, const char* last) {
    string::Append(first, last);
    return this;
}

string* StringAbi::AssignRange(const char* first, const char* last) {
    Assign(first, last);
    return this;
}

string* StringAbi::AssignRangeTagged(const char* first, const char* last, std::uint32_t) {
    Assign(first, last);
    return this;
}

string* StringAbi::AssignString(const string* from) {
    if (from != this) Assign(from->start_, from->finish_);
    return this;
}

string* StringAbi::AssignText(const char* text) {
    Assign(text, text + std::strlen(text));
    return this;
}

char* StringAbi::Erase(char* first, char* last) {
    if (first != last) {
        std::memmove(first, last, static_cast<std::size_t>(finish_ - last) + 1);
        finish_ -= last - first;
    }
    return first;
}

string string::substr(std::size_t pos, std::size_t n) const {
    std::size_t size = this->size();
    if (pos > size) ThrowOutOfRange();
    std::size_t length = std::min(size - pos, n);
    return string(start_ + pos, start_ + pos + length);
}

string* StringAbi::Substr(string* out, std::size_t pos, std::size_t n) const {
    return new (out) string(substr(pos, n));
}

void StringAbi::Reserve(std::size_t n) {
    std::size_t length = size();
    std::size_t capacity = std::max(length, n) + 1;
    char* fresh = allocate_as<char>(capacity);
    std::memcpy(fresh, start_, length);
    fresh[length] = 0;
    Release();
    start_ = fresh;
    finish_ = fresh + length;
    eos_ = fresh + capacity;
}

void StringAbi::PushBack(char c) {
    if (finish_ + 1 == eos_) {
        std::size_t length = size();
        Reserve(length + std::max<std::size_t>(length, 1));
    }
    finish_[1] = 0;
    *finish_ = c;
    ++finish_;
}

bool __fastcall StringAbi::Less(const string* a, const string* b) { return *a < *b; }

int __fastcall StringAbi::Compare(const char* first1, const char* last1, const char* first2, const char* last2) {
    std::ptrdiff_t n1 = last1 - first1;
    std::ptrdiff_t n2 = last2 - first2;
    int c = std::memcmp(first1, first2, static_cast<std::size_t>(std::min(n1, n2)));
    if (c != 0) return c < 0 ? -1 : 1;
    return n1 < n2 ? -1 : n1 > n2 ? 1 : 0;
}

string* StringAbi::DeletingDestructor(unsigned flags) {
    Release();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

} // namespace stlp

using stlp::StringAbi;

namespace stlp {

void StringAbi::AllocateBlock(std::size_t n) {
    start_ = finish_ = allocate_as<char>(n);
    eos_ = start_ + n;
}

void StringAbi::Join(string* out, const string& a, const char* first, const char* last) {
    string joined;
    string::Reserve(joined, a.size() + static_cast<std::size_t>(last - first) + 1);
    joined.Append(a.start_, a.finish_);
    joined.Append(first, last);
    out->InitRange(joined.start_, joined.finish_);
}

string* __fastcall StringAbi::PlusText(string* out, const string* a, const char* text) {
    Join(out, *a, text, text + std::strlen(text));
    return out;
}

string* __fastcall StringAbi::PlusString(string* out, const string* a, const string* b) {
    Join(out, *a, b->start_, b->finish_);
    return out;
}

bool __fastcall StringAbi::Equals(const string* a, const string* b) { return *a == *b; }

namespace {

// STLport's out_of_range (C25c: a class of ours, thrown by the compiler with its own throw information in place of
// game.exe's ThrowInfo 0074C640, catchable types and type descriptors, and of its tables 0073B994 / 0073BE8C):
// std::exception, __Named_exception's 256-byte name (its what), logic_error. Nothing catches one by type.
class NamedException : public std::exception {
public:
    // The text copied into the name (strncpy, the last byte zero).
    explicit NamedException(const string& what) {
        gamecrt::strncpy(name_, what.data(), sizeof(name_));
        name_[sizeof(name_) - 1] = 0;
    }
    const char* what() const noexcept override { return name_; }

private:
    char name_[0x100];
};
class LogicError : public NamedException {
public:
    using NamedException::NamedException;
};
class OutOfRange : public LogicError {
public:
    using LogicError::LogicError;
};

} // namespace

void __fastcall ThrowOutOfRange() {
    const string what("basic_string");
    throw OutOfRange(what);
}

} // namespace stlp

EI_REWRITTEN(0x00428DF0, StringAbi::RangeInitialize);
EI_REWRITTEN(0x00428F10, StringAbi::RangeInitialize);
EI_REWRITTEN(0x0042A2B0, StringAbi::RangeInitializeTagged);
EI_REWRITTEN(0x0042A3D0, StringAbi::RangeInitializeTagged);
EI_REWRITTEN(0x004A8930, StringAbi::CopyConstruct);
EI_REWRITTEN(0x004F5DF0, StringAbi::ConstructAt);
EI_REWRITTEN(0x0040C5A0, StringAbi::ConstructFromText);
EI_REWRITTEN(0x0041F460, StringAbi::ConstructDefault);
EI_REWRITTEN(0x006B6CC0, StringAbi::ConstructDefault);
EI_REWRITTEN(0x00424E40, StringAbi::AllocateBlock);
EI_REWRITTEN(0x00427D60, StringAbi::PlusText);
EI_REWRITTEN(0x00427E80, StringAbi::PlusString);
EI_REWRITTEN(0x00427FA0, StringAbi::Equals);
EI_REWRITTEN(0x004410A0, stlp::ThrowOutOfRange);
EI_REWRITTEN(0x00421800, StringAbi::ConstructWithCapacity);
EI_REWRITTEN(0x0040C2A0, StringAbi::Append);
EI_REWRITTEN(0x0042B700, StringAbi::Append);
EI_REWRITTEN(0x0040C280, StringAbi::Append);
EI_REWRITTEN(0x0042AFB0, StringAbi::Append);
EI_REWRITTEN(0x00675690, StringAbi::AppendRange);
EI_REWRITTEN(0x00426A80, StringAbi::AssignRange);
EI_REWRITTEN(0x0042A4F0, StringAbi::AssignRangeTagged);
EI_REWRITTEN(0x0041F4E0, StringAbi::AssignString);
EI_REWRITTEN(0x0041F550, StringAbi::AssignText);
EI_REWRITTEN(0x0040B3C0, StringAbi::Erase);
EI_REWRITTEN(0x0041F5F0, StringAbi::Substr);
EI_REWRITTEN(0x0068C680, StringAbi::Reserve);
EI_REWRITTEN(0x0069BF10, StringAbi::PushBack);
EI_REWRITTEN(0x00445780, StringAbi::Less);
EI_REWRITTEN(0x00445810, StringAbi::Compare);
EI_REWRITTEN(0x004DDC90, StringAbi::DeletingDestructor);
