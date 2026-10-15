#pragma once

// STLport's list<T> as compiled into game.exe (stlp.h includes it; docs/notes/standalone.md,
// section 2).
//
// One pointer to the sentinel node the constructor allocates (its next is the first element, its
// prev the last; an empty list's sentinel points at itself). Nodes are next, prev, then the element,
// from the node allocator. The game's instantiations of the members below (constructor, begin / end,
// insert, erase, clear, destructor, distance) are registered at their own addresses in
// stlp_list.cpp through the ListAbi shims: one body for every element type, the element's copy and
// destruction supplied by an element policy.

#include "stlp_alloc.h"

#include <cstddef>
#include <new>

namespace stlp {

// How an instantiation copies an element into a new node (_M_create_node) and destroys one (erase,
// clear): T's own copy constructor and destructor. Holders whose copy the game writes as functions
// of the holder (RefPtr's Share, ...) get policies of their own (stlp_list.cpp).
template <typename T>
struct Element {
    static void Copy(T* to, const T& from) { new (to) T(from); }
    static void Destroy(T* p) { p->~T(); }
};

template <typename T>
struct list {
    struct Node {
        Node* next;
        Node* prev;
        T data;
    };

    Node* head;   // the sentinel

    Node* First() const { return head->next; }
    Node* End() const { return head; }
    bool Empty() const { return head->next == head; }

    // For a local list the original builds and drops in one function (e.g. the id lists of
    // 00498480): the constructor (00404A70: a sentinel from the pool, linked to itself),
    // push_back (insert(end(), value), 00404A40: one node from the pool) and the destructor
    // (clear, then the sentinel back to the pool), allocating in the original's order.
    void Construct() {
        head = static_cast<Node*>(allocate(sizeof(Node)));
        head->next = head;
        head->prev = head;
    }
    void PushBack(const T& value) {
        Node* node = static_cast<Node*>(allocate(sizeof(Node)));
        new (&node->data) T(value);
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;
    }
    void Destroy() {
        ClearWith([](T*) {});
        deallocate(head, sizeof(Node));
    }

    // clear() as STLport writes it, for an instantiation the original inlines: each element
    // destroyed by `destroy(&data)` (its own destructor), the node returned to the pool, the
    // sentinel linked to itself.
    template <typename F> void ClearWith(F destroy) {
        Node* cur = head->next;
        while (cur != head) {
            Node* node = cur;
            cur = cur->next;
            destroy(&node->data);
            deallocate(node, sizeof(Node));
        }
        head->next = head;
        head->prev = head;
    }

    // _M_create_node(x) (e.g. 00425710): a node from the pool, then the element copied into it.
    template <typename E = Element<T>>
    static Node* CreateNode(const T& x) {
        Node* node = static_cast<Node*>(allocate(sizeof(Node)));
        E::Copy(&node->data, x);
        return node;
    }
    // insert(pos, x) (e.g. 00404A40): _M_create_node(x), linked in before `pos`. The list itself
    // is not read.
    template <typename E = Element<T>>
    static Node* Insert(Node* pos, const T& x) {
        Node* node = CreateNode<E>(x);
        node->next = pos;
        node->prev = pos->prev;
        pos->prev->next = node;
        pos->prev = node;
        return node;
    }
    // insert(pos) (e.g. 0040A8D0): insert(pos, T()). The default element is made on the stack
    // (E::Default), copied into the new node as insert(pos, x) copies, then destroyed (E::Destroy).
    template <typename E = Element<T>>
    static Node* InsertDefault(Node* pos) {
        alignas(T) unsigned char storage[sizeof(T)];
        T* value = reinterpret_cast<T*>(storage);
        E::Default(value);
        Node* node = Insert<E>(pos, *value);
        E::Destroy(value);
        return node;
    }
    // insert(pos, first, last) (e.g. 004795B0): a copy of each element of [first, last) inserted
    // before `pos`, in order.
    template <typename E = Element<T>>
    static void InsertRange(Node* pos, const Node* first, const Node* last) {
        for (; first != last; first = first->next) Insert<E>(pos, first->data);
    }
    // erase(pos) (e.g. 00420290): unlinked first, then the element destroyed and the node returned
    // to the pool; returns the node that followed it.
    template <typename E = Element<T>>
    static Node* Erase(Node* pos) {
        Node* next = pos->next;
        Node* prev = pos->prev;
        prev->next = next;
        next->prev = prev;
        E::Destroy(&pos->data);
        deallocate(pos, sizeof(Node));
        return next;
    }
    // clear() (e.g. 00427100).
    template <typename E = Element<T>>
    void Clear() {
        ClearWith([](T* p) { E::Destroy(p); });
    }
    // ~list() (e.g. 00423370): clear(), then the sentinel back to the pool.
    template <typename E = Element<T>>
    void Release() {
        Clear<E>();
        deallocate(head, sizeof(Node));
    }
    // remove(value) (e.g. 00427320): erases every element `same` as `value`; `value` is read again
    // for each node (it may be one of the elements).
    template <typename E = Element<T>, typename Same>
    void Remove(const T& value, Same same) {
        Node* last = head;
        for (Node* first = head->next; first != last;) {
            Node* next = first->next;
            if (same(first->data, value)) Erase<E>(first);
            first = next;
        }
    }
    // distance(first, last, n) (e.g. 00429040): adds the number of nodes from first to last to n.
    static void Distance(const Node* first, const Node* last, int& n) {
        for (; first != last; first = first->next) ++n;
    }
    // _List_global::_Transfer(pos, first, last), inlined into the splices: [first, last) unlinked
    // and linked in before `pos`, unless pos is `last`.
    static void Transfer(Node* pos, Node* first, Node* last) {
        if (pos == last) return;
        last->prev->next = pos;
        first->prev->next = last;
        pos->prev->next = first;
        Node* tmp = pos->prev;
        pos->prev = last->prev;
        last->prev = first->prev;
        first->prev = tmp;
    }
    // splice(pos, x, i) (e.g. 004380C0): the node `i` moved before `pos` (nothing when pos is i or
    // the node after it).
    static void SpliceOne(Node* pos, Node* i) {
        Node* j = i->next;
        if (pos == i || pos == j) return;
        Transfer(pos, i, j);
    }
    // splice(pos, x, first, last) (e.g. 00438770).
    static void SpliceRange(Node* pos, Node* first, Node* last) {
        if (first != last) Transfer(pos, first, last);
    }
};
static_assert(sizeof(list<int>) == 4 && sizeof(list<int>::Node) == 12);

// Instantiations seen only through their node size (the constructors and the destructors of plain
// data never touch an element).
template <std::size_t NodeBytes>
using OpaqueList = list<Opaque<NodeBytes - 8>>;
static_assert(sizeof(OpaqueList<16>::Node) == 16);

// The game's instantiations are __thiscall members, declared here as __fastcall functions with the
// unused EDX second. An iterator (one node pointer) comes back through a hidden pointer the caller
// passes first; the function returns that pointer in EAX.
template <typename T, typename E = Element<T>>
struct ListAbi {
    using List = list<T>;
    using Node = typename List::Node;

    // list(const allocator_type&) (e.g. 00404A70; RET 4): returns this.
    static List* __fastcall Construct(List* self, void*, const void* /*allocator*/) {
        self->Construct();
        return self;
    }
    // list() (00562A80): the same without the allocator argument.
    static List* __fastcall ConstructDefault(List* self) {
        self->Construct();
        return self;
    }
    // ~list() (e.g. 00423370).
    static void __fastcall Destruct(List* self) { self->template Release<E>(); }
    // clear() (e.g. 00427100).
    static void __fastcall Clear(List* self) { self->template Clear<E>(); }
    // _M_create_node(x) (e.g. 00482C30; RET 4): the list is not read; the node's links are left as
    // the pool had them.
    static Node* __fastcall CreateNode(List*, void*, const T* x) { return List::template CreateNode<E>(*x); }
    // insert(pos, x) (e.g. 00404A40; RET 0xC). The list is not read (0062C090 is called with any
    // ECX).
    static Node** __fastcall Insert(List*, void*, Node** out, Node* pos, const T* x) {
        *out = List::template Insert<E>(pos, *x);
        return out;
    }
    // insert(pos) (e.g. 0040A8D0; RET 8): insert(pos, T()).
    static Node** __fastcall InsertDefault(List*, void*, Node** out, Node* pos) {
        *out = List::template InsertDefault<E>(pos);
        return out;
    }
    // insert(pos, first, last) (e.g. 004795B0; RET 0xC).
    static void __fastcall InsertRange(List*, void*, Node* pos, const Node* first, const Node* last) {
        List::template InsertRange<E>(pos, first, last);
    }
    // push_back(x) (e.g. 004207C0; RET 4): insert(end(), x).
    static void __fastcall PushBack(List* self, void*, const T* x) { List::template Insert<E>(self->head, *x); }
    // push_back() (0040A6B0): insert(end()).
    static void __fastcall PushBackDefault(List* self) { List::template InsertDefault<E>(self->head); }
    // pop_back() (0040A6D0): erase(--end()).
    static void __fastcall PopBack(List* self) { List::template Erase<E>(self->head->prev); }
    // erase(pos) (e.g. 00420290; RET 8): the list itself is not read.
    static Node** __fastcall Erase(List*, void*, Node** out, Node* pos) {
        *out = List::template Erase<E>(pos);
        return out;
    }
};
// The element policies of the instantiations that hold the engine's reference holders are in
// stlp_list_refs.h.

// The members that never touch an element, one body for all instantiations (stlp_list.cpp).
using AnyList = OpaqueList<16>;
// begin() (e.g. 0040A9F0) and end() (e.g. 004049D0; RET 4).
AnyList::Node** __fastcall ListBegin(const AnyList* self, void*, AnyList::Node** out);
AnyList::Node** __fastcall ListEnd(const AnyList* self, void*, AnyList::Node** out);
// distance(first, last, n) (e.g. 00429040; RET 8): ECX the count.
void __fastcall ListDistance(int* n, void*, const AnyList::Node* first, const AnyList::Node* last);
// The sentinel given back while a list's constructor unwinds (00482790, 004FB840; only exception
// unwind funclets call them): a non-null node back to the pool as a block of NodeBytes.
template <std::size_t NodeBytes>
void __fastcall ListPutSentinel(OpaqueList<NodeBytes>* self) {
    if (self->head) deallocate(self->head, NodeBytes);
}
// The iterator's copy constructor (004387C0; RET 4): returns this.
AnyList::Node** __fastcall ListIteratorCopy(AnyList::Node** self, void*, AnyList::Node* const* from);
// --it (00439A60, 004FB570): returns it.
AnyList::Node** __fastcall ListIteratorDecrement(AnyList::Node** it);
// splice(pos, x, i) (004380C0; RET 0xC) and splice(pos, x, first, last) (00438770; RET 0x10):
// neither list is read.
void __fastcall ListSpliceOne(AnyList* self, void*, AnyList::Node* pos, AnyList* x, AnyList::Node* i);
void __fastcall ListSpliceRange(AnyList* self, void*, AnyList::Node* pos, AnyList* x, AnyList::Node* first,
                                AnyList::Node* last);
// remove(value) of a list<int> (00511FB0; RET 4; S16a: the light factory's lit ids): every element equal
// to *value (read again for each node) erased, through 00512030.
void __fastcall ListRemoveInt(list<int>* self, void*, const int* value);

} // namespace stlp
