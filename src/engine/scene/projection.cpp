// The transform pipeline's projections: points to clip space with their outcodes and the batch's verdict, clip space
// to the screen, screen-facing sprites, the distance fog of screen vertices, and the flat projection and bounds
// fitting of shadows and figure parts. Plain C++ on the pipeline's matrices; 3DfpFPU.dll's transform object did
// this in the original (docs/notes/renderer.md, section 10).
#include "engine/scene/clip.h"          // TLVertex, ClipCodeOf, ClipVerdict

#include "engine/core/core_maths.h"
#include "engine/core/core_transform.h"
#include "engine/models/figure_mesh.h"   // VertexPacket
#include "engine/scene/pipeline.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "render/render_state.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// x, y and z divided by w, and 1 / w.
Vector4 Divided(const Vector4& p) {
    const float rhw = 1.0f / p.w;
    return {p.x * rhw, p.y * rhw, p.z * rhw, rhw};
}

// The points `pointAt(0 .. count - 1)` to clip space with `m` into `projected`, an outcode each, all to the screen
// when none is outside any plane. Returns the verdict.
template <typename PointAt>
int ProjectToClip(const TransformPipeline& pipeline, const Matrix4& m, PointAt pointAt, Vector4* projected,
                  std::uint8_t* outcodes, int count) {
    ClipVerdict verdict;
    for (int i = 0; i < count; ++i) {
        const Vector3 p = pointAt(i);
        const Vector4 clip = ToClip(m, p);
        projected[i] = clip;
        const std::uint32_t code = ClipCodeOf(clip.x, clip.y, clip.z, clip.w);
        outcodes[i] = static_cast<std::uint8_t>(code);
        verdict.Add(code);
    }
    const int result = verdict.Result();
    if (result == 0)
        for (int i = 0; i < count; ++i) projected[i] = pipeline.ToScreen(projected[i]);
    return result;
}

// A corner's screen position, depth and 1 / w.
void SetPosition(TLVertex& v, float x, float y, float z, float rhw) {
    v.x = x;
    v.y = y;
    v.z = z;
    v.rhw = rhw;
}

}   // namespace

Vector4 ToClip(const Matrix4& m, const Vector3& p) {
    const Vector3 c = TransformColumn(m, p);
    return {c.x, c.y, c.z, TransformColumnW(m, p)};
}

// The packets from the last to the first (which of two equal extremes stays, +0 or -0, follows from the order).
void ClipBounds::Fit(const VertexPacket* packets, int blocks) {
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int b = blocks - 1; b >= 0; --b)
        for (int lane = 0; lane < 4; ++lane) {
            const Vector3 p = render::PacketVertex(packets, b * 4 + lane);
            high = MaxKeepB(high, p);
            low = MinKeepA(p, low);
        }
    center = (high + low) * 0.5f;
    boxMax = high - center;
    boxMin = low - center;
    radius = Length(boxMax);
    flags |= static_cast<std::uint32_t>(BoundsFlag::Sphere | BoundsFlag::Box);
}

// ---- TransformPipeline -----------------------------------------------------------------------------

void TransformPipeline::SetViewportSize(float width, float height) {
    halfWidth = width * 0.5f;
    halfHeight = height * 0.5f;
}

void TransformPipeline::Project(const Matrix4& m, const VertexPacket* positions, Vector4* projected, int blocks) {
    for (int i = 0; i < blocks * 4; ++i) {
        const Vector3 p = render::PacketVertex(positions, i);
        const Vector3 transformed = TransformColumn(m, p);
        projected[i] = Divided({transformed.x, transformed.y, transformed.z, TransformColumnW(m, p)});
    }
}

TransformPipeline::ClipResult TransformPipeline::ProjectClipped(const Matrix4& m, const VertexPacket* positions, Vector4* projected,
                                       std::uint8_t* outcodes, int blocks) const {
    return static_cast<ClipResult>(ProjectToClip(
        *this, m, [positions](int i) { return render::PacketVertex(positions, i); }, projected, outcodes, blocks * 4));
}

void TransformPipeline::ProjectPoints(const Matrix4& m, const Vector3* points, Vector4* projected, int count) {
    for (int i = 0; i < count; ++i) projected[i] = Divided(ToClip(m, points[i]));
}

TransformPipeline::ClipResult TransformPipeline::ProjectPointsClipped(const Matrix4& m, const Vector3* points, Vector4* projected,
                                             std::uint8_t* outcodes, int count) const {
    return static_cast<ClipResult>(ProjectToClip(
        *this, m, [points](int i) { return points[i]; }, projected, outcodes,
        count));
}

// A sprite's corners lie `size` / w times the viewport's half width to either side of its centre on the screen (the
// same number of pixels both ways), all at the centre's depth and 1 / w.
void TransformPipeline::ProjectSprites(const Matrix4& m, const SpritePoint* points, TLVertex* corners,
                                        int count) const {
    for (int i = 0; i < count; ++i) {
        const SpritePoint& point = points[i];
        const Vector4 centre = ToScreen(ToClip(m, point.position));
        const float size = halfWidth * (point.size * centre.w);
        const float left = centre.x - size;
        const float right = centre.x + size;
        const float top = centre.y - size;
        const float bottom = size + centre.y;
        TLVertex* corner = corners + i * 4;
        SetPosition(corner[0], left, top, centre.z, centre.w);
        SetPosition(corner[1], right, top, centre.z, centre.w);
        SetPosition(corner[2], right, bottom, centre.z, centre.w);
        SetPosition(corner[3], left, bottom, centre.z, centre.w);
    }
}

Vector4 TransformPipeline::ToScreen(const Vector4& p) const {
    const float rhw = 1.0f / p.w;
    return {(p.x * rhw + 1.0f) * halfWidth, (1.0f - p.y * rhw) * halfHeight, p.z * rhw, rhw};
}

void TransformPipeline::ToScreen(TLVertex* vertices, int count) const {
    for (int i = 0; i < count; ++i) {
        TLVertex& v = vertices[i];
        const Vector4 screen = ToScreen(Vector4{v.x, v.y, v.z, v.rhw});
        SetPosition(v, screen.x, screen.y, screen.z, screen.w);
    }
}

void TransformPipeline::FogVertices(TLVertex* vertices, int count) const {
    const render::DistanceFog& fog = render::g_renderState.distanceFog;
    if (!(fog.density > 0.0f)) return;
    for (int i = 0; i < count; ++i) {
        TLVertex& v = vertices[i];
        const float kept = fog.Kept(fog.depthPerW / v.rhw) * 255.0f;
        const std::uint32_t lowered = static_cast<std::uint32_t>(lib::RoundEven(kept)) << 24;
        const std::uint32_t old = v.specular & 0xFF000000u;
        v.specular = (v.specular & 0x00FFFFFFu) | (lowered < old ? lowered : old);
    }
}

// --- Matrix math -------------------------------------------------------------------------------------

// The transform pipeline's matrix push. See pipeline.h and docs/notes/script.md ("Leftovers").

void TransformPipeline::PushMatrix(const float* m, int index) {
    MatrixStack& stack = stacks[index];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices.data(), m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    stack.matrices[stack.count - 1].Multiply(stack.matrices[stack.count - 2].m, m);
}

// --- View matrices -----------------------------------------------------------------------------------

// The view matrix: the camera's rotated axes as rows, each with its offset from the camera (pipeline.h).

namespace view {

Matrix4 ViewMatrix(const Vector3& position, const Quaternion& q) {
    Matrix4 result = {};
    const Vector3 axes[3] = {q.AxisX(), q.AxisY(), q.AxisZ()};
    for (int row = 0; row < 3; ++row) {
        float* out = result.m + row * 4;
        out[0] = axes[row].x;
        out[1] = axes[row].y;
        out[2] = axes[row].z;
        out[3] = -Dot(position, axes[row]);
    }
    result.m[15] = 1.0f;
    return result;
}

} // namespace view
