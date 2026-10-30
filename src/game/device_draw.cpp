// The device's indexed draw with the current material (on the device holder): used by the mesh draws and the flat
// geometry draw. Without a material it is one counted pass. A material (TextureDevice::material)
// brings its blend, a preset (its passes' states, model.h) and texture stages: with one pass the stages are
// set and drawn once; with more, the first pass sets every stage, each further pass applies its
// states, sets stage 0 from the next stage record and draws again, and the blend states are
// restored at the end. A second form draws twice: with a first surface, then blended (source alpha, inverse source
// alpha) with a second one. See docs/notes/models.md ("Material draw").
#include "device_material.h"
#include "engine_object.h"
#include "model.h"   // RenderPreset
#include "texture.h"

#include "render/render_state.h"
#include "render/render.h"
#include "game_singletons.h"
#include "world.h"

#include <cstdint>

namespace {

DeviceMaterial* CurrentMaterial(TextureDevice* device) { return device->material; }
Surface* TextureSurface(Texture* texture) { return texture->surface; }

// The material's blend: blending on with its factors.
void SetMaterialBlend(const DeviceMaterial* material) {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.sourceBlend = material->sourceBlend;
    state.destBlend = material->destBlend;
}

// Single pass: the stage's texture, looked up by name on first use (an inline counted assignment);
// a stage without one unbinds the device's texture of that stage (stageSurfaces) once.
void SetSinglePassStage(TextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, TextureSurface(s.texture));
        return;
    }
    if (!s.name.empty()) {
        Texture* texture = g_clientWorld.textureManager->FindTexture(s.name.data());
        EngineObject* old = s.texture;
        s.texture = texture;
        if (texture) texture->AddRef();
        if (old) old->Release();
        device->BindSurface(stage, s.texture ? TextureSurface(s.texture) : nullptr);
        return;
    }
    device->stageSurfaces[stage] = nullptr;
}

// First of several passes: every stage of the first pass.
void SetFirstPassStage(TextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, TextureSurface(s.texture));
    } else if (!s.name.empty()) {
        s.texture.Assign(g_clientWorld.textureManager->FindTexture(s.name.data()));
        device->BindTexture(stage, s.texture);
    } else {
        device->BindSurface(stage, nullptr);
    }
}

// A further pass: its stage record on stage 0.
void SetLaterPassStage(TextureDevice* device, MaterialStage& s) {
    Surface* surface = nullptr;
    if (s.texture) {
        surface = TextureSurface(s.texture);
    } else if (!s.name.empty()) {
        s.texture.Assign(g_clientWorld.textureManager->FindTexture(s.name.data()));
        if (s.texture) surface = TextureSurface(s.texture);
    }
    device->BindSurface(0, surface);
}

void DrawPasses(TextureDevice* device, RenderPreset* preset) {
    render::RenderState& state = render::g_renderState;
    const bool savedBlend = state.blend;
    const render::Blend savedSource = state.sourceBlend;
    const render::Blend savedDest = state.destBlend;
    SetMaterialBlend(CurrentMaterial(device));
    preset->ApplyPass(0);
    for (int stage = 0; stage < preset->stages; ++stage) SetFirstPassStage(device, stage, CurrentMaterial(device)->stages[stage]);
    render::MeshPass(device);
    for (int pass = 1; pass < preset->PassCount(); ++pass) {
        preset->ApplyPass(pass);
        SetLaterPassStage(device, CurrentMaterial(device)->stages[preset->stages + pass - 1]);
        render::MeshPass(device);
    }
    state.blend = savedBlend;
    state.sourceBlend = savedSource;
    state.destBlend = savedDest;
}

} // namespace

// Draws the mesh the renderer has with the current material. Returns 1.
int TextureDevice::DrawIndexed() {
    DeviceMaterial* current = CurrentMaterial(this);
    if (current) {
        RenderPreset* preset = current->preset;
        if (current->blend) SetMaterialBlend(current);
        else if (preset->PassCount() <= 1) render::g_renderState.blend = false;
        if (preset->PassCount() > 1) {
            DrawPasses(this, preset);
            return 1;
        }
        preset->ApplyPass(0);
        for (int stage = 0; stage < preset->stages; ++stage) SetSinglePassStage(this, stage, current->stages[stage]);
    }
    render::MeshPass(this);
    return 1;
}

// Draws the indexed primitives with `first` on stage 0, then again blended (source
// alpha / inverse source alpha, blending on) with `second` and texture coordinates 0. Returns 1.
int TextureDevice::DrawTwoTextures(Surface* first, Surface* second) {
    stageSurfaces[0] = first;
    render::MeshPass(this);
    render::RenderState& state = render::g_renderState;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.blend = true;
    stageSurfaces[0] = second;
    render::MeshPass(this);
    return 1;
}
