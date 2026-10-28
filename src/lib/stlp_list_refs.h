#pragma once

// Element policies (stlp_list.h) of the game's list<T> instantiations that hold the engine's
// reference holders: how each _M_create_node copies an element and each erase / clear destroys one.
// Each is what the instantiation's own code does inline or calls (the out-of-line copies of the
// holder helpers are in src/game/refptr_copies.cpp).

#include "stlp_list.h"

#include "game/core_refcount.h"

#include <cstdint>

void RefPtrRelease(RefPtr* self);

namespace stlp {

// RefPtr copied with Share (null for a null or detached object, a clone for a
// copy-on-share one) and destroyed with the owner's release (RefPtr::Destroy).
struct SharedRef {
    static void Copy(RefPtr* to, const RefPtr& from) { to->Share(from.ptr); }
    static void Destroy(RefPtr* p) { p->Destroy(); }
};

// RefPtr copied with one more reference and destroyed with a plain release (RefPtrRelease).
struct CountedRef {
    static void Copy(RefPtr* to, const RefPtr& from) { to->FromRawCounted(from.ptr); }
    static void Destroy(RefPtr* p) { RefPtrRelease(p); }
};

// A key and a reference (a pair of a dword and a RefPtr named key and ref: the timed lists' TimedEntry): the
// key, then the reference as SharedRef (destroyed the same way).
struct SharedEntry {
    template <typename Entry>
    static void Copy(Entry* to, const Entry& from) {
        to->key = from.key;
        SharedRef::Copy(&to->ref, from.ref);
    }
    template <typename Entry>
    static void Destroy(Entry* p) { SharedRef::Destroy(&p->ref); }
};

// The same with the reference as CountedRef (destroyed the same way).
struct CountedEntry {
    template <typename Entry>
    static void Copy(Entry* to, const Entry& from) {
        to->key = from.key;
        CountedRef::Copy(&to->ref, from.ref);
    }
    template <typename Entry>
    static void Destroy(Entry* p) { CountedRef::Destroy(&p->ref); }
};

// A list<T> of an instantiation that copies and destroys its elements by the policy E: the members
// its code compiles (stlp_list.h's bodies with E). Construct (the sentinel) and the walks (First, End,
// Distance) are list<T>'s.
template <typename T, typename E>
struct PolicyList : list<T> {
    using Base = list<T>;
    using Node = typename Base::Node;

    // insert(pos, x): a copy of x linked in before pos.
    Node* Insert(Node* pos, const T& x) { return Base::template Insert<E>(pos, x); }
    // push_back(x).
    void PushBack(const T& x) { Base::template Insert<E>(this->head, x); }
    // erase(pos): the node after pos.
    Node* Erase(Node* pos) { return Base::template Erase<E>(pos); }
    // clear.
    void Clear() { Base::template Clear<E>(); }
    // ~list: clear, then the sentinel back to the pool.
    void Release() { Base::template Release<E>(); }
    template <typename Same> void Remove(const T& x, Same same) { Base::template Remove<E>(x, same); }
};

// The reference lists by their element policy.
using SharedRefList = PolicyList<RefPtr, SharedRef>;
using CountedRefList = PolicyList<RefPtr, CountedRef>;
static_assert(sizeof(SharedRefList) == 4);

// A reference list whose elements all point at a T (one network message class): the list's layout, policy and
// operations (it still passes as a SharedRefList / CountedRefList), the elements read typed.
template <typename T, typename E>
struct RefListOf : PolicyList<RefPtr, E> {
    using Node = typename PolicyList<RefPtr, E>::Node;
    // The object `node` (an element of this list) points at.
    static T* At(const Node* node) { return static_cast<T*>(node->data.ptr); }
    T* Front() const { return At(this->head->next); }
    T* Back() const { return At(this->head->prev); }
};
template <typename T> using SharedRefListOf = RefListOf<T, SharedRef>;
template <typename T> using CountedRefListOf = RefListOf<T, CountedRef>;
static_assert(sizeof(SharedRefListOf<CRefObject>) == 4);

// The network sessions' DriverNode and ClientNode as list<RefPtr>'s nodes (AsNode), for the
// statics with an element policy (Insert / Erase / InsertRange<SharedRef or CountedRef>).
template <typename Node>
list<RefPtr>::Node* AsRefNode(Node* node) { return AsNode<RefPtr>(node); }

// RefPtr equality for remove(value): the same object.
struct SameObject {
    bool operator()(const RefPtr& a, const RefPtr& b) const { return a.ptr == b.ptr; }
};

} // namespace stlp
