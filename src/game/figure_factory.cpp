// 005AA8E0 (virtual, slot of the figure object factory 0073FBC0; thiscall, RET 4, `this` unused):
// creates a scene object of a class id. 0x41-0x43 are the figure part meshes built on the common
// base (005AABC0): 0x41 with two empty arrays (CSharedFigureMesh), 0x42 with four more
// (CPlainFigureMesh), 0x43 the same (CUiMesh, ui.h; the original constructs its six arrays); the
// original inlines their constructors. 0x44 (004D9110) and 0x300B-0x300D (AnimationTrack 0050A5D0,
// FigureBone 0050AF40, CFigureAnimationState 0050C5F0) have their own. All are C++ classes (C19),
// allocated at the game's sizes. The new object's
// type_ (+0x10) gets the class id; an unknown id or a failed allocation returns null. See
// docs/notes/models.md.
#include "engine_object.h"   // CEngineObject
#include "figure_mesh.h"     // CFigureMesh
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "sky_object.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "scene_loader.h"
#include "ui.h"   // CUiMesh

#include <cstddef>
#include <cstdint>
#include <new>

namespace {

// A figure object of class T in new storage (the game's size for the class), or null when the
// allocation fails.
template <typename T, std::size_t kGameSize>
T* New() {
    static_assert(sizeof(T) == kGameSize);
    void* o = gamecrt::operator_new(sizeof(T));
    return o ? new (o) T() : nullptr;
}

void* Create(int classId) {
    switch (classId) {
    case 0x41:
        return New<CSharedFigureMesh, 0x1E0>();
    case 0x42:
        return New<CPlainFigureMesh, 0x220>();
    case 0x43:
        return New<CUiMesh, 0x284>();
    case 0x44:
        return New<CSkyObject, 0x230>();
    case 0x300B:
        return New<AnimationTrack, 0x78>();
    case 0x300C:
        return New<FigureBone, 0xA0>();
    case 0x300D:
        return New<CFigureAnimationState, 0x78>();
    default:
        return nullptr;
    }
}

} // namespace

void* __fastcall CreateFigureObject(void*, void*, int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    static_cast<CEngineObject*>(object)->type_ = classId;
    return object;
}

EI_REWRITTEN(0x005AA8E0, CreateFigureObject);
