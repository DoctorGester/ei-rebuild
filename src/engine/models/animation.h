#pragma once

// A figure's skeleton and its animation: the bones, the tracks that place them and the state that plays a figure's
// animations on them. See docs/notes/map_objects.md ("Skeleton").

#include "engine/core/engine_object.h"
#include "engine/core/ref.h"
#include "engine/models/animation_db.h"   // FigureAnimation
#include "engine/models/figure_mesh.h"    // FigureMesh (a bone's part)
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

class FigureInstance;
class MappedResource;
struct FigureModel;

// The animation tracks' defaults without key frames (static initialisers: the origin and the identity).
inline constexpr Vector3 kTrackDefaultPosition = /*007AF110*/ {};
inline constexpr Quaternion kTrackDefaultRotation = /*007AF100*/ {1.0f, 0.0f, 0.0f, 0.0f};
struct FigureBone;

// A part's track (only the fields used here): key frames of positions, rotations and morphed vertex sets, one
// per time unit, and the values at the current time. Class id 0x300B, made by a bone's manager on first use
// and kept counted as the bone's animator. A real virtual class.
struct AnimationTrack : EngineObject {
    // The values at `time`: between key frame floor(time) (rounded from the CRT's
    // modf) and the next by the fraction - positions and morph vertices linearly, rotations by
    // Quaternion::Slerp; past the last key frame its values, without key frames the defaults
    // (kTrackDefaultPosition / kTrackDefaultRotation). Remembers `time`.
    void Evaluate(float time);

    AnimationTrack();
    ~AnimationTrack() override;

    void Create() override;   // as Destroy
    // No animation, bone or key frames, the default position and rotation (kTrackDefaultPosition /
    // kTrackDefaultRotation), no morphed vertices, time 0.
    void Destroy() override;
    const char* ClassName() override;   // "CAnimationTrack" (models/animation.cpp)
    ClassId GetClassId() override;

    // The key frames copied from the model entry at *cursor (advanced past them): the rotations (a count, then
    // that many quaternions, w first), the positions (likewise, three floats each), the frame count, the vertex
    // count and the frames' vertex sets one after another; the morphed vertices sized to the vertex count.
    void LoadKeyFrames(std::uint8_t** cursor);
    // Key frame `frame`'s vertex set (vertexCount vertices).
    const Vector3* FrameVertices(int frame) const {
        return frames.data() + static_cast<std::size_t>(frame) * static_cast<std::size_t>(vertexCount);
    }
    // Destroy's reset except for the bone: the animation, key frames, morphed vertices and time.
    void ClearKeys();

    const FigureAnimation* animation = nullptr;  // the animation playing (null: none)
    FigureBone* bone = nullptr;     // the bone it animates
    std::vector<Vector3> positions;      // key frames
    std::vector<Quaternion> rotations;   // key frames
    int vertexCount = 0;
    int frameCount = 0;
    std::vector<Vector3> frames;   // the key frames' vertex sets (FrameVertices), one after another
    float time = 0.0f;              // (0 when the animation changes)
    Vector3 position;
    Quaternion rotation;
    // the morphed vertices ({data, size} is what a bone gives).
    std::vector<Vector3> vertices;
};

// A bone of a figure's skeleton (made from the skeleton links). The animation state's bone list holds them,
// one of its members the root. Only the fields the pose code uses.
struct FigureBone : EngineObject {
    std::string name;                      // the piece's name (children are linked by it)
    std::string parentName;                // its parent's name (empty for the root)
    std::string modelName;                 // the figure's model name (LinkBone)
    FigureBone* parent = nullptr;           // null for the root
    std::vector<FigureBone*> children;
    Ref<FigureMesh> mesh;                   // the part the bone places, or null
    AnimationTrack* pose = nullptr;         // the animator whose values place the bone
    Ref<AnimationTrack> animator;           // made on first use; Play copies it to `pose`
    Vector3 offset;                         // the place relative to the parent
    float morphS = 0.0f;                    // the figure's morph coefficients
    float morphT = 0.0f;
    float morphU = 0.0f;                    // (also the time factor's t)
    Vector3 position;                       // in figure space
    Quaternion rotation;                    // in figure space
    std::vector<Matrix4> matrices;    // (emptied without freeing; freed by the destructor)
    // The blend from one animation into the next (FigureAnimationState::BeginBlend): the bone's own turn and
    // (the root) its position before the origin as last placed (`shown` once placed); the outgoing pose a switch
    // blends from (`blendFrom` while it has one: the mesh's offsets then too) and the blended offsets.
    Quaternion shownTurn;
    Vector3 shownPosition;
    bool shown = false;
    Quaternion fromTurn;
    Vector3 fromPosition;
    std::vector<Vector3> fromOffsets;
    bool blendFrom = false;
    std::vector<Vector3> blendedOffsets;

    FigureBone();
    ~FigureBone() override;

    // The names "none"; no parent, children, mesh, animator or pose; the default place.
    void Create() override;
    // The mesh and the animator released, then Create.
    void Destroy() override;
    std::string_view GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Places this bone and, recursively, its children: a child turns its offset by the parent's
    // rotation ((w^2 - u.u) o + 2w (u x o) + 2 (u.o) u, the scaled terms rounded) onto the parent's position and
    // takes rotation = pose * parent (Hamilton); the root takes the pose's position (times the animation's time
    // factor at morphU while one plays) minus `origin`, and its rotation; a bone without a pose keeps both. A bone
    // with a mesh gives it the position and rotation and the pose's morphed vertices as offsets. With `blend`
    // below 1 a bone with an outgoing pose takes that much of the animator's (its turn slerped, the root's
    // position and the offsets lerped); at 1 the animator's alone.
    void Place(const Vector3& origin, float blend);
};

// The figure's animation state (FigureInstance::animation): the skeleton's bones, the animation playing
// and the queue behind it, the frame reached.
class FigureAnimationState : public EngineObject {
public:
    // Plays `animation` from its start: set through the world's figure manager by the owner's model name, or
    // else on each bone's animator (AnimateBone).
    void Play(FigureAnimation* animation);
    // Queues `animation` after the one playing.
    void Queue(FigureAnimation* animation);
    // The frame reached becomes `frame` (`changed` set when it differs): with nothing playing the next queued
    // animation starts at 0; while the frame lies beyond the animation's frame count the count is taken off,
    // and the next queued animation plays, or else the animation holds its last frame
    // unless its key's top bits are 10 (loops). When the animation changed, it is set as in Play.
    void SetTime(float frame);
    void Advance(float frames);

    // Each bone's animator plays `animation`: the bone's manager is told and the animator's time cleared.
    static void AnimateBone(FigureBone* bone, const FigureAnimation* animation);

    FigureAnimationState();
    ~FigureAnimationState() override;

    void Create() override;
    // The bones released, then Reset.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void Reset();
    // The first bone named `boneName` (_mbscmp), or null.
    FigureBone* FindBone(std::string_view boneName);
    // A new bone (the figure manager's class 0x300C) named `child` with parent `parentName` joins through
    // LinkBone.
    void LinkPiece(FigureModel* model, std::string_view child, std::string_view parentName);
    // `bone` becomes a child of the first bone named as its parent (parentName) unless that one has a child of its
    // name; it joins the list (referenced), takes the model name and morph, and its manager loads its
    // key frames from `model`. `changed` set.
    void LinkBone(FigureModel* model, FigureBone* bone);
    // The bone named as `part` carries it as its mesh (referenced, the old one released): the part takes the
    // bone's position and rotation. 0 when no bone has the name, else 1. `changed` set.
    int AttachMesh(FigureMesh* part);
    // The root becomes the first bone without a parent name. 1, or 0 when none.
    int FindRoot();
    // The figure manager links the skeleton of `model` into this state; `changed` set. Returns its result.
    int SetModel(std::string_view model);
    // The first bone carrying `part` lets it go (released). 1, or 0 when none.
    int DetachMesh(FigureMesh* part);
    // Every bone lets its mesh go (released). `changed` set.
    void DetachMeshes();
    // When `changed`: cleared, and the root (if any) places the skeleton at the owner's time factor (at its scale
    // z) times its default scale.
    void PlaceIfChanged();
    // When `changed`: each bone's placing animator is evaluated at the frame reached then PlaceIfChanged. While a
    // blend runs the skeleton is placed again at its weight now, changed or not.
    void UpdatePose();
    // UpdatePose for a draw (the world's, a shadow's, the interface's): the pose placed is the one shown.
    void PlaceForDraw();
    // The morph becomes (s, t, u) on every bone, whose manager reloads its key frames from the owner's model,
    // and on the state (`changed` cleared); then the root places the skeleton as PlaceIfChanged does.
    void Rescale(float s, float t, float u);
    // Every bone's name.
    std::vector<std::string> BoneNames();
    // The figure manager's SetAnimation without an animation. Every bone's placing track forgets its animation and
    // key frames (ClearKeys). `changed` set.
    void ClearBoneTracks();
    // The figure manager's SetAnimation with an animation. Every bone's placing track loads the entry of `part`
    // named as the bone (LoadKeyFrames), or without one is cleared as above, and takes `animation`. `changed` set.
    void LoadBoneTracks(MappedResource* part, const FigureAnimation* animation);

    FigureInstance* owner = nullptr;   // the figure it animates
    FigureBone* root = nullptr;     // the skeleton's root bone (FindRoot)
    std::vector<HeldRef<FigureBone>> bones;   // LinkBone adds
    FigureAnimation* current = nullptr;  // the animation playing, or null
    // The animations queued after it (the client update keeps fewer than 3).
    std::deque<FigureAnimation*> queue;
    float time = 0.0f;              // the frame reached
    float morphS = 0.0f;            // the figure's morph coefficients (Rescale; bones copy them)
    float morphT = 0.0f;
    float morphU = 0.0f;
    bool changed = true;            // set when the frame, the animation or the bones changed

    // A switch from one animation to another blends the pose last placed into the new one's over a short time
    // (client side only: what plays and its frame stay the game's). The blend runs from `placedAt`, when the
    // outgoing pose was placed (the draw before the switch), so a draw a blend's length after it shows the new
    // pose alone, as at the harness's default frames. In the platform's milliseconds; `blendLength` 0 while none
    // runs.
    std::uint32_t blendStart = 0;
    std::uint32_t blendLength = 0;
    std::uint32_t placedAt = 0;
    // A switch blends only from a pose the player saw, in a figure seen moving: `poseShown` while the pose last
    // placed has been drawn (PlaceForDraw), `seenMoving` once a placement replaced a drawn pose. So a figure's first
    // shown pose is its current animation's, never a blend from the rest pose its building placed (the user's Lost
    // in Astral load: the HUD's hero swung out of a T pose) or from a pose shown frozen before its world ran (the
    // main menu after a game: its creatures' idles, drawn in the paused world before the server's first tick, blended
    // into their walks).
    bool poseShown = false;
    bool seenMoving = false;
    // Before a switch from `from` to `to`: each bone keeps the pose last placed as the outgoing one and the blend
    // starts, as long as `to`'s kind asks (none when either is null or the figure was not seen as above; one running
    // is cut short).
    void BeginBlend(const FigureAnimation* from, const FigureAnimation* to);
    // The incoming animation's share of the pose now (eased; 1 once the blend is over, which ends it).
    float BlendWeight();
    // The harness's seam (its "animblend off"): switches snap, as in the original, while off.
    static void SetBlends(bool on);
    // The harness's count ("animblend count"): the blends begun so far in world figures (not the interface's
    // previews, FigureFlag::Preview).
    static std::uint32_t WorldBlendsBegun();
};
