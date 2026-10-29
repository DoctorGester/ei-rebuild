// Particle drawing: each visible source fills the shared vertex array (position and size interpolated between
// the last two ticks) and sprite array (interpolated colour, texture frame), binds its texture and hands both
// arrays to the renderer. Names are provisional.
#include "effects.h"
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

#define EI_ONE 1.0f

// The draw's work arrays, reused by every source.
std::vector<ParticleVertex> g_vertices;
std::vector<ParticleSprite> g_sprites;

constexpr int kReserve = 0x1000;

// How far the frame is between the last tick and the next, clamped to [0, 1] (NaN gives 1).
float Interpolation() {
    float fraction = g_clientWorld.tickFraction;
    float clamped = fraction <= 1.0f ? fraction : 1.0f;
    return clamped >= 0.0f ? clamped : 0.0f;
}

// One colour channel (at `shift`) between the previous and the current colour, truncated.
int MixChannel(std::uint32_t current, std::uint32_t previous, int shift, float t, float u) {
    return static_cast<int>(((current >> shift) & 0xFF) * t + ((previous >> shift) & 0xFF) * u);
}

} // namespace

int SceneRenderer::BindLoadedTexture(int stage, CTexture* texture) {
    EnsureTextureLoaded(texture);
    return SetTexture(stage, texture);
}

// Off screen (the renderer's clip test on the bounds says 2) only counts the hidden
// ticks. Otherwise the arrays are reserved for 0x1000 particles, sized to this source's, filled,
// and drawn; 0 when the texture cannot be bound.
int ParticleSource::Draw(SceneRenderer* renderer) {
    if (particles.empty()) return 1;
    if (renderer->pipeline->ClipTest(&clip) == 2) {
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
        float u = EI_ONE - t;
        ParticleVertex* vertex = g_vertices.data();
        do {
            const Particle* p = *cursor++;
            vertex->x = u * p->prevPos.x + t * p->pos.x;
            vertex->y = u * p->prevPos.y + t * p->pos.y;
            vertex->z = u * p->prevPos.z + t * p->pos.z;
            vertex->size = u * p->prevSize + t * p->size;
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
