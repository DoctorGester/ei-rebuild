// The bones of a fake unit's skeleton (called for the scene's root and recursively for every child): a bone
// (CAnimationNode, figure.h; 0x128 bytes, constructed by the caller) takes a scene node's name, rotation
// keys from the node's position keys (axis and angle), translation keys from its rotation keys with the
// step to the next key, a normals-sized array, the node's rest rotation and pivot, and, for nodes with
// morph keys, one morph frame per frame of the animation whose points are the morph keys interpolated
// linearly in between; then a child bone per child node. A node with a pivot other than (0, 0, 0) is a
// fatal error. Names provisional; see docs/notes/objects.md ("Rewritten").
#include "core_maths.h"
#include "figure.h"
#include "object_maths.h"
#include "scene_loader.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>
#include <memory>

namespace scene {
namespace {

using PointArray = lib::AlignedArray<Vector3>;

// Out-of-line SetSize [size, growth or -1]: the morph frames'; the aligned arrays' (a frame's points, the children;
// a frame's points copied) are their members.


// (a - b) / (c - b) as FILD (a - b), FIDIV (c - b), FSTP: one rounded division of exact integers.
void FrameRatio(int numerator, int denominator, float* out) { *out = static_cast<float>(static_cast<double>(numerator) / denominator); }

// --- The steps -----------------------------------------------------------------------------------

// The node's position keys (axis-angle) into the bone's rotation keys: frame, frame + 1, the axis
// and the angle, copied as they are.
void CopyRotationKeys(CAnimationNode* bone, const SceneNode* node) {
    for (int i = 0; i < node->positionKeys.size; ++i) {
        const NodePositionKey& src = node->positionKeys.data[i];
        AnimationRotationKey& dst = bone->rotationKeys_[i];
        int frame = src.frame;
        dst.frame = frame;
        dst.next = frame + 1;
        std::memcpy(&dst.axis, &src.axis, sizeof(Vector3));
        std::memcpy(&dst.angle, &src.angle, sizeof(float));
    }
}

// The node's rotation keys (a vector each) into the bone's translation keys: frame, frame + 1, the
// vector; each key's step is the difference to the next key (written when the next one is copied);
// the first key starts with the zero vector.
void CopyTranslationKeys(CAnimationNode* bone, const SceneNode* node) {
    for (int i = 0; i < node->rotationKeys.size; ++i) {
        const NodeRotationKey& src = node->rotationKeys.data[i];
        AnimationTranslationKey* dst = &bone->translationKeys_[i];
        int frame = src.frame;
        dst->frame = frame;
        dst->next = frame + 1;
        std::memcpy(&dst->position, &src.value, sizeof(Vector3));
        if (i == 0) {
            std::memcpy(&dst->step, &kZeroVector, sizeof(Vector3));
        } else {
            AnimationTranslationKey* previous = dst - 1;
            previous->step = dst->position - previous->position;
        }
    }
}

const PointArray& KeyPoints(const SceneNode* node, int key) { return node->morphKeys.data[key].points; }

// One morph frame per frame (the bone's frame count): before each key the points between the
// previous key and it, a + (b - a) * t with each operation rounded (or the key itself before the
// first), at a key the points from it towards the next one through the out-of-line vector helpers
// (or a copy of the last key).
void BuildMorphFrames(CAnimationNode* bone, const SceneNode* node) {
    mfc::CArray<AnimationMorphFrame, AnimationMorphFrameElements>& frames = bone->morphFrames_;
    int key = 0;
    int previousFrame = 0;
    float t = 0.0f;
    for (int frame = 0, index = 0; frame < frames.size; ++index) {
        int keyFrame = node->morphKeys.data[key].frame;
        if (frame < keyFrame) {
            FrameRatio(frame - previousFrame, keyFrame - previousFrame, &t);
            AnimationMorphFrame& out = frames.data[index];
            out.frame = frame;
            ++frame;
            out.next = frame;
            out.points.resize(KeyPoints(node, 0).size);
            for (int j = 0; j < std::ssize(frames.data[index].points); ++j) {
                Vector3* p = &frames.data[index].points[j];
                if (key <= 0) {
                    std::memcpy(p, &KeyPoints(node, key).data[j], sizeof(Vector3));
                    continue;
                }
                const Vector3& a = KeyPoints(node, key - 1).data[j];
                const Vector3& b = KeyPoints(node, key).data[j];
                *p = Lerp(a, b, t);
            }
            continue;
        }

        ++key;
        previousFrame = keyFrame;
        float toNext = 0.0f;
        if (key < node->morphKeys.size) FrameRatio(frame - keyFrame, node->morphKeys.data[key].frame - keyFrame, &toNext);
        AnimationMorphFrame& out = frames.data[index];
        out.frame = frame;
        ++frame;
        out.next = frame;
        out.points.resize(KeyPoints(node, 0).size);
        if (key >= node->morphKeys.size) {
            const PointArray& last = KeyPoints(node, key - 1);
            frames.data[index].points.assign(last.data, last.data + last.size);
            continue;
        }
        for (int j = 0; j < std::ssize(frames.data[index].points); ++j) {
            Vector3* p = &frames.data[index].points[j];
            const Vector3* a = &KeyPoints(node, key - 1).data[j];
            const Vector3* b = &KeyPoints(node, key).data[j];
            Vector3 d, s, r;
            SubtractVectors(&d, *b, *a);
            ScaleVector(&s, d, toNext);
            const Vector3* sum = AddVectors(&r, *a, s);
            std::memcpy(p, sum, sizeof(Vector3));
        }
    }
}

} // namespace

// `node` is the scene node, the low word of `frames` the animation's frame count.
void __fastcall BuildBone(SceneSkin* skin, CAnimationNode* bone, const SceneNode* node, int frames) {
    CAnimationNode* added = bone;
    skin->bones.SetAtGrowRef(skin->bones.size, &added);
    bone = added;
    bone->name_ = node->name;
    bone->frameCount_ = static_cast<short>(frames);

    bone->translationKeys_.resize(node->rotationKeys.size);
    bone->rotationKeys_.resize(node->positionKeys.size);
    bone->normals_.SetSize(node->normals.size);
    int morphKeys = node->morphKeys.size;
    bone->morphFrames_.SetSize(morphKeys != 0 ? morphKeys + 1 : 0);
    std::memcpy(&bone->restRotation_, &node->restRotation, sizeof(Quaternion));
    std::memcpy(&bone->restPosition_, &node->restPivot, sizeof(Vector3));
    if (node->morphKeys.size != 0) {
        const auto points = static_cast<std::size_t>(KeyPoints(node, 0).size);
        bone->pointsA_.assign(points, Vector3{});
        bone->pointsB_.assign(points, Vector3{});
    }

    CopyRotationKeys(bone, node);
    CopyTranslationKeys(bone, node);
    if (node->morphKeys.size == 0) {
        bone->morphFrames_.SetSize(0, -1);
    } else {
        bone->morphFrames_.SetSize(node->positionKeys.size);
        if (bone->morphFrames_.size > 0) BuildMorphFrames(bone, node);
    }

    bone->children_.resize(node->children.size);
    for (int i = 0; i < std::ssize(bone->children_); ++i) {
        CAnimationNode* block = gamecrt::operator_new_as<CAnimationNode>();
        CAnimationNode* child = block ? block->Construct() : nullptr;
        bone->children_[i] = child;
        bone->children_[i]->parent_ = bone;
        BuildBone(skin, bone->children_[i], node->children.data[i], frames);
    }

    const Vector3& pivot = node->offset;
    EI_NAN_AUDIT(pivot.x, pivot.y, pivot.z);
    if (pivot.x != 0.0f || pivot.y != 0.0f || pivot.z != 0.0f) {
        game::FatalError(NonNullPivotText(), bone->name_.c_str(), static_cast<double>(pivot.x),
                         static_cast<double>(pivot.y), static_cast<double>(pivot.z));
    }
}

} // namespace scene

void AnimationMorphFrameElements::Construct(AnimationMorphFrame* frames, int count) {
    for (; count--; ++frames) std::construct_at(frames);
}
void AnimationMorphFrameElements::Destruct(AnimationMorphFrame* frames, int count) {
    std::destroy_n(frames, count);
}
void AnimationMorphFrameElements::Relocate(AnimationMorphFrame* to, AnimationMorphFrame* from, int count) {
    mfc::MoveElements(to, from, count);
}
