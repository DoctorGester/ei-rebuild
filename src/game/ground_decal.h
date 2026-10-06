#pragma once

// Ground decals (004D6C50-004D8BCC): the mesh builder the shadow projector 0056C750 lays a shadow
// texture on the ground with (and its terrain part 006CCF20). Every ground triangle under the
// projected quad is clipped to the decal: a plane facing along the projection direction, then
// u >= 0, v >= 0, u <= 1 and v <= 1 (u, v the triangle corners' texture coordinates in the
// decal); what is left is appended to the caller's vertex, texture coordinate and index arrays.
// Names provisional; see docs/notes/script.md ("Ground decals").

#include "figure_mesh.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// A clipper vertex: position, then the decal's texture coordinate. 0x14 bytes.
struct DecalVertex {
    float x, y, z;
    float u, v;
};
static_assert(sizeof(DecalVertex) == 0x14);

// The texture coordinate array's element.
struct DecalTexCoord {
    float u, v;
};
static_assert(sizeof(DecalTexCoord) == 8);

// 0x7C bytes, on the projector's stack.
class DecalBuilder {
public:
    // 004D7F50. Nulls the three array pointers; returns this.
    DecalBuilder* Construct();
    // 004D8000 (RET 0x1C). Starts a decal: empties the arrays (keeping their buffers) and sets the
    // projection up from the quad's corners (`corners` points at a pointer to a row-major 4x4
    // matrix whose columns 3, 2 and 0 are the origin, the u and the v corner), the projection
    // direction (normalised in place in the copy) and the point the cut-off plane is placed from,
    // `depth` * 0.33 back along the direction.
    void Begin(lib::AlignedArray<VertexPacket>* points, lib::AlignedArray<DecalTexCoord>* texCoords,
               lib::AlignedArray<std::uint16_t>* indices, float* const* corners, const Vector3* direction,
               const Vector3* point, float depth);
    // 004D7F60 (RET 8). Whether a sphere around `center` can touch the decal: its u and v ranges
    // widened by radius * the axis lengths overlap [0, 1]. The result is in AL only.
    bool Touches(const Vector3* center, float radius) const;
    // 004D8780 (RET 4). Makes room for `triangles` more triangles (three vertices and indices each;
    // the point array is grown by triangles * 3 / 4 + 1 + the vertex count packets).
    void Reserve(int triangles);
    // 004D85F0 / 004D8490 (RET 0xC, the same code with their own static triangle). Clips the
    // triangle (a, b, c) and appends what is left.
    void AddTriangle(const Vector3* a, const Vector3* b, const Vector3* c);
    void AddTerrainTriangle(const Vector3* a, const Vector3* b, const Vector3* c);
    // 004D89A0. Sets the arrays' sizes (the last packet padded with copies of vertex 0) and drops
    // the array pointers.
    void Finish();

    // The clipping stages; `tri` is clipped in place (and may be one of the stage buffers).
    // 004D7BD0: from the plane on. 004D74B0: from v >= 0 on. 004D71D0: from u <= 1 on.
    void ClipPlane(DecalVertex* tri);
    void ClipFromV(DecalVertex* tri);
    void ClipFromUMax(DecalVertex* tri);
    // 004D6CF0: appends the three corners and their indices.
    void Emit(const DecalVertex* tri);
    // 004D6C50: appends one vertex; returns its index (AX only).
    std::uint16_t AddVertex(const DecalVertex* v);

    lib::AlignedArray<VertexPacket>* points_;       // +0x00 x[4], y[4], z[4] per packet (w untouched)
    lib::AlignedArray<DecalTexCoord>* texCoords_;   // +0x04
    lib::AlignedArray<std::uint16_t>* indices_;     // +0x08
    Vector3 uAxis_;          // +0x0C the u edge across the direction, over its squared length
    Vector3 vAxis_;          // +0x18 the same for v
    Vector3 normal_;         // +0x24 the (normalised) direction
    Vector3 origin_;         // +0x30 the origin corner
    Vector3 slope_;          // +0x3C direction / |direction.z|
    Vector3 direction_;      // +0x48
    Vector3 point_;          // +0x54
    float uOffset_;          // +0x60 u of a point p = uAxis . p + uOffset
    float vOffset_;          // +0x64
    float planeOffset_;      // +0x68 plane distance = normal . p + planeOffset
    float uLength_;          // +0x6C |uAxis|
    float vLength_;          // +0x70 |vAxis|
    int vertexCount_;        // +0x74
    int indexCount_;         // +0x78

private:
    void AddCorners(DecalVertex* tri, const Vector3* a, const Vector3* b, const Vector3* c);
    void ClipStage(DecalVertex* tri, int stage);
};
static_assert(sizeof(DecalBuilder) == 0x7C);
static_assert(offsetof(DecalBuilder, uAxis_) == 0x0C && offsetof(DecalBuilder, normal_) == 0x24);
static_assert(offsetof(DecalBuilder, slope_) == 0x3C && offsetof(DecalBuilder, point_) == 0x54);
static_assert(offsetof(DecalBuilder, uOffset_) == 0x60 && offsetof(DecalBuilder, vLength_) == 0x70);
static_assert(offsetof(DecalBuilder, vertexCount_) == 0x74 && offsetof(DecalBuilder, indexCount_) == 0x78);

namespace decal {

// 004D7140 (__fastcall, RET 0xC). The point where the edge p -> q crosses distance 0, given the
// distances dp and dq of its ends: (dq * p - dp * q) / (dq - dp), all five components.
void __fastcall Interpolate(const DecalVertex* p, const DecalVertex* q, DecalVertex* out, float dp, float dq);
// 004D6E90 (__fastcall, RET 0xC). Clips the triangle `tri` (corner distances a, b, c; a corner is
// inside when its distance's sign bit is clear) and returns how many triangles are left: 0, 1
// (in `tri`) or 2 (`tri` and `out`).
int __fastcall ClipTriangle(DecalVertex* tri, DecalVertex* out, float a, float b, float c);

} // namespace decal
