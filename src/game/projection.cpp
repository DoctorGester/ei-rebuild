// The transform pipeline's projections: points to clip space with their outcodes and the batch's verdict, clip space
// to the screen, screen-facing sprites, the distance fog of screen vertices, and the flat projection and bounds
// fitting of shadows and figure parts. Plain C++ on the pipeline's matrices; 3DfpFPU.dll's transform object did
// this in the original (docs/notes/renderer.md, section 10).
#include "clip.h"          // TLVertex, ClipCodeOf, ClipVerdict
#include "figure_mesh.h"   // VertexPacket
#include "lightning.h"     // LightningPoint
#include "model.h"

#include "lib/float_select.h"

#include "ei/x87.h"

#include <cfloat>
#include <cstdint>

namespace {

// x, y and z divided by w, and 1 / w.
Vector4 Divided(const Vector4& p) {
    const float rhw = 1.0f / p.w;
    return {p.x * rhw, p.y * rhw, p.z * rhw, rhw};
}

// The points `pointAt(0 .. count - 1)` to clip space with `m` into `projected`, an outcode each, all to the screen
// when none is outside any plane. Returns the verdict.
template <typename PointAt>
int ProjectToClip(const CTransformPipeline& pipeline, const Matrix4& m, PointAt pointAt, Vector4* projected,
                  std::uint8_t* outcodes, int count) {
    ClipVerdict verdict;
    for (int i = 0; i < count; ++i) {
        const Vector3 p = pointAt(i);
        const Vector4 clip = ToClip(m, p.x, p.y, p.z);
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

// Point i of packets of four.
Vector3 PacketPoint(const VertexPacket* packets, int i) {
    const VertexPacket& packet = packets[i >> 2];
    const int lane = i & 3;
    return {packet.block[0][lane], packet.block[1][lane], packet.block[2][lane]};
}

// A corner's screen position, depth and 1 / w.
void SetPosition(TLVertex& v, float x, float y, float z, float rhw) {
    v.x = x;
    v.y = y;
    v.z = z;
    v.rhw = rhw;
}

}   // namespace

Vector4 ToClip(const Matrix4& m, float x, float y, float z) {
    const float* r = m.m;
    return {((x * r[0] + r[3]) + y * r[1]) + z * r[2], ((x * r[4] + r[7]) + y * r[5]) + z * r[6],
            ((x * r[8] + r[11]) + y * r[9]) + z * r[10], ((x * r[12] + r[15]) + y * r[13]) + z * r[14]};
}

void ProjectFlat(const Matrix4& m, const VertexPacket* positions, Vector2* points, int blocks, float* bounds) {
    for (int i = 0; i < blocks * 4; ++i) {
        const Vector3 transformed = TransformColumn<Sum3::XYZ>(m, PacketPoint(positions, i));
        const Vector2 flat = {transformed.x, transformed.y};
        points[i] = flat;
        bounds[0] = MinKeepA(bounds[0], flat.x);
        bounds[2] = MaxKeepB(flat.x, bounds[2]);
        bounds[1] = MinKeepA(bounds[1], flat.y);
        bounds[3] = MaxKeepB(flat.y, bounds[3]);
    }
}

// The packets from the last to the first (which of two equal extremes stays, +0 or -0, follows from the order).
void ClipBounds::Fit(const VertexPacket* packets, int blocks) {
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int b = blocks - 1; b >= 0; --b)
        for (int lane = 0; lane < 4; ++lane) {
            const Vector3 p = PacketPoint(packets, b * 4 + lane);
            high.x = MaxKeepB(high.x, p.x);
            low.x = MinKeepA(p.x, low.x);
            high.y = MaxKeepB(high.y, p.y);
            low.y = MinKeepA(p.y, low.y);
            high.z = MaxKeepB(high.z, p.z);
            low.z = MinKeepA(p.z, low.z);
        }
    center = {(high.x + low.x) * 0.5f, (high.y + low.y) * 0.5f, (high.z + low.z) * 0.5f};
    boxMax = {high.x - center.x, high.y - center.y, high.z - center.z};
    boxMin = {low.x - center.x, low.y - center.y, low.z - center.z};
    radius = LengthXYZ(boxMax);
    flags |= static_cast<std::uint32_t>(BoundsFlag::Sphere | BoundsFlag::Box);
}

// ---- CTransformPipeline -----------------------------------------------------------------------------

void CTransformPipeline::SetViewportSize(float width, float height) {
    halfWidth = width * 0.5f;
    halfHeight = height * 0.5f;
}

void CTransformPipeline::SetDepthRange(float start, float end) {
    fogStart = start;
    fogScale = 255.0f / (end - start);
}

void CTransformPipeline::Project(const Matrix4& m, const VertexPacket* positions, Vector4* projected, int blocks) {
    for (int i = 0; i < blocks * 4; ++i) {
        const Vector3 p = PacketPoint(positions, i);
        const Vector3 transformed = TransformColumn<Sum3::XYZ>(m, p);
        projected[i] = Divided({transformed.x, transformed.y, transformed.z, TransformColumnW<Sum3::XYZ>(m, p)});
    }
}

int CTransformPipeline::ProjectClipped(const Matrix4& m, const VertexPacket* positions, Vector4* projected,
                                       std::uint8_t* outcodes, int blocks) const {
    return ProjectToClip(
        *this, m, [positions](int i) { return PacketPoint(positions, i); }, projected, outcodes, blocks * 4);
}

void CTransformPipeline::ProjectPoints(const Matrix4& m, const LightningPoint* points, Vector4* projected, int count) {
    for (int i = 0; i < count; ++i) projected[i] = Divided(ToClip(m, points[i].x, points[i].y, points[i].z));
}

int CTransformPipeline::ProjectPointsClipped(const Matrix4& m, const LightningPoint* points, Vector4* projected,
                                             std::uint8_t* outcodes, int count) const {
    return ProjectToClip(
        *this, m, [points](int i) { return Vector3{points[i].x, points[i].y, points[i].z}; }, projected, outcodes,
        count);
}

// A sprite's corners lie `size` / w times the viewport's half width to either side of its centre on the screen (the
// same number of pixels both ways), all at the centre's depth and 1 / w.
void CTransformPipeline::ProjectSprites(const Matrix4& m, const SpritePoint* points, TLVertex* corners,
                                        int count) const {
    for (int i = 0; i < count; ++i) {
        const SpritePoint& point = points[i];
        const Vector4 centre = ToScreen(ToClip(m, point.x, point.y, point.z));
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

Vector4 CTransformPipeline::ToScreen(const Vector4& p) const {
    const float rhw = 1.0f / p.w;
    return {(p.x * rhw + 1.0f) * halfWidth, (1.0f - p.y * rhw) * halfHeight, p.z * rhw, rhw};
}

void CTransformPipeline::ToScreen(TLVertex* vertices, int count) const {
    for (int i = 0; i < count; ++i) {
        TLVertex& v = vertices[i];
        const Vector4 screen = ToScreen(Vector4{v.x, v.y, v.z, v.rhw});
        SetPosition(v, screen.x, screen.y, screen.z, screen.w);
    }
}

// The depth (the camera's distance along the view) from the screen z through the projection's depth row and the
// homogeneous row; the fog's 0..255 from the range's start, capped, and the alpha only lowered.
void CTransformPipeline::FogVertices(TLVertex* vertices, int count) const {
    const float* p = projection.m;
    for (int i = 0; i < count; ++i) {
        TLVertex& v = vertices[i];
        const float depth = p[11] / (v.z * p[14] - p[10]);
        if (!(depth >= fogStart)) continue;
        const float fog = (depth - fogStart) * fogScale;
        const float alpha = 255.0f - MinKeepA(fog, 255.0f);
        const std::uint32_t lowered = static_cast<std::uint32_t>(x87::fistp(alpha)) << 24;
        const std::uint32_t old = v.specular & 0xFF000000u;
        v.specular = (v.specular & 0x00FFFFFFu) | (lowered < old ? lowered : old);
    }
}
