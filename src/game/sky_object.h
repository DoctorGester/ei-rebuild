#pragma once

// The sky dome: SkyObject (0x230 bytes, figure class 0x44's constructor
// in the figure factory) is a class 0x42 figure mesh (PlainFigureMesh, figure_mesh.h) with one more
// aligned array, and Sky, the world view's holder that lights and draws it.
// sky_object.cpp. A real virtual class: the compiler makes its vtable.
// Names from ClassName ("SkyObject"); the rest provisional.

#include "core_transform.h"
#include "figure_mesh.h"   // PlainFigureMesh
#include "model.h"         // Matrix4, SceneRenderer

#include <cstddef>
#include <cstdint>
#include <vector>

// A light colour as the sky gets it (by value): red, green, blue (0..1) and a fourth value.
struct SkyColour {
    float r, g, b, a;
};

class SkyObject : public PlainFigureMesh {
public:
    SkyObject();
    ~SkyObject() override;

    // The vtable: the class 0x42 mesh's (PlainFigureMesh) but for
    // 0, 11 and 12.
    const char* ClassName() override;   // "SkyObject"
    ClassId GetClassId() override;

    // Lights the vertices. Each vertex's haze (distances) is recomputed
    // when the vertex count changed: 0 below the horizon (vertex z < 1), else sqrt(-normal z).
    // A vertex without haze gets `ambient` as its diffuse colour (alpha 0) and no specular; the
    // others get it with alpha 0xFF and a specular of alpha haze * 255 and `light` * max(0,
    // normal . direction)^2 * 150. Components truncated (_ftol). The colours
    // are the mesh's colorsA (diffuse) and colorsB (specular), one per vertex.
    void Light(const float* direction, SkyColour ambient, SkyColour light);

    // The mesh's fields (FigureMesh): worldMatrix the world matrix Sky::Draw builds, name
    // "nask0", directory emptied by LoadSky, the vertices and normals four per packet.
    std::vector<float> distances;             // haze per vertex
};

// The world view's sky.
struct Sky {
    // No object yet; the texture holder loads "sky00" (kind 0x3D); turning.
    Sky();
    // The texture, the material and the object released, in that order, each when set.
    ~Sky();
    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;
    // Unless there is no object: it is placed at the eye lowered by 16, turned
    // about +z by the world time / 3000 when `turns` is set, its world matrix built from both and
    // pushed onto the pipeline's stacks 2 and 1; then the texture bound to stage 0 and the object
    // drawn (the renderer's draw with alpha 0xFF), and both stacks popped (when not empty).
    void Draw(SceneRenderer* renderer, const Vector3& eye);

    Ref<SkyObject> object;   // null until the zone's sky is loaded
    Ref<EngineObject> material;   // type 0x12, by WorldClient::LoadSky
    Ref<CTexture> texture;  // texture holder
    int turns;            // 1 when constructed

    // Object->Light(...) when there is an object.
    void Light(const float* direction, SkyColour ambient, SkyColour light);
};
