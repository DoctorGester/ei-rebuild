// Differential checks for the particle callbacks in particle_callbacks.cpp: ours and the original
// run from the same state (source, particle, element array, generator) and everything is compared
// (particle_verify.h). A first emission that allocates the element array runs the original on a
// scratch copy of the source.
#include "particle_verify.h"

using namespace particle_verify;

// Emit callbacks that set the element array up on their first call.
#define EI_SETUP_EMIT_WRAPPER(Method)                                                                \
    static EmitFn s_##Method;                                                                        \
    static int __fastcall Method##Verify(CParticleSource* self, void*, Particle* p, int index) {     \
        auto ours = [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::Method(q, i); }; \
        if (self->elementCount == 0 && !self->elements)                                              \
            return RunBothSetup("CParticleSource::" #Method, self, p, index, ours, s_##Method);      \
        return RunBothEmit("CParticleSource::" #Method, self, p, index, ours, s_##Method);           \
    }

// Prologues (whole instructions, no relative operands):
// 004918E0: SUB ESP,0x60; PUSH EBX; PUSH EBP; MOV EBX,ECX (7).
// 00492030: SUB ESP,0xC; PUSH ESI; PUSH EDI (5). 004921D0: SUB ESP,0x14; PUSH ESI; MOV ESI,[ESP+0x1C] (8).
EI_SETUP_EMIT_WRAPPER(EmitCasting)
EI_VERIFY(0x004918E0, EmitCastingVerify, 7, s_EmitCasting);
EI_UPDATE_WRAPPER(UpdateCasting)
EI_VERIFY(0x00492030, UpdateCastingVerify, 5, s_UpdateCasting);
EI_ELEMENT_WRAPPER(TickCasting)
EI_VERIFY(0x004921D0, TickCastingVerify, 8, s_TickCasting);

// 0048E070: PUSH EBP; MOV EBP,ESP; SUB ESP,0x2C (6).
EI_SETUP_EMIT_WRAPPER(EmitOrbit)
EI_VERIFY(0x0048E070, EmitOrbitVerify, 6, s_EmitOrbit);

// 00489320: SUB ESP,0x2C; PUSH EBX; MOV EBX,ECX (6). 00489A50: MOV EAX,[ESP+4]; PUSH ESI (5).
// 00489B40: MOV EAX,[ESP+4]; MOV EDX,[EAX+0x34] (7).
EI_SETUP_EMIT_WRAPPER(EmitCastRing)
EI_VERIFY(0x00489320, EmitCastRingVerify, 6, s_EmitCastRing);
EI_UPDATE_WRAPPER(UpdateCastRing)
EI_VERIFY(0x00489A50, UpdateCastRingVerify, 5, s_UpdateCastRing);
EI_ELEMENT_WRAPPER(TickCastRing)
EI_VERIFY(0x00489B40, TickCastRingVerify, 7, s_TickCastRing);

// 00490C10: SUB ESP,0x30; PUSH EBX; MOV EBX,ECX (6). Each emission adds an element until there are
// `lifetime` of them (the array may grow): those calls run the original on a scratch source.
static EmitFn s_EmitCastSwirl;
static int __fastcall EmitCastSwirlVerify(CParticleSource* self, void*, Particle* p, int index) {
    auto ours = [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::EmitCastSwirl(q, i); };
    if (self->elementCount < self->lifetime)
        return RunBothSetup("CParticleSource::EmitCastSwirl", self, p, index, ours, s_EmitCastSwirl);
    return RunBothEmit("CParticleSource::EmitCastSwirl", self, p, index, ours, s_EmitCastSwirl);
}
EI_VERIFY(0x00490C10, EmitCastSwirlVerify, 6, s_EmitCastSwirl);
// 00491230: SUB ESP,8; PUSH ESI; MOV ESI,[ESP+0x10] (8). 00491390: SUB ESP,0xC; MOV EAX,[ESP+0x10] (7).
EI_UPDATE_WRAPPER(UpdateCastSwirl)
EI_VERIFY(0x00491230, UpdateCastSwirlVerify, 8, s_UpdateCastSwirl);
EI_ELEMENT_WRAPPER(TickCastSwirl)
EI_VERIFY(0x00491390, TickCastSwirlVerify, 7, s_TickCastSwirl);

// 0048B630: PUSH EBP; MOV EBP,ESP; SUB ESP,0x2C (6). 0048BBE0: MOV EAX,[ESP+4]; PUSH ESI (5).
// 0048BCE0: MOV EAX,[ESP+4]; MOV EDX,[EAX+0x34] (7).
EI_SETUP_EMIT_WRAPPER(EmitHealing)
EI_VERIFY(0x0048B630, EmitHealingVerify, 6, s_EmitHealing);
EI_UPDATE_WRAPPER(UpdateHealing)
EI_VERIFY(0x0048BBE0, UpdateHealingVerify, 5, s_UpdateHealing);
EI_ELEMENT_WRAPPER(TickHealing)
EI_VERIFY(0x0048BCE0, TickHealingVerify, 7, s_TickHealing);
