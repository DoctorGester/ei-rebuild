// The renderer's ribbon draw with its helpers (indexed draw of TL vertices, one ribbon cross section, the
// direction at a point).
// More: docs/notes/comments-c22i.md, "renderer_strip.cpp".
#include "clip.h"
#include "device.h"
#include "model.h"
#include "lightning.h"   // LightningPoint
#include "texture.h"

#include "lib/containers.h"
#include "lib/gameapi.h"
#include "render/render_state.h"
#include "render/render.h"
#include "device_buffers.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// --- The statics ---------------------------------------------------------------------------------

using devicebuffers::ProjectedVertex;

// The strip's work buffers.
constinit std::vector<TLVertex> g_strip = /*007AF000*/ {};
std::vector<TLVertex>& Strip() { return g_strip; }      // TL vertices of the points
void FreeStrip() { lib::FreeStorage(g_strip); }
constinit std::vector<TLVertex> g_ribbon = /*007AF010*/ {};
std::vector<TLVertex>& Ribbon() { return g_ribbon; }     // TL vertices, two per point
void FreeRibbon() { lib::FreeStorage(g_ribbon); }
constinit std::vector<ProjectedVertex> g_projected = /*007AF020*/ {};
std::vector<ProjectedVertex>& Projected() { return g_projected; }
void FreeProjected() { lib::FreeStorage(g_projected); }
constinit std::vector<std::uint16_t> g_indices = /*007AF030*/ {};
std::vector<std::uint16_t>& Indices() { return g_indices; }
void FreeIndices() { lib::FreeStorage(g_indices); }
constinit std::vector<std::uint8_t> g_clipped = /*007AF040*/ {};
std::vector<std::uint8_t>& Clipped() { return g_clipped; }    // a flag byte per point
void FreeClipped() { lib::FreeStorage(g_clipped); }
std::uint8_t g_guards = /*007AF050*/ {};   // the function-local arrays constructed
// The texture coordinates of the next cross section: u and the two v values.
float g_bottomV = /*007AF054*/ {};
float g_topV = /*007AF058*/ {};
float g_currentU = /*007AF05C*/ {};

// One function-local static array: constructed empty once (its guard bit), destroyed at exit.
template <typename T>
void EnsureStatic(std::uint8_t bit, std::vector<T>& array, void(__cdecl* onExit)()) {
    if (g_guards & bit) return;
    array = std::vector<T>{};
    g_guards |= bit;
    libcrt::AtExit(onExit);
}

void EnsureStatics() {
    EnsureStatic(1, Clipped(), FreeClipped);
    EnsureStatic(2, Indices(), FreeIndices);
    EnsureStatic(4, Projected(), FreeProjected);
    EnsureStatic(8, Ribbon(), FreeRibbon);
    EnsureStatic(0x10, Strip(), FreeStrip);
}

// --- Device calls --------------------------------------------------------------------------------

IDirect3DDevice7* Device(CSceneRenderer* renderer) { return device::D3D(renderer->device_); }
constexpr int kTriangleList = D3DPT_TRIANGLELIST;
constexpr int kFvfTlVertex = 0x1C4;   // D3DFVF_TLVERTEX

// --- Cross sections ------------------------------------------------------------------------------

// The screen width of a ribbon per unit of width * rhw.
constexpr float kWidthScale = 15.0f;

// The texture coordinates of a new cross-section vertex: the current u and `v` (copied as bits).
void SetUv(TLVertex* vertex, const float& v) {
    std::memcpy(&vertex->u, &g_currentU, sizeof(float));
    std::memcpy(&vertex->v, &v, sizeof(float));
}

// The first cross section of a ribbon, at the line's first point: the direction (dx, dy) to the next point
// rounded to floats, k = (width * rhw * 15, rounded) / hypot(dx, dy) unrounded; vertex 1 moved by (k*dy,
// -k*dx) as in EmitSection, vertex 2 by (-(float)(k*dy), k*dx) (its x offset rounded first).
void EmitFirstSection(const TLVertex* first, const TLVertex* next, TLVertex** array, int* count, std::uint32_t widthBits) {
    const float dx = static_cast<float>(static_cast<double>(next->x) - first->x);
    const float dy = static_cast<float>(static_cast<double>(next->y) - first->y);
    const float scale = static_cast<float>(static_cast<double>(std::bit_cast<float>(widthBits)) * first->rhw * kWidthScale);
    const double k = scale / libcrt::Hypot(dx, dy);
    TLVertex* top = *array + (*count)++;
    *top = *first;
    const double acrossX = dy * k;
    const float offsetX = static_cast<float>(acrossX);
    top->x = static_cast<float>(acrossX + top->x);
    const double acrossY = k * dx;
    top->y = static_cast<float>(top->y - acrossY);
    SetUv(top, g_topV);
    TLVertex* bottom = *array + (*count)++;
    *bottom = *first;
    bottom->x = static_cast<float>(static_cast<double>(bottom->x) - offsetX);
    bottom->y = static_cast<float>(acrossY + bottom->y);
    SetUv(bottom, g_bottomV);
}

// The two triangles of the last two cross sections (count - 4 .. count - 1).
void AddQuad(int count, bool firstBySetAtGrow) {
    if (firstBySetAtGrow) {
        Indices().push_back(static_cast<unsigned short>(count - 4));
        Indices().push_back(static_cast<unsigned short>(count - 3));
    } else {
        Indices().push_back(static_cast<unsigned short>(count - 4));
        Indices().push_back(static_cast<unsigned short>(count - 3));
    }
    Indices().push_back(static_cast<unsigned short>(count - 2));
    Indices().push_back(static_cast<unsigned short>(count - 2));
    Indices().push_back(static_cast<unsigned short>(count - 3));
    Indices().push_back(static_cast<unsigned short>(count - 1));
}

TLVertex* StripAt(int i) { return Strip().data() + i; }

} // namespace

// The cross section at `src` across `direction`: k = (width * rhw * 15, rounded) / hypot(dx, dy) unrounded;
// vertex 1 = src moved by (k*dy, -k*dx) with v at the top, vertex 2 moved by (-k*dy, k*dx) with v at the
// bottom, both with the current u. Appends both to `*array` at `*count`.
void EmitSection(const TLVertex* src, const float* direction, TLVertex** array, int* count, float width) {
    TLVertex* top = *array + (*count)++;
    const float scale = static_cast<float>(static_cast<double>(width) * src->rhw * kWidthScale);
    const double k = scale / libcrt::Hypot(direction[0], direction[1]);
    *top = *src;
    top->x = static_cast<float>(k * direction[1] + top->x);
    top->y = static_cast<float>(top->y - k * direction[0]);
    SetUv(top, g_topV);
    TLVertex* bottom = *array + (*count)++;
    *bottom = *src;
    bottom->x = static_cast<float>(bottom->x - k * direction[1]);
    bottom->y = static_cast<float>(k * direction[0] + bottom->y);
    SetUv(bottom, g_bottomV);
}


// The screen direction at point `index` of `points` (TL vertices): mode 1 the segment from
// the previous point, mode 2 from the previous to the next point (the last point: the segment from
// the previous one), else (1, 0). Returns out.
float* StripDirection(float* out, int index, int mode, const void* array) {
    const auto* points = static_cast<const std::vector<TLVertex>*>(array);
    const TLVertex* v = points->data();
    if (mode == 1 || (mode == 2 && !(index < std::ssize(*points) - 1))) {
        out[0] = v[index].x - v[index - 1].x;
        out[1] = v[index].y - v[index - 1].y;
    } else if (mode == 2) {
        out[0] = v[index + 1].x - v[index - 1].x;
        out[1] = v[index + 1].y - v[index - 1].y;
    } else {
        out[0] = 1.0f;
        out[1] = 0.0f;
    }
    return out;
}

// Draws an indexed triangle list of TL vertices under the device's critical section; with distanceFog_ set
// the transform object fogs the vertices first. Counts vertices, triangles and calls. Returns 1.
int CTextureDevice::DrawTlTriangles(TLVertex* vertices, int vertexCount,
                               std::uint16_t* indices, int indexCount) {
    if (distanceFog_) pipeline_->transform_->FogVertices(vertices, vertexCount);
    // The new renderer (src/render) takes the draw (after the fog above); with the old path's
    // objects off it is skipped.
    if (render::TlDraw(this, vertices, kFvfTlVertex, vertexCount, indices, indexCount, kTriangleList)) {
        gameapi::EnterCriticalSection(TheDeviceLock());
        lastLoadResult_ = device::DrawIndexedPrimitive(device::D3D(this), 
            D3DPT_TRIANGLELIST, kFvfTlVertex, vertices, static_cast<DWORD>(vertexCount), indices,
            static_cast<DWORD>(indexCount), 0);
        gameapi::LeaveCriticalSection(TheDeviceLock());
    }
    vertexCount_ += vertexCount;
    primitiveCount_ += static_cast<int>(static_cast<std::uint32_t>(indexCount) / 3);
    drawCount_ += 1;
    return 1;
}

// `points` holds the world points (x, y, z and one more); `width` scales
// the ribbon's screen width; `mode` 0 projects, 1 projects and clips (points flagged by the transform object
// are skipped), 2 only sizes the buffers; u runs over `uLength` along the ribbon (0.875 at the start when it
// is 0), v from `vStart` over `vSize`. Returns the draw's result (1 when nothing is drawn).
int CSceneRenderer::DrawStrip(const std::vector<LightningPoint>* pointArray, float width, int mode, float uLength,
                              float vStart, float vSize) {
    const LightningPoint* points = pointArray->data();
    const int pointCount = static_cast<int>(std::ssize(*pointArray));
    CTransformPipeline* pipeline = pipeline_;
    ITransformPipeline* transform = pipeline->transform_;
    EnsureStatics();

    const auto n = static_cast<std::size_t>(pointCount);
    Projected().resize(n);
    Strip().resize(n);
    Ribbon().resize(n * 2 + 100);
    Clipped().resize(n);
    // The indices are appended later; room for the ribbon's.
    Indices().clear();
    Indices().reserve(Ribbon().size() * 3 + 0x96);

    if (mode == 2) return 1;
    if (pointCount < 2) return 1;

    g_currentU = 0.0f;
    float step = static_cast<float>(uLength / static_cast<double>(pointCount - 1));
    float zero = 0.0f;
    EI_NAN_AUDIT(uLength);
    if (uLength == zero) g_currentU = 0.875f;
    g_bottomV = vStart + vSize;
    std::memcpy(&g_topV, &vStart, 4);

    int clipped = mode;
    if (mode == 0) {
        transform->ProjectPoints(pipeline->Top(1), points, Projected().data(), pointCount);
    } else {
        clipped = transform->ProjectPointsClipped(pipeline->Top(CTransformPipeline::kClipStack), points,
                                                  Projected().data(), Clipped().data(), pointCount);
        if (clipped == 2) return 1;
    }

    // The projected points as TL vertices: opaque white, black specular; the ends transparent.
    for (int i = 0; i < std::ssize(Projected()); ++i) {
        TLVertex* v = StripAt(i);
        std::memcpy(v, &Projected()[i], 16);
        v->color = 0xFFFFFFFF;
        v->specular = 0xFF000000;
    }
    StripAt(0)->color = 0x00FFFFFF;
    StripAt(std::ssize(Projected()) - 1)->color = 0x00FFFFFF;

    int count = 0;
    TLVertex* ribbon = Ribbon().data();
    std::uint32_t widthBits;
    std::memcpy(&widthBits, &width, 4);
    if (clipped == 0) {
        bool started = false;
        for (int i = 1; i < std::ssize(Strip()); ++i) {
            if (!started) EmitFirstSection(StripAt(i - 1), StripAt(i), &ribbon, &count, widthBits);
            g_currentU = g_currentU + step;
            float direction[2];
            StripDirection(direction, i, i == std::ssize(Strip()) - 1 ? 1 : 2, &Strip());
            EmitSection(StripAt(i), direction, &ribbon, &count, width);
            started = true;
            AddQuad(count, true);
        }
    } else {
        transform->ToScreenClipped(Strip().data(), std::ssize(Strip()));
        bool started = false;
        for (int i = 1; i < std::ssize(Strip()); ++i) {
            const std::uint8_t* flags = Clipped().data();
            if (flags[i - 1] && flags[i]) {
                g_currentU = g_currentU + step;
                started = false;
                continue;
            }
            if (!started) {
                float direction[2];
                StripDirection(direction, i, 1, &Strip());
                EmitSection(StripAt(i - 1), direction, &ribbon, &count, width);
            }
            g_currentU = g_currentU + step;
            float direction[2];
            StripDirection(direction, i, i == std::ssize(Strip()) - 1 ? 1 : 2, &Strip());
            EmitSection(StripAt(i), direction, &ribbon, &count, width);
            started = true;
            AddQuad(count, false);
        }
    }

    if (Indices().empty()) return 1;
    IDirect3DDevice7* device = Device(this);
    DWORD savedSource = 0, savedDest = 0;
    device::GetRenderState(device, D3DRENDERSTATE_SRCBLEND, &savedSource);
    device::GetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, &savedDest);
    device::SetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    device::SetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    render::state::Render(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    render::state::Render(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    int result = device_->DrawTlTriangles(Ribbon().data(), std::ssize(Ribbon()),
                                 Indices().data(), std::ssize(Indices()));
    device::SetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, savedSource);
    device::SetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, savedDest);
    render::state::Render(D3DRENDERSTATE_SRCBLEND, static_cast<std::uint32_t>(savedSource));
    render::state::Render(D3DRENDERSTATE_DESTBLEND, static_cast<std::uint32_t>(savedDest));
    return result;
}
