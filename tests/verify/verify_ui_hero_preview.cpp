// Check for the hero preview (ui_hero_preview.cpp).
//
// Ours only: the preview draws through Direct3D with a time-dependent animation step, so two runs
// cannot be compared. What it must leave: the model's position, orientation and flags as they
// were, its light factors back at 1, and the pass's colour (+0x18) as it was.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>

// 005EC300: PUSH EBP; MOV EBP,ESP; SUB ESP,0x110 (9).
static void(__thiscall* s_drawHeroPreview)(CUiWindow*, void*, std::uint8_t*, float, float, int);
static void __fastcall DrawHeroPreviewVerify(CUiWindow* self, void*, void* pass, std::uint8_t* model, float turn,
                                             float depth, int wide) {
    if (!model) {
        self->DrawHeroPreview(pass, model, turn, depth, wide);
        return;
    }
    std::uint8_t before[12 + 16 + 4];
    std::memcpy(before, model + 0x1C, 12);
    std::memcpy(before + 12, model + 0x34, 16);
    std::memcpy(before + 28, model + 0x08, 4);
    std::uint32_t colour;
    std::memcpy(&colour, static_cast<std::uint8_t*>(pass) + 0x18, 4);
    self->DrawHeroPreview(pass, model, turn, depth, wide);
    std::uint8_t after[sizeof(before)];
    std::memcpy(after, model + 0x1C, 12);
    std::memcpy(after + 12, model + 0x34, 16);
    std::memcpy(after + 28, model + 0x08, 4);
    std::uint32_t colourAfter;
    std::memcpy(&colourAfter, static_cast<std::uint8_t*>(pass) + 0x18, 4);
    std::uint8_t* light;
    std::memcpy(&light, model + 0x94, 4);
    float factors[3];
    std::memcpy(factors, light + 0xF0, 12);
    bool ok = std::memcmp(before, after, sizeof(before)) == 0 && colour == colourAfter && factors[0] == 1.0f &&
              factors[1] == 1.0f && factors[2] == 1.0f;
    ei::verify::Check("CUiWindow::DrawHeroPreview", true, ok, "transform kept %d, colour %08X -> %08X, light %g",
                      std::memcmp(before, after, sizeof(before)) == 0, colour, colourAfter, factors[0]);
}
EI_VERIFY(0x005EC300, DrawHeroPreviewVerify, 9, s_drawHeroPreview);
