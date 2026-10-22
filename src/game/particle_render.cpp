// Particle drawing (004976B0-00497EB0): each visible source fills the shared vertex array
// (007A9BD0: position and size interpolated between the last two ticks) and sprite array
// (007A9BE0: interpolated colour, texture frame), binds its texture and hands both arrays to the
// renderer (007AF060, vtable 0073D93C, slot 17 = 00502CA0). Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "effects.h"
#include "at_exit_functions.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"

#include <cstdint>
#include <cstring>
#include "model.h"
#include "originals_library.h"
#include "originals_render.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

inline double D(float f) { return f; }

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

#define EI_ONE 1.0f /*0073B84C*/

constinit lib::AlignedArray<ParticleVertex> g_vertices = /*007A9BD0*/ {};
lib::AlignedArray<ParticleVertex>& Vertices() { return g_vertices; }
void __cdecl FreeVertices() { g_vertices.FreeData(); }   // 00497D10
ParticleSpriteArray& Sprites() { return ei::GameData<ParticleSpriteArray>(0x007A9BE0); }
// The guard bits of the arrays below (function-local statics in the original).
std::uint32_t g_arraysConstructed = /*007A9BF4*/ {};

constexpr int kReserve = 0x1000;

void __fastcall ConstructSpriteArray(ParticleSpriteArray* a0) { mfc::impl::ConstructHeader<mfc::impl::SpriteArrayClass>((void*)a0); }
// The arrays are function-local statics in the original (guard bits 1 and 2 of g_arraysConstructed, the
// destructors 00497D30 / 00497D10 registered with atexit).
void ConstructArrays() {
    if (!(g_arraysConstructed & 1)) {
        g_arraysConstructed |= 1;
        ConstructSpriteArray(&Sprites());
        GameAtExit(ei::static_init::Exit00497D30);
    }
    if (!(g_arraysConstructed & 2)) {
        Vertices().data = nullptr;
        g_arraysConstructed |= 2;
        Vertices().growBy = 0;
        Vertices().capacity = 0;
        Vertices().size = 0;
        GameAtExit(FreeVertices);   // 00497D10
    }
}

// How far the frame is between the last tick and the next, clamped to [0, 1] (NaN gives 1).
float Interpolation() {
    double ticks = CurrentClientTick();
    float fraction = static_cast<float>((ticks + D(ClientTickFraction())) - ticks);
    float one;
    std::uint32_t oneBits = 0x3F800000;
    std::memcpy(&one, &oneBits, sizeof(one));
    float clamped = fraction <= one ? fraction : one;
    return clamped >= 0.0f ? clamped : 0.0f;
}

// One colour channel (at `shift`) between the previous and the current colour, truncated.
int MixChannel(std::uint32_t current, std::uint32_t previous, int shift, float t, float u) {
    return static_cast<int>(gamecrt::Ftol(Unsigned((current >> shift) & 0xFF) * D(t) + Unsigned((previous >> shift) & 0xFF) * D(u)));
}

} // namespace

// 00497D40: the list's GetNext (the list itself not read).
EI_REPLACE_METHOD(0x00497D40, mfc::CTypedPtrList<Particle>::GetNext);
// Byte-identical copies (other CTypedPtrList iterators in the world range; A-W12).
EI_REPLACE_METHOD(0x0059A040, mfc::CTypedPtrList<Particle>::GetNext);
EI_REPLACE_METHOD(0x0059A670, mfc::CTypedPtrList<Particle>::GetNext);
EI_REPLACE_METHOD(0x005BD340, mfc::CTypedPtrList<Particle>::GetNext);

int CSceneRenderer::BindHeldTexture(int stage, HeldTexture* holder) {
    TextureHolderEnsureLoaded(holder);
    return SetTexture(stage, *holder);
}
EI_REPLACE_METHOD(0x00497CE0, CSceneRenderer::BindHeldTexture);

// 00497D50. CArray::SetSize for the 8-byte sprites: new elements are zeroed.
EI_REPLACE_METHOD(0x00497D50, ParticleSpriteArray::SetSize);

// 004976B0. Off screen (the renderer's clip test on the bounds says 2) only counts the hidden
// ticks. Otherwise the arrays are reserved for 0x1000 particles, sized to this source's, filled,
// and drawn; 0 when the texture cannot be bound.
int CParticleSource::Draw(void* renderer) {
    if (particles.count == 0) return 1;
    CTransformPipeline* pipeline = static_cast<CTransformPipeline**>(renderer)[2];
    if (pipeline->ClipTest(&clip) == 2) {
        ++hiddenTicks;
        return 1;
    }
    hiddenTicks = 0;
    ConstructArrays();
    float t = Interpolation();
    Sprites().SetSize(kReserve, -1);
    Vertices().SetSize(kReserve);   // SetSize inlined by the original
    Sprites().SetSize(particles.count, -1);
    Vertices().SetSize(particles.count);
    if (!static_cast<CSceneRenderer*>(renderer)->BindHeldTexture(0, &texture)) return 0;

    ParticleSprite* sprite = Sprites().data;
    mfc::CTypedPtrList<Particle>::Node* cursor = particles.head;
    if (cursor) {
        float u = static_cast<float>(D(EI_ONE) - D(t));
        ParticleVertex* vertex = Vertices().data;
        do {
            const Particle* p = particles.GetNext(cursor);
            vertex->x = static_cast<float>(D(u) * D(p->prevPos.x) + D(t) * D(p->pos.x));
            vertex->y = static_cast<float>(D(u) * D(p->prevPos.y) + D(t) * D(p->pos.y));
            vertex->z = static_cast<float>(D(u) * D(p->prevPos.z) + D(t) * D(p->pos.z));
            vertex->size = static_cast<float>(D(u) * D(p->prevSize) + D(t) * D(p->size));
            std::uint32_t now = p->color;
            std::uint32_t before = p->prevColor;
            std::uint32_t argb = static_cast<std::uint32_t>(MixChannel(now, before, 16, t, u));
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 24, t, u)) << 8;
            argb <<= 8;
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 8, t, u));
            argb <<= 8;
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 0, t, u));
            sprite->color = argb;
            sprite->frame = frames.data + p->prevFrame;
            ++sprite;
            ++vertex;
        } while (cursor);
    }
    using DrawFn = int(__fastcall*)(void*, void*, int, ParticleVertexArray*, ParticleSpriteArray*);
    DrawFn draw = ei::ObjectSlot<DrawFn>(renderer, 17);
    int blend;
    std::memcpy(&blend, &additiveBlend, sizeof(blend));
    return draw(renderer, nullptr, blend != 0, &Vertices(), &Sprites());
}
EI_REWRITTEN(0x004976B0, CParticleSource::Draw);
EI_REPLACE(0x00497D10, FreeVertices);
