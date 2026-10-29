// The renderer's sprite draw with its helpers (indexed draw, a radix sort pass, a merge sort pass).
// More: docs/notes/comments-c22i.md, "renderer_sprites.cpp".
#include "clip.h"   // TLVertex
#include "model.h"
#include "render/render.h"
#include "render/render_state.h"
#include "texture.h"

#include "lib/containers.h"
#include "lib/vecmath.h"

#include "ei/x87.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

// One record of the back-to-front sort.
struct SpriteSortRecord {
    std::int32_t key;             // the first corner's z, as bits (never negative here)
    SpriteSortRecord* next;       // the radix pass's bucket list
    TLVertex* corners;            // the sprite's four corners (in the copy)
};

void SpriteRadixPass(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                int ascending);
void SpriteMergePass(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count);

namespace {

// --- The arguments -------------------------------------------------------------------------------

struct Sprite {               // 8 bytes
    std::uint32_t color;      // ARGB
    const float* uv;          // the texture rectangle: u0, v0, u1, v1
};

// --- The statics ---------------------------------------------------------------------------------

// The sprite draw's work buffers: vectors of vertices and indices.
std::vector<TLVertex> g_corners;         // 4 per sprite
std::vector<std::uint16_t> g_indices;
// The sort's buffers.
std::vector<TLVertex> g_cornersCopy;     // the records point here
std::vector<SpriteSortRecord> g_records;
std::vector<SpriteSortRecord*> g_order;
std::vector<SpriteSortRecord*> g_spare;


// --- The steps -----------------------------------------------------------------------------------

void SetCorner(TLVertex& v, std::uint32_t color, const float* u, const float* t) {
    v.color = color;
    v.specular = 0xFF000000;
    v.u = *u;
    v.v = *t;
}

// The colour and texture coordinates of `count` sprites' corners: (u0,v0), (u1,v0), (u1,v1), (u0,v1).
void FillCorners(const Sprite* sprite, int count) {
    g_corners.resize(static_cast<std::size_t>(count) * 4);
    TLVertex* v = g_corners.data();
    for (int i = 0; i < count; ++i, ++sprite, v += 4) {
        const float* uv = sprite->uv;
        std::uint32_t color = sprite->color;
        SetCorner(v[0], color, &uv[0], &uv[1]);
        SetCorner(v[1], color, &uv[2], &uv[1]);
        SetCorner(v[2], color, &uv[2], &uv[3]);
        SetCorner(v[3], color, &uv[0], &uv[3]);
    }
}

// Sorts the batch back to front: a record per sprite whose depth (its corners' z) lies in 0..1 (its
// bits are the key), sorted largest first by four radix passes above 128 sprites, else by merge
// passes; the corners are then written back in that order. Returns the sprites kept.
int SortBackToFront() {
    // The depth range is tested here, not by the GPU: a pre-transformed draw keeps z inside it. A sprite
    // behind the camera has a z above 1 (1 / w is negative), one at w 0 an infinite or NaN z.
    constexpr std::int32_t kFarKey = std::bit_cast<std::int32_t>(1.0f);
    g_cornersCopy.resize(std::ssize(g_corners));
    std::memcpy(g_cornersCopy.data(), g_corners.data(), static_cast<std::size_t>(std::ssize(g_corners)) * sizeof(TLVertex));
    TLVertex* corners = g_cornersCopy.data();
    int sprites = std::ssize(g_cornersCopy) / 4;
    g_records.resize(sprites);
    g_order.resize(sprites);
    int kept = 0;
    for (int i = 0; i < sprites; ++i, corners += 4) {
        SpriteSortRecord* record = g_records.data() + kept;
        record->key = std::bit_cast<std::int32_t>(corners->z);       // written for dropped sprites too
        if (record->key < 0 || record->key > kFarKey) continue;
        record->corners = corners;
        g_order[kept] = record;
        ++kept;
    }
    g_spare.resize(kept);

    auto* sorted = g_order.data();
    auto* other = g_spare.data();
    if (kept > 0x80) {
        SpriteRadixPass(0, sorted, other, kept, 1);
        SpriteRadixPass(8, other, sorted, kept, 0);
        SpriteRadixPass(16, sorted, other, kept, 1);
        SpriteRadixPass(24, other, sorted, kept, 0);
    } else if (kept > 1) {
        int width = 0x40000000;          // the largest power of two below the count, then halving
        if (kept <= width) {
            do width >>= 1;
            while (width >= kept);
        }
        while (width > 0) {
            SpriteMergePass(width, sorted, other, kept);
            std::swap(sorted, other);
            width >>= 1;
        }
    }

    g_corners.resize(kept * 4);
    TLVertex* out = g_corners.data();
    for (int i = 0; i < kept; ++i) std::memcpy(out + i * 4, sorted[i]->corners, 4 * sizeof(TLVertex));
    return kept;
}

// The batch's triangle list through the device object (fogged by distance when it says so). True.
bool Draw(CSceneRenderer* renderer, int vertexCount, int indexCount) {
    return renderer->device->DrawTlTriangles(g_corners.data(), vertexCount, g_indices.data(), indexCount) != 0;
}

void SetTriangle(std::uint16_t* tri, int a, int b, int c) {
    tri[0] = static_cast<std::uint16_t>(a);
    tri[1] = static_cast<std::uint16_t>(b);
    tri[2] = static_cast<std::uint16_t>(c);
}

// Two triangles per sprite, (0, 2, 1) and (0, 3, 2); the GPU clips the ones that cross the screen's edges.
bool DrawQuads(CSceneRenderer* renderer, int sprites) {
    g_indices.resize(static_cast<std::size_t>(sprites) * 6);
    std::uint16_t* tri = g_indices.data();
    for (int i = 0; i < sprites * 4; i += 4, tri += 6) {
        SetTriangle(tri, i, i + 2, i + 1);
        SetTriangle(tri + 3, i, i + 3, i + 2);
    }
    if (sprites == 0) return true;
    return Draw(renderer, sprites * 4, sprites * 6);
}
} // namespace

// One radix sort pass: the records of `src` go to 256 buckets by the key's byte at
// `shift` (each bucket a list through the records' link, the last one first), then the buckets go
// to `dst` in ascending order (`ascending` set) or descending.
void SpriteRadixPass(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                int ascending) {
    SpriteSortRecord* buckets[256] = {};
    for (int i = 0; i < count; ++i) {
        SpriteSortRecord* record = src[i];
        int bucket = (record->key >> shift) & 0xFF;
        record->next = buckets[bucket];
        buckets[bucket] = record;
    }
    for (int i = 0; i < 256; ++i) {
        for (SpriteSortRecord* r = buckets[ascending ? i : 255 - i]; r; r = r->next) *dst++ = r;
    }
}

// One merge sort pass over `count` record pointers: for each column j below `width`, the
// sorted runs src[j], src[j + 2w], ... and src[j + w], src[j + 3w], ... merge, larger key first
// (the first run's on ties), into dst[j], dst[j + w], ...; when 2w > count the last `width`
// pointers are first copied as they are (the columns without a second run).
void SpriteMergePass(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count) {
    int step = width * 2;
    if (step > count) {
        std::memcpy(dst + (count - width), src + (count - width), static_cast<std::size_t>(width) * sizeof(*src));
    }
    int columns = width < count - width ? width : count - width;
    for (int j = 0; j < columns; ++j) {
        int a = j;
        int b = j + width;
        int out = j;
        for (;;) {
            if (src[a]->key >= src[b]->key) {
                dst[out] = src[a];
                a += step;
                out += width;
                if (a >= count) {
                    for (; b < count; b += step, out += width) dst[out] = src[b];
                    break;
                }
            } else {
                dst[out] = src[b];
                b += step;
                out += width;
                if (b >= count) {
                    for (; a < count; a += step, out += width) dst[out] = src[a];
                    break;
                }
            }
        }
    }
}

// With `additive` the blend is source alpha + one for the call (render states 0x13 / 0x14 saved and restored).
// The sprites are projected with the top of the clip stack. Returns 1, or 0 when a draw fails (the blend is
// then left as it is).
int CSceneRenderer::DrawSprites(int additiveArgument, void* pointArray, void* spriteArray) {
    const char additive = static_cast<char>(additiveArgument);   // only its low byte counts
    const auto* points = static_cast<const std::vector<SpritePoint>*>(pointArray);
    const auto* sprites = static_cast<const std::vector<Sprite>*>(spriteArray);
    const CTransformPipeline* currentPipeline = pipeline;
    const Matrix4& clip = *currentPipeline->Top(CTransformPipeline::kClipStack);

    const SpritePoint* point = points->data();
    int remaining = std::ssize(*points);
    const Sprite* sprite = sprites->data();
    render::RenderState& state = render::g_renderState;
    const render::Blend savedSource = state.sourceBlend;
    const render::Blend savedDest = state.destBlend;
    if (additive) {
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::One;
    }

    constexpr int kBatch = 0x1FF;   // sprites
    while (remaining > 0) {
        const int count = remaining < kBatch ? remaining : kBatch;
        FillCorners(sprite, count);
        sprite += count;
        remaining -= count;
        currentPipeline->ProjectSprites(clip, point, g_corners.data(), count);
        point += count;
        if (!DrawQuads(this, SortBackToFront())) return 0;
    }

    if (additive) {
        state.sourceBlend = savedSource;
        state.destBlend = savedDest;
    }
    return 1;
}
