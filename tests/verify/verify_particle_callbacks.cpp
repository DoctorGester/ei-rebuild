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

// 004889D0: SUB ESP,0x1C; PUSH EBX; PUSH EBP; MOV EBX,ECX (7). 00488DB0: MOV EDX,[ESP+4]; SUB ESP,8 (7).
EI_SETUP_EMIT_WRAPPER(EmitMushroom)
EI_VERIFY(0x004889D0, EmitMushroomVerify, 7, s_EmitMushroom);
EI_UPDATE_WRAPPER(UpdateMushroom)
EI_VERIFY(0x00488DB0, UpdateMushroomVerify, 7, s_UpdateMushroom);

// 0048D420: SUB ESP,0x2C; PUSH EBX; MOV EBX,ECX (6). 0048D7F0: MOV EAX,[ESP+4]; PUSH ESI (5).
EI_SETUP_EMIT_WRAPPER(EmitStench)
EI_VERIFY(0x0048D420, EmitStenchVerify, 6, s_EmitStench);
EI_UPDATE_WRAPPER(UpdateStench)
EI_VERIFY(0x0048D7F0, UpdateStenchVerify, 5, s_UpdateStench);

// 00489F70: PUSH EBP; MOV EBP,ESP; SUB ESP,0x28 (6). 0048A310: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6).
// 0048A560: MOV EAX,[ESP+4]; SUB ESP,0x24 (7). (Its Attach(null) on a free source has no effect
// outside the source, so it runs both ways.)
EI_EMIT_WRAPPER(EmitTornado)
EI_VERIFY(0x00489F70, EmitTornadoVerify, 6, s_EmitTornado);
EI_UPDATE_WRAPPER(UpdateTornado)
EI_VERIFY(0x0048A310, UpdateTornadoVerify, 6, s_UpdateTornado);
EI_ELEMENT_WRAPPER(TickTornado)
EI_VERIFY(0x0048A560, TickTornadoVerify, 7, s_TickTornado);

// 00493D10: MOV EAX,[ESP+4]; XOR EDX,EDX (6). 00493D50: SUB ESP,8; PUSH ESI; MOV ESI,ECX (6).
// 00493DE0: SUB ESP,0x34; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (7). 00494170: MOV EAX,[ESP+4];
// SUB ESP,0x10 (7). 00494340: MOV EAX,[ESP+4]; MOV EDX,[ECX+0x154] (10).
EI_EMIT_WRAPPER(EmitPortalStar)
EI_VERIFY(0x00493D10, EmitPortalStarVerify, 6, s_EmitPortalStar);
EI_UPDATE_WRAPPER(UpdatePortalStar)
EI_VERIFY(0x00493D50, UpdatePortalStarVerify, 6, s_UpdatePortalStar);
EI_EMIT_WRAPPER(EmitCylinder)
EI_VERIFY(0x00493DE0, EmitCylinderVerify, 7, s_EmitCylinder);
EI_UPDATE_WRAPPER(UpdateCylinder)
EI_VERIFY(0x00494170, UpdateCylinderVerify, 7, s_UpdateCylinder);
EI_ELEMENT_WRAPPER(TickCylinder)
EI_VERIFY(0x00494340, TickCylinderVerify, 10, s_TickCylinder);

// 0048C800: SUB ESP,0x14; PUSH EBX; PUSH EBP; PUSH ESI (6). 0048CAD0: MOV EAX,[ESP+4];
// MOV EDX,[EAX+0x50] (7). 0048CBA0: SUB ESP,0xC; PUSH EBX; PUSH EBP; PUSH ESI (6).
// 0048CED0: PUSH ESI; MOV ESI,[ESP+8]; PUSH EDI (6).
EI_EMIT_WRAPPER(EmitFireBall)
EI_VERIFY(0x0048C800, EmitFireBallVerify, 6, s_EmitFireBall);
EI_UPDATE_WRAPPER(UpdateFireBall)
EI_VERIFY(0x0048CAD0, UpdateFireBallVerify, 7, s_UpdateFireBall);
EI_EMIT_WRAPPER(EmitArrowSmoke)
EI_VERIFY(0x0048CBA0, EmitArrowSmokeVerify, 6, s_EmitArrowSmoke);
EI_UPDATE_WRAPPER(UpdateArrowSmoke)
EI_VERIFY(0x0048CED0, UpdateArrowSmokeVerify, 6, s_UpdateArrowSmoke);

// 00489BE0: SUB ESP,0x18; PUSH EBX; PUSH EBP; MOV EBX,ECX (7). 00489EF0: MOV EAX,[ESP+4];
// MOV ECX,[EAX+0x50] (7). 00489F30: MOV EAX,[ESP+4]; MOV EDX,[EAX+0x34] (7).
EI_SETUP_EMIT_WRAPPER(EmitBanshee)
EI_VERIFY(0x00489BE0, EmitBansheeVerify, 7, s_EmitBanshee);
EI_UPDATE_WRAPPER(UpdateBanshee)
EI_VERIFY(0x00489EF0, UpdateBansheeVerify, 7, s_UpdateBanshee);
EI_ELEMENT_WRAPPER(TickBanshee)
EI_VERIFY(0x00489F30, TickBansheeVerify, 7, s_TickBanshee);

// 00494470: SUB ESP,0x18; PUSH EBX; PUSH EDI; MOV EDI,ECX (7). 0048D050: SUB ESP,0x18; PUSH EBX;
// PUSH EBP; PUSH ESI (6). 0048D330: MOV EAX,[ESP+4]; SUB ESP,0x18 (7).
EI_EMIT_WRAPPER(EmitPortal)
EI_VERIFY(0x00494470, EmitPortalVerify, 7, s_EmitPortal);
EI_EMIT_WRAPPER(EmitAcidRay)
EI_VERIFY(0x0048D050, EmitAcidRayVerify, 6, s_EmitAcidRay);
EI_UPDATE_WRAPPER(UpdateAcidRay)
EI_VERIFY(0x0048D330, UpdateAcidRayVerify, 7, s_UpdateAcidRay);

// 00494A10: SUB ESP,0x28; PUSH EBP; PUSH ESI; MOV ESI,ECX (7).
EI_EMIT_WRAPPER(EmitSparks)
EI_VERIFY(0x00494A10, EmitSparksVerify, 7, s_EmitSparks);

// 0048F7F0: SUB ESP,0x24; PUSH EBX; MOV EBX,ECX; PUSH ESI (7). 0048FAB0: MOV EAX,[ESP+4];
// SUB ESP,0xC (7). 0048FBE0: SUB ESP,0x24; PUSH ESI; MOV ESI,[ESP+0x2C] (8).
EI_EMIT_WRAPPER(EmitClayRing)
EI_VERIFY(0x0048F7F0, EmitClayRingVerify, 7, s_EmitClayRing);
EI_UPDATE_WRAPPER(UpdateClayRing)
EI_VERIFY(0x0048FAB0, UpdateClayRingVerify, 7, s_UpdateClayRing);
EI_ELEMENT_WRAPPER(TickClayRing)
EI_VERIFY(0x0048FBE0, TickClayRingVerify, 8, s_TickClayRing);

// 0048F3B0: SUB ESP,0x18; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (7). 0048F6F0: SUB ESP,8; PUSH ESI;
// MOV ESI,[ESP+0x10] (8); from the second tick it detaches the source (shared state): ours only.
// 0048BD50: SUB ESP,0xC; PUSH EBX; PUSH ESI; MOV ESI,[ESP+0x18] (9). 0048C010: MOV EAX,[ESP+4];
// MOV EDX,[EAX+0x50] (7).
EI_EMIT_WRAPPER(EmitFalling)
EI_VERIFY(0x0048F3B0, EmitFallingVerify, 7, s_EmitFalling);
static ElementFn s_TickFalling;
static void __fastcall TickFallingVerify(CParticleSource* self, void*, ParticleElement* e) {
    if (reinterpret_cast<const std::int32_t*>(e)[0x30 / 4] > 0) {
        self->CParticleSource::TickFalling(e);
        ei::verify::Check("CParticleSource::TickFalling", true, (self->flags_ & 1u) == 0 && !self->attachedTo,
                          "flags %X", self->flags_);
        return;
    }
    RunBothElement("CParticleSource::TickFalling", self, e,
                   [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::TickFalling(x); }, s_TickFalling);
}
EI_VERIFY(0x0048F6F0, TickFallingVerify, 8, s_TickFalling);
EI_EMIT_WRAPPER(EmitVulkan)
EI_VERIFY(0x0048BD50, EmitVulkanVerify, 9, s_EmitVulkan);
EI_UPDATE_WRAPPER(UpdateVulkan)
EI_VERIFY(0x0048C010, UpdateVulkanVerify, 7, s_UpdateVulkan);

// 00494950: MOV EAX,[ECX+8]; AND AL,0xFE; MOV [ECX+8],EAX (8). 004949B0: MOV EAX,[ESP+4];
// MOV ECX,[EAX+0x50] (7). 004950B0: CMP DWORD [ECX+0x1C0],1 (7). 00495120: SUB ESP,0x24; PUSH EDI;
// MOV EDI,ECX (6). 00494E00: MOV EDX,[ECX+0x1C0]; PUSH ESI (7). 00494F30: SUB ESP,0x10; PUSH ESI;
// MOV ESI,[ESP+0x18] (8). 00492F40: MOV EAX,[ESP+4]; PUSH ESI; MOV ECX,[EAX+0x58] (8).
EI_EMIT_WRAPPER(EmitFireStar)
EI_VERIFY(0x00494950, EmitFireStarVerify, 8, s_EmitFireStar);
EI_UPDATE_WRAPPER(UpdateFireStar)
EI_VERIFY(0x004949B0, UpdateFireStarVerify, 7, s_UpdateFireStar);
EI_EMIT_WRAPPER(EmitRegeneration)
EI_VERIFY(0x004950B0, EmitRegenerationVerify, 7, s_EmitRegeneration);
EI_UPDATE_WRAPPER(UpdateRegeneration)
EI_VERIFY(0x00495120, UpdateRegenerationVerify, 6, s_UpdateRegeneration);
EI_EMIT_WRAPPER(EmitVisionStar)
EI_VERIFY(0x00494E00, EmitVisionStarVerify, 7, s_EmitVisionStar);
EI_UPDATE_WRAPPER(UpdateVisionStar)
EI_VERIFY(0x00494F30, UpdateVisionStarVerify, 8, s_UpdateVisionStar);
EI_UPDATE_WRAPPER(UpdateZoneExit)
EI_VERIFY(0x00492F40, UpdateZoneExitVerify, 8, s_UpdateZoneExit);

// 00495950: MOV EAX,[ESP+4]; MOV ECX,[EAX+0x50] (7). 00495C80: MOV EAX,[ESP+4]; SUB ESP,0x24 (7).
// 00495E00: SUB ESP,0x24; PUSH EBX; MOV EBX,ECX (6). 00495320: SUB ESP,0x34; PUSH EBX; MOV EBX,ECX
// (6). 004955D0: SUB ESP,0xC; PUSH ESI; MOV ESI,[ESP+0x14] (8).
EI_UPDATE_WRAPPER(UpdateFeebleMind)
EI_VERIFY(0x00495950, UpdateFeebleMindVerify, 7, s_UpdateFeebleMind);
EI_UPDATE_WRAPPER(UpdateFeetCloud)
EI_VERIFY(0x00495C80, UpdateFeetCloudVerify, 7, s_UpdateFeetCloud);
EI_ELEMENT_WRAPPER(TickFeetCloud)
EI_VERIFY(0x00495E00, TickFeetCloudVerify, 6, s_TickFeetCloud);
EI_EMIT_WRAPPER(EmitSilence)
EI_VERIFY(0x00495320, EmitSilenceVerify, 6, s_EmitSilence);
EI_UPDATE_WRAPPER(UpdateSilence)
EI_VERIFY(0x004955D0, UpdateSilenceVerify, 8, s_UpdateSilence);

// 00490990 and 00490B00: MOV EDX,[ESP+4]; SUB ESP,0xC (7). 00492B80: MOV EDX,[ESP+4]; PUSH ESI;
// MOV EAX,[EDX+0x50] (8). 00492CC0: MOV EAX,[ESP+4]; PUSH EBX (5).
EI_UPDATE_WRAPPER(UpdateModifier)
EI_VERIFY(0x00490990, UpdateModifierVerify, 7, s_UpdateModifier);
EI_ELEMENT_WRAPPER(TickModifier)
EI_VERIFY(0x00490B00, TickModifierVerify, 7, s_TickModifier);
EI_UPDATE_WRAPPER(UpdateLightning)
EI_VERIFY(0x00492B80, UpdateLightningVerify, 8, s_UpdateLightning);
EI_ELEMENT_WRAPPER(TickLightning)
EI_VERIFY(0x00492CC0, TickLightningVerify, 5, s_TickLightning);

// 0048FDB0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6). 00490060: MOV EAX,[ESP+4]; PUSH ESI;
// MOV EDX,[EAX+0x50] (8). 00490110: MOV EAX,[ESP+4]; MOV EDX,[EAX+0x34] (7). 00496270: SUB ESP,0x28;
// PUSH EBX; PUSH ESI (5). 004964B0: SUB ESP,8; PUSH ESI; MOV ESI,[ESP+0x10] (8); without a carrier
// it lets the source go (Attach): ours only. 004965E0: SUB ESP,0x28; PUSH EBP; PUSH ESI (5).
// 00496840: SUB ESP,8; PUSH ESI; MOV ESI,[ESP+0x10] (8).
EI_EMIT_WRAPPER(EmitTeleport)
EI_VERIFY(0x0048FDB0, EmitTeleportVerify, 6, s_EmitTeleport);
EI_UPDATE_WRAPPER(UpdateTeleport)
EI_VERIFY(0x00490060, UpdateTeleportVerify, 8, s_UpdateTeleport);
EI_ELEMENT_WRAPPER(TickTeleport)
EI_VERIFY(0x00490110, TickTeleportVerify, 7, s_TickTeleport);
EI_EMIT_WRAPPER(EmitCurseStars)
EI_VERIFY(0x00496270, EmitCurseStarsVerify, 5, s_EmitCurseStars);
static UpdateFn s_UpdateCurseStars;
static int __fastcall UpdateCurseStarsVerify(CParticleSource* self, void*, Particle* p) {
    if (!self->attachedTo) {
        int life = p->life;
        int result = self->CParticleSource::UpdateCurseStars(p);
        ei::verify::Check("CParticleSource::UpdateCurseStars", true,
                          life <= 0 || (result == 1 && (self->flags_ & 5u) == 0), "flags %X", self->flags_);
        return result;
    }
    return RunBoth("CParticleSource::UpdateCurseStars", self, p,
                   [&] { return self->CParticleSource::UpdateCurseStars(p); },
                   [&] { return s_UpdateCurseStars(self, nullptr, p); });
}
EI_VERIFY(0x004964B0, UpdateCurseStarsVerify, 8, s_UpdateCurseStars);
EI_EMIT_WRAPPER(EmitStartTrans)
EI_VERIFY(0x004965E0, EmitStartTransVerify, 5, s_EmitStartTrans);
EI_UPDATE_WRAPPER(UpdateStartTrans)
EI_VERIFY(0x00496840, UpdateStartTransVerify, 8, s_UpdateStartTrans);

// 00488F90: SUB ESP,0x20; PUSH EBX; PUSH EBP (5). 0048ED60: MOV EAX,[ESP+8]; SUB ESP,8 (7).
// 00493300: MOV EAX,[ESP+8]; SUB ESP,0xC (7).
EI_EMIT_WRAPPER(EmitNuke)
EI_VERIFY(0x00488F90, EmitNukeVerify, 5, s_EmitNuke);
EI_EMIT_WRAPPER(EmitTendril)
EI_VERIFY(0x0048ED60, EmitTendrilVerify, 7, s_EmitTendril);
EI_EMIT_WRAPPER(EmitGroundDust)
EI_VERIFY(0x00493300, EmitGroundDustVerify, 7, s_EmitGroundDust);
