// Checks for the STLport hashtable instantiations of plain data (src/lib/stlp_hash.cpp).
//
// - Pure members (find, begin, _M_bkt_num_key, the iterators) compare with the original.
// - Constructors and _M_initialize_buckets run the original on a scratch table, give its bucket
//   storage back and run ours: the same storage when pool-sized, the same size, capacity, functor
//   bytes, null buckets and no elements.
// - Members that only free nodes (clear, erase(iterator), _M_delete_node) run the original, take
//   the nodes it freed off their free list again, restore the nodes and buckets and run ours, which
//   must free the same nodes in the same order and leave the same table.
// - find_or_insert and operator[] without a rehash run the original, unlink and free the node it
//   made, then run ours: the same node (the pool is LIFO), bytes, result and table. With a rehash
//   ours runs alone and the key must be found at the result.
// - resize with a rehash runs the original on a scratch copy (its nodes in our heap, its bucket
//   storage from the game's allocator), then ours: the same bucket count and the same chains.
// - _M_new_node: the original's node goes back to the pool, ours must take it with the same bytes.
// - ~hashtable runs ours only: the nodes on top of their free list (the last freed first).
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/game_hashes.h"
#include "../../src/lib/stlp.h"
#include "../../src/lib/stlp_hash.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::Name;

using Map8 = stlp::HashtableAbi<stlp::DwordKeyTraits<8>>;
using Set4 = stlp::HashtableAbi<stlp::DwordKeyTraits<4>>;
using Map28 = stlp::HashtableAbi<stlp::DwordKeyTraits<28>>;

// A table's words, bucket contents and nodes (bucket by bucket, each chain from its head).
template <typename Abi>
struct Snapshot {
    using Table = typename Abi::Table;
    using Node = typename Abi::Node;
    std::uint8_t functors[4];
    Node** start;
    Node** finish;
    Node** eos;
    std::size_t count;
    std::size_t first;             // the buckets held: [first, first + buckets.size())
    std::vector<Node*> buckets;
    std::vector<Node*> nodes;
    std::vector<unsigned char> bytes;

    static Snapshot Of(Table* t) { return Of(t, 0, t->BucketCount()); }
    // Only the bucket `b` (for members that touch one bucket; a whole-table copy per call would
    // make the checks quadratic).
    static Snapshot OfBucket(Table* t, std::size_t b) { return Of(t, b, b < t->BucketCount() ? 1 : 0); }
    static Snapshot Of(Table* t, std::size_t from, std::size_t many) {
        Snapshot s;
        std::memcpy(s.functors, t->Functors(), 4);
        s.start = t->BucketVector().Start();
        s.finish = t->BucketVector().Finish();
        s.eos = t->BucketVector().EndOfStorage();
        s.count = t->Count();
        s.first = from;
        s.buckets.assign(s.start + from, s.start + from + many);
        for (Node* head : s.buckets) {
            for (Node* n = head; n && s.nodes.size() <= s.count + 1; n = n->next) {
                s.nodes.push_back(n);
                const auto* p = reinterpret_cast<const unsigned char*>(n);
                s.bytes.insert(s.bytes.end(), p, p + sizeof(Node));
            }
        }
        return s;
    }
    // The table's words, buckets and node bytes put back.
    void Restore(Table* t) const {
        std::memcpy(t->Functors(), functors, 4);
        t->BucketVector().Start() = start;
        t->BucketVector().Finish() = finish;
        t->BucketVector().EndOfStorage() = eos;
        t->CountRef() = count;
        std::memcpy(start + first, buckets.data(), buckets.size() * sizeof(Node*));
        for (std::size_t i = 0; i < nodes.size(); ++i) std::memcpy(nodes[i], &bytes[i * sizeof(Node)], sizeof(Node));
    }
    bool operator==(const Snapshot& o) const {
        return std::memcmp(functors, o.functors, 3) == 0 && start == o.start && finish == o.finish && eos == o.eos &&
               count == o.count && first == o.first && buckets == o.buckets && nodes == o.nodes && bytes == o.bytes;
    }
};

// The first `n` blocks of a free list, from its top.
std::vector<void*> TopOf(void* const* list, std::size_t n) {
    std::vector<void*> out;
    for (void* p = *list; p && out.size() < n; p = *static_cast<void**>(p)) out.push_back(p);
    return out;
}

// A key for a mismatch line: a dword in hex, a string's first characters.
struct KeyText {
    char text[48];
    explicit KeyText(std::uint32_t key) { std::snprintf(text, sizeof(text), "%08X", static_cast<unsigned>(key)); }
    explicit KeyText(const stlp::string& key) {
        std::snprintf(text, sizeof(text), "\"%.40s\"", key.size() ? key.data() : "");
    }
    explicit KeyText(const mfc::CString& key) { std::snprintf(text, sizeof(text), "\"%.40s\"", static_cast<const char*>(key)); }
    explicit KeyText(const ShapeCacheHandle& key) {
        std::uint32_t height;
        std::memcpy(&height, &key.height, sizeof(height));
        std::snprintf(text, sizeof(text), "%08X height %08X", static_cast<unsigned>(key.key), static_cast<unsigned>(height));
    }
};

// A CString's reference count (CStringData::nRefs, before the length). The empty string's shared
// data and a locked buffer hold -1: those cannot be retained.
long& RefsOf(const mfc::CString& s) { return reinterpret_cast<long*>(const_cast<char*>(static_cast<const char*>(s)))[-3]; }

// A string's words (start, finish, end of storage): its block and the block's size.
struct StringWords {
    char* start;
    char* finish;
    char* eos;
    static StringWords Of(const stlp::string& s) {
        StringWords w;
        std::memcpy(&w, &s, sizeof(w));
        return w;
    }
};
static_assert(sizeof(StringWords) == sizeof(stlp::string));

// A string over `text` that owns nothing (for hashing and comparing; never destroyed).
struct BorrowedString {
    alignas(stlp::string) unsigned char raw[sizeof(stlp::string)];
    explicit BorrowedString(const char* text) {
        std::size_t n = std::strlen(text);
        StringWords w{const_cast<char*>(text), const_cast<char*>(text) + n, const_cast<char*>(text) + n + 1};
        std::memcpy(raw, &w, sizeof(w));
    }
    const stlp::string& get() const { return *reinterpret_cast<const stlp::string*>(raw); }
};

// Blocks freed in order (each onto the top of its pool list). Undo takes them off again, the last
// first, as long as each is on top of its list, and puts back the first word the free list
// overwrote (a string's first characters); Redo frees them again.
struct Trail {
    struct Block {
        void* p;
        std::size_t bytes;
        void* firstWord;
    };
    std::vector<Block> blocks;
    bool pooled = true;   // every block is pool-sized (the larger ones go to operator delete)

    void Add(void* p, std::size_t bytes) {
        if (!p) return;
        pooled = pooled && bytes <= 128;
        blocks.push_back({p, bytes, *static_cast<void**>(p)});
    }
    void AddString(const stlp::string& s) {
        StringWords w = StringWords::Of(s);
        Add(w.start, static_cast<std::size_t>(w.eos - w.start));
    }
    // For checks of ours only: blocks above 128 bytes (operator delete) are left out.
    void AddIfPooled(void* p, std::size_t bytes) {
        if (bytes <= 128) Add(p, bytes);
    }
    void AddStringIfPooled(const stlp::string& s) {
        StringWords w = StringWords::Of(s);
        AddIfPooled(w.start, static_cast<std::size_t>(w.eos - w.start));
    }
    template <typename T>
    void AddStorage(stlp::vector<T>& v) {
        AddIfPooled(v.Start(), static_cast<std::size_t>(v.EndOfStorage() - v.Start()) * sizeof(T));
    }
    // True when all were on top; otherwise the ones taken off are freed again.
    bool Undo() const {
        std::size_t i = blocks.size();
        for (; i > 0; --i) {
            const Block& b = blocks[i - 1];
            void** list = FreeList(b.bytes);
            if (*list != b.p) break;
            *list = *static_cast<void**>(b.p);
            *static_cast<void**>(b.p) = b.firstWord;
        }
        if (i == 0) return true;
        Redo(i);
        return false;
    }
    void Redo(std::size_t from = 0) const {
        for (std::size_t i = from; i < blocks.size(); ++i) {
            void** list = FreeList(blocks[i].bytes);
            *static_cast<void**>(blocks[i].p) = *list;
            *list = blocks[i].p;
        }
    }
};

template <typename T>
std::uintptr_t Word(T v) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(v);
    else return static_cast<std::uintptr_t>(v);
}

template <std::uintptr_t A, typename Abi>
struct HashCheck {
    using Table = typename Abi::Table;
    using Node = typename Abi::Node;
    using Value = typename Abi::Value;
    using Key = typename Abi::Key;
    using Iterator = typename Abi::Iterator;
    using Snap = Snapshot<Abi>;
    static inline void* original = nullptr;

    template <typename Fn>
    static Fn Original() { return reinterpret_cast<Fn>(original); }

    // --- Pure members -----------------------------------------------------------------------

    static Iterator* __fastcall Find(Table* self, void*, Iterator* out, const Key* key) {
        Iterator theirs{reinterpret_cast<Node*>(1), nullptr};
        bool returned = Original<decltype(&Abi::Find)>()(self, nullptr, &theirs, key) == &theirs;
        Iterator* result = Abi::Find(self, nullptr, out, key);
        bool ok = returned && result == out && out->cur == theirs.cur && out->table == theirs.table;
        char detail[96] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "key %s: ours %p, original's %p", KeyText(*key).text, out->cur, theirs.cur);
        ei::verify::Report(Name<A>("stlp::hashtable::find"), ok, detail);
        return result;
    }
    static Iterator* __fastcall Begin(Table* self, void*, Iterator* out) {
        Iterator theirs{reinterpret_cast<Node*>(1), nullptr};
        bool returned = Original<decltype(&Abi::Begin)>()(self, nullptr, &theirs) == &theirs;
        Iterator* result = Abi::Begin(self, nullptr, out);
        bool ok = returned && result == out && out->cur == theirs.cur && out->table == theirs.table;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p", out->cur, theirs.cur);
        ei::verify::Report(Name<A>("stlp::hashtable::begin"), ok, detail);
        return result;
    }
    static std::size_t __fastcall BucketOfKey(const Table* self, void*, const Key* key) {
        std::size_t theirs = Original<decltype(&Abi::BucketOfKey)>()(self, nullptr, key);
        std::size_t ours = Abi::BucketOfKey(self, nullptr, key);
        ei::verify::Check(Name<A>("stlp::hashtable::_M_bkt_num_key"), theirs, ours, "key %s", KeyText(*key).text);
        return ours;
    }
    static std::size_t __fastcall NextSize(const Table* self, void*, std::size_t n) {
        std::size_t theirs = Original<decltype(&Abi::NextSize)>()(self, nullptr, n);
        std::size_t ours = Abi::NextSize(self, nullptr, n);
        ei::verify::Check(Name<A>("stlp::__stl_next_prime"), theirs, ours, "n %u", static_cast<unsigned>(n));
        return ours;
    }
    static Node* __fastcall SkipToNext(const Iterator* self) {
        Node* theirs = Original<decltype(&Abi::SkipToNext)>()(self);
        Node* ours = Abi::SkipToNext(self);
        ei::verify::Check(Name<A>("stlp::_Ht_iterator::_M_skip_to_next"), Word(theirs), Word(ours), "node %p", self->cur);
        return ours;
    }
    static Iterator* __fastcall Increment(Iterator* self) {
        Iterator copy = *self;
        bool returned = Original<decltype(&Abi::Increment)>()(&copy) == &copy;
        Iterator* result = Abi::Increment(self);
        bool ok = returned && result == self && self->cur == copy.cur && self->table == copy.table;
        ei::verify::Report(Name<A>("stlp::_Ht_iterator::operator++"), ok, ok ? "" : "another node");
        return result;
    }
    static Iterator* __fastcall PostIncrement(Iterator* self, void*, Iterator* out, int unused) {
        Iterator copy = *self;
        Iterator theirs{};
        bool returned = Original<decltype(&Abi::PostIncrement)>()(&copy, nullptr, &theirs, unused) == &theirs;
        Iterator* result = Abi::PostIncrement(self, nullptr, out, unused);
        bool ok = returned && result == out && self->cur == copy.cur && out->cur == theirs.cur && out->table == theirs.table;
        ei::verify::Report(Name<A>("stlp::_Ht_iterator::operator++(int)"), ok, ok ? "" : "another node");
        return result;
    }
    static Iterator* __fastcall ConstructIterator(Iterator* self, void*, Node* cur, Table* table) {
        Iterator theirs{};
        bool returned = Original<decltype(&Abi::ConstructIterator)>()(&theirs, nullptr, cur, table) == &theirs;
        Iterator* result = Abi::ConstructIterator(self, nullptr, cur, table);
        bool ok = returned && result == self && self->cur == theirs.cur && self->table == theirs.table;
        ei::verify::Report(Name<A>("stlp::_Ht_iterator::_Ht_iterator"), ok, ok ? "" : "other words");
        return result;
    }

    // equal_range (S16a): both ends compared with the original's.
    using Range = typename Abi::Range;
    static void __fastcall EqualRange(Table* self, void*, Range* out, const Key* key) {
        Range theirs{};
        Original<decltype(&Abi::EqualRange)>()(self, nullptr, &theirs, key);
        Abi::EqualRange(self, nullptr, out, key);
        bool ok = out->first.cur == theirs.first.cur && out->first.table == theirs.first.table &&
                  out->last.cur == theirs.last.cur && out->last.table == theirs.last.table;
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "key %s: ours %p..%p, original's %p..%p", KeyText(*key).text, out->first.cur,
                          out->last.cur, theirs.first.cur, theirs.last.cur);
        ei::verify::Report(Name<A>("stlp::hashtable::equal_range"), ok, detail);
    }

    // --- Construction -----------------------------------------------------------------------

    // The original's empty table (scratch) against ours (self): storage, size, functor bytes.
    static void CompareFresh(const char* name, Table* scratch, Table* self, bool functors) {
        Snap theirs = Snap::Of(scratch);
        std::size_t capBytes = static_cast<std::size_t>(theirs.eos - theirs.start) * sizeof(Node*);
        stlp::deallocate(theirs.start, capBytes);
        Snap ours = Snap::Of(self);
        bool nulls = true;
        for (Node* b : ours.buckets) nulls = nulls && b == nullptr;
        bool ok = nulls && ours.count == 0 && theirs.count == 0 && ours.finish - ours.start == theirs.finish - theirs.start &&
                  ours.eos - ours.start == theirs.eos - theirs.start && (capBytes > 128 || ours.start == theirs.start) &&
                  (!functors || std::memcmp(ours.functors, theirs.functors, 3) == 0);
        char detail[128] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "ours %p %d/%d count %u functors %06X, original's %p %d/%d count %u functors %06X",
                          ours.start, static_cast<int>(ours.finish - ours.start), static_cast<int>(ours.eos - ours.start),
                          static_cast<unsigned>(ours.count), static_cast<unsigned>(ours.functors[0] | ours.functors[1] << 8 | ours.functors[2] << 16),
                          theirs.start, static_cast<int>(theirs.finish - theirs.start), static_cast<int>(theirs.eos - theirs.start),
                          static_cast<unsigned>(theirs.count), static_cast<unsigned>(theirs.functors[0] | theirs.functors[1] << 8 | theirs.functors[2] << 16));
        ei::verify::Report(name, ok, detail);
    }
    static Table* __fastcall Construct(Table* self, void*, std::size_t n, const std::uint8_t* hasher, const std::uint8_t* equal,
                                       const void* allocator) {
        alignas(Table) unsigned char storage[sizeof(Table)];
        std::memset(storage, 0xCD, sizeof(storage));
        Table* scratch = reinterpret_cast<Table*>(storage);
        bool returned = Original<decltype(&Abi::Construct)>()(scratch, nullptr, n, hasher, equal, allocator) == scratch;
        Table* result = Abi::Construct(self, nullptr, n, hasher, equal, allocator);
        CompareFresh(Name<A>("stlp::hashtable::hashtable"), scratch, self, returned && result == self);
        return result;
    }
    static Table* __fastcall ConstructDefault(Table* self) {
        alignas(Table) unsigned char storage[sizeof(Table)];
        std::memset(storage, 0xCD, sizeof(storage));
        Table* scratch = reinterpret_cast<Table*>(storage);
        bool returned = Original<decltype(&Abi::ConstructDefault)>()(scratch) == scratch;
        Table* result = Abi::ConstructDefault(self);
        // The functor bytes are an uninitialised stack byte in the original: not compared.
        CompareFresh(Name<A>("stlp::hash_map::hash_map"), scratch, self, false);
        if (!returned || result != self) ei::verify::Report(Name<A>("stlp::hash_map::hash_map"), false, "not this");
        return result;
    }
    static void __fastcall InitializeBuckets(Table* self, void*, std::size_t n) {
        if (self->BucketVector().Start() != nullptr) {
            Abi::InitializeBuckets(self, nullptr, n);
            ei::verify::Report(Name<A>("stlp::hashtable::_M_initialize_buckets"), self->Count() == 0, "elements left");
            return;
        }
        alignas(Table) unsigned char storage[sizeof(Table)];
        std::memcpy(storage, self, sizeof(Table));
        Table* scratch = reinterpret_cast<Table*>(storage);
        Original<decltype(&Abi::InitializeBuckets)>()(scratch, nullptr, n);
        Abi::InitializeBuckets(self, nullptr, n);
        CompareFresh(Name<A>("stlp::hashtable::_M_initialize_buckets"), scratch, self, true);
    }

    // --- Members that free nodes ------------------------------------------------------------

    // `theirs` and then `ours` on the same table (null: only `node`'s bytes): the nodes the original
    // freed are taken off the free list again and the table restored before ours runs.
    // `bucket`: the one bucket the member touches, or kAll.
    static constexpr std::size_t kAll = ~std::size_t{0};
    static Snap Take(Table* t, std::size_t bucket) { return bucket == kAll ? Snap::Of(t) : Snap::OfBucket(t, bucket); }

    template <typename Theirs, typename Ours>
    static void CheckFrees(const char* name, Table* table, Node* node, Theirs theirs, Ours ours, std::size_t bucket = kAll) {
        void** list = FreeList(sizeof(Node));
        void* top = *list;
        Snap before = table ? Take(table, bucket) : Snap{};
        unsigned char nodeBytes[sizeof(Node)] = {};
        if (node) std::memcpy(nodeBytes, node, sizeof(Node));
        std::size_t most = (table ? before.count : 0) + 1;
        theirs();
        Snap after = table ? Take(table, bucket) : Snap{};
        std::vector<void*> freed;
        for (void* p = *list; p != top && freed.size() <= most; p = *static_cast<void**>(p)) freed.push_back(p);
        if (freed.size() > most) {
            ei::verify::Report(name, false, "the original's free list does not lead back to its old top");
            return;   // the original's result stays
        }
        *list = top;
        if (table) before.Restore(table);
        if (node) std::memcpy(node, nodeBytes, sizeof(Node));
        ours();
        Snap mine = table ? Take(table, bucket) : Snap{};
        std::vector<void*> ourFreed = TopOf(list, freed.size());
        bool ok = ourFreed == freed && (freed.empty() ? *list == top : *static_cast<void**>(freed.back()) == top) &&
                  (!table || mine == after);
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "ours freed %u (top %p), the original %u (top %p), count %u / %u",
                          static_cast<unsigned>(ourFreed.size()), *list, static_cast<unsigned>(freed.size()),
                          freed.empty() ? top : freed.front(), static_cast<unsigned>(mine.count), static_cast<unsigned>(after.count));
        ei::verify::Report(name, ok, detail);
    }
    static void __fastcall Clear(Table* self) {
        CheckFrees(
            Name<A>("stlp::hashtable::clear"), self, nullptr, [&] { Original<decltype(&Abi::Clear)>()(self); },
            [&] { Abi::Clear(self); });
    }
    static void __fastcall EraseIterator(Table* self, void*, const Node* cur, const Table* table) {
        CheckFrees(
            Name<A>("stlp::hashtable::erase(iterator)"), self, nullptr,
            [&] { Original<decltype(&Abi::EraseIterator)>()(self, nullptr, cur, table); },
            [&] { Abi::EraseIterator(self, nullptr, cur, table); }, cur ? self->BucketOf(cur->value.key) : 0);
    }
    static void __fastcall DeleteNode(Table* self, void*, Node* n) {
        CheckFrees(
            Name<A>("stlp::hashtable::_M_delete_node"), nullptr, n,
            [&] { Original<decltype(&Abi::DeleteNode)>()(self, nullptr, n); }, [&] { Abi::DeleteNode(self, nullptr, n); });
    }

    // ~hashtable: ours only; the nodes must be on top of their free list, the last freed first.
    static void __fastcall Destroy(Table* self) {
        Snap before = Snap::Of(self);
        Abi::Destroy(self);
        std::vector<void*> expected(before.nodes.rbegin(), before.nodes.rend());
        bool ok = before.nodes.size() == before.count && TopOf(FreeList(sizeof(Node)), expected.size()) == expected;
        ei::verify::Report(Name<A>("stlp::hashtable::~hashtable"), ok, ok ? "" : "the nodes are not on top of their free list");
    }

    // --- Members that add a node ------------------------------------------------------------

    static bool WouldRehash(const Table* t) {
        std::size_t hint = t->Count() + 1;
        return hint > t->BucketCount() && stlp::NextPrime(hint) > t->BucketCount();
    }

    // `theirs` and then `ours` on the same table, both returning an address inside the node of `key`
    // (or the mapped value itself, `byValue`). `temps` strings of the key's size are allocated before
    // the node and freed after it (operator[]'s temporaries); to undo the original's insert they
    // are taken off their list, the node deleted (its string, then the node) and they put back.
    template <typename Theirs, typename Ours>
    static void CheckInsert(const char* name, Table* self, const Key& key, Theirs theirs, Ours ours, int temps = 0,
                            bool byValue = false) {
        char detail[128] = "";
        std::size_t tempBytes = 0;
        if constexpr (std::is_same_v<Key, stlp::string>) tempBytes = key.size() + 1;
        if (WouldRehash(self) || tempBytes > 128) {
            std::uintptr_t result = ours();
            Node* found = self->Find(key);
            std::uintptr_t mapped = 0;
            if constexpr (std::is_same_v<Key, stlp::string>) {
                if (byValue && found) std::memcpy(&mapped, &found->value.mapped, sizeof(mapped));
            }
            bool ok = found && (byValue ? result == mapped : result >= Word(found) && result < Word(found) + sizeof(Node));
            if (!ok) std::snprintf(detail, sizeof(detail), "key %s: result %08X, node %p", KeyText(key).text, static_cast<unsigned>(result), found);
            ei::verify::Report(name, ok, detail);
            return;
        }
        std::size_t b = self->BucketOf(key);
        Snap before = Snap::OfBucket(self, b);
        std::uintptr_t theirResult = theirs();
        Snap after = Snap::OfBucket(self, b);
        if (after.count == before.count + 1) {
            Node* made = self->BucketVector()[b];
            if (!made || made->next != before.buckets[0]) {
                ei::verify::Report(name, false, "the original's node is not at the head of its bucket");
                return;   // the original's result stays
            }
            self->BucketVector()[b] = made->next;
            self->CountRef() = before.count;
            void* taken[2] = {};
            void** list = temps ? FreeList(tempBytes) : nullptr;
            for (int i = 0; i < temps; ++i) {
                taken[i] = *list;
                *list = *static_cast<void**>(taken[i]);
            }
            Table::DeleteNode(made);
            for (int i = temps; i > 0; --i) {
                *static_cast<void**>(taken[i - 1]) = *list;
                *list = taken[i - 1];
            }
        }
        std::uintptr_t ourResult = ours();
        Snap mine = Snap::OfBucket(self, b);
        bool ok = ourResult == theirResult && mine == after;
        if (!ok)
            std::snprintf(detail, sizeof(detail), "key %s: ours %08X count %u, original's %08X count %u", KeyText(key).text,
                          static_cast<unsigned>(ourResult), static_cast<unsigned>(mine.count), static_cast<unsigned>(theirResult),
                          static_cast<unsigned>(after.count));
        ei::verify::Report(name, ok, detail);
    }
    // insert_unique: the node's address with the bool in its low bit (nodes are dword-aligned).
    using InsertResult = typename Abi::InsertResult;
    static InsertResult* __fastcall InsertUnique(Table* self, void*, InsertResult* out, const Value* obj) {
        InsertResult* result = nullptr;
        CheckInsert(
            Name<A>("stlp::hashtable::insert_unique"), self, obj->key,
            [&] {
                InsertResult theirs{};
                bool returned = Original<decltype(&Abi::InsertUnique)>()(self, nullptr, &theirs, obj) == &theirs;
                return returned && theirs.it.table == self ? Word(theirs.it.cur) | (theirs.inserted ? 1u : 0u) : 0u;
            },
            [&] {
                result = Abi::InsertUnique(self, nullptr, out, obj);
                return result == out && out->it.table == self ? Word(out->it.cur) | (out->inserted ? 1u : 0u) : 0u;
            });
        return result;
    }
    static void* __fastcall SubscriptKey(Table* self, void*, const Key* key) {
        void* result = nullptr;
        CheckInsert(
            Name<A>("stlp::hash_map::operator[]"), self, *key,
            [&] { return Word(Original<decltype(&Abi::SubscriptKey)>()(self, nullptr, key)); },
            [&] { return Word(result = Abi::SubscriptKey(self, nullptr, key)); }, 1);
        return result;
    }

    // --- Members that free nodes with strings -----------------------------------------------

    // `theirs`, then `ours` on the same table, each expected to free `trail` in order: the
    // original's frees are undone and the table restored before ours runs. When a block is not
    // pool-sized ours runs alone.
    template <typename Theirs, typename Ours>
    static void CheckTrail(const char* name, Table* self, const Trail& trail, Theirs theirs, Ours ours, std::size_t bucket = kAll) {
        if (!trail.pooled) {
            std::uintptr_t result = ours();
            (void)result;
            ei::verify::Report(name, true, "");
            return;
        }
        Snap before = Take(self, bucket);
        std::uintptr_t theirResult = theirs();
        Snap after = Take(self, bucket);
        if (!trail.Undo()) {
            ei::verify::Report(name, false, "the original freed other blocks");
            return;   // the original's result stays
        }
        before.Restore(self);
        std::uintptr_t ourResult = ours();
        Snap mine = Take(self, bucket);
        bool freed = trail.Undo();
        if (freed) trail.Redo();
        bool ok = freed && ourResult == theirResult && mine == after;
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "%s, result %u / %u, count %u / %u", freed ? "the same blocks" : "other blocks freed",
                          static_cast<unsigned>(ourResult), static_cast<unsigned>(theirResult), static_cast<unsigned>(mine.count),
                          static_cast<unsigned>(after.count));
        ei::verify::Report(name, ok, detail);
    }
    // clear() of string keys: each node's string, then the node, bucket by bucket.
    static void __fastcall ClearStrings(Table* self) {
        Trail trail;
        for (std::size_t b = 0; b < self->BucketCount(); ++b) {
            for (Node* n = self->BucketVector()[b]; n; n = n->next) {
                trail.AddString(n->value.key);
                trail.Add(n, sizeof(Node));
            }
        }
        CheckTrail(
            Name<A>("stlp::hashtable::clear"), self, trail,
            [&] { Original<decltype(&Abi::Clear)>()(self); return std::uintptr_t{0}; },
            [&] { Abi::Clear(self); return std::uintptr_t{0}; });
    }
    // erase(key) of string keys: the matching nodes after the bucket's head, then the head.
    static std::size_t __fastcall EraseKeyStrings(Table* self, void*, const Key* key) {
        Trail trail;
        Node* first = self->BucketVector()[self->BucketOf(*key)];
        if (first) {
            for (Node* n = first->next; n; n = n->next) {
                if (n->value.key == *key) {
                    trail.AddString(n->value.key);
                    trail.Add(n, sizeof(Node));
                }
            }
            if (first->value.key == *key) {
                trail.AddString(first->value.key);
                trail.Add(first, sizeof(Node));
            }
        }
        std::size_t result = 0;
        CheckTrail(
            Name<A>("stlp::hashtable::erase(key)"), self, trail,
            [&] { return Word(Original<decltype(&Abi::EraseKey)>()(self, nullptr, key)); },
            [&] { return Word(result = Abi::EraseKey(self, nullptr, key)); }, self->BucketOf(*key));
        return result;
    }
    // erase(key) of plain values (S12e): the matching nodes after the bucket's head, then the head.
    static std::size_t __fastcall EraseKeyPlain(Table* self, void*, const Key* key) {
        Trail trail;
        Node* first = self->BucketVector()[self->BucketOf(*key)];
        if (first) {
            for (Node* n = first->next; n; n = n->next)
                if (n->value.key == *key) trail.Add(n, sizeof(Node));
            if (first->value.key == *key) trail.Add(first, sizeof(Node));
        }
        std::size_t result = 0;
        CheckTrail(
            Name<A>("stlp::hashtable::erase(key)"), self, trail,
            [&] { return Word(Original<decltype(&Abi::EraseKey)>()(self, nullptr, key)); },
            [&] { return Word(result = Abi::EraseKey(self, nullptr, key)); }, self->BucketOf(*key));
        return result;
    }
    static Value* __fastcall FindOrInsert(Table* self, void*, const Value* obj) {
        Value* result = nullptr;
        CheckInsert(
            Name<A>("stlp::hashtable::find_or_insert"), self, obj->key,
            [&] { return Word(Original<decltype(&Abi::FindOrInsert)>()(self, nullptr, obj)); },
            [&] { return Word(result = Abi::FindOrInsert(self, nullptr, obj)); });
        return result;
    }
    static void* __fastcall Subscript(Table* self, void*, const Key* key) {
        void* result = nullptr;
        CheckInsert(
            Name<A>("stlp::hash_map::operator[]"), self, *key,
            [&] { return Word(Original<decltype(&Abi::Subscript)>()(self, nullptr, key)); },
            [&] { return Word(result = Abi::Subscript(self, nullptr, key)); });
        return result;
    }
    static Node* __fastcall NewNode(Table* self, void*, const Value* obj) {
        if constexpr (std::is_same_v<Key, stlp::string>) {
            if (obj->key.size() + 1 > 128) {   // the string's block from operator new: ours alone
                Node* ours = Abi::NewNode(self, nullptr, obj);
                bool ok = !ours->next && ours->value.key == obj->key &&
                          std::memcmp(ours->value.mapped, obj->mapped, sizeof(obj->mapped)) == 0;
                ei::verify::Report(Name<A>("stlp::hashtable::_M_new_node"), ok, ok ? "" : "another value");
                return ours;
            }
        }
        // The original's node deleted again (its string first, then the node: the reverse of how
        // they were taken), so ours must take the same blocks.
        Node* theirs = Original<decltype(&Abi::NewNode)>()(self, nullptr, obj);
        unsigned char theirBytes[sizeof(Node)];
        std::memcpy(theirBytes, theirs, sizeof(Node));
        Table::DeleteNode(theirs);
        Node* ours = Abi::NewNode(self, nullptr, obj);
        bool ok = ours == theirs && std::memcmp(ours, theirBytes, sizeof(Node)) == 0;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p", ours, theirs);
        ei::verify::Report(Name<A>("stlp::hashtable::_M_new_node"), ok, detail);
        return ours;
    }

    // --- resize -------------------------------------------------------------------------------

    static void __fastcall Resize(Table* self, void*, std::size_t hint) {
        const char* name = Name<A>("stlp::hashtable::resize");
        if (hint <= self->BucketCount() || stlp::NextPrime(hint) <= self->BucketCount()) {
            // Nothing happens: the table's words (bucket storage, count) stay.
            Snap before = Snap::OfBucket(self, 0);
            Original<decltype(&Abi::Resize)>()(self, nullptr, hint);
            bool same = Snap::OfBucket(self, 0) == before;
            Abi::Resize(self, nullptr, hint);
            ei::verify::Report(name, same && Snap::OfBucket(self, 0) == before, "a resize below the bucket count changed the table");
            return;
        }
        Snap before = Snap::Of(self);
        // The original on a copy: its nodes in our heap, its bucket storage from the game's allocator.
        std::size_t buckets = before.buckets.size();
        std::size_t capacity = static_cast<std::size_t>(before.eos - before.start);
        // Raw copies: a node's value is only read by resize (a string's words stay the original's).
        std::vector<unsigned char> cloneBytes(before.nodes.size() * sizeof(Node) + 1);
        Node* clones = reinterpret_cast<Node*>(cloneBytes.data());
        std::size_t index = 0;
        alignas(Table) unsigned char storage[sizeof(Table)];
        std::memcpy(storage, self, sizeof(Table));
        Table* copy = reinterpret_cast<Table*>(storage);
        Node** start = static_cast<Node**>(stlp::allocate(capacity * sizeof(Node*)));
        copy->BucketVector().Start() = start;
        copy->BucketVector().Finish() = start + buckets;
        copy->BucketVector().EndOfStorage() = start + capacity;
        for (std::size_t b = 0; b < buckets; ++b) {
            Node** link = &start[b];
            for (Node* n = before.buckets[b]; n; n = n->next) {
                Node* clone = &clones[index++];
                std::memcpy(clone, n, sizeof(Node));
                *link = clone;
                link = &clone->next;
            }
            *link = nullptr;
        }
        Original<decltype(&Abi::Resize)>()(copy, nullptr, hint);
        Abi::Resize(self, nullptr, hint);
        // Same bucket count and capacity; each chain the same nodes (clone i stands for node i).
        bool ok = copy->BucketCount() == self->BucketCount() &&
                  copy->BucketVector().EndOfStorage() - copy->BucketVector().Start() ==
                      self->BucketVector().EndOfStorage() - self->BucketVector().Start();
        for (std::size_t b = 0; ok && b < self->BucketCount(); ++b) {
            Node* mine = self->BucketVector()[b];
            Node* theirs = copy->BucketVector()[b];
            for (; ok && mine && theirs; mine = mine->next, theirs = theirs->next)
                ok = before.nodes[static_cast<std::size_t>(theirs - clones)] == mine;
            ok = ok && !mine && !theirs;
        }
        copy->BucketVector().FreeStorage();
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "hint %u: %u buckets, the original %u", static_cast<unsigned>(hint),
                               static_cast<unsigned>(self->BucketCount()), static_cast<unsigned>(copy->BucketCount()));
        ei::verify::Report(name, ok, detail);
    }
};

template <std::uintptr_t A> using H8 = HashCheck<A, Map8>;
template <std::uintptr_t A> using H4 = HashCheck<A, Set4>;
template <std::uintptr_t A> using H28 = HashCheck<A, Map28>;
using Str4 = stlp::HashtableAbi<stlp::StringKeyTraits<4>>;
template <std::uintptr_t A> using S4 = HashCheck<A, Str4>;

// __stl_hash_string: pure, compared with the original.
template <std::uintptr_t A>
struct HashFunctionCheck {
    static inline void* original = nullptr;
    static std::uint32_t __fastcall String(const stlp::string* s) {
        std::uint32_t theirs = reinterpret_cast<decltype(&stlp::HashString)>(original)(s);
        std::uint32_t ours = stlp::HashString(s);
        ei::verify::Check(Name<A>("stlp::__stl_hash_string"), theirs, ours, "%s", KeyText(*s).text);
        return ours;
    }
    static std::uint32_t __fastcall Text(const char* s) {
        std::uint32_t theirs = reinterpret_cast<decltype(&stlp::HashText)>(original)(s);
        std::uint32_t ours = stlp::HashText(s);
        ei::verify::Check(Name<A>("stlp::__stl_hash_string"), theirs, ours, "\"%.40s\"", s);
        return ours;
    }
};

// operator[](const char*) on a record table: as operator[], with two temporary strings.
template <std::uintptr_t A>
struct RecordNamedCheck {
    static inline void* original = nullptr;
    static void* __fastcall Run(stlp::RecordsByName* self, void*, const char* name) {
        BorrowedString key(name);
        void* result = nullptr;
        S4<A>::CheckInsert(
            Name<A>("stlp::hash_map::operator[](const char*)"), &self->byName, key.get(),
            [&] { return Word(reinterpret_cast<decltype(&stlp::RecordNamed)>(original)(self, nullptr, name)); },
            [&] { return Word(result = stlp::RecordNamed(self, nullptr, name)); }, 2, true);
        return result;
    }
};

// 00571050: hash_map() with the dword after it; ours only (the extra dword zeroed, an empty table).
stlp::TableWithDword* __fastcall ConstructTableWithDword(stlp::TableWithDword* self) {
    stlp::TableWithDword* result = stlp::ConstructTableWithDword(self);
    bool nulls = true;
    for (std::size_t b = 0; b < self->table.BucketCount(); ++b) nulls = nulls && self->table.BucketVector()[b] == nullptr;
    bool ok = result == self && self->extra == 0 && self->table.Count() == 0 && self->table.BucketCount() == 193 && nulls;
    ei::verify::Report("stlp::hash_map::hash_map 00571050", ok, ok ? "" : "not an empty table of 193 buckets");
    return result;
}

// --- S11a: values with their own copy and destruction ------------------------------------------

using CStrFloat = stlp::HashtableAbi<stlp::CStringFloatTraits>;
using StrList = stlp::HashtableAbi<stlp::StringListTraits>;
using FiguredGroups = stlp::HashtableAbi<gamehash::FiguredGroupsTraits>;
using Dressed = stlp::HashtableAbi<gamehash::DressedTraits>;
template <std::uintptr_t A> using CF = HashCheck<A, CStrFloat>;
template <std::uintptr_t A> using SL = HashCheck<A, StrList>;
template <std::uintptr_t A> using SH = HashCheck<A, gamehash::ShapeAbi>;

// The pool blocks a value's destruction frees, in order (ours-only checks; blocks above 128 bytes
// are left out).
void AddValueBlocks(Trail&, const stlp::CStringFloat&) {}   // the CString's block is MFC's
void AddValueBlocks(Trail& trail, stlp::StringKeyed<4>& v) { trail.AddStringIfPooled(v.key); }
void AddValueBlocks(Trail& trail, stlp::StringList& v) {
    using ListNode = stlp::list<void*>::Node;
    for (ListNode* n = v.mapped.First(); n != v.mapped.End(); n = n->next) trail.Add(n, sizeof(ListNode));
    trail.Add(v.mapped.head, sizeof(ListNode));
    trail.AddStringIfPooled(v.key);
}
void AddValueBlocks(Trail& trail, stlp::DwordKeyedObject<DressedUnitRecord>& v) {
    trail.AddStorage(v.mapped.extraWeapons);
    trail.AddStorage(v.mapped.items);
}
void AddValueBlocks(Trail& trail, stlp::DwordKeyedObject<FiguredUnitRecord>& v) {
    trail.AddStorage(recordvec::FiguredUnitElement::NamesOf(v.mapped));   // the names are MFC's blocks
    trail.AddStorage(v.mapped.weaponIds);
    trail.AddStorage(v.mapped.armourIds);
}
// A party frees many blocks of its own, none from the nodes' list (checked: a mismatch says otherwise).
void AddValueBlocks(Trail&, stlp::DwordKeyedObject<PartyInPlace>&) {}
// The node blocks of a table (each value's blocks first) and its bucket storage when `storage`.
template <typename Table>
void AddTableBlocks(Trail& trail, Table& t, bool storage);
void AddValueBlocks(Trail& trail, stlp::DwordKeyedObject<gamehash::FiguredTable>& v) { AddTableBlocks(trail, v.mapped, true); }
template <typename Table>
void AddTableBlocks(Trail& trail, Table& t, bool storage) {
    for (std::size_t b = 0; b < t.BucketCount(); ++b) {
        for (auto* n = t.BucketVector()[b]; n; n = n->next) {
            AddValueBlocks(trail, n->value);
            trail.Add(n, sizeof(*n));
        }
    }
    if (storage) trail.AddStorage(t.BucketVector());
}

// clear() / ~hashtable() of ours alone: no elements, null buckets, and the blocks freed in order
// (each value's, then its node; bucket by bucket, each chain from its head; then the storage).
template <std::uintptr_t A, typename Abi>
struct ClearOurs {
    using Table = typename Abi::Table;
    static void Run(const char* name, Table* self, bool storage) {
        Trail trail;
        AddTableBlocks(trail, *self, storage);
        bool empty = true;
        if (storage) {
            Abi::Destroy(self);
        } else {
            Abi::Clear(self);
            for (std::size_t b = 0; b < self->BucketCount(); ++b) empty = empty && self->BucketVector()[b] == nullptr;
        }
        bool freed = trail.Undo();
        if (freed) trail.Redo();
        bool ok = freed && empty && self->Count() == 0;
        ei::verify::Report(name, ok, ok ? "" : freed ? "elements left" : "other blocks freed, or in another order");
    }
    static void __fastcall Clear(Table* self) { Run(Name<A>("stlp::hashtable::clear"), self, false); }
    static void __fastcall Destroy(Table* self) { Run(Name<A>("stlp::hashtable::~hashtable"), self, true); }
};

template <std::uintptr_t A> using ClearStr4 = ClearOurs<A, Str4>;
template <std::uintptr_t A> using ClearStrList = ClearOurs<A, StrList>;
template <std::uintptr_t A> using ClearParties = ClearOurs<A, gamehash::PartyAbi>;
template <std::uintptr_t A> using ClearFigured = ClearOurs<A, FiguredGroups>;
template <std::uintptr_t A> using ClearDressed = ClearOurs<A, Dressed>;
template <std::uintptr_t A> using ClearCStrFloat = ClearOurs<A, CStrFloat>;

// --- hash_map<CString, float> ---

// Every key's reference retained (true), or none when one cannot be (the empty string's data or a
// locked buffer).
template <typename Table>
bool RetainAll(Table* t) {
    for (std::size_t b = 0; b < t->BucketCount(); ++b)
        for (auto* n = t->BucketVector()[b]; n; n = n->next)
            if (RefsOf(n->value.key) < 1) return false;
    for (std::size_t b = 0; b < t->BucketCount(); ++b)
        for (auto* n = t->BucketVector()[b]; n; n = n->next) ++RefsOf(n->value.key);
    return true;
}

template <std::uintptr_t A>
struct CStringFloatCheck {
    using Check = CF<A>;
    using Table = CStrFloat::Table;
    using Node = CStrFloat::Node;
    static inline void* original = nullptr;
    template <typename Fn> static Fn Original() { return reinterpret_cast<Fn>(original); }

    // operator[]: as find_or_insert (the temporary pair only shares the key's text: no pool blocks).
    static void* __fastcall Subscript(Table* self, void*, const mfc::CString* key) {
        void* result = nullptr;
        Check::CheckInsert(
            Name<A>("stlp::hash_map::operator[]"), self, *key,
            [&] { return Word(Original<decltype(&CStrFloat::SubscriptKey)>()(self, nullptr, key)); },
            [&] { return Word(result = CStrFloat::SubscriptKey(self, nullptr, key)); });
        return result;
    }
    // clear(): each key retained first, so the original's release leaves its string; the nodes it
    // freed are taken back and ours runs (and releases for good). Ours alone when a key cannot be
    // retained.
    static void __fastcall Clear(Table* self) {
        if (!RetainAll(self)) {
            ClearOurs<A, CStrFloat>::Clear(self);
            return;
        }
        Check::CheckFrees(
            Name<A>("stlp::hashtable::clear"), self, nullptr, [&] { Original<decltype(&CStrFloat::Clear)>()(self); },
            [&] { CStrFloat::Clear(self); });
    }
    // _M_delete_node: as clear, for the one node.
    static void __fastcall DeleteNode(Table* self, void*, Node* n) {
        if (!n || RefsOf(n->value.key) < 1) {
            void* top = *FreeList(sizeof(Node));
            CStrFloat::DeleteNode(self, nullptr, n);
            bool ok = !n || (*FreeList(sizeof(Node)) == n && *reinterpret_cast<void**>(n) == top);
            ei::verify::Report(Name<A>("stlp::hashtable::_M_delete_node"), ok, ok ? "" : "the node is not on top of its free list");
            return;
        }
        ++RefsOf(n->value.key);
        Check::CheckFrees(
            Name<A>("stlp::hashtable::_M_delete_node"), nullptr, n,
            [&] { Original<decltype(&CStrFloat::DeleteNode)>()(self, nullptr, n); }, [&] { CStrFloat::DeleteNode(self, nullptr, n); });
    }

    // The copies (ours only): the same bucket count, count and functor bytes as the source, each
    // chain the same keys (the same shared text) and value bits in order, in nodes of their own.
    static bool SameAs(Table* self, const Table* source) {
        Table* from = const_cast<Table*>(source);
        bool ok = self->BucketCount() == from->BucketCount() && self->Count() == from->Count() &&
                  std::memcmp(self->Functors(), from->Functors(), 3) == 0;
        for (std::size_t b = 0; ok && b < self->BucketCount(); ++b) {
            Node* mine = self->BucketVector()[b];
            Node* theirs = from->BucketVector()[b];
            for (; ok && mine && theirs; mine = mine->next, theirs = theirs->next)
                ok = mine != theirs && static_cast<const char*>(mine->value.key) == static_cast<const char*>(theirs->value.key) &&
                     std::memcmp(&mine->value.mapped, &theirs->value.mapped, sizeof(float)) == 0;
            ok = ok && !mine && !theirs;
        }
        return ok;
    }
    static Table* __fastcall CopyConstruct(Table* self, void*, const Table* source) {
        Table* result = CStrFloat::CopyConstruct(self, nullptr, source);
        bool ok = result == self && SameAs(self, source);
        ei::verify::Report(Name<A>("stlp::hashtable::hashtable(const hashtable&)"), ok, ok ? "" : "not a copy of the source");
        return result;
    }
    static Table* __fastcall Assign(Table* self, void*, const Table* source) {
        Table* result = CStrFloat::Assign(self, nullptr, source);
        bool ok = result == self && SameAs(self, source);
        ei::verify::Report(Name<A>("stlp::hashtable::operator="), ok, ok ? "" : "not a copy of the source");
        return result;
    }
    static void __fastcall CopyFrom(Table* self, void*, const Table* source) {
        CStrFloat::CopyFrom(self, nullptr, source);
        bool ok = SameAs(self, source);
        ei::verify::Report(Name<A>("stlp::hashtable::_M_copy_from"), ok, ok ? "" : "not a copy of the source");
    }
};

// --- hash_map<int, CParty> (ours only: a party's copy and destruction cannot be undone) ---

template <std::uintptr_t A>
struct PartyCheck {
    using Table = gamehash::PartyTable;
    using Node = Table::Node;
    // operator[]: the key's node afterwards (one more element when it was not there); the result
    // its party.
    static PartyInPlace* __fastcall At(Table* self, void*, const std::uint32_t* key) {
        bool had = self->Find(*key) != nullptr;
        std::size_t count = self->Count();
        PartyInPlace* result = gamehash::PartyAt(self, nullptr, key);
        Node* found = self->Find(*key);
        bool ok = found && result == &found->value.mapped && self->Count() == count + (had ? 0 : 1);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "key %s: result %p, node %p", KeyText(*key).text, result, found);
        ei::verify::Report(Name<A>("stlp::hash_map::operator[]"), ok, detail);
        return result;
    }
    // _M_delete_node: the node on top of its free list.
    static void __fastcall DeleteNode(Table* self, void*, Node* n) {
        gamehash::PartyAbi::DeleteNode(self, nullptr, n);
        bool ok = !n || *FreeList(sizeof(Node)) == n;
        ei::verify::Report(Name<A>("stlp::hashtable::_M_delete_node"), ok, ok ? "" : "the node is not on top of its free list");
    }
};

// --- The unit shapes' cache ---

template <std::uintptr_t A>
struct ShapeCheck {
    using Check = SH<A>;
    using Table = gamehash::ShapeTable;
    using Node = Table::Node;
    using Abi = gamehash::ShapeAbi;
    static inline void* original = nullptr;
    template <typename Fn> static Fn Original() { return reinterpret_cast<Fn>(original); }

    // 005BCCB0: the original on a copy of the handle, ours on the handle.
    static ShapeCacheHandle* __fastcall InitHandle(ShapeCacheHandle* self) {
        ShapeCacheHandle theirs;
        std::memcpy(&theirs, self, sizeof(theirs));
        bool returned = Original<decltype(&gamehash::InitShapeHandle)>()(&theirs) == &theirs;
        ShapeCacheHandle* result = gamehash::InitShapeHandle(self);
        bool ok = returned && result == self && std::memcmp(&theirs, self, sizeof(theirs)) == 0;
        ei::verify::Report(Name<A>("ShapeCacheHandle::Init"), ok, ok ? "" : "other words");
        return result;
    }
    // operator[]: the new pair holds a null RefPtr and the handle's reference, which deleting the
    // node gives back, so it is compared with the original's as find_or_insert of plain data is.
    static RefPtr* __fastcall Slot(Table* self, void*, const ShapeCacheHandle* handle) {
        RefPtr* result = nullptr;
        Check::CheckInsert(
            Name<A>("stlp::hash_map::operator[]"), self, *handle,
            [&] { return Word(Original<decltype(&gamehash::ShapeSlot)>()(self, nullptr, handle)); },
            [&] { return Word(result = gamehash::ShapeSlot(self, nullptr, handle)); });
        return result;
    }
    // find_or_insert: a new pair shares its shape (which marks it copy-on-share), so an insert runs
    // ours alone (the key found at the result, one more element); a lookup is compared with the
    // original.
    using ShapePair = gamehash::ShapePair;
    static ShapePair* __fastcall FindOrInsert(Table* self, void*, const ShapePair* obj) {
        const char* name = Name<A>("stlp::hashtable::find_or_insert");
        if (Check::WouldRehash(self) || !self->Find(obj->key)) {
            std::size_t count = self->Count();
            ShapePair* result = Abi::FindOrInsert(self, nullptr, obj);
            Node* found = self->Find(obj->key);
            bool ok = found && result == &found->value && self->Count() == count + 1;
            ei::verify::Report(name, ok, ok ? "" : "the new pair is not the key's");
            return result;
        }
        ShapePair* theirs = Original<decltype(&Abi::FindOrInsert)>()(self, nullptr, obj);
        ShapePair* ours = Abi::FindOrInsert(self, nullptr, obj);
        ei::verify::Check(name, Word(theirs), Word(ours), "key %s", KeyText(obj->key).text);
        return ours;
    }
};

// hash_map<int, unsigned>::operator[] of the bridge heights: the original's new value is the
// caller's stack dword after the key (ours zero); ours gets the original's before the compare.
template <std::uintptr_t A>
struct BridgeCheck {
    static inline void* original = nullptr;
    static void* __fastcall Subscript(stlp::hashtable<stlp::DwordKeyTraits<8>>* self, void*, const std::uint32_t* key) {
        void* result = nullptr;
        bool theirsRan = false;
        std::uint32_t value = 0;
        H8<A>::CheckInsert(
            Name<A>("stlp::hash_map::operator[]"), self, *key,
            [&] {
                void* theirs = reinterpret_cast<decltype(&Map8::Subscript)>(original)(self, nullptr, key);
                std::memcpy(&value, theirs, sizeof(value));
                theirsRan = true;
                return Word(theirs);
            },
            [&] {
                result = Map8::Subscript(self, nullptr, key);
                if (theirsRan) std::memcpy(result, &value, sizeof(value));
                return Word(result);
            });
        return result;
    }
};

// hash_map<string, list<void*>>::clear against the original: per node its list's nodes, the
// list's sentinel, the string, then the node, taken off their lists again before ours runs.
template <std::uintptr_t A>
struct StringListCheck {
    static inline void* original = nullptr;
    static void __fastcall Clear(StrList::Table* self) {
        using ListNode = stlp::list<void*>::Node;
        Trail trail;
        for (std::size_t b = 0; b < self->BucketCount(); ++b) {
            for (StrList::Node* n = self->BucketVector()[b]; n; n = n->next) {
                stlp::list<void*>& list = n->value.mapped;
                for (ListNode* e = list.First(); e != list.End(); e = e->next) trail.Add(e, sizeof(ListNode));
                trail.Add(list.head, sizeof(ListNode));
                trail.AddString(n->value.key);
                trail.Add(n, sizeof(*n));
            }
        }
        SL<A>::CheckTrail(
            Name<A>("stlp::hashtable::clear"), self, trail,
            [&] { reinterpret_cast<decltype(&StrList::Clear)>(original)(self); return std::uintptr_t{0}; },
            [&] { StrList::Clear(self); return std::uintptr_t{0}; });
    }
};

// --- S16a: the unit set's tables (world_unit_set_add.cpp) ---

using FiguredAbi = gamehash::FiguredAbi;
template <std::uintptr_t A> using HF = HashCheck<A, FiguredAbi>;
template <std::uintptr_t A> using ClearFiguredMap = ClearOurs<A, FiguredAbi>;

// A copy of `source`: the same bucket count, count and functor bytes, each chain the same keys in
// nodes of their own (the values are copied by the element policies, checked with their vectors).
template <typename Table>
bool SameKeys(const Table* self, const Table* source) {
    Table* mine = const_cast<Table*>(self);
    Table* from = const_cast<Table*>(source);
    bool ok = mine->BucketCount() == from->BucketCount() && mine->Count() == from->Count() &&
              std::memcmp(mine->Functors(), from->Functors(), 3) == 0;
    for (std::size_t b = 0; ok && b < mine->BucketCount(); ++b) {
        auto* m = mine->BucketVector()[b];
        auto* t = from->BucketVector()[b];
        for (; ok && m && t; m = m->next, t = t->next) ok = m != t && m->value.key == t->value.key;
        ok = ok && !m && !t;
    }
    return ok;
}

template <std::uintptr_t A>
struct UnitSetTables {
    using Figured = FiguredAbi::Table;
    using Groups = gamehash::FiguredGroupsTable;
    static inline void* original = nullptr;
    template <typename Fn> static Fn Original() { return reinterpret_cast<Fn>(original); }

    // hashtable(const hashtable&) and the group pair (ours only).
    static Figured* __fastcall CopyConstruct(Figured* self, void*, const Figured* source) {
        Figured* result = FiguredAbi::CopyConstruct(self, nullptr, source);
        bool ok = result == self && SameKeys(self, source);
        ei::verify::Report(Name<A>("stlp::hashtable::hashtable(const hashtable&)"), ok, ok ? "" : "not a copy of the source");
        return result;
    }
    static Groups::Value* __fastcall GroupPair(Groups::Value* self, void*, const std::uint32_t* key, const Figured* table) {
        Groups::Value* result = gamehash::ConstructGroupPair(self, nullptr, key, table);
        bool ok = result == self && self->key == *key && SameKeys(&self->mapped, table);
        ei::verify::Report(Name<A>("stlp::pair<const unsigned, hashtable>::pair"), ok, ok ? "" : "not the key and a copy of the table");
        return result;
    }
    // find_or_insert of a group: a lookup (no rehash, the key there) against the original; else
    // ours alone, the key found at the result and one more element.
    static Groups::Value* __fastcall FindOrInsert(Groups* self, void*, const Groups::Value* obj) {
        using Abi = gamehash::FiguredGroupsAbi;
        const char* name = Name<A>("stlp::hashtable::find_or_insert");
        if (HashCheck<A, Abi>::WouldRehash(self) || !self->Find(obj->key)) {
            std::size_t count = self->Count();
            Groups::Value* result = Abi::FindOrInsert(self, nullptr, obj);
            Groups::Node* found = self->Find(obj->key);
            bool ok = found && result == &found->value && self->Count() == count + 1;
            ei::verify::Report(name, ok, ok ? "" : "the new group is not the key's");
            return result;
        }
        Groups::Value* theirs = Original<decltype(&Abi::FindOrInsert)>()(self, nullptr, obj);
        Groups::Value* ours = Abi::FindOrInsert(self, nullptr, obj);
        ei::verify::Check(name, Word(theirs), Word(ours), "key %s", KeyText(obj->key).text);
        return ours;
    }
};

// insert_equal_noresize (ours only): one more element, the new node holding obj's key right after
// the first node of that key, else at the head of its bucket.
template <std::uintptr_t A, typename Abi>
struct InsertEqualCheck {
    using Table = typename Abi::Table;
    using Node = typename Abi::Node;
    static typename Abi::Iterator* __fastcall Run(Table* self, void*, typename Abi::Iterator* out, const typename Abi::Value* obj) {
        std::size_t count = self->Count();
        std::size_t b = self->BucketOf(obj->key);
        Node* head = self->BucketVector()[b];
        Node* equal = head;
        while (equal && equal->value.key != obj->key) equal = equal->next;
        Node* after = equal ? equal->next : head;
        typename Abi::Iterator* result = Abi::InsertEqualNoresize(self, nullptr, out, obj);
        Node* made = out->cur;
        bool ok = result == out && out->table == self && made && self->Count() == count + 1 && made->value.key == obj->key &&
                  made->next == after && (equal ? equal->next == made : self->BucketVector()[b] == made);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "key %s: node %p", KeyText(obj->key).text, made);
        ei::verify::Report(Name<A>("stlp::hashtable::insert_equal_noresize"), ok, detail);
        return result;
    }
};
template <std::uintptr_t A> using InsertFigured = InsertEqualCheck<A, FiguredAbi>;
template <std::uintptr_t A> using InsertDressed = InsertEqualCheck<A, Dressed>;

} // namespace

// Construction.
EI_VERIFY(0x00405820, H8<0x00405820>::Construct, 7, H8<0x00405820>::original);
EI_VERIFY(0x00405C40, H8<0x00405C40>::Construct, 7, H8<0x00405C40>::original);
EI_VERIFY(0x00438B70, H8<0x00438B70>::Construct, 7, H8<0x00438B70>::original);
EI_VERIFY(0x00439F20, H8<0x00439F20>::Construct, 7, H8<0x00439F20>::original);
EI_VERIFY(0x0053B4B0, H8<0x0053B4B0>::Construct, 7, H8<0x0053B4B0>::original);
EI_VERIFY(0x0053B650, H8<0x0053B650>::Construct, 7, H8<0x0053B650>::original);
EI_VERIFY(0x0053B800, H8<0x0053B800>::Construct, 7, H8<0x0053B800>::original);
EI_VERIFY(0x0053BA00, H8<0x0053BA00>::Construct, 7, H8<0x0053BA00>::original);
EI_VERIFY(0x00609C10, H8<0x00609C10>::Construct, 7, H8<0x00609C10>::original);
EI_VERIFY(0x006BBF90, H8<0x006BBF90>::Construct, 7, H8<0x006BBF90>::original);
EI_VERIFY(0x006BC2B0, H8<0x006BC2B0>::Construct, 7, H8<0x006BC2B0>::original);
EI_VERIFY(0x004639D0, H8<0x004639D0>::ConstructDefault, 7, H8<0x004639D0>::original);
EI_VERIFY(0x004DC830, H8<0x004DC830>::ConstructDefault, 7, H8<0x004DC830>::original);
EI_VERIFY(0x005725E0, H8<0x005725E0>::ConstructDefault, 7, H8<0x005725E0>::original);
EI_VERIFY(0x00581790, H8<0x00581790>::ConstructDefault, 7, H8<0x00581790>::original);
EI_VERIFY(0x005985E0, H8<0x005985E0>::ConstructDefault, 7, H8<0x005985E0>::original);
EI_VERIFY(0x00598790, H8<0x00598790>::ConstructDefault, 7, H8<0x00598790>::original);
EI_VERIFY(0x005BCCD0, H8<0x005BCCD0>::ConstructDefault, 7, H8<0x005BCCD0>::original);
EI_VERIFY(0x0066B320, H8<0x0066B320>::ConstructDefault, 7, H8<0x0066B320>::original);
EI_VERIFY(0x00571050, ConstructTableWithDword, 0, s_unused);
EI_VERIFY(0x00405EB0, H8<0x00405EB0>::InitializeBuckets, 5, H8<0x00405EB0>::original);
EI_VERIFY(0x00406300, H8<0x00406300>::InitializeBuckets, 5, H8<0x00406300>::original);
EI_VERIFY(0x0040AA80, H8<0x0040AA80>::InitializeBuckets, 5, H8<0x0040AA80>::original);

// resize.
EI_VERIFY(0x00406640, H8<0x00406640>::Resize, 6, H8<0x00406640>::original);
EI_VERIFY(0x00406D10, H8<0x00406D10>::Resize, 6, H8<0x00406D10>::original);
EI_VERIFY(0x00406EE0, H8<0x00406EE0>::Resize, 6, H8<0x00406EE0>::original);
EI_VERIFY(0x004070B0, H8<0x004070B0>::Resize, 6, H8<0x004070B0>::original);
EI_VERIFY(0x0040BAC0, H8<0x0040BAC0>::Resize, 6, H8<0x0040BAC0>::original);
EI_VERIFY(0x0042AA90, H8<0x0042AA90>::Resize, 6, H8<0x0042AA90>::original);
EI_VERIFY(0x0042B9C0, H8<0x0042B9C0>::Resize, 6, H8<0x0042B9C0>::original);

// Pure members.
EI_VERIFY(0x00405560, H8<0x00405560>::Find, 5, H8<0x00405560>::original);
EI_VERIFY(0x004057A0, H8<0x004057A0>::Find, 5, H8<0x004057A0>::original);
EI_VERIFY(0x00405AE0, H8<0x00405AE0>::Find, 5, H8<0x00405AE0>::original);
EI_VERIFY(0x00405B20, H8<0x00405B20>::Find, 5, H8<0x00405B20>::original);
EI_VERIFY(0x00405BB0, H8<0x00405BB0>::Find, 5, H8<0x00405BB0>::original);
EI_VERIFY(0x00405D70, H8<0x00405D70>::Find, 5, H8<0x00405D70>::original);
EI_VERIFY(0x0040B400, H8<0x0040B400>::Find, 5, H8<0x0040B400>::original);
EI_VERIFY(0x0042A810, H8<0x0042A810>::Find, 5, H8<0x0042A810>::original);
EI_VERIFY(0x0066B490, H8<0x0066B490>::Find, 5, H8<0x0066B490>::original);
EI_VERIFY(0x0055C5C0, H8<0x0055C5C0>::BucketOfKey, 7, H8<0x0055C5C0>::original);
// 00408A50 lower_bound<unsigned*> (S16): the original on the same range; MOV EAX,ECX; PUSH ESI;
// SUB EDX,EAX (5).
static const std::uint32_t*(__fastcall* s_lowerBound)(const std::uint32_t*, const std::uint32_t*, const std::uint32_t*, int);
static const std::uint32_t* __fastcall LowerBoundVerify(const std::uint32_t* first, const std::uint32_t* last,
                                                        const std::uint32_t* value, int tag) {
    const std::uint32_t* expected = s_lowerBound(first, last, value, tag);
    const std::uint32_t* actual = stlp::LowerBoundDwords(first, last, value, tag);
    ei::verify::Check("stlp::lower_bound<unsigned*>", static_cast<long long>(expected - first),
                      static_cast<long long>(actual - first), "value %u of %d", *value, static_cast<int>(last - first));
    return expected;
}
EI_VERIFY(0x00408A50, LowerBoundVerify, 5, s_lowerBound);
EI_VERIFY(0x0040AE00, H8<0x0040AE00>::NextSize, 5, H8<0x0040AE00>::original);
EI_VERIFY(0x00672EC0, H8<0x00672EC0>::NextSize, 5, H8<0x00672EC0>::original);
EI_VERIFY(0x00404C30, H8<0x00404C30>::Begin, 5, H8<0x00404C30>::original);
EI_VERIFY(0x00406280, H8<0x00406280>::Begin, 5, H8<0x00406280>::original);
EI_VERIFY(0x0040A970, H8<0x0040A970>::Begin, 5, H8<0x0040A970>::original);
EI_VERIFY(0x004614C0, H8<0x004614C0>::Begin, 5, H8<0x004614C0>::original);
EI_VERIFY(0x0055AB50, H8<0x0055AB50>::Begin, 5, H8<0x0055AB50>::original);
EI_VERIFY(0x0055ADF0, H8<0x0055ADF0>::Begin, 5, H8<0x0055ADF0>::original);
EI_VERIFY(0x0055AE50, H8<0x0055AE50>::Begin, 5, H8<0x0055AE50>::original);
EI_VERIFY(0x0069C160, H8<0x0069C160>::Begin, 5, H8<0x0069C160>::original);
EI_VERIFY(0x006BC3E0, H8<0x006BC3E0>::Begin, 5, H8<0x006BC3E0>::original);
EI_VERIFY(0x004073C0, H8<0x004073C0>::SkipToNext, 6, H8<0x004073C0>::original);
EI_VERIFY(0x0055B500, H8<0x0055B500>::SkipToNext, 6, H8<0x0055B500>::original);
EI_VERIFY(0x0055B9D0, H8<0x0055B9D0>::SkipToNext, 6, H8<0x0055B9D0>::original);
EI_VERIFY(0x006BCFB0, H8<0x006BCFB0>::SkipToNext, 6, H8<0x006BCFB0>::original);
EI_VERIFY(0x00404980, H8<0x00404980>::Increment, 6, H8<0x00404980>::original);
EI_VERIFY(0x0040A700, H8<0x0040A700>::PostIncrement, 5, H8<0x0040A700>::original);
EI_VERIFY(0x0040AD20, H8<0x0040AD20>::ConstructIterator, 6, H8<0x0040AD20>::original);
EI_VERIFY(0x005BE450, H8<0x005BE450>::ConstructIterator, 6, H8<0x005BE450>::original);
EI_VERIFY(0x006B0B10, H8<0x006B0B10>::ConstructIterator, 6, H8<0x006B0B10>::original);
EI_VERIFY(0x006B0B30, H8<0x006B0B30>::ConstructIterator, 6, H8<0x006B0B30>::original);

// Inserting.
EI_VERIFY(0x004055A0, H8<0x004055A0>::Subscript, 7, H8<0x004055A0>::original);
EI_VERIFY(0x004056A0, H8<0x004056A0>::Subscript, 7, H8<0x004056A0>::original);
EI_VERIFY(0x004075C0, H8<0x004075C0>::Subscript, 7, H8<0x004075C0>::original);
EI_VERIFY(0x00407680, H8<0x00407680>::Subscript, 7, H8<0x00407680>::original);
EI_VERIFY(0x0040A800, H8<0x0040A800>::Subscript, 7, H8<0x0040A800>::original);
EI_VERIFY(0x00439EA0, H8<0x00439EA0>::Subscript, 7, H8<0x00439EA0>::original);
EI_VERIFY(0x004065D0, H8<0x004065D0>::FindOrInsert, 6, H8<0x004065D0>::original);
EI_VERIFY(0x004085E0, H8<0x004085E0>::FindOrInsert, 6, H8<0x004085E0>::original);
EI_VERIFY(0x0040B2C0, H8<0x0040B2C0>::FindOrInsert, 6, H8<0x0040B2C0>::original);
EI_VERIFY(0x006BCFF0, H8<0x006BCFF0>::FindOrInsert, 6, H8<0x006BCFF0>::original);
EI_VERIFY(0x004E2E90, H28<0x004E2E90>::FindOrInsert, 6, H28<0x004E2E90>::original);
EI_VERIFY(0x00407420, H8<0x00407420>::NewNode, 6, H8<0x00407420>::original);
EI_VERIFY(0x004074F0, H8<0x004074F0>::NewNode, 6, H8<0x004074F0>::original);
EI_VERIFY(0x00408B20, H8<0x00408B20>::NewNode, 6, H8<0x00408B20>::original);
EI_VERIFY(0x0040B440, H8<0x0040B440>::NewNode, 6, H8<0x0040B440>::original);
EI_VERIFY(0x0042AA00, H8<0x0042AA00>::NewNode, 6, H8<0x0042AA00>::original);
EI_VERIFY(0x0042B8A0, H8<0x0042B8A0>::NewNode, 6, H8<0x0042B8A0>::original);

// Freeing.
EI_VERIFY(0x00405620, H8<0x00405620>::EraseIterator, 5, H8<0x00405620>::original);
EI_VERIFY(0x00405720, H8<0x00405720>::EraseIterator, 5, H8<0x00405720>::original);
EI_VERIFY(0x005BCE60, H8<0x005BCE60>::EraseIterator, 5, H8<0x005BCE60>::original);
EI_VERIFY(0x004067B0, H8<0x004067B0>::Clear, 5, H8<0x004067B0>::original);
EI_VERIFY(0x00406E80, H8<0x00406E80>::Clear, 5, H8<0x00406E80>::original);
EI_VERIFY(0x00407050, H8<0x00407050>::Clear, 5, H8<0x00407050>::original);
EI_VERIFY(0x00407220, H8<0x00407220>::Clear, 5, H8<0x00407220>::original);
EI_VERIFY(0x0040B330, H8<0x0040B330>::Clear, 5, H8<0x0040B330>::original);
EI_VERIFY(0x0043B4D0, H8<0x0043B4D0>::Clear, 5, H8<0x0043B4D0>::original);
EI_VERIFY(0x0043D590, H8<0x0043D590>::Clear, 5, H8<0x0043D590>::original);
EI_VERIFY(0x0059D6D0, H8<0x0059D6D0>::Clear, 5, H8<0x0059D6D0>::original);
EI_VERIFY(0x0059D7A0, H8<0x0059D7A0>::Clear, 5, H8<0x0059D7A0>::original);
EI_VERIFY(0x006BD100, H8<0x006BD100>::Clear, 5, H8<0x006BD100>::original);
EI_VERIFY(0x004074B0, H8<0x004074B0>::DeleteNode, 6, H8<0x004074B0>::original);
EI_VERIFY(0x00407580, H8<0x00407580>::DeleteNode, 6, H8<0x00407580>::original);
EI_VERIFY(0x00407640, H8<0x00407640>::DeleteNode, 6, H8<0x00407640>::original);
EI_VERIFY(0x00407700, H8<0x00407700>::DeleteNode, 6, H8<0x00407700>::original);
EI_VERIFY(0x004A8B70, H8<0x004A8B70>::Destroy, 0, s_unused);
EI_VERIFY(0x005984A0, H8<0x005984A0>::Destroy, 0, s_unused);
EI_VERIFY(0x004DB8A0, H4<0x004DB8A0>::Destroy, 0, s_unused);
EI_VERIFY(0x00668B70, H4<0x00668B70>::Destroy, 0, s_unused);
EI_VERIFY(0x0066B400, H4<0x0066B400>::InsertUnique, 5, H4<0x0066B400>::original);
EI_VERIFY(0x0066B900, H4<0x0066B900>::Resize, 6, H4<0x0066B900>::original);
EI_VERIFY(0x0066BAD0, H4<0x0066BAD0>::NewNode, 6, H4<0x0066BAD0>::original);
EI_VERIFY(0x0066BA70, H4<0x0066BA70>::Clear, 5, H4<0x0066BA70>::original);

// String keys.
EI_VERIFY(0x004298D0, HashFunctionCheck<0x004298D0>::String, 5, HashFunctionCheck<0x004298D0>::original);
EI_VERIFY(0x00460BA0, HashFunctionCheck<0x00460BA0>::Text, 6, HashFunctionCheck<0x00460BA0>::original);
EI_VERIFY(0x004FB0F0, S4<0x004FB0F0>::Find, 5, S4<0x004FB0F0>::original);
EI_VERIFY(0x005AD520, S4<0x005AD520>::Find, 5, S4<0x005AD520>::original);
EI_VERIFY(0x006B07C0, S4<0x006B07C0>::Find, 5, S4<0x006B07C0>::original);
EI_VERIFY(0x006B0870, S4<0x006B0870>::Find, 5, S4<0x006B0870>::original);
EI_VERIFY(0x006B0930, S4<0x006B0930>::Find, 5, S4<0x006B0930>::original);
EI_VERIFY(0x006BC0C0, S4<0x006BC0C0>::Find, 5, S4<0x006BC0C0>::original);
EI_VERIFY(0x0069D0E0, S4<0x0069D0E0>::SkipToNext, 5, S4<0x0069D0E0>::original);
EI_VERIFY(0x006BCCD0, S4<0x006BCCD0>::EraseKeyStrings, 5, S4<0x006BCCD0>::original);
// erase(key) of dword keys (S12e): PUSH EBX; MOV EBX,[ECX+4] (4 bytes) + PUSH EBP (5).
EI_VERIFY(0x004E2F00, H28<0x004E2F00>::EraseKeyPlain, 5, H28<0x004E2F00>::original);
EI_VERIFY(0x006BD060, H8<0x006BD060>::EraseKeyPlain, 5, H8<0x006BD060>::original);
EI_VERIFY(0x004297A0, S4<0x004297A0>::SubscriptKey, 7, S4<0x004297A0>::original);
EI_VERIFY(0x004ABAA0, S4<0x004ABAA0>::SubscriptKey, 7, S4<0x004ABAA0>::original);
EI_VERIFY(0x004ABBD0, S4<0x004ABBD0>::SubscriptKey, 7, S4<0x004ABBD0>::original);
EI_VERIFY(0x004ABD00, S4<0x004ABD00>::SubscriptKey, 7, S4<0x004ABD00>::original);
EI_VERIFY(0x0053C650, S4<0x0053C650>::SubscriptKey, 7, S4<0x0053C650>::original);
EI_VERIFY(0x0053C780, S4<0x0053C780>::SubscriptKey, 7, S4<0x0053C780>::original);
EI_VERIFY(0x0053C8B0, S4<0x0053C8B0>::SubscriptKey, 7, S4<0x0053C8B0>::original);
EI_VERIFY(0x0060AC20, S4<0x0060AC20>::SubscriptKey, 7, S4<0x0060AC20>::original);
EI_VERIFY(0x00619FB0, S4<0x00619FB0>::SubscriptKey, 7, S4<0x00619FB0>::original);
EI_VERIFY(0x00684B20, S4<0x00684B20>::SubscriptKey, 7, S4<0x00684B20>::original);
EI_VERIFY(0x006A75E0, S4<0x006A75E0>::SubscriptKey, 7, S4<0x006A75E0>::original);
EI_VERIFY(0x00464750, S4<0x00464750>::SubscriptKey, 7, S4<0x00464750>::original);
EI_VERIFY(0x005335E0, S4<0x005335E0>::SubscriptKey, 7, S4<0x005335E0>::original);
EI_VERIFY(0x006BBDF0, S4<0x006BBDF0>::SubscriptKey, 7, S4<0x006BBDF0>::original);
EI_VERIFY(0x0064EE90, S4<0x0064EE90>::SubscriptKey, 7, S4<0x0064EE90>::original);
EI_VERIFY(0x006AD670, S4<0x006AD670>::SubscriptKey, 7, S4<0x006AD670>::original);
EI_VERIFY(0x004AB720, RecordNamedCheck<0x004AB720>::Run, 7, RecordNamedCheck<0x004AB720>::original);
EI_VERIFY(0x004AB820, RecordNamedCheck<0x004AB820>::Run, 7, RecordNamedCheck<0x004AB820>::original);
EI_VERIFY(0x0052CA40, RecordNamedCheck<0x0052CA40>::Run, 7, RecordNamedCheck<0x0052CA40>::original);
EI_VERIFY(0x0052CB40, RecordNamedCheck<0x0052CB40>::Run, 7, RecordNamedCheck<0x0052CB40>::original);
EI_VERIFY(0x0069D290, RecordNamedCheck<0x0069D290>::Run, 7, RecordNamedCheck<0x0069D290>::original);
EI_VERIFY(0x0069D380, RecordNamedCheck<0x0069D380>::Run, 7, RecordNamedCheck<0x0069D380>::original);
EI_VERIFY(0x006AD580, RecordNamedCheck<0x006AD580>::Run, 7, RecordNamedCheck<0x006AD580>::original);
EI_VERIFY(0x00613820, RecordNamedCheck<0x00613820>::Run, 7, RecordNamedCheck<0x00613820>::original);
EI_VERIFY(0x004ACC10, S4<0x004ACC10>::FindOrInsert, 6, S4<0x004ACC10>::original);
EI_VERIFY(0x004CE110, S4<0x004CE110>::FindOrInsert, 6, S4<0x004CE110>::original);
EI_VERIFY(0x004FBBD0, S4<0x004FBBD0>::FindOrInsert, 6, S4<0x004FBBD0>::original);
EI_VERIFY(0x00514880, S4<0x00514880>::FindOrInsert, 6, S4<0x00514880>::original);
EI_VERIFY(0x0053D340, S4<0x0053D340>::FindOrInsert, 6, S4<0x0053D340>::original);
EI_VERIFY(0x005ADC10, S4<0x005ADC10>::FindOrInsert, 6, S4<0x005ADC10>::original);
EI_VERIFY(0x00613B70, S4<0x00613B70>::FindOrInsert, 6, S4<0x00613B70>::original);
EI_VERIFY(0x0069D1E0, S4<0x0069D1E0>::FindOrInsert, 6, S4<0x0069D1E0>::original);
EI_VERIFY(0x0069E2A0, S4<0x0069E2A0>::FindOrInsert, 6, S4<0x0069E2A0>::original);
EI_VERIFY(0x006A7870, S4<0x006A7870>::FindOrInsert, 6, S4<0x006A7870>::original);
EI_VERIFY(0x006BCC20, S4<0x006BCC20>::FindOrInsert, 6, S4<0x006BCC20>::original);
EI_VERIFY(0x004ACAB0, S4<0x004ACAB0>::FindOrInsert, 6, S4<0x004ACAB0>::original);
EI_VERIFY(0x004ACB60, S4<0x004ACB60>::FindOrInsert, 6, S4<0x004ACB60>::original);
EI_VERIFY(0x0052CF60, S4<0x0052CF60>::FindOrInsert, 6, S4<0x0052CF60>::original);
EI_VERIFY(0x0069E1F0, S4<0x0069E1F0>::FindOrInsert, 6, S4<0x0069E1F0>::original);
EI_VERIFY(0x0042AFD0, S4<0x0042AFD0>::Resize, 7, S4<0x0042AFD0>::original);
EI_VERIFY(0x004AEA90, S4<0x004AEA90>::Resize, 7, S4<0x004AEA90>::original);
EI_VERIFY(0x004AEC40, S4<0x004AEC40>::Resize, 7, S4<0x004AEC40>::original);
EI_VERIFY(0x004AEDF0, S4<0x004AEDF0>::Resize, 7, S4<0x004AEDF0>::original);
EI_VERIFY(0x004AEFA0, S4<0x004AEFA0>::Resize, 7, S4<0x004AEFA0>::original);
EI_VERIFY(0x004AF150, S4<0x004AF150>::Resize, 7, S4<0x004AF150>::original);
EI_VERIFY(0x004AF300, S4<0x004AF300>::Resize, 7, S4<0x004AF300>::original);
EI_VERIFY(0x00464F30, S4<0x00464F30>::Resize, 7, S4<0x00464F30>::original);
EI_VERIFY(0x00514F70, S4<0x00514F70>::Resize, 7, S4<0x00514F70>::original);
EI_VERIFY(0x0053DDF0, S4<0x0053DDF0>::Resize, 7, S4<0x0053DDF0>::original);
EI_VERIFY(0x0053E050, S4<0x0053E050>::Resize, 7, S4<0x0053E050>::original);
EI_VERIFY(0x005AE430, S4<0x005AE430>::Resize, 7, S4<0x005AE430>::original);
EI_VERIFY(0x0060B740, S4<0x0060B740>::Resize, 7, S4<0x0060B740>::original);
EI_VERIFY(0x0061A650, S4<0x0061A650>::Resize, 7, S4<0x0061A650>::original);
EI_VERIFY(0x0069E090, S4<0x0069E090>::Resize, 7, S4<0x0069E090>::original);
EI_VERIFY(0x0069EB70, S4<0x0069EB70>::Resize, 7, S4<0x0069EB70>::original);
EI_VERIFY(0x006BD840, S4<0x006BD840>::Resize, 7, S4<0x006BD840>::original);
EI_VERIFY(0x004CEF00, S4<0x004CEF00>::Resize, 5, S4<0x004CEF00>::original);
EI_VERIFY(0x00514D50, S4<0x00514D50>::Resize, 5, S4<0x00514D50>::original);
EI_VERIFY(0x004FC320, S4<0x004FC320>::Resize, 5, S4<0x004FC320>::original);
EI_VERIFY(0x0042A650, S4<0x0042A650>::NewNode, 5, S4<0x0042A650>::original);
EI_VERIFY(0x004ADBE0, S4<0x004ADBE0>::NewNode, 5, S4<0x004ADBE0>::original);
EI_VERIFY(0x004ADD00, S4<0x004ADD00>::NewNode, 5, S4<0x004ADD00>::original);
EI_VERIFY(0x004ADE20, S4<0x004ADE20>::NewNode, 5, S4<0x004ADE20>::original);
EI_VERIFY(0x004ADF40, S4<0x004ADF40>::NewNode, 5, S4<0x004ADF40>::original);
EI_VERIFY(0x004AE060, S4<0x004AE060>::NewNode, 5, S4<0x004AE060>::original);
EI_VERIFY(0x004AE180, S4<0x004AE180>::NewNode, 5, S4<0x004AE180>::original);
EI_VERIFY(0x00464E10, S4<0x00464E10>::NewNode, 5, S4<0x00464E10>::original);
EI_VERIFY(0x00514C10, S4<0x00514C10>::NewNode, 5, S4<0x00514C10>::original);
EI_VERIFY(0x0053D9D0, S4<0x0053D9D0>::NewNode, 5, S4<0x0053D9D0>::original);
EI_VERIFY(0x0053DAF0, S4<0x0053DAF0>::NewNode, 5, S4<0x0053DAF0>::original);
EI_VERIFY(0x0060B600, S4<0x0060B600>::NewNode, 5, S4<0x0060B600>::original);
EI_VERIFY(0x0061A380, S4<0x0061A380>::NewNode, 5, S4<0x0061A380>::original);
EI_VERIFY(0x004CE460, S4<0x004CE460>::NewNode, 5, S4<0x004CE460>::original);
EI_VERIFY(0x004FBF60, S4<0x004FBF60>::NewNode, 5, S4<0x004FBF60>::original);
EI_VERIFY(0x00514AF0, S4<0x00514AF0>::NewNode, 5, S4<0x00514AF0>::original);
EI_VERIFY(0x005ADF10, S4<0x005ADF10>::NewNode, 5, S4<0x005ADF10>::original);
EI_VERIFY(0x0069D880, S4<0x0069D880>::NewNode, 5, S4<0x0069D880>::original);
EI_VERIFY(0x0069E5B0, S4<0x0069E5B0>::NewNode, 5, S4<0x0069E5B0>::original);
EI_VERIFY(0x006BD240, S4<0x006BD240>::NewNode, 5, S4<0x006BD240>::original);
EI_VERIFY(0x004AB2B0, S4<0x004AB2B0>::ClearStrings, 6, S4<0x004AB2B0>::original);
EI_VERIFY(0x0053C0D0, S4<0x0053C0D0>::ClearStrings, 6, S4<0x0053C0D0>::original);
EI_VERIFY(0x0053C280, S4<0x0053C280>::ClearStrings, 6, S4<0x0053C280>::original);
EI_VERIFY(0x0053C430, S4<0x0053C430>::ClearStrings, 6, S4<0x0053C430>::original);
EI_VERIFY(0x0053C5E0, S4<0x0053C5E0>::ClearStrings, 6, S4<0x0053C5E0>::original);
EI_VERIFY(0x006A6D00, S4<0x006A6D00>::ClearStrings, 6, S4<0x006A6D00>::original);
EI_VERIFY(0x006A6D70, S4<0x006A6D70>::ClearStrings, 6, S4<0x006A6D70>::original);
EI_VERIFY(0x006A6DE0, S4<0x006A6DE0>::ClearStrings, 6, S4<0x006A6DE0>::original);
EI_VERIFY(0x006A70A0, S4<0x006A70A0>::ClearStrings, 6, S4<0x006A70A0>::original);
EI_VERIFY(0x006A7340, S4<0x006A7340>::ClearStrings, 6, S4<0x006A7340>::original);
EI_VERIFY(0x006B0520, S4<0x006B0520>::ClearStrings, 6, S4<0x006B0520>::original);
EI_VERIFY(0x006B0590, S4<0x006B0590>::ClearStrings, 6, S4<0x006B0590>::original);
EI_VERIFY(0x006BCE00, S4<0x006BCE00>::ClearStrings, 6, S4<0x006BCE00>::original);

// S11a: hash_map<CString, float>.
EI_VERIFY(0x004613C0, CStringFloatCheck<0x004613C0>::Subscript, 7, CStringFloatCheck<0x004613C0>::original);
EI_VERIFY(0x00461540, CStringFloatCheck<0x00461540>::Clear, 6, CStringFloatCheck<0x00461540>::original);
EI_VERIFY(0x004615B0, CF<0x004615B0>::NewNode, 5, CF<0x004615B0>::original);
EI_VERIFY(0x004616A0, CStringFloatCheck<0x004616A0>::DeleteNode, 5, CStringFloatCheck<0x004616A0>::original);
EI_VERIFY(0x00461750, CF<0x00461750>::Resize, 7, CF<0x00461750>::original);
EI_VERIFY(0x004A8A50, CStringFloatCheck<0x004A8A50>::CopyConstruct, 0, s_unused);
EI_VERIFY(0x00580900, CStringFloatCheck<0x00580900>::CopyConstruct, 0, s_unused);
EI_VERIFY(0x004A8AD0, CStringFloatCheck<0x004A8AD0>::Assign, 0, s_unused);
EI_VERIFY(0x004AA950, CStringFloatCheck<0x004AA950>::CopyFrom, 0, s_unused);
EI_VERIFY(0x005E0DB0, CF<0x005E0DB0>::SkipToNext, 6, CF<0x005E0DB0>::original);

// S11a: string keys: the destructors of 4-byte values (ours only); hash_map<string, list<void*>>.
EI_VERIFY(0x004A9010, ClearStr4<0x004A9010>::Destroy, 0, s_unused);
EI_VERIFY(0x006A6C20, ClearStr4<0x006A6C20>::Destroy, 0, s_unused);
EI_VERIFY(0x004A8E60, ClearStrList<0x004A8E60>::Destroy, 0, s_unused);
EI_VERIFY(0x004AAEB0, StringListCheck<0x004AAEB0>::Clear, 6, StringListCheck<0x004AAEB0>::original);
EI_VERIFY(0x0069D130, SL<0x0069D130>::FindOrInsert, 6, SL<0x0069D130>::original);

// S11a: game values (ours only), the shape cache, the bridge heights.
EI_VERIFY(0x004A91B0, PartyCheck<0x004A91B0>::At, 0, s_unused);
EI_VERIFY(0x004ABA70, PartyCheck<0x004ABA70>::DeleteNode, 0, s_unused);
EI_VERIFY(0x004AD050, ClearParties<0x004AD050>::Clear, 0, s_unused);
EI_VERIFY(0x005986B0, ClearFigured<0x005986B0>::Destroy, 0, s_unused);
EI_VERIFY(0x00598860, ClearDressed<0x00598860>::Destroy, 0, s_unused);
EI_VERIFY(0x00663330, ClearCStrFloat<0x00663330>::Destroy, 0, s_unused);   // S12e: the party records' variables
EI_VERIFY(0x005BCCB0, ShapeCheck<0x005BCCB0>::InitHandle, 9, ShapeCheck<0x005BCCB0>::original);
EI_VERIFY(0x005BE090, ShapeCheck<0x005BE090>::Slot, 7, ShapeCheck<0x005BE090>::original);
EI_VERIFY(0x005BE320, SH<0x005BE320>::Find, 5, SH<0x005BE320>::original);
EI_VERIFY(0x005BE730, ShapeCheck<0x005BE730>::FindOrInsert, 5, ShapeCheck<0x005BE730>::original);
EI_VERIFY(0x005BCDE0, BridgeCheck<0x005BCDE0>::Subscript, 7, BridgeCheck<0x005BCDE0>::original);

// S16a: the unit set's tables (world_unit_set_add.cpp).
EI_VERIFY(0x0055AC40, HF<0x0055AC40>::ConstructDefault, 7, HF<0x0055AC40>::original);
EI_VERIFY(0x0055AEB0, UnitSetTables<0x0055AEB0>::CopyConstruct, 0, s_unused);
EI_VERIFY(0x0055ABB0, UnitSetTables<0x0055ABB0>::GroupPair, 0, s_unused);
EI_VERIFY(0x0055AD10, ClearFiguredMap<0x0055AD10>::Destroy, 0, s_unused);
EI_VERIFY(0x0055BA10, UnitSetTables<0x0055BA10>::FindOrInsert, 5, UnitSetTables<0x0055BA10>::original);
EI_VERIFY(0x0055CEB0, H8<0x0055CEB0>::Resize, 6, H8<0x0055CEB0>::original);
EI_VERIFY(0x0055B670, H8<0x0055B670>::Resize, 6, H8<0x0055B670>::original);
EI_VERIFY(0x0055BBF0, H8<0x0055BBF0>::Resize, 6, H8<0x0055BBF0>::original);
EI_VERIFY(0x0055B5C0, H8<0x0055B5C0>::EqualRange, 5, H8<0x0055B5C0>::original);
EI_VERIFY(0x0055BB40, H8<0x0055BB40>::EqualRange, 5, H8<0x0055BB40>::original);
EI_VERIFY(0x0055BA80, H8<0x0055BA80>::SkipToNext, 6, H8<0x0055BA80>::original);
EI_VERIFY(0x0055B540, InsertFigured<0x0055B540>::Run, 0, s_unused);
EI_VERIFY(0x0055BAC0, InsertDressed<0x0055BAC0>::Run, 0, s_unused);
