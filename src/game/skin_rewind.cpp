// Rewinding a fake unit's skeleton: each bone (CAnimationNode,
// figure.h) goes back to the pose of its animation's first key and places its mesh there. The scene
// loader (LoadFakeUnitScene) rewinds the skin once its bones are bound. See
// docs/notes/objects.md ("Facts").
#include "figure.h"         // CAnimationNode
#include "figure_mesh.h"    // CFigureMesh
#include "scene_loader.h"   // SceneSkin
#include "bone_pose.h"
#include "engine_object.h"  // CEngineObject::GetName

#include "lib/vecmath.h"

#include <cstring>

namespace {

// The parent's rotation `p` times the key's `k` (Hamilton order) with the game's sums.
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
    const Quaternion& key = rotationKeys_[0].rotation;
    if (parent_) {
        const Quaternion& parentRotation = parent_->poseRotation_;
        poseRotation_ = ParentTimesKey(parentRotation, key);
        posePosition_ = TurnByQuaternion(parentRotation, position_) + parent_->posePosition_;
    } else {
        poseRotation_ = key;
        posePosition_ = TurnByQuaternion(poseRotation_, position_);
    }
    pointsB_.resize(std::ssize(pointsA_));
    std::memcpy(pointsB_.data(), pointsA_.data(), static_cast<std::size_t>(std::ssize(pointsA_)) * sizeof(Vector3));
    translationCursor_ = 0;
    rotationCursor_ = 0;
    normalCursor_ = 0;
    morphCursor_ = 0;
}

void CAnimationNode::Rewind() {
    clock_[0] = 0;
    clock_[1] = 0;
    ResetPose();
    if (mesh_) {
        mesh_->localPosition_ = posePosition_;
        mesh_->localRotation_ = poseRotation_;
        CFigureMesh::OffsetSpan points{pointsB_.data(), std::ssize(pointsB_)};
        mesh_->SetOffsets(&points);
    }
    for (int i = 0; i < std::ssize(children_); ++i) children_[i]->Rewind();
}

// The parts are engine objects (figure mesh classes 0x41-0x44); slot 7 gives the mesh's name.
int CAnimationNode::AttachMeshes(const lib::AlignedArray<CFigureMesh*>* parts) {
    for (int i = 0; i < parts->size; ++i) {
        CFigureMesh* part = parts->data[i];
        if (std::strcmp(part->GetName(), name_.c_str()) != 0) continue;
        mesh_ = part;
        ++mesh_->refCount_;
        if (mesh_) {
            mesh_->localPosition_ = posePosition_;
            mesh_->localRotation_ = poseRotation_;
            CFigureMesh::OffsetSpan points{pointsB_.data(), std::ssize(pointsB_)};
            mesh_->SetOffsets(&points);
        }
        break;
    }
    for (int i = 0; i < std::ssize(children_); ++i) {
        if (children_[i]) children_[i]->AttachMeshes(parts);
    }
    return 1;
}

namespace scene {

void SceneSkin::Rewind() {
    if (root) root->Rewind();   // inlined in the game
    clock[0] = 0;
    clock[1] = 0;
}

int SceneSkin::BindMeshes(const lib::AlignedArray<CFigureMesh*>* parts) {
    if (root) root->AttachMeshes(parts);
    return 1;
}

} // namespace scene
