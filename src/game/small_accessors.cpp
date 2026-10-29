// Small out-of-line accessors: a few instructions each. The script
// ones are called by SSCCmd::Execute and the script definition's
// Save only; both are rewritten with the same work inline (script_exec.cpp,
// script_save.cpp), so those hooks fire only for a caller still in the exe. All main thread.
#include "base_manager.h"
#include "combat.h"
#include "effects.h"
#include "item.h"
#include "map_object.h"
#include "material.h"
#include "script.h"
#include "unit.h"

#include "ei/x87.h"
#include <cstdint>
#include "lib/vecmath.h"

#include <cstring>

// ---- Materials ------------------------------------------------------------------------------------

std::uint32_t Material::GpuLitColour() const {
    const float alpha = colors.diffuse.w * 255.0f;
    return static_cast<std::uint32_t>(x87::fistp(alpha)) << 24 | 0x00FFFFFFu;
}


// The particle manager's own part of its constructor (the client world's manager set-up
// calls it after the base manager's): quietStart cleared.
void ParticleManagerClearQuietStart(ParticleManager* manager) {
    manager->quietStart = 0;
}
