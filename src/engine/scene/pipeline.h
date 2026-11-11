#pragma once

// The transform pipeline of the 3D library: the matrix stacks, the clip test of a part's bounds against the view
// volume, the projections to clip space and to the screen, and the viewport, projection and view matrices. Matrices
// are row-major 4x4 float arrays (translation in column 3). See docs/notes/models.md and docs/notes/renderer.md.

#include "engine/scene/renderer_base.h"
#include "render/mesh_records.h"

#include "lib/bit_flags.h"
#include "lib/rect.h"
#include "lib/vecmath.h"

#include <array>
#include <cstdint>

// ClipBounds::flags. A figure part's bounds (FootprintPart) also say how the part stands in the AI map: bits 3-5
// its OccluderKind (BoundsOccluderKind), bits 6-9 its transparency; the part's name sets them
// (objects/object3d.cpp).
enum class BoundsFlag : std::uint32_t {
    Sphere = 0x1,          // the centre and radius are valid
    Box = 0x2,             // the box is valid
    Loaded = 0x4,          // a part's bounds are loaded
    Crown = 0x90,          // a "CROWN..." part: OccluderKind::Crown, transparency 2
    Base = 0x400,          // a "BASE..." part: its footprint is a bridge deck
    NoFootprint = 0x800,   // an "EMPTY..." part: no footprint
};
template <> inline constexpr bool lib::kBitFlags<BoundsFlag> = true;

enum class OccluderKind : std::int8_t;   // sector_map.h

// A part's occluder kind and transparency (index into the transparency table).
inline OccluderKind BoundsOccluderKind(std::uint32_t flags) { return static_cast<OccluderKind>((flags >> 3) & 7); }
inline std::int8_t BoundsTransparency(std::uint32_t flags) { return static_cast<std::int8_t>((flags >> 6) & 0xF); }

using VertexPacket = render::VertexPacket;
using NormalPacket = render::NormalPacket;

// Bounding volume of a figure part, tested against the view frustum.
struct ClipBounds {
    std::uint32_t flags = 0;  // BoundsFlag
    Vector3 center;
    float radius = 0.0f;   // half the side of the cube around the centre
    Vector3 boxMin;        // box corners, relative to the centre
    Vector3 boxMax;

    // Sets the box and its flag; returns the new flags.
    std::uint32_t SetBox(const Vector3& min, const Vector3& max);
    // Fits the box to the points of `blocks` packets of four (the centre midway between their extremes, the box
    // around it) and the sphere around the box (its half diagonal), and sets both flags.
    void Fit(const VertexPacket* packets, int blocks);
};

// A box of an object's model that blocks the AI map: the part's bounds with the part's position when it was
// loaded. A figure mesh has one (FigureMesh::box), copied whole from the piece's header into the object's
// part boxes (Object3D::partBoxes).
struct FootprintPart {

    ClipBounds bounds;      // flags: also the part's occluder kind and transparency; the centre is the part's offset
    Vector3 pivot;
};

// One of the transform pipeline's matrix stacks: the matrices and the depth.
constexpr int kMatrixStackDepth = 16;   // matrices a stack has room for
struct MatrixStack {
    std::array<Matrix4, kMatrixStackDepth> matrices;
    int count = 0;          // the top is matrices[count - 1]
};

struct TLVertex;

// A screen-facing sprite's centre and size (SceneRenderer::DrawSprites' points).
struct SpritePoint {
    Vector3 position;
    float size = 0.0f;
};

// `m` times the point (x, y, z, 1), each row's sum adding the translation first
// (((x m[4i] + m[4i + 3]) + y m[4i + 1]) + z m[4i + 2]): the projections' clip space.
Vector4 ToClip(const Matrix4& m, const Vector3& p);

// The transform pipeline of the 3D library: the matrix stacks, and the projections of points to clip space and
// to the screen, with the viewport and the fog's depth range they map with (docs/notes/renderer.md, section 10).
class TransformPipeline final : public RendererBase {
public:
    // Stack 2 holds the matrix ClipTest projects with; 14 the decal projector's platform objects
    // and parts (WorldClient::ProjectDecal).
    static constexpr int kClipStack = 2;
    static constexpr int kDecalStack = 14;
    // Stack 1 is the world's (the screen stack the draws project meshes with), 3 the object's: the placement
    // alone, for a part about to be lit on the CPU.
    static constexpr int kWorldStack = 1;
    static constexpr int kObjectStack = 3;
    // Stack 0 is the picking's (the view matrix pushed onto it), 9 the view matrices.
    static constexpr int kPickStack = 0;
    static constexpr int kViewStack = 9;

    // ClipTest's verdicts, also the result of the device's mesh draws.
    enum class ClipResult : int { Inside = 0, Crossing = 1, Outside = 2 };
    // Classifies `bounds` against the view volume of the current (top) matrix:
    // ClipResult::Outside = entirely outside one clip plane, ClipResult::Inside = entirely inside, ClipResult::Crossing = crossing or
    // not decided. The cube (flag 0) is tried first, then the box (flag 1).
    ClipResult ClipTest(const ClipBounds* bounds) const;
    // Pushes `m` onto stack `index`: an empty stack gets `m` itself, otherwise the new top is the old top
    // times `m` (no capacity check).
    void PushMatrix(const float* m, int index);
    // PushMatrix as the draw code inlines it: the new top by Matrix4::Multiply, whose elements sum in
    // another order.
    void PushMatrixProduct(const Matrix4& m, int index) {
        MatrixStack& stack = stacks[index];
        if (stack.count == 0) {
            stack.count = 1;
            stack.matrices[0] = m;
            return;
        }
        const int depth = ++stack.count;
        stack.matrices[depth - 1].Multiply(stack.matrices[depth - 2].m, m.m);
    }
    // The top of stack `index` (matrices[count - 1], computed even when empty).
    const Matrix4* Top(int index) const { return stacks[index].matrices.data() + (stacks[index].count - 1); }
    void PopMatrix(int index) {
        if (stacks[index].count != 0) --stacks[index].count;
    }

    // The viewport the projections map clip space to: the screen's size.
    void SetViewportSize(float width, float height);

    // The positions of `blocks` packets of four projected with `m` (a screen matrix: stack 1's top) into
    // `projected`: x, y and z divided by w, and 1 / w (each row's sum adding the translation last).
    static void Project(const Matrix4& m, const VertexPacket* positions, Vector4* projected, int blocks);
    // The positions of `blocks` packets of four to clip space with `m` (ToClip) into `projected`, an outcode
    // each; returns the verdict (ClipVerdict). On screen (0) they are taken to the screen (ToScreen).
    ClipResult ProjectClipped(const Matrix4& m, const VertexPacket* positions, Vector4* projected, std::uint8_t* outcodes,
                       int blocks) const;
    // The same for `count` ribbon points: Project's division without clipping (with ToClip's sums), and
    // ProjectClipped's.
    static void ProjectPoints(const Matrix4& m, const Vector3* points, Vector4* projected, int count);
    ClipResult ProjectPointsClipped(const Matrix4& m, const Vector3* points, Vector4* projected, std::uint8_t* outcodes,
                             int count) const;
    // `count` screen-facing sprites, their centres to the screen with `m` (the clip stack's top), as four
    // corners each (positions only; `size` the half side at w 1).
    void ProjectSprites(const Matrix4& m, const SpritePoint* points, TLVertex* corners, int count) const;
    // A clip-space point on the screen: x right and y down from the top left corner, z / w, and 1 / w.
    Vector4 ToScreen(const Vector4& p) const;
    // ToScreen on the first 16 bytes (x, y, z, w) of `count` vertices.
    void ToScreen(TLVertex* vertices, int count) const;
    // The distance fog (render::RenderState::distanceFog) of `count` screen vertices by their depth (from 1 / w):
    // the specular's alpha, the fog factor, lowered to the share of their colour kept.
    void FogVertices(TLVertex* vertices, int count) const;

    // Counted (RendererBase): holders drop a reference, and the last one deletes it.

    MatrixStack stacks[16];     // (models.md: stacks 0-2, 5-10 and 14 are in use)
    float halfWidth = 0.0f;     // the viewport's half size (SetViewportSize)
    float halfHeight = 0.0f;
};

// Viewport matrix for a screen rectangle {left, top, right, bottom}: scale and offset by the half extents,
// y flipped.
Matrix4* ViewportMatrix(Matrix4* out, const Rect* rect);
// Perspective projection with no far plane; `fov` in radians (full angle). The depth z / w is 1 - zNear / distance:
// 0 at the near plane, below 1 at any distance (renderer.md, "The view's depth").
Matrix4* ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear);

namespace view {

// The view matrix of a camera at `position` turned by `rotation` (the world's draw passes the pose it shows,
// camera::DrawnView): rows are the rotation's X, Y and Z axes, column 3 minus the position projected on each.
Matrix4 ViewMatrix(const Vector3& position, const Quaternion& rotation);

} // namespace view
