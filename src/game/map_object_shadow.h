#pragma once

// The shadow a 3D object casts on the client (CObject3DClientSpecific, vtable 0073DE90): the
// figure is drawn from the strongest light direction into a 64x64 texture, which is then laid
// onto the ground under the object. Names provisional; see docs/notes/map_objects.md
// ("Shadows").

#include "engine_object.h"

#include <cstddef>
#include <cstdint>

class CObject3D;
struct FigurePart;   // map_object_figures.h

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

    // 0051B7E0 (map_object_draw.cpp). Draws the object (`context` as above; `dirty` receives the screen rectangle the
    // parts flagged 0x40 covered, or null): the object's state is updated (its slot 26) and its
    // animation (0050D450), the lights of 007AF748 within their range + 10 are collected, the
    // object's matrix (and its extra scale +0xF0) pushed, and every part of the two part sets
    // (+0x20 / +0x24, the first with the texture +0xDC) within sqrt(1e9) drawn through the
    // context's slot 13 with those lights. A figure whose parts are all static (part flags 0x10)
    // becomes static (object flags 0x400; +0xEC the time it did, flags_ bit 8). Returns 1, or
    // a fatal error's result.
    int Draw(ShadowDrawContext* context, void* dirty);

    // unit_client_update.cpp (A-U). 0051B6D0 / 0051B6F0. Walk the drawn parts (+0x38): the first
    // (cursor 0; with none the cursor is -1) / the next (the cursor moves on); null at the end.
    FigurePart* FirstDrawnPart();
    FigurePart* NextDrawnPart();

    // unit_client_animation.cpp (A-U2). The pulse of an invisible or stunned unit: the clock
    // (+0xE8) moves on by `frames`, its phase fmod(clock, 30) folded at 15 gives 0.2..0.7.
    // 0051CC20: every drawn part takes it as its alpha. 0051CCD0: every drawn part takes the tint
    // (pulse, 0, 0) and flags_ 0x10.
    void InvisiblePulse(float frames);
    void StunnedPulse(float frames);
    // map_object_client_effects.cpp (A-U3). 0051CCA0: every drawn part's alpha back to 1 (an
    // invisibility ended). 0051CD60: every drawn part's tint cleared and flags_ 0x10 off (a stun
    // ended).
    void ResetPartAlpha();
    void ClearPartTints();

    CObject3D* object;              // +0x1C
    CEngineObject** material;       // +0x20 its object's +0x60 is the shadow colour key; the
                                    //       first of the two part sets Draw walks (+0x24 the
                                    //       second)
    CEngineObject** secondMaterial; // +0x24 its object is the texture of the second part set (a
                                    //       hero's simple skin, CUnitClientSpecific slot 30)
    std::uint8_t unknown_28[0x38 - 0x28];
    FigurePart** drawnParts;        // +0x38 the parts a unit's fade-in reaches (FirstDrawnPart)
    int drawnPartCount;             // +0x3C
    std::uint8_t unknown_40[0x48 - 0x40];
    int drawnPartCursor;            // +0x48 the iteration's index, -1 when there are none
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
    std::uint8_t unknown_A0[0xE8 - 0xA0];
    float pulseClock;               // +0xE8 frames counted by the invisible / stunned pulses
};
static_assert(offsetof(CObject3DClientSpecific, object) == 0x1C);
static_assert(offsetof(CObject3DClientSpecific, vertices) == 0x4C);
static_assert(offsetof(CObject3DClientSpecific, indices) == 0x5C);
static_assert(offsetof(CObject3DClientSpecific, texCoords) == 0x6C);
static_assert(offsetof(CObject3DClientSpecific, texture) == 0x7C);
static_assert(offsetof(CObject3DClientSpecific, bounds) == 0x80);
static_assert(offsetof(CObject3DClientSpecific, sphere) == 0x90);
static_assert(offsetof(CObject3DClientSpecific, pulseClock) == 0xE8);
