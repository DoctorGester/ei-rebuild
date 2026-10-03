// Differential checks for the particle emit callbacks (particle_emitters.cpp).
//
// An emit callback changes the new particle, its source, the source's element array and the ISAAC
// generator (007A9D40). Ours runs first; that state is captured, the state before is restored, the
// original runs, and both results are compared (the original's stand). Calls that allocate the
// element array (the first emission of a source) run ours only, checked for the array's size.
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

struct EmitState {
    unsigned char source[sizeof(CParticleSource)];
    unsigned char particle[sizeof(Particle)];
    unsigned char random[sizeof(IsaacRandom)];
    std::vector<unsigned char> elements;

    void Capture(const CParticleSource* s, const Particle* p) {
        std::memcpy(source, s, sizeof(source));
        std::memcpy(particle, p, sizeof(particle));
        std::memcpy(random, &GlobalRandom(), sizeof(random));
        std::size_t n = s->elements ? static_cast<std::size_t>(s->elementCount) * sizeof(ParticleElement) : 0;
        elements.assign(reinterpret_cast<const unsigned char*>(s->elements),
                        reinterpret_cast<const unsigned char*>(s->elements) + n);
    }
    void Restore(CParticleSource* s, Particle* p) const {
        std::memcpy(s, source, sizeof(source));
        std::memcpy(p, particle, sizeof(particle));
        std::memcpy(&GlobalRandom(), random, sizeof(random));
        if (!elements.empty()) std::memcpy(s->elements, elements.data(), elements.size());
    }
    // 0 same, 1 source, 2 particle, 3 generator, 4 elements.
    int Difference(const EmitState& o) const {
        if (std::memcmp(source, o.source, sizeof(source)) != 0) return 1;
        if (std::memcmp(particle, o.particle, sizeof(particle)) != 0) return 2;
        if (std::memcmp(random, o.random, sizeof(random)) != 0) return 3;
        if (elements != o.elements) return 4;
        return 0;
    }
};

using EmitFn = int(__fastcall*)(CParticleSource*, void*, Particle*, int);
using UpdateFn = int(__fastcall*)(CParticleSource*, void*, Particle*);
using ElementFn = void(__fastcall*)(CParticleSource*, void*, ParticleElement*);

// `ours` and `original` take no arguments (they capture them) and return the callback's result.
template <typename Ours, typename Original>
int RunBoth(const char* name, CParticleSource* self, Particle* p, Ours ours, Original original) {
    EmitState before, mine, theirs;
    before.Capture(self, p);
    int actual = ours();
    mine.Capture(self, p);
    before.Restore(self, p);
    int expected = original();
    theirs.Capture(self, p);
    int part = mine.Difference(theirs);
    ei::verify::Check(name, true, part == 0 && actual == expected, "type %X result %d vs %d, differs in %d",
                      self->type_, expected, actual, part);
    return expected;
}

template <typename Ours>
int RunBothEmit(const char* name, CParticleSource* self, Particle* p, int index, Ours ours, EmitFn original) {
    return RunBoth(name, self, p, [&] { return ours(self, p, index); },
                   [&] { return original(self, nullptr, p, index); });
}

// Element callbacks: the element lives in the captured array; a dummy particle stands in.
template <typename Ours>
void RunBothElement(const char* name, CParticleSource* self, ParticleElement* e, Ours ours, ElementFn original) {
    Particle dummy{};
    RunBoth(name, self, &dummy, [&] { ours(self, e); return 0; }, [&] { original(self, nullptr, e); return 0; });
}

// The first emission of a source with no element array yet: ours runs on the real source (its
// array stays); the original then runs on a scratch copy of the source as it was (allocating its
// own array, freed afterwards) with the particle and generator as they were. Everything is
// compared except the array's address; ours' particle and generator are put back.
template <typename Ours>
int RunBothSetup(const char* name, CParticleSource* self, Particle* p, int index, Ours ours, EmitFn original) {
    EmitState before, mine, theirs;
    before.Capture(self, p);
    int actual = ours(self, p, index);
    mine.Capture(self, p);

    alignas(16) unsigned char scratchBytes[sizeof(CParticleSource)];
    std::memcpy(scratchBytes, before.source, sizeof(scratchBytes));
    auto* scratch = reinterpret_cast<CParticleSource*>(scratchBytes);
    std::memcpy(p, before.particle, sizeof(before.particle));
    std::memcpy(&GlobalRandom(), before.random, sizeof(before.random));
    int expected = original(scratch, nullptr, p, index);
    theirs.Capture(scratch, p);
    if (scratch->elements) gamecrt::operator_delete(reinterpret_cast<void**>(scratch->elements)[-1]);
    std::memcpy(theirs.source + offsetof(CParticleSource, elements), mine.source + offsetof(CParticleSource, elements),
                sizeof(void*));
    int part = mine.Difference(theirs);
    ei::verify::Check(name, true, part == 0 && actual == expected, "type %X setup: result %d vs %d, differs in %d",
                      self->type_, expected, actual, part);

    std::memcpy(p, mine.particle, sizeof(mine.particle));
    std::memcpy(&GlobalRandom(), mine.random, sizeof(mine.random));
    return actual;
}

} // namespace

// 00497EB0: ours only (allocates). The block's address sits below the 32-byte aligned result.
static void* s_unusedAllocate;
static ParticleElement* __fastcall AllocateElementsVerify(int count) {
    ParticleElement* e = AllocateElements(count);
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

#define EI_EMIT_WRAPPER(Method)                                                              \
    static EmitFn s_##Method;                                                                                \
    static int __fastcall Method##Verify(CParticleSource* self, void*, Particle* p, int index) {             \
        return RunBothEmit("CParticleSource::" #Method, self, p, index,                                      \
                           [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::Method(q, i); }, \
                           s_##Method);                                                                      \
    }

#define EI_UPDATE_WRAPPER(Method)                                                            \
    static UpdateFn s_##Method;                                                                              \
    static int __fastcall Method##Verify(CParticleSource* self, void*, Particle* p) {                        \
        return RunBoth("CParticleSource::" #Method, self, p, [&] { return self->CParticleSource::Method(p); }, \
                       [&] { return s_##Method(self, nullptr, p); });                                        \
    }

#define EI_ELEMENT_WRAPPER(Method)                                                           \
    static ElementFn s_##Method;                                                                             \
    static void __fastcall Method##Verify(CParticleSource* self, void*, ParticleElement* e) {                \
        RunBothElement("CParticleSource::" #Method, self, e,                                                 \
                       [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::Method(x); }, s_##Method); \
    }

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
