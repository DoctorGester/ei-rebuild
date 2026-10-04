// Checks for CAIMap::StampWaterPatch (src/game/ai_water_patch.cpp). Ours only (it changes the
// unit layer): afterwards the shared patch exists, is not detached and holds a reference.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>

namespace {
void* s_unused;
}

// 005B7EF0.
static void __fastcall StampWaterPatchVerify(CAIMap* self, void*, const Vector3* position, bool add) {
    self->StampWaterPatch(position, add);
    auto* patch = *reinterpret_cast<std::uint8_t**>(0x007C2F64);
    bool ok = patch && !(patch[7] & 0x80) && *reinterpret_cast<std::int16_t*>(patch + 4) > 0;
    ei::verify::Check("CAIMap::StampWaterPatch", true, ok, "add %d", add);
}
EI_VERIFY(0x005B7EF0, StampWaterPatchVerify, 0, s_unused);
