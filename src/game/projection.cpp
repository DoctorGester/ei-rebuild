// The transform pipeline's projections: points to clip space with their outcodes and the batch's verdict, clip space
// to the screen, screen-facing sprites, the distance fog of screen vertices, and the flat projection and bounds
// fitting of shadows and figure parts. Plain C++ on the pipeline's matrices; 3DfpFPU.dll's transform object did
// this in the original (docs/notes/renderer.md, section 10).
#include "clip.h"          // TLVertex, ClipCodeOf, ClipVerdict

#include "core_maths.h"
#include "core_transform.h"
#include "figure_mesh.h"   // VertexPacket
#include "lightning.h"     // LightningPoint
#include "model.h"
#include "view_matrices.h"
#include "lib/float_select.h"
#include "lib/rounding.h"

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

// ---- TransformPipeline -----------------------------------------------------------------------------

void TransformPipeline::SetViewportSize(float width, float height) {
    halfWidth = width * 0.5f;
    halfHeight = height * 0.5f;
}

void TransformPipeline::SetDepthRange(float start, float end) {
    fogStart = start;
    fogScale = 255.0f / (end - start);
}

void TransformPipeline::Project(const Matrix4& m, const VertexPacket* positions, Vector4* projected, int blocks) {
    for (int i = 0; i < blocks * 4; ++i) {
        const Vector3 p = PacketPoint(positions, i);
        const Vector3 transformed = TransformColumn<Sum3::XYZ>(m, p);
        projected[i] = Divided({transformed.x, transformed.y, transformed.z, TransformColumnW<Sum3::XYZ>(m, p)});
    }
}

int TransformPipeline::ProjectClipped(const Matrix4& m, const VertexPacket* positions, Vector4* projected,
                                       std::uint8_t* outcodes, int blocks) const {
    return ProjectToClip(
        *this, m, [positions](int i) { return PacketPoint(positions, i); }, projected, outcodes, blocks * 4);
}

void TransformPipeline::ProjectPoints(const Matrix4& m, const LightningPoint* points, Vector4* projected, int count) {
    for (int i = 0; i < count; ++i) projected[i] = Divided(ToClip(m, points[i].x, points[i].y, points[i].z));
}

int TransformPipeline::ProjectPointsClipped(const Matrix4& m, const LightningPoint* points, Vector4* projected,
                                             std::uint8_t* outcodes, int count) const {
    return ProjectToClip(
        *this, m, [points](int i) { return Vector3{points[i].x, points[i].y, points[i].z}; }, projected, outcodes,
        count);
}

// A sprite's corners lie `size` / w times the viewport's half width to either side of its centre on the screen (the
// same number of pixels both ways), all at the centre's depth and 1 / w.
void TransformPipeline::ProjectSprites(const Matrix4& m, const SpritePoint* points, TLVertex* corners,
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

// The depth (the camera's distance along the view) from the screen z through the projection's depth row and the
// homogeneous row; the fog's 0..255 from the range's start, capped, and the alpha only lowered.
void TransformPipeline::FogVertices(TLVertex* vertices, int count) const {
    const float* p = projection.m;
    for (int i = 0; i < count; ++i) {
        TLVertex& v = vertices[i];
        const float depth = p[11] / (v.z * p[14] - p[10]);
        if (!(depth >= fogStart)) continue;
        const float fog = (depth - fogStart) * fogScale;
        const float alpha = 255.0f - MinKeepA(fog, 255.0f);
        const std::uint32_t lowered = static_cast<std::uint32_t>(lib::RoundEven(alpha)) << 24;
        const std::uint32_t old = v.specular & 0xFF000000u;
        v.specular = (v.specular & 0x00FFFFFFu) | (lowered < old ? lowered : old);
    }
}

// --- Matrix math -------------------------------------------------------------------------------------

// The 4x4 matrix helpers: the product, a quaternion's rotation matrix and the
// transform pipeline's matrix push. See model.h and docs/notes/script.md ("Leftovers").

namespace {

// The order in which each product element sums its four terms a[r][k] * b[k][c], per column, for row 0 and
// for rows 1-3 (the game's instruction order). The partial sums are rounded to float; on the game's main
// thread (24-bit x87 precision) they equal the ones the game keeps on the FPU stack.
using SumOrder = std::uint8_t[2][4][4];
constexpr SumOrder kProductOrder = {
    {{0, 2, 1, 3}, {2, 0, 1, 3}, {0, 2, 1, 3}, {3, 1, 0, 2}},
    {{1, 3, 2, 0}, {1, 0, 3, 2}, {1, 3, 2, 0}, {0, 3, 2, 1}},
};
constexpr SumOrder kPushOrder = {
    {{3, 2, 1, 0}, {1, 0, 3, 2}, {0, 3, 1, 2}, {2, 1, 0, 3}},
    {{2, 1, 0, 3}, {0, 3, 2, 1}, {3, 2, 0, 1}, {3, 2, 1, 0}},
};

// out = a * b, element by element in memory order, reading the inputs again for each element.
void Multiply(float* out, const float* a, const float* b, const SumOrder& order) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const std::uint8_t* k = order[r ? 1 : 0][c];
            const float* row = a + r * 4;
            float sum = static_cast<float>(row[k[0]] * b[k[0] * 4 + c] + row[k[1]] * b[k[1] * 4 + c]);
            sum = static_cast<float>(sum + row[k[2]] * b[k[2] * 4 + c]);
            out[r * 4 + c] = sum + row[k[3]] * b[k[3] * 4 + c];
        }
    }
}

} // namespace

void Matrix4::Multiply(const float* a, const float* b) { ::Multiply(m, a, b, kProductOrder); }

void Quaternion::ToMatrix(float* out) const {
    // Twice the vector part; the products the game stores as floats are rounded to float.
    float x2 = x + x;
    float y2 = static_cast<float>(y + y);
    float z2 = static_cast<float>(z + z);
    float wx = static_cast<float>(x2 * w);
    float wy = static_cast<float>(y2 * w);
    float wz = static_cast<float>(z2 * w);
    float xx = x2 * x;
    float xy = y2 * x;
    float xz = static_cast<float>(z2 * x);
    float yy = static_cast<float>(y2 * y);
    float yz = static_cast<float>(z2 * y);
    float zz = static_cast<float>(z2 * z);
    out[0] = 1.0f - static_cast<float>(zz + yy);
    out[1] = xy - wz;
    out[2] = xz + wy;
    out[4] = xy + wz;
    out[5] = 1.0f - static_cast<float>(zz + xx);
    out[6] = yz - wx;
    out[8] = xz - wy;
    out[9] = yz + wx;
    out[10] = 1.0f - static_cast<float>(yy + xx);
}

void TransformPipeline::PushMatrix(const float* m, int index) {
    MatrixStack& stack = stacks[index];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices.data(), m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    Multiply(stack.matrices[stack.count - 1].m, stack.matrices[stack.count - 2].m, m, kPushOrder);
}

// --- View matrices -----------------------------------------------------------------------------------

// The view matrix: see view_matrices.h. The sums keep the game's order.

namespace view {

Matrix4 ViewMatrix(const Vector3& position, const Quaternion& q) {
    Matrix4 result = {};
    float* out = result.m;
    Vector3 axisX = q.AxisX();
    Vector3 axisY = RotatedAxisY(q);   // inlined copies, the same orders
    Vector3 axisZ = RotatedAxisZ(q);
    const Vector3 p = position;
    out[0] = axisX.x;
    out[1] = axisX.y;
    out[2] = axisX.z;
    out[4] = axisY.x;
    out[5] = axisY.y;
    out[6] = axisY.z;
    out[8] = axisZ.x;
    out[3] = -DotZYX(p, axisX);
    out[9] = axisZ.y;
    out[10] = axisZ.z;
    out[7] = -DotZYX(p, axisY);
    out[11] = -DotZYX(p, axisZ);
    out[14] = 0.0f;
    out[13] = 0.0f;
    out[12] = 0.0f;
    out[15] = 1.0f;
    return result;
}

} // namespace view
