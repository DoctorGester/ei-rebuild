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

// The deleting destructors of a holder of an engine object,
// released through slot 4 (ObjectHolderRelease, refptr_copies.cpp) before it is freed.
void __fastcall ObjectHolderRelease(CEngineObject** self);

// Slot 1 (the deleting destructor) of seven MFC collection instantiations, which only the
// vtables point at (world_deleting_destructors.cpp, mfc::impl::ScalarDeleting over the instantiations'
// MFC destructors, library rows, called at their addresses):
// The client's platform set (CList).
// A client map's CArray.
// The AI map's sector array (CArray<Sector>).
// A CList whose elements hold a CArray.
// The AI's reached-block list (CList).
// The AI map's other CList.
// The AI plan's waypoint list (CList).
