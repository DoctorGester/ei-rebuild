#pragma once

// Static initialisers' constructors of global STLport lists (run by the image's initialiser list
// in the standalone build; the hook build sees them run before the hooks). Each builds the list
// empty: the head zeroed, then a sentinel from the node pool linked to itself.

namespace static_lists {

void __stdcall ConstructDirectDrawDrivers();   // 004ED540: the driver list 007AEE64 (0x38-byte nodes)
void __stdcall ConstructDeferredUnits();       // 0056A410: the deferred unit list 007AF5B4 (12-byte nodes)

} // namespace static_lists
