// The figure animation state's skeleton (class 0x300D; FigureAnimationState in map_object_figures.h): its
// lifetime, the bones the skeleton file links (LinkPiece per link, then FindRoot), the meshes the bones
// carry, and placing the skeleton when the animation or the figure's morph changes. See
// docs/notes/map_objects.md.
#include "base_manager.h"
#include "core_maths.h"
#include "figure_manager.h"
#include "figure_mesh.h"
#include "lib/str.h"
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

// A bone's mesh is a counted figure part (FigureMesh, an engine object).
FigureMesh* AsMesh(EngineObject* part) { return static_cast<FigureMesh*>(part); }

// The root places the skeleton (through its address) at the owner's time factor at its scale z times its
// default scale.
void PlaceRoot(FigureAnimationState* state) {
    CObject3D* owner = state->owner;
    float factor = owner->AnimationFactor(owner->scale.z);
    Vector3 origin;
    origin.z = factor * owner->defaultScale.z;
    origin.y = factor * owner->defaultScale.y;
    origin.x = factor * owner->defaultScale.x;
    state->root->Place(origin);
}

// Inlines DestroyTrack's reset without the bone (the key frames' array emptied through its SetSize(0)).
void ClearTrack(AnimationTrack* track) {
    track->animation = nullptr;
    track->positionCount = 0;
    track->positions = nullptr;
    track->rotationCount = 0;
    track->rotations = nullptr;
    track->vertexCount = 0;
    track->frames.clear();
    track->position = kTrackDefaultPosition;
    track->rotation = kTrackDefaultRotation;
    track->vertices.clear();
    track->time = 0.0f;
}

} // namespace

FigureAnimationState::FigureAnimationState() {
    Reset();
}

FigureAnimationState::~FigureAnimationState() {
    ReleaseBones();
    Reset();
    queue.clear();
    bones.clear();
}

void FigureAnimationState::Destroy() { DestroyState(); }

void FigureAnimationState::Create() {
    changed = 1;
    Reset();
}

void FigureAnimationState::DestroyState() {
    ReleaseBones();
    Reset();
}

void FigureAnimationState::ReleaseBones() {
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        bone->Release();
    }
    bones.clear();
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
    changed = 1;
}

FigureBone* FigureAnimationState::FindBone(std::string_view boneName) {
    for (auto node = bones.begin(); node != bones.end(); ++node)
        if (str::Compare((*node)->name, boneName) == 0) return *node;
    return nullptr;
}

int FigureAnimationState::LinkPiece(FigureModel* model, std::string_view child, std::string_view parentName) {
    BaseManager* manager = FigureManagerOf(this);
    auto* bone = manager->CreateObjectAs<FigureBone>(FigureObjectType::FigureBone, -1);
    bone->name = child;
    bone->name2 = parentName;
    return LinkBone(model, bone);
}

int FigureAnimationState::LinkBone(FigureModel* model, FigureBone* bone) {
    FigureBone* parent = nullptr;
    for (auto& node : bones) {
        if (str::Compare(node->name, bone->name2) == 0) {
            parent = node;
            break;
        }
    }
    if (parent) {
        bool known = false;
        for (auto node = parent->children.begin(); node != parent->children.end() && !known; ++node)
            known = str::Compare((*node)->name, bone->name) == 0;
        if (!known) {
            parent->children.push_back(bone);
            bone->parent = parent;
        }
    }
    bones.push_back(bone);
    bone->AddRef();
    bone->name3 = owner->name;
    bone->morphS = morphS;
    bone->morphT = morphT;
    bone->morphU = morphU;
    OwningFigureManager(bone)->LoadFigureBones(model, bone);
    changed = 1;
    return 1;
}

int FigureAnimationState::AttachMesh(EngineObject* part) {
    changed = 1;
    FigureBone* bone = nullptr;
    for (auto& node : bones) {
        if (str::Compare(node->name, part->GetName()) == 0) {
            bone = node;
            break;
        }
    }
    if (!bone) return 0;
    FigureMesh* old = bone->mesh;
    bone->mesh = AsMesh(part);
    if (part) {
        part->AddRef();
        bone->mesh->localPosition = bone->position;
        bone->mesh->localRotation = bone->rotation;
    }
    if (old) old->Release();
    return 1;
}

int FigureAnimationState::FindRoot() {
    changed = 1;
    for (auto& node : bones) {
        if (node->name2.empty()) {
            root = node;
            return 1;
        }
    }
    return 0;
}

int FigureAnimationState::SetModel(std::string_view model) {
    changed = 1;
    return FigureManagerOf(this)->LoadSkeletonLinks(this, model);
}

int FigureAnimationState::DetachMesh(EngineObject* part) {
    changed = 1;
    for (auto& node : bones) {
        FigureBone* bone = node;
        if (bone->mesh && bone->mesh == AsMesh(part)) {
            FigureMesh* old = bone->mesh;
            bone->mesh = nullptr;
            old->Release();
            return 1;
        }
    }
    return 0;
}

int FigureAnimationState::DetachMeshes() {
    changed = 1;
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        FigureMesh* old = bone->mesh;
        bone->mesh = nullptr;
        if (old) old->Release();
    }
    return 1;
}

void FigureAnimationState::PlaceIfChanged() {
    if (!changed) return;
    changed = 0;
    if (root) PlaceRoot(this);
}

void FigureAnimationState::UpdatePose() {
    if (!changed) return;
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        bone->pose->Evaluate(time);
    }
    PlaceIfChanged();
}

void FigureAnimationState::Rescale(float s, float t, float u) {
    FigureModel* model;
    {
        std::string key = owner->name;
        model = FigureManagerOf(this)->FindModel(&key);
    }
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        bone->morphS = s;
        bone->morphT = t;
        bone->morphU = u;
        OwningFigureManager(bone)->LoadFigureBones(model, bone);
    }
    morphS = s;
    morphT = t;
    morphU = u;
    changed = 0;
    if (root) PlaceRoot(this);
}

std::vector<std::string> FigureAnimationState::BoneNames() {
    std::vector<std::string> names;
    for (auto& node : bones) names.push_back(node->name);
    return names;
}

int FigureAnimationState::ClearBoneTracks() {
    changed = 1;
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        ClearTrack(bone->pose);
        bone->pose->animation = nullptr;
    }
    return 1;
}

int FigureAnimationState::LoadBoneTracks(MappedResource* part, const FigureAnimation* animation) {
    changed = 1;
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        auto* data = static_cast<std::uint8_t*>(part->GetFile(bone->name, nullptr));
        if (!data) ClearTrack(bone->pose);
        else bone->pose->LoadKeyFrames(&data);
        bone->pose->animation = animation;
    }
    return 1;
}

// --- The bone's lifetime (class 0x300C, 0xA0 bytes; FigureBone in map_object_figures.h). The figure
// factory makes bones; the animation state links them.

namespace {

// The mesh is a counted engine object (its release virtual).
void ReleaseMesh(FigureMesh* mesh) { mesh->Release(); }

} // namespace

// The names are members (empty strings before the body).
FigureBone::FigureBone() {
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    CreateBone();
}

void FigureBone::CreateBone() {
    // The original assigns "none" to all three names, not an empty string.
    name = "none";
    name2 = "none";
    name3 = "none";
    parent = nullptr;
    children.clear();
    mesh = nullptr;
    pose = nullptr;
    animator = nullptr;
    position = kTrackDefaultPosition;
    rotation = kTrackDefaultRotation;
    matrices.clear();
    morphS = 0.0f;
    morphT = 0.0f;
    morphU = 0.0f;
}

void FigureBone::DestroyBone() {
    FigureMesh* placed = mesh;
    mesh = nullptr;
    if (placed) ReleaseMesh(placed);
    pose = nullptr;
    if (animator) animator->Release();
    animator = nullptr;
    CreateBone();
}

// The names go after the body (name3, name2, name), then the engine object's part.
FigureBone::~FigureBone() {
    DestroyBone();   // its reset empties the matrices here, the same SetSize
    // The child list's destructor: its vtable, emptied again.
    children.clear();
}

void FigureBone::Create() { CreateBone(); }
void FigureBone::Destroy() { DestroyBone(); }

namespace {
constexpr const char* kBoneClassName = "CAnimationNode";
} // namespace

const char* FigureBone::GetName() { return name.c_str(); }
const char* FigureBone::ClassName() { return kBoneClassName; }
ClassId FigureBone::GetClassId() { return ClassId::FigureBone; }
