#pragma once

#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <string>

class CFigureMesh;

// Six corner positions of a bone, from a .bon file, interpolated by the figure's three morph coefficients:
// bone = lerp(lerp(lerp(c[0], c[1], s), c[2], t), lerp(lerp(c[3], c[4], s), c[5], t), u)
struct BoneMorphBox {
    Vector3 c[6];
};
static_assert(sizeof(BoneMorphBox) == 0x48);

// A rotation key of an animation node: frames [frame, next); BuildBone copies the axis-angle from the node's
// position keys.
struct AnimationRotationKey {
    int frame;
    int next;
    Quaternion rotation;
    Vector3 axis;
    float angle;
};
static_assert(sizeof(AnimationRotationKey) == 40);

// A translation key: the step to the next key's position (the last key keeps zero).
struct AnimationTranslationKey {
    int frame;
    int next;
    Vector3 position;
    Vector3 step;
};
static_assert(sizeof(AnimationTranslationKey) == 32);

// A morph frame of an animation node: frames and the morphed points.
struct AnimationMorphFrame {
    int frame;
    int next;
    lib::AlignedArray<Vector3> points;
};
static_assert(sizeof(AnimationMorphFrame) == 0x18);
// The morph frames' CArray instantiation constructs and destroys them out of line.
struct AnimationMorphFrameElements {
    static void Construct(AnimationMorphFrame* frames, int count);
    static void Destruct(AnimationMorphFrame* frames, int count);
};

// An animated node of a figure (one bone). The fake units' skeletons build them from scene nodes (BuildBone).
class CAnimationNode {
public:
    // The pose / rest positions and the morph stay unwritten.
    CAnimationNode* Construct();
    // The mesh loses a reference without being released.
    void DestructBody();

    // The pose at the animation's start from the parent's pose and the first keys; the morph points become the
    // rest ones and the key cursors 0.
    void ResetPose();
    // Rewinds this node and its children (clock_ to 0, ResetPose) and places the mesh at the pose.
    void Rewind();
    // Binds the first part named like each node as its mesh (one more reference; a mesh held before is
    // overwritten without a release). Returns 1.
    int AttachMeshes(const lib::AlignedArray<CFigureMesh*>* parts);

    // Cleared by the constructor and the destructor; nothing reads them.
    std::uint32_t spare000_;
    std::string name_;
    std::string spareName008_;
    std::string spareName00C_;
    std::uint32_t spare010_[3];
    CAnimationNode* parent_;
    lib::AlignedArray<CAnimationNode*> children_;
    class CFigureMesh* mesh_;                            // the part the node places, or null
    std::uint32_t spare034_;
    std::uint32_t clock_[2];                             // cleared by Rewind (a double?)
    int frameCount_;                                     // the animation's frames (a short)
    lib::AlignedArray<AnimationRotationKey> rotationKeys_;
    int rotationCursor_;                                 // -1 after construction, 0 after ResetPose
    lib::AlignedArray<AnimationTranslationKey> translationKeys_;
    int translationCursor_;                              // the same
    mfc::CArray<Vector3> normals_;                       // zeroed when added
    int normalCursor_;                                   // the same
    mfc::CArray<AnimationMorphFrame, AnimationMorphFrameElements> morphFrames_;
    int morphCursor_;                                    // the same
    Quaternion defaultRotation_;                         // the identity (constructor); nothing reads it
    Vector3 defaultPosition_;                            // the origin (constructor); nothing reads it
    Quaternion poseRotation_;                            // the node's rotation in the figure
    Vector3 posePosition_;                               // its position in the figure
    lib::AlignedArray<Vector3> pointsB_;                 // one per morph point, zeroed
    Quaternion restRotation_;                            // the node's rest rotation
    Vector3 restPosition_;                               // the node's rest pivot
    lib::AlignedArray<Vector3> pointsA_;                 // one per morph point, zeroed
    // the bone position (SetMorph); the skeleton code keeps the rest position in the parent's frame here.
    Vector3 position_;
    float morphS_;
    float morphT_;
    float morphU_;
};
static_assert(offsetof(CAnimationNode, name_) == 0x04);
