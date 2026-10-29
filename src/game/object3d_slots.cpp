// Small virtuals of the 3D object base (CObject3D; shared by every map
// object's vtable): the world rotation, copying a figure, the side half, the effect sources and
// the drawing slots. See map_object_figures.h and docs/notes/map_objects.md.
#include "effects.h"
#include "map_object_figures.h"

#include "core_transform.h"

#include "lib/file_io.h"
#include "lib/gameapi.h"

#include <cstring>

namespace {
} // namespace

// The same body as the base's.
void CObject3D::CombineRotation(const Quaternion& parent) { TransformObject::CombineRotation(parent); }

// TransformObject's functions, called directly.
void CObject3D::SaveTransform(lib::File* file) { TransformObject::SaveTransform(file); }

void CObject3D::LoadTransform(lib::File* file) { TransformObject::LoadTransform(file); }

// The parts are copied uncounted, as the original copies the pointers: the old ones are dropped without a release
// and the copies take no reference.
void CObject3D::CopyFigure(const CObject3D* other) {
    for (HeldRef<FigureMesh>& part : parts) part.Take();
    parts = other->parts;
    for (FigureMesh* part : parts)
        if (part) part->ForgetReference();
    TransformObject::CopyTransform(*other);
}

void CObject3D::AttachSpecific(EngineObject* half) {
    if (!half || SideTypeOf(half) != SideObjectType::Object3D) return;
    if (attached) {
        attached->object = nullptr;
        attached->Release();
    }
    attached = static_cast<Object3DSpecific*>(half);
    attached->object = this;
    attached->AddRef();
}

void CObject3D::RemoveAllEffectSources() {
    while (!effectSources.empty()) RemoveEffectSource(effectSources.front());
}

int CObject3D::DrawFigure(SceneRenderer* context) { return attached ? attached->DrawParts(context, nullptr) : 1; }

int CObject3D::DrawCollectDirty(SceneRenderer* context) {
    gameapi::SetRectEmpty(&dirtyRect);
    return attached ? attached->DrawParts(context, &dirtyRect) : 1;
}

int CObject3D::DrawFigureShadow(SceneRenderer* context) { return attached ? attached->DrawPartsShadow(context) : 1; }

// --- Small virtuals ----------------------------------------------------------------------------

bool CObject3D::AcceptsAnimation(const FigureAnimation*) { return true; }
void CObject3D::AttachObject(EngineObject* object) {
    AttachSpecific(object);
}
bool CObject3D::InvisibleSlot() { return false; }
void CObject3D::AdvanceTime(float) {}
