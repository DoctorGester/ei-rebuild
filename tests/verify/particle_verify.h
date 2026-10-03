#pragma once

// Shared pieces of the particle callback verify wrappers (verify_particle_emitters.cpp,
// verify_particle_callbacks.cpp): capture / restore of a source, a particle, the element array and
// the ISAAC generator (007A9D40), and wrappers that run ours, then the original from the same
// state, and compare everything (the original's results stand).
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern const std::vector<unsigned char>* g_seedElements;   // verify_particle_emitters.cpp

namespace particle_verify {

inline IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

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
    // 0 same, 1 source, 2 particle, 3 generator, 4 elements; `offset` gets the first differing byte.
    int Difference(const EmitState& o, int* offset) const {
        auto first = [offset](const unsigned char* a, const unsigned char* b, std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                if (a[i] != b[i]) {
                    *offset = static_cast<int>(i);
                    return true;
                }
            return false;
        };
        *offset = -1;
        if (first(source, o.source, sizeof(source))) return 1;
        if (first(particle, o.particle, sizeof(particle))) return 2;
        if (first(random, o.random, sizeof(random))) return 3;
        if (elements.size() != o.elements.size()) return 4;
        if (first(elements.data(), o.elements.data(), elements.size())) return 4;
        return 0;
    }
};

inline const char* PartName(int part) {
    static const char* const names[] = {"nothing", "source", "particle", "generator", "elements"};
    return names[part];
}


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
    int offset;
    int part = mine.Difference(theirs, &offset);
    ei::verify::Check(name, true, part == 0 && actual == expected,
                      "type %X result %d vs %d, differs in %s at +0x%X", self->type_, expected, actual,
                      PartName(part), offset);
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

// A private copy of the element array captured in `state` (32-byte aligned like 00497EB0's, the
// block below it), with room for `capacity` elements.
inline ParticleElement* CopyElements(const std::vector<unsigned char>& bytes, int capacity) {
    auto* raw = static_cast<unsigned char*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * 0x38 + 0x24));
    auto* aligned = reinterpret_cast<unsigned char*>((reinterpret_cast<std::uintptr_t>(raw) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(aligned)[-1] = raw;
    if (!bytes.empty()) std::memcpy(aligned, bytes.data(), bytes.size());
    return reinterpret_cast<ParticleElement*>(aligned);
}

// A call that (re)allocates the element array (the first emission of a source, or one that grows
// the array): ours runs on the real source (its array stays); the original then runs on a scratch
// copy of the source as it was, with a private copy of the array (or none), with the particle and
// generator as they were; the scratch's array is freed afterwards. Everything is compared except
// the array's address; ours' particle and generator are put back.
template <typename Ours>
int RunBothSetup(const char* name, CParticleSource* self, Particle* p, int index, Ours ours, EmitFn original) {
    EmitState before, mine, theirs;
    before.Capture(self, p);
    int actual = ours(self, p, index);
    mine.Capture(self, p);

    alignas(16) unsigned char scratchBytes[sizeof(CParticleSource)];
    std::memcpy(scratchBytes, before.source, sizeof(scratchBytes));
    auto* scratch = reinterpret_cast<CParticleSource*>(scratchBytes);
    if (scratch->elements) scratch->elements = CopyElements(before.elements, scratch->elementCapacity);
    std::memcpy(p, before.particle, sizeof(before.particle));
    std::memcpy(&GlobalRandom(), before.random, sizeof(before.random));
    g_seedElements = &mine.elements;
    int expected = original(scratch, nullptr, p, index);
    g_seedElements = nullptr;
    theirs.Capture(scratch, p);
    if (scratch->elements) gamecrt::operator_delete(reinterpret_cast<void**>(scratch->elements)[-1]);
    std::memcpy(theirs.source + offsetof(CParticleSource, elements), mine.source + offsetof(CParticleSource, elements),
                sizeof(void*));
    int offset;
    int part = mine.Difference(theirs, &offset);
    ei::verify::Check(name, true, part == 0 && actual == expected,
                      "type %X setup: result %d vs %d, differs in %s at +0x%X", self->type_, expected, actual,
                      PartName(part), offset);

    std::memcpy(p, mine.particle, sizeof(mine.particle));
    std::memcpy(&GlobalRandom(), mine.random, sizeof(mine.random));
    return actual;
}

} // namespace particle_verify

// Wrapper definitions for callbacks that run both ways (EmitFn / UpdateFn / ElementFn from above).
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

