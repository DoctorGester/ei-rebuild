#pragma once

// The scalar deleting destructors whose destructor bodies are rewritten
// elsewhere (A-W14; world_deleting_destructors.cpp): each runs the body and frees the object when
// bit 0 of the flags is set (RET 4, returns the object). docs/notes/world.md.

#include <cstdint>

class CEngineObject;
class CFigureManager;
struct AICallRecord;
struct RefPtr;
struct ScriptPointLight;
namespace mapspec {
struct Behaviour;
}

// The deleting destructors of a holder of an engine object, released through Release
// (ObjectHolderRelease, refptr_copies.cpp) before it is freed.
void __fastcall ObjectHolderRelease(CEngineObject** self);
