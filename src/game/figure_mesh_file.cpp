// FigureMesh's own file format (reached through virtuals only): the mesh is written to a file and read
// back. The name, then every array as a count and its elements, then the bounding values. See figure_mesh.h
// and docs/notes/world.md.
#include "figure_mesh.h"

#include "core_maths.h"

#include "platform/file.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "object_maths.h"

#include <cstring>

namespace {

void Write(platform::File* file, const void* data, int size) { file->Write(data, static_cast<unsigned>(size)); }
void Read(platform::File* file, void* data, int size) { file->Read(data, static_cast<unsigned>(size)); }

void WriteCount(platform::File* file, int count) { Write(file, &count, 4); }
int ReadCount(platform::File* file) {
    int count;
    Read(file, &count, 4);
    return count;
}

} // namespace

void FigureMesh::SaveTransform(platform::File* file) {
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

void PlainFigureMesh::LoadTransform(platform::File* file) {
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

void FigureMesh::CopyFigure(const FigureMesh* other) {
    BuffersChanged();
    TransformObject::CopyTransform(*other);   // the transform base's copy
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
    radius = other->radius;
    materialValue = other->materialValue;
    meshType = other->meshType;
}

void FigureMesh::UpdateWorld(const Quaternion& rotation, const Vector3& offset) {
    // The pose virtual stores the quaternion as the world rotation.
    CombineRotation(rotation);
    // The position (+0x1C) rotated by the quaternion (w, v):
    // 2 (v.p) v + 2w (v x p) + (w^2 - v.v) p, then moved by `offset`, into +0x28.
    Vector3 v{rotation.x, rotation.y, rotation.z};
    auto* p = &localPosition;
    float dot = DotUnrounded<Sum3::ZYX>(v, *p);
    float twice = dot + dot;
    float z = v.z * twice;
    float y = v.y * twice;
    float x = twice * v.x;
    Vector3 along;
    along.Construct(x, y, z);
    float w = rotation.w;
    Vector3 cross, turned, scaled, sum, rotated;
    CrossProduct(&cross, v, *p);
    ScaleVector(&turned, cross, w + w);
    float ww = w * w;
    float k = ww - Dot(v, v);
    ScaleVector(&scaled, *p, k);
    AddVectors(&sum, scaled, turned);
    AddVectors(&rotated, sum, along);
    float* moved = &worldPosition.x;
    float mx = rotated.x + offset.x;
    float my = rotated.y + offset.y;
    float mz = rotated.z + offset.z;
    moved[0] = mx;
    moved[1] = my;
    moved[2] = mz;

    // The rotation matrix (row-major 4x4) from the stored quaternion (w, x, y, z).
    float* m = poseMatrix.m;
    std::memset(m, 0, sizeof(poseMatrix));
    m[15] = 1.0f;
    m[10] = 1.0f;
    m[5] = 1.0f;
    m[0] = 1.0f;
    float qw = worldRotation.w, qx = worldRotation.x, qy = worldRotation.y, qz = worldRotation.z;
    float x2 = qx + qx;
    float y2 = qy + qy;
    float z2 = qz + qz;
    float wx = x2 * qw;
    float wy = y2 * qw;
    float wz = z2 * qw;
    float xx = x2 * qx;
    float xy = y2 * qx;
    float xz = z2 * qx;
    float yy = y2 * qy;
    float yz = z2 * qy;
    float zz = z2 * qz;
    m[0] = 1.0f - (zz + yy);
    m[4] = xy - wz;
    m[8] = wy + xz;
    m[1] = xy + wz;
    m[5] = 1.0f - (zz + xx);
    m[9] = yz - wx;
    m[2] = xz - wy;
    m[6] = yz + wx;
    m[10] = 1.0f - (yy + xx);
    m[12] = moved[0];
    m[13] = moved[1];
    m[14] = moved[2];
}
