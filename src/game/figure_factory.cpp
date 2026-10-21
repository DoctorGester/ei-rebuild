// 005AA8E0 (virtual, slot of the figure object factory 0073FBC0; thiscall, RET 4, `this` unused):
// creates a scene object of a class id. 0x41-0x43 are the figure part meshes built on the common
// base (005AABC0): 0x41 with two empty arrays (CSharedFigureMesh), 0x42 with four more
// (CPlainFigureMesh), 0x43 the same (CUiMesh, ui.h; the original constructs its six arrays); the
// original inlines their constructors, ours are C++ (C19). 0x44 (004D9110) and
// 0x300B-0x300D (0050A5D0, 0050AF40, 0050C5F0) have their own constructors. The new object's
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

// A figure object of class T in new storage of `size` bytes (the class 0x41 object is smaller than
// its C++ class: figure_mesh.h), or null when the allocation fails.
template <typename T>
T* New(std::size_t size) {
    void* o = gamecrt::operator_new(size);
    return o ? new (o) T() : nullptr;
}

void* Create(int classId) {
    switch (classId) {
    case 0x41:
        return New<CSharedFigureMesh>(0x1E0);
    case 0x42:
        return New<CPlainFigureMesh>(0x220);
    case 0x43:
        return New<CUiMesh>(0x284);
    case 0x44:
        return New<CSkyObject>(0x230);
    case 0x300B: {
        void* o = gamecrt::operator_new(0x78);
        return o ? static_cast<AnimationTrack*>(o)->Construct() : nullptr;
    }
    case 0x300C: {
        void* o = gamecrt::operator_new(0xA0);
        return o ? static_cast<FigureBone*>(o)->Construct() : nullptr;
    }
    case 0x300D: {
        void* o = gamecrt::operator_new(0x78);
        return o ? static_cast<CFigureAnimationState*>(o)->Construct() : nullptr;
    }
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
