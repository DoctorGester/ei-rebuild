// The fake unit scene's vertex normals (0044DCF0, Ghidra: PrepareMeshes) and packed positions
// (0044E3B0), both called by LoadFakeUnitScene. Normals: for every mesh, each face's normal (0049AEC0), each position's normal as the
// normalised sum of the normals of the faces using it (the zero vector 0079B9B0 when the sum is
// within the weld distance of zero), the faces' position indices refreshed, and the normals packed
// four at a time into 64-byte blocks (x[4], y[4], z[4], w[4]; unused lanes repeat the last
// normal, w is 1). Plain float maths (rule 7). Names provisional; see docs/notes/objects.md
// ("Sixth round").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "lib/vecmath.h"
#include "ei/hook.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace scene {
namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

// Mesh members used here (see scene_loader.h for the rest).
constexpr std::size_t kNormalBlocks = 0xB8;   // aligned array of 64-byte blocks
constexpr std::size_t kNormals = 0xF8;        // aligned array of points
constexpr std::size_t kFaceNormal = 0x24;     // in MeshFace: 3 floats

__declspec(naked) void __fastcall PointsSetAtGrow(PointBuffer*, void*, int, Vector3) EI_ORIGINAL(0x00452AF0)
__declspec(naked) void __fastcall DwordsSetAtGrow(DwordBuffer*, void*, int, std::uint32_t) EI_ORIGINAL(0x004538D0)
// 0049AEC0: the face's normal from the mesh's positions (stored at face +0x24).
__declspec(naked) void __fastcall FaceNormal(MeshFace*, void*, const PointBuffer* /*positions*/) EI_ORIGINAL(0x0049AEC0)

Vector3 GamePoint(std::uintptr_t address) {
    Vector3 p;
    std::memcpy(&p, reinterpret_cast<const void*>(address), sizeof(p));
    return p;
}
double WeldDistance() {
    double d;
    std::memcpy(&d, reinterpret_cast<const void*>(0x0073B860), sizeof(d));
    return d;
}

} // namespace

void __fastcall ComputeMeshNormals(VtArray* meshes) {
    DwordBuffer facesOfVertex{};
    const double weld = WeldDistance();
    for (int m = 0; m < meshes->size; ++m) {
        char* mesh = reinterpret_cast<char*>(meshes->data) + m * mesh::kSize;
        PointBuffer& normals = At<PointBuffer>(mesh, kNormals);
        PointBuffer& positions = At<PointBuffer>(mesh, mesh::kPositions);
        FaceBuffer& faces = At<FaceBuffer>(mesh, mesh::kFaces);
        normals.SetSize(0);
        for (int f = 0; f < faces.size; ++f) FaceNormal(&faces.data[f], nullptr, &positions);

        for (int v = 0; v < positions.size; ++v) {
            facesOfVertex.SetSize(0);
            const std::uint32_t key = static_cast<std::uint32_t>(v) & 0xFFFF;
            for (int f = 0; f < faces.size; ++f) {
                const MeshFace& face = faces.data[f];
                if (face.vertex[0] == key || face.vertex[1] == key || face.vertex[2] == key)
                    DwordsSetAtGrow(&facesOfVertex, nullptr, facesOfVertex.size, static_cast<std::uint32_t>(f));
            }
            Vector3 sum = GamePoint(0x0079B9B0);
            const std::uint32_t* list = facesOfVertex.data;
            for (int i = 0; i < facesOfVertex.size; ++i) {
                const Vector3& n = At<Vector3>(&faces.data[list[i]], kFaceNormal);
                sum.x += n.x;
                sum.y += n.y;
                sum.z += n.z;
            }
            float length = LengthXYZ(sum);
            Vector3 normal;
            if (!(length > weld)) {   // within the weld distance of zero, or NaN
                normal = GamePoint(0x0079B9B0);
            } else {
                normal.x = sum.x / length;
                normal.y = sum.y / length;
                normal.z = sum.z / length;
            }
            PointsSetAtGrow(&normals, nullptr, normals.size, normal);
        }

        for (int f = 0; f < faces.size; ++f) {
            MeshFace& face = faces.data[f];
            face.position[0] = face.vertex[0];
            face.position[1] = face.vertex[1];
            face.position[2] = face.vertex[2];
        }

        // Four normals per block (lanes x[4], y[4], z[4], w[4]).
        BlockBuffer& blocks = At<BlockBuffer>(mesh, kNormalBlocks);
        int count = normals.size;
        blocks.SetSize(count % 4 == 0 ? count / 4 : count / 4 + 1);
        for (int b = 0; b < blocks.size; ++b) {
            float* block = reinterpret_cast<float*>(blocks.data) + b * 16;
            for (int lane = 0; lane < 4; ++lane) {
                std::uint32_t one = 0x3F800000u, zero = 0;
                std::memcpy(&block[lane], &one, 4);
                std::memcpy(&block[4 + lane], &zero, 4);
                std::memcpy(&block[8 + lane], &zero, 4);
                std::memcpy(&block[12 + lane], &one, 4);
            }
        }
        for (int j = 0; j < normals.size; ++j) {
            float* block = reinterpret_cast<float*>(blocks.data) + (j / 4) * 16 + (j & 3);
            const Vector3& n = reinterpret_cast<Vector3*>(normals.data)[j];
            std::memcpy(&block[0], &n.x, 4);
            std::memcpy(&block[4], &n.y, 4);
            std::memcpy(&block[8], &n.z, 4);
        }
        count = normals.size;
        if (count & 3) {
            Vector3 last = reinterpret_cast<Vector3*>(normals.data)[count - 1];
            float* block = reinterpret_cast<float*>(blocks.data) + (count / 4) * 16;
            for (int lane = count & 3; lane < 4; ++lane) {
                block[lane] = last.x;
                block[4 + lane] = last.y;
                block[8 + lane] = last.z;
            }
        }
    }
    facesOfVertex.FreeData();
}
EI_REPLACE(0x0044DCF0, ComputeMeshNormals);

// 0044E3B0 (Ghidra: PlaceMeshes; `unit` is the fake unit, meshes at +0x544): every mesh's
// positions packed four at a time into 64-byte blocks at +0xA8 (x[4], y[4], z[4], w[4] = 1); every
// lane starts as the first position, so the unused lanes of the last block keep it.
void __fastcall PackMeshPositions(void* unit) {
    VtArray& meshes = At<VtArray>(unit, 0x544);
    for (int m = 0; m < meshes.size; ++m) {
        char* mesh = reinterpret_cast<char*>(meshes.data) + m * mesh::kSize;
        PointBuffer& positions = At<PointBuffer>(mesh, mesh::kPositions);
        BlockBuffer& blocks = At<BlockBuffer>(mesh, 0xA8);
        int count = positions.size;
        blocks.SetSize(count % 4 == 0 ? count / 4 : count / 4 + 1);
        for (int b = 0; b < blocks.size; ++b) {
            float* block = reinterpret_cast<float*>(blocks.data) + b * 16;
            for (int lane = 0; lane < 4; ++lane) {
                const std::uint32_t* first = reinterpret_cast<std::uint32_t*>(positions.data);
                std::uint32_t one = 0x3F800000u;
                std::memcpy(&block[lane], &first[0], 4);
                std::memcpy(&block[4 + lane], &first[1], 4);
                std::memcpy(&block[8 + lane], &first[2], 4);
                std::memcpy(&block[12 + lane], &one, 4);
            }
        }
        for (int j = 0; j < positions.size; ++j) {
            float* block = reinterpret_cast<float*>(blocks.data) + (j / 4) * 16 + (j & 3);
            const std::uint32_t* p = reinterpret_cast<std::uint32_t*>(positions.data) + j * 3;
            std::uint32_t one = 0x3F800000u;
            std::memcpy(&block[0], &p[0], 4);
            std::memcpy(&block[4], &p[1], 4);
            std::memcpy(&block[8], &p[2], 4);
            std::memcpy(&block[12], &one, 4);
        }
    }
}
EI_REPLACE(0x0044E3B0, PackMeshPositions);

namespace {
float GameFloat(std::uintptr_t address) {
    float f;
    std::memcpy(&f, reinterpret_cast<const void*>(address), sizeof(f));
    return f;
}
// The original's branchless select after FCOMP: `b` when a < b or unordered, else `a`.
float SelectBelow(float a, float b) { return !(a >= b) ? b : a; }
} // namespace

// 0044E8A0 (Ghidra: PrepareMesh; ECX: the mesh): the bounding box of the positions (+0x168 min,
// +0x174 max, both made relative to the centre), its centre (+0x15C, half the sum, 0073B86C) and
// the radius (+0x180, half the box's diagonal). The running maximum of x starts at 0073C1A8
// (-FLT_MAX), the others at +/-FLT_MAX. Plain float maths (rule 7).
void __fastcall ComputeMeshBounds(char* mesh) {
    const PointBuffer& positions = At<PointBuffer>(mesh, mesh::kPositions);
    float maxX = GameFloat(0x0073C1A8);
    std::uint32_t lowest = 0xFF7FFFFFu, highest = 0x7F7FFFFFu;
    float maxY, maxZ, minX, minY, minZ;
    std::memcpy(&maxY, &lowest, 4);
    std::memcpy(&maxZ, &lowest, 4);
    std::memcpy(&minX, &highest, 4);
    std::memcpy(&minY, &highest, 4);
    std::memcpy(&minZ, &highest, 4);
    for (int i = 0; i < positions.size; ++i) {
        const Vector3& p = reinterpret_cast<Vector3*>(positions.data)[i];
        maxX = SelectBelow(p.x, maxX);
        minX = SelectBelow(minX, p.x);
        maxY = SelectBelow(p.y, maxY);
        minY = SelectBelow(minY, p.y);
        maxZ = SelectBelow(p.z, maxZ);
        minZ = SelectBelow(minZ, p.z);
    }
    Vector3& low = At<Vector3>(mesh, 0x168);
    Vector3& high = At<Vector3>(mesh, 0x174);
    Vector3& centre = At<Vector3>(mesh, 0x15C);
    low = {minX, minY, minZ};
    high = {maxX, maxY, maxZ};
    const float half = GameFloat(0x0073B86C);
    centre = {(high.x + low.x) * half, (high.y + low.y) * half, (high.z + low.z) * half};
    float dx = (high.x - low.x) * half, dy = (high.y - low.y) * half, dz = (high.z - low.z) * half;
    At<float>(mesh, 0x180) = std::sqrt(dz * dz + dy * dy + dx * dx);
    low.x -= centre.x;
    low.y -= centre.y;
    low.z -= centre.z;
    high.x -= centre.x;
    high.y -= centre.y;
    high.z -= centre.z;
}
EI_REPLACE(0x0044E8A0, ComputeMeshBounds);

} // namespace scene
