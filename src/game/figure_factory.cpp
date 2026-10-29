// The figure object factory (virtual, `this` unused): creates a scene object of a class id. 0x41-0x43 are the
// figure part meshes built on the common base: 0x41 with two empty arrays (SharedFigureMesh), 0x42 with four
// more (PlainFigureMesh), 0x43 the same (UiMesh, ui.h); the original inlines their constructors. 0x44 and
// 0x300B-0x300D (AnimationTrack, FigureBone, FigureAnimationState) have their own. All are C++ classes,
// allocated at the game's sizes. The new object's type gets the class id; an unknown id or a failed
// allocation returns null. See docs/notes/models.md.
#include "engine_object.h"   // EngineObject
#include "figure_manager.h"   // FigureObjectType
#include "figure_mesh.h"     // FigureMesh
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "sky_object.h"

#include "ui.h"   // UiMesh

#include <cstddef>
#include <cstdint>
#include <new>

namespace {

void* Create(int classId) {
    switch (static_cast<FigureObjectType>(classId)) {
    case FigureObjectType::SharedFigureMesh:
        return new SharedFigureMesh();
    case FigureObjectType::PlainFigureMesh:
        return new PlainFigureMesh();
    case FigureObjectType::UiMesh:
        return new UiMesh();
    case FigureObjectType::SkyObject:
        return new SkyObject();
    case FigureObjectType::AnimationTrack:
        return new AnimationTrack();
    case FigureObjectType::FigureBone:
        return new FigureBone();
    case FigureObjectType::AnimationState:
        return new FigureAnimationState();
    default:
        return nullptr;
    }
}

} // namespace

void* CreateFigureObject(int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    static_cast<EngineObject*>(object)->type = classId;
    return object;
}
