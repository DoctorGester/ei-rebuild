// Small methods of the 3D object (CObject3D, vtable 0073DDD4; A-U12): its deleting destructor, the
// attached half and animation state, and the setters that reach every part. See
// map_object_figures.h.
#include "map_object_figures.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
constexpr std::uint32_t kBoxIgnored = 0x800;   // FootprintPart::flags: the part's box is left out
constexpr float kFirstFrame = 0.0f;

CFigureMesh* PartAt(const CObject3D* object, int index) { return object->parts[index]; }
} // namespace

CObject3D* CObject3D::DeleteObject3D(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00515D50, CObject3D::DeleteObject3D);

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
EI_REPLACE_METHOD(0x005161A0, CObject3D::SetSpecific);

// 005174E0 (ECX = a part box's flags, RET 4): the box is marked ignored (0x800) or not (the "EMPTY"
// pieces, CObject3D::LoadPart).
void __fastcall BoxSetIgnored(std::uint32_t* flags, void*, bool ignored) {
    if (ignored)
        *flags |= kBoxIgnored;
    else
        *flags &= ~kBoxIgnored;
}
EI_REPLACE(0x005174E0, BoxSetIgnored);

void CObject3D::SetPartFlags(std::uint32_t mask) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flags_ |= mask;
}
EI_REPLACE_METHOD(0x00517910, CObject3D::SetPartFlags);

void CObject3D::ClearPartFlags(std::uint32_t mask) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flags_ &= ~mask;
}
EI_REPLACE_METHOD(0x00517950, CObject3D::ClearPartFlags);

void CObject3D::SetPartsFlatColour(std::uint32_t value) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flatColour_ = value;
}
EI_REPLACE_METHOD(0x00519500, CObject3D::SetPartsFlatColour);

void CObject3D::SetPartsFlatSpecular(std::uint32_t value) {
    for (int i = 0; i < parts.size; ++i) PartAt(this, i)->flatSpecular_ = value;
}
EI_REPLACE_METHOD(0x00519530, CObject3D::SetPartsFlatSpecular);

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
EI_REPLACE_METHOD(0x005187D0, CObject3D::SetAnimationState);

void CObject3D::PlayAnimation(FigureAnimation* figureAnimation, int) {
    if (animation) animation->Play(figureAnimation);
}
EI_REPLACE_METHOD(0x005187B0, CObject3D::PlayAnimation);

int CObject3D::PlayAnimationSecond(int, FigureAnimation* figureAnimation) {
    if (animation) animation->Play(figureAnimation);
    return 1;
}
EI_REPLACE_METHOD(0x00518840, CObject3D::PlayAnimationSecond);

int CObject3D::AttachPartMesh(CEngineObject* part) {
    if (!animation) return 0;
    return animation->AttachMesh(part);
}
EI_REPLACE_METHOD(0x00518820, CObject3D::AttachPartMesh);

void CObject3D::RestartPose() {
    if (!animation) return;
    animation->SetTime(kFirstFrame);
    animation->PlaceIfChanged();
}
EI_REPLACE_METHOD(0x00518860, CObject3D::RestartPose);
