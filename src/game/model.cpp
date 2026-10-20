// The 3D library side of figure loading and drawing (004F0000-00500000): bounding-volume clip
// test, transform matrices, triangle index buffers and the link-file loader's archive list.
#include "model.h"

#include "core_maths.h"
#include "link_figure.h"   // ITransformPipeline
#include "resource.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "record_lists.h"

#include <cstring>

namespace {

std::uint32_t Bits(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    return b;
}

std::uint32_t SignBit(float v) { return Bits(v) >> 31; }

// Combines the codes of a volume's 8 corners: 2 if all corners are outside one plane, 0 if all
// are inside, -1 if undecided.
int Classify(const std::uint32_t (&codes)[8]) {
    std::uint32_t all = codes[0] & 0x3F, any = codes[0];
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
    flags |= 2;
    return flags;
}
EI_REPLACE_METHOD(0x004F93A0, ClipBounds::SetBox);

// Clip code of one corner p under matrix m: M * (p, 1), rows x, y and w summed ZXY, z ZYX, each
// rounded to float as the original stores them. The original keeps the corner coordinates on the
// FPU stack (the same at the main thread's 24-bit precision).
//   bit 0  X + W < 0 (sign)     bit 1  2W < X + W (or unordered)
//   bit 2  Y + W < 0 (sign)     bit 3  2W < Y + W
//   bit 4  Z < 0 (sign)         bit 5  W < Z
static std::uint32_t ClipCode(const Matrix4& m, const Vector3& p) {
    Vector3 c = TransformColumn<Sum3::ZXY, Sum3::ZXY, Sum3::ZYX>(m, p);
    float cw = TransformColumnW<Sum3::ZXY>(m, p);
    float w2 = static_cast<float>(cw + cw);
    float xw = static_cast<float>(c.x + cw);
    float yw = static_cast<float>(c.y + cw);
    return SignBit(xw) | SignBit(yw) << 2 | SignBit(c.z) << 4 | std::uint32_t(!(w2 >= xw)) << 1 |
           std::uint32_t(!(w2 >= yw)) << 3 | std::uint32_t(!(cw >= c.z)) << 5;
}

int CTransformPipeline::ClipTest(const ClipBounds* b) const {
    const MatrixStack& stack = stacks_[kClipStack];
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
EI_REPLACE_METHOD(0x004F16A0, CTransformPipeline::ClipTest);

int CTransformPipeline::ProjectBlocks(const void* positions, void* projected, void* outcodes, int blocks,
                                      const ClipBounds* bounds) {
    const int where = bounds ? ClipTest(bounds) : 1;
    if (where == 0) {
        transform_->Project(Top(1), positions, projected, blocks);
        return 0;
    }
    if (where == 1) return transform_->ProjectClipped(Top(kClipStack), positions, projected, outcodes, blocks);
    return 2;
}
EI_REPLACE_METHOD(0x004F2650, CTransformPipeline::ProjectBlocks);

int CTransformPipeline::ProjectTerrain(const void* vertices, void* projected, void* outcodes, int count,
                                       const ClipBounds* bounds) {
    const int where = bounds ? ClipTest(bounds) : 1;
    if (where == 0) {
        transform_->ProjectTerrain(Top(kClipStack), vertices, projected, count);
        return 0;
    }
    if (where == 1) return transform_->ProjectTerrainClipped(Top(kClipStack), vertices, projected, outcodes, count);
    return 2;
}
EI_REPLACE_METHOD(0x004F29F0, CTransformPipeline::ProjectTerrain);

// ---- Matrices -----------------------------------------------------------------------------------------

Matrix4* __stdcall ViewportMatrix(Matrix4* out, const int* rect) {
    float halfWidth = static_cast<float>((rect[2] - rect[0]) >> 1);
    float halfHeight = static_cast<float>((rect[3] - rect[1]) >> 1);
    std::memset(out, 0, sizeof(Matrix4));
    out->m[15] = 1.0f;
    out->m[10] = 1.0f;
    out->m[0] = halfWidth;
    out->m[3] = halfWidth;
    out->m[5] = -halfHeight;
    out->m[7] = halfHeight;
    return out;
}
EI_REPLACE(0x004F1370, ViewportMatrix);

Matrix4* __stdcall ShearMatrix(Matrix4* out, const Vector3* dir) {
    std::memset(out, 0, sizeof(Matrix4));
    out->m[15] = 1.0f;
    out->m[5] = 1.0f;
    out->m[0] = 1.0f;
    out->m[10] = 0.0f;   // sic: the original clears the z scale it has just zeroed
    out->m[2] = -(dir->x / dir->z);
    out->m[6] = -(dir->y / dir->z);
    return out;
}
EI_REPLACE(0x004F13F0, ShearMatrix);

Matrix4* __stdcall ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar) {
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
EI_REPLACE(0x004F0E80, ProjectionMatrix);

EI_REPLACE_METHOD(0x004F57B0, TriangleIndexArray::SetSize);   // the figure parts' triangle indices

// ---- CLinkFigureLoader ---------------------------------------------------------------------------

namespace {
// list<pair<string, CMMIOResource*>> members (recordlist::LinkArchives'): insert(pos,
// value_type()) with the string reserved for 8 bytes (004FB2E0), and erase(pos) (004FAE00).
using Archives = stlp::list<recordlist::LinkArchive>;
} // namespace

int CLinkFigureLoader::AddArchive(const stlp::string* path) {
    for (LinkArchiveNode* n = archives_->next; n != archives_; n = n->next)
        if (n->name == *path) return 1;

    Archives::InsertDefault<recordlist::LinkArchiveElement>(stlp::AsNode<recordlist::LinkArchive>(archives_));
    LinkArchiveNode* node = archives_->prev;
    // The new string is empty (with room for 8 bytes): STLport's assign appends the whole path.
    if (&node->name != path && path->size() != 0) node->name.Append(path->data(), path->data() + path->size());

    void* memory = gamecrt::operator_new(sizeof(CMMIOResource));
    CMappedResource* resource = memory ? static_cast<CMMIOResource*>(memory)->Construct() : nullptr;
    node->resource = resource;
    if (resource->OpenResource(path->data(), 0)) return 1;

    // Failure: drop the node again (the resource object is leaked) and log. The original passes
    // the string object by value to "%s", so the three words of the string go on the stack.
    Archives::Erase<recordlist::LinkArchiveElement>(stlp::AsNode<recordlist::LinkArchive>(archives_->prev));
    auto words = reinterpret_cast<const char* const*>(path);
    game::DebugLog("Can't open resource file \"%s\"\n", words[0], words[1], words[2]);
    return 0;
}
EI_REPLACE_METHOD(0x004F6D30, CLinkFigureLoader::AddArchive);
