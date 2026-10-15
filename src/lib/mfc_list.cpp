// CPlex and the game's CList<T> / CMap<K, V> instantiations whose elements need nothing of the game
// (mfc_list.h), registered at their own addresses so the standalone image has them; the forwarders
// in src/game keep calling these addresses. Checked by tests/verify/verify_mfc_list.cpp. The groups
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
EI_REPLACE(0x006FE8EF, CreatePlex);

int __fastcall FreePlexChain(CPlex* head) {
    while (head) {
        CPlex* next = head->next;
        gamecrt::operator_delete(head);
        head = next;
    }
    return 0;
}
EI_REPLACE(0x006FE90F, FreePlexChain);

// ---- Pointer lists (CPtrList / CTypedPtrList: 4-byte elements, zeroed, nothing destroyed) ---------

// The constructor (block size given; 00545750 blocks of 0x80).
EI_REPLACE(0x004C96A0, ListHeader<PtrListClass>::Construct);
EI_REPLACE(0x004C98C0, ListHeader<ItemListClass>::Construct);
EI_REPLACE(0x00556D10, ListHeader<CellListClass>::Construct);
EI_REPLACE(0x0056FD40, ListHeader<ClientListClass>::Construct);
EI_REPLACE(0x00570EA0, ListHeader<ClientList2Class>::Construct);
EI_REPLACE(0x005715D0, ListHeader<ClientList3Class>::Construct);
EI_REPLACE(0x00598EC0, ListHeader<TempListClass>::Construct);
EI_REPLACE(0x005CDAD0, ListHeader<StaticListClass>::Construct);
EI_REPLACE(0x00545750, ListHeader<CellListClass>::ConstructFixed<0x80>);

// The destructor: the vtable, RemoveAll.
EI_REPLACE(0x00405130, PtrList::Destruct<TempListClass>);
EI_REPLACE(0x00427830, PtrList::Destruct<ItemListClass>);
EI_REPLACE(0x004C96F0, PtrList::Destruct<PtrListClass>);
EI_REPLACE(0x00539890, PtrList::Destruct<CellListClass>);
EI_REPLACE(0x00558470, PtrList::Destruct<LootListClass>);
EI_REPLACE(0x0056FE40, PtrList::Destruct<PlatformListClass>);
EI_REPLACE(0x005995E0, PtrList::Destruct<NodeListClass>);
EI_REPLACE(0x005B1810, PtrList::Destruct<BlockCostListClass>);
EI_REPLACE(0x005B19A0, PtrList::Destruct<AiMapListClass>);
EI_REPLACE(0x005BDEE0, PtrList::Destruct<WaypointListClass>);

// NewNode (the zero fill inlined, or through a ConstructElements helper that only zeroes).
EI_REPLACE(0x004069A0, PtrList::NewNode);
EI_REPLACE(0x004621A0, PtrList::NewNode);
EI_REPLACE(0x0052CEF0, PtrList::NewNode);
EI_REPLACE(0x00573290, PtrList::NewNode);
EI_REPLACE(0x00429EA0, PtrList::NewNode);
EI_REPLACE(0x0052CDB0, PtrList::NewNode);
EI_REPLACE(0x0059C7A0, PtrList::NewNode);

// AddTail (NewNode inlined), RemoveHead, RemoveAt, FreeNode, RemoveAll.
EI_REPLACE(0x00405040, PtrList::AddTailByValue);
EI_REPLACE(0x004C98F0, PtrList::AddTailByValue);
EI_REPLACE(0x0059CC60, PtrList::AddTailByValue);
EI_REPLACE(0x00404FE0, PtrList::RemoveHeadByValue);
EI_REPLACE(0x004050C0, PtrList::RemoveAt);
EI_REPLACE(0x0056FDD0, PtrList::RemoveAt);
EI_REPLACE(0x005704A0, PtrList::RemoveAt);
EI_REPLACE(0x0059A680, PtrList::RemoveAt);
EI_REPLACE(0x005588A0, PtrList::FreeNode);
EI_REPLACE(0x00558C00, PtrList::FreeNode);
EI_REPLACE(0x00573300, PtrList::FreeNode);
EI_REPLACE(0x00429F10, PtrList::RemoveAll);
EI_REPLACE(0x0052CA10, PtrList::RemoveAll);
EI_REPLACE(0x00556D40, PtrList::RemoveAll);
EI_REPLACE(0x00558C50, PtrList::RemoveAll);
EI_REPLACE(0x0056FDA0, PtrList::RemoveAll);
EI_REPLACE(0x00570470, PtrList::RemoveAll);
EI_REPLACE(0x0059D5C0, PtrList::RemoveAll);

// ---- NewNode of lists of larger elements built without game code ----------------------------------

EI_REPLACE(0x0059D5F0, List8::NewNode);        // 0x10-byte nodes
EI_REPLACE(0x00558B10, List12::NewNode);       // 0x14 (the armour spells)
EI_REPLACE(0x005BEDB0, List12::NewNode);       // 0x14 (the AI plan's waypoints)
EI_REPLACE(0x004CD540, ParticleSourceList::NewNode);       // 0x2C (the script's particle sources)
EI_REPLACE(0x005BE7B0, List1C::NewNode);       // 0x24 (the zeroing inlined)
EI_REPLACE(0x00573000, List4C::NewNode);       // 0x54 (the client maps' platforms)
EI_REPLACE(0x004CD620, LightningList::NewNode);   // 0x10: 0, then -1
EI_REPLACE(0x004CD5B0, FxSourceList::NewNode);    // 0x3C: the name empty
EI_REPLACE(0x004CD6D0, CommandBlockList::NewNode);   // 0x20: a CArray<SSCCmd> header
EI_REPLACE(0x0059CA60, SharedObjectNewList::NewNode);   // 0x28: a header at +0x0C

// Their ConstructElements helpers (RET 8; the zero fill, then the element's inlined constructor).
EI_REPLACE(0x004CE580, Plain4::ConstructElements);
EI_REPLACE(0x0059E2C0, Plain4::ConstructElements);
EI_REPLACE(0x00479620, Plain4::ConstructElements);   // counted references (S6e)
EI_REPLACE(0x00558EF0, Plain4::ConstructElements);   // counted references (S6e)
EI_REPLACE(0x0059E390, Plain8::ConstructElements);
EI_REPLACE(0x00558EA0, Plain12::ConstructElements);
EI_REPLACE(0x005BF320, Plain12::ConstructElements);
EI_REPLACE(0x004CEA30, ParticleSourceArray::ConstructElements);
EI_REPLACE(0x00575660, Plain4C::ConstructElements);
EI_REPLACE(0x004CEAD0, LightningArray::ConstructElements);
EI_REPLACE(0x004CEA80, FxSourceArray::ConstructElements);
EI_REPLACE(0x004CEB20, CommandBlockArray::ConstructElements);
EI_REPLACE(0x0059E330, SharedObjectArray::ConstructElements);

// ---- CMap -----------------------------------------------------------------------------------------

// InitHashTable (the same code for every key and value).
EI_REPLACE(0x00453B90, Map32::InitHashTable);
EI_REPLACE(0x00453FD0, Map32::InitHashTable);
EI_REPLACE(0x004540F0, Map32::InitHashTable);
EI_REPLACE(0x00454210, Map32::InitHashTable);

// GetAssocAt: 4-byte keys; 8-byte keys (hashed by the low dword, compared whole).
EI_REPLACE(0x00453C60, Map32::GetAssocAt);
EI_REPLACE(0x004542E0, Map32::GetAssocAt);
EI_REPLACE(0x00477340, Map32::GetAssocAt);
EI_REPLACE(0x004540A0, Map64x64::GetAssocAt);
EI_REPLACE(0x004541C0, Map64x32::GetAssocAt);

// NewAssoc: 0x10-, 0x18- and 0x14-byte associations.
EI_REPLACE(0x00453C00, Map32::NewAssoc);
EI_REPLACE(0x00454280, Map32::NewAssoc);
EI_REPLACE(0x00454040, Map64x64::NewAssoc);
EI_REPLACE(0x00454160, Map64x32::NewAssoc);

// operator[], RemoveAll, the destructor, ConstructElements of an array of maps.
EI_REPLACE(0x00476050, Map32::At);
EI_REPLACE(0x00453F80, Map32::RemoveAll);
EI_REPLACE(0x00476140, Map32::Destruct<DwordMapClass>);

void __stdcall ConstructDwordMaps(CMap<std::uint32_t, std::uint32_t>* maps, int count) {
    MapElements::Construct(maps, count);
}
EI_REPLACE(0x00477380, ConstructDwordMaps);

} // namespace mfc::impl
