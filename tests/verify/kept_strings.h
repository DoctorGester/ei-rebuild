#pragma once

// Helpers for the checks whose outputs are CStrings (SV2h1). The SV2f rule: ours runs first on the
// real output and allocates first; the original runs afterwards into scratch strings, made from
// what was kept in our memory before ours ran, and those are freed again.
//
// CString's blocks of up to 512 characters come from MFC's fixed-size allocators, which hand out
// the block freed last first. Strings the original made one after the other are freed last to
// first, so each allocator's free list ends as it was before them.

#include "../../src/lib/mfc.h"

#include <string>

namespace ei::verify {

// A CString's text kept in our memory (the CString itself may be changed or freed by ours).
inline std::string KeepText(const mfc::CString& s) { return std::string(static_cast<const char*>(s)); }

// Frees a scratch string array's strings last to first; the array's own destructor (or SetSize(0))
// then frees only its buffer.
inline void EmptyBackwards(mfc::CStringArray& array) {
    for (int i = array.size - 1; i >= 0; --i) array.data[i].Empty();
}

} // namespace ei::verify
