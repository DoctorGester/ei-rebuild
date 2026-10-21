#pragma once

// The scalar deleting destructors of 00540000-005E0000 whose destructor bodies are rewritten
// elsewhere (A-W14; world_deleting_destructors.cpp): each runs the body and frees the object when
// bit 0 of the flags is set (RET 4, returns the object). docs/notes/world.md.

#include <cstdint>

class CEngineObject;
class CFigureManager;
struct AICallRecord;
struct AIRef;
struct RefPtr;
struct ScriptPointLight;
namespace mapspec {
struct Behaviour;
}

// 005754E0 / 00575510 / 005768A0 / 0059DDB0: the deleting destructors of a holder of an engine object,
// released through slot 4 (ObjectHolderRelease, refptr_copies.cpp) before it is freed.
void __fastcall ObjectHolderRelease(CEngineObject** self);

// S13: slot 1 (the deleting destructor) of seven MFC collection instantiations, which only the
// vtables point at (world_deleting_destructors.cpp, mfc::impl::ScalarDelete over the instantiations'
// MFC destructors, library rows, called at their addresses):
// 00571E50: the client's platform set (CList, vtable 0073F548); body 0056FE40.
// 00571ED0: a client map's CArray (vtable 0073F730); body 005711E0.
// 0059B430: the AI map's sector array (CArray<Sector>, vtable 0073F9EC); body 00599330.
// 0059B4F0: a CList whose elements hold a CArray (vtable 0073FA10); body 0059A3C0.
// 005B1B30: the AI's reached-block list (CList, vtable 0073FCFC); body 005B1810.
// 005B1B50: the AI map's other CList (vtable 0073FCF0); body 005B19A0.
// 005BE070: the AI plan's waypoint list (CList, vtable 0073FD9C); body 005BDEE0.
