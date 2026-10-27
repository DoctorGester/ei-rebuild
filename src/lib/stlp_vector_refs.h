#pragma once

// Element policies (stlp_vector.h) of the game's vector<T> instantiations that hold the engine's
// counted reference holders (RefPtr, counted_ptr<T>: one pointer, the object's 16-bit count at +4).

#include "stlp_vector.h"

#include "game/core_refcount.h"

void __fastcall RefPtrRelease(RefPtr* self);

namespace stlp {

// Copied with one more reference, destroyed with a plain release (RefPtrRelease). Assignment (erase's shift)
// stores and counts the new object first, then releases the old one.
struct CountedRefElement {
    static void Construct(RefPtr* to, const RefPtr& from) { to->FromRawCounted(from.ptr); }
    static void Assign(RefPtr& to, const RefPtr& from) {
        RefPtr old = to;
        to.FromRawCounted(from.ptr);
        RefPtrRelease(&old);
    }
    static void Destroy(RefPtr* p) { RefPtrRelease(p); }
    // resize(n)'s element: a null pointer.
    static void Default(RefPtr* p) { p->ptr = nullptr; }
};

// The owners' holders (the server's file chunks): copied by sharing
// (RefPtr::Share: null for a detached object, a clone for a copy-on-share one), assigned by
// RefPtr::ShareAssign (nothing when it already holds the object), destroyed by the owner's release
// (RefPtr::Destroy: a still-shared object detached and shut down first).
struct SharedRefElement {
    static void Construct(RefPtr* to, const RefPtr& from) { to->Share(from.ptr); }
    static void Assign(RefPtr& to, const RefPtr& from) { to.ShareAssign(from.ptr); }
    static void Destroy(RefPtr* p) { p->Destroy(); }
    static void Default(RefPtr* p) { p->ptr = nullptr; }
};

// The same policy for a vector<T*> of counted objects (CRefCounted's count and flags) that is not shared
// with the RefPtr instantiation: the pointer typed, nothing reinterpreted.
template <typename T>
struct CountedPtrElement {
    static void Construct(T** to, T* const& from) {
        *to = from;
        if (from) ++from->refs_;
    }
    static void Assign(T*& to, T* const& from) {
        T* old = to;
        to = from;
        if (from) ++from->refs_;
        ReleaseCounted(old);
    }
    static void Destroy(T** p) { ReleaseCounted(*p); }
    static void Default(T** p) { *p = nullptr; }
};

using CountedRefVector = VectorAbi<RefPtr, CountedRefElement>;
using SharedRefVector = VectorAbi<RefPtr, SharedRefElement>;

} // namespace stlp
