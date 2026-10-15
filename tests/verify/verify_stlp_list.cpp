// Checks for the STLport list<T> instantiations (src/lib/stlp_list.cpp).
//
// begin / end / distance and the iterator's copy and decrement compare with the original (pure).
// A splice runs the original on twins of both lists' nodes (links only) and ours on the lists. A constructor runs the original first,
// gives its sentinel back to the pool and then runs ours, which must take the same node from the
// same free list (the pool is LIFO, so the pool ends as if only ours had run) and link it to itself.
// The destructors of plain elements run ours only: the sentinel and the nodes must be on their free
// list afterwards, sentinel first, then the nodes from the last back to the first.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_list.h"

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::Name;
using stlp::AnyList;
using Node = AnyList::Node;

using List16 = stlp::ListAbi<stlp::Opaque<8>>;
using List24 = stlp::ListAbi<stlp::Opaque<16>>;
using List40 = stlp::ListAbi<stlp::Opaque<32>>;
using List352 = stlp::ListAbi<stlp::Opaque<0x158>>;
using List48 = stlp::ListAbi<stlp::Opaque<0x28>>;
using List268 = stlp::ListAbi<stlp::Opaque<0x104>>;
using List288 = stlp::ListAbi<stlp::Opaque<0x118>>;

template <std::uintptr_t A>
struct IteratorCheck {
    using Fn = Node** (__fastcall*)(const AnyList*, void*, Node**);
    static inline Fn original = nullptr;

    static Node** __fastcall Begin(const AnyList* self, void*, Node** out) {
        Node* expected = nullptr;
        original(self, nullptr, &expected);
        stlp::ListBegin(self, nullptr, out);
        ei::verify::Check(Name<A>("stlp::list::begin"), expected, *out, "list %p", self);
        return out;
    }
    static Node** __fastcall End(const AnyList* self, void*, Node** out) {
        Node* expected = nullptr;
        original(self, nullptr, &expected);
        stlp::ListEnd(self, nullptr, out);
        ei::verify::Check(Name<A>("stlp::list::end"), expected, *out, "list %p", self);
        return out;
    }
};

// The iterator's copy constructor and --it (S12d): pure, the original on a scratch iterator.
template <std::uintptr_t A>
struct IteratorStepCheck {
    static inline void* original = nullptr;

    static Node** __fastcall Copy(Node** self, void*, Node* const* from) {
        using Fn = Node** (__fastcall*)(Node**, void*, Node* const*);
        Node* theirs = nullptr;
        Node** theirResult = reinterpret_cast<Fn>(original)(&theirs, nullptr, from);
        Node** result = stlp::ListIteratorCopy(self, nullptr, from);
        ei::verify::Check(Name<A>("stlp::list::iterator copy"), theirs, *self, "returned %p (original's %p)", result,
                          theirResult);
        ei::verify::Report(Name<A>("stlp::list::iterator copy result"), result == self && theirResult == &theirs, "");
        return result;
    }
    static Node** __fastcall Decrement(Node** it) {
        using Fn = Node** (__fastcall*)(Node**);
        Node* theirs = *it;
        Node** theirResult = reinterpret_cast<Fn>(original)(&theirs);
        Node** result = stlp::ListIteratorDecrement(it);
        ei::verify::Check(Name<A>("stlp::list::iterator --"), theirs, *it, "");
        ei::verify::Report(Name<A>("stlp::list::iterator -- result"), result == it && theirResult == &theirs, "");
        return result;
    }
};

// splice (S12d): only links change, so the original runs on a twin of every node of both lists
// (next and prev mapped to the twins), ours on the lists; each node's links must then match its
// twin's.
template <std::uintptr_t A>
struct SpliceCheck {
    static inline void* original = nullptr;

    struct Twins {
        std::vector<Node*> real;
        std::vector<Node> twin;
        std::unordered_map<const Node*, int> index;

        explicit Twins(const AnyList* a, const AnyList* b) {
            Collect(a);
            if (b != a) Collect(b);
            twin.resize(real.size());
            for (std::size_t k = 0; k < real.size(); ++k) {
                twin[k].next = Map(real[k]->next);
                twin[k].prev = Map(real[k]->prev);
            }
        }
        void Collect(const AnyList* list) {
            Node* n = list->head;
            do {
                index.emplace(n, static_cast<int>(real.size()));
                real.push_back(n);
                n = n->next;
            } while (n != list->head && real.size() < 100000);
        }
        int Index(const Node* n) const {
            auto it = index.find(n);
            return it == index.end() ? -1 : it->second;
        }
        int TwinIndex(const Node* n) const {
            if (twin.empty() || n < twin.data() || n >= twin.data() + twin.size()) return -1;
            return static_cast<int>(n - twin.data());
        }
        Node* Map(Node* n) {
            int k = Index(n);
            return k < 0 ? n : &twin[static_cast<std::size_t>(k)];
        }
        // The first node whose links differ from its twin's, or -1.
        int Mismatch() const {
            for (std::size_t k = 0; k < real.size(); ++k)
                if (Index(real[k]->next) != TwinIndex(twin[k].next) || Index(real[k]->prev) != TwinIndex(twin[k].prev))
                    return static_cast<int>(k);
            return -1;
        }
    };

    static void __fastcall One(AnyList* self, void*, Node* pos, AnyList* x, Node* i) {
        using Fn = void(__fastcall*)(AnyList*, void*, Node*, AnyList*, Node*);
        Twins twins(self, x);
        if (twins.Index(pos) < 0 || twins.Index(i) < 0) {
            // Not nodes of the two lists: the original would link real nodes.
            stlp::ListSpliceOne(self, nullptr, pos, x, i);
            ei::verify::Report(Name<A>("stlp::list::splice(pos, x, i)"), false, "pos or i is not in the lists");
            return;
        }
        reinterpret_cast<Fn>(original)(self, nullptr, twins.Map(pos), x, twins.Map(i));
        stlp::ListSpliceOne(self, nullptr, pos, x, i);
        ei::verify::Check(Name<A>("stlp::list::splice(pos, x, i)"), -1, twins.Mismatch(), "%u nodes",
                          static_cast<unsigned>(twins.real.size()));
    }
    static void __fastcall Range(AnyList* self, void*, Node* pos, AnyList* x, Node* first, Node* last) {
        using Fn = void(__fastcall*)(AnyList*, void*, Node*, AnyList*, Node*, Node*);
        Twins twins(self, x);
        if (twins.Index(pos) < 0 || twins.Index(first) < 0 || twins.Index(last) < 0) {
            stlp::ListSpliceRange(self, nullptr, pos, x, first, last);
            ei::verify::Report(Name<A>("stlp::list::splice(pos, x, first, last)"), false, "pos, first or last is not in the lists");
            return;
        }
        reinterpret_cast<Fn>(original)(self, nullptr, twins.Map(pos), x, twins.Map(first), twins.Map(last));
        stlp::ListSpliceRange(self, nullptr, pos, x, first, last);
        ei::verify::Check(Name<A>("stlp::list::splice(pos, x, first, last)"), -1, twins.Mismatch(), "%u nodes",
                          static_cast<unsigned>(twins.real.size()));
    }
};

template <std::uintptr_t A>
struct DistanceCheck {
    using Fn = void(__fastcall*)(int*, void*, const Node*, const Node*);
    static inline Fn original = nullptr;

    static void __fastcall Verify(int* n, void*, const Node* first, const Node* last) {
        int expected = *n;
        original(&expected, nullptr, first, last);
        int before = *n;
        stlp::ListDistance(n, nullptr, first, last);
        ei::verify::Check(Name<A>("stlp::distance(list)"), expected, *n, "start %d, first %p, last %p", before,
                          first, last);
    }
};

template <std::uintptr_t A, typename Abi>
struct ConstructCheck {
    using List = typename Abi::List;
    using Fn = List*(__fastcall*)(List*, void*, const void*);
    using DefaultFn = List*(__fastcall*)(List*);
    static inline void* original = nullptr;
    static constexpr std::size_t kNode = sizeof(typename List::Node);

    static void Compare(const List* self, void* expected) {
        bool linked = self->head->next == self->head && self->head->prev == self->head;
        // Above 128 bytes the node comes from operator new, which need not hand the block back.
        bool same = kNode > 128 || self->head == expected;
        char detail[96] = "";
        if (!(linked && same))
            std::snprintf(detail, sizeof(detail), "sentinel %p (original's %p), next %p, prev %p", self->head,
                          expected, self->head->next, self->head->prev);
        ei::verify::Report(Name<A>("stlp::list::list"), linked && same, detail);
    }
    static List* __fastcall Verify(List* self, void*, const void* allocator) {
        List scratch;
        reinterpret_cast<Fn>(original)(&scratch, nullptr, allocator);
        stlp::deallocate(scratch.head, kNode);
        Abi::Construct(self, nullptr, allocator);
        Compare(self, scratch.head);
        return self;
    }
    static List* __fastcall VerifyDefault(List* self) {
        List scratch;
        reinterpret_cast<DefaultFn>(original)(&scratch);
        stlp::deallocate(scratch.head, kNode);
        Abi::ConstructDefault(self);
        Compare(self, scratch.head);
        return self;
    }
};
template <std::uintptr_t A> using Construct16 = ConstructCheck<A, List16>;
template <std::uintptr_t A> using Construct24 = ConstructCheck<A, List24>;
template <std::uintptr_t A> using Construct40 = ConstructCheck<A, List40>;
template <std::uintptr_t A> using Construct352 = ConstructCheck<A, List352>;
template <std::uintptr_t A> using Construct48 = ConstructCheck<A, List48>;
template <std::uintptr_t A> using Construct268 = ConstructCheck<A, List268>;
template <std::uintptr_t A> using Construct288 = ConstructCheck<A, List288>;

// What a destructor or clear() leaves on the node pool: `freed` (in the order freed) pushed on
// the free list for `bytes`, whose head was `top`.
bool PushedInOrder(std::size_t bytes, const std::vector<void*>& freed, void* top) {
    void** list = FreeList(bytes);
    if (!list) return true;
    void* p = *list;
    for (auto it = freed.rbegin(); it != freed.rend(); ++it) {
        if (p != *it) return false;
        p = *static_cast<void**>(p);
    }
    return p == top;
}

template <std::uintptr_t A, typename Abi>
struct DestructCheck {
    using List = typename Abi::List;
    static constexpr std::size_t kNode = sizeof(typename List::Node);

    static void __fastcall Verify(List* self) {
        std::vector<void*> freed;
        for (auto* n = self->head->next; n != self->head; n = n->next) freed.push_back(n);
        freed.push_back(self->head);
        void** list = FreeList(kNode);
        void* top = list ? *list : nullptr;
        Abi::Destruct(self);
        bool ok = PushedInOrder(kNode, freed, top);
        char detail[96] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "%u nodes, free list top %p", static_cast<unsigned>(freed.size()), top);
        ei::verify::Report(Name<A>("stlp::list::~list"), ok, detail);
    }
    // clear(): the nodes on their free list from the last back to the first, the sentinel linked to
    // itself.
    static void __fastcall Clear(List* self) {
        std::vector<void*> freed;
        for (auto* n = self->head->next; n != self->head; n = n->next) freed.push_back(n);
        void** list = FreeList(kNode);
        void* top = list ? *list : nullptr;
        Abi::Clear(self);
        bool ok = PushedInOrder(kNode, freed, top) && self->head->next == self->head && self->head->prev == self->head;
        char detail[96] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "%u nodes, free list top %p", static_cast<unsigned>(freed.size()), top);
        ei::verify::Report(Name<A>("stlp::list::clear"), ok, detail);
    }
};
template <std::uintptr_t A> using Destruct16 = DestructCheck<A, List16>;
template <std::uintptr_t A> using Destruct24 = DestructCheck<A, List24>;
template <std::uintptr_t A> using Destruct40 = DestructCheck<A, List40>;
template <std::uintptr_t A> using Destruct268 = DestructCheck<A, List268>;

} // namespace

// begin(): mov eax,[ecx]; mov ecx,[eax]; mov eax,[esp+4] (8 bytes).
EI_VERIFY(0x0040A9F0, IteratorCheck<0x0040A9F0>::Begin, 8, IteratorCheck<0x0040A9F0>::original);
EI_VERIFY(0x0041FCC0, IteratorCheck<0x0041FCC0>::Begin, 8, IteratorCheck<0x0041FCC0>::original);
EI_VERIFY(0x00420280, IteratorCheck<0x00420280>::Begin, 8, IteratorCheck<0x00420280>::original);
EI_VERIFY(0x00420380, IteratorCheck<0x00420380>::Begin, 8, IteratorCheck<0x00420380>::original);
EI_VERIFY(0x004219E0, IteratorCheck<0x004219E0>::Begin, 8, IteratorCheck<0x004219E0>::original);
EI_VERIFY(0x00422B90, IteratorCheck<0x00422B90>::Begin, 8, IteratorCheck<0x00422B90>::original);
EI_VERIFY(0x00438640, IteratorCheck<0x00438640>::Begin, 8, IteratorCheck<0x00438640>::original);
EI_VERIFY(0x00438820, IteratorCheck<0x00438820>::Begin, 8, IteratorCheck<0x00438820>::original);

// end(): mov eax,[esp+4]; mov ecx,[ecx] (6 bytes).
EI_VERIFY(0x004049D0, IteratorCheck<0x004049D0>::End, 6, IteratorCheck<0x004049D0>::original);
EI_VERIFY(0x0040AA00, IteratorCheck<0x0040AA00>::End, 6, IteratorCheck<0x0040AA00>::original);
EI_VERIFY(0x00411530, IteratorCheck<0x00411530>::End, 6, IteratorCheck<0x00411530>::original);
EI_VERIFY(0x0041FCD0, IteratorCheck<0x0041FCD0>::End, 6, IteratorCheck<0x0041FCD0>::original);
EI_VERIFY(0x00420390, IteratorCheck<0x00420390>::End, 6, IteratorCheck<0x00420390>::original);
EI_VERIFY(0x00420450, IteratorCheck<0x00420450>::End, 6, IteratorCheck<0x00420450>::original);
EI_VERIFY(0x00420550, IteratorCheck<0x00420550>::End, 6, IteratorCheck<0x00420550>::original);
EI_VERIFY(0x00420610, IteratorCheck<0x00420610>::End, 6, IteratorCheck<0x00420610>::original);
EI_VERIFY(0x004206E0, IteratorCheck<0x004206E0>::End, 6, IteratorCheck<0x004206E0>::original);
EI_VERIFY(0x004208A0, IteratorCheck<0x004208A0>::End, 6, IteratorCheck<0x004208A0>::original);
EI_VERIFY(0x00420960, IteratorCheck<0x00420960>::End, 6, IteratorCheck<0x00420960>::original);
EI_VERIFY(0x00420C30, IteratorCheck<0x00420C30>::End, 6, IteratorCheck<0x00420C30>::original);
EI_VERIFY(0x00420CF0, IteratorCheck<0x00420CF0>::End, 6, IteratorCheck<0x00420CF0>::original);
EI_VERIFY(0x004210C0, IteratorCheck<0x004210C0>::End, 6, IteratorCheck<0x004210C0>::original);
EI_VERIFY(0x004210E0, IteratorCheck<0x004210E0>::End, 6, IteratorCheck<0x004210E0>::original);
EI_VERIFY(0x004219F0, IteratorCheck<0x004219F0>::End, 6, IteratorCheck<0x004219F0>::original);
EI_VERIFY(0x00422BA0, IteratorCheck<0x00422BA0>::End, 6, IteratorCheck<0x00422BA0>::original);
EI_VERIFY(0x00423950, IteratorCheck<0x00423950>::End, 6, IteratorCheck<0x00423950>::original);
EI_VERIFY(0x00423B40, IteratorCheck<0x00423B40>::End, 6, IteratorCheck<0x00423B40>::original);
EI_VERIFY(0x00423D30, IteratorCheck<0x00423D30>::End, 6, IteratorCheck<0x00423D30>::original);
EI_VERIFY(0x004242E0, IteratorCheck<0x004242E0>::End, 6, IteratorCheck<0x004242E0>::original);
EI_VERIFY(0x004244D0, IteratorCheck<0x004244D0>::End, 6, IteratorCheck<0x004244D0>::original);
EI_VERIFY(0x004436C0, IteratorCheck<0x004436C0>::End, 6, IteratorCheck<0x004436C0>::original);
EI_VERIFY(0x0068C500, IteratorCheck<0x0068C500>::End, 6, IteratorCheck<0x0068C500>::original);
EI_VERIFY(0x0069BE60, IteratorCheck<0x0069BE60>::End, 6, IteratorCheck<0x0069BE60>::original);
EI_VERIFY(0x0069C2E0, IteratorCheck<0x0069C2E0>::End, 6, IteratorCheck<0x0069C2E0>::original);
EI_VERIFY(0x00437F20, IteratorCheck<0x00437F20>::End, 6, IteratorCheck<0x00437F20>::original);
EI_VERIFY(0x00438650, IteratorCheck<0x00438650>::End, 6, IteratorCheck<0x00438650>::original);
EI_VERIFY(0x00438830, IteratorCheck<0x00438830>::End, 6, IteratorCheck<0x00438830>::original);

// The iterator's copy (MOV EAX,ECX; MOV ECX,[ESP+4]: 6 bytes) and --it (MOV EAX,ECX; MOV ECX,[EAX];
// MOV EDX,[ECX+4]: 7 bytes); the splices (two MOV r,[ESP+n]: 8 bytes).
EI_VERIFY(0x004387C0, IteratorStepCheck<0x004387C0>::Copy, 6, IteratorStepCheck<0x004387C0>::original);
EI_VERIFY(0x00439A60, IteratorStepCheck<0x00439A60>::Decrement, 7, IteratorStepCheck<0x00439A60>::original);
EI_VERIFY(0x004380C0, SpliceCheck<0x004380C0>::One, 8, SpliceCheck<0x004380C0>::original);
EI_VERIFY(0x00438770, SpliceCheck<0x00438770>::Range, 8, SpliceCheck<0x00438770>::original);

// distance(): mov eax,[esp+4]; mov edx,[esp+8] (8 bytes).
EI_VERIFY(0x00429040, DistanceCheck<0x00429040>::Verify, 8, DistanceCheck<0x00429040>::original);
EI_VERIFY(0x004290D0, DistanceCheck<0x004290D0>::Verify, 8, DistanceCheck<0x004290D0>::original);
EI_VERIFY(0x004290F0, DistanceCheck<0x004290F0>::Verify, 8, DistanceCheck<0x004290F0>::original);
EI_VERIFY(0x00429110, DistanceCheck<0x00429110>::Verify, 8, DistanceCheck<0x00429110>::original);

// Constructors: push -1; push <handler> (7 bytes).
EI_VERIFY(0x00404A70, Construct16<0x00404A70>::Verify, 7, Construct16<0x00404A70>::original);
EI_VERIFY(0x00411850, Construct16<0x00411850>::Verify, 7, Construct16<0x00411850>::original);
EI_VERIFY(0x0041F8B0, Construct16<0x0041F8B0>::Verify, 7, Construct16<0x0041F8B0>::original);
EI_VERIFY(0x00420F10, Construct16<0x00420F10>::Verify, 7, Construct16<0x00420F10>::original);
EI_VERIFY(0x004221E0, Construct16<0x004221E0>::Verify, 7, Construct16<0x004221E0>::original);
EI_VERIFY(0x00422400, Construct16<0x00422400>::Verify, 7, Construct16<0x00422400>::original);
EI_VERIFY(0x004225E0, Construct16<0x004225E0>::Verify, 7, Construct16<0x004225E0>::original);
EI_VERIFY(0x00422800, Construct16<0x00422800>::Verify, 7, Construct16<0x00422800>::original);
EI_VERIFY(0x004229E0, Construct16<0x004229E0>::Verify, 7, Construct16<0x004229E0>::original);
EI_VERIFY(0x00422BE0, Construct16<0x00422BE0>::Verify, 7, Construct16<0x00422BE0>::original);
EI_VERIFY(0x00422DC0, Construct16<0x00422DC0>::Verify, 7, Construct16<0x00422DC0>::original);
EI_VERIFY(0x00422FE0, Construct16<0x00422FE0>::Verify, 7, Construct16<0x00422FE0>::original);
EI_VERIFY(0x004231C0, Construct16<0x004231C0>::Verify, 7, Construct16<0x004231C0>::original);
EI_VERIFY(0x00423440, Construct16<0x00423440>::Verify, 7, Construct16<0x00423440>::original);
EI_VERIFY(0x00423700, Construct16<0x00423700>::Verify, 7, Construct16<0x00423700>::original);
EI_VERIFY(0x00423990, Construct16<0x00423990>::Verify, 7, Construct16<0x00423990>::original);
EI_VERIFY(0x00423B80, Construct16<0x00423B80>::Verify, 7, Construct16<0x00423B80>::original);
EI_VERIFY(0x00423D70, Construct16<0x00423D70>::Verify, 7, Construct16<0x00423D70>::original);
EI_VERIFY(0x00423F50, Construct16<0x00423F50>::Verify, 7, Construct16<0x00423F50>::original);
EI_VERIFY(0x00424130, Construct16<0x00424130>::Verify, 7, Construct16<0x00424130>::original);
EI_VERIFY(0x00424320, Construct16<0x00424320>::Verify, 7, Construct16<0x00424320>::original);
EI_VERIFY(0x00424510, Construct16<0x00424510>::Verify, 7, Construct16<0x00424510>::original);
EI_VERIFY(0x00424750, Construct16<0x00424750>::Verify, 7, Construct16<0x00424750>::original);
EI_VERIFY(0x00463AA0, Construct16<0x00463AA0>::Verify, 7, Construct16<0x00463AA0>::original);
EI_VERIFY(0x005F9A00, Construct16<0x005F9A00>::Verify, 7, Construct16<0x005F9A00>::original);
EI_VERIFY(0x0069BF70, Construct16<0x0069BF70>::Verify, 7, Construct16<0x0069BF70>::original);
EI_VERIFY(0x0069C440, Construct16<0x0069C440>::Verify, 7, Construct16<0x0069C440>::original);
EI_VERIFY(0x0069C680, Construct16<0x0069C680>::Verify, 7, Construct16<0x0069C680>::original);
EI_VERIFY(0x00411660, Construct24<0x00411660>::Verify, 7, Construct24<0x00411660>::original);
EI_VERIFY(0x0041FB10, Construct24<0x0041FB10>::Verify, 7, Construct24<0x0041FB10>::original);
EI_VERIFY(0x00421B20, Construct24<0x00421B20>::Verify, 7, Construct24<0x00421B20>::original);
EI_VERIFY(0x00439120, Construct24<0x00439120>::Verify, 7, Construct24<0x00439120>::original);
EI_VERIFY(0x004A94A0, Construct24<0x004A94A0>::Verify, 7, Construct24<0x004A94A0>::original);
EI_VERIFY(0x005F9300, Construct24<0x005F9300>::Verify, 7, Construct24<0x005F9300>::original);
EI_VERIFY(0x00634300, Construct24<0x00634300>::Verify, 7, Construct24<0x00634300>::original);
EI_VERIFY(0x00562A80, Construct24<0x00562A80>::VerifyDefault, 7, Construct24<0x00562A80>::original);
EI_VERIFY(0x0041FD30, Construct40<0x0041FD30>::Verify, 7, Construct40<0x0041FD30>::original);
EI_VERIFY(0x004249E0, Construct352<0x004249E0>::Verify, 7, Construct352<0x004249E0>::original);
EI_VERIFY(0x00478D50, Construct48<0x00478D50>::Verify, 7, Construct48<0x00478D50>::original);
EI_VERIFY(0x00479150, Construct48<0x00479150>::Verify, 7, Construct48<0x00479150>::original);
EI_VERIFY(0x005F90E0, Construct48<0x005F90E0>::Verify, 7, Construct48<0x005F90E0>::original);
EI_VERIFY(0x00439CE0, Construct268<0x00439CE0>::Verify, 7, Construct268<0x00439CE0>::original);
EI_VERIFY(0x00439960, Construct288<0x00439960>::Verify, 7, Construct288<0x00439960>::original);

// Destructors of plain elements: ours only (stolen 0).
EI_VERIFY(0x004A96A0, Destruct16<0x004A96A0>::Verify, 0, s_unused);
EI_VERIFY(0x0069C830, Destruct16<0x0069C830>::Verify, 0, s_unused);
EI_VERIFY(0x00421CD0, Destruct24<0x00421CD0>::Verify, 0, s_unused);
EI_VERIFY(0x004A9650, Destruct24<0x004A9650>::Verify, 0, s_unused);
EI_VERIFY(0x00421D20, Destruct40<0x00421D20>::Verify, 0, s_unused);
EI_VERIFY(0x00439D40, Destruct268<0x00439D40>::Verify, 0, s_unused);
EI_VERIFY(0x0043D420, Destruct268<0x0043D420>::Clear, 0, s_unused);
// clear() of plain elements (S12d): ours only.
EI_VERIFY(0x00405500, Destruct16<0x00405500>::Clear, 0, s_unused);
EI_VERIFY(0x0040B390, Destruct16<0x0040B390>::Clear, 0, s_unused);
EI_VERIFY(0x0040B290, Destruct24<0x0040B290>::Clear, 0, s_unused);
