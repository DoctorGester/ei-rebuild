// The engine's arrays of plain elements in 32-byte aligned blocks (the block operator new returned
// is kept just before the data and freed through it), as instantiated around the animation and
// figure loaders (0044F1B0-00454A30): {data, size, capacity, growBy}. One template per operation;
// each instantiation is registered under its own name with its element size. The growth step is
// MFC's (growBy, else size / 8 clamped to 4 .. 1024). See docs/notes/objects.md ("Rewritten").
#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace aligned {

// The element types: plain bytes of the instantiation's size.
template <std::size_t K>
struct Blob {
    std::uint8_t bytes[K];
};
static_assert(sizeof(Blob<6>) == 6 && sizeof(Blob<12>) == 12 && sizeof(Blob<64>) == 64);

using Words = lib::AlignedArray<std::uint16_t>;
using Dwords = lib::AlignedArray<std::uint32_t>;
using Qwords = lib::AlignedArray<Blob<8>>;
using Vectors = lib::AlignedArray<Blob<12>>;
using Blocks16 = lib::AlignedArray<Blob<16>>;
using Matrices = lib::AlignedArray<Blob<64>>;

// ---- SetSize(n, growBy) ------------------------------------------------------------------------
EI_REPLACE_METHOD(0x0044FA90, Words::SetSize);
EI_REPLACE_METHOD(0x0044F540, Dwords::SetSize);
EI_REPLACE_METHOD(0x005E6810, Dwords::SetSize);   // byte-identical copy (interface)
EI_REPLACE_METHOD(0x004513A0, Dwords::SetSize);
EI_REPLACE_METHOD(0x00452EB0, Dwords::SetSize);
// Byte-identical copies in the units range (A-U11).
EI_REPLACE_METHOD(0x0051EA10, Dwords::SetSize);
EI_REPLACE_METHOD(0x0051EB20, Dwords::SetSize);
EI_REPLACE_METHOD(0x0051EC30, Dwords::SetSize);
EI_REPLACE_METHOD(0x0051ED40, Dwords::SetSize);
EI_REPLACE_METHOD(0x00453100, Qwords::SetSize);
EI_REPLACE_METHOD(0x0044F1D0, Vectors::SetSize);
EI_REPLACE_METHOD(0x00475F40, Vectors::SetSize);   // the scene loader's instantiation
EI_REPLACE_METHOD(0x0044F960, Matrices::SetSize);
// Copies found among the CArray instantiations (S6): the AI map's pointer lists, the lightning points.
EI_REPLACE_METHOD(0x0059CB50, Dwords::SetSize);
EI_REPLACE_METHOD(0x00468C50, Blocks16::SetSize);

// ---- Add(value) (the new element's index) ------------------------------------------------------
EI_REPLACE_METHOD(0x0044FBB0, Qwords::Add);
EI_REPLACE_METHOD(0x0044F2E0, Vectors::Add);

// ---- SetAtGrow(index, value) -------------------------------------------------------------------
EI_REPLACE_METHOD(0x00452FC0, Words::SetAtGrow);
void __fastcall DwordArraySetAtGrowRef(Dwords* a, void*, int index, const std::uint32_t* value) {
    a->SetAtGrow(index, *value);
}
EI_REPLACE(0x00452C40, DwordArraySetAtGrowRef);
EI_REPLACE_METHOD(0x004538D0, Dwords::SetAtGrow);
EI_REPLACE_METHOD(0x0051EE50, Dwords::SetAtGrow);   // byte-identical copy (A-U11)
EI_REPLACE_METHOD(0x0059CCE0, Dwords::SetAtGrow);   // byte-identical copy (S6)
EI_REPLACE_METHOD(0x00452AF0, Vectors::SetAtGrow);
EI_REPLACE_METHOD(0x00476DD0, Qwords::SetAtGrow);

// ---- operator=(other) --------------------------------------------------------------------------
EI_REPLACE_METHOD(0x00474C80, Qwords::Copy);
EI_REPLACE_METHOD(0x0044F410, Vectors::Copy);

// ---- Allocate(count) (the aligned data pointer) -------------------------------------------------
std::uint32_t* __fastcall AllocateDwords(int count) { return Dwords::Allocate(count); }
EI_REPLACE(0x00453A10, AllocateDwords);
EI_REPLACE(0x00600A80, AllocateDwords);   // byte-identical copy (the interface's UiPtrArray)
// Byte-identical copies in the units range (A-U11).
EI_REPLACE(0x0050E100, AllocateDwords);
EI_REPLACE(0x00514D30, AllocateDwords);
EI_REPLACE(0x0051A170, AllocateDwords);
EI_REPLACE(0x0051EF90, AllocateDwords);
// Byte-identical copies in the HUD / items range (A-H24).
EI_REPLACE(0x00664910, AllocateDwords);
EI_REPLACE(0x00671240, AllocateDwords);
Blob<6>* __fastcall AllocateWords6(int count) { return lib::AlignedArray<Blob<6>>::Allocate(count); }
EI_REPLACE(0x00452E90, AllocateWords6);
Blob<8>* __fastcall AllocateQwords(int count) { return Qwords::Allocate(count); }
EI_REPLACE(0x004545A0, AllocateQwords);
Blob<12>* __fastcall AllocateVectors(int count) { return Vectors::Allocate(count); }
EI_REPLACE(0x00452C20, AllocateVectors);
Blob<32>* __fastcall AllocateBlocks32(int count) { return lib::AlignedArray<Blob<32>>::Allocate(count); }
EI_REPLACE(0x00452DA0, AllocateBlocks32);
Blob<40>* __fastcall AllocateBlocks40(int count) { return lib::AlignedArray<Blob<40>>::Allocate(count); }
EI_REPLACE(0x00452D80, AllocateBlocks40);
Blob<64>* __fastcall AllocateMatrices(int count) { return Matrices::Allocate(count); }
EI_REPLACE(0x00452E70, AllocateMatrices);
Blob<16>* __fastcall AllocateBlocks16(int count) { return Blocks16::Allocate(count); }
EI_REPLACE(0x00468F70, AllocateBlocks16);   // the lightning points (S6)

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    void* data = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) lib::FreeAligned32(data);
}
} // namespace
void __fastcall ArrayFree1(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x0044F1B0, ArrayFree1);
// Byte-identical copies in the interface (A-I15): the window arrays' frees.
EI_REPLACE(0x00600330, ArrayFree1);
EI_REPLACE(0x00600350, ArrayFree1);
EI_REPLACE(0x00600370, ArrayFree1);
EI_REPLACE(0x00600390, ArrayFree1);
EI_REPLACE(0x006003B0, ArrayFree1);
EI_REPLACE(0x00601B50, ArrayFree1);
// Byte-identical copies in the units range (A-U11).
EI_REPLACE(0x00505380, ArrayFree1);
EI_REPLACE(0x00509F40, ArrayFree1);
EI_REPLACE(0x00509F60, ArrayFree1);
EI_REPLACE(0x0050A090, ArrayFree1);
EI_REPLACE(0x0050A3F0, ArrayFree1);
EI_REPLACE(0x0050DAF0, ArrayFree1);
EI_REPLACE(0x005199F0, ArrayFree1);
EI_REPLACE(0x00519D10, ArrayFree1);
EI_REPLACE(0x0051E5B0, ArrayFree1);
EI_REPLACE(0x0051E8D0, ArrayFree1);
EI_REPLACE(0x0051E8F0, ArrayFree1);
EI_REPLACE(0x0051E910, ArrayFree1);
EI_REPLACE(0x0051E930, ArrayFree1);
EI_REPLACE(0x0051E950, ArrayFree1);
// Byte-identical copies in the world range (A-W12).
EI_REPLACE(0x0058A880, ArrayFree1);
EI_REPLACE(0x005995C0, ArrayFree1);
EI_REPLACE(0x00599780, ArrayFree1);
EI_REPLACE(0x0059A6F0, ArrayFree1);
EI_REPLACE(0x005A98B0, ArrayFree1);
EI_REPLACE(0x005BDEC0, ArrayFree1);
void __fastcall ArrayFree2(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x0044FA70, ArrayFree2);
void __fastcall ArrayFree3(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x0044FB90, ArrayFree3);
void __fastcall ArrayFree4(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x00451380, ArrayFree4);
void __fastcall ArrayFree5(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x004534A0, ArrayFree5);
void __fastcall ArrayFree6(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x004549D0, ArrayFree6);
void __fastcall ArrayFree7(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x004549F0, ArrayFree7);
void __fastcall ArrayFree8(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x00454A10, ArrayFree8);
void __fastcall ArrayFree9(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x00454A30, ArrayFree9);
// A record holding such an array at +8 / +4.
void __fastcall ArrayAt8Free(void* self) { FreeAt(self, 8); }
EI_REPLACE(0x00454380, ArrayAt8Free);
void __fastcall ArrayAt4Free(void* self) { FreeAt(self, 4); }
EI_REPLACE(0x004543C0, ArrayAt4Free);

} // namespace aligned
