// The device's indexed draw with the current material (on the device holder): used by the mesh draws and the flat
// geometry draw. Without a material it is one counted pass. A material (TextureDevice::material)
// brings its blend, a preset (its passes' states, model.h) and texture stages: with one pass the stages are
// set and drawn once; with more, the first pass sets every stage, each further pass applies its
// states, sets stage 0 from the next stage record and draws again, and the blend states are
// restored at the end. A second form draws twice: with a first surface, then blended (source alpha, inverse source
// alpha) with a second one. See docs/notes/models.md ("Material draw").
#include "device_material.h"

#include "app.h"
#include "device_buffers.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "ground_print.h"       // NamedTexture
#include "model.h"   // RenderPreset
#include "present_window.h"
#include "texture.h"
#include "world.h"
#include "lib/rect.h"
#include "lib/rounding.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cfloat>
#include <cstdint>
#include <vector>

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

// --- Device binds ------------------------------------------------------------------------------------

// The device object's texture binds: a surface holder, a texture, a texture kept by name. See
// docs/notes/renderer.md (section 12).

int TextureDevice::BindSurface(int stage, Surface* holder) {
    stageSurfaces[stage] = holder;
    return 1;
}

// The same body as BindSurface's, for a texture and for none.
int TextureDevice::BindTexture(int stage, Texture* texture) {
    return BindSurface(stage, texture ? texture->surface.Get() : nullptr);
}

int TextureDevice::BindNamedTexture(int stage, NamedTexture* named) {
    if (!named->texture && named->name.size() != 0) {
        Texture* found = g_clientWorld.textureManager->FindTexture(named->name.data());
        named->texture.Assign(found);
    }
    return BindSurface(stage, named->texture ? named->texture->surface.Get() : nullptr);
}

// --- Device states -----------------------------------------------------------------------------------

// Small draw-state setters of the renderer objects (src/render/render_state.h): the renderer's fog colour, its blend
// factors (the particles' additive / normal switch) and the device object's material. See docs/notes/renderer.md
// ("Pass state").

// Sets the fog colour. Returns 1.
int SceneRenderer::SetFogColourState(std::uint32_t newColour) {
    render::g_renderState.fogColour = newColour;
    return 1;
}

// Sets the blend factors (the particle draw: source alpha and one for additive sources, source alpha and inverse
// source alpha to go back). Returns 1.
int SceneRenderer::SetBlend(render::Blend source, render::Blend dest) {
    render::g_renderState.sourceBlend = source;
    render::g_renderState.destBlend = dest;
    return 1;
}

// The current material (material, counted; the old one released when its count reaches 0); without a material
// stage 1's colour and alpha operations are turned off. Returns 1.
int TextureDevice::SetMaterial(DeviceMaterial* newMaterial) {
    DeviceMaterial* old = material;
    material = newMaterial;
    if (newMaterial) newMaterial->AddRef();
    if (old) old->Release();
    if (!newMaterial) {
        render::g_renderState.stages[1].combiner.colourOp = render::TextureOp::Disable;
        render::g_renderState.stages[1].combiner.alphaOp = render::TextureOp::Disable;
    }
    return 1;
}

// --- Device buffers ----------------------------------------------------------------------------------

// The Direct3D device's mesh work buffers (device_buffers.h).

namespace devicebuffers {

std::vector<Vector4> g_projected;
std::vector<std::uint8_t> g_outcodes;

} // namespace devicebuffers

// --- Device present ----------------------------------------------------------------------------------

// The device object's present, called once per frame by the frame function after the scene, the interface and the
// software cursor are drawn. The game flips the primary's chain in full screen and copies the render target to the
// primary at the client rectangle's corner in a window; here it ends the new renderer's frame, which shows it on the
// game window placed first (full screen over its monitor, present_window.h). See docs/notes/renderer.md.

// Returns the DirectDraw result: 0.
long DevicePresent(TextureDevice* device) {
    present_window::Update(device);
    render::Present();
    return 0;
}

// --- Device bounds -----------------------------------------------------------------------------------

// The Direct3D device's screen rectangle of a mesh draw (mesh draw flag 1): the bounds of its projected positions
// clamped to the screen, rounded into the device's drawn rectangle (+0x15C), which the figures' and the 3D library's
// draws read back for picking. See docs/notes/renderer.md (section 3, "Picking").

namespace {

// The bounds so far: they start inside out, and a coordinate replaces a bound it is not inside of
// (a NaN never does).
struct ScreenBounds {
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;

    void Include(float x, float y) {
        minX = x < minX ? x : minX;
        maxX = maxX < x ? x : maxX;
        minY = y < minY ? y : minY;
        maxY = maxY < y ? y : maxY;
    }

    // Clamped to the screen (0..width, 0..height); bounds that hold no point, or none on screen, come out inside out
    // (an empty rectangle).
    void Clamp(float width, float height) {
        minX = minX < 0.0f ? 0.0f : minX;
        minY = minY < 0.0f ? 0.0f : minY;
        maxX = width < maxX ? width : maxX;
        maxY = height < maxY ? height : maxY;
    }

    // The bounds rounded to nearest into the device's rectangle.
    void Store(TextureDevice* device) const {
        const int bottom = lib::RoundEven(maxY);
        const int right = lib::RoundEven(maxX);
        const int top = lib::RoundEven(minY);
        const int left = lib::RoundEven(minX);
        rects::Set(&device->drawnRect, left, top, right, bottom);
    }
};

// A clip-space point inside the depth range: in front of the near plane (z = 0) and not past the far plane (z = w).
bool InDepth(const Vector4& p) { return p.z >= 0.0f && p.w - p.z >= 0.0f; }

} // namespace

// The projection leaves screen positions when every one is on screen, clip-space ones when the mesh crosses the view's
// edges: those count when inside the depth range (nothing behind the camera), mapped as the pipeline's ToScreen maps
// them (x right and y down from the top left corner, the viewport the screen's size). Past a corner of the screen
// this is larger than the original's rectangle (the bounds of the clipped triangles), and across one edge it can be
// wider along that edge: an accepted difference (renderer.md section 3).
void TextureDevice::BoundsOfProjected(const std::vector<Vector4>* positions, bool clipSpace) {
    const float width = static_cast<float>(screen.right - screen.left);
    const float height = static_cast<float>(screen.bottom - screen.top);
    ScreenBounds bounds;
    for (const Vector4& p : *positions) {
        if (!clipSpace) {
            bounds.Include(p.x, p.y);
        } else if (InDepth(p) && p.w > 0.0f) {
            const Vector4 newScreen = pipeline->ToScreen(p);
            bounds.Include(newScreen.x, newScreen.y);
        }
    }
    bounds.Clamp(width, height);
    bounds.Store(this);
}
