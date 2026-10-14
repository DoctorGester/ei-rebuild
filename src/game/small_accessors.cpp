// Small out-of-line accessors of 0x00455610-0x004BC1F0 (A-E1): a few instructions each. The script
// ones (004B2520-004BC1F0) are called by SSCCmd::Execute (004B6760) and the script definition's
// Save (004BBFE0) only; both are rewritten with the same work inline (script_exec.cpp,
// script_save.cpp), so those hooks fire only for a caller still in the exe. All main thread.
#include "base_manager.h"
#include "combat.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "effects.h"
#include "engine_object.h"
#include "item.h"
#include "map_object.h"
#include "scene_loader.h"
#include "script.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

// ---- App ------------------------------------------------------------------------------------------

// 00455C80, 00455610 and 004BB050 (three identical copies): the destructor of a counted pointer
// that does not delete (an object list iterator's manager, base_manager.h, and the like). One
// reference dropped and the pointer cleared; nothing for a null pointer. 00455610 is called by
// unwind funclets only.
void __fastcall DropObjectReference(CEngineObject** slot) {
    if (!*slot) return;
    --(*slot)->refCount_;
    *slot = nullptr;
}
EI_REPLACE(0x00455C80, DropObjectReference);
EI_REPLACE(0x00455610, DropObjectReference);
EI_REPLACE(0x004BB050, DropObjectReference);

// ---- Scene loader ---------------------------------------------------------------------------------

// 00471A90 (RET 0xC): a Vector3 from its three parts (a second copy of 00401DE0); returns it.
Vector3* __fastcall Vector3FromParts(Vector3* self, void*, float x, float y, float z) {
    self->x = x;
    self->y = y;
    self->z = z;
    return self;
}
EI_REPLACE(0x00471A90, Vector3FromParts);

// 00471AB0 / 00471AD0 / 00471AF0 (RET 4): a scene helper's (a CMaterial's) diffuse, ambient and
// specular red, green and blue, set from `value` (SceneInfo::Load builds each from three numbers
// of the file with 00471A90); the alphas stay.
void CMaterial::SetDiffuse(const Vector3* value) {
    std::memcpy(&colors.diffuse, value, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x00471AB0, CMaterial::SetDiffuse);

void CMaterial::SetAmbient(const Vector3* value) {
    std::memcpy(&colors.ambient, value, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x00471AD0, CMaterial::SetAmbient);

void CMaterial::SetSpecular(const Vector3* value) {
    std::memcpy(&colors.specular, value, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x00471AF0, CMaterial::SetSpecular);

// ---- Effects --------------------------------------------------------------------------------------

// 004981A0: the particle manager's own part of its constructor (the client world's manager set-up,
// 00561E50, calls it after the base manager's): quietStart cleared.
void __fastcall ParticleManagerClearQuietStart(CParticleManager* manager) {
    manager->quietStart = 0;
}
EI_REPLACE(0x004981A0, ParticleManagerClearQuietStart);

// ---- Script commands' accessors -------------------------------------------------------------------

// 004B2520: the companion (+0x170, the side-specific half) of the referenced map object; null
// without an object.
CEngineObject* __fastcall ScriptRefCompanion(const ScriptObjectRef* ref) {
    if (!ref->object) return nullptr;
    return static_cast<CMapObject*>(ref->object)->companion;
}
EI_REPLACE(0x004B2520, ScriptRefCompanion);

// 004B9F50 / 004B9F60 (RET 4): bits of the object's flags set / cleared (the commands pass 0x10000).
void CEngineObject::SetFlagBits(std::uint32_t bits) {
    flags_ |= bits;
}
EI_REPLACE_METHOD(0x004B9F50, CEngineObject::SetFlagBits);

void CEngineObject::ClearFlagBits(std::uint32_t bits) {
    flags_ &= ~bits;
}
EI_REPLACE_METHOD(0x004B9F60, CEngineObject::ClearFlagBits);

// 004B9F90 (RET 4): the map object's position.
void CMapObject::SetScriptPosition(const Vector3* point) {
    position = *point;
}
EI_REPLACE_METHOD(0x004B9F90, CMapObject::SetScriptPosition);

// 004B9FD0: 1 when the counted object exists and its owner has not shut it down (the commands ask
// it of the game server), else 0.
int __fastcall ScriptSessionActive(const CRefCounted* session) {
    return session && !(session->flags7_ & CRefCounted::kDetached) ? 1 : 0;
}
EI_REPLACE(0x004B9FD0, ScriptSessionActive);

// 004BA010 (RET 4; `this` unused): a number rounded to an integer in the current rounding mode
// (FISTP).
int __fastcall ScriptRound(void*, void*, float value) {
    return x87::fistp(value);
}
EI_REPLACE(0x004BA010, ScriptRound);

// 004BA030: the item's condition back to its maximum.
void __fastcall ScriptItemRepair(CItem* item) {
    item->Repair();
}
EI_REPLACE(0x004BA030, ScriptItemRepair);

// 004BA060: the unit's server half.
CEngineObject* __fastcall ScriptUnitController(const CUnit* unit) {
    return unit->controller;
}
EI_REPLACE(0x004BA060, ScriptUnitController);

// 004BA070 / 004BA080 (RET 4): the number of items the unit carries, and one of them (no range check).
int __fastcall ScriptUnitCarriedCount(const CUnit* unit) {
    return unit->carriedCount;
}
EI_REPLACE(0x004BA070, ScriptUnitCarriedCount);

CEngineObject* CUnit::CarriedItem(int index) const {
    return carried[index];
}
EI_REPLACE_METHOD(0x004BA080, CUnit::CarriedItem);

// 004BA9C0: the statistics of the unit's server half.
UnitStats* __fastcall ScriptControllerStats(CUnitServer* controller) {
    return &controller->stats;
}
EI_REPLACE(0x004BA9C0, ScriptControllerStats);

// 004BAA20 (RET 4): the iterator set on `list`; its first object.
EI_REPLACE_METHOD(0x004BAA20, ObjectListIterator::First);

// 004BC1F0 (the text in ECX, the stream in EDX): the text written with its terminator.
void __fastcall ScriptWriteText(const mfc::CString* text, CStream* stream) {
    stream->Write(static_cast<const char*>(*text), static_cast<std::uint32_t>(text->GetLength() + 1));
}
EI_REPLACE(0x004BC1F0, ScriptWriteText);
