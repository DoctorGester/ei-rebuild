// The interface sprites' shape builders (framed rectangle, disc, ring).
// See ui_mesh_build.h and docs/notes/ui.md ("Sprite shapes").
#include "ui.h"

#include "figure_mesh.h"
#include "material.h"
#include "model.h"
#include "ui_widescreen.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "render/render.h"   // SetInterfaceDraw

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// The common start: one normal packet (four 0s, four 0s, four 1s, four 1s) and the two 4-entry arrays.
void ResetVertexColours(UiMesh* mesh) {
    mesh->normals.resize(1);
    mesh->colorsA.resize(4);
    mesh->colorsB.resize(4);
    NormalPacket& block = mesh->normals[0];
    for (int i = 0; i < 4; ++i) {
        block.block[0][i] = 0.0f;
        block.block[1][i] = 0.0f;
        block.block[2][i] = 1.0f;
        block.block[3][i] = 1.0f;
    }
}

// A vertex record: the position it uses, 0, and the texture coordinates' index.
void SetVertex(UiMesh* mesh, int index, int position, int uv) {
    MeshCorner& v = mesh->corners[index];
    v.position = static_cast<std::uint16_t>(position);
    v.normal = 0;
    v.texCoord = static_cast<std::uint16_t>(uv);
}

// Appends index `value` (the array's SetSize to size + 1, then the store, or its SetAtGrow; the
// original mixes both).
void AppendBySize(std::vector<std::uint16_t>* indices, UiMesh* mesh, int value) {
    int at = lib::Count(mesh->indices);
    indices->resize(at + 1);
    mesh->indices[at] = static_cast<std::uint16_t>(value);
}
void AppendByGrow(std::vector<std::uint16_t>* indices, UiMesh* mesh, int value) { lib::SetAtGrow(*indices, lib::Count(mesh->indices), static_cast<std::uint16_t>(value)); }

// Half a texel towards the texture's inside on every coordinate, then the arrays to the renderer.
void NudgeAndCommit(UiMesh* mesh) {
    float half = worlddata::kHalfTexel;
    for (int i = 0; i < std::ssize(mesh->texCoords); ++i) {
        mesh->texCoords[i].u = mesh->texCoords[i].u + half;
        mesh->texCoords[i].v = mesh->texCoords[i].v - half;
    }
    mesh->RefreshBuffers();
}

// The scale every builder sets: 0.48157 * depth * scale * 0.0025, in the three scale fields.
void SetBuildScale(UiMesh* mesh, float depth, float scale) {
    float s = worlddata::kInterfaceView * depth * scale * worlddata::kLayoutY;
    mesh->ScaleX() = s;
    mesh->ScaleY() = s;
    mesh->ScaleZ() = s;
}

// The position from a layout point: ((x * 2/1600 - 1) * 0.48157 * depth, (y / 400 - 0.75) *
// 0.48157 * depth, depth) (the rectangle builder adds the two edges first).
void SetPosition(UiMesh* mesh, float x, float y, float depth) {
    mesh->localPosition.x = (x - 1.0f) * worlddata::kInterfaceView * depth;
    mesh->localPosition.y = (y - worlddata::kThreeQuarters) * worlddata::kInterfaceView * depth;
    mesh->localPosition.z = depth;
}

// --- Points on a circle ------------------------------------------------------------------------------
// The angle of point j of n is (j * pi + j * pi) / n with the float pi.

float CircleAngle(int j, int n) {
    const float turn = j * 3.1415927f;
    return (turn + turn) / n;
}

// cos or sin of the angle, times the float at `factor`, divided by the float at `divisor`; stored as float.
float CircleScaled(int j, int n, bool sine, const float* factor, const float* divisor) {
    const float angle = CircleAngle(j, n);
    return (sine ? std::sin(angle) : std::cos(angle)) * *factor / *divisor;
}
// The same with (the float at `r` - the float at `w`) as the factor.
float CircleScaledInner(int j, int n, bool sine, const float* r, const float* w, const float* divisor) {
    const float factor = (*r) - *w;
    const float angle = CircleAngle(j, n);
    return (sine ? std::sin(angle) : std::cos(angle)) * factor / *divisor;
}
// A texture coordinate on the circle of radius R around c (integers): (cos * R + c) / 256, or for v
// 1 - (sin * R + c) / 256 (the scale is the 1/256 texel constant).
float CircleTexel(int j, int n, bool sine, int radius, int centre) {
    const float angle = CircleAngle(j, n);
    const float scale = 0.00390625f;
    if (!sine) return (std::cos(angle) * radius + centre) * scale;
    return 1.0f - (std::sin(angle) * radius + centre) * scale;
}

} // namespace

void UiMesh::Framed(float depth, float scale, float left, float top, float right, float bottom, float border,
                            int u0, int v0, int u1, int v1) {
    ResetVertexColours(this);
    // How far the inner edge lies in texels: the border's share of the width (height) of the texels.
    float width = right - left;
    float du = (u1 - u0) * border / width;
    float height = bottom - top;
    float dv = (v1 - v0) * border / height;

    // Texture coordinates: the outer corners, then the inner ones.
    SetUvCount(8);
    float texel = worlddata::kTexel;
    float one = 1.0f;
    TexCoord* uv = texCoords.data();
    float outerV0 = one - v0 * texel;
    float outerV1 = one - v1 * texel;
    uv[0].u = u0 * texel;
    uv[0].v = outerV0;
    uv[1].u = uv[0].u;
    uv[1].v = outerV1;
    uv[2].u = u1 * texel;
    uv[2].v = outerV0;
    uv[3].u = uv[2].u;
    uv[3].v = outerV1;
    float innerV0 = one - (v0 + dv) * texel;
    uv[4].u = (u0 + du) * texel;
    uv[4].v = innerV0;
    uv[5].u = uv[4].u;
    uv[5].v = one - (v1 - dv) * texel;
    uv[6].u = (u1 - du) * texel;
    uv[6].v = innerV0;
    uv[7].u = uv[6].u;
    uv[7].v = uv[5].v;

    // The frame's eight triangles between the outer (0-3) and inner (4-7) corners.
    auto* currentIndices = &indices;
    currentIndices->clear();
    static const int kFrame[24] = {0, 4, 2, 4, 6, 2, 2, 6, 3, 6, 7, 3, 3, 7, 1, 7, 5, 1, 1, 5, 0, 5, 4, 0};
    for (int index : kFrame) AppendByGrow(currentIndices, this, index);

    SetVertexCount(8);
    for (int i = 0; i < 8; ++i) SetVertex(this, i, i, i);

    // Two quads: the outer corners and the inner ones, centred, in sprite units.
    SetQuadCount(2);
    for (int q = 0; q < 2; ++q) {
        for (int i = 0; i < 4; ++i) {
            vertices[q].z()[i] = 0.0f;
            vertices[q].w()[i] = 1.0f;
        }
    }
    float half = 0.5f;
    float halfWidth = width * half;
    float outerX = halfWidth / scale;
    float halfHeight = height * half;
    float outerY = halfHeight / scale;
    float innerX = (halfWidth - border) / scale;
    float innerY = (halfHeight - border) / scale;
    VertexPacket& outer = vertices[0];
    outer.x()[0] = -outerX;
    outer.y()[0] = -outerY;
    outer.x()[1] = -outerX;
    outer.y()[1] = outerY;
    outer.x()[2] = outerX;
    outer.y()[2] = -outerY;
    outer.x()[3] = outerX;
    outer.y()[3] = outerY;
    VertexPacket& inner = vertices[1];
    inner.x()[0] = -innerX;
    inner.y()[0] = -innerY;
    inner.x()[1] = -innerX;
    inner.y()[1] = innerY;
    inner.x()[2] = innerX;
    inner.y()[2] = -innerY;
    inner.x()[3] = innerX;
    inner.y()[3] = innerY;

    // Placed at the rectangle's centre.
    float layoutX = worlddata::kLayoutX;
    SetPosition(this, (left + right) * layoutX, (top + bottom) * layoutX, depth);
    SetBuildScale(this, depth, scale);
    NudgeAndCommit(this);
}

void UiMesh::Disc(float depth, float scale, float x, float y, float outerRadius, int count, int u, int v,
                          int uvRadius) {
    ResetVertexColours(this);
    SetVertexCount(count);
    for (int i = 0; i < count; ++i) SetVertex(this, i, i, i);
    SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        texCoords[i].u = CircleTexel(i, count, false, uvRadius, u);
        texCoords[i].v = CircleTexel(i, count, true, uvRadius, v);
    }
    // A fan around point 0.
    auto* currentIndices = &indices;
    currentIndices->clear();
    for (int i = 1; i < count - 1; ++i) {
        AppendByGrow(currentIndices, this, 0);
        AppendByGrow(currentIndices, this, i + 1);
        AppendByGrow(currentIndices, this, i);
    }
    // The points, four per quad (count / 4 quads, rounded towards zero).
    int quadCount = count / 4;
    SetQuadCount(quadCount);
    int j = 0;
    for (int q = 0; q < quadCount; ++q) {
        for (int i = 0; i < 4; ++i, ++j) {
            VertexPacket& block = vertices[q];
            block.x()[i] = CircleScaled(j, count, false, &outerRadius, &scale);
            block.y()[i] = CircleScaled(j, count, true, &outerRadius, &scale);
            block.z()[i] = 0.0f;
            block.w()[i] = 1.0f;
        }
    }
    // Placed by the sprite's own Place (Rebirth-patched), scaled through the sprite's scale.
    Place(x, y, depth);
    float s = worlddata::kInterfaceView * depth * scale * worlddata::kLayoutY;
    SetScale(s);
    NudgeAndCommit(this);
}

void UiMesh::Ring(float depth, float scale, float x, float y, float outerRadius, int count, float width, int u0,
                          int v0, int u1, int v1) {
    ResetVertexColours(this);
    // Four vertices per segment: outer and inner point of its start and of its end (texture corners
    // 0-3); the last segment ends on the first one's points.
    SetVertexCount(count * 4);
    for (int i = 0; i < count; ++i) {
        SetVertex(this, 4 * i, 2 * i, 0);
        SetVertex(this, 4 * i + 1, 2 * i + 1, 1);
        SetVertex(this, 4 * i + 2, 2 * i + 2, 2);
        SetVertex(this, 4 * i + 3, 2 * i + 3, 3);
    }
    // (Written even without segments, before the array, like the original.)
    corners[count * 4 - 2].position = 0;
    corners[count * 4 - 1].position = 1;

    SetUvCount(4);
    float texel = worlddata::kTexel;
    float one = 1.0f;
    float outerV0 = one - v0 * texel;
    texCoords[0].u = u0 * texel;
    texCoords[0].v = outerV0;
    texCoords[1].u = texCoords[0].u;
    texCoords[1].v = one - v1 * texel;
    texCoords[2].u = u1 * texel;
    texCoords[2].v = outerV0;
    texCoords[3].u = texCoords[2].u;
    texCoords[3].v = texCoords[1].v;

    // Two triangles per segment.
    auto* currentIndices = &indices;
    currentIndices->clear();
    for (int i = 0; i < count; ++i) {
        int base = 4 * i;
        AppendBySize(currentIndices, this, base);
        AppendBySize(currentIndices, this, base + 1);
        AppendBySize(currentIndices, this, base + 2);
        AppendByGrow(currentIndices, this, base + 1);
        AppendByGrow(currentIndices, this, base + 3);
        AppendBySize(currentIndices, this, base + 2);
    }

    // count / 2 quads: points j and j + 1 at the outer and the inner radius.
    int quadCount = count / 2;
    SetQuadCount(quadCount);
    int j = 0;
    for (int q = 0; q < quadCount; ++q) {
        VertexPacket& block = vertices[q];
        float* z = block.z();
        float* w = block.w();
        block.x()[0] = CircleScaled(j, count, false, &outerRadius, &scale);
        block.y()[0] = CircleScaled(j, count, true, &outerRadius, &scale);
        z[0] = 0.0f;
        w[0] = 1.0f;
        block.x()[1] = CircleScaledInner(j, count, false, &outerRadius, &width, &scale);
        block.y()[1] = CircleScaledInner(j, count, true, &outerRadius, &width, &scale);
        z[1] = 0.0f;
        w[1] = 1.0f;
        ++j;
        block.x()[2] = CircleScaled(j, count, false, &outerRadius, &scale);
        block.y()[2] = CircleScaled(j, count, true, &outerRadius, &scale);
        z[2] = 0.0f;
        w[2] = 1.0f;
        block.x()[3] = CircleScaledInner(j, count, false, &outerRadius, &width, &scale);
        block.y()[3] = CircleScaledInner(j, count, true, &outerRadius, &width, &scale);
        z[3] = 0.0f;
        w[3] = 1.0f;
        ++j;
    }
    float layoutY = worlddata::kLayoutY;
    SetPosition(this, x * layoutY, y * layoutY, depth);
    SetBuildScale(this, depth, scale);
    NudgeAndCommit(this);
}

void UiMesh::ResetColours() { ResetVertexColours(this); }

void UiMesh::SetScaleYBits(std::uint32_t bits) { scaleMatrix.m[5] = std::bit_cast<float>(bits); }

// --- Draw --------------------------------------------------------------------------------------------

// An interface sprite (UiMesh, ui.h): its Create, its draw (called by the window base's draw and the panels
// that draw single sprites), its constructor, destructor and virtuals. docs/notes/ui.md, section 9.

namespace {

constexpr float kColourScale = 255.0f;
constexpr float kOne = 1.0f;
constexpr int kStackWorld = 1;           // the transform pipeline's stacks the sprite is pushed on
constexpr int kStackView = 2;

// The identity orientation (w 1, x y z 0): a sprite's first orientation, and what the draw hands DrawFigure
// as its second argument (not read on that path).
constexpr Quaternion kIdentityOrientation = /*007C7CD8*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
constexpr float kSpriteDepth = 10.0f;
constexpr std::uint32_t kOpaqueBlack = 0xFF000000;
constexpr const char* kClassName = "CI3DFigure";
constexpr const char* kSetTextureFailed = "CI3DFigure::Draw.\n SetTexture failed.\n";
constexpr const char* kDrawFailed = "CI3DFigure::Draw.\n CFigure::Draw failed.\n";

// One component of the tint as a colour byte: the product stored as a float, then rounded to the nearest
// integer.
std::uint32_t ColourByte(float factor) { return static_cast<std::uint32_t>(std::lround(factor * kColourScale)); }

// A stack's top dropped, unless it is empty.
void Pop(MatrixStack& stack) {
    if (stack.count != 0) --stack.count;
}

} // namespace

UiMesh::Placement UiMesh::GetPlacement() const {
    Placement placement;
    placement.x = (localPosition.x / localPosition.z / 0.48157462f + 1.0f) * 400.0f;
    placement.y = localPosition.y / localPosition.z / 0.48157462f * 400.0f + 300.0f;
    placement.depth = localPosition.z;
    return placement;
}

// The layout's 1 / 400 and 0.75 with the constants above. Rebirth's WideScreen replaces it (addon's
// 10023151): x moved by the panel offset first (an integer add; 0 outside its panel builders,
// ui_widescreen.h).
void UiMesh::Place(float x, float y, float depth) {
    x += static_cast<float>(widescreen::PanelOffset());
    float placedX = (x * 0.0025f - 1.0f) * 0.48157462f * depth;
    float placedY = (y * 0.0025f - 0.75f) * 0.48157462f * depth;
    localPosition.x = placedX;
    localPosition.y = placedY;
    localPosition.z = depth;
}

// Nothing unless visible. Binds the texture to stage 0 (failure is fatal), builds the placement from the
// orientation and position, pushes it on the view and world stacks, then the scale matrix on both (the view
// stack's push inlined), draws the sprite as a figure in its tint (the renderer's colour put back after) with
// its material, and drops both pushes from both stacks.
void UiMesh::Draw(SceneRenderer* renderer) {
    if (!visible) return;
    TransformPipeline* pipeline = renderer->pipeline;
    if (!renderer->SetTexture(0, texture)) {
        game::FatalError(kSetTextureFailed);
        return;
    }

    localRotation.ToMatrix(worldMatrix.m);
    worldMatrix.m[3] = localPosition.x;
    worldMatrix.m[7] = localPosition.y;
    worldMatrix.m[11] = localPosition.z;
    worldMatrix.m[12] = 0.0f;
    worldMatrix.m[13] = 0.0f;
    worldMatrix.m[14] = 0.0f;
    worldMatrix.m[15] = kOne;
    Matrix4 placement;
    std::memcpy(&placement, &worldMatrix, sizeof(placement));
    pipeline->PushMatrix(placement.m, kStackView);
    pipeline->PushMatrix(placement.m, kStackWorld);
    pipeline->PushMatrix(scaleMatrix.m, kStackView);
    pipeline->PushMatrix(scaleMatrix.m, kStackWorld);

    const std::uint32_t saved = renderer->colour;
    const std::uint32_t red = ColourByte(tint[0]);
    const std::uint32_t green = ColourByte(tint[1]);
    const std::uint32_t blue = ColourByte(tint[2]);
    renderer->SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
    render::SetInterfaceDraw(true);   // the new renderer streams interface sprites
    const int drawn = renderer->DrawFigure(this, static_cast<Material*>(material), 0xFF, nullptr);
    render::SetInterfaceDraw(false);
    renderer->SetColour(saved);
    if (!drawn) {
        game::FatalError(kDrawFailed);
        return;
    }

    MatrixStack& view = pipeline->stacks[kStackView];
    MatrixStack& world = pipeline->stacks[kStackWorld];
    Pop(view);
    Pop(world);
    Pop(view);
    Pop(world);
}

// --- The class 0x43's lifetime and slots ---------------------------------------------------

UiMesh::UiMesh() {}
UiMesh::~UiMesh() {}

void UiMesh::Create() {
    CreateMesh();
    material = nullptr;
    texture = nullptr;
    std::memset(&scaleMatrix, 0, sizeof(scaleMatrix));
    scaleMatrix.m[15] = 1.0f;
    scaleMatrix.m[10] = 1.0f;
    scaleMatrix.m[5] = 1.0f;
    scaleMatrix.m[0] = 1.0f;
    tint[0] = 1.0f;
    tint[1] = 1.0f;
    tint[2] = 1.0f;
    tintSpare[0] = 0.0f;
    tintSpare[1] = 0.0f;
    tintSpare[2] = 0.0f;
    localRotation = kIdentityOrientation;
    localPosition.x = 0.0f;
    localPosition.y = 0.0f;
    localPosition.z = kSpriteDepth;
    flatColour = kOpaqueBlack;
    flatSpecular = kOpaqueBlack;
    visible = true;
}

void UiMesh::Destroy() {
    material = nullptr;
    texture = nullptr;
    PlainFigureMesh::Destroy();
}

const char* UiMesh::ClassName() { return kClassName; }
ClassId UiMesh::GetClassId() { return ClassId::UiMesh; }

// --- The arrays' sizes ---------------------------------

void UiMesh::SetQuadCount(int count) {
    vertices.resize(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k) {
            vertices[i].z()[k] = 0.0f;
            vertices[i].w()[k] = 1.0f;
        }
}

void UiMesh::SetVertexCount(int count) {
    corners.resize(count);
    for (int i = 0; i < count; ++i) corners[i].normal = 0;
}

void UiMesh::SetIndexCount(int count) { indices.resize(count); }

void UiMesh::SetUvCount(int count) { texCoords.resize(count); }

// --- Shapes ------------------------------------------------------------------------------------------

// The shapes an interface sprite (UiMesh, ui.h) is given. Rebirth's WideScreen patches each of
// them (one site: the x of the sprite's position moved by its panel offset, ui_widescreen.h), which
// these reproduce for every window. docs/notes/ui.md, section 9.

namespace {

// The layout: half a texel (1 / 512) and a texel (1 / 256) of the 256-wide
// interface textures; 1 / 800 and 1 / 400 of the layout's width; the view's
// half width at depth 1 (0.48157); 0.75, the layout's height over its width.
constexpr float kTexel = 0.00390625f;
constexpr float kHalfTexel = 0.001953125f;
constexpr float kViewHalfWidth = 0.48157462f;

// The two triangles of a quad whose corners are numbered down the left side, then down the right.
constexpr std::uint16_t kQuadCorners[] = {0, 1, 2, 1, 3, 2};

// One packet of normals facing the viewer (0, 0, 1; the fourth row 1) and a colour pair per corner.
void SetFlatNormals(FigureMesh* mesh, int corners) {
    mesh->normals.resize(1);
    mesh->colorsA.resize(corners);
    mesh->colorsB.resize(corners);
    NormalPacket& normal = mesh->normals[0];
    for (int k = 0; k < 4; ++k) {
        normal.block[0][k] = 0.0f;
        normal.block[1][k] = 0.0f;
        normal.block[2][k] = 1.0f;
        normal.block[3][k] = 1.0f;
    }
}

// The four texture corners of the texel rectangle (u0, v0)-(u1, v1): v runs up, so 1 - v / 256.
void SetTexelCorners(UiMesh* sprite, int u0, int v0, int u1, int v1) {
    sprite->texCoords.resize(4);
    TexCoord* texels = sprite->texCoords.data();
    float left = u0 * kTexel;
    float top = 1.0f - v0 * kTexel;
    float bottom = 1.0f - v1 * kTexel;
    float right = u1 * kTexel;
    texels[0].u = left;
    texels[0].v = top;
    texels[1].u = left;
    texels[1].v = bottom;
    texels[2].u = right;
    texels[2].v = top;
    texels[3].u = right;
    texels[3].v = bottom;
}

// Every texture coordinate moved half a texel into its texel.
void InsetTexels(UiMesh* sprite) {
    for (int i = 0; i < std::ssize(sprite->texCoords); ++i) {
        TexCoord* texel = sprite->texCoords.data() + i;
        texel->u = texel->u + kHalfTexel;
        texel->v = texel->v - kHalfTexel;
    }
}

// Corner `corner` of the sprite: point `point` with texture corner `texel`.
void SetCorner(UiMesh* sprite, int corner, int point, int texel) {
    MeshCorner& vertex = sprite->corners[corner];
    vertex.position = static_cast<std::uint16_t>(point);
    vertex.texCoord = static_cast<std::uint16_t>(texel);
    vertex.normal = 0;
}

// The two triangles of the quad whose four corners start at `first`, appended to the indices.
void AddQuadIndices(UiMesh* sprite, int first) {
    for (std::uint16_t corner : kQuadCorners)
        sprite->indices.push_back(static_cast<std::uint16_t>(first + corner));
}

// `count` packets of points on the sprite's plane (z 0, the fourth row 1).
void SetFlatPackets(UiMesh* sprite, int count) {
    sprite->vertices.resize(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k) {
            sprite->vertices[i].z()[k] = 0.0f;
            sprite->vertices[i].w()[k] = 1.0f;
        }
}

// The three scales of a sprite drawn at `depth` in units of `scale`.
void SetLayoutScale(UiMesh* sprite, float depth, float scale) {
    float size = kViewHalfWidth * depth * scale * 0.0025f;
    sprite->ScaleX() = size;
    sprite->ScaleY() = size;
    sprite->ScaleZ() = size;
}

// Rebirth's WideScreen: addon's code at the four sites adds its panel offset (an int, FIADD) to the
// layout x before the scaling: once to a left edge or a point, twice to a sum of two edges.
float MovedX(float x) { return x + widescreen::PanelOffset(); }

} // namespace

void UiMesh::SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0,
                                  int v0, int u1, int v1) {
    SetFlatNormals(this, 4);
    corners.resize(4);
    for (int i = 0; i < 4; ++i) SetCorner(this, i, i, i);
    // A mirrored picture starts a texel further left.
    if (u0 > u1) {
        --u0;
        --u1;
    }
    SetTexelCorners(this, u0, v0, u1, v1);
    indices.clear();
    AddQuadIndices(this, 0);

    SetFlatPackets(this, 1);
    VertexPacket& quad = vertices[0];
    // The rectangle about its centre, in units of `scale`.
    float halfWidth = (right - left) * 0.5f / scale;
    float halfHeight = (bottom - top) * 0.5f / scale;
    quad.x()[0] = -halfWidth;
    quad.y()[0] = -halfHeight;
    quad.x()[1] = -halfWidth;
    quad.y()[1] = halfHeight;
    quad.x()[2] = halfWidth;
    quad.y()[2] = -halfHeight;
    quad.x()[3] = halfWidth;
    quad.y()[3] = halfHeight;
    // The centre in front of the interface camera, as Place puts a layout point.
    float y = ((top + bottom) * 0.00125f - 0.75f) * kViewHalfWidth * depth;
    // WideScreen (addon's handler): both edges moved.
    const float sumX = MovedX(MovedX(left + right));
    float x = (sumX * 0.00125f - 1.0f) * kViewHalfWidth * depth;
    Vector3 centre;
    localPosition = *centre.Construct(x, y, depth);
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    RefreshBuffers();
}

void UiMesh::SetGrid(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1, int columns, int rows) {
    SetFlatNormals(this, 4);
    // The points, row by row from the top left, a cell apart in units of `scale`.
    float stepX = (right - left) / scale / columns;
    float stepY = (bottom - top) / scale / rows;
    const int points = (columns + 1) * (rows + 1);
    const int packets = points / 4 + (points % 4 != 0 ? 1 : 0);
    vertices.resize(packets);
    int column = 0;
    int row = 0;
    for (int i = 0; i < packets; ++i) {
        VertexPacket& packet = vertices[i];
        for (int k = 0; k < 4; ++k) {
            packet.x()[k] = column * stepX;
            packet.y()[k] = row * stepY;
            packet.z()[k] = 0.0f;
            packet.w()[k] = 1.0f;
            // (The points left over in the last packet go on along the last row.)
            if (++column > columns) {
                column = 0;
                if (row < rows) ++row;
            }
        }
    }
    SetTexelCorners(this, u0, v0, u1, v1);
    // Every cell is a quad showing the whole picture.
    corners.resize(columns * rows * 4);
    indices.clear();
    int first = 0;
    int point = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            SetCorner(this, first, point, 0);
            SetCorner(this, first + 1, columns + 1 + point, 1);
            SetCorner(this, first + 2, point + 1, 2);
            SetCorner(this, first + 3, columns + 2 + point, 3);
            AddQuadIndices(this, first);
            first += 4;
            ++point;
        }
        ++point;
    }
    // The top left corner in front of the interface camera.
    // WideScreen (addon's handler): the left edge moved.
    const float gridLeft = MovedX(left);
    localPosition.x = (gridLeft * 0.0025f - 1.0f) * kViewHalfWidth * depth;
    localPosition.y = (top * 0.0025f - 0.75f) * kViewHalfWidth * depth;
    localPosition.z = depth;
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    RefreshBuffers();
}

void UiMesh::SetFrame(float depth, float scale, float left, float top, float right, float bottom, float border,
                              int u0, int v0, int u1, int v1) {
    SetFlatNormals(this, 4);
    // Four quads round the frame; each joins an outer and an inner corner to the next pair, the
    // last one back to the first.
    corners.resize(16);
    for (int side = 0; side < 4; ++side)
        for (int k = 0; k < 4; ++k) SetCorner(this, side * 4 + k, side * 2 + k, k);
    corners[14].position = 0;
    corners[15].position = 1;
    SetTexelCorners(this, u0, v0, u1, v1);
    indices.clear();
    for (int side = 0; side < 4; ++side) AddQuadIndices(this, side * 4);

    SetFlatPackets(this, 2);
    // The outer and the inner rectangle about the centre, in units of `scale`.
    float halfWidth = (right - left) * 0.5f;
    float halfHeight = (bottom - top) * 0.5f;
    float outerX = halfWidth / scale;
    float outerY = halfHeight / scale;
    float innerX = (halfWidth - border) / scale;
    float innerY = (halfHeight - border) / scale;
    VertexPacket& upper = vertices[0];
    upper.x()[0] = -outerX;
    upper.y()[0] = -outerY;
    upper.x()[1] = -innerX;
    upper.y()[1] = -innerY;
    upper.x()[2] = outerX;
    upper.y()[2] = -outerY;
    upper.x()[3] = innerX;
    upper.y()[3] = -innerY;
    VertexPacket& lower = vertices[1];
    lower.x()[0] = outerX;
    lower.y()[0] = outerY;
    lower.x()[1] = innerX;
    lower.y()[1] = innerY;
    lower.x()[2] = -outerX;
    lower.y()[2] = outerY;
    lower.x()[3] = -innerX;
    lower.y()[3] = innerY;
    // WideScreen (addon's handler): both edges moved.
    const float sumX = MovedX(MovedX(left + right));
    localPosition.x = (sumX * 0.00125f - 1.0f) * kViewHalfWidth * depth;
    localPosition.y = ((top + bottom) * 0.00125f - 0.75f) * kViewHalfWidth * depth;
    localPosition.z = depth;
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    RefreshBuffers();
}

void UiMesh::SetTriangle(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2,
                                 float u0, float v0, float u1, float v1, float u2, float v2) {
    SetFlatNormals(this, 4);
    corners.resize(4);
    for (int i = 0; i < 4; ++i) SetCorner(this, i, i, i);
    texCoords.resize(4);
    TexCoord* texels = texCoords.data();
    texels[0].u = u0 * kTexel;
    texels[0].v = 1.0f - v0 * kTexel;
    texels[1].u = u1 * kTexel;
    texels[1].v = 1.0f - v1 * kTexel;
    texels[2].u = u2 * kTexel;
    texels[2].v = 1.0f - v2 * kTexel;
    texels[3].u = 0.0f;
    texels[3].v = 0.0f;
    indices.clear();
    for (std::uint16_t corner = 0; corner < 3; ++corner) indices.push_back(corner);

    SetFlatPackets(this, 1);
    // The first point is the origin; the fourth is not drawn.
    VertexPacket& points = vertices[0];
    points.x()[0] = 0.0f;
    points.y()[0] = 0.0f;
    points.x()[1] = (x1 - x0) / scale;
    points.y()[1] = (y1 - y0) / scale;
    points.x()[2] = (x2 - x0) / scale;
    points.y()[2] = (y2 - y0) / scale;
    points.x()[3] = 0.0f;
    points.y()[3] = 0.0f;
    float y = (y0 * 0.0025f - 0.75f) * kViewHalfWidth * depth;
    // WideScreen (addon's handler): the first point moved.
    const float firstX = MovedX(x0);
    float x = (firstX * 0.0025f - 1.0f) * kViewHalfWidth * depth;
    Vector3 first;
    localPosition = *first.Construct(x, y, depth);
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    RefreshBuffers();
}
