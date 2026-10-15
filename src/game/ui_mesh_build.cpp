// The interface sprites' shape builders (005E8450 framed rectangle, 005E8C60 disc, 005E91F0 ring).
// See ui_mesh_build.h and docs/notes/ui.md ("Sprite shapes").
//
// Floating point: the rectangle's values are sums and products of floats and small integers kept
// in the original's order (precise floating point; values the original stores as float go through
// Store). The circle points use FCOS/FSIN of an angle the original computes on the FPU stack and
// never stores; they are computed by the same instruction sequence in inline assembly, so they
// round the same way whatever the FPU's precision. Constants are read from the exe's memory.
#include "ui_mesh_build.h"

#include "lib/containers.h"
#include "ei/hook.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

constexpr std::uintptr_t kPi = 0x00740154;          // float pi
constexpr std::uintptr_t kTexel = 0x0073EEB8;       // 1 / 256
constexpr std::uintptr_t kHalfTexel = 0x0073CC90;   // 1 / 512
constexpr std::uintptr_t kOne = 0x0073B84C;
constexpr std::uintptr_t kHalf = 0x0073B86C;
constexpr std::uintptr_t kLayoutX = 0x00740164;     // 2 / 1600
constexpr std::uintptr_t kLayoutY = 0x0074015C;     // 1 / 400
constexpr std::uintptr_t kThreeQuarters = 0x0073C524;
constexpr std::uintptr_t kView = 0x00740158;        // 0.48157 (the interface camera's half view)

// The aligned arrays inside the sprite: the 0x40-byte normal packets (normals_; SetSize 0044F960),
// the two colour arrays (colorsA_, colorsB_; 005A6E70) and the 16-bit indices (+0x200; SetSize
// 0044FA90, SetAtGrow 00452FC0). All are lib::AlignedArray instantiations (aligned_arrays.cpp).
using IndexArray = lib::AlignedArray<std::uint16_t>;

// The common start: one normal packet (four 0s, four 0s, four 1s, four 1s) and the two 4-entry arrays.
void ResetVertexColours(CUiMesh* mesh) {
    mesh->normals_.SetSize(1, -1);
    mesh->colorsA_.SetSize(4, -1);
    mesh->colorsB_.SetSize(4, -1);
    UiQuadBlock& block = mesh->normals_[0];
    for (int i = 0; i < 4; ++i) {
        block.x[i] = 0.0f;
        block.y[i] = 0.0f;
        block.z[i] = 1.0f;
        block.w[i] = 1.0f;
    }
}

// A vertex record: the position it uses, 0, and the texture coordinates' index.
void SetVertex(CUiMesh* mesh, int index, int position, int uv) {
    UiMeshVertex& v = mesh->vertices_[index];
    v.position = static_cast<std::int16_t>(position);
    v.unknown_2 = 0;
    v.frame = static_cast<std::int16_t>(uv);
}

// Appends index `value` (the array's SetSize to size + 1, then the store, or its SetAtGrow; the
// original mixes both).
void AppendBySize(IndexArray* indices, CUiMesh* mesh, int value) {
    int at = mesh->indexCount_;
    indices->SetSize(at + 1, -1);
    mesh->indices_[at] = static_cast<std::uint16_t>(value);
}
void AppendByGrow(IndexArray* indices, CUiMesh* mesh, int value) { indices->SetAtGrow(mesh->indexCount_, static_cast<std::uint16_t>(value)); }

// Half a texel towards the texture's inside on every coordinate, then the arrays to the renderer.
void NudgeAndCommit(CUiMesh* mesh) {
    double half = D(GameFloat(kHalfTexel));
    for (int i = 0; i < mesh->uvCount_; ++i) {
        mesh->uvs_[2 * i] = Store(D(mesh->uvs_[2 * i]) + half);
        mesh->uvs_[2 * i + 1] = Store(D(mesh->uvs_[2 * i + 1]) - half);
    }
    mesh->Commit();
}

// The scale every builder sets: 0.48157 * depth * scale * 0.0025, in the three scale fields.
void SetBuildScale(CUiMesh* mesh, float depth, float scale) {
    float s = Store(D(GameFloat(kView)) * D(depth) * D(scale) * D(GameFloat(kLayoutY)));
    mesh->ScaleX() = s;
    mesh->ScaleY() = s;
    mesh->ScaleZ() = s;
}

// The position from a layout point: ((x * 2/1600 - 1) * 0.48157 * depth, (y / 400 - 0.75) *
// 0.48157 * depth, depth) (the rectangle builder adds the two edges first).
void SetPosition(CUiMesh* mesh, double x, double y, float depth) {
    mesh->position_.x = Store((x - D(GameFloat(kOne))) * D(GameFloat(kView)) * D(depth));
    mesh->position_.y = Store((y - D(GameFloat(kThreeQuarters))) * D(GameFloat(kView)) * D(depth));
    std::memcpy(&mesh->position_.z, &depth, 4);
}

// --- Points on a circle, as the original computes them on the FPU ------------------------------------
// The angle of point j of n is (j * pi + j * pi) / n (FILD, FMUL float pi, FADD ST0,ST0, FDIV of
// the FILD'd n), never stored.

// cos or sin of the angle, times the float at `factor`, divided by the float at `divisor`; stored as float.
float CircleScaled(int j, int n, bool sine, const float* factor, const float* divisor) {
    float result;
    __asm {
        fild n
        fild j
        fmul dword ptr ds:[0x00740154]
        fadd st(0), st(0)
        fdiv st(0), st(1)
        fstp st(1)
        cmp sine, 0
        jne take_sine
        fcos
        jmp scaled
    take_sine:
        fsin
    scaled:
        mov eax, factor
        fmul dword ptr [eax]
        mov eax, divisor
        fdiv dword ptr [eax]
        fstp result
    }
    return result;
}
// The same with (the float at `r` - the float at `w`) as the factor, computed on the FPU.
float CircleScaledInner(int j, int n, bool sine, const float* r, const float* w, const float* divisor) {
    float result;
    __asm {
        mov eax, r
        fld dword ptr [eax]
        mov eax, w
        fsub dword ptr [eax]
        fild n
        fild j
        fmul dword ptr ds:[0x00740154]
        fadd st(0), st(0)
        fdiv st(0), st(1)
        fstp st(1)
        cmp sine, 0
        jne take_sine
        fcos
        jmp scaled
    take_sine:
        fsin
    scaled:
        fmul st(0), st(1)
        fstp st(1)
        mov eax, divisor
        fdiv dword ptr [eax]
        fstp result
    }
    return result;
}
// A texture coordinate on the circle of radius R around c (integers): (cos * R + c) / 256, or for v
// 1 - (sin * R + c) / 256.
float CircleTexel(int j, int n, bool sine, int radius, int centre) {
    float result;
    __asm {
        fild centre
        fild radius
        fild n
        fild j
        fmul dword ptr ds:[0x00740154]
        fadd st(0), st(0)
        fdiv st(0), st(1)
        fstp st(1)
        cmp sine, 0
        jne take_sine
        fcos
        fmul st(0), st(1)
        fadd st(0), st(2)
        fmul dword ptr ds:[0x0073EEB8]
        jmp done
    take_sine:
        fsin
        fmul st(0), st(1)
        fadd st(0), st(2)
        fmul dword ptr ds:[0x0073EEB8]
        fsubr dword ptr ds:[0x0073B84C]
    done:
        fstp result
        fstp st(0)
        fstp st(0)
    }
    return result;
}

} // namespace

// --- 005E8450 -----------------------------------------------------------------------------------------

void CUiMeshBuilder::Framed(float depth, float scale, float left, float top, float right, float bottom, float border,
                            int u0, int v0, int u1, int v1) {
    ResetVertexColours(this);
    // How far the inner edge lies in texels: the border's share of the width (height) of the texels.
    float width = Store(D(right) - D(left));
    float du = Store(static_cast<double>(u1 - u0) * D(border) / D(width));
    float height = Store(D(bottom) - D(top));
    float dv = Store(static_cast<double>(v1 - v0) * D(border) / D(height));

    // Texture coordinates: the outer corners, then the inner ones.
    SetUvCount(8);
    double texel = D(GameFloat(kTexel));
    double one = D(GameFloat(kOne));
    float* uv = uvs_;
    float outerV0 = Store(one - static_cast<double>(v0) * texel);
    float outerV1 = Store(one - static_cast<double>(v1) * texel);
    uv[0] = Store(static_cast<double>(u0) * texel);
    uv[1] = outerV0;
    uv[2] = uv[0];
    uv[3] = outerV1;
    uv[4] = Store(static_cast<double>(u1) * texel);
    uv[5] = outerV0;
    uv[6] = uv[4];
    uv[7] = outerV1;
    float innerV0 = Store(one - (static_cast<double>(static_cast<float>(v0)) + D(dv)) * texel);
    uv[8] = Store((static_cast<double>(u0) + D(du)) * texel);
    uv[9] = innerV0;
    uv[10] = uv[8];
    uv[11] = Store(one - (static_cast<double>(static_cast<float>(v1)) - D(dv)) * texel);
    uv[12] = Store((static_cast<double>(static_cast<float>(u1)) - D(du)) * texel);
    uv[13] = innerV0;
    uv[14] = uv[12];
    uv[15] = uv[11];

    // The frame's eight triangles between the outer (0-3) and inner (4-7) corners.
    auto* indices = &Indices();
    indices->SetSize(0, -1);
    static const int kFrame[24] = {0, 4, 2, 4, 6, 2, 2, 6, 3, 6, 7, 3, 3, 7, 1, 7, 5, 1, 1, 5, 0, 5, 4, 0};
    for (int index : kFrame) AppendByGrow(indices, this, index);

    SetVertexCount(8);
    for (int i = 0; i < 8; ++i) SetVertex(this, i, i, i);

    // Two quads: the outer corners and the inner ones, centred, in sprite units.
    SetQuadCount(2);
    for (int q = 0; q < 2; ++q) {
        for (int i = 0; i < 4; ++i) {
            reinterpret_cast<float*>(&quads_[q])[8 + i] = 0.0f;
            reinterpret_cast<float*>(&quads_[q])[12 + i] = 1.0f;
        }
    }
    double half = D(GameFloat(kHalf));
    double halfWidth = D(width) * half;
    double outerX = halfWidth / D(scale);
    double halfHeight = D(height) * half;
    float outerY = Store(halfHeight / D(scale));
    float innerX = Store((D(Store(halfWidth)) - D(border)) / D(scale));
    float innerY = Store((halfHeight - D(border)) / D(scale));
    UiQuadBlock& outer = quads_[0];
    outer.x[0] = Store(-outerX);
    outer.y[0] = -outerY;
    outer.x[1] = Store(-outerX);
    outer.y[1] = outerY;
    outer.x[2] = Store(outerX);
    outer.y[2] = -outerY;
    outer.x[3] = Store(outerX);
    outer.y[3] = outerY;
    UiQuadBlock& inner = quads_[1];
    inner.x[0] = -innerX;
    inner.y[0] = -innerY;
    inner.x[1] = -innerX;
    inner.y[1] = innerY;
    inner.x[2] = innerX;
    inner.y[2] = -innerY;
    inner.x[3] = innerX;
    inner.y[3] = innerY;

    // Placed at the rectangle's centre.
    double layoutX = D(GameFloat(kLayoutX));
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
        uvs_[2 * i] = CircleTexel(i, count, false, uvRadius, u);
        uvs_[2 * i + 1] = CircleTexel(i, count, true, uvRadius, v);
    }
    // A fan around point 0.
    auto* indices = &Indices();
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
            UiQuadBlock& block = quads_[q];
            block.x[i] = CircleScaled(j, count, false, &radius, &scale);
            block.y[i] = CircleScaled(j, count, true, &radius, &scale);
            reinterpret_cast<float*>(&block)[8 + i] = 0.0f;
            reinterpret_cast<float*>(&block)[12 + i] = 1.0f;
        }
    }
    // Placed by the sprite's own Place (Rebirth-patched), scaled through 005E7060.
    Place(x, y, depth);
    float s = Store(D(GameFloat(kView)) * D(depth) * D(scale) * D(GameFloat(kLayoutY)));
    reinterpret_cast<void(__thiscall*)(CUiMesh*, float)>(0x005E7060)(this, s);
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
    vertices_[count * 4 - 2].position = 0;
    vertices_[count * 4 - 1].position = 1;

    SetUvCount(4);
    double texel = D(GameFloat(kTexel));
    double one = D(GameFloat(kOne));
    float outerV0 = Store(one - static_cast<double>(v0) * texel);
    uvs_[0] = Store(static_cast<double>(u0) * texel);
    uvs_[1] = outerV0;
    uvs_[2] = uvs_[0];
    uvs_[3] = Store(one - static_cast<double>(v1) * texel);
    uvs_[4] = Store(static_cast<double>(u1) * texel);
    uvs_[5] = outerV0;
    uvs_[6] = uvs_[4];
    uvs_[7] = uvs_[3];

    // Two triangles per segment.
    auto* indices = &Indices();
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
        UiQuadBlock& block = quads_[q];
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
    double layoutY = D(GameFloat(kLayoutY));
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
