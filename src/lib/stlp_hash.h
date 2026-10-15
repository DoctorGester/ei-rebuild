#pragma once

// STLport's hashtable (hash_map / hash_set) as compiled into game.exe (docs/notes/standalone.md,
// section 2). stlp.h's older hash_map view keeps serving the game code that uses it; this is the
// library body the instantiations' own addresses are registered to (stlp_hash.cpp).
//
// Layout (0x14 bytes): the hasher, key_equal and key extractor (empty, one byte each, then a pad
// byte), the bucket vector (vector<Node*>, stlp_vector.h) and the element count, which STLport
// keeps in the node allocator's proxy. A node is the next pointer followed by the value; its size
// picks the pool's free list, so each instantiation's node type comes from its node-creation code.
//
// Order: bucket counts are STLport's primes (NextPrime, stlp.cpp: the first of the 28 primes not
// below the hint, else the last), a key lands in bucket hash(key) % buckets, a new node goes to the
// head of its bucket, and iteration walks the buckets in order and each chain from its head. A
// rehash (resize) moves each old bucket's chain, head first, to the head of its new bucket.

#include "crt.h"
#include "mfc.h"
#include "stlp.h"
#include "stlp_alloc.h"
#include "stlp_vector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace stlp {

std::size_t NextPrime(std::size_t n);   // stlp.cpp

// A value whose first dword is the key, hashed as itself (hash<int>, hash<unsigned>, hash of a
// pointer). Bytes is the whole value's size: 4 for a hash_set of dwords, 8 for a map of dword to
// dword.
template <std::size_t Bytes>
struct DwordKeyed {
    std::uint32_t key;
    unsigned char rest[Bytes - 4];
};
template <>
struct DwordKeyed<4> {
    std::uint32_t key;
};

// The traits of a hashtable over plain data keyed by its first dword.
template <std::size_t Bytes>
struct DwordKeyTraits {
    using Value = DwordKeyed<Bytes>;
    using Key = std::uint32_t;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return k; }
    static bool Equal(const Key& a, const Key& b) { return a == b; }
    static void Construct(Value* to, const Value& from) { std::memcpy(to, &from, sizeof(Value)); }
    static void Destroy(Value*) {}
};

// __stl_hash_string: h = 5 * h + c over the characters, which are signed in this build.
inline std::uint32_t HashChars(const char* first, const char* last) {
    std::uint32_t h = 0;
    for (; first != last; ++first) h = h * 5 + static_cast<std::uint32_t>(static_cast<int>(*first));
    return h;
}
inline std::uint32_t HashChars(const char* s) {
    std::uint32_t h = 0;
    for (; *s; ++s) h = h * 5 + static_cast<std::uint32_t>(static_cast<int>(*s));
    return h;
}

// pair<const string, T> with T plain data of MappedBytes (a pointer, a number). A temporary built
// from a key holds a copy of it and zeroes, as value_type(key, T()) does.
template <std::size_t MappedBytes>
struct StringKeyed {
    string key;
    unsigned char mapped[MappedBytes];
    StringKeyed() = default;
    explicit StringKeyed(const string& k) : key(k), mapped{} {}
};

// The traits of a hashtable keyed by a string (hash<string>: HashChars over its bytes; equal when
// the sizes and bytes are). The node copies the string with string(first, last) and the mapped
// bytes; deleting it frees the string's block.
template <std::size_t MappedBytes>
struct StringKeyTraits {
    using Value = StringKeyed<MappedBytes>;
    using Key = string;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return k.Hash(); }
    static bool Equal(const Key& a, const Key& b) { return a == b; }
    static void Construct(Value* to, const Value& from) {
        new (&to->key) string(from.key);
        std::memcpy(to->mapped, from.mapped, MappedBytes);
    }
    static void Destroy(Value* p) { p->key.~string(); }
};

// A value of a dword key (hashed as itself) and a T whose copy and destruction are an element
// policy's (Construct(T*, const T&), Destroy(T*): stlp_vector.h's policies, record_vectors.h's).
template <typename T>
struct DwordKeyedObject {
    std::uint32_t key;
    T mapped;
};
template <typename T, typename E>
struct DwordObjectTraits {
    using Value = DwordKeyedObject<T>;
    using Key = std::uint32_t;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return k; }
    static bool Equal(const Key& a, const Key& b) { return a == b; }
    static void Construct(Value* to, const Value& from) {
        to->key = from.key;
        E::Construct(&to->mapped, from.mapped);
    }
    static void Destroy(Value* p) { E::Destroy(&p->mapped); }
};

// pair<const CString, float> (the party's named variables): hashed by the text up to its zero
// (HashChars), equal when _mbscmp says so (the node's key first). The node copies the CString
// (a shared reference) and the float's bits; deleting it releases the CString. A temporary built
// from a key holds a copy of it and zero.
struct CStringFloat {
    mfc::CString key;
    float mapped;
    explicit CStringFloat(const mfc::CString& k) : key(k), mapped(0.0f) {}
};
struct CStringFloatTraits {
    using Value = CStringFloat;
    using Key = mfc::CString;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return HashChars(static_cast<const char*>(k)); }
    static bool Equal(const Key& a, const Key& b) {
        return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(a)),
                               reinterpret_cast<const unsigned char*>(static_cast<const char*>(b))) == 0;
    }
    static void Construct(Value* to, const Value& from) {
        new (&to->key) mfc::CString(from.key);
        std::memcpy(&to->mapped, &from.mapped, sizeof(float));
    }
    static void Destroy(Value* p) { p->key.~CString(); }
};

// pair<const string, list<void*>> (the item stock's recipes by spell name): string keys as
// StringKeyTraits. The node copies the string, then the list (its sentinel, then each element in
// order); deleting it frees the list's nodes, its sentinel, then the string.
struct StringList {
    string key;
    list<void*> mapped;
};
struct StringListTraits {
    using Value = StringList;
    using Key = string;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return k.Hash(); }
    static bool Equal(const Key& a, const Key& b) { return a == b; }
    static void Construct(Value* to, const Value& from) {
        new (&to->key) string(from.key);
        to->mapped.Construct();
        list<void*>::InsertRange(to->mapped.head, from.mapped.First(), from.mapped.End());
    }
    static void Destroy(Value* p) {
        p->mapped.Release();
        p->key.~string();
    }
};

// hashtable<Value, Key, HashFcn, ExtractKey, EqualKey, Alloc> over Traits (Value, Key, KeyOf, Hash,
// Equal, Construct, Destroy).
template <typename Traits>
class hashtable {
public:
    using Value = typename Traits::Value;
    using Key = typename Traits::Key;
    struct Node {
        Node* next;
        Value value;
    };
    // _Ht_iterator: the node (null at the end) and the table.
    struct iterator {
        Node* cur;
        hashtable* table;
    };
    using Buckets = vector<Node*>;
    using BucketElement = PlainElement<Node*>;

    hashtable() = default;
    hashtable(const hashtable&) = delete;

    std::size_t BucketCount() const { return buckets_.size(); }
    std::size_t Count() const { return count_; }
    Buckets& BucketVector() { return buckets_; }
    std::size_t& CountRef() { return count_; }
    std::uint8_t* Functors() { return functors_; }
    std::size_t BucketOf(const Key& key, std::size_t n) const { return Traits::Hash(key) % n; }
    std::size_t BucketOf(const Key& key) const { return BucketOf(key, BucketCount()); }

    // hashtable(n, hf, eql, a) (e.g. 00405820): the functor bytes, an empty bucket vector, no
    // elements, then _M_initialize_buckets(n).
    void Construct(std::size_t n, std::uint8_t hasher, std::uint8_t equal, std::uint8_t extract) {
        functors_[0] = hasher;
        functors_[1] = equal;
        functors_[2] = extract;
        buckets_.ConstructEmpty();
        count_ = 0;
        InitializeBuckets(n);
    }

    // _M_initialize_buckets(n) (e.g. 0040AA80): reserve(NextPrime(n)), insert that many nulls at
    // the end, no elements.
    void InitializeBuckets(std::size_t n) {
        std::size_t buckets = NextPrime(n);
        buckets_.template Reserve<BucketElement>(buckets);
        Node* none = nullptr;
        buckets_.template FillInsert<BucketElement>(buckets_.end(), buckets, none);
        count_ = 0;
    }

    // resize(hint) (e.g. 00406640): when the hint exceeds the bucket count and the next prime does
    // too, a zeroed bucket vector of that many, each old chain moved head first to the head of its
    // new bucket, the vectors swapped and the old storage freed.
    void Resize(std::size_t hint) {
        std::size_t oldCount = BucketCount();
        if (hint <= oldCount) return;
        std::size_t n = NextPrime(hint);
        if (n <= oldCount) return;
        Buckets fresh;
        fresh.ConstructWithCapacity(n);
        for (std::size_t i = 0; i < n; ++i) fresh.Start()[i] = nullptr;
        fresh.Finish() = fresh.Start() + n;
        for (std::size_t b = 0; b < oldCount; ++b) {
            Node* first = buckets_[b];
            while (first) {
                std::size_t to = BucketOf(Traits::KeyOf(first->value), n);
                buckets_[b] = first->next;
                first->next = fresh[to];
                fresh[to] = first;
                first = buckets_[b];
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

    // find(key) (e.g. 00405560): the node holding `key`, or null.
    Node* Find(const Key& key) const {
        Node* n = buckets_[BucketOf(key)];
        while (n && !Traits::Equal(Traits::KeyOf(n->value), key)) n = n->next;
        return n;
    }

    // find_or_insert(obj) (e.g. 004065D0): resize(count + 1), then the value with obj's key in its
    // bucket, else a copy of obj in a new node at the head of the bucket.
    Value& FindOrInsert(const Value& obj) {
        Resize(count_ + 1);
        std::size_t b = BucketOf(Traits::KeyOf(obj));
        Node* first = buckets_[b];
        for (Node* cur = first; cur; cur = cur->next)
            if (Traits::Equal(Traits::KeyOf(cur->value), Traits::KeyOf(obj))) return cur->value;
        Node* node = NewNode(obj);
        node->next = first;
        buckets_[b] = node;
        ++count_;
        return node->value;
    }

    // insert_unique(obj) (0066B400): resize(count + 1), then the node with obj's key in its bucket
    // (not inserted), else a copy of obj in a new node at the head of the bucket (inserted).
    Node* InsertUnique(const Value& obj, bool& inserted) {
        Resize(count_ + 1);
        std::size_t b = BucketOf(Traits::KeyOf(obj));
        Node* first = buckets_[b];
        for (Node* cur = first; cur; cur = cur->next) {
            if (Traits::Equal(Traits::KeyOf(cur->value), Traits::KeyOf(obj))) {
                inserted = false;
                return cur;
            }
        }
        Node* node = NewNode(obj);
        node->next = first;
        buckets_[b] = node;
        ++count_;
        inserted = true;
        return node;
    }

    // begin() (e.g. 00404C30): the head of the first non-empty bucket, or null.
    Node* First() const {
        for (std::size_t b = 0; b < BucketCount(); ++b)
            if (buckets_[b]) return buckets_[b];
        return nullptr;
    }

    // _Ht_iterator::_M_skip_to_next() (e.g. 004073C0): the head of the first non-empty bucket after
    // the one `cur`'s key hashes to, or null.
    Node* SkipToNext(const Node* cur) const {
        std::size_t b = BucketOf(Traits::KeyOf(cur->value));
        Node* next = nullptr;
        while (!next && ++b < BucketCount()) next = buckets_[b];
        return next;
    }
    // iterator::operator++ (e.g. 00404980): the next node of the chain, else SkipToNext.
    Node* Next(const Node* cur) const { return cur->next ? cur->next : SkipToNext(cur); }

    // erase(const_iterator) (e.g. 00405620): `p` unlinked from its bucket's chain and deleted when
    // it is there; nothing for null.
    void EraseNode(const Node* p) {
        if (!p) return;
        std::size_t b = BucketOf(Traits::KeyOf(p->value));
        Node* cur = buckets_[b];
        if (cur == p) {
            buckets_[b] = cur->next;
            DeleteNode(cur);
            --count_;
            return;
        }
        for (Node* next = cur->next; next; cur = next, next = cur->next) {
            if (next == p) {
                cur->next = next->next;
                DeleteNode(next);
                --count_;
                return;
            }
        }
    }

    // clear() (e.g. 004067B0): every chain deleted from its head, each bucket emptied, no elements.
    void Clear() {
        for (std::size_t i = 0; i < BucketCount(); ++i) {
            Node* cur = buckets_[i];
            while (cur) {
                Node* next = cur->next;
                DeleteNode(cur);
                cur = next;
            }
            buckets_[i] = nullptr;
        }
        count_ = 0;
    }

    // erase(key) (e.g. 006BCCD0): the nodes after the bucket's head that hold `key` deleted, then
    // the head when it does; the number deleted.
    std::size_t EraseKey(const Key& key) {
        std::size_t b = BucketOf(key);
        Node* first = buckets_[b];
        std::size_t erased = 0;
        if (!first) return 0;
        Node* cur = first;
        Node* next = cur->next;
        while (next) {
            if (Traits::Equal(Traits::KeyOf(next->value), key)) {
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
        if (Traits::Equal(Traits::KeyOf(first->value), key)) {
            buckets_[b] = first->next;
            DeleteNode(first);
            ++erased;
            --count_;
        }
        return erased;
    }

    // ~hashtable() (e.g. 004A8B70): clear(), then the bucket storage freed.
    void Destroy() {
        Clear();
        buckets_.FreeStorage();
    }

    // _M_copy_from(ht) (e.g. 004AA950): the bucket vector emptied, reserve(ht's bucket count), that
    // many nulls appended; each of ht's chains copied from its head, a node at a time
    // (_M_new_node), each linked after the one before; then ht's count. The table holds no nodes.
    void CopyFrom(const hashtable& ht) {
        std::size_t n = ht.BucketCount();
        buckets_.Finish() = buckets_.Start();
        buckets_.template Reserve<BucketElement>(n);
        Node* none = nullptr;
        buckets_.template FillInsert<BucketElement>(buckets_.end(), n, none);
        for (std::size_t i = 0; i < n; ++i) {
            const Node* cur = ht.buckets_[i];
            if (!cur) continue;
            Node* copy = NewNode(cur->value);
            buckets_[i] = copy;
            for (cur = cur->next; cur; cur = cur->next) {
                Node* next = NewNode(cur->value);
                copy->next = next;
                copy = next;
            }
        }
        count_ = ht.count_;
    }
    // hashtable(const hashtable& ht) (e.g. 004A8A50): ht's functor bytes, an empty bucket vector,
    // no elements, then _M_copy_from(ht).
    void CopyConstruct(const hashtable& ht) {
        functors_[0] = ht.functors_[0];
        functors_[1] = ht.functors_[1];
        functors_[2] = ht.functors_[2];
        buckets_.ConstructEmpty();
        count_ = 0;
        CopyFrom(ht);
    }
    // operator=(ht) (e.g. 004A8AD0): nothing for the table itself; else clear(), ht's functor
    // bytes, _M_copy_from(ht).
    void Assign(const hashtable& ht) {
        if (&ht == this) return;
        Clear();
        functors_[0] = ht.functors_[0];
        functors_[1] = ht.functors_[1];
        functors_[2] = ht.functors_[2];
        CopyFrom(ht);
    }

    // _M_new_node(obj) (e.g. 00407420): a node from the pool, no next, a copy of obj.
    static Node* NewNode(const Value& obj) {
        Node* n = static_cast<Node*>(allocate(sizeof(Node)));
        n->next = nullptr;
        Traits::Construct(&n->value, obj);
        return n;
    }
    // _M_delete_node(n) (e.g. 004074B0): the value destroyed, the node back to the pool.
    static void DeleteNode(Node* n) {
        Traits::Destroy(&n->value);
        deallocate(n, sizeof(Node));
    }

private:
    std::uint8_t functors_[4];
    Buckets buckets_;
    std::size_t count_;
};

// The game's instantiations are __thiscall members, declared as __fastcall functions with the
// unused EDX second. Iterators are returned through a hidden first argument.
template <typename Traits>
struct HashtableAbi {
    using Table = hashtable<Traits>;
    using Node = typename Table::Node;
    using Value = typename Traits::Value;
    using Key = typename Traits::Key;
    using Iterator = typename Table::iterator;

    // hashtable(n, hf, eql, a) (e.g. 00405820; RET 0x10): returns this. The key extractor's byte
    // is whatever the stack slot of the hasher argument holds: the low byte of that pointer.
    static Table* __fastcall Construct(Table* self, void*, std::size_t n, const std::uint8_t* hasher,
                                       const std::uint8_t* equal, const void*) {
        self->Construct(n, *hasher, *equal, static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(hasher)));
        return self;
    }
    // hash_map() / hash_set() (e.g. 004639D0): hashtable(100, ...) inlined; the functor bytes are
    // an uninitialised stack byte in the original (0 here). Returns this.
    static Table* __fastcall ConstructDefault(Table* self) {
        self->Construct(100, 0, 0, 0);
        return self;
    }
    // _M_initialize_buckets(n) (e.g. 0040AA80; RET 4).
    static void __fastcall InitializeBuckets(Table* self, void*, std::size_t n) { self->InitializeBuckets(n); }
    // resize(hint) (e.g. 00406640; RET 4).
    static void __fastcall Resize(Table* self, void*, std::size_t hint) { self->Resize(hint); }
    // find(key) (e.g. 00405560; RET 8): the iterator into *out; returns out.
    static Iterator* __fastcall Find(Table* self, void*, Iterator* out, const Key* key) {
        out->cur = self->Find(*key);
        out->table = self;
        return out;
    }
    // begin() (e.g. 00404C30; RET 4).
    static Iterator* __fastcall Begin(Table* self, void*, Iterator* out) {
        out->cur = self->First();
        out->table = self;
        return out;
    }
    // _M_next_size(n) (__stl_next_prime inlined, e.g. 0040AE00; RET 4): ECX is not read.
    static std::size_t __fastcall NextSize(const Table*, void*, std::size_t n) { return NextPrime(n); }
    // _M_bkt_num_key(key) (0055C5C0; RET 4).
    static std::size_t __fastcall BucketOfKey(const Table* self, void*, const Key* key) { return self->BucketOf(*key); }
    // find_or_insert(obj) (e.g. 004065D0; RET 4): the stored value.
    static Value* __fastcall FindOrInsert(Table* self, void*, const Value* obj) { return &self->FindOrInsert(*obj); }
    // hash_map::operator[](key) (e.g. 004055A0; RET 4): find_or_insert of {key, zeroes}; the
    // address of the mapped part (after the key dword).
    static void* __fastcall Subscript(Table* self, void*, const Key* key) {
        Value obj{};
        obj.key = *key;
        return reinterpret_cast<unsigned char*>(&self->FindOrInsert(obj)) + sizeof(Key);
    }
    // hash_map::operator[](key) with a string key (e.g. 004297A0; RET 4): find_or_insert of a
    // temporary {copy of key, zeroes}, which is destroyed after; the address of the mapped part.
    static void* __fastcall SubscriptKey(Table* self, void*, const Key* key) {
        Value obj(*key);
        return &self->FindOrInsert(obj).mapped;
    }
    // hashtable(const hashtable&) (e.g. 004A8A50; RET 4): returns this.
    static Table* __fastcall CopyConstruct(Table* self, void*, const Table* source) {
        self->CopyConstruct(*source);
        return self;
    }
    // operator= (e.g. 004A8AD0; RET 4): returns this.
    static Table* __fastcall Assign(Table* self, void*, const Table* source) {
        self->Assign(*source);
        return self;
    }
    // _M_copy_from(ht) (e.g. 004AA950; RET 4).
    static void __fastcall CopyFrom(Table* self, void*, const Table* source) { self->CopyFrom(*source); }
    // pair<iterator, bool> (insert_unique's result): only the bool's byte is written after the iterator.
    struct InsertResult {
        Iterator it;
        bool inserted;
    };
    // insert_unique(obj) (0066B400; RET 8): the result into *out; returns out.
    static InsertResult* __fastcall InsertUnique(Table* self, void*, InsertResult* out, const Value* obj) {
        bool inserted = false;
        out->it.cur = self->InsertUnique(*obj, inserted);
        out->it.table = self;
        out->inserted = inserted;
        return out;
    }
    // erase(key) (e.g. 006BCCD0; RET 4): the number erased.
    static std::size_t __fastcall EraseKey(Table* self, void*, const Key* key) { return self->EraseKey(*key); }
    // erase(const_iterator) (e.g. 00405620; RET 8: the iterator by value).
    static void __fastcall EraseIterator(Table* self, void*, const Node* cur, const Table*) { self->EraseNode(cur); }
    // clear() (e.g. 004067B0).
    static void __fastcall Clear(Table* self) { self->Clear(); }
    // ~hashtable() (e.g. 004A8B70).
    static void __fastcall Destroy(Table* self) { self->Destroy(); }
    // _M_new_node(obj) (e.g. 00407420; RET 4): the node.
    static Node* __fastcall NewNode(Table*, void*, const Value* obj) { return Table::NewNode(*obj); }
    // _M_delete_node(n) (e.g. 004074B0; RET 4): nothing for null.
    static void __fastcall DeleteNode(Table*, void*, Node* n) {
        if (n) Table::DeleteNode(n);
    }
    // _Ht_iterator::_M_skip_to_next() (e.g. 004073C0): the node.
    static Node* __fastcall SkipToNext(const Iterator* self) { return self->table->SkipToNext(self->cur); }
    // _Ht_iterator::operator++() (00404980): returns this.
    static Iterator* __fastcall Increment(Iterator* self) {
        self->cur = self->table->Next(self->cur);
        return self;
    }
    // _Ht_iterator::operator++(int) (0040A700; RET 8): the old iterator into *out; returns out.
    static Iterator* __fastcall PostIncrement(Iterator* self, void*, Iterator* out, int) {
        Iterator old = *self;
        self->cur = self->table->Next(self->cur);
        *out = old;
        return out;
    }
    // _Ht_iterator(node, table) (e.g. 0040AD20; RET 8; also end() of some instantiations): returns this.
    static Iterator* __fastcall ConstructIterator(Iterator* self, void*, Node* cur, Table* table) {
        self->cur = cur;
        self->table = table;
        return self;
    }
};

// __stl_hash_string of a string (004298D0; ECX the string) and of a C string (00460BA0; ECX the text).
std::uint32_t __fastcall HashString(const string* s);
std::uint32_t __fastcall HashText(const char* s);

// A vector of records (start, finish, end of storage) followed by the map from a record's name to it
// (the item databases' weapon, armor, quick item, material and loot tables, the lever table).
struct RecordsByName {
    void* records[3];
    hashtable<StringKeyTraits<4>> byName;
};
// hash_map<string, T*>::operator[](const char*) on the map (e.g. 004AB720; RET 4): a string of the
// name, a {copy of it, null} value for find_or_insert, both freed after (the copy first); the
// mapped record, null when the name was not there (it is inserted).
void* __fastcall RecordNamed(RecordsByName* self, void*, const char* name);

// A hash_map<int, T> with a dword after the table (the terrain's light table, 00571050):
// hashtable(100) and the dword zeroed. Returns this.
struct TableWithDword {
    hashtable<DwordKeyTraits<8>> table;
    std::uint32_t extra;
};
TableWithDword* __fastcall ConstructTableWithDword(TableWithDword* self);

} // namespace stlp
