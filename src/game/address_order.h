#pragma once

// Orders the game takes from heap addresses, settled without them in harness runs and in the
// standalone (ticket T2f; ScriptGroup::OrderById, script.h, is the same for script groups, SV7b1).
// Which address a block gets follows the heap's history, so these orders differ between the original
// and ours, and between two runs of ours. With ById on, ours uses the order given here, and the
// harness patches the original's sites to do the same (tests/verify/harness_address_order.cpp):
//
//   - the object sets a player is sent (world_object_sets.cpp, 0047ADF0 / 0047B1F0): by object id,
//     then address, instead of address; the order the client gets new objects in (message 0x40);
//   - the lit sectors SetSun spreads relighting over (terrain_animation.cpp, 006C8E90): by the
//     sectors' (sy, sx), as meant, instead of the slots' neighbours' pointers;
//   - the client's draw lists (DrawPass 0056AFE0, UnitOrderLess 00574300, world_rules.cpp; SV7b6):
//     by the texture's and the figure's names instead of their addresses, so the blending order of
//     objects and shadows, and with it the frame, does not follow the heap.

#include <cstdint>

class CEngineObject;

namespace address_order {

// From now on ours settles the orders above (the harness, before the game starts).
void UseIds();
// Whether ours does (UseIds was called, or the standalone, which always does).
bool ById();

// Whether the object set element `a` sorts before `b` (two object pointers as the sets keep them):
// the address, or with ById the object's id (signed), then the address.
bool ObjectBefore(std::uint32_t a, std::uint32_t b);

// The draw lists' order with ById (UnitOrderLess::Less): the first part set's texture (null first,
// then by name), then the figure (null first, then by name and morph weights), then the view depth
// as the original (world_rules.cpp). Objects with the same keys keep the sort's own order.
bool DrawBefore(CEngineObject* a, CEngineObject* b);

} // namespace address_order
