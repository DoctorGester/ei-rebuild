// A-W12: FigureMeshDrawer::Animate (005A7160). Compared: the original first, the mesh's texture
// offset (+0xCC / +0xD0, the only fields it writes) recorded and put back, then ours; bit for bit.
// Reached by every draw of an animated figure mesh (types 13..16; figure_part_draw*.cpp call it
// through its address).
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/link_figure.h"

#include <cstdint>

namespace {

// 005A7160: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESI+0x1C] (7 bytes).
void(__fastcall* s_animate)(FigureMeshDrawer*, void*, float);

void __fastcall AnimateVerify(FigureMeshDrawer* self, void*, float time) {
    CFigureMesh* mesh = self->mesh_;
    const std::uint32_t u = mesh->unknown_0CC, v = mesh->unknown_0D0;
    s_animate(self, nullptr, time);
    const std::uint32_t expectedU = mesh->unknown_0CC, expectedV = mesh->unknown_0D0;
    mesh->unknown_0CC = u;
    mesh->unknown_0D0 = v;
    self->Animate(time);
    ei::verify::Check("FigureMeshDrawer::Animate", true, mesh->unknown_0CC == expectedU && mesh->unknown_0D0 == expectedV,
                      "type %u time %g", mesh->unknown_15C, static_cast<double>(time));
}

} // namespace

EI_VERIFY(0x005A7160, AnimateVerify, 7, s_animate);
