// Verify wrappers for the landscape's light tests (terrain_shadow.cpp).
#include "verify.h"

#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cstdint>

namespace {

// 006C92C0: SUB ESP,0x34; PUSH EBX; PUSH EBP (5 bytes). From SectorData::ShadeSun for every ground
// vertex lit while the landscape's sun shadows are on (flag 0x10). Side by side: the original
// first (the sectors its ray crosses are loaded through the hooked SectorAt and stamped with this
// frame, so ours finds the same sectors cached); the results compared.
int(__fastcall* s_sunReaches)(Terrain*, void*, const terrain::SectorPosition*);

int __fastcall SunReachesVerify(Terrain* self, void*, const terrain::SectorPosition* position) {
    const int expected = s_sunReaches(self, nullptr, position);
    const int actual = self->SunReaches(position);
    ei::verify::Check("Terrain::SunReaches", expected, actual, "sector %d,%d vertex %d,%d steps %d,%d",
                      self->lightingSector->sx, self->lightingSector->sy, self->lightingX, self->lightingY,
                      self->sunStepX, self->sunStepY);
    return expected;
}
EI_VERIFY(0x006C92C0, SunReachesVerify, 5, s_sunReaches);

// 006CB020: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes). Pure (AL). Every vertex lit, every
// figure drawn and every ground print made: all scenarios.
std::uint8_t(__fastcall* s_edgeFade)(Terrain*, void*, const Vector3*);

std::uint8_t __fastcall EdgeFadeVerify(Terrain* self, void*, const Vector3* point) {
    const std::uint8_t expected = s_edgeFade(self, nullptr, point);
    const std::uint8_t actual = self->EdgeFade(point);
    ei::verify::Check("Terrain::EdgeFade", expected, actual, "(%g, %g) of %g x %g: %d / %d", point->x, point->y,
                      self->width, self->height, expected, actual);
    return expected;
}
EI_VERIFY(0x006CB020, EdgeFadeVerify, 6, s_edgeFade);

} // namespace
