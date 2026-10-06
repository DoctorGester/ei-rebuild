// The engine's arrays of plain elements in 32-byte aligned blocks (the block operator new returned
// is kept just before the data and freed through it), as instantiated around the animation and
// figure loaders (0044F1B0-00454A30): {data, size, capacity, growBy}. One template per operation;
// each instantiation is registered under its own name with its element size. The growth step is
// MFC's (growBy, else size / 8 clamped to 4 .. 1024). See docs/notes/objects.md ("Sixth round").
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
using Matrices = lib::AlignedArray<Blob<64>>;

// ---- SetSize(n, growBy) ------------------------------------------------------------------------
void __fastcall ShortArraySetSize(Words* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x0044FA90, ShortArraySetSize);
void __fastcall DwordArraySetSize(Dwords* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x0044F540, DwordArraySetSize);
void __fastcall DwordArraySetSize2(Dwords* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x004513A0, DwordArraySetSize2);
void __fastcall DwordArraySetSize3(Dwords* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x00452EB0, DwordArraySetSize3);
void __fastcall QwordArraySetSize(Qwords* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x00453100, QwordArraySetSize);
void __fastcall Vector3ArraySetSize(Vectors* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x0044F1D0, Vector3ArraySetSize);
void __fastcall Vector3ArraySetSize2(Vectors* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x00475F40, Vector3ArraySetSize2);   // the scene loader's instantiation
void __fastcall MatrixArraySetSize(Matrices* a, void*, int n, int growBy) { a->SetSize(n, growBy); }
EI_REPLACE(0x0044F960, MatrixArraySetSize);

// ---- Add(value) (the new element's index) ------------------------------------------------------
int __fastcall QwordArrayAdd(Qwords* a, void*, Blob<8> value) { return a->Add(value); }
EI_REPLACE(0x0044FBB0, QwordArrayAdd);
int __fastcall Vector3ArrayAdd(Vectors* a, void*, Blob<12> value) { return a->Add(value); }
EI_REPLACE(0x0044F2E0, Vector3ArrayAdd);

// ---- SetAtGrow(index, value) -------------------------------------------------------------------
void __fastcall ShortArraySetAtGrow(Words* a, void*, int index, std::uint16_t value) { a->SetAtGrow(index, value); }
EI_REPLACE(0x00452FC0, ShortArraySetAtGrow);
void __fastcall DwordArraySetAtGrowRef(Dwords* a, void*, int index, const std::uint32_t* value) {
    a->SetAtGrow(index, *value);
}
EI_REPLACE(0x00452C40, DwordArraySetAtGrowRef);
void __fastcall DwordArraySetAtGrow(Dwords* a, void*, int index, std::uint32_t value) { a->SetAtGrow(index, value); }
EI_REPLACE(0x004538D0, DwordArraySetAtGrow);
void __fastcall Vector3ArraySetAtGrow(Vectors* a, void*, int index, Blob<12> value) { a->SetAtGrow(index, value); }
EI_REPLACE(0x00452AF0, Vector3ArraySetAtGrow);
void __fastcall QwordArraySetAtGrow(Qwords* a, void*, int index, Blob<8> value) { a->SetAtGrow(index, value); }
EI_REPLACE(0x00476DD0, QwordArraySetAtGrow);

// ---- operator=(other) --------------------------------------------------------------------------
void __fastcall QwordArrayCopy(Qwords* a, void*, const Qwords* other) { a->Copy(*other); }
EI_REPLACE(0x00474C80, QwordArrayCopy);
void __fastcall Vector3ArrayCopy(Vectors* a, void*, const Vectors* other) { a->Copy(*other); }
EI_REPLACE(0x0044F410, Vector3ArrayCopy);

// ---- Allocate(count) (the aligned data pointer) -------------------------------------------------
std::uint32_t* __fastcall AllocateDwords(int count) { return Dwords::Allocate(count); }
EI_REPLACE(0x00453A10, AllocateDwords);
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

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    void* data = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) lib::FreeAligned32(data);
}
} // namespace
void __fastcall ArrayFree1(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REPLACE(0x0044F1B0, ArrayFree1);
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
