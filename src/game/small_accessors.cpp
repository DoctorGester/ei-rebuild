// Small out-of-line accessors: a few instructions each. The script
// ones are called by SSCCmd::Execute and the script definition's
// Save only; both are rewritten with the same work inline (script_exec.cpp,
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

std::uint32_t CMaterial::GpuLitColour() const {
    const float alpha = colors.diffuse.w * 255.0f;
    return static_cast<std::uint32_t>(x87::fistp(alpha)) << 24 | 0x00FFFFFFu;
}


// The particle manager's own part of its constructor (the client world's manager set-up
// calls it after the base manager's): quietStart cleared.
void __fastcall ParticleManagerClearQuietStart(CParticleManager* manager) {
    manager->quietStart = 0;
}
EI_REWRITTEN(0x004981A0, ParticleManagerClearQuietStart);

// ---- Script commands' accessors -------------------------------------------------------------------

EI_REWRITTEN(0x004BAA20, ObjectListIterator::First);
