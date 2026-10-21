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
EI_REWRITTEN(0x0059CB50, Dwords::SetSize);
EI_REWRITTEN(0x0051EA10, Dwords::SetSize);
EI_REWRITTEN(0x0051EB20, Dwords::SetSize);
EI_REWRITTEN(0x0051EC30, Dwords::SetSize);
EI_REWRITTEN(0x0051ED40, Dwords::SetSize);
EI_REWRITTEN(0x0044FA90, Words::SetSize);
EI_REWRITTEN(0x0044F540, Dwords::SetSize);
EI_REWRITTEN(0x005E6810, Dwords::SetSize);
EI_REWRITTEN(0x004513A0, Dwords::SetSize);
EI_REWRITTEN(0x00452EB0, Dwords::SetSize);

// ---- SetAtGrow(index, value) -------------------------------------------------------------------
EI_REPLACE_METHOD(0x00452FC0, Words::SetAtGrow);
void __fastcall DwordArraySetAtGrowRef(Dwords* a, void*, int index, const std::uint32_t* value) {
    a->SetAtGrow(index, *value);
}
EI_REWRITTEN(0x00452C40, DwordArraySetAtGrowRef);
EI_REWRITTEN(0x004538D0, Dwords::SetAtGrow);
EI_REWRITTEN(0x0051EE50, Dwords::SetAtGrow);
EI_REWRITTEN(0x0059CCE0, Dwords::SetAtGrow);

// ---- Allocate(count) (the aligned data pointer) -------------------------------------------------
std::uint32_t* __fastcall AllocateDwords(int count) { return Dwords::Allocate(count); }
EI_REWRITTEN(0x0050E100, AllocateDwords);
EI_REWRITTEN(0x00514D30, AllocateDwords);
EI_REWRITTEN(0x0051A170, AllocateDwords);
EI_REWRITTEN(0x0051EF90, AllocateDwords);
EI_REWRITTEN(0x00453A10, AllocateDwords);
EI_REWRITTEN(0x00600A80, AllocateDwords);
// Byte-identical copies in the HUD / items range (A-H24).
EI_REWRITTEN(0x00664910, AllocateDwords);
EI_REWRITTEN(0x00671240, AllocateDwords);
Blob<32>* __fastcall AllocateBlocks32(int count) { return lib::AlignedArray<Blob<32>>::Allocate(count); }
EI_REWRITTEN(0x00452DA0, AllocateBlocks32);

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    void* data = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) lib::FreeAligned32(data);
}
} // namespace
void __fastcall ArrayFree1(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
EI_REWRITTEN(0x00505380, ArrayFree1);
EI_REWRITTEN(0x00509F40, ArrayFree1);
EI_REWRITTEN(0x00509F60, ArrayFree1);
EI_REWRITTEN(0x0050A090, ArrayFree1);
EI_REWRITTEN(0x0050A3F0, ArrayFree1);
EI_REWRITTEN(0x0050DAF0, ArrayFree1);
EI_REWRITTEN(0x005199F0, ArrayFree1);
EI_REWRITTEN(0x00519D10, ArrayFree1);
EI_REWRITTEN(0x0051E5B0, ArrayFree1);
EI_REWRITTEN(0x0051E8D0, ArrayFree1);
EI_REWRITTEN(0x0051E8F0, ArrayFree1);
EI_REWRITTEN(0x0051E910, ArrayFree1);
EI_REWRITTEN(0x0051E930, ArrayFree1);
EI_REWRITTEN(0x0051E950, ArrayFree1);
EI_REWRITTEN(0x00475F20, ArrayFree1);
EI_REWRITTEN(0x00600330, ArrayFree1);
EI_REWRITTEN(0x00600350, ArrayFree1);
EI_REWRITTEN(0x00600370, ArrayFree1);
EI_REWRITTEN(0x00600390, ArrayFree1);
EI_REWRITTEN(0x006003B0, ArrayFree1);
EI_REWRITTEN(0x00601B50, ArrayFree1);
EI_REWRITTEN(0x0044F1B0, ArrayFree1);
// Byte-identical copies in the world range (A-W12).
EI_REPLACE(0x0058A880, ArrayFree1);
EI_REWRITTEN(0x005995C0, ArrayFree1);
EI_REWRITTEN(0x00599780, ArrayFree1);
EI_REWRITTEN(0x0059A6F0, ArrayFree1);
EI_REWRITTEN(0x005A98B0, ArrayFree1);
EI_REWRITTEN(0x005BDEC0, ArrayFree1);
// A record holding such an array at +8 / +4.
void __fastcall ArrayAt8Free(void* self) { FreeAt(self, 8); }
EI_REWRITTEN(0x00454380, ArrayAt8Free);
void __fastcall ArrayAt4Free(void* self) { FreeAt(self, 4); }
EI_REWRITTEN(0x004543C0, ArrayAt4Free);

} // namespace aligned
