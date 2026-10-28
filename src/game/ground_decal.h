#pragma once

// Ground decals: the mesh builder the shadow projector lays a shadow texture on the ground with. Every ground
// triangle under the projected quad is clipped to the decal (a plane facing along the projection, then u, v in
// [0, 1]) and appended to the caller's arrays. See docs/notes/script.md ("Ground decals").

#include "figure_mesh.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// A clipper vertex: position, then the decal's texture coordinate.
struct DecalVertex {
    Vector3 position;
    float u, v;
};
static_assert(sizeof(DecalVertex) == 0x14 && offsetof(DecalVertex, u) == 0x0C);

struct DecalTexCoord {
    float u, v;
};
static_assert(sizeof(DecalTexCoord) == 8);

class DecalBuilder {
public:
    DecalBuilder* Construct();
    // Starts a decal: empties the arrays (keeping their buffers) and sets the projection up from the quad's
    // corners (a row-major 4x4 matrix whose columns 3, 2 and 0 are the origin, the u and the v corner), the
    // direction (normalised in the copy) and the cut-off plane's point, `depth` * 0.33 back along it.
    void Begin(std::vector<VertexPacket>* points, std::vector<DecalTexCoord>* texCoords,
               std::vector<std::uint16_t>* indices, float* const* corners, const Vector3* direction,
               const Vector3* point, float depth);
    // Whether a sphere around `center` can touch the decal: its u and v ranges widened by radius * the axis
    // lengths overlap [0, 1].
    bool Touches(const Vector3* center, float radius) const;
    // Makes room for `triangles` more triangles (the point array grows by triangles * 3 / 4 + 1 + the vertex
    // count packets).
    void Reserve(int triangles);
    // Two copies of the same code with their own static triangle: clip (a, b, c) and append what is left.
    void AddTriangle(const Vector3* a, const Vector3* b, const Vector3* c);
    void AddTerrainTriangle(const Vector3* a, const Vector3* b, const Vector3* c);
    // Sets the arrays' sizes (the last packet padded with copies of vertex 0) and drops the array pointers.
    void Finish();

    // The clipping stages; `tri` is clipped in place. ClipFromV runs from v >= 0 on, ClipFromUMax from u <= 1.
    void ClipPlane(DecalVertex* tri);
    void Emit(const DecalVertex* tri);
    // Appends one vertex; returns its index.
    std::uint16_t AddVertex(const DecalVertex* v);

    std::vector<VertexPacket>* points_;       // x[4], y[4], z[4] per packet (w untouched)
    std::vector<DecalTexCoord>* texCoords_;
    std::vector<std::uint16_t>* indices_;
    Vector3 uAxis_;          // the u edge across the direction, over its squared length
    Vector3 vAxis_;          // the same for v
    Vector3 normal_;         // the (normalised) direction
    Vector3 origin_;         // the origin corner
    Vector3 slope_;          // direction / |direction.z|
    Vector3 direction_;
    Vector3 point_;
    float uOffset_;          // u of a point p = uAxis . p + uOffset
    float vOffset_;
    float planeOffset_;      // plane distance = normal . p + planeOffset
    float uLength_;          // |uAxis|
    float vLength_;          // |vAxis|
    int vertexCount_;
    int indexCount_;

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

// Where the edge p -> q crosses distance 0, given its ends' distances: (dq * p - dp * q) / (dq - dp).
void __fastcall Interpolate(const DecalVertex* p, const DecalVertex* q, DecalVertex* out, float dp, float dq);
// Clips the triangle `tri` (corner distances a, b, c; a corner is inside when its distance's sign bit is
// clear) and returns how many triangles are left: 0, 1 (in `tri`) or 2 (`tri` and `out`).
int __fastcall ClipTriangle(DecalVertex* tri, DecalVertex* out, float a, float b, float c);

} // namespace decal
