#pragma once

// The shadow a 3D object casts on the client (CObject3DClientSpecific, vtable 0073DE90): the
// figure is drawn from the strongest light direction into a 64x64 texture, which is then laid
// onto the ground under the object. Names provisional; see docs/notes/map_objects.md
// ("Shadows").

#include "engine_object.h"
#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CObject3D;
class CTexture;      // texture.h
struct FigurePart;   // map_object_figures.h
struct VertexPacket;    // figure_mesh.h
struct DecalTexCoord;   // ground_decal.h

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

struct tagRECT;   // windows.h
#include "material.h"   // CMaterial
#include "model.h"      // ClipBounds

// The parts of a figure drawn with one material (0x14 bytes; a part set's group).
struct PartGroup {
    // 0051AAA0. Every part released, the array's block freed. Returns 1.
    int ReleaseParts();

    CMaterial* material;                    // +0x00 counted
    lib::AlignedArray<FigurePart*> parts;   // +0x04 counted
};
static_assert(sizeof(PartGroup) == 0x14);

// A part set of a 3D object's client half (0x14 bytes): a texture and the groups of parts drawn
// with it, one per material.
struct TextureParts {
    // 0051A370. The destructor: every group's parts and material released, the group deleted; the
    // array freed. The texture is only forgotten (the half's `textures` holds its reference).
    void Destruct();

    CTexture* texture;                      // +0x00
    lib::AlignedArray<PartGroup*> groups;   // +0x04 owned
};
static_assert(sizeof(TextureParts) == 0x14);

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

    // unit_client_update.cpp (A-U). 0051B6D0 / 0051B6F0. Walk the materials (+0x38): the first
    // (cursor 0; with none the cursor is -1) / the next (the cursor moves on); null at the end.
    CMaterial* FirstMaterial();
    CMaterial* NextMaterial();

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
    // object3d_specific_slots.cpp (A-U11). Slots 22-24, the screen picking (RET 4; a shown object only,
    // flags_ 0x20 of the 3D object): 0051C130 whether the point (x, y) is in the object's dirtyRect;
    // 0051C160 in the screen rectangle of one of its parts with their own mesh (part flags 0x40);
    // 0051C1D0 whether `rect` meets such a part's rectangle (a non-empty intersection).
    int BoundsContainPoint(const long* point);
    int PartsContainPoint(const long* point);
    int PartsIntersectRect(const tagRECT* rect);
    // Slots 16 / 17 (RET 4): 0051B2E0 the object's part named `name` (CObject3D::FindPart), 0051B2D0
    // its part of type `type` (CObject3D::FindPartOfType).
    FigurePart* ObjectPartNamed(const char* name);
    FigurePart* ObjectPartOfType(int type);

    // object3d_client_parts.cpp (A-U12): the part sets, lifetime and skins.
    // 0051AB00. The constructor: the engine object, the base half (vtable 0073DEFC, no object), the
    // empty arrays and names, the shadow sphere zeroed, then the vtable 0073DE90, cursor -1, the
    // shadow texture and the wounds texture / material dropped, bounds and pulse clock 0. Returns this.
    CObject3DClientSpecific* Construct();
    // 0051AD90. The destructor body: DestroyClient, the names, the wounds material and texture and
    // the shadow texture released, the arrays' blocks freed, then the base half's.
    void DestructBody();
    // 0051AD70 (slot 0, RET 4). DestructBody, then operator delete with flags bit 0. Returns this.
    CObject3DClientSpecific* DeleteClient(unsigned flags);
    // 0051AEC0 (slot 2). The cursor -1, flags_ 0, the shadow and wounds textures and the wounds
    // material dropped, bounds and sphere 0, no part sets (not freed), touch time 0, scale 1.
    // Returns 1.
    int CreateClient();
    // 0051AF70 (slot 3). The shadow and wounds textures and the wounds material dropped, both part
    // sets destroyed and deleted, every texture and material released and the arrays emptied; no
    // object, touch time and flags_ 0.
    void DestroyClient();
    // 0051B050 (slot 13, RET 4). `part` joins the group of its material (piece +0x158) in the set
    // of its texture (piece +0x15C), both made on first use (FindOrAddTexture, FindOrAddMaterial);
    // referenced unless already there. 0 when no set has the texture, else 1.
    int PartAdded(FigurePart* part);
    // 0051B210 (slot 14, RET 4). The part named `name` (slot 16) leaves its group and is released.
    // Returns it, or null when there is none or no group holds it.
    FigurePart* RemovePartNamed(const char* name);
    // 0051B160 (slot 15, RET 4). `part` leaves its group and is released: 1, or 0 when no group
    // holds it.
    int PartRemoved(FigurePart* part);
    // 0051B2F0 (RET 4). The texture of kind `kind`: one of `textures`, else a new one of the client
    // world (005655D0) for a new part set, the first for the skin kinds (1, 3, 4, 6-9, 0xB,
    // 0xD-0x10), the second for any other; an existing set only has its texture's kind changed (the
    // first with a debug log line). Objects of type 0x35 flag the texture 0x2000.
    CTexture* FindOrAddTexture(int kind);
    // 0051B470 (RET 8). The material of kind `material` in the set whose texture has kind
    // `texture`: its group's, else a new one of the client world (005655F0) in a new group. Null
    // when no set has the texture.
    CMaterial* FindOrAddMaterial(int texture, int material);
    // 0051B5B0 (RET 8). The same lookup without adding: the material or null.
    CMaterial* FindMaterial(int texture, int material);
    // 0051B630 (RET 4). `texture` replaces the one of its kind in `textures` (referenced; the old
    // one released) and in the part set of that kind. Returns the old texture, or null.
    CTexture* ReplaceTexture(CTexture* texture);
    // 0051C250 (RET 8). The skin names (+0xE0 / +0xE4) become `first` / `second`; for each that is
    // not null or "none" (any case) the part set's texture is replaced by the texture manager's of
    // that kind and name (00692D30) or else renamed and loaded (006922A0). Returns 1.
    int SetSkins(const char* first, const char* second);
    // 0051CB30 (slot 21). Every material takes the world material manager's colours for its kind
    // (0068EAF0; the cursor walks them), then SetSkins with the names kept (the second only when it
    // is not "none").
    void ApplyMaterials();
    // 0051C9F0 (RET 4). With `lit` every material's emissive colour is pushed on its stack and set
    // to white (alpha kept); without, each takes back the colour last pushed (if any).
    void SetLit(int lit);
    // 0051C9E0 (slot 26, RET 4): SetLit. 0051C9D0 (slot 25, RET 4): nothing.
    void SetLitSlot(int lit);
    void EmptySlot25(int);
    // object3d_client_shadow_state.cpp (A-U12).
    // 0051E200. Whether shadows are drawn for the object's kind: 0079BD0C above 0 and the switch of
    // its class (units 0079BD14, class 0x52 0079BD1C, others 0079BD18). All of EAX; with shadows
    // off only AL is cleared (the original's XOR AL, AL on the setting's value).
    std::uint32_t ShadowsShown();
    // 0051E280. Drops the shadow: its geometry, texture, bounds and sphere.
    void ClearShadow();
    // 0051E300. Whether the shadow can be seen: shadows are on for the kind and the half is
    // flagged 0x20 or its bounds, moved to the object for the test, are not outside the view (the
    // scene pipeline's ClipTest, 004F16A0, gives 2 for outside).
    bool ShadowInView();

    // The half as the interface its slots are called through (map_object_figures.h).
    class CObject3DSpecific* AsSpecific() { return reinterpret_cast<class CObject3DSpecific*>(this); }

    CObject3D* object;              // +0x1C
    // +0x20 the two part sets Draw walks: the first has the skin texture (its +0x60 is the shadow
    // colour key), the second a hero's simple skin (CUnitClientSpecific slot 30).
    TextureParts* partSets[2];
    lib::AlignedArray<CTexture*> textures;     // +0x28 the sets' textures (counted)
    lib::AlignedArray<CMaterial*> materials;   // +0x38 the groups' materials (counted): fade-in, tints
    int materialCursor;             // +0x48 the iteration's index, -1 when there are none
    // The projected shadow (0056C750 fills the three, 004EA4A0 draws them): blocks of four
    // positions, their texture coordinates, the triangles' indices.
    lib::AlignedArray<VertexPacket> vertices;      // +0x4C
    lib::AlignedArray<DecalTexCoord> texCoords;    // +0x5C
    lib::AlignedArray<std::uint16_t> indices;      // +0x6C
    CEngineObject* texture;         // +0x7C the shadow texture (counted)
    float bounds[4];                // +0x80 minimum x, y and maximum x, y in the light's view
    ClipBounds sphere;              // +0x90 the shadow's bounds (the scene object's slot 7), the
                                    //       centre relative to the object
    std::uint8_t unknown_BC[0xD8 - 0xBC];   // +0xBC..+0xC8 zeroed with the bounds
    CTexture* woundsTexture;        // +0xD8 the wounds texture (counted; 0051C3D0 loads it)
    struct WoundsMaterial* woundsMaterial;  // +0xDC the material drawing it (counted; slot 0 deletes)
    mfc::CString skinName;          // +0xE0 the first set's texture name (SetSkins)
    mfc::CString secondSkinName;    // +0xE4 the second set's
    float pulseClock;               // +0xE8 frames counted by the invisible / stunned pulses
    std::uint32_t touchTime;        // +0xEC the frame time the model was last kept loaded (CollectVisible)
    Vector3 figureScale;            // +0xF0 the figure's extra scale (slot 18, ExtraScale)
};
static_assert(offsetof(CObject3DClientSpecific, object) == 0x1C);
static_assert(offsetof(CObject3DClientSpecific, textures) == 0x28);
static_assert(offsetof(CObject3DClientSpecific, materials) == 0x38);
static_assert(offsetof(CObject3DClientSpecific, materialCursor) == 0x48);
static_assert(offsetof(CObject3DClientSpecific, skinName) == 0xE0);
static_assert(sizeof(CObject3DClientSpecific) == 0xFC);
static_assert(offsetof(CObject3DClientSpecific, vertices) == 0x4C);
static_assert(offsetof(CObject3DClientSpecific, texCoords) == 0x5C);
static_assert(offsetof(CObject3DClientSpecific, indices) == 0x6C);
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
