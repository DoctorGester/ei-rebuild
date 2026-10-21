// The skeleton bone's lifetime (class 0x300C, 0xA0 bytes; FigureBone in
// map_object_figures.h): constructor, reset (slot 2), Destroy (slot 3), destructor and deleting
// destructor. The figure factory (005AAB57) makes bones; the animation state links them (0050CC60).
// See docs/notes/map_objects.md.
#include "map_object_figures.h"   // FigureBone

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstdint>
#include <new>

namespace {

constexpr std::uintptr_t kChildListVtable = std::uintptr_t(0x0073DA40);
constexpr int kChildBlockSize = 10;

// The animation tracks' defaults (AnimationTrack::Evaluate without key frames).
const Vector3& DefaultPosition() { return ei::GameData<const Vector3>(0x007AF110); }
const Quaternion& DefaultRotation() { return ei::GameData<const Quaternion>(0x007AF100); }

// The mesh is a counted engine object (its slot 4 releases it).
void ReleaseMesh(CFigureMesh* mesh) { reinterpret_cast<CEngineObject*>(mesh)->Release(); }

} // namespace

// The names are members (empty strings before the body).
FigureBone::FigureBone() {
    ClearFields();
    children.vtable = ei::GamePointer<void>(kChildListVtable);
    children.count = 0;
    children.free = nullptr;
    children.tail = nullptr;
    children.head = nullptr;
    children.blocks = nullptr;
    children.blockSize = kChildBlockSize;
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    matrices.data = nullptr;
    matrices.growBy = 0;
    matrices.capacity = 0;
    matrices.size = 0;
    CreateBone();
}

FigureBone* FigureBone::Construct() { return new (this) FigureBone(); }
EI_REWRITTEN(0x0050AF40, FigureBone::Construct);

int FigureBone::CreateBone() {
    // The original assigns "none" (007846F0) to all three names, not an empty string.
    name = "none";
    name2 = "none";
    name3 = "none";
    parent = nullptr;
    children.RemoveAll();
    mesh = nullptr;
    pose = nullptr;
    animator = nullptr;
    counted54 = nullptr;
    counted58 = nullptr;
    position = DefaultPosition();
    rotation = DefaultRotation();
    matrices.SetSizeDiscard(0);
    morphS = 0.0f;
    morphT = 0.0f;
    morphU = 0.0f;
    return 1;
}
EI_REWRITTEN(0x0050B370, FigureBone::CreateBone);

void FigureBone::DestroyBone() {
    CFigureMesh* placed = mesh;
    mesh = nullptr;
    if (placed) ReleaseMesh(placed);
    pose = nullptr;
    if (animator) animator->Release();
    animator = nullptr;
    if (counted54) counted54->Release();
    counted54 = nullptr;
    if (counted58) counted58->Release();
    counted58 = nullptr;
    CreateBone();
}
EI_REWRITTEN(0x0050B510, FigureBone::DestroyBone);

// The names go after the body (name3, name2, name), then the engine object's part.
FigureBone::~FigureBone() {
    DestroyBone();   // its reset empties the matrices through 0050DD40 here, the same SetSize
    if (matrices.data) lib::AlignedArray<Matrix4>::Free(matrices.data);
    // The child list's destructor: its vtable, RemoveAll again.
    children.vtable = ei::GamePointer<void>(kChildListVtable);
    children.RemoveAll();
}

void FigureBone::DestructBody() { this->FigureBone::~FigureBone(); }
EI_REWRITTEN(0x0050B1D0, FigureBone::DestructBody);

FigureBone* FigureBone::DeleteBone(unsigned flags) {
    this->FigureBone::~FigureBone();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0050B1B0, FigureBone::DeleteBone);

int FigureBone::Create() { return CreateBone(); }
void FigureBone::Destroy() { DestroyBone(); }

EI_REWRITTEN(0x0050DD40, lib::AlignedArray<Matrix4>::SetSizeDiscardStep);
