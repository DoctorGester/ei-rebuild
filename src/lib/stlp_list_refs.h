#pragma once

// Element policies (stlp_list.h) of the game's list<T> instantiations that hold the engine's
// reference holders: how each _M_create_node copies an element and each erase / clear destroys one.
// Each is what the instantiation's own code does inline or calls (the out-of-line copies of the
// holder helpers are registered in src/game/refptr_copies.cpp).

#include "stlp_list.h"

#include "game/core_refcount.h"
#include "game/engine_object.h"

#include <cstdint>

void __fastcall RefPtrRelease(RefPtr* self);   // refptr_copies.cpp

namespace stlp {

// A key and a reference (the server's award, trade and finished lists, the client's timers).
struct RefEntry {
    std::uint32_t key;
    RefPtr ref;
};
static_assert(sizeof(RefEntry) == 8);

// RefPtr copied with Share (e.g. 00425250: null for a null or detached object, a clone for a
// copy-on-share one) and destroyed with the owner's release (RefPtr::Destroy, e.g. 00421640).
struct SharedRef {
    static void Copy(RefPtr* to, const RefPtr& from) { to->Share(from.ptr); }
    static void Destroy(RefPtr* p) { p->Destroy(); }
};

// RefPtr copied with one more reference (e.g. 00425710) and destroyed with a plain release
// (RefPtrRelease, e.g. 00427240).
struct CountedRef {
    static void Copy(RefPtr* to, const RefPtr& from) { to->FromRawCounted(from.ptr); }
    static void Destroy(RefPtr* p) { RefPtrRelease(p); }
};

// RefEntry: the key, then the reference as SharedRef (e.g. 00426680; destroyed as 00421560).
struct SharedEntry {
    static void Copy(RefEntry* to, const RefEntry& from) {
        to->key = from.key;
        SharedRef::Copy(&to->ref, from.ref);
    }
    static void Destroy(RefEntry* p) { SharedRef::Destroy(&p->ref); }
};

// RefEntry: the key, then the reference as CountedRef (e.g. 004265F0; destroyed as 004215A0).
struct CountedEntry {
    static void Copy(RefEntry* to, const RefEntry& from) {
        to->key = from.key;
        CountedRef::Copy(&to->ref, from.ref);
    }
    static void Destroy(RefEntry* p) { CountedRef::Destroy(&p->ref); }
};

// A held engine object copied with one more reference (its dword count; e.g. 0056F680). The
// instantiations that use it never destroy an element here.
struct HeldObject {
    static void Copy(CEngineObject** to, CEngineObject* const& from) {
        *to = from;
        if (from) ++from->refCount_;
    }
};

// RefPtr equality for remove(value): the same object.
struct SameObject {
    bool operator()(const RefPtr& a, const RefPtr& b) const { return a.ptr == b.ptr; }
};

// remove(value) (00427320; RET 4) of a CountedRef list: every element holding the same object.
void __fastcall ListRemoveCounted(list<RefPtr>* self, void*, const RefPtr* value);

} // namespace stlp
