// The renderer's ribbon draw with its helpers (indexed draw of TL vertices, one ribbon cross section, the
// direction at a point).
// More: docs/notes/models.md, "Renderer".
#include "engine/scene/clip.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "render/render.h"
#include "render/render_state.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// --- The statics ---------------------------------------------------------------------------------

// The strip's work buffers.
std::vector<TLVertex> g_strip;          // TL vertices of the points
std::vector<TLVertex> g_ribbon;         // TL vertices, two per point
std::vector<Vector4> g_projected;
std::vector<std::uint16_t> g_indices;
std::vector<std::uint8_t> g_clipped;   // an outcode per point
// The texture coordinates of the next cross section: u and the two v values.
float g_bottomV = /*007AF054*/ {};
float g_topV = /*007AF058*/ {};
float g_currentU = /*007AF05C*/ {};

// --- Cross sections ------------------------------------------------------------------------------

// The screen width of a ribbon per unit of width * rhw.
constexpr float kWidthScale = 15.0f;

// The texture coordinates of a new cross-section vertex: the current u and `v` (copied as bits).
void SetUv(TLVertex* vertex, const float& v) {
    vertex->u = g_currentU;
    vertex->v = v;
}

// The first cross section of a ribbon, at the line's first point: the direction (dx, dy) to the next point,
// k = width * rhw * 15 / hypot(dx, dy); vertex 1 moved by (k*dy, -k*dx) as in EmitSection, vertex 2 by
// (-k*dy, k*dx).
void EmitFirstSection(const TLVertex* first, const TLVertex* next, TLVertex** array, int* count, std::uint32_t widthBits) {
    const float dx = next->x - first->x;
    const float dy = next->y - first->y;
    const float scale = std::bit_cast<float>(widthBits) * first->rhw * kWidthScale;
    const float k = scale / lib::Hypot(dx, dy);
    TLVertex* top = *array + (*count)++;
    *top = *first;
    const float acrossX = dy * k;
    const float offsetX = acrossX;
    top->x = acrossX + top->x;
    const float acrossY = k * dx;
    top->y -= acrossY;
    SetUv(top, g_topV);
    TLVertex* bottom = *array + (*count)++;
    *bottom = *first;
    bottom->x -= offsetX;
    bottom->y = acrossY + bottom->y;
    SetUv(bottom, g_bottomV);
}

// The two triangles of the last two cross sections (count - 4 .. count - 1).
void AddQuad(int count, bool firstBySetAtGrow) {
    if (firstBySetAtGrow) {
        g_indices.push_back(static_cast<unsigned short>(count - 4));
        g_indices.push_back(static_cast<unsigned short>(count - 3));
    } else {
        g_indices.push_back(static_cast<unsigned short>(count - 4));
        g_indices.push_back(static_cast<unsigned short>(count - 3));
    }
    g_indices.push_back(static_cast<unsigned short>(count - 2));
    g_indices.push_back(static_cast<unsigned short>(count - 2));
    g_indices.push_back(static_cast<unsigned short>(count - 3));
    g_indices.push_back(static_cast<unsigned short>(count - 1));
}

TLVertex* StripAt(int i) { return g_strip.data() + i; }

} // namespace

// The cross section at `src` across `direction`: k = width * rhw * 15 / hypot(dx, dy);
// vertex 1 = src moved by (k*dy, -k*dx) with v at the top, vertex 2 moved by (-k*dy, k*dx) with v at the
// bottom, both with the current u. Appends both to `*array` at `*count`.
void EmitSection(const TLVertex* src, const Vector2& direction, TLVertex** array, int* count, float width) {
    TLVertex* top = *array + (*count)++;
    const float scale = width * src->rhw * kWidthScale;
    const float k = scale / lib::Hypot(direction.x, direction.y);
    *top = *src;
    top->x = k * direction.y + top->x;
    top->y -= k * direction.x;
    SetUv(top, g_topV);
    TLVertex* bottom = *array + (*count)++;
    *bottom = *src;
    bottom->x -= k * direction.y;
    bottom->y = k * direction.x + bottom->y;
    SetUv(bottom, g_bottomV);
}


// The screen direction at point `index` of `points` (TL vertices): mode 1 the segment from
// the previous point, mode 2 from the previous to the next point (the last point: the segment from
// the previous one), else (1, 0). Returns out.
Vector2 StripDirection(int index, int mode, const void* array) {
    const auto* points = static_cast<const std::vector<TLVertex>*>(array);
    const TLVertex* v = points->data();
    if (mode == 1 || (mode == 2 && !(index < std::ssize(*points) - 1))) {
        return {v[index].x - v[index - 1].x, v[index].y - v[index - 1].y};
    }
    if (mode == 2) {
        return {v[index + 1].x - v[index - 1].x, v[index + 1].y - v[index - 1].y};
    }
    return {1.0f, 0.0f};
}

// Draws an indexed triangle list of TL vertices (the new renderer, src/render, takes it); with the distance fog set
// the pipeline fogs the vertices first. Counts vertices, triangles and calls. Returns 1.
int TextureDevice::DrawTlTriangles(TLVertex* vertices, int vertexCount, const std::uint16_t* indices,
                                    int indexCount) {
    if (fog == render::MeshFog::Distance) pipeline->FogVertices(vertices, vertexCount);
    render::TlDraw(this, vertices, render::kFvfTextured, vertexCount, indices, indexCount, render::TlPrimitive::TriangleList);
    return 1;
}

// `points` holds the world points (x, y, z and one more); `width` scales
// the ribbon's screen width; `mode` Inside projects, Crossing projects and clips (a section between two points that are
// both off screen is skipped), Outside only sizes the buffers; u runs over `uLength` along the ribbon (0.875 at the start when it
// is 0), v from `vStart` over `vSize`. Returns the draw's result (1 when nothing is drawn).
int SceneRenderer::DrawStrip(const std::vector<Vector3>& pointArray, float width, TransformPipeline::ClipResult mode, float uLength,
                              float vStart, float vSize) {
    const Vector3* points = pointArray.data();
    const int pointCount = static_cast<int>(std::ssize(pointArray));
    const TransformPipeline* currentPipeline = pipeline;

    const auto n = static_cast<std::size_t>(pointCount);
    g_projected.resize(n);
    g_strip.resize(n);
    g_ribbon.resize(n * 2 + 100);
    g_clipped.resize(n);
    // The indices are appended later; room for the ribbon's.
    g_indices.clear();
    g_indices.reserve(g_ribbon.size() * 3 + 0x96);

    if (mode == TransformPipeline::ClipResult::Outside) return 1;
    if (pointCount < 2) return 1;

    g_currentU = 0.0f;
    float step = uLength / (pointCount - 1);
    float zero = 0.0f;
    if (uLength == zero) g_currentU = 0.875f;
    g_bottomV = vStart + vSize;
    g_topV = vStart;

    TransformPipeline::ClipResult clipped = mode;
    if (mode == TransformPipeline::ClipResult::Inside) {
        TransformPipeline::ProjectPoints(*currentPipeline->Top(1), points, g_projected.data(), pointCount);
    } else {
        clipped = currentPipeline->ProjectPointsClipped(*currentPipeline->Top(TransformPipeline::kClipStack), points,
                                                 g_projected.data(), g_clipped.data(), pointCount);
        if (clipped == TransformPipeline::ClipResult::Outside) return 1;
    }

    // The projected points as TL vertices: opaque white, black specular; the ends transparent.
    for (int i = 0; i < std::ssize(g_projected); ++i) {
        TLVertex* v = StripAt(i);
        std::memcpy(v, &g_projected[i], 16);
        v->color = 0xFFFFFFFF;
        v->specular = 0xFF000000;
    }
    StripAt(0)->color = 0x00FFFFFF;
    StripAt(lib::Count(g_projected) - 1)->color = 0x00FFFFFF;

    int count = 0;
    TLVertex* ribbon = g_ribbon.data();
    std::uint32_t widthBits;
    widthBits = std::bit_cast<std::uint32_t>(width);
    if (clipped == TransformPipeline::ClipResult::Inside) {
        bool started = false;
        for (int i = 1; i < std::ssize(g_strip); ++i) {
            if (!started) EmitFirstSection(StripAt(i - 1), StripAt(i), &ribbon, &count, widthBits);
            g_currentU += step;
            Vector2 direction = StripDirection(i, i == std::ssize(g_strip) - 1 ? 1 : 2, &g_strip);
            EmitSection(StripAt(i), direction, &ribbon, &count, width);
            started = true;
            AddQuad(count, true);
        }
    } else {
        currentPipeline->ToScreen(g_strip.data(), lib::Count(g_strip));
        bool started = false;
        for (int i = 1; i < std::ssize(g_strip); ++i) {
            const std::uint8_t* clipFlags = g_clipped.data();
            if (clipFlags[i - 1] && clipFlags[i]) {
                g_currentU += step;
                started = false;
                continue;
            }
            if (!started) {
                Vector2 direction = StripDirection(i, 1, &g_strip);
                EmitSection(StripAt(i - 1), direction, &ribbon, &count, width);
            }
            g_currentU += step;
            Vector2 direction = StripDirection(i, i == std::ssize(g_strip) - 1 ? 1 : 2, &g_strip);
            EmitSection(StripAt(i), direction, &ribbon, &count, width);
            started = true;
            AddQuad(count, false);
        }
    }

    if (g_indices.empty()) return 1;
    render::RenderState& state = render::g_renderState;
    const render::Blend savedSource = state.sourceBlend;
    const render::Blend savedDest = state.destBlend;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::One;
    int result = device->DrawTlTriangles(g_ribbon.data(), lib::Count(g_ribbon),
                                 g_indices.data(), lib::Count(g_indices));
    state.sourceBlend = savedSource;
    state.destBlend = savedDest;
    return result;
}
