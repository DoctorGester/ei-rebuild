#pragma once

// Orders the game takes from heap addresses, settled without them in ours (ScriptGroup::After is the same for
// script groups): the object sets a player is sent, the lit sectors SetSun relights, the client's draw lists and
// the client's player list. The harness patches the original's sites to do the same. See
// docs/notes/standalone-verification.md section 5.

#include <cstdint>

class CEngineObject;

namespace address_order {

// Whether the object set element `a` sorts before `b` (two object pointers as the sets keep them):
// the object's id (signed), then the address.
bool ObjectBefore(const CEngineObject* a, const CEngineObject* b);

// The draw lists' order: the first part set's texture (null first,
// then by name), then the figure (null first, then by name and morph weights), then the view depth.
// Objects with the same keys keep the sort's own order.
bool DrawBefore(CEngineObject* a, CEngineObject* b);

} // namespace address_order
