// The scene renderer's lifetime and small slots (the renderer; its base class, owns +0x04.. +0x1C):
// constructors and destructors, Destroy, the draw colour and sun, the frame clear, texture binding, the
// figure draws (slots 13 / 14), the line list draw (15), the gamma getter (23), the surfaces' restore after a
// switch back (24), and the device object's counted holder. The old Direct3D 7 path, kept as it is
// (docs/notes/renderer.md).
#include "model.h"         // CSceneRenderer, CTransformPipeline
#include "renderer_lines.h"
#include "texture.h"       // CTextureDevice, CTexture, CDXSurface
#include "device.h"        // the old device's calls

#include "figure_mesh.h"   // CFigureMesh
#include "link_figure.h"   // ITransformPipeline, FigureMeshDrawer
#include "spell.h"         // CLight
#include "ui.h"            // GameDeviceLock, UiTextureManager
#include "game_singletons.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <d3d.h>

#include <bit>
#include <cstdint>
#include <new>

namespace {

constexpr float kColourScale = 255.0f;
constexpr float kInverseColourScale = std::bit_cast<float>(0x3B808081u);   // 1 / 255
constexpr std::uint32_t kFigureHidden = 0x80;   // CFigureMesh::flags_
// TransformAmbientColour: what SetBaseColour gave the transform object last.
float g_transformAmbientColour[3] = {};
constexpr std::uint32_t kTextureIsLoaded = 8;   // CTexture::flags_ (CEngineObject)

// "Final loading data to NULL texture!" and a newline.
const char* NullTextureText() { return "Final loading data to NULL texture!\n"; }

IDirect3DDevice7* D3DDevice(CTextureDevice* device) { return device->d3dDevice_; }

} // namespace

// --- Base class (CVisualizator) ----------------------

CVisualizator* CVisualizator::SetUpBase() {
    pipeline_ = nullptr;
    flags_ = 0;
    sun_ = nullptr;
    return this;
}
EI_REWRITTEN(0x00500440, CVisualizator::SetUpBase);

CVisualizator::CVisualizator() { SetUpBase(); }

void CVisualizator::operator delete(void* memory) { gamecrt::operator_delete(memory); }

void CVisualizator::CreateRenderer(void* window, options::GameOptions* settings) { CreateBase(window, settings); }
void CVisualizator::DestroyRenderer() { DestroyBase(); }
void CVisualizator::SetColour(std::uint32_t colour) { SetBaseColour(colour); }

void CVisualizator::DestroyBase() {
    CLight* sun = sun_;
    sun_ = nullptr;
    if (sun) sun->Release();
    if (pipeline_ && pipeline_->transform_) pipeline_->transform_->AddSun(sun_);
    CTransformPipeline* pipeline = pipeline_;
    pipeline_ = nullptr;
    if (pipeline) pipeline->Release();
    if (graphicsLibrary_) gameapi::FreeLibrary(static_cast<HMODULE>(graphicsLibrary_));
    graphicsLibrary_ = nullptr;
    graphicsProcessor_ = nullptr;
}
EI_REWRITTEN(0x005006E0, CVisualizator::DestroyBase);

// The member destructor of pipeline_ then finds it null.
CVisualizator::~CVisualizator() { DestroyBase(); }
EI_REWRITTEN_SPECIAL(0x00500480, CVisualizator::~CVisualizator);
EI_REWRITTEN_SPECIAL(0x00500460, CVisualizator::~CVisualizator);   // deleting

void CVisualizator::SetBaseColour(std::uint32_t colour) {
    colour_ = colour;
    ITransformPipeline* transform = pipeline_->transform_;
    // Each byte (FILD of a 64-bit integer) times the float constant, stored as the float argument.
    const int red = static_cast<int>((colour >> 16) & 0xFF);
    const int green = static_cast<int>((colour >> 8) & 0xFF);
    const int blue = static_cast<int>(colour & 0xFF);
    g_transformAmbientColour[0] = red * kInverseColourScale;
    g_transformAmbientColour[1] = green * kInverseColourScale;
    g_transformAmbientColour[2] = blue * kInverseColourScale;
    transform->SetAmbient(g_transformAmbientColour[0], g_transformAmbientColour[1], g_transformAmbientColour[2]);
}
EI_REWRITTEN(0x00500740, CVisualizator::SetBaseColour);

const float* TransformAmbientColour() { return g_transformAmbientColour; }

void CVisualizator::SetSun(CLight* light) {
    CLight* old = sun_;
    sun_ = light;
    if (light) ++light->refCount_;
    if (old) old->Release();
    if (pipeline_ && pipeline_->transform_) pipeline_->transform_->AddSun(sun_);
}
EI_REWRITTEN(0x005007C0, CVisualizator::SetSun);

void CVisualizator::SetSunAndAmbient(CLight* light, float r, float g, float b) {
    SetSun(light);
    // Each product is stored as a float, then FISTP rounds it to the nearest integer.
    const std::uint32_t red = static_cast<std::uint32_t>(x87::fistp(r * kColourScale));
    const std::uint32_t green = static_cast<std::uint32_t>(x87::fistp(g * kColourScale));
    const std::uint32_t blue = static_cast<std::uint32_t>(x87::fistp(b * kColourScale));
    SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
}
EI_REWRITTEN(0x00500800, CVisualizator::SetSunAndAmbient);

// --- The renderer -----------------------------------

// The renderer.
CSceneRenderer* __cdecl SceneRenderer() { return TheSceneRenderer(); }
EI_REWRITTEN(0x00500960, SceneRenderer);

CSceneRenderer::CSceneRenderer() {
    device_ = nullptr;
    helper_ = nullptr;
}
EI_REWRITTEN_SPECIAL(0x00500A70, CSceneRenderer::CSceneRenderer);

CSceneRenderer::~CSceneRenderer() {
    CTextureDevice* device = device_;
    device_ = nullptr;
    if (device) device->Release();
    // The helper's member destructor: released, the pointer left as it is.
    if (helper_ && --helper_->refs_ == 0) delete helper_;
}
EI_REWRITTEN_SPECIAL(0x00500AC0, CSceneRenderer::~CSceneRenderer);

// The scene renderer and the device's lock, zero at the start: built by their static initialisers
// in the list's order and destroyed by the exit functions those register;
// names and registered at their addresses (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CSceneRenderer> g_sceneRenderer = /*007AF060*/ {};
static_assert(sizeof(CSceneRenderer) == 0x5C);
CSceneRenderer* TheSceneRenderer() { return &*g_sceneRenderer; }
constinit CRITICAL_SECTION g_deviceLock = /*007AEE78*/ {};
_RTL_CRITICAL_SECTION* TheDeviceLock() { return &g_deviceLock; }

namespace {
void __cdecl DestroySceneRenderer() { g_sceneRenderer.Destroy(); }
void __cdecl ConstructSceneRenderer() {
    g_sceneRenderer.Construct();
    libcrt::AtExit(DestroySceneRenderer);
}
void __cdecl DestroyDeviceLock() { gameapi::DeleteCriticalSection(&g_deviceLock); }
void __cdecl ConstructDeviceLock() {
    gameapi::InitializeCriticalSection(&g_deviceLock);
    libcrt::AtExit(DestroyDeviceLock);
}
} // namespace

EI_REPLACE(0x00500930, ConstructSceneRenderer);
EI_REPLACE(0x00500950, DestroySceneRenderer);
EI_REPLACE(0x004F0B10, ConstructDeviceLock);
EI_REPLACE(0x004F0B30, DestroyDeviceLock);

EI_REWRITTEN_SPECIAL(0x00500AA0, CSceneRenderer::~CSceneRenderer);   // deleting

void CSceneRenderer::DestroyRenderer() {
    DestroyBase();
    flags_ &= ~2u;
    CRenderHelper* helper = helper_;
    helper_ = nullptr;
    if (helper && --helper->refs_ == 0) delete helper;
    CTextureDevice* device = device_;
    device_ = nullptr;
    if (device) device->Release();
}
EI_REWRITTEN(0x005019F0, CSceneRenderer::DestroyRenderer);

int CSceneRenderer::ClearFrame() {
    const std::uint32_t colour = fogColour_;
    CTextureDevice* device = device_;
    DWORD flags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
    if ((device->depthFormat_.words[1] & DDPF_STENCILBUFFER) && device->depthFormat_.words[4] != 0) flags |= D3DCLEAR_STENCIL;
    gameapi::EnterCriticalSection(TheDeviceLock());
    device->lastLoadResult_ = device::Clear(D3DDevice(device), 0, nullptr, flags, colour, 1.0f, 0);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    return 1;
}
EI_REWRITTEN(0x00501A40, CSceneRenderer::ClearFrame);

std::uint32_t CSceneRenderer::ZEnable() { return device_->zEnable_; }
EI_REWRITTEN(0x00501AA0, CSceneRenderer::ZEnable);

// The holder is stored (the new renderer draws with it); no device lock.
int CSceneRenderer::SetTexture(int stage, void* texture) {
    CTextureDevice* device = device_;
    CDXSurface*& bound = device->stageSurfaces_[stage];
    CDXSurface* surface = texture ? static_cast<CTexture*>(texture)->surface_ : nullptr;
    if (bound == surface) return 1;
    bound = surface;
    device->lastLoadResult_ = device::SetTexture(D3DDevice(device), static_cast<DWORD>(stage), nullptr);
    ++device->textureChanges_;
    return 1;
}
EI_REWRITTEN(0x00501AF0, CSceneRenderer::SetTexture);

void CSceneRenderer::SetColour(std::uint32_t colour) {
    SetBaseColour(colour);
    device_->colour_ = colour;
}
EI_REWRITTEN(0x00501CC0, CSceneRenderer::SetColour);

void CSceneRenderer::DrawLineList(LineBatch* lines) { lines->Draw(pipeline_, device_); }
EI_REWRITTEN(0x00501CE0, CSceneRenderer::DrawLineList);

int CSceneRenderer::DrawFigure(CFigureMesh* mesh, const void* unknownA, const void* unknownB, CMaterial* material, int alpha,
                               tagRECT* screen) {
    if (mesh->flags_ & kFigureHidden) return 1;
    return mesh->drawer_->DrawPlain(device_, unknownA, unknownB, material, alpha, screen);
}
EI_REWRITTEN(0x00501D00, CSceneRenderer::DrawFigure);

int CSceneRenderer::DrawFigureLit(CFigureMesh* mesh, const FigureLights* lights, CMaterial* material, int alpha, tagRECT* screen,
                                  CTexture* textureA, CTexture* textureB) {
    if (mesh->flags_ & kFigureHidden) return 1;
    return mesh->drawer_->DrawLit(device_, lights, material, alpha, screen, textureA, textureB);
}
EI_REWRITTEN(0x00501D40, CSceneRenderer::DrawFigureLit);

void CSceneRenderer::GetGamma(float* brightness, float* contrast, float* gamma) const {
    *brightness = brightness_;
    *contrast = contrast_;
    *gamma = gamma_;
}
EI_REWRITTEN(0x00504A40, CSceneRenderer::GetGamma);

// There are no DirectDraw surfaces to lose nor a display mode to set again.
bool CSceneRenderer::RestoreSurfaces() { return device_ != nullptr; }
EI_REWRITTEN(0x00504780, CSceneRenderer::RestoreSurfaces);

// --- The device object's counted holder ----------------------------------------------------

// The holder takes `device` (one more reference), then the old one is released.
void HeldDevice::Assign(CTextureDevice* value) {
    CTextureDevice* old = device;
    device = value;
    if (value) value->AddRef();
    if (old) old->Release();
}
EI_REWRITTEN(0x00504E20, HeldDevice::Assign);

// --- Texture holders -----------------------------------------------------------------------------

int __fastcall TextureHolderEnsureLoaded(HeldTexture* holder) {
    CTexture* texture = *holder;
    if (!texture) return game::FatalError(NullTextureText());
    if (texture->flags_ & kTextureIsLoaded) return 1;
    return TheTextureManager()->LoadTexture(texture);
}
EI_REWRITTEN(0x00500970, TextureHolderEnsureLoaded);

void HeldTexture::Load(int kind, const char* name) {
    if (CTexture* old = texture) {
        texture = nullptr;
        old->Release();
    }
    auto* manager = TheTextureManager();
    auto* found = manager->FindTypedTexture(kind, name);
    if (!found) found = manager->NewTextureOfType(kind);
    found->name_ = name;
    CTexture* old = texture;
    texture = found;
    if (found) ++found->refCount_;
    if (old) old->Release();
}
EI_REWRITTEN(0x00500A00, HeldTexture::Load);

EI_REWRITTEN(0x00504F60, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x00505140, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x00505260, lib::AlignedArray<std::uint32_t>::Construct);
