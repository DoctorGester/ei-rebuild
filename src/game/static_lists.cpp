// Static initialisers' list constructors (static_lists.h). Checked by
// tests/verify/verify_static_lists.cpp.
#include "static_lists.h"

#include "texture.h"
#include "world.h"

#include "ei/hook.h"

namespace static_lists {

void __stdcall ConstructDirectDrawDrivers() { DirectDrawDrivers().Construct(); }

void __stdcall ConstructDeferredUnits() { FrameDeferredUnits()->Construct(); }

} // namespace static_lists

EI_REPLACE(0x004ED540, static_lists::ConstructDirectDrawDrivers);
EI_REPLACE(0x0056A410, static_lists::ConstructDeferredUnits);
