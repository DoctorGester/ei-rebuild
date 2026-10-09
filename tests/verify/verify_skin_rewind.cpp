// Checks for rewinding a fake unit's skeleton (skin_rewind.cpp).
#include "verify.h"

#include "../../src/game/figure.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/scene_loader.h"

#include <cstring>
#include <vector>

namespace {

struct PoseState {
    Quaternion rotation;
    Vector3 position;
    int cursors[4];
};

PoseState Take(const CAnimationNode* node) {
    return {node->poseRotation_, node->posePosition_,
            {node->rotationCursor_, node->translationCursor_, node->normalCursor_, node->morphCursor_}};
}

void Put(CAnimationNode* node, const PoseState& s) {
    node->poseRotation_ = s.rotation;
    node->posePosition_ = s.position;
    node->rotationCursor_ = s.cursors[0];
    node->translationCursor_ = s.cursors[1];
    node->normalCursor_ = s.cursors[2];
    node->morphCursor_ = s.cursors[3];
}

bool Near(const float* a, const float* b, int n) {
    for (int i = 0; i < n; ++i)
        if (!ei::verify::NearlyEqual(a[i], b[i], 4, 1e-6f)) return false;
    return true;
}

std::vector<Vector3> Points(const CAnimationNode* node) {
    return std::vector<Vector3>(node->pointsB_.data, node->pointsB_.data + node->pointsB_.size);
}

} // namespace

// 00507E80: the node's pose and cursors are saved, the original runs, its results kept, the saved
// values put back and ours runs: pose within 4 ULPs, cursors and the current morph points exactly.
// Both runs size the points the same (the second SetSize keeps the block) and copy the same rest
// points. Reached by every fake unit scene load (LoadFakeUnitScene, through 00508990 / 005081C0).
static void* s_resetPose;
static void __fastcall ResetPoseVerify(CAnimationNode* node, void*) {
    PoseState before = Take(node);
    reinterpret_cast<void(__fastcall*)(CAnimationNode*)>(s_resetPose)(node);
    PoseState expected = Take(node);
    std::vector<Vector3> expectedPoints = Points(node);
    Put(node, before);
    node->ResetPose();
    PoseState actual = Take(node);
    std::vector<Vector3> actualPoints = Points(node);
    bool ok = Near(&expected.rotation.w, &actual.rotation.w, 4) && Near(&expected.position.x, &actual.position.x, 3) &&
              std::memcmp(expected.cursors, actual.cursors, sizeof(expected.cursors)) == 0 &&
              expectedPoints.size() == actualPoints.size() &&
              (expectedPoints.empty() ||
               std::memcmp(expectedPoints.data(), actualPoints.data(), expectedPoints.size() * sizeof(Vector3)) == 0);
    ei::verify::Check("CAnimationNode::ResetPose", true, ok, "parent %p, %d points", static_cast<void*>(node->parent_),
                      static_cast<int>(actualPoints.size()));
}
EI_VERIFY(0x00507E80, ResetPoseVerify, 6, s_resetPose);

// 005081C0: ours only (each node's pose is compared by the wrapper above; children come through
// this wrapper one by one): the clock and cursors 0, the mesh at the pose with as many offsets as
// morph points.
static void* s_rewind;
static void __fastcall RewindVerify(CAnimationNode* node, void*) {
    node->Rewind();
    bool ok = node->clock_[0] == 0 && node->clock_[1] == 0 && node->rotationCursor_ == 0 &&
              node->translationCursor_ == 0 && node->normalCursor_ == 0 && node->morphCursor_ == 0;
    if (CFigureMesh* mesh = node->mesh_) {
        ok = ok && std::memcmp(&mesh->position_, &node->posePosition_, sizeof(Vector3)) == 0 &&
             std::memcmp(&mesh->rotation_, &node->poseRotation_, sizeof(Quaternion)) == 0 &&
             mesh->offsets_.size == node->pointsB_.size;
    }
    ei::verify::Check("CAnimationNode::Rewind", true, ok, "mesh %p, %d children", static_cast<void*>(node->mesh_),
                      node->children_.size);
}
EI_VERIFY(0x005081C0, RewindVerify, 0, s_rewind);

// 00508990: ours only: the skin's clock and its root's are 0.
static void* s_skinRewind;
static void __fastcall SkinRewindVerify(scene::SceneSkin* skin, void*) {
    skin->Rewind();
    bool ok = skin->clock[0] == 0 && skin->clock[1] == 0 &&
              (!skin->root || (skin->root->clock_[0] == 0 && skin->root->clock_[1] == 0));
    ei::verify::Check("SceneSkin::Rewind", true, ok, "root %p", static_cast<void*>(skin->root));
}
EI_VERIFY(0x00508990, SkinRewindVerify, 0, s_skinRewind);
