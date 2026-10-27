// The figure object factory (virtual, `this` unused): creates a scene object of a class id. 0x41-0x43 are the
// figure part meshes built on the common base: 0x41 with two empty arrays (CSharedFigureMesh), 0x42 with four
// more (CPlainFigureMesh), 0x43 the same (CUiMesh, ui.h); the original inlines their constructors. 0x44 and
// 0x300B-0x300D (AnimationTrack, FigureBone, CFigureAnimationState) have their own. All are C++ classes,
// allocated at the game's sizes. The new object's type_ gets the class id; an unknown id or a failed
// allocation returns null. See docs/notes/models.md.
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

// A figure object of class T in new storage, or null when the allocation fails.
template <typename T>
T* New() {
    T* o = gamecrt::operator_new_as<T>();
    return o ? new (o) T() : nullptr;
}

void* Create(int classId) {
    switch (classId) {
    case 0x41:
        return New<CSharedFigureMesh>();
    case 0x42:
        return New<CPlainFigureMesh>();
    case 0x43:
        return New<CUiMesh>();
    case 0x44:
        return New<CSkyObject>();
    case 0x300B:
        return New<AnimationTrack>();
    case 0x300C:
        return New<FigureBone>();
    case 0x300D:
        return New<CFigureAnimationState>();
    default:
        return nullptr;
    }
}

} // namespace

void* __stdcall CreateFigureObject(int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    static_cast<CEngineObject*>(object)->type_ = classId;
    return object;
}

EI_REWRITTEN(0x005AA8E0, CreateFigureObject);
