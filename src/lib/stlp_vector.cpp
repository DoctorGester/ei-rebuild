// The game's STLport vector<T> instantiations (stlp_vector.h) registered at their own addresses: the ones
// something still reaches by game.exe's address (the exit functions; tools/registration_readers.py, C20c). Our
// code calls the others directly. Instantiations with the same instructions (addresses masked) and element size
// share one body; the element size of each was read from its size arithmetic.
#include "stlp_vector.h"

#include "mfc.h"
#include "stlp.h"
#include "stlp_vector_refs.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

// Instantiations seen only through their element size.
using Vector1 = stlp::VectorAbi<char>;
using Vector4 = stlp::VectorAbi<std::uint32_t>;
using Vector16 = stlp::VectorAbi<stlp::Opaque<16>>;

} // namespace

// ~_Vector_base(): 32 instantiations, by element size (1 bytes: 7, 4 bytes: 21, 12 bytes: 1, 96 bytes: 1, 288 bytes: 1, 360 bytes: 1),
// and six ~vector() with it inlined that only exit functions call (S3c): of pointers 0056E800 (the
// frame lists 007AF508..007AF5A8, world.h), 0069BE20, 004A8C60, 004A8D00, 004A8D50 (the item
// stock's scratch vectors 007C8650, 007C8680, 007C8670, 007C8660) and of 16-byte entries 004A8CB0
// (its taken materials 007C8640).
EI_REWRITTEN(0x0040C720, Vector1::DestroyBase);
EI_REWRITTEN(0x0040C770, Vector1::DestroyBase);
EI_REWRITTEN(0x00425040, Vector1::DestroyBase);
EI_REWRITTEN(0x0042D2C0, Vector1::DestroyBase);
EI_REWRITTEN(0x004DEAB0, Vector1::DestroyBase);
EI_REWRITTEN(0x00598D80, Vector1::DestroyBase);
EI_REPLACE(0x006B6D30, Vector1::DestroyBase);   // S16a: the sound voices' string (no Ghidra function)
EI_REWRITTEN(0x006B72E0, Vector1::DestroyBase);
EI_REWRITTEN(0x00405BF0, Vector4::DestroyBase);
EI_REWRITTEN(0x00405DB0, Vector4::DestroyBase);
EI_REWRITTEN(0x00405E00, Vector4::DestroyBase);
EI_REWRITTEN(0x00405E50, Vector4::DestroyBase);
EI_REWRITTEN(0x0043A2B0, Vector4::DestroyBase);
EI_REWRITTEN(0x0043AAB0, Vector4::DestroyBase);
EI_REWRITTEN(0x004AEDA0, Vector4::DestroyBase);
EI_REWRITTEN(0x004AEF50, Vector4::DestroyBase);
EI_REWRITTEN(0x004AF100, Vector4::DestroyBase);
EI_REPLACE(0x004E2A20, Vector4::DestroyBase);
EI_REWRITTEN(0x005150D0, Vector4::DestroyBase);
EI_REWRITTEN(0x00520EB0, Vector4::DestroyBase);
EI_REWRITTEN(0x0053B930, Vector4::DestroyBase);
EI_REWRITTEN(0x0053BE70, Vector4::DestroyBase);
EI_REWRITTEN(0x0053BF00, Vector4::DestroyBase);
EI_REWRITTEN(0x005843A0, Vector4::DestroyBase);
EI_REWRITTEN(0x00584450, Vector4::DestroyBase);
EI_REWRITTEN(0x005F9880, Vector4::DestroyBase);
EI_REWRITTEN(0x00609F50, Vector4::DestroyBase);
EI_REWRITTEN(0x006BC4D0, Vector4::DestroyBase);
EI_REWRITTEN(0x006BC510, Vector4::DestroyBase);
EI_REPLACE(0x0056E800, Vector4::DestroyBase);
EI_REPLACE(0x0069BE20, Vector4::DestroyBase);
EI_REPLACE(0x004A8C60, Vector4::DestroyBase);
EI_REPLACE(0x004A8D00, Vector4::DestroyBase);
EI_REPLACE(0x004A8D50, Vector4::DestroyBase);
EI_REPLACE(0x004A8CB0, Vector16::DestroyBase);
EI_REWRITTEN(0x0043B6C0, Vector4::InsertOverflow);
EI_REWRITTEN(0x0043B850, Vector4::InsertOverflow);
EI_REWRITTEN(0x0043D5F0, Vector4::InsertOverflow);
EI_REWRITTEN(0x004E2FA0, Vector4::InsertOverflow);
EI_REWRITTEN(0x00571B00, Vector4::InsertOverflow);
EI_REWRITTEN(0x00587700, Vector4::InsertOverflow);
EI_REWRITTEN(0x005FA9A0, Vector4::InsertOverflow);
EI_REWRITTEN(0x0069D470, Vector4::InsertOverflow);
EI_REWRITTEN(0x006ADD50, Vector4::InsertOverflow);
EI_REWRITTEN(0x00441180, Vector16::InsertOverflow);
EI_REWRITTEN(0x0069CC20, Vector16::InsertOverflow);
EI_REWRITTEN(0x0069DB00, Vector16::AllocateAndCopy);
EI_REWRITTEN(0x0069D9A0, Vector4::AllocateAndCopy);
EI_REWRITTEN(0x0069DC70, Vector4::AllocateAndCopy);
EI_REWRITTEN(0x0069DDD0, Vector4::AllocateAndCopy);
EI_REWRITTEN(0x00464DF0, Vector1::DeleteBase);
EI_REWRITTEN(0x0053D970, Vector1::DeleteBase);
EI_REWRITTEN(0x0053D990, Vector1::DeleteBase);
EI_REWRITTEN(0x0053D9B0, Vector1::DeleteBase);
EI_REWRITTEN(0x00421F60, Vector4::ConstructWithCapacity);
EI_REWRITTEN(0x00438D00, Vector4::ConstructWithCapacity);
EI_REWRITTEN(0x005FA3C0, Vector4::DeallocateFast);
EI_REWRITTEN(0x0069C9D0, Vector16::DeallocateFast);
EI_REWRITTEN(0x00406590, Vector4::DeallocateFast);
EI_REWRITTEN(0x00411EA0, Vector4::DeallocateFast);
EI_REWRITTEN(0x00426490, Vector4::DeallocateFast);
EI_REWRITTEN(0x004268A0, Vector4::DeallocateFast);
EI_REWRITTEN(0x004AA7D0, Vector4::DeallocateFast);
EI_REWRITTEN(0x004AA810, Vector4::DeallocateFast);
EI_REWRITTEN(0x00520F60, Vector4::DeallocateFast);
EI_REWRITTEN(0x00584A90, Vector4::DeallocateFast);
EI_REWRITTEN(0x0069C990, Vector4::DeallocateFast);
EI_REWRITTEN(0x005F9F40, Vector4::Deallocate);
EI_REWRITTEN(0x004251B0, Vector4::Deallocate);
EI_REWRITTEN(0x00425210, Vector4::Deallocate);
EI_REWRITTEN(0x0043A300, Vector4::Deallocate);
EI_REWRITTEN(0x00520F20, Vector4::Deallocate);
EI_REWRITTEN(0x00425AB0, Vector16::Deallocate);
EI_REWRITTEN(0x00426260, Vector16::Deallocate);
EI_REWRITTEN(0x004FB6A0, Vector1::DestroyBase);
EI_REWRITTEN(0x004FB290, Vector4::DestroyBase);
EI_REWRITTEN(0x004FB6F0, Vector4::DestroyBase);
EI_REWRITTEN(0x005F8E00, Vector4::DestroyBase);
EI_REWRITTEN(0x004389C0, Vector4::DestroyBase);

EI_REWRITTEN(0x0064AF30, Vector4::Assign);
EI_REWRITTEN(0x0040AF00, Vector4::Reserve);
EI_REWRITTEN(0x005AD880, Vector4::Reserve);
EI_REWRITTEN(0x004D8EB0, Vector1::Resize);
EI_REWRITTEN(0x004AE3A0, Vector4::Resize);
EI_REWRITTEN(0x004FE0C0, Vector4::Resize);
EI_REWRITTEN(0x0042D340, Vector1::EraseRange);
EI_REWRITTEN(0x0056F5E0, Vector4::EraseRange);
EI_REWRITTEN(0x0056E870, Vector4::Clear);
EI_REWRITTEN(0x0069BD30, Vector4::PushBack);
EI_REWRITTEN(0x0069BDC0, Vector4::PushBack);
EI_REWRITTEN(0x0069BDF0, Vector4::PushBack);
EI_REWRITTEN(0x0069ECD0, Vector16::RangeInsert);
EI_REWRITTEN(0x0069E3F0, Vector4::RangeInsert);
EI_REWRITTEN(0x0040B150, Vector4::FillInsert);
EI_REWRITTEN(0x0055B030, Vector4::FillInsert);
EI_REWRITTEN(0x005ADAD0, Vector4::FillInsert);
EI_REWRITTEN(0x006ADC10, Vector4::FillInsert);
EI_REWRITTEN(0x0042D370, Vector1::FillInsert);
EI_REWRITTEN(0x004FBA40, Vector4::FillInsert);
// vector<stlp::string> (StringElement).
namespace {
using StringVector = stlp::VectorAbi<stlp::string, stlp::StringElement>;
} // namespace
EI_REWRITTEN(0x004DC900, StringVector::ResizeDefault);
EI_REWRITTEN(0x004DCE90, StringVector::InsertOverflow);
EI_REWRITTEN(0x00575E50, StringVector::RangeInsert);
EI_REWRITTEN(0x00581860, StringVector::Clear);
EI_REWRITTEN(0x00583CB0, StringVector::EraseRange);
EI_REPLACE(0x00421260, StringVector::Destruct);   // S3c: the quest files and folders 007AFDD0 / 007AFDC0 at exit
EI_REWRITTEN(0x0043D7E0, stlp::ConstructAt<std::uint16_t>);
EI_REWRITTEN(0x0043D7F0, stlp::ConstructAt<std::uint32_t>);
EI_REWRITTEN(0x0064B620, stlp::ConstructAt<std::uint32_t>);
EI_REWRITTEN(0x004193D0, stlp::CopyTrivial<char>);
EI_REWRITTEN(0x0043B360, stlp::CopyTrivial<std::uint32_t>);
EI_REWRITTEN(0x0043DC80, stlp::CopyElements<std::uint32_t>);
EI_REWRITTEN(0x00571E30, stlp::CopyElements<std::uint32_t>);
EI_REWRITTEN(0x004AB910, (stlp::CopyAssigned<RefPtr, stlp::CountedRefElement>));
EI_REWRITTEN(0x00429080, (stlp::CopyAssigned<RefPtr, stlp::CountedRefElement>));
EI_REWRITTEN(0x0053D7F0, StringVector::AllocateAndCopy);
