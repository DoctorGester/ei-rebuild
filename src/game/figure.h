#pragma once

#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// Six corner positions of a bone, from a .bon file. A figure's proportions are set by three morph
// coefficients; the bone position is interpolated between these corners:
//   lower = lerp(lerp(c[0], c[1], s), c[2], t)
//   upper = lerp(lerp(c[3], c[4], s), c[5], t)
//   bone  = lerp(lower, upper, u)
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
    Quaternion rotation;   // +0x08
    Vector3 axis;          // +0x18
    float angle;           // +0x24
};
static_assert(sizeof(AnimationRotationKey) == 40);

// A translation key of an animation node (32 bytes): frames, the position, the step to the next
// key's position (the last key keeps the zero vector it started with).
struct AnimationTranslationKey {
    int frame;
    int next;
    Vector3 position;      // +0x08
    Vector3 step;          // +0x14
};
static_assert(sizeof(AnimationTranslationKey) == 32);

// A morph frame of an animation node (24 bytes): frames and the morphed points.
struct AnimationMorphFrame {
    int frame;
    int next;
    lib::AlignedArray<Vector3> points;   // +0x08
};
static_assert(sizeof(AnimationMorphFrame) == 0x18);
// The morph frames' CArray instantiation constructs (00452E20) and destroys (00452DF0) them out of
// line (scene_bones.cpp).
struct AnimationMorphFrameElements {
    static void Construct(AnimationMorphFrame* frames, int count);
    static void Destruct(AnimationMorphFrame* frames, int count);
};

// An animated node of a figure (one bone; 0x128 bytes, constructor 00506710, destructor body
// 005068C0). The fake units' skeletons build them from scene nodes (BuildBone 004492F0,
// scene_bones.cpp). Names and layout provisional.
class CAnimationNode {
public:
    // animation_node_lifetime.cpp (A-U10).
    // 00506710. The constructor: empty names and arrays (CArray vtables 0073C120 / 0073D9BC), the
    // four key cursors -1, identity pose and rest rotations, +0x9C / +0xAC and position_ from the
    // defaults 007AF0E0 / 007AF0F0. The pose / rest positions and the morph stay unwritten.
    // Returns this.
    CAnimationNode* Construct();
    // 005068C0. The destructor body: the mesh loses a reference (not released) and is forgotten,
    // each child is destroyed (recursively) and deleted, the child array emptied, +0x00 cleared,
    // the arrays freed, the names destroyed.
    void DestructBody();

    // 005064C0. Stores the morph coefficients and computes the bone position from `box`.
    void SetMorph(const BoneMorphBox* box, float s, float t, float u);

    // skin_rewind.cpp (A-U9):
    // 00507E80. The pose at the animation's start: rotation = the parent's pose rotation times the
    // first rotation key's (the key's alone for the root), position = the parent's pose position plus
    // position_ turned by the parent's rotation (the root: turned by its own); the current morph
    // points become the rest ones (pointsB_ = pointsA_) and the four key cursors 0.
    void ResetPose();
    // 005081C0. Rewinds this node and its children: the clock (+0x38 / +0x3C) to 0, ResetPose, and
    // the mesh (when there is one) placed at the pose with the morph points as its offsets (005A0B10).
    void Rewind();
    // 00507C50 (RET 4, EAX 1; skin_rewind.cpp, A-U11). Binds the figure's parts (CFigureMesh*) to this
    // node and its children: the first part whose name (engine object slot 7) equals the node's
    // becomes its mesh (one more reference; a mesh held before is overwritten without a release) and
    // is placed at the pose with the morph points as its offsets (005A0B10).
    int AttachMeshes(const lib::AlignedArray<void*>* parts);

    std::uint32_t unknown_000;
    mfc::CString name_;                                  // +0x004
    mfc::CString unknown_008;                            // +0x008
    mfc::CString unknown_00C;                            // +0x00C
    std::uint32_t unknown_010[3];
    CAnimationNode* parent_;                             // +0x01C
    lib::AlignedArray<CAnimationNode*> children_;        // +0x020
    class CFigureMesh* mesh_;                            // +0x030 the part the node places, or null
    std::uint32_t unknown_034;
    std::uint32_t clock_[2];                             // +0x038 cleared by Rewind (a double?)
    int frameCount_;                                     // +0x040 the animation's frames (a short)
    lib::AlignedArray<AnimationRotationKey> rotationKeys_;   // +0x044
    int rotationCursor_;                                 // +0x054 -1 after construction, 0 after ResetPose
    lib::AlignedArray<AnimationTranslationKey> translationKeys_;   // +0x058
    int translationCursor_;                              // +0x068 the same
    mfc::CArray<Vector3> normals_;                       // +0x06C (vtable 0073C120; zeroed when added)
    int normalCursor_;                                   // +0x080 the same
    mfc::CArray<AnimationMorphFrame, AnimationMorphFrameElements> morphFrames_;   // +0x084 (vtable 0073D9BC)
    int morphCursor_;                                    // +0x098 the same
    Quaternion unknown_09C;                              // +0x09C from 007AF0E0 (constructor)
    Vector3 unknown_0AC;                                 // +0x0AC from 007AF0F0
    Quaternion poseRotation_;                            // +0x0B8 the node's rotation in the figure
    Vector3 posePosition_;                               // +0x0C8 its position in the figure
    lib::AlignedArray<Vector3> pointsB_;                 // +0x0D4 one per morph point, zeroed
    Quaternion restRotation_;                            // +0x0E4 the node's rest rotation
    Vector3 restPosition_;                               // +0x0F4 the node's rest pivot
    lib::AlignedArray<Vector3> pointsA_;                 // +0x100 one per morph point, zeroed
    // +0x110 the bone position (SetMorph); the skeleton code keeps the rest position in the
    // parent's frame here (scene_bone_frames.cpp).
    Vector3 position_;
    float morphS_;          // +0x11C
    float morphT_;          // +0x120
    float morphU_;          // +0x124
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
static_assert(offsetof(CAnimationNode, restRotation_) == 0xE4 && offsetof(CAnimationNode, unknown_0AC) == 0xAC);
static_assert(offsetof(CAnimationNode, restPosition_) == 0xF4);
static_assert(offsetof(CAnimationNode, pointsA_) == 0x100);
static_assert(offsetof(CAnimationNode, position_) == 0x110);
static_assert(sizeof(CAnimationNode) == 0x128);
