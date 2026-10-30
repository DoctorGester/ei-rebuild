// The 3D library side of figure loading and drawing: bounding-volume clip
// test, transform matrices, triangle index buffers and the link-file loader's archive list.
#include "model.h"

#include "clip.h"   // ClipOutcode, ClipCodeOf
#include "core_maths.h"
#include "resource.h"

#include "lib/crt.h"
#include "lib/pc24.h"

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

// |v| per component, and the larger of a and b per component.
Vector3 Magnitudes(const Vector3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
Vector3 Larger(const Vector3& a, const Vector3& b) {
    return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
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
        reach[0] = Magnitudes(x + w);
        reach[1] = Magnitudes(w - x);
        reach[2] = Magnitudes(y + w);
        reach[3] = Magnitudes(w - y);
        reach[4] = Magnitudes(z);
        reach[5] = Magnitudes(w - z);
        scale = Magnitudes(x) + Magnitudes(y) + Magnitudes(z) + Magnitudes(w);
        scaleW = std::fabs(r[3]) + std::fabs(r[7]) + std::fabs(r[11]) + std::fabs(r[15]);
    }

    // Classify's answer on the codes of the corners c + (lo or hi on each axis) as ClipCode computes them (2, 0 or
    // -1), or kNearPlane. The margin is 2^-16 of a bound on every term the corners' sums and these add (a
    // coordinate is at most |c| plus the larger extent); the rounding of either is below 2^-19 of that bound. A
    // NaN or an infinity anywhere fails every test and gives kNearPlane.
    int Classify(const Vector3& c, const Vector3& lo, const Vector3& hi) const {
        const Vector3 reaches = Magnitudes(c) + Larger(Magnitudes(lo), Magnitudes(hi));
        const float margin = (vecmath::Dot<Sum3::XYZ>(scale, reaches) + scaleW) * 0x1p-16f + 1e-30f;
        if (!(margin < 1e30f)) return kNearPlane;
        const Vector3 middle = c + (lo + hi) * 0.5f;
        const Vector3 half = Magnitudes(hi - lo) * 0.5f;
        const Vector3 clip = TransformColumn<Sum3::XYZ>(m, middle);
        const float w = TransformColumnW<Sum3::XYZ>(m, middle);
        const float centres[6] = {clip.x + w, w - clip.x, clip.y + w, w - clip.y, clip.z, w - clip.z};
        bool undecided = false, crossing = false;
        for (int i = 0; i < 6; ++i) {
            const float extent = vecmath::Dot<Sum3::XYZ>(reach[i], half);
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
    float scaleW;       // the same of the translation column
};

} // namespace

// ---- ClipBounds / TransformPipeline ---------------------------------------------------------------

std::uint32_t ClipBounds::SetBox(const float* min, const float* max) {
    std::memcpy(&boxMin, min, 12);
    std::memcpy(&boxMax, max, 12);
    flags |= BoundsFlag::Box;
    return flags;
}

// Clip code of one corner p under matrix m (ClipCodeOf): M * (p, 1), rows x, y and w summed ZXY, z ZYX, each rounded
// to float. The corner coordinates are kept on the FPU stack (the same at the main thread's 24-bit precision).
static std::uint32_t ClipCode(const Matrix4& m, const Vector3& p) {
    Vector3 c = TransformColumn<Sum3::ZXY, Sum3::ZXY, Sum3::ZYX>(m, p);
    float cw = TransformColumnW<Sum3::ZXY>(m, p);
    return ClipCodeOf(c.x, c.y, c.z, cw);
}

// Classify of the codes of the cube of half side r around c. The corner coordinates are sums the original keeps on
// the FPU stack; each use recomputes the same value.
static int CubeCorners(const Matrix4& m, const Vector3& c, float r) {
    const std::uint32_t codes[8] = {
        ClipCode(m, {c.x + r, r + c.y, r + c.z}),
        ClipCode(m, {c.x - r, r + c.y, r + c.z}),
        ClipCode(m, {c.x + r, c.y - r, r + c.z}),
        ClipCode(m, {c.x - r, c.y - r, r + c.z}),
        ClipCode(m, {c.x + r, r + c.y, c.z - r}),
        ClipCode(m, {c.x - r, r + c.y, c.z - r}),
        ClipCode(m, {c.x + r, c.y - r, c.z - r}),
        ClipCode(m, {c.x - r, c.y - r, c.z - r}),
    };
    return Classify(codes);
}

// Classify of the codes of the box c + [lo, hi].
static int BoxCorners(const Matrix4& m, const Vector3& c, const Vector3& lo, const Vector3& hi) {
    const std::uint32_t codes[8] = {
        ClipCode(m, {hi.x + c.x, hi.y + c.y, hi.z + c.z}),
        ClipCode(m, {hi.x + c.x, hi.y + c.y, lo.z + c.z}),
        ClipCode(m, {hi.x + c.x, lo.y + c.y, hi.z + c.z}),
        ClipCode(m, {hi.x + c.x, lo.y + c.y, lo.z + c.z}),
        ClipCode(m, {lo.x + c.x, hi.y + c.y, hi.z + c.z}),
        ClipCode(m, {lo.x + c.x, hi.y + c.y, lo.z + c.z}),
        ClipCode(m, {lo.x + c.x, lo.y + c.y, hi.z + c.z}),
        ClipCode(m, {lo.x + c.x, lo.y + c.y, lo.z + c.z}),
    };
    return Classify(codes);
}

// The answers are the corners' codes' (CubeCorners, BoxCorners); the planes' ranges decide most volumes first, and
// the codes are computed only for a volume with a corner near a plane.
int TransformPipeline::ClipTest(const ClipBounds* bounds) const {
    const MatrixStack& stack = stacks[kClipStack];
    const Matrix4& m = stack.matrices[stack.count - 1];
    const PlaneRanges planes(m);

    if (bounds->flags & 1) {
        const float r = bounds->radius;
        int result = planes.Classify(bounds->center, {-r, -r, -r}, {r, r, r});
        if (result == kNearPlane) result = CubeCorners(m, bounds->center, r);
        if (result >= 0) return result;
    }

    if (bounds->flags & 2) {
        int result = planes.Classify(bounds->center, bounds->boxMin, bounds->boxMax);
        if (result == kNearPlane) result = BoxCorners(m, bounds->center, bounds->boxMin, bounds->boxMax);
        if (result >= 0) return result;
    }
    return 1;
}

int TransformPipeline::ProjectBlocks(const VertexPacket* positions, Vector4* projected, std::uint8_t* outcodes,
                                      int blocks, const ClipBounds* bounds) const {
    const int where = bounds ? ClipTest(bounds) : 1;
    if (where == 1) return ProjectClipped(*Top(kClipStack), positions, projected, outcodes, blocks);
    return where;
}

Matrix4* ViewportMatrix(Matrix4* out, const Rect* rect) {
    float halfWidth = static_cast<float>((rect->right - rect->left) >> 1);
    float halfHeight = static_cast<float>((rect->bottom - rect->top) >> 1);
    std::memset(out, 0, sizeof(Matrix4));
    out->m[15] = 1.0f;
    out->m[10] = 1.0f;
    out->m[0] = halfWidth;
    out->m[3] = halfWidth;
    out->m[5] = -halfHeight;
    out->m[7] = halfHeight;
    return out;
}

Matrix4* ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar) {
    // FSIN / FCOS of the half angle (lib/pc24.h): the sine times zFar in one rounding, both stored.
    float half = fov * 0.5f;
    float c = pc24::Round(pc24::Cos(half));
    const pc24::Unrounded s = pc24::Sin(half);
    float q = pc24::Mul(s, zFar) / (zFar - zNear);
    std::memset(out, 0, sizeof(Matrix4));
    out->m[0] = c;
    out->m[5] = c * aspect;
    out->m[10] = q;
    out->m[14] = pc24::Round(s);
    out->m[11] = -(q * zNear);
    return out;
}

int LinkFigureLoader::AddArchive(const std::string* path) {
    for (const LinkArchive& archive : archives)
        if (archive.name == *path) return 1;

    LinkArchive& archive = archives.emplace_back(*path);
    MappedResource* resource = new MMIOResource();
    archive.resource = resource;
    if (resource->OpenResource(path->data())) return 1;

    // Failure: drop the archive again (the resource object is leaked) and log. (The original passed STLport's
    // string object by value to "%s": its first word, the text pointer, is what printed.)
    archives.pop_back();
    game::DebugLog("Can't open resource file \"%s\"\n", *path);
    return 0;
}
