// Checks for the STLport list<T> instantiations (src/lib/stlp_list.cpp).
//
// begin / end / distance compare with the original (pure). A constructor runs the original first,
// gives its sentinel back to the pool and then runs ours, which must take the same node from the
// same free list (the pool is LIFO, so the pool ends as if only ours had run) and link it to itself.
// The destructors of plain elements run ours only: the sentinel and the nodes must be on their free
// list afterwards, sentinel first, then the nodes from the last back to the first.
#include "verify.h"

#include "../../src/lib/stlp_list.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

void* s_unused;

using stlp::AnyList;
using Node = AnyList::Node;

using List16 = stlp::ListAbi<stlp::Opaque<8>>;
using List24 = stlp::ListAbi<stlp::Opaque<16>>;
using List40 = stlp::ListAbi<stlp::Opaque<32>>;
using List352 = stlp::ListAbi<stlp::Opaque<0x158>>;

// The node allocator's free list for blocks of `bytes` (16 lists of 8-byte steps from 0079B224);
// null above 128 bytes, which come from operator new.
void** FreeList(std::size_t bytes) {
    if (bytes > 128) return nullptr;
    return reinterpret_cast<void**>(0x0079B224 + 4 * ((bytes - 1) >> 3));
}

// One report name per instantiation ("stlp::list::end 004049D0"), so the counts show which are reached.
template <std::uintptr_t A>
const char* Name(const char* kind) {
    static char name[64];
    static const bool once = (std::snprintf(name, sizeof(name), "%s %08X", kind, static_cast<unsigned>(A)), true);
    (void)once;
    return name;
}

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
};
template <std::uintptr_t A> using Destruct16 = DestructCheck<A, List16>;
template <std::uintptr_t A> using Destruct24 = DestructCheck<A, List24>;
template <std::uintptr_t A> using Destruct40 = DestructCheck<A, List40>;

} // namespace

// begin(): mov eax,[ecx]; mov ecx,[eax]; mov eax,[esp+4] (8 bytes).
EI_VERIFY(0x0040A9F0, IteratorCheck<0x0040A9F0>::Begin, 8, IteratorCheck<0x0040A9F0>::original);
EI_VERIFY(0x0041FCC0, IteratorCheck<0x0041FCC0>::Begin, 8, IteratorCheck<0x0041FCC0>::original);
EI_VERIFY(0x00420280, IteratorCheck<0x00420280>::Begin, 8, IteratorCheck<0x00420280>::original);
EI_VERIFY(0x00420380, IteratorCheck<0x00420380>::Begin, 8, IteratorCheck<0x00420380>::original);
EI_VERIFY(0x004219E0, IteratorCheck<0x004219E0>::Begin, 8, IteratorCheck<0x004219E0>::original);
EI_VERIFY(0x00422B90, IteratorCheck<0x00422B90>::Begin, 8, IteratorCheck<0x00422B90>::original);

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

// Destructors of plain elements: ours only (stolen 0).
EI_VERIFY(0x004A96A0, Destruct16<0x004A96A0>::Verify, 0, s_unused);
EI_VERIFY(0x0069C830, Destruct16<0x0069C830>::Verify, 0, s_unused);
EI_VERIFY(0x00421CD0, Destruct24<0x00421CD0>::Verify, 0, s_unused);
EI_VERIFY(0x004A9650, Destruct24<0x004A9650>::Verify, 0, s_unused);
EI_VERIFY(0x00421D20, Destruct40<0x00421D20>::Verify, 0, s_unused);
