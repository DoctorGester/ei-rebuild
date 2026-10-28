// The destructors of records that hold one of the engine's 32-byte aligned blocks (the block operator new
// returned is kept just before the data and freed through it): game_arrays.h's BlocksAt8 / BlocksAt4.
// See docs/notes/objects.md ("Rewritten").
#include "lib/containers.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

namespace aligned {

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    void* data = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) lib::FreeAligned32(data);
}
} // namespace
// A record holding such a block at +8 / +4.
void __fastcall ArrayAt8Free(void* self) { FreeAt(self, 8); }

} // namespace aligned
