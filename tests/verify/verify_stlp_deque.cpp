// Checks for the STLport deque instantiations (src/lib/stlp_deque.cpp).
//
// The camera's deque is built by a static initialiser (before the hooks) and destroyed at exit, the
// view stack's push (00410E70) is dead code and the material's push and destroy are rewritten
// inline, so in a run only the camera's _Destroy and the constructors and destructors of materials
// the original still builds are reached. Hence a start-up check: for both instantiations a deque
// built, partly emptied from the front and filled again through the original _M_initialize_map,
// _M_reallocate_map (both the recentring and the new-map path), _M_push_back_aux and destructor,
// next to one through ours; after every step the same shape (map size, where the
// start and finish nodes sit in the map, their offsets) and the same elements.
//
// Per call: deque(alloc, n) and _M_initialize_map run the original on a scratch deque first and
// free it again (nodes, then the map on top of its free list), then ours: the same map block, map
// size and shape. The rest run ours only and check the state left: _M_create_nodes, ~deque (the
// map on top of its free list), _M_push_back_aux, _M_reallocate_map (the same node pointers in
// order, room for the added ones), the iterator copy and the destroy helpers (nothing happens).
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_deque.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::Name;

using ViewStack = stlp::DequeAbi<stlp::Opaque<0x24>>;
using MaterialEntries = stlp::DequeAbi<stlp::Opaque<0x10>>;

template <typename Deque>
std::ptrdiff_t NodeIndex(const Deque& d, const typename Deque::Iterator& it) {
    return it.node - d.Map();
}

// The deque's shape: map size, where the start and finish nodes sit in it, the start's and
// finish's offsets in their nodes, and each iterator's node range.
template <typename Deque>
bool SameShape(const Deque& a, const Deque& b) {
    auto inNode = [](const typename Deque::Iterator& it) {
        return it.first == *it.node && it.last == it.first + Deque::kPerNode;
    };
    return a.MapSize() == b.MapSize() && NodeIndex(a, a.Start()) == NodeIndex(b, b.Start()) &&
           NodeIndex(a, a.Finish()) == NodeIndex(b, b.Finish()) &&
           a.Start().cur - a.Start().first == b.Start().cur - b.Start().first &&
           a.Finish().cur - a.Finish().first == b.Finish().cur - b.Finish().first && inNode(a.Start()) &&
           inNode(a.Finish()) && inNode(b.Start()) && inNode(b.Finish());
}

template <typename Deque>
bool NodesSet(const Deque& d) {
    for (auto** p = d.Start().node; p <= d.Finish().node; ++p)
        if (!*p) return false;
    return true;
}

// Every element from start to finish, in order.
template <typename Deque, typename F>
void ForEach(const Deque& d, F f) {
    auto it = d.Start();
    while (it.cur != d.Finish().cur) {
        f(*it.cur);
        if (++it.cur == it.last) it.SetNode(it.node + 1), it.cur = it.first;
    }
}

template <typename Deque>
bool SameElements(const Deque& a, const Deque& b) {
    using T = std::remove_reference_t<decltype(**a.Map())>;
    std::vector<unsigned char> x, y;
    ForEach(a, [&](const T& e) { x.insert(x.end(), reinterpret_cast<const unsigned char*>(&e), reinterpret_cast<const unsigned char*>(&e) + sizeof(T)); });
    ForEach(b, [&](const T& e) { y.insert(y.end(), reinterpret_cast<const unsigned char*>(&e), reinterpret_cast<const unsigned char*>(&e) + sizeof(T)); });
    return x == y;
}

template <typename Abi>
struct Originals {
    using Deque = typename Abi::Deque;
    static inline void* construct = nullptr;
    static inline void* initializeMap = nullptr;
    static inline void* destruct = nullptr;
    static inline void* pushBackAux = nullptr;
    static inline void* reallocateMap = nullptr;
};

template <std::uintptr_t A, typename Abi>
struct DequeCheck {
    using Deque = typename Abi::Deque;
    using T = std::remove_reference_t<decltype(**std::declval<Deque&>().Map())>;
    using O = Originals<Abi>;

    // Runs the original builder `build` on a scratch deque and frees it again, then ours; the same
    // map block (when pooled) and shape.
    template <typename Build, typename Ours>
    static void AgainstScratch(const char* name, Deque* self, std::size_t n, Build build, Ours ours) {
        alignas(Deque) unsigned char raw[sizeof(Deque)] = {};
        auto* scratch = reinterpret_cast<Deque*>(raw);
        build(scratch);
        void* theirMap = scratch->Map();
        const bool pooled = FreeList(scratch->MapSize() * sizeof(T*)) != nullptr;
        Deque expected;
        std::memcpy(&expected, scratch, sizeof(Deque));
        scratch->Destroy();
        ours();
        const bool ok = SameShape(expected, *self) && NodesSet(*self) && (!pooled || self->Map() == theirMap) &&
                        self->size() == n;
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "n %u: map %p / %p, size %u / %u", static_cast<unsigned>(n), theirMap,
                          static_cast<void*>(self->Map()), static_cast<unsigned>(expected.MapSize()),
                          static_cast<unsigned>(self->MapSize()));
        ei::verify::Report(name, ok, detail);
    }

    static Deque* __fastcall Construct(Deque* self, void*, const void* alloc, std::size_t n) {
        Deque* result = nullptr;
        AgainstScratch(
            Name<A>("stlp::deque::deque"), self, n,
            [&](Deque* d) { reinterpret_cast<decltype(&Abi::Construct)>(O::construct)(d, nullptr, alloc, n); },
            [&] { result = Abi::Construct(self, nullptr, alloc, n); });
        return result;
    }

    static void __fastcall InitializeMap(Deque* self, void*, std::size_t n) {
        AgainstScratch(
            Name<A>("stlp::deque::_M_initialize_map"), self, n,
            [&](Deque* d) { reinterpret_cast<decltype(&Abi::InitializeMap)>(O::initializeMap)(d, nullptr, n); },
            [&] { Abi::InitializeMap(self, nullptr, n); });
    }

    // _M_create_nodes(nstart, nfinish): every slot gets a node.
    static void __fastcall CreateNodes(Deque* self, void*, T** nstart, T** nfinish) {
        Abi::CreateNodes(self, nullptr, nstart, nfinish);
        bool ok = true;
        for (T** p = nstart; p < nfinish; ++p) ok = ok && *p;
        ei::verify::Report(Name<A>("stlp::deque::_M_create_nodes"), ok, ok ? "" : "null node");
    }

    // ~deque: the map is the last block freed (on top of its free list when pooled).
    static void __fastcall Destruct(Deque* self) {
        void* map = self->Map();
        void** list = map ? FreeList(self->MapSize() * sizeof(T*)) : nullptr;
        Abi::Destruct(self);
        const bool ok = !list || *list == map;
        ei::verify::Report(Name<A>("stlp::deque::~deque"), ok, ok ? "" : "map not on top of its free list");
    }

    // _M_push_back_aux(x): one more element, x at the back, the finish at a new node's start.
    static void __fastcall PushBackAux(Deque* self, void*, const T* x) {
        const T value = *x;
        const std::size_t before = self->size();
        Abi::PushBackAux(self, nullptr, x);
        const bool ok = self->size() == before + 1 && self->Finish().cur == self->Finish().first && NodesSet(*self) &&
                        std::memcmp(&self->back(), &value, sizeof(T)) == 0;
        ei::verify::Report(Name<A>("stlp::deque::_M_push_back_aux"), ok, ok ? "" : "state");
    }

    // _M_reallocate_map(add, atFront): the node pointers kept in order, with `add` free slots on
    // the side asked for; the iterators' cur pointers untouched.
    static void __fastcall ReallocateMap(Deque* self, void*, std::size_t add, std::uint32_t atFront) {
        const std::vector<T*> nodes(self->Start().node, self->Finish().node + 1);
        const auto startOffset = self->Start().cur - self->Start().first;
        const auto finishOffset = self->Finish().cur - self->Finish().first;
        Abi::ReallocateMap(self, nullptr, add, atFront);
        const bool front = static_cast<std::uint8_t>(atFront) != 0;
        const std::size_t room = front ? static_cast<std::size_t>(NodeIndex(*self, self->Start()))
                                       : self->MapSize() - 1 - static_cast<std::size_t>(NodeIndex(*self, self->Finish()));
        const bool ok = std::vector<T*>(self->Start().node, self->Finish().node + 1) == nodes && room >= add &&
                        self->Start().first == nodes.front() && self->Finish().first == nodes.back() &&
                        self->Start().cur - self->Start().first == startOffset &&
                        self->Finish().cur - self->Finish().first == finishOffset;
        ei::verify::Report(Name<A>("stlp::deque::_M_reallocate_map"), ok, ok ? "" : "nodes or room");
    }
};

// The iterator copy: four words, returns this.
template <std::uintptr_t A>
stlp::AnyDequeIterator* __fastcall IteratorCopy(stlp::AnyDequeIterator* self, void*, const stlp::AnyDequeIterator* from) {
    const stlp::AnyDequeIterator source = *from;
    stlp::AnyDequeIterator* result = stlp::DequeIteratorCopy(self, nullptr, from);
    const bool ok = result == self && std::memcmp(self, &source, sizeof(source)) == 0;
    ei::verify::Report(Name<A>("stlp::_Deque_iterator copy"), ok, ok ? "" : "words");
    return result;
}

// The destroy helpers: nothing is destroyed; the range must be ordered.
template <std::uintptr_t A>
void __stdcall DestroyRange(stlp::AnyDequeIterator first, stlp::AnyDequeIterator last) {
    stlp::DequeDestroyRange(first, last);
    ei::verify::Report(Name<A>("stlp::_Destroy (deque)"), first.node <= last.node, "range");
}
template <std::uintptr_t A>
void __stdcall DestroyRangeAux(stlp::AnyDequeIterator first, stlp::AnyDequeIterator last, const void* tag) {
    stlp::DequeDestroyRangeAux(first, last, tag);
    ei::verify::Report(Name<A>("stlp::__destroy_aux (deque)"), first.node <= last.node, "range");
}

template <std::uintptr_t A> using View = DequeCheck<A, ViewStack>;
template <std::uintptr_t A> using Material = DequeCheck<A, MaterialEntries>;

// --- Start-up check ----------------------------------------------------------------------------

template <typename Abi>
void Battery(const char* name) {
    using Deque = typename Abi::Deque;
    using T = std::remove_reference_t<decltype(**std::declval<Deque&>().Map())>;
    using O = Originals<Abi>;
    // The originals call the hooked members inside them (the constructor _M_initialize_map, the
    // push _M_reallocate_map), so each is called directly: the words zeroed and
    // _M_initialize_map, the map made room for before _M_push_back_aux.
    auto initializeMap = reinterpret_cast<decltype(&Abi::InitializeMap)>(O::initializeMap);
    auto destruct = reinterpret_cast<decltype(&Abi::Destruct)>(O::destruct);
    auto pushBackAux = reinterpret_cast<decltype(&Abi::PushBackAux)>(O::pushBackAux);
    auto reallocateMap = reinterpret_cast<decltype(&Abi::ReallocateMap)>(O::reallocateMap);
    if (!initializeMap || !destruct || !pushBackAux || !reallocateMap) return;
    unsigned counter = 0;
    auto value = [&] {
        T v;
        for (std::size_t i = 0; i < sizeof(T); ++i) reinterpret_cast<unsigned char*>(&v)[i] = static_cast<unsigned char>(counter * 7 + i);
        ++counter;
        return v;
    };
    // The original's push_back: the inline part, then its _M_push_back_aux.
    auto theirPush = [&](Deque* d, const T& v) {
        if (d->Finish().cur != d->Finish().last - 1) {
            d->push_back(v);
            return;
        }
        if (d->MapSize() - static_cast<std::size_t>(d->Finish().node - d->Map()) < 2) reallocateMap(d, nullptr, 1, 0);
        pushBackAux(d, nullptr, &v);
    };
    for (std::size_t n : {0u, 1u, 13u, 14u, 15u, 31u, 32u, 33u, 100u, 447u, 448u, 1000u}) {
        for (std::size_t dropNodes : {0u, 1u, 3u, 8u}) {
            for (std::size_t pushes : {1u, 40u, 400u, 3000u}) {
                alignas(Deque) unsigned char rawA[sizeof(Deque)] = {}, rawB[sizeof(Deque)] = {};
                auto* a = reinterpret_cast<Deque*>(rawA);
                auto* b = reinterpret_cast<Deque*>(rawB);
                initializeMap(a, nullptr, n);
                Abi::Construct(b, nullptr, nullptr, n);
                bool ok = SameShape(*a, *b);
                // Emptied from the front (whole nodes), so later pushes recentre the map in place.
                const std::size_t drop = dropNodes * Deque::kPerNode < n ? dropNodes * Deque::kPerNode : n;
                for (std::size_t i = 0; i < drop; ++i) {
                    a->pop_front();
                    b->pop_front();
                }
                ok = ok && SameShape(*a, *b);
                // The constructor leaves its n elements uninitialised: the same bytes in both.
                auto fill = [](const T& e) { std::memset(const_cast<T*>(&e), 0x5A, sizeof(T)); };
                ForEach(*a, fill);
                ForEach(*b, fill);
                for (std::size_t i = 0; i < pushes && ok; ++i) {
                    const T v = value();
                    theirPush(a, v);
                    b->push_back(v);
                    ok = SameShape(*a, *b) && a->size() == b->size() && std::memcmp(&a->back(), &b->back(), sizeof(T)) == 0;
                }
                ok = ok && SameElements(*a, *b);
                char detail[128] = "";
                if (!ok)
                    std::snprintf(detail, sizeof(detail), "n %u, %u dropped, %u pushes: map %u / %u, start %d / %d, finish %d / %d",
                                  static_cast<unsigned>(n), static_cast<unsigned>(drop), static_cast<unsigned>(pushes),
                                  static_cast<unsigned>(a->MapSize()), static_cast<unsigned>(b->MapSize()),
                                  static_cast<int>(NodeIndex(*a, a->Start())), static_cast<int>(NodeIndex(*b, b->Start())),
                                  static_cast<int>(NodeIndex(*a, a->Finish())), static_cast<int>(NodeIndex(*b, b->Finish())));
                ei::verify::Report(name, ok, detail);
                destruct(a);
                b->Destroy();
            }
        }
    }
}

void DequeStartupCheck() {
    Battery<ViewStack>("stlp::deque start-up (view stack)");
    Battery<MaterialEntries>("stlp::deque start-up (material)");
}
EI_STARTUP_CHECK(DequeStartupCheck);

} // namespace

// The camera's view stack. Prologues: PUSH -1, PUSH imm32 (7); PUSH EBP, MOV EBP,ESP, PUSH -1,
// PUSH imm32 (10); SUB ESP,0x24, PUSH EBX, PUSH ESI (5); PUSH ECX/EBX/EBP/ESI, MOV ESI,ECX (6).
EI_VERIFY(0x00411AB0, View<0x00411AB0>::Construct, 7, Originals<ViewStack>::construct);
EI_VERIFY(0x00411C00, View<0x00411C00>::InitializeMap, 10, Originals<ViewStack>::initializeMap);
EI_VERIFY(0x00411EE0, View<0x00411EE0>::CreateNodes, 0, s_unused);
EI_VERIFY(0x0040CFB0, View<0x0040CFB0>::Destruct, 7, Originals<ViewStack>::destruct);
EI_VERIFY(0x00411D60, View<0x00411D60>::PushBackAux, 5, Originals<ViewStack>::pushBackAux);
EI_VERIFY(0x00411F70, View<0x00411F70>::ReallocateMap, 6, Originals<ViewStack>::reallocateMap);
EI_VERIFY(0x00411810, IteratorCopy<0x00411810>, 0, s_unused);
EI_VERIFY(0x00411DF0, DestroyRange<0x00411DF0>, 0, s_unused);
EI_VERIFY(0x00412140, DestroyRange<0x00412140>, 0, s_unused);
EI_VERIFY(0x00412190, DestroyRangeAux<0x00412190>, 0, s_unused);

// The material's entries. 0051E840: SUB ESP,0x10, MOV EAX,[ESP+0x14] (7).
EI_VERIFY(0x0068E0B0, Material<0x0068E0B0>::Construct, 7, Originals<MaterialEntries>::construct);
EI_VERIFY(0x0068E1C0, Material<0x0068E1C0>::InitializeMap, 10, Originals<MaterialEntries>::initializeMap);
EI_VERIFY(0x0068E320, Material<0x0068E320>::CreateNodes, 0, s_unused);
EI_VERIFY(0x0068DD40, Material<0x0068DD40>::Destruct, 7, Originals<MaterialEntries>::destruct);
EI_VERIFY(0x0051E840, Material<0x0051E840>::PushBackAux, 7, Originals<MaterialEntries>::pushBackAux);
EI_VERIFY(0x004A39C0, Material<0x004A39C0>::ReallocateMap, 6, Originals<MaterialEntries>::reallocateMap);
EI_VERIFY(0x004A2BF0, IteratorCopy<0x004A2BF0>, 0, s_unused);
EI_VERIFY(0x004A2FC0, DestroyRange<0x004A2FC0>, 0, s_unused);
EI_VERIFY(0x004A3060, DestroyRange<0x004A3060>, 0, s_unused);
EI_VERIFY(0x004A3090, DestroyRangeAux<0x004A3090>, 0, s_unused);
