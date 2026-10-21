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
struct ListAbi {
    using Element = T;
    using List = CList<T, E>;
    using Node = typename List::Node;

    // NewNode(pPrev, pNext) (RET 8): the node, its element constructed.
    static Node* __fastcall NewNode(List* self, void*, Node* prev, Node* next) { return self->NewNode(prev, next); }
    // FreeNode(pNode) (RET 4): the element destroyed, the node onto the free list; the last one
    // frees every block (RemoveAll).
    static void __fastcall FreeNode(List* self, void*, Node* node) { self->FreeNode(node); }
    // RemoveAt(position) (RET 4): unlinked, then FreeNode.
    static void __fastcall RemoveAt(List* self, void*, Node* node) { self->RemoveAt(node); }
    // RemoveAll (plain RET): the elements destroyed, every block freed.
    static void __fastcall RemoveAll(List* self) { self->RemoveAll(); }
    // AddTail / AddHead with ARG_TYPE by value (RET 4): the new node (POSITION).
    static Node* __fastcall AddTailByValue(List* self, void*, T value) { return self->AddTail(value); }
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
struct ListHeader {
    using List = CList<void*>;
    static List* __fastcall Construct(List* self, void*, int blockSize) {
        self->vtable = Class::Vtable();
        self->head = nullptr;
        self->tail = nullptr;
        self->count = 0;
        self->free = nullptr;
        self->blocks = nullptr;
        self->blockSize = blockSize;
        return self;
    }
    template <int BlockSize>
    static List* __fastcall ConstructFixed(List* self) {
        return Construct(self, nullptr, BlockSize);
    }
};

// The CList constructor on a list inside an element or a layout: the vtable of `Class`, empty,
// `blockSize` nodes a block. Returns the list.
template <typename Class>
CList<void*>* ConstructListHeader(void* at, int blockSize) {
    return ListHeader<Class>::Construct(static_cast<CList<void*>*>(at), nullptr, blockSize);
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
struct MapAbi {
    using Map = CMap<K, V>;
    using Assoc = typename Map::Assoc;

    // InitHashTable(nHashSize, bAllocNow) (RET 8): the old table deleted; a zeroed one of
    // `hashSize` buckets when `allocNow`; the size kept either way.
    static void __fastcall InitHashTable(Map* self, void*, std::uint32_t hashSize, int allocNow) {
        if (self->table) {
            gamecrt::operator_delete(self->table);
            self->table = nullptr;
        }
        if (allocNow) {
            self->table = static_cast<Assoc**>(gamecrt::operator_new(hashSize * sizeof(Assoc*)));
            std::memset(self->table, 0, hashSize * sizeof(Assoc*));
        }
        self->hashSize = hashSize;
    }

    // GetAssocAt(key, nHash&) (RET 8): the key's bucket into `hash`; its association, or null
    // (also when there is no table).
    static Assoc* __fastcall GetAssocAt(const Map* self, void*, const K* key, std::uint32_t* hash) {
        *hash = HashKey(*key) % self->hashSize;
        if (!self->table) return nullptr;
        for (Assoc* assoc = self->table[*hash]; assoc; assoc = assoc->next)
            if (assoc->key == *key) return assoc;
        return nullptr;
    }

    // NewAssoc (plain RET): an association from the free list (a new block of `blockSize` when it is
    // empty, its first association used first), the count raised, key and value zeroed. `next` and
    // `hash` are the caller's.
    static Assoc* __fastcall NewAssoc(Map* self) {
        if (!self->freeList) {
            CPlex* block = PlexCreate(self->blocks, static_cast<unsigned>(self->blockSize), sizeof(Assoc));
            Assoc* assoc = reinterpret_cast<Assoc*>(block + 1) + (self->blockSize - 1);
            for (int i = self->blockSize - 1; i >= 0; --i, --assoc) {
                assoc->next = self->freeList;
                self->freeList = assoc;
            }
        }
        Assoc* assoc = self->freeList;
        self->freeList = assoc->next;
        ++self->count;
        std::memset(static_cast<void*>(&assoc->key), 0, sizeof(K));
        std::memset(static_cast<void*>(&assoc->value), 0, sizeof(V));
        return assoc;
    }

    // operator[](key) (RET 4): the value of `key`, a new association (value zero) put at the head of
    // its bucket when missing (the table allocated first if there is none).
    static V* __fastcall ElementAt(Map* self, void*, const K* key) {
        std::uint32_t hash;
        Assoc* assoc = GetAssocAt(self, nullptr, key, &hash);
        if (!assoc) {
            if (!self->table) InitHashTable(self, nullptr, self->hashSize, 1);
            assoc = NewAssoc(self);
            assoc->hash = hash;
            assoc->key = *key;
            assoc->next = self->table[hash];
            self->table[hash] = assoc;
        }
        return &assoc->value;
    }

    // RemoveAll (plain RET; elements without destructors).
    static void __fastcall RemoveAll(Map* self) { self->RemoveAll(); }

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

struct PtrListClass {   // CPtrList (the lists the code builds on the stack; blocks of 10)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D454); }
};
struct ItemListClass {   // the item and object pointer lists (CTypedPtrList)
    static void* Vtable() { return ei::GamePointer<void>(0x0073BE80); }
};
struct TempListClass {   // a pointer list built around each item creation (world quests)
    static void* Vtable() { return ei::GamePointer<void>(0x0073B8D4); }
};
struct CellListClass {   // the units' path cell lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073EDB0); }
};
struct LootListClass {   // the loot lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073F12C); }
};
struct PlatformListClass {   // the client maps' platform lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073F548); }
};
struct ClientListClass {   // the client map's lists (world_client_maps 0056FD40)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D018); }
};
struct ClientList2Class {   // 00570EA0's list
    static void* Vtable() { return ei::GamePointer<void>(0x0073F724); }
};
struct ClientList3Class {   // 005715D0's list
    static void* Vtable() { return ei::GamePointer<void>(0x0073F73C); }
};
struct NodeListClass {   // the AI map's node lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073F9F8); }
};
struct BlockCostListClass {   // the AI's block cost lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073FCFC); }
};
struct AiMapListClass {   // the AI map's lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073FCF0); }
};
struct WaypointListClass {   // the AI plan's waypoint lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073FD9C); }
};
struct StaticListClass {   // the list a static initialiser builds (005CDAD0)
    static void* Vtable() { return ei::GamePointer<void>(0x0073FEB4); }
};
struct DwordMapClass {   // the scene loader's CMap<DWORD, DWORD> (and the maps a node record holds)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C818); }
};
struct ScriptCommandsClass {   // CArray<SSCCmd> (a script block, a CList element)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D3DC); }
};

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
using CommandBlock = WithArray<0x18, 0, ScriptCommandsClass>;
using CommandBlocks = InitElements<CommandBlock, InitWithArray<0x18, 0, ScriptCommandsClass>>;
using SharedObjectElement = WithArray<0x20, 0xC, ObjectPtrArrayClass>;   // game/world.h SharedObject
using SharedObjectElements = InitElements<SharedObjectElement, InitWithArray<0x20, 0xC, ObjectPtrArrayClass>>;

using PtrList = ListAbi<void*>;
using List8 = ListAbi<Bytes<8>>;
using List12 = ListAbi<Bytes<12>>;
using ParticleSourceList = ListAbi<Bytes<0x24>>;
using List1C = ListAbi<Bytes<0x1C>>;
using List4C = ListAbi<Bytes<0x4C>>;
using LightningList = ListAbi<Lightning8, LightningElements>;
using FxSourceList = ListAbi<FxSourceElement, FxSourceElements>;
using CommandBlockList = ListAbi<CommandBlock, CommandBlocks>;
using SharedObjectNewList = ListAbi<SharedObjectElement, SharedObjectElements>;

// The ConstructElements helpers those NewNodes call (__stdcall, RET 8).
using ParticleSourceArray = ArrayAbi<Bytes<0x24>>;   // 0x24-byte particle sources: the zero fill
using Plain4C = ArrayAbi<Bytes<0x4C>>;
using LightningArray = ArrayAbi<Lightning8, LightningElements>;
using FxSourceArray = ArrayAbi<FxSourceElement, FxSourceElements>;
using CommandBlockArray = ArrayAbi<CommandBlock, CommandBlocks>;
using SharedObjectArray = ArrayAbi<SharedObjectElement, SharedObjectElements>;

using Map32 = MapAbi<std::uint32_t, std::uint32_t>;
using Map64x32 = MapAbi<std::uint64_t, std::uint32_t>;
using Map64x64 = MapAbi<std::uint64_t, std::uint64_t>;

} // namespace mfc::impl
