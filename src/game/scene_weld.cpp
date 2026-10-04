// The fake unit scene's vertex welding (0044C4A0, called by LoadFakeUnitScene after loading; Ghidra
// names it LinkNodes): for every mesh, positions closer than the weld distance (0073B860) to an
// earlier one are merged, the face corners and the node's morph key points are renumbered to the
// merged list, then the texture coordinates the same way. Names provisional; see
// docs/notes/objects.md ("Sixth round").
#include "scene_loader.h"

#include "../lib/crt.h"
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

struct Point {
    float x, y, z;
};
static_assert(sizeof(Point) == 12);

// The engine's aligned array (data 32-byte aligned with the raw block before it, size, capacity,
// growth) and the instantiations used here: points (SetSize 0044F1D0, Add 0044F2E0), texture
// coordinates (00453100, 0044FBB0), dwords (004513A0); aligned allocation of n dwords (00453A10)
// and n points (00452C20).
__declspec(naked) void __fastcall PointsSetSize(ArrayBody*, void*, int, int) EI_ORIGINAL(0x0044F1D0)
__declspec(naked) int __fastcall PointsAdd(ArrayBody*, void*, Point) EI_ORIGINAL(0x0044F2E0)
__declspec(naked) void __fastcall CoordsSetSize(ArrayBody*, void*, int, int) EI_ORIGINAL(0x00453100)
__declspec(naked) int __fastcall CoordsAdd(ArrayBody*, void*, float, float) EI_ORIGINAL(0x0044FBB0)
__declspec(naked) void __fastcall DwordsSetSize(ArrayBody*, void*, int, int) EI_ORIGINAL(0x004513A0)
__declspec(naked) void* __fastcall AllocDwords(int) EI_ORIGINAL(0x00453A10)
__declspec(naked) void* __fastcall AllocPoints(int) EI_ORIGINAL(0x00452C20)
__declspec(naked) double __cdecl Hypot(double, double) EI_ORIGINAL(0x006FD026)   // _hypot

void FreeAligned(void* data) {
    if (data) gamecrt::operator_delete(static_cast<void**>(data)[-1]);
}

double WeldDistance() {
    double d;
    std::memcpy(&d, reinterpret_cast<const void*>(0x0073B860), sizeof(d));
    return d;
}

// The dword array's SetSize as the original inlines it here (aligned storage).
void SetSizeDwords(ArrayBody& a, int n) {
    if (n == 0) {
        if (a.data) {
            FreeAligned(a.data);
            a.data = nullptr;
        }
        a.max = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = AllocDwords(n);
        a.max = n;
        a.size = n;
    } else if (n <= a.max) {
        a.size = n;
    } else {
        int grow = a.grow;
        if (grow == 0) {
            grow = a.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 0x400) grow = 0x400;
        }
        int capacity = a.max + grow;
        if (n >= capacity) capacity = n;
        void* data = AllocDwords(capacity);
        std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * 4);
        FreeAligned(a.data);
        a.data = data;
        a.size = n;
        a.max = capacity;
    }
}

int* Ints(const ArrayBody& a) { return static_cast<int*>(a.data); }

} // namespace

// 0044C4A0. `unit`: the fake unit (meshes: CArray at +0x544, 0x18C each); `nodes`: the scene's
// nodes, one per mesh (morph keys at +0xD0). A position or coordinate within the weld distance of
// an earlier kept one (or a NaN distance) maps to it, otherwise it is kept.
void __fastcall WeldMeshVertices(void* unit, VtArray* nodes) {
    ArrayBody points{}, pointMap{}, coords{}, coordMap{};
    VtArray& meshes = At<VtArray>(unit, 0x544);
    if (meshes.body.size <= 0) return;
    const double weld = WeldDistance();
    for (int m = 0; m < meshes.body.size; ++m) {
        char* mesh = static_cast<char*>(meshes.body.data) + m * mesh::kSize;
        ArrayBody& positions = At<ArrayBody>(mesh, mesh::kPositions);

        PointsSetSize(&points, nullptr, 0, -1);
        DwordsSetSize(&pointMap, nullptr, 0, -1);
        SetSizeDwords(pointMap, positions.size);
        for (int i = 0; i < positions.size; ++i) {
            const Point& p = static_cast<Point*>(positions.data)[i];
            int index = 0;
            for (; index < points.size; ++index) {
                const Point& q = static_cast<Point*>(points.data)[index];
                float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                double length = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                                          static_cast<double>(dz) * dz);
                if (!(length >= weld)) break;
            }
            if (index == points.size) {
                PointsAdd(&points, nullptr, p);
                index = points.size - 1;
            }
            Ints(pointMap)[i] = index;
        }
        PointsSetSize(&positions, nullptr, points.size, -1);
        std::memcpy(positions.data, points.data, static_cast<std::size_t>(points.size) * sizeof(Point));

        // The node's morph keys (0x14 each, an aligned point array at +4) move to the merged list.
        char* node = static_cast<char*>(nodes->body.data) + m * node::kSize;
        VtArray& morph = At<VtArray>(node, node::kMorphKeys);
        for (int k = 0; k < morph.body.size; ++k) {
            ArrayBody& keyPoints = At<ArrayBody>(static_cast<char*>(morph.body.data) + k * 0x14, 4);
            int count = points.size;
            Point* moved = count ? static_cast<Point*>(AllocPoints(count)) : nullptr;
            for (int i = 0; i < keyPoints.size; ++i) moved[Ints(pointMap)[i]] = static_cast<Point*>(keyPoints.data)[i];
            PointsSetSize(&keyPoints, nullptr, count, -1);
            std::memcpy(keyPoints.data, moved, static_cast<std::size_t>(count) * sizeof(Point));
            FreeAligned(moved);
        }

        ArrayBody& texCoords = At<ArrayBody>(mesh, mesh::kTexCoords);
        CoordsSetSize(&coords, nullptr, 0, -1);
        DwordsSetSize(&coordMap, nullptr, 0, -1);
        SetSizeDwords(coordMap, texCoords.size);
        for (int i = 0; i < texCoords.size; ++i) {
            const float* uv = static_cast<float*>(texCoords.data) + i * 2;
            int index = 0;
            for (; index < coords.size; ++index) {
                const float* known = static_cast<float*>(coords.data) + index * 2;
                float du = known[0] - uv[0], dv = known[1] - uv[1];
                if (!(Hypot(du, dv) >= weld)) break;
            }
            if (index == coords.size) {
                CoordsAdd(&coords, nullptr, uv[0], uv[1]);
                index = coords.size - 1;
            }
            Ints(coordMap)[i] = index;
        }
        CoordsSetSize(&texCoords, nullptr, coords.size, -1);
        std::memcpy(texCoords.data, coords.data, static_cast<std::size_t>(coords.size) * 8);

        ArrayBody& faces = At<ArrayBody>(mesh, mesh::kFaces);
        for (int f = 0; f < faces.size; ++f) {
            MeshFace& face = static_cast<MeshFace*>(faces.data)[f];
            for (auto& v : face.vertex) v = static_cast<std::uint32_t>(Ints(pointMap)[v]);
            for (auto& v : face.vertex2) v = static_cast<std::uint32_t>(Ints(coordMap)[v]);
        }
    }
    FreeAligned(points.data);
    FreeAligned(coords.data);
    FreeAligned(coordMap.data);
    FreeAligned(pointMap.data);
}
EI_REPLACE(0x0044C4A0, WeldMeshVertices);

} // namespace scene
