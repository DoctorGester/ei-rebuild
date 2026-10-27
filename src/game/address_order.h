#pragma once

// Orders the game takes from heap addresses, settled without them in ours (ScriptGroup::After,
// script.h, is the same for script groups). Which address a block gets follows the heap's history,
// so these orders would differ between the original and ours, and between two runs of ours. Ours uses the
// order given here, and the harness patches the original's sites to do the same
// (tests/verify/harness_address_order.cpp):
//   - the object sets a player is sent (world_object_sets.cpp): by object id,
//     then address, instead of address; the order the client gets new objects in (message 0x40);
//   - the lit sectors SetSun spreads relighting over (terrain_animation.cpp): by the
//     sectors' (sy, sx), as meant, instead of the slots' neighbours' pointers;
//   - the client's draw lists (DrawPass, UnitOrderLess, world_rules.cpp):
//     by the texture's and the figure's names instead of their addresses, so the blending order of
//     objects and shadows, and with it the frame, does not follow the heap;
//   - the client's player list (net_client_handlers.cpp): an update packet lists its
//     objects in the order of the server's ReplicaEntry addresses (Connection::priorities_), so the
//     players may come before the client's binding and be listed as nobody (the base screen's faces
//     and statuses missing); in ours the binding's arrival lists them again, as if it came first.

#include <cstdint>

class CEngineObject;

namespace address_order {

// Whether the object set element `a` sorts before `b` (two object pointers as the sets keep them):
// the object's id (signed), then the address.
bool ObjectBefore(const CEngineObject* a, const CEngineObject* b);

// The draw lists' order (UnitOrderLess::Less; address_order.cpp): the first part set's texture (null first,
// then by name), then the figure (null first, then by name and morph weights), then the view depth.
// Objects with the same keys keep the sort's own order.
bool DrawBefore(CEngineObject* a, CEngineObject* b);

} // namespace address_order
