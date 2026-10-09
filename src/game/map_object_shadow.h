#pragma once

// The shadow a 3D object casts on the client (CObject3DClientSpecific, vtable 0073DE90): the
// figure is drawn from the strongest light direction into a 64x64 texture, which is then laid
// onto the ground under the object. Names provisional; see docs/notes/map_objects.md
// ("Shadows").

#include "engine_object.h"
#include "maths.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CObject3D;
class CTexture;      // texture.h
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
    // unit_client_wounds.cpp (A-U4). 0051C920: the wounds texture (+0xD8) and its material (+0xDC)
    // are taken off and released (the material deleted at its last reference).
    void HideWounds();
    // 0051C3D0 (RET 4; unit_client_wounds.cpp, A-U10). Shows the wounds texture `name` (kind 6): 0
    // without a first part set (+0x20). The texture is found (00692D30) or made (00691D00) on first
    // use (none: FatalError with the object's class name and id). Unless its .mmp extra block
    // (006928C0) is nonempty and equal to the texture's, the texture is reloaded (loaded flag 8
    // cleared, 006922A0); a failed load drops the texture and the material and returns 0. Without
    // a material, a new one (blended 5 / 6, the renderer helper's preset 4) gets two stages: the
    // first part set's texture, then the wounds texture. Returns 1.
    int ShowWounds(const char* name);
    // object3d_specific_slots.cpp (A-U4). 0051AD20: virtual slot 18 (GetScale; the class declares no
    // virtuals, so the member is registered as it is): x, y, z = figureScale. 0051C970: each drawn part (the cursor +0x48 walks them
    // from 0, up to the first null) registers its material with the world's material manager
    // (world slot 33, 0068EAF0 with the part's type_).
    void ExtraScale(float* x, float* y, float* z);
    void RegisterPartMaterials();

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
    Vector3 sphereCentre;           // +0x94  7), its centre relative to the object
    std::uint8_t unknown_A0[0xD8 - 0xA0];
    CTexture* woundsTexture;        // +0xD8 the wounds texture (counted; 0051C3D0 loads it)
    struct WoundsMaterial* woundsMaterial;  // +0xDC the material drawing it (counted; slot 0 deletes)
    std::uint8_t unknown_E0[0xE8 - 0xE0];
    float pulseClock;               // +0xE8 frames counted by the invisible / stunned pulses
    std::uint32_t touchTime;        // +0xEC the frame time the model was last kept loaded (CollectVisible)
    Vector3 figureScale;            // +0xF0 the figure's extra scale (slot 18, ExtraScale)
};
static_assert(offsetof(CObject3DClientSpecific, object) == 0x1C);
static_assert(offsetof(CObject3DClientSpecific, vertices) == 0x4C);
static_assert(offsetof(CObject3DClientSpecific, indices) == 0x5C);
static_assert(offsetof(CObject3DClientSpecific, texCoords) == 0x6C);
static_assert(offsetof(CObject3DClientSpecific, texture) == 0x7C);
static_assert(offsetof(CObject3DClientSpecific, bounds) == 0x80);
static_assert(offsetof(CObject3DClientSpecific, sphere) == 0x90);
static_assert(offsetof(CObject3DClientSpecific, pulseClock) == 0xE8);
static_assert(offsetof(CObject3DClientSpecific, woundsTexture) == 0xD8);

// A texture stage of a material (0x14 bytes): copied by 0051E970 / 0051E430 (the texture gains a
// reference), destroyed by releasing the texture (0051E3D0) and freeing the name.
struct MaterialStage {
    explicit MaterialStage(CEngineObject* stageTexture) : name("", ""), texture(stageTexture) {
        if (texture) ++texture->refCount_;
    }
    MaterialStage(const MaterialStage& other) : unknown_00(other.unknown_00), name(other.name), texture(other.texture) {
        if (texture) ++texture->refCount_;
    }
    MaterialStage& operator=(const MaterialStage&) = delete;
    ~MaterialStage() {
        if (texture) texture->Release();
    }

    int unknown_00 = 0;             // +0x00
    stlp::string name;              // +0x04 (empty for the wounds stages)
    CEngineObject* texture;         // +0x10 counted
};
static_assert(sizeof(MaterialStage) == 0x14);

// A preset of the renderer's helper (CRenderHelper slot 5; counted, slot 0 deletes).
class CRenderPreset;   // model.h

// The material that draws a wounds texture (0x24 bytes, vtable 0073DF68 over the counted root
// 0073C368; 0051C3D0 makes it, slot 0 0051C710 deletes it).
struct WoundsMaterial {
    static constexpr std::uintptr_t kRootVtableAddress = std::uintptr_t(0x0073C368);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073DF68);

    virtual void DeletingDestructor(unsigned flags);   // 0
    int refCount;                   // +0x04
    std::uint8_t blend;             // +0x08 1
    int sourceBlend;                // +0x0C 5 (D3DBLEND_SRCALPHA)
    int destBlend;                  // +0x10 6 (D3DBLEND_INVSRCALPHA)
    CRenderPreset* preset;          // +0x14 counted (the renderer helper's preset 4)
    stlp::vector<MaterialStage> stages;   // +0x18
};
static_assert(offsetof(WoundsMaterial, preset) == 0x14 && offsetof(WoundsMaterial, stages) == 0x18 &&
              sizeof(WoundsMaterial) == 0x24);
