// The 3D object's Create / Destroy slots, its effect sources and the counted figure reference.
// See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "effects.h"
#include "map_object_figures.h"
#include "link_figure.h"   // LinkMesh: the figure
#include "map_object_shadow.h"   // WoundsMaterial: the client half's
#include "model.h"               // RenderPreset: the wounds material's

#include <cstring>
#include <memory>

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
constexpr Quaternion kIdentityOrientation = /*007AF1C0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
} // namespace

void CObject3D::Create3D() {
    localPosition = {};
    localRotation = kIdentityOrientation;
    attached = nullptr;
    flags = 0;
    animations = nullptr;
    scale = {};
    figure.Reset();
    defaultScale = kDefaultFigureScale;
    ClearBounds();
    std::vector<FootprintPart>().swap(partBoxes);
}

void CObject3D::Destroy3D() {
    figure.Reset();
    RemoveAllEffectSources();
    if (animation) {
        animation->DetachMeshes();
        if (animation) {
            animation->owner = nullptr;
            animation->Release();
        }
        animation = nullptr;
    }
    for (HeldRef<FigureMesh>& part : parts)
        if (FigureMesh* forgotten = part.Take()) forgotten->ForgetReference();
    parts.clear();
    if (attached) attached->Release();
    attached = nullptr;
    animations = nullptr;
    flags = 0;
}

void CObject3D::AddEffectSource(ParticleSource* source) {
    if (std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get) != effectSources.end()) return;
    effectSources.emplace_back(source);
    if (source) source->Attach(this);
}

void CObject3D::RemoveEffectSource(ParticleSource* source) {
    auto node = std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get);
    if (node == effectSources.end()) return;
    HeldRef<ParticleSource> held = std::move(*node);   // released after the detach
    effectSources.erase(node);
    if (source) source->Attach(nullptr);
}
