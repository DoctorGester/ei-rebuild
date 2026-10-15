#pragma once

// STLport basic_string<char> (stlp::string, stlp.h) members as the exe calls them, registered at
// their addresses by stlp_string.cpp. Thiscalls are __fastcall with an unused EDX. Several members
// exist more than once (the same body compiled into different objects); one body serves each.
// The length_error paths (a size above 0xFFFFFFFE) are left out: no string comes near it.

#include "stlp.h"

#include <cstddef>
#include <cstdint>

namespace stlp {

struct StringAbi {
    // _M_range_initialize(first, last) (00428DF0, 00428F10; RET 8) and the overload with the
    // iterator tag (0042A2B0, 0042A3D0; RET 0xC): a block of n + 1 bytes, the range, the terminator.
    static void __fastcall RangeInitialize(string* self, void*, const char* first, const char* last);
    static void __fastcall RangeInitializeTagged(string* self, void*, const char* first, const char* last, std::uint32_t tag);
    // basic_string(const basic_string&) (004A8930; RET 4).
    static string* __fastcall CopyConstruct(string* self, void*, const string* from);
    // _Construct(p, value) (004F5DF0; p in ECX, value in EDX): a copy at p unless p is null.
    static void __fastcall ConstructAt(string* p, const string* from);
    // basic_string(const char*, const allocator&) (0040C5A0; RET 8).
    static string* __fastcall ConstructFromText(string* self, void*, const char* text, const void* alloc);
    // basic_string() (0041F460, 006B6CC0): empty, an 8-byte block.
    static string* __fastcall ConstructDefault(string* self);
    // _String_base(const allocator&, n) (00421800; RET 8): a block of n bytes, nothing written in it
    // (no block for n 0).
    static string* __fastcall ConstructWithCapacity(string* self, void*, const void* alloc, std::size_t n);
    // _M_append(first, last, tag) (0040C2A0, 0042B700; RET 0xC) and append(first, last) with the
    // dispatch argument (0040C280, 0042AFB0; RET 0xC) or without (00675690; RET 8).
    static string* __fastcall Append(string* self, void*, const char* first, const char* last, std::uint32_t tag);
    static string* __fastcall AppendRange(string* self, void*, const char* first, const char* last);
    // assign(first, last) (00426A80; RET 8; 0042A4F0 with the tag, RET 0xC).
    static string* __fastcall AssignRange(string* self, void*, const char* first, const char* last);
    static string* __fastcall AssignRangeTagged(string* self, void*, const char* first, const char* last, std::uint32_t tag);
    // operator=(const basic_string&) (0041F4E0; RET 4): nothing for self-assignment.
    static string* __fastcall AssignString(string* self, void*, const string* from);
    // operator=(const char*) (0041F550; RET 4).
    static string* __fastcall AssignText(string* self, void*, const char* text);
    // erase(first, last) (0040B3C0; RET 8): the tail and terminator moved down; returns first.
    static char* __fastcall Erase(string* self, void*, char* first, char* last);
    // substr(pos, n) (0041F5F0; RET 0xC): out_of_range past the end.
    static string* __fastcall Substr(const string* self, void*, string* out, std::size_t pos, std::size_t n);
    // reserve(n) (0068C680; RET 4): a block of max(size, n) + 1 bytes, the contents moved.
    static void __fastcall Reserve(string* self, void*, std::size_t n);
    // push_back(c) (0069BF10; RET 4): reserve(size + max(size, 1)) when full.
    static void __fastcall PushBack(string* self, void*, char c);
    // operator<(a, b) (00445780; a in ECX, b in EDX).
    static bool __fastcall Less(const string* a, const string* b);
    // __compare(first1, last1, first2, last2) (00445810; RET 8): -1, 0 or 1.
    static int __fastcall Compare(const char* first1, const char* last1, const char* first2, const char* last2);
    // The scalar deleting destructor (004DDC90; RET 4).
    static string* __fastcall DeletingDestructor(string* self, void*, unsigned flags);
    // _String_base::_M_allocate_block(n) (00424E40; RET 4): a block of n bytes (none for 0), the
    // string empty at its start; the terminator is not written.
    static void __fastcall AllocateBlock(string* self, void*, std::size_t n);
    // operator+(const string&, const char*) (00427D60; out in ECX, a in EDX, RET 4) and
    // operator+(const string&, const string&) (00427E80): Join. Return out.
    static string* __fastcall PlusText(string* out, const string* a, const char* text);
    static string* __fastcall PlusString(string* out, const string* a, const string* b);
    // operator==(a, b) (00427FA0; a in ECX, b in EDX): the same size and bytes.
    static bool __fastcall Equals(const string* a, const string* b);
    // The body of both operator+: a temporary with room for exactly a and [first, last) and the
    // terminator (a's bytes appended, then the range), then `out` made as a copy of it (a block of
    // its own), then the temporary's block freed.
    static void Join(string* out, const string& a, const char* first, const char* last);

    // The three words, for checks.
    static char* Start(const string& s) { return s.start_; }
    static char* Finish(const string& s) { return s.finish_; }
    static char* EndOfStorage(const string& s) { return s.eos_; }
};

// __stl_throw_out_of_range("basic_string") (004410A0; substr past the end): the text as a string,
// MSVC 6's out_of_range made on the stack from it (the exe's __Named_exception vtable 0073B994, its
// 256-byte name, then out_of_range's vtable 0073BE8C) and thrown with the exe's throw information
// 0074C640 through _CxxThrowException; the string's block is freed as the throw unwinds this frame.
[[noreturn]] void __fastcall ThrowOutOfRange();

} // namespace stlp
