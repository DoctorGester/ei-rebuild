#pragma once

// The 3D object base under MapObject: the object's figure (a model resource shared through the figure
// manager), its parts (scene nodes, one per figure piece) and their bounding box, and the scale the figure
// was built for. See docs/notes/map_objects.md ("Figures").

#include "engine/core/engine_object.h"
#include "engine/models/animation.h"         // FigureAnimationState, FigureBone
#include "engine/models/animation_db.h"      // FigureAnimation, the animation keys
#include "engine/models/figure_instance.h"   // FigureInstance (the object's figure)
#include "engine/models/figure_mesh.h"       // FigureMesh (an object's figure parts)
#include "game/items/item_db.h"                // WeaponType
#include "lib/vecmath.h"
#include "engine/core/ref.h"
#include "game/objects/map_object_manager.h"   // MapObjectType

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Object3D;
class ParticleSource;
class SceneRenderer;

// The figures' default scale (zeros).
inline constexpr Vector3 kDefaultFigureScale = /*007AF1D0*/ {};
// The figure parts of a hero, in the game's order; "hr" is the hair ("hr.%.2d" with the prototype's number).
inline constexpr int kHeroPartCount = 16;
inline constexpr const char* kHeroFigureParts[kHeroPartCount] = {
    "hd", "bd", "hp", "hr", "lh1", "lh2", "lh3", "rh1", "rh2", "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"};

// Which generator a random animation pick draws from: the game's rand() (the server's steps and idles), or the client
// effects' (g_clientRandom, random.h: the client's idle animations, picked as often as its draw time restarts them).
enum class AnimationRandom { Game, Client };

// The weapon bit of weapon type `type` (the original's shift takes the count's low 5 bits).
constexpr std::uint32_t AnimationWeaponBit(WeaponType type) { return 1u << (static_cast<int>(type) & 0x1F); }

// The side-specific half of a 3D object (Object3D::attached): Object3DClientSpecific (class id 0x37) on
// the client, Object3DServer (0x38, stubs) on the server.
class Object3DSpecific;
class Object3DClientSpecific;
class Object3DSpecific : public EngineObject {
public:
    // This half as the client's: null on the server.
    virtual Object3DClientSpecific* ClientSide() { return nullptr; }
    ~Object3DSpecific() override;

    virtual int OnPartAdded(FigureMesh* part) = 0;   // server: 0
    virtual int OnPartRemoved(FigureMesh* part) = 0;      // server: 0
    virtual FigureMesh* FindPart(std::string_view name) = 0;   // a part by name (client); server: null
    virtual FigureMesh* FindPartOfType(int type) = 0;     // a part by type (client); server: null
    // The figure's extra scale (the base's: 1, 1, 1).
    virtual Vector3 GetScale();
    // The client draws the parts / their shadow; the base returns 0.
    virtual int DrawParts(SceneRenderer* context, Rect* dirty);
    virtual int DrawPartsShadow(SceneRenderer* context);
    // After LoadFigures: the client gives the parts the world's materials, then the skin texture. The base does
    // nothing.
    virtual void ApplyMaterials() {}
    // Screen picking: whether the point (x, y) is in the object's dirtyRect (Pick with HitTest::Bounds).
    virtual int BoundsContainPoint(const std::int32_t* point);
    // Whether the screen rectangle (a RECT) meets a drawn part's rectangle.
    virtual int PartsIntersectRect(const Rect* rect);
    // Whether the point is in the screen rectangle of a part with flag 0x40 (Pick with HitTest::Parts).
    virtual int PartsContainPoint(const std::int32_t* point);
    // The depth Pick sorts a hit by: the view depth of the object's nearest drawn surface under the point, else of
    // its origin as drawn. The base (nothing drawn): FLT_MAX.
    virtual float PickDepth(const Point& point);
    virtual void SetLit(bool) {}                     // the object's light is on / off (client); base: nothing

    Object3D* object = nullptr;    // the object it belongs to
};

// The server's half: no parts, nothing drawn.
class Object3DServer : public Object3DSpecific {
public:
    Object3DServer();             // The factory allocates it zeroed.
    ~Object3DServer() override;
    void Create() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    int OnPartAdded(FigureMesh* part) override;
    int OnPartRemoved(FigureMesh* part) override;
    FigureMesh* FindPart(std::string_view name) override;
    FigureMesh* FindPartOfType(int type) override;
};


// The 3D object: a figure (figure_instance.h) of the game's world with its side half, bounds and effect sources
// (abstract: class name and id are pure). WorldClient's CollectVisible sets its world position to the position in
// view space.
class Object3D : public FigureInstance {
public:
    ~Object3D() override;
    // type as the map object factory's type.
    MapObjectType MapType() const { return static_cast<MapObjectType>(type); }
    // Back to an empty object: no half, flags, figure or bounds.
    void Create() override;
    // The figure, the effect sources, the animation state, the parts and the side half let go.
    void Destroy() override;
    void ClearState() override { RemoveAllEffectSources(); }
    // Whether the object may play the animation entry (FigureAnimation); any here, Unit overrides it.
    virtual bool AcceptsAnimation(const FigureAnimation* animation);
    // A side half (SideObjectType::Object3D) becomes the attached one (SetHalf); other objects (and null) are
    // ignored. MapObject and units override it.
    virtual void AttachObject(EngineObject* object);
    // Whether the object is invisible: false here; map objects override it.
    virtual bool InvisibleSlot();
    // Draw the figure / draw it and collect its screen rectangle / draw its shadow with the scene context (the
    // side half's draw virtuals, the second first emptying dirtyRect to have it filled; 1 without a half).
    virtual int DrawFigure(SceneRenderer* context);
    virtual int DrawCollectDirty(SceneRenderer* context);
    virtual int DrawFigureShadow(SceneRenderer* context);
    // Empty; units override it.
    virtual void AdvanceTime(float time);
    // Loads the model named by the object's name into the figure manager; non-units mark every part and themselves
    // (flags 8 / 0x200). Then the half's LoadFigures-done virtual and flags 0x80. Nothing when already loaded.
    virtual void LoadFigures();

    // The bounding box of the parts (only those within 1e9 squared of the object) into the bounds: centre, box
    // corners relative to it, radius; sets two flag bits. Nothing changes without parts.
    void ComputeBounds();
    // The bounds, the bytes after them and the eye height zeroed.
    void ClearBounds();

    // Sets the scale and, when it changed, rebuilds the figure at that scale through the figure manager, copies
    // each part's piece record from it, rescales the animation and recomputes the bounds.
    void SetFigureScale(const Vector3& newScale);

    // Takes `part` out of the parts (it forgets its owner), tells the animation state and releases it. The side
    // half hears of it first, found or not. Returns 1 when it was there, else 0.
    int RemovePart(FigureMesh* part);
    // FindPart(`partName`), removed by RemovePart. Returns the part (released; the pointer may be dangling)
    // or null.
    FigureMesh* RemovePartNamed(std::string_view partName);
    // `part`'s position turned by the object's rotation, scaled by the side half's GetScale and moved to the
    // object's position.
    Vector3 PartWorldPosition(const FigureMesh* part);

    // The animation database, loaded on first use from the owner (AnimationDatabaseOf "<name>"; nothing without
    // an owner). Null when there is none.
    AnimationDatabase* Animations();
    // Animation `key & 0xFF` of the database, or null (no database, or beyond its count).
    FigureAnimation* AnimationAt(std::uint32_t key);
    // The first animation of kind Special whose number (key bits 22-29) is `number`, or null.
    FigureAnimation* FindSkillAnimation(SkillAnimation number);
    // A random animation for `key`: the database's entries whose key has the same bits above kAnimationVariantBits and all
    // of key's low 15 bits (none when key has none) and that AcceptsAnimation accepts; one is returned as it
    // is, several are narrowed to those whose chance is at least a random percent and one of them is picked
    // (the pick made before narrowing when none is left); the draws from `random`'s generator. Null without a
    // database or a match.
    FigureAnimation* RandomAnimation(std::uint32_t key, AnimationRandom random = AnimationRandom::Game);
    // As RandomAnimation for the entries whose key's bits above kAnimationVariantBits equal key's, one picked from
    // `random`'s generator; no chance narrowing.
    FigureAnimation* RandomAnimationOfKind(std::uint32_t key, AnimationRandom random = AnimationRandom::Game);
    // For units (class id 0x50) with an animation database whose length is neither 0 nor NaN:
    // (start + (end - start) * time) / length, else 1.
    float AnimationFactor(float time) override;
    // Sets the figure up for the animated model `model`: one part box and part name per animated part (the
    // figure manager's bounds of the part, its pivot, flags 4 | 0x400 "BASE", 0x90 "CROWN", 0x890 "EMPTY" by
    // name prefix); the bounds span the boxes (heroes, Hero: x and y -0.5..0.5).
    void SetUpAnimatedFigure(std::string_view model);
    // A part object for the figure part `partName` (the figure manager's LoadPart; "Can't load figure" logged
    // and null when missing). With a figure the part must be one of its pieces (else null, the part leaked)
    // and takes its box; box flags by name prefix as SetUpAnimatedFigure. It joins the parts.
    FigureMesh* LoadPart(std::string_view partName);
    // The object's figure becomes `model` and each name of `partList` is loaded by LoadPart
    // (failures logged). Without a figure manager the game ends. Returns 1.
    int LoadParts(std::string_view model, const std::vector<std::string>& partList);
    // When a unit becomes the controlled one (world client): with the figure loaded (flags 0x80) the parts'
    // world transforms are updated and the bounds recomputed from their world boxes.
    void UpdatePartBounds();
    // Builds the figure of the model for the part names `partList` (none: the animation
    // state's parts and the whole figure), then LoadParts and the bounds; flags 0x80 set.
    void MakeFigure(std::string_view model, std::vector<std::string>* partList);
    // The index of the part name equal to `partName` (_mbscmp), or -1.
    int FindPartIndex(std::string_view partName);
    // The box (FigureMesh::bounds) of part `index`.
    FootprintPart* PartBox(int index);

    // Adds `source` (a particle source; it calls this from its Attach, attached to this object already) to the
    // effect sources unless it is there: referenced. (The game calls Attach(this) back, which then does nothing.)
    void AddEffectSource(ParticleSource* source);
    // Takes `source` out of the effect sources, detaches it and releases it. Nothing when not there.
    void RemoveEffectSource(ParticleSource* source);

    // The release-effects virtual: every effect source is taken out of the list, detached and released.
    void RemoveAllEffectSources();

    // `half` (or null) replaces the attached half: the old one is detached and released, the new one points back
    // here and gains a reference.
    void SetHalf(Object3DSpecific* half);

    // The same half on the client (defined in map_object_shadow.h).
    Object3DClientSpecific* ClientSpecific() const;

    Ref<Object3DSpecific> attached;    // the side half (SetHalf)
    std::vector<HeldRef<ParticleSource>> effectSources;   // the particle sources attached to the object
    Rect dirtyRect;              // the screen area the last DrawCollectDirty covered
    std::vector<std::string> partNames;    // the figure's part names, in the order of the part boxes
    ClipBounds bounds;              // flags 1 and 2: the sphere and the box are valid; the box is relative to the centre
    // Cleared with the bounds; a part's piece record sets it (LoadPart). The AI adds it to a unit's eye height.
    float eyeHeight = 0.0f;
    std::vector<FootprintPart> partBoxes;   // one box per animated part (SetUpAnimatedFigure); FootprintPart: pipeline.h

protected:
    Object3D();
};
