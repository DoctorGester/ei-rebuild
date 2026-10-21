// The game's CArray<T> instantiations (mfc_array.h) registered at their own addresses: the ones something still
// reaches by game.exe's address (tools/registration_readers.py, C20c); our code calls the others directly.
// Instantiations with the same element policy share one body; the
// groups were found by comparing the instructions of every instantiation our code forwards to
// (addresses masked), the element sizes and helpers read from each.
#include "mfc_array.h"

#include "ei/hook.h"

namespace mfc::impl {

// ---- Constructors and destructors of arrays whose elements need nothing ---------------------------

// The constructor: the vtable, the rest zero (22 instantiations).
EI_REPLACE(0x005C1760, ArrayHeader<IntArrayClass>::Construct);

// The destructor: the vtable, the storage freed (20 instantiations).
EI_REPLACE(0x004600D0, ArrayHeader<IntArrayClass>::Destruct);
EI_REPLACE(0x00557250, ArrayHeader<PointArrayClass>::Destruct);

} // namespace mfc::impl
