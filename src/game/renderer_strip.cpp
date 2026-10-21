// The renderer's ribbon draw (00501D90, virtual slot 16 of the renderer at 007AF060, vtable
// 0073D93C) with its helpers 00502A20 (indexed draw of TL vertices), 00502AC0 (one ribbon cross
// section) and 00502B90 (the direction at a point). A polyline of world points is projected (slot 13
// of the pipeline's transform object, or slot 8 with clipping flags), and every point becomes a
// pair of screen vertices offset across the line's screen direction by width * rhw * 15, with the
// texture's u running along the ribbon and v across it; consecutive pairs make two triangles. The
// ends are transparent (colour 0x00FFFFFF). Used for lightning bolts and similar strips. Buffers are
// function-local statics (007AF000..007AF05C). See docs/notes/models.md ("Renderer").
#include "clip.h"
#include "device.h"
#include "model.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render_state.h"
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "device_buffers.h"
#include "originals_render.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// --- The statics ---------------------------------------------------------------------------------

using devicebuffers::ProjectedVertex;

// The engine's aligned arrays (lib::AlignedArray; the original's inlined SetSize calls 004ECB70,
// 00468F70, 004ECB50 and 00406B30 for the element sizes, which AlignedArray::Allocate matches).
lib::AlignedArray<TLVertex>& Strip() { return ei::GameData<lib::AlignedArray<TLVertex>>(0x007AF000); }      // TL vertices of the points
lib::AlignedArray<TLVertex>& Ribbon() { return ei::GameData<lib::AlignedArray<TLVertex>>(0x007AF010); }     // TL vertices, two per point
lib::AlignedArray<ProjectedVertex>& Projected() { return ei::GameData<lib::AlignedArray<ProjectedVertex>>(0x007AF020); }
lib::AlignedArray<std::uint16_t>& Indices() { return ei::GameData<lib::AlignedArray<std::uint16_t>>(0x007AF030); }
lib::AlignedArray<std::uint8_t>& Clipped() { return ei::GameData<lib::AlignedArray<std::uint8_t>>(0x007AF040); }    // a flag byte per point
std::uint8_t& Guards() { return ei::GameData<std::uint8_t>(0x007AF050); }
// The texture coordinates of the next cross section: u (007AF05C) and the two v values.
float& CurrentU() { return ei::GameData<float>(0x007AF05C); }
float& TopV() { return ei::GameData<float>(0x007AF058); }
float& BottomV() { return ei::GameData<float>(0x007AF054); }

// The index array's SetAtGrow (00452FC0) and Add (00504E50).

// One function-local static array: constructed empty once (its guard bit), destroyed at exit.
template <typename T>
void EnsureStatic(std::uint8_t bit, lib::AlignedArray<T>& array, std::uintptr_t onExit) {
    if (Guards() & bit) return;
    array = lib::AlignedArray<T>{};
    Guards() |= bit;
    GameAtExit(reinterpret_cast<void(__cdecl*)()>(onExit));
}

void EnsureStatics() {
    EnsureStatic(1, Clipped(), 0x00502C80);
    EnsureStatic(2, Indices(), 0x00502C60);
    EnsureStatic(4, Projected(), 0x00502C40);
    EnsureStatic(8, Ribbon(), 0x00502C20);
    EnsureStatic(0x10, Strip(), 0x00502C00);
}

// --- Device calls --------------------------------------------------------------------------------

IDirect3DDevice7* Device(CSceneRenderer* renderer) { return device::D3D(renderer->device_); }
constexpr int kTriangleList = D3DPT_TRIANGLELIST;
constexpr int kFvfTlVertex = 0x1C4;   // D3DFVF_TLVERTEX

// --- Cross sections ------------------------------------------------------------------------------

// The screen width of a ribbon per unit of width * rhw (0073C518).
constexpr float kWidthScale = 15.0f;

// The texture coordinates of a new cross-section vertex: the current u and `v` (copied as bits).
void SetUv(TLVertex* vertex, const float& v) {
    std::memcpy(&vertex->u, &CurrentU(), sizeof(float));
    std::memcpy(&vertex->v, &v, sizeof(float));
}

// The first cross section of a ribbon, inlined by the original at the line's first point: the
// direction (dx, dy) to the next point rounded to floats, k = (width * rhw * 15, rounded) /
// hypot(dx, dy) unrounded; vertex 1 moved by (k*dy, -k*dx) as in EmitSection, vertex 2 by
// (-(float)(k*dy), k*dx) (its x offset rounded first).
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
    SetUv(top, TopV());
    TLVertex* bottom = *array + (*count)++;
    *bottom = *first;
    bottom->x = static_cast<float>(static_cast<double>(bottom->x) - offsetX);
    bottom->y = static_cast<float>(acrossY + bottom->y);
    SetUv(bottom, BottomV());
}

// The two triangles of the last two cross sections (count - 4 .. count - 1).
void AddQuad(int count, bool firstBySetAtGrow) {
    if (firstBySetAtGrow) {
        WordsSetAtGrow(&Indices(), nullptr, Indices().size, count - 4);
        WordsSetAtGrow(&Indices(), nullptr, Indices().size, count - 3);
    } else {
        Indices().Add(static_cast<unsigned short>(count - 4));
        Indices().Add(static_cast<unsigned short>(count - 3));
    }
    Indices().Add(static_cast<unsigned short>(count - 2));
    Indices().Add(static_cast<unsigned short>(count - 2));
    Indices().Add(static_cast<unsigned short>(count - 3));
    Indices().Add(static_cast<unsigned short>(count - 1));
}

TLVertex* StripAt(int i) { return Strip().data + i; }
TLVertex** RibbonData() { return &Ribbon().data; }

} // namespace

// 00502AC0. The cross section at `src` across `direction`: k = (width * rhw * 15, rounded) /
// hypot(dx, dy) unrounded; vertex 1 = src moved by (k*dy, -k*dx) with v at the top, vertex 2 moved
// by (-k*dy, k*dx) with v at the bottom, both with the current u. Appends both to `*array` at
// `*count`. __fastcall: src in ECX, direction in EDX.
void __fastcall EmitSection(const TLVertex* src, const float* direction, TLVertex** array, int* count, float width) {
    TLVertex* top = *array + (*count)++;
    const float scale = static_cast<float>(static_cast<double>(width) * src->rhw * kWidthScale);
    const double k = scale / libcrt::Hypot(direction[0], direction[1]);
    *top = *src;
    top->x = static_cast<float>(k * direction[1] + top->x);
    top->y = static_cast<float>(top->y - k * direction[0]);
    SetUv(top, TopV());
    TLVertex* bottom = *array + (*count)++;
    *bottom = *src;
    bottom->x = static_cast<float>(bottom->x - k * direction[1]);
    bottom->y = static_cast<float>(k * direction[0] + bottom->y);
    SetUv(bottom, BottomV());
}

// --- 00502B90 / 00502AC0 / 00502A20 ---------------------------------------------------------------

// 00502B90. The screen direction at point `index` of `points` (TL vertices): mode 1 the segment from
// the previous point, mode 2 from the previous to the next point (the last point: the segment from
// the previous one), else (1, 0). __fastcall: out in ECX, index in EDX. Returns out.
float* __fastcall StripDirection(float* out, int index, int mode, const void* array) {
    const auto* points = static_cast<const lib::AlignedArray<TLVertex>*>(array);
    const TLVertex* v = points->data;
    if (mode == 1 || (mode == 2 && !(index < points->size - 1))) {
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
EI_REPLACE(0x00502B90, StripDirection);
EI_REPLACE(0x00502AC0, EmitSection);

// 00502A20 (thiscall on the device holder, the renderer's device_). Draws an indexed triangle list
// of TL vertices under the device's critical section (007AEE78); with distanceFog_ set the
// transform object fogs the vertices first. Counts vertices, triangles and calls. Returns 1.
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
EI_REPLACE_METHOD(0x00502A20, CTextureDevice::DrawTlTriangles);

// 00501D90 (thiscall, RET 0x18). `points` is an array {data, count} of world points (16 bytes each:
// x, y, z and one more); `width` scales the ribbon's screen width; `mode` 0 projects, 1 projects and
// clips (points flagged by the transform object are skipped), 2 only sizes the buffers; u runs over
// `uLength` along the ribbon (0.875 at the start when it is 0), v from `vStart` over `vSize`.
// Returns the draw's result (1 when nothing is drawn).
int CSceneRenderer::DrawStripImpl(const int* points, float width, int mode, float uLength, float vStart, float vSize) {
    CTransformPipeline* pipeline = pipeline_;
    ITransformPipeline* transform = pipeline->transform_;
    EnsureStatics();

    int n = points[1];
    if (n == 0) {
        Projected().RemoveAll();
        Strip().RemoveAll();
    } else {
        Projected().SetSize(n);
        Strip().SetSize(n);
    }
    Ribbon().SetSize(n * 2 + 100);
    Clipped().SetSize(n);
    // The index array is sized for the ribbon and then freed at once; the indices are appended later.
    Indices().SetSize(Ribbon().size * 3 + 0x96);
    Indices().RemoveAll();

    if (mode == 2) return 1;
    if (points[1] < 2) return 1;

    CurrentU() = 0.0f;
    float step = static_cast<float>(uLength / static_cast<double>(points[1] - 1));
    float zero = 0.0f;
    EI_NAN_AUDIT(uLength);
    if (uLength == zero) CurrentU() = 0.875f;
    BottomV() = vStart + vSize;
    std::memcpy(&TopV(), &vStart, 4);

    int clipped = mode;
    if (mode == 0) {
        transform->ProjectPoints(pipeline->Top(1), reinterpret_cast<const void*>(points[0]), Projected().data,
                                 points[1]);
    } else {
        clipped = transform->ProjectPointsClipped(pipeline->Top(CTransformPipeline::kClipStack),
                                                  reinterpret_cast<const void*>(points[0]), Projected().data,
                                                  Clipped().data, points[1]);
        if (clipped == 2) return 1;
    }

    // The projected points as TL vertices: opaque white, black specular; the ends transparent.
    for (int i = 0; i < Projected().size; ++i) {
        TLVertex* v = StripAt(i);
        std::memcpy(v, &Projected().data[i], 16);
        v->color = 0xFFFFFFFF;
        v->specular = 0xFF000000;
    }
    StripAt(0)->color = 0x00FFFFFF;
    StripAt(Projected().size - 1)->color = 0x00FFFFFF;

    int count = 0;
    std::uint32_t widthBits;
    std::memcpy(&widthBits, &width, 4);
    if (clipped == 0) {
        bool started = false;
        for (int i = 1; i < Strip().size; ++i) {
            if (!started) EmitFirstSection(StripAt(i - 1), StripAt(i), RibbonData(), &count, widthBits);
            CurrentU() = CurrentU() + step;
            float direction[2];
            StripDirection(direction, i, i == Strip().size - 1 ? 1 : 2, &Strip());
            EmitSection(StripAt(i), direction, RibbonData(), &count, width);
            started = true;
            AddQuad(count, true);
        }
    } else {
        transform->ToScreenClipped(Strip().data, Strip().size);
        bool started = false;
        for (int i = 1; i < Strip().size; ++i) {
            const auto* flags = reinterpret_cast<const std::uint8_t*>(Clipped().data);
            if (flags[i - 1] && flags[i]) {
                CurrentU() = CurrentU() + step;
                started = false;
                continue;
            }
            if (!started) {
                float direction[2];
                StripDirection(direction, i, 1, &Strip());
                EmitSection(StripAt(i - 1), direction, RibbonData(), &count, width);
            }
            CurrentU() = CurrentU() + step;
            float direction[2];
            StripDirection(direction, i, i == Strip().size - 1 ? 1 : 2, &Strip());
            EmitSection(StripAt(i), direction, RibbonData(), &count, width);
            started = true;
            AddQuad(count, false);
        }
    }

    if (Indices().size == 0) return 1;
    IDirect3DDevice7* device = Device(this);
    DWORD savedSource = 0, savedDest = 0;
    device::GetRenderState(device, D3DRENDERSTATE_SRCBLEND, &savedSource);
    device::GetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, &savedDest);
    device::SetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    device::SetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    render::state::Render(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    render::state::Render(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    int result = device_->DrawTlTriangles(reinterpret_cast<TLVertex*>(Ribbon().data), Ribbon().size,
                                 reinterpret_cast<std::uint16_t*>(Indices().data), Indices().size);
    device::SetRenderState(Device(this), D3DRENDERSTATE_SRCBLEND, savedSource);
    device::SetRenderState(Device(this), D3DRENDERSTATE_DESTBLEND, savedDest);
    render::state::Render(D3DRENDERSTATE_SRCBLEND, static_cast<std::uint32_t>(savedSource));
    render::state::Render(D3DRENDERSTATE_DESTBLEND, static_cast<std::uint32_t>(savedDest));
    return result;
}
EI_REPLACE_METHOD(0x00501D90, CSceneRenderer::DrawStripImpl);

// 00504E50 (RET 4): the index array's Add (A-U10), registered on the template; returns the index.
EI_REPLACE_METHOD(0x00504E50, lib::AlignedArray<std::uint16_t>::Add);
