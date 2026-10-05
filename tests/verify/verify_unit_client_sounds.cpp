// Checks for unit_client_sounds.cpp (A-U). Both write only `out` and draw from rand(): the random
// state is saved, the original runs into a copy of `out`, the state is put back and ours runs into
// `out`; the result, the string and the random state afterwards are compared.
// Reached by: the client controller's step sounds (00534B60, every update of every unit on the
// client): FootstepSound for each footstep of a moving unit (movement, combat, units, unit_ai),
// ActionSound for animations with a sound frame (attacks, hits, deaths, idles, skills: combat,
// combat_more, units).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include <cstring>

// 00534590: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6 bytes).
static int(__fastcall* s_footstepSound)(CUnitClientSpecific*, void*, int, mfc::CString*);
static int __fastcall FootstepSoundVerify(CUnitClientSpecific* self, void*, int terrain, mfc::CString* out) {
    combatverify::RandomState before;
    combatverify::RandomState afterOriginal;
    combatverify::RandomState afterOurs;
    before.Save();
    mfc::CString scratch(*out);
    int expected = s_footstepSound(self, nullptr, terrain, &scratch);
    afterOriginal.Save();
    before.Restore();
    int actual = self->FootstepSound(terrain, out);
    afterOurs.Save();
    bool ok = expected == actual && std::strcmp(scratch, *out) == 0 && afterOriginal.SameAs(afterOurs);
    ei::verify::Check("CUnitClientSpecific::FootstepSound", true, ok, "unit %d terrain %d: %d '%s' vs %d '%s'",
                      self->unit->id_, terrain, expected, static_cast<const char*>(scratch), actual,
                      static_cast<const char*>(*out));
    return actual;
}
EI_VERIFY(0x00534590, FootstepSoundVerify, 6, s_footstepSound);

// 00534760: PUSH -1; PUSH 0x0071E9D8 (7 bytes).
static int(__fastcall* s_actionSound)(CUnitClientSpecific*, void*, const FigureAnimation*, float, float, mfc::CString*,
                                      std::uint8_t);
static int __fastcall ActionSoundVerify(CUnitClientSpecific* self, void*, const FigureAnimation* animation, float from,
                                        float to, mfc::CString* out, std::uint8_t miss) {
    combatverify::RandomState before;
    combatverify::RandomState afterOriginal;
    combatverify::RandomState afterOurs;
    before.Save();
    mfc::CString scratch(*out);
    int expected = s_actionSound(self, nullptr, animation, from, to, &scratch, miss);
    afterOriginal.Save();
    before.Restore();
    int actual = self->ActionSound(animation, from, to, out, miss);
    afterOurs.Save();
    bool ok = expected == actual && std::strcmp(scratch, *out) == 0 && afterOriginal.SameAs(afterOurs);
    ei::verify::Check("CUnitClientSpecific::ActionSound", true, ok, "unit %d key %08X: %d '%s' vs %d '%s'",
                      self->unit->id_, animation->key, expected, static_cast<const char*>(scratch), actual,
                      static_cast<const char*>(*out));
    return actual;
}
EI_VERIFY(0x00534760, ActionSoundVerify, 7, s_actionSound);

// 00534B60. Ours only (plays sounds, shakes the camera, adds footprints, draws rand()). Afterwards the
// controller remembers the animation playing and, unless the frame did not move (or there is no
// animation), its frame; the footprint countdown stays within 0..7.
static void* s_unusedPlayStepSounds;
static void __fastcall PlayStepSoundsVerify(CUnitClientSpecific* self) {
    const CFigureAnimationState* state = self->unit->Object3D()->animation;
    const FigureAnimation* current = state->current;
    float now = state->time;
    float last = self->lastAnimation == current ? self->lastAnimationTime : 0.0f;
    bool moved = now < last || now > last;
    self->PlayStepSounds();
    float expected = moved && current ? now : last;
    bool ok = self->lastAnimation == current && std::memcmp(&self->lastAnimationTime, &expected, sizeof(float)) == 0 &&
              self->footprintRepeats >= 0 && self->footprintRepeats <= 7;
    ei::verify::Check("CUnitClientSpecific::PlayStepSounds", true, ok, "unit %d: time %g (expected %g), repeats %d",
                      self->unit->id_, self->lastAnimationTime, expected, self->footprintRepeats);
}
EI_VERIFY(0x00534B60, PlayStepSoundsVerify, 0, s_unusedPlayStepSounds);
