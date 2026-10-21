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

// ---- SetAtGrow(index, value) -------------------------------------------------------------------
EI_REPLACE_METHOD(0x00452FC0, Words::SetAtGrow);
void __fastcall DwordArraySetAtGrowRef(Dwords* a, void*, int index, const std::uint32_t* value) {
    a->SetAtGrow(index, *value);
}

// ---- Allocate(count) (the aligned data pointer) -------------------------------------------------
std::uint32_t* __fastcall AllocateDwords(int count) { return Dwords::Allocate(count); }
// Byte-identical copies in the HUD / items range (A-H24).
Blob<32>* __fastcall AllocateBlocks32(int count) { return lib::AlignedArray<Blob<32>>::Allocate(count); }

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    void* data = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) lib::FreeAligned32(data);
}
} // namespace
void __fastcall ArrayFree1(lib::AlignedArray<std::uint8_t>* a) { a->FreeData(); }
// Byte-identical copies in the world range (A-W12).
EI_REPLACE(0x0058A880, ArrayFree1);
// A record holding such an array at +8 / +4.
void __fastcall ArrayAt8Free(void* self) { FreeAt(self, 8); }
void __fastcall ArrayAt4Free(void* self) { FreeAt(self, 4); }

} // namespace aligned
