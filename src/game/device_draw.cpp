// The device's indexed draw with the current material (on the device holder): used by the mesh draws and the flat
// geometry draw. Without a material it is one counted pass. A material (CTextureDevice::material_)
// brings its blend, a preset (its passes' states, model.h) and texture stages: with one pass the stages are
// set and drawn once; with more, the first pass sets every stage, each further pass applies its
// states, sets stage 0 from the next stage record and draws again, and the blend states are
// restored at the end. A second form draws twice: with a first surface, then blended (source alpha, inverse source
// alpha) with a second one. See docs/notes/models.md ("Material draw").
#include "device_material.h"
#include "engine_object.h"
#include "model.h"   // CRenderPreset
#include "texture.h"

#include "render/render_state.h"
#include "render/render.h"
#include "game_singletons.h"
#include "world.h"

#include <cstdint>

namespace {

CDeviceMaterial* CurrentMaterial(CTextureDevice* device) { return device->material_; }
Surface* TextureSurface(CTexture* texture) { return texture->surface_; }

// The material's blend: blending on with its factors.
void SetMaterialBlend(const CDeviceMaterial* material) {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.sourceBlend = material->sourceBlend;
    state.destBlend = material->destBlend;
}

// One pass: the mesh handed over to the renderer (src/render, device_mesh.cpp) gets it with the device's
// current state; the call counter.
void Draw(CTextureDevice* device) {
    render::MeshPass(device);
    device->drawCount_ += 1;
}

// Single pass: the stage's texture, looked up by name on first use (an inline counted assignment);
// a stage without one unbinds the device's texture of that stage (stageSurfaces_) once.
void SetSinglePassStage(CTextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, TextureSurface(s.texture));
        return;
    }
    if (!s.name.empty()) {
        CTexture* texture = g_clientWorld->textureManager_->FindTexture(s.name.data());
        CEngineObject* old = s.texture;
        s.texture = texture;
        if (texture) texture->AddRef();
        if (old) old->Release();
        device->BindSurface(stage, s.texture ? TextureSurface(s.texture) : nullptr);
        return;
    }
    Surface*& bound = device->stageSurfaces_[stage];
    if (bound) {
        bound = nullptr;
        ++device->textureChanges_;
    }
}

// First of several passes: every stage of the first pass.
void SetFirstPassStage(CTextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, TextureSurface(s.texture));
    } else if (!s.name.empty()) {
        s.texture.Assign(g_clientWorld->textureManager_->FindTexture(s.name.data()));
        device->BindTexture(stage, s.texture);
    } else {
        device->BindSurface(stage, nullptr);
    }
}

// A further pass: its stage record on stage 0.
void SetLaterPassStage(CTextureDevice* device, MaterialStage& s) {
    Surface* surface = nullptr;
    if (s.texture) {
        surface = TextureSurface(s.texture);
    } else if (!s.name.empty()) {
        s.texture.Assign(g_clientWorld->textureManager_->FindTexture(s.name.data()));
        if (s.texture) surface = TextureSurface(s.texture);
    }
    device->BindSurface(0, surface);
}

void DrawPasses(CTextureDevice* device, CRenderPreset* preset) {
    render::RenderState& state = render::g_renderState;
    const bool savedBlend = state.blend;
    const render::Blend savedSource = state.sourceBlend;
    const render::Blend savedDest = state.destBlend;
    SetMaterialBlend(CurrentMaterial(device));
    preset->ApplyPass(0);
    for (int stage = 0; stage < preset->stages_; ++stage) SetFirstPassStage(device, stage, CurrentMaterial(device)->stages[stage]);
    Draw(device);
    for (int pass = 1; pass < preset->PassCount(); ++pass) {
        preset->ApplyPass(pass);
        SetLaterPassStage(device, CurrentMaterial(device)->stages[preset->stages_ + pass - 1]);
        Draw(device);
    }
    state.blend = savedBlend;
    state.sourceBlend = savedSource;
    state.destBlend = savedDest;
}

// Stage 0's holder, as the surface bind does it: when it changes, kept for the new renderer (stageSurfaces_) and the
// change counted.
void BindStage0(CTextureDevice* device, Surface* holder) {
    if (device->stageSurfaces_[0] == holder) return;
    device->stageSurfaces_[0] = holder;
    ++device->textureChanges_;
}

} // namespace

// Draws the mesh the renderer has with the current material. Returns 1.
int CTextureDevice::DrawIndexed() {
    CDeviceMaterial* material = CurrentMaterial(this);
    if (material) {
        CRenderPreset* preset = material->preset;
        if (material->blend) SetMaterialBlend(material);
        else if (preset->PassCount() <= 1) render::g_renderState.blend = false;
        if (preset->PassCount() > 1) {
            DrawPasses(this, preset);
            return 1;
        }
        preset->ApplyPass(0);
        for (int stage = 0; stage < preset->stages_; ++stage) SetSinglePassStage(this, stage, material->stages[stage]);
    }
    Draw(this);
    return 1;
}

// Draws the indexed primitives with `first` on stage 0, then again blended (source
// alpha / inverse source alpha, blending on) with `second` and texture coordinates 0. Returns 1.
int CTextureDevice::DrawTwoTextures(Surface* first, Surface* second) {
    BindStage0(this, first);
    Draw(this);
    render::RenderState& state = render::g_renderState;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.blend = true;
    BindStage0(this, second);
    Draw(this);
    return 1;
}
