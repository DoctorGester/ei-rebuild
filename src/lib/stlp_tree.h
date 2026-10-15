#pragma once

// STLport's red-black tree (_Rb_tree, map, multimap, set) as compiled into game.exe (stlp.h
// includes it; docs/notes/standalone.md, section 2). The STLport of the exe is SGI's 3.x tree:
// _M_insert(x, y, v) without the later hint argument, the hint insert falling back to
// insert_unique(v), erase(key) through equal_range.
//
// A tree is 0xC bytes: the header node (from the node pool; its parent is the root, its left /
// right the leftmost / rightmost node, red), the element count and the empty compare object (one
// byte). Nodes are color (a bool: red 0, black 1, in a dword whose other bytes are left as the
// allocation had them), parent, left, right, then the value. The node-only algorithms
// (_Rb_global<bool>: increment, decrement, the rotations and both rebalances) are one body each in
// stlp_tree.cpp, registered at their addresses.

#include "stlp_alloc.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace stlp {

struct RbNodeBase {
    static constexpr std::uint8_t kRed = 0;
    static constexpr std::uint8_t kBlack = 1;

    std::uint8_t color;          // +0x00
    std::uint8_t unused_01[3];
    RbNodeBase* parent;          // +0x04
    RbNodeBase* left;            // +0x08
    RbNodeBase* right;           // +0x0C

    static RbNodeBase* Minimum(RbNodeBase* x) {
        while (x->left) x = x->left;
        return x;
    }
    static RbNodeBase* Maximum(RbNodeBase* x) {
        while (x->right) x = x->right;
        return x;
    }
};
static_assert(sizeof(RbNodeBase) == 0x10);

namespace rb {
// _Rb_global<bool> (the node-only algorithms), with the original's calling conventions: the node
// in ECX, the root (a reference to the header's parent) in EDX.
// 0043DBE0 / 0043FBE0: _Rb_tree_base_iterator::_M_increment / _M_decrement on the iterator (one
// node pointer) in ECX.
void __fastcall Increment(RbNodeBase** it);
void __fastcall Decrement(RbNodeBase** it);
// 0043E300 / 0043E350.
void __fastcall RotateLeft(RbNodeBase* x, RbNodeBase*& root);
void __fastcall RotateRight(RbNodeBase* x, RbNodeBase*& root);
// 0043FE90: x (just linked in) red, then recoloured and rotated up to the root, which ends black.
void __fastcall Rebalance(RbNodeBase* x, RbNodeBase*& root);
// 0043D830 (RET 8): z unlinked (its successor takes its place and colour when it has two
// children), leftmost / rightmost kept, the tree recoloured and rotated; returns the node to free.
RbNodeBase* __fastcall RebalanceForErase(RbNodeBase* z, RbNodeBase*& root, RbNodeBase*& leftmost,
                                         RbNodeBase*& rightmost);

inline RbNodeBase* Next(RbNodeBase* n) {
    Increment(&n);
    return n;
}
inline RbNodeBase* Previous(RbNodeBase* n) {
    Decrement(&n);
    return n;
}
} // namespace rb

// How a node's value is copied in (_M_create_node) and destroyed (destroy_node): its own copy
// constructor and destructor. Holders whose copy the game writes as functions of the holder get
// policies of their own.
template <typename T>
struct TreeElement {
    static void Copy(T* to, const T& from) { new (to) T(from); }
    // pair(key, value) in place (e.g. 00445170): the key copied, then the value.
    template <typename K, typename V>
    static void Make(T* to, const K& key, const V& value) { new (to) T{key, value}; }
    static void Destroy(T* p) { p->~T(); }
};

// The key order of a map: operator< (signed for int keys, unsigned for pointers and unsigned keys,
// bytes for stlp::string). Keys without one (MFC's CString, compared with _mbscmp) get an order of
// their own as map's third argument.
template <typename K>
struct TreeLess {
    static bool Less(const K& a, const K& b) { return a < b; }
};

// map<K, V> (and multimap): keys ordered by L. V is the mapped type; the node's value is the pair
// {first, second}.
template <typename K, typename V, typename L = TreeLess<K>>
class map {
public:
    struct Node {
        std::uint8_t color;      // +0x00
        std::uint8_t unused_01[3];
        Node* parent;            // +0x04
        Node* left;              // +0x08
        Node* right;             // +0x0C
        K first;                 // +0x10
        V second;

        RbNodeBase* Base() { return reinterpret_cast<RbNodeBase*>(this); }
        static Node* From(RbNodeBase* n) { return reinterpret_cast<Node*>(n); }
    };
    // value_type: pair<const K, V> as laid out in the node.
    struct Value {
        K first;
        V second;
    };
    using Element = TreeElement<Value>;
    using key_type = K;
    using mapped_type = V;

    static bool KeyLess(const K& a, const K& b) { return L::Less(a, b); }
    static bool SameKey(const K& a, const K& b) { return !L::Less(a, b) && !L::Less(b, a); }

    map() = default;
    map(const map&) = delete;

    Node* end() const { return header_; }
    std::size_t size() const { return count_; }
    Node* Root() const { return header_->parent; }
    Node*& Leftmost() { return header_->left; }
    Node*& Rightmost() { return header_->right; }

    // lower_bound (e.g. 00439AA0): the first node whose key is not less than `key`, or end().
    Node* lower_bound(const K& key) const {
        Node* result = header_;
        for (Node* n = header_->parent; n;) {
            if (KeyLess(n->first, key)) {
                n = n->right;
            } else {
                result = n;
                n = n->left;
            }
        }
        return result;
    }
    // find (e.g. 0043CE70): the node holding `key`, or end().
    Node* find(const K& key) const {
        Node* n = lower_bound(key);
        return n == header_ || KeyLess(key, n->first) ? header_ : n;
    }
    // upper_bound: the first node whose key is greater than `key`, or end(). With lower_bound, the
    // equal range of a multimap (e.g. 004FB5E0).
    Node* upper_bound(const K& key) const {
        Node* result = header_;
        for (Node* n = header_->parent; n;) {
            if (KeyLess(key, n->first)) {
                result = n;
                n = n->left;
            } else {
                n = n->right;
            }
        }
        return result;
    }
    // _M_empty_initialize (e.g. 0043A570): the header red, no root, its left / right linked to
    // itself (in that order).
    void EmptyInitialize() {
        header_->color = RbNodeBase::kRed;
        header_->parent = nullptr;
        header_->left = header_;
        header_->right = header_;
    }
    // clear()'s end, once the nodes are freed: the header links to itself, no root, no elements.
    void ResetToEmpty() {
        header_->left = header_;
        header_->parent = nullptr;
        header_->right = header_;
        count_ = 0;
    }
    // The constructor (e.g. 004393C0 / 0043A590 + 0043A570): a header node from the node pool,
    // _M_empty_initialize, no elements.
    void Construct() {
        header_ = static_cast<Node*>(allocate(sizeof(Node)));
        EmptyInitialize();
        count_ = 0;
    }
    // clear() (e.g. 00439AE0): every node destroyed (_M_erase) and the tree emptied; nothing
    // happens when there are no elements.
    template <typename E = Element>
    void Clear() {
        if (count_ == 0) return;
        EraseSubtree<E>(header_->parent);
        ResetToEmpty();
    }
    // The destructor: clear, then the header node back to the pool (e.g. 004F53B0 / 004F5360).
    template <typename E = Element>
    void Destroy() {
        Clear<E>();
        deallocate(header_, sizeof(Node));
    }
    // _M_erase (e.g. 0043CB70): the right subtrees recursively, the left spine iteratively; each
    // node's value destroyed and the node returned to the pool.
    template <typename E = Element>
    static void EraseSubtree(Node* n) {
        while (n) {
            EraseSubtree<E>(n->right);
            Node* left = n->left;
            DestroyNode<E>(n);
            n = left;
        }
    }
    // _M_create_node (e.g. 0043DF00): a node from the pool, the value copied into it.
    template <typename E = Element>
    static Node* CreateNode(const Value& v) {
        Node* z = static_cast<Node*>(allocate(sizeof(Node)));
        E::Copy(reinterpret_cast<Value*>(&z->first), v);
        return z;
    }
    template <typename E = Element>
    static void DestroyNode(Node* n) {
        E::Destroy(reinterpret_cast<Value*>(&n->first));
        deallocate(n, sizeof(Node));
    }

    // _M_clone_node: a node with a copy of x's value and colour, no children.
    template <typename E = Element>
    static Node* CloneNode(const Node* x) {
        Node* z = CreateNode<E>(*reinterpret_cast<const Value*>(&x->first));
        z->color = x->color;
        z->left = nullptr;
        z->right = nullptr;
        return z;
    }
    // _M_copy(x, p) (e.g. 0043BA60; RET 8): a copy of the subtree at x under p, the right subtrees
    // recursively and the left spine iteratively; returns the copy of x.
    template <typename E = Element>
    static Node* CopySubtree(const Node* x, Node* p) {
        Node* top = CloneNode<E>(x);
        top->parent = p;
        if (x->right) top->right = CopySubtree<E>(x->right, top);
        p = top;
        for (x = x->left; x; x = x->left) {
            Node* y = CloneNode<E>(x);
            p->left = y;
            y->parent = p;
            if (x->right) y->right = CopySubtree<E>(x->right, y);
            p = y;
        }
        return top;
    }
    // The copy constructor (e.g. 00437AA0): a header from the pool, the compare byte, the nodes
    // copied with their colours (_M_copy) and leftmost / rightmost found again, then the count.
    template <typename E = Element>
    void CopyFrom(const map& from) {
        header_ = static_cast<Node*>(allocate(sizeof(Node)));
        count_ = 0;
        compare_ = from.compare_;
        if (!from.header_->parent) {
            EmptyInitialize();
        } else {
            header_->color = RbNodeBase::kRed;
            Node* root = CopySubtree<E>(from.header_->parent, header_);
            header_->parent = root;
            header_->left = Node::From(RbNodeBase::Minimum(root->Base()));
            header_->right = Node::From(RbNodeBase::Maximum(root->Base()));
        }
        count_ = from.count_;
    }

    // _M_insert(x, y, v) (e.g. 0043E140; RET 0x10): a new node as y's left child when y is the
    // header, x is set or v's key is less than y's, else as its right; leftmost / rightmost kept,
    // then rebalanced from the new node and counted.
    template <typename E = Element>
    Node* Insert(Node* x, Node* y, const Value& v) {
        Node* z;
        if (y == header_ || x || KeyLess(v.first, y->first)) {
            z = CreateNode<E>(v);
            y->left = z;   // also the leftmost when y is the header
            if (y == header_) {
                header_->parent = z;
                header_->right = z;
            } else if (y == header_->left) {
                header_->left = z;
            }
        } else {
            z = CreateNode<E>(v);
            y->right = z;
            if (y == header_->right) header_->right = z;
        }
        z->parent = y;
        z->left = nullptr;
        z->right = nullptr;
        rb::Rebalance(z->Base(), header_->Base()->parent);
        ++count_;
        return z;
    }
    // insert_unique(v) (RET 8 with the pair returned through a hidden pointer): down from the
    // root to a leaf, then the node before the leaf's place checked for an equal key.
    template <typename E = Element>
    std::pair<Node*, bool> InsertUnique(const Value& v) {
        Node* y = header_;
        Node* x = header_->parent;
        bool comp = true;
        while (x) {
            y = x;
            comp = KeyLess(v.first, x->first);
            x = comp ? x->left : x->right;
        }
        Node* j = y;
        if (comp) {
            if (j == header_->left) return {Insert<E>(x, y, v), true};
            j = Node::From(rb::Previous(j->Base()));
        }
        if (KeyLess(j->first, v.first)) return {Insert<E>(x, y, v), true};
        return {j, false};
    }
    // insert_unique(hint, v) (e.g. 0043CBC0; RET 0xC): at the hint when v goes right before it (or
    // after the last element for end()), else insert_unique(v).
    template <typename E = Element>
    Node* InsertUnique(Node* position, const Value& v) {
        if (position == header_->left) {   // begin()
            if (count_ > 0 && KeyLess(v.first, position->first)) return Insert<E>(position, position, v);
            return InsertUnique<E>(v).first;
        }
        if (position == header_) {   // end()
            if (KeyLess(header_->right->first, v.first)) return Insert<E>(nullptr, header_->right, v);
            return InsertUnique<E>(v).first;
        }
        Node* before = Node::From(rb::Previous(position->Base()));
        if (KeyLess(before->first, v.first) && KeyLess(v.first, position->first)) {
            if (!before->right) return Insert<E>(nullptr, before, v);
            return Insert<E>(position, position, v);
        }
        return InsertUnique<E>(v).first;
    }
    // insert_equal(v): down from the root (equal keys go right), then _M_insert.
    template <typename E = Element>
    Node* InsertEqual(const Value& v) {
        Node* y = header_;
        Node* x = header_->parent;
        while (x) {
            y = x;
            x = KeyLess(v.first, x->first) ? x->left : x->right;
        }
        return Insert<E>(x, y, v);
    }
    // equal_range(key) (e.g. 004FB5E0): lower_bound, then upper_bound.
    std::pair<Node*, Node*> equal_range(const K& key) const { return {lower_bound(key), upper_bound(key)}; }
    // erase(position) (e.g. 004450F0): unlinked by _Rebalance_for_erase, the node destroyed and
    // freed, counted down.
    template <typename E = Element>
    void Erase(Node* position) {
        RbNodeBase* h = header_->Base();
        Node* y = Node::From(rb::RebalanceForErase(position->Base(), h->parent, h->left, h->right));
        DestroyNode<E>(y);
        --count_;
    }
    // erase(first, last): clear() for the whole tree, else one node at a time.
    template <typename E = Element>
    void Erase(Node* first, Node* last) {
        if (first == header_->left && last == header_) {
            Clear<E>();
            return;
        }
        while (first != last) {
            Node* next = Node::From(rb::Next(first->Base()));
            Erase<E>(first);
            first = next;
        }
    }
    // erase(key) (e.g. 0043C3B0): the equal range (upper_bound, then lower_bound), its length
    // counted, then erased; returns the length.
    template <typename E = Element>
    std::size_t EraseKey(const K& key) {
        Node* last = upper_bound(key);
        Node* first = lower_bound(key);
        std::size_t n = 0;
        for (Node* p = first; p != last; p = Node::From(rb::Next(p->Base()))) ++n;
        Erase<E>(first, last);
        return n;
    }
    // operator[] (e.g. 004382A0): lower_bound, and when the key is not there a value-initialised
    // element inserted at that hint; returns the mapped value.
    template <typename E = Element>
    V& Subscript(const K& key) {
        Node* i = lower_bound(key);
        if (i == header_ || KeyLess(key, i->first)) {
            alignas(Value) unsigned char storage[sizeof(Value)];
            Value* v = reinterpret_cast<Value*>(storage);
            E::Make(v, key, V());
            i = InsertUnique<E>(i, *v);
            E::Destroy(v);
        }
        return i->second;
    }

    // The element count and the compare byte by reference, for owners whose constructors set them
    // by hand (the connection's trees).
    std::size_t& Count() { return count_; }
    std::uint8_t& Compare() { return compare_; }
    Node*& Header() { return header_; }
    // The first node in key order (end() when empty) and the in-order successor (0043DBE0).
    Node* begin() const { return header_->left; }
    static Node* Next(Node* n) { return Node::From(rb::Next(n->Base())); }

private:
    Node* header_ = nullptr;
    std::size_t count_ = 0;
    std::uint8_t compare_ = 0;
    std::uint8_t unused_09[3] = {};
};

} // namespace stlp
