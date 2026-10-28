// The animated node's lifetime (CAnimationNode, 0x128 bytes, figure.h): constructor and destructor body. The
// fake units' skeletons make them and the skin code destroys them See docs/notes/map_objects.md.
#include "figure.h"

#include "figure_mesh.h"

#include "lib/crt.h"
#include "lib/mfc_slots.h"
#include "game_data.h"

#include <cstdint>
#include <memory>
#include <new>

namespace {

constexpr int kNoKey = -1;   // the key cursors before the first ResetPose

constexpr Quaternion kDefaultRotation = /*007AF0E0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // static initialisers: the identity
constexpr Vector3 kDefaultPosition = /*007AF0F0*/ {};               // and the origin

} // namespace

CAnimationNode* CAnimationNode::Construct() {
    std::construct_at(&name_);
    std::construct_at(&spareName008_);
    std::construct_at(&spareName00C_);
    children_.Construct();
    rotationKeys_.Construct();
    translationKeys_.Construct();
    normals_.Construct(mfc::impl::Vector3ArrayClass::Vtable());
    morphFrames_.Construct(mfc::impl::AlignedAt8ArrayClass::Vtable());
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
    position_ = kDefaultPosition;
    defaultPosition_ = kDefaultPosition;
    defaultRotation_ = kDefaultRotation;
    spare010_[0] = 0;
    spare010_[1] = 0;
    return this;
}

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
    morphFrames_.vtable = mfc::impl::AlignedAt8ArrayClass::Vtable();
    morphFrames_.FreeData();
    normals_.vtable = mfc::impl::Vector3ArrayClass::Vtable();
    normals_.FreeData();
    translationKeys_.FreeData();
    rotationKeys_.FreeData();
    children_.FreeData();
    std::destroy_at(&spareName00C_);
    std::destroy_at(&spareName008_);
    std::destroy_at(&name_);
}
