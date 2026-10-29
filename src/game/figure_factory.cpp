// The figure object factory (virtual, `this` unused): creates a scene object of a class id. 0x41-0x43 are the
// figure part meshes built on the common base: 0x41 with two empty arrays (CSharedFigureMesh), 0x42 with four
// more (CPlainFigureMesh), 0x43 the same (CUiMesh, ui.h); the original inlines their constructors. 0x44 and
// 0x300B-0x300D (AnimationTrack, FigureBone, CFigureAnimationState) have their own. All are C++ classes,
// allocated at the game's sizes. The new object's type gets the class id; an unknown id or a failed
// allocation returns null. See docs/notes/models.md.
#include "engine_object.h"   // CEngineObject
#include "figure_manager.h"   // FigureObjectType
#include "figure_mesh.h"     // CFigureMesh
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "sky_object.h"

#include "ui.h"   // CUiMesh

#include <cstddef>
#include <cstdint>
#include <new>

namespace {

void* Create(int classId) {
    switch (static_cast<FigureObjectType>(classId)) {
    case FigureObjectType::SharedFigureMesh:
        return new CSharedFigureMesh();
    case FigureObjectType::PlainFigureMesh:
        return new CPlainFigureMesh();
    case FigureObjectType::UiMesh:
        return new CUiMesh();
    case FigureObjectType::SkyObject:
        return new CSkyObject();
    case FigureObjectType::AnimationTrack:
        return new AnimationTrack();
    case FigureObjectType::FigureBone:
        return new FigureBone();
    case FigureObjectType::AnimationState:
        return new CFigureAnimationState();
    default:
        return nullptr;
    }
}

} // namespace

void* CreateFigureObject(int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    static_cast<CEngineObject*>(object)->type = classId;
    return object;
}
