// Small virtual slots of the 3D object base (CObject3D; shared by every map
// object's vtable): the world rotation, copying a figure, the side half, the effect sources and
// the drawing slots. See map_object_figures.h and docs/notes/map_objects.md.
#include "effects.h"
#include "map_object_figures.h"

#include "core_transform.h"

#include "lib/file_io.h"
#include "lib/gameapi.h"

#include <cstring>

namespace {
constexpr int kSpecificType = 0x46;   // the side halves' type_
} // namespace

// The same body as the base's.
void CObject3D::CombineRotation(const Quaternion& parent) { CTransformObject::CombineRotation(parent); }

// CTransformObject's functions, called directly.
void CObject3D::SaveTransform(lib::File* file) { CTransformObject::SaveTransform(file); }

void CObject3D::LoadTransform(lib::File* file) { CTransformObject::LoadTransform(file); }

void CObject3D::CopyFigure(const CObject3D* other) {
    parts = other->parts;
    CTransformObject::CopyTransform(*other);
}

void CObject3D::AttachSpecific(CEngineObject* half) {
    if (!half || half->type_ != kSpecificType) return;
    if (attached) {
        Specific()->object = nullptr;
        attached->Release();
    }
    attached = static_cast<CObject3DSpecific*>(half);
    Specific()->object = this;
    ++attached->refCount_;
}

void CObject3D::RemoveAllEffectSources() {
    while (!effectSources.empty()) {
        CParticleSource* source = effectSources.front();
        auto node = effectSources.begin();
        while (node != effectSources.end() && *node != source) ++node;
        if (node == effectSources.end()) continue;
        effectSources.erase(node);
        if (source) {
            source->Attach(nullptr);
            source->Release();
        }
    }
}

int CObject3D::DrawFigure(CSceneRenderer* context) { return attached ? Specific()->DrawParts(context, nullptr) : 1; }

int CObject3D::DrawCollectDirty(CSceneRenderer* context) {
    gameapi::SetRectEmpty(DirtyRect());
    return attached ? Specific()->DrawParts(context, &dirtyRect) : 1;
}

int CObject3D::DrawFigureShadow(CSceneRenderer* context) { return attached ? Specific()->DrawPartsShadow(context) : 1; }

// --- Small virtuals ----------------------------------------------------------------------------

bool CObject3D::AcceptsAnimation(const FigureAnimation*) { return true; }
void CObject3D::AttachObject(CEngineObject* object) {
    AttachSpecific(object);
}
bool CObject3D::InvisibleSlot() { return false; }
void CObject3D::AdvanceTime(float) {}
