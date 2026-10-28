// Small methods of the 3D object (CObject3D): its deleting destructor, the
// attached half and animation state, and the setters that reach every part. See map_object_figures.h.
#include "map_object_figures.h"

#include "ai_map.h"   // FootprintPart

#include "lib/crt.h"

namespace {
constexpr std::uint32_t kBoxIgnored = 0x800;   // FootprintPart::flags: the part's box is left out
constexpr float kFirstFrame = 0.0f;

CFigureMesh* PartAt(const CObject3D* object, int index) { return object->parts[index]; }
} // namespace

void CObject3D::SetSpecific(CObject3DSpecific* half) {
    if (attached) {
        Specific()->object = nullptr;
        attached->Release();
    }
    attached = half;
    if (half) {
        half->object = this;
        ++attached->refCount_;
    }
}

void CObject3D::SetPartFlags(std::uint32_t mask) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flags_ |= mask;
}

void CObject3D::ClearPartFlags(std::uint32_t mask) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flags_ &= ~mask;
}

void CObject3D::SetPartsFlatColour(std::uint32_t value) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flatColour_ = value;
}

void CObject3D::SetPartsFlatSpecular(std::uint32_t value) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flatSpecular_ = value;
}

void CObject3D::SetAnimationState(CFigureAnimationState* state) {
    if (animation) {
        animation->owner = nullptr;
        animation->Release();
    }
    animation = state;
    if (state) {
        ++state->refCount_;
        animation->owner = this;
    }
}

void CObject3D::PlayAnimation(FigureAnimation* figureAnimation, int) {
    if (animation) animation->Play(figureAnimation);
}

int CObject3D::PlayAnimationSecond(int, FigureAnimation* figureAnimation) {
    if (animation) animation->Play(figureAnimation);
    return 1;
}

int CObject3D::AttachPartMesh(CEngineObject* part) {
    if (!animation) return 0;
    return animation->AttachMesh(part);
}

void CObject3D::RestartPose() {
    if (!animation) return;
    animation->SetTime(kFirstFrame);
    animation->PlaceIfChanged();
}
