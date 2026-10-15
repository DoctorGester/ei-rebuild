// Checks for the list<T> instantiations that copy or destroy elements (src/lib/stlp_list.cpp).
//
// insert of plain data compares with the original: the original's node is taken out again
// (unlinked, back to the pool), then ours must take the same node, link it the same way and hold the
// same bytes. The reference holders' and CStrings' insert / push_back / erase / clear / ~list /
// remove run ours only (the copies add references, Share may clone): the links must be consistent,
// the element copied as its policy says, an erased node or a freed sentinel on top of its free list.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/mfc.h"
#include "../../src/lib/stlp_list.h"
#include "../../src/lib/stlp_list_refs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::LinkedBetween;
using stlp_verify::Name;

using PlainList4 = stlp::ListAbi<std::uint32_t>;
using PlainList16 = stlp::ListAbi<stlp::Opaque<16>>;
using PlainList20 = stlp::ListAbi<stlp::Opaque<20>>;
using PlainList28 = stlp::ListAbi<stlp::Opaque<28>>;
using PlainList260 = stlp::ListAbi<stlp::Opaque<0x104>>;
using SharedRefList = stlp::ListAbi<RefPtr, stlp::SharedRef>;
using CountedRefList = stlp::ListAbi<RefPtr, stlp::CountedRef>;
using SharedEntryList = stlp::ListAbi<stlp::RefEntry, stlp::SharedEntry>;
using CountedEntryList = stlp::ListAbi<stlp::RefEntry, stlp::CountedEntry>;
using HeldObjectList = stlp::ListAbi<CEngineObject*, stlp::HeldObject>;
using CStringList = stlp::ListAbi<mfc::CString>;

template <std::uintptr_t A, typename Abi>
struct PlainInsertCheck {
    using List = typename Abi::List;
    using Node = typename List::Node;
    using T = decltype(Node::data);
    using Fn = Node** (__fastcall*)(List*, void*, Node**, Node*, const T*);
    static inline void* original = nullptr;
    static constexpr std::size_t kNode = sizeof(Node);

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos, const T* x) {
        Node* prev = pos->prev;
        Node* theirs = nullptr;
        reinterpret_cast<Fn>(original)(self, nullptr, &theirs, pos, x);
        bool theirsLinked = LinkedBetween(theirs, prev, pos);
        T expected;
        std::memcpy(&expected, &theirs->data, sizeof(T));
        prev->next = pos;
        pos->prev = prev;
        stlp::deallocate(theirs, kNode);
        Abi::Insert(self, nullptr, out, pos, x);
        Node* ours = *out;
        bool ok = theirsLinked && LinkedBetween(ours, prev, pos) && (kNode > 128 || ours == theirs) &&
                  std::memcmp(&ours->data, &expected, sizeof(T)) == 0;
        char detail[96] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p, pos %p", ours, theirs, pos);
        ei::verify::Report(Name<A>("stlp::list::insert"), ok, detail);
        return out;
    }
};
template <std::uintptr_t A> using PlainInsert4 = PlainInsertCheck<A, PlainList4>;
template <std::uintptr_t A> using PlainInsert16 = PlainInsertCheck<A, PlainList16>;
template <std::uintptr_t A> using PlainInsert20 = PlainInsertCheck<A, PlainList20>;
template <std::uintptr_t A> using PlainInsert28 = PlainInsertCheck<A, PlainList28>;
template <std::uintptr_t A> using PlainInsert260 = PlainInsertCheck<A, PlainList260>;

// Plain inserts checked ours first (SV2f): ours links its node in before `pos`, then the original
// links one in after it, which is compared, unlinked and freed.
template <std::uintptr_t A, typename Abi>
struct PlainInsertOursFirst {
    using List = typename Abi::List;
    using Node = typename List::Node;
    using T = decltype(Node::data);
    using InsertFn = Node** (__fastcall*)(List*, void*, Node**, Node*, const T*);
    using CreateFn = Node* (__fastcall*)(List*, void*, const T*);
    static inline void* original = nullptr;

    static Node** __fastcall Insert(List* self, void*, Node** out, Node* pos, const T* x) {
        Node* prev = pos->prev;
        T value;
        std::memcpy(&value, x, sizeof(T));
        Abi::Insert(self, nullptr, out, pos, x);
        Node* ours = *out;
        bool ok = LinkedBetween(ours, prev, pos) && std::memcmp(&ours->data, &value, sizeof(T)) == 0;
        Node* theirs = nullptr;
        reinterpret_cast<InsertFn>(original)(self, nullptr, &theirs, pos, &value);
        ok = ok && LinkedBetween(theirs, ours, pos) && std::memcmp(&theirs->data, &ours->data, sizeof(T)) == 0;
        ours->next = pos;
        pos->prev = ours;
        stlp::deallocate(theirs, sizeof(Node));
        ei::verify::Report(Name<A>("stlp::list::insert"), ok, ok ? "" : "another node or other bytes");
        return out;
    }
    static Node* __fastcall CreateNode(List* self, void*, const T* x) {
        Node* ours = Abi::CreateNode(self, nullptr, x);
        Node* theirs = reinterpret_cast<CreateFn>(original)(self, nullptr, x);
        const bool ok = std::memcmp(&theirs->data, &ours->data, sizeof(T)) == 0;
        stlp::deallocate(theirs, sizeof(Node));
        ei::verify::Report(Name<A>("stlp::list::_M_create_node"), ok, ok ? "" : "other bytes");
        return ours;
    }
};
// The client's screen entries (24 bytes, 32-byte nodes).
template <std::uintptr_t A> using ScreenEntryList = PlainInsertOursFirst<A, stlp::ListAbi<stlp::Opaque<24>>>;

// What an element policy's copy must have done: Expect(x) before the copy, Holds(copy, x, expected)
// after it.
struct SharedRefCopy {
    struct Expected {
        CRefObject* object;   // what the copy holds (null when the source is null or detached)
        bool clone;           // a copy-on-share source: the copy holds a clone
        std::int16_t refs;
    };
    static Expected Expect(const RefPtr& x) {
        CRefObject* o = x.ptr;
        if (!o || (o->flags7_ & CRefObject::kDetached)) return {nullptr, false, 0};
        return {o, (o->flags6_ & CRefObject::kCopyOnShare) != 0, o->refs_};
    }
    static bool Holds(const RefPtr& copy, const RefPtr&, const Expected& e) {
        if (!e.object) return copy.ptr == nullptr;
        if (!copy.ptr || !(copy.ptr->flags6_ & CRefObject::kCopyOnShare)) return false;
        return e.clone ? copy.ptr != e.object : copy.ptr == e.object && copy.ptr->refs_ == e.refs + 1;
    }
};
struct CountedRefCopy {
    using Expected = std::int16_t;
    static Expected Expect(const RefPtr& x) { return x.ptr ? x.ptr->refs_ : 0; }
    static bool Holds(const RefPtr& copy, const RefPtr& x, Expected refs) {
        return copy.ptr == x.ptr && (!x.ptr || x.ptr->refs_ == refs + 1);
    }
};
struct SharedEntryCopy {
    using Expected = SharedRefCopy::Expected;
    static Expected Expect(const stlp::RefEntry& x) { return SharedRefCopy::Expect(x.ref); }
    static bool Holds(const stlp::RefEntry& copy, const stlp::RefEntry& x, const Expected& e) {
        return copy.key == x.key && SharedRefCopy::Holds(copy.ref, x.ref, e);
    }
};
struct CountedEntryCopy {
    using Expected = std::int16_t;
    static Expected Expect(const stlp::RefEntry& x) { return CountedRefCopy::Expect(x.ref); }
    static bool Holds(const stlp::RefEntry& copy, const stlp::RefEntry& x, Expected refs) {
        return copy.key == x.key && CountedRefCopy::Holds(copy.ref, x.ref, refs);
    }
};
struct HeldObjectCopy {
    using Expected = int;
    static Expected Expect(CEngineObject* const& x) { return x ? x->refCount_ : 0; }
    static bool Holds(CEngineObject* const& copy, CEngineObject* const& x, Expected refs) {
        return copy == x && (!x || x->refCount_ == refs + 1);
    }
};
struct CStringCopy {
    using Expected = int;
    static Expected Expect(const mfc::CString&) { return 0; }
    static bool Holds(const mfc::CString& copy, const mfc::CString& x, Expected) { return std::strcmp(copy, x) == 0; }
};

template <std::uintptr_t A, typename Abi, typename Copy>
struct InsertCheck {
    using List = typename Abi::List;
    using Node = typename List::Node;
    using T = decltype(Node::data);

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos, const T* x) {
        Node* prev = pos->prev;
        auto expected = Copy::Expect(*x);
        Abi::Insert(self, nullptr, out, pos, x);
        bool ok = LinkedBetween(*out, prev, pos) && Copy::Holds((*out)->data, *x, expected);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "node %p, pos %p", *out, pos);
        ei::verify::Report(Name<A>("stlp::list::insert"), ok, detail);
        return out;
    }
    static void __fastcall PushBack(List* self, void*, const T* x) {
        Node* prev = self->head->prev;
        auto expected = Copy::Expect(*x);
        Abi::PushBack(self, nullptr, x);
        Node* node = self->head->prev;
        bool ok = node != prev && LinkedBetween(node, prev, self->head) && Copy::Holds(node->data, *x, expected);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "list %p, node %p", self, node);
        ei::verify::Report(Name<A>("stlp::list::push_back"), ok, detail);
    }
};
template <std::uintptr_t A> using SharedRefInsert = InsertCheck<A, SharedRefList, SharedRefCopy>;
template <std::uintptr_t A> using CountedRefInsert = InsertCheck<A, CountedRefList, CountedRefCopy>;
template <std::uintptr_t A> using SharedEntryInsert = InsertCheck<A, SharedEntryList, SharedEntryCopy>;
template <std::uintptr_t A> using CountedEntryInsert = InsertCheck<A, CountedEntryList, CountedEntryCopy>;
template <std::uintptr_t A> using HeldObjectInsert = InsertCheck<A, HeldObjectList, HeldObjectCopy>;
template <std::uintptr_t A> using CStringInsert = InsertCheck<A, CStringList, CStringCopy>;

template <std::uintptr_t A, typename Abi>
struct EraseCheck {
    using List = typename Abi::List;
    using Node = typename List::Node;
    static constexpr std::size_t kNode = sizeof(Node);

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos) {
        Node* prev = pos->prev;
        Node* next = pos->next;
        Abi::Erase(self, nullptr, out, pos);
        void** list = FreeList(kNode);
        bool ok = *out == next && prev->next == next && next->prev == prev && (!list || *list == pos);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "pos %p, returned %p, next %p", pos, *out, next);
        ei::verify::Report(Name<A>("stlp::list::erase"), ok, detail);
        return out;
    }
    static void __fastcall Clear(List* self) {
        Abi::Clear(self);
        bool ok = self->head->next == self->head && self->head->prev == self->head;
        ei::verify::Report(Name<A>("stlp::list::clear"), ok, ok ? "" : "the sentinel is not linked to itself");
    }
    static void __fastcall Destruct(List* self) {
        Node* sentinel = self->head;
        Abi::Destruct(self);
        void** list = FreeList(kNode);
        bool ok = !list || *list == sentinel;
        ei::verify::Report(Name<A>("stlp::list::~list"), ok, ok ? "" : "the sentinel is not on top of its free list");
    }
};
template <std::uintptr_t A> using SharedRefErase = EraseCheck<A, SharedRefList>;
template <std::uintptr_t A> using CountedRefErase = EraseCheck<A, CountedRefList>;
template <std::uintptr_t A> using SharedEntryErase = EraseCheck<A, SharedEntryList>;
template <std::uintptr_t A> using CountedEntryErase = EraseCheck<A, CountedEntryList>;
template <std::uintptr_t A> using CStringErase = EraseCheck<A, CStringList>;
template <std::uintptr_t A> using HeldObjectErase = EraseCheck<A, HeldObjectList>;
template <std::uintptr_t A> using PlainErase260 = EraseCheck<A, PlainList260>;
// By node size (S12d): 16, 24, 32 and 40 bytes.
template <std::uintptr_t A> using PlainEraseNode16 = EraseCheck<A, stlp::ListAbi<stlp::Opaque<8>>>;
template <std::uintptr_t A> using PlainEraseNode24 = EraseCheck<A, stlp::ListAbi<stlp::Opaque<16>>>;
template <std::uintptr_t A> using PlainEraseNode32 = EraseCheck<A, stlp::ListAbi<stlp::Opaque<24>>>;
template <std::uintptr_t A> using PlainEraseNode40 = EraseCheck<A, stlp::ListAbi<stlp::Opaque<32>>>;

// insert(pos, first, last) of reference holders (S12g): one new node per source element, linked in
// order between pos's old predecessor and pos, each holding what the policy's copy makes (the counts
// are not compared: an object may come more than once).
template <std::uintptr_t A, typename Abi, bool Shared>
struct RangeInsertCheck {
    using List = typename Abi::List;
    using Node = typename List::Node;

    static void __fastcall Verify(List* self, void*, Node* pos, const Node* first, const Node* last) {
        std::vector<SharedRefCopy::Expected> expected;
        for (const Node* n = first; n != last; n = n->next)
            expected.push_back(Shared ? SharedRefCopy::Expect(n->data) : SharedRefCopy::Expected{n->data.ptr, false, 0});
        Node* prev = pos->prev;
        Abi::InsertRange(self, nullptr, pos, first, last);
        bool ok = true;
        Node* node = prev;
        for (const SharedRefCopy::Expected& e : expected) {
            Node* next = node->next;
            ok = ok && next != pos && next->prev == node;
            if (!ok) break;
            CRefObject* copy = next->data.ptr;
            if (!e.object) ok = copy == nullptr;
            else if (e.clone) ok = copy && copy != e.object && (copy->flags6_ & CRefObject::kCopyOnShare);
            else ok = copy == e.object;
            node = next;
        }
        ok = ok && node->next == pos && pos->prev == node;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "%u elements before %p", static_cast<unsigned>(expected.size()), pos);
        ei::verify::Report(Name<A>("stlp::list::insert(pos, first, last)"), ok, detail);
    }
};

// remove(value): every node still linked both ways, none holding the object (unless `value` is one
// of the elements: then the original too compares later nodes with the erased node's bytes).
void __fastcall RemoveCountedVerify(stlp::list<RefPtr>* self, void*, const RefPtr* value) {
    using Node = stlp::list<RefPtr>::Node;
    bool aliased = false;
    for (Node* n = self->head->next; n != self->head; n = n->next) aliased |= &n->data == value;
    CRefObject* object = value->ptr;
    stlp::ListRemoveCounted(self, nullptr, value);
    bool ok = true;
    Node* prev = self->head;
    for (Node* n = self->head->next; ok; prev = n, n = n->next) {
        ok = n->prev == prev && (n == self->head || aliased || n->data.ptr != object);
        if (n == self->head) break;
    }
    ei::verify::Report("stlp::list::remove 00427320", ok, ok ? "" : "a node holds the object or is unlinked");
}

// remove(value) of a HeldObject list: every node still linked both ways, none the object (unless
// `value` is one of the elements).
void __fastcall RemoveHeldVerify(stlp::list<CEngineObject*>* self, void*, CEngineObject* const* value) {
    using Node = stlp::list<CEngineObject*>::Node;
    bool aliased = false;
    for (Node* n = self->head->next; n != self->head; n = n->next) aliased |= &n->data == value;
    CEngineObject* object = *value;
    stlp::ListRemoveHeld(self, nullptr, value);
    bool ok = true;
    Node* prev = self->head;
    for (Node* n = self->head->next; ok; prev = n, n = n->next) {
        ok = n->prev == prev && (n == self->head || aliased || n->data != object);
        if (n == self->head) break;
    }
    ei::verify::Report("stlp::list::remove 00570400", ok, ok ? "" : "a node holds the object or is unlinked");
}

// remove(value) of a list<int> (S16a): every node still linked both ways, none equal to the value
// (unless `value` is one of the elements).
void __fastcall RemoveIntVerify(stlp::list<int>* self, void*, const int* value) {
    using Node = stlp::list<int>::Node;
    bool aliased = false;
    for (Node* n = self->head->next; n != self->head; n = n->next) aliased |= &n->data == value;
    const int id = *value;
    stlp::ListRemoveInt(self, nullptr, value);
    bool ok = true;
    Node* prev = self->head;
    for (Node* n = self->head->next; ok; prev = n, n = n->next) {
        ok = n->prev == prev && (n == self->head || aliased || n->data != id);
        if (n == self->head) break;
    }
    ei::verify::Report("stlp::list::remove 00511FB0", ok, ok ? "" : "a node holds the value or is unlinked");
}

// for_each with the releasing functor: the nodes stay linked, the functor comes back (ours only:
// the releases cannot run twice).
char __stdcall ForEachReleaseVerify(stlp::list<CEngineObject*>::Node* first, stlp::list<CEngineObject*>::Node* last,
                                    char f) {
    const char result = stlp::ListForEachRelease(first, last, f);
    bool ok = result == f;
    for (stlp::list<CEngineObject*>::Node* n = first; ok && n != last; n = n->next) ok = n->next->prev == n;
    ei::verify::Report("stlp::for_each 00571780", ok, ok ? "" : "a node unlinked or another functor");
    return result;
}

} // namespace

// insert(pos, x) of plain data: mov eax,[esp+0xC]; push eax (5 bytes).
EI_VERIFY(0x00404A40, PlainInsert4<0x00404A40>::Verify, 5, PlainInsert4<0x00404A40>::original);
EI_VERIFY(0x0040AA10, PlainInsert4<0x0040AA10>::Verify, 5, PlainInsert4<0x0040AA10>::original);
EI_VERIFY(0x0056EA80, PlainInsert4<0x0056EA80>::Verify, 5, PlainInsert4<0x0056EA80>::original);
EI_VERIFY(0x0068C560, PlainInsert4<0x0068C560>::Verify, 5, PlainInsert4<0x0068C560>::original);
EI_VERIFY(0x0069C2F0, PlainInsert4<0x0069C2F0>::Verify, 5, PlainInsert4<0x0069C2F0>::original);
EI_VERIFY(0x0069C3D0, PlainInsert4<0x0069C3D0>::Verify, 5, PlainInsert4<0x0069C3D0>::original);
EI_VERIFY(0x00411630, PlainInsert16<0x00411630>::Verify, 5, PlainInsert16<0x00411630>::original);
EI_VERIFY(0x00421AF0, PlainInsert16<0x00421AF0>::Verify, 5, PlainInsert16<0x00421AF0>::original);
EI_VERIFY(0x00443800, PlainInsert28<0x00443800>::Verify, 5, PlainInsert28<0x00443800>::original);
// S16a: mov eax,[esp+0xC]; push eax (5 bytes) / push ecx; mov eax,[0079B230] (6 bytes).
EI_VERIFY(0x0056EAF0, ScreenEntryList<0x0056EAF0>::Insert, 5, ScreenEntryList<0x0056EAF0>::original);
EI_VERIFY(0x0056F7C0, ScreenEntryList<0x0056F7C0>::CreateNode, 6, ScreenEntryList<0x0056F7C0>::original);
EI_VERIFY(0x005392C0, PlainInsert20<0x005392C0>::Verify, 5, PlainInsert20<0x005392C0>::original);
EI_VERIFY(0x00439C50, PlainInsert260<0x00439C50>::Verify, 5, PlainInsert260<0x00439C50>::original);

// insert(pos, x) and push_back(x) of reference holders and CStrings: ours only.
EI_VERIFY(0x004221B0, SharedRefInsert<0x004221B0>::Verify, 0, s_unused);
EI_VERIFY(0x004223D0, SharedRefInsert<0x004223D0>::Verify, 0, s_unused);
EI_VERIFY(0x004225B0, SharedRefInsert<0x004225B0>::Verify, 0, s_unused);
EI_VERIFY(0x004227D0, SharedRefInsert<0x004227D0>::Verify, 0, s_unused);
EI_VERIFY(0x00422BB0, SharedRefInsert<0x00422BB0>::Verify, 0, s_unused);
EI_VERIFY(0x00424100, SharedRefInsert<0x00424100>::Verify, 0, s_unused);
EI_VERIFY(0x00439E30, SharedRefInsert<0x00439E30>::Verify, 0, s_unused);
EI_VERIFY(0x004437A0, SharedRefInsert<0x004437A0>::Verify, 0, s_unused);
EI_VERIFY(0x0047A450, SharedRefInsert<0x0047A450>::Verify, 0, s_unused);
EI_VERIFY(0x004229B0, CountedRefInsert<0x004229B0>::Verify, 0, s_unused);
EI_VERIFY(0x00423190, CountedRefInsert<0x00423190>::Verify, 0, s_unused);
EI_VERIFY(0x00423410, CountedRefInsert<0x00423410>::Verify, 0, s_unused);
EI_VERIFY(0x00423960, CountedRefInsert<0x00423960>::Verify, 0, s_unused);
EI_VERIFY(0x00423B50, CountedRefInsert<0x00423B50>::Verify, 0, s_unused);
EI_VERIFY(0x00423D40, CountedRefInsert<0x00423D40>::Verify, 0, s_unused);
EI_VERIFY(0x00423F20, CountedRefInsert<0x00423F20>::Verify, 0, s_unused);
EI_VERIFY(0x004242F0, CountedRefInsert<0x004242F0>::Verify, 0, s_unused);
EI_VERIFY(0x004244E0, CountedRefInsert<0x004244E0>::Verify, 0, s_unused);
EI_VERIFY(0x00424720, CountedRefInsert<0x00424720>::Verify, 0, s_unused);
EI_VERIFY(0x0047A480, CountedRefInsert<0x0047A480>::Verify, 0, s_unused);
EI_VERIFY(0x00426230, SharedEntryInsert<0x00426230>::Verify, 0, s_unused);
EI_VERIFY(0x0056E8A0, HeldObjectInsert<0x0056E8A0>::Verify, 0, s_unused);
EI_VERIFY(0x00572B00, HeldObjectInsert<0x00572B00>::Verify, 0, s_unused);
EI_VERIFY(0x0069C410, HeldObjectInsert<0x0069C410>::Verify, 0, s_unused);
EI_VERIFY(0x005F97E0, CStringInsert<0x005F97E0>::Verify, 0, s_unused);
EI_VERIFY(0x004207C0, SharedRefInsert<0x004207C0>::PushBack, 0, s_unused);
EI_VERIFY(0x00422F70, SharedEntryInsert<0x00422F70>::PushBack, 0, s_unused);
EI_VERIFY(0x004246C0, SharedEntryInsert<0x004246C0>::PushBack, 0, s_unused);
EI_VERIFY(0x00423690, CountedEntryInsert<0x00423690>::PushBack, 0, s_unused);
EI_VERIFY(0x0056DED0, HeldObjectInsert<0x0056DED0>::PushBack, 0, s_unused);
// S12g: insert(pos, first, last) of shared and counted references; ~list() of the client's servers
// and of the server's keyed references. Ours only.
EI_VERIFY(0x00560430, (RangeInsertCheck<0x00560430, SharedRefList, true>::Verify), 0, s_unused);
EI_VERIFY(0x00560460, (RangeInsertCheck<0x00560460, CountedRefList, false>::Verify), 0, s_unused);
EI_VERIFY(0x00421A50, SharedRefErase<0x00421A50>::Destruct, 0, s_unused);
EI_VERIFY(0x004238B0, CountedEntryErase<0x004238B0>::Destruct, 0, s_unused);

// erase(pos), clear(), ~list() and remove(value): ours only.
EI_VERIFY(0x00420290, SharedRefErase<0x00420290>::Verify, 0, s_unused);
EI_VERIFY(0x00420460, SharedRefErase<0x00420460>::Verify, 0, s_unused);
EI_VERIFY(0x004293B0, SharedRefErase<0x004293B0>::Verify, 0, s_unused);
EI_VERIFY(0x00429490, SharedRefErase<0x00429490>::Verify, 0, s_unused);
EI_VERIFY(0x00429650, SharedRefErase<0x00429650>::Verify, 0, s_unused);
EI_VERIFY(0x00438840, SharedRefErase<0x00438840>::Verify, 0, s_unused);
EI_VERIFY(0x004436D0, SharedRefErase<0x004436D0>::Verify, 0, s_unused);
EI_VERIFY(0x0047A380, SharedRefErase<0x0047A380>::Verify, 0, s_unused);
EI_VERIFY(0x004293F0, CountedRefErase<0x004293F0>::Verify, 0, s_unused);
EI_VERIFY(0x004294D0, CountedRefErase<0x004294D0>::Verify, 0, s_unused);
EI_VERIFY(0x00429550, CountedRefErase<0x00429550>::Verify, 0, s_unused);
EI_VERIFY(0x00429590, CountedRefErase<0x00429590>::Verify, 0, s_unused);
EI_VERIFY(0x004295D0, CountedRefErase<0x004295D0>::Verify, 0, s_unused);
EI_VERIFY(0x00429610, CountedRefErase<0x00429610>::Verify, 0, s_unused);
EI_VERIFY(0x00429690, CountedRefErase<0x00429690>::Verify, 0, s_unused);
EI_VERIFY(0x004296D0, CountedRefErase<0x004296D0>::Verify, 0, s_unused);
EI_VERIFY(0x00422FA0, SharedEntryErase<0x00422FA0>::Verify, 0, s_unused);
EI_VERIFY(0x004246E0, SharedEntryErase<0x004246E0>::Verify, 0, s_unused);
EI_VERIFY(0x004236C0, CountedEntryErase<0x004236C0>::Verify, 0, s_unused);
EI_VERIFY(0x005F8E50, CStringErase<0x005F8E50>::Verify, 0, s_unused);
EI_VERIFY(0x00438660, PlainErase260<0x00438660>::Verify, 0, s_unused);   // S12d: the node goes to operator delete
EI_VERIFY(0x0040AA40, PlainEraseNode16<0x0040AA40>::Verify, 0, s_unused);
EI_VERIFY(0x00512030, PlainEraseNode16<0x00512030>::Verify, 0, s_unused);
EI_VERIFY(0x0056DFB0, PlainEraseNode16<0x0056DFB0>::Verify, 0, s_unused);
EI_VERIFY(0x0040A920, PlainEraseNode24<0x0040A920>::Verify, 0, s_unused);
EI_VERIFY(0x00411540, PlainEraseNode24<0x00411540>::Verify, 0, s_unused);
EI_VERIFY(0x004437D0, PlainEraseNode24<0x004437D0>::Verify, 0, s_unused);
EI_VERIFY(0x005392F0, PlainEraseNode32<0x005392F0>::Verify, 0, s_unused);
EI_VERIFY(0x00443730, PlainEraseNode40<0x00443730>::Verify, 0, s_unused);
EI_VERIFY(0x00427100, SharedRefErase<0x00427100>::Clear, 0, s_unused);
EI_VERIFY(0x00427140, SharedRefErase<0x00427140>::Clear, 0, s_unused);
EI_VERIFY(0x00427180, SharedRefErase<0x00427180>::Clear, 0, s_unused);
EI_VERIFY(0x004271C0, SharedRefErase<0x004271C0>::Clear, 0, s_unused);
EI_VERIFY(0x00427260, SharedRefErase<0x00427260>::Clear, 0, s_unused);
EI_VERIFY(0x004272A0, SharedRefErase<0x004272A0>::Clear, 0, s_unused);
EI_VERIFY(0x00427480, SharedRefErase<0x00427480>::Clear, 0, s_unused);
EI_VERIFY(0x00427200, CountedRefErase<0x00427200>::Clear, 0, s_unused);
// S16: the client's sector grid cell lists.
EI_VERIFY(0x005703C0, HeldObjectErase<0x005703C0>::Clear, 0, s_unused);
EI_VERIFY(0x00573660, HeldObjectErase<0x00573660>::Clear, 0, s_unused);
EI_VERIFY(0x00572270, HeldObjectErase<0x00572270>::Verify, 0, s_unused);
EI_VERIFY(0x005755E0, HeldObjectErase<0x005755E0>::Verify, 0, s_unused);
EI_VERIFY(0x00572E90, HeldObjectErase<0x00572E90>::Destruct, 0, s_unused);
EI_VERIFY(0x0069BE70, HeldObjectErase<0x0069BE70>::Destruct, 0, s_unused);
EI_VERIFY(0x00427360, CountedRefErase<0x00427360>::Clear, 0, s_unused);
EI_VERIFY(0x004273A0, CountedRefErase<0x004273A0>::Clear, 0, s_unused);
EI_VERIFY(0x004273E0, CountedRefErase<0x004273E0>::Clear, 0, s_unused);
EI_VERIFY(0x00427420, CountedRefErase<0x00427420>::Clear, 0, s_unused);
EI_VERIFY(0x004274C0, CountedRefErase<0x004274C0>::Clear, 0, s_unused);
EI_VERIFY(0x00427500, CountedRefErase<0x00427500>::Clear, 0, s_unused);
EI_VERIFY(0x004275A0, CountedRefErase<0x004275A0>::Clear, 0, s_unused);
EI_VERIFY(0x004272E0, SharedEntryErase<0x004272E0>::Clear, 0, s_unused);
EI_VERIFY(0x00427560, SharedEntryErase<0x00427560>::Clear, 0, s_unused);
EI_VERIFY(0x00423370, CountedRefErase<0x00423370>::Destruct, 0, s_unused);
EI_VERIFY(0x004235F0, CountedRefErase<0x004235F0>::Destruct, 0, s_unused);
EI_VERIFY(0x00427320, RemoveCountedVerify, 0, s_unused);
EI_VERIFY(0x00570400, RemoveHeldVerify, 0, s_unused);
EI_VERIFY(0x00511FB0, RemoveIntVerify, 0, s_unused);
EI_VERIFY(0x00571780, ForEachReleaseVerify, 0, s_unused);
