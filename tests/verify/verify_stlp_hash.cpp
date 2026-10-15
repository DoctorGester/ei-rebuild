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
    std::vector<Node*> buckets;
    std::vector<Node*> nodes;
    std::vector<unsigned char> bytes;

    static Snapshot Of(Table* t) {
        Snapshot s;
        std::memcpy(s.functors, t->Functors(), 4);
        s.start = t->BucketVector().Start();
        s.finish = t->BucketVector().Finish();
        s.eos = t->BucketVector().EndOfStorage();
        s.count = t->Count();
        s.buckets.assign(s.start, s.finish);
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
        std::memcpy(start, buckets.data(), buckets.size() * sizeof(Node*));
        for (std::size_t i = 0; i < nodes.size(); ++i) std::memcpy(nodes[i], &bytes[i * sizeof(Node)], sizeof(Node));
    }
    bool operator==(const Snapshot& o) const {
        return std::memcmp(functors, o.functors, 3) == 0 && start == o.start && finish == o.finish && eos == o.eos &&
               count == o.count && buckets == o.buckets && nodes == o.nodes && bytes == o.bytes;
    }
};

// The first `n` blocks of a free list, from its top.
std::vector<void*> TopOf(void* const* list, std::size_t n) {
    std::vector<void*> out;
    for (void* p = *list; p && out.size() < n; p = *static_cast<void**>(p)) out.push_back(p);
    return out;
}

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
        if (!ok) std::snprintf(detail, sizeof(detail), "key %08X: ours %p, original's %p", static_cast<unsigned>(*key), out->cur, theirs.cur);
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
        ei::verify::Check(Name<A>("stlp::hashtable::_M_bkt_num_key"), theirs, ours, "key %08X", static_cast<unsigned>(*key));
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
    template <typename Theirs, typename Ours>
    static void CheckFrees(const char* name, Table* table, Node* node, Theirs theirs, Ours ours) {
        void** list = FreeList(sizeof(Node));
        void* top = *list;
        Snap before = table ? Snap::Of(table) : Snap{};
        Node nodeBytes{};
        if (node) std::memcpy(&nodeBytes, node, sizeof(Node));
        std::size_t most = (table ? before.count : 0) + 1;
        theirs();
        Snap after = table ? Snap::Of(table) : Snap{};
        std::vector<void*> freed;
        for (void* p = *list; p != top && freed.size() <= most; p = *static_cast<void**>(p)) freed.push_back(p);
        if (freed.size() > most) {
            ei::verify::Report(name, false, "the original's free list does not lead back to its old top");
            return;   // the original's result stays
        }
        *list = top;
        if (table) before.Restore(table);
        if (node) std::memcpy(node, &nodeBytes, sizeof(Node));
        ours();
        Snap mine = table ? Snap::Of(table) : Snap{};
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
            [&] { Abi::EraseIterator(self, nullptr, cur, table); });
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

    // `theirs` and then `ours` on the same table, both returning an address inside the node of `key`.
    template <typename Theirs, typename Ours>
    static void CheckInsert(const char* name, Table* self, Key key, Theirs theirs, Ours ours) {
        char detail[128] = "";
        if (WouldRehash(self)) {
            std::uintptr_t result = ours();
            Node* found = self->Find(key);
            bool ok = found && result >= Word(found) && result < Word(found) + sizeof(Node);
            if (!ok) std::snprintf(detail, sizeof(detail), "key %08X: result %08X, node %p", static_cast<unsigned>(key), static_cast<unsigned>(result), found);
            ei::verify::Report(name, ok, detail);
            return;
        }
        Snap before = Snap::Of(self);
        std::uintptr_t theirResult = theirs();
        Snap after = Snap::Of(self);
        if (after.count == before.count + 1) {
            std::size_t b = self->BucketOf(key);
            Node* made = self->BucketVector()[b];
            if (!made || made->next != before.buckets[b]) {
                ei::verify::Report(name, false, "the original's node is not at the head of its bucket");
                return;   // the original's result stays
            }
            self->BucketVector()[b] = made->next;
            self->CountRef() = before.count;
            stlp::deallocate(made, sizeof(Node));
        }
        std::uintptr_t ourResult = ours();
        Snap mine = Snap::Of(self);
        bool ok = ourResult == theirResult && mine == after;
        if (!ok)
            std::snprintf(detail, sizeof(detail), "key %08X: ours %08X count %u, original's %08X count %u", static_cast<unsigned>(key),
                          static_cast<unsigned>(ourResult), static_cast<unsigned>(mine.count), static_cast<unsigned>(theirResult),
                          static_cast<unsigned>(after.count));
        ei::verify::Report(name, ok, detail);
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
        Node* theirs = Original<decltype(&Abi::NewNode)>()(self, nullptr, obj);
        Node theirBytes;
        std::memcpy(&theirBytes, theirs, sizeof(Node));
        stlp::deallocate(theirs, sizeof(Node));
        Node* ours = Abi::NewNode(self, nullptr, obj);
        bool ok = ours == theirs && std::memcmp(ours, &theirBytes, sizeof(Node)) == 0;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p", ours, theirs);
        ei::verify::Report(Name<A>("stlp::hashtable::_M_new_node"), ok, detail);
        return ours;
    }

    // --- resize -------------------------------------------------------------------------------

    static void __fastcall Resize(Table* self, void*, std::size_t hint) {
        const char* name = Name<A>("stlp::hashtable::resize");
        Snap before = Snap::Of(self);
        if (hint <= self->BucketCount() || stlp::NextPrime(hint) <= self->BucketCount()) {
            Original<decltype(&Abi::Resize)>()(self, nullptr, hint);
            bool same = Snap::Of(self) == before;
            Abi::Resize(self, nullptr, hint);
            ei::verify::Report(name, same && Snap::Of(self) == before, "a resize below the bucket count changed the table");
            return;
        }
        // The original on a copy: its nodes in our heap, its bucket storage from the game's allocator.
        std::size_t buckets = before.buckets.size();
        std::size_t capacity = static_cast<std::size_t>(before.eos - before.start);
        std::vector<Node> clones(before.nodes.size());
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
                ok = before.nodes[static_cast<std::size_t>(theirs - clones.data())] == mine;
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

// 00571050: hash_map() with the dword after it; ours only (the extra dword zeroed, an empty table).
stlp::TableWithDword* __fastcall ConstructTableWithDword(stlp::TableWithDword* self) {
    stlp::TableWithDword* result = stlp::ConstructTableWithDword(self);
    bool nulls = true;
    for (std::size_t b = 0; b < self->table.BucketCount(); ++b) nulls = nulls && self->table.BucketVector()[b] == nullptr;
    bool ok = result == self && self->extra == 0 && self->table.Count() == 0 && self->table.BucketCount() == 193 && nulls;
    ei::verify::Report("stlp::hash_map::hash_map 00571050", ok, ok ? "" : "not an empty table of 193 buckets");
    return result;
}

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
