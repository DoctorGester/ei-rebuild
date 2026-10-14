// Rewinding a fake unit's skeleton (00507E80, 005081C0, 00508990): each bone (CAnimationNode,
// figure.h) goes back to the pose of its animation's first key and places its mesh there. The scene
// loader (LoadFakeUnitScene 00446C10) rewinds the skin once its bones are bound. See
// docs/notes/objects.md ("Scene loader").
#include "figure.h"         // CAnimationNode
#include "figure_mesh.h"    // CFigureMesh
#include "scene_loader.h"   // SceneSkin
#include "bone_pose.h"
#include "engine_object.h"  // CEngineObject::GetName

#include "ei/hook.h"
#include "lib/vecmath.h"

#include <cstring>

namespace {

// Rewritten here, called through their game addresses so the verify wrappers see each node.
__declspec(naked) void __fastcall CallResetPose(CAnimationNode*) EI_ORIGINAL(0x00507E80)
__declspec(naked) void __fastcall CallRewind(CAnimationNode*) EI_ORIGINAL(0x005081C0)

// The parent's rotation `p` times the key's `k` (Hamilton order) with the original's sums.
Quaternion ParentTimesKey(const Quaternion& p, const Quaternion& k) {
    Vector3 kv = {k.x, k.y, k.z};
    Vector3 pv = {p.x, p.y, p.z};
    Quaternion q;
    q.w = k.w * p.w - DotXYZ(kv, pv);
    q.x = ((k.z * p.y - k.y * p.z) + k.w * p.x) + p.w * k.x;
    q.y = ((k.x * p.z - k.z * p.x) + k.w * p.y) + p.w * k.y;
    q.z = ((k.y * p.x - k.x * p.y) + p.w * k.z) + k.w * p.z;
    return q;
}

} // namespace

void CAnimationNode::ResetPose() {
    const Quaternion& key = rotationKeys_.data[0].rotation;
    if (parent_) {
        const Quaternion& parentRotation = parent_->poseRotation_;
        poseRotation_ = ParentTimesKey(parentRotation, key);
        posePosition_ = TurnByQuaternion(parentRotation, position_) + parent_->posePosition_;
    } else {
        poseRotation_ = key;
        posePosition_ = TurnByQuaternion(poseRotation_, position_);
    }
    pointsB_.SetSize(pointsA_.size);
    std::memcpy(pointsB_.data, pointsA_.data, static_cast<std::size_t>(pointsA_.size) * sizeof(Vector3));
    translationCursor_ = 0;
    rotationCursor_ = 0;
    normalCursor_ = 0;
    morphCursor_ = 0;
}
EI_REPLACE_METHOD(0x00507E80, CAnimationNode::ResetPose);

void CAnimationNode::Rewind() {
    clock_[0] = 0;
    clock_[1] = 0;
    CallResetPose(this);
    if (mesh_) {
        mesh_->localPosition_ = posePosition_;
        mesh_->localRotation_ = poseRotation_;
        CFigureMesh::OffsetSpan points{pointsB_.data, pointsB_.size};
        mesh_->SetOffsets(&points);
    }
    for (int i = 0; i < children_.size; ++i) CallRewind(children_.data[i]);
}
EI_REPLACE_METHOD(0x005081C0, CAnimationNode::Rewind);

// The parts are engine objects (figure mesh classes 0x41-0x44); slot 7 gives the mesh's name.
int CAnimationNode::AttachMeshes(const lib::AlignedArray<void*>* parts) {
    for (int i = 0; i < parts->size; ++i) {
        auto* part = static_cast<CEngineObject*>(parts->data[i]);
        if (std::strcmp(part->GetName(), name_) != 0) continue;
        mesh_ = static_cast<CFigureMesh*>(parts->data[i]);
        ++mesh_->refCount_;
        if (mesh_) {
            mesh_->localPosition_ = posePosition_;
            mesh_->localRotation_ = poseRotation_;
            CFigureMesh::OffsetSpan points{pointsB_.data, pointsB_.size};
            mesh_->SetOffsets(&points);
        }
        break;
    }
    for (int i = 0; i < children_.size; ++i) {
        if (children_.data[i]) children_.data[i]->AttachMeshes(parts);
    }
    return 1;
}
EI_REPLACE_METHOD(0x00507C50, CAnimationNode::AttachMeshes);

namespace scene {

void SceneSkin::Rewind() {
    if (root) CallRewind(root);   // the original inlines the root's Rewind
    clock[0] = 0;
    clock[1] = 0;
}
EI_REPLACE_METHOD(0x00508990, SceneSkin::Rewind);

int SceneSkin::BindMeshes(const lib::AlignedArray<void*>* parts) {
    if (root) root->AttachMeshes(parts);
    return 1;
}
EI_REPLACE_METHOD(0x00508BF0, SceneSkin::BindMeshes);

} // namespace scene
