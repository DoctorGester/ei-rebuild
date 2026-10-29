// The scene renderer's lifetime and small members:
// constructors and destructors, Destroy, the draw colour and sun, the frame clear, texture binding, the
// figure draws, the line list draw, the gamma getter, the surfaces' restore after a
// switch back, and the device object's counted holder (docs/notes/renderer.md).
#include "model.h"         // CSceneRenderer, CTransformPipeline
#include "renderer_lines.h"
#include "texture.h"       // CTextureDevice, CTexture, Surface

#include "figure_mesh.h"   // CFigureMesh
#include "link_figure.h"   // ITransformPipeline, FigureMeshDrawer
#include "spell.h"         // CLight
#include "ui.h"            // GameDeviceLock, UiTextureManager
#include "game_singletons.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>

#include <cstdlib>

#include <bit>
#include <new>

namespace {

constexpr float kColourScale = 255.0f;
constexpr float kInverseColourScale = 0.003921569f;   // 1 / 255
static_assert(std::bit_cast<std::uint32_t>(kInverseColourScale) == 0x3B808081u);
constexpr std::uint32_t kFigureHidden = 0x80;   // CFigureMesh::flags_
// TransformAmbientColour: what SetBaseColour gave the transform object last.
float g_transformAmbientColour[3] = {};
constexpr std::uint32_t kTextureIsLoaded = 8;   // CTexture::flags_ (CEngineObject)

// "Final loading data to NULL texture!" and a newline.
const char* NullTextureText() { return "Final loading data to NULL texture!\n"; }

} // namespace

// --- The renderer -----------------------------------

CSceneRenderer::~CSceneRenderer() {
    device_.Reset();
    helper_.Reset();
    ReleasePipeline();
}

void CSceneRenderer::ReleasePipeline() {
    sun_.Reset();
    if (pipeline_ && pipeline_->transform_) pipeline_->transform_->AddSun(sun_);
    pipeline_.Reset();
}

void CSceneRenderer::SetColour(std::uint32_t colour) {
    colour_ = colour;
    ITransformPipeline* transform = pipeline_->transform_;
    // Each byte (as a 64-bit integer) times the float constant, stored as the float argument.
    const int red = static_cast<int>((colour >> 16) & 0xFF);
    const int green = static_cast<int>((colour >> 8) & 0xFF);
    const int blue = static_cast<int>(colour & 0xFF);
    g_transformAmbientColour[0] = red * kInverseColourScale;
    g_transformAmbientColour[1] = green * kInverseColourScale;
    g_transformAmbientColour[2] = blue * kInverseColourScale;
    transform->SetAmbient(g_transformAmbientColour[0], g_transformAmbientColour[1], g_transformAmbientColour[2]);
    device_->colour_ = colour;
}

const float* TransformAmbientColour() { return g_transformAmbientColour; }

void CSceneRenderer::SetSun(CLight* light) {
    sun_.Assign(light);
    if (pipeline_ && pipeline_->transform_) pipeline_->transform_->AddSun(sun_);
}

void CSceneRenderer::SetSunAndAmbient(CLight* light, float r, float g, float b) {
    SetSun(light);
    // Each product is stored as a float, then it is rounded to the nearest integer.
    const std::uint32_t red = static_cast<std::uint32_t>(x87::fistp(r * kColourScale));
    const std::uint32_t green = static_cast<std::uint32_t>(x87::fistp(g * kColourScale));
    const std::uint32_t blue = static_cast<std::uint32_t>(x87::fistp(b * kColourScale));
    SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
}

// The scene renderer and the device's lock, zero at the start: built by their static initialisers
// in the list's order and destroyed by the exit functions those register (docs/notes/standalone.md, "The static
// initialisers' objects").
constinit lib::StaticObject<CSceneRenderer> g_sceneRenderer = /*007AF060*/ {};
constinit CRITICAL_SECTION g_deviceLock = /*007AEE78*/ {};

// A second block: it uses g_sceneRenderer and g_deviceLock (defined above).
namespace {
void DestroySceneRenderer() { g_sceneRenderer.Destroy(); }
void DestroyDeviceLock() { gameapi::DeleteCriticalSection(&g_deviceLock); }
} // namespace

void ConstructSceneRenderer() {
    g_sceneRenderer.Construct();
    std::atexit(DestroySceneRenderer);
}
void ConstructDeviceLock() {
    gameapi::InitializeCriticalSection(&g_deviceLock);
    std::atexit(DestroyDeviceLock);
}

void CSceneRenderer::DestroyRenderer() {
    ReleasePipeline();
    flags_ &= ~2u;
    helper_.Reset();
    device_.Reset();
}

// The holder is stored (the new renderer draws with it); no device lock.
int CSceneRenderer::SetTexture(int stage, CTexture* texture) {
    device_->stageSurfaces_[stage] = texture ? texture->surface_.Get() : nullptr;
    return 1;
}

void CSceneRenderer::DrawLineList(LineBatch* lines) { lines->Draw(pipeline_, device_); }

int CSceneRenderer::DrawFigure(CFigureMesh* mesh, CMaterial* material, int alpha, tagRECT* screen) {
    if (mesh->flags_ & kFigureHidden) return 1;
    return mesh->drawer_->DrawPlain(device_, material, alpha, screen);
}

int CSceneRenderer::DrawFigureLit(CFigureMesh* mesh, const std::vector<CLight*>* lights, CMaterial* material, int alpha, tagRECT* screen,
                                  CTexture* textureA, CTexture* textureB) {
    if (mesh->flags_ & kFigureHidden) return 1;
    return mesh->drawer_->DrawLit(device_, lights, material, alpha, screen, textureA, textureB);
}

void CSceneRenderer::GetGamma(float* brightness, float* contrast, float* gamma) const {
    *brightness = brightness_;
    *contrast = contrast_;
    *gamma = gamma_;
}

// There are no DirectDraw surfaces to lose nor a display mode to set again.
bool CSceneRenderer::RestoreSurfaces() { return device_ != nullptr; }

// --- Texture holders -----------------------------------------------------------------------------

int EnsureTextureLoaded(CTexture* texture) {
    if (!texture) return game::FatalError(NullTextureText());
    if (texture->flags_ & kTextureIsLoaded) return 1;
    return g_clientWorld->textureManager_->LoadTexture(texture);
}

void LoadNamedTexture(Ref<CTexture>& slot, int kind, const char* name) {
    slot.Reset();
    auto* manager = g_clientWorld->textureManager_;
    auto* found = manager->FindTypedTexture(kind, name);
    if (!found) found = manager->NewTextureOfType(kind);
    found->name_ = name;
    slot.Assign(found);
}
