// The 3D object's Create / Destroy slots, its effect sources and the counted figure reference.
// See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "effects.h"
#include "map_object_figures.h"
#include "link_figure.h"   // CLinkMesh: the figure
#include "map_object_shadow.h"   // WoundsMaterial: the client half's
#include "model.h"               // CRenderPreset: the wounds material's

#include <cstring>
#include <memory>

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
constexpr Quaternion kIdentityOrientation = /*007AF1C0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
} // namespace

int CObject3D::Create3D() {
    localPosition_ = {};
    localRotation_ = kIdentityOrientation;
    attached = nullptr;
    flags_ = 0;
    animations = nullptr;
    scale = {};
    figure.Reset();
    defaultScale = kDefaultFigureScale;
    ClearBounds();
    std::vector<FootprintPart>().swap(partBoxes);
    return 1;
}

void CObject3D::Destroy3D() {
    figure.Reset();
    while (!effectSources.empty()) RemoveEffectSource(effectSources.front());
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
    animations = nullptr;
    flags_ = 0;
}

void CObject3D::AddEffectSource(CParticleSource* source) {
    if (lib::Contains(effectSources, source)) return;
    effectSources.push_back(source);
    if (source) {
        source->AddRef();
        source->Attach(this);
    }
}

void CObject3D::RemoveEffectSource(CParticleSource* source) {
    auto node = std::ranges::find(effectSources, source);
    if (node == effectSources.end()) return;
    effectSources.erase(node);
    if (source) {
        source->Attach(nullptr);
        source->Release();
    }
}
