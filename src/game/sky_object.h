#pragma once

// The sky dome: CSkyObject (0x230 bytes, figure class 0x44's constructor
// in the figure factory) is a class 0x42 figure mesh (CPlainFigureMesh, figure_mesh.h) with one more
// aligned array, and CSky, the world view's holder that lights and draws it.
// sky_object.cpp. A real virtual class: the compiler makes its vtable.
// Names from GetClassName ("CSkyObject"); the rest provisional.

#include "core_transform.h"
#include "figure_mesh.h"   // CPlainFigureMesh
#include "model.h"         // Matrix4, CSceneRenderer

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CTexture;   // texture.h

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// A light colour as the sky gets it (by value): red, green, blue (0..1) and a fourth value.
struct SkyColour {
    float r, g, b, a;
};
static_assert(sizeof(SkyColour) == 16);

class CSkyObject : public CPlainFigureMesh {
public:
    // The figure's constructor (CPlainFigureMesh's: the base work with
    // its vtable and Create, then the mesh's arrays empty), the haze empty,
    // then the sky's Create (slot 2: the same CreateMesh).
    CSkyObject();
    // The haze freed, then the mesh's arrays (CPlainFigureMesh's destructor) and the
    // figure's (CFigureMesh's).
    ~CSkyObject() override;

    // The vtable: the class 0x42 mesh's (CPlainFigureMesh) but for
    // 0, 11 and 12.
    const char* GetClassName() override;   // 11: "CSkyObject"
    int GetClassId() override;             // 12: 0x20

    // Lights the vertices. Each vertex's haze (distances_) is recomputed
    // when the vertex count changed: 0 below the horizon (vertex z < 1), else sqrt(-normal z).
    // A vertex without haze gets `ambient` as its diffuse colour (alpha 0) and no specular; the
    // others get it with alpha 0xFF and a specular of alpha haze * 255 and `light` * max(0,
    // normal . direction)^2 * 150. Components truncated (_ftol); `unused` is not read. The colours
    // are the mesh's colorsA_ (diffuse) and colorsB_ (specular), one per vertex.
    void Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour unused);

    // The mesh's fields (CFigureMesh): worldMatrix_ the world matrix CSky::Draw builds, name_
    // "nask0", directory_ emptied by LoadSky, the vertices and normals four per packet.
    lib::AlignedArray<float> distances_;            // +0x220 haze per vertex
};
static_assert(offsetof(CSkyObject, worldMatrix_) == 0x54);
static_assert(offsetof(CSkyObject, colorsA_) == 0x138);
static_assert(offsetof(CSkyObject, colorsB_) == 0x148);
static_assert(offsetof(CSkyObject, vertices_) == 0x1C0);
static_assert(offsetof(CSkyObject, distances_) == 0x220);
static_assert(sizeof(CSkyObject) == 0x230);

class CSceneRenderer;

// The world view's sky.
struct CSky {
    // The constructor (returns this). No object yet; the texture holder loads "sky00" (kind 0x3D); turning.
    CSky* Construct();
    // Unless there is no object: it is placed at the eye lowered by 16, turned
    // about +z by the world time / 3000 when `turns` is set, its world matrix built from both and
    // pushed onto the pipeline's stacks 2 and 1; then the texture bound to stage 0 and the object
    // drawn (renderer slot 14 with alpha 0xFF), and both stacks popped (when not empty).
    void Draw(CSceneRenderer* renderer, const Vector3* eye);
    // The compiler's destructor (from CWorldClient::Destroy): the texture, the
    // material and the object released (slot 4), in that order, each when set.
    void Destruct();

    CSkyObject* object;   // +0x00 null until the zone's sky is loaded
    CEngineObject* material;   // +0x04 counted (type 0x12, by CWorldClient::LoadSky); 0 when constructed
    HeldTexture texture;  // +0x08 texture holder
    int turns;            // +0x0C 1 when constructed

    // Object->Light(...) when there is an object.
    void Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour unused);
};
static_assert(offsetof(CSky, texture) == 0x08 && offsetof(CSky, turns) == 0x0C);
