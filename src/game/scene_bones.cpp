// The bones of a fake unit's skeleton (004492F0, called for the scene's root by 00449240 and
// recursively for every child): a bone (CAnimationNode, figure.h; 0x128 bytes, constructed by the
// caller with 00506710) takes a scene node's name, rotation keys from the node's position keys
// (axis and angle), translation keys from its rotation keys with the step to the next key, a
// normals-sized array, the node's rest rotation and pivot, and, for nodes with morph keys, one
// morph frame per frame of the animation whose points are the morph keys interpolated linearly in
// between; then a child bone per child node. A node with a pivot other than (0, 0, 0) is a fatal
// error. Names provisional; see docs/notes/objects.md ("Rewritten").
#include "core_maths.h"
#include "figure.h"
#include "object_maths.h"
#include "scene_loader.h"
#include "game_arrays.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace scene {
namespace {

using PointArray = lib::AlignedArray<Vector3>;

// --- Forwarders ----------------------------------------------------------------------------------

// Element helpers [pointer, count]: the morph frame constructor 00452E20 and destructor 00452DF0.
__declspec(naked) void __stdcall ConstructMorphFrames(void*, int) EI_ORIGINAL(0x00452E20)
__declspec(naked) void __stdcall DestroyMorphFrames(void*, int) EI_ORIGINAL(0x00452DF0)
// Out-of-line SetSize [size, growth or -1]: the morph frames 0044F7E0, a frame's points 0044F1D0,
// the children 0044F540; a frame's points copied from a point array 0044F410.
using MorphFrames = mfc::CArray<AnimationMorphFrame, AnimationMorphFrameElements>;
__declspec(naked) void __fastcall MorphFramesSetSize(MorphFrames*, void*, int, int) EI_ORIGINAL(0x0044F7E0)
__declspec(naked) void __fastcall PointsSetSize(PointArray*, void*, int, int) EI_ORIGINAL(0x0044F1D0)
__declspec(naked) void __fastcall PointsCopy(PointArray*, void*, const PointArray*) EI_ORIGINAL(0x0044F410)
__declspec(naked) void __fastcall ChildrenSetSize(lib::AlignedArray<CAnimationNode*>*, void*, int, int) EI_ORIGINAL(0x0044F540)
// Vector helpers, __fastcall out in ECX: b - a [00448A10 scales instead: v * t, t as float bits],
// a + b [0040CDC0]. Each component is one rounded operation.

// --- x87 -----------------------------------------------------------------------------------------

// (a - b) / (c - b) as FILD (a - b), FIDIV (c - b), FSTP: one rounded division of exact integers.
void FrameRatio(int numerator, int denominator, float* out) { *out = static_cast<float>(static_cast<double>(numerator) / denominator); }

// FCOMP with 0.0 (0073B858) and C3: equal, or unordered.
bool ZeroOrNaN(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return (bits & 0x7FFFFFFFu) == 0 || ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0);
}

// --- The steps -----------------------------------------------------------------------------------

// The node's position keys (axis-angle) into the bone's rotation keys: frame, frame + 1, the axis
// and the angle, copied as they are.
void CopyRotationKeys(CAnimationNode* bone, const SceneNode* node) {
    for (int i = 0; i < node->positionKeys.size; ++i) {
        const NodePositionKey& src = node->positionKeys.data[i];
        AnimationRotationKey& dst = bone->rotationKeys_.data[i];
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
        AnimationTranslationKey* dst = &bone->translationKeys_.data[i];
        int frame = src.frame;
        dst->frame = frame;
        dst->next = frame + 1;
        std::memcpy(&dst->position, &src.value, sizeof(Vector3));
        if (i == 0) {
            std::memcpy(&dst->step, &ZeroVector(), sizeof(Vector3));
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
    MorphFrames& frames = bone->morphFrames_;
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
            PointsSetSize(&out.points, nullptr, KeyPoints(node, 0).size, -1);
            for (int j = 0; j < frames.data[index].points.size; ++j) {
                Vector3* p = &frames.data[index].points.data[j];
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
        PointsSetSize(&out.points, nullptr, KeyPoints(node, 0).size, -1);
        if (key >= node->morphKeys.size) {
            PointsCopy(&frames.data[index].points, nullptr, &KeyPoints(node, key - 1));
            continue;
        }
        for (int j = 0; j < frames.data[index].points.size; ++j) {
            Vector3* p = &frames.data[index].points.data[j];
            const Vector3* a = &KeyPoints(node, key - 1).data[j];
            const Vector3* b = &KeyPoints(node, key).data[j];
            Vector3 d, s, r;
            SubtractVectors(&d, *b, *a);
            ScaleVector(&s, d, toNext);
            const Vector3* sum = VectorAdd(&r, a, &s);
            std::memcpy(p, sum, sizeof(Vector3));
        }
    }
}

} // namespace

// 004492F0 (__fastcall: the skin in ECX, the new bone in EDX; RET 8). `node` is the scene node
// (0x120 bytes), the low word of `frames` the animation's frame count.
void __fastcall BuildBone(SceneSkin* skin, CAnimationNode* bone, const SceneNode* node, int frames) {
    CAnimationNode* added = bone;
    aligned::DwordArraySetAtGrowRef(reinterpret_cast<lib::AlignedArray<std::uint32_t>*>(&skin->bones), nullptr,   // 00452C40
                                    skin->bones.size, reinterpret_cast<const std::uint32_t*>(&added));
    bone = added;
    bone->name_ = node->name;
    bone->frameCount_ = static_cast<short>(frames);

    bone->translationKeys_.SetSize(node->rotationKeys.size);
    bone->rotationKeys_.SetSize(node->positionKeys.size);
    bone->normals_.SetSize(node->normals.size);
    int morphKeys = node->morphKeys.size;
    bone->morphFrames_.SetSize(morphKeys != 0 ? morphKeys + 1 : 0);
    std::memcpy(&bone->restRotation_, &node->restRotation, sizeof(Quaternion));
    std::memcpy(&bone->restPosition_, &node->restPivot, sizeof(Vector3));
    if (node->morphKeys.size != 0) {
        PointArray& a = bone->pointsA_;
        a.SetSize(KeyPoints(node, 0).size);
        std::memset(a.data, 0, static_cast<std::size_t>(a.size) * sizeof(Vector3));
        PointArray& b = bone->pointsB_;
        b.SetSize(KeyPoints(node, 0).size);
        std::memset(b.data, 0, static_cast<std::size_t>(b.size) * sizeof(Vector3));
    }

    CopyRotationKeys(bone, node);
    CopyTranslationKeys(bone, node);
    if (node->morphKeys.size == 0) {
        MorphFramesSetSize(&bone->morphFrames_, nullptr, 0, -1);
    } else {
        bone->morphFrames_.SetSize(node->positionKeys.size);
        if (bone->morphFrames_.size > 0) BuildMorphFrames(bone, node);
    }

    ChildrenSetSize(&bone->children_, nullptr, node->children.size, -1);
    for (int i = 0; i < bone->children_.size; ++i) {
        void* block = gamecrt::operator_new(sizeof(CAnimationNode));
        CAnimationNode* child = block ? static_cast<CAnimationNode*>(block)->Construct() : nullptr;
        bone->children_.data[i] = child;
        bone->children_.data[i]->parent_ = bone;
        BuildBone(skin, bone->children_.data[i], node->children.data[i], frames);
    }

    const Vector3& pivot = node->offset;
    if (!ZeroOrNaN(pivot.x) || !ZeroOrNaN(pivot.y) || !ZeroOrNaN(pivot.z)) {
        game::FatalError(NonNullPivotText(), static_cast<const char*>(bone->name_), static_cast<double>(pivot.x),
                         static_cast<double>(pivot.y), static_cast<double>(pivot.z));
    }
}
EI_REPLACE(0x004492F0, BuildBone);

} // namespace scene

void AnimationMorphFrameElements::Construct(AnimationMorphFrame* frames, int count) {
    scene::ConstructMorphFrames(frames, count);
}
void AnimationMorphFrameElements::Destruct(AnimationMorphFrame* frames, int count) {
    scene::DestroyMorphFrames(frames, count);
}
