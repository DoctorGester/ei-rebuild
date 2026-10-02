// Differential check for TerrainOwner::SurfaceHeight: every call runs the original and the
// rewritten function with the same arguments and logs any result that differs bit for bit.
#include "verify.h"

#include "../../src/game/terrain.h"

// 00564070 starts with MOV ECX,[ECX+0x180] (6 bytes, no relative operands).
static float(__fastcall* s_original)(TerrainOwner*, void*, float, float);

static float __fastcall SurfaceHeightVerify(TerrainOwner* self, void*, float x, float y) {
    float expected = s_original(self, nullptr, x, y);
    float actual = self->SurfaceHeight(x, y);
    ei::verify::Check("TerrainOwner::SurfaceHeight", expected, actual, "x=%g y=%g", x, y);
    return expected;
}
EI_VERIFY(0x00564070, SurfaceHeightVerify, 6, s_original);
