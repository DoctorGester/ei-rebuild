#pragma once

// Checks of the STLport map instantiations (stlp_tree_abi.h's TreeAbi), shared by
// verify_net_connection_trees.cpp and verify_library_trees.cpp.
//
// Per call:
// - lookups (find, lower_bound, begin, equal_range), the iterator copy, get_allocator, the alloc
//   proxy and pair constructors and distance are pure or write only their output: the original runs
//   on a scratch output, then ours; the same result.
// - _M_empty_initialize: the original, the header's bytes kept, then ours over it: the same bytes.
// - The constructors take a header from the pool: the original builds, its header goes back on top
//   of the free list, then ours: the same header, the same words.
// - The rest change the tree and its values (reference counts, string blocks), so ours runs alone
//   and the state left is checked: the copy constructor and _M_copy (the same shape, colours and
//   keys as the source), insert and operator[] (the node returned holds the key, the count up by at
//   most one), erase (the count down by what went, an erased key no longer found), clear (empty).
// A start-up script (Script below) runs an instantiation's originals against ours: two trees built
// side by side from one random sequence of inserts and erases, compared after every step.

#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_tree_abi.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace stlp_tree_verify {

using stlp::RbNodeBase;
using stlp_verify::Name;

template <typename T>
std::uintptr_t Word(T v) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(v);
    else return static_cast<std::uintptr_t>(v);
}

// Values compared as bytes (plain keys and values).
struct SameBytes {
    template <typename Value>
    static bool Same(const Value& a, const Value& b) {
        return std::memcmp(&a, &b, sizeof(Value)) == 0;
    }
};

// --- Tree comparisons ---------------------------------------------------------------------------

template <typename Abi, typename Values = SameBytes>
bool SameSubtree(const typename Abi::Node* x, const typename Abi::Node* y, const typename Abi::Node* xParent,
                 const typename Abi::Node* yParent) {
    using Value = typename Abi::Value;
    if (!x || !y) return !x && !y;
    if (x->parent != xParent || y->parent != yParent || x->color != y->color ||
        !Values::Same(*reinterpret_cast<const Value*>(&x->first), *reinterpret_cast<const Value*>(&y->first)))
        return false;
    return SameSubtree<Abi, Values>(x->left, y->left, x, y) && SameSubtree<Abi, Values>(x->right, y->right, x, y);
}

// The same count, shape, colours and values, each header linked to its own leftmost / rightmost.
template <typename Abi, typename Values = SameBytes>
bool SameTree(typename Abi::Tree& a, typename Abi::Tree& b) {
    using Node = typename Abi::Node;
    Node* ha = a.end();
    Node* hb = b.end();
    if (a.size() != b.size() || ha->color != hb->color) return false;
    if (!SameSubtree<Abi, Values>(ha->parent, hb->parent, ha, hb)) return false;
    if (!ha->parent) return ha->left == ha && ha->right == ha && hb->left == hb && hb->right == hb;
    return ha->left == Node::From(RbNodeBase::Minimum(ha->parent->Base())) &&
           ha->right == Node::From(RbNodeBase::Maximum(ha->parent->Base())) &&
           hb->left == Node::From(RbNodeBase::Minimum(hb->parent->Base())) &&
           hb->right == Node::From(RbNodeBase::Maximum(hb->parent->Base()));
}

// A copy against its source: the same shape, colours and keys (values may be copies).
template <typename Abi>
bool SameKeys(const typename Abi::Node* x, const typename Abi::Node* y) {
    if (!x || !y) return !x && !y;
    return x->color == y->color && Abi::Tree::SameKey(x->first, y->first) && SameKeys<Abi>(x->left, y->left) &&
           SameKeys<Abi>(x->right, y->right);
}

template <typename Abi>
bool Empty(typename Abi::Tree& t) {
    auto* h = t.end();
    return t.size() == 0 && !h->parent && h->left == h && h->right == h;
}

// --- Per-call wrappers --------------------------------------------------------------------------

template <std::uintptr_t A, typename Abi>
struct TreeCheck {
    using Tree = typename Abi::Tree;
    using Node = typename Abi::Node;
    using Value = typename Abi::Value;
    using K = typename Abi::K;
    using V = typename Abi::V;
    using InsertResult = typename Abi::InsertResult;
    static inline void* original = nullptr;

    template <typename Fn>
    static Fn Original() {
        return reinterpret_cast<Fn>(original);
    }
    static bool HasKey(const Node* n, const K& key) { return Tree::SameKey(n->first, key); }

    // --- Pure members ---
    template <typename Fn>
    static Node** Lookup(const char* kind, Fn ours, const Tree* self, Node** out, const K* key) {
        Node* theirs = reinterpret_cast<Node*>(1);
        bool returned = Original<Fn>()(self, nullptr, &theirs, key) == &theirs;
        Node** result = ours(self, nullptr, out, key);
        bool ok = returned && result == out && *out == theirs;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p", static_cast<void*>(*out), static_cast<void*>(theirs));
        ei::verify::Report(Name<A>(kind), ok, detail);
        return result;
    }
    static Node** __fastcall Find(const Tree* self, void*, Node** out, const K* key) {
        return Lookup("stlp::map::find", &Abi::Find, self, out, key);
    }
    static Node** __fastcall LowerBound(const Tree* self, void*, Node** out, const K* key) {
        return Lookup("stlp::map::lower_bound", &Abi::LowerBound, self, out, key);
    }
    static Node** __fastcall Begin(const Tree* self, void*, Node** out) {
        Node* theirs = reinterpret_cast<Node*>(1);
        bool returned = Original<decltype(&Abi::Begin)>()(self, nullptr, &theirs) == &theirs;
        Node** result = Abi::Begin(self, nullptr, out);
        ei::verify::Report(Name<A>("stlp::map::begin"), returned && result == out && *out == theirs, "");
        return result;
    }
    static Node** __fastcall EqualRange(const Tree* self, void*, Node** out, const K* key) {
        Node* theirs[2] = {reinterpret_cast<Node*>(1), reinterpret_cast<Node*>(1)};
        bool returned = Original<decltype(&Abi::EqualRange)>()(self, nullptr, theirs, key) == theirs;
        Node** result = Abi::EqualRange(self, nullptr, out, key);
        bool ok = returned && result == out && out[0] == theirs[0] && out[1] == theirs[1];
        ei::verify::Report(Name<A>("stlp::_Rb_tree::equal_range"), ok, "");
        return result;
    }
    static void __fastcall EmptyInitialize(Tree* self) {
        unsigned char before[sizeof(RbNodeBase)];
        unsigned char theirs[sizeof(RbNodeBase)];
        std::memcpy(before, self->end(), sizeof(before));
        Original<decltype(&Abi::EmptyInitialize)>()(self);
        std::memcpy(theirs, self->end(), sizeof(theirs));
        std::memcpy(self->end(), before, sizeof(before));
        Abi::EmptyInitialize(self);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_M_empty_initialize"),
                           std::memcmp(theirs, self->end(), sizeof(theirs)) == 0, "");
    }

    // --- Constructors: the original's header back on the free list, then ours ---
    // Returns whether ours took the same header and wrote the same words (`words` of the tree, the
    // header's 16 bytes when `header`).
    template <typename Build>
    static bool SameBuild(Tree* self, Build build, std::size_t words, bool header) {
        build(true);
        unsigned char tree[sizeof(Tree)];
        unsigned char head[sizeof(RbNodeBase)];
        std::memcpy(tree, self, sizeof(tree));
        Node* theirs = self->end();
        if (header) std::memcpy(head, theirs, sizeof(head));
        stlp::deallocate(theirs, sizeof(Node));
        build(false);
        // The header's colour byte and links (its other three bytes are what the pool left: the
        // free list's link, which putting it back rewrites).
        const unsigned char* ours = reinterpret_cast<const unsigned char*>(theirs);
        return self->end() == theirs && std::memcmp(tree, self, words * 4) == 0 &&
               (!header || (head[0] == ours[0] && std::memcmp(head + 4, ours + 4, sizeof(head) - 4) == 0));
    }
    static Tree* __fastcall Construct(Tree* self) {
        bool ok = SameBuild(self, [&](bool orig) { orig ? Original<decltype(&Abi::Construct)>()(self) : Abi::Construct(self); },
                            2, true);
        ei::verify::Report(Name<A>("stlp::map::map"), ok, "");
        return self;
    }
    static Tree* __fastcall ConstructWith(Tree* self, void*, const std::uint8_t* compare, const void* alloc) {
        bool ok = SameBuild(self, [&](bool orig) {
            orig ? Original<decltype(&Abi::ConstructWith)>()(self, nullptr, compare, alloc)
                 : Abi::ConstructWith(self, nullptr, compare, alloc);
        }, 3, true);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_Rb_tree"), ok, "");
        return self;
    }
    static Tree* __fastcall ConstructBase(Tree* self, void*, const void* alloc) {
        bool ok = SameBuild(self, [&](bool orig) {
            orig ? Original<decltype(&Abi::ConstructBase)>()(self, nullptr, alloc) : Abi::ConstructBase(self, nullptr, alloc);
        }, 1, false);
        ei::verify::Report(Name<A>("stlp::_Rb_tree_base::_Rb_tree_base"), ok, "");
        return self;
    }

    // --- Ours only ---
    static Tree* __fastcall CopyConstruct(Tree* self, void*, const Tree* from) {
        Tree* result = Abi::CopyConstruct(self, nullptr, from);
        Node* h = self->end();
        Node* fh = from->end();
        bool ok = result == self && self->size() == from->size() && h != fh && SameKeys<Abi>(h->parent, fh->parent) &&
                  (h->parent ? HasKey(h->left, fh->left->first) && HasKey(h->right, fh->right->first)
                             : h->left == h && h->right == h);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_Rb_tree(const _Rb_tree&)"), ok, "");
        return result;
    }
    static Node* __fastcall CopySubtree(Tree* self, void*, const Node* x, Node* p) {
        Node* result = Abi::CopySubtree(self, nullptr, x, p);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_M_copy"), result->parent == p && SameKeys<Abi>(result, x), "");
        return result;
    }
    static void __fastcall EraseSubtree(Tree* self, void*, Node* x) {
        Abi::EraseSubtree(self, nullptr, x);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_M_erase"), true, "");
    }
    static Node* __fastcall CreateNode(Tree* self, void*, const Value* v) {
        Node* result = Abi::CreateNode(self, nullptr, v);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_M_create_node"), result && HasKey(result, v->first), "");
        return result;
    }
    static void __fastcall DestroyNode(Tree* self, void*, Node* p) {
        Abi::DestroyNode(self, nullptr, p);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_M_destroy_node"), true, "");
    }
    static V* __fastcall Subscript(Tree* self, void*, const K* key) {
        std::size_t before = self->size();
        V* result = Abi::Subscript(self, nullptr, key);
        Node* node = self->find(*key);
        bool ok = node != self->end() && result == &node->second && self->size() - before <= 1;
        ei::verify::Report(Name<A>("stlp::map::operator[]"), ok, "");
        return result;
    }
    static Node** __fastcall InsertAt(Tree* self, void*, Node** out, Node* x, Node* y, const Value* v) {
        std::size_t before = self->size();
        Node** result = Abi::InsertAt(self, nullptr, out, x, y, v);
        bool ok = result == out && HasKey(*out, v->first) && self->size() == before + 1;
        ei::verify::Report(Name<A>("stlp::_Rb_tree::_M_insert"), ok, "");
        return result;
    }
    static InsertResult* __fastcall InsertUnique(Tree* self, void*, InsertResult* out, const Value* v) {
        std::size_t before = self->size();
        InsertResult* result = Abi::InsertUnique(self, nullptr, out, v);
        bool ok = result == out && HasKey(out->node, v->first) && self->size() == before + out->inserted;
        ei::verify::Report(Name<A>("stlp::_Rb_tree::insert_unique"), ok, "");
        return result;
    }
    static Node** __fastcall InsertHint(Tree* self, void*, Node** out, Node* hint, const Value* v) {
        std::size_t before = self->size();
        Node** result = Abi::InsertHint(self, nullptr, out, hint, v);
        bool ok = result == out && HasKey(*out, v->first) && self->size() - before <= 1;
        ei::verify::Report(Name<A>("stlp::_Rb_tree::insert_unique(hint)"), ok, "");
        return result;
    }
    static Node** __fastcall InsertEqual(Tree* self, void*, Node** out, const Value* v) {
        std::size_t before = self->size();
        Node** result = Abi::InsertEqual(self, nullptr, out, v);
        bool ok = result == out && HasKey(*out, v->first) && self->size() == before + 1;
        ei::verify::Report(Name<A>("stlp::_Rb_tree::insert_equal"), ok, "");
        return result;
    }
    static void __fastcall EraseAt(Tree* self, void*, Node* position) {
        std::size_t before = self->size();
        Abi::EraseAt(self, nullptr, position);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::erase(iterator)"), self->size() == before - 1, "");
    }
    static void __fastcall EraseRange(Tree* self, void*, Node* first, Node* last) {
        std::size_t before = self->size();
        std::size_t n = 0;
        for (Node* p = first; p != last; p = Tree::Next(p)) ++n;
        Abi::EraseRange(self, nullptr, first, last);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::erase(first, last)"), self->size() == before - n, "");
    }
    static std::size_t __fastcall EraseKey(Tree* self, void*, const K* key) {
        std::size_t before = self->size();
        std::size_t n = Abi::EraseKey(self, nullptr, key);
        bool ok = n <= 1 && self->size() == before - n && self->find(*key) == self->end();
        ei::verify::Report(Name<A>("stlp::_Rb_tree::erase(key)"), ok, "");
        return n;
    }
    static void __fastcall Clear(Tree* self) {
        Abi::Clear(self);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::clear"), Empty<Abi>(*self), "");
    }
    static void __fastcall Destroy(Tree* self) {
        Abi::Destroy(self);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::~_Rb_tree"), true, "");
    }
    static void __fastcall FreeHeader(Tree* self) {
        Abi::FreeHeader(self);
        ei::verify::Report(Name<A>("stlp::_Rb_tree_base::~_Rb_tree_base"), true, "");
    }
    static void __fastcall DeallocateNodes(Tree* self, void*, Node* p, std::size_t n) {
        Abi::DeallocateNodes(self, nullptr, p, n);
        ei::verify::Report(Name<A>("stlp::_STLP_alloc_proxy::deallocate"), true, "");
    }
    static void __fastcall DeallocateNodesFast(Node* p, std::size_t n) {
        Abi::DeallocateNodesFast(p, n);
        ei::verify::Report(Name<A>("stlp::allocator::deallocate"), true, "");
    }
    static Value* __fastcall MakeValue(Value* self, void*, const K* key, const V* value) {
        Value* result = Abi::MakeValue(self, nullptr, key, value);
        ei::verify::Report(Name<A>("stlp::pair::pair"), result == self && Tree::SameKey(self->first, *key), "");
        return result;
    }
    static void __fastcall DestroyValue(Value* self) {
        Abi::DestroyValue(self);
        ei::verify::Report(Name<A>("stlp::pair::~pair"), true, "");
    }
    static Value* __fastcall DeletingDestroyValue(Value* self, void*, unsigned flags) {
        Value* result = Abi::DeletingDestroyValue(self, nullptr, flags);
        ei::verify::Report(Name<A>("stlp::pair scalar deleting destructor"), result == self, "");
        return result;
    }
};

// The members shared by every tree, side by side with the original.
template <std::uintptr_t A>
struct SharedCheck {
    static inline void* original = nullptr;

    template <typename Fn>
    static Fn Original() {
        return reinterpret_cast<Fn>(original);
    }

    static RbNodeBase** __fastcall IteratorCopy(RbNodeBase** self, void*, RbNodeBase* const* from) {
        RbNodeBase* theirs = nullptr;
        bool returned = Original<decltype(&stlp::TreeIteratorCopy)>()(&theirs, nullptr, from) == &theirs;
        RbNodeBase** result = stlp::TreeIteratorCopy(self, nullptr, from);
        ei::verify::Report(Name<A>("stlp::_Rb_tree_iterator::_Rb_tree_iterator"), returned && result == self && *self == theirs, "");
        return result;
    }
    static void** __fastcall InsertResult(void** self, void*, RbNodeBase* const* node, const bool* inserted) {
        void* theirs[2] = {nullptr, reinterpret_cast<void*>(0x5A5A5A5A)};
        void* before = self[1];
        bool returned = Original<decltype(&stlp::TreeInsertResultConstruct)>()(theirs, nullptr, node, inserted) == theirs;
        void** result = stlp::TreeInsertResultConstruct(self, nullptr, node, inserted);
        bool ok = returned && result == self && self[0] == theirs[0] &&
                  (Word(self[1]) ^ Word(before)) >> 8 == 0 && (Word(self[1]) & 0xFF) == (Word(theirs[1]) & 0xFF) &&
                  Word(theirs[1]) >> 8 == 0x5A5A5A;
        ei::verify::Report(Name<A>("stlp::pair<iterator, bool>::pair"), ok, "");
        return result;
    }
    static void __fastcall Distance(std::size_t* n, void*, RbNodeBase* first, RbNodeBase* last) {
        std::size_t theirs = *n;
        Original<decltype(&stlp::TreeDistance)>()(&theirs, nullptr, first, last);
        stlp::TreeDistance(n, nullptr, first, last);
        ei::verify::Check(Name<A>("stlp::distance"), theirs, *n, "");
    }
    static void __fastcall DistanceTagged(std::size_t* n, void*, RbNodeBase* first, RbNodeBase* last, std::uint32_t tag) {
        std::size_t theirs = *n;
        Original<decltype(&stlp::TreeDistanceTagged)>()(&theirs, nullptr, first, last, tag);
        stlp::TreeDistanceTagged(n, nullptr, first, last, tag);
        ei::verify::Check(Name<A>("stlp::__distance"), theirs, *n, "");
    }
    static std::uint8_t* __fastcall GetAllocator(const std::uint8_t* self, void*, std::uint8_t* out) {
        std::uint8_t theirs = 0x5A;
        bool returned = Original<decltype(&stlp::TreeGetAllocator)>()(self, nullptr, &theirs) == &theirs;
        std::uint8_t* result = stlp::TreeGetAllocator(self, nullptr, out);
        ei::verify::Report(Name<A>("stlp::_Rb_tree::get_allocator"), returned && result == out && *out == theirs, "");
        return result;
    }
    static void** __fastcall AllocProxy(void** self, void*, const std::uint8_t* alloc, void* const* value) {
        void* theirs = nullptr;
        bool returned = Original<decltype(&stlp::TreeAllocProxyConstruct)>()(&theirs, nullptr, alloc, value) == &theirs;
        void** result = stlp::TreeAllocProxyConstruct(self, nullptr, alloc, value);
        ei::verify::Report(Name<A>("stlp::_STLP_alloc_proxy::_STLP_alloc_proxy"), returned && result == self && *self == theirs, "");
        return result;
    }
    static std::uint32_t* __fastcall WordPair(std::uint32_t* self, void*, const std::uint32_t* first, const std::uint32_t* second) {
        std::uint32_t theirs[2] = {};
        bool returned = Original<decltype(&stlp::TreeWordPairConstruct)>()(theirs, nullptr, first, second) == theirs;
        std::uint32_t* result = stlp::TreeWordPairConstruct(self, nullptr, first, second);
        bool ok = returned && result == self && self[0] == theirs[0] && self[1] == theirs[1];
        ei::verify::Report(Name<A>("stlp::pair::pair(words)"), ok, "");
        return result;
    }
};

// --- Start-up script ----------------------------------------------------------------------------

// The originals one instantiation's script runs on its first tree (null: ours on both).
template <typename Abi>
struct Originals {
    decltype(&Abi::InsertUnique) insertUnique = nullptr;
    decltype(&Abi::InsertHint) insertHint = nullptr;
    decltype(&Abi::InsertEqual) insertEqual = nullptr;
    decltype(&Abi::Subscript) subscript = nullptr;
    decltype(&Abi::EraseKey) eraseKey = nullptr;
    decltype(&Abi::EraseAt) eraseAt = nullptr;
    decltype(&Abi::EraseRange) eraseRange = nullptr;
    decltype(&Abi::CopyConstruct) copy = nullptr;
    decltype(&Abi::Clear) clear = nullptr;
    decltype(&Abi::Destroy) destroy = nullptr;
};

template <typename Fn>
Fn Pick(Fn original, Fn ours, bool useOriginal) {
    return useOriginal && original ? original : ours;
}

struct Random {
    std::uint32_t state;
    std::uint32_t Next() {
        state = state * 1103515245u + 12345u;
        return state >> 8;
    }
};

template <typename Abi>
typename Abi::Node* NodeAt(typename Abi::Tree& t, std::uint32_t index) {
    auto* n = t.begin();
    for (std::uint32_t i = 0; i < index && n != t.end(); ++i) n = Abi::Tree::Next(n);
    return n;
}

template <std::uintptr_t A, typename Abi, typename Fn>
Fn OriginalOf(Fn) {
    return reinterpret_cast<Fn>(TreeCheck<A, Abi>::original);
}

// Traits of one instantiation's script:
//   static void Make(Value* v, std::uint32_t r): a value in raw storage, its key from a small set
//     (so inserts meet existing keys and erases find them);
//   static void Destroy(Value* v);
//   static bool Same(const Value& a, const Value& b): two trees' values alike;
//   static void Assign(V* slot, const Value& v): what the game stores through operator[]'s slot;
//   static constexpr bool subscript: operator[] in the script (false where the value has no default
//     the map could build).
// `multi` runs insert_equal in place of the unique inserts (a multimap).
template <typename Abi, typename Traits>
void Script(const char* name, const Originals<Abi>& o, std::uint32_t seed, bool multi = false) {
    using Tree = typename Abi::Tree;
    using Node = typename Abi::Node;
    using Value = typename Abi::Value;
    alignas(4) unsigned char storage[2][sizeof(Tree)] = {};
    Tree* trees[2] = {reinterpret_cast<Tree*>(storage[0]), reinterpret_cast<Tree*>(storage[1])};
    const std::uint8_t compare = 0;
    for (Tree* t : trees) Abi::ConstructWith(t, nullptr, &compare, &compare);
    Random random{seed};
    int failedStep = -1;
    const int kSteps = 400;
    for (int step = 0; step < kSteps && failedStep < 0; ++step) {
        std::uint32_t op = random.Next() % 9;
        alignas(Value) unsigned char valueStorage[sizeof(Value)];
        Value* v = reinterpret_cast<Value*>(valueStorage);
        Traits::Make(v, random.Next());
        std::uint32_t r1 = random.Next(), r2 = random.Next();
        bool same = true;
        for (int i = 0; i < 2; ++i) {
            Tree* t = trees[i];
            bool orig = i == 0;
            std::uint32_t count = static_cast<std::uint32_t>(t->size());
            switch (op) {
            case 0:
            case 1:
                if (multi) {
                    Node* out = nullptr;
                    Pick(o.insertEqual, &Abi::InsertEqual, orig)(t, nullptr, &out, v);
                } else {
                    typename Abi::InsertResult out{};
                    Pick(o.insertUnique, &Abi::InsertUnique, orig)(t, nullptr, &out, v);
                }
                break;
            case 2: {
                Node* out = nullptr;
                if (multi) {
                    Pick(o.insertEqual, &Abi::InsertEqual, orig)(t, nullptr, &out, v);
                } else {
                    Node* hint = NodeAt<Abi>(*t, count ? r1 % (count + 1) : 0);
                    Pick(o.insertHint, &Abi::InsertHint, orig)(t, nullptr, &out, hint, v);
                }
                break;
            }
            case 3:
                if constexpr (Traits::subscript) {
                    if (!multi) Traits::Assign(Pick(o.subscript, &Abi::Subscript, orig)(t, nullptr, &v->first), *v);
                }
                break;
            case 4: Pick(o.eraseKey, &Abi::EraseKey, orig)(t, nullptr, &v->first); break;
            case 5:
                if (count) Pick(o.eraseAt, &Abi::EraseAt, orig)(t, nullptr, NodeAt<Abi>(*t, r1 % count));
                break;
            case 6: {
                std::uint32_t a = count ? r1 % (count + 1) : 0;
                std::uint32_t b = a + (count + 1 - a ? r2 % (count + 1 - a) : 0);
                if (r2 % 16 == 0) a = 0, b = count;   // now and then the whole tree (clear)
                Pick(o.eraseRange, &Abi::EraseRange, orig)(t, nullptr, NodeAt<Abi>(*t, a), NodeAt<Abi>(*t, b));
                break;
            }
            case 7: {
                // A copy, compared, destroyed; the tree stays.
                alignas(4) unsigned char copy[sizeof(Tree)];
                Tree* c = reinterpret_cast<Tree*>(copy);
                Pick(o.copy, &Abi::CopyConstruct, orig)(c, nullptr, t);
                same = same && SameTree<Abi, Traits>(*c, *t) && c->end() != t->end();
                Pick(o.destroy, &Abi::Destroy, orig)(c);
                break;
            }
            case 8:
                if (r1 % 8 == 0) Pick(o.clear, &Abi::Clear, orig)(t);
                break;
            }
        }
        Traits::Destroy(v);
        if (!same || !SameTree<Abi, Traits>(*trees[0], *trees[1])) failedStep = step;
    }
    char detail[64] = "";
    if (failedStep >= 0) std::snprintf(detail, sizeof(detail), "seed %u: step %d", static_cast<unsigned>(seed), failedStep);
    ei::verify::Report(name, failedStep < 0, detail);
    Pick(o.destroy, &Abi::Destroy, true)(trees[0]);
    Abi::Destroy(trees[1]);
}

} // namespace stlp_tree_verify
