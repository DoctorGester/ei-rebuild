// The skeleton bone's lifetime (class 0x300C, 0xA0 bytes; FigureBone in map_object_figures.h): constructor,
// reset, Destroy, destructor and deleting destructor. The figure factory makes bones; the animation state
// links them. See docs/notes/map_objects.md.
#include "map_object_figures.h"   // FigureBone

#include <cstdint>
#include <new>

namespace {

// The mesh is a counted engine object (its release virtual).
void ReleaseMesh(CFigureMesh* mesh) { mesh->Release(); }

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
    CFigureMesh* placed = mesh;
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
