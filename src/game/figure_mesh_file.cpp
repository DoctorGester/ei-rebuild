// CFigureMesh's own file format (vtable slots; no direct calls in the game): 0059F710 writes the
// mesh to an MFC CFile, 0059F8F0 reads it back. The name, then every array as a count and its
// elements, then the bounding values. See figure_mesh.h and docs/notes/world.md.
#include "figure_mesh.h"

#include "core_maths.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "object_maths.h"
#include "originals_core.h"

#include <cstring>
#include "originals_library.h"
#include "game_data.h"

namespace {

// The file's slots 14 (Write) and 13 (Read).
void Write(mfc::CFileVirtuals* file, const void* data, int size) { file->Write(data, static_cast<unsigned>(size)); }
void Read(mfc::CFileVirtuals* file, void* data, int size) { file->Read(data, static_cast<unsigned>(size)); }

void WriteCount(mfc::CFileVirtuals* file, int count) { Write(file, &count, 4); }
int ReadCount(mfc::CFileVirtuals* file) {
    int count;
    Read(file, &count, 4);
    return count;
}

} // namespace

void CFigureMesh::SaveToFile(mfc::CFileVirtuals* file) {
    int length = name_.GetLength();
    WriteCount(file, length);
    Write(file, static_cast<const char*>(name_), length);
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
    Write(file, &materialValue_, 4);
    Write(file, &meshType_, 4);
}

void CFigureMesh::LoadFromFile(mfc::CFileVirtuals* file) {
    BuffersChanged();
    int length = ReadCount(file);
    Read(file, name_.GetBufferSetLength(length), length);
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
    corners_.SetSize(count, -1);
    Read(file, corners_.data, count * 6);
    count = ReadCount(file);
    words_.SetSize(count, -1);
    Read(file, words_.data, count * 4);
    count = ReadCount(file);
    indices_.SetSize(count, -1);
    Read(file, indices_.data, count * 2);
    Read(file, &center_, 12);
    Read(file, &boxMin_, 12);
    Read(file, &boxMax_, 12);
    Read(file, &radius_, 4);
    Read(file, &materialValue_, 4);
    Read(file, &meshType_, 4);
}

// ---- Copy (slot 18) and pose (slot 15) ----------------------------------------------------------

namespace {

// The original's out-of-line Vector3 helpers (all __fastcall: out in ECX, first vector in EDX).
__declspec(naked) Vector3* __fastcall VectorMake(Vector3*, void*, float, float, float) EI_ORIGINAL(0x00401DE0)
__declspec(naked) Vector3* __fastcall VectorScale(Vector3*, const Vector3*, float) EI_ORIGINAL(0x00410770)

float One() { return 1.0f /*0073B84C*/; }

} // namespace

void CFigureMesh::CopyFigure(const CFigureMesh* other) {
    BuffersChanged();
    CTransformObject::CopyTransformImpl(*other);   // 00401490: the transform base's copy
    name_ = other->name_;
    buffers_.corners = other->buffers_.corners;
    buffers_.cornerCount = other->buffers_.cornerCount;
    buffers_.words = other->buffers_.words;
    buffers_.wordCount = other->buffers_.wordCount;
    buffers_.indices = other->buffers_.indices;
    buffers_.indexCount = other->buffers_.indexCount;
    buffers_.texCoords = other->buffers_.texCoords;
    buffers_.texCoordCount = other->buffers_.texCoordCount;
    textureOffsetU_ = 0;
    textureOffsetV_ = 0;
    spareDC_ = other->spareDC_;
    center_ = other->center_;
    boxMin_ = other->boxMin_;
    boxMax_ = other->boxMax_;
    std::memcpy(&radius_, &other->radius_, 4);
    materialValue_ = other->materialValue_;
    meshType_ = other->meshType_;
}

void CFigureMesh::SetPose(const Quaternion& rotation, const Vector3& offset) {
    // Slot 14 stores the quaternion as the world rotation (+0x44).
    FigureSlots(this)->CombineRotation(rotation);
    // The position (+0x1C) rotated by the quaternion (w, v):
    //   2 (v.p) v + 2w (v x p) + (w^2 - v.v) p, then moved by `offset`, into +0x28.
    Vector3 v{rotation.x, rotation.y, rotation.z};
    auto* p = &localPosition_;
    double dot = DotUnrounded<Sum3::ZYX, double>(v, *p);
    double twice = dot + dot;
    float z = static_cast<float>(v.z * twice);
    float y = static_cast<float>(v.y * twice);
    float x = static_cast<float>(twice * v.x);
    Vector3 along;
    VectorMake(&along, nullptr, x, y, z);
    float w = rotation.w;
    Vector3 cross, turned, scaled, sum, rotated;
    CrossProduct(&cross, v, *p);
    ScaleVector(&turned, cross, static_cast<float>(static_cast<double>(w) + w));
    float ww = static_cast<float>(static_cast<double>(w) * w);
    float k = static_cast<float>(ww - DotRaw(&v, &v));
    VectorScale(&scaled, p, k);
    VectorAdd(&sum, &scaled, &turned);
    VectorAdd(&rotated, &sum, &along);
    float* moved = &worldPosition_.x;
    float mx = static_cast<float>(static_cast<double>(rotated.x) + offset.x);
    float my = static_cast<float>(static_cast<double>(rotated.y) + offset.y);
    float mz = static_cast<float>(static_cast<double>(rotated.z) + offset.z);
    moved[0] = mx;
    moved[1] = my;
    moved[2] = mz;

    // The rotation matrix (+0xE0, row-major 4x4) from the stored quaternion (+0x44 w, +0x48 x,
    // +0x4C y, +0x50 z); the products the original keeps on the FPU stay doubles.
    float* m = poseMatrix_.m;
    for (int i = 0; i < 16; ++i) reinterpret_cast<std::uint32_t*>(m)[i] = 0;
    std::uint32_t one = 0x3F800000;
    std::memcpy(&m[15], &one, 4);
    std::memcpy(&m[10], &one, 4);
    std::memcpy(&m[5], &one, 4);
    std::memcpy(&m[0], &one, 4);
    float qw = worldRotation_.w, qx = worldRotation_.x, qy = worldRotation_.y, qz = worldRotation_.z;
    double x2 = static_cast<double>(qx) + qx;
    float y2 = static_cast<float>(static_cast<double>(qy) + qy);
    float z2 = static_cast<float>(static_cast<double>(qz) + qz);
    float wx = static_cast<float>(x2 * qw);
    float wy = static_cast<float>(static_cast<double>(y2) * qw);
    float wz = static_cast<float>(static_cast<double>(z2) * qw);
    double xx = x2 * qx;
    double xy = static_cast<double>(y2) * qx;
    float xz = static_cast<float>(static_cast<double>(z2) * qx);
    float yy = static_cast<float>(static_cast<double>(y2) * qy);
    float yz = static_cast<float>(static_cast<double>(z2) * qy);
    double zz = static_cast<double>(z2) * qz;
    float zzStored = static_cast<float>(zz);
    m[0] = static_cast<float>(One() - (zz + yy));
    m[4] = static_cast<float>(xy - wz);
    m[8] = static_cast<float>(static_cast<double>(wy) + xz);
    m[1] = static_cast<float>(xy + wz);
    m[5] = static_cast<float>(One() - (static_cast<double>(zzStored) + xx));
    m[9] = static_cast<float>(static_cast<double>(yz) - wx);
    m[2] = static_cast<float>(static_cast<double>(xz) - wy);
    m[6] = static_cast<float>(static_cast<double>(yz) + wx);
    m[10] = static_cast<float>(One() - (static_cast<double>(yy) + xx));
    std::memcpy(&m[12], &moved[0], sizeof(float));
    std::memcpy(&m[13], &moved[1], sizeof(float));
    std::memcpy(&m[14], &moved[2], sizeof(float));
}

EI_REPLACE_METHOD(0x0059F0F0, CFigureMesh::CopyFigure);
EI_REPLACE_METHOD(0x0059F510, CFigureMesh::SetPose);
EI_REPLACE_METHOD(0x0059F710, CFigureMesh::SaveToFile);
EI_REPLACE_METHOD(0x0059F8F0, CFigureMesh::LoadFromFile);
