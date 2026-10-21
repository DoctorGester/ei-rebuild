// Small virtual slots of the 3D object base (CObject3D, vtable 0073DDD4; shared by every map
// object's vtable): the world rotation, copying a figure, the side half, the effect sources and
// the drawing slots. See map_object_figures.h and docs/notes/map_objects.md.
#include "map_object_figures.h"

#include "core_transform.h"

#include "lib/gameapi.h"

#include "ei/hook.h"
#include "originals_engine.h"

#include <cstring>

namespace {
constexpr int kSpecificType = 0x46;   // the side halves' type_
} // namespace

void CObject3D::CombineRotation3D(const Quaternion* parent) { Transform()->CombineRotationImpl(*parent); }
EI_REPLACE_METHOD(0x00516370, CObject3D::CombineRotation3D);

void CObject3D::CopyFigureImpl(const CObject3D* other) {
    parts.SetSize(other->parts.size, -1);
    std::memcpy(parts.data, other->parts.data, static_cast<std::size_t>(other->parts.size & 0x3FFFFFFF) * sizeof(void*));
    Transform()->CopyTransformImpl(*other->Transform());
}
EI_REPLACE_METHOD(0x005161E0, CObject3D::CopyFigureImpl);

void CObject3D::AttachSpecific(CEngineObject* half) {
    if (!half || half->type_ != kSpecificType) return;
    if (attached) {
        Specific()->object = nullptr;
        attached->Release();
    }
    attached = half;
    Specific()->object = this;
    ++attached->refCount_;
}
EI_REPLACE_METHOD(0x00516D50, CObject3D::AttachSpecific);

void CObject3D::RemoveAllEffectSources() {
    while (effectSources.head) {
        CEngineObject* source = effectSources.head->data;
        auto* node = effectSources.head;
        while (node && node->data != source) node = node->next;
        if (!node) continue;
        effectSources.RemoveAt(node);
        if (source) {
            ParticleSourceAttachOriginal(source, nullptr, nullptr);
            source->Release();
        }
    }
}
EI_REPLACE_METHOD(0x00515E90, CObject3D::RemoveAllEffectSources);

int CObject3D::DrawFigureImpl(void* context) { return attached ? Specific()->DrawParts(context, nullptr) : 1; }
EI_REPLACE_METHOD(0x00516460, CObject3D::DrawFigureImpl);

int CObject3D::DrawCollectDirtyImpl(void* context) {
    gameapi::SetRectEmpty(DirtyRect());
    return attached ? Specific()->DrawParts(context, dirtyRect) : 1;
}
EI_REPLACE_METHOD(0x00516420, CObject3D::DrawCollectDirtyImpl);

int CObject3D::DrawFigureShadowImpl(void* context) { return attached ? Specific()->DrawPartsShadow(context) : 1; }
EI_REPLACE_METHOD(0x00516490, CObject3D::DrawFigureShadowImpl);

// --- The virtual slots (vtable 0073DDD4) for the compiler's tables (C19) -------------------------------

CTransformObject* CObject3D::CopyTransform(const CTransformObject& other) {
    return Transform()->CTransformObject::CopyTransformImpl(other);
}
void CObject3D::CombineRotation(const Quaternion& parent) { CombineRotation3D(&parent); }
void CObject3D::UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition) {
    Transform()->CTransformObject::UpdateWorldImpl(parentRotation, parentPosition);
}
void CObject3D::SaveTransform(void* file) { Transform()->CTransformObject::SaveTransformImpl(file); }
void CObject3D::LoadTransform(void* file) { Transform()->CTransformObject::LoadTransformImpl(file); }
bool CObject3D::AcceptsAnimation(const void*) { return true; }
std::uintptr_t CObject3D::AttachObject(CEngineObject* object) {
    AttachSpecific(object);
    return 0;
}
void CObject3D::CopyFigure(const CObject3D* other) { CopyFigureImpl(other); }
bool CObject3D::InvisibleSlot() { return false; }
int CObject3D::DrawFigure(void* context) { return DrawFigureImpl(context); }
int CObject3D::DrawCollectDirty(void* context) { return DrawCollectDirtyImpl(context); }
int CObject3D::DrawFigureShadow(void* context) { return DrawFigureShadowImpl(context); }
void CObject3D::AdvanceTime(float) {}
std::uintptr_t CObject3D::LoadFigures() { return static_cast<std::uintptr_t>(LoadFiguresImpl()); }
