// The figure animation state's skeleton (class 0x300D, vtable 0073DA4C; CFigureAnimationState in
// map_object_figures.h): its lifetime, the bones the skeleton file links (005AC880 calls LinkPiece
// per link, then FindRoot), the meshes the bones carry, and placing the skeleton when the
// animation or the figure's morph changes. See docs/notes/map_objects.md.
#include "figure_manager.h"
#include "map_object_animation.h"
#include "map_object_figures.h"

#include "base_manager.h"
#include "core_maths.h"
#include "figure_mesh.h"
#include "originals_units.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uintptr_t kStateVtable = std::uintptr_t(0x0073DA4C);
constexpr std::uintptr_t kBoneListVtable = std::uintptr_t(0x0073DA40);
constexpr std::uintptr_t kQueueVtable = std::uintptr_t(0x0073DA80);
constexpr int kListBlockSize = 10;
constexpr int kBoneClass = 0x300C;   // FigureBone's class id

int CompareNames(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}

CEngineObject* FigureManagerOf(const CFigureAnimationState* state) {
    return static_cast<CWorld*>(state->ManagerContext())->FigureManager();
}

// A bone's mesh is a counted figure part (an engine object; figure_mesh.h declares no base).
CEngineObject* AsObject(CFigureMesh* mesh) { return reinterpret_cast<CEngineObject*>(mesh); }
CFigureMesh* AsMesh(CEngineObject* part) { return reinterpret_cast<CFigureMesh*>(part); }

template <typename T>
void ConstructList(mfc::CTypedPtrList<T>* list, std::uintptr_t vtableAddress) {
    list->vtable = ei::GamePointer<void>(vtableAddress);
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    list->blocks = nullptr;
    list->blockSize = kListBlockSize;
}

// The root places the skeleton (0050C230, through its address) at the owner's time factor at
// its scale z times its default scale: x and y from the factor stored to a float, z from it
// unrounded (0050D3E0, 0050D450, 0050D7C0).
void PlaceRoot(CFigureAnimationState* state) {
    CObject3D* owner = state->owner;
    double factor = owner->AnimationFactor(owner->scale.z);
    float stored = static_cast<float>(factor);
    Vector3 origin;
    origin.z = static_cast<float>(factor * owner->defaultScale.z);
    origin.y = stored * owner->defaultScale.y;
    origin.x = stored * owner->defaultScale.x;
    static_cast<FigureBone*>(state->root)->Place(&origin);
}

// 0050D950 / 0050DA10 inline DestroyTrack's reset without the bone (the key frames' array emptied
// through its SetSize(0), 0050DFF0).
void ClearTrack(AnimationTrack* track) {
    track->animation = nullptr;
    track->positionCount = 0;
    track->positions = nullptr;
    track->rotationCount = 0;
    track->rotations = nullptr;
    track->vertexCount = 0;
    track->frames.SetSize(0, -1);
    track->position = ei::GameData<const Vector3>(0x007AF110);
    track->rotation = ei::GameData<const Quaternion>(0x007AF100);
    track->vertices.RemoveAll();
    track->time = 0.0f;
}

} // namespace

CFigureAnimationState* CFigureAnimationState::Construct() {
    ClearFields();
    ConstructList(&bones, kBoneListVtable);
    ConstructList(&queue, kQueueVtable);
    ei::SetVtable(this, kStateVtable);
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x0050C5F0, CFigureAnimationState::Construct);

void CFigureAnimationState::DestructBody() {
    ei::SetVtable(this, kStateVtable);
    ReleaseBones();
    Reset();
    queue.vtable = ei::GamePointer<void>(kQueueVtable);
    queue.RemoveAll();
    bones.vtable = ei::GamePointer<void>(kBoneListVtable);
    bones.RemoveAll();
    DestructBase();
}
EI_REPLACE_METHOD(0x0050C730, CFigureAnimationState::DestructBody);

CFigureAnimationState* CFigureAnimationState::DeleteState(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0050C710, CFigureAnimationState::DeleteState);

int CFigureAnimationState::CreateState() {
    changed = 1;
    Reset();
    return 1;
}
EI_REPLACE_METHOD(0x0050C860, CFigureAnimationState::CreateState);

void CFigureAnimationState::DestroyState() {
    ReleaseBones();
    Reset();
}
EI_REPLACE_METHOD(0x0050C8E0, CFigureAnimationState::DestroyState);

void CFigureAnimationState::ReleaseBones() {
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        bone->Release();
    }
    bones.RemoveAll();
}
EI_REPLACE_METHOD(0x0050C990, CFigureAnimationState::ReleaseBones);

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
EI_REPLACE_METHOD(0x0050C9E0, CFigureAnimationState::Reset);

FigureBone* CFigureAnimationState::FindBone(const char* boneName) {
    for (auto* node = bones.head; node; node = node->next)
        if (CompareNames(node->data->name, boneName) == 0) return node->data;
    return nullptr;
}
EI_REPLACE_METHOD(0x0050CC20, CFigureAnimationState::FindBone);

int CFigureAnimationState::LinkPiece(void* model, const char* child, const char* parentName) {
    auto* manager = static_cast<CBaseManager*>(FigureManagerOf(this));
    auto* bone = static_cast<FigureBone*>(manager->CreateObject(kBoneClass, -1));
    bone->name = child;
    bone->name2 = parentName;
    return LinkBone(model, bone);
}
EI_REPLACE_METHOD(0x0050CC60, CFigureAnimationState::LinkPiece);

int CFigureAnimationState::LinkBone(void* model, FigureBone* bone) {
    const char* parentName = bone->name2;
    FigureBone* parent = nullptr;
    for (auto* node = bones.head; node; node = node->next) {
        if (CompareNames(node->data->name, parentName) == 0) {
            parent = node->data;
            break;
        }
    }
    if (parent) {
        bool known = false;
        for (auto* node = parent->children.head; node && !known; node = node->next)
            known = CompareNames(node->data->name, bone->name) == 0;
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
    static_cast<CFigureManager*>(bone->owner_)->LoadFigureBones(static_cast<FigureModel*>(model), bone);
    changed = 1;
    return 1;
}
EI_REPLACE_METHOD(0x0050CD70, CFigureAnimationState::LinkBone);

int CFigureAnimationState::AttachMesh(CEngineObject* part) {
    changed = 1;
    FigureBone* bone = nullptr;
    for (auto* node = bones.head; node; node = node->next) {
        if (CompareNames(node->data->name, part->GetName()) == 0) {
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
    if (old) AsObject(old)->Release();
    return 1;
}
EI_REPLACE_METHOD(0x0050D0E0, CFigureAnimationState::AttachMesh);

int CFigureAnimationState::FindRoot() {
    changed = 1;
    for (auto* node = bones.head; node; node = node->next) {
        if (node->data->name2.GetLength() == 0) {
            root = node->data;
            return 1;
        }
    }
    return 0;
}
EI_REPLACE_METHOD(0x0050D2E0, CFigureAnimationState::FindRoot);

int CFigureAnimationState::SetModel(const char* model) {
    changed = 1;
    return static_cast<CFigureManager*>(FigureManagerOf(this))->LoadSkeletonLinks(this, model);
}
EI_REPLACE_METHOD(0x0050D320, CFigureAnimationState::SetModel);

int CFigureAnimationState::DetachMesh(CEngineObject* part) {
    changed = 1;
    for (auto* node = bones.head; node; node = node->next) {
        FigureBone* bone = node->data;
        if (bone->mesh && bone->mesh == AsMesh(part)) {
            CFigureMesh* old = bone->mesh;
            bone->mesh = nullptr;
            AsObject(old)->Release();
            return 1;
        }
    }
    return 0;
}
EI_REPLACE_METHOD(0x0050D350, CFigureAnimationState::DetachMesh);

int CFigureAnimationState::DetachMeshes() {
    changed = 1;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        CFigureMesh* old = bone->mesh;
        bone->mesh = nullptr;
        if (old) AsObject(old)->Release();
    }
    return 1;
}
EI_REPLACE_METHOD(0x0050D3A0, CFigureAnimationState::DetachMeshes);

void CFigureAnimationState::PlaceIfChanged() {
    if (!changed) return;
    changed = 0;
    if (root) PlaceRoot(this);
}
EI_REPLACE_METHOD(0x0050D3E0, CFigureAnimationState::PlaceIfChanged);

void CFigureAnimationState::UpdatePose() {
    if (!changed) return;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        static_cast<AnimationTrack*>(bone->pose)->Evaluate(time);
    }
    PlaceIfChanged();
}
EI_REPLACE_METHOD(0x0050D450, CFigureAnimationState::UpdatePose);

void CFigureAnimationState::Stop() {
    queue.RemoveAll();
    Play(nullptr);
    changed = 1;
}
EI_REPLACE_METHOD(0x0050D5A0, CFigureAnimationState::Stop);

void CFigureAnimationState::Rescale(float s, float t, float u) {
    void* model;
    {
        const char* name = owner->name;
        stlp::string key(name, name + std::strlen(name));
        model = static_cast<CFigureManager*>(FigureManagerOf(this))->FindModel(&key);
    }
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        bone->morphS = s;
        bone->morphT = t;
        bone->morphU = u;
        static_cast<CFigureManager*>(bone->owner_)->LoadFigureBones(static_cast<FigureModel*>(model), bone);
    }
    morphS = s;
    morphT = t;
    morphU = u;
    changed = 0;
    if (root) PlaceRoot(this);
}
EI_REPLACE_METHOD(0x0050D7C0, CFigureAnimationState::Rescale);

void CFigureAnimationState::CollectBoneNames(mfc::CStringArray* out) {
    for (auto* node = bones.head; node; node = node->next) out->SetAtGrow(out->size, node->data->name);
}
EI_REPLACE_METHOD(0x0050D920, CFigureAnimationState::CollectBoneNames);

// 0050DFF0 (RET 8): AlignedPtrArray::SetSize's copy for the animation tracks' arrays (the key
// frames' vertex sets; 0050A720, 0050C552). Registered on the template (map_object_figures.cpp).
EI_REPLACE_METHOD(0x0050DFF0, AlignedPtrArray::SetSize);

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
EI_REPLACE_METHOD(0x0050D950, CFigureAnimationState::ClearBoneTracks);

int CFigureAnimationState::LoadBoneTracks(CFigureModelPart* part, const FigureAnimation* animation) {
    changed = 1;
    for (auto* node = bones.head; node;) {
        FigureBone* bone = node->data;
        node = node->next;
        auto* data = static_cast<std::uint8_t*>(part->ReadEntry(bone->name, nullptr));
        if (!data) ClearTrack(bone->pose);
        else bone->pose->LoadKeyFrames(&data);
        bone->pose->animation = animation;
    }
    return 1;
}
EI_REPLACE_METHOD(0x0050DA10, CFigureAnimationState::LoadBoneTracks);
