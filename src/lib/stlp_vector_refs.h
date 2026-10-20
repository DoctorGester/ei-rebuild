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
    // resize(n)'s element (e.g. 004200C0, 004AD700): a null pointer.
    static void Default(RefPtr* p) { p->ptr = nullptr; }
};

// The owners' holders (the server's file chunks, 00421D90 / 00426CB0): copied by sharing
// (RefPtr::Share: null for a detached object, a clone for a copy-on-share one), assigned by
// RefPtr::ShareAssign (nothing when it already holds the object), destroyed by the owner's release
// (RefPtr::Destroy, 00419D00: a still-shared object detached and shut down first).
struct SharedRefElement {
    static void Construct(RefPtr* to, const RefPtr& from) { to->Share(from.ptr); }
    static void Assign(RefPtr& to, const RefPtr& from) { to.ShareAssign(from.ptr); }
    static void Destroy(RefPtr* p) { p->Destroy(); }
    static void Default(RefPtr* p) { p->ptr = nullptr; }
};

using CountedRefVector = VectorAbi<RefPtr, CountedRefElement>;
using SharedRefVector = VectorAbi<RefPtr, SharedRefElement>;

// A vector of counted pointers (counted_ptr<T>: the object's pointer alone) as the vector<RefPtr>
// the game's instantiations share, and its elements likewise.
template <typename T>
vector<RefPtr>& AsRefPtrs(vector<T*>& v) {
    static_assert(sizeof(T*) == sizeof(RefPtr));
    return reinterpret_cast<vector<RefPtr>&>(v);
}
template <typename T>
RefPtr* AsRefPtr(T** p) { return reinterpret_cast<RefPtr*>(p); }
template <typename T>
const RefPtr& AsRefPtr(T* const& p) { return reinterpret_cast<const RefPtr&>(p); }

} // namespace stlp
