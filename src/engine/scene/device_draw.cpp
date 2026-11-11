// The device's indexed draw with the current material (on the device holder): used by the mesh draws and the flat
// geometry draw. Without a material it is one counted pass. A material (TextureDevice::material)
// brings its blend, a preset (its stages' states, render_helper.h) and texture stages: the stages are set and drawn
// once. See docs/notes/models.md ("Material draw").
#include "engine/scene/device_material.h"

#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/render_helper.h"   // RenderPreset
#include "engine/models/texture.h"
#include "engine/core/engine_globals.h"
#include "engine/core/engine_object.h"
#include "engine/core/manager_context.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>

namespace {

// The material's blend: blending on with its factors.
void SetMaterialBlend(const DeviceMaterial* material) {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.sourceBlend = material->sourceBlend;
    state.destBlend = material->destBlend;
}

// The stage's texture, looked up by name on first use; a stage without one unbinds the device's texture of that stage
// (stageSurfaces) once.
void SetStage(TextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, s.texture->surface.Get());
        return;
    }
    if (!s.name.empty()) {
        s.texture.Assign(g_sceneRenderer.world->GetTextureManager()->FindTexture(s.name.data()));
        device->BindSurface(stage, s.texture ? s.texture->surface.Get() : nullptr);
        return;
    }
    device->stageSurfaces[stage] = nullptr;
}

} // namespace

// Draws the mesh the renderer has with the current material.
void TextureDevice::DrawIndexed() {
    DeviceMaterial* current = material;
    if (current) {
        RenderPreset* preset = current->preset;
        if (current->blend) SetMaterialBlend(current);
        else render::g_renderState.blend = false;
        preset->Apply();
        for (int stage = 0; stage < preset->StageCount(); ++stage) SetStage(this, stage, current->stages[stage]);
    }
    render::MeshPass(this);
}


// --- Device binds ------------------------------------------------------------------------------------

// The device object's texture binds: a surface holder, a texture. See docs/notes/renderer.md (section 12).

void TextureDevice::BindSurface(int stage, Surface* holder) { stageSurfaces[stage] = holder; }

void TextureDevice::BindTexture(int stage, Texture* texture) {
    BindSurface(stage, texture ? texture->surface.Get() : nullptr);
}


// --- Device states -----------------------------------------------------------------------------------

// Small draw-state setters of the renderer objects (src/render/render_state.h): the renderer's fog colour, its blend
// factors (the particles' additive / normal switch) and the device object's material. See docs/notes/renderer.md
// ("Pass state").

void SceneRenderer::SetFogColourState(std::uint32_t newColour) { render::g_renderState.fogColour = newColour; }

// Sets the blend factors (the particle draw: source alpha and one for additive sources, source alpha and inverse
// source alpha to go back).
void SceneRenderer::SetBlend(render::Blend source, render::Blend dest) {
    render::g_renderState.sourceBlend = source;
    render::g_renderState.destBlend = dest;
}

// The current material (material, counted; the old one released when its count reaches 0); without a material
// stage 1's colour and alpha operations are turned off.
void TextureDevice::SetMaterial(DeviceMaterial* newMaterial) {
    DeviceMaterial* old = material;
    material = newMaterial;
    if (newMaterial) newMaterial->AddRef();
    if (old) old->Release();
    if (!newMaterial) {
        render::g_renderState.stages[1].combiner.colourOp = render::TextureOp::Disable;
        render::g_renderState.stages[1].combiner.alphaOp = render::TextureOp::Disable;
    }
}

