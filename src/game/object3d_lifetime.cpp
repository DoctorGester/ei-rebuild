// The 3D object's Create / Destroy slots, its effect sources and the counted figure reference.
// See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "effects.h"
#include "map_object_figures.h"
#include "link_figure.h"   // CLinkMesh: the figure
#include "map_object_shadow.h"   // WoundsMaterial: the client half's
#include "model.h"               // CRenderPreset: the wounds material's

#include "lib/crt.h"

#include "game_data.h"

#include <cstring>
#include <memory>

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
constexpr Quaternion kIdentityOrientation = /*007AF1C0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser

// The counted objects CountedRef holds: their count; each is deleted at its last reference.
inline int& CountOf(CLinkMesh* figure) { return figure->refCount_; }
inline int& CountOf(WoundsMaterial* material) { return material->refCount; }
inline int& CountOf(CRenderPreset* preset) { return preset->refs_; }

template <typename T>
void DropReference(T* object) {
    if (object && --CountOf(object) == 0) delete object;
}
} // namespace

template <typename T>
void CountedRef<T>::Assign(T* value) {
    T* old = object;
    object = value;
    if (value) ++CountOf(value);
    DropReference(old);
}
template struct CountedRef<CLinkMesh>;
template struct CountedRef<WoundsMaterial>;
template struct CountedRef<CRenderPreset>;

int CObject3D::Create3D() {
    std::memset(&localPosition_, 0, sizeof(localPosition_));
    localRotation_ = kIdentityOrientation;
    attached = nullptr;
    flags_ = 0;
    spare09C = 0;
    animations = nullptr;
    std::memset(&scale, 0, sizeof(scale));
    CLinkMesh* old = figure;
    figure = nullptr;
    DropReference(old);
    defaultScale = kDefaultFigureScale;
    ClearBounds();
    if (partBoxes) {
        gamecrt::operator_delete(partBoxes);
        partBoxes = nullptr;
    }
    partBoxCapacity = 0;
    partBoxCount = 0;
    return 1;
}

void CObject3D::Destroy3D() {
    figure.Assign(nullptr);
    while (effectSources.head) RemoveEffectSource(effectSources.head->data);
    if (animation) {
        animation->DetachMeshes();
        if (animation) {
            animation->owner = nullptr;
            animation->Release();
        }
        animation = nullptr;
    }
    for (int i = 0; i < std::ssize(parts); ++i) {
        CEngineObject* part = parts[i];
        if (part) {
            --part->refCount_;   // no release (sic)
            parts[i] = nullptr;
        }
    }
    parts.clear();
    for (int i = 0; i < std::ssize(pendingParts); ++i) {
        CEngineObject* part = pendingParts[i];
        if (part) {
            part->Release();
            pendingParts[i] = nullptr;
        }
    }
    lib::FreeStorage(pendingParts);
    if (attached) attached->Release();
    attached = nullptr;
    spare09C = 0;
    animations = nullptr;
    flags_ = 0;
}

void CObject3D::AddEffectSource(CParticleSource* source) {
    if (effectSources.Find(source)) return;
    effectSources.AddTail(source);
    if (source) {
        ++source->refCount_;
        source->Attach(this);
    }
}

void CObject3D::RemoveEffectSource(CParticleSource* source) {
    auto* node = effectSources.Find(source);
    if (!node) return;
    effectSources.RemoveAt(node);
    if (source) {
        source->Attach(nullptr);
        source->Release();
    }
}
