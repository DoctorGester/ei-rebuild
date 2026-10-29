// The 3D library side of figure loading and drawing: bounding-volume clip
// test, transform matrices, triangle index buffers and the link-file loader's archive list.
#include "model.h"

#include "clip.h"   // ClipOutcode, ClipCodeOf
#include "core_maths.h"
#include "resource.h"

#include "lib/crt.h"

#include "ei/x87.h"

#include <cstring>
#include <windows.h>   // RECT (ViewportMatrix)

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

} // namespace

// ---- ClipBounds / CTransformPipeline ---------------------------------------------------------------

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

int CTransformPipeline::ClipTest(const ClipBounds* b) const {
    const MatrixStack& stack = stacks[kClipStack];
    const Matrix4& m = stack.matrices[stack.count - 1];

    if (b->flags & 1) {
        // The cube of half side `radius` around the centre. The corner coordinates are sums the
        // original keeps on the FPU stack; each use recomputes the same value.
        const Vector3& c = b->center;
        const float r = b->radius;
        std::uint32_t codes[8] = {
            ClipCode(m, {c.x + r, r + c.y, r + c.z}),
            ClipCode(m, {c.x - r, r + c.y, r + c.z}),
            ClipCode(m, {c.x + r, c.y - r, r + c.z}),
            ClipCode(m, {c.x - r, c.y - r, r + c.z}),
            ClipCode(m, {c.x + r, r + c.y, c.z - r}),
            ClipCode(m, {c.x - r, r + c.y, c.z - r}),
            ClipCode(m, {c.x + r, c.y - r, c.z - r}),
            ClipCode(m, {c.x - r, c.y - r, c.z - r}),
        };
        int result = Classify(codes);
        if (result >= 0) return result;
    }

    if (b->flags & 2) {
        const Vector3& c = b->center;
        const Vector3& lo = b->boxMin;
        const Vector3& hi = b->boxMax;
        std::uint32_t codes[8] = {
            ClipCode(m, {hi.x + c.x, hi.y + c.y, hi.z + c.z}),
            ClipCode(m, {hi.x + c.x, hi.y + c.y, lo.z + c.z}),
            ClipCode(m, {hi.x + c.x, lo.y + c.y, hi.z + c.z}),
            ClipCode(m, {hi.x + c.x, lo.y + c.y, lo.z + c.z}),
            ClipCode(m, {lo.x + c.x, hi.y + c.y, hi.z + c.z}),
            ClipCode(m, {lo.x + c.x, hi.y + c.y, lo.z + c.z}),
            ClipCode(m, {lo.x + c.x, lo.y + c.y, hi.z + c.z}),
            ClipCode(m, {lo.x + c.x, lo.y + c.y, lo.z + c.z}),
        };
        int result = Classify(codes);
        if (result >= 0) return result;
    }
    return 1;
}

int CTransformPipeline::ProjectBlocks(const VertexPacket* positions, Vector4* projected, std::uint8_t* outcodes,
                                      int blocks, const ClipBounds* bounds) const {
    const int where = bounds ? ClipTest(bounds) : 1;
    if (where == 1) return ProjectClipped(*Top(kClipStack), positions, projected, outcodes, blocks);
    return where;
}

Matrix4* ViewportMatrix(Matrix4* out, const tagRECT* rect) {
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
    double half = fov * 0.5;
    float c = static_cast<float>(x87::fcos(half));
    double s = x87::fsin(half);
    float q = static_cast<float>(s * zFar / (zFar - zNear));
    std::memset(out, 0, sizeof(Matrix4));
    out->m[0] = c;
    out->m[5] = c * aspect;
    out->m[10] = q;
    out->m[14] = static_cast<float>(s);
    out->m[11] = -(q * zNear);
    return out;
}

int CLinkFigureLoader::AddArchive(const std::string* path) {
    for (const LinkArchive& archive : archives)
        if (archive.name == *path) return 1;

    LinkArchive& archive = archives.emplace_back(*path);
    CMappedResource* resource = new CMMIOResource();
    archive.resource = resource;
    if (resource->OpenResource(path->data(), 0)) return 1;

    // Failure: drop the archive again (the resource object is leaked) and log. (The original passed STLport's
    // string object by value to "%s": its first word, the text pointer, is what printed.)
    archives.pop_back();
    game::DebugLog("Can't open resource file \"%s\"\n", *path);
    return 0;
}
