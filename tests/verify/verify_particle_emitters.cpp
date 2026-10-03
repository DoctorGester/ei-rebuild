// Differential checks for the particle emit callbacks (particle_emitters.cpp).
//
// An emit callback changes the new particle, its source, the source's element array and the ISAAC
// generator (007A9D40). Ours runs first; that state is captured, the state before is restored, the
// original runs, and both results are compared (the original's stand). Calls that allocate the
// element array (the first emission of a source) run ours only, checked for the array's size.
#include "particle_verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace particle_verify;

// While the original runs the first emission on its scratch source, the element array it allocates
// starts with the bytes ours' array held: the bytes neither writes (padding such as +0x24..+0x30 of
// each point) then match, and a byte only the original writes still shows. CreateForType's verify
// (verify_particle_create.cpp) seeds ours' arrays from the original's the same way.
const std::vector<unsigned char>* g_seedElements = nullptr;

// 00497EB0: ours only (allocates). The block's address sits below the 32-byte aligned result.
static void* s_unusedAllocate;
static ParticleElement* __fastcall AllocateElementsVerify(int count) {
    ParticleElement* e = AllocateElements(count);
    if (g_seedElements) {
        std::size_t n = static_cast<std::size_t>(count) * sizeof(ParticleElement);
        if (n > g_seedElements->size()) n = g_seedElements->size();
        std::memcpy(e, g_seedElements->data(), n);
    }
    auto address = reinterpret_cast<std::uintptr_t>(e);
    auto raw = reinterpret_cast<std::uintptr_t>(reinterpret_cast<void**>(e)[-1]);
    ei::verify::Check("AllocateElements", true, (address & 0x1F) == 0 && address > raw && address - raw <= 0x23,
                      "count %d", count);
    return e;
}
EI_VERIFY(0x00497EB0, AllocateElementsVerify, 0, s_unusedAllocate);

// 004923B0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x5C (6 bytes).
static EmitFn s_emitLightning;
static int __fastcall EmitLightningVerify(CParticleSource* self, void*, Particle* p, int index) {
    auto ours = [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::EmitLightning(q, i); };
    if (self->elementCount == 0) {
        if (!self->elements) return RunBothSetup("CParticleSource::EmitLightning", self, p, index, ours, s_emitLightning);
        int result = ours(self, p, index);
        ei::verify::Check("CParticleSource::EmitLightning setup", true, self->elementCount == 10 && result == 1,
                          "%d points", self->elementCount);
        return result;
    }
    return RunBothEmit("CParticleSource::EmitLightning", self, p, index, ours, s_emitLightning);
}
EI_VERIFY(0x004923B0, EmitLightningVerify, 6, s_emitLightning);

// 00490130: PUSH EBP; MOV EBP,ESP; SUB ESP,0x58 (6 bytes).
static EmitFn s_emitModifier;
static int __fastcall EmitModifierVerify(CParticleSource* self, void*, Particle* p, int index) {
    auto ours = [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::EmitModifier(q, i); };
    if (self->elementCount == 0) {
        if (!self->elements) return RunBothSetup("CParticleSource::EmitModifier", self, p, index, ours, s_emitModifier);
        int result = ours(self, p, index);
        ei::verify::Check("CParticleSource::EmitModifier setup", true, self->elementCount > 0 || !self->attachedTo,
                          "%d points", self->elementCount);
        return result;
    }
    return RunBothEmit("CParticleSource::EmitModifier", self, p, index, ours, s_emitModifier);
}
EI_VERIFY(0x00490130, EmitModifierVerify, 6, s_emitModifier);

// --- Smaller callbacks: all run both ways -------------------------------------------------------

// Prologues (whole instructions, no relative operands):
// 00492D30, 004933F0, 00494770, 004957E0, 00495A30: PUSH EBP; MOV EBP,ESP; SUB ESP,imm8 (6 bytes).
// 00493280: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI (5). 004934F0: PUSH ESI; MOV ESI,[ESP+8] (5).
// 00494930: PUSH ESI; MOV ESI,ECX; FLD [ESI+0x10C] (9). 00494CD0: SUB ESP,8; PUSH ESI; MOV ESI,[ESP+0x10] (8).
EI_EMIT_WRAPPER(EmitZoneExit)
EI_VERIFY(0x00492D30, EmitZoneExitVerify, 6, s_EmitZoneExit);
EI_EMIT_WRAPPER(EmitFeebleMind)
EI_VERIFY(0x004957E0, EmitFeebleMindVerify, 6, s_EmitFeebleMind);
EI_EMIT_WRAPPER(EmitFeetCloud)
EI_VERIFY(0x00495A30, EmitFeetCloudVerify, 6, s_EmitFeetCloud);
EI_UPDATE_WRAPPER(UpdateGlow)
EI_VERIFY(0x00493280, UpdateGlowVerify, 5, s_UpdateGlow);
EI_UPDATE_WRAPPER(UpdateGroundDust)
EI_VERIFY(0x004933F0, UpdateGroundDustVerify, 6, s_UpdateGroundDust);
EI_UPDATE_WRAPPER(UpdatePortal)
EI_VERIFY(0x00494770, UpdatePortalVerify, 6, s_UpdatePortal);
EI_UPDATE_WRAPPER(UpdateSparks)
EI_VERIFY(0x00494CD0, UpdateSparksVerify, 8, s_UpdateSparks);
EI_ELEMENT_WRAPPER(TickGroundPlane)
EI_VERIFY(0x004934F0, TickGroundPlaneVerify, 5, s_TickGroundPlane);
EI_ELEMENT_WRAPPER(TickPortal)
EI_VERIFY(0x00494930, TickPortalVerify, 9, s_TickPortal);

// 00496990: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI; PUSH EDI (6 bytes). When the countdown has
// run out the source detaches (shared state): ours only, the flags checked.
static ElementFn s_TickStartTrans;
static void __fastcall TickStartTransVerify(CParticleSource* self, void*, ParticleElement* e) {
    if (e->ticks < 0) {
        self->CParticleSource::TickStartTrans(e);
        ei::verify::Check("CParticleSource::TickStartTrans", true, (self->flags_ & 5u) == 0 && !self->attachedTo,
                          "flags %X", self->flags_);
        return;
    }
    RunBothElement("CParticleSource::TickStartTrans", self, e,
                   [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::TickStartTrans(x); },
                   s_TickStartTrans);
}
EI_VERIFY(0x00496990, TickStartTransVerify, 6, s_TickStartTrans);
