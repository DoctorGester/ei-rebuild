#pragma once

// The client's half of a 3D object and the shadow it casts: the renderer's shadow map draws it; the game keeps a
// sphere around the object and its shadow on the ground along the lights' direction (it decides the shadow's
// visibility out of view). See docs/notes/map_objects.md ("Shadows").

#include "engine/core/ref.h"
#include "engine/scene/device_material.h"   // DeviceMaterial, MaterialStage
#include "engine/core/engine_object.h"
#include "game/objects/object3d.h"   // Object3D, Object3DSpecific (the base half)
#include "engine/models/material.h"             // Material
#include "lib/bit_flags.h"
#include "lib/vecmath.h"
#include "engine/scene/pipeline.h"   // ClipBounds, TransformPipeline
#include "lib/rect.h"   // RECT
#include "render/render.h"   // CasterMark

#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

// The parts of a figure drawn with one material (a part set's group). The parts are released before the material.
struct PartGroup {
    Ref<Material> material;
    std::vector<HeldRef<FigureMesh>> parts;
};

// A part set of a 3D object's client half: a texture and the groups of parts drawn with it, one per material.
struct TextureParts {
    Texture* texture = nullptr;   // the half's `textures` holds its reference
    std::vector<std::unique_ptr<PartGroup>> groups;
};

class SceneRenderer;
struct WoundsMaterial;

// The client half's `flags` bits.
enum class ClientHalfFlag : std::uint32_t {
    Shown = 0x20,  // the object was shown: its shadow counts as in view
};
constexpr bool EnableBitFlags(ClientHalfFlag) { return true; }

// The client's half of a 3D object (class id 0x37): the shadow, the part sets and the skins.
class Object3DClientSpecific : public Object3DSpecific {
public:
    Object3DClientSpecific();
    ~Object3DClientSpecific() override;
    Object3DClientSpecific* ClientSide() override { return this; }
    void Create() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // The object's shadow when shadows are on for its kind and the object is shown: its meshes cast into the
    // renderer's shadow map (render::CastsShadow, units as strongly as they have faded in). Unless the shadow is
    // still valid, the lights reaching the object give a direction (at least 20 degrees above the ground) and the
    // object's bounding sphere swept along it to the ground gives `sphere` (the original projected the
    // figure's parts and a decal under them); no shadow texture or decal draw. Returns 1.
    int DrawPartsShadow(SceneRenderer* context) override;

    // Draws the object (`dirty` receives the screen rectangle the parts flagged 0x40 covered, or null): its
    // state and animation updated, the lights within their range + 10 collected, and every part of the two
    // part sets within sqrt(1e9) drawn with those lights. A figure whose parts are all static (part flags 0x10)
    // becomes static (object flags 0x400, flags bit 8). Returns 1.
    int DrawParts(SceneRenderer* context, Rect* dirty) override;

    // The pulse of an invisible or stunned unit: the clock moves on by `frames`, its phase fmod(clock, 30)
    // folded at 15 gives 0.2..0.7. Every drawn part takes it as its alpha / its tint (pulse, 0, 0) and flags
    // 0x10.
    void InvisiblePulse(float frames);
    void StunnedPulse(float frames);
    // Undo the pulses: every drawn part's alpha back to 1 / tint and flags 0x10 cleared.
    void ResetPartAlpha();
    void ClearPartTints();
    // The wounds texture and its material are taken off and released.
    void HideWounds();
    // Shows the wounds texture `name` (kind 6): 0 without a first part set. The texture is found or made on
    // first use (none: the game ends) and reloaded unless its .mmp extra block is nonempty and equal; a failed
    // load drops the texture and the material and returns 0. Without a material a new one (blended 5 / 6, the
    // helper's preset 4) gets the first part set's texture, then the wounds texture. Returns 1.
    int ShowWounds(const char* name);
    // figureScale.
    Vector3 GetScale() override;
    // Each drawn part registers its material with the world's material manager (with the part's type).
    void RegisterPartMaterials();
    // Screen picking (a shown object only, flags 0x20 of the 3D object): whether the point (x, y) is in the
    // object's dirtyRect; in the screen rectangle of one of its parts with their own mesh (part flags 0x40);
    // whether `rect` meets such a part's rectangle.
    int BoundsContainPoint(const std::int32_t* point) override;
    int PartsContainPoint(const std::int32_t* point) override;
    float PickDepth(const Point& point) override;
    int PartsIntersectRect(const Rect* rect) override;
    FigureMesh* FindPart(std::string_view name) override;
    FigureMesh* FindPartOfType(int type) override;

    // The wounds material slot cleared and the material it held released.
    void ClearWoundsMaterial();
    // `part` joins the group of its material in the set of its texture, both made on first use; referenced
    // unless already there. 0 when no set has the texture, else 1.
    int OnPartAdded(FigureMesh* part) override;
    // `part` leaves its group and is released: 1, or 0 when no group holds it.
    int OnPartRemoved(FigureMesh* part) override;
    // The texture of kind `kind`: one of `textures`, else a new one of the client world for a new part set,
    // the first for the skin kinds (1, 3, 4, 6-9, 0xB, 0xD-0x10), the second for any other; an existing set
    // only has its texture's kind changed. Trees (MapObjectType::Tree) flag the texture 0x2000.
    Texture* FindOrAddTexture(int kind);
    // The material of kind `material` in the set whose texture has kind `texture`: its group's, else a new
    // one of the client world in a new group. Null when no set has the texture.
    Material* FindOrAddMaterial(int texture, int material);
    // The same lookup without adding.
    Material* FindMaterial(int texture, int material);
    // `texture` replaces the one of its kind in `textures` (referenced; the old one released) and in the part
    // set of that kind. Returns the old texture, or null.
    Texture* ReplaceTexture(Texture* texture);
    // The skin names become `first` / `second`; for each that is not null or "none" (any case) the part set's
    // texture is replaced by the texture manager's of that kind and name or else renamed and loaded. Returns 1.
    int SetSkins(std::optional<std::string_view> first, std::optional<std::string_view> second);
    // Every material takes the world material manager's colours for its kind, then SetSkins with the names
    // kept (the second only when it is not "none").
    void ApplyMaterials() override;
    // With `lit` every material's emissive colour is pushed on its stack and set to white (alpha kept);
    // without, each takes back the colour last pushed (if any).
    void SetLit(bool lit) override;
    // Drops the shadow's sphere (the next draw fits it again).
    void ClearShadow();
    // Whether the shadow can be seen: shadows are on and the half is flagged 0x20 or its sphere,
    // moved to the object, is not outside the view (not fitted yet: listed).
    bool ShadowInView();

    // The two part sets DrawParts walks: the first has the skin texture (its colour key is the shadow colour key),
    // the second a hero's simple skin (UnitClientSpecific).
    std::unique_ptr<TextureParts> partSets[2];
    std::vector<HeldRef<Texture>> textures;     // the sets' textures
    std::vector<HeldRef<Material>> materials;   // the groups' materials: fade-in, tints
    bool shadowMade = false;        // the sphere fitted since Create / ClearShadow
    ClipBounds sphere;              // the object and its shadow (box and sphere), the centre relative to the object
    Rect drawnRect;                 // the screen area of the dirty parts DrawParts drew
    float originDepth = 0.0f;       // the view depth (the screen stack's w) of the origin DrawParts drew it at, with drawnRect
    Ref<Texture> woundsTexture;    // the wounds texture (ShowWounds loads it)
    Ref<WoundsMaterial> woundsMaterial;  // the material drawing it (counted)
    std::string skinName;          // the first set's texture name (SetSkins)
    std::string secondSkinName;    // the second set's
    float pulseClock = 0.0f;        // frames counted by the invisible / stunned pulses
    render::CasterMark casterMark;   // its place in the renderer's casters (BeginCaster, CastsShadow)
    Vector3 figureScale;            // the figure's extra scale (GetScale)
};

// Whether the game shows `object`'s shadow (shadows on for its kind, the object shown and not invisible) and,
// when it does, how strongly it casts in the renderer's shadow map (*strength: 1, a unit as far as it has
// faded in). Reads only; the shadow draw and the casters outside the view share it.
std::optional<float> ShadowCastStrength(Object3D* object);

// The material that draws a wounds texture (made when the wounds are first shown): blending on, source
// factor the source alpha, destination factor its inverse, the renderer helper's preset 4, two stages.
struct WoundsMaterial final : DeviceMaterial {
    WoundsMaterial();
    ~WoundsMaterial() override;
};

inline void Object3DClientSpecific::ClearWoundsMaterial() { woundsMaterial.Reset(); }

inline Object3DClientSpecific* Object3D::ClientSpecific() const { return attached ? attached->ClientSide() : nullptr; }
