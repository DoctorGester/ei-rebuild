// The transform pipeline's clip test of a part's bounds (ClipBounds, TransformPipeline::ClipTest)
// and the viewport and projection matrices. See pipeline.h.
#include "engine/scene/pipeline.h"

#include "engine/scene/clip.h"   // ClipOutcode, ClipCodeOf
#include "engine/core/core_maths.h"

#include "lib/crt.h"
#include "lib/maths.h"

#include <cmath>
#include <cstring>

namespace {

// Combines the codes of a volume's 8 corners: 2 if all corners are outside one plane, 0 if all
// are inside, -1 if undecided.
int Classify(const std::uint32_t (&codes)[8]) {
    std::uint32_t all = codes[0] & ClipOutcode::All, any = codes[0];
    for (int i = 1; i < 8; ++i) {
        all &= codes[i];
        any |= codes[i];
    }
    if (all) return 2;
    if (!any) return 0;
    return -1;
}

// PlaneRanges::Classify's answer when a plane passes too near a corner to decide without the corners' codes.
constexpr int kNearPlane = 3;

// The six clip planes of a matrix as functions of the model-space point p, each positive inside: ClipCodeOf's x + w,
// w - x, y + w, w - y, z and w - z of the clip-space point M * (p, 1). A volume's least and greatest value on each
// plane give Classify's answer on its corners' codes without computing them, when no corner is within a margin of
// a plane.
class PlaneRanges {
public:
    explicit PlaneRanges(const Matrix4& matrix) : m(matrix) {
        const float* r = m.m;
        const Vector3 x{r[0], r[1], r[2]}, y{r[4], r[5], r[6]}, z{r[8], r[9], r[10]}, w{r[12], r[13], r[14]};
        reach[0] = Abs(x + w);
        reach[1] = Abs(w - x);
        reach[2] = Abs(y + w);
        reach[3] = Abs(w - y);
        reach[4] = Abs(z);
        reach[5] = Abs(w - z);
        scale = Abs(x) + Abs(y) + Abs(z) + Abs(w);
        scaleW = std::fabs(r[3]) + std::fabs(r[7]) + std::fabs(r[11]) + std::fabs(r[15]);
    }

    // Classify's answer on the codes of the corners c + (lo or hi on each axis) as ClipCode computes them (2, 0 or
    // -1), or kNearPlane. The margin is 2^-16 of a bound on every term the corners' sums and these add (a
    // coordinate is at most |c| plus the larger extent); the rounding of either is below 2^-19 of that bound. A
    // NaN or an infinity anywhere fails every test and gives kNearPlane.
    int Classify(const Vector3& c, const Vector3& lo, const Vector3& hi) const {
        const Vector3 reaches = Abs(c) + Max(Abs(hi), Abs(lo));
        const float margin = (Dot(scale, reaches) + scaleW) * 0x1p-16f + 1e-30f;
        if (!(margin < 1e30f)) return kNearPlane;
        const Vector3 middle = c + (lo + hi) * 0.5f;
        const Vector3 half = Abs(hi - lo) * 0.5f;
        const Vector3 clip = TransformColumn(m, middle);
        const float w = TransformColumnW(m, middle);
        const float centres[6] = {clip.x + w, w - clip.x, clip.y + w, w - clip.y, clip.z, w - clip.z};
        bool undecided = false, crossing = false;
        for (int i = 0; i < 6; ++i) {
            const float extent = Dot(reach[i], half);
            const float most = centres[i] + extent, least = centres[i] - extent;
            if (most < -margin) return 2;   // every corner outside this plane
            if (least > margin) continue;   // every corner inside it
            if (most > margin && least < -margin) crossing = true;
            else undecided = true;
        }
        if (undecided) return kNearPlane;
        return crossing ? -1 : 0;
    }

private:
    const Matrix4& m;
    Vector3 reach[6];   // |coefficient| of p.x, p.y, p.z in each plane
    Vector3 scale;      // |x| + |y| + |z| + |w| of the columns of p.x, p.y, p.z: bounds every plane's coefficient
    float scaleW = 0.0f;  // the same of the translation column
};

} // namespace

// ---- ClipBounds / TransformPipeline ---------------------------------------------------------------

std::uint32_t ClipBounds::SetBox(const Vector3& min, const Vector3& max) {
    boxMin = min;
    boxMax = max;
    flags |= BoundsFlag::Box;
    return flags;
}

// Clip code of one corner p under matrix m (ClipCodeOf): M * (p, 1).
static std::uint32_t ClipCode(const Matrix4& m, const Vector3& p) {
    Vector3 c = TransformColumn(m, p);
    float cw = TransformColumnW(m, p);
    return ClipCodeOf(c.x, c.y, c.z, cw);
}

// Classify of the codes of the cube of half side r around c (corner i: -r in x, y, z for bits 0, 1, 2).
static int CubeCorners(const Matrix4& m, const Vector3& c, float r) {
    std::uint32_t codes[8];
    for (int i = 0; i < 8; ++i) codes[i] = ClipCode(m, c + Vector3{(i & 1) ? -r : r, (i & 2) ? -r : r, (i & 4) ? -r : r});
    return Classify(codes);
}

// Classify of the codes of the box c + [lo, hi] (corner i: lo in x, y, z for bits 2, 1, 0).
static int BoxCorners(const Matrix4& m, const Vector3& c, const Vector3& lo, const Vector3& hi) {
    std::uint32_t codes[8];
    for (int i = 0; i < 8; ++i)
        codes[i] = ClipCode(m, Vector3{(i & 4) ? lo.x : hi.x, (i & 2) ? lo.y : hi.y, (i & 1) ? lo.z : hi.z} + c);
    return Classify(codes);
}

// The answers are the corners' codes' (CubeCorners, BoxCorners); the planes' ranges decide most volumes first, and
// the codes are computed only for a volume with a corner near a plane.
TransformPipeline::ClipResult TransformPipeline::ClipTest(const ClipBounds* bounds) const {
    const MatrixStack& stack = stacks[kClipStack];
    const Matrix4& m = stack.matrices[stack.count - 1];
    const PlaneRanges planes(m);

    if (bounds->flags & 1) {
        const float r = bounds->radius;
        int result = planes.Classify(bounds->center, {-r, -r, -r}, {r, r, r});
        if (result == kNearPlane) result = CubeCorners(m, bounds->center, r);
        if (result >= 0) return static_cast<ClipResult>(result);
    }

    if (bounds->flags & 2) {
        int result = planes.Classify(bounds->center, bounds->boxMin, bounds->boxMax);
        if (result == kNearPlane) result = BoxCorners(m, bounds->center, bounds->boxMin, bounds->boxMax);
        if (result >= 0) return static_cast<ClipResult>(result);
    }
    return ClipResult::Crossing;
}

Matrix4* ViewportMatrix(Matrix4* out, const Rect* rect) {
    float halfWidth = static_cast<float>((rect->Width()) >> 1);
    float halfHeight = static_cast<float>((rect->Height()) >> 1);
    std::memset(out, 0, sizeof(Matrix4));
    out->m[15] = 1.0f;
    out->m[10] = 1.0f;
    out->m[0] = halfWidth;
    out->m[3] = halfWidth;
    out->m[5] = -halfHeight;
    out->m[7] = halfHeight;
    return out;
}

Matrix4* ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear) {
    float half = fov * 0.5f;
    float c = lib::Cos(half);
    const float s = lib::Sin(half);
    std::memset(out, 0, sizeof(Matrix4));
    out->m[0] = c;
    out->m[5] = c * aspect;
    out->m[10] = s;
    out->m[14] = s;
    out->m[11] = -(s * zNear);
    return out;
}
