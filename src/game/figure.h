#pragma once

#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CFigureMesh;

// Six corner positions of a bone, from a .bon file. A figure's proportions are set by three morph
// coefficients; the bone position is interpolated between these corners:
// lower = lerp(lerp(c[0], c[1], s), c[2], t)
// upper = lerp(lerp(c[3], c[4], s), c[5], t)
// bone  = lerp(lower, upper, u)
struct BoneMorphBox {
    Vector3 c[6];
};
static_assert(sizeof(BoneMorphBox) == 0x48);

// A rotation key of an animation node (40 bytes): frames [frame, next), the rotation (made from
// the axis and angle by the skeleton code, scene_bone_frames.cpp) and the scene's axis-angle
// (BuildBone, scene_bones.cpp, copies them from the node's position keys).
struct AnimationRotationKey {
    int frame;
    int next;
    Quaternion rotation;
    Vector3 axis;
    float angle;
};
static_assert(sizeof(AnimationRotationKey) == 40);

// A translation key of an animation node (32 bytes): frames, the position, the step to the next
// key's position (the last key keeps the zero vector it started with).
struct AnimationTranslationKey {
    int frame;
    int next;
    Vector3 position;
    Vector3 step;
};
static_assert(sizeof(AnimationTranslationKey) == 32);

// A morph frame of an animation node (24 bytes): frames and the morphed points.
struct AnimationMorphFrame {
    int frame;
    int next;
    lib::AlignedArray<Vector3> points;
};
static_assert(sizeof(AnimationMorphFrame) == 0x18);
// The morph frames' CArray instantiation constructs and destroys them out of
// line.
struct AnimationMorphFrameElements {
    static void Construct(AnimationMorphFrame* frames, int count);
    static void Destruct(AnimationMorphFrame* frames, int count);
};

// An animated node of a figure (one bone; 0x128 bytes). The fake units' skeletons build them from scene nodes
// (BuildBone, scene_bones.cpp). Names and layout provisional.
class CAnimationNode {
public:
    // The constructor: empty names and arrays, the four key cursors -1, identity pose
    // and rest rotations, two quaternion members and position_ from the defaults (the identity and the origin).
    // The pose / rest positions and the morph stay unwritten. Returns this.
    CAnimationNode* Construct();
    // The destructor body: the mesh loses a reference (not released) and is forgotten,
    // each child is destroyed (recursively) and deleted, the child array emptied, +0x00 cleared,
    // the arrays freed, the names destroyed.
    void DestructBody();

    // Stores the morph coefficients and computes the bone position from `box`.
    void SetMorph(const BoneMorphBox* box, float s, float t, float u);

    // skin_rewind.cpp (A-U9):
    // The pose at the animation's start: rotation = the parent's pose rotation times the
    // first rotation key's (the key's alone for the root), position = the parent's pose position plus
    // position_ turned by the parent's rotation (the root: turned by its own); the current morph
    // points become the rest ones (pointsB_ = pointsA_) and the four key cursors 0.
    void ResetPose();
    // Rewinds this node and its children: the clock (+0x38 / +0x3C) to 0, ResetPose, and
    // the mesh (when there is one) placed at the pose with the morph points as its offsets.
    void Rewind();
    // Binds the figure's parts (CFigureMesh*) to this node and its children: the first part
    // whose name equals the node's becomes its mesh (one more reference; a mesh held before is overwritten
    // without a release) and is placed at the pose with the morph points as its offsets. Returns 1.
    int AttachMeshes(const lib::AlignedArray<CFigureMesh*>* parts);

    // +0x000, +0x008..+0x018, +0x034: cleared (the names constructed and destroyed) by the
    // constructor and the destructor; nothing reads them.
    std::uint32_t spare000_;
    mfc::CString name_;
    mfc::CString spareName008_;
    mfc::CString spareName00C_;
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
    // the bone position (SetMorph); the skeleton code keeps the rest position in the
    // parent's frame here.
    Vector3 position_;
    float morphS_;
    float morphT_;
    float morphU_;
};
static_assert(offsetof(CAnimationNode, name_) == 0x04);
static_assert(offsetof(CAnimationNode, parent_) == 0x1C);
static_assert(offsetof(CAnimationNode, children_) == 0x20);
static_assert(offsetof(CAnimationNode, frameCount_) == 0x40);
static_assert(offsetof(CAnimationNode, rotationKeys_) == 0x44);
static_assert(offsetof(CAnimationNode, translationKeys_) == 0x58);
static_assert(offsetof(CAnimationNode, normals_) == 0x6C);
static_assert(offsetof(CAnimationNode, morphFrames_) == 0x84);
static_assert(offsetof(CAnimationNode, pointsB_) == 0xD4);
static_assert(offsetof(CAnimationNode, mesh_) == 0x30 && offsetof(CAnimationNode, clock_) == 0x38);
static_assert(offsetof(CAnimationNode, rotationCursor_) == 0x54 && offsetof(CAnimationNode, morphCursor_) == 0x98);
static_assert(offsetof(CAnimationNode, poseRotation_) == 0xB8 && offsetof(CAnimationNode, posePosition_) == 0xC8);
static_assert(offsetof(CAnimationNode, restRotation_) == 0xE4 && offsetof(CAnimationNode, defaultPosition_) == 0xAC);
static_assert(offsetof(CAnimationNode, restPosition_) == 0xF4);
static_assert(offsetof(CAnimationNode, pointsA_) == 0x100);
static_assert(offsetof(CAnimationNode, position_) == 0x110);
static_assert(sizeof(CAnimationNode) == 0x128);
