#pragma once

// STLport's hashtable (hash_map / hash_set) as compiled into game.exe. Its layout and iteration order (which
// game code depends on): docs/notes/standalone.md, "The image's source files".

#include "crt.h"
#include "mfc.h"
#include "stlp.h"
#include "stlp_alloc.h"
#include "stlp_pair.h"
#include "stlp_vector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace stlp {

std::size_t NextPrime(std::size_t n);

// A value whose first dword is the key, hashed as itself (hash<int>, hash<unsigned>, hash of a pointer).
// Bytes is the whole value's size: 4 for a hash_set of dwords, 8 for a map of dword to dword.
template <std::size_t Bytes>
struct DwordKeyed {
    std::uint32_t key;
    unsigned char rest[Bytes - 4];
};
template <>
struct DwordKeyed<4> {
    std::uint32_t key;
};

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

// pair<const string, T> with T plain data of MappedBytes. A temporary built from a key holds a copy of it
// and zeroes, as value_type(key, T()) does.
template <std::size_t MappedBytes>
struct StringKeyed {
    std::string key;
    unsigned char mapped[MappedBytes];
    StringKeyed() = default;
    explicit StringKeyed(const std::string& k) : key(k), mapped{} {}
};

// The traits of a hashtable keyed by a string (HashChars over its bytes; equal when the sizes and bytes
// are). The node copies the string and the mapped bytes; deleting it frees the string's block.
template <std::size_t MappedBytes>
struct StringKeyTraits {
    using Value = StringKeyed<MappedBytes>;
    using Key = std::string;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return HashChars(k.data(), k.data() + k.size()); }
    static bool Equal(const Key& a, const Key& b) { return a == b; }
    static void Construct(Value* to, const Value& from) {
        std::construct_at(&to->key, from.key);
        std::memcpy(to->mapped, from.mapped, MappedBytes);
    }
    static void Destroy(Value* p) { std::destroy_at(&p->key); }
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

// pair<const CString, float> (the party's named variables): hashed by the text up to its zero (HashChars),
// equal when _mbscmp says so. The node copies the CString (a shared reference) and the float's bits; deleting
// it releases the CString. A temporary built from a key holds a copy of it and zero.
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
        return gamecrt::mbscmp(static_cast<const char*>(a),
                               static_cast<const char*>(b)) == 0;
    }
    static void Construct(Value* to, const Value& from) {
        new (&to->key) mfc::CString(from.key);
        std::memcpy(&to->mapped, &from.mapped, sizeof(float));
    }
    static void Destroy(Value* p) { p->key.~CString(); }
};

// pair<const string, list<void*>> (the item stock's recipes by spell name): string keys as StringKeyTraits.
// The node copies the string, then the list; deleting it destroys the list, then the string.
struct StringListTraits {
    using Value = StringListPair;
    using Key = std::string;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return HashChars(k.data(), k.data() + k.size()); }
    static bool Equal(const Key& a, const Key& b) { return a == b; }
    static void Construct(Value* to, const Value& from) {
        std::construct_at(&to->key, from.key);
        std::construct_at(&to->mapped, from.mapped);
    }
    static void Destroy(Value* p) {
        std::destroy_at(&p->mapped);
        std::destroy_at(&p->key);
    }
};

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

    // hashtable(n, hf, eql, a): the functor bytes, an empty bucket vector, no
    // elements, then _M_initialize_buckets(n).
    void Construct(std::size_t n, std::uint8_t hasher, std::uint8_t equal, std::uint8_t extract) {
        functors_[0] = hasher;
        functors_[1] = equal;
        functors_[2] = extract;
        buckets_.ConstructEmpty();
        count_ = 0;
        InitializeBuckets(n);
    }

    // The constructor as an owner inlines it with the functor bytes left as they are (the item
    // stock's maps): an empty bucket vector, no elements, then _M_initialize_buckets(n).
    void ConstructBuckets(std::size_t n) {
        buckets_.ConstructEmpty();
        count_ = 0;
        InitializeBuckets(n);
    }

    // _M_initialize_buckets(n): reserve(NextPrime(n)), insert that many nulls at the end, no elements.
    void InitializeBuckets(std::size_t n) {
        std::size_t buckets = NextPrime(n);
        buckets_.template Reserve<BucketElement>(buckets);
        Node* none = nullptr;
        buckets_.template FillInsert<BucketElement>(buckets_.end(), buckets, none);
        count_ = 0;
    }

    // _M_initialize_buckets(n) on a table its owner zeroed: NextPrime(n) null buckets in
    // a fresh block, no elements.
    void InitZeroedBuckets(std::size_t n) {
        std::size_t buckets = NextPrime(n);
        buckets_.ConstructWithCapacity(buckets);
        std::memset(buckets_.Start(), 0, buckets * sizeof(Node*));
        buckets_.Finish() = buckets_.EndOfStorage();
        count_ = 0;
    }

    // resize(hint): when the hint exceeds the bucket count and the next prime does
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

    // find(key): the node holding `key`, or null.
    Node* Find(const Key& key) const {
        Node* n = buckets_[BucketOf(key)];
        while (n && !Traits::Equal(Traits::KeyOf(n->value), key)) n = n->next;
        return n;
    }

    // find_or_insert(obj): resize(count + 1), then the value with obj's key in its
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

    // insert_unique(obj): resize(count + 1), then the node with obj's key in its bucket
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

    // equal_range(key): the first node holding `key` and the first node after it that does not (its chain's
    // next one, else the head of the next non-empty bucket, else null); both null when no node holds the key.
    void EqualRange(const Key& key, Node*& first, Node*& last) const {
        std::size_t b = BucketOf(key);
        for (Node* cur = buckets_[b]; cur; cur = cur->next) {
            if (!Traits::Equal(Traits::KeyOf(cur->value), key)) continue;
            first = cur;
            for (Node* other = cur->next; other; other = other->next) {
                if (!Traits::Equal(Traits::KeyOf(other->value), key)) {
                    last = other;
                    return;
                }
            }
            last = nullptr;
            while (!last && ++b < BucketCount()) last = buckets_[b];
            return;
        }
        first = nullptr;
        last = nullptr;
    }

    // insert_equal_noresize(obj): a copy of obj in a new node right after the
    // first node with obj's key, else at the head of its bucket; the new node.
    Node* InsertEqualNoresize(const Value& obj) {
        std::size_t b = BucketOf(Traits::KeyOf(obj));
        Node* first = buckets_[b];
        for (Node* cur = first; cur; cur = cur->next) {
            if (Traits::Equal(Traits::KeyOf(cur->value), Traits::KeyOf(obj))) {
                Node* node = NewNode(obj);
                node->next = cur->next;
                cur->next = node;
                ++count_;
                return node;
            }
        }
        Node* node = NewNode(obj);
        node->next = first;
        buckets_[b] = node;
        ++count_;
        return node;
    }

    Node* First() const {
        for (std::size_t b = 0; b < BucketCount(); ++b)
            if (buckets_[b]) return buckets_[b];
        return nullptr;
    }

    // _Ht_iterator::_M_skip_to_next: the head of the first non-empty bucket after
    // the one `cur`'s key hashes to, or null.
    Node* SkipToNext(const Node* cur) const {
        std::size_t b = BucketOf(Traits::KeyOf(cur->value));
        Node* next = nullptr;
        while (!next && ++b < BucketCount()) next = buckets_[b];
        return next;
    }
    Node* Next(const Node* cur) const { return cur->next ? cur->next : SkipToNext(cur); }

    // erase(const_iterator): `p` unlinked from its bucket's chain and deleted when
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

    // clear: every chain deleted from its head, each bucket emptied, no elements.
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

    // erase(key): the nodes after the bucket's head that hold `key` deleted, then
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

    // ~hashtable: clear, then the bucket storage freed.
    void Destroy() {
        Clear();
        buckets_.FreeStorage();
    }

    // _M_copy_from(ht): the bucket vector emptied, reserve(ht's bucket count), that
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
    // hashtable(const hashtable& ht): ht's functor bytes, an empty bucket vector,
    // no elements, then _M_copy_from(ht).
    void CopyConstruct(const hashtable& ht) {
        functors_[0] = ht.functors_[0];
        functors_[1] = ht.functors_[1];
        functors_[2] = ht.functors_[2];
        buckets_.ConstructEmpty();
        count_ = 0;
        CopyFrom(ht);
    }
    // operator=(ht): nothing for the table itself; else clear, ht's functor bytes, _M_copy_from(ht).
    void Assign(const hashtable& ht) {
        if (&ht == this) return;
        Clear();
        functors_[0] = ht.functors_[0];
        functors_[1] = ht.functors_[1];
        functors_[2] = ht.functors_[2];
        CopyFrom(ht);
    }

    static Node* NewNode(const Value& obj) {
        Node* n = allocate_as<Node>();
        n->next = nullptr;
        Traits::Construct(&n->value, obj);
        return n;
    }
    static void DeleteNode(Node* n) {
        Traits::Destroy(&n->value);
        deallocate(n, sizeof(Node));
    }

private:
    std::uint8_t functors_[4];
    Buckets buckets_;
    std::size_t count_;
};

// The game's instantiations are __thiscall members, declared as __fastcall functions with an unused second
// argument. Iterators are returned through a hidden first argument.
template <typename Traits>
struct HashtableAbi : hashtable<Traits> {
    using Table = hashtable<Traits>;
    using Node = typename Table::Node;
    using Value = typename Traits::Value;
    using Key = typename Traits::Key;
    using Iterator = typename Table::iterator;

    // hashtable(n, hf, eql, a): returns this. The key extractor's byte
    // is whatever the stack slot of the hasher argument holds: the low byte of that pointer.
    Table* Construct(std::size_t n, const std::uint8_t* hasher, const std::uint8_t* equal, const void*) {
        Table::Construct(n, *hasher, *equal, static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(hasher)));
        return this;
    }
    // hash_map / hash_set: hashtable(100, ...) inlined; the functor bytes are
    // an uninitialised stack byte (0 here). Returns this.
    static Table* __fastcall ConstructDefault(Table* self) {
        self->Construct(100, 0, 0, 0);
        return self;
    }
    // _M_initialize_buckets(n), resize(hint), erase(key) and _M_copy_from(ht) are hashtable's own members,
    // registered through this type.
    Iterator* Find(Iterator* out, const Key& key) {
        out->cur = Table::Find(key);
        out->table = this;
        return out;
    }
    Iterator* Begin(Iterator* out) {
        out->cur = Table::First();
        out->table = this;
        return out;
    }
    std::size_t NextSize(std::size_t n) const { return NextPrime(n); }
    std::size_t BucketOfKey(const Key& key) const { return Table::BucketOf(key); }
    Value* FindOrInsert(const Value& obj) { return &Table::FindOrInsert(obj); }
    // hash_map::operator[](key): find_or_insert of {key, zeroes}; the
    // address of the mapped part (after the key dword).
    void* Subscript(const Key& key) {
        Value obj{};
        obj.key = key;
        return reinterpret_cast<unsigned char*>(&Table::FindOrInsert(obj)) + sizeof(Key);
    }
    // hash_map::operator[](key) with a string key: find_or_insert of a
    // temporary {copy of key, zeroes}, which is destroyed after; the address of the mapped part.
    void* SubscriptKey(const Key& key) {
        Value obj(key);
        return &Table::FindOrInsert(obj).mapped;
    }
    Table* CopyConstruct(const Table& source) {
        Table::CopyConstruct(source);
        return this;
    }
    Table* Assign(const Table& source) {
        Table::Assign(source);
        return this;
    }
    // pair<iterator, bool> (insert_unique's result): only the bool's byte is written after the iterator.
    struct InsertResult {
        Iterator it;
        bool inserted;
    };
    // insert_unique(obj): the result into *out; returns out.
    InsertResult* InsertUnique(InsertResult* out, const Value& obj) {
        bool inserted = false;
        out->it.cur = Table::InsertUnique(obj, inserted);
        out->it.table = this;
        out->inserted = inserted;
        return out;
    }
    // pair<iterator, iterator> (equal_range's result).
    struct Range {
        Iterator first;
        Iterator last;
    };
    // equal_range(key): the range into *out (EAX is not read).
    void EqualRange(Range* out, const Key& key) {
        Node* first;
        Node* last;
        Table::EqualRange(key, first, last);
        out->first.cur = first;
        out->first.table = this;
        out->last.cur = last;
        out->last.table = this;
    }
    // insert_equal_noresize(obj): the new node's iterator into *out; returns out.
    Iterator* InsertEqualNoresize(Iterator* out, const Value& obj) {
        Node* node = Table::InsertEqualNoresize(obj);
        out->table = this;
        out->cur = node;
        return out;
    }
    void EraseIterator(const Node* cur, const Table*) { Table::EraseNode(cur); }
    static void __fastcall Clear(Table* self) { self->Clear(); }
    static void __fastcall Destroy(Table* self) { self->Destroy(); }
    Node* NewNode(const Value& obj) { return Table::NewNode(obj); }
    void DeleteNode(Node* n) {
        if (n) Table::DeleteNode(n);
    }
    static Node* __fastcall SkipToNext(const Iterator* self) { return self->table->SkipToNext(self->cur); }
    static Iterator* __fastcall Increment(Iterator* self) {
        self->cur = self->table->Next(self->cur);
        return self;
    }
    struct IteratorAbi : Iterator {
        // _Ht_iterator::operator++(int): the old iterator into *out; returns out.
        Iterator* PostIncrement(Iterator* out, int) {
            Iterator old = *this;
            this->cur = this->table->Next(this->cur);
            *out = old;
            return out;
        }
        // _Ht_iterator(node, table) (also end of some instantiations): returns this.
        Iterator* ConstructIterator(Node* node, Table* owner) {
            this->cur = node;
            this->table = owner;
            return this;
        }
    };
};

std::uint32_t __fastcall HashString(const std::string* s);

// hash_map<string, T*>: the record tables' maps by name, the spells and modifiers by name.
using NameMap = hashtable<StringKeyTraits<4>>;
// operator[](name) and find_or_insert({name, null}): the slot of
// `name`'s record, a null one inserted for a new name (from a copy of the name, freed after).
template <typename T>
T*& NamedSlot(NameMap& map, const std::string& name) {
    NameMap::Value obj(name);
    return *reinterpret_cast<T**>(map.FindOrInsert(obj).mapped);
}
// The same on a typed hash_map<string, T*> (the item and spell tables' indexes): operator[] as
// STLport writes it, a {name, null} temporary for find_or_insert.
template <typename T>
T*& NamedSlot(hash_map<std::string, T*, StringHash>& map, const std::string& name) {
    return map.Subscript(name);
}

// A vector of records (start, finish, end of storage) followed by the map from a record's name to it
// (the item databases' weapon, armor, quick item, material and loot tables, the lever table).
struct RecordsByName {
    void* records[3];
    hashtable<StringKeyTraits<4>> byName;

};

// A hash_map<int, T> with a dword after the table (the terrain's light table):
// hashtable(100) and the dword zeroed. Returns this.
struct TableWithDword {
    hashtable<DwordKeyTraits<8>> table;
    std::uint32_t extra;
};

} // namespace stlp
