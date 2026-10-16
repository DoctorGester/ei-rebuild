// Static initialisers' list constructors (static_lists.h). Checked in game by
// verify_static_lists.cpp until S21d.
#include "static_lists.h"

#include "app_helpers.h"
#include "texture.h"
#include "world.h"

#include "ei/hook.h"

namespace static_lists {

void __stdcall ConstructDirectDrawDrivers() { DirectDrawDrivers().Construct(); }

void __stdcall ConstructDeferredUnits() { FrameDeferredUnits()->Construct(); }

} // namespace static_lists

EI_REPLACE(0x004ED540, static_lists::ConstructDirectDrawDrivers);
EI_REPLACE(0x0056A410, static_lists::ConstructDeferredUnits);

// ~list() of the lists only exit functions destroy (S3c): every node's element destroyed and the
// node returned to the pool, then the sentinel. 004584E0: the copy protection's check list 0079BC2C
// (bytes; the exit function 00455F00). 004F0520: the DirectDraw driver list 007AEE64 (each driver's
// description, then its name; the guarded exit function 004ED5F0).
EI_REPLACE(0x004584E0, stlp::ListAbi<std::uint8_t>::Destruct);
EI_REPLACE(0x004F0520, stlp::ListAbi<DirectDrawDriver>::Destruct);
