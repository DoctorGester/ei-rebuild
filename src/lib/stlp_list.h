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

#include <cstddef>
#include <new>

namespace stlp {

void* allocate(std::size_t bytes);                  // 00405950
void deallocate(void* p, std::size_t bytes);        // 00404D70

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
    // insert(pos, x) (e.g. 00404A40): _M_create_node(x), linked in before `pos`.
    template <typename E = Element<T>>
    Node* Insert(Node* pos, const T& x) {
        Node* node = CreateNode<E>(x);
        node->next = pos;
        node->prev = pos->prev;
        pos->prev->next = node;
        pos->prev = node;
        return node;
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
};
static_assert(sizeof(list<int>) == 4 && sizeof(list<int>::Node) == 12);

// An element as raw bytes, for instantiations seen only through their node size (the constructors
// and the destructors of plain data never touch an element; the size picks the pool's free list).
template <std::size_t Bytes>
struct Opaque {
    unsigned char bytes[Bytes];
};
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
    // insert(pos, x) (e.g. 00404A40; RET 0xC).
    static Node** __fastcall Insert(List* self, void*, Node** out, Node* pos, const T* x) {
        *out = self->template Insert<E>(pos, *x);
        return out;
    }
    // push_back(x) (e.g. 004207C0; RET 4): insert(end(), x).
    static void __fastcall PushBack(List* self, void*, const T* x) { self->template Insert<E>(self->head, *x); }
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

} // namespace stlp
