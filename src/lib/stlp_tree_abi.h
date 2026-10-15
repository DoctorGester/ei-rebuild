#pragma once

// The exe's calling conventions of the map<K, V> members (stlp_tree.h), for registering one
// instantiation's bodies at its addresses. Thiscalls are __fastcall with an unused EDX; iterators
// are node pointers, passed by value and returned through a hidden pointer (EAX = that pointer).
// E is how a value is copied into a node and destroyed (the instantiation's pair copy and
// destructor).

#include "stlp_tree.h"

#include <cstddef>
#include <cstdint>

namespace stlp {

// value_type pair<const K, V> whose key is plain and whose mapped value has a policy of its own
// (Copy / Destroy, e.g. stlp_list_refs.h's CountedRef and SharedRef).
template <typename Value, typename Second>
struct PairElement {
    static void Copy(Value* to, const Value& from) {
        to->first = from.first;
        Second::Copy(&to->second, from.second);
    }
    static void Destroy(Value* p) { Second::Destroy(&p->second); }
};

template <typename Map, typename E = typename Map::Element>
struct TreeAbi {
    using Tree = Map;
    using Node = typename Map::Node;
    using Value = typename Map::Value;
    using K = typename Map::key_type;
    using V = typename Map::mapped_type;

    // insert_unique(v)'s pair<iterator, bool>: the node, then the flag byte (the other three bytes
    // left as they were).
    struct InsertResult {
        Node* node;
        std::uint8_t inserted;
    };

    // map() (e.g. 00437D70): a header from the pool, no elements, empty. The compare byte is the
    // original's uninitialised less<> temporary; ours writes 0.
    static Map* __fastcall Construct(Map* self) {
        self->Header() = static_cast<Node*>(allocate(sizeof(Node)));
        self->Count() = 0;
        self->Compare() = 0;
        self->EmptyInitialize();
        return self;
    }
    // _Rb_tree(const Compare&, const allocator&) (e.g. 004393C0; RET 8).
    static Map* __fastcall ConstructWith(Map* self, void*, const std::uint8_t* compare, const void* /*alloc*/) {
        self->Header() = static_cast<Node*>(allocate(sizeof(Node)));
        self->Count() = 0;
        self->Compare() = *compare;
        self->EmptyInitialize();
        return self;
    }
    // _Rb_tree_base(const allocator&) (e.g. 0043A590; RET 4): the header only.
    static Map* __fastcall ConstructBase(Map* self, void*, const void* /*alloc*/) {
        self->Header() = static_cast<Node*>(allocate(sizeof(Node)));
        return self;
    }
    // The copy constructor (e.g. 00437AA0; RET 4).
    static Map* __fastcall CopyConstruct(Map* self, void*, const Map* from) {
        self->template CopyFrom<E>(*from);
        return self;
    }
    // _M_empty_initialize (e.g. 0043A570).
    static void __fastcall EmptyInitialize(Map* self) { self->EmptyInitialize(); }
    // clear() (e.g. 004394A0).
    static void __fastcall Clear(Map* self) { self->template Clear<E>(); }
    // ~_Rb_tree (e.g. 004383A0): clear, then the header back to the pool.
    static void __fastcall Destroy(Map* self) { self->template Destroy<E>(); }
    // ~_Rb_tree_base (e.g. 00439500): the header back to the pool when there is one.
    static void __fastcall FreeHeader(Map* self) {
        if (self->Header()) deallocate(self->Header(), sizeof(Node));
    }
    // _STLP_alloc_proxy<Node>::deallocate(p, n) (e.g. 0043A480; RET 8).
    static void __fastcall DeallocateNodes(Map*, void*, Node* p, std::size_t n) {
        if (p) deallocate(p, n * sizeof(Node));
    }
    // The same as a static member, p in ECX and n in EDX (e.g. 0043B280).
    static void __fastcall DeallocateNodesFast(Node* p, std::size_t n) {
        if (p) deallocate(p, n * sizeof(Node));
    }

    // operator[] (e.g. 00437B90; RET 4). The original's default value is a V() temporary; for
    // the record and attached-id maps some of its bytes are uninitialised stack, ours zero.
    static V* __fastcall Subscript(Map* self, void*, const K* key) { return &self->template Subscript<E>(*key); }
    // lower_bound / upper_bound / find (e.g. 0043BF60 / 0043E5B0 / 0043BF00; RET 8).
    static Node** __fastcall LowerBound(const Map* self, void*, Node** out, const K* key) {
        *out = self->lower_bound(*key);
        return out;
    }
    static Node** __fastcall UpperBound(const Map* self, void*, Node** out, const K* key) {
        *out = self->upper_bound(*key);
        return out;
    }
    static Node** __fastcall Find(const Map* self, void*, Node** out, const K* key) {
        *out = self->find(*key);
        return out;
    }
    // begin() (e.g. 0043C3A0; RET 4).
    static Node** __fastcall Begin(const Map* self, void*, Node** out) {
        *out = self->begin();
        return out;
    }

    // _M_create_node (e.g. 0043DD50; RET 4).
    static Node* __fastcall CreateNode(Map*, void*, const Value* v) { return Map::template CreateNode<E>(*v); }
    // _M_copy(x, p) (e.g. 0043BA60; RET 8).
    static Node* __fastcall CopySubtree(Map*, void*, const Node* x, Node* p) {
        return Map::template CopySubtree<E>(x, p);
    }
    // _M_erase(x) (e.g. 0043BB70; RET 4).
    static void __fastcall EraseSubtree(Map*, void*, Node* x) { Map::template EraseSubtree<E>(x); }
    // _M_insert(x, y, v) (e.g. 0043E140; RET 0x10).
    static Node** __fastcall InsertAt(Map* self, void*, Node** out, Node* x, Node* y, const Value* v) {
        *out = self->template Insert<E>(x, y, *v);
        return out;
    }
    // insert_unique(v) (e.g. 0043E3A0; RET 8).
    static InsertResult* __fastcall InsertUnique(Map* self, void*, InsertResult* out, const Value* v) {
        std::pair<Node*, bool> r = self->template InsertUnique<E>(*v);
        out->node = r.first;
        out->inserted = r.second;
        return out;
    }
    // insert_unique(hint, v) and map::insert(hint, v) (e.g. 0043BBC0 / 00439390; RET 0xC).
    static Node** __fastcall InsertHint(Map* self, void*, Node** out, Node* hint, const Value* v) {
        *out = self->template InsertUnique<E>(hint, *v);
        return out;
    }
    // erase(position) (e.g. 0043FD50; RET 4).
    static void __fastcall EraseAt(Map* self, void*, Node* position) { self->template Erase<E>(position); }
    // erase(first, last) (e.g. 0043E4E0; RET 8).
    static void __fastcall EraseRange(Map* self, void*, Node* first, Node* last) {
        self->template Erase<E>(first, last);
    }
    // erase(key) (e.g. 0043BE60; RET 4): the number erased.
    static std::size_t __fastcall EraseKey(Map* self, void*, const K* key) {
        return self->template EraseKey<E>(*key);
    }

    // pair<const K, V>(const K&, const V&) (e.g. 0043A050; RET 8): the key, then the value
    // copied as a node's value is.
    static Value* __fastcall MakeValue(Value* self, void*, const K* key, const V* value) {
        Value from{*key, *value};
        E::Copy(self, from);
        return self;
    }
};

// Members that only move node pointers, one body for every instantiation.
// An iterator's copy constructor (e.g. 00439530; RET 4).
inline RbNodeBase** __fastcall TreeIteratorCopy(RbNodeBase** self, void*, RbNodeBase* const* from) {
    *self = *from;
    return self;
}
// pair<iterator, bool>(const iterator&, const bool&) (e.g. 0043FDF0; RET 8): the flag byte
// stored alone.
inline void** __fastcall TreeInsertResultConstruct(void** self, void*, RbNodeBase* const* node, const bool* inserted) {
    self[0] = *node;
    *reinterpret_cast<std::uint8_t*>(self + 1) = *inserted;
    return self;
}
// __distance(first, last, n&, tag) with n in ECX (e.g. 00440040; RET 0xC): n raised by the number
// of steps from first to last.
inline void __fastcall TreeDistanceTagged(std::size_t* n, void*, RbNodeBase* first, RbNodeBase* last, std::uint32_t /*tag*/) {
    for (; first != last; first = rb::Next(first)) ++*n;
}
// distance(first, last, n&) with n in ECX (e.g. 0043E5F0; RET 8).
inline void __fastcall TreeDistance(std::size_t* n, void*, RbNodeBase* first, RbNodeBase* last) {
    TreeDistanceTagged(n, nullptr, first, last, 0);
}
// get_allocator() (e.g. 0043A460; RET 4): the allocator byte copied out.
inline std::uint8_t* __fastcall TreeGetAllocator(const std::uint8_t* self, void*, std::uint8_t* out) {
    *out = *self;
    return out;
}
// _STLP_alloc_proxy(const allocator&, const value&) (e.g. 0043AD80; RET 8): the allocator byte,
// then the header pointer over it.
inline void** __fastcall TreeAllocProxyConstruct(void** self, void*, const std::uint8_t* alloc, void* const* value) {
    *reinterpret_cast<std::uint8_t*>(self) = *alloc;
    *self = *value;
    return self;
}

} // namespace stlp
