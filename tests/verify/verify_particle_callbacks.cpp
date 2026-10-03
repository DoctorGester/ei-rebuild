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

// 0048D8D0: SUB ESP,0x2C; PUSH EBX; MOV EBX,ECX (6). 0048DE30: PUSH EBP; MOV EBP,ESP; SUB ESP,0x3C (6).
// 0048E040: MOV AL,[ECX+8]; MOV ECX,[ESP+4] (7).
EI_SETUP_EMIT_WRAPPER(EmitLink)
EI_VERIFY(0x0048D8D0, EmitLinkVerify, 6, s_EmitLink);
EI_UPDATE_WRAPPER(UpdateLink)
EI_VERIFY(0x0048DE30, UpdateLinkVerify, 6, s_UpdateLink);
EI_ELEMENT_WRAPPER(TickLink)
EI_VERIFY(0x0048E040, TickLinkVerify, 7, s_TickLink);

// 0048C090: SUB ESP,0x24; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (7). 0048C590: PUSH EBP; MOV EBP,ESP;
// PUSH ECX; MOV EDX,[EBP+8] (7). 0048C760: MOV EAX,[ESP+4]; SUB ESP,0xC (7).
EI_EMIT_WRAPPER(EmitBlast)
EI_VERIFY(0x0048C090, EmitBlastVerify, 7, s_EmitBlast);
EI_UPDATE_WRAPPER(UpdateBlast)
EI_VERIFY(0x0048C590, UpdateBlastVerify, 7, s_UpdateBlast);
EI_ELEMENT_WRAPPER(TickBlast)
EI_VERIFY(0x0048C760, TickBlastVerify, 7, s_TickBlast);

// 0048B080: ours only (it takes particles from the shared pool into the source's list): nothing
// is emitted, and every particle of a freshly laid disc lies inside the bounds.
static EmitFn s_unusedPoisonFog;
static int __fastcall EmitPoisonFogVerify(CParticleSource* self, void*, Particle* p, int index) {
    int before = self->particles.count;
    int result = self->CParticleSource::EmitPoisonFog(p, index);
    bool inside = true;
    if (before == 0)
        for (auto* node = self->particles.head; node; node = node->next) {
            const Vector3& q = node->data->pos;
            inside = inside && !(q.x < self->boundsMin.x) && !(q.y < self->boundsMin.y) && !(q.z < self->boundsMin.z) &&
                     !(q.x > self->boundsMax.x) && !(q.y > self->boundsMax.y) && !(q.z > self->boundsMax.z);
        }
    ei::verify::Check("CParticleSource::EmitPoisonFog", true, result == 0 && inside && (before == 0 || self->particles.count == before),
                      "type %X: %d -> %d particles", self->type_, before, self->particles.count);
    return result;
}
EI_VERIFY(0x0048B080, EmitPoisonFogVerify, 0, s_unusedPoisonFog);

// 0048A8F0: MOV EAX,[0x7A9D44]; SUB ESP,0x14 (8). 0048ADA0: SUB ESP,8; PUSH ESI; MOV ESI,[ESP+0x10] (8).
// 0048AEC0: SUB ESP,0x14; PUSH ESI; MOV ESI,[ESP+0x1C] (8).
EI_EMIT_WRAPPER(EmitGeyser)
EI_VERIFY(0x0048A8F0, EmitGeyserVerify, 8, s_EmitGeyser);
EI_UPDATE_WRAPPER(UpdateGeyser)
EI_VERIFY(0x0048ADA0, UpdateGeyserVerify, 8, s_UpdateGeyser);
EI_ELEMENT_WRAPPER(TickGeyser)
EI_VERIFY(0x0048AEC0, TickGeyserVerify, 8, s_TickGeyser);

// 0048EEA0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x30 (6); the first call reallocates the elements.
// 0048F300: MOV EAX,[ESP+4]; MOV EDX,[EAX+0x50] (7). 0048F380: MOV EAX,[ESP+4]; MOV EDX,[EAX+0x30] (7).
static EmitFn s_EmitShower;
static int __fastcall EmitShowerVerify(CParticleSource* self, void*, Particle* p, int index) {
    auto ours = [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::EmitShower(q, i); };
    if (reinterpret_cast<const std::int32_t*>(self->elements)[0x30 / 4] < 0)
        return RunBothSetup("CParticleSource::EmitShower", self, p, index, ours, s_EmitShower);
    return RunBothEmit("CParticleSource::EmitShower", self, p, index, ours, s_EmitShower);
}
EI_VERIFY(0x0048EEA0, EmitShowerVerify, 6, s_EmitShower);
EI_UPDATE_WRAPPER(UpdateShower)
EI_VERIFY(0x0048F300, UpdateShowerVerify, 7, s_UpdateShower);
// After 5 ticks the source detaches (shared state): ours only, the flags checked.
static ElementFn s_TickShower;
static void __fastcall TickShowerVerify(CParticleSource* self, void*, ParticleElement* e) {
    if (reinterpret_cast<const std::int32_t*>(e)[0x30 / 4] >= 5) {
        self->CParticleSource::TickShower(e);
        ei::verify::Check("CParticleSource::TickShower", true, (self->flags_ & 1u) == 0 && !self->attachedTo,
                          "flags %X", self->flags_);
        return;
    }
    RunBothElement("CParticleSource::TickShower", self, e,
                   [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::TickShower(x); }, s_TickShower);
}
EI_VERIFY(0x0048F380, TickShowerVerify, 7, s_TickShower);

// 00493560: SUB ESP,0xC; PUSH EBP; MOV EBP,ECX (6); its first call fills the size table 007A9BFC,
// which the state compare does not see (the elements and particles that use it do).
// 00493770: MOV EDX,[ESP+4]; PUSH ESI; MOV EAX,[EDX+0x54] (8). 004938C0: SUB ESP,0x30; PUSH EBX;
// PUSH EBP; PUSH ESI (6).
EI_EMIT_WRAPPER(EmitBag)
EI_VERIFY(0x00493560, EmitBagVerify, 6, s_EmitBag);
EI_UPDATE_WRAPPER(UpdateBag)
EI_VERIFY(0x00493770, UpdateBagVerify, 8, s_UpdateBag);
EI_ELEMENT_WRAPPER(TickBag)
EI_VERIFY(0x004938C0, TickBagVerify, 6, s_TickBag);

// 00491420: SUB ESP,0x2C; PUSH EBX; PUSH EBP; MOV EBX,ECX (7). 00491830: MOV EAX,[ESP+4]; SUB ESP,0xC (7).
// 004918D0: MOV EAX,[ESP+4]; INC DWORD PTR [EAX+0x34] (7).
EI_SETUP_EMIT_WRAPPER(EmitColumn)
EI_VERIFY(0x00491420, EmitColumnVerify, 7, s_EmitColumn);
EI_UPDATE_WRAPPER(UpdateColumn)
EI_VERIFY(0x00491830, UpdateColumnVerify, 7, s_UpdateColumn);
EI_ELEMENT_WRAPPER(TickColumn)
EI_VERIFY(0x004918D0, TickColumnVerify, 7, s_TickColumn);
