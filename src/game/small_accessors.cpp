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

// ---- Scene loader ---------------------------------------------------------------------------------

// 00471A90 (RET 0xC): a Vector3 from its three parts (a second copy of 00401DE0); returns it.
Vector3* Vector3::ConstructScene(float x_, float y_, float z_) {
    x = x_;
    y = y_;
    z = z_;
    return this;
}

// 00471AB0 / 00471AD0 / 00471AF0 (RET 4): a scene helper's (a CMaterial's) diffuse, ambient and
// specular red, green and blue, set from `value` (SceneInfo::Load builds each from three numbers
// of the file with 00471A90); the alphas stay.
void CMaterial::SetDiffuse(const Vector3* value) {
    std::memcpy(&colors.diffuse, value, sizeof(Vector3));
}

void CMaterial::SetAmbient(const Vector3* value) {
    std::memcpy(&colors.ambient, value, sizeof(Vector3));
}

void CMaterial::SetSpecular(const Vector3* value) {
    std::memcpy(&colors.specular, value, sizeof(Vector3));
}

// ---- Effects --------------------------------------------------------------------------------------

// 004981A0: the particle manager's own part of its constructor (the client world's manager set-up,
// 00561E50, calls it after the base manager's): quietStart cleared.
void __fastcall ParticleManagerClearQuietStart(CParticleManager* manager) {
    manager->quietStart = 0;
}

// ---- Script commands' accessors -------------------------------------------------------------------

// 004B9F50 / 004B9F60 (RET 4): bits of the object's flags set / cleared (the commands pass 0x10000).
void CEngineObject::SetFlagBits(std::uint32_t bits) {
    flags_ |= bits;
}

void CEngineObject::ClearFlagBits(std::uint32_t bits) {
    flags_ &= ~bits;
}

// 004B9F90 (RET 4): the map object's position.
void CMapObject::SetScriptPosition(const Vector3* point) {
    position = *point;
}
EI_REPLACE_METHOD(0x004B9F90, CMapObject::SetScriptPosition);

CItem* CUnit::CarriedItem(int index) const {
    return carried[index];
}
EI_REPLACE_METHOD(0x004BA080, CUnit::CarriedItem);

