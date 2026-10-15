#pragma once

// The sky dome (A-S7): CSkyObject (004D9110-004D9360, vtable 0073D5B8, 0x230 bytes, figure class
// 0x44's constructor in the figure factory 005AA8E0) is a figure part mesh (CFigure base, 0x1C0
// bytes, vtable 0073C224) with seven aligned arrays, and CSky (004D9680-004D9DE0), the world view's
// holder that lights and draws it. sky_object.cpp. Names from GetClassName ("CSkyObject"); the
// rest provisional.

#include "core_transform.h"
#include "figure_mesh.h"   // VertexPacket
#include "model.h"         // Matrix4, CSceneRenderer

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// A light colour as the sky gets it (by value): red, green, blue (0..1) and a fourth value.
struct SkyColour {
    float r, g, b, a;
};
static_assert(sizeof(SkyColour) == 16);

class CSkyObject : public CTransformObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D5B8);
    static constexpr std::uintptr_t kFigureVtableAddress = std::uintptr_t(0x0073C224);

    // 11: 004D9270 "CSkyObject" (0078DB14). 12: 004D9280 (0x20).
    const char* GetClassName() override;
    int GetClassId() override;
    // The figure mesh's slots (vtable 0073D5B8 as 0073C1C4): 18 0059F0F0 the copy (arguments not
    // declared); 19 005A2490 (RET 0x10) loads the mesh from memory at *cursor once (flag 0x20):
    // "FIG" data through slot 20 (LoadFig), other data through slot 21 (LoadOld).
    virtual void CopyFigure() = 0;
    virtual void LoadFromMemory(char** cursor, float s, float t, float u) = 0;

    // 004D9110. The constructor body: the figure's (its transform base, names, arrays, vtable
    // 0073C224, its Create 0059F210), the sky's arrays empty, vtable 0073D5B8, its Create
    // (slot 2, 005A6C70). Returns this.
    CSkyObject* Construct();
    // 004D92B0. The destructor body: the sky's arrays freed (last first), then the figure's
    // (0059F030). 004D9290: slot 0's scalar deleting destructor (returns this).
    void Destruct();
    CSkyObject* DeletingDestruct(unsigned flags);

    // 004D9360 (RET 0x34). Lights the vertices. Each vertex's haze (distances_) is recomputed
    // when the vertex count changed: 0 below the horizon (vertex z < 1), else sqrt(-normal z).
    // A vertex without haze gets `ambient` as its diffuse colour (alpha 0) and no specular; the
    // others get it with alpha 0xFF and a specular of alpha haze * 255 and `light` * max(0,
    // normal . direction)^2 * 150. Components truncated (_ftol); `unused` is not read.
    void Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour unused);

    Matrix4 matrix_;                              // +0x54 world matrix (CSky::Draw builds it)
    // From here the figure mesh's fields (figure_mesh.h, CFigureMesh), which the figure code reads.
    std::uint8_t drawer_[4];                      // +0x94 CFigureMesh::drawer_
    std::uint32_t figureOwner_;                   // +0x98 CFigureMesh::owner_; 0 when constructed
    std::uint8_t meshFields_[0x120 - 0x9C];       // +0x9C CFigureMesh::buffers_ .. the pose matrix
    mfc::CString name_;                           // +0x120 (CFigure's slot 7)
    mfc::CString directory_;                      // +0x124 CFigureMesh::directory_ (LoadSky empties it)
    lib::AlignedArray<std::uint32_t> offsets_;      // +0x128 CFigureMesh::offsets_
    lib::AlignedArray<std::uint32_t> diffuse_;      // +0x138 a colour per vertex
    lib::AlignedArray<std::uint32_t> specular_;     // +0x148 a colour per vertex
    std::uint8_t meshHeader_[0x188 - 0x158];        // +0x158 CFigureMesh: the header dwords, centre, box, radius
    std::uint8_t bounds_[0x38];                     // +0x188 CFigureMesh::bounds_; cleared when constructed
    lib::AlignedArray<VertexPacket> vertices_;      // +0x1C0 four vertices per packet (x, y, z, w rows)
    lib::AlignedArray<VertexPacket> normals_;       // +0x1D0
    lib::AlignedArray<std::uint32_t> corners_;      // +0x1E0 CFigureMesh::corners_
    lib::AlignedArray<std::uint32_t> words_;        // +0x1F0 CFigureMesh::words_
    lib::AlignedArray<std::uint32_t> indices_;      // +0x200 CFigureMesh::indices_
    lib::AlignedArray<std::uint32_t> texCoords_;    // +0x210 CFigureMesh::texCoords_
    lib::AlignedArray<float> distances_;            // +0x220 haze per vertex
};
static_assert(offsetof(CSkyObject, matrix_) == 0x54);
static_assert(offsetof(CSkyObject, figureOwner_) == 0x98);
static_assert(offsetof(CSkyObject, name_) == 0x120);
static_assert(offsetof(CSkyObject, diffuse_) == 0x138);
static_assert(offsetof(CSkyObject, specular_) == 0x148);
static_assert(offsetof(CSkyObject, bounds_) == 0x188);
static_assert(offsetof(CSkyObject, vertices_) == 0x1C0);
static_assert(offsetof(CSkyObject, words_) == 0x1F0);
static_assert(offsetof(CSkyObject, distances_) == 0x220);
static_assert(sizeof(CSkyObject) == 0x230);

class CSceneRenderer;

// The world view's sky (A-S8: constructor 004D9680, draw 004D96E0).
struct CSky {
    // 004D9680 (returns this). No object yet; the texture holder loads "sky00" (kind 0x3D); turning.
    CSky* Construct();
    // 004D96E0 (RET 8). Unless there is no object: it is placed at the eye lowered by 16, turned
    // about +z by the world time / 3000 when `turns` is set, its world matrix built from both and
    // pushed onto the pipeline's stacks 2 and 1; then the texture bound to stage 0 and the object
    // drawn (renderer slot 14 with alpha 0xFF), and both stacks popped (when not empty).
    void Draw(CSceneRenderer* renderer, const Vector3* eye);
    // 005636A0 (S13; the compiler's destructor, from CWorldClient::Teardown): the texture, the
    // material and the object released (slot 4), in that order, each when set.
    void Destruct();

    CSkyObject* object;   // +0x00 null until the zone's sky is loaded
    CEngineObject* material;   // +0x04 counted (0068E970, type 0x12, by CWorldClient::LoadSky); 0 when constructed
    void* texture;        // +0x08 texture holder
    int turns;            // +0x0C 1 when constructed

    // 004D9DE0 (RET 0x34). object->Light(...) when there is an object.
    void Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour unused);
};
static_assert(offsetof(CSky, texture) == 0x08 && offsetof(CSky, turns) == 0x0C);
