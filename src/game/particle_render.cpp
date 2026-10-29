// Particle drawing: each visible source fills the shared vertex array (position and size interpolated between
// the last two ticks) and sprite array (interpolated colour, texture frame), binds its texture and hands both
// arrays to the renderer. Names are provisional. Floating point follows the original's x87 code: `double`
// for values the original keeps on the FPU stack, static_cast<float> where it stores to a float.
#include "effects.h"
#include "lib/static_object.h"
#include "texture.h"

#include "lib/crt.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <vector>
#include "model.h"
#include "game_singletons.h"
#include "world.h"

namespace {

inline double D(float f) { return f; }

// A zero-extended dword as a double (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

#define EI_ONE 1.0f

// The draw's work arrays, reused by every source.
std::vector<ParticleVertex> g_vertices;
std::vector<ParticleSprite> g_sprites;

constexpr int kReserve = 0x1000;

// How far the frame is between the last tick and the next, clamped to [0, 1] (NaN gives 1).
float Interpolation() {
    double ticks = g_clientWorld.tick_;
    float fraction = static_cast<float>((ticks + D(g_clientWorld.tickFraction_)) - ticks);
    float one;
    one = 1.0f;
    float clamped = fraction <= one ? fraction : one;
    return clamped >= 0.0f ? clamped : 0.0f;
}

// One colour channel (at `shift`) between the previous and the current colour, truncated.
int MixChannel(std::uint32_t current, std::uint32_t previous, int shift, float t, float u) {
    return static_cast<int>(libcrt::Ftol(Unsigned((current >> shift) & 0xFF) * D(t) + Unsigned((previous >> shift) & 0xFF) * D(u)));
}

} // namespace

int CSceneRenderer::BindLoadedTexture(int stage, CTexture* texture) {
    EnsureTextureLoaded(texture);
    return SetTexture(stage, texture);
}

// Off screen (the renderer's clip test on the bounds says 2) only counts the hidden
// ticks. Otherwise the arrays are reserved for 0x1000 particles, sized to this source's, filled,
// and drawn; 0 when the texture cannot be bound.
int CParticleSource::Draw(CSceneRenderer* renderer) {
    if (particles.empty()) return 1;
    if (renderer->pipeline_->ClipTest(&clip) == 2) {
        ++hiddenTicks;
        return 1;
    }
    hiddenTicks = 0;
    float t = Interpolation();
    g_sprites.resize(kReserve);
    g_vertices.resize(kReserve);
    g_sprites.resize(particles.size());
    g_vertices.resize(particles.size());
    if (!renderer->BindLoadedTexture(0, texture)) return 0;

    ParticleSprite* sprite = g_sprites.data();
    auto cursor = particles.begin();
    if (cursor != particles.end()) {
        float u = static_cast<float>(D(EI_ONE) - D(t));
        ParticleVertex* vertex = g_vertices.data();
        do {
            const Particle* p = *cursor++;
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
            // A frame past the grid (a link spark is emitted on a frame up to frameCount, one past the last): the
            // original read the heap after the frames there, here the first frame.
            sprite->frame = frames.data() + (p->prevFrame < std::ssize(frames) ? p->prevFrame : 0);
            ++sprite;
            ++vertex;
        } while (cursor != particles.end());
    }
    int blend;
    blend = std::bit_cast<std::int32_t>(additiveBlend);
    return renderer->DrawSprites(blend != 0, &g_vertices, &g_sprites);
}
