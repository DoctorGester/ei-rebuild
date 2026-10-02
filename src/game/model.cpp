// The 3D library side of figure loading and drawing (004F0000-00500000): bounding-volume clip
// test, transform matrices, triangle index buffers and the link-file loader's archive list.
#include "model.h"

#include "core_maths.h"
#include "resource.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cstring>

namespace {

std::uint32_t Bits(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    return b;
}

std::uint32_t SignBit(float v) { return Bits(v) >> 31; }

// Clip code of one corner (x, y, z) under matrix m. The four products are rounded to float as
// the original stores them; the corner coordinates stay unrounded (they live on the FPU stack).
//   bit 0  X + W < 0 (sign)     bit 1  2W < X + W (or unordered)
//   bit 2  Y + W < 0 (sign)     bit 3  2W < Y + W
//   bit 4  Z < 0 (sign)         bit 5  W < Z
#define EI_CLIP_CODE(m, x, y, z)                                                                  \
    [&]() -> std::uint32_t {                                                                      \
        float cx = StoreFloat((x) * (m)[0] + (z) * (m)[2] + (y) * (m)[1] + (m)[3]);               \
        float cy = StoreFloat((x) * (m)[4] + (z) * (m)[6] + (y) * (m)[5] + (m)[7]);               \
        float cz = StoreFloat((z) * (m)[10] + (y) * (m)[9] + (x) * (m)[8] + (m)[11]);             \
        float cw = StoreFloat((x) * (m)[12] + (z) * (m)[14] + (y) * (m)[13] + (m)[15]);           \
        float w2 = StoreFloat(cw + cw);                                                           \
        float xw = StoreFloat(cx + cw);                                                           \
        float yw = StoreFloat(cy + cw);                                                           \
        return SignBit(xw) | SignBit(yw) << 2 | SignBit(cz) << 4 | std::uint32_t(!(w2 >= xw)) << 1 | \
               std::uint32_t(!(w2 >= yw)) << 3 | std::uint32_t(!(cw >= cz)) << 5;                 \
    }()

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
    std::memcpy(boxMin, min, 12);
    std::memcpy(boxMax, max, 12);
    flags |= 2;
    return flags;
}
EI_REPLACE_METHOD(0x004F93A0, ClipBounds::SetBox);

// /fp:fast would regroup the sums ((a + b) + c) + d as (a + b) + (c + d), which rounds differently.
#pragma float_control(precise, on, push)
int CTransformPipeline::ClipTest(const ClipBounds* b) const {
    const float* m = matrices_[matrixCount_ - 1].m;

    if (b->flags & 1) {
        // The cube of half side `radius` around the centre. The corner coordinates are sums the
        // original keeps on the FPU stack; each use recomputes the same value.
        const float* c = b->center;
        const float r = b->radius;
        std::uint32_t codes[8] = {
            EI_CLIP_CODE(m, c[0] + r, r + c[1], r + c[2]),
            EI_CLIP_CODE(m, c[0] - r, r + c[1], r + c[2]),
            EI_CLIP_CODE(m, c[0] + r, c[1] - r, r + c[2]),
            EI_CLIP_CODE(m, c[0] - r, c[1] - r, r + c[2]),
            EI_CLIP_CODE(m, c[0] + r, r + c[1], c[2] - r),
            EI_CLIP_CODE(m, c[0] - r, r + c[1], c[2] - r),
            EI_CLIP_CODE(m, c[0] + r, c[1] - r, c[2] - r),
            EI_CLIP_CODE(m, c[0] - r, c[1] - r, c[2] - r),
        };
        int result = Classify(codes);
        if (result >= 0) return result;
    }

    if (b->flags & 2) {
        const float* c = b->center;
        const float* lo = b->boxMin;
        const float* hi = b->boxMax;
        std::uint32_t codes[8] = {
            EI_CLIP_CODE(m, hi[0] + c[0], hi[1] + c[1], hi[2] + c[2]),
            EI_CLIP_CODE(m, hi[0] + c[0], hi[1] + c[1], lo[2] + c[2]),
            EI_CLIP_CODE(m, hi[0] + c[0], lo[1] + c[1], hi[2] + c[2]),
            EI_CLIP_CODE(m, hi[0] + c[0], lo[1] + c[1], lo[2] + c[2]),
            EI_CLIP_CODE(m, lo[0] + c[0], hi[1] + c[1], hi[2] + c[2]),
            EI_CLIP_CODE(m, lo[0] + c[0], hi[1] + c[1], lo[2] + c[2]),
            EI_CLIP_CODE(m, lo[0] + c[0], lo[1] + c[1], hi[2] + c[2]),
            EI_CLIP_CODE(m, lo[0] + c[0], lo[1] + c[1], lo[2] + c[2]),
        };
        int result = Classify(codes);
        if (result >= 0) return result;
    }
    return 1;
}
#pragma float_control(pop)
EI_REPLACE_METHOD(0x004F16A0, CTransformPipeline::ClipTest);

#undef EI_CLIP_CODE

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

#pragma float_control(precise, on, push)
Matrix4* __stdcall ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar) {
    double half = fov * 0.5;
    float c = StoreFloat(static_cast<float>(x87::fcos(half)));
    double s = x87::fsin(half);
    float q = StoreFloat(static_cast<float>(s * zFar / (zFar - zNear)));
    std::memset(out, 0, sizeof(Matrix4));
    out->m[0] = c;
    out->m[5] = c * aspect;
    out->m[10] = q;
    out->m[14] = static_cast<float>(s);
    out->m[11] = -(q * zNear);
    return out;
}
#pragma float_control(pop)
EI_REPLACE(0x004F0E80, ProjectionMatrix);

// ---- TriangleIndexArray -------------------------------------------------------------------------

namespace {
// operator_new block with the data aligned to 32 bytes; the block pointer is stored before it.
std::uint16_t* AllocateTriangles(int count) {
    char* block = static_cast<char*>(gamecrt::operator_new(static_cast<unsigned>(count * 3 + 0x12) * 2));
    auto* data = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(block + 0x23) & ~std::uintptr_t(0x1F));
    reinterpret_cast<char**>(data)[-1] = block;
    return data;
}
void FreeTriangles(std::uint16_t* data) { gamecrt::operator_delete(reinterpret_cast<char**>(data)[-1]); }
} // namespace

void TriangleIndexArray::Resize(int count, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (count == 0) {
        if (data) {
            FreeTriangles(data);
            data = nullptr;
        }
        capacity = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = AllocateTriangles(count);
        capacity = count;
        size = count;
        return;
    }
    if (count <= capacity) {
        size = count;
        return;
    }
    int step = growBy;
    if (step == 0) {
        step = size / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int newCapacity = step + capacity;
    if (count >= newCapacity) newCapacity = count;
    std::uint16_t* fresh = AllocateTriangles(newCapacity);
    std::memcpy(fresh, data, static_cast<unsigned>(size * 6));
    FreeTriangles(data);
    data = fresh;
    size = count;
    capacity = newCapacity;
}
EI_REPLACE_METHOD(0x004F57B0, TriangleIndexArray::Resize);

// ---- CLinkFigureLoader ---------------------------------------------------------------------------

namespace {
struct ListIterator {
    LinkArchiveNode* node;
};
// list<pair<string, CMMIOResource*>> members (STLport instantiations, not rewritten):
// insert(pos, value_type()) with the string reserved for 8 bytes, and erase(pos).
__declspec(naked) ListIterator* __fastcall ListInsertDefault(LinkArchiveNode**, void*, ListIterator*, ListIterator) EI_ORIGINAL(0x004FB2E0)
__declspec(naked) ListIterator* __fastcall ListErase(LinkArchiveNode**, void*, ListIterator*, ListIterator) EI_ORIGINAL(0x004FAE00)
__declspec(naked) CMappedResource* __fastcall ConstructMmioResource(void*) EI_ORIGINAL(0x006721D0)
} // namespace

int CLinkFigureLoader::AddArchive(const stlp::string* path) {
    for (LinkArchiveNode* n = archives_->next; n != archives_; n = n->next)
        if (n->name == *path) return 1;

    ListIterator inserted;
    ListInsertDefault(&archives_, nullptr, &inserted, ListIterator{archives_});
    LinkArchiveNode* node = archives_->prev;
    // The new string is empty (with room for 8 bytes): STLport's assign appends the whole path.
    if (&node->name != path && path->size() != 0) node->name.Append(path->data(), path->data() + path->size());

    void* memory = gamecrt::operator_new(sizeof(CMMIOResource));
    CMappedResource* resource = memory ? ConstructMmioResource(memory) : nullptr;
    node->resource = resource;
    if (resource->OpenResource(path->data(), 0)) return 1;

    // Failure: drop the node again (the resource object is leaked) and log. The original passes
    // the string object by value to "%s", so the three words of the string go on the stack.
    ListIterator last{archives_->prev};
    ListIterator next;
    ListErase(&archives_, nullptr, &next, last);
    auto words = reinterpret_cast<const char* const*>(path);
    game::DebugLog("Can't open resource file \"%s\"\n", words[0], words[1], words[2]);
    return 0;
}
EI_REPLACE_METHOD(0x004F6D30, CLinkFigureLoader::AddArchive);
