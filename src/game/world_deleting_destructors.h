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

// 00558CA0: a counted reference of the AI (AIRef::Reset 00558CE0).
AIRef* __fastcall AIRefDelete(AIRef* self, void*, unsigned flags);
// 00558CC0: a call record of a brain (AICallRecord::Destruct 00558D00).
AICallRecord* __fastcall AICallRecordDelete(AICallRecord* self, void*, unsigned flags);
// 00560590: an owning pointer (RefPtr::Destroy 0047A3F0); the driver session's teardown 0055F7B0
// calls it.
RefPtr* __fastcall RefPtrDestroyDelete(RefPtr* self, void*, unsigned flags);
// 0059DD70: a script point light (ScriptPointLightDestruct 004B17F0).
ScriptPointLight* __fastcall ScriptPointLightDelete(ScriptPointLight* self, void*, unsigned flags);
// 005AA1A0: the figure manager (slot 0 of 0073FB88; DestroyFigureManager 005AA1C0).
CFigureManager* __fastcall FigureManagerDelete(CFigureManager* self, void*, unsigned flags);
// 005DA290: a unit's behaviour record (mapspec::BehaviourDestruct 0046C290).
mapspec::Behaviour* __fastcall BehaviourDelete(mapspec::Behaviour* self, void*, unsigned flags);
// 005754E0 / 00575510 / 005768A0 / 0059DDB0: a holder of an engine object, released through slot 4
// (ObjectHolderRelease, refptr_copies.cpp) before it is freed.
CEngineObject** __fastcall ObjectHolderDelete(CEngineObject** self, void*, unsigned flags);
