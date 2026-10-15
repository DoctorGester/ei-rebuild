// Checks for CPlex and the game's CList<T> / CMap<K, V> instantiations (src/lib/mfc_list.cpp, ticket
// S7). Where the elements are plain data the original runs on a twin header and ours on the caller's:
// the nodes both touch (the one taken or freed and its neighbours) are saved, the original's result
// kept and the nodes restored before ours runs, then header, result and node bytes are compared. A
// NewNode / AddTail / NewAssoc that carves a new block compares the two blocks by position (the
// twin's block is freed after). Calls that free every block (the last node removed, RemoveAll,
// destructors) and the CPlex functions run ours only, with checks of the state they leave.
#include "verify.h"

#include "../../src/game/game_lists.h"
#include "../../src/lib/mfc_list.h"

#include <cstdio>
#include <cstring>

using namespace mfc::impl;

namespace {

using ei::verify::Check;
void* s_unused;

// "CList <what> <address>", kept for the process (Report keeps the pointer).
const char* Name(const char* family, const char* what, std::uintptr_t address) {
    char* name = new char[64];
    std::snprintf(name, 64, "%s %s %08X", family, what, static_cast<unsigned>(address));
    return name;
}

template <typename H>
bool SameHeader(const H& a, const H& b) {
    return std::memcmp(&a, &b, sizeof(H)) == 0;
}

// Pointers into the twin's new block mapped to the same place in ours.
struct Mapping {
    const std::uint8_t* from = nullptr;
    std::size_t bytes = 0;
    std::uint8_t* to = nullptr;
    template <typename P>
    P Map(P p) const {
        auto* b = reinterpret_cast<const std::uint8_t*>(p);
        if (from && b >= from && b < from + bytes) return reinterpret_cast<P>(to + (b - from));
        return p;
    }
};

// Nodes (or associations) a call writes: their bytes before, and the original's after.
template <typename Node>
struct Touched {
    Node* at[4] = {};
    std::uint8_t before[4][sizeof(Node)];
    std::uint8_t after[4][sizeof(Node)];
    int n = 0;
    void Add(Node* p) {
        if (!p) return;
        for (int i = 0; i < n; ++i)
            if (at[i] == p) return;
        at[n] = p;
        std::memcpy(before[n], p, sizeof(Node));
        ++n;
    }
    void Keep() {
        for (int i = 0; i < n; ++i) std::memcpy(after[i], at[i], sizeof(Node));
    }
    void Restore() const {
        for (int i = 0; i < n; ++i) std::memcpy(at[i], before[i], sizeof(Node));
    }
    // The first two dwords are links (mapped when the twin carved a block), the rest compared as is.
    bool Same(const Mapping& m) const {
        for (int i = 0; i < n; ++i) {
            void* const* links = reinterpret_cast<void* const*>(after[i]);
            void* const* ours = reinterpret_cast<void* const*>(at[i]);
            std::size_t linkBytes = sizeof(Node) < 2 * sizeof(void*) ? sizeof(Node) : 2 * sizeof(void*);
            for (std::size_t k = 0; k < linkBytes / sizeof(void*); ++k)
                if (m.Map(links[k]) != ours[k]) return false;
            if (std::memcmp(after[i] + linkBytes, reinterpret_cast<const std::uint8_t*>(at[i]) + linkBytes,
                            sizeof(Node) - linkBytes) != 0)
                return false;
        }
        return true;
    }
};

// ---- Lists ---------------------------------------------------------------------------------------

template <typename List>
Mapping BlockMapping(const List& twin, const List& ours) {
    using Node = typename List::Node;
    return {reinterpret_cast<const std::uint8_t*>(twin.blocks),
            sizeof(mfc::CPlex) + static_cast<std::size_t>(ours.blockSize) * sizeof(Node),
            reinterpret_cast<std::uint8_t*>(ours.blocks)};
}

// After a call that carved a block (twin: the original's, ours: ours): the headers by position, the
// result's links and element, the free chains.
template <typename List>
bool SameGrown(const List& twin, const List& ours, const typename List::Node* expected,
               const typename List::Node* result, const Mapping& m) {
    using Node = typename List::Node;
    bool ok = twin.blocks && ours.blocks && twin.blocks != ours.blocks && twin.blocks->next == ours.blocks->next &&
              twin.vtable == ours.vtable && m.Map(twin.head) == ours.head && m.Map(twin.tail) == ours.tail &&
              twin.count == ours.count && m.Map(twin.free) == ours.free && twin.blockSize == ours.blockSize &&
              m.Map(expected) == result && m.Map(expected->next) == result->next &&
              m.Map(expected->prev) == result->prev &&
              std::memcmp(&expected->data, &result->data, sizeof(expected->data)) == 0;
    const Node* a = twin.free;
    const Node* b = ours.free;
    for (; ok && (a || b); a = a->next, b = b->next) ok = a && b && m.Map(a->next) == b->next;
    return ok;
}

// Ours only: what an emptied list looks like.
template <typename List>
bool Emptied(const List& list) {
    return !list.head && !list.tail && list.count == 0 && !list.free && !list.blocks;
}

template <std::uintptr_t Address, typename Abi>
struct TwinNewNode {
    using List = typename Abi::List;
    using Node = typename Abi::Node;
    static inline Node*(__fastcall* original)(List*, void*, Node*, Node*);
    static Node* __fastcall Run(List* self, void*, Node* prev, Node* next) {
        static const char* name = Name("CList", "NewNode", Address);
        List twin = *self;
        Touched<Node> touched;
        touched.Add(self->free);
        Node* expected = original(&twin, nullptr, prev, next);
        touched.Keep();
        touched.Restore();
        Node* result = Abi::NewNode(self, nullptr, prev, next);
        if (!touched.n) {
            Mapping m = BlockMapping(twin, *self);
            Check(name, true, SameGrown(twin, *self, expected, result, m), "grown %d", self->count);
            gamecrt::operator_delete(twin.blocks);
        } else {
            Check(name, true, expected == result && SameHeader(twin, *self) && touched.Same(Mapping{}), "%d",
                  self->count);
        }
        return result;
    }
};

// AddTail / AddHead by value: the node taken and the old tail / head (whose link is set).
template <std::uintptr_t Address, typename Abi, bool Tail>
struct TwinAdd {
    using List = typename Abi::List;
    using Node = typename Abi::Node;
    using T = typename Abi::Element;
    static inline Node*(__fastcall* original)(List*, void*, T);
    static Node* __fastcall Run(List* self, void*, T value) {
        static const char* name = Name("CList", Tail ? "AddTail" : "AddHead", Address);
        List twin = *self;
        Touched<Node> touched;
        touched.Add(Tail ? self->tail : self->head);
        bool grows = !self->free;
        touched.Add(self->free);
        Node* expected = original(&twin, nullptr, value);
        touched.Keep();
        touched.Restore();
        Node* result = Tail ? Abi::AddTailByValue(self, nullptr, value) : Abi::AddHeadByValue(self, nullptr, value);
        if (grows) {
            Mapping m = BlockMapping(twin, *self);
            Check(name, true, SameGrown(twin, *self, expected, result, m) && touched.Same(m), "grown %d", self->count);
            gamecrt::operator_delete(twin.blocks);
        } else {
            Check(name, true, expected == result && SameHeader(twin, *self) && touched.Same(Mapping{}), "%d",
                  self->count);
        }
        return result;
    }
};

// RemoveAt / FreeNode / RemoveHead: against the original while the list keeps a node; the last one
// (every block freed) ours only.
enum class Removal { At, Free, Head };
template <std::uintptr_t Address, typename Abi, Removal Kind>
struct TwinRemove {
    using List = typename Abi::List;
    using Node = typename Abi::Node;
    using T = typename Abi::Element;
    static inline void* original;
    static const char* Label() { return Kind == Removal::At ? "RemoveAt" : Kind == Removal::Free ? "FreeNode" : "RemoveHead"; }
    static void Ours(List* self, Node* node) {
        if constexpr (Kind == Removal::At) Abi::RemoveAt(self, nullptr, node);
        else Abi::FreeNode(self, nullptr, node);
    }
    static void Original(List* twin, Node* node) {
        reinterpret_cast<void(__fastcall*)(List*, void*, Node*)>(original)(twin, nullptr, node);
    }
    static void Run(List* self, Node* node) {
        static const char* name = Name("CList", Label(), Address);
        if (self->count == 1) {
            Ours(self, node);
            Check(name, true, Emptied(*self), "last");
            return;
        }
        List twin = *self;
        Touched<Node> touched;
        touched.Add(node);
        if constexpr (Kind != Removal::Free) {
            touched.Add(node->prev);
            touched.Add(node->next);
        }
        Original(&twin, node);
        touched.Keep();
        touched.Restore();
        Ours(self, node);
        Check(name, true, SameHeader(twin, *self) && touched.Same(Mapping{}), "%d", self->count);
    }
    static void __fastcall RunAt(List* self, void*, Node* node) { Run(self, node); }
    static T __fastcall RunHead(List* self) {
        static const char* name = Name("CList", "RemoveHead", Address);
        Node* head = self->head;
        T value = head->data;
        if (self->count == 1) {
            T result = Abi::RemoveHeadByValue(self);
            Check(name, true, Emptied(*self) && std::memcmp(&result, &value, sizeof(T)) == 0, "last");
            return result;
        }
        List twin = *self;
        Touched<Node> touched;
        touched.Add(head);
        touched.Add(head->next);
        T expected = reinterpret_cast<T(__fastcall*)(List*)>(original)(&twin);
        touched.Keep();
        touched.Restore();
        T result = Abi::RemoveHeadByValue(self);
        Check(name, true,
              SameHeader(twin, *self) && touched.Same(Mapping{}) && std::memcmp(&result, &expected, sizeof(T)) == 0,
              "%d", self->count);
        return result;
    }
};

// RemoveAll / destructors: ours only.
template <std::uintptr_t Address, typename Abi>
struct OursRemoveAll {
    using List = typename Abi::List;
    static void __fastcall Run(List* self) {
        static const char* name = Name("CList", "RemoveAll", Address);
        Abi::RemoveAll(self);
        Check(name, true, Emptied(*self), "");
    }
};
template <std::uintptr_t Address, typename Abi, typename Class>
struct OursDestruct {
    using List = typename Abi::List;
    static void __fastcall Run(List* self) {
        static const char* name = Name("CList", "destructor", Address);
        Abi::template Destruct<Class>(self);
        Check(name, true, Emptied(*self) && self->vtable == Class::Vtable(), "");
    }
};

// Constructors: against the original on a copy of the header.
template <std::uintptr_t Address, typename Class>
struct TwinConstruct {
    using List = mfc::CList<void*>;
    static inline List*(__fastcall* original)(List*, void*, int);
    static List* __fastcall Run(List* self, void*, int blockSize) {
        static const char* name = Name("CList", "constructor", Address);
        List twin = *self;
        List* expected = original(&twin, nullptr, blockSize);
        List* result = ListHeader<Class>::Construct(self, nullptr, blockSize);
        Check(name, true, SameHeader(twin, *self) && expected == &twin && result == self, "%d", blockSize);
        return result;
    }
};
template <std::uintptr_t Address, typename Class, int BlockSize>
struct TwinConstructFixed {
    using List = mfc::CList<void*>;
    static inline List*(__fastcall* original)(List*);
    static List* __fastcall Run(List* self) {
        static const char* name = Name("CList", "constructor", Address);
        List twin = *self;
        List* expected = original(&twin);
        List* result = ListHeader<Class>::template ConstructFixed<BlockSize>(self);
        Check(name, true, SameHeader(twin, *self) && expected == &twin && result == self, "");
        return result;
    }
};

// ConstructElements helpers: the elements built from the same bytes.
template <std::uintptr_t Address, typename Abi>
struct TwinConstructElements {
    using T = typename Abi::Element;
    static inline void(__stdcall* original)(T*, int);
    static void __stdcall Run(T* elements, int count) {
        static const char* name = Name("CList", "ConstructElements", Address);
        std::size_t bytes = static_cast<std::size_t>(count) * sizeof(T);
        auto* twin = static_cast<T*>(gamecrt::operator_new(bytes ? bytes : 1));
        std::memcpy(static_cast<void*>(twin), elements, bytes);
        original(twin, count);
        Abi::ConstructElements(elements, count);
        Check(name, true, std::memcmp(twin, elements, bytes) == 0, "%d", count);
        gamecrt::operator_delete(twin);
    }
};

// ---- Maps ----------------------------------------------------------------------------------------

template <std::uintptr_t Address, typename Abi>
struct TwinInitHashTable {
    using Map = typename Abi::Map;
    using Assoc = typename Abi::Assoc;
    static inline void(__fastcall* original)(Map*, void*, std::uint32_t, int);
    static void __fastcall Run(Map* self, void*, std::uint32_t hashSize, int allocNow) {
        static const char* name = Name("CMap", "InitHashTable", Address);
        Map twin = *self;
        if (self->table) {   // the original deletes its own copy
            std::size_t bytes = self->hashSize * sizeof(Assoc*);
            twin.table = static_cast<Assoc**>(gamecrt::operator_new(bytes ? bytes : 1));
            std::memcpy(twin.table, self->table, bytes);
        }
        original(&twin, nullptr, hashSize, allocNow);
        Abi::InitHashTable(self, nullptr, hashSize, allocNow);
        bool ok = twin.hashSize == self->hashSize && (twin.table == nullptr) == (self->table == nullptr) &&
                  (!self->table || std::memcmp(twin.table, self->table, hashSize * sizeof(Assoc*)) == 0);
        Map rest = twin;
        rest.table = self->table;
        Check(name, true, ok && SameHeader(rest, *self), "%u, %d", hashSize, allocNow);
        gamecrt::operator_delete(twin.table);
    }
};

template <std::uintptr_t Address, typename Abi>
struct TwinGetAssocAt {
    using Map = typename Abi::Map;
    using Assoc = typename Abi::Assoc;
    using K = decltype(Assoc::key);
    static inline Assoc*(__fastcall* original)(const Map*, void*, const K*, std::uint32_t*);
    static Assoc* __fastcall Run(const Map* self, void*, const K* key, std::uint32_t* hash) {
        static const char* name = Name("CMap", "GetAssocAt", Address);
        std::uint32_t expectedHash = 0;
        Assoc* expected = original(self, nullptr, key, &expectedHash);
        Assoc* result = Abi::GetAssocAt(self, nullptr, key, hash);
        Check(name, true, expected == result && expectedHash == *hash, "%u", *hash);
        return result;
    }
};

template <std::uintptr_t Address, typename Abi>
struct TwinNewAssoc {
    using Map = typename Abi::Map;
    using Assoc = typename Abi::Assoc;
    static inline Assoc*(__fastcall* original)(Map*);
    static Assoc* __fastcall Run(Map* self) {
        static const char* name = Name("CMap", "NewAssoc", Address);
        Map twin = *self;
        Touched<Assoc> touched;
        touched.Add(self->freeList);
        Assoc* expected = original(&twin);
        touched.Keep();
        touched.Restore();
        Assoc* result = Abi::NewAssoc(self);
        if (!touched.n) {
            Mapping m{reinterpret_cast<const std::uint8_t*>(twin.blocks),
                      sizeof(mfc::CPlex) + static_cast<std::size_t>(self->blockSize) * sizeof(Assoc),
                      reinterpret_cast<std::uint8_t*>(self->blocks)};
            bool ok = twin.blocks != self->blocks && twin.blocks->next == self->blocks->next &&
                      twin.count == self->count && m.Map(twin.freeList) == self->freeList && m.Map(expected) == result &&
                      std::memcmp(&expected->key, &result->key, sizeof(Assoc) - 8) == 0 && twin.table == self->table &&
                      twin.hashSize == self->hashSize;
            const Assoc* a = twin.freeList;
            const Assoc* b = self->freeList;
            for (; ok && (a || b); a = a->next, b = b->next) ok = a && b && m.Map(a->next) == b->next;
            Check(name, true, ok, "grown %d", self->count);
            gamecrt::operator_delete(twin.blocks);
        } else {
            Check(name, true, expected == result && SameHeader(twin, *self) && touched.Same(Mapping{}), "%d",
                  self->count);
        }
        return result;
    }
};

// operator[]: against the original when the key is there (a read); ours only when it is added.
template <std::uintptr_t Address, typename Abi>
struct CheckedAt {
    using Map = typename Abi::Map;
    using Assoc = typename Abi::Assoc;
    using K = decltype(Assoc::key);
    using V = decltype(Assoc::value);
    static inline V*(__fastcall* original)(Map*, void*, const K*);
    static V* __fastcall Run(Map* self, void*, const K* key) {
        static const char* name = Name("CMap", "operator[]", Address);
        std::uint32_t hash = 0;
        if (Abi::GetAssocAt(self, nullptr, key, &hash)) {
            V* expected = original(self, nullptr, key);
            V* result = Abi::At(self, nullptr, key);
            Check(name, true, expected == result, "found");
            return result;
        }
        int count = self->count;
        V* result = Abi::At(self, nullptr, key);
        Assoc* head = self->table ? self->table[hash] : nullptr;
        V zero{};
        Check(name, true,
              self->count == count + 1 && head && &head->value == result && head->key == *key && head->hash == hash &&
                  std::memcmp(result, &zero, sizeof(V)) == 0,
              "added");
        return result;
    }
};

template <typename Map>
bool MapEmptied(const Map& map) {
    return !map.table && map.count == 0 && !map.freeList && !map.blocks;
}
template <std::uintptr_t Address, typename Abi>
struct OursMapRemoveAll {
    using Map = typename Abi::Map;
    static void __fastcall Run(Map* self) {
        static const char* name = Name("CMap", "RemoveAll", Address);
        Abi::RemoveAll(self);
        Check(name, true, MapEmptied(*self), "");
    }
};
template <std::uintptr_t Address, typename Abi, typename Class>
struct OursMapDestruct {
    using Map = typename Abi::Map;
    static void __fastcall Run(Map* self) {
        static const char* name = Name("CMap", "destructor", Address);
        Abi::template Destruct<Class>(self);
        Check(name, true, MapEmptied(*self) && self->vtable == Class::Vtable(), "");
    }
};

struct DwordMapsAbi {
    using Element = mfc::CMap<std::uint32_t, std::uint32_t>;
    static void __stdcall ConstructElements(Element* maps, int count) { ConstructDwordMaps(maps, count); }
};

// ---- CPlex: ours only ----------------------------------------------------------------------------

struct PlexChecks {
    static mfc::CPlex* __stdcall Create(mfc::CPlex*& head, unsigned count, unsigned elementSize) {
        static const char* name = Name("CPlex", "Create", 0x006FE8EF);
        mfc::CPlex* old = head;
        mfc::CPlex* block = CreatePlex(head, count, elementSize);
        Check(name, true, block && head == block && block->next == old, "%u x %u", count, elementSize);
        return block;
    }
    static int __fastcall FreeDataChain(mfc::CPlex* head) {
        static const char* name = Name("CPlex", "FreeDataChain", 0x006FE90F);
        int blocks = 0;
        for (mfc::CPlex* p = head; p; p = p->next) ++blocks;
        int result = FreePlexChain(head);
        Check(name, true, result == 0, "%d", blocks);
        return result;
    }
};

// ---- Game element lists (src/game/game_lists.cpp): ours only -------------------------------------

// FreeNode / RemoveAt: one node less, the node first on the free list (or the list emptied).
template <std::uintptr_t Address, typename Abi, bool At>
struct OursFree {
    using List = typename Abi::List;
    using Node = typename Abi::Node;
    static void __fastcall Run(List* self, void*, Node* node) {
        static const char* name = Name("CList", At ? "RemoveAt" : "FreeNode", Address);
        int count = self->count;
        if constexpr (At) Abi::RemoveAt(self, nullptr, node);
        else Abi::FreeNode(self, nullptr, node);
        bool ok = count == 1 ? Emptied(*self) : self->count == count - 1 && self->free == node;
        if constexpr (At) ok = ok && self->head != node && self->tail != node;
        Check(name, true, ok, "%d", count);
    }
};

template <std::uintptr_t Address, typename Abi>
struct OursNewNode {
    using List = typename Abi::List;
    using Node = typename Abi::Node;
    static Node* __fastcall Run(List* self, void*, Node* prev, Node* next) {
        static const char* name = Name("CList", "NewNode", Address);
        int count = self->count;
        Node* node = Abi::NewNode(self, nullptr, prev, next);
        Check(name, true, node && node->prev == prev && node->next == next && self->count == count + 1, "%d", count);
        return node;
    }
};

struct CountedAddTailCheck {
    using List = gamelists::EngineObjects::List;
    using Node = gamelists::EngineObjects::Node;
    static Node* __fastcall Run(List* self, void*, CEngineObject* object) {
        static const char* name = Name("CList", "AddTail", 0x00599FA0);
        int count = self->count;
        int refs = object ? object->refCount_ : 0;
        Node* node = gamelists::CountedAddTail(self, nullptr, object);
        Check(name, true,
              self->tail == node && node->data == object && !node->next && self->count == count + 1 &&
                  (!object || object->refCount_ == refs + 1),
              "%d", count);
        return node;
    }
};

} // namespace

// ---- Registrations (one alias per address: each twin check keeps its own original) ---------------

EI_VERIFY(0x006FE8EF, PlexChecks::Create, 0, s_unused);
EI_VERIFY(0x006FE90F, PlexChecks::FreeDataChain, 0, s_unused);

using C004C96A0 = TwinConstruct<0x004C96A0, PtrListClass>;
EI_VERIFY(0x004C96A0, C004C96A0::Run, 7, C004C96A0::original);
using C004C98C0 = TwinConstruct<0x004C98C0, ItemListClass>;
EI_VERIFY(0x004C98C0, C004C98C0::Run, 7, C004C98C0::original);
using C00556D10 = TwinConstruct<0x00556D10, CellListClass>;
EI_VERIFY(0x00556D10, C00556D10::Run, 7, C00556D10::original);
using C0056FD40 = TwinConstruct<0x0056FD40, ClientListClass>;
EI_VERIFY(0x0056FD40, C0056FD40::Run, 7, C0056FD40::original);
using C00570EA0 = TwinConstruct<0x00570EA0, ClientList2Class>;
EI_VERIFY(0x00570EA0, C00570EA0::Run, 7, C00570EA0::original);
using C005715D0 = TwinConstruct<0x005715D0, ClientList3Class>;
EI_VERIFY(0x005715D0, C005715D0::Run, 7, C005715D0::original);
using C00598EC0 = TwinConstruct<0x00598EC0, TempListClass>;
EI_VERIFY(0x00598EC0, C00598EC0::Run, 7, C00598EC0::original);
using C005CDAD0 = TwinConstruct<0x005CDAD0, StaticListClass>;
EI_VERIFY(0x005CDAD0, C005CDAD0::Run, 7, C005CDAD0::original);
using C00545750 = TwinConstructFixed<0x00545750, CellListClass, 0x80>;
EI_VERIFY(0x00545750, C00545750::Run, 10, C00545750::original);

using D00405130 = OursDestruct<0x00405130, PtrList, TempListClass>;
EI_VERIFY(0x00405130, D00405130::Run, 0, s_unused);
using D00427830 = OursDestruct<0x00427830, PtrList, ItemListClass>;
EI_VERIFY(0x00427830, D00427830::Run, 0, s_unused);
using D004C96F0 = OursDestruct<0x004C96F0, PtrList, PtrListClass>;
EI_VERIFY(0x004C96F0, D004C96F0::Run, 0, s_unused);
using D00539890 = OursDestruct<0x00539890, PtrList, CellListClass>;
EI_VERIFY(0x00539890, D00539890::Run, 0, s_unused);
using D00558470 = OursDestruct<0x00558470, PtrList, LootListClass>;
EI_VERIFY(0x00558470, D00558470::Run, 0, s_unused);
using D0056FE40 = OursDestruct<0x0056FE40, PtrList, PlatformListClass>;
EI_VERIFY(0x0056FE40, D0056FE40::Run, 0, s_unused);
using D005995E0 = OursDestruct<0x005995E0, PtrList, NodeListClass>;
EI_VERIFY(0x005995E0, D005995E0::Run, 0, s_unused);
using D005B1810 = OursDestruct<0x005B1810, PtrList, BlockCostListClass>;
EI_VERIFY(0x005B1810, D005B1810::Run, 0, s_unused);
using D005B19A0 = OursDestruct<0x005B19A0, PtrList, AiMapListClass>;
EI_VERIFY(0x005B19A0, D005B19A0::Run, 0, s_unused);
using D005BDEE0 = OursDestruct<0x005BDEE0, PtrList, WaypointListClass>;
EI_VERIFY(0x005BDEE0, D005BDEE0::Run, 0, s_unused);

using N004069A0 = TwinNewNode<0x004069A0, PtrList>;
EI_VERIFY(0x004069A0, N004069A0::Run, 6, N004069A0::original);
using N004621A0 = TwinNewNode<0x004621A0, PtrList>;
EI_VERIFY(0x004621A0, N004621A0::Run, 6, N004621A0::original);
using N0052CEF0 = TwinNewNode<0x0052CEF0, PtrList>;
EI_VERIFY(0x0052CEF0, N0052CEF0::Run, 6, N0052CEF0::original);
using N00573290 = TwinNewNode<0x00573290, PtrList>;
EI_VERIFY(0x00573290, N00573290::Run, 6, N00573290::original);
using N00429EA0 = TwinNewNode<0x00429EA0, PtrList>;
EI_VERIFY(0x00429EA0, N00429EA0::Run, 7, N00429EA0::original);
using N0052CDB0 = TwinNewNode<0x0052CDB0, PtrList>;
EI_VERIFY(0x0052CDB0, N0052CDB0::Run, 7, N0052CDB0::original);
using N0059C7A0 = TwinNewNode<0x0059C7A0, PtrList>;
EI_VERIFY(0x0059C7A0, N0059C7A0::Run, 7, N0059C7A0::original);
using N0059D5F0 = TwinNewNode<0x0059D5F0, List8>;
EI_VERIFY(0x0059D5F0, N0059D5F0::Run, 7, N0059D5F0::original);
using N00558B10 = TwinNewNode<0x00558B10, List12>;
EI_VERIFY(0x00558B10, N00558B10::Run, 7, N00558B10::original);
using N005BEDB0 = TwinNewNode<0x005BEDB0, List12>;
EI_VERIFY(0x005BEDB0, N005BEDB0::Run, 7, N005BEDB0::original);
using N004CD540 = TwinNewNode<0x004CD540, ParticleSourceList>;
EI_VERIFY(0x004CD540, N004CD540::Run, 7, N004CD540::original);
using N005BE7B0 = TwinNewNode<0x005BE7B0, List1C>;
EI_VERIFY(0x005BE7B0, N005BE7B0::Run, 7, N005BE7B0::original);
using N00573000 = TwinNewNode<0x00573000, List4C>;
EI_VERIFY(0x00573000, N00573000::Run, 7, N00573000::original);
using N004CD620 = TwinNewNode<0x004CD620, LightningList>;
EI_VERIFY(0x004CD620, N004CD620::Run, 7, N004CD620::original);
using N004CD5B0 = TwinNewNode<0x004CD5B0, FxSourceList>;
EI_VERIFY(0x004CD5B0, N004CD5B0::Run, 7, N004CD5B0::original);
using N004CD6D0 = TwinNewNode<0x004CD6D0, CommandBlockList>;
EI_VERIFY(0x004CD6D0, N004CD6D0::Run, 7, N004CD6D0::original);
using N0059CA60 = TwinNewNode<0x0059CA60, SharedObjectNewList>;
EI_VERIFY(0x0059CA60, N0059CA60::Run, 7, N0059CA60::original);

using T00405040 = TwinAdd<0x00405040, PtrList, true>;
EI_VERIFY(0x00405040, T00405040::Run, 7, T00405040::original);
using T004C98F0 = TwinAdd<0x004C98F0, PtrList, true>;
EI_VERIFY(0x004C98F0, T004C98F0::Run, 7, T004C98F0::original);
using T0059CC60 = TwinAdd<0x0059CC60, PtrList, true>;
EI_VERIFY(0x0059CC60, T0059CC60::Run, 7, T0059CC60::original);

using H00404FE0 = TwinRemove<0x00404FE0, PtrList, Removal::Head>;
EI_VERIFY(0x00404FE0, H00404FE0::RunHead, 5, H00404FE0::original);
using R004050C0 = TwinRemove<0x004050C0, PtrList, Removal::At>;
EI_VERIFY(0x004050C0, R004050C0::RunAt, 5, R004050C0::original);
using R0056FDD0 = TwinRemove<0x0056FDD0, PtrList, Removal::At>;
EI_VERIFY(0x0056FDD0, R0056FDD0::RunAt, 5, R0056FDD0::original);
using R005704A0 = TwinRemove<0x005704A0, PtrList, Removal::At>;
EI_VERIFY(0x005704A0, R005704A0::RunAt, 5, R005704A0::original);
using R0059A680 = TwinRemove<0x0059A680, PtrList, Removal::At>;
EI_VERIFY(0x0059A680, R0059A680::RunAt, 5, R0059A680::original);
using F005588A0 = TwinRemove<0x005588A0, PtrList, Removal::Free>;
EI_VERIFY(0x005588A0, F005588A0::RunAt, 5, F005588A0::original);
using F00558C00 = TwinRemove<0x00558C00, PtrList, Removal::Free>;
EI_VERIFY(0x00558C00, F00558C00::RunAt, 5, F00558C00::original);
using F00573300 = TwinRemove<0x00573300, PtrList, Removal::Free>;
EI_VERIFY(0x00573300, F00573300::RunAt, 5, F00573300::original);

using A00429F10 = OursRemoveAll<0x00429F10, PtrList>;
EI_VERIFY(0x00429F10, A00429F10::Run, 0, s_unused);
using A0052CA10 = OursRemoveAll<0x0052CA10, PtrList>;
EI_VERIFY(0x0052CA10, A0052CA10::Run, 0, s_unused);
using A00556D40 = OursRemoveAll<0x00556D40, PtrList>;
EI_VERIFY(0x00556D40, A00556D40::Run, 0, s_unused);
using A00558C50 = OursRemoveAll<0x00558C50, PtrList>;
EI_VERIFY(0x00558C50, A00558C50::Run, 0, s_unused);
using A0056FDA0 = OursRemoveAll<0x0056FDA0, PtrList>;
EI_VERIFY(0x0056FDA0, A0056FDA0::Run, 0, s_unused);
using A00570470 = OursRemoveAll<0x00570470, PtrList>;
EI_VERIFY(0x00570470, A00570470::Run, 0, s_unused);
using A0059D5C0 = OursRemoveAll<0x0059D5C0, PtrList>;
EI_VERIFY(0x0059D5C0, A0059D5C0::Run, 0, s_unused);

using E004CE580 = TwinConstructElements<0x004CE580, Plain4>;
EI_VERIFY(0x004CE580, E004CE580::Run, 5, E004CE580::original);
using E0059E2C0 = TwinConstructElements<0x0059E2C0, Plain4>;
EI_VERIFY(0x0059E2C0, E0059E2C0::Run, 5, E0059E2C0::original);
using E0059E390 = TwinConstructElements<0x0059E390, Plain8>;
EI_VERIFY(0x0059E390, E0059E390::Run, 5, E0059E390::original);
using E00558EA0 = TwinConstructElements<0x00558EA0, Plain12>;
EI_VERIFY(0x00558EA0, E00558EA0::Run, 6, E00558EA0::original);
using E005BF320 = TwinConstructElements<0x005BF320, Plain12>;
EI_VERIFY(0x005BF320, E005BF320::Run, 5, E005BF320::original);
using E004CEA30 = TwinConstructElements<0x004CEA30, ParticleSourceArray>;
EI_VERIFY(0x004CEA30, E004CEA30::Run, 6, E004CEA30::original);
using E00575660 = TwinConstructElements<0x00575660, Plain4C>;
EI_VERIFY(0x00575660, E00575660::Run, 5, E00575660::original);
using E004CEAD0 = TwinConstructElements<0x004CEAD0, LightningArray>;
EI_VERIFY(0x004CEAD0, E004CEAD0::Run, 5, E004CEAD0::original);
using E004CEA80 = TwinConstructElements<0x004CEA80, FxSourceArray>;
EI_VERIFY(0x004CEA80, E004CEA80::Run, 6, E004CEA80::original);
using E004CEB20 = TwinConstructElements<0x004CEB20, CommandBlockArray>;
EI_VERIFY(0x004CEB20, E004CEB20::Run, 6, E004CEB20::original);
using E0059E330 = TwinConstructElements<0x0059E330, SharedObjectArray>;
EI_VERIFY(0x0059E330, E0059E330::Run, 5, E0059E330::original);
using E00477380 = TwinConstructElements<0x00477380, DwordMapsAbi>;
EI_VERIFY(0x00477380, E00477380::Run, 6, E00477380::original);

using I00453B90 = TwinInitHashTable<0x00453B90, Map32>;
EI_VERIFY(0x00453B90, I00453B90::Run, 6, I00453B90::original);
using I00453FD0 = TwinInitHashTable<0x00453FD0, Map64x64>;
EI_VERIFY(0x00453FD0, I00453FD0::Run, 6, I00453FD0::original);
using I004540F0 = TwinInitHashTable<0x004540F0, Map64x32>;
EI_VERIFY(0x004540F0, I004540F0::Run, 6, I004540F0::original);
using I00454210 = TwinInitHashTable<0x00454210, Map32>;
EI_VERIFY(0x00454210, I00454210::Run, 6, I00454210::original);

using G00453C60 = TwinGetAssocAt<0x00453C60, Map32>;
EI_VERIFY(0x00453C60, G00453C60::Run, 5, G00453C60::original);
using G004542E0 = TwinGetAssocAt<0x004542E0, Map32>;
EI_VERIFY(0x004542E0, G004542E0::Run, 5, G004542E0::original);
using G00477340 = TwinGetAssocAt<0x00477340, Map32>;
EI_VERIFY(0x00477340, G00477340::Run, 5, G00477340::original);
using G004540A0 = TwinGetAssocAt<0x004540A0, Map64x64>;
EI_VERIFY(0x004540A0, G004540A0::Run, 5, G004540A0::original);
using G004541C0 = TwinGetAssocAt<0x004541C0, Map64x32>;
EI_VERIFY(0x004541C0, G004541C0::Run, 5, G004541C0::original);

using M00453C00 = TwinNewAssoc<0x00453C00, Map32>;
EI_VERIFY(0x00453C00, M00453C00::Run, 6, M00453C00::original);
using M00454280 = TwinNewAssoc<0x00454280, Map32>;
EI_VERIFY(0x00454280, M00454280::Run, 6, M00454280::original);
using M00454040 = TwinNewAssoc<0x00454040, Map64x64>;
EI_VERIFY(0x00454040, M00454040::Run, 6, M00454040::original);
using M00454160 = TwinNewAssoc<0x00454160, Map64x32>;
EI_VERIFY(0x00454160, M00454160::Run, 6, M00454160::original);

using K00476050 = CheckedAt<0x00476050, Map32>;
EI_VERIFY(0x00476050, K00476050::Run, 5, K00476050::original);
using X00453F80 = OursMapRemoveAll<0x00453F80, Map32>;
EI_VERIFY(0x00453F80, X00453F80::Run, 0, s_unused);
using X00476140 = OursMapDestruct<0x00476140, Map32, DwordMapClass>;
EI_VERIFY(0x00476140, X00476140::Run, 0, s_unused);

// Game element lists (ours only).
using G0059A050 = OursFree<0x0059A050, gamelists::EngineObjects, true>;
EI_VERIFY(0x0059A050, G0059A050::Run, 0, s_unused);
using G00429E50 = OursRemoveAll<0x00429E50, gamelists::EngineObjects>;
EI_VERIFY(0x00429E50, G00429E50::Run, 0, s_unused);
EI_VERIFY(0x00599FA0, CountedAddTailCheck::Run, 0, s_unused);
using G0052CE20 = OursFree<0x0052CE20, gamelists::ObjectRefs, false>;
EI_VERIFY(0x0052CE20, G0052CE20::Run, 0, s_unused);
using G0059C810 = OursFree<0x0059C810, gamelists::Refs, false>;
EI_VERIFY(0x0059C810, G0059C810::Run, 0, s_unused);
using G00599CC0 = OursFree<0x00599CC0, gamelists::Refs, true>;
EI_VERIFY(0x00599CC0, G00599CC0::Run, 0, s_unused);
using G00598EF0 = OursRemoveAll<0x00598EF0, gamelists::Refs>;
EI_VERIFY(0x00598EF0, G00598EF0::Run, 0, s_unused);
using G005575F0 = OursDestruct<0x005575F0, gamelists::OwnedRefs, gamelists::OwnedRefListClass>;
EI_VERIFY(0x005575F0, G005575F0::Run, 0, s_unused);
using G00558B80 = OursFree<0x00558B80, gamelists::Holders, false>;
EI_VERIFY(0x00558B80, G00558B80::Run, 0, s_unused);
using G00558020 = OursRemoveAll<0x00558020, gamelists::Holders>;
EI_VERIFY(0x00558020, G00558020::Run, 0, s_unused);
using G0059CAD0 = OursFree<0x0059CAD0, gamelists::SharedObjects, false>;
EI_VERIFY(0x0059CAD0, G0059CAD0::Run, 0, s_unused);
using G0059A3C0 = OursDestruct<0x0059A3C0, gamelists::SharedObjects, gamelists::SharedObjectListClass>;
EI_VERIFY(0x0059A3C0, G0059A3C0::Run, 0, s_unused);
using G006BD4F0 = OursNewNode<0x006BD4F0, gamelists::Waves>;
EI_VERIFY(0x006BD4F0, G006BD4F0::Run, 0, s_unused);
using G006BD560 = OursFree<0x006BD560, gamelists::Waves, false>;
EI_VERIFY(0x006BD560, G006BD560::Run, 0, s_unused);
using G004C6040 = OursFree<0x004C6040, gamelists::ParticleSources, true>;
EI_VERIFY(0x004C6040, G004C6040::Run, 0, s_unused);
using G004C6370 = OursFree<0x004C6370, gamelists::FxSources, true>;
EI_VERIFY(0x004C6370, G004C6370::Run, 0, s_unused);
using G004C6680 = OursFree<0x004C6680, gamelists::Lightnings, true>;
EI_VERIFY(0x004C6680, G004C6680::Run, 0, s_unused);
using G004CA260 = OursDestruct<0x004CA260, gamelists::Tokens, gamelists::TokenListClass>;
EI_VERIFY(0x004CA260, G004CA260::Run, 0, s_unused);
using G004CD9B0 = OursFree<0x004CD9B0, gamelists::Definitions, false>;
EI_VERIFY(0x004CD9B0, G004CD9B0::Run, 0, s_unused);
using G004CDAB0 = OursFree<0x004CDAB0, gamelists::Runs, false>;
EI_VERIFY(0x004CDAB0, G004CDAB0::Run, 0, s_unused);
// CScript's lists' destructors (S13b; CScript::Destruct calls ours directly, so these count only
// other callers).
using G004C92C0 = OursDestruct<0x004C92C0, gamelists::ObjectRefs, gamelists::TempObjectListClass>;
EI_VERIFY(0x004C92C0, G004C92C0::Run, 0, s_unused);
using G004C8E20 = OursDestruct<0x004C8E20, gamelists::Groups, gamelists::TempGroupListClass>;
EI_VERIFY(0x004C8E20, G004C8E20::Run, 0, s_unused);
using G004C7560 = OursDestruct<0x004C7560, gamelists::Decls, gamelists::DeclListClass>;
EI_VERIFY(0x004C7560, G004C7560::Run, 0, s_unused);
using G004C8520 = OursDestruct<0x004C8520, gamelists::Runs, gamelists::RunListClass>;
EI_VERIFY(0x004C8520, G004C8520::Run, 0, s_unused);
using G004C7E30 = OursDestruct<0x004C7E30, gamelists::Definitions, gamelists::InstanceListClass>;
EI_VERIFY(0x004C7E30, G004C7E30::Run, 0, s_unused);
using G004C6760 = OursDestruct<0x004C6760, gamelists::Lightnings, gamelists::LightningListClass>;
EI_VERIFY(0x004C6760, G004C6760::Run, 0, s_unused);
using G004C6410 = OursDestruct<0x004C6410, gamelists::FxSources, gamelists::FxSourceListClass>;
EI_VERIFY(0x004C6410, G004C6410::Run, 0, s_unused);
using G004C6120 = OursDestruct<0x004C6120, gamelists::ParticleSources, gamelists::ParticleSourceListClass>;
EI_VERIFY(0x004C6120, G004C6120::Run, 0, s_unused);

// ---- The script lists with the game's copy semantics (S7a; ours only) ------------------------------
//
// Reached: the instance and run lists at every script reset (new game, zone load) and script call
// (InstanceAddHead), the temporaries at every group / object evaluation, the command blocks at the
// reset. ConstructElements against the original (the elements allocate nothing).

namespace {

// The list after a node left it: one fewer, or emptied with every block freed.
template <typename List>
bool Removed(const List& list, int countBefore) {
    return countBefore == 1 ? Emptied(list) : list.count == countBefore - 1;
}

void __fastcall DeclElementDestructCheck(ScriptDecl* block) {
    ScriptDeclDestruct(block);
    Check("ScriptDecl destructor 004C54A0", true,
          reinterpret_cast<std::uintptr_t>(block->commands.vtable) == 0x0073D3D0, "");
}

ScriptDecl* __fastcall DeclRemoveHeadCheck(gamelists::Decls::List* self, void*, ScriptDecl* out) {
    int count = self->count;
    int id = self->head->data.id, size = self->head->data.commands.size;
    ScriptDecl* result = gamelists::DeclRemoveHead(self, nullptr, out);
    Check("CList RemoveHead 004C69B0", true,
          result == out && out->id == id && out->commands.size == size && Removed(*self, count), "%d", count);
    return result;
}

ScriptDefinition* __fastcall InstanceRemoveHeadCheck(gamelists::Definitions::List* self, void*, ScriptDefinition* out) {
    int count = self->count;
    const ScriptDefinition& head = self->head->data;
    int index = head.index, pairs = head.ifThen.size, floats = head.vars.floats.size;
    mfc::CString name(head.name);
    ScriptDefinition* result = gamelists::InstanceRemoveHead(self, nullptr, out);
    Check("CList RemoveHead 004C7A10", true,
          result == out && out->index == index && out->ifThen.size == pairs && out->vars.floats.size == floats &&
              std::strcmp(out->name, name) == 0 && Removed(*self, count),
          "%d", count);
    return result;
}

ScriptRun* __fastcall RunRemoveHeadCheck(gamelists::Runs::List* self, void*, ScriptRun* out) {
    int count = self->count;
    ScriptRun head;
    std::memcpy(static_cast<void*>(&head), &self->head->data, sizeof(ScriptRun));   // the plain fields
    ScriptRun* result = gamelists::RunRemoveHead(self, nullptr, out);
    Check("CList RemoveHead 004C82B0", true,
          result == out && out->block == head.block && out->index == head.index && out->ticks == head.ticks &&
              out->sleepStart == head.sleepStart && out->killFlag == head.killFlag &&
              out->vars.floats.size == head.vars.floats.size && out->vars.groups.size == head.vars.groups.size &&
              Removed(*self, count),
          "%d", count);
    return result;
}

gamelists::Definitions::Node* __fastcall InstanceAddHeadCheck(gamelists::Definitions::List* self, void*,
                                                              const ScriptDefinition* value) {
    int count = self->count;
    auto* node = gamelists::InstanceAddHead(self, nullptr, value);
    Check("CList AddHead 004C7C90", true,
          self->head == node && !node->prev && self->count == count + 1 && node->data.index == value->index &&
              node->data.vars.floats.size == value->vars.floats.size,
          "%d", count);
    return node;
}

gamelists::ObjectRefs::Node* __fastcall ObjectRefAddHeadCheck(gamelists::ObjectRefs::List* self, void*,
                                                             const ScriptObjectRef* value) {
    int count = self->count;
    CEngineObject* object = value->object;
    int refs = object ? object->refCount_ : 0;
    auto* node = gamelists::ObjectRefAddHead(self, nullptr, value);
    Check("CList AddHead 004C9210", true,
          self->head == node && !node->prev && self->count == count + 1 && node->data.object == object &&
              (!object || object->refCount_ == refs + 1),
          "%d", count);
    return node;
}

ScriptObjectRef* __fastcall ObjectRefRemoveTailCheck(gamelists::ObjectRefs::List* self, void*, ScriptObjectRef* out) {
    int count = self->count;
    CEngineObject* object = self->tail->data.object;
    int refs = object ? object->refCount_ : 0;
    ScriptObjectRef* result = gamelists::ObjectRefRemoveTail(self, nullptr, out);
    Check("CList RemoveTail 004C9100", true,
          result == out && out->object == object && (!object || object->refCount_ == refs) && Removed(*self, count),
          "%d", count);
    return result;
}

template <bool Head>
gamelists::Groups::Node* __fastcall GroupAddCheck(gamelists::Groups::List* self, void*, const ScriptGroup* value) {
    int count = self->count;
    auto* node = Head ? gamelists::GroupAddHead(self, nullptr, value) : gamelists::GroupAddTail(self, nullptr, value);
    bool linked = Head ? self->head == node && !node->prev : self->tail == node && !node->next;
    Check(Head ? "CList AddHead 004C8CE0" : "CList AddTail 004C8D80", true,
          linked && self->count == count + 1 && node->data.size == value->size &&
              reinterpret_cast<std::uintptr_t>(node->data.vtable) == 0x0073D110,
          "%d", count);
    return node;
}

ScriptGroup* __fastcall GroupRemoveTailCheck(gamelists::Groups::List* self, void*, ScriptGroup* out) {
    int count = self->count;
    int size = self->tail->data.size;
    ScriptGroup* result = gamelists::GroupRemoveTail(self, nullptr, out);
    Check("CList RemoveTail 004C8AF0", true, result == out && out->size == size && Removed(*self, count), "%d", count);
    return result;
}

using DefinitionArray = ArrayAbi<ScriptDefinition, gamelists::DefinitionElements>;
using RunArray = ArrayAbi<ScriptRun, gamelists::RunElements>;

} // namespace

EI_VERIFY(0x004C54A0, DeclElementDestructCheck, 0, s_unused);
EI_VERIFY(0x004C69B0, DeclRemoveHeadCheck, 0, s_unused);
EI_VERIFY(0x004C7A10, InstanceRemoveHeadCheck, 0, s_unused);
EI_VERIFY(0x004C82B0, RunRemoveHeadCheck, 0, s_unused);
EI_VERIFY(0x004C7C90, InstanceAddHeadCheck, 0, s_unused);
EI_VERIFY(0x004C9210, ObjectRefAddHeadCheck, 0, s_unused);
EI_VERIFY(0x004C9100, ObjectRefRemoveTailCheck, 0, s_unused);
EI_VERIFY(0x004C8CE0, GroupAddCheck<true>, 0, s_unused);
EI_VERIFY(0x004C8D80, GroupAddCheck<false>, 0, s_unused);
EI_VERIFY(0x004C8AF0, GroupRemoveTailCheck, 0, s_unused);
using G004CD930 = OursNewNode<0x004CD930, gamelists::Definitions>;
EI_VERIFY(0x004CD930, G004CD930::Run, 0, s_unused);
using G004CDA30 = OursNewNode<0x004CDA30, gamelists::Runs>;
EI_VERIFY(0x004CDA30, G004CDA30::Run, 0, s_unused);
using E004CEB80 = TwinConstructElements<0x004CEB80, DefinitionArray>;
EI_VERIFY(0x004CEB80, E004CEB80::Run, 5, E004CEB80::original);
using E004CECB0 = TwinConstructElements<0x004CECB0, RunArray>;
EI_VERIFY(0x004CECB0, E004CECB0::Run, 5, E004CECB0::original);
