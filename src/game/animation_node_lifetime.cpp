// The animated node's lifetime (CAnimationNode, 0x128 bytes, figure.h): constructor and destructor body. The
// fake units' skeletons make them and the skin code destroys them See docs/notes/map_objects.md.
#include "figure.h"

#include "figure_mesh.h"
#include "record_arrays.h"

#include "lib/crt.h"
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
    std::construct_at(&children_);
    std::construct_at(&rotationKeys_);
    std::construct_at(&translationKeys_);
    std::construct_at(&normals_);
    morphFrames_.Construct(recordarrays::MorphFrameArrayClass::Vtable());
    defaultRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    poseRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    std::construct_at(&pointsB_);
    restRotation_ = {1.0f, 0.0f, 0.0f, 0.0f};
    std::construct_at(&pointsA_);
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
    for (int i = 0; i < std::ssize(children_); ++i) {
        if (CAnimationNode* child = children_[i]) {
            child->DestructBody();
            gamecrt::operator_delete(child);
            children_[i] = nullptr;
        }
    }
    children_.clear();
    spare000_ = 0;
    lib::FreeStorage(pointsA_);
    lib::FreeStorage(pointsB_);
    morphFrames_.vtable = recordarrays::MorphFrameArrayClass::Vtable();
    morphFrames_.FreeData();
    std::destroy_at(&normals_);
    lib::FreeStorage(translationKeys_);
    lib::FreeStorage(rotationKeys_);
    lib::FreeStorage(children_);
    std::destroy_at(&spareName00C_);
    std::destroy_at(&spareName008_);
    std::destroy_at(&name_);
}
