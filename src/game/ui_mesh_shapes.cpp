// The shapes an interface sprite (CUiMesh, ui.h) is given. Rebirth's WideScreen patches each of
// them (one site: the x of the sprite's position moved by its panel offset, ui_widescreen.h), which
// these reproduce behind the flag (ticket W2); rewritten callers keep going through the forwarders
// (CUiMesh::SetRectangle, ...). docs/notes/ui.md, section 9.
#include "ui.h"
#include "ui_widescreen.h"

#include "figure_mesh.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace {

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The layout: half a texel (0073CC90, 1 / 512) and a texel (0073EEB8, 1 / 256) of the 256-wide
// interface textures; 1 / 800 (00740164) and 1 / 400 (0074015C) of the layout's width; the view's
// half width at depth 1 (00740158, 0.48157); 0.75 (0073C524), the layout's height over its width.
float Texel() { return GameFloat(0x0073EEB8); }
float HalfTexel() { return GameFloat(0x0073CC90); }
float One() { return GameFloat(0x0073B84C); }
float Half() { return GameFloat(0x0073B86C); }
float ViewHalfWidth() { return GameFloat(0x00740158); }

constexpr std::uint32_t kOneBits = 0x3F800000;
// The two triangles of a quad whose corners are numbered down the left side, then down the right.
constexpr std::uint16_t kQuadCorners[] = {0, 1, 2, 1, 3, 2};

// One packet of normals facing the viewer (0, 0, 1; the fourth row 1) and a colour pair per corner.
void SetFlatNormals(CFigureMesh* mesh, int corners) {
    mesh->normals_.SetSize(1);
    mesh->colorsA_.SetSize(corners);
    mesh->colorsB_.SetSize(corners);
    NormalPacket& normal = mesh->normals_.data[0];
    for (int k = 0; k < 4; ++k) {
        normal.w[k] = 0;
        normal.w[4 + k] = 0;
        normal.w[8 + k] = kOneBits;
        normal.w[12 + k] = kOneBits;
    }
}

// The four texture corners of the texel rectangle (u0, v0)-(u1, v1): v runs up, so 1 - v / 256.
void SetTexelCorners(CUiMesh* sprite, int u0, int v0, int u1, int v1) {
    sprite->Texels().SetSize(4);
    UiTexel* texels = sprite->Texels().data;
    float left = static_cast<float>(u0) * Texel();
    float top = One() - static_cast<float>(v0) * Texel();
    float bottom = One() - static_cast<float>(v1) * Texel();
    float right = static_cast<float>(u1) * Texel();
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
void InsetTexels(CUiMesh* sprite) {
    for (int i = 0; i < sprite->texCoords_.size; ++i) {
        UiTexel* texel = sprite->Texels().data + i;
        texel->u = texel->u + HalfTexel();
        texel->v = texel->v - HalfTexel();
    }
}

// Corner `corner` of the sprite: point `point` with texture corner `texel`.
void SetCorner(CUiMesh* sprite, int corner, int point, int texel) {
    UiMeshVertex& vertex = sprite->Vertices()[corner];
    vertex.position = static_cast<std::int16_t>(point);
    vertex.frame = static_cast<std::int16_t>(texel);
    vertex.normal = 0;
}

// The two triangles of the quad whose four corners start at `first`, appended to the indices.
void AddQuadIndices(CUiMesh* sprite, int first) {
    for (std::uint16_t corner : kQuadCorners)
        sprite->indices_.SetAtGrow(sprite->indices_.size, static_cast<std::uint16_t>(first + corner));
}

// `count` packets of points on the sprite's plane (z 0, the fourth row 1).
void SetFlatPackets(CUiMesh* sprite, int count) {
    sprite->Quads().SetSize(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k) {
            sprite->Quads()[i].z[k] = 0.0f;
            sprite->Quads()[i].w[k] = 1.0f;
        }
}

// The three scales of a sprite drawn at `depth` in units of `scale`.
void SetLayoutScale(CUiMesh* sprite, float depth, float scale) {
    float size = ViewHalfWidth() * depth * scale * GameFloat(0x0074015C);
    sprite->ScaleX() = size;
    sprite->ScaleY() = size;
    sprite->ScaleZ() = size;
}

// Rebirth's WideScreen: addon's code at the four sites adds its panel offset (an int, FIADD) to the
// layout x before the scaling: once to a left edge or a point, twice to a sum of two edges.
float MovedX(float x) { return x + static_cast<float>(widescreen::PanelOffset()); }

} // namespace

void CUiMesh::SetRectangleVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0,
                                  int v0, int u1, int v1) {
    SetFlatNormals(this, 4);
    Vertices().SetSize(4);
    for (int i = 0; i < 4; ++i) SetCorner(this, i, i, i);
    // A mirrored picture starts a texel further left.
    if (u0 > u1) {
        --u0;
        --u1;
    }
    SetTexelCorners(this, u0, v0, u1, v1);
    indices_.SetSize(0);
    AddQuadIndices(this, 0);

    SetFlatPackets(this, 1);
    UiQuadBlock& quad = Quads()[0];
    // The rectangle about its centre, in units of `scale`.
    float halfWidth = (right - left) * Half() / scale;
    float halfHeight = (bottom - top) * Half() / scale;
    quad.x[0] = -halfWidth;
    quad.y[0] = -halfHeight;
    quad.x[1] = -halfWidth;
    quad.y[1] = halfHeight;
    quad.x[2] = halfWidth;
    quad.y[2] = -halfHeight;
    quad.x[3] = halfWidth;
    quad.y[3] = halfHeight;
    // The centre in front of the interface camera, as Place puts a layout point.
    float y = ((top + bottom) * GameFloat(0x00740164) - GameFloat(0x0073C524)) * ViewHalfWidth() * depth;
    // WideScreen (005E75D1, addon's 100231E1): both edges moved.
    float sumX = left + right;
    if (widescreen::Active()) sumX = MovedX(MovedX(sumX));
    float x = (sumX * GameFloat(0x00740164) - One()) * ViewHalfWidth() * depth;
    Vector3 centre;
    localPosition_ = *centre.Construct(x, y, depth);
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    Commit();
}
EI_REPLACE_METHOD(0x005E70E0, CUiMesh::SetRectangleVanilla);

void CUiMesh::SetGridVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1, int columns, int rows) {
    SetFlatNormals(this, 4);
    // The points, row by row from the top left, a cell apart in units of `scale`.
    float stepX = (right - left) / scale / static_cast<float>(columns);
    float stepY = (bottom - top) / scale / static_cast<float>(rows);
    const int points = (columns + 1) * (rows + 1);
    const int packets = points / 4 + (points % 4 != 0 ? 1 : 0);
    Quads().SetSize(packets);
    int column = 0;
    int row = 0;
    for (int i = 0; i < packets; ++i) {
        UiQuadBlock& packet = Quads()[i];
        for (int k = 0; k < 4; ++k) {
            packet.x[k] = static_cast<float>(column) * stepX;
            packet.y[k] = static_cast<float>(row) * stepY;
            packet.z[k] = 0.0f;
            packet.w[k] = 1.0f;
            // (The points left over in the last packet go on along the last row.)
            if (++column > columns) {
                column = 0;
                if (row < rows) ++row;
            }
        }
    }
    SetTexelCorners(this, u0, v0, u1, v1);
    // Every cell is a quad showing the whole picture.
    Vertices().SetSize(columns * rows * 4);
    indices_.SetSize(0);
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
    // WideScreen (005E7C8D, addon's 100231CA): the left edge moved.
    const float gridLeft = widescreen::Active() ? MovedX(left) : left;
    localPosition_.x = (gridLeft * GameFloat(0x0074015C) - One()) * ViewHalfWidth() * depth;
    localPosition_.y = (top * GameFloat(0x0074015C) - GameFloat(0x0073C524)) * ViewHalfWidth() * depth;
    localPosition_.z = depth;
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    Commit();
}
EI_REPLACE_METHOD(0x005E7680, CUiMesh::SetGridVanilla);

void CUiMesh::SetFrameVanilla(float depth, float scale, float left, float top, float right, float bottom, float border,
                              int u0, int v0, int u1, int v1) {
    SetFlatNormals(this, 4);
    // Four quads round the frame; each joins an outer and an inner corner to the next pair, the
    // last one back to the first.
    Vertices().SetSize(16);
    for (int side = 0; side < 4; ++side)
        for (int k = 0; k < 4; ++k) SetCorner(this, side * 4 + k, side * 2 + k, k);
    Vertices()[14].position = 0;
    Vertices()[15].position = 1;
    SetTexelCorners(this, u0, v0, u1, v1);
    indices_.SetSize(0);
    for (int side = 0; side < 4; ++side) AddQuadIndices(this, side * 4);

    SetFlatPackets(this, 2);
    // The outer and the inner rectangle about the centre, in units of `scale`.
    float halfWidth = (right - left) * Half();
    float halfHeight = (bottom - top) * Half();
    float outerX = halfWidth / scale;
    float outerY = halfHeight / scale;
    float innerX = (halfWidth - border) / scale;
    float innerY = (halfHeight - border) / scale;
    UiQuadBlock& upper = Quads()[0];
    upper.x[0] = -outerX;
    upper.y[0] = -outerY;
    upper.x[1] = -innerX;
    upper.y[1] = -innerY;
    upper.x[2] = outerX;
    upper.y[2] = -outerY;
    upper.x[3] = innerX;
    upper.y[3] = -innerY;
    UiQuadBlock& lower = Quads()[1];
    lower.x[0] = outerX;
    lower.y[0] = outerY;
    lower.x[1] = innerX;
    lower.y[1] = innerY;
    lower.x[2] = -outerX;
    lower.y[2] = outerY;
    lower.x[3] = -innerX;
    lower.y[3] = innerY;
    // WideScreen (005E835E, addon's 100231FA): both edges moved.
    float sumX = left + right;
    if (widescreen::Active()) sumX = MovedX(MovedX(sumX));
    localPosition_.x = (sumX * GameFloat(0x00740164) - One()) * ViewHalfWidth() * depth;
    localPosition_.y = ((top + bottom) * GameFloat(0x00740164) - GameFloat(0x0073C524)) * ViewHalfWidth() * depth;
    localPosition_.z = depth;
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    Commit();
}
EI_REPLACE_METHOD(0x005E7D60, CUiMesh::SetFrameVanilla);

void CUiMesh::SetTriangleVanilla(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2,
                                 float u0, float v0, float u1, float v1, float u2, float v2) {
    SetFlatNormals(this, 4);
    Vertices().SetSize(4);
    for (int i = 0; i < 4; ++i) SetCorner(this, i, i, i);
    Texels().SetSize(4);
    UiTexel* texels = Texels().data;
    texels[0].u = u0 * Texel();
    texels[0].v = One() - v0 * Texel();
    texels[1].u = u1 * Texel();
    texels[1].v = One() - v1 * Texel();
    texels[2].u = u2 * Texel();
    texels[2].v = One() - v2 * Texel();
    texels[3].u = 0.0f;
    texels[3].v = 0.0f;
    indices_.SetSize(0);
    for (std::uint16_t corner = 0; corner < 3; ++corner) indices_.SetAtGrow(indices_.size, corner);

    SetFlatPackets(this, 1);
    // The first point is the origin; the fourth is not drawn.
    UiQuadBlock& points = Quads()[0];
    points.x[0] = 0.0f;
    points.y[0] = 0.0f;
    points.x[1] = (x1 - x0) / scale;
    points.y[1] = (y1 - y0) / scale;
    points.x[2] = (x2 - x0) / scale;
    points.y[2] = (y2 - y0) / scale;
    points.x[3] = 0.0f;
    points.y[3] = 0.0f;
    float y = (y0 * GameFloat(0x0074015C) - GameFloat(0x0073C524)) * ViewHalfWidth() * depth;
    // WideScreen (005E9E77, addon's 100231B3): the first point moved.
    const float firstX = widescreen::Active() ? MovedX(x0) : x0;
    float x = (firstX * GameFloat(0x0074015C) - One()) * ViewHalfWidth() * depth;
    Vector3 first;
    localPosition_ = *first.Construct(x, y, depth);
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    Commit();
}
EI_REPLACE_METHOD(0x005E99D0, CUiMesh::SetTriangleVanilla);
