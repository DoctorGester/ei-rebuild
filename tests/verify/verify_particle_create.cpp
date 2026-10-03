// Differential checks for CParticleSource's set-up (particle_create.cpp).
//
// CreateForType: the original runs first on a scratch copy of the source (no texture, element or
// frame array, so it neither releases nor frees anything of the real source), ours then runs on
// the real source with the generator restored and its element array seeded with the original's
// bytes (00497EB0's verify wrapper), so bytes neither writes compare equal. Compared: the result,
// the whole source except the two array addresses (and the frame capacity when the source had a
// frame array before), the element array, the frame rectangles, and the generator. The texture
// both take is the same cached resource, so +0x1EC compares equal; the scratch's reference is
// released afterwards.
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern const std::vector<unsigned char>* g_seedElements;   // verify_particle_emitters.cpp

namespace {

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

void ReleaseTexture(void* texture) {
    if (texture) (*static_cast<void (__fastcall***)(void*, void*)>(texture))[4](texture, nullptr);
}

void FreeAligned(const void* data) {
    if (data) gamecrt::operator_delete(reinterpret_cast<void* const*>(data)[-1]);
}

struct CreateState {
    unsigned char source[sizeof(CParticleSource)];
    unsigned char random[sizeof(IsaacRandom)];
    std::vector<unsigned char> elements, frames;

    void Capture(const CParticleSource* s) {
        std::memcpy(source, s, sizeof(source));
        std::memcpy(random, &GlobalRandom(), sizeof(random));
        auto* e = reinterpret_cast<const unsigned char*>(s->elements);
        elements.assign(e, e + (e ? s->elementCount * sizeof(ParticleElement) : 0));
        auto* f = reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(s->frameBase));
        frames.assign(f, f + (f ? s->frameRectCount * 16 : 0));
    }
};

int FirstDifference(const unsigned char* a, const unsigned char* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return static_cast<int>(i);
    return -1;
}

void Mask(unsigned char* bytes, std::size_t offset) { std::memset(bytes + offset, 0, 4); }

} // namespace

// 00485540: SUB ESP,0x17C (6 bytes).
using CreateFn = int(__fastcall*)(CParticleSource*, void*);
static CreateFn s_create;
static int __fastcall CreateForTypeVerify(CParticleSource* self, void*) {
    CreateState before;
    std::memcpy(before.source, self, sizeof(before.source));
    std::memcpy(before.random, &GlobalRandom(), sizeof(before.random));
    bool hadFrames = self->frameBase != 0;

    alignas(16) unsigned char scratchBytes[sizeof(CParticleSource)];
    std::memcpy(scratchBytes, self, sizeof(scratchBytes));
    auto* scratch = reinterpret_cast<CParticleSource*>(scratchBytes);
    scratch->texture = nullptr;
    scratch->elements = nullptr;
    scratch->elementCount = 0;
    scratch->elementCapacity = 0;
    scratch->frameBase = 0;
    scratch->frameRectCount = 0;
    scratch->frameRectCapacity = 0;
    int expected = s_create(scratch, nullptr);
    CreateState theirs;
    theirs.Capture(scratch);
    // The scratch's texture reference is kept until ours has loaded: released first, a texture no
    // other source holds is freed and ours gets a new one at another address (type 0x2005 "Vulkan").
    void* theirTexture = scratch->texture;
    FreeAligned(scratch->elements);
    FreeAligned(reinterpret_cast<void*>(static_cast<std::uintptr_t>(scratch->frameBase)));

    std::memcpy(&GlobalRandom(), before.random, sizeof(before.random));
    g_seedElements = &theirs.elements;
    int actual = self->CParticleSource::CreateForType();
    g_seedElements = nullptr;
    ReleaseTexture(theirTexture);
    CreateState mine;
    mine.Capture(self);

    for (CreateState* state : {&theirs, &mine}) {
        Mask(state->source, offsetof(CParticleSource, elements));
        Mask(state->source, offsetof(CParticleSource, frameBase));
        if (hadFrames) Mask(state->source, offsetof(CParticleSource, frameRectCapacity));
    }
    int source = FirstDifference(theirs.source, mine.source, sizeof(mine.source));
    int random = FirstDifference(theirs.random, mine.random, sizeof(mine.random));
    int elements = theirs.elements.size() != mine.elements.size() ? -2
                   : FirstDifference(theirs.elements.data(), mine.elements.data(), mine.elements.size());
    int frames = theirs.frames.size() != mine.frames.size() ? -2
                 : FirstDifference(theirs.frames.data(), mine.frames.data(), mine.frames.size());
    ei::verify::Check("CParticleSource::CreateForType", true,
                      expected == actual && source < 0 && random < 0 && elements == -1 && frames == -1,
                      "type %X result %d vs %d, source +0x%X, generator +0x%X, elements +0x%X, frames +0x%X",
                      self->type_, expected, actual, source, random, elements, frames);
    return actual;
}
EI_VERIFY(0x00485540, CreateForTypeVerify, 6, s_create);

// 004847E0: SUB ESP,0xC; PUSH ESI; MOV ESI,ECX (6 bytes). The original runs on a scratch copy
// without the texture and element array (it would release / free the real ones), ours on the real
// source; afterwards both have neither, and all 0x1F0 bytes are compared.
using ResetFn = void(__fastcall*)(CParticleSource*, void*);
static ResetFn s_reset;
static void __fastcall ResetVerify(CParticleSource* self, void*) {
    alignas(16) unsigned char scratchBytes[sizeof(CParticleSource)];
    std::memcpy(scratchBytes, self, sizeof(scratchBytes));
    auto* scratch = reinterpret_cast<CParticleSource*>(scratchBytes);
    scratch->texture = nullptr;
    scratch->elements = nullptr;
    s_reset(scratch, nullptr);
    self->CParticleSource::Reset();
    int offset = FirstDifference(scratchBytes, reinterpret_cast<const unsigned char*>(self), sizeof(scratchBytes));
    ei::verify::Check("CParticleSource::Reset", true, offset < 0, "type %X differs at +0x%X", self->type_, offset);
}
EI_VERIFY(0x004847E0, ResetVerify, 6, s_reset);

// 00484710: ours only (returns particles to the shared pool, detaches): the list is empty, the
// texture, attachment, callbacks and elements are gone.
static ResetFn s_unusedClear;
static void __fastcall ClearVerify(CParticleSource* self, void*) {
    self->CParticleSource::Clear();
    ei::verify::Check("CParticleSource::Clear", true,
                      self->particles.count == 0 && !self->particles.head && !self->particles.tail &&
                          !self->particles.blocks && !self->texture && !self->attachedTo && !self->elements &&
                          !self->emitFn,
                      "type %X: %d particles left", self->type_, self->particles.count);
}
EI_VERIFY(0x00484710, ClearVerify, 0, s_unusedClear);

// 004981B0 and 00498310 allocate / free: ours only. The new source has its vtable, type, an empty
// particle list in blocks of 10 and Reset's defaults; the destroyed one has an empty, block-less
// particle list.
static void* s_unusedFactory;
static CParticleSource* __fastcall NewParticleSourceVerify(void* ecx, void* edx, int type) {
    CParticleSource* s = NewParticleSource(ecx, edx, type);
    ei::verify::Check("NewParticleSource", true,
                      !s || (*reinterpret_cast<std::uintptr_t*>(s) == 0x0073CFD0 && s->type_ == type &&
                             s->particles.count == 0 && !s->particles.head && s->particles.blockSize == 10 &&
                             !s->elements && !s->texture && s->lifetime == -1 && s->frameCount == 1),
                      "type %X", type);
    return s;
}
EI_VERIFY(0x004981B0, NewParticleSourceVerify, 0, s_unusedFactory);

static void* s_unusedDestruct;
static void __fastcall DestructVerify(CParticleSource* self, void*) {
    self->CParticleSource::Destruct();
    ei::verify::Check("CParticleSource::Destruct", true,
                      self->particles.count == 0 && !self->particles.head && !self->particles.blocks,
                      "%d particles left", self->particles.count);
}
EI_VERIFY(0x00498310, DestructVerify, 0, s_unusedDestruct);
