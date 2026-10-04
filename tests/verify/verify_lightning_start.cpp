// Checks for lightning_start.cpp: ours only (it draws random numbers, allocates the segments'
// points and loads textures). Afterwards the bolt has the power, both textures, every segment
// starts its noise at the same time, and the radius is the length of the grown box's corner.
#include "verify.h"

#include "../../src/game/lightning.h"

namespace {
void* s_unused;

void __fastcall StartVerify(CLightning* self, void*, const Vector3* from, const Vector3* to, float power, bool demon) {
    self->Start(from, to, power, demon);
    bool ok = self->texture_ && self->glowTexture_;
    for (const LightningSegment& b : self->branches_) ok = ok && b.noiseTime == self->main_.noiseTime;
    ei::verify::CheckNear("CLightning::Start radius", self->boundsMax_.Length(), self->radius_, "");
    ei::verify::Check("CLightning::Start", true, ok, "");
}
} // namespace

EI_VERIFY(0x004680D0, StartVerify, 0, s_unused);
