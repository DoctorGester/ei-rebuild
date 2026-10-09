// The animated node's lifetime (CAnimationNode, 0x128 bytes, figure.h): constructor and destructor
// body. The fake units' skeletons make them (00449240, 0044A163) and the skin code destroys them
// (005087B0-00508CD0). See docs/notes/map_objects.md.
#include "figure.h"

#include "figure_mesh.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <new>

namespace {

constexpr std::uintptr_t kNormalsVtable = std::uintptr_t(0x0073C120);
constexpr std::uintptr_t kMorphFramesVtable = std::uintptr_t(0x0073D9BC);
constexpr int kNoKey = -1;   // the key cursors before the first ResetPose

const Quaternion& DefaultRotation() { return *reinterpret_cast<const Quaternion*>(std::uintptr_t(0x007AF0E0)); }
const Vector3& DefaultPosition() { return *reinterpret_cast<const Vector3*>(std::uintptr_t(0x007AF0F0)); }

} // namespace

CAnimationNode* CAnimationNode::Construct() {
    new (&name_) mfc::CString();
    new (&unknown_008) mfc::CString();
    new (&unknown_00C) mfc::CString();
    children_.Construct();
    rotationKeys_.Construct();
    translationKeys_.Construct();
    normals_.Construct(kNormalsVtable);
    morphFrames_.Construct(kMorphFramesVtable);
    unknown_09C = {1.0f, 0.0f, 0.0f, 0.0f};
    poseRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    pointsB_.Construct();
    restRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    pointsA_.Construct();
    clock_[0] = 0;
    rotationCursor_ = kNoKey;
    translationCursor_ = kNoKey;
    morphCursor_ = kNoKey;
    normalCursor_ = kNoKey;
    unknown_000 = 0;
    unknown_010[2] = 0;
    mesh_ = nullptr;
    clock_[1] = 0;
    unknown_034 = 0;
    frameCount_ = 0;
    parent_ = nullptr;
    position_ = DefaultPosition();
    unknown_0AC = DefaultPosition();
    unknown_09C = DefaultRotation();
    unknown_010[0] = 0;
    unknown_010[1] = 0;
    return this;
}
EI_REPLACE_METHOD(0x00506710, CAnimationNode::Construct);

void CAnimationNode::DestructBody() {
    if (mesh_) {
        --mesh_->refCount_;
        mesh_ = nullptr;
    }
    for (int i = 0; i < children_.size; ++i) {
        if (CAnimationNode* child = children_.data[i]) {
            child->DestructBody();
            gamecrt::operator_delete(child);
            children_.data[i] = nullptr;
        }
    }
    children_.SetSize(0, -1);
    unknown_000 = 0;
    pointsA_.FreeData();
    pointsB_.FreeData();
    morphFrames_.vtable = reinterpret_cast<void*>(kMorphFramesVtable);
    morphFrames_.FreeData();
    normals_.vtable = reinterpret_cast<void*>(kNormalsVtable);
    normals_.FreeData();
    translationKeys_.FreeData();
    rotationKeys_.FreeData();
    children_.FreeData();
    unknown_00C.~CString();
    unknown_008.~CString();
    name_.~CString();
}
EI_REPLACE_METHOD(0x005068C0, CAnimationNode::DestructBody);
