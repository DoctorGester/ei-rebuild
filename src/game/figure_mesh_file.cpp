// CFigureMesh's own file format (reached through virtuals only): the mesh is written to a file and read
// back. The name, then every array as a count and its elements, then the bounding values. See figure_mesh.h
// and docs/notes/world.md.
#include "figure_mesh.h"

#include "core_maths.h"

#include "ei/x87.h"
#include "lib/file_io.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "object_maths.h"

#include <cstring>

namespace {

void Write(lib::File* file, const void* data, int size) { file->Write(data, static_cast<unsigned>(size)); }
void Read(lib::File* file, void* data, int size) { file->Read(data, static_cast<unsigned>(size)); }

void WriteCount(lib::File* file, int count) { Write(file, &count, 4); }
int ReadCount(lib::File* file) {
    int count;
    Read(file, &count, 4);
    return count;
}

} // namespace

void CFigureMesh::SaveTransform(lib::File* file) {
    int length = static_cast<int>(name.size());
    WriteCount(file, length);
    Write(file, name.c_str(), length);
    WriteCount(file, buffers.vertexCount);
    Write(file, buffers.vertices, buffers.vertexCount * 0x40);
    WriteCount(file, buffers.normalCount);
    Write(file, buffers.normals, buffers.normalCount * 0x40);
    WriteCount(file, buffers.texCoordCount);
    Write(file, buffers.texCoords, buffers.texCoordCount * 8);
    WriteCount(file, 0);
    WriteCount(file, buffers.cornerCount);
    Write(file, buffers.corners, buffers.cornerCount * 6);
    WriteCount(file, buffers.wordCount);
    Write(file, buffers.words, buffers.wordCount * 4);
    WriteCount(file, buffers.indexCount);
    Write(file, buffers.indices, buffers.indexCount * 2);
    Write(file, &center, 12);
    Write(file, &boxMin, 12);
    Write(file, &boxMax, 12);
    Write(file, &radius, 4);
    Write(file, &materialValue, 4);
    Write(file, &meshType, 4);
}

void CPlainFigureMesh::LoadTransform(lib::File* file) {
    BuffersChanged();
    int length = ReadCount(file);
    Read(file, str::SetLength(name, length), length);
    // The arrays the file fills (the renderer's descriptor is not updated here).
    int count = ReadCount(file);
    vertices.resize(count);
    Read(file, vertices.data(), count * 0x40);
    count = ReadCount(file);
    normals.resize(count);
    Read(file, normals.data(), count * 0x40);
    count = ReadCount(file);
    texCoords.resize(count);
    Read(file, texCoords.data(), count * 8);
    ReadCount(file);   // the 0 the writer puts here
    count = ReadCount(file);
    corners.resize(count);
    Read(file, corners.data(), count * 6);
    count = ReadCount(file);
    words.resize(count);
    Read(file, words.data(), count * 4);
    count = ReadCount(file);
    indices.resize(count);
    Read(file, indices.data(), count * 2);
    Read(file, &center, 12);
    Read(file, &boxMin, 12);
    Read(file, &boxMax, 12);
    Read(file, &radius, 4);
    Read(file, &materialValue, 4);
    Read(file, &meshType, 4);
}

// ---- Copy and pose --------------------------------------------------------------------------------

void CFigureMesh::CopyFigure(const CFigureMesh* other) {
    BuffersChanged();
    CTransformObject::CopyTransform(*other);   // the transform base's copy
    name = other->name;
    buffers.corners = other->buffers.corners;
    buffers.cornerCount = other->buffers.cornerCount;
    buffers.words = other->buffers.words;
    buffers.wordCount = other->buffers.wordCount;
    buffers.indices = other->buffers.indices;
    buffers.indexCount = other->buffers.indexCount;
    buffers.texCoords = other->buffers.texCoords;
    buffers.texCoordCount = other->buffers.texCoordCount;
    textureOffsetU = 0;
    textureOffsetV = 0;
    center = other->center;
    boxMin = other->boxMin;
    boxMax = other->boxMax;
    x87::CopyBits(radius, other->radius);
    materialValue = other->materialValue;
    meshType = other->meshType;
}

void CFigureMesh::UpdateWorld(const Quaternion& rotation, const Vector3& offset) {
    // The pose virtual stores the quaternion as the world rotation.
    CombineRotation(rotation);
    // The position (+0x1C) rotated by the quaternion (w, v):
    // 2 (v.p) v + 2w (v x p) + (w^2 - v.v) p, then moved by `offset`, into +0x28.
    Vector3 v{rotation.x, rotation.y, rotation.z};
    auto* p = &localPosition;
    double dot = DotUnrounded<Sum3::ZYX, double>(v, *p);
    double twice = dot + dot;
    float z = static_cast<float>(v.z * twice);
    float y = static_cast<float>(v.y * twice);
    float x = static_cast<float>(twice * v.x);
    Vector3 along;
    along.Construct(x, y, z);
    float w = rotation.w;
    Vector3 cross, turned, scaled, sum, rotated;
    CrossProduct(&cross, v, *p);
    ScaleVector(&turned, cross, static_cast<float>(static_cast<double>(w) + w));
    float ww = static_cast<float>(static_cast<double>(w) * w);
    float k = static_cast<float>(ww - Dot(v, v));
    ScaleVector(&scaled, *p, k);
    AddVectors(&sum, scaled, turned);
    AddVectors(&rotated, sum, along);
    float* moved = &worldPosition.x;
    float mx = static_cast<float>(static_cast<double>(rotated.x) + offset.x);
    float my = static_cast<float>(static_cast<double>(rotated.y) + offset.y);
    float mz = static_cast<float>(static_cast<double>(rotated.z) + offset.z);
    moved[0] = mx;
    moved[1] = my;
    moved[2] = mz;

    // The rotation matrix (row-major 4x4) from the stored quaternion (w, x, y, z); the products the original
    // keeps on the FPU stay doubles.
    float* m = poseMatrix.m;
    std::memset(m, 0, sizeof(poseMatrix));
    m[15] = 1.0f;
    m[10] = 1.0f;
    m[5] = 1.0f;
    m[0] = 1.0f;
    float qw = worldRotation.w, qx = worldRotation.x, qy = worldRotation.y, qz = worldRotation.z;
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
    m[0] = static_cast<float>(1.0f - (zz + yy));
    m[4] = static_cast<float>(xy - wz);
    m[8] = static_cast<float>(static_cast<double>(wy) + xz);
    m[1] = static_cast<float>(xy + wz);
    m[5] = static_cast<float>(1.0f - (static_cast<double>(zzStored) + xx));
    m[9] = static_cast<float>(static_cast<double>(yz) - wx);
    m[2] = static_cast<float>(static_cast<double>(xz) - wy);
    m[6] = static_cast<float>(static_cast<double>(yz) + wx);
    m[10] = static_cast<float>(1.0f - (static_cast<double>(yy) + xx));
    x87::CopyBits(m[12], moved[0]);
    x87::CopyBits(m[13], moved[1]);
    x87::CopyBits(m[14], moved[2]);
}
