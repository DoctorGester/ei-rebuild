// std::exception's destructor, which the exception classes' own destructors jump to (mfc_slots.h).
#include "mfc_slots.h"

#include "crt.h"

namespace mfc::impl {

void __fastcall ExceptionDestruct(libcrt::Exception* self) { self->Destruct(); }

} // namespace mfc::impl
