#pragma once

// The scalar deleting destructors whose destructor bodies are rewritten elsewhere
// (world_deleting_destructors.cpp): each runs the body and frees the object when bit 0 of the flags is
// set, and returns the object. docs/notes/world.md.

#include <cstdint>

class CEngineObject;
namespace mapspec {
}

// The deleting destructors of a holder of an engine object, released through Release
// (ObjectHolderRelease, refptr_copies.cpp) before it is freed.
void ObjectHolderRelease(CEngineObject** self);
