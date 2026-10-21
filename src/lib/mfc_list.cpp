// CPlex and the game's CList<T> / CMap<K, V> instantiations whose elements need nothing of the game
// (mfc_list.h); registered at their own addresses where something still reaches them by game.exe's address
// (tools/registration_readers.py, C20c), our code calls them directly. The groups
// were found by comparing the instructions of every instantiation our code forwards to (addresses
// masked); each node size is the one its NewNode passes to CPlex::Create.
#include "mfc_list.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace mfc::impl {

// ---- CPlex ----------------------------------------------------------------------------------------

CPlex* __stdcall CreatePlex(CPlex*& head, unsigned count, unsigned elementSize) {
    auto* block = static_cast<CPlex*>(gamecrt::operator_new(sizeof(CPlex) + count * elementSize));
    block->next = head;
    head = block;
    return block;
}

int __fastcall FreePlexChain(CPlex* head) {
    while (head) {
        CPlex* next = head->next;
        gamecrt::operator_delete(head);
        head = next;
    }
    return 0;
}

// ---- Pointer lists (CPtrList / CTypedPtrList: 4-byte elements, zeroed, nothing destroyed) ---------

// The constructor (block size given; 00545750 blocks of 0x80).
EI_REPLACE(0x005CDAD0, ListHeader<StaticListClass>::Construct);

// The destructor: the vtable, RemoveAll.
EI_REPLACE(0x004C96F0, PtrList::Destruct<PtrListClass>);

} // namespace mfc::impl
