#pragma once

// The exe's calling conventions of the map<K, V> members (stlp_tree.h), for registering one
// instantiation's bodies at its addresses. Thiscalls are __fastcall with an unused EDX; iterators
// are node pointers, passed by value and returned through a hidden pointer (EAX = that pointer).
// E is how a value is copied into a node and destroyed (the instantiation's pair copy and
// destructor).

#include "crt.h"
#include "stlp_tree.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace stlp {

// value_type pair<const K, V> whose key is copied and destroyed as itself and whose mapped value
// has a policy of its own (Copy / Destroy, e.g. stlp_list_refs.h's CountedRef and SharedRef): the
// key first, then the value; destroyed value first (the pair's destructor).
template <typename Value, typename Second>
struct PairElement {
    using Key = decltype(Value::first);

    static void Copy(Value* to, const Value& from) { Make(to, from.first, from.second); }
    template <typename V>
    static void Make(Value* to, const Key& key, const V& value) {
        new (&to->first) Key(key);
        Second::Copy(&to->second, value);
    }
    static void Destroy(Value* p) {
        Second::Destroy(&p->second);
        p->first.~Key();
    }
};

template <typename Map, typename E = typename Map::Element>
struct TreeAbi : Map {
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

    // map: a header from the pool, no elements, empty. The compare byte is the
    // original's uninitialised less<> temporary; ours writes 0.
    static Map* __fastcall Construct(Map* self) {
        self->Header() = allocate_as<Node>();
        self->Count() = 0;
        self->Compare() = 0;
        self->EmptyInitialize();
        return self;
    }
    // _Rb_tree(const Compare&, const allocator&).
    Map* ConstructWith(const std::uint8_t* compare, const void* /*alloc*/) {
        this->Header() = allocate_as<Node>();
        this->Count() = 0;
        this->Compare() = *compare;
        Map::EmptyInitialize();
        return this;
    }
    // _Rb_tree_base(const allocator&): the header only.
    Map* ConstructBase(const void* /*alloc*/) {
        this->Header() = allocate_as<Node>();
        return this;
    }
    // The copy constructor.
    Map* CopyConstruct(const Map* from) {
        Map::template CopyFrom<E>(*from);
        return this;
    }
    // _M_empty_initialize.
    static void __fastcall EmptyInitialize(Map* self) { self->EmptyInitialize(); }
    // clear.
    static void __fastcall Clear(Map* self) { self->template Clear<E>(); }
    // ~_Rb_tree: clear, then the header back to the pool.
    static void __fastcall Destroy(Map* self) { self->template Destroy<E>(); }
    // ~_Rb_tree_base: the header back to the pool when there is one.
    static void __fastcall FreeHeader(Map* self) {
        if (self->Header()) deallocate(self->Header(), sizeof(Node));
    }
    // _STLP_alloc_proxy<Node>::deallocate(p, n).
    void DeallocateNodes(Node* p, std::size_t n) {
        if (p) deallocate(p, n * sizeof(Node));
    }
    // _STLP_alloc_proxy<Node>::allocate(n, hint): n nodes' bytes from the
    // pool (operator new above 128), null for none.
    Node* AllocateNodes(std::size_t n, const void* /*hint*/) { return allocate_as<Node>(n); }
    // The same as a static member, p in ECX and n in EDX.
    static void __fastcall DeallocateNodesFast(Node* p, std::size_t n) {
        if (p) deallocate(p, n * sizeof(Node));
    }

    // operator[]. The default value is a V temporary; for
    // the record and attached-id maps some of its bytes are uninitialised stack, ours zero.
    V* Subscript(const K& key) { return &Map::template Subscript<E>(key); }
    // lower_bound / upper_bound / find.
    Node** LowerBound(Node** out, const K& key) const {
        *out = this->lower_bound(key);
        return out;
    }
    Node** UpperBound(Node** out, const K& key) const {
        *out = this->upper_bound(key);
        return out;
    }
    Node** Find(Node** out, const K& key) const {
        *out = this->find(key);
        return out;
    }
    // begin.
    Node** Begin(Node** out) const {
        *out = this->begin();
        return out;
    }

    // _M_create_node.
    Node* CreateNode(const Value& v) { return Map::template CreateNode<E>(v); }
    // _M_copy(x, p).
    Node* CopySubtree(const Node* x, Node* p) { return Map::template CopySubtree<E>(x, p); }
    // _M_erase(x).
    void EraseSubtree(Node* x) { Map::template EraseSubtree<E>(x); }
    // _M_insert(x, y, v).
    Node** InsertAt(Node** out, Node* x, Node* y, const Value& v) {
        *out = Map::template Insert<E>(x, y, v);
        return out;
    }
    // insert_unique(v).
    InsertResult* InsertUnique(InsertResult* out, const Value& v) {
        std::pair<Node*, bool> r = Map::template InsertUnique<E>(v);
        out->node = r.first;
        out->inserted = r.second;
        return out;
    }
    // insert_unique(hint, v) and map::insert(hint, v).
    Node** InsertHint(Node** out, Node* hint, const Value& v) {
        *out = Map::template InsertUnique<E>(hint, v);
        return out;
    }
    // erase(position).
    void EraseAt(Node* position) { Map::template Erase<E>(position); }
    // erase(first, last).
    void EraseRange(Node* first, Node* last) { Map::template Erase<E>(first, last); }
    // erase(key): the number erased.
    std::size_t EraseKey(const K& key) { return Map::template EraseKey<E>(key); }

    // _M_destroy_node(p): the value destroyed, the node back to the pool.
    void DestroyNode(Node* p) { Map::template DestroyNode<E>(p); }
    // insert_equal(v): a multimap's insert, equal keys after the others.
    Node** InsertEqual(Node** out, const Value& v) {
        *out = Map::template InsertEqual<E>(v);
        return out;
    }
    // equal_range(key): the pair of iterators through the hidden pointer.
    Node** EqualRange(Node** out, const K& key) const {
        std::pair<Node*, Node*> r = this->equal_range(key);
        out[0] = r.first;
        out[1] = r.second;
        return out;
    }

    // The pair<const K, V> value's members at the instantiation's addresses.
    struct ValueAbi : Value {
        // pair<const K, V>(const K&, const V&): the key, then the value
        // copied as a node's value is.
        Value* MakeValue(const K& key, const V& value) {
            E::Make(this, key, value);
            return this;
        }
        // ~pair: the value's destructor.
        void DestroyValue() { E::Destroy(this); }
        // The pair's scalar deleting destructor: the destructor, then operator
        // delete when bit 0 of the flags is set.
        Value* DeletingDestroyValue(unsigned flags) {
            E::Destroy(this);
            if (flags & 1) gamecrt::operator_delete(this);
            return this;
        }
    };
};

// Members that only move node pointers, one body for every instantiation.
// An iterator.
struct TreeIterator {
    // The copy constructor.
    TreeIterator* CopyConstruct(const TreeIterator& from) {
        node = from.node;
        return this;
    }

    RbNodeBase* node;
};
// pair<iterator, bool>: the iterator, then the flag byte.
struct TreeInsertResult {
    // pair<iterator, bool>(const iterator&, const bool&): the flag byte
    // stored alone.
    TreeInsertResult* Construct(const TreeIterator& iterator, const bool& flag) {
        node = iterator.node;
        inserted = flag;
        return this;
    }

    RbNodeBase* node;
    std::uint8_t inserted;
};
// A counter that distance() adds to (ECX is the counter).
struct TreeDistanceCount {
    // __distance(first, last, n&, tag) with n in ECX: n raised by the
    // number of steps from first to last.
    void DistanceTagged(RbNodeBase* first, RbNodeBase* last, std::uint32_t /*tag*/) {
        for (; first != last; first = rb::Next(first)) ++count;
    }
    // distance(first, last, n&) with n in ECX.
    void Distance(RbNodeBase* first, RbNodeBase* last) { DistanceTagged(first, last, 0); }

    std::size_t count;
};
// The allocator, one byte.
struct TreeAllocator {
    // get_allocator: the allocator byte copied out.
    std::uint8_t* GetAllocator(std::uint8_t* out) const {
        *out = byte;
        return out;
    }

    std::uint8_t byte;
};
// _STLP_alloc_proxy: the allocator byte, then the header pointer over it.
struct TreeAllocProxy {
    // _STLP_alloc_proxy(const allocator&, const value&).
    TreeAllocProxy* Construct(const std::uint8_t& alloc, void* const& value) {
        *reinterpret_cast<std::uint8_t*>(this) = alloc;
        header = value;
        return this;
    }

    void* header;
};

} // namespace stlp
