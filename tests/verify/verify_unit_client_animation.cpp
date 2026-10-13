// Checks for unit_client_animation.cpp (A-U2).
// - InvisiblePulse (0051CC20) / StunnedPulse (0051CCD0): the original runs, the clock and the
//   drawn parts' alpha (tint and flags_ for the stun) are kept and put back, ours runs; compared
//   (floats within 4 ULPs: std::fmod and plain float maths).
// - AdvanceTime (00535B30): ours only (it places the unit, draws a random number and advances the
//   animation); the last time is the new time unless the time went back (or is NaN), when it stays;
//   with +0x260 set the animation speed is 1.
// - RestartAnimation (005352B0): ours only; unless frozen the queue holds at most one animation and
//   the last time is +0x258 or above it.
// - StartIdleAnimation (00535480): ours only; unless frozen, at most one more animation is queued.
// Reached by: slot 40 every frame for every unit on the client (units, combat, movement; the pulses
// need an invisible or stunned unit: spells_cast); slot 14 when a unit's animation restarts from
// the network state (units, combat); the idle animation from slot 14 and the client update
// (+0x255 == -2).
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unusedAdvanceTime;
void* s_unusedRestart;
void* s_unusedIdle;

struct PartSnapshot {
    float alpha;
    float tint[3];
    std::uint32_t flags;
};

std::vector<PartSnapshot> Snapshot(const CObject3DClientSpecific* half) {
    std::vector<PartSnapshot> parts;
    for (int i = 0; i < half->materials.size; ++i) {
        const CMaterial* part = half->materials[i];
        parts.push_back({part->colors.diffuse.w, {part->colors.specular.x, part->colors.specular.y, part->colors.specular.z}, part->flags_});
    }
    return parts;
}

void Restore(CObject3DClientSpecific* half, const std::vector<PartSnapshot>& parts) {
    for (int i = 0; i < half->materials.size; ++i) {
        CMaterial* part = half->materials[i];
        part->colors.diffuse.w = parts[i].alpha;
        std::memcpy(&part->colors.specular, parts[i].tint, sizeof(parts[i].tint));
        part->flags_ = parts[i].flags;
    }
}

using Pulse = void(__fastcall*)(CObject3DClientSpecific*, void*, float);

void ComparePulse(const char* name, Pulse original, CObject3DClientSpecific* self, float frames, bool stun) {
    float clock = self->pulseClock;
    std::vector<PartSnapshot> before = Snapshot(self);
    original(self, nullptr, frames);
    float expectedClock = self->pulseClock;
    std::vector<PartSnapshot> expected = Snapshot(self);
    self->pulseClock = clock;
    Restore(self, before);
    if (stun) self->StunnedPulse(frames);
    else self->InvisiblePulse(frames);
    std::vector<PartSnapshot> actual = Snapshot(self);
    bool same = ei::verify::NearlyEqual(expectedClock, self->pulseClock, 4, 1e-6f);
    for (std::size_t i = 0; same && i < actual.size(); ++i) {
        same = ei::verify::NearlyEqual(expected[i].alpha, actual[i].alpha, 4, 1e-6f) &&
               expected[i].flags == actual[i].flags;
        for (int k = 0; k < 3; ++k) same = same && ei::verify::NearlyEqual(expected[i].tint[k], actual[i].tint[k], 4, 1e-6f);
    }
    ei::verify::Check(name, true, same, "clock %g + %g, %d parts", clock, frames, self->materials.size);
}
} // namespace

// 0051CC20: FLD [ESP+4]; PUSH ESI; MOV ESI,ECX (7 bytes).
static Pulse s_invisiblePulse;
static void __fastcall InvisiblePulseVerify(CObject3DClientSpecific* self, void*, float frames) {
    ComparePulse("CObject3DClientSpecific::InvisiblePulse", s_invisiblePulse, self, frames, false);
}
EI_VERIFY(0x0051CC20, InvisiblePulseVerify, 7, s_invisiblePulse);

// 0051CCD0: FLD [ESP+4]; PUSH ESI; MOV ESI,ECX (7 bytes).
static Pulse s_stunnedPulse;
static void __fastcall StunnedPulseVerify(CObject3DClientSpecific* self, void*, float frames) {
    ComparePulse("CObject3DClientSpecific::StunnedPulse", s_stunnedPulse, self, frames, true);
}
EI_VERIFY(0x0051CCD0, StunnedPulseVerify, 7, s_stunnedPulse);

static void __fastcall AdvanceTimeVerify(CUnitClientSpecific* self, void*, float time) {
    float last = self->lastTime;
    volatile float stored = time - last;
    float step = stored;
    self->AdvanceTime(time);
    bool ok = !(step >= 0.0f) ? std::memcmp(&self->lastTime, &last, sizeof(float)) == 0
                              : std::memcmp(&self->lastTime, &time, sizeof(float)) == 0 &&
                                    (self->unit->unknown_260 == 0 || self->unit->animationSpeed == 1.0f);
    ei::verify::Check("CUnitClientSpecific::AdvanceTime", true, ok, "unit %d time %g (last %g)", self->unit->id_, time,
                      last);
}
EI_VERIFY(0x00535B30, AdvanceTimeVerify, 0, s_unusedAdvanceTime);

static void __fastcall RestartAnimationVerify(CUnitClientSpecific* self, void*, float time) {
    bool frozen = (self->unit->flags_ & 0x10000000) != 0;
    float last = self->lastTime;
    self->RestartAnimation(time);
    const CFigureAnimationState* state = self->unit->Object3D()->animation;
    bool ok = frozen ? std::memcmp(&self->lastTime, &last, sizeof(float)) == 0
                     : state->queue.count <= 1 && !(self->lastTime < static_cast<float>(self->unit->unknown_258));
    ei::verify::Check("CUnitClientSpecific::RestartAnimation", true, ok, "unit %d time %g, %d queued", self->unit->id_,
                      time, state->queue.count);
}
EI_VERIFY(0x005352B0, RestartAnimationVerify, 0, s_unusedRestart);

static void __fastcall StartIdleAnimationVerify(CUnitClientSpecific* self, void*, std::uint8_t play) {
    const CFigureAnimationState* state = self->unit->Object3D()->animation;
    int queued = state->queue.count;
    self->StartIdleAnimation(play);
    bool ok = state->queue.count >= 0 && state->queue.count <= queued + 1;
    ei::verify::Check("CUnitClientSpecific::StartIdleAnimation", true, ok, "unit %d play %d, %d -> %d queued",
                      self->unit->id_, play, queued, state->queue.count);
}
EI_VERIFY(0x00535480, StartIdleAnimationVerify, 0, s_unusedIdle);
