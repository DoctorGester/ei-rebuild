// Verify wrapper for the client world's decal projector (src/game/world_client_decals.cpp, 0056C750):
// every drawn object shadow (0051CE60) and every ground print built (006CEDE0); zone1 after a new
// game, the platform pass where a bridge or another platform object is near.
//
// Ours runs first into the caller's arrays, then the original into private copies of the three
// arrays as they were (same capacities, their own buffers, made after ours from bytes kept in our
// memory, SV2f); both use the same builder code (through its addresses), so
// the sizes, capacities and used elements must agree (positions without the packets' w row, which
// the builder never writes; texture coordinates only of the vertices the indices use). The rebuilt object matrices are the same values both times, the
// pipeline's stack 14 is balanced, the static builder starts afresh in Begin and the static point
// array's contents are not kept, so nothing needs restoring between the runs.
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/ground_decal.h"
#include "../../src/game/world.h"

#include <cstring>
#include <vector>

namespace {

// An array kept in our memory (header and used elements); Copy makes the private copy (same
// capacity, its own buffer) later.
template <typename T>
struct KeptArray {
    lib::AlignedArray<T> header;
    std::vector<unsigned char> elements;
    explicit KeptArray(const lib::AlignedArray<T>& source) : header(source) {
        if (source.data && source.size > 0) {
            const auto* p = reinterpret_cast<const unsigned char*>(source.data);
            elements.assign(p, p + static_cast<std::size_t>(source.size) * sizeof(T));
        }
    }
    lib::AlignedArray<T> Copy() const {
        lib::AlignedArray<T> copy = header;
        if (header.data) {
            copy.data = lib::AlignedArray<T>::Allocate(header.capacity);
            if (!elements.empty()) std::memcpy(static_cast<void*>(copy.data), elements.data(), elements.size());
        }
        return copy;
    }
};

template <typename T>
void FreeCopy(lib::AlignedArray<T>& copy) {
    if (copy.data) lib::AlignedArray<T>::Free(copy.data);
}

template <typename T>
bool SameHeader(const lib::AlignedArray<T>& a, const lib::AlignedArray<T>& b) {
    return a.size == b.size && a.capacity == b.capacity && a.growBy == b.growBy;
}

bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 4, 1e-6f); }

// The first differing element, or -1.
int FirstPointDifference(const lib::AlignedArray<VertexPacket>& e, const lib::AlignedArray<VertexPacket>& a) {
    for (int i = 0; i < a.size; ++i)
        for (int row = 0; row < 3; ++row)
            for (int k = 0; k < 4; ++k)
                if (!Near(e[i].block[row][k], a[i].block[row][k])) return i;
    return -1;
}
// Only the vertices the indices use: the builder leaves the array at its reserved size (Reserve,
// never trimmed by Finish), and the elements past the last vertex (and the padding vertices of
// the last packet) are never written, so they hold whatever the buffers held before (X29).
int FirstTexCoordDifference(const lib::AlignedArray<DecalTexCoord>& e, const lib::AlignedArray<DecalTexCoord>& a,
                            const lib::AlignedArray<std::uint16_t>& indices) {
    for (int k = 0; k < indices.size; ++k) {
        int i = indices[k];
        if (i >= a.size) return i;
        if (!Near(e[i].u, a[i].u) || !Near(e[i].v, a[i].v)) return i;
    }
    return -1;
}
int FirstIndexDifference(const lib::AlignedArray<std::uint16_t>& e, const lib::AlignedArray<std::uint16_t>& a) {
    for (int i = 0; i < a.size; ++i)
        if (e[i] != a[i]) return i;
    return -1;
}

// 0056C750: PUSH -1; PUSH 00722A20 (7 bytes).
void(__fastcall* s_project)(CWorldClient*, void*, const Vector3*, const Vector3*, float, VertexPacket* const*,
                            lib::AlignedArray<VertexPacket>*, lib::AlignedArray<DecalTexCoord>*,
                            lib::AlignedArray<std::uint16_t>*, std::uint32_t, std::uint32_t);

void __fastcall ProjectDecalVerify(CWorldClient* self, void*, const Vector3* direction, const Vector3* point, float depth,
                                   VertexPacket* const* corners, lib::AlignedArray<VertexPacket>* points,
                                   lib::AlignedArray<DecalTexCoord>* texCoords, lib::AlignedArray<std::uint16_t>* indices,
                                   std::uint32_t skipOnPlatform, std::uint32_t objects) {
    KeptArray<VertexPacket> keptPoints(*points);
    KeptArray<DecalTexCoord> keptTexCoords(*texCoords);
    KeptArray<std::uint16_t> keptIndices(*indices);
    self->ProjectDecal(direction, point, depth, corners, points, texCoords, indices,
                       static_cast<std::uint8_t>(skipOnPlatform), static_cast<std::uint8_t>(objects));
    lib::AlignedArray<VertexPacket> expectedPoints = keptPoints.Copy();
    lib::AlignedArray<DecalTexCoord> expectedTexCoords = keptTexCoords.Copy();
    lib::AlignedArray<std::uint16_t> expectedIndices = keptIndices.Copy();
    s_project(self, nullptr, direction, point, depth, corners, &expectedPoints, &expectedTexCoords, &expectedIndices,
              skipOnPlatform, objects);
    bool headers = SameHeader(expectedPoints, *points) && SameHeader(expectedTexCoords, *texCoords) &&
                   SameHeader(expectedIndices, *indices);
    int packet = headers ? FirstPointDifference(expectedPoints, *points) : -1;
    int index = headers ? FirstIndexDifference(expectedIndices, *indices) : -1;
    int uv = headers && index < 0 ? FirstTexCoordDifference(expectedTexCoords, *texCoords, *indices) : -1;
    bool uvInRange = uv >= 0 && uv < texCoords->size;
    ei::verify::Check("CWorldClient::ProjectDecal", true, headers && packet < 0 && uv < 0 && index < 0,
                      "sizes %d / %d, %d / %d, %d / %d; first difference: packet %d, uv %d (%g, %g / %g, %g), "
                      "index %d (objects %u)",
                      expectedPoints.size, points->size, expectedTexCoords.size, texCoords->size, expectedIndices.size,
                      indices->size, packet, uv, uvInRange ? expectedTexCoords[uv].u : 0.0, uvInRange ? expectedTexCoords[uv].v : 0.0,
                      uvInRange ? (*texCoords)[uv].u : 0.0, uvInRange ? (*texCoords)[uv].v : 0.0, index, objects & 0xFF);
    FreeCopy(expectedPoints);
    FreeCopy(expectedTexCoords);
    FreeCopy(expectedIndices);
}
EI_VERIFY(0x0056C750, ProjectDecalVerify, 7, s_project);

} // namespace
