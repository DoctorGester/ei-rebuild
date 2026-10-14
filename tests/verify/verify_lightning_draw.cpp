// Checks for lightning_draw.cpp: ours only (both draw through the render device and draw random
// numbers). Draw returns 0 or 1; after a glow the two static arrays hold eight entries.
#include "verify.h"

#include "../../src/game/lightning.h"

namespace {
void* s_unused;
}

int __fastcall DrawLightning(CLightning* self, void*, void* device);

namespace {
int __fastcall DrawVerify(CLightning* self, void*, void* device) {
    int result = DrawLightning(self, nullptr, device);
    ei::verify::Check("CLightning::Draw", true, result == 0 || result == 1, "");
    return result;
}
void __fastcall GlowVerify(CLightning* self, void*, void* device, const Vector3* position, int phase, float size) {
    self->DrawGlow(device, position, phase, size);
    const int* sprites = reinterpret_cast<const int*>(0x0079C1E0);
    const int* points = reinterpret_cast<const int*>(0x0079C1D0);
    bool ok = (sprites[2] == 8 && points[1] == 8) || !sprites[1];
    ei::verify::Check("CLightning::DrawGlow", true, ok, "");
}
} // namespace

EI_VERIFY(0x00467AD0, DrawVerify, 0, s_unused);
EI_VERIFY(0x00467590, GlowVerify, 0, s_unused);
