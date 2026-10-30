// The renderer's scene bracket: BeginScene with the last frame's time, EndScene closing the frame. The main loop
// brackets each frame's drawing with them.
#include "core_maths.h"
#include "figure_mesh.h"   // FigureMesh
#include "game_singletons.h"
#include "lib/crt.h"
#include "platform/clock.h"
#include "link_figure.h"   // FigureMeshDrawer
#include "model.h"     // SceneRenderer
#include "renderer_lines.h"
#include "spell.h"     // CLight
#include "texture.h"   // TextureDevice, CTexture
#include "ui.h"
#include "world.h"

#include <bit>
#include <cmath>
#include <cstdint>

int SceneRenderer::BeginScene() {
    if (!(flags & 1)) return 0;
    std::uint32_t now = platform::Milliseconds();
    frameStart = now;
    frameTime = now - lastFrameStart;
    return 1;
}

int SceneRenderer::EndScene() {
    lastFrameStart = frameStart;
    return 1;
}

// --- The renderer's lifetime and small members: Destroy, the draw colour and sun, the frame clear, texture
// binding, the figure draws, the line list draw, the gamma getter, the surfaces' restore after a switch
// back, and the device object's counted holder (docs/notes/renderer.md).

namespace {

constexpr float kColourScale = 255.0f;
constexpr float kInverseColourScale = 0.003921569f;   // 1 / 255
static_assert(std::bit_cast<std::uint32_t>(kInverseColourScale) == 0x3B808081u);
// AmbientColour: the ambient colour SetColour set last.
float g_ambientColour[3] = {};

// "Final loading data to NULL texture!" and a newline.
const char* NullTextureText() { return "Final loading data to NULL texture!\n"; }

} // namespace

// --- The renderer -----------------------------------

SceneRenderer::~SceneRenderer() {
    device.Reset();
    helper.Reset();
    ReleasePipeline();
}

void SceneRenderer::ReleasePipeline() {
    sun.Reset();
    pipeline.Reset();
}

void SceneRenderer::SetColour(std::uint32_t newColour) {
    colour = newColour;
    // Each byte (as a 64-bit integer) times the float constant, stored as a float.
    const int red = static_cast<int>((newColour >> 16) & 0xFF);
    const int green = static_cast<int>((newColour >> 8) & 0xFF);
    const int blue = static_cast<int>(newColour & 0xFF);
    g_ambientColour[0] = red * kInverseColourScale;
    g_ambientColour[1] = green * kInverseColourScale;
    g_ambientColour[2] = blue * kInverseColourScale;
    device->colour = newColour;
}

const float* AmbientColour() { return g_ambientColour; }

void SceneRenderer::SetSun(CLight* light) { sun.Assign(light); }

void SceneRenderer::SetSunAndAmbient(CLight* light, float r, float g, float b) {
    SetSun(light);
    // Each product is stored as a float, then it is rounded to the nearest integer.
    const std::uint32_t red = static_cast<std::uint32_t>(std::lround(r * kColourScale));
    const std::uint32_t green = static_cast<std::uint32_t>(std::lround(g * kColourScale));
    const std::uint32_t blue = static_cast<std::uint32_t>(std::lround(b * kColourScale));
    SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
}

// The scene renderer: constructed by the C++ runtime and destroyed after the game's exit functions. ShutDown's
// DestroyRenderer has released its holders by then; on an exit that skips ShutDown its destructor drops the counts
// (the device object's destructor releases only the pipeline and the material: no device or window call).
SceneRenderer g_sceneRenderer;

void SceneRenderer::DestroyRenderer() {
    ReleasePipeline();
    flags &= ~2u;
    helper.Reset();
    device.Reset();
}

// The holder is stored (the new renderer draws with it); no device lock.
int SceneRenderer::SetTexture(int stage, CTexture* texture) {
    device->stageSurfaces[stage] = texture ? texture->surface.Get() : nullptr;
    return 1;
}

void SceneRenderer::DrawLineList(LineBatch* lines) { lines->Draw(pipeline, device); }

int SceneRenderer::DrawFigure(FigureMesh* mesh, Material* material, int alpha, Rect* screen) {
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    return mesh->drawer->DrawPlain(device, material, alpha, screen);
}

int SceneRenderer::DrawFigureLit(FigureMesh* mesh, const std::vector<CLight*>* lights, Material* material, int alpha, Rect* screen,
                                  CTexture* textureA, CTexture* textureB) {
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    return mesh->drawer->DrawLit(device, lights, material, alpha, screen, textureA, textureB);
}

// There are no DirectDraw surfaces to lose nor a display mode to set again.
bool SceneRenderer::RestoreSurfaces() { return device != nullptr; }

// --- Texture holders -----------------------------------------------------------------------------

int EnsureTextureLoaded(CTexture* texture) {
    if (!texture) return game::FatalError(NullTextureText());
    if (texture->flags & TextureFlag::Loaded) return 1;
    return g_clientWorld.textureManager->LoadTexture(texture);
}

void LoadNamedTexture(Ref<CTexture>& slot, int kind, const char* name) {
    slot.Reset();
    auto* manager = g_clientWorld.textureManager;
    auto* found = manager->FindTypedTexture(kind, name);
    if (!found) found = manager->NewTextureOfType(kind);
    found->name = name;
    slot.Assign(found);
}
