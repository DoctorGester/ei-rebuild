#pragma once

// Static initialisers' constructors of global STLport lists (run by the image's initialiser list
// in the standalone build; the hook build sees them run before the hooks). Each builds the list
// empty: the head zeroed, then a sentinel from the node pool linked to itself. Also the
// destructors only exit functions call (static_lists.cpp: stlp::ListAbi's ~list()).

namespace static_lists {

void ConstructDirectDrawDrivers();   // the driver list (0x38-byte nodes)
void ConstructDeferredUnits();       // the deferred unit list (12-byte nodes)

} // namespace static_lists
