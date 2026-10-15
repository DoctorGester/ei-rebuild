// Checks for S12c's registrations (docs/notes/standalone.md, S12c): the lists, hashtables, vectors and
// unwind helpers of S12a's ranges (src/lib/stlp_list.cpp, stlp_hash.cpp, stlp_vector.cpp,
// stlp_pair.cpp, src/game/library_containers.cpp).
//
// - Pure members (begin / end, the iterators, get_allocator, the vector base constructor,
//   uninitialized_copy) compare with the original.
// - The list constructor, plain insert and _M_create_node run the original first, give its node back
//   to the pool (LIFO) and run ours, which must take the same node and hold the same bytes; frees with
//   a trampoline run the original first, take the block off its free list again and run ours.
//   _M_fill_insert with room runs the original on a twin over a copy of the storage in our memory.
// - The rest run ours only (they destroy, release or copy CStrings and counted pointers): the blocks
//   freed on top of their free lists in order, the copies holding what they were copied from.
// A start-up check drives every address once with scratch objects (most are reached only from
// unwind funclets or from callers we rewrote).
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/library_containers.h"
#include "../../src/game/link_figure.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc_string.h"
#include "../../src/lib/stlp_hash.h"
#include "../../src/lib/stlp_pair.h"
#include "../../src/lib/stlp_tree_abi.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::LinkedBetween;
using stlp_verify::Name;
using stlp::AnyList;

using List16 = stlp::ListAbi<stlp::Opaque<8>>;
using List24 = stlp::ListAbi<stlp::Opaque<16>>;
using StringDwordList = stlp::ListAbi<stlp::StringDwordPair, stlp::StringDwordElement>;
using CStringList = stlp::ListAbi<mfc::CString>;
using Map8 = stlp::HashtableAbi<stlp::DwordKeyTraits<8>>;
using Str4 = stlp::HashtableAbi<stlp::StringKeyTraits<4>>;
using Vec1 = stlp::VectorAbi<char>;
using Vec4 = stlp::VectorAbi<std::uint32_t>;
using Vec8 = stlp::VectorAbi<stlp::Opaque<8>>;
using Vec12 = stlp::VectorAbi<stlp::Opaque<12>>;
using Vec24 = stlp::VectorAbi<stlp::Opaque<24>>;
using Vec32 = stlp::VectorAbi<stlp::Opaque<32>>;
using Vec48 = stlp::VectorAbi<stlp::Opaque<48>>;
using libcontainers::KeyedTextsNodes;
using libcontainers::ShownItemNodes;
using libcontainers::StreamQueue;

// The first `n` blocks of the free list for `bytes`, from its top.
std::vector<void*> TopOf(std::size_t bytes, std::size_t n) {
    std::vector<void*> out;
    void** list = FreeList(bytes);
    for (void* p = list ? *list : nullptr; p && out.size() < n; p = *static_cast<void**>(p)) out.push_back(p);
    return out;
}

// `freed` (in the order freed) on top of the free list for `bytes`, the last one first.
bool OnTopInOrder(std::size_t bytes, const std::vector<void*>& freed) {
    if (!FreeList(bytes)) return true;
    std::vector<void*> expected(freed.rbegin(), freed.rend());
    return TopOf(bytes, expected.size()) == expected;
}

// A CString's reference count (CStringData::refs, three words before the text).
int RefsAt(const char* text) { return reinterpret_cast<const mfc::impl::CStringData*>(text)[-1].refs; }
int RefsOf(const mfc::CString& s) { return RefsAt(s); }

// ---- Lists ----------------------------------------------------------------------------------------

using AnyNode = AnyList::Node;

template <std::uintptr_t A>
struct ListPure {
    static inline void* original = nullptr;

    static AnyNode** __fastcall Begin(const AnyList* self, void*, AnyNode** out) {
        AnyNode* theirs = nullptr;
        reinterpret_cast<decltype(&stlp::ListBegin)>(original)(self, nullptr, &theirs);
        stlp::ListBegin(self, nullptr, out);
        ei::verify::Check(Name<A>("stlp::list::begin"), theirs, *out, "list %p", self);
        return out;
    }
    static AnyNode** __fastcall End(const AnyList* self, void*, AnyNode** out) {
        AnyNode* theirs = nullptr;
        reinterpret_cast<decltype(&stlp::ListEnd)>(original)(self, nullptr, &theirs);
        stlp::ListEnd(self, nullptr, out);
        ei::verify::Check(Name<A>("stlp::list::end"), theirs, *out, "list %p", self);
        return out;
    }
    static std::uint8_t* __fastcall GetAllocator(const std::uint8_t* self, void*, std::uint8_t* out) {
        std::uint8_t theirs = 0xCD;
        bool returned = reinterpret_cast<decltype(&stlp::TreeGetAllocator)>(original)(self, nullptr, &theirs) == &theirs;
        std::uint8_t* result = stlp::TreeGetAllocator(self, nullptr, out);
        ei::verify::Check(Name<A>("stlp::list::get_allocator"), theirs | (returned ? 0 : 0x100),
                          *out | (result == out ? 0 : 0x100), "list %p", self);
        return result;
    }
    static stlp::RbNodeBase** __fastcall IteratorCopy(stlp::RbNodeBase** self, void*, stlp::RbNodeBase* const* from) {
        stlp::RbNodeBase* theirs = nullptr;
        bool returned = reinterpret_cast<decltype(&stlp::TreeIteratorCopy)>(original)(&theirs, nullptr, from) == &theirs;
        stlp::RbNodeBase** result = stlp::TreeIteratorCopy(self, nullptr, from);
        bool ok = returned && result == self && *self == theirs;
        ei::verify::Report(Name<A>("stlp::list::iterator::iterator"), ok, ok ? "" : "another node");
        return result;
    }
    static AnyNode** __fastcall Decrement(AnyNode** self) {
        AnyNode* theirs = *self;
        bool returned = reinterpret_cast<decltype(&stlp::ListIteratorDecrement)>(original)(&theirs) == &theirs;
        AnyNode** result = stlp::ListIteratorDecrement(self);
        bool ok = returned && result == self && *self == theirs;
        ei::verify::Report(Name<A>("stlp::list::iterator::operator--"), ok, ok ? "" : "another node");
        return result;
    }
};

// list(const allocator_type&) of 24-byte nodes: the original's sentinel back to the pool, then ours
// must take it and link it to itself.
template <std::uintptr_t A>
struct List24Construct {
    using List = List24::List;
    static inline void* original = nullptr;

    static List* __fastcall Verify(List* self, void*, const void* allocator) {
        List scratch;
        reinterpret_cast<decltype(&List24::Construct)>(original)(&scratch, nullptr, allocator);
        stlp::deallocate(scratch.head, sizeof(List::Node));
        List24::Construct(self, nullptr, allocator);
        bool ok = self->head == scratch.head && self->head->next == self->head && self->head->prev == self->head;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "sentinel %p, original's %p", self->head, scratch.head);
        ei::verify::Report(Name<A>("stlp::list::list"), ok, detail);
        return self;
    }
};

// insert(pos, x) and _M_create_node(x) of 16 bytes of plain data: the original's node unlinked and
// given back, then ours must take it with the same bytes.
template <std::uintptr_t A>
struct Plain16 {
    using List = List24::List;
    using Node = List::Node;
    using T = stlp::Opaque<16>;
    static inline void* original = nullptr;

    static Node** __fastcall Insert(List* self, void*, Node** out, Node* pos, const T* x) {
        Node* prev = pos->prev;
        Node* theirs = nullptr;
        reinterpret_cast<decltype(&List24::Insert)>(original)(self, nullptr, &theirs, pos, x);
        bool theirsLinked = LinkedBetween(theirs, prev, pos);
        T expected = theirs->data;
        prev->next = pos;
        pos->prev = prev;
        stlp::deallocate(theirs, sizeof(Node));
        List24::Insert(self, nullptr, out, pos, x);
        bool ok = theirsLinked && LinkedBetween(*out, prev, pos) && *out == theirs &&
                  std::memcmp(&(*out)->data, &expected, sizeof(T)) == 0;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p, pos %p", *out, theirs, pos);
        ei::verify::Report(Name<A>("stlp::list::insert"), ok, detail);
        return out;
    }
    static Node* __fastcall CreateNode(List* self, void*, const T* x) {
        Node* theirs = reinterpret_cast<decltype(&List24::CreateNode)>(original)(self, nullptr, x);
        T expected = theirs->data;
        stlp::deallocate(theirs, sizeof(Node));
        Node* ours = List24::CreateNode(self, nullptr, x);
        bool ok = ours == theirs && std::memcmp(&ours->data, &expected, sizeof(T)) == 0 &&
                  std::memcmp(&ours->data, x, sizeof(T)) == 0;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p", ours, theirs);
        ei::verify::Report(Name<A>("stlp::list::_M_create_node"), ok, detail);
        return ours;
    }
};

// ~list() ours only: the nodes from the first, then the sentinel, on top of their free list.
template <std::uintptr_t A, typename Abi>
struct ListDestruct {
    using List = typename Abi::List;
    static void __fastcall Verify(List* self) {
        std::vector<void*> freed;
        for (auto* n = self->head->next; n != self->head; n = n->next) freed.push_back(n);
        freed.push_back(self->head);
        Abi::Destruct(self);
        bool ok = OnTopInOrder(sizeof(typename List::Node), freed);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "%u blocks not on top in order", static_cast<unsigned>(freed.size()));
        ei::verify::Report(Name<A>("stlp::list::~list"), ok, detail);
    }
};

// The list of pair<const string, dword>: ours only.
bool HoldsPair(const stlp::StringDwordPair& copy, const stlp::StringDwordPair& from) {
    const stlp::string& s = copy.first;
    return s == from.first && copy.second == from.second && s.data()[s.size()] == 0;
}
template <std::uintptr_t A>
struct StringDwordChecks {
    using List = StringDwordList::List;
    using Node = List::Node;
    static Node** __fastcall Insert(List* self, void*, Node** out, Node* pos, const stlp::StringDwordPair* x) {
        Node* prev = pos->prev;
        StringDwordList::Insert(self, nullptr, out, pos, x);
        bool ok = LinkedBetween(*out, prev, pos) && HoldsPair((*out)->data, *x);
        ei::verify::Report(Name<A>("stlp::list<pair<string, dword>>::insert"), ok, ok ? "" : "another node or element");
        return out;
    }
    static Node* __fastcall CreateNode(List* self, void*, const stlp::StringDwordPair* x) {
        Node* node = StringDwordList::CreateNode(self, nullptr, x);
        bool ok = HoldsPair(node->data, *x);
        ei::verify::Report(Name<A>("stlp::list<pair<string, dword>>::_M_create_node"), ok, ok ? "" : "another element");
        return node;
    }
    static void __fastcall Clear(List* self) {
        Node* last = self->head->prev;
        StringDwordList::Clear(self);
        bool ok = self->head->next == self->head && self->head->prev == self->head &&
                  (last == self->head || TopOf(sizeof(Node), 1) == std::vector<void*>{last});
        ei::verify::Report(Name<A>("stlp::list<pair<string, dword>>::clear"), ok, ok ? "" : "not emptied");
    }
};

// _M_create_node of a CString and of the briefing's shown item: ours only.
template <std::uintptr_t A>
struct CStringCreate {
    static CStringList::Node* __fastcall Verify(CStringList::List* self, void*, const mfc::CString* x) {
        CStringList::Node* node = CStringList::CreateNode(self, nullptr, x);
        bool ok = std::strcmp(node->data, *x) == 0;
        ei::verify::Report(Name<A>("stlp::list<CString>::_M_create_node"), ok, ok ? "" : "another text");
        return node;
    }
};
template <std::uintptr_t A>
struct ShownItemCreate {
    using T = libcontainers::ShownItem;
    static ShownItemNodes::Node* __fastcall Verify(ShownItemNodes::List* self, void*, const T* x) {
        ShownItemNodes::Node* node = ShownItemNodes::CreateNode(self, nullptr, x);
        const T& c = node->data;
        bool ok = std::strcmp(c.name, x->name) == 0 && c.item == x->item && c.back == x->back && c.frame == x->frame;
        ei::verify::Report(Name<A>("stlp::list<ShownItem>::_M_create_node"), ok, ok ? "" : "another element");
        return node;
    }
};

// The sentinel given back while a constructor unwinds: ours only.
template <std::uintptr_t A>
struct PutSentinel {
    static void __fastcall Verify(stlp::OpaqueList<32>* self) {
        void* head = self->head;
        stlp::ListPutSentinel<32>(self);
        bool ok = !head || TopOf(32, 1) == std::vector<void*>{head};
        ei::verify::Report(Name<A>("stlp::list::_M_put_node"), ok, ok ? "" : "the sentinel is not on top of its free list");
    }
};

// ---- Hashtables -----------------------------------------------------------------------------------

template <std::uintptr_t A, typename Abi>
struct HashPure {
    using Table = typename Abi::Table;
    using Node = typename Abi::Node;
    using Iterator = typename Abi::Iterator;
    static inline void* original = nullptr;

    static Iterator* __fastcall Begin(Table* self, void*, Iterator* out) {
        Iterator theirs{reinterpret_cast<Node*>(1), nullptr};
        bool returned = reinterpret_cast<decltype(&Abi::Begin)>(original)(self, nullptr, &theirs) == &theirs;
        Iterator* result = Abi::Begin(self, nullptr, out);
        bool ok = returned && result == out && out->cur == theirs.cur && out->table == theirs.table;
        ei::verify::Report(Name<A>("stlp::hashtable::begin"), ok, ok ? "" : "another node");
        return result;
    }
    static Iterator* __fastcall ConstructIterator(Iterator* self, void*, Node* cur, Table* table) {
        Iterator theirs{};
        bool returned = reinterpret_cast<decltype(&Abi::ConstructIterator)>(original)(&theirs, nullptr, cur, table) == &theirs;
        Iterator* result = Abi::ConstructIterator(self, nullptr, cur, table);
        bool ok = returned && result == self && self->cur == theirs.cur && self->table == theirs.table;
        ei::verify::Report(Name<A>("stlp::_Ht_iterator::_Ht_iterator"), ok, ok ? "" : "other words");
        return result;
    }
    static Node* __fastcall SkipToNext(const Iterator* self) {
        Node* theirs = reinterpret_cast<decltype(&Abi::SkipToNext)>(original)(self);
        Node* ours = Abi::SkipToNext(self);
        ei::verify::Check(Name<A>("stlp::_Ht_iterator::_M_skip_to_next"), theirs, ours, "node %p", self->cur);
        return ours;
    }
};

// The nodes of a table, bucket by bucket, each chain from its head.
template <typename Table>
std::vector<void*> NodesOf(Table* t) {
    std::vector<void*> nodes;
    for (std::size_t b = 0; b < t->BucketCount(); ++b)
        for (auto* n = t->BucketVector()[b]; n; n = n->next) nodes.push_back(n);
    return nodes;
}

// clear() / ~hashtable() ours only: no elements; the last node freed on top of its free list (under
// the bucket storage when that went to the same list). Dword nodes: every node, the last first.
template <std::uintptr_t A, typename Abi>
struct HashFree {
    using Table = typename Abi::Table;
    using Node = typename Abi::Node;
    static void Run(const char* name, Table* self, bool destroy, bool allNodes) {
        std::vector<void*> nodes = NodesOf(self);
        void* storage = self->BucketVector().Start();
        std::size_t storageBytes = self->BucketVector().Capacity() * sizeof(Node*);
        if (destroy) Abi::Destroy(self);
        else Abi::Clear(self);
        std::vector<void*> top = TopOf(sizeof(Node), nodes.size() + 1);
        if (destroy && storage && FreeList(storageBytes) == FreeList(sizeof(Node)) && !top.empty() && top[0] == storage)
            top.erase(top.begin());
        bool ok = self->Count() == 0;
        if (!nodes.empty()) {
            if (allNodes) ok = ok && top.size() >= nodes.size() && std::equal(nodes.rbegin(), nodes.rend(), top.begin());
            else ok = ok && !top.empty() && top[0] == nodes.back();
        }
        if (!destroy)
            for (std::size_t b = 0; b < self->BucketCount(); ++b) ok = ok && self->BucketVector()[b] == nullptr;
        ei::verify::Report(name, ok, ok ? "" : "elements left, or the nodes are not on top of their free list");
    }
    static void __fastcall Destroy(Table* self) { Run(Name<A>("stlp::hashtable::~hashtable"), self, true, sizeof(Node) == 12); }
    static void __fastcall Clear(Table* self) { Run(Name<A>("stlp::hashtable::clear"), self, false, false); }
};

// ---- Vectors --------------------------------------------------------------------------------------

template <std::uintptr_t A>
struct VectorBaseConstruct {
    static inline void* original = nullptr;
    static stlp::AnyVector* __fastcall Verify(stlp::AnyVector* self, void*, const void* allocator) {
        std::uint32_t theirs[3];
        std::memset(theirs, 0xCD, sizeof(theirs));
        auto* scratch = reinterpret_cast<stlp::AnyVector*>(theirs);
        bool returned = reinterpret_cast<decltype(&stlp::VectorConstructBase)>(original)(scratch, nullptr, allocator) == scratch;
        stlp::AnyVector* result = stlp::VectorConstructBase(self, nullptr, allocator);
        bool ok = returned && result == self && std::memcmp(self, theirs, sizeof(theirs)) == 0;
        ei::verify::Report(Name<A>("stlp::_Vector_base::_Vector_base"), ok, ok ? "" : "other words");
        return result;
    }
};

// A free by the original (when it has a trampoline) and then by ours: the block on top of its free
// list with the old top under it; the original's is taken off again before ours runs.
template <typename Theirs, typename Ours>
void CheckFree(const char* name, void* p, std::size_t bytes, bool runOriginal, Theirs theirs, Ours ours) {
    void** list = p ? FreeList(bytes) : nullptr;
    void* top = list ? *list : nullptr;
    bool ok = true;
    if (list && runOriginal) {
        theirs();
        ok = *list == p && *static_cast<void**>(p) == top;
        *list = top;
    }
    ours();
    if (list) ok = ok && *list == p && *static_cast<void**>(p) == top;
    char detail[64] = "";
    if (!ok) std::snprintf(detail, sizeof(detail), "%p (%u bytes) not on top of its free list", p, static_cast<unsigned>(bytes));
    ei::verify::Report(name, ok, detail);
}

template <std::uintptr_t A, typename Abi>
struct VectorFree {
    using Vector = typename Abi::Vector;
    using T = typename Vector::value_type;
    static inline void* original = nullptr;

    static void __fastcall DestroyBase(Vector* self) {
        CheckFree(
            Name<A>("stlp::_Vector_base::~_Vector_base"), self->Start(), self->Capacity() * sizeof(T), original != nullptr,
            [&] { reinterpret_cast<void(__fastcall*)(Vector*)>(original)(self); }, [&] { Abi::DestroyBase(self); });
    }
    static void __fastcall Deallocate(const stlp::AllocProxy* proxy, void*, T* p, std::size_t n) {
        CheckFree(
            Name<A>("stlp::_STLP_alloc_proxy::deallocate"), p, n * sizeof(T), original != nullptr,
            [&] { reinterpret_cast<decltype(&Abi::Deallocate)>(original)(proxy, nullptr, p, n); },
            [&] { Abi::Deallocate(proxy, nullptr, p, n); });
    }
    static void __fastcall DeallocateFast(T* p, std::size_t n) {
        CheckFree(Name<A>("stlp::_STLP_alloc_proxy::deallocate"), p, n * sizeof(T), false, [] {},
                  [&] { Abi::DeallocateFast(p, n); });
    }
};

// _M_fill_insert(pos, n, x) of dwords. With room: the original on a twin over a copy of the storage
// in our memory, ours on the vector; the same elements and size. Without: ours only, the elements
// against the ones before.
template <std::uintptr_t A>
struct FillInsert4 {
    using Vector = Vec4::Vector;
    static inline void* original = nullptr;

    static void __fastcall Verify(Vector* self, void*, std::uint32_t* pos, std::size_t n, const std::uint32_t* x) {
        std::uint32_t* start = self->Start();
        std::vector<std::uint32_t> before(start, self->Finish());
        std::size_t at = static_cast<std::size_t>(pos - start);
        std::uint32_t value = *x;
        bool room = static_cast<std::size_t>(self->EndOfStorage() - self->Finish()) >= n;
        std::vector<std::uint32_t> twinBlock(start, self->EndOfStorage());
        std::uint32_t* twinWords[3] = {twinBlock.data(), twinBlock.data() + before.size(), twinBlock.data() + twinBlock.size()};
        bool runOriginal = n != 0 && room && original;
        if (runOriginal) {
            const std::uint32_t* twinX = x >= start && x < self->EndOfStorage() ? twinBlock.data() + (x - start) : x;
            reinterpret_cast<decltype(&Vec4::FillInsert)>(original)(reinterpret_cast<Vector*>(twinWords), nullptr,
                                                                        twinBlock.data() + at, n, twinX);
        }
        Vec4::FillInsert(self, nullptr, pos, n, x);
        bool ok = self->size() == before.size() + n;
        for (std::size_t i = 0; ok && i < self->size(); ++i) {
            std::uint32_t expected = i < at ? before[i] : i < at + n ? value : before[i - n];
            ok = (*self)[i] == expected && (!runOriginal || twinWords[0][i] == expected);
        }
        if (runOriginal) ok = ok && self->Start() == start && twinWords[1] - twinWords[0] == static_cast<std::ptrdiff_t>(self->size());
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "%u at %u into %u", static_cast<unsigned>(n), static_cast<unsigned>(at),
                               static_cast<unsigned>(before.size()));
        ei::verify::Report(Name<A>("stlp::vector::_M_fill_insert"), ok, detail);
    }
};

// _M_insert_overflow(pos, x, n) of 8-byte pairs: ours only; the elements, the new capacity
// (size + max(size, n)) and the old block on top of its free list.
template <std::uintptr_t A>
struct InsertOverflow8 {
    using Vector = Vec8::Vector;
    using T = stlp::Opaque<8>;
    static void __fastcall Verify(Vector* self, void*, T* pos, const T* x, std::size_t n) {
        std::vector<T> before(self->Start(), self->Finish());
        std::size_t at = static_cast<std::size_t>(pos - self->Start());
        void* old = self->Start();
        std::size_t oldBytes = self->Capacity() * sizeof(T);
        T value = *x;
        Vec8::InsertOverflow(self, nullptr, pos, x, n);
        std::size_t len = before.size() + (before.size() > n ? before.size() : n);
        bool ok = self->size() == before.size() + n && self->Capacity() == len;
        for (std::size_t i = 0; ok && i < self->size(); ++i) {
            const T& expected = i < at ? before[i] : i < at + n ? value : before[i - n];
            ok = std::memcmp(&(*self)[i], &expected, sizeof(T)) == 0;
        }
        if (old && FreeList(oldBytes) && FreeList(oldBytes) != FreeList(len * sizeof(T)))
            ok = ok && TopOf(oldBytes, 1) == std::vector<void*>{old};
        ei::verify::Report(Name<A>("stlp::vector::_M_insert_overflow"), ok, ok ? "" : "other elements or storage");
    }
};

template <std::uintptr_t A>
struct Uninitialized8 {
    using T = stlp::Opaque<8>;
    static inline void* original = nullptr;
    static T* __fastcall Copy(const T* first, const T* last, T* result, const void* tag) {
        std::vector<T> theirs(static_cast<std::size_t>(last - first));
        T* theirsEnd = reinterpret_cast<decltype(&Vec8::UninitializedCopy)>(original)(first, last, theirs.data(), tag);
        T* end = Vec8::UninitializedCopy(first, last, result, tag);
        bool ok = theirsEnd - theirs.data() == end - result &&
                  (theirs.empty() || std::memcmp(theirs.data(), result, theirs.size() * sizeof(T)) == 0);
        ei::verify::Report(Name<A>("stlp::uninitialized_copy"), ok, ok ? "" : "other elements");
        return end;
    }
    static T* __fastcall Fill(T* first, std::size_t n, const T* x, const void* tag) {
        T value = *x;
        T* end = Vec8::UninitializedFill(first, n, x, tag);
        bool ok = end == first + n;
        for (std::size_t i = 0; ok && i < n; ++i) ok = std::memcmp(&first[i], &value, sizeof(T)) == 0;
        ei::verify::Report(Name<A>("stlp::uninitialized_fill_n"), ok, ok ? "" : "other elements");
        return end;
    }
};

// ---- Pairs, algorithms and helpers over game types (ours only) ------------------------------------

template <std::uintptr_t A>
struct PairConstruct {
    static stlp::StringDwordPair* __fastcall Verify(stlp::StringDwordPair* self, void*, const stlp::string* first,
                                                    const std::uint32_t* second) {
        stlp::StringDwordPair* result = stlp::PairAbi::ConstructStringDword(self, nullptr, first, second);
        bool ok = result == self && self->first == *first && self->second == *second;
        ei::verify::Report(Name<A>("stlp::pair<string, dword>::pair"), ok, ok ? "" : "another pair");
        return result;
    }
};

template <std::uintptr_t A>
struct GameAlgorithms {
    static BriefingSpeaker* __fastcall CopySpeakers(const BriefingSpeaker* first, const BriefingSpeaker* last,
                                                    BriefingSpeaker* result) {
        BriefingSpeaker* end = libcontainers::CopySpeakers(first, last, result);
        bool ok = end == result + (last - first);
        for (std::ptrdiff_t i = 0; ok && i < last - first; ++i)
            ok = result[i].unit == first[i].unit && std::strcmp(result[i].name, first[i].name) == 0 &&
                 result[i].spareFlag == first[i].spareFlag;
        ei::verify::Report(Name<A>("stlp::copy(BriefingSpeaker)"), ok, ok ? "" : "other speakers");
        return end;
    }
    static void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value) {
        int refs = p && value->ptr ? value->ptr->refs_ : 0;
        libcontainers::ConstructRefPtr(p, value);
        bool ok = !p || (p->ptr == value->ptr && (!value->ptr || value->ptr->refs_ == refs + 1));
        ei::verify::Report(Name<A>("stlp::_Construct(RefPtr)"), ok, ok ? "" : "another pointer or count");
    }
    static RefPtr* __fastcall CopyBackward(const RefPtr* first, const RefPtr* last, RefPtr* result) {
        std::vector<CRefObject*> sources;
        for (const RefPtr* p = first; p != last; ++p) sources.push_back(p->ptr);
        RefPtr* start = libcontainers::CopyBackwardRefPtrs(first, last, result);
        bool ok = start == result - sources.size();
        for (std::size_t i = 0; ok && i < sources.size(); ++i) ok = start[i].ptr == sources[i];
        ei::verify::Report(Name<A>("stlp::copy_backward(RefPtr)"), ok, ok ? "" : "other pointers");
        return start;
    }
    static CEngineObject** __fastcall ConstructHeld(CEngineObject** p, void*, CEngineObject* const* value) {
        CEngineObject* object = *value;
        int refs = object ? object->refCount_ : 0;
        CEngineObject** result = libcontainers::ConstructHeldObject(p, nullptr, value);
        bool ok = result == p && *p == object && (!object || object->refCount_ == refs + 1);
        ei::verify::Report(Name<A>("stlp::_Construct(CEngineObject*)"), ok, ok ? "" : "another pointer or count");
        return result;
    }
};

// A CString released: its count one less (when it was shared; a last reference frees the data).
template <std::uintptr_t A>
struct CStringRelease {
    static bool Shared(const mfc::CString& s) { return RefsOf(s) > 1; }
    static void __fastcall Destroy(mfc::CString* self) {
        const char* text = *self;
        int refs = RefsOf(*self);
        libcontainers::DestroyCString(self);
        bool ok = refs <= 1 || RefsAt(text) == refs - 1;
        ei::verify::Report(Name<A>("CString::~CString"), ok, ok ? "" : "the count is not one less");
    }
    static void* __fastcall Delete(void* self, void*, unsigned flags) {
        auto* s = static_cast<mfc::CString*>(self);
        const char* text = *s;
        int refs = RefsOf(*s);
        void* result = libcontainers::DeleteCString(self, nullptr, flags);
        bool ok = result == self && (refs <= 1 || RefsAt(text) == refs - 1);
        ei::verify::Report(Name<A>("CString scalar deleting destructor"), ok, ok ? "" : "the count is not one less");
        return result;
    }
    static void __fastcall DestroyWord(libcontainers::TokenWord* self) {
        const char* text = self->text;
        int refs = RefsOf(self->text);
        libcontainers::DestroyTokenWord(self);
        bool ok = refs <= 1 || RefsAt(text) == refs - 1;
        ei::verify::Report(Name<A>("TokenWord::~TokenWord"), ok, ok ? "" : "the count is not one less");
    }
};

template <std::uintptr_t A>
struct UnwindHelpers {
    static void __fastcall ClearLocal(libcontainers::LoaderLocal* self) {
        libcontainers::LoaderLocal before = *self;
        libcontainers::ClearLoaderLocal(self);
        bool ok = self->cleared == 0 && std::memcmp(self->unused_00, before.unused_00, sizeof(before.unused_00)) == 0;
        ei::verify::Report(Name<A>("unwind: clear +8"), ok, ok ? "" : "other bytes changed");
    }
    static void __fastcall DestroyVectors(libcontainers::PointerVectorsPair* self) {
        void* first = self->first.Start();
        void* second = self->second.Start();
        std::size_t firstBytes = self->first.Capacity() * 4;
        std::size_t secondBytes = self->second.Capacity() * 4;
        libcontainers::DestroyPointerVectors(self);
        bool ok = !first || TopOf(firstBytes, 1) == std::vector<void*>{first};
        if (second && FreeList(secondBytes) != FreeList(firstBytes)) ok = ok && TopOf(secondBytes, 1) == std::vector<void*>{second};
        if (second && first && FreeList(secondBytes) == FreeList(firstBytes)) ok = ok && TopOf(firstBytes, 2)[1] == second;
        ei::verify::Report(Name<A>("unwind: two vectors"), ok, ok ? "" : "the storage is not on top of its free list");
    }
    static void __fastcall SetRoot(void* self) {
        libcontainers::SetRootVtable(self);
        bool ok = *static_cast<std::uintptr_t*>(self) == CLinkMesh::kRootVtableAddress;
        ei::verify::Report(Name<A>("unwind: root vtable"), ok, ok ? "" : "another vtable");
    }
};

// CTime::operator-(CTimeSpan) against the original into a scratch time.
template <std::uintptr_t A>
struct TimeMinus {
    using CTime = mfc::impl::CTime;
    static inline void* original = nullptr;
    static CTime* __fastcall Verify(const CTime* self, void*, CTime* out, long span) {
        CTime theirs{0x5A5A5A5A};
        bool returned = reinterpret_cast<decltype(&libcontainers::TimeMinusSpan)>(original)(self, nullptr, &theirs, span) == &theirs;
        CTime* result = libcontainers::TimeMinusSpan(self, nullptr, out, span);
        bool ok = returned && result == out && out->time == theirs.time;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "%ld - %ld: %ld, original's %ld", self->time, span, out->time, theirs.time);
        ei::verify::Report(Name<A>("CTime::operator-(CTimeSpan)"), ok, detail);
        return result;
    }
};

// ---- Start-up check -------------------------------------------------------------------------------

template <typename Fn>
Fn At(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

// A fake counted object (CRefObject's or CEngineObject's header) that never reaches zero.
struct FakeCounted {
    void* vtable = nullptr;
    int refs = 50;
};

void StartupLists() {
    using Node = List24::List::Node;
    std::uint8_t alloc = 0;
    // 24-byte nodes: built, walked and destroyed through the addresses.
    for (std::uintptr_t ctor : {0x004827D0u, 0x004FB3C0u}) {
        List24::List list;
        At<decltype(&List24::Construct)>(ctor)(&list, nullptr, &alloc);
        for (std::uint8_t i = 0; i < 4; ++i) {
            stlp::Opaque<16> value;
            std::memset(&value, 0x10 + i, sizeof(value));
            Node* out = nullptr;
            At<decltype(&List24::Insert)>(0x00482980)(&list, nullptr, &out, list.head, &value);
        }
        stlp::Opaque<16> value;
        std::memset(&value, 0x5A, sizeof(value));
        Node* node = At<decltype(&List24::CreateNode)>(0x00482C30)(&list, nullptr, &value);
        stlp::deallocate(node, sizeof(Node));
        auto* any = reinterpret_cast<AnyList*>(&list);
        AnyNode* it = nullptr;
        At<decltype(&stlp::ListBegin)>(0x00478FC0)(any, nullptr, &it);
        At<decltype(&stlp::ListBegin)>(0x00478FD0)(any, nullptr, &it);
        At<decltype(&stlp::ListEnd)>(0x00478FE0)(any, nullptr, &it);
        for (int i = 0; i < 5; ++i) At<decltype(&stlp::ListIteratorDecrement)>(0x004FB570)(&it);
        stlp::RbNodeBase* copy = nullptr;
        At<decltype(&stlp::TreeIteratorCopy)>(0x004FB3B0)(&copy, nullptr, reinterpret_cast<stlp::RbNodeBase* const*>(&it));
        std::uint8_t tag = 0;
        At<decltype(&stlp::TreeGetAllocator)>(0x00479130)(reinterpret_cast<const std::uint8_t*>(&list), nullptr, &tag);
        At<decltype(&List24::Destruct)>(ctor == 0x004827D0u ? 0x004829B0u : 0x00482CC0u)(&list);
    }
    // 16-byte nodes.
    {
        List16::List list;
        List16::Construct(&list, nullptr, &alloc);
        for (int i = 0; i < 3; ++i) list.PushBack(stlp::Opaque<8>{});
        At<decltype(&List16::Destruct)>(0x005F9620)(&list);
    }
    // The unwinding constructors' sentinel.
    for (std::uintptr_t put : {0x00482790u, 0x004FB840u}) {
        stlp::OpaqueList<32> list;
        list.head = static_cast<stlp::OpaqueList<32>::Node*>(stlp::allocate(32));
        At<decltype(&stlp::ListPutSentinel<32>)>(put)(&list);
        list.head = nullptr;
        At<decltype(&stlp::ListPutSentinel<32>)>(put)(&list);
    }
    // pair<const string, dword>.
    {
        using SNode = StringDwordList::List::Node;
        StringDwordList::List list;
        List24::Construct(reinterpret_cast<List24::List*>(&list), nullptr, &alloc);
        const char* names[] = {"alpha", "", "a name longer than twenty-four bytes"};
        for (std::uint32_t i = 0; i < 3; ++i) {
            stlp::StringDwordPair value{stlp::string(names[i]), i * 7};
            SNode* out = nullptr;
            At<decltype(&StringDwordList::Insert)>(0x004FB730)(&list, nullptr, &out, list.head, &value);
            SNode* node = At<decltype(&StringDwordList::CreateNode)>(0x004FB8B0)(&list, nullptr, &value);
            stlp::StringDwordElement::Destroy(&node->data);
            stlp::deallocate(node, sizeof(SNode));
            alignas(stlp::StringDwordPair) unsigned char storage[sizeof(stlp::StringDwordPair)];
            auto* pair = reinterpret_cast<stlp::StringDwordPair*>(storage);
            At<decltype(&stlp::PairAbi::ConstructStringDword)>(0x004FB170)(pair, nullptr, &value.first, &value.second);
            pair->~StringDwordPair();
        }
        At<decltype(&StringDwordList::Clear)>(0x004FBCF0)(&list);
        stlp::deallocate(list.head, sizeof(SNode));
    }
    // A CString node and a shown item's.
    {
        mfc::CString text("text");
        CStringList::Node* node = At<decltype(&CStringList::CreateNode)>(0x005F9C70)(nullptr, nullptr, &text);
        node->data.~CString();
        stlp::deallocate(node, sizeof(*node));
        libcontainers::ShownItem item{mfc::CString("item"), nullptr, nullptr, nullptr};
        ShownItemNodes::Node* shown = At<decltype(&ShownItemNodes::CreateNode)>(0x0060A010)(nullptr, nullptr, &item);
        shown->data.name.~CString();
        stlp::deallocate(shown, sizeof(*shown));
    }
    // ~list of keyed CString pairs and of queued streams.
    {
        KeyedTextsNodes::List list;
        list.head = static_cast<KeyedTextsNodes::Node*>(stlp::allocate(sizeof(KeyedTextsNodes::Node)));
        list.head->next = list.head->prev = list.head;
        for (int i = 0; i < 2; ++i) {
            auto* node = static_cast<KeyedTextsNodes::Node*>(stlp::allocate(sizeof(KeyedTextsNodes::Node)));
            node->data.key[0] = node->data.key[1] = static_cast<std::uint32_t>(i);
            new (&node->data.first) mfc::CString("first");
            new (&node->data.second) mfc::CString("second");
            node->next = list.head;
            node->prev = list.head->prev;
            list.head->prev->next = node;
            list.head->prev = node;
        }
        At<decltype(&KeyedTextsNodes::Destruct)>(0x005F9570)(&list);
    }
    {
        StreamQueue::List queue;
        queue.head = static_cast<StreamQueue::Node*>(stlp::allocate(sizeof(StreamQueue::Node)));
        queue.head->next = queue.head->prev = queue.head;
        for (int i = 0; i < 2; ++i) {
            auto* node = static_cast<StreamQueue::Node*>(stlp::allocate(sizeof(StreamQueue::Node)));
            node->data.ConstructQueueStream();
            node->next = queue.head;
            node->prev = queue.head->prev;
            queue.head->prev->next = node;
            queue.head->prev = node;
        }
        At<decltype(&StreamQueue::Destruct)>(0x00478F10)(&queue);
    }
}

void StartupHashtables() {
    {
        Map8::Table table;
        Map8::ConstructDefault(&table);
        for (std::uint32_t k = 0; k < 400; k += 37) {
            stlp::DwordKeyed<8> v{};
            v.key = k;
            table.FindOrInsert(v);
        }
        for (std::uintptr_t begin : {0x004FB090u, 0x005E0C00u}) {
            Map8::Iterator it{};
            At<decltype(&Map8::Begin)>(begin)(&table, nullptr, &it);
        }
        for (Map8::Node* n = table.First(); n; n = table.Next(n)) {
            Map8::Iterator it{};
            At<decltype(&Map8::ConstructIterator)>(0x00482DD0)(&it, nullptr, n, &table);
            At<decltype(&Map8::ConstructIterator)>(0x005E0D50)(&it, nullptr, n, &table);
            At<decltype(&Map8::SkipToNext)>(0x005E0E00)(&it);
        }
        At<decltype(&Map8::Destroy)>(0x00482DF0)(&table);
    }
    {
        Str4::Table table;
        Str4::ConstructDefault(&table);
        const char* keys[] = {"a", "bb", "\xC3\xA9t\xC3\xA9", "figure", "x9", ""};
        for (int round = 0; round < 2; ++round) {
            for (const char* k : keys) {
                stlp::StringKeyed<4> v(stlp::string{k});
                table.FindOrInsert(v);
            }
            for (Str4::Node* n = table.First(); n; n = table.Next(n)) {
                Str4::Iterator it{n, &table};
                At<decltype(&Str4::SkipToNext)>(0x004FBB80)(&it);
            }
            if (round == 0) At<decltype(&Str4::Clear)>(0x004FBC80)(&table);
        }
        At<decltype(&Str4::Destroy)>(0x004F6A50)(&table);
    }
}

template <typename T>
void MakeVector(typename stlp::VectorAbi<T>::Vector& v, std::size_t size, std::size_t capacity) {
    v.ConstructEmpty();
    if (!capacity) return;
    v.Start() = static_cast<T*>(stlp::allocate(capacity * sizeof(T)));
    std::memset(v.Start(), 0x3C, capacity * sizeof(T));
    v.Finish() = v.Start() + size;
    v.EndOfStorage() = v.Start() + capacity;
}

void StartupVectors() {
    std::uint8_t alloc = 0;
    for (std::uintptr_t ctor : {0x00482A80u, 0x00619600u, 0x006316C0u}) {
        stlp::AnyVector v;
        At<decltype(&stlp::VectorConstructBase)>(ctor)(&v, nullptr, &alloc);
    }
    // ~_Vector_base.
    for (std::size_t cap : {0u, 3u, 40u}) {
        {
            Vec1::Vector v;
            MakeVector<char>(v, 0, cap);
            At<decltype(&Vec1::DestroyBase)>(0x004FB6A0)(&v);
        }
        for (std::uintptr_t a : {0x004FB290u, 0x004FB6F0u, 0x005F8E00u}) {
            Vec4::Vector v;
            MakeVector<std::uint32_t>(v, 1, cap);
            At<decltype(&Vec4::DestroyBase)>(a)(&v);
        }
        for (std::uintptr_t a : {0x005F9090u, 0x006342C0u}) {
            Vec8::Vector v;
            MakeVector<stlp::Opaque<8>>(v, 1, cap);
            At<decltype(&Vec8::DestroyBase)>(a)(&v);
        }
        for (std::uintptr_t a : {0x00482440u, 0x005F99B0u}) {
            Vec12::Vector v;
            MakeVector<stlp::Opaque<12>>(v, 1, cap);
            At<decltype(&Vec12::DestroyBase)>(a)(&v);
        }
    }
    // deallocate(p, n), both forms.
    stlp::AllocProxy proxy{};
    for (std::size_t n : {1u, 2u, 5u}) {
        At<decltype(&Vec4::Deallocate)>(0x005F9F40)(&proxy, nullptr, static_cast<std::uint32_t*>(stlp::allocate(n * 4)), n);
        for (std::uintptr_t a : {0x00482D10u, 0x005FA300u})
            At<decltype(&Vec12::Deallocate)>(a)(&proxy, nullptr, static_cast<stlp::Opaque<12>*>(stlp::allocate(n * 12)), n);
        for (std::uintptr_t a : {0x00482BF0u, 0x004FB9D0u})
            At<decltype(&Vec32::Deallocate)>(a)(&proxy, nullptr, static_cast<stlp::Opaque<32>*>(stlp::allocate(n * 32)), n);
        At<decltype(&Vec48::Deallocate)>(0x005FA2C0)(&proxy, nullptr, static_cast<stlp::Opaque<48>*>(stlp::allocate(n * 48)), n);
        At<decltype(&Vec4::DeallocateFast)>(0x005FA3C0)(static_cast<std::uint32_t*>(stlp::allocate(n * 4)), n);
        At<decltype(&Vec12::DeallocateFast)>(0x005FA360)(static_cast<stlp::Opaque<12>*>(stlp::allocate(n * 12)), n);
        for (std::uintptr_t a : {0x004FB860u, 0x005FA8E0u})
            At<decltype(&Vec24::DeallocateFast)>(a)(static_cast<stlp::Opaque<24>*>(stlp::allocate(n * 24)), n);
    }
    // _M_fill_insert with and without room, x inside and outside the vector.
    for (std::size_t n : {0u, 1u, 3u, 9u}) {
        Vec4::Vector v;
        MakeVector<std::uint32_t>(v, 5, 8);
        for (std::uint32_t i = 0; i < 5; ++i) v[i] = i + 1;
        std::uint32_t x = 77;
        At<decltype(&Vec4::FillInsert)>(0x004FBA40)(&v, nullptr, v.Start() + 2, n, &x);
        At<decltype(&Vec4::FillInsert)>(0x004FBA40)(&v, nullptr, v.Start() + 1, n, v.Start());
        v.FreeStorage();
    }
    // _M_insert_overflow of 8-byte pairs, and the copies it uses.
    for (std::size_t n : {1u, 2u, 7u}) {
        Vec8::Vector v;
        MakeVector<stlp::Opaque<8>>(v, 3, 3);
        stlp::Opaque<8> x;
        std::memset(&x, 0x61, sizeof(x));
        At<decltype(&Vec8::InsertOverflow)>(0x006317E0)(&v, nullptr, v.Start() + 1, &x, n);
        stlp::Opaque<8> out[16];
        At<decltype(&Vec8::UninitializedCopy)>(0x006325E0)(v.Start(), v.Finish(), out, nullptr);
        At<decltype(&Vec8::UninitializedFill)>(0x00632610)(out, n, &x, nullptr);
        v.FreeStorage();
    }
}

void StartupGameTypes() {
    // Counted pointers over fake objects (their counts never reach zero).
    FakeCounted objects[4];
    RefPtr from[3] = {{reinterpret_cast<CRefObject*>(&objects[0])}, {nullptr}, {reinterpret_cast<CRefObject*>(&objects[1])}};
    RefPtr to[4] = {{reinterpret_cast<CRefObject*>(&objects[2])}, {reinterpret_cast<CRefObject*>(&objects[3])}, {nullptr},
                    {reinterpret_cast<CRefObject*>(&objects[2])}};
    At<decltype(&libcontainers::CopyBackwardRefPtrs)>(0x0060AAA0)(from, from + 3, to + 4);
    RefPtr made{};
    At<decltype(&libcontainers::ConstructRefPtr)>(0x0060AA80)(&made, &from[0]);
    At<decltype(&libcontainers::ConstructRefPtr)>(0x0060AA80)(nullptr, &from[0]);
    CEngineObject* held = reinterpret_cast<CEngineObject*>(&objects[3]);
    CEngineObject* copy = nullptr;
    At<decltype(&libcontainers::ConstructHeldObject)>(0x004FB680)(&copy, nullptr, &held);
    held = nullptr;
    At<decltype(&libcontainers::ConstructHeldObject)>(0x004FB680)(&copy, nullptr, &held);
    // The briefing's speakers.
    {
        BriefingSpeaker source[2]{};
        BriefingSpeaker target[3]{};
        source[0].name = mfc::CString("Haart");
        for (auto& t : target) t.name = mfc::CString("old");
        source[1].spareFlag = true;
        At<decltype(&libcontainers::CopySpeakers)>(0x0060AB60)(source, source + 2, target + 1);
    }
    // CString's destructors.
    mfc::CString text("shared text");
    for (std::uintptr_t a : {0x005F9680u, 0x005FBE20u}) {
        alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
        At<decltype(&libcontainers::DestroyCString)>(a)(new (storage) mfc::CString(text));
    }
    for (std::uintptr_t a : {0x005FBCB0u, 0x0060ABE0u}) {
        alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
        At<decltype(&libcontainers::DeleteCString)>(a)(new (storage) mfc::CString(text), nullptr, 0);
        void* heap = gamecrt::operator_new(sizeof(mfc::CString));
        At<decltype(&libcontainers::DeleteCString)>(a)(new (heap) mfc::CString(text), nullptr, 1);
    }
    // The unwind helpers.
    {
        alignas(libcontainers::TokenWord) unsigned char storage[sizeof(libcontainers::TokenWord)];
        auto* word = new (storage) libcontainers::TokenWord;
        std::memset(word->unused_00, 0x11, sizeof(word->unused_00));
        word->text = text;
        At<decltype(&libcontainers::DestroyTokenWord)>(0x00445FC0)(word);
        libcontainers::LoaderLocal local;
        std::memset(&local, 0x22, sizeof(local));
        At<decltype(&libcontainers::ClearLoaderLocal)>(0x00482E90)(&local);
        libcontainers::PointerVectorsPair vectors;
        MakeVector<void*>(vectors.first, 1, 4);
        MakeVector<void*>(vectors.second, 2, 2);
        At<decltype(&libcontainers::DestroyPointerVectors)>(0x005F60B0)(&vectors);
        std::uintptr_t object[2] = {0, 0};
        At<decltype(&libcontainers::SetRootVtable)>(0x004F69F0)(object);
    }
    // CTime less a span, across zero and the ends of the range.
    for (long time : {0L, 1L, 946684800L, 0x7FFFFFFFL, -5L})
        for (long span : {0L, 1L, -1L, 86400L, 0x7FFFFFFFL}) {
            mfc::impl::CTime at{time};
            mfc::impl::CTime out{};
            At<decltype(&libcontainers::TimeMinusSpan)>(0x005E0550)(&at, nullptr, &out, span);
        }
}

void LibraryContainersStartupCheck() {
    StartupLists();
    StartupHashtables();
    StartupVectors();
    StartupGameTypes();
}

} // namespace

EI_STARTUP_CHECK(LibraryContainersStartupCheck);

// Lists: begin (8 bytes), end (6), get_allocator (push ecx; mov eax,[esp+8]: 5), the iterator's copy
// (6) and operator-- (7).
EI_VERIFY(0x00478FC0, ListPure<0x00478FC0>::Begin, 8, ListPure<0x00478FC0>::original);
EI_VERIFY(0x00478FD0, ListPure<0x00478FD0>::Begin, 8, ListPure<0x00478FD0>::original);
EI_VERIFY(0x00478FE0, ListPure<0x00478FE0>::End, 6, ListPure<0x00478FE0>::original);
EI_VERIFY(0x00479130, ListPure<0x00479130>::GetAllocator, 5, ListPure<0x00479130>::original);
EI_VERIFY(0x004FB3B0, ListPure<0x004FB3B0>::IteratorCopy, 6, ListPure<0x004FB3B0>::original);
EI_VERIFY(0x004FB570, ListPure<0x004FB570>::Decrement, 7, ListPure<0x004FB570>::original);
// Constructors (push -1; push imm32), insert (5) and _M_create_node (6) against the original.
EI_VERIFY(0x004827D0, List24Construct<0x004827D0>::Verify, 7, List24Construct<0x004827D0>::original);
EI_VERIFY(0x004FB3C0, List24Construct<0x004FB3C0>::Verify, 7, List24Construct<0x004FB3C0>::original);
EI_VERIFY(0x00482980, Plain16<0x00482980>::Insert, 5, Plain16<0x00482980>::original);
EI_VERIFY(0x00482C30, Plain16<0x00482C30>::CreateNode, 6, Plain16<0x00482C30>::original);
// Ours only.
EI_VERIFY(0x004829B0, (ListDestruct<0x004829B0, List24>::Verify), 0, s_unused);
EI_VERIFY(0x00482CC0, (ListDestruct<0x00482CC0, List24>::Verify), 0, s_unused);
EI_VERIFY(0x005F9620, (ListDestruct<0x005F9620, List16>::Verify), 0, s_unused);
EI_VERIFY(0x00478F10, (ListDestruct<0x00478F10, StreamQueue>::Verify), 0, s_unused);
EI_VERIFY(0x005F9570, (ListDestruct<0x005F9570, KeyedTextsNodes>::Verify), 0, s_unused);
EI_VERIFY(0x004FB730, StringDwordChecks<0x004FB730>::Insert, 0, s_unused);
EI_VERIFY(0x004FB8B0, StringDwordChecks<0x004FB8B0>::CreateNode, 0, s_unused);
EI_VERIFY(0x004FBCF0, StringDwordChecks<0x004FBCF0>::Clear, 0, s_unused);
EI_VERIFY(0x005F9C70, CStringCreate<0x005F9C70>::Verify, 0, s_unused);
EI_VERIFY(0x0060A010, ShownItemCreate<0x0060A010>::Verify, 0, s_unused);
EI_VERIFY(0x00482790, PutSentinel<0x00482790>::Verify, 0, s_unused);
EI_VERIFY(0x004FB840, PutSentinel<0x004FB840>::Verify, 0, s_unused);

// Hashtables: begin (mov edx,[ecx+8]; push ebx; push esi: 5), the iterator's constructor (6),
// _M_skip_to_next (dword keys 6, strings 5) against the original; the frees ours only.
EI_VERIFY(0x004FB090, (HashPure<0x004FB090, Map8>::Begin), 5, (HashPure<0x004FB090, Map8>::original));
EI_VERIFY(0x005E0C00, (HashPure<0x005E0C00, Map8>::Begin), 5, (HashPure<0x005E0C00, Map8>::original));
EI_VERIFY(0x00482DD0, (HashPure<0x00482DD0, Map8>::ConstructIterator), 6, (HashPure<0x00482DD0, Map8>::original));
EI_VERIFY(0x005E0D50, (HashPure<0x005E0D50, Map8>::ConstructIterator), 6, (HashPure<0x005E0D50, Map8>::original));
EI_VERIFY(0x005E0E00, (HashPure<0x005E0E00, Map8>::SkipToNext), 6, (HashPure<0x005E0E00, Map8>::original));
EI_VERIFY(0x004FBB80, (HashPure<0x004FBB80, Str4>::SkipToNext), 5, (HashPure<0x004FBB80, Str4>::original));
EI_VERIFY(0x00482DF0, (HashFree<0x00482DF0, Map8>::Destroy), 0, s_unused);
EI_VERIFY(0x004F6A50, (HashFree<0x004F6A50, Str4>::Destroy), 0, s_unused);
EI_VERIFY(0x004FBC80, (HashFree<0x004FBC80, Str4>::Clear), 0, s_unused);

// Vectors: the base constructor (6) and uninitialized_copy (6) against the original; frees with a
// copyable start (5 or 6 bytes) original first, the others ours only.
EI_VERIFY(0x00482A80, VectorBaseConstruct<0x00482A80>::Verify, 6, VectorBaseConstruct<0x00482A80>::original);
EI_VERIFY(0x00619600, VectorBaseConstruct<0x00619600>::Verify, 6, VectorBaseConstruct<0x00619600>::original);
EI_VERIFY(0x006316C0, VectorBaseConstruct<0x006316C0>::Verify, 6, VectorBaseConstruct<0x006316C0>::original);
EI_VERIFY(0x004FB6A0, (VectorFree<0x004FB6A0, Vec1>::DestroyBase), 5, (VectorFree<0x004FB6A0, Vec1>::original));
EI_VERIFY(0x005F8E00, (VectorFree<0x005F8E00, Vec4>::DestroyBase), 5, (VectorFree<0x005F8E00, Vec4>::original));
EI_VERIFY(0x005F9090, (VectorFree<0x005F9090, Vec8>::DestroyBase), 5, (VectorFree<0x005F9090, Vec8>::original));
EI_VERIFY(0x00482440, (VectorFree<0x00482440, Vec12>::DestroyBase), 6, (VectorFree<0x00482440, Vec12>::original));
EI_VERIFY(0x005F99B0, (VectorFree<0x005F99B0, Vec12>::DestroyBase), 5, (VectorFree<0x005F99B0, Vec12>::original));
EI_VERIFY(0x004FB290, (VectorFree<0x004FB290, Vec4>::DestroyBase), 0, s_unused);
EI_VERIFY(0x004FB6F0, (VectorFree<0x004FB6F0, Vec4>::DestroyBase), 0, s_unused);
EI_VERIFY(0x006342C0, (VectorFree<0x006342C0, Vec8>::DestroyBase), 0, s_unused);
EI_VERIFY(0x005F9F40, (VectorFree<0x005F9F40, Vec4>::Deallocate), 6, (VectorFree<0x005F9F40, Vec4>::original));
EI_VERIFY(0x00482D10, (VectorFree<0x00482D10, Vec12>::Deallocate), 6, (VectorFree<0x00482D10, Vec12>::original));
EI_VERIFY(0x005FA300, (VectorFree<0x005FA300, Vec12>::Deallocate), 6, (VectorFree<0x005FA300, Vec12>::original));
EI_VERIFY(0x00482BF0, (VectorFree<0x00482BF0, Vec32>::Deallocate), 6, (VectorFree<0x00482BF0, Vec32>::original));
EI_VERIFY(0x004FB9D0, (VectorFree<0x004FB9D0, Vec32>::Deallocate), 6, (VectorFree<0x004FB9D0, Vec32>::original));
EI_VERIFY(0x005FA2C0, (VectorFree<0x005FA2C0, Vec48>::Deallocate), 6, (VectorFree<0x005FA2C0, Vec48>::original));
EI_VERIFY(0x005FA3C0, (VectorFree<0x005FA3C0, Vec4>::DeallocateFast), 0, s_unused);
EI_VERIFY(0x005FA360, (VectorFree<0x005FA360, Vec12>::DeallocateFast), 0, s_unused);
EI_VERIFY(0x004FB860, (VectorFree<0x004FB860, Vec24>::DeallocateFast), 0, s_unused);
EI_VERIFY(0x005FA8E0, (VectorFree<0x005FA8E0, Vec24>::DeallocateFast), 0, s_unused);
// _M_fill_insert: push ebx/ebp/esi/edi; mov edi,[esp+0x18] (8 bytes).
EI_VERIFY(0x004FBA40, FillInsert4<0x004FBA40>::Verify, 8, FillInsert4<0x004FBA40>::original);
EI_VERIFY(0x006317E0, InsertOverflow8<0x006317E0>::Verify, 0, s_unused);
EI_VERIFY(0x006325E0, Uninitialized8<0x006325E0>::Copy, 6, Uninitialized8<0x006325E0>::original);
EI_VERIFY(0x00632610, Uninitialized8<0x00632610>::Fill, 0, s_unused);

// Pairs, algorithms and helpers over game types: ours only.
EI_VERIFY(0x004FB170, PairConstruct<0x004FB170>::Verify, 0, s_unused);
EI_VERIFY(0x0060AB60, GameAlgorithms<0x0060AB60>::CopySpeakers, 0, s_unused);
EI_VERIFY(0x0060AA80, GameAlgorithms<0x0060AA80>::ConstructRefPtr, 0, s_unused);
EI_VERIFY(0x0060AAA0, GameAlgorithms<0x0060AAA0>::CopyBackward, 0, s_unused);
EI_VERIFY(0x004FB680, GameAlgorithms<0x004FB680>::ConstructHeld, 0, s_unused);
EI_VERIFY(0x005F9680, CStringRelease<0x005F9680>::Destroy, 0, s_unused);
EI_VERIFY(0x005FBE20, CStringRelease<0x005FBE20>::Destroy, 0, s_unused);
EI_VERIFY(0x005FBCB0, CStringRelease<0x005FBCB0>::Delete, 0, s_unused);
EI_VERIFY(0x0060ABE0, CStringRelease<0x0060ABE0>::Delete, 0, s_unused);
EI_VERIFY(0x00445FC0, CStringRelease<0x00445FC0>::DestroyWord, 0, s_unused);
EI_VERIFY(0x00482E90, UnwindHelpers<0x00482E90>::ClearLocal, 0, s_unused);
EI_VERIFY(0x005F60B0, UnwindHelpers<0x005F60B0>::DestroyVectors, 0, s_unused);
EI_VERIFY(0x004F69F0, UnwindHelpers<0x004F69F0>::SetRoot, 0, s_unused);
// CTime - CTimeSpan: mov ecx,[ecx]; mov eax,[esp+8] (6 bytes), against the original.
EI_VERIFY(0x005E0550, TimeMinus<0x005E0550>::Verify, 6, TimeMinus<0x005E0550>::original);
