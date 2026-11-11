// The figure animation state's skeleton (class 0x300D; FigureAnimationState in objects/object3d.h): its
// lifetime, the bones the skeleton file links (LinkPiece per link, then FindRoot), the meshes the bones
// carry, and placing the skeleton when the animation or the figure's morph changes. FigureBone::Place is
// the skeleton's pose: each bone of a figure takes its place from its parent and its animator, and places
// the mesh it carries. See docs/notes/map_objects.md.
#include "game/objects/object3d.h"   // FigureBone, AnimationDatabase

#include "game/core/base_manager.h"
#include "game/core/core_maths.h"
#include "game/models/animation.h"   // AnimationTrack
#include "game/models/figure_manager.h"
#include "game/models/figure_mesh.h"   // FigureMesh

#include "lib/containers.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr const char* kBoneClassName = "CAnimationNode";

// The root places the skeleton at the owner's time factor at its scale z times its
// default scale.
void PlaceRoot(FigureAnimationState* state) {
    Object3D* owner = state->owner;
    float factor = owner->AnimationFactor(owner->scale.z);
    Vector3 origin = factor * owner->defaultScale;
    state->root->Place(origin, state->BlendWeight());
    state->placedAt = platform::FrameClock();
    if (state->poseShown) state->seenMoving = true;
    state->poseShown = false;
}

} // namespace

FigureAnimationState::FigureAnimationState() {
    Reset();
}

FigureAnimationState::~FigureAnimationState() { FigureAnimationState::Destroy(); }

void FigureAnimationState::Create() {
    changed = true;
    Reset();
}

void FigureAnimationState::Destroy() {
    bones.clear();   // each bone's reference released, first to last
    Reset();
}

void FigureAnimationState::Reset() {
    onEnd = nullptr;
    owner = nullptr;
    root = nullptr;
    bones.clear();
    current = nullptr;
    queue.clear();
    time = 0.0f;
    morphS = 0.0f;
    morphT = 0.0f;
    morphU = 0.0f;
    changed = true;
    blendStart = 0;
    blendLength = 0;
    placedAt = 0;
    poseShown = false;
    seenMoving = false;
}

FigureBone* FigureAnimationState::FindBone(std::string_view boneName) {
    for (FigureBone* bone : bones)
        if (str::Compare(bone->name, boneName) == 0) return bone;
    return nullptr;
}

void FigureAnimationState::LinkPiece(FigureModel* model, std::string_view child, std::string_view parentName) {
    BaseManager* manager = FigureManagerOf(this);
    auto* bone = manager->CreateObjectAs<FigureBone>(FigureObjectType::FigureBone, -1);
    bone->name = child;
    bone->parentName = parentName;
    LinkBone(model, bone);
}

void FigureAnimationState::LinkBone(FigureModel* model, FigureBone* bone) {
    FigureBone* parent = nullptr;
    for (FigureBone* candidate : bones) {
        if (str::Compare(candidate->name, bone->parentName) == 0) {
            parent = candidate;
            break;
        }
    }
    if (parent) {
        const bool known = std::ranges::any_of(
            parent->children, [&](const FigureBone* child) { return str::Compare(child->name, bone->name) == 0; });
        if (!known) {
            parent->children.push_back(bone);
            bone->parent = parent;
        }
    }
    bones.push_back(bone);
    bone->modelName = owner->name;
    bone->morphS = morphS;
    bone->morphT = morphT;
    bone->morphU = morphU;
    FigureManagerOf(bone)->LoadFigureBones(model, bone);
    changed = true;
}

int FigureAnimationState::AttachMesh(FigureMesh* part) {
    changed = true;
    FigureBone* bone = nullptr;
    for (FigureBone* candidate : bones) {
        if (str::Compare(candidate->name, part->GetName()) == 0) {
            bone = candidate;
            break;
        }
    }
    if (!bone) return 0;
    bone->mesh.Assign(part);
    if (part) {
        part->localPosition = bone->position;
        part->localRotation = bone->rotation;
    }
    return 1;
}

int FigureAnimationState::FindRoot() {
    changed = true;
    for (FigureBone* bone : bones) {
        if (bone->parentName.empty()) {
            root = bone;
            return 1;
        }
    }
    return 0;
}

int FigureAnimationState::SetModel(std::string_view model) {
    changed = true;
    return FigureManagerOf(this)->LoadSkeletonLinks(this, model);
}

int FigureAnimationState::DetachMesh(FigureMesh* part) {
    changed = true;
    for (FigureBone* bone : bones) {
        if (bone->mesh && bone->mesh == part) {
            bone->mesh.Reset();
            return 1;
        }
    }
    return 0;
}

void FigureAnimationState::DetachMeshes() {
    changed = true;
    for (FigureBone* bone : bones) bone->mesh.Reset();
}

void FigureAnimationState::PlaceIfChanged() {
    if (!changed) return;
    changed = false;
    if (root) PlaceRoot(this);
}

void FigureAnimationState::UpdatePose() {
    if (changed) {
        for (FigureBone* bone : bones) bone->pose->Evaluate(time);
    } else if (blendLength != 0) {
        changed = true;
    } else {
        return;
    }
    PlaceIfChanged();
}

void FigureAnimationState::PlaceForDraw() {
    UpdatePose();
    poseShown = true;
}

void FigureAnimationState::Rescale(float s, float t, float u) {
    FigureModel* model = FigureManagerOf(this)->FindModel(owner->name);
    for (FigureBone* bone : bones) {
        bone->morphS = s;
        bone->morphT = t;
        bone->morphU = u;
        FigureManagerOf(bone)->LoadFigureBones(model, bone);
    }
    morphS = s;
    morphT = t;
    morphU = u;
    changed = false;
    if (root) PlaceRoot(this);
}

std::vector<std::string> FigureAnimationState::BoneNames() {
    std::vector<std::string> names;
    for (const FigureBone* bone : bones) names.push_back(bone->name);
    return names;
}

void FigureAnimationState::ClearBoneTracks() {
    changed = true;
    for (FigureBone* bone : bones) bone->pose->ClearKeys();
}

void FigureAnimationState::LoadBoneTracks(MappedResource* part, const FigureAnimation* animation) {
    changed = true;
    for (FigureBone* bone : bones) {
        auto* data = static_cast<std::uint8_t*>(part->GetFile(bone->name, nullptr));
        if (!data) bone->pose->ClearKeys();
        else bone->pose->LoadKeyFrames(&data);
        bone->pose->animation = animation;
    }
}

// --- The bone's lifetime (FigureBone in objects/object3d.h) ---

// The figure factory makes bones; the animation state links them.

FigureBone::FigureBone() {
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    FigureBone::Create();
}

void FigureBone::Create() {
    // The original assigns "none" to all three names, not an empty string.
    name = "none";
    parentName = "none";
    modelName = "none";
    parent = nullptr;
    children.clear();
    mesh.Reset();
    pose = nullptr;
    animator.Reset();
    position = kTrackDefaultPosition;
    rotation = kTrackDefaultRotation;
    matrices.clear();
    morphS = 0.0f;
    morphT = 0.0f;
    morphU = 0.0f;
    shownTurn = kTrackDefaultRotation;
    shownPosition = kTrackDefaultPosition;
    shown = false;
    fromTurn = kTrackDefaultRotation;
    fromPosition = kTrackDefaultPosition;
    fromOffsets.clear();
    blendFrom = false;
    blendedOffsets.clear();
}

void FigureBone::Destroy() {
    mesh.Reset();
    pose = nullptr;
    animator.Reset();
    FigureBone::Create();
}

FigureBone::~FigureBone() { FigureBone::Destroy(); }

std::string_view FigureBone::GetName() { return name; }
const char* FigureBone::ClassName() { return kBoneClassName; }
ClassId FigureBone::GetClassId() { return ClassId::FigureBone; }

// --- FigureBone::Place ---

void FigureBone::Place(const Vector3& origin, float blend) {
    const bool blended = blend < 1.0f && blendFrom;
    if (parent && pose) {
        position = parent->rotation.Rotate(offset) + parent->position;
        Quaternion turn = pose->rotation;
        if (blended) turn.Slerp(blend, fromTurn, pose->rotation);
        rotation = turn * parent->rotation;
        shownTurn = turn;
        shown = true;
    } else if (pose) {
        Vector3 at;
        if (pose->animation) {
            float factor = pose->animation->database->TimeFactor(morphU);
            at = {factor * pose->position.x, factor * pose->position.y, factor * pose->position.z};
        } else {
            at = pose->position;
        }
        Quaternion turn = pose->rotation;
        if (blended) {
            at = Lerp(fromPosition, at, blend);
            turn.Slerp(blend, fromTurn, pose->rotation);
        }
        rotation = turn;
        position = at - origin;
        shownTurn = turn;
        shownPosition = at;
        shown = true;
    }
    if (mesh) {
        mesh->localPosition = position;
        mesh->localRotation = rotation;
        // The pose's morphed vertices (none without a pose), within a blend lerped from the outgoing ones when
        // both have as many.
        static const std::vector<Vector3> kNoOffsets;
        const std::vector<Vector3>& poseOffsets = pose ? pose->vertices : kNoOffsets;
        const std::vector<Vector3>* offsets = &poseOffsets;
        if (blended && !fromOffsets.empty() && fromOffsets.size() == poseOffsets.size()) {
            blendedOffsets.resize(fromOffsets.size());
            for (std::size_t i = 0; i < fromOffsets.size(); ++i)
                blendedOffsets[i] = Lerp(fromOffsets[i], poseOffsets[i], blend);
            offsets = &blendedOffsets;
        }
        FigureMesh::OffsetSpan vertices{offsets->data(), static_cast<int>(lib::Count(*offsets))};
        mesh->SetOffsets(&vertices);
    }
    for (FigureBone* child : children) child->Place(origin, blend);
}
