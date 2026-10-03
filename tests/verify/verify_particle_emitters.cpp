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

template <typename Ours>
int RunBothEmit(const char* name, CParticleSource* self, Particle* p, int index, Ours ours, EmitFn original) {
    EmitState before, mine, theirs;
    before.Capture(self, p);
    int actual = ours(self, p, index);
    mine.Capture(self, p);
    before.Restore(self, p);
    int expected = original(self, nullptr, p, index);
    theirs.Capture(self, p);
    int part = mine.Difference(theirs);
    ei::verify::Check(name, true, part == 0 && actual == expected, "type %X result %d vs %d, differs in %d",
                      self->type_, expected, actual, part);
    return expected;
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
