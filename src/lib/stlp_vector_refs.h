#pragma once

// Element policies (stlp_vector.h) of the game's vector<T> instantiations that hold the engine's
// counted reference holders (RefPtr, counted_ptr<T>: one pointer, the object's 16-bit count at +4).

#include "stlp_vector.h"

#include "game/core_refcount.h"

void __fastcall RefPtrRelease(RefPtr* self);   // refptr_copies.cpp

namespace stlp {

// Copied with one more reference (e.g. 0042B600), destroyed with a plain release (RefPtrRelease,
// e.g. 0041DDA0). Assignment (erase's shift, e.g. 00421ED0) stores and counts the new object
// first, then releases the old one.
struct CountedRefElement {
    static void Construct(RefPtr* to, const RefPtr& from) { to->FromRawCounted(from.ptr); }
    static void Assign(RefPtr& to, const RefPtr& from) {
        RefPtr old = to;
        to.FromRawCounted(from.ptr);
        RefPtrRelease(&old);
    }
    static void Destroy(RefPtr* p) { RefPtrRelease(p); }
};

} // namespace stlp
