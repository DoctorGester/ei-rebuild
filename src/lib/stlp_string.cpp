// STLport basic_string<char> members (stlp_string.h) registered at the library addresses. Checked
// in game by verify_stlp_string.cpp until S21d.
#include "stlp_string.h"

#include "ei/hook.h"
#include "game/originals_library.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace stlp {

void __fastcall StringAbi::RangeInitialize(string* self, void*, const char* first, const char* last) {
    self->InitRange(first, last);
}

void __fastcall StringAbi::RangeInitializeTagged(string* self, void*, const char* first, const char* last, std::uint32_t) {
    self->InitRange(first, last);
}

string* __fastcall StringAbi::CopyConstruct(string* self, void*, const string* from) {
    self->InitRange(from->start_, from->finish_);
    return self;
}

void __fastcall StringAbi::ConstructAt(string* p, const string* from) {
    if (p) p->InitRange(from->start_, from->finish_);
}

string* __fastcall StringAbi::ConstructFromText(string* self, void*, const char* text, const void*) {
    self->InitRange(text, text + std::strlen(text));
    return self;
}

string* __fastcall StringAbi::ConstructDefault(string* self) {
    self->start_ = self->finish_ = static_cast<char*>(allocate(8));
    self->eos_ = self->start_ + 8;
    *self->finish_ = 0;
    return self;
}

string* __fastcall StringAbi::ConstructWithCapacity(string* self, void*, const void*, std::size_t n) {
    self->start_ = self->finish_ = n ? static_cast<char*>(allocate(n)) : nullptr;
    self->eos_ = self->start_ + n;
    return self;
}

string* __fastcall StringAbi::Append(string* self, void*, const char* first, const char* last, std::uint32_t) {
    self->Append(first, last);
    return self;
}

string* __fastcall StringAbi::AppendRange(string* self, void*, const char* first, const char* last) {
    self->Append(first, last);
    return self;
}

string* __fastcall StringAbi::AssignRange(string* self, void*, const char* first, const char* last) {
    self->Assign(first, last);
    return self;
}

string* __fastcall StringAbi::AssignRangeTagged(string* self, void*, const char* first, const char* last, std::uint32_t) {
    self->Assign(first, last);
    return self;
}

string* __fastcall StringAbi::AssignString(string* self, void*, const string* from) {
    if (from != self) self->Assign(from->start_, from->finish_);
    return self;
}

string* __fastcall StringAbi::AssignText(string* self, void*, const char* text) {
    self->Assign(text, text + std::strlen(text));
    return self;
}

char* __fastcall StringAbi::Erase(string* self, void*, char* first, char* last) {
    if (first != last) {
        std::memmove(first, last, static_cast<std::size_t>(self->finish_ - last) + 1);
        self->finish_ -= last - first;
    }
    return first;
}

string string::substr(std::size_t pos, std::size_t n) const {
    std::size_t size = this->size();
    if (pos > size) ThrowOutOfRange();
    std::size_t length = std::min(size - pos, n);
    return string(start_ + pos, start_ + pos + length);
}

string* __fastcall StringAbi::Substr(const string* self, void*, string* out, std::size_t pos, std::size_t n) {
    return new (out) string(self->substr(pos, n));
}

void __fastcall StringAbi::Reserve(string* self, void*, std::size_t n) {
    std::size_t size = self->size();
    std::size_t capacity = std::max(size, n) + 1;
    char* fresh = static_cast<char*>(allocate(capacity));
    std::memcpy(fresh, self->start_, size);
    fresh[size] = 0;
    self->Release();
    self->start_ = fresh;
    self->finish_ = fresh + size;
    self->eos_ = fresh + capacity;
}

void __fastcall StringAbi::PushBack(string* self, void*, char c) {
    if (self->finish_ + 1 == self->eos_) {
        std::size_t size = self->size();
        Reserve(self, nullptr, size + std::max<std::size_t>(size, 1));
    }
    self->finish_[1] = 0;
    *self->finish_ = c;
    ++self->finish_;
}

bool __fastcall StringAbi::Less(const string* a, const string* b) { return *a < *b; }

int __fastcall StringAbi::Compare(const char* first1, const char* last1, const char* first2, const char* last2) {
    std::ptrdiff_t n1 = last1 - first1;
    std::ptrdiff_t n2 = last2 - first2;
    int c = std::memcmp(first1, first2, static_cast<std::size_t>(std::min(n1, n2)));
    if (c != 0) return c < 0 ? -1 : 1;
    return n1 < n2 ? -1 : n1 > n2 ? 1 : 0;
}

string* __fastcall StringAbi::DeletingDestructor(string* self, void*, unsigned flags) {
    self->Release();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace stlp

using stlp::StringAbi;

namespace stlp {

void __fastcall StringAbi::AllocateBlock(string* self, void*, std::size_t n) {
    self->start_ = self->finish_ = static_cast<char*>(allocate(n));
    self->eos_ = self->start_ + n;
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

// MSVC 6's out_of_range (0x10C bytes): std::exception, then __Named_exception's name.
struct OutOfRange {
    libcrt::Exception base;
    char name[0x100];
};
static_assert(sizeof(OutOfRange) == 0x10C);

const void* NamedExceptionVtable() { return reinterpret_cast<const void*>(0x0073B994); }
const void* OutOfRangeVtable() { return reinterpret_cast<const void*>(0x0073BE8C); }
const void* OutOfRangeThrowInfo() { return reinterpret_cast<const void*>(0x0074C640); }
const char* BasicStringText() { return reinterpret_cast<const char*>(0x00783CCC); }   // "basic_string"

} // namespace

void __fastcall ThrowOutOfRange() {
    const string what(BasicStringText());
    OutOfRange error;
    error.base.Construct();
    error.base.vtable = NamedExceptionVtable();
    gamecrt::strncpy(error.name, what.data(), sizeof(error.name));
    error.name[sizeof(error.name) - 1] = 0;
    error.base.vtable = OutOfRangeVtable();
    libcrt::CxxThrowException(&error, OutOfRangeThrowInfo());
    __assume(0);
}

} // namespace stlp

EI_REPLACE(0x00428DF0, StringAbi::RangeInitialize);
EI_REPLACE(0x00428F10, StringAbi::RangeInitialize);
EI_REPLACE(0x0042A2B0, StringAbi::RangeInitializeTagged);
EI_REPLACE(0x0042A3D0, StringAbi::RangeInitializeTagged);
EI_REPLACE(0x004A8930, StringAbi::CopyConstruct);
EI_REPLACE(0x004F5DF0, StringAbi::ConstructAt);
EI_REPLACE(0x0040C5A0, StringAbi::ConstructFromText);
EI_REPLACE(0x0041F460, StringAbi::ConstructDefault);
EI_REPLACE(0x006B6CC0, StringAbi::ConstructDefault);
EI_REPLACE(0x00421800, StringAbi::ConstructWithCapacity);
EI_REPLACE(0x0040C2A0, StringAbi::Append);
EI_REPLACE(0x0042B700, StringAbi::Append);
EI_REPLACE(0x0040C280, StringAbi::Append);
EI_REPLACE(0x0042AFB0, StringAbi::Append);
EI_REPLACE(0x00675690, StringAbi::AppendRange);
EI_REPLACE(0x00426A80, StringAbi::AssignRange);
EI_REPLACE(0x0042A4F0, StringAbi::AssignRangeTagged);
EI_REPLACE(0x0041F4E0, StringAbi::AssignString);
EI_REPLACE(0x0041F550, StringAbi::AssignText);
EI_REPLACE(0x0040B3C0, StringAbi::Erase);
EI_REPLACE(0x0041F5F0, StringAbi::Substr);
EI_REPLACE(0x0068C680, StringAbi::Reserve);
EI_REPLACE(0x0069BF10, StringAbi::PushBack);
EI_REPLACE(0x00445780, StringAbi::Less);
EI_REPLACE(0x00445810, StringAbi::Compare);
EI_REPLACE(0x004DDC90, StringAbi::DeletingDestructor);
// S12g: the net code's string members.
EI_REPLACE(0x00424E40, StringAbi::AllocateBlock);
EI_REPLACE(0x00427D60, StringAbi::PlusText);
EI_REPLACE(0x00427E80, StringAbi::PlusString);
EI_REPLACE(0x00427FA0, StringAbi::Equals);
EI_REPLACE(0x004410A0, stlp::ThrowOutOfRange);
