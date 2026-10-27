#pragma once

// The shadow a 3D object casts on the client (CObject3DClientSpecific): the figure is drawn from the
// strongest light direction into a 64x64 texture, which is then laid onto the ground under the object. Names
// provisional; see docs/notes/map_objects.md ("Shadows").

#include "core_refcount.h"   // CountedRef
#include "engine_object.h"
#include "map_object_figures.h"   // CObject3D, CObject3DSpecific (the base half)
#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

#include <windows.h>   // RECT

class CObject3D;
class CTexture;
class CFigureMesh;
struct VertexPacket;
struct DecalTexCoord;

#include "device_material.h"   // CDeviceMaterial, MaterialStage
#include "material.h"   // CMaterial
#include "model.h"      // ClipBounds, CSceneRenderer (the draws' context), CTransformPipeline

// The world matrix of a 3D object or a figure part as the draws build it (map_object_draw.cpp): the rotation
// of `orientation`, `position` as column 3, the last row (0, 0, 0, 1).
void ObjectWorldMatrix(const Quaternion* orientation, const Vector3* position, Matrix4* out);

// The parts of a figure drawn with one material (0x14 bytes; a part set's group).
struct PartGroup {
    // Every part released, the array's block freed. Returns 1.
    int ReleaseParts();

    CMaterial* material;                    // counted
    lib::AlignedArray<CFigureMesh*> parts;   // counted
};
static_assert(sizeof(PartGroup) == 0x14);

// A part set of a 3D object's client half: a texture and the groups of parts drawn
// with it, one per material.
struct TextureParts {
    // The destructor: every group's parts and material released, the group deleted; the
    // array freed. The texture is only forgotten (the half's `textures` holds its reference).
    void Destruct();

    CTexture* texture;
    lib::AlignedArray<PartGroup*> groups;   // owned
};
static_assert(sizeof(TextureParts) == 0x14);

struct WoundsMaterial;   // below

// The client's half of a 3D object (GetClassName "CObject3DClientSpecific", class id 0x37): a
// CObject3DSpecific with the shadow, the part sets and the skins.
class CObject3DClientSpecific : public CObject3DSpecific {
public:
    // The empty arrays and names, the shadow sphere zeroed, cursor -1, the shadow
    // texture and the wounds texture / material dropped, bounds and pulse clock 0.
    CObject3DClientSpecific();
    ~CObject3DClientSpecific() override;
    // The cursor -1, flags_ 0, the shadow and wounds textures and the wounds
    // material dropped, bounds and sphere 0, no part sets (not freed), touch time 0, scale 1.
    // Returns 1.
    int Create() override;
    // The shadow and wounds textures and the wounds material dropped, both part
    // sets destroyed and deleted, every texture and material released and the arrays emptied; no
    // object, touch time and flags_ 0.
    void Destroy() override;
    // Class name and id: "CObject3DClientSpecific", 0x37.
    const char* GetClassName() override;
    int GetClassId() override;
    // Draw and DrawShadow below (`context` is the scene renderer).
    int DrawParts(void* context, int* dirty) override { return Draw(static_cast<CSceneRenderer*>(context), dirty); }
    int DrawPartsShadow(void* context) override { return DrawShadow(static_cast<CSceneRenderer*>(context)); }
    // PartRectsMeet below.
    int PartsIntersectRect(const long* rect) override { return PartRectsMeet(reinterpret_cast<const RECT*>(rect)); }
    // Draws the object's shadow (`context` is the scene renderer). When shadows are on for its kind (the option
    // above 0 and the per-kind switch) and the object is shown: unless the shadow is still valid, sums the lights
    // reaching the object into a direction (tilted to at least 20 degrees above the ground), renders the figure's
    // parts from there into the 64x64 shadow texture (created on first use) through the render surface, and
    // projects the texture's square onto the ground. Then the shadow geometry is drawn with the shadow's alpha
    // (units: their controller's). Returns 1, or a fatal error's result when the texture or the surface cannot be
    // had.
    int DrawShadow(CSceneRenderer* context);

    // Draws the object (`context` as above; `dirty` receives the screen rectangle the parts
    // flagged 0x40 covered, or null): the object's state is updated and its animation, the lights within their
    // range + 10 are collected, the object's matrix (and its extra scale) pushed, and every part of the two part
    // sets (the first with the texture) within sqrt(1e9) drawn through the context's draw with those lights. A
    // figure whose parts are all static (part flags 0x10) becomes static (object flags 0x400; a member the time it
    // did, flags_ bit 8). Returns 1, or a fatal error's result.
    int Draw(CSceneRenderer* context, void* dirty);

    // Walk the materials: the first (cursor 0; with none the cursor is -1) / the next
    // (the cursor moves on); null at the end.
    CMaterial* FirstMaterial();
    CMaterial* NextMaterial();

    // The pulse of an invisible or stunned unit: the clock moves on by `frames`, its
    // phase fmod(clock, 30) folded at 15 gives 0.2..0.7. Every drawn part takes it as its alpha. The second: every
    // drawn part takes the tint (pulse, 0, 0) and flags_ 0x10.
    void InvisiblePulse(float frames);
    void StunnedPulse(float frames);
    // Every drawn part's alpha back to 1 (an invisibility ended). Every drawn
    // part's tint cleared and flags_ 0x10 off (a stun ended).
    void ResetPartAlpha();
    void ClearPartTints();
    // The wounds texture and its material are taken off and released (the material
    // deleted at its last reference).
    void HideWounds();
    // Shows the wounds texture `name` (kind 6): 0 without a first part set. The texture
    // is found or made on first use (none: FatalError with the object's class name and id). Unless its .mmp extra
    // block is nonempty and equal to the texture's, the texture is reloaded (loaded flag 8 cleared); a failed load
    // drops the texture and the material and returns 0. Without a material, a new one (blended 5 / 6, the
    // renderer helper's preset 4) gets two stages: the first part set's texture, then the wounds texture. Returns
    // 1.
    int ShowWounds(const char* name);
    // The scale virtual: x, y, z = figureScale. The materials registration: each
    // drawn part (the cursor walks them from 0, up to the first null) registers its material with the world's
    // material manager (with the part's type_).
    void GetScale(float* x, float* y, float* z) override;
    void RegisterPartMaterials();
    // Screen picking (a shown object only, flags_ 0x20 of the 3D object): whether
    // the point (x, y) is in the object's dirtyRect; in the screen rectangle of one of its parts with their own
    // mesh (part flags 0x40); whether `rect` meets such a part's rectangle (a non-empty intersection).
    int BoundsContainPoint(const long* point) override;
    int PartsContainPoint(const long* point) override;
    int PartRectsMeet(const RECT* rect);
    // The object's part named `name` (CObject3D::FindPart) and its part of type `type`
    // (CObject3D::FindPartOfType).
    CFigureMesh* FindPart(const char* name) override;
    CFigureMesh* FindPartOfType(int type) override;

    // The part sets and skins. The wounds material slot cleared and the material it
    // held released (deleted at its last reference): CountedRef<WoundsMaterial>::Assign(nullptr), inlined by the
    // callers.
    void ClearWoundsMaterial();
    // `part` joins the group of its material in the set
    // of its texture, both made on first use (FindOrAddTexture, FindOrAddMaterial);
    // referenced unless already there. 0 when no set has the texture, else 1.
    int OnPartAdded(CFigureMesh* part) override;
    // The part named `name` leaves its group and is released. Returns it, or null when there is none or no group
    // holds it.
    CFigureMesh* RemoveFromPartSets(const char* name) override;
    // `part` leaves its group and is released: 1, or 0 when no group
    // holds it.
    int OnPartRemoved(CFigureMesh* part) override;
    // The texture of kind `kind`: one of `textures`, else a new one of the client
    // world for a new part set, the first for the skin kinds (1, 3, 4, 6-9, 0xB,
    // 0xD-0x10), the second for any other; an existing set only has its texture's kind changed (the
    // first with a debug log line). Objects of type 0x35 flag the texture 0x2000.
    CTexture* FindOrAddTexture(int kind);
    // The material of kind `material` in the set whose texture has kind
    // `texture`: its group's, else a new one of the client world in a new group. Null
    // when no set has the texture.
    CMaterial* FindOrAddMaterial(int texture, int material);
    // The same lookup without adding: the material or null.
    CMaterial* FindMaterial(int texture, int material);
    // `texture` replaces the one of its kind in `textures` (referenced; the old
    // one released) and in the part set of that kind. Returns the old texture, or null.
    CTexture* ReplaceTexture(CTexture* texture);
    // The skin names (+0xE0 / +0xE4) become `first` / `second`; for each that is
    // not null or "none" (any case) the part set's texture is replaced by the texture manager's of
    // that kind and name or else renamed and loaded. Returns 1.
    int SetSkins(const char* first, const char* second);
    // Every material takes the world material manager's colours for its kind (the cursor walks them), then
    // SetSkins with the names kept (the second only when it is not "none").
    void ApplyMaterials() override;
    // With `lit` every material's emissive colour is pushed on its stack and set
    // to white (alpha kept); without, each takes back the colour last pushed (if any).
    void SetMaterialsLit(int lit);
    // SetMaterialsLit. The unused virtual: nothing (EmptySlot25).
    void SetLit(int lit) override;
    void Unused25(int) override {}
    void EmptySlot25(int);
    // Whether shadows are drawn for the object's kind: the option above 0 and
    // the switch of its class (units, class 0x52, others each have one). All of EAX; with shadows off only AL is
    // cleared (the original's XOR AL, AL on the setting's value).
    std::uint32_t ShadowsShown();
    // Drops the shadow: its geometry, texture, bounds and sphere.
    void ClearShadow();
    // Whether the shadow can be seen: shadows are on for the kind and the half is flagged 0x20 or its bounds,
    // moved to the object for the test, are not outside the view (the scene pipeline's ClipTest gives 2 for
    // outside).
    bool ShadowInView();


    // the object it belongs to (CObject3DSpecific::object)
    // the two part sets Draw walks: the first has the skin texture (its colour key is the shadow colour key), the
    // second a hero's simple skin (CUnitClientSpecific).
    TextureParts* partSets[2];
    lib::AlignedArray<CTexture*> textures;     // the sets' textures (counted)
    lib::AlignedArray<CMaterial*> materials;   // the groups' materials (counted): fade-in, tints
    int materialCursor;             // the iteration's index, -1 when there are none
    // The projected shadow (the projector fills the three, the device draws them): blocks of four positions,
    // their texture coordinates, the triangles' indices.
    lib::AlignedArray<VertexPacket> vertices;
    lib::AlignedArray<DecalTexCoord> texCoords;
    lib::AlignedArray<std::uint16_t> indices;
    CTexture* texture;              // the shadow texture (counted)
    float bounds[4];                // minimum x, y and maximum x, y in the light's view
    ClipBounds sphere;              // the shadow's bounds (the scene object's transform), the
                                    //       centre relative to the object
    std::uint8_t unused_BC[0xC8 - 0xBC];    // zeroed with the bounds
    RECT drawnRect;                 // the screen area of the dirty parts Draw drew
    CTexture* woundsTexture;        // the wounds texture (counted; ShowWounds loads it)
    CountedRef<WoundsMaterial> woundsMaterial;  // the material drawing it (counted)
    mfc::CString skinName;          // the first set's texture name (SetSkins)
    mfc::CString secondSkinName;    // the second set's
    float pulseClock;               // frames counted by the invisible / stunned pulses
    std::uint32_t touchTime;        // the frame time the model was last kept loaded (CollectVisible)
    Vector3 figureScale;            // the figure's extra scale (GetScale)
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
static_assert(offsetof(CObject3DClientSpecific, drawnRect) == 0xC8 && offsetof(CObject3DClientSpecific, touchTime) == 0xEC);
static_assert(offsetof(CObject3DClientSpecific, figureScale) == 0xF0 && offsetof(CObject3DClientSpecific, woundsMaterial) == 0xDC);

// Whether the game shows `object`'s shadow (the shadow draw's tests: shadows on for its kind, the object shown
// and not invisible) and, when it does, how strongly it casts in the new renderer's shadow map (*strength: 1,
// a unit as far as it has faded in). Reads only; the shadow draw and the casters outside the view
// (world_client_shadow_casters.cpp) share it.
bool ShadowCastStrength(CObject3D* object, float* strength);

// The material that draws a wounds texture (a CDeviceMaterial, device_material.h: 0x24 bytes, over the plain
// counted root; made when the wounds are first shown): blend 1, source 5 (D3DBLEND_SRCALPHA), destination 6
// (D3DBLEND_INVSRCALPHA), the renderer helper's preset 4, two stages.
struct WoundsMaterial final : CDeviceMaterial {
    // The constructor ShowWounds inlines (unit_client_wounds.cpp).
    WoundsMaterial();
    ~WoundsMaterial() override;
};

inline void CObject3DClientSpecific::ClearWoundsMaterial() {
    WoundsMaterial* old = woundsMaterial;
    woundsMaterial = nullptr;
    if (old) old->Release();
}

// The 3D object's half on the client.
inline CObject3DClientSpecific* CObject3D::ClientSpecific() const { return static_cast<CObject3DClientSpecific*>(attached); }
