// The 3D object's Create / Destroy slots, its effect sources and the counted figure reference.
// See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"
#include "link_figure.h"   // CLinkMesh: the figure

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_engine.h"

#include <cstring>

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
const Quaternion& IdentityOrientation() { return *reinterpret_cast<const Quaternion*>(0x007AF1C0); }
const float* DefaultFigureScale() { return reinterpret_cast<const float*>(0x007AF1D0); }

// Not rewritten, forwarder: 0050D3A0, the animation state lets go of every part (plain RET).
__declspec(naked) int __fastcall AnimationReleaseParts(CFigureAnimationState* /*state*/) EI_ORIGINAL(0x0050D3A0)

void DropReference(CLinkMesh* object) {
    if (object && --object->refCount_ == 0) object->DeleteThis(1);
}
} // namespace

void CountedRef::Assign(CLinkMesh* value) {
    CLinkMesh* old = object;
    object = value;
    if (value) ++value->refCount_;
    DropReference(old);
}
EI_REPLACE_METHOD(0x00519580, CountedRef::Assign);
EI_REPLACE_METHOD(0x0051E4F0, CountedRef::Assign);   // byte-identical copies (A-U11)
EI_REPLACE_METHOD(0x0051E570, CountedRef::Assign);

int CObject3D::Create3D() {
    std::memset(&position, 0, sizeof(position));
    orientation = IdentityOrientation();
    attached = nullptr;
    flags_ = 0;
    spare09C = 0;
    animations = nullptr;
    std::memset(&scale, 0, sizeof(scale));
    CLinkMesh* old = figure;
    figure = nullptr;
    DropReference(old);
    std::memcpy(&defaultScale, DefaultFigureScale(), sizeof(defaultScale));
    std::memset(&boundsFlags, 0, reinterpret_cast<char*>(&partBoxArrayVtable) - reinterpret_cast<char*>(&boundsFlags));
    if (partBoxes) {
        gamecrt::operator_delete(partBoxes);
        partBoxes = nullptr;
    }
    partBoxCapacity = 0;
    partBoxCount = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00515F70, CObject3D::Create3D);

void CObject3D::Destroy3D() {
    FigureRef().Assign(nullptr);
    while (effectSources.head) RemoveEffectSource(effectSources.head->data);
    if (animation) {
        AnimationReleaseParts(animation);
        if (animation) {
            animation->owner = nullptr;
            animation->Release();
        }
        animation = nullptr;
    }
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<CEngineObject*>(parts.data[i]);
        if (part) {
            --part->refCount_;   // no release (sic)
            parts.data[i] = nullptr;
        }
    }
    parts.SetSize(0, -1);
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = static_cast<CEngineObject*>(pendingParts.data[i]);
        if (part) {
            part->Release();
            pendingParts.data[i] = nullptr;
        }
    }
    if (pendingParts.data) {
        gamecrt::operator_delete(pendingParts.data[-1]);
        pendingParts.data = nullptr;
    }
    pendingParts.capacity = 0;
    pendingParts.size = 0;
    if (attached) attached->Release();
    attached = nullptr;
    spare09C = 0;
    animations = nullptr;
    flags_ = 0;
}
EI_REPLACE_METHOD(0x00516080, CObject3D::Destroy3D);

void CObject3D::AddEffectSource(CEngineObject* source) {
    if (effectSources.Find(source)) return;
    effectSources.AddTail(source);
    if (source) {
        ++source->refCount_;
        ParticleSourceAttachOriginal(source, nullptr, this);
    }
}
EI_REPLACE_METHOD(0x00518A00, CObject3D::AddEffectSource);

void CObject3D::RemoveEffectSource(CEngineObject* source) {
    auto* node = effectSources.Find(source);
    if (!node) return;
    effectSources.RemoveAt(node);
    if (source) {
        ParticleSourceAttachOriginal(source, nullptr, nullptr);
        source->Release();
    }
}
EI_REPLACE_METHOD(0x00518AE0, CObject3D::RemoveEffectSource);
