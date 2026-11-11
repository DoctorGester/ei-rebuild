#pragma once

// A figure in the scene (FigureInstance): a model's parts (figure_mesh.h) on its skeleton with an animation playing
// (animation.h), the points of its parts and its draw. The game's 3D objects are figures with their halves
// (game/objects/object3d.h); a figure alone, without a game object, is what the effect viewer's own program puts
// effects on. See docs/notes/map_objects.md ("Figures").

#include "engine/core/core_transform.h"
#include "engine/core/ref.h"
#include "engine/models/animation.h"   // FigureAnimationState
#include "engine/models/figure_mesh.h"
#include "engine/scene/drawn_motion.h"
#include "lib/bit_flags.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class LinkMesh;
class Material;
class SceneRenderer;
class Texture;
struct AnimationDatabase;
struct ClipBounds;
namespace render {
struct MeshLighting;
}

// The bits of EngineObject::flags the engine reads on a figure; the map objects' flags (MapObjectFlag) name them too.
enum class FigureFlag : std::uint32_t {
    NoEdgeFade = 0x800,       // an interface figure while it is drawn: placed by the interface in its own view
    Preview = 0x4000000,      // an interface figure (its blends are not the world's: FigureAnimationState::WorldBlendsBegun)
};
template <>
inline constexpr bool lib::kBitFlags<FigureFlag> = true;

// A figure's part counts as near when its squared distance from the figure's origin is at most this (farther parts
// are skipped by the bounds and the draw).
inline constexpr float kPartsNearSquared = 1e9f;

// The world matrix of a figure or a figure part as the draws build it: the rotation of `orientation`, `position` as
// column 3, the last row (0, 0, 0, 1).
void ObjectWorldMatrix(const Quaternion* orientation, const Vector3& position, Matrix4* out);
// A part's box flags by its name: "BASE..." BoundsFlag::Base, "CROWN..." Crown, "EMPTY..." NoFootprint and Crown
// (the case ignored).
void MarkPartBox(ClipBounds* bounds, std::string_view partName);
// The scene's sun and ambient colour (SceneRenderer::sun, AmbientColour) for figure parts lit on the GPU at
// `coefficient` (the point lights are the GPU's too).
render::MeshLighting FigureLighting(float coefficient);

class FigureInstance : public TransformObject {
public:
    // The figure factory's (FigureObjectType::Instance) figure alone; the game's objects are made as Object3D.
    FigureInstance();
    ~FigureInstance() override;
    // A figure alone: no figure, parts or animation, at the origin.
    void Create() override;
    // The looks, the meshes, the animation state, the parts and the figure let go.
    void Destroy() override;
    const char* ClassName() override;   // "FigureInstance" (ours)
    ClassId GetClassId() override;

    // The factor of the animation database's time at `time` (the figure's scale z) the skeleton's root places the
    // figure's origin by (defaultScale times it): 1 here; a unit's (Object3D) from its database.
    virtual float AnimationFactor(float time);

    // The first part whose name starts with `prefix` (case-sensitive), or null.
    FigureMesh* FindPartByPrefix(std::string_view prefix);
    // The first part named exactly `partName`, or null.
    FigureMesh* FindPart(std::string_view partName);
    // The first part whose engine object type is `type`, or null.
    FigureMesh* FindPartOfType(int type);
    // Every part whose name starts with `prefix`.
    std::vector<FigureMesh*> CollectPartsByPrefix(std::string_view prefix);
    // `part` joins the parts unless it is there (appended, referenced): it takes this figure as its owner and the
    // animation state's bone of its name carries it. Whether it joined.
    bool AddPart(FigureMesh* part);
    // `part` takes its piece `partName` of the figure (loaded into the figure unless it has it): the piece's kinds and
    // box. False when the figure has no such piece. Needs a figure.
    bool TakePiece(FigureMesh* part, std::string_view partName);
    // `part`'s position turned by the figure's rotation, scaled by `figureScale` and moved to its position.
    Vector3 PartPoint(const FigureMesh* part, const Vector3& figureScale) const;

    // Every part's flags gains / loses the bits of `mask`.
    void SetPartFlags(std::uint32_t mask);
    void ClearPartFlags(std::uint32_t mask);
    // Every part's flatColour / flatSpecular becomes `value`.
    void SetPartsFlatColour(std::uint32_t value);
    void SetPartsFlatSpecular(std::uint32_t value);
    // `state` (or null) replaces the animation state: the old one loses its owner and is released, the new
    // one gains a reference and this owner.
    void SetAnimationState(FigureAnimationState* state);
    // The animation state (if any) plays `animation`.
    void PlayAnimation(FigureAnimation* animation);
    // The animation state's bone named as `part` carries it (AttachMesh); 0 without a state.
    int AttachPartMesh(FigureMesh* part);
    // The animation state (if any) goes to frame 0 and places the skeleton.
    void RestartPose();

    // --- A figure alone (its owner the figure manager) ---

    // The figure of `model` at the morph `size` (s, t, u: a unit's scale) as a unit's is built (Object3D::MakeFigure):
    // a skeleton (an animation state of the figure manager unless it has one), the figure of `partList`'s pieces
    // (none: every bone's, the whole link figure) and a part per piece, posed at rest. False when the figure manager
    // has no such model or no figure was made; the pieces the figure lacks are left out.
    bool Build(std::string_view model, const Vector3& size, const std::vector<std::string>& partList);
    // Each part's look: the texture `skin` of its kind (the texture manager's, loaded on first use) and a material of
    // its kind with the material table's colours. A part without a look is not drawn.
    void Dress(std::string_view skin);
    // Draws the parts near enough and shown (not FigureMeshFlag::Hidden) with their looks, placed by the figure's
    // own place (worldMatrix) under the pipeline's world stack, lit by the scene (FigureLighting at
    // `lightingCoefficient`); FigureMeshFlag::Drawn on each drawn part. The skeleton is placed first (PlaceForDraw).
    void Draw(SceneRenderer* context, float lightingCoefficient);

    Matrix4 worldMatrix;            // object -> world as last drawn: the rotation, the position as column 3
    Ref<FigureAnimationState> animation;   // the figure's animation state (SetAnimationState)
    AnimationDatabase* animations = nullptr;  // its animation database (an object loads it on first use)
    std::string name;               // the figure's model name
    Vector3 scale;                  // the morph the figure is built at (s, t, u)
    // the figure's parts, each holding a reference
    std::vector<HeldRef<FigureMesh>> parts;
    // the figure (counted; deleted at its last reference), made by the figure cache: the parts' piece headers
    // and the meshes the parts draw and shadow through.
    Ref<LinkMesh> figure;
    Vector3 defaultScale;           // the root's origin at a time factor of 1 (zeros: kDefaultFigureScale; a unit's lift)
    drawn::ObjectStep drawnStep;    // client objects: their last tick's step as the draw spreads it (drawn_motion.h)

    // A figure alone's look of a part (Dress): by the parts' order.
    struct PartLook {
        Ref<Texture> texture;
        Ref<Material> material;
    };
    std::vector<PartLook> looks;
};
