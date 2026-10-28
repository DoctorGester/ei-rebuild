#pragma once

// The client's half of a 3D object and the shadow it casts: the renderer's shadow map draws it; the game still
// projects the figure from the strongest light direction onto the ground under the object (the decal's sphere
// decides the shadow's visibility out of view). See docs/notes/map_objects.md ("Shadows").

#include "core_refcount.h"   // CountedRef
#include "engine_object.h"
#include "map_object_figures.h"   // CObject3D, CObject3DSpecific (the base half)
#include "maths.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <vector>

#include <windows.h>   // RECT

#include "device_material.h"   // CDeviceMaterial, MaterialStage
#include "ground_decal.h"      // DecalTexCoord
#include "material.h"   // CMaterial
#include "model.h"      // ClipBounds, CSceneRenderer (the draws' context), CTransformPipeline

// The world matrix of a 3D object or a figure part as the draws build it: the rotation of `orientation`,
// `position` as column 3, the last row (0, 0, 0, 1).
void ObjectWorldMatrix(const Quaternion* orientation, const Vector3* position, Matrix4* out);

// The parts of a figure drawn with one material (a part set's group).
struct PartGroup {
    int ReleaseParts();

    CMaterial* material;                    // counted
    std::vector<CFigureMesh*> parts;   // counted
};

// A part set of a 3D object's client half: a texture and the groups of parts drawn with it, one per material.
struct TextureParts {
    TextureParts() = default;
    // The groups deleted (their parts and materials released); the texture is only forgotten (the half's
    // `textures` holds its reference).
    ~TextureParts();
    TextureParts(const TextureParts&) = delete;
    TextureParts& operator=(const TextureParts&) = delete;

    CTexture* texture;
    std::vector<PartGroup*> groups;   // owned
};

struct WoundsMaterial;

// The client's half of a 3D object (class id 0x37): the shadow, the part sets and the skins.
class CObject3DClientSpecific : public CObject3DSpecific {
public:
    CObject3DClientSpecific();
    ~CObject3DClientSpecific() override;
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    ClassId GetClassId() override;
    // `context` is the scene renderer.
    int DrawParts(CSceneRenderer* context, tagRECT* dirty) override { return Draw(context, dirty); }
    int DrawPartsShadow(CSceneRenderer* context) override { return DrawShadow(context); }
    int PartsIntersectRect(const tagRECT* rect) override { return PartRectsMeet(rect); }
    // The object's shadow when shadows are on for its kind and the object is shown: its meshes cast into the
    // renderer's shadow map (render::CastsShadow, units as strongly as they have faded in). Unless the shadow is
    // still valid, the lights reaching the object give a direction (at least 20 degrees above the ground), the
    // figure's parts are projected from there and the square of their bounds is projected onto the ground (the
    // decal's vertices and sphere); no shadow texture or decal draw. Returns 1.
    int DrawShadow(CSceneRenderer* context);

    // Draws the object (`dirty` receives the screen rectangle the parts flagged 0x40 covered, or null): its
    // state and animation updated, the lights within their range + 10 collected, and every part of the two
    // part sets within sqrt(1e9) drawn with those lights. A figure whose parts are all static (part flags 0x10)
    // becomes static (object flags 0x400, flags_ bit 8). Returns 1, or a fatal error's result.
    int Draw(CSceneRenderer* context, tagRECT* dirty);

    // Walk the materials: the first (cursor 0; with none the cursor is -1) / the next; null at the end.
    CMaterial* FirstMaterial();
    CMaterial* NextMaterial();

    // The pulse of an invisible or stunned unit: the clock moves on by `frames`, its phase fmod(clock, 30)
    // folded at 15 gives 0.2..0.7. Every drawn part takes it as its alpha / its tint (pulse, 0, 0) and flags_
    // 0x10.
    void InvisiblePulse(float frames);
    void StunnedPulse(float frames);
    // Undo the pulses: every drawn part's alpha back to 1 / tint and flags_ 0x10 cleared.
    void ResetPartAlpha();
    void ClearPartTints();
    // The wounds texture and its material are taken off and released.
    void HideWounds();
    // Shows the wounds texture `name` (kind 6): 0 without a first part set. The texture is found or made on
    // first use (none: FatalError) and reloaded unless its .mmp extra block is nonempty and equal; a failed
    // load drops the texture and the material and returns 0. Without a material a new one (blended 5 / 6, the
    // helper's preset 4) gets the first part set's texture, then the wounds texture. Returns 1.
    int ShowWounds(const char* name);
    // x, y, z = figureScale.
    void GetScale(float* x, float* y, float* z) override;
    // Each drawn part registers its material with the world's material manager (with the part's type_).
    void RegisterPartMaterials();
    // Screen picking (a shown object only, flags_ 0x20 of the 3D object): whether the point (x, y) is in the
    // object's dirtyRect; in the screen rectangle of one of its parts with their own mesh (part flags 0x40);
    // whether `rect` meets such a part's rectangle.
    int BoundsContainPoint(const long* point) override;
    int PartsContainPoint(const long* point) override;
    int PartRectsMeet(const RECT* rect);
    CFigureMesh* FindPart(std::string_view name) override;
    CFigureMesh* FindPartOfType(int type) override;

    // The wounds material slot cleared and the material it held released.
    void ClearWoundsMaterial();
    // `part` joins the group of its material in the set of its texture, both made on first use; referenced
    // unless already there. 0 when no set has the texture, else 1.
    int OnPartAdded(CFigureMesh* part) override;
    // The part named `name` leaves its group and is released. Returns it, or null when there is none or no group
    // holds it.
    CFigureMesh* RemoveFromPartSets(const char* name) override;
    // `part` leaves its group and is released: 1, or 0 when no group holds it.
    int OnPartRemoved(CFigureMesh* part) override;
    // The texture of kind `kind`: one of `textures`, else a new one of the client world for a new part set,
    // the first for the skin kinds (1, 3, 4, 6-9, 0xB, 0xD-0x10), the second for any other; an existing set
    // only has its texture's kind changed. Objects of type 0x35 flag the texture 0x2000.
    CTexture* FindOrAddTexture(int kind);
    // The material of kind `material` in the set whose texture has kind `texture`: its group's, else a new
    // one of the client world in a new group. Null when no set has the texture.
    CMaterial* FindOrAddMaterial(int texture, int material);
    // The same lookup without adding.
    CMaterial* FindMaterial(int texture, int material);
    // `texture` replaces the one of its kind in `textures` (referenced; the old one released) and in the part
    // set of that kind. Returns the old texture, or null.
    CTexture* ReplaceTexture(CTexture* texture);
    // The skin names become `first` / `second`; for each that is not null or "none" (any case) the part set's
    // texture is replaced by the texture manager's of that kind and name or else renamed and loaded. Returns 1.
    int SetSkins(std::optional<std::string_view> first, std::optional<std::string_view> second);
    // Every material takes the world material manager's colours for its kind, then SetSkins with the names
    // kept (the second only when it is not "none").
    void ApplyMaterials() override;
    // With `lit` every material's emissive colour is pushed on its stack and set to white (alpha kept);
    // without, each takes back the colour last pushed (if any).
    void SetMaterialsLit(int lit);
    void SetLit(int lit) override;
    void Unused25(int) override {}
    // Whether shadows are drawn for the object's kind: the option above 0 and the switch of its class (units,
    // class 0x52, others each have one). With shadows off only the low byte is cleared (the original's XOR
    // AL, AL on the setting's value).
    std::uint32_t ShadowsShown();
    // Drops the shadow: its geometry, bounds and sphere (the next draw builds it again).
    void ClearShadow();
    // Whether the shadow can be seen: shadows are on for the kind and the half is flagged 0x20 or its bounds,
    // moved to the object, are not outside the view.
    bool ShadowInView();

    // The two part sets Draw walks: the first has the skin texture (its colour key is the shadow colour key),
    // the second a hero's simple skin (CUnitClientSpecific).
    TextureParts* partSets[2];
    std::vector<CTexture*> textures;     // the sets' textures (counted)
    std::vector<CMaterial*> materials;   // the groups' materials (counted): fade-in, tints
    int materialCursor;             // the iteration's index, -1 when there are none
    // The projected shadow (the projector fills the three; only its sphere is read): blocks of four positions,
    // their texture coordinates, the triangles' indices.
    std::vector<VertexPacket> vertices;
    std::vector<DecalTexCoord> texCoords;
    std::vector<std::uint16_t> indices;
    bool shadowMade;                // built since Create / ClearShadow
    float bounds[4];                // minimum x, y and maximum x, y in the light's view
    ClipBounds sphere;              // the shadow's bounds, the centre relative to the object
    RECT drawnRect;                 // the screen area of the dirty parts Draw drew
    CTexture* woundsTexture;        // the wounds texture (counted; ShowWounds loads it)
    CountedRef<WoundsMaterial> woundsMaterial;  // the material drawing it (counted)
    std::string skinName;          // the first set's texture name (SetSkins)
    std::string secondSkinName;    // the second set's
    float pulseClock;               // frames counted by the invisible / stunned pulses
    std::uint32_t touchTime;        // the frame time the model was last kept loaded (CollectVisible)
    Vector3 figureScale;            // the figure's extra scale (GetScale)
};

// Whether the game shows `object`'s shadow (shadows on for its kind, the object shown and not invisible) and,
// when it does, how strongly it casts in the new renderer's shadow map (*strength: 1, a unit as far as it has
// faded in). Reads only; the shadow draw and the casters outside the view share it.
bool ShadowCastStrength(CObject3D* object, float* strength);

// The material that draws a wounds texture (made when the wounds are first shown): blending on, source
// factor the source alpha, destination factor its inverse, the renderer helper's preset 4, two stages.
struct WoundsMaterial final : CDeviceMaterial {
    WoundsMaterial();
    ~WoundsMaterial() override;
};

inline void CObject3DClientSpecific::ClearWoundsMaterial() {
    WoundsMaterial* old = woundsMaterial;
    woundsMaterial = nullptr;
    if (old) old->Release();
}

inline CObject3DClientSpecific* CObject3D::ClientSpecific() const { return static_cast<CObject3DClientSpecific*>(attached); }
