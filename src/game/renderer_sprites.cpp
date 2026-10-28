// The renderer's sprite draw with its helpers (indexed draw, a radix sort pass, a merge sort pass).
// More: docs/notes/comments-c22i.md, "renderer_sprites.cpp".
#include "clip.h"
#include "device.h"
#include "model.h"
#include "texture.h"

#include "render/render_state.h"
#include "render/render.h"
#include "ei/x87.h"
#include "device_buffers.h"
#include "lib/containers.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include "lib/vecmath.h"
#include "lib/static_object.h"
#include "game_singletons.h"

#include <cstdlib>
#include <vector>

// One record of the back-to-front sort (16 bytes).
struct SpriteSortRecord {
    std::int32_t key;             // the first corner's z, as bits (never negative here)
    SpriteSortRecord* next;       // the radix pass's bucket list
    TLVertex* corners;            // the sprite's four corners (in the copy)
    std::uint32_t outcodes;       // their four outcode bytes
};

void SpriteRadixPass(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                int ascending);
void SpriteMergePass(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count);

namespace {

// --- The arguments -------------------------------------------------------------------------------

struct SpritePoint {          // 16 bytes: the centre and the size
    float x, y, z, size;
};
struct Sprite {               // 8 bytes
    std::uint32_t color;      // ARGB
    const float* uv;          // the texture rectangle: u0, v0, u1, v1
};

// --- The statics ---------------------------------------------------------------------------------

// The sprite draw's work buffers: vectors of vertices, corner outcodes and indices.
using devicebuffers::ProjectedVertex;
constinit std::vector<ProjectedVertex> g_unused = /*007AEFA0*/ {};   // sized only
void FreeUnused() { lib::FreeStorage(g_unused); }
constinit std::vector<TLVertex> g_corners = /*007AEF90*/ {};   // 4 per sprite
void FreeCorners() { lib::FreeStorage(g_corners); }
// A CByteArray of a byte per corner and a CWordArray of indices: constructed on the first draw, destroyed by the exit
// functions it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<std::vector<std::uint8_t>> g_outcodes = /*007AEF78*/ {};
constinit lib::StaticObject<std::vector<std::uint16_t>> g_indices = /*007AEF60*/ {};
std::vector<std::uint8_t>& Outcodes() { return *g_outcodes; }
std::vector<std::uint16_t>& Indices() { return *g_indices; }
void DestroyOutcodes() { g_outcodes.Destroy(); }
void DestroyIndices() { g_indices.Destroy(); }
std::uint8_t g_guards = /*007AEFB0*/ {};   // the draw's statics constructed (bits 1, 2, 4, 8)
constinit std::vector<TLVertex> g_cornersCopy = /*007AEFE8*/ {};   // the records point here
void FreeCornersCopy() { lib::FreeStorage(g_cornersCopy); }
constinit std::vector<SpriteSortRecord> g_records = /*007AEFD8*/ {};
void FreeRecords() { lib::FreeStorage(g_records); }
constinit std::vector<SpriteSortRecord*> g_order = /*007AEFC8*/ {};
void FreeOrder() { lib::FreeStorage(g_order); }
constinit std::vector<SpriteSortRecord*> g_spare = /*007AEFB8*/ {};
void FreeSpare() { lib::FreeStorage(g_spare); }
std::uint8_t g_sortGuards = /*007AEFF8*/ {};   // the sort's statics constructed (bits 1, 2, 4, 8)

// First use: the two aligned arrays are zeroed, the MFC arrays constructed; each gets its exit handler.
void EnsureStatics() {
    if (!(g_guards & 1)) {
        g_guards |= 1;
        g_unused = std::vector<ProjectedVertex>{};
        std::atexit(FreeUnused);
    }
    if (!(g_guards & 2)) {
        g_guards |= 2;
        g_corners = std::vector<TLVertex>{};
        std::atexit(FreeCorners);
    }
    if (!(g_guards & 4)) {
        g_guards |= 4;
        g_outcodes.Construct();
        std::atexit(DestroyOutcodes);
    }
    if (!(g_guards & 8)) {
        g_guards |= 8;
        g_indices.Construct();
        std::atexit(DestroyIndices);
    }
}

// One of the sort's statics, constructed (zeroed) on the first batch.
template <typename T>
void EnsureSortStatic(std::uint8_t bit, std::vector<T>& array, void(* onExit)()) {
    if (g_sortGuards & bit) return;
    g_sortGuards |= bit;
    array = std::vector<T>{};
    std::atexit(onExit);
}
void EnsureSortStatics() {
    EnsureSortStatic(1, g_cornersCopy, FreeCornersCopy);
    EnsureSortStatic(2, g_records, FreeRecords);
    EnsureSortStatic(4, g_order, FreeOrder);
    EnsureSortStatic(8, g_spare, FreeSpare);
}

TLVertex* CornerData() { return g_corners.data(); }
std::uint16_t* IndexData() { return Indices().data(); }
// --- Device calls --------------------------------------------------------------------------------

IDirect3DDevice7* Device(CSceneRenderer* renderer) { return device::D3D(renderer->device_); }
constexpr int kTriangleList = D3DPT_TRIANGLELIST;
constexpr int kFvfTlVertex = 0x1C4;   // D3DFVF_TLVERTEX

// --- The steps -----------------------------------------------------------------------------------

// A float copied through the FPU (FLD / FSTP dword): exact, except that a signalling NaN comes out quiet.
std::uint32_t ThroughFpu(std::uint32_t bits) {
    bool nan = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu) != 0;
    return nan ? bits | 0x00400000u : bits;
}

// The top of the pipeline's matrix stack (data) / +0x28 (count), transposed. The lower
// triangle moves up as integers and the upper one down through the FPU.
void TransposedTop(CTransformPipeline* pipeline, std::uint32_t (&m)[16]) {
    const Matrix4* top = pipeline->Top(CTransformPipeline::kClipStack);
    std::memcpy(m, top, sizeof(m));
    for (int r = 0; r < 4; ++r) {
        for (int c = r + 1; c < 4; ++c) {
            std::uint32_t upper = m[r * 4 + c];
            m[r * 4 + c] = m[c * 4 + r];
            m[c * 4 + r] = ThroughFpu(upper);
        }
    }
}

void SetCorner(TLVertex& v, std::uint32_t color, const float* u, const float* t) {
    v.color = color;
    v.specular = 0xFF000000;
    std::memcpy(&v.u, u, 4);
    std::memcpy(&v.v, t, 4);
}

// The colour and texture coordinates of the next sprites' corners, at most 511 and at most
// `remaining`; corners (u0,v0), (u1,v0), (u1,v1), (u0,v1). Returns how many.
int FillCorners(const Sprite*& sprite, int& remaining) {
    TLVertex* v = CornerData();
    int count = 0;
    while (remaining > 0) {
        const float* uv = sprite->uv;
        std::uint32_t color = sprite->color;
        SetCorner(v[0], color, &uv[0], &uv[1]);
        SetCorner(v[1], color, &uv[2], &uv[1]);
        SetCorner(v[2], color, &uv[2], &uv[3]);
        SetCorner(v[3], color, &uv[0], &uv[3]);
        v += 4;
        ++count;
        --remaining;
        ++sprite;
        if (count >= 0x1FF) break;
    }
    return count;
}

// Sorts the batch back to front: a record per sprite whose first corner's z is not negative (its
// bits are the key), sorted largest first by four radix passes above 128 sprites, else by merge
// passes; the corners and outcodes are then written back in that order. Returns the sprites kept.
int SortBackToFront() {
    EnsureSortStatics();
    g_cornersCopy.resize(std::ssize(g_corners));
    std::memcpy(g_cornersCopy.data(), g_corners.data(), static_cast<std::size_t>(std::ssize(g_corners)) * sizeof(TLVertex));
    const auto* outcodes = reinterpret_cast<const std::uint32_t*>(Outcodes().data());   // four bytes per sprite
    TLVertex* corners = g_cornersCopy.data();
    int sprites = std::ssize(g_cornersCopy) / 4;
    g_records.resize(sprites);
    g_order.resize(sprites);
    int kept = 0;
    for (int i = 0; i < sprites; ++i, corners += 4) {
        SpriteSortRecord* record = g_records.data() + kept;
        std::memcpy(&record->key, &corners->z, 4);       // written for dropped sprites too
        if (record->key < 0) continue;
        record->corners = corners;
        record->outcodes = outcodes[i];
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
    TLVertex* out = CornerData();
    auto* codes = reinterpret_cast<std::uint32_t*>(Outcodes().data());
    for (int i = 0; i < kept; ++i) {
        std::memcpy(out + i * 4, sorted[i]->corners, 4 * sizeof(TLVertex));
        codes[i] = sorted[i]->outcodes;
    }
    return std::ssize(g_corners) / 4;
}

// The batch's triangle list through the device holder (the renderer's device_); with distanceFog_
// set the vertices are fogged by the transform object first. False when the draw fails.
bool Draw(CSceneRenderer* renderer, int vertexCount, int indexCount) {
    CTextureDevice* holder = renderer->device_;
    TLVertex* vertices = CornerData();
    std::uint16_t* indices = IndexData();
    if (holder->distanceFog_) holder->pipeline_->transform_->FogVertices(vertices, vertexCount);
    return holder->DrawIndexedDirect(kTriangleList, kFvfTlVertex, vertices, vertexCount, indices, indexCount, 0) != 0;
}

void SetTriangle(std::uint16_t* tri, int a, int b, int c) {
    tri[0] = static_cast<std::uint16_t>(a);
    tri[1] = static_cast<std::uint16_t>(b);
    tri[2] = static_cast<std::uint16_t>(c);
}

// All corners on screen: two triangles per sprite, (0, 2, 1) and (0, 3, 2).
bool DrawQuads(CSceneRenderer* renderer, int sprites) {
    std::uint16_t* tri = IndexData();
    for (int i = 0; i < sprites * 4; i += 4, tri += 6) {
        SetTriangle(tri, i, i + 2, i + 1);
        SetTriangle(tri + 3, i, i + 3, i + 2);
    }
    if (sprites == 0) return true;
    return Draw(renderer, sprites * 4, sprites * 6);
}

// The render target's size (the holder's render target's width / height) while there is a primary
// surface; both are kept in stack slots that are never initialised, so without a
// surface the previous batch's values (here 0 at first) are used.
struct ScreenSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Some corners off screen: each triangle goes through the clipper, which may append vertices.
bool DrawClipped(CSceneRenderer* renderer, int sprites, ScreenSize& screen) {
    CTextureDevice* holder = renderer->device_;
    if (holder->primary_) {
        CDXSurface* target = holder->renderTarget_;
        screen.width = static_cast<std::uint32_t>(target->width);
        screen.height = static_cast<std::uint32_t>(target->height);
    }
    float xMax = static_cast<float>(x87::FromUnsigned(screen.width));
    float yMax = static_cast<float>(x87::FromUnsigned(screen.height));
    // The clipper appends its new vertices after the corners (up to two per plane, six planes, two triangles per
    // sprite): the vector holds them (the original wrote them past its end, into its spare capacity).
    constexpr int kClipAddedPerSprite = 2 * 6 * 2;
    g_corners.resize(static_cast<std::size_t>(sprites) * (4 + kClipAddedPerSprite));
    TLVertex* vertices = CornerData();
    const auto* outcodes = Outcodes().data();
    std::uint16_t* tri = IndexData();
    int vertexCount = sprites * 4;
    int indexCount = 0;
    for (int i = 0; i < sprites * 4; i += 4) {
        int added;
        SetTriangle(tri, i, i + 2, i + 1);
        clip::ClipTriangle(vertices, outcodes, tri, &vertexCount, &added, xMax, yMax);
        indexCount += added;
        tri += added;
        SetTriangle(tri, i, i + 3, i + 2);
        clip::ClipTriangle(vertices, outcodes, tri, &vertexCount, &added, xMax, yMax);
        indexCount += added;
        tri += added;
    }
    if (indexCount <= 0) return true;
    return Draw(renderer, vertexCount, indexCount);
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

// DrawIndexedPrimitive under the device's
// critical section; the result goes to lastLoadResult_; counts vertices, triangles
// (indices / 3) and calls. Returns 1.
int CTextureDevice::DrawIndexedDirect(int primitive, int fvf, void* vertices, int vertexCount,
                           std::uint16_t* indices, int indexCount, int flags) {
    // The new renderer (src/render) takes the draw; with the old path's objects off it is skipped.
    if (render::TlDraw(this, vertices, fvf, vertexCount, indices, indexCount, primitive)) {
        gameapi::EnterCriticalSection(TheDeviceLock());
        lastLoadResult_ = device::DrawIndexedPrimitive(device::D3D(this), 
            static_cast<D3DPRIMITIVETYPE>(primitive), static_cast<DWORD>(fvf), vertices, static_cast<DWORD>(vertexCount),
            indices, static_cast<DWORD>(indexCount), static_cast<DWORD>(flags));
        gameapi::LeaveCriticalSection(TheDeviceLock());
    }
    vertexCount_ += vertexCount;
    primitiveCount_ += static_cast<int>(static_cast<std::uint32_t>(indexCount) / 3);
    drawCount_ += 1;
    return 1;
}

// With `additive` the blend is source alpha + one for the call (render states 0x13 / 0x14 saved and restored).
// The view matrix is the top of the clip stack, transposed. Returns 1, or 0 when a draw fails (the blend is
// then left as it is).
int CSceneRenderer::DrawSprites(int additiveArgument, void* pointArray, void* spriteArray) {
    const char additive = static_cast<char>(additiveArgument);   // only its low byte counts
    const auto* points = static_cast<const std::vector<SpritePoint>*>(pointArray);
    const auto* sprites = static_cast<const std::vector<Sprite>*>(spriteArray);
    CTransformPipeline* pipeline = pipeline_;
    ITransformPipeline* transform = pipeline->transform_;
    EnsureStatics();
    g_unused.resize(0x200);
    g_corners.resize(0x2000);
    Outcodes().resize(0x800);
    Indices().resize(0x3000);
    std::uint32_t view[16];
    TransposedTop(pipeline, view);

    const SpritePoint* point = points->data();
    int remaining = std::ssize(*points);
    const Sprite* sprite = sprites->data();
    DWORD savedSource = 0;
    DWORD savedDest = 0;
    if (additive) {
        device::GetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, &savedSource);
        device::GetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, &savedDest);
        device::SetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        device::SetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
        render::state::Render(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        render::state::Render(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    }

    ScreenSize screen;
    while (remaining > 0) {
        int count = FillCorners(sprite, remaining);
        g_corners.resize(count * 4);
        // 0: all corners on screen, 1: some off screen, else nothing to draw.
        int visibility = transform->ProjectSprites(view, point, g_corners.data(), Outcodes().data(), count);
        point += count;
        int kept = SortBackToFront();
        if (visibility == 1) {
            if (!DrawClipped(this, kept, screen)) return 0;
        } else if (visibility == 0) {
            if (!DrawQuads(this, kept)) return 0;
        }
    }

    if (additive) {
        device::SetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, savedSource);
        device::SetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, savedDest);
        render::state::Render(D3DRENDERSTATE_SRCBLEND, static_cast<std::uint32_t>(savedSource));
        render::state::Render(D3DRENDERSTATE_DESTBLEND, static_cast<std::uint32_t>(savedDest));
    }
    return 1;
}
