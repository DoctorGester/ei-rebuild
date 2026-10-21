// The animated node's lifetime (CAnimationNode, 0x128 bytes, figure.h): constructor and destructor
// body. The fake units' skeletons make them (00449240, 0044A163) and the skin code destroys them
// (005087B0-00508CD0). See docs/notes/map_objects.md.
#include "figure.h"

#include "figure_mesh.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_data.h"

#include <cstdint>
#include <new>

namespace {

constexpr std::uintptr_t kNormalsVtable = std::uintptr_t(0x0073C120);
constexpr std::uintptr_t kMorphFramesVtable = std::uintptr_t(0x0073D9BC);
constexpr int kNoKey = -1;   // the key cursors before the first ResetPose

const Quaternion& DefaultRotation() { return ei::GameData<const Quaternion>(0x007AF0E0); }
const Vector3& DefaultPosition() { return ei::GameData<const Vector3>(0x007AF0F0); }

} // namespace

CAnimationNode* CAnimationNode::Construct() {
    new (&name_) mfc::CString();
    new (&spareName008_) mfc::CString();
    new (&spareName00C_) mfc::CString();
    children_.Construct();
    rotationKeys_.Construct();
    translationKeys_.Construct();
    normals_.Construct(kNormalsVtable);
    morphFrames_.Construct(kMorphFramesVtable);
    defaultRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    poseRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    pointsB_.Construct();
    restRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    pointsA_.Construct();
    clock_[0] = 0;
    rotationCursor_ = kNoKey;
    translationCursor_ = kNoKey;
    morphCursor_ = kNoKey;
    normalCursor_ = kNoKey;
    spare000_ = 0;
    spare010_[2] = 0;
    mesh_ = nullptr;
    clock_[1] = 0;
    spare034_ = 0;
    frameCount_ = 0;
    parent_ = nullptr;
    position_ = DefaultPosition();
    defaultPosition_ = DefaultPosition();
    defaultRotation_ = DefaultRotation();
    spare010_[0] = 0;
    spare010_[1] = 0;
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
    spare000_ = 0;
    pointsA_.FreeData();
    pointsB_.FreeData();
    morphFrames_.vtable = ei::GamePointer<void>(kMorphFramesVtable);
    morphFrames_.FreeData();
    normals_.vtable = ei::GamePointer<void>(kNormalsVtable);
    normals_.FreeData();
    translationKeys_.FreeData();
    rotationKeys_.FreeData();
    children_.FreeData();
    spareName00C_.~CString();
    spareName008_.~CString();
    name_.~CString();
}
EI_REPLACE_METHOD(0x005068C0, CAnimationNode::DestructBody);
