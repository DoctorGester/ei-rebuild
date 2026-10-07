// Checks for scene_renderer.cpp (A-S8). 004BA120 sets the fog colour twice (the original, then ours
// with the same components; the render state ends as ours): the packed colours compared.
// Scenario: script_exec (SetBackGroundColor(40,60,80), script command 0x1F).
#include "verify.h"

#include "../../src/game/model.h"

// 004BA120: PUSH EBP; MOV EBP,ESP; PUSH ECX; FLD [EBP+8] (7 bytes).
static void(__fastcall* s_setFogColour)(CSceneRenderer*, void*, float, float, float);
static void __fastcall SetFogColourVerify(CSceneRenderer* self, void*, float r, float g, float b) {
    s_setFogColour(self, nullptr, r, g, b);
    const std::uint32_t expected = self->fogColour_;
    self->SetFogColour(r, g, b);
    ei::verify::Check("CSceneRenderer::SetFogColour", expected, self->fogColour_, "%08X");
}
EI_VERIFY(0x004BA120, SetFogColourVerify, 7, s_setFogColour);
