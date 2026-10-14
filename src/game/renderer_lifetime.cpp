// The scene renderer's lifetime and small slots (the renderer at 007AF060, vtable 0073D93C; its base
// class, vtable 0073D8A0, owns +0x04 .. +0x1C): constructors and destructors, Destroy (slot 3), the
// draw colour and sun, the frame clear (slot 8), texture binding (slot 11), the figure draws (slots
// 13 / 14), the line list draw (15), the gamma getter (23), the surfaces' restore after a switch back
// (24), and the device object's counted holder. The old Direct3D 7 path, kept as it is
// (docs/notes/renderer.md). A-U11.
#include "model.h"         // CSceneRenderer, CTransformPipeline
#include "texture.h"       // CTextureDevice, CTexture, CDXSurface
#include "device.h"        // device::WithoutDirectDraw

#include "figure_mesh.h"   // CFigureMesh
#include "link_figure.h"   // ITransformPipeline, FigureMeshDrawer, DrawFigurePart(Plain)
#include "spell.h"         // CLight
#include "ui.h"            // GameDeviceLock, UiTextureManager
#include "originals_engine.h"   // game::FatalError
#include "originals_render.h"
#include "originals_ui.h"       // TextureLoadOriginal, TextureFindOriginal

#include "ei/hook.h"
#include "ei/vcall.h"      // SetVtable
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <d3d.h>

#include <bit>
#include <cstdint>

namespace {

constexpr std::uintptr_t kBaseVtable = std::uintptr_t(0x0073D8A0);
constexpr std::uintptr_t kRendererVtable = std::uintptr_t(0x0073D93C);
constexpr float kColourScale = 255.0f;          // 0073C370
constexpr float kInverseColourScale = std::bit_cast<float>(0x3B808081u);   // 0073D904, 1 / 255
constexpr std::uint32_t kFigureHidden = 0x80;   // CFigureMesh::flags_
constexpr std::uint32_t kTextureIsLoaded = 8;   // CTexture::flags_ (CEngineObject)

// 0078EDF8: "Final loading data to NULL texture!" and a newline.
const char* NullTextureText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078EDF8)); }

IDirect3DDevice7* D3DDevice(CTextureDevice* device) { return static_cast<IDirect3DDevice7*>(device->d3dDevice_); }
IDirectDrawSurface7* Surface(CDXSurface* holder) { return static_cast<IDirectDrawSurface7*>(holder->surface); }

} // namespace

// --- Base class (vtable 0073D8A0) ------------------------------------------------------------------

CSceneRenderer* CSceneRenderer::ConstructBase() {
    pipeline_ = nullptr;
    ei::SetVtable(this, kBaseVtable);
    flags_ = 0;
    sun_ = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x00500440, CSceneRenderer::ConstructBase);

void CSceneRenderer::DestroyBase() {
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
EI_REPLACE_METHOD(0x005006E0, CSceneRenderer::DestroyBase);

// The member destructor of pipeline_ then finds it null.
void CSceneRenderer::DestructBase() {
    ei::SetVtable(this, kBaseVtable);
    DestroyBase();
}
EI_REPLACE_METHOD(0x00500480, CSceneRenderer::DestructBase);

CSceneRenderer* CSceneRenderer::DeleteBase(unsigned flags) {
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00500460, CSceneRenderer::DeleteBase);

void CSceneRenderer::SetBaseColour(std::uint32_t colour) {
    colour_ = colour;
    ITransformPipeline* transform = pipeline_->transform_;
    // Each byte (FILD of a 64-bit integer) times the float constant, stored as the float argument.
    const int red = static_cast<int>((colour >> 16) & 0xFF);
    const int green = static_cast<int>((colour >> 8) & 0xFF);
    const int blue = static_cast<int>(colour & 0xFF);
    transform->SetAmbient(red * kInverseColourScale, green * kInverseColourScale, blue * kInverseColourScale);
}
EI_REPLACE_METHOD(0x00500740, CSceneRenderer::SetBaseColour);

void CSceneRenderer::SetSun(CLight* light) {
    CLight* old = sun_;
    sun_ = light;
    if (light) ++light->refCount_;
    if (old) old->Release();
    if (pipeline_ && pipeline_->transform_) pipeline_->transform_->AddSun(sun_);
}
EI_REPLACE_METHOD(0x005007C0, CSceneRenderer::SetSun);

void CSceneRenderer::SetSunAndAmbient(CLight* light, float r, float g, float b) {
    SetSun(light);
    // Each product is stored as a float, then FISTP rounds it to the nearest integer.
    const std::uint32_t red = static_cast<std::uint32_t>(x87::fistp(r * kColourScale));
    const std::uint32_t green = static_cast<std::uint32_t>(x87::fistp(g * kColourScale));
    const std::uint32_t blue = static_cast<std::uint32_t>(x87::fistp(b * kColourScale));
    SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
}
EI_REPLACE_METHOD(0x00500800, CSceneRenderer::SetSunAndAmbient);

// --- The renderer (vtable 0073D93C) --------------------------------------------------------------

// 00500960 (cdecl, no arguments). The renderer.
CSceneRenderer* __cdecl SceneRenderer() { return static_cast<CSceneRenderer*>(SceneContext()); }
EI_REPLACE(0x00500960, SceneRenderer);

CSceneRenderer* CSceneRenderer::ConstructRenderer() {
    ConstructBase();
    device_ = nullptr;
    helper_ = nullptr;
    ei::SetVtable(this, kRendererVtable);
    return this;
}
EI_REPLACE_METHOD(0x00500A70, CSceneRenderer::ConstructRenderer);

void CSceneRenderer::DestructRenderer() {
    ei::SetVtable(this, kRendererVtable);
    CTextureDevice* device = device_;
    device_ = nullptr;
    if (device) device->Release();
    // The helper's member destructor: released, the pointer left as it is.
    if (helper_ && --helper_->refs_ == 0) helper_->DeleteThis(1);
    DestructBase();
}
EI_REPLACE_METHOD(0x00500AC0, CSceneRenderer::DestructRenderer);

void* CSceneRenderer::DeleteRenderer(unsigned flags) {
    DestructRenderer();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
void* __fastcall RendererDeleteHook(CSceneRenderer* self, void*, unsigned flags) {
    return self->CSceneRenderer::DeleteRenderer(flags);
}
EI_REPLACE(0x00500AA0, RendererDeleteHook);

void CSceneRenderer::DestroyRenderer() {
    DestroyBase();
    flags_ &= ~2u;
    CRenderHelper* helper = helper_;
    helper_ = nullptr;
    if (helper && --helper->refs_ == 0) helper->DeleteThis(1);
    CTextureDevice* device = device_;
    device_ = nullptr;
    if (device) device->Release();
}
void __fastcall RendererDestroyHook(CSceneRenderer* self) { self->CSceneRenderer::DestroyRenderer(); }
EI_REPLACE(0x005019F0, RendererDestroyHook);

int CSceneRenderer::ClearFrame() {
    const std::uint32_t colour = fogColour_;
    CTextureDevice* device = device_;
    DWORD flags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
    if ((device->depthFormat_.words[1] & DDPF_STENCILBUFFER) && device->depthFormat_.words[4] != 0) flags |= D3DCLEAR_STENCIL;
    gameapi::EnterCriticalSection(GameDeviceLock());
    device->lastLoadResult_ = D3DDevice(device)->Clear(0, nullptr, flags, colour, 1.0f, 0);
    gameapi::LeaveCriticalSection(GameDeviceLock());
    return 1;
}
int __fastcall RendererClearFrameHook(CSceneRenderer* self) { return self->CSceneRenderer::ClearFrame(); }
EI_REPLACE(0x00501A40, RendererClearFrameHook);

std::uint32_t CSceneRenderer::ZEnable() { return device_->zEnable_; }
std::uint32_t __fastcall RendererZEnableHook(CSceneRenderer* self) { return self->CSceneRenderer::ZEnable(); }
EI_REPLACE(0x00501AA0, RendererZEnableHook);

// The holder is stored before the batch is flushed; no device lock.
int CSceneRenderer::SetTexture(int stage, void* texture) {
    CTextureDevice* device = device_;
    CDXSurface*& bound = device->stageSurfaces_[stage];
    if (!texture) {
        if (!bound) return 1;
        bound = nullptr;
        DeviceFlushBatchAOriginal(device);
        device->lastLoadResult_ = D3DDevice(device)->SetTexture(static_cast<DWORD>(stage), nullptr);
        ++device->textureChanges_;
        return 1;
    }
    CDXSurface* surface = static_cast<CTexture*>(texture)->surface_;
    if (bound == surface) return 1;
    bound = surface;
    DeviceFlushBatchAOriginal(device);
    device->lastLoadResult_ = D3DDevice(device)->SetTexture(static_cast<DWORD>(stage), surface ? Surface(surface) : nullptr);
    ++device->textureChanges_;
    return 1;
}
int __fastcall RendererSetTextureHook(CSceneRenderer* self, void*, int stage, void* texture) {
    return self->CSceneRenderer::SetTexture(stage, texture);
}
EI_REPLACE(0x00501AF0, RendererSetTextureHook);

void CSceneRenderer::SetColour(std::uint32_t colour) {
    SetBaseColour(colour);
    device_->colour_ = colour;
}
void __fastcall RendererSetColourHook(CSceneRenderer* self, void*, std::uint32_t colour) {
    self->CSceneRenderer::SetColour(colour);
}
EI_REPLACE(0x00501CC0, RendererSetColourHook);

void CSceneRenderer::DrawLineList(void* lines) { LineListDrawOriginal(lines, nullptr, pipeline_, device_); }
void __fastcall RendererDrawLineListHook(CSceneRenderer* self, void*, void* lines) {
    self->CSceneRenderer::DrawLineList(lines);
}
EI_REPLACE(0x00501CE0, RendererDrawLineListHook);

int CSceneRenderer::DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                               tagRECT* screen) {
    auto* mesh = static_cast<CFigureMesh*>(figure);
    if (mesh->flags_ & kFigureHidden) return 1;
    return DrawFigurePartPlain(mesh->drawer_, nullptr, device_,
                               reinterpret_cast<int>(unknownA), reinterpret_cast<int>(unknownB),
                               reinterpret_cast<int>(material), alpha, screen);
}
int __fastcall RendererDrawFigureHook(CSceneRenderer* self, void*, void* figure, const void* unknownA,
                                      const void* unknownB, void* material, int alpha, tagRECT* screen) {
    return self->CSceneRenderer::DrawFigure(figure, unknownA, unknownB, material, alpha, screen);
}
EI_REPLACE(0x00501D00, RendererDrawFigureHook);

int CSceneRenderer::DrawFigureLit(void* figure, const void* lights, void* material, int alpha, tagRECT* screen,
                                  CTexture* textureA, CTexture* textureB) {
    auto* mesh = static_cast<CFigureMesh*>(figure);
    if (mesh->flags_ & kFigureHidden) return 1;
    return DrawFigurePart(mesh->drawer_, nullptr, device_, static_cast<const FigureLights*>(lights),
                          static_cast<FigureMaterial*>(material), alpha, screen, textureA, textureB);
}
int __fastcall RendererDrawFigureLitHook(CSceneRenderer* self, void*, void* figure, const void* lights, void* material,
                                         int alpha, tagRECT* screen, CTexture* textureA, CTexture* textureB) {
    return self->CSceneRenderer::DrawFigureLit(figure, lights, material, alpha, screen, textureA, textureB);
}
EI_REPLACE(0x00501D40, RendererDrawFigureLitHook);

void CSceneRenderer::GetGamma(float* brightness, float* contrast, float* gamma) const {
    *brightness = brightness_;
    *contrast = contrast_;
    *gamma = gamma_;
}
EI_REPLACE_METHOD(0x00504A40, CSceneRenderer::GetGamma);

// Without a primary surface the original passes an unwritten local as all three display mode values
// (not reproducible; ours passes 0). A full-screen device always has one.
bool CSceneRenderer::RestoreSurfaces() {
    if (!device_) return false;
    // The switch-over (device.h) has no DirectDraw surfaces to lose, nor a display mode to set
    // again (a lost Direct3D 11 device is ticket R9c's).
    if (device::WithoutDirectDraw()) return true;
    if (device_->flags_ & 1) {
        RECT screen = device_->screen_;
        DeviceSetCooperativeLevelOriginal(device_, nullptr, 1, &screen);
        CTextureDevice* device = device_;
        int width = 0, height = 0, bits = 0;
        if (device->primary_) {
            width = device->renderTarget_->width;
            height = device->renderTarget_->height;
            bits = device->renderTarget_->bitsPerPixel;
        }
        DeviceSetDisplayModeOriginal(device, nullptr, width, height, bits, 0);
    }
    CTextureDevice* device = device_;
    gameapi::EnterCriticalSection(GameDeviceLock());
    device->lastLoadResult_ = static_cast<IDirectDraw7*>(device->ddraw_)->RestoreAllSurfaces();
    gameapi::LeaveCriticalSection(GameDeviceLock());
    Surface(device_->primary_)->Restore();
    Surface(device_->renderTarget_)->Restore();
    Surface(device_->depth_)->Restore();
    return TextureManagerRestoreOriginal(UiTextureManager());
}
EI_REPLACE_METHOD(0x00504780, CSceneRenderer::RestoreSurfaces);

// --- The device object's counted holder (+0x20) ----------------------------------------------------

// 00504E00 (the holder's destructor; reached from an unwind funclet of the creation). Releases the
// device object, the pointer left as it is.
void __fastcall DeviceHolderRelease(CTextureDevice** holder) {
    if (*holder) (*holder)->Release();
}
EI_REPLACE(0x00504E00, DeviceHolderRelease);
// Byte-identical copies: other counted holders released the same way (an int count at +4, slot 0 with 1 at zero).
EI_REPLACE(0x00519560, DeviceHolderRelease);
EI_REPLACE(0x0051E3E0, DeviceHolderRelease);
EI_REPLACE(0x0055FCE0, DeviceHolderRelease);

// 00504E20 (RET 4). The holder takes `device` (one more reference), then the old one is released.
void __fastcall DeviceHolderAssign(CTextureDevice** holder, void*, CTextureDevice* device) {
    CTextureDevice* old = *holder;
    *holder = device;
    if (device) device->AddRef();
    if (old) old->Release();
}
EI_REPLACE(0x00504E20, DeviceHolderAssign);

// --- Texture holders -----------------------------------------------------------------------------

int __fastcall TextureHolderEnsureLoaded(CTexture** holder) {
    CTexture* texture = *holder;
    if (!texture) return game::FatalError(NullTextureText());
    if (texture->flags_ & kTextureIsLoaded) return 1;
    return TextureLoadOriginal(UiTextureManager(), nullptr, texture);
}
EI_REPLACE(0x00500970, TextureHolderEnsureLoaded);

void __fastcall TextureHolderLoad(CTexture** holder, void*, int kind, const char* name) {
    if (CTexture* old = *holder) {
        *holder = nullptr;
        old->Release();
    }
    auto* manager = static_cast<CTextureManager*>(UiTextureManager());
    auto* texture = static_cast<CTexture*>(TextureFindOriginal(manager, nullptr, kind, name));
    if (!texture) texture = static_cast<CTexture*>(manager->CreateObject(kind, -1));
    *reinterpret_cast<mfc::CString*>(&texture->name_) = name;
    CTexture* old = *holder;
    *holder = texture;
    if (texture) ++texture->refCount_;
    if (old) old->Release();
}
EI_REPLACE(0x00500A00, TextureHolderLoad);

// --- Aligned array constructors --------------------------------------------------------------------
// 00504F60, 00505140, 00505260: the constructors of the sprite draw's work arrays (TLVertex, sort
// records, their pointers; 00502CA0's statics, 00561536), every field zero (lib::AlignedArray).
EI_REPLACE_METHOD(0x00504F60, lib::AlignedArray<std::uint32_t>::Construct);
EI_REPLACE_METHOD(0x00505140, lib::AlignedArray<std::uint32_t>::Construct);
EI_REPLACE_METHOD(0x00505260, lib::AlignedArray<std::uint32_t>::Construct);
