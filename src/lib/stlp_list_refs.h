#pragma once

// Element policies (stlp_list.h) of the game's list<T> instantiations that hold the engine's
// reference holders: how each _M_create_node copies an element and each erase / clear destroys one.
// Each is what the instantiation's own code does inline or calls (the out-of-line copies of the
// holder helpers are registered in src/game/refptr_copies.cpp).

#include "stlp_list.h"

#include "game/core_refcount.h"
#include "game/engine_object.h"

#include <cstdint>

void __fastcall RefPtrRelease(RefPtr* self);

namespace stlp {

// A key and a reference (the server's award, trade and finished lists, the client's timers).
struct RefEntry {
    std::uint32_t key;
    RefPtr ref;
};
static_assert(sizeof(RefEntry) == 8);

// RefPtr copied with Share (e.g: null for a null or detached object, a clone for a
// copy-on-share one) and destroyed with the owner's release (RefPtr::Destroy, e.g.).
struct SharedRef {
    static void Copy(RefPtr* to, const RefPtr& from) { to->Share(from.ptr); }
    static void Destroy(RefPtr* p) { p->Destroy(); }
};

// RefPtr copied with one more reference (e.g.) and destroyed with a plain release
// (RefPtrRelease, e.g.).
struct CountedRef {
    static void Copy(RefPtr* to, const RefPtr& from) { to->FromRawCounted(from.ptr); }
    static void Destroy(RefPtr* p) { RefPtrRelease(p); }
};

// A key and a reference (RefEntry, or another pair of a dword and a RefPtr with the same member
// names): the key, then the reference as SharedRef (e.g.; destroyed as).
struct SharedEntry {
    template <typename Entry>
    static void Copy(Entry* to, const Entry& from) {
        to->key = from.key;
        SharedRef::Copy(&to->ref, from.ref);
    }
    template <typename Entry>
    static void Destroy(Entry* p) { SharedRef::Destroy(&p->ref); }
};

// The same with the reference as CountedRef (e.g.; destroyed as).
struct CountedEntry {
    template <typename Entry>
    static void Copy(Entry* to, const Entry& from) {
        to->key = from.key;
        CountedRef::Copy(&to->ref, from.ref);
    }
    template <typename Entry>
    static void Destroy(Entry* p) { CountedRef::Destroy(&p->ref); }
};

// A held engine object copied with one more reference (its dword count; e.g.) and
// destroyed with its Release when there is one (clear).
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

    // insert(pos, x) (e.g.): a copy of x linked in before pos.
    Node* Insert(Node* pos, const T& x) { return Base::template Insert<E>(pos, x); }
    // push_back(x) (e.g.).
    void PushBack(const T& x) { Base::template Insert<E>(this->head, x); }
    // erase(pos) (e.g.): the node after pos.
    Node* Erase(Node* pos) { return Base::template Erase<E>(pos); }
    // clear (e.g.).
    void Clear() { Base::template Clear<E>(); }
    // ~list (e.g.): clear, then the sentinel back to the pool.
    void Release() { Base::template Release<E>(); }
    template <typename Same> void Remove(const T& x, Same same) { Base::template Remove<E>(x, same); }
};

// The reference lists by their element policy.
using SharedRefList = PolicyList<RefPtr, SharedRef>;
using CountedRefList = PolicyList<RefPtr, CountedRef>;
using SharedEntryList = PolicyList<RefEntry, SharedEntry>;
using CountedEntryList = PolicyList<RefEntry, CountedEntry>;
static_assert(sizeof(SharedRefList) == 4);

// A reference list whose elements all point at a T (one network message class): the list's layout,
// policy and operations (it still passes as a SharedRefList / CountedRefList), the elements read
// typed.
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

// remove(value) of a HeldObject list (the client sector grid's cells): every
// element that is the same object, erased.
void ListRemoveHeld(list<CEngineObject*>* self, CEngineObject* const& value);

// for_each(first, last, f) over a list of counted objects with a functor that
// releases each object (when there is one); the nodes stay. Returns the functor (one byte).
template <typename T>
char ListForEachRelease(typename list<T*>::Node* first, typename list<T*>::Node* last, char f) {
    for (; first != last; first = first->next)
        if (first->data) first->data->Release();
    return f;
}

} // namespace stlp
