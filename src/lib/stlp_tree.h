#pragma once

// STLport's red-black tree (_Rb_tree, map, multimap, set) as compiled into game.exe: SGI's 3.x tree. Layout:
// docs/notes/standalone.md, "The image's source files".

#include "stlp_alloc.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace stlp {

struct RbNodeBase {
    static constexpr std::uint8_t kRed = 0;
    static constexpr std::uint8_t kBlack = 1;

    std::uint8_t color;
    std::uint8_t unused_01[3];
    RbNodeBase* parent;
    RbNodeBase* left;
    RbNodeBase* right;

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
// _Rb_global<bool> (the node-only algorithms), with their calling conventions: the node in ECX, the root (a
// reference to the header's parent) in EDX. _Rb_tree_base_iterator::_M_increment / _M_decrement on the
// iterator (one node pointer) in ECX.
void Increment(RbNodeBase** it);
void Decrement(RbNodeBase** it);
void RotateLeft(RbNodeBase* x, RbNodeBase*& root);
void RotateRight(RbNodeBase* x, RbNodeBase*& root);
// X (just linked in) red, then recoloured and rotated up to the root, which ends black.
void Rebalance(RbNodeBase* x, RbNodeBase*& root);
// Z unlinked (its successor takes its place and colour when it has two
// children), leftmost / rightmost kept, the tree recoloured and rotated; returns the node to free.
RbNodeBase* RebalanceForErase(RbNodeBase* z, RbNodeBase*& root, RbNodeBase*& leftmost,
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

// How a node's value is copied in (_M_create_node) and destroyed (destroy_node): its own copy constructor and
// destructor. Holders whose copy the game writes as functions of the holder get policies of their own.
template <typename T>
struct TreeElement {
    static void Copy(T* to, const T& from) { new (to) T(from); }
    // pair(key, value) in place: the key copied, then the value.
    template <typename K, typename V>
    static void Make(T* to, const K& key, const V& value) { new (to) T{key, value}; }
    static void Destroy(T* p) { p->~T(); }
};

// The key order of a map: operator< (signed for int keys, unsigned for pointers and unsigned keys,
// bytes for std::string). Keys without one (MFC's CString, compared with _mbscmp) get an order of
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
        std::uint8_t color;
        std::uint8_t unused_01[3];
        Node* parent;
        Node* left;
        Node* right;
        K first;
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
    Node* find(const K& key) const {
        Node* n = lower_bound(key);
        return n == header_ || KeyLess(key, n->first) ? header_ : n;
    }
    // upper_bound: the first node whose key is greater than `key`, or end(). With lower_bound, the
    // equal range of a multimap.
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
    void EmptyInitialize() {
        header_->color = RbNodeBase::kRed;
        header_->parent = nullptr;
        header_->left = header_;
        header_->right = header_;
    }
    void ResetToEmpty() {
        header_->left = header_;
        header_->parent = nullptr;
        header_->right = header_;
        count_ = 0;
    }
    void Construct() {
        header_ = allocate_as<Node>();
        EmptyInitialize();
        count_ = 0;
    }
    template <typename E = Element>
    void Clear() {
        if (count_ == 0) return;
        EraseSubtree<E>(header_->parent);
        ResetToEmpty();
    }
    template <typename E = Element>
    void Destroy() {
        Clear<E>();
        deallocate(header_, sizeof(Node));
    }
    // _M_erase: the right subtrees recursively, the left spine iteratively; each
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
    template <typename E = Element>
    static Node* CreateNode(const Value& v) {
        Node* z = allocate_as<Node>();
        E::Copy(reinterpret_cast<Value*>(&z->first), v);
        return z;
    }
    template <typename E = Element>
    static void DestroyNode(Node* n) {
        E::Destroy(reinterpret_cast<Value*>(&n->first));
        deallocate(n, sizeof(Node));
    }

    template <typename E = Element>
    static Node* CloneNode(const Node* x) {
        Node* z = CreateNode<E>(*reinterpret_cast<const Value*>(&x->first));
        z->color = x->color;
        z->left = nullptr;
        z->right = nullptr;
        return z;
    }
    // _M_copy(x, p): a copy of the subtree at x under p, the right subtrees
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
    // The copy constructor: a header from the pool, the compare byte, the nodes
    // copied with their colours (_M_copy) and leftmost / rightmost found again, then the count.
    template <typename E = Element>
    void CopyFrom(const map& from) {
        header_ = allocate_as<Node>();
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

    // _M_insert(x, y, v): a new node as y's left child when y is the header, x is set or v's key is less than
    // y's, else as its right; leftmost / rightmost kept, then rebalanced from the new node and counted.
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
    // insert_unique(hint, v): at the hint when v goes right before it (or
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
    std::pair<Node*, Node*> equal_range(const K& key) const { return {lower_bound(key), upper_bound(key)}; }
    template <typename E = Element>
    void Erase(Node* position) {
        RbNodeBase* h = header_->Base();
        Node* y = Node::From(rb::RebalanceForErase(position->Base(), h->parent, h->left, h->right));
        DestroyNode<E>(y);
        --count_;
    }
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
    template <typename E = Element>
    std::size_t EraseKey(const K& key) {
        Node* last = upper_bound(key);
        Node* first = lower_bound(key);
        std::size_t n = 0;
        for (Node* p = first; p != last; p = Node::From(rb::Next(p->Base()))) ++n;
        Erase<E>(first, last);
        return n;
    }
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
    // The first node in key order (end() when empty) and the in-order successor.
    Node* begin() const { return header_->left; }
    static Node* Next(Node* n) { return Node::From(rb::Next(n->Base())); }

private:
    Node* header_ = nullptr;
    std::size_t count_ = 0;
    std::uint8_t compare_ = 0;
    std::uint8_t unused_09[3] = {};
};

} // namespace stlp
