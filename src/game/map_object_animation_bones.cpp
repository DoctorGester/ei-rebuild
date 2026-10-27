// The figure animation state's skeleton (class 0x300D; CFigureAnimationState in map_object_figures.h): its
// lifetime, the bones the skeleton file links (LinkPiece per link, then FindRoot), the meshes the bones
// carry, and placing the skeleton when the animation or the figure's morph changes. See
// docs/notes/map_objects.md.
#include "figure_manager.h"
#include "map_object_animation.h"
#include "map_object_figures.h"

#include "base_manager.h"
#include "core_maths.h"
#include "figure_mesh.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kListBlockSize = 10;
constexpr int kBoneClass = 0x300C;   // FigureBone's class id

int CompareNames(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}

// A bone's mesh is a counted figure part (CFigureMesh, an engine object).
CFigureMesh* AsMesh(CEngineObject* part) { return static_cast<CFigureMesh*>(part); }

template <typename T>
void ConstructList(mfc::CTypedPtrList<T>* list, void* vtable) {
    list->vtable = vtable;
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    list->blocks = nullptr;
    list->blockSize = kListBlockSize;
}

// The root places the skeleton (through its address) at the owner's time factor at its scale z times its
// default scale: x and y from the factor stored to a float, z from it unrounded.
void PlaceRoot(CFigureAnimationState* state) {
    CObject3D* owner = state->owner;
    double factor = owner->AnimationFactor(owner->scale.z);
    float stored = static_cast<float>(factor);
    Vector3 origin;
    origin.z = static_cast<float>(factor * owner->defaultScale.z);
    origin.y = stored * owner->defaultScale.y;
    origin.x = stored * owner->defaultScale.x;
    state->root->Place(&origin);
}

// Inlines DestroyTrack's reset without the bone (the key frames' array emptied through its SetSize(0)).
void ClearTrack(AnimationTrack* track) {
    track->animation = nullptr;
    track->positionCount = 0;
    track->positions = nullptr;
    track->rotationCount = 0;
    track->rotations = nullptr;
    track->vertexCount = 0;
    track->frames.SetSize(0, -1);
    track->position = kTrackDefaultPosition;
    track->rotation = kTrackDefaultRotation;
    track->vertices.RemoveAll();
    track->time = 0.0f;
}

} // namespace

CFigureAnimationState::CFigureAnimationState() {
    ClearFields();
    ConstructList(&bones, mfc::impl::BoneListClass::Vtable());
    ConstructList(&queue, mfc::impl::AnimationStateListClass::Vtable());
    Reset();
}
EI_REWRITTEN_SPECIAL(0x0050C5F0, CFigureAnimationState::CFigureAnimationState);

CFigureAnimationState::~CFigureAnimationState() {
    ReleaseBones();
    Reset();
    queue.vtable = mfc::impl::AnimationStateListClass::Vtable();
    queue.RemoveAll();
    bones.vtable = mfc::impl::BoneListClass::Vtable();
    bones.RemoveAll();
}
EI_REWRITTEN_SPECIAL(0x0050C730, CFigureAnimationState::~CFigureAnimationState);

EI_REWRITTEN_SPECIAL(0x0050C710, CFigureAnimationState::~CFigureAnimationState);   // deleting

int CFigureAnimationState::Create() { return CreateState(); }
void CFigureAnimationState::Destroy() { DestroyState(); }

int CFigureAnimationState::CreateState() {
    changed = 1;
    Reset();
    return 1;
}
EI_REWRITTEN(0x0050C860, CFigureAnimationState::CreateState);

void CFigureAnimationState::DestroyState() {
    ReleaseBones();
    Reset();
}
EI_REWRITTEN(0x0050C8E0, CFigureAnimationState::DestroyState);

void CFigureAnimationState::ReleaseBones() {
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        bone->Release();
    }
    bones.RemoveAll();
}
EI_REWRITTEN(0x0050C990, CFigureAnimationState::ReleaseBones);

void CFigureAnimationState::Reset() {
    onEnd = nullptr;
    owner = nullptr;
    root = nullptr;
    bones.RemoveAll();
    current = nullptr;
    queue.RemoveAll();
    time = 0.0f;
    morphS = 0.0f;
    morphT = 0.0f;
    morphU = 0.0f;
    changed = 1;
}
EI_REWRITTEN(0x0050C9E0, CFigureAnimationState::Reset);

FigureBone* CFigureAnimationState::FindBone(const char* boneName) {
    for (auto* node = bones.head; node; node = node->next)
        if (CompareNames(node->data->name.c_str(), boneName) == 0) return node->data;
    return nullptr;
}
EI_REWRITTEN(0x0050CC20, CFigureAnimationState::FindBone);

int CFigureAnimationState::LinkPiece(FigureModel* model, const char* child, const char* parentName) {
    CBaseManager* manager = FigureManagerOf(this);
    auto* bone = manager->CreateObjectAs<FigureBone>(kBoneClass, -1);
    bone->name = child;
    bone->name2 = parentName;
    return LinkBone(model, bone);
}
EI_REWRITTEN(0x0050CC60, CFigureAnimationState::LinkPiece);

int CFigureAnimationState::LinkBone(FigureModel* model, FigureBone* bone) {
    const char* parentName = bone->name2.c_str();
    FigureBone* parent = nullptr;
    for (auto* node = bones.head; node; node = node->next) {
        if (CompareNames(node->data->name.c_str(), parentName) == 0) {
            parent = node->data;
            break;
        }
    }
    if (parent) {
        bool known = false;
        for (auto* node = parent->children.head; node && !known; node = node->next)
            known = CompareNames(node->data->name.c_str(), bone->name.c_str()) == 0;
        if (!known) {
            parent->children.AddTail(bone);
            bone->parent = parent;
        }
    }
    bones.AddTail(bone);
    ++bone->refCount_;
    bone->name3 = owner->name;
    bone->morphS = morphS;
    bone->morphT = morphT;
    bone->morphU = morphU;
    OwningFigureManager(bone)->LoadFigureBones(model, bone);
    changed = 1;
    return 1;
}
EI_REWRITTEN(0x0050CD70, CFigureAnimationState::LinkBone);

int CFigureAnimationState::AttachMesh(CEngineObject* part) {
    changed = 1;
    FigureBone* bone = nullptr;
    for (auto* node = bones.head; node; node = node->next) {
        if (CompareNames(node->data->name.c_str(), part->GetName()) == 0) {
            bone = node->data;
            break;
        }
    }
    if (!bone) return 0;
    CFigureMesh* old = bone->mesh;
    bone->mesh = AsMesh(part);
    if (part) {
        ++part->refCount_;
        bone->mesh->localPosition_ = bone->position;
        bone->mesh->localRotation_ = bone->rotation;
    }
    if (old) old->Release();
    return 1;
}
EI_REWRITTEN(0x0050D0E0, CFigureAnimationState::AttachMesh);

int CFigureAnimationState::FindRoot() {
    changed = 1;
    for (auto* node = bones.head; node; node = node->next) {
        if (node->data->name2.empty()) {
            root = node->data;
            return 1;
        }
    }
    return 0;
}
EI_REWRITTEN(0x0050D2E0, CFigureAnimationState::FindRoot);

int CFigureAnimationState::SetModel(const char* model) {
    changed = 1;
    return FigureManagerOf(this)->LoadSkeletonLinks(this, model);
}
EI_REWRITTEN(0x0050D320, CFigureAnimationState::SetModel);

int CFigureAnimationState::DetachMesh(CEngineObject* part) {
    changed = 1;
    for (auto* node = bones.head; node; node = node->next) {
        FigureBone* bone = node->data;
        if (bone->mesh && bone->mesh == AsMesh(part)) {
            CFigureMesh* old = bone->mesh;
            bone->mesh = nullptr;
            old->Release();
            return 1;
        }
    }
    return 0;
}
EI_REWRITTEN(0x0050D350, CFigureAnimationState::DetachMesh);

int CFigureAnimationState::DetachMeshes() {
    changed = 1;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        CFigureMesh* old = bone->mesh;
        bone->mesh = nullptr;
        if (old) old->Release();
    }
    return 1;
}
EI_REWRITTEN(0x0050D3A0, CFigureAnimationState::DetachMeshes);

void CFigureAnimationState::PlaceIfChanged() {
    if (!changed) return;
    changed = 0;
    if (root) PlaceRoot(this);
}
EI_REWRITTEN(0x0050D3E0, CFigureAnimationState::PlaceIfChanged);

void CFigureAnimationState::UpdatePose() {
    if (!changed) return;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        bone->pose->Evaluate(time);
    }
    PlaceIfChanged();
}
EI_REWRITTEN(0x0050D450, CFigureAnimationState::UpdatePose);

void CFigureAnimationState::Stop() {
    queue.RemoveAll();
    Play(nullptr);
    changed = 1;
}
EI_REWRITTEN(0x0050D5A0, CFigureAnimationState::Stop);

void CFigureAnimationState::Rescale(float s, float t, float u) {
    FigureModel* model;
    {
        const char* name = owner->name;
        stlp::string key(name, name + std::strlen(name));
        model = FigureManagerOf(this)->FindModel(&key);
    }
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
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
EI_REWRITTEN(0x0050D7C0, CFigureAnimationState::Rescale);

void CFigureAnimationState::CollectBoneNames(mfc::CStringArray* out) {
    for (auto* node = bones.head; node; node = node->next) out->SetAtGrow(out->size, node->data->name);
}
EI_REWRITTEN(0x0050D920, CFigureAnimationState::CollectBoneNames);

// AlignedPtrArray::SetSize's copy for the animation tracks' arrays (the key frames' vertex sets). Registered
// on the template (map_object_figures.cpp).
EI_REWRITTEN(0x0050DFF0, AlignedPtrArray::SetSize);

int CFigureAnimationState::ClearBoneTracks() {
    changed = 1;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        ClearTrack(bone->pose);
        bone->pose->animation = nullptr;
    }
    return 1;
}
EI_REWRITTEN(0x0050D950, CFigureAnimationState::ClearBoneTracks);

int CFigureAnimationState::LoadBoneTracks(CFigureModelPart* part, const FigureAnimation* animation) {
    changed = 1;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        auto* data = static_cast<std::uint8_t*>(part->ReadEntry(bone->name.c_str(), nullptr));
        if (!data) ClearTrack(bone->pose);
        else bone->pose->LoadKeyFrames(&data);
        bone->pose->animation = animation;
    }
    return 1;
}
EI_REWRITTEN(0x0050DA10, CFigureAnimationState::LoadBoneTracks);
