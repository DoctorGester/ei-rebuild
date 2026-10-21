// The interface sprites' shape builders (005E8450 framed rectangle, 005E8C60 disc, 005E91F0 ring).
// See ui_mesh_build.h and docs/notes/ui.md ("Sprite shapes").
//
// Floating point: the rectangle's values are sums and products of floats and small integers kept in
// the original's order (precise floating point; values the original stores as float are rounded
// with static_cast<float>). The circle points use FCOS/FSIN of an angle the original computes on
// the FPU stack and never stores; they are computed by the same instruction sequence in inline
// assembly, so they round the same way whatever the FPU's precision. Constants are read from the
// exe's memory.
#include "ui_mesh_build.h"

#include "lib/containers.h"
#include "ei/hook.h"
#include "world_data.h"
#include "ei/x87.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// The aligned arrays inside the sprite: the 0x40-byte normal packets (normals_; SetSize 0044F960),
// the two colour arrays (colorsA_, colorsB_; 005A6E70) and the 16-bit indices (+0x200; SetSize
// 0044FA90, SetAtGrow 00452FC0). All are lib::AlignedArray instantiations (aligned_arrays.cpp).
using IndexArray = lib::AlignedArray<std::uint16_t>;

// The common start: one normal packet (four 0s, four 0s, four 1s, four 1s) and the two 4-entry arrays.
void ResetVertexColours(CUiMesh* mesh) {
    mesh->normals_.SetSize(1, -1);
    mesh->colorsA_.SetSize(4, -1);
    mesh->colorsB_.SetSize(4, -1);
    constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
    NormalPacket& block = mesh->normals_[0];
    for (int i = 0; i < 4; ++i) {
        block.w[i] = 0;
        block.w[4 + i] = 0;
        block.w[8 + i] = kOneBits;
        block.w[12 + i] = kOneBits;
    }
}

// A vertex record: the position it uses, 0, and the texture coordinates' index.
void SetVertex(CUiMesh* mesh, int index, int position, int uv) {
    UiMeshVertex& v = mesh->Vertices()[index];
    v.position = static_cast<std::int16_t>(position);
    v.normal = 0;
    v.frame = static_cast<std::int16_t>(uv);
}

// Appends index `value` (the array's SetSize to size + 1, then the store, or its SetAtGrow; the
// original mixes both).
void AppendBySize(IndexArray* indices, CUiMesh* mesh, int value) {
    int at = mesh->indices_.size;
    indices->SetSize(at + 1, -1);
    mesh->indices_[at] = static_cast<std::uint16_t>(value);
}
void AppendByGrow(IndexArray* indices, CUiMesh* mesh, int value) { indices->SetAtGrow(mesh->indices_.size, static_cast<std::uint16_t>(value)); }

// Half a texel towards the texture's inside on every coordinate, then the arrays to the renderer.
void NudgeAndCommit(CUiMesh* mesh) {
    double half = D(worlddata::HalfTexel());
    for (int i = 0; i < mesh->texCoords_.size; ++i) {
        mesh->Texels()[i].u = static_cast<float>(D(mesh->Texels()[i].u) + half);
        mesh->Texels()[i].v = static_cast<float>(D(mesh->Texels()[i].v) - half);
    }
    mesh->RefreshBuffers();
}

// The scale every builder sets: 0.48157 * depth * scale * 0.0025, in the three scale fields.
void SetBuildScale(CUiMesh* mesh, float depth, float scale) {
    float s = static_cast<float>(D(worlddata::InterfaceView()) * D(depth) * D(scale) * D(worlddata::LayoutY()));
    mesh->ScaleX() = s;
    mesh->ScaleY() = s;
    mesh->ScaleZ() = s;
}

// The position from a layout point: ((x * 2/1600 - 1) * 0.48157 * depth, (y / 400 - 0.75) *
// 0.48157 * depth, depth) (the rectangle builder adds the two edges first).
void SetPosition(CUiMesh* mesh, double x, double y, float depth) {
    mesh->localPosition_.x = static_cast<float>((x - D(worlddata::One())) * D(worlddata::InterfaceView()) * D(depth));
    mesh->localPosition_.y = static_cast<float>((y - D(worlddata::ThreeQuarters())) * D(worlddata::InterfaceView()) * D(depth));
    std::memcpy(&mesh->localPosition_.z, &depth, 4);
}

// --- Points on a circle, as the original computes them on the FPU ------------------------------------
// The angle of point j of n is (j * pi + j * pi) / n (FILD, FMUL float pi, FADD ST0,ST0, FDIV of
// the FILD'd n), never stored. The FCOS / FSIN result is used unrounded in the same expression.

double CircleAngle(int j, int n) {
    const double turn = static_cast<double>(j) * 3.1415927f /*00740154*/;
    return (turn + turn) / n;
}

// cos or sin of the angle, times the float at `factor`, divided by the float at `divisor`; stored as float.
float CircleScaled(int j, int n, bool sine, const float* factor, const float* divisor) {
    const double angle = CircleAngle(j, n);
    return static_cast<float>((sine ? x87::fsin(angle) : x87::fcos(angle)) * *factor / *divisor);
}
// The same with (the float at `r` - the float at `w`) as the factor, computed unrounded.
float CircleScaledInner(int j, int n, bool sine, const float* r, const float* w, const float* divisor) {
    const double factor = static_cast<double>(*r) - *w;
    const double angle = CircleAngle(j, n);
    return static_cast<float>((sine ? x87::fsin(angle) : x87::fcos(angle)) * factor / *divisor);
}
// A texture coordinate on the circle of radius R around c (integers): (cos * R + c) / 256, or for v
// 1 - (sin * R + c) / 256 (the scale is the float at 0073EEB8, the one the float at 0073B84C).
float CircleTexel(int j, int n, bool sine, int radius, int centre) {
    const double angle = CircleAngle(j, n);
    const double scale = 0.00390625f /*0073EEB8*/;
    if (!sine) return static_cast<float>((x87::fcos(angle) * radius + centre) * scale);
    return static_cast<float>(1.0f /*0073B84C*/ - (x87::fsin(angle) * radius + centre) * scale);
}

} // namespace

// --- 005E8450 -----------------------------------------------------------------------------------------

void CUiMeshBuilder::Framed(float depth, float scale, float left, float top, float right, float bottom, float border,
                            int u0, int v0, int u1, int v1) {
    ResetVertexColours(this);
    // How far the inner edge lies in texels: the border's share of the width (height) of the texels.
    float width = static_cast<float>(D(right) - D(left));
    float du = static_cast<float>(static_cast<double>(u1 - u0) * D(border) / D(width));
    float height = static_cast<float>(D(bottom) - D(top));
    float dv = static_cast<float>(static_cast<double>(v1 - v0) * D(border) / D(height));

    // Texture coordinates: the outer corners, then the inner ones.
    SetUvCount(8);
    double texel = D(worlddata::Texel());
    double one = D(worlddata::One());
    UiTexel* uv = Texels().data;
    float outerV0 = static_cast<float>(one - static_cast<double>(v0) * texel);
    float outerV1 = static_cast<float>(one - static_cast<double>(v1) * texel);
    uv[0].u = static_cast<float>(static_cast<double>(u0) * texel);
    uv[0].v = outerV0;
    uv[1].u = uv[0].u;
    uv[1].v = outerV1;
    uv[2].u = static_cast<float>(static_cast<double>(u1) * texel);
    uv[2].v = outerV0;
    uv[3].u = uv[2].u;
    uv[3].v = outerV1;
    float innerV0 = static_cast<float>(one - (static_cast<double>(static_cast<float>(v0)) + D(dv)) * texel);
    uv[4].u = static_cast<float>((static_cast<double>(u0) + D(du)) * texel);
    uv[4].v = innerV0;
    uv[5].u = uv[4].u;
    uv[5].v = static_cast<float>(one - (static_cast<double>(static_cast<float>(v1)) - D(dv)) * texel);
    uv[6].u = static_cast<float>((static_cast<double>(static_cast<float>(u1)) - D(du)) * texel);
    uv[6].v = innerV0;
    uv[7].u = uv[6].u;
    uv[7].v = uv[5].v;

    // The frame's eight triangles between the outer (0-3) and inner (4-7) corners.
    auto* indices = &indices_;
    indices->SetSize(0, -1);
    static const int kFrame[24] = {0, 4, 2, 4, 6, 2, 2, 6, 3, 6, 7, 3, 3, 7, 1, 7, 5, 1, 1, 5, 0, 5, 4, 0};
    for (int index : kFrame) AppendByGrow(indices, this, index);

    SetVertexCount(8);
    for (int i = 0; i < 8; ++i) SetVertex(this, i, i, i);

    // Two quads: the outer corners and the inner ones, centred, in sprite units.
    SetQuadCount(2);
    for (int q = 0; q < 2; ++q) {
        for (int i = 0; i < 4; ++i) {
            Quads()[q].z[i] = 0.0f;
            Quads()[q].w[i] = 1.0f;
        }
    }
    double half = D(worlddata::Half());
    double halfWidth = D(width) * half;
    double outerX = halfWidth / D(scale);
    double halfHeight = D(height) * half;
    float outerY = static_cast<float>(halfHeight / D(scale));
    float innerX = static_cast<float>((D(static_cast<float>(halfWidth)) - D(border)) / D(scale));
    float innerY = static_cast<float>((halfHeight - D(border)) / D(scale));
    UiQuadBlock& outer = Quads()[0];
    outer.x[0] = static_cast<float>(-outerX);
    outer.y[0] = -outerY;
    outer.x[1] = static_cast<float>(-outerX);
    outer.y[1] = outerY;
    outer.x[2] = static_cast<float>(outerX);
    outer.y[2] = -outerY;
    outer.x[3] = static_cast<float>(outerX);
    outer.y[3] = outerY;
    UiQuadBlock& inner = Quads()[1];
    inner.x[0] = -innerX;
    inner.y[0] = -innerY;
    inner.x[1] = -innerX;
    inner.y[1] = innerY;
    inner.x[2] = innerX;
    inner.y[2] = -innerY;
    inner.x[3] = innerX;
    inner.y[3] = innerY;

    // Placed at the rectangle's centre.
    double layoutX = D(worlddata::LayoutX());
    SetPosition(this, (D(left) + D(right)) * layoutX, (D(top) + D(bottom)) * layoutX, depth);
    SetBuildScale(this, depth, scale);
    NudgeAndCommit(this);
}
EI_REPLACE_METHOD(0x005E8450, CUiMeshBuilder::Framed);

// --- 005E8C60 -----------------------------------------------------------------------------------------

void CUiMeshBuilder::Disc(float depth, float scale, float x, float y, float radius, int count, int u, int v,
                          int uvRadius) {
    ResetVertexColours(this);
    SetVertexCount(count);
    for (int i = 0; i < count; ++i) SetVertex(this, i, i, i);
    SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        Texels()[i].u = CircleTexel(i, count, false, uvRadius, u);
        Texels()[i].v = CircleTexel(i, count, true, uvRadius, v);
    }
    // A fan around point 0.
    auto* indices = &indices_;
    indices->SetSize(0, -1);
    for (int i = 1; i < count - 1; ++i) {
        AppendByGrow(indices, this, 0);
        AppendByGrow(indices, this, i + 1);
        AppendByGrow(indices, this, i);
    }
    // The points, four per quad (count / 4 quads, rounded towards zero).
    int quadCount = count / 4;
    SetQuadCount(quadCount);
    int j = 0;
    for (int q = 0; q < quadCount; ++q) {
        for (int i = 0; i < 4; ++i, ++j) {
            UiQuadBlock& block = Quads()[q];
            block.x[i] = CircleScaled(j, count, false, &radius, &scale);
            block.y[i] = CircleScaled(j, count, true, &radius, &scale);
            reinterpret_cast<float*>(&block)[8 + i] = 0.0f;
            reinterpret_cast<float*>(&block)[12 + i] = 1.0f;
        }
    }
    // Placed by the sprite's own Place (Rebirth-patched), scaled through 005E7060.
    Place(x, y, depth);
    float s = static_cast<float>(D(worlddata::InterfaceView()) * D(depth) * D(scale) * D(worlddata::LayoutY()));
    ei::Code<void(__thiscall*)(CUiMesh*, float)>(0x005E7060)(this, s);
    NudgeAndCommit(this);
}
EI_REPLACE_METHOD(0x005E8C60, CUiMeshBuilder::Disc);

// --- 005E91F0 -----------------------------------------------------------------------------------------

void CUiMeshBuilder::Ring(float depth, float scale, float x, float y, float radius, int count, float width, int u0,
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
    // (Written even without segments, before the array, as the original.)
    Vertices()[count * 4 - 2].position = 0;
    Vertices()[count * 4 - 1].position = 1;

    SetUvCount(4);
    double texel = D(worlddata::Texel());
    double one = D(worlddata::One());
    float outerV0 = static_cast<float>(one - static_cast<double>(v0) * texel);
    Texels()[0].u = static_cast<float>(static_cast<double>(u0) * texel);
    Texels()[0].v = outerV0;
    Texels()[1].u = Texels()[0].u;
    Texels()[1].v = static_cast<float>(one - static_cast<double>(v1) * texel);
    Texels()[2].u = static_cast<float>(static_cast<double>(u1) * texel);
    Texels()[2].v = outerV0;
    Texels()[3].u = Texels()[2].u;
    Texels()[3].v = Texels()[1].v;

    // Two triangles per segment.
    auto* indices = &indices_;
    indices->SetSize(0, -1);
    for (int i = 0; i < count; ++i) {
        int base = 4 * i;
        AppendBySize(indices, this, base);
        AppendBySize(indices, this, base + 1);
        AppendBySize(indices, this, base + 2);
        AppendByGrow(indices, this, base + 1);
        AppendByGrow(indices, this, base + 3);
        AppendBySize(indices, this, base + 2);
    }

    // count / 2 quads: points j and j + 1 at the outer and the inner radius.
    int quadCount = count / 2;
    SetQuadCount(quadCount);
    int j = 0;
    for (int q = 0; q < quadCount; ++q) {
        UiQuadBlock& block = Quads()[q];
        float* z = reinterpret_cast<float*>(&block) + 8;
        float* w = reinterpret_cast<float*>(&block) + 12;
        block.x[0] = CircleScaled(j, count, false, &radius, &scale);
        block.y[0] = CircleScaled(j, count, true, &radius, &scale);
        z[0] = 0.0f;
        w[0] = 1.0f;
        block.x[1] = CircleScaledInner(j, count, false, &radius, &width, &scale);
        block.y[1] = CircleScaledInner(j, count, true, &radius, &width, &scale);
        z[1] = 0.0f;
        w[1] = 1.0f;
        ++j;
        block.x[2] = CircleScaled(j, count, false, &radius, &scale);
        block.y[2] = CircleScaled(j, count, true, &radius, &scale);
        z[2] = 0.0f;
        w[2] = 1.0f;
        block.x[3] = CircleScaledInner(j, count, false, &radius, &width, &scale);
        block.y[3] = CircleScaledInner(j, count, true, &radius, &width, &scale);
        z[3] = 0.0f;
        w[3] = 1.0f;
        ++j;
    }
    double layoutY = D(worlddata::LayoutY());
    SetPosition(this, D(x) * layoutY, D(y) * layoutY, depth);
    SetBuildScale(this, depth, scale);
    NudgeAndCommit(this);
}
EI_REPLACE_METHOD(0x005E91F0, CUiMeshBuilder::Ring);

// --- 005E9F30 -----------------------------------------------------------------------------------------

void CUiMeshBuilder::ResetColours() { ResetVertexColours(this); }
namespace {
void __fastcall MeshResetColours(CUiMeshBuilder* self) { self->ResetColours(); }
} // namespace
EI_REPLACE(0x005E9F30, MeshResetColours);
