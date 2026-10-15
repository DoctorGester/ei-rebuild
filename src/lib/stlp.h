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

namespace stlp {

// allocate / deallocate: stlp_alloc.h.
// 0079B228: the node allocator's free list of 12-byte nodes (list nodes), which some code pushes
// to inline.
inline void*& SmallNodeFreeList() { return *reinterpret_cast<void**>(0x0079B228); }

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

    const char* data() const { return start_; }
    std::size_t size() const { return static_cast<std::size_t>(finish_ - start_); }
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
string Plus(const string& a, const char* text);

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

// hashtable<pair<const Key, Value>, ...> as used by hash_map. 0x14 bytes:
//   +0x00 hasher / key_equal / select1st (empty, one byte each)
//   +0x04 vector<Node*> buckets (start, finish, end of storage)
//   +0x10 element count
template <typename Key, typename Value, typename Hash>
class hash_map {
public:
    struct Node {
        Node* next;
        Key key;
        Value value;
    };
    // value_type and iterator as STLport lays them out, for owners that call the game's own
    // instantiations (insert(pair), find, begin, ++) through their original addresses.
    struct Pair {
        Key key;
        Value value;
    };
    struct Iterator {
        Node* node;
        const hash_map* table;
    };

    // Construction happens in the owner's constructor via InitBuckets (STLport has no default bucket count).
    hash_map() = default;

    // 006729E0 (_M_initialize_buckets): an empty table with NextPrime(n) buckets.
    void InitBuckets(std::size_t n);

    std::size_t BucketCount() const { return static_cast<std::size_t>(finish_ - buckets_); }
    std::size_t Size() const { return count_; }

    Node* Find(const Key& key) const {
        for (Node* n = buckets_[Hash()(key) % BucketCount()]; n; n = n->next)
            if (n->key == key) return n;
        return nullptr;
    }

    // operator[]: resize for one more element, then find or insert at the head of the bucket.
    Value& operator[](const Key& key) {
        Resize(count_ + 1);
        std::size_t b = Hash()(key) % BucketCount();
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

    // iterator::operator++ (e.g. 0059B8A0): the next node of the chain, else the head of the next
    // non-empty bucket after the one `node`'s key hashes to now (so a rehash while iterating moves
    // the walk the way the original's does), or null.
    Node* After(const Node* node) const {
        if (node->next) return node->next;
        for (std::size_t b = Hash()(node->key) % BucketCount() + 1; b < BucketCount(); ++b)
            if (buckets_[b]) return buckets_[b];
        return nullptr;
    }

    // erase(iterator): unlink and free one node.
    void Erase(Node* node);

    // clear(): free every node, keep the buckets.
    void Clear();

    // ~hashtable: free every node and the bucket array.
    void Destroy() {
        Clear();
        if (buckets_) deallocate(buckets_, static_cast<std::size_t>(eos_ - buckets_) * sizeof(Node*));
    }

    // resize(hint): grow to the next prime and rehash, moving nodes to the head of new buckets.
    void Resize(std::size_t hint);

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
    // the functor bytes, the bucket vector, and the element count, which STLport keeps in the
    // node allocator's proxy (the node allocator's `this`).
    std::uint8_t* Functors() { return functors_; }
    void* BucketVector() { return &buckets_; }
    void* CountProxy() { return &count_; }
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
    Node** buckets_;
    Node** finish_;
    Node** eos_;
    std::size_t count_;
};

template <typename K, typename V, typename H>
void hash_map<K, V, H>::InitBuckets(std::size_t n) {
    std::size_t buckets = NextPrime(n);
    buckets_ = static_cast<Node**>(allocate(buckets * sizeof(Node*)));
    std::memset(buckets_, 0, buckets * sizeof(Node*));
    finish_ = eos_ = buckets_ + buckets;
    count_ = 0;
}

template <typename K, typename V, typename H>
void hash_map<K, V, H>::Erase(Node* node) {
    if (!node) return;
    Node** slot = &buckets_[H()(node->key) % BucketCount()];
    if (*slot == node) {
        *slot = node->next;
    } else {
        Node* prev = *slot;
        while (prev->next && prev->next != node) prev = prev->next;
        if (!prev->next) return;
        prev->next = node->next;
    }
    node->key.~K();
    deallocate(node, sizeof(Node));
    --count_;
}

template <typename K, typename V, typename H>
void hash_map<K, V, H>::Clear() {
    for (std::size_t i = 0; i < BucketCount(); ++i) {
        Node* n = buckets_[i];
        while (n) {
            Node* next = n->next;
            n->key.~K();
            deallocate(n, sizeof(Node));
            n = next;
        }
        buckets_[i] = nullptr;
    }
    count_ = 0;
}

template <typename K, typename V, typename H>
void hash_map<K, V, H>::Resize(std::size_t hint) {
    std::size_t oldN = BucketCount();
    if (hint <= oldN) return;
    std::size_t n = NextPrime(hint);
    if (n <= oldN) return;
    Node** fresh = static_cast<Node**>(allocate(n * sizeof(Node*)));
    std::memset(fresh, 0, n * sizeof(Node*));
    for (std::size_t b = 0; b < oldN; ++b) {
        while (Node* first = buckets_[b]) {
            std::size_t nb = H()(first->key) % n;
            buckets_[b] = first->next;
            first->next = fresh[nb];
            fresh[nb] = first;
        }
    }
    Node** old = buckets_;
    std::size_t oldBytes = static_cast<std::size_t>(eos_ - old) * sizeof(Node*);
    buckets_ = fresh;
    finish_ = eos_ = fresh + n;
    if (old) deallocate(old, oldBytes);
}

} // namespace stlp
