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
CLightning* __fastcall NewObjectVerify(CLightningManager* self, void*, int type);
void __fastcall DestructVerify(CLightning* self);
} // namespace
void __fastcall LightningDestruct(CLightning* self);

namespace {
// 00468680 / 00468810: ours only (they allocate and free); the new bolt is reset (power 1, no
// textures, empty point arrays); the destroyed one has no textures.
CLightning* __fastcall NewObjectVerify(CLightningManager* self, void*, int type) {
    CLightning* bolt = self->NewObjectImpl(type);
    bool ok = !bolt || (bolt->power_ == 1.0f && !bolt->texture_ && !bolt->glowTexture_ && !bolt->main_.points.data &&
                        !bolt->branches_[5].points.data);
    ei::verify::Check("CLightningManager::NewObject", true, ok, "");
    return bolt;
}
void __fastcall DestructVerify(CLightning* self) {
    LightningDestruct(self);
    ei::verify::Check("CLightning::Destruct", true, !self->texture_ && !self->glowTexture_, "");
}
} // namespace

EI_VERIFY(0x004680D0, StartVerify, 0, s_unused);
EI_VERIFY(0x00468680, NewObjectVerify, 0, s_unused);
EI_VERIFY(0x00468810, DestructVerify, 0, s_unused);
