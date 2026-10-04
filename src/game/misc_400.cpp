// Small pieces of 00400000-00440000 without a better home: the CBaseManager subclass 0073BECC
// (constructed by 005620D9; only its deleting destructor differs), a cursor reset of the coder and
// an exception cleanup step. See docs/notes/unmapped_410_470.md ("Eighth round").
#include "base_manager.h"
#include "net_coder.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace misc400 {

constexpr std::uintptr_t kManagerVtable = 0x0073BECC;

// 0042D690: the base manager constructed, then this class's vtable. Returns this.
CBaseManager* __fastcall ManagerConstruct(CBaseManager* self) {
    self->ConstructBase();
    std::memcpy(self, &kManagerVtable, 4);
    return self;
}

// 0042D6F0: the destructor body: this class's vtable, then the base manager's body (a tail call).
void __fastcall ManagerDestruct(CBaseManager* self) {
    std::memcpy(self, &kManagerVtable, 4);
    self->DestructBody();
}

// 00430FC0: a cursor started at `p` (no byte reserved, no pending bits; the accumulator is left
// alone). The two other arguments are not used. Returns 0.
std::uint32_t __fastcall CursorStart(BufferCursor* self, void*, std::uint8_t* p, std::uint32_t, std::uint32_t) {
    self->p = p;
    self->bits = 0;
    self->bytePtr = nullptr;
    return 0;
}

// 00433F10: an exception cleanup step: the pointer ECX points at is cleared where it points.
void __fastcall ClearPointee(void** self) { *static_cast<void**>(*self) = nullptr; }

} // namespace misc400

EI_REPLACE(0x0042D690, misc400::ManagerConstruct);
EI_REPLACE(0x0042D6F0, misc400::ManagerDestruct);
EI_REPLACE(0x00430FC0, misc400::CursorStart);
EI_REPLACE(0x00433F10, misc400::ClearPointee);
