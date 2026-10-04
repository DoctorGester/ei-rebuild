// CFigureMesh's own file format (vtable slots; no direct calls in the game): 0059F710 writes the
// mesh to an MFC CFile, 0059F8F0 reads it back. The name, then every array as a count and its
// elements, then the bounding values. See figure_mesh.h and docs/notes/world.md (seventh round).
#include "figure_mesh.h"

#include "core_maths.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

constexpr int kSlotFileRead = 13;    // CFile slot 13 (+0x34): Read(buffer, count)
constexpr int kSlotFileWrite = 14;   // CFile slot 14 (+0x38): Write(buffer, count)

// 00700E17 (RET 4). CString::GetBufferSetLength.
__declspec(naked) char* __fastcall StringBufferSetLength(void*, void*, int) EI_ORIGINAL(0x00700E17)

void Write(void* file, const void* data, int size) { ei::VCall<void>(file, kSlotFileWrite, data, static_cast<unsigned>(size)); }
void Read(void* file, void* data, int size) { ei::VCall<unsigned>(file, kSlotFileRead, data, static_cast<unsigned>(size)); }

void WriteCount(void* file, int count) { Write(file, &count, 4); }
int ReadCount(void* file) {
    int count;
    Read(file, &count, 4);
    return count;
}

char*& Name(CFigureMesh* mesh) { return *reinterpret_cast<char**>(reinterpret_cast<std::uint8_t*>(mesh) + 0x120); }

} // namespace

void CFigureMesh::SaveToFile(void* file) {
    int length = reinterpret_cast<const int*>(Name(this))[-2];
    WriteCount(file, length);
    Write(file, Name(this), length);
    WriteCount(file, buffers_.vertexCount);
    Write(file, buffers_.vertices, buffers_.vertexCount * 0x40);
    WriteCount(file, buffers_.normalCount);
    Write(file, buffers_.normals, buffers_.normalCount * 0x40);
    WriteCount(file, buffers_.texCoordCount);
    Write(file, buffers_.texCoords, buffers_.texCoordCount * 8);
    WriteCount(file, 0);
    WriteCount(file, buffers_.cornerCount);
    Write(file, buffers_.corners, buffers_.cornerCount * 6);
    WriteCount(file, buffers_.wordCount);
    Write(file, buffers_.words, buffers_.wordCount * 4);
    WriteCount(file, buffers_.indexCount);
    Write(file, buffers_.indices, buffers_.indexCount * 2);
    Write(file, &center_, 12);
    Write(file, &boxMin_, 12);
    Write(file, &boxMax_, 12);
    Write(file, &radius_, 4);
    Write(file, &unknown_158, 4);
    Write(file, &unknown_15C, 4);
}

void CFigureMesh::LoadFromFile(void* file) {
    int length = ReadCount(file);
    Read(file, StringBufferSetLength(reinterpret_cast<std::uint8_t*>(this) + 0x120, nullptr, length), length);
    // The arrays the file fills (the renderer's descriptor is not updated here).
    int count = ReadCount(file);
    vertices_.SetSize(count);
    Read(file, vertices_.data, count * 0x40);
    count = ReadCount(file);
    normals_.SetSize(count);
    Read(file, normals_.data, count * 0x40);
    count = ReadCount(file);
    texCoords_.SetSize(count);
    Read(file, texCoords_.data, count * 8);
    ReadCount(file);   // the 0 the writer puts here
    count = ReadCount(file);
    corners_.Resize(count, -1);
    Read(file, corners_.data, count * 6);
    count = ReadCount(file);
    words_.Resize(count, -1);
    Read(file, words_.data, count * 4);
    count = ReadCount(file);
    indices_.Resize(count, -1);
    Read(file, indices_.data, count * 2);
    Read(file, &center_, 12);
    Read(file, &boxMin_, 12);
    Read(file, &boxMax_, 12);
    Read(file, &radius_, 4);
    Read(file, &unknown_158, 4);
    Read(file, &unknown_15C, 4);
}

// ---- Copy (slot 18) and pose (slot 15) ----------------------------------------------------------

namespace {

// 00401490 (RET 4). The transform base's copy.
__declspec(naked) void __fastcall TransformCopy(void*, void*, const void*) EI_ORIGINAL(0x00401490)
__declspec(naked) void __fastcall StringAssign(void*, void*, const void*) EI_ORIGINAL(0x00700AA9)
// The original's out-of-line Vector3 helpers (all __fastcall: out in ECX, first vector in EDX).
__declspec(naked) Vector3* __fastcall VectorMake(Vector3*, void*, float, float, float) EI_ORIGINAL(0x00401DE0)
__declspec(naked) double __fastcall VectorDot(const Vector3*, const Vector3*) EI_ORIGINAL(0x00401E00)
__declspec(naked) Vector3* __fastcall VectorCross(Vector3*, const Vector3*, const Vector3*) EI_ORIGINAL(0x004489D0)
__declspec(naked) Vector3* __fastcall VectorScaleCopy(Vector3*, const Vector3*, float) EI_ORIGINAL(0x00448A10)
__declspec(naked) Vector3* __fastcall VectorScale(Vector3*, const Vector3*, float) EI_ORIGINAL(0x00410770)
__declspec(naked) Vector3* __fastcall VectorAdd(Vector3*, const Vector3*, const Vector3*) EI_ORIGINAL(0x0040CDC0)
// A float copied through the FPU (FLD / FSTP), as the original copies +0x28 to +0x110.
__declspec(naked) void __fastcall FpuCopy(const float* /*from*/, float* /*to*/) {
    __asm fld dword ptr [ecx]
    __asm fstp dword ptr [edx]
    __asm ret
}

constexpr int kSlotSetRotation = 14;   // slot 14 (+0x38): stores the quaternion at +0x44
float One() { return *reinterpret_cast<const volatile float*>(0x0073B84C); }

template <typename T>
T& Field(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

} // namespace

void CFigureMesh::CopyFrom(const CFigureMesh* other) {
    TransformCopy(this, nullptr, other);
    StringAssign(reinterpret_cast<std::uint8_t*>(this) + 0x120, nullptr, reinterpret_cast<const std::uint8_t*>(other) + 0x120);
    buffers_.corners = other->buffers_.corners;
    buffers_.cornerCount = other->buffers_.cornerCount;
    buffers_.words = other->buffers_.words;
    buffers_.wordCount = other->buffers_.wordCount;
    buffers_.indices = other->buffers_.indices;
    buffers_.indexCount = other->buffers_.indexCount;
    buffers_.texCoords = other->buffers_.texCoords;
    buffers_.texCoordCount = other->buffers_.texCoordCount;
    Field<std::uint32_t>(this, 0xCC) = 0;
    Field<std::uint32_t>(this, 0xD0) = 0;
    Field<std::uint32_t>(this, 0xDC) = Field<std::uint32_t>(const_cast<CFigureMesh*>(other), 0xDC);
    center_ = other->center_;
    boxMin_ = other->boxMin_;
    boxMax_ = other->boxMax_;
    std::memcpy(&radius_, &other->radius_, 4);
    unknown_158 = other->unknown_158;
    unknown_15C = other->unknown_15C;
}

#pragma float_control(precise, on, push)

void CFigureMesh::SetPose(const float* q, const float* offset) {
    ei::VCall<void>(this, kSlotSetRotation, q);
    // The position (+0x1C) rotated by the quaternion q = (w, x, y, z):
    //   2 (v.p) v + 2w (v x p) + (w^2 - v.v) p, then moved by `offset`, into +0x28.
    Vector3 v{q[1], q[2], q[3]};
    auto* p = reinterpret_cast<const Vector3*>(position_);
    double dot = static_cast<double>(v.z) * p->z + static_cast<double>(v.y) * p->y + static_cast<double>(v.x) * p->x;
    double twice = dot + dot;
    float z = StoreFloat(static_cast<float>(v.z * twice));
    float y = StoreFloat(static_cast<float>(v.y * twice));
    float x = StoreFloat(static_cast<float>(twice * v.x));
    Vector3 along;
    VectorMake(&along, nullptr, x, y, z);
    float w = q[0];
    Vector3 cross, turned, scaled, sum, rotated;
    VectorCross(&cross, &v, p);
    VectorScaleCopy(&turned, &cross, StoreFloat(static_cast<float>(static_cast<double>(w) + w)));
    float ww = StoreFloat(static_cast<float>(static_cast<double>(w) * w));
    float k = StoreFloat(static_cast<float>(ww - VectorDot(&v, &v)));
    VectorScale(&scaled, p, k);
    VectorAdd(&sum, &scaled, &turned);
    VectorAdd(&rotated, &sum, &along);
    float* moved = &Field<float>(this, 0x28);
    float mx = StoreFloat(static_cast<float>(static_cast<double>(rotated.x) + offset[0]));
    float my = StoreFloat(static_cast<float>(static_cast<double>(rotated.y) + offset[1]));
    float mz = StoreFloat(static_cast<float>(static_cast<double>(rotated.z) + offset[2]));
    moved[0] = mx;
    moved[1] = my;
    moved[2] = mz;

    // The rotation matrix (+0xE0, row-major 4x4) from the stored quaternion (+0x44 w, +0x48 x,
    // +0x4C y, +0x50 z); the products the original keeps on the FPU stay doubles.
    float* m = &Field<float>(this, 0xE0);
    for (int i = 0; i < 16; ++i) reinterpret_cast<std::uint32_t*>(m)[i] = 0;
    std::uint32_t one = 0x3F800000;
    std::memcpy(&m[15], &one, 4);
    std::memcpy(&m[10], &one, 4);
    std::memcpy(&m[5], &one, 4);
    std::memcpy(&m[0], &one, 4);
    float qw = Field<float>(this, 0x44), qx = Field<float>(this, 0x48), qy = Field<float>(this, 0x4C),
          qz = Field<float>(this, 0x50);
    double x2 = static_cast<double>(qx) + qx;
    float y2 = StoreFloat(static_cast<float>(static_cast<double>(qy) + qy));
    float z2 = StoreFloat(static_cast<float>(static_cast<double>(qz) + qz));
    float wx = StoreFloat(static_cast<float>(x2 * qw));
    float wy = StoreFloat(static_cast<float>(static_cast<double>(y2) * qw));
    float wz = StoreFloat(static_cast<float>(static_cast<double>(z2) * qw));
    double xx = x2 * qx;
    double xy = static_cast<double>(y2) * qx;
    float xz = StoreFloat(static_cast<float>(static_cast<double>(z2) * qx));
    float yy = StoreFloat(static_cast<float>(static_cast<double>(y2) * qy));
    float yz = StoreFloat(static_cast<float>(static_cast<double>(z2) * qy));
    double zz = static_cast<double>(z2) * qz;
    float zzStored = StoreFloat(static_cast<float>(zz));
    m[0] = StoreFloat(static_cast<float>(One() - (zz + yy)));
    m[4] = StoreFloat(static_cast<float>(xy - wz));
    m[8] = StoreFloat(static_cast<float>(static_cast<double>(wy) + xz));
    m[1] = StoreFloat(static_cast<float>(xy + wz));
    m[5] = StoreFloat(static_cast<float>(One() - (static_cast<double>(zzStored) + xx)));
    m[9] = StoreFloat(static_cast<float>(static_cast<double>(yz) - wx));
    m[2] = StoreFloat(static_cast<float>(static_cast<double>(xz) - wy));
    m[6] = StoreFloat(static_cast<float>(static_cast<double>(yz) + wx));
    m[10] = StoreFloat(static_cast<float>(One() - (static_cast<double>(yy) + xx)));
    FpuCopy(&moved[0], &Field<float>(this, 0x110));
    FpuCopy(&moved[1], &Field<float>(this, 0x114));
    FpuCopy(&moved[2], &Field<float>(this, 0x118));
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x0059F0F0, CFigureMesh::CopyFrom);
EI_REPLACE_METHOD(0x0059F510, CFigureMesh::SetPose);
EI_REPLACE_METHOD(0x0059F710, CFigureMesh::SaveToFile);
EI_REPLACE_METHOD(0x0059F8F0, CFigureMesh::LoadFromFile);
