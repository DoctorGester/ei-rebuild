#pragma once

// The sky dome (A-S7): CSkyObject (004D9110-004D9360, 0x230 bytes, figure class 0x44's constructor
// in the figure factory 005AA8E0) is a figure part mesh (CFigureMesh, figure_mesh.h) with one more
// aligned array, and CSky (004D9680-004D9DE0), the world view's holder that lights and draws it.
// sky_object.cpp. A real virtual class: the compiler makes its vtable (C19; game.exe's 0073D5B8 is
// retired). Names from GetClassName ("CSkyObject"); the rest provisional.

#include "core_transform.h"
#include "figure_mesh.h"   // CFigureMesh
#include "model.h"         // Matrix4, CSceneRenderer

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// A light colour as the sky gets it (by value): red, green, blue (0..1) and a fourth value.
struct SkyColour {
    float r, g, b, a;
};
static_assert(sizeof(SkyColour) == 16);

class CSkyObject : public CFigureMesh {
public:
    // 004D9110. The figure's constructor (CFigureMesh's: in the original 005AABC0's work with its
    // vtable 0073C224 and its Create 0059F210), the haze empty, then the sky's Create (slot 2,
    // 005A6C70: the same CreateMesh).
    CSkyObject();
    // 004D92B0. The mesh's arrays and the haze freed (last first), then the figure's destructor
    // (CFigureMesh's: 0059F030's work).
    ~CSkyObject() override;
    // The deleting destructor (slot 0, 004D9290) frees into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    // The vtable (game.exe's 0073D5B8, retired; the class 0x42 mesh's 0073C1C4 but for 0, 11 and
    // 12). The slots the sky does not override are CTransformObject's (1, 4-6, 8-10, 13, 14).
    int Create() override;       // 2: 005A6C70 (CreateMesh)
    void Destroy() override;     // 3: 005A6C80 (ClearMesh)
    const char* GetName() override;        // 7: 00453440 (the name at +0x120)
    const char* GetClassName() override;   // 11: 004D9270 "CSkyObject" (0078DB14)
    int GetClassId() override;             // 12: 004D9280 (0x20)
    // 15: 0059F510 SetPose. 16 / 17: 0059F710 / 0059F8F0, the mesh written to / read from an MFC
    // CFile (SaveToFile / LoadFromFile).
    void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition) override;
    void SaveTransform(void* file) override;
    void LoadTransform(void* file) override;
    // The figure mesh's own slots (CFigureObjectVirtuals' 18-21, then 22 / 23), CFigureMesh's bodies.
    virtual void CopyFigure(const CFigureMesh* other);                          // 18: 0059F0F0
    virtual void LoadFromMemory(char** cursor, float s, float t, float u);      // 19: 005A2490
    virtual void LoadFig(char** cursor, float s, float t, float u);             // 20: 005A2500
    virtual void LoadOld(char** cursor, float s, float t, float u);             // 21: 005A3800
    virtual void MirrorX();                                                     // 22: 005A4B30
    virtual void MirrorY();                                                     // 23: 005A4BF0

    // The registrations (C20 retires them): 004D9110 constructs in place (returns this), 004D92B0
    // the destructor, 004D9290 (slot 0) the destructor, then operator delete when bit 0 of `flags`
    // is set (returns this).
    CSkyObject* Construct();
    void Destruct();
    CSkyObject* DeletingDestruct(unsigned flags);

    // 004D9360 (RET 0x34). Lights the vertices. Each vertex's haze (distances_) is recomputed
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
