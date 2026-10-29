// Small draw-state setters of the renderer objects (src/render/render_state.h): the renderer's fog colour, its blend
// factors (the particles' additive / normal switch) and the device object's material. See docs/notes/renderer.md
// ("Pass state").
#include "device_material.h"
#include "model.h"   // CSceneRenderer
#include "texture.h"

#include "render/render_state.h"

#include <cstdint>

// Sets the fog colour. Returns 1.
int CSceneRenderer::SetFogColourState(std::uint32_t newColour) {
    render::g_renderState.fogColour = newColour;
    return 1;
}

// Sets the blend factors (the particle draw: source alpha and one for additive sources, source alpha and inverse
// source alpha to go back). Returns 1.
int CSceneRenderer::SetBlend(render::Blend source, render::Blend dest) {
    render::g_renderState.sourceBlend = source;
    render::g_renderState.destBlend = dest;
    return 1;
}


// The current material (material, counted; the old one released when its count reaches 0); without a material
// stage 1's colour and alpha operations are turned off. Returns 1.
int CTextureDevice::SetMaterial(CDeviceMaterial* newMaterial) {
    CDeviceMaterial* old = material;
    material = newMaterial;
    if (newMaterial) newMaterial->AddRef();
    if (old) old->Release();
    if (!newMaterial) {
        render::g_renderState.stages[1].combiner.colourOp = render::TextureOp::Disable;
        render::g_renderState.stages[1].combiner.alphaOp = render::TextureOp::Disable;
    }
    return 1;
}
