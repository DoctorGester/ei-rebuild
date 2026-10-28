#pragma once

// STLport basic_string<char> (stlp::string, stlp.h) members as the exe calls them, registered at
// their addresses by stlp_string.cpp. Thiscalls are __fastcall with an unused EDX. Several members
// exist more than once (the same body compiled into different objects); one body serves each.
// The length_error paths (a size above 0xFFFFFFFE) are left out: no string comes near it.

#include "stlp.h"

#include <cstddef>
#include <cstdint>

namespace stlp {

struct StringAbi : string {
    // _Construct(p, value) (p in ECX, value in EDX): a copy at p unless p is null.
    static void __fastcall ConstructAt(string* p, const string* from);
    // basic_string(): empty, an 8-byte block.
    static string* __fastcall ConstructDefault(string* self);
    // operator<(a, b) (a in ECX, b in EDX).
    static bool __fastcall Less(const string* a, const string* b);
    // The scalar deleting destructor.
    string* DeletingDestructor(unsigned flags);

    // The three words, for checks.
    static char* Start(const string& s) { return s.start_; }
    static char* Finish(const string& s) { return s.finish_; }
    static char* EndOfStorage(const string& s) { return s.eos_; }
};

// __stl_throw_out_of_range("basic_string") (substr past the end): the text as a string, an out_of_range made
// from it (stlp_string.cpp's class) and thrown; the string's block is freed as the throw unwinds this frame.
[[noreturn]] void __fastcall ThrowOutOfRange();

} // namespace stlp
