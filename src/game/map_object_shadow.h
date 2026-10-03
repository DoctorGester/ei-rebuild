#pragma once

// The shadow a 3D object casts on the client (CObject3DClientSpecific, vtable 0073DE90): the
// figure is drawn from the strongest light direction into a 64x64 texture, which is then laid
// onto the ground under the object. Names provisional; see docs/notes/map_objects.md
// ("Shadows").

#include "engine_object.h"

#include <cstddef>
#include <cstdint>

class CObject3D;

// The draw call's context: the scene (+0x08: its matrix stack) and the device (+0x0C).
struct ShadowDrawContext {
    std::uint8_t unknown_00[8];
    void* scene;                    // +0x08
    void* device;                   // +0x0C
};

// The scene's matrix stack (scene + 0x64: 4x4 float matrices, +0x68: depth).
struct SceneMatrixStack {
    float (*matrices)[16];
    int depth;
};

// A growable array of fixed-size records in 32-byte aligned storage (the block is kept at
// data[-1]): the shadow code's three global work buffers.
struct AlignedRecordBuffer {
    void* data;
    int size;
    int capacity;
    int growBy;
};

class CObject3DClientSpecific : public CEngineObject {
public:
    // 0051CE60. Draws the object's shadow (`context` is the frame's draw context). When shadows
    // are on for its kind (0079BD0C > 0 and the per-kind switch) and the object is shown: unless
    // the shadow is still valid, sums the lights reaching the object into a direction (tilted
    // to at least 20 degrees above the ground), renders the figure's parts from there into the
    // 64x64 shadow texture (+0x7C, created on first use) through the render surface, and
    // projects the texture's square onto the ground (0056C750 into +0x4C/+0x5C/+0x6C). Then the
    // shadow geometry is drawn with the shadow's alpha (units: their controller's +0x78).
    // Returns 1, or a fatal error's result when the texture or the surface cannot be had.
    int DrawShadow(ShadowDrawContext* context);

    CObject3D* object;              // +0x1C
    CEngineObject** material;       // +0x20 its object's +0x60 is the shadow colour key
    std::uint8_t unknown_24[0x4C - 0x24];
    void* vertices;                 // +0x4C \ the projected shadow (0056C750 fills them,
    int vertexCount;                // +0x50 |  004EA4A0 draws them)
    std::uint8_t unknown_54[0x5C - 0x54];
    void* indices;                  // +0x5C
    std::uint8_t unknown_60[0x6C - 0x60];
    void* texCoords;                // +0x6C
    int primitiveCount;             // +0x70
    std::uint8_t unknown_74[0x7C - 0x74];
    CEngineObject* texture;         // +0x7C the shadow texture (counted)
    float bounds[4];                // +0x80 minimum x, y and maximum x, y in the light's view
    std::uint32_t sphere;           // +0x90 the shadow's bounding sphere (the scene object's slot
    float sphereCentre[3];          // +0x94  7), its centre relative to the object
};
static_assert(offsetof(CObject3DClientSpecific, object) == 0x1C);
static_assert(offsetof(CObject3DClientSpecific, vertices) == 0x4C);
static_assert(offsetof(CObject3DClientSpecific, indices) == 0x5C);
static_assert(offsetof(CObject3DClientSpecific, texCoords) == 0x6C);
static_assert(offsetof(CObject3DClientSpecific, texture) == 0x7C);
static_assert(offsetof(CObject3DClientSpecific, bounds) == 0x80);
static_assert(offsetof(CObject3DClientSpecific, sphere) == 0x90);
