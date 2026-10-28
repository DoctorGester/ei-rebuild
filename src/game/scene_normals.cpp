// The fake unit scene's vertex normals (PrepareMeshes) and packed positions (PlaceMeshes), both called by
// LoadFakeUnitScene. Normals: for every mesh, each face's normal, each position's normal as the normalised
// sum of the normals of the faces using it (the zero vector when the sum is within the weld distance of
// zero), the faces' position indices refreshed, and the normals packed four at a time into 64-byte blocks
// (x[4], y[4], z[4], w[4]; unused lanes repeat the last normal, w is 1). Plain float maths (rule 7). Names
// provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "lib/containers.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace scene {
namespace {


double WeldDistance() { return 0.00001 /*0073B860*/; }

} // namespace

void __fastcall ComputeMeshNormals(MeshArray* meshes) {
    DwordBuffer facesOfVertex{};
    const double weld = WeldDistance();
    for (int m = 0; m < meshes->size; ++m) {
        SceneMesh* mesh = &meshes->data[m];
        PointBuffer& normals = mesh->normals;
        PointBuffer& positions = mesh->positions;
        FaceBuffer& faces = mesh->faces;
        normals.SetSize(0);
        for (int f = 0; f < faces.size; ++f) faces.data[f].ComputeNormal(&positions);

        for (int v = 0; v < positions.size; ++v) {
            facesOfVertex.SetSize(0);
            const std::uint32_t key = static_cast<std::uint32_t>(v) & 0xFFFF;
            for (int f = 0; f < faces.size; ++f) {
                const MeshFace& face = faces.data[f];
                if (face.vertex[0] == key || face.vertex[1] == key || face.vertex[2] == key)
                    facesOfVertex.SetAtGrow(facesOfVertex.size, static_cast<std::uint32_t>(f));
            }
            Vector3 sum = kZeroVector;
            const std::uint32_t* list = facesOfVertex.data;
            for (int i = 0; i < facesOfVertex.size; ++i) {
                const Vector3& n = faces.data[list[i]].normal;
                sum.x += n.x;
                sum.y += n.y;
                sum.z += n.z;
            }
            float length = LengthXYZ(sum);
            Vector3 normal;
            if (length > weld) {
                normal.x = sum.x / length;
                normal.y = sum.y / length;
                normal.z = sum.z / length;
            } else {   // within the weld distance of zero, or NaN
                normal = kZeroVector;
            }
            normals.SetAtGrow(normals.size, std::bit_cast<PointBits>(normal));
        }

        for (int f = 0; f < faces.size; ++f) {
            MeshFace& face = faces.data[f];
            face.position[0] = face.vertex[0];
            face.position[1] = face.vertex[1];
            face.position[2] = face.vertex[2];
        }

        // Four normals per block (lanes x[4], y[4], z[4], w[4]).
        BlockBuffer& blocks = mesh->normalBlocks;
        int count = normals.size;
        blocks.SetSize(count % 4 == 0 ? count / 4 : count / 4 + 1);
        for (int b = 0; b < blocks.size; ++b) {
            float* block = blocks.data[b].block;
            for (int lane = 0; lane < 4; ++lane) {
                std::uint32_t one = 0x3F800000u, zero = 0;
                std::memcpy(&block[lane], &one, 4);
                std::memcpy(&block[4 + lane], &zero, 4);
                std::memcpy(&block[8 + lane], &zero, 4);
                std::memcpy(&block[12 + lane], &one, 4);
            }
        }
        for (int j = 0; j < normals.size; ++j) {
            float* block = blocks.data[j / 4].block + (j & 3);
            const Vector3& n = normals.data[j].point;
            std::memcpy(&block[0], &n.x, 4);
            std::memcpy(&block[4], &n.y, 4);
            std::memcpy(&block[8], &n.z, 4);
        }
        count = normals.size;
        if (count & 3) {
            Vector3 last = normals.data[count - 1].point;
            float* block = blocks.data[count / 4].block;
            for (int lane = count & 3; lane < 4; ++lane) {
                block[lane] = last.x;
                block[4 + lane] = last.y;
                block[8 + lane] = last.z;
            }
        }
    }
    facesOfVertex.FreeData();
}

// PlaceMeshes (`unit` is the fake unit, meshes): every mesh's positions packed four at a time
// into 64-byte blocks (x[4], y[4], z[4], w[4] = 1); every lane starts as the first position, so
// the unused lanes of the last block keep it.
void __fastcall PackMeshPositions(FakeUnitScene* unit) {
    MeshArray& meshes = unit->meshes;
    for (int m = 0; m < meshes.size; ++m) {
        SceneMesh* mesh = &meshes.data[m];
        PointBuffer& positions = mesh->positions;
        BlockBuffer& blocks = mesh->positionBlocks;
        int count = positions.size;
        blocks.SetSize(count % 4 == 0 ? count / 4 : count / 4 + 1);
        for (int b = 0; b < blocks.size; ++b) {
            float* block = blocks.data[b].block;
            for (int lane = 0; lane < 4; ++lane) {
                const std::uint32_t* first = positions.data[0].v;
                std::uint32_t one = 0x3F800000u;
                std::memcpy(&block[lane], &first[0], 4);
                std::memcpy(&block[4 + lane], &first[1], 4);
                std::memcpy(&block[8 + lane], &first[2], 4);
                std::memcpy(&block[12 + lane], &one, 4);
            }
        }
        for (int j = 0; j < positions.size; ++j) {
            float* block = blocks.data[j / 4].block + (j & 3);
            const std::uint32_t* p = positions.data[j].v;
            std::uint32_t one = 0x3F800000u;
            std::memcpy(&block[0], &p[0], 4);
            std::memcpy(&block[4], &p[1], 4);
            std::memcpy(&block[8], &p[2], 4);
            std::memcpy(&block[12], &one, 4);
        }
    }
}

namespace {
// The branchless select after FCOMP: `b` when a < b or unordered, else `a`.
float SelectBelow(float a, float b) { return a >= b ? a : b; }
} // namespace

// Bounds of a mesh: the bounding box of the positions (+0x168 min, +0x174 max, both made relative to the
// centre), its centre (+0x15C, half the sum) and the radius (+0x180, half the box's diagonal). The running
// maximum of x starts at -FLT_MAX, the others at +/-FLT_MAX. Plain float maths (rule 7).
void __fastcall ComputeMeshBounds(SceneMesh* mesh) {
    const PointBuffer& positions = mesh->positions;
    float maxX = (-3.4028235e38f /*0073C1A8*/);
    std::uint32_t lowest = 0xFF7FFFFFu, highest = 0x7F7FFFFFu;
    float maxY, maxZ, minX, minY, minZ;
    std::memcpy(&maxY, &lowest, 4);
    std::memcpy(&maxZ, &lowest, 4);
    std::memcpy(&minX, &highest, 4);
    std::memcpy(&minY, &highest, 4);
    std::memcpy(&minZ, &highest, 4);
    for (int i = 0; i < positions.size; ++i) {
        const Vector3& p = positions.data[i].point;
        maxX = SelectBelow(p.x, maxX);
        minX = SelectBelow(minX, p.x);
        maxY = SelectBelow(p.y, maxY);
        minY = SelectBelow(minY, p.y);
        maxZ = SelectBelow(p.z, maxZ);
        minZ = SelectBelow(minZ, p.z);
    }
    Vector3& low = mesh->boxMin;
    Vector3& high = mesh->boxMax;
    Vector3& centre = mesh->center;
    low = {minX, minY, minZ};
    high = {maxX, maxY, maxZ};
    const float half = 0.5f /*0073B86C*/;
    centre = {(high.x + low.x) * half, (high.y + low.y) * half, (high.z + low.z) * half};
    float dx = (high.x - low.x) * half, dy = (high.y - low.y) * half, dz = (high.z - low.z) * half;
    mesh->radius = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
    low.x -= centre.x;
    low.y -= centre.y;
    low.z -= centre.z;
    high.x -= centre.x;
    high.y -= centre.y;
    high.z -= centre.z;
}

} // namespace scene
