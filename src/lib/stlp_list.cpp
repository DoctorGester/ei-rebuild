// The game's STLport list<T> instantiations (stlp_list.h), registered at their own addresses so the
// standalone image has them; the forwarders in src/game keep calling these addresses. Checked by
// tests/verify/verify_stlp_list.cpp. Instantiations whose node holds the same kind of element share
// one shim; the groups were found by comparing the instructions of every instantiation our code
// forwards to (addresses and jump targets masked).
#include "stlp_list.h"

#include "ei/hook.h"


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

} // namespace stlp

namespace {

// Instantiations seen only through their node size: 16 bytes (elements up to 8 bytes), 24 (up to
// 16), 40 (the lobby's server records) and 0x160 (the client's trade records, from operator new).
using List16 = stlp::ListAbi<stlp::Opaque<8>>;
using List24 = stlp::ListAbi<stlp::Opaque<16>>;
using List40 = stlp::ListAbi<stlp::Opaque<32>>;
using List352 = stlp::ListAbi<stlp::Opaque<0x158>>;

} // namespace

// begin(): 6 instantiations.
EI_REPLACE(0x0040A9F0, stlp::ListBegin);
EI_REPLACE(0x0041FCC0, stlp::ListBegin);
EI_REPLACE(0x00420280, stlp::ListBegin);
EI_REPLACE(0x00420380, stlp::ListBegin);
EI_REPLACE(0x004219E0, stlp::ListBegin);
EI_REPLACE(0x00422B90, stlp::ListBegin);

// end(): 26 instantiations.
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
EI_REPLACE(0x00562A80, List24::ConstructDefault);
// With 40-byte and 0x160-byte nodes.
EI_REPLACE(0x0041FD30, List40::Construct);
EI_REPLACE(0x004249E0, List352::Construct);

// ~list() of plain elements (nothing to destroy; Ghidra's names say clear): 5 instantiations.
EI_REPLACE(0x004A96A0, List16::Destruct);
EI_REPLACE(0x0069C830, List16::Destruct);
EI_REPLACE(0x00421CD0, List24::Destruct);
EI_REPLACE(0x004A9650, List24::Destruct);
EI_REPLACE(0x00421D20, List40::Destruct);

