#pragma once

// Orders the game takes from heap addresses, settled without them in ours (ScriptGroup::After is the same for
// script groups): the object sets a player is sent, the client's draw lists and the client's player list. The
// harness patches the original's sites to do the same. See docs/notes/standalone-verification.md section 5.

#include <cstdint>
#include <vector>

class EngineObject;
class MapObject;
class Object3D;

namespace object_order {

// Whether the object set element `a` sorts before `b` (two object pointers as the sets keep them):
// the object's id (signed), then the address.
bool ObjectBefore(const EngineObject* a, const EngineObject* b);

// The draw lists' order: the first part set's texture (null first,
// then by name), then the figure (null first, then by name and morph weights), then the view depth.
// Objects with the same keys keep the sort's own order.
bool DrawBefore(Object3D* a, Object3D* b);
// Sorts a draw list as lib::IntroSort with DrawBefore does, to the same permutation, on keys made once per object
// (the texture's and the figure's ranks among the list's, the depth's bits) instead of comparing names.
void SortForDraw(std::vector<MapObject*>& objects);

} // namespace object_order
