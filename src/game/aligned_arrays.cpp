// The engine's arrays of plain elements in 32-byte aligned blocks (the block operator new returned
// is kept just before the data and freed through it), as instantiated around the animation and
// figure loaders: {data, size, capacity, growBy}. One template per operation;
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
EI_REWRITTEN(0x00452FC0, Words::SetAtGrow);
EI_REWRITTEN(0x00452C40, Dwords::SetAtGrowRef);
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
// Byte-identical copies in the HUD / items range.
EI_REWRITTEN(0x00664910, AllocateDwords);
EI_REWRITTEN(0x00671240, AllocateDwords);

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    void* data = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) lib::FreeAligned32(data);
}
} // namespace
// A record holding such an array at +8 / +4.
void __fastcall ArrayAt8Free(void* self) { FreeAt(self, 8); }
EI_REWRITTEN(0x00454380, ArrayAt8Free);
void __fastcall ArrayAt4Free(void* self) { FreeAt(self, 4); }
EI_REWRITTEN(0x004543C0, ArrayAt4Free);

} // namespace aligned
