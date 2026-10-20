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

// A key and a reference (RefEntry, or another pair of a dword and a RefPtr with the same member
// names): the key, then the reference as SharedRef (e.g. 00426680; destroyed as 00421560).
struct SharedEntry {
    template <typename Entry>
    static void Copy(Entry* to, const Entry& from) {
        to->key = from.key;
        SharedRef::Copy(&to->ref, from.ref);
    }
    template <typename Entry>
    static void Destroy(Entry* p) { SharedRef::Destroy(&p->ref); }
};

// The same with the reference as CountedRef (e.g. 004265F0; destroyed as 004215A0).
struct CountedEntry {
    template <typename Entry>
    static void Copy(Entry* to, const Entry& from) {
        to->key = from.key;
        CountedRef::Copy(&to->ref, from.ref);
    }
    template <typename Entry>
    static void Destroy(Entry* p) { CountedRef::Destroy(&p->ref); }
};

// A held engine object copied with one more reference (its dword count; e.g. 0056F680) and
// destroyed with its Release (slot 4) when there is one (clear 00573660).
struct HeldObject {
    static void Copy(CEngineObject** to, CEngineObject* const& from) {
        *to = from;
        if (from) ++from->refCount_;
    }
    static void Destroy(CEngineObject** p) {
        if (*p) (*p)->Release();
    }
};

// A list<T> of an instantiation that copies and destroys its elements by the policy E: the members
// its code compiles (stlp_list.h's bodies with E; the game's out-of-line copies are registered in
// stlp_list.cpp). Construct (the sentinel) and the walks (First, End, Distance) are list<T>'s.
template <typename T, typename E>
struct PolicyList : list<T> {
    using Base = list<T>;
    using Node = typename Base::Node;

    // insert(pos, x) (e.g. 004229B0): a copy of x linked in before pos.
    Node* Insert(Node* pos, const T& x) { return Base::template Insert<E>(pos, x); }
    // push_back(x) (e.g. 004207C0).
    void PushBack(const T& x) { Base::template Insert<E>(this->head, x); }
    // erase(pos) (e.g. 00420290): the node after pos.
    Node* Erase(Node* pos) { return Base::template Erase<E>(pos); }
    // clear() (e.g. 00427100).
    void Clear() { Base::template Clear<E>(); }
    // ~list() (e.g. 00423370): clear(), then the sentinel back to the pool.
    void Release() { Base::template Release<E>(); }
    template <typename Same> void Remove(const T& x, Same same) { Base::template Remove<E>(x, same); }
};

// The reference lists by their element policy.
using SharedRefList = PolicyList<RefPtr, SharedRef>;
using CountedRefList = PolicyList<RefPtr, CountedRef>;
using SharedEntryList = PolicyList<RefEntry, SharedEntry>;
using CountedEntryList = PolicyList<RefEntry, CountedEntry>;
static_assert(sizeof(SharedRefList) == 4);

// The network sessions' DriverNode and ClientNode as list<RefPtr>'s nodes (AsNode), for the
// statics with an element policy (Insert / Erase / InsertRange<SharedRef or CountedRef>).
template <typename Node>
list<RefPtr>::Node* AsRefNode(Node* node) { return AsNode<RefPtr>(node); }

// RefPtr equality for remove(value): the same object.
struct SameObject {
    bool operator()(const RefPtr& a, const RefPtr& b) const { return a.ptr == b.ptr; }
};

// remove(value) (00427320; RET 4) of a CountedRef list: every element holding the same object.
void __fastcall ListRemoveCounted(list<RefPtr>* self, void*, const RefPtr* value);

// remove(value) (00570400; RET 4) of a HeldObject list (the client sector grid's cells): every
// element that is the same object, erased through 00572270.
void __fastcall ListRemoveHeld(list<CEngineObject*>* self, void*, CEngineObject* const* value);

// for_each(first, last, f) (00571780; RET 0xC) over a HeldObject list with a functor that releases
// each object (slot 4, when there is one); the nodes stay. Returns the functor (one byte).
char __stdcall ListForEachRelease(list<CEngineObject*>::Node* first, list<CEngineObject*>::Node* last, char f);

} // namespace stlp
