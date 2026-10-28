// CPlex and the game's CList<T> instantiations whose elements need nothing of the game
// (mfc_list.h); our code calls them by name. The groups
// were found by comparing the instructions of every instantiation (addresses
// masked); each node size is the one its NewNode passes to CPlex::Create.
#include "mfc_list.h"
#include "mfc_slots.h"

#include "lib/crt.h"

namespace mfc::impl {


CPlex* CreatePlex(CPlex*& head, unsigned count, unsigned elementSize) {
    auto* block = static_cast<CPlex*>(::operator new(sizeof(CPlex) + count * elementSize));
    block->next = head;
    head = block;
    return block;
}

int FreePlexChain(CPlex* head) {
    while (head) {
        CPlex* next = head->next;
        ::operator delete(head);
        head = next;
    }
    return 0;
}

} // namespace mfc::impl
