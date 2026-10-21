#pragma once

// A layout- and allocation-compatible subset of the STLport containers compiled into game.exe.
//
// Original code and rewritten code share these objects (for example the loaded-file maps inside
// CMappedResource), so they must have STLport's exact layout, allocate from the game's own pooled
// node allocator, and hash and order elements the way STLport does. Only what rewritten code needs
// is implemented; each member names the original routine it mirrors.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "stlp_alloc.h"
#include "stlp_deque.h"
#include "stlp_list.h"
#include "stlp_tree.h"
#include "stlp_vector.h"
#include "game/game_data.h"

namespace stlp {

// allocate / deallocate: stlp_alloc.h.
// 0079B228: the node allocator's free list of 12-byte nodes (list nodes), which some code pushes
// to inline.
inline void*& SmallNodeFreeList() { return ei::GameData<void*>(0x0079B228); }

// basic_string<char>: three pointers, no small-string buffer. 12 bytes.
class string {
public:
    string() = default;
    string(const char* first, const char* last) { InitRange(first, last); }   // 0042A3D0
    explicit string(const char* s) : string(s, s + std::strlen(s)) {}
    string(const string& s) : string(s.start_, s.finish_) {}                   // 004A8930
    string& operator=(const string&) = delete;
    ~string() { Release(); }                                                   // 006727B0

    // 00421800: empty string with room for `capacity` bytes (including the terminator).
    static void Reserve(string& s, std::size_t capacity);
    // 0042A4F0: replaces the contents with [first, last), reusing the buffer when possible.
    void Assign(const char* first, const char* last);
    // 0042B700: appends [first, last), growing to size + max(size, n) + 1 when full.
    void Append(const char* first, const char* last);

    // operator=: STLport assigns through assign(first, last).
    void AssignFrom(const string& s) { Assign(s.start_, s.finish_); }
    // operator=(const char*) (0041F550): assign(text, text + strlen(text)).
    void Assign(const char* text) { Assign(text, text + std::strlen(text)); }
    // substr(pos, n) (0041F5F0): out_of_range past the end; the rest of the string at most n long.
    string substr(std::size_t pos, std::size_t n) const;

    const char* data() const { return start_; }
    char* data() { return start_; }
    char& operator[](std::size_t i) { return start_[i]; }
    char operator[](std::size_t i) const { return start_[i]; }
    std::size_t size() const { return static_cast<std::size_t>(finish_ - start_); }
    std::size_t capacity() const { return static_cast<std::size_t>(eos_ - start_); }
    bool empty() const { return start_ == finish_; }
    bool operator==(const string& o) const {
        return size() == o.size() && std::memcmp(start_, o.start_, size()) == 0;
    }
    // operator< (00445780): the common length's bytes, unsigned, then the shorter one first.
    bool operator<(const string& o) const {
        std::size_t n = size() < o.size() ? size() : o.size();
        int c = std::memcmp(start_, o.start_, n);
        return c != 0 ? c < 0 : size() < o.size();
    }

    // __stl_hash_string over the characters, which are signed in this build.
    std::uint32_t Hash() const {
        std::uint32_t h = 0;
        for (const char* p = start_; p != finish_; ++p) h = h * 5 + static_cast<std::uint32_t>(static_cast<int>(*p));
        return h;
    }

private:
    friend struct StringAbi;   // the exe's members at their addresses (stlp_string.h)

    void InitRange(const char* first, const char* last);
    void Release() {
        if (start_) deallocate(start_, static_cast<std::size_t>(eos_ - start_));
    }

    char* start_ = nullptr;
    char* finish_ = nullptr;
    char* eos_ = nullptr;   // end of storage; STLport keeps the (empty) allocator in the same word
};
static_assert(sizeof(string) == 12);

// operator+(const string&, const char*) (00427D60): joined in a temporary with exactly the room,
// which the result is a copy of (two blocks; the temporary's is freed once the result has its own).
// operator+(const string&, const string&) (00427E80) the same.
string Plus(const string& a, const char* text);
string Plus(const string& a, const string& b);

// The vector element policy (stlp_vector.h) of strings: copied with the copy constructor
// (00428DF0), assigned through assign(first, last), destroyed with the buffer freed. A
// default-constructed one (resize(n), e.g. 004DC900) is empty with room for 8 bytes.
struct StringElement {
    static void Construct(string* to, const string& from) { new (to) string(from); }
    static void Assign(string& to, const string& from) { to.AssignFrom(from); }
    static void Destroy(string* p) { p->~string(); }
    static void Default(string* p) {
        new (p) string();
        string::Reserve(*p, 8);
    }
};

// vector<T>: stlp_vector.h, list<T>: stlp_list.h, deque<T>: stlp_deque.h, map<K, V>: stlp_tree.h
// (included at the top).

// Hash for pointer keys: the pointer value itself.
struct PointerHash {
    template <typename T> std::uint32_t operator()(T* p) const { return reinterpret_cast<std::uint32_t>(p); }
};
// Hash for integer keys: the value itself.
struct IntHash {
    template <typename T> std::uint32_t operator()(T v) const { return static_cast<std::uint32_t>(v); }
};
struct StringHash {
    std::uint32_t operator()(const string& s) const { return s.Hash(); }
};

// Next STLport bucket count >= n (__stl_prime_list, 0073B914).
std::size_t NextPrime(std::size_t n);
// 00408A50: lower_bound over [first, last) of dwords (stlp.cpp).
const std::uint32_t* __fastcall LowerBoundDwords(const std::uint32_t* first, const std::uint32_t* last,
                                                 const std::uint32_t* value, int tag);

// hashtable<pair<const Key, Value>, ...> as used by hash_map. 0x14 bytes:
//   +0x00 hasher / key_equal / select1st (empty, one byte each)
//   +0x04 vector<Node*> buckets (start, finish, end of storage)
//   +0x10 element count (STLport keeps it in the node allocator's proxy)
// The members named after STLport's (Construct, InitializeBuckets, Resize, Find, FindOrInsert,
// First, After, SkipToNext, Erase, EraseKey, Clear, Destroy, NewNode, DeleteNode) do what the
// game's instantiations do (stlp_hash.h's hashtable, registered at their addresses), so code can
// call them where the original calls an instantiation.
template <typename Key, typename Value, typename Hash>
class hash_map {
public:
    struct Node {
        Node* next;
        Key key;
        Value value;
    };
    // value_type and iterator as STLport lays them out.
    struct Pair {
        Key key;
        Value value;
    };
    struct Iterator {
        Node* node;
        const hash_map* table;
    };
    using Buckets = vector<Node*>;
    using BucketElement = PlainElement<Node*>;

    hash_map() = default;

    // hashtable(n, hf, eql, a) (e.g. 00405820): the functor bytes, an empty bucket vector, no
    // elements, then _M_initialize_buckets(n). hash_map() (e.g. 004639D0) is Construct(100) with
    // the functor bytes an uninitialised stack byte (0 here).
    void Construct(std::size_t n, std::uint8_t hasher = 0, std::uint8_t equal = 0, std::uint8_t extract = 0) {
        functors_[0] = hasher;
        functors_[1] = equal;
        functors_[2] = extract;
        buckets_.ConstructEmpty();
        count_ = 0;
        InitializeBuckets(n);
    }

    // _M_initialize_buckets(n) (e.g. 0040AA80): reserve(NextPrime(n)), that many nulls appended at
    // the end, no elements.
    void InitializeBuckets(std::size_t n) {
        std::size_t buckets = NextPrime(n);
        buckets_.template Reserve<BucketElement>(buckets);
        Node* none = nullptr;
        buckets_.template FillInsert<BucketElement>(buckets_.end(), buckets, none);
        count_ = 0;
    }

    // 006729E0 (_M_initialize_buckets on a table the owner zeroed): an empty table with
    // NextPrime(n) buckets in a fresh block.
    void InitBuckets(std::size_t n);

    std::size_t BucketCount() const { return buckets_.size(); }
    std::size_t Size() const { return count_; }
    std::size_t BucketOf(const Key& key) const { return Hash()(key) % BucketCount(); }

    // find(key) (e.g. 00405560): the node holding `key`, or null.
    Node* Find(const Key& key) const {
        for (Node* n = buckets_[BucketOf(key)]; n; n = n->next)
            if (n->key == key) return n;
        return nullptr;
    }

    // find_or_insert(obj) (e.g. 004065D0): resize(count + 1), then the node with obj's key in its
    // bucket, else a copy of obj in a new node at the head of the bucket.
    Node* FindOrInsert(const Pair& obj) {
        Resize(count_ + 1);
        std::size_t b = BucketOf(obj.key);
        Node* first = buckets_[b];
        for (Node* cur = first; cur; cur = cur->next)
            if (cur->key == obj.key) return cur;
        Node* node = NewNode(obj);
        node->next = first;
        buckets_[b] = node;
        ++count_;
        return node;
    }

    // operator[] (e.g. 004055A0): find_or_insert of {key, Value()}.
    Value& operator[](const Key& key) {
        Resize(count_ + 1);
        std::size_t b = BucketOf(key);
        for (Node* n = buckets_[b]; n; n = n->next)
            if (n->key == key) return n->value;
        Node* n = static_cast<Node*>(allocate(sizeof(Node)));
        n->next = nullptr;
        new (&n->key) Key(key);
        n->value = Value();
        n->next = buckets_[b];
        buckets_[b] = n;
        ++count_;
        return n->value;
    }

    // operator[] as STLport writes it for a key with a copy constructor (e.g. 004297A0): a
    // temporary {key, Value()} for find_or_insert (copied into a new node when the key is
    // missing), destroyed after.
    Value& Subscript(const Key& key) {
        Pair obj{key, Value()};
        return FindOrInsert(obj)->value;
    }

    // Visits nodes in iterator order: buckets in order, each chain from its head.
    template <typename F> void ForEach(F f) const {
        for (std::size_t i = 0; i < BucketCount(); ++i)
            for (Node* n = buckets_[i]; n; n = n->next) f(n);
    }

    // begin() (e.g. 0059C020): the head of the first non-empty bucket, or null.
    Node* First() const {
        for (std::size_t b = 0; b < BucketCount(); ++b)
            if (buckets_[b]) return buckets_[b];
        return nullptr;
    }

    // _Ht_iterator::_M_skip_to_next() (e.g. 004073C0): the head of the first non-empty bucket after
    // the one `node`'s key hashes to now, or null.
    Node* SkipToNext(const Node* node) const {
        std::size_t b = BucketOf(node->key);
        Node* next = nullptr;
        while (!next && ++b < BucketCount()) next = buckets_[b];
        return next;
    }
    // iterator::operator++ (e.g. 0059B8A0): the next node of the chain, else SkipToNext (so a
    // rehash while iterating moves the walk the way the original's does).
    Node* After(const Node* node) const { return node->next ? node->next : SkipToNext(node); }

    // erase(iterator) (e.g. 00405620): unlink and delete one node (nothing for null or a node that
    // is not in its bucket's chain).
    void Erase(Node* node);
    // erase(key) (e.g. 006BCCD0): the nodes after the bucket's head that hold `key` deleted, then
    // the head when it does; the number deleted.
    std::size_t EraseKey(const Key& key);

    // clear() (e.g. 004067B0): free every node, keep the buckets.
    void Clear();

    // ~hashtable (e.g. 004A8B70): clear(), then the bucket storage freed.
    void Destroy() {
        Clear();
        buckets_.FreeStorage();
    }

    // resize(hint) (e.g. 00406640): grow to the next prime and rehash, moving nodes to the head of
    // new buckets.
    void Resize(std::size_t hint);

    // _M_new_node(obj) (e.g. 00407420): a node from the pool, no next, a copy of obj.
    static Node* NewNode(const Pair& obj) {
        Node* n = static_cast<Node*>(allocate(sizeof(Node)));
        n->next = nullptr;
        new (&n->key) Key(obj.key);
        new (&n->value) Value(obj.value);
        return n;
    }
    // _M_delete_node(n) (e.g. 004074B0): the key destroyed, the node back to the pool.
    static void DeleteNode(Node* n) {
        n->key.~Key();
        deallocate(n, sizeof(Node));
    }

    // clear() as STLport writes it, each node freed by `deleteNode(node)` (the instantiation's own
    // node deleter, for owners that call the game's copy): every chain from its head, the bucket
    // emptied, then the count zeroed.
    template <typename F> void ClearWith(F deleteNode) {
        for (std::size_t i = 0; i < BucketCount(); ++i) {
            for (Node* n = buckets_[i]; n;) {
                Node* next = n->next;
                deleteNode(n);
                n = next;
            }
            buckets_[i] = nullptr;
        }
        count_ = 0;
    }

    // The parts an owner constructs and destroys by hand when the game inlines it (CBaseManager):
    // the functor bytes, the bucket vector, and the element count.
    std::uint8_t* Functors() { return functors_; }
    Buckets& BucketStorage() { return buckets_; }
    void* BucketVector() { return &buckets_; }
    void* CountProxy() { return &count_; }
    std::size_t& Count() { return count_; }
    // The insert half of operator[] for owners that call the game's own resize and node
    // allocation (the session's type registry): the head of bucket `b`, and a new node linked in
    // front of it.
    Node* Bucket(std::size_t b) const { return buckets_[b]; }
    void LinkAtHead(std::size_t b, Node* node) {
        node->next = buckets_[b];
        buckets_[b] = node;
        ++count_;
    }

private:
    std::uint8_t functors_[4];
    Buckets buckets_;
    std::size_t count_;
};

template <typename K, typename V, typename H>
void hash_map<K, V, H>::InitBuckets(std::size_t n) {
    std::size_t buckets = NextPrime(n);
    buckets_.ConstructWithCapacity(buckets);
    std::memset(buckets_.Start(), 0, buckets * sizeof(Node*));
    buckets_.Finish() = buckets_.EndOfStorage();
    count_ = 0;
}

template <typename K, typename V, typename H>
void hash_map<K, V, H>::Erase(Node* node) {
    if (!node) return;
    Node** slot = &buckets_[BucketOf(node->key)];
    if (*slot == node) {
        *slot = node->next;
    } else {
        Node* prev = *slot;
        while (prev->next && prev->next != node) prev = prev->next;
        if (!prev->next) return;
        prev->next = node->next;
    }
    DeleteNode(node);
    --count_;
}

template <typename K, typename V, typename H>
std::size_t hash_map<K, V, H>::EraseKey(const K& key) {
    std::size_t b = BucketOf(key);
    Node* first = buckets_[b];
    std::size_t erased = 0;
    if (!first) return 0;
    Node* cur = first;
    Node* next = cur->next;
    while (next) {
        if (next->key == key) {
            cur->next = next->next;
            DeleteNode(next);
            next = cur->next;
            ++erased;
            --count_;
        } else {
            cur = next;
            next = cur->next;
        }
    }
    if (first->key == key) {
        buckets_[b] = first->next;
        DeleteNode(first);
        ++erased;
        --count_;
    }
    return erased;
}

template <typename K, typename V, typename H>
void hash_map<K, V, H>::Clear() {
    ClearWith([](Node* n) { DeleteNode(n); });
}

template <typename K, typename V, typename H>
void hash_map<K, V, H>::Resize(std::size_t hint) {
    std::size_t oldN = BucketCount();
    if (hint <= oldN) return;
    std::size_t n = NextPrime(hint);
    if (n <= oldN) return;
    Buckets fresh;
    fresh.ConstructWithCapacity(n);
    for (std::size_t i = 0; i < n; ++i) fresh.Start()[i] = nullptr;
    fresh.Finish() = fresh.Start() + n;
    for (std::size_t b = 0; b < oldN; ++b) {
        while (Node* first = buckets_[b]) {
            std::size_t nb = H()(first->key) % n;
            buckets_[b] = first->next;
            first->next = fresh[nb];
            fresh[nb] = first;
        }
    }
    Node** start = buckets_.Start();
    Node** finish = buckets_.Finish();
    Node** eos = buckets_.EndOfStorage();
    buckets_.Start() = fresh.Start();
    buckets_.Finish() = fresh.Finish();
    buckets_.EndOfStorage() = fresh.EndOfStorage();
    fresh.Start() = start;
    fresh.Finish() = finish;
    fresh.EndOfStorage() = eos;
    fresh.FreeStorage();
}

} // namespace stlp
