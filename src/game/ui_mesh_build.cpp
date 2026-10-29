// The interface sprites' shape builders (framed rectangle, disc, ring).
// See ui_mesh_build.h and docs/notes/ui.md ("Sprite shapes").
#include "ui.h"

#include "lib/containers.h"
#include "world_data.h"

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
    int at = std::ssize(mesh->indices);
    indices->resize(at + 1);
    mesh->indices[at] = static_cast<std::uint16_t>(value);
}
void AppendByGrow(std::vector<std::uint16_t>* indices, UiMesh* mesh, int value) { lib::SetAtGrow(*indices, std::ssize(mesh->indices), static_cast<std::uint16_t>(value)); }

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
