// The game's STLport vector<T> instantiations (stlp_vector.h), registered at their own addresses so
// the standalone image has them; the forwarders in src/game keep calling these addresses. Checked by
// tests/verify/verify_stlp_vector.cpp. Instantiations with the same instructions (addresses masked)
// and element size share one shim; the element size of each was read from its size arithmetic.
#include "stlp_vector.h"

#include "mfc.h"
#include "stlp.h"
#include "stlp_vector_refs.h"

#include "ei/hook.h"

#include <cstdint>

namespace stlp {

AnyVector* __fastcall VectorConstructBase(AnyVector* self, void*, const void*) {
    self->ConstructEmpty();
    return self;
}

AllocProxy* __fastcall AllocProxyConstruct(AllocProxy* self, void*, const void*, void* const* data) {
    self->data = *data;
    return self;
}

std::uint8_t* __fastcall VectorGetAllocator(const AnyVector* self, void*, std::uint8_t* out) {
    *out = self->AllocatorTag();
    return out;
}

} // namespace stlp

namespace {

// Instantiations seen only through their element size.
using Vector1 = stlp::VectorAbi<char>;
using Vector2 = stlp::VectorAbi<std::uint16_t>;
using Vector4 = stlp::VectorAbi<std::uint32_t>;
using Vector8 = stlp::VectorAbi<stlp::Opaque<8>>;
using Vector12 = stlp::VectorAbi<stlp::Opaque<12>>;
using Vector16 = stlp::VectorAbi<stlp::Opaque<16>>;
using Vector20 = stlp::VectorAbi<stlp::Opaque<20>>;
using Vector24 = stlp::VectorAbi<stlp::Opaque<24>>;
using Vector32 = stlp::VectorAbi<stlp::Opaque<32>>;
using Vector36 = stlp::VectorAbi<stlp::Opaque<36>>;
using Vector40 = stlp::VectorAbi<stlp::Opaque<40>>;
using Vector48 = stlp::VectorAbi<stlp::Opaque<48>>;
using Vector96 = stlp::VectorAbi<stlp::Opaque<96>>;
using Vector288 = stlp::VectorAbi<stlp::Opaque<288>>;
using Vector268 = stlp::VectorAbi<stlp::Opaque<0x10C>>;   // the connection's header list nodes
using Vector60 = stlp::VectorAbi<stlp::Opaque<60>>;
using Vector68 = stlp::VectorAbi<stlp::Opaque<68>>;
using Vector164 = stlp::VectorAbi<stlp::Opaque<164>>;
using Vector360 = stlp::VectorAbi<stlp::Opaque<360>>;

} // namespace

// _Vector_base(const allocator_type&): 29 instantiations.
EI_REPLACE(0x00406240, stlp::VectorConstructBase);
EI_REPLACE(0x00406530, stlp::VectorConstructBase);
EI_REPLACE(0x0040ADE0, stlp::VectorConstructBase);
EI_REPLACE(0x0040AE50, stlp::VectorConstructBase);
EI_REPLACE(0x0041FF90, stlp::VectorConstructBase);
EI_REPLACE(0x0041FFF0, stlp::VectorConstructBase);
EI_REPLACE(0x004217E0, stlp::VectorConstructBase);
EI_REPLACE(0x00424B40, stlp::VectorConstructBase);
EI_REPLACE(0x0042D310, stlp::VectorConstructBase);
EI_REPLACE(0x00438CE0, stlp::VectorConstructBase);
EI_REPLACE(0x00439E10, stlp::VectorConstructBase);
EI_REPLACE(0x004E2BA0, stlp::VectorConstructBase);
EI_REPLACE(0x0053B440, stlp::VectorConstructBase);
EI_REPLACE(0x0053B5E0, stlp::VectorConstructBase);
EI_REPLACE(0x0053B780, stlp::VectorConstructBase);
EI_REPLACE(0x0053B980, stlp::VectorConstructBase);
EI_REPLACE(0x0053BB80, stlp::VectorConstructBase);
EI_REPLACE(0x0053F4F0, stlp::VectorConstructBase);
EI_REPLACE(0x0055A3E0, stlp::VectorConstructBase);
EI_REPLACE(0x0056F570, stlp::VectorConstructBase);
EI_REPLACE(0x00584720, stlp::VectorConstructBase);
EI_REPLACE(0x00598940, stlp::VectorConstructBase);
EI_REPLACE(0x005F8F10, stlp::VectorConstructBase);
EI_REPLACE(0x005F9540, stlp::VectorConstructBase);
EI_REPLACE(0x0062B550, stlp::VectorConstructBase);
EI_REPLACE(0x0062B570, stlp::VectorConstructBase);
EI_REPLACE(0x0062B5E0, stlp::VectorConstructBase);
EI_REPLACE(0x006342A0, stlp::VectorConstructBase);
EI_REPLACE(0x006AFB00, stlp::VectorConstructBase);
EI_REPLACE(0x00482A80, stlp::VectorConstructBase);   // S12c
EI_REPLACE(0x00619600, stlp::VectorConstructBase);
EI_REPLACE(0x006316C0, stlp::VectorConstructBase);

// _STLP_alloc_proxy(const allocator_type&, const pointer&): 9 instantiations.
EI_REPLACE(0x00406260, stlp::AllocProxyConstruct);
EI_REPLACE(0x00406550, stlp::AllocProxyConstruct);
EI_REPLACE(0x00425080, stlp::AllocProxyConstruct);
EI_REPLACE(0x004263D0, stlp::AllocProxyConstruct);
EI_REPLACE(0x0043A980, stlp::AllocProxyConstruct);
EI_REPLACE(0x005AD640, stlp::AllocProxyConstruct);
EI_REPLACE(0x005FA0C0, stlp::AllocProxyConstruct);
EI_REPLACE(0x005FA340, stlp::AllocProxyConstruct);
EI_REPLACE(0x00619750, stlp::AllocProxyConstruct);

// get_allocator(): 2 instantiations.
EI_REPLACE(0x00421EB0, stlp::VectorGetAllocator);
EI_REPLACE(0x00438CC0, stlp::VectorGetAllocator);

// ~_Vector_base(): 32 instantiations, by element size (1 bytes: 7, 4 bytes: 21, 12 bytes: 1, 96 bytes: 1, 288 bytes: 1, 360 bytes: 1),
// and six ~vector() with it inlined that only exit functions call (S3c): of pointers 0056E800 (the
// frame lists 007AF508..007AF5A8, world.h), 0069BE20, 004A8C60, 004A8D00, 004A8D50 (the item
// stock's scratch vectors 007C8650, 007C8680, 007C8670, 007C8660) and of 16-byte entries 004A8CB0
// (its taken materials 007C8640).
EI_REPLACE(0x0040C720, Vector1::DestroyBase);
EI_REPLACE(0x0040C770, Vector1::DestroyBase);
EI_REPLACE(0x00425040, Vector1::DestroyBase);
EI_REPLACE(0x0042D2C0, Vector1::DestroyBase);
EI_REPLACE(0x004DEAB0, Vector1::DestroyBase);
EI_REPLACE(0x00598D80, Vector1::DestroyBase);
EI_REPLACE(0x006B6D30, Vector1::DestroyBase);   // S16a: the sound voices' string (no Ghidra function)
EI_REPLACE(0x006B72E0, Vector1::DestroyBase);
EI_REPLACE(0x00405BF0, Vector4::DestroyBase);
EI_REPLACE(0x00405DB0, Vector4::DestroyBase);
EI_REPLACE(0x00405E00, Vector4::DestroyBase);
EI_REPLACE(0x00405E50, Vector4::DestroyBase);
EI_REPLACE(0x0043A2B0, Vector4::DestroyBase);
EI_REPLACE(0x0043AAB0, Vector4::DestroyBase);
EI_REPLACE(0x004AEDA0, Vector4::DestroyBase);
EI_REPLACE(0x004AEF50, Vector4::DestroyBase);
EI_REPLACE(0x004AF100, Vector4::DestroyBase);
EI_REPLACE(0x004E2A20, Vector4::DestroyBase);
EI_REPLACE(0x005150D0, Vector4::DestroyBase);
EI_REPLACE(0x00520EB0, Vector4::DestroyBase);
EI_REPLACE(0x0053B930, Vector4::DestroyBase);
EI_REPLACE(0x0053BE70, Vector4::DestroyBase);
EI_REPLACE(0x0053BF00, Vector4::DestroyBase);
EI_REPLACE(0x005843A0, Vector4::DestroyBase);
EI_REPLACE(0x00584450, Vector4::DestroyBase);
EI_REPLACE(0x005F9880, Vector4::DestroyBase);
EI_REPLACE(0x00609F50, Vector4::DestroyBase);
EI_REPLACE(0x006BC4D0, Vector4::DestroyBase);
EI_REPLACE(0x006BC510, Vector4::DestroyBase);
EI_REPLACE(0x0056E800, Vector4::DestroyBase);
EI_REPLACE(0x0069BE20, Vector4::DestroyBase);
EI_REPLACE(0x004A8C60, Vector4::DestroyBase);
EI_REPLACE(0x004A8D00, Vector4::DestroyBase);
EI_REPLACE(0x004A8D50, Vector4::DestroyBase);
EI_REPLACE(0x004A8CB0, Vector16::DestroyBase);
EI_REPLACE(0x0056F590, Vector12::DestroyBase);
EI_REPLACE(0x0053B600, Vector96::DestroyBase);
EI_REPLACE(0x0053B460, Vector288::DestroyBase);
// S12c: bytes (the link loader's string base), 4 bytes (3), 8 bytes (2) and 12 bytes (2).
EI_REPLACE(0x004FB6A0, Vector1::DestroyBase);
EI_REPLACE(0x004FB290, Vector4::DestroyBase);
EI_REPLACE(0x004FB6F0, Vector4::DestroyBase);
EI_REPLACE(0x005F8E00, Vector4::DestroyBase);
EI_REPLACE(0x004389C0, Vector4::DestroyBase);   // the session's handler vectors (no Ghidra function; S16b)
EI_REPLACE(0x004336D0, Vector2::DestroyBase);   // the connection's id pools (S16b)
EI_REPLACE(0x005F9090, Vector8::DestroyBase);
EI_REPLACE(0x006342C0, Vector8::DestroyBase);
EI_REPLACE(0x00482440, Vector12::DestroyBase);
EI_REPLACE(0x005F99B0, Vector12::DestroyBase);
EI_REPLACE(0x0053B7A0, Vector360::DestroyBase);   // S12d

// _STLP_alloc_proxy::deallocate(p, n) (RET 8): 43 instantiations, by element size (4 bytes: 4, 12 bytes: 23, 16 bytes: 2, 20 bytes: 6, 24 bytes: 4, 32 bytes: 1, 36 bytes: 1, 68 bytes: 1, 268 bytes: 1).
EI_REPLACE(0x004251B0, Vector4::Deallocate);
EI_REPLACE(0x00425210, Vector4::Deallocate);
EI_REPLACE(0x0043A300, Vector4::Deallocate);
EI_REPLACE(0x00520F20, Vector4::Deallocate);
EI_REPLACE(0x00404D10, Vector12::Deallocate);
EI_REPLACE(0x00407DA0, Vector12::Deallocate);
EI_REPLACE(0x00408020, Vector12::Deallocate);
EI_REPLACE(0x0040B560, Vector12::Deallocate);
EI_REPLACE(0x0040B5A0, Vector12::Deallocate);
EI_REPLACE(0x00422390, Vector12::Deallocate);
EI_REPLACE(0x00422790, Vector12::Deallocate);
EI_REPLACE(0x00425470, Vector12::Deallocate);
EI_REPLACE(0x004256D0, Vector12::Deallocate);
EI_REPLACE(0x004257A0, Vector12::Deallocate);
EI_REPLACE(0x004258F0, Vector12::Deallocate);
EI_REPLACE(0x00425A40, Vector12::Deallocate);
EI_REPLACE(0x00425D60, Vector12::Deallocate);
EI_REPLACE(0x00425E30, Vector12::Deallocate);
EI_REPLACE(0x00425F00, Vector12::Deallocate);
EI_REPLACE(0x00426050, Vector12::Deallocate);
EI_REPLACE(0x00426120, Vector12::Deallocate);
EI_REPLACE(0x004261F0, Vector12::Deallocate);
EI_REPLACE(0x00426330, Vector12::Deallocate);
EI_REPLACE(0x00439E60, Vector12::Deallocate);
EI_REPLACE(0x0043E0A0, Vector12::Deallocate);
EI_REPLACE(0x0053BF40, Vector12::Deallocate);
EI_REPLACE(0x0056F700, Vector12::Deallocate);
EI_REPLACE(0x00425AB0, Vector16::Deallocate);
EI_REPLACE(0x00426260, Vector16::Deallocate);
EI_REPLACE(0x0043A410, Vector20::Deallocate);
EI_REPLACE(0x004648D0, Vector20::Deallocate);
EI_REPLACE(0x0053CA60, Vector20::Deallocate);
EI_REPLACE(0x0053CAA0, Vector20::Deallocate);
EI_REPLACE(0x0053CAE0, Vector20::Deallocate);
EI_REPLACE(0x005AE150, Vector20::Deallocate);
EI_REPLACE(0x0040ACE0, Vector24::Deallocate);
EI_REPLACE(0x0043A480, Vector24::Deallocate);
EI_REPLACE(0x0043A4E0, Vector24::Deallocate);
EI_REPLACE(0x006135F0, Vector24::Deallocate);
EI_REPLACE(0x0056F840, Vector32::Deallocate);
EI_REPLACE(0x00411E20, Vector36::Deallocate);
// S12c: 4 bytes, 12 bytes (2), 32 bytes (2) and 48 bytes.
EI_REPLACE(0x005F9F40, Vector4::Deallocate);
EI_REPLACE(0x00482D10, Vector12::Deallocate);
EI_REPLACE(0x005FA300, Vector12::Deallocate);
EI_REPLACE(0x00482BF0, Vector32::Deallocate);
EI_REPLACE(0x004FB9D0, Vector32::Deallocate);
EI_REPLACE(0x005FA2C0, Vector48::Deallocate);
EI_REPLACE(0x00439D80, Vector268::Deallocate);   // S12d: a header list's sentinel (to operator delete)
EI_REPLACE(0x0053BEB0, Vector68::Deallocate);    // S12d

// _STLP_alloc_proxy::deallocate(p, n) (ECX p, EDX n): 32 instantiations, by element size (4 bytes: 9, 8 bytes: 2, 12 bytes: 10, 16 bytes: 1, 20 bytes: 2, 24 bytes: 1, 36 bytes: 2, 40 bytes: 1, 48 bytes: 1, 60 bytes: 1, 164 bytes: 1, 288 bytes: 1).
EI_REPLACE(0x00406590, Vector4::DeallocateFast);
EI_REPLACE(0x00411EA0, Vector4::DeallocateFast);
EI_REPLACE(0x00426490, Vector4::DeallocateFast);
EI_REPLACE(0x004268A0, Vector4::DeallocateFast);
EI_REPLACE(0x004AA7D0, Vector4::DeallocateFast);
EI_REPLACE(0x004AA810, Vector4::DeallocateFast);
EI_REPLACE(0x00520F60, Vector4::DeallocateFast);
EI_REPLACE(0x00584A90, Vector4::DeallocateFast);
EI_REPLACE(0x0069C990, Vector4::DeallocateFast);
EI_REPLACE(0x005FA7E0, Vector8::DeallocateFast);
EI_REPLACE(0x006344C0, Vector8::DeallocateFast);
EI_REPLACE(0x00426860, Vector12::DeallocateFast);
EI_REPLACE(0x00426920, Vector12::DeallocateFast);
EI_REPLACE(0x00426960, Vector12::DeallocateFast);
EI_REPLACE(0x0043AB30, Vector12::DeallocateFast);
EI_REPLACE(0x00461710, Vector12::DeallocateFast);
EI_REPLACE(0x0047A650, Vector12::DeallocateFast);
EI_REPLACE(0x00498C40, Vector12::DeallocateFast);
EI_REPLACE(0x0056FB00, Vector12::DeallocateFast);
EI_REPLACE(0x0056FB40, Vector12::DeallocateFast);
EI_REPLACE(0x0068C640, Vector12::DeallocateFast);
EI_REPLACE(0x0043B180, Vector20::DeallocateFast);
EI_REPLACE(0x004FC080, Vector20::DeallocateFast);
EI_REPLACE(0x00411B30, Vector24::DeallocateFast);
EI_REPLACE(0x005FA920, Vector36::DeallocateFast);
EI_REPLACE(0x005FA960, Vector36::DeallocateFast);
EI_REPLACE(0x004452D0, Vector40::DeallocateFast);
EI_REPLACE(0x00479530, Vector48::DeallocateFast);
EI_REPLACE(0x0043AAF0, Vector288::DeallocateFast);
// S12c: 4 bytes, 12 bytes and 24 bytes (2).
EI_REPLACE(0x005FA3C0, Vector4::DeallocateFast);
EI_REPLACE(0x005FA360, Vector12::DeallocateFast);
EI_REPLACE(0x004FB860, Vector24::DeallocateFast);
EI_REPLACE(0x005FA8E0, Vector24::DeallocateFast);
EI_REPLACE(0x0069C9D0, Vector16::DeallocateFast);    // S12d
EI_REPLACE(0x00613630, Vector60::DeallocateFast);    // S12d
EI_REPLACE(0x0056FB80, Vector164::DeallocateFast);   // S12d

// The members that copy plain-data elements (PlainElement), by element size.

// _Vector_base(n, allocator): 2 instantiations of 4 bytes.
EI_REPLACE(0x00421F60, Vector4::ConstructWithCapacity);
EI_REPLACE(0x00438D00, Vector4::ConstructWithCapacity);

// ~vector() of 8-byte elements (nothing to destroy).
EI_REPLACE(0x004387D0, Vector8::DestroyBase);

// The scalar deleting destructor of a _Vector_base of bytes: 4 instantiations. The same bodies at
// 005AE5B0 / 005AE5D0 are the figure maps' pair destructors, registered by S13 (record_lifetime.h).
EI_REPLACE(0x00464DF0, Vector1::DeleteBase);
EI_REPLACE(0x0053D970, Vector1::DeleteBase);
EI_REPLACE(0x0053D990, Vector1::DeleteBase);
EI_REPLACE(0x0053D9B0, Vector1::DeleteBase);

// _M_allocate_and_copy(n, first, last) of 16 bytes, pointers (the item stock's three vectors; S12g)
// and 24 bytes (the client maps; S12g).
EI_REPLACE(0x0069DB00, Vector16::AllocateAndCopy);
EI_REPLACE(0x0069D9A0, Vector4::AllocateAndCopy);
EI_REPLACE(0x0069DC70, Vector4::AllocateAndCopy);
EI_REPLACE(0x0069DDD0, Vector4::AllocateAndCopy);
EI_REPLACE(0x00573520, Vector24::AllocateAndCopy);

// _M_insert_overflow(pos, x, n): 4 bytes (9), 16 (2), 24 and 2.
EI_REPLACE(0x0043B6C0, Vector4::InsertOverflow);
EI_REPLACE(0x0043B850, Vector4::InsertOverflow);
EI_REPLACE(0x0043D5F0, Vector4::InsertOverflow);
EI_REPLACE(0x004E2FA0, Vector4::InsertOverflow);
EI_REPLACE(0x00571B00, Vector4::InsertOverflow);
EI_REPLACE(0x00587700, Vector4::InsertOverflow);
EI_REPLACE(0x005FA9A0, Vector4::InsertOverflow);
EI_REPLACE(0x0069D470, Vector4::InsertOverflow);
EI_REPLACE(0x006ADD50, Vector4::InsertOverflow);
EI_REPLACE(0x00441180, Vector16::InsertOverflow);
EI_REPLACE(0x0069CC20, Vector16::InsertOverflow);
EI_REPLACE(0x00573350, Vector24::InsertOverflow);
EI_REPLACE(0x006317E0, Vector8::InsertOverflow);   // S12c: 8-byte pairs (from 006312C0)

// uninitialized_copy / uninitialized_fill_n of those pairs (called by 006317E0).
EI_REPLACE(0x006325E0, Vector8::UninitializedCopy);
EI_REPLACE(0x00632610, Vector8::UninitializedFill);
EI_REPLACE(0x0043B530, Vector2::InsertOverflow);   // the connection's id pools (S12d)

// _M_fill_insert(pos, n, x) / insert(pos, n, x): 4 bytes (4, one of floats) and bytes.
EI_REPLACE(0x0040B150, Vector4::FillInsert);
EI_REPLACE(0x0055B030, Vector4::FillInsert);
EI_REPLACE(0x005ADAD0, Vector4::FillInsert);
EI_REPLACE(0x006ADC10, Vector4::FillInsert);
EI_REPLACE(0x0042D370, Vector1::FillInsert);
EI_REPLACE(0x004FBA40, Vector4::FillInsert);   // S12c: the link loader's and a figure model's

// _M_range_insert(pos, first, last) of 16 bytes and insert(pos, first, last) of pointers.
EI_REPLACE(0x0069ECD0, Vector16::RangeInsert);
EI_REPLACE(0x0069E3F0, Vector4::RangeInsert);

// push_back(x) of pointers: 3 instantiations.
EI_REPLACE(0x0069BD30, Vector4::PushBack);
EI_REPLACE(0x0069BDC0, Vector4::PushBack);
EI_REPLACE(0x0069BDF0, Vector4::PushBack);

// erase(first, last) of bytes, pointers and 8 bytes; clear() of pointers.
EI_REPLACE(0x0042D340, Vector1::EraseRange);
EI_REPLACE(0x0056F5E0, Vector4::EraseRange);
EI_REPLACE(0x00439DD0, Vector8::EraseRange);   // the connection's static replicated vector (S12d)
EI_REPLACE(0x0056E870, Vector4::Clear);

// resize(n, x): bytes, 4 bytes (2) and 12.
EI_REPLACE(0x004D8EB0, Vector1::Resize);
EI_REPLACE(0x004AE3A0, Vector4::Resize);
EI_REPLACE(0x004FE0C0, Vector4::Resize);
EI_REPLACE(0x0056F360, Vector12::Resize);

// reserve(n) of 4 bytes: 2 instantiations.
EI_REPLACE(0x0040AF00, Vector4::Reserve);
EI_REPLACE(0x005AD880, Vector4::Reserve);

// operator=(x) of 4 bytes.
EI_REPLACE(0x0064AF30, Vector4::Assign);

// vector<CString> (ObjectElement: CString's copy constructor, operator= and destructor).
namespace {
using CStringVector = stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>;
using StringVector = stlp::VectorAbi<stlp::string, stlp::StringElement>;
} // namespace
EI_REPLACE(0x0051F5B0, CStringVector::ResizeDefault);
EI_REPLACE(0x0051F790, CStringVector::InsertOverflow);
EI_REPLACE(0x0052E010, CStringVector::Clear);
EI_REPLACE(0x0053F020, CStringVector::Destruct);
EI_REPLACE(0x006AF790, CStringVector::PushBack);
EI_REPLACE(0x006AFB60, CStringVector::EraseRange);
EI_REPLACE(0x006B02B0, CStringVector::Reserve);

// vector<stlp::string> (StringElement).
EI_REPLACE(0x004DC900, StringVector::ResizeDefault);
EI_REPLACE(0x004DCE90, StringVector::InsertOverflow);
EI_REPLACE(0x00575E50, StringVector::RangeInsert);
EI_REPLACE(0x00581860, StringVector::Clear);
EI_REPLACE(0x00583CB0, StringVector::EraseRange);
EI_REPLACE(0x00421260, StringVector::Destruct);   // S3c: the quest files and folders 007AFDD0 / 007AFDC0 at exit
EI_REPLACE(0x0053D7F0, StringVector::AllocateAndCopy);   // S12g: the unit skins' names

// vector<RefPtr> and vector<counted_ptr<T>> (CountedRefElement, stlp_vector_refs.h).
namespace {
using CountedRefVector = stlp::VectorAbi<RefPtr, stlp::CountedRefElement>;
} // namespace
EI_REPLACE(0x00426F00, CountedRefVector::InsertOverflow);
EI_REPLACE(0x004AB320, CountedRefVector::InsertOverflow);
EI_REPLACE(0x004AB910, (stlp::CopyAssigned<RefPtr, stlp::CountedRefElement>));   // S16a: the party records' copy
EI_REPLACE(0x00429080, (stlp::CopyAssigned<RefPtr, stlp::CountedRefElement>));   // S12g: the client's parties (same code)
EI_REPLACE(0x004AB520, CountedRefVector::InsertOverflow);
EI_REPLACE(0x0060A120, CountedRefVector::InsertOverflow);
EI_REPLACE(0x00421ED0, CountedRefVector::EraseRange);
EI_REPLACE(0x004A9A00, CountedRefVector::EraseRange);
// resize(n): the RefPtr chunk vector (004200C0), the party manager's records and class templates
// (004AD700 / 004AD8D0; counted_ptr<T>).
EI_REPLACE(0x004200C0, CountedRefVector::ResizeDefault);
// uninitialized_copy (ECX first, EDX last, RET 8): the client's party members (S12g).
EI_REPLACE(0x0042AF80, CountedRefVector::UninitializedCopy);
EI_REPLACE(0x004AD700, CountedRefVector::ResizeDefault);
EI_REPLACE(0x004AD8D0, CountedRefVector::ResizeDefault);

// vector<RefPtr> of shared holders (SharedRefElement): the server's file chunks.
namespace {
using SharedRefVector = stlp::VectorAbi<RefPtr, stlp::SharedRefElement>;
} // namespace
EI_REPLACE(0x00421D90, SharedRefVector::EraseRange);
EI_REPLACE(0x00426CB0, SharedRefVector::FillInsert);
EI_REPLACE(0x0042AE90, SharedRefVector::UninitializedCopy);   // S12g: the net messages' shared copies

// The plain-data helpers compiled out of line (S12d): _Construct of a uint16 (the id pools), of
// 4-byte values (the freed ids, the battle screen's ids); copy of ints (__copy_trivial: memmove) and
// of the freed ids (the element loop).
EI_REPLACE(0x0043D7E0, stlp::ConstructAt<std::uint16_t>);
EI_REPLACE(0x0043D7F0, stlp::ConstructAt<std::uint32_t>);
EI_REPLACE(0x0064B620, stlp::ConstructAt<std::uint32_t>);
EI_REPLACE(0x004193D0, stlp::CopyTrivial<char>);   // S12g: the net strings' bytes (memmove)
EI_REPLACE(0x0043B360, stlp::CopyTrivial<std::uint32_t>);
EI_REPLACE(0x0043DC80, stlp::CopyElements<std::uint32_t>);
EI_REPLACE(0x00571E30, stlp::CopyElements<std::uint32_t>);   // S16a: the client's drawn objects
