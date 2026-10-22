// Small virtual slots of the 3D object base (CObject3D, vtable 0073DDD4; shared by every map
// object's vtable): the world rotation, copying a figure, the side half, the effect sources and
// the drawing slots. See map_object_figures.h and docs/notes/map_objects.md.
#include "effects.h"
#include "map_object_figures.h"

#include "core_transform.h"

#include "lib/gameapi.h"

#include "ei/hook.h"

#include <cstring>

namespace {
constexpr int kSpecificType = 0x46;   // the side halves' type_
} // namespace

// The same body as the base's 0040CB80.
void CObject3D::CombineRotation(const Quaternion& parent) { CTransformObject::CombineRotation(parent); }
EI_REWRITTEN(0x00516370, CObject3D::CombineRotation);

// CTransformObject's 00401510 / 00401540, called directly.
void CObject3D::SaveTransform(mfc::CFileVirtuals* file) { CTransformObject::SaveTransform(file); }
EI_REWRITTEN(0x00516350, CObject3D::SaveTransform);

void CObject3D::LoadTransform(mfc::CFileVirtuals* file) { CTransformObject::LoadTransform(file); }
EI_REWRITTEN(0x00516360, CObject3D::LoadTransform);

void CObject3D::CopyFigure(const CObject3D* other) {
    parts.SetSize(other->parts.size, -1);
    std::memcpy(parts.data, other->parts.data, static_cast<std::size_t>(other->parts.size & 0x3FFFFFFF) * sizeof(void*));
    CTransformObject::CopyTransform(*other);
}
EI_REWRITTEN(0x005161E0, CObject3D::CopyFigure);

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
EI_REWRITTEN(0x00516D50, CObject3D::AttachSpecific);

void CObject3D::RemoveAllEffectSources() {
    while (effectSources.head) {
        CEngineObject* source = effectSources.head->data;
        auto* node = effectSources.head;
        while (node && node->data != source) node = node->next;
        if (!node) continue;
        effectSources.RemoveAt(node);
        if (source) {
            static_cast<CParticleSource*>(source)->Attach(nullptr);
            source->Release();
        }
    }
}
EI_REWRITTEN(0x00515E90, CObject3D::RemoveAllEffectSources);

int CObject3D::DrawFigure(void* context) { return attached ? Specific()->DrawParts(context, nullptr) : 1; }
EI_REWRITTEN(0x00516460, CObject3D::DrawFigure);

int CObject3D::DrawCollectDirty(void* context) {
    gameapi::SetRectEmpty(DirtyRect());
    return attached ? Specific()->DrawParts(context, dirtyRect) : 1;
}
EI_REWRITTEN(0x00516420, CObject3D::DrawCollectDirty);

int CObject3D::DrawFigureShadow(void* context) { return attached ? Specific()->DrawPartsShadow(context) : 1; }
EI_REWRITTEN(0x00516490, CObject3D::DrawFigureShadow);

// --- Small virtuals ----------------------------------------------------------------------------

bool CObject3D::AcceptsAnimation(const void*) { return true; }
std::uintptr_t CObject3D::AttachObject(CEngineObject* object) {
    AttachSpecific(object);
    return 0;
}
bool CObject3D::InvisibleSlot() { return false; }
void CObject3D::AdvanceTime(float) {}
