#pragma once

// STLport's list<T> as compiled into game.exe (stlp.h includes it; docs/notes/standalone.md, section 2). One
// pointer to the sentinel node the constructor allocates (its next is the first element, its prev the last; an
// empty list's sentinel points at itself). Nodes are next, prev, then the element, from the node allocator. One
// body for every element type, the element's copy and destruction supplied by an element policy (stlp_list_refs.h).

#include "stlp_alloc.h"

#include <cstddef>
#include <new>

namespace stlp {

// How an instantiation copies an element into a new node (_M_create_node) and destroys one (erase,
// clear): T's own copy constructor and destructor. Holders whose copy the game writes as functions
// of the holder (RefPtr's Share, ...) get policies of their own (stlp_list_refs.h).
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

    // The constructor's sentinel (from the pool, linked to itself) and the destructor (clear, then the sentinel
    // back to the pool).
    void Construct() {
        head = allocate_as<Node>();
        head->next = head;
        head->prev = head;
    }
    void Destroy() {
        ClearWith([](T*) {});
        deallocate(head, sizeof(Node));
    }

    // clear() as STLport writes it, for an instantiation that inlines it: each element destroyed by
    // `destroy(&data)` (its own destructor), the node returned to the pool, the sentinel linked to itself.
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

    template <typename E = Element<T>>
    static Node* CreateNode(const T& x) {
        Node* node = allocate_as<Node>();
        E::Copy(&node->data, x);
        return node;
    }
    // insert(pos, x): _M_create_node(x), linked in before `pos`. The list itself is not read.
    template <typename E = Element<T>>
    static Node* Insert(Node* pos, const T& x) {
        Node* node = CreateNode<E>(x);
        node->next = pos;
        node->prev = pos->prev;
        pos->prev->next = node;
        pos->prev = node;
        return node;
    }
    // insert(pos, first, last): a copy of each element of [first, last) inserted before `pos`, in order.
    template <typename E = Element<T>>
    static void InsertRange(Node* pos, const Node* first, const Node* last) {
        for (; first != last; first = first->next) Insert<E>(pos, first->data);
    }
    // erase(pos): unlinked first, then the element destroyed and the node returned
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
    template <typename E = Element<T>>
    void CopyConstruct(const list& from) {
        Construct();
        InsertRange<E>(head, from.First(), from.End());
    }
    template <typename E = Element<T>>
    void Clear() {
        ClearWith([](T* p) { E::Destroy(p); });
    }
    template <typename E = Element<T>>
    void Release() {
        Clear<E>();
        deallocate(head, sizeof(Node));
    }
    // remove(value): erases every element `same` as `value`; `value` is read again
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
    static void Distance(const Node* first, const Node* last, int& n) {
        for (; first != last; first = first->next) ++n;
    }
};
static_assert(sizeof(list<int>) == 4 && sizeof(list<int>::Node) == 12);

// A node declared by its own struct with list<T>::Node's layout (next, prev, then the element: the
// speech queue's, the spell modifiers', the sessions' driver and client nodes) seen as list<T>'s,
// for the statics above.
template <typename T, typename Node>
typename list<T>::Node* AsNode(Node* node) {
    static_assert(sizeof(Node) == sizeof(typename list<T>::Node));
    return reinterpret_cast<typename list<T>::Node*>(node);
}

} // namespace stlp
