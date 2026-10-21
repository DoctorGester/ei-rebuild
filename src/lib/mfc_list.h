#pragma once

// MFC 4.2's CList<TYPE, ARG_TYPE>, CMap<KEY, ARG_KEY, VALUE, ARG_VALUE> (afxtempl.h) and CPlex
// (afxplex_.h) as game.exe instantiates them, for the standalone exe (ticket S7,
// docs/notes/standalone.md section 2). The list bodies are mfc::CList's (mfc.h), the map bodies are
// written here over mfc::CMap's layout; what differs between the instantiations is the element: its
// size (read from each NewNode's CPlex::Create) and the element policy (mfc_array.h: how
// ConstructElements builds a new one, how DestructElements destroys it). ListAbi / MapAbi turn the
// members into the instantiations' calling conventions (__thiscall as __fastcall with EDX unused);
// mfc_list.cpp registers the library-only instantiations, src/game/game_lists.cpp the ones whose
// elements are game types. Checked in game by verify_mfc_list.cpp until S21d.

#include "mfc_array.h"
#include "game/game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mfc::impl {

// ---- CPlex ----------------------------------------------------------------------------------------

// 006FE8EF CPlex::Create (__stdcall, RET 0xC): a block of `count` elements of `elementSize` bytes
// after the CPlex header (operator new), linked in front of `head`. Returns the block.
CPlex* __stdcall CreatePlex(CPlex*& head, unsigned count, unsigned elementSize);
// 006FE90F CPlex::FreeDataChain (__thiscall, `this` may be null): every block of the chain freed.
// Returns 0: the original leaves EAX as its caller had it for a null chain, and AIPath::Reset
// (005C7D50) returns that EAX (0 there; its checks run on empty lists).
int __fastcall FreePlexChain(CPlex* head);

// ---- CList ----------------------------------------------------------------------------------------

template <typename T, typename E = PlainElements<T>>
struct ListAbi : CList<T, E> {
    using Element = T;
    using List = CList<T, E>;
    using Node = typename List::Node;

    // Serialize (vtable slot 2, RET 4; defined in mfc_slots.h): the count, then each element from the
    // head. Loading reads each into a local and adds it at the tail.
    void Serialize(CArchive* ar);
    // The same stored form, loading through a temporary of `Through` (a ListTemporary, mfc_slots.h).
    template <typename Through>
    void SerializeThrough(CArchive* ar);

    // The instantiations' NewNode(pPrev, pNext) (RET 8: the node, its element constructed), FreeNode(pNode)
    // (RET 4: the element destroyed, the node onto the free list; the last one frees every block),
    // RemoveAt(position) (RET 4: unlinked, then FreeNode) and RemoveAll (plain RET: the elements
    // destroyed, every block freed) are CList's own members (mfc.h), registered through this type.
    // AddTail / AddHead with ARG_TYPE by value (RET 4): the new node (POSITION).
    Node* AddTailByValue(T value) { return List::AddTail(value); }
    // RemoveHead (plain RET): the head's element (EAX).
    static T __fastcall RemoveHeadByValue(List* self) { return self->RemoveHead(); }

    // The destructor (the vtable of `Class` written first): RemoveAll.
    template <typename Class>
    static void __fastcall Destruct(List* self) {
        self->vtable = Class::Vtable();
        self->RemoveAll();
    }
};

// The constructor of every instantiation: the vtable of `Class`, empty, the block size given (RET 4)
// or fixed (plain RET). Returns this.
template <typename Class>
struct ListHeader : CList<void*> {
    using List = CList<void*>;
    List* Construct(int nodesPerBlock) {
        this->vtable = Class::Vtable();
        this->head = nullptr;
        this->tail = nullptr;
        this->count = 0;
        this->free = nullptr;
        this->blocks = nullptr;
        this->blockSize = nodesPerBlock;
        return this;
    }
    template <int BlockSize>
    List* ConstructFixed() {
        return Construct(BlockSize);
    }
};

// The CList constructor on a list inside an element or a layout: the vtable of `Class`, empty,
// `blockSize` nodes a block. Returns the list.
template <typename Class>
CList<void*>* ConstructListHeader(void* at, int blockSize) {
    return static_cast<ListHeader<Class>*>(static_cast<CList<void*>*>(at))->Construct(blockSize);
}

// The destructor of a list on the stack or in a layout (ListAbi::Destruct on the list's own type):
// the vtable of `Class`, then RemoveAll.
template <typename Class, typename T, typename E>
void DestructList(CList<T, E>& list) {
    list.vtable = Class::Vtable();
    list.RemoveAll();
}

// A list declared with another element policy (or another element type of the same size) seen as
// the instantiation `Abi` of the same layout, whose out-of-line code builds and destroys the
// elements (ArrayAs's counterpart; a list of raw bytes takes any instantiation); its nodes likewise.
template <typename Abi, typename T, typename E>
typename Abi::List* ListAs(CList<T, E>* list) {
    return reinterpret_cast<typename Abi::List*>(list);
}
template <typename Abi, typename Node>
typename Abi::Node* NodeAs(Node* node) {
    return reinterpret_cast<typename Abi::Node*>(node);
}
// NewNode(prev, next), FreeNode(node) and RemoveAt(node) of the list as `Abi`.
template <typename Abi, typename T, typename E>
typename CList<T, E>::Node* NewNodeAs(CList<T, E>* list, typename CList<T, E>::Node* prev,
                                      typename CList<T, E>::Node* next) {
    return reinterpret_cast<typename CList<T, E>::Node*>(
        ListAs<Abi>(list)->NewNode(NodeAs<Abi>(prev), NodeAs<Abi>(next)));
}
template <typename Abi, typename T, typename E>
void FreeNodeAs(CList<T, E>* list, typename CList<T, E>::Node* node) {
    ListAs<Abi>(list)->FreeNode(NodeAs<Abi>(node));
}
template <typename Abi, typename T, typename E>
void RemoveAtAs(CList<T, E>* list, typename CList<T, E>::Node* node) {
    ListAs<Abi>(list)->RemoveAt(NodeAs<Abi>(node));
}

// ---- CMap -----------------------------------------------------------------------------------------

// MFC's HashKey for DWORD-sized keys: the key (its low dword for a 64-bit one) shifted right by 4.
template <typename K>
std::uint32_t HashKey(const K& key) {
    return static_cast<std::uint32_t>(key) >> 4;
}

template <typename K, typename V>
struct MapAbi : CMap<K, V> {
    using Map = CMap<K, V>;
    using Assoc = typename Map::Assoc;

    // Serialize (vtable slot 2, RET 4; defined in mfc_slots.h): the count, then (when there are any)
    // key and value of each association, bucket by bucket. Loading reads each pair and sets it.
    void Serialize(CArchive* ar);

    // The instantiations' InitHashTable, GetAssocAt, NewAssoc, operator[] (ElementAt) and RemoveAll
    // (elements without destructors) are CMap's own members (mfc.h), registered through this type.

    // The destructor (the vtable of `Class` written first): RemoveAll.
    template <typename Class>
    static void __fastcall Destruct(Map* self) {
        self->vtable = Class::Vtable();
        self->RemoveAll();
    }
};

// A map as an element (the constructor inlined by ConstructElements after its memset): the vtable of
// `Class`, no table, 17 buckets, blocks of 10.
template <typename Class>
void InitMap(CMap<std::uint32_t, std::uint32_t>* p) {
    p->vtable = Class::Vtable();
    p->table = nullptr;
    p->hashSize = 17;
    p->count = 0;
    p->freeList = nullptr;
    p->blocks = nullptr;
    p->blockSize = 10;
}

// ---- The instantiations' vtables (game data) ------------------------------------------------------

struct PtrListClass;   // CPtrList (the lists the code builds on the stack; blocks of 10)
struct ItemListClass;   // the item and object pointer lists (CTypedPtrList)
struct TempListClass;   // a pointer list built around each item creation (world quests)
struct CellListClass;   // the units' path cell lists
struct LootListClass;   // the loot lists
struct PlatformListClass;   // the client maps' platform lists
struct ClientListClass;   // the client map's lists (world_client_maps 0056FD40)
struct ClientList2Class;   // 00570EA0's list
struct ClientList3Class;   // 005715D0's list
struct NodeListClass;   // the AI map's node lists
struct BlockCostListClass;   // the AI's block cost lists
struct AiMapListClass;   // the AI map's lists
struct WaypointListClass;   // the AI plan's waypoint lists
struct StaticListClass;   // the list a static initialiser builds (005CDAD0)
struct DwordMapClass;   // the scene loader's CMap<DWORD, DWORD> (and the maps a node record holds)
struct ScriptCommandsClass;   // CArray<SSCCmd> (a script block, a CList element)

// ---- Elements ------------------------------------------------------------------------------------

// 8-byte lightning records: 0, then -1 (004CEAD0).
struct Lightning8 {
    int object;
    int id;
};
inline void InitLightning8(Lightning8* p) {
    p->id = -1;
    p->object = 0;
}

// ---- The instantiations --------------------------------------------------------------------------

using LightningElements = InitElements<Lightning8, InitLightning8>;
using FxSourceElement = WithString<0x34, 8>;   // the FX source: its name (+8) empty
using FxSourceElements = InitElements<FxSourceElement, InitWithString<0x34, 8>>;

using PtrList = ListAbi<void*>;
using List8 = ListAbi<Bytes<8>>;
using List12 = ListAbi<Bytes<12>>;
using ParticleSourceList = ListAbi<Bytes<0x24>>;
using List1C = ListAbi<Bytes<0x1C>>;
using List4C = ListAbi<Bytes<0x4C>>;
using LightningList = ListAbi<Lightning8, LightningElements>;
using FxSourceList = ListAbi<FxSourceElement, FxSourceElements>;

// The ConstructElements helpers those NewNodes call (__stdcall, RET 8).
using ParticleSourceArray = ArrayAbi<Bytes<0x24>>;   // 0x24-byte particle sources: the zero fill
using Plain4C = ArrayAbi<Bytes<0x4C>>;
using LightningArray = ArrayAbi<Lightning8, LightningElements>;
using FxSourceArray = ArrayAbi<FxSourceElement, FxSourceElements>;

using Map32 = MapAbi<std::uint32_t, std::uint32_t>;
using Map64x32 = MapAbi<std::uint64_t, std::uint32_t>;
using Map64x64 = MapAbi<std::uint64_t, std::uint64_t>;

} // namespace mfc::impl
