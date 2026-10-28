#pragma once

// Static initialisers' constructors of global lists (run by the image's initialiser list in the standalone
// build). Each builds the list empty; the exit function its initialiser registers destroys it
// (static_lists.cpp).

namespace static_lists {

void ConstructDeferredUnits();       // the frame's deferred unit list

} // namespace static_lists
