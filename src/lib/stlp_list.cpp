// The game's STLport list<T> instantiations (stlp_list.h), registered at their own addresses so the
// standalone image has them; the forwarders in src/game keep calling these addresses. Checked by
// tests/verify/verify_stlp_list.cpp. Instantiations whose node holds the same kind of element share
// one shim; the groups were found by comparing the instructions of every instantiation our code
// forwards to (addresses and jump targets masked).
#include "stlp_list.h"

#include "stlp_list_refs.h"
#include "stlp_pair.h"
#include "stlp_tree_abi.h"
#include "mfc.h"

#include "ei/hook.h"

#include <cstdint>

namespace stlp {

AnyList::Node** __fastcall ListBegin(const AnyList* self, void*, AnyList::Node** out) {
    *out = self->First();
    return out;
}

AnyList::Node** __fastcall ListEnd(const AnyList* self, void*, AnyList::Node** out) {
    *out = self->End();
    return out;
}

void __fastcall ListDistance(int* n, void*, const AnyList::Node* first, const AnyList::Node* last) {
    AnyList::Distance(first, last, *n);
}

AnyList::Node** __fastcall ListIteratorCopy(AnyList::Node** self, void*, AnyList::Node* const* from) {
    *self = *from;
    return self;
}

AnyList::Node** __fastcall ListIteratorDecrement(AnyList::Node** it) {
    *it = (*it)->prev;
    return it;
}

void __fastcall ListSpliceOne(AnyList*, void*, AnyList::Node* pos, AnyList*, AnyList::Node* i) { AnyList::SpliceOne(pos, i); }

void __fastcall ListSpliceRange(AnyList*, void*, AnyList::Node* pos, AnyList*, AnyList::Node* first, AnyList::Node* last) {
    AnyList::SpliceRange(pos, first, last);
}

} // namespace stlp

namespace {

// Instantiations seen only through their node size: 16 bytes (elements up to 8 bytes), 24 (up to
// 16), 40 (the lobby's server records) and 0x160 (the client's trade records, from operator new).
using List16 = stlp::ListAbi<stlp::Opaque<8>>;
using List24 = stlp::ListAbi<stlp::Opaque<16>>;
using List40 = stlp::ListAbi<stlp::Opaque<32>>;
using List352 = stlp::ListAbi<stlp::Opaque<0x158>>;
using List48 = stlp::ListAbi<stlp::Opaque<0x28>>;
// The connection's reliable headers (0x10C-byte nodes) and send history (0x120), from operator new.
using List268 = stlp::ListAbi<stlp::Opaque<0x104>>;
using List32 = stlp::ListAbi<stlp::Opaque<24>>;
using List288 = stlp::ListAbi<stlp::Opaque<0x118>>;

} // namespace

// begin(): 8 instantiations.
EI_REPLACE(0x0040A9F0, stlp::ListBegin);
EI_REPLACE(0x0041FCC0, stlp::ListBegin);
EI_REPLACE(0x00420280, stlp::ListBegin);
EI_REPLACE(0x00420380, stlp::ListBegin);
EI_REPLACE(0x004219E0, stlp::ListBegin);
EI_REPLACE(0x00422B90, stlp::ListBegin);
EI_REPLACE(0x00438640, stlp::ListBegin);   // the connection's headers (S12d)
EI_REPLACE(0x00438820, stlp::ListBegin);   // the session's clients (S12d)

// end(): 29 instantiations.
EI_REPLACE(0x004049D0, stlp::ListEnd);
EI_REPLACE(0x0040AA00, stlp::ListEnd);
EI_REPLACE(0x00411530, stlp::ListEnd);
EI_REPLACE(0x0041FCD0, stlp::ListEnd);
EI_REPLACE(0x00420390, stlp::ListEnd);
EI_REPLACE(0x00420450, stlp::ListEnd);
EI_REPLACE(0x00420550, stlp::ListEnd);
EI_REPLACE(0x00420610, stlp::ListEnd);
EI_REPLACE(0x004206E0, stlp::ListEnd);
EI_REPLACE(0x004208A0, stlp::ListEnd);
EI_REPLACE(0x00420960, stlp::ListEnd);
EI_REPLACE(0x00420C30, stlp::ListEnd);
EI_REPLACE(0x00420CF0, stlp::ListEnd);
EI_REPLACE(0x004210C0, stlp::ListEnd);
EI_REPLACE(0x004210E0, stlp::ListEnd);
EI_REPLACE(0x004219F0, stlp::ListEnd);
EI_REPLACE(0x00422BA0, stlp::ListEnd);
EI_REPLACE(0x00423950, stlp::ListEnd);
EI_REPLACE(0x00423B40, stlp::ListEnd);
EI_REPLACE(0x00423D30, stlp::ListEnd);
EI_REPLACE(0x004242E0, stlp::ListEnd);
EI_REPLACE(0x004244D0, stlp::ListEnd);
EI_REPLACE(0x004436C0, stlp::ListEnd);
EI_REPLACE(0x0068C500, stlp::ListEnd);
EI_REPLACE(0x0069BE60, stlp::ListEnd);
EI_REPLACE(0x0069C2E0, stlp::ListEnd);
EI_REPLACE(0x00572AF0, stlp::ListEnd);   // S16a: the client sector grid's second cells
// S12c: the .mob loader's tint lists (00478FC0, 00478FD0, 00478FE0).
EI_REPLACE(0x00478FC0, stlp::ListBegin);
EI_REPLACE(0x00478FD0, stlp::ListBegin);
EI_REPLACE(0x00478FE0, stlp::ListEnd);

// get_allocator() (00479130; RET 4) and the iterator's copy (004FB3B0; RET 4) have the tree's bodies
// (stlp_tree_abi.h): the list's first byte copied out; one node pointer copied, this returned.
EI_REPLACE(0x00479130, stlp::TreeGetAllocator);
EI_REPLACE(0x004FB3B0, stlp::TreeIteratorCopy);
// The iterator's operator--().
EI_REPLACE(0x004FB570, stlp::ListIteratorDecrement);
// The sentinel (0x20 bytes) given back while a list's constructor unwinds.
EI_REPLACE(0x00482790, stlp::ListPutSentinel<32>);
EI_REPLACE(0x004FB840, stlp::ListPutSentinel<32>);
EI_REPLACE(0x00437F20, stlp::ListEnd);   // S12d: the connection's history, headers, the session's clients
EI_REPLACE(0x00438650, stlp::ListEnd);
EI_REPLACE(0x00438830, stlp::ListEnd);

// The iterator's copy constructor and --it; splice(pos, x, i) and splice(pos, x, first, last) (the
// connection's history and headers, S12d).
EI_REPLACE(0x004387C0, stlp::ListIteratorCopy);
EI_REPLACE(0x00439A60, stlp::ListIteratorDecrement);
EI_REPLACE(0x004380C0, stlp::ListSpliceOne);
EI_REPLACE(0x00438770, stlp::ListSpliceRange);

// distance(first, last, n): 4 instantiations.
EI_REPLACE(0x00429040, stlp::ListDistance);
EI_REPLACE(0x004290D0, stlp::ListDistance);
EI_REPLACE(0x004290F0, stlp::ListDistance);
EI_REPLACE(0x00429110, stlp::ListDistance);

// list(const allocator_type&) with 16-byte nodes: 28 instantiations.
EI_REPLACE(0x00404A70, List16::Construct);
EI_REPLACE(0x00411850, List16::Construct);
EI_REPLACE(0x0041F8B0, List16::Construct);
EI_REPLACE(0x00420F10, List16::Construct);
EI_REPLACE(0x004221E0, List16::Construct);
EI_REPLACE(0x00422400, List16::Construct);
EI_REPLACE(0x004225E0, List16::Construct);
EI_REPLACE(0x00422800, List16::Construct);
EI_REPLACE(0x004229E0, List16::Construct);
EI_REPLACE(0x00422BE0, List16::Construct);
EI_REPLACE(0x00422DC0, List16::Construct);
EI_REPLACE(0x00422FE0, List16::Construct);
EI_REPLACE(0x004231C0, List16::Construct);
EI_REPLACE(0x00423440, List16::Construct);
EI_REPLACE(0x00423700, List16::Construct);
EI_REPLACE(0x00423990, List16::Construct);
EI_REPLACE(0x00423B80, List16::Construct);
EI_REPLACE(0x00423D70, List16::Construct);
EI_REPLACE(0x00423F50, List16::Construct);
EI_REPLACE(0x00424130, List16::Construct);
EI_REPLACE(0x00424320, List16::Construct);
EI_REPLACE(0x00424510, List16::Construct);
EI_REPLACE(0x00424750, List16::Construct);
EI_REPLACE(0x00463AA0, List16::Construct);
EI_REPLACE(0x005F9A00, List16::Construct);
EI_REPLACE(0x0069BF70, List16::Construct);
EI_REPLACE(0x0069C440, List16::Construct);
EI_REPLACE(0x0069C680, List16::Construct);
// With 24-byte nodes: 7 instantiations, and list() without the allocator argument (00562A80).
EI_REPLACE(0x00411660, List24::Construct);
EI_REPLACE(0x0041FB10, List24::Construct);
EI_REPLACE(0x00421B20, List24::Construct);
EI_REPLACE(0x00439120, List24::Construct);
EI_REPLACE(0x004A94A0, List24::Construct);
EI_REPLACE(0x005F9300, List24::Construct);
EI_REPLACE(0x00634300, List24::Construct);
EI_REPLACE(0x004827D0, List24::Construct);
EI_REPLACE(0x004FB3C0, List24::Construct);
EI_REPLACE(0x00562A80, List24::ConstructDefault);
// With 40-byte, 0x30-byte and 0x160-byte nodes (the 0x30-byte ones: the link's send queues
// (net_link.cpp) and the create screen's list, 005F90E0).
EI_REPLACE(0x0041FD30, List40::Construct);
EI_REPLACE(0x00478D50, List48::Construct);
EI_REPLACE(0x00479150, List48::Construct);
EI_REPLACE(0x005F90E0, List48::Construct);
EI_REPLACE(0x004249E0, List352::Construct);
EI_REPLACE(0x00439CE0, List268::Construct);   // S12d
EI_REPLACE(0x00439960, List288::Construct);   // S12d

// ~list() of plain elements (nothing to destroy; Ghidra's names say clear): 5 instantiations.
EI_REPLACE(0x004A96A0, List16::Destruct);
EI_REPLACE(0x0069C830, List16::Destruct);
EI_REPLACE(0x00421CD0, List24::Destruct);
EI_REPLACE(0x004A9650, List24::Destruct);
EI_REPLACE(0x00421D20, List40::Destruct);
EI_REPLACE(0x004829B0, List24::Destruct);   // S12c (library.csv: clear)
EI_REPLACE(0x00482CC0, List24::Destruct);
EI_REPLACE(0x005F9620, List16::Destruct);
// The connection's reliable headers (S12d): ~list() and clear().
EI_REPLACE(0x00439D40, List268::Destruct);
EI_REPLACE(0x0043D420, List268::Clear);
// clear() and erase(pos) of plain elements by node size (S12d): clear 16 bytes (2) and 24; erase
// 16 bytes (3), 24 (3), 32 and 40.
EI_REPLACE(0x00405500, List16::Clear);
EI_REPLACE(0x0040B390, List16::Clear);
EI_REPLACE(0x0040B290, List24::Clear);
EI_REPLACE(0x0040AA40, List16::Erase);
EI_REPLACE(0x00512030, List16::Erase);
EI_REPLACE(0x0056DFB0, List16::Erase);
EI_REPLACE(0x0040A920, List24::Erase);
EI_REPLACE(0x00411540, List24::Erase);
EI_REPLACE(0x004437D0, List24::Erase);
EI_REPLACE(0x005392F0, List32::Erase);
EI_REPLACE(0x00443730, List40::Erase);

// The instantiations that copy or destroy elements, grouped by element policy (stlp_list_refs.h).
namespace {

using PlainList4 = stlp::ListAbi<std::uint32_t>;
using PlainList16 = stlp::ListAbi<stlp::Opaque<16>>;
using PlainList20 = stlp::ListAbi<stlp::Opaque<20>>;
using PlainList28 = stlp::ListAbi<stlp::Opaque<28>>;
using PlainList260 = stlp::ListAbi<stlp::Opaque<0x104>>;
using SharedRefList = stlp::ListAbi<RefPtr, stlp::SharedRef>;
using CountedRefList = stlp::ListAbi<RefPtr, stlp::CountedRef>;
using SharedEntryList = stlp::ListAbi<stlp::RefEntry, stlp::SharedEntry>;
using CountedEntryList = stlp::ListAbi<stlp::RefEntry, stlp::CountedEntry>;
using HeldObjectList = stlp::ListAbi<CEngineObject*, stlp::HeldObject>;
using CStringList = stlp::ListAbi<mfc::CString>;
using StringDwordList = stlp::ListAbi<stlp::StringDwordPair, stlp::StringDwordElement>;

} // namespace

namespace stlp {

void __fastcall ListRemoveCounted(list<RefPtr>* self, void*, const RefPtr* value) {
    self->Remove<CountedRef>(*value, SameObject());
}

void __fastcall ListRemoveHeld(list<CEngineObject*>* self, void*, CEngineObject* const* value) {
    self->Remove<HeldObject>(*value, [](CEngineObject* a, CEngineObject* b) { return a == b; });
}

void __fastcall ListRemoveInt(list<int>* self, void*, const int* value) {
    self->Remove(*value, [](int a, int b) { return a == b; });
}

char __stdcall ListForEachRelease(list<CEngineObject*>::Node* first, list<CEngineObject*>::Node* last, char f) {
    for (; first != last; first = first->next) HeldObject::Destroy(&first->data);
    return f;
}

} // namespace stlp

// insert(pos, x) of plain data: 4 bytes (6), 16 (2), 20, 28 and 0x104 (from operator new).
EI_REPLACE(0x00404A40, PlainList4::Insert);
EI_REPLACE(0x0040AA10, PlainList4::Insert);
EI_REPLACE(0x0056EA80, PlainList4::Insert);
EI_REPLACE(0x0068C560, PlainList4::Insert);
EI_REPLACE(0x0069C2F0, PlainList4::Insert);
EI_REPLACE(0x0069C3D0, PlainList4::Insert);
EI_REPLACE(0x00411630, PlainList16::Insert);
EI_REPLACE(0x00421AF0, PlainList16::Insert);
EI_REPLACE(0x00443800, PlainList28::Insert);
EI_REPLACE(0x0056EAF0, List32::Insert);   // S16a: the client's screen entries (+0xF0)
EI_REPLACE(0x005392C0, PlainList20::Insert);
EI_REPLACE(0x00439C50, PlainList260::Insert);
// insert(pos, x) of SharedRef (9), CountedRef (11), SharedEntry, HeldObject (3) and CString.
EI_REPLACE(0x004221B0, SharedRefList::Insert);
EI_REPLACE(0x004223D0, SharedRefList::Insert);
EI_REPLACE(0x004225B0, SharedRefList::Insert);
EI_REPLACE(0x004227D0, SharedRefList::Insert);
EI_REPLACE(0x00422BB0, SharedRefList::Insert);
EI_REPLACE(0x00424100, SharedRefList::Insert);
EI_REPLACE(0x00439E30, SharedRefList::Insert);
EI_REPLACE(0x004437A0, SharedRefList::Insert);
EI_REPLACE(0x0047A450, SharedRefList::Insert);
EI_REPLACE(0x004229B0, CountedRefList::Insert);
EI_REPLACE(0x00423190, CountedRefList::Insert);
EI_REPLACE(0x00423410, CountedRefList::Insert);
EI_REPLACE(0x00423960, CountedRefList::Insert);
EI_REPLACE(0x00423B50, CountedRefList::Insert);
EI_REPLACE(0x00423D40, CountedRefList::Insert);
EI_REPLACE(0x00423F20, CountedRefList::Insert);
EI_REPLACE(0x004242F0, CountedRefList::Insert);
EI_REPLACE(0x004244E0, CountedRefList::Insert);
EI_REPLACE(0x00424720, CountedRefList::Insert);
EI_REPLACE(0x0047A480, CountedRefList::Insert);
EI_REPLACE(0x00426230, SharedEntryList::Insert);
EI_REPLACE(0x0056E8A0, HeldObjectList::Insert);
EI_REPLACE(0x00572B00, HeldObjectList::Insert);
EI_REPLACE(0x0069C410, HeldObjectList::Insert);
EI_REPLACE(0x005F97E0, CStringList::Insert);
// S12c: 16 bytes (the tint lists) and pair<const string, dword> (the link loader's archives).
EI_REPLACE(0x00482980, PlainList16::Insert);
EI_REPLACE(0x004FB730, StringDwordList::Insert);

// _M_create_node(x): 16 bytes of plain data, a CString (called by 005F97E0) and pair<const string,
// dword> (by 004FB730).
EI_REPLACE(0x0056F7C0, List32::CreateNode);   // S16a: the client's screen entries
EI_REPLACE(0x00482C30, PlainList16::CreateNode);   // library.csv: hashtable::_M_new_node
EI_REPLACE(0x005F9C70, CStringList::CreateNode);
EI_REPLACE(0x004FB8B0, StringDwordList::CreateNode);

// push_back(x): end(), then the instantiation's insert.
EI_REPLACE(0x004207C0, SharedRefList::PushBack);
EI_REPLACE(0x00422F70, SharedEntryList::PushBack);
EI_REPLACE(0x004246C0, SharedEntryList::PushBack);
EI_REPLACE(0x00423690, CountedEntryList::PushBack);
EI_REPLACE(0x0056DED0, HeldObjectList::PushBack);

// erase(pos): SharedRef (8), CountedRef (8), SharedEntry (2), CountedEntry and CString.
EI_REPLACE(0x00420290, SharedRefList::Erase);
EI_REPLACE(0x00420460, SharedRefList::Erase);
EI_REPLACE(0x004293B0, SharedRefList::Erase);
EI_REPLACE(0x00429490, SharedRefList::Erase);
EI_REPLACE(0x00429650, SharedRefList::Erase);
EI_REPLACE(0x00438840, SharedRefList::Erase);
EI_REPLACE(0x004436D0, SharedRefList::Erase);
EI_REPLACE(0x0047A380, SharedRefList::Erase);
EI_REPLACE(0x004293F0, CountedRefList::Erase);
EI_REPLACE(0x004294D0, CountedRefList::Erase);
EI_REPLACE(0x00429550, CountedRefList::Erase);
EI_REPLACE(0x00429590, CountedRefList::Erase);
EI_REPLACE(0x004295D0, CountedRefList::Erase);
EI_REPLACE(0x00429610, CountedRefList::Erase);
EI_REPLACE(0x00429690, CountedRefList::Erase);
EI_REPLACE(0x004296D0, CountedRefList::Erase);
EI_REPLACE(0x00422FA0, SharedEntryList::Erase);
EI_REPLACE(0x004246E0, SharedEntryList::Erase);
EI_REPLACE(0x004236C0, CountedEntryList::Erase);
EI_REPLACE(0x005F8E50, CStringList::Erase);
// erase(pos) of the connection's 0x104-byte headers (nodes from operator new; S12d).
EI_REPLACE(0x00438660, PlainList260::Erase);
// erase(pos) of pair<const string, dword> (the link loader's archives; S12e).
EI_REPLACE(0x004FAE00, StringDwordList::Erase);

// clear(): SharedRef (7), CountedRef (8), SharedEntry (2).
EI_REPLACE(0x00427100, SharedRefList::Clear);
EI_REPLACE(0x00427140, SharedRefList::Clear);
EI_REPLACE(0x00427180, SharedRefList::Clear);
EI_REPLACE(0x004271C0, SharedRefList::Clear);
EI_REPLACE(0x00427260, SharedRefList::Clear);
EI_REPLACE(0x004272A0, SharedRefList::Clear);
EI_REPLACE(0x00427480, SharedRefList::Clear);
EI_REPLACE(0x00427200, CountedRefList::Clear);
EI_REPLACE(0x00427360, CountedRefList::Clear);
EI_REPLACE(0x004273A0, CountedRefList::Clear);
EI_REPLACE(0x004273E0, CountedRefList::Clear);
EI_REPLACE(0x00427420, CountedRefList::Clear);
EI_REPLACE(0x004274C0, CountedRefList::Clear);
EI_REPLACE(0x00427500, CountedRefList::Clear);
EI_REPLACE(0x004275A0, CountedRefList::Clear);
EI_REPLACE(0x004272E0, SharedEntryList::Clear);
// The client's sector grids' cell lists (S16, on the standalone boot path): clear (2), erase,
// ~list (2).
EI_REPLACE(0x005703C0, HeldObjectList::Clear);
EI_REPLACE(0x00573660, HeldObjectList::Clear);
EI_REPLACE(0x00572270, HeldObjectList::Erase);
EI_REPLACE(0x005755E0, HeldObjectList::Erase);   // S16a: the second grid's cells
EI_REPLACE(0x00572E90, HeldObjectList::Destruct);
EI_REPLACE(0x0069BE70, HeldObjectList::Destruct);
EI_REPLACE(0x00427560, SharedEntryList::Clear);
EI_REPLACE(0x004FBCF0, StringDwordList::Clear);   // S12c: the link loader's archives

// ~list() of CountedRef (the release inlined): 2 instantiations.
EI_REPLACE(0x00423370, CountedRefList::Destruct);
EI_REPLACE(0x004235F0, CountedRefList::Destruct);

// S12g: ~list() of the client's servers (SharedRef, the owner's release inlined) and of the
// server's keyed references at +0x25C0 (CountedEntry); insert(pos, first, last) of SharedRef and
// CountedRef (each element through the instantiation's insert 0047A450 / 0047A480).
EI_REPLACE(0x00421A50, SharedRefList::Destruct);
EI_REPLACE(0x004238B0, CountedEntryList::Destruct);
EI_REPLACE(0x00560430, SharedRefList::InsertRange);
EI_REPLACE(0x00560460, CountedRefList::InsertRange);

// remove(value) of CountedRef (erasing through 00429510).
EI_REPLACE(0x00427320, stlp::ListRemoveCounted);
// remove(value) of HeldObject (erasing through 00572270): the client sector grid's cells.
EI_REPLACE(0x00570400, stlp::ListRemoveHeld);
// remove(value) of int (erasing through 00512030): the light factory's lit ids (S16a).
EI_REPLACE(0x00511FB0, stlp::ListRemoveInt);
// for_each with the releasing functor over a HeldObject list (the client's object maps).
EI_REPLACE(0x00571780, stlp::ListForEachRelease);
