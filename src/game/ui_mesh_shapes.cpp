// The shapes an interface sprite (UiMesh, ui.h) is given. Rebirth's WideScreen patches each of
// them (one site: the x of the sprite's position moved by its panel offset, ui_widescreen.h), which
// these reproduce behind the flag. docs/notes/ui.md, section 9.
#include "ui.h"
#include "ui_widescreen.h"

#include "figure_mesh.h"

#include "lib/vecmath.h"

#include <cstdint>

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
    float left = static_cast<float>(u0) * kTexel;
    float top = 1.0f - static_cast<float>(v0) * kTexel;
    float bottom = 1.0f - static_cast<float>(v1) * kTexel;
    float right = static_cast<float>(u1) * kTexel;
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
float MovedX(float x) { return x + static_cast<float>(widescreen::PanelOffset()); }

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
    float sumX = left + right;
    if (widescreen::Active()) sumX = MovedX(MovedX(sumX));
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
    float stepX = (right - left) / scale / static_cast<float>(columns);
    float stepY = (bottom - top) / scale / static_cast<float>(rows);
    const int points = (columns + 1) * (rows + 1);
    const int packets = points / 4 + (points % 4 != 0 ? 1 : 0);
    vertices.resize(packets);
    int column = 0;
    int row = 0;
    for (int i = 0; i < packets; ++i) {
        VertexPacket& packet = vertices[i];
        for (int k = 0; k < 4; ++k) {
            packet.x()[k] = static_cast<float>(column) * stepX;
            packet.y()[k] = static_cast<float>(row) * stepY;
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
    const float gridLeft = widescreen::Active() ? MovedX(left) : left;
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
    float sumX = left + right;
    if (widescreen::Active()) sumX = MovedX(MovedX(sumX));
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
    const float firstX = widescreen::Active() ? MovedX(x0) : x0;
    float x = (firstX * 0.0025f - 1.0f) * kViewHalfWidth * depth;
    Vector3 first;
    localPosition = *first.Construct(x, y, depth);
    SetLayoutScale(this, depth, scale);
    InsetTexels(this);
    RefreshBuffers();
}
