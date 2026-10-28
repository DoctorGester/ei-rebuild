#pragma once

// MFC 4.2's CList<TYPE, ARG_TYPE> (afxtempl.h) and CPlex (afxplex_.h) as game.exe instantiates them
// (docs/notes/standalone.md section 2). The list bodies are mfc::CList's (mfc.h); the instantiations differ in
// the element: its size (read from each NewNode's CPlex::Create) and the element policy (mfc_array.h). ListAbi
// turns the members into the instantiations' calling conventions; mfc_list.cpp has the library-only instantiations,
// src/game/game_lists.cpp the ones whose elements are game types.

#include "mfc_array.h"
#include "game/game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mfc::impl {


// CPlex::Create (__stdcall): a block of `count` elements of `elementSize` bytes
// after the CPlex header (operator new), linked in front of `head`. Returns the block.
CPlex* CreatePlex(CPlex*& head, unsigned count, unsigned elementSize);
// CPlex::FreeDataChain (__thiscall, `this` may be null): every block of the chain freed.
// Returns 0: a null chain leaves EAX as the caller had it, and AIPath::Reset
// returns that EAX (0 there; its checks run on empty lists).
int FreePlexChain(CPlex* head);


template <typename T, typename E = PlainElements<T>>
struct ListAbi : CList<T, E> {
    using Element = T;
    using List = CList<T, E>;
    using Node = typename List::Node;

    // Serialize (vtable slot 2; defined in mfc_slots.h): the count, then each element from the
    // head. Loading reads each into a local and adds it at the tail.
    void Serialize(CArchive* ar);
    // The same stored form, loading through a temporary of `Through` (a ListTemporary, mfc_slots.h).
    template <typename Through>
    void SerializeThrough(CArchive* ar);

    // The instantiations' NewNode(pPrev, pNext) (the node, its element constructed), FreeNode(pNode)
    // (the element destroyed, the node onto the free list; the last one frees every block),
    // RemoveAt(position) (unlinked, then FreeNode) and RemoveAll (the elements
    // destroyed, every block freed) are CList's own members (mfc.h), reached through this type.
    // AddTail / AddHead with ARG_TYPE by value: the new node (POSITION).
    Node* AddTailByValue(T value) { return List::AddTail(value); }
    // RemoveHead: the head's element.
    static T RemoveHeadByValue(List* self) { return self->RemoveHead(); }

    // The destructor (the vtable of `Class` written first): RemoveAll.
    template <typename Class>
    static void Destruct(List* self) {
        self->vtable = Class::Vtable();
        self->RemoveAll();
    }
};

// The constructor of every instantiation: the vtable of `Class`, empty, the block size given
// or fixed. Returns this.
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
// elements (ArrayAs's counterpart; the nodes must be the same size); its nodes likewise.
template <typename Abi, typename T, typename E>
typename Abi::List* ListAs(CList<T, E>* list) {
    static_assert(sizeof(typename Abi::List::Node) == sizeof(typename CList<T, E>::Node));
    return reinterpret_cast<typename Abi::List*>(list);
}
template <typename Abi, typename Node>
typename Abi::Node* NodeAs(Node* node) {
    static_assert(sizeof(typename Abi::Node) == sizeof(Node));
    return reinterpret_cast<typename Abi::Node*>(node);
}
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

// ---- The instantiations' vtables (game data) ------------------------------------------------------

// ---- Elements ------------------------------------------------------------------------------------

// 8-byte lightning records: 0, then -1.
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

using PtrList = ListAbi<void*>;
using List8 = ListAbi<Bytes<8>>;
using List12 = ListAbi<Bytes<12>>;
using ParticleSourceList = ListAbi<Bytes<0x24>>;
using List1C = ListAbi<Bytes<0x1C>>;
using List4C = ListAbi<Bytes<0x4C>>;
using LightningList = ListAbi<Lightning8, LightningElements>;



} // namespace mfc::impl
