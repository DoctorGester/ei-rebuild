// The scalar deleting destructors the exe's vtables point at and the destructors they call, for the
// instantiations whose elements need nothing of the game (mfc_slots.h); the game-element
// ones are in src/game/game_slots.cpp. Each deleting destructor calls its class's destructor directly
// (by address), the destructors are templates.
#include "mfc_slots.h"

#include "crt.h"

namespace mfc::impl {

void __fastcall ExceptionDestruct(libcrt::Exception* self) { self->Destruct(); }

} // namespace mfc::impl
