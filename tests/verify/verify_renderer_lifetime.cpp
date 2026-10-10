// Checks for renderer_lifetime.cpp (A-U11).
// Compared with the original: SceneRenderer (00500960), ZEnable (slot 9, 00501AA0) and GetGamma
// (slot 23, 00504A40; the original writes into scratch floats).
// Ours only (Direct3D / DirectDraw calls, the transform object, counted objects):
// - the constructors (00500440, 00500A70: start-up) leave the vtable and null members; the
//   destructors and Destroy (00500460, 00500480, 005006E0, 00500AA0, 00500AC0, 005019F0: exit)
//   leave the sun, pipeline, library, device and helper null;
// - SetBaseColour / SetColour / SetSun / SetSunAndAmbient (00500740, 00501CC0, 005007C0, 00500800:
//   creation, the client's lighting every frame, interface models) leave the colour (also in the
//   device object) and the sun as given; SetSunAndAmbient's colour is predicted from r, g, b;
// - ClearFrame (slot 8, every BeginScene) returns 1; SetTexture (slot 11, every textured draw)
//   leaves the stage bound to the texture's surface holder (or null) and returns 1;
// - the figure draws (slots 13 / 14: units, objects, sky, interface models) return 1; the line
//   list draw (slot 15, debug bounds) is counted;
// - RestoreSurfaces (slot 24, a switch back to the game window; not reached by the harness): false
//   only without a device object;
// - the device holder (00504E00 unwind only, 00504E20 the creation): holds the device, one more
//   reference.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/spell.h"
#include "../../src/game/texture.h"
#include "../../src/game/link_figure.h"

#include "ei/x87.h"

#include <cstdint>
#include <cstring>

namespace {

void* s_unused[24];
void* s_unusedArray;

bool BaseCleared(const CSceneRenderer* r) {
    return !r->sun_ && !r->pipeline_ && !r->graphicsLibrary_ && !r->graphicsProcessor_;
}
std::uintptr_t VtableOf(const void* object) { return *static_cast<const std::uintptr_t*>(object); }

} // namespace

// --- Compared ------------------------------------------------------------------------------------

static CSceneRenderer*(__cdecl* s_sceneRenderer)();
static CSceneRenderer* __cdecl SceneRendererVerify() {
    CSceneRenderer* expected = s_sceneRenderer();
    CSceneRenderer* actual = SceneRenderer();
    ei::verify::Check("SceneRenderer", expected, actual, "%p", static_cast<void*>(actual));
    return actual;
}
EI_VERIFY(0x00500960, SceneRendererVerify, 5, s_sceneRenderer);

static std::uint32_t(__fastcall* s_zEnable)(CSceneRenderer*);
static std::uint32_t __fastcall ZEnableVerify(CSceneRenderer* self) {
    std::uint32_t expected = s_zEnable(self);
    std::uint32_t actual = self->CSceneRenderer::ZEnable();
    ei::verify::Check("CSceneRenderer::ZEnable", expected, actual, "%u", actual);
    return actual;
}
EI_VERIFY(0x00501AA0, ZEnableVerify, 6, s_zEnable);

static void(__fastcall* s_getGamma)(const CSceneRenderer*, void*, float*, float*, float*);
static void __fastcall GetGammaVerify(const CSceneRenderer* self, void*, float* brightness, float* contrast, float* gamma) {
    float expected[3] = {};
    s_getGamma(self, nullptr, &expected[0], &expected[1], &expected[2]);
    self->GetGamma(brightness, contrast, gamma);
    ei::verify::Check("CSceneRenderer::GetGamma", expected[0], *brightness, "brightness");
    ei::verify::Check("CSceneRenderer::GetGamma", expected[1], *contrast, "contrast");
    ei::verify::Check("CSceneRenderer::GetGamma", expected[2], *gamma, "gamma");
}
EI_VERIFY(0x00504A40, GetGammaVerify, 7, s_getGamma);

// --- Lifetime --------------------------------------------------------------------------------------

static CSceneRenderer* __fastcall ConstructBaseVerify(CSceneRenderer* self) {
    CSceneRenderer* result = self->ConstructBase();
    bool ok = result == self && VtableOf(self) == 0x0073D8A0 && self->flags_ == 0 && !self->pipeline_ && !self->sun_;
    ei::verify::Check("CSceneRenderer::ConstructBase", true, ok, "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x00500440, ConstructBaseVerify, 0, s_unused[0]);

static CSceneRenderer* __fastcall ConstructRendererVerify(CSceneRenderer* self) {
    CSceneRenderer* result = self->ConstructRenderer();
    bool ok = result == self && VtableOf(self) == 0x0073D93C && !self->device_ && !self->helper_ && !self->sun_;
    ei::verify::Check("CSceneRenderer::ConstructRenderer", true, ok, "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x00500A70, ConstructRendererVerify, 0, s_unused[1]);

static void __fastcall DestroyBaseVerify(CSceneRenderer* self) {
    self->DestroyBase();
    ei::verify::Check("CSceneRenderer::DestroyBase", true, BaseCleared(self), "%p", static_cast<void*>(self));
}
EI_VERIFY(0x005006E0, DestroyBaseVerify, 0, s_unused[2]);

static void __fastcall DestructBaseVerify(CSceneRenderer* self) {
    self->DestructBase();
    bool ok = BaseCleared(self) && VtableOf(self) == 0x0073D8A0;
    ei::verify::Check("CSceneRenderer::DestructBase", true, ok, "%p", static_cast<void*>(self));
}
EI_VERIFY(0x00500480, DestructBaseVerify, 0, s_unused[3]);

static CSceneRenderer* __fastcall DeleteBaseVerify(CSceneRenderer* self, void*, unsigned flags) {
    if (flags & 1) return self->DeleteBase(flags);   // freed: nothing left to look at
    CSceneRenderer* result = self->DeleteBase(flags);
    ei::verify::Check("CSceneRenderer::DeleteBase", true, result == self && BaseCleared(self), "%p",
                      static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x00500460, DeleteBaseVerify, 0, s_unused[4]);

static void __fastcall DestructRendererVerify(CSceneRenderer* self) {
    self->DestructRenderer();
    bool ok = BaseCleared(self) && !self->device_ && VtableOf(self) == 0x0073D8A0;
    ei::verify::Check("CSceneRenderer::DestructRenderer", true, ok, "%p", static_cast<void*>(self));
}
EI_VERIFY(0x00500AC0, DestructRendererVerify, 0, s_unused[5]);

static void* __fastcall DeleteRendererVerify(CSceneRenderer* self, void*, unsigned flags) {
    if (flags & 1) return self->CSceneRenderer::DeleteRenderer(flags);
    void* result = self->CSceneRenderer::DeleteRenderer(flags);
    ei::verify::Check("CSceneRenderer::DeleteRenderer", true, result == self && BaseCleared(self) && !self->device_,
                      "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x00500AA0, DeleteRendererVerify, 0, s_unused[6]);

static void __fastcall DestroyRendererVerify(CSceneRenderer* self) {
    self->CSceneRenderer::DestroyRenderer();
    bool ok = BaseCleared(self) && !self->device_ && !self->helper_ && !(self->flags_ & 2);
    ei::verify::Check("CSceneRenderer::DestroyRenderer", true, ok, "%p", static_cast<void*>(self));
}
EI_VERIFY(0x005019F0, DestroyRendererVerify, 0, s_unused[7]);

// --- Colour and sun ------------------------------------------------------------------------------

static void __fastcall SetBaseColourVerify(CSceneRenderer* self, void*, std::uint32_t colour) {
    self->SetBaseColour(colour);
    ei::verify::Check("CSceneRenderer::SetBaseColour", colour, self->colour_, "%08X", colour);
}
EI_VERIFY(0x00500740, SetBaseColourVerify, 0, s_unused[8]);

static void __fastcall SetColourVerify(CSceneRenderer* self, void*, std::uint32_t colour) {
    self->CSceneRenderer::SetColour(colour);
    bool ok = self->colour_ == colour && self->device_->colour_ == colour;
    ei::verify::Check("CSceneRenderer::SetColour", true, ok, "%08X", colour);
}
EI_VERIFY(0x00501CC0, SetColourVerify, 0, s_unused[9]);

static void __fastcall SetSunVerify(CSceneRenderer* self, void*, CLight* light) {
    self->SetSun(light);
    ei::verify::Check("CSceneRenderer::SetSun", static_cast<void*>(light), static_cast<void*>(self->sun_), "%p",
                      static_cast<void*>(light));
}
EI_VERIFY(0x005007C0, SetSunVerify, 0, s_unused[10]);

static void __fastcall SetSunAndAmbientVerify(CSceneRenderer* self, void*, CLight* light, float r, float g, float b) {
    self->SetSunAndAmbient(light, r, g, b);
    std::uint32_t red = static_cast<std::uint32_t>(x87::fistp(r * 255.0f));
    std::uint32_t green = static_cast<std::uint32_t>(x87::fistp(g * 255.0f));
    std::uint32_t blue = static_cast<std::uint32_t>(x87::fistp(b * 255.0f));
    std::uint32_t expected = (((red | 0xFFFFFF00u) << 8 | green) << 8) | blue;
    ei::verify::Check("CSceneRenderer::SetSunAndAmbient", expected, self->colour_, "(%g, %g, %g)", r, g, b);
    ei::verify::Check("CSceneRenderer::SetSunAndAmbient sun", static_cast<void*>(light),
                      static_cast<void*>(self->sun_), "%p", static_cast<void*>(light));
}
EI_VERIFY(0x00500800, SetSunAndAmbientVerify, 0, s_unused[11]);

// --- Device slots --------------------------------------------------------------------------------

static int __fastcall ClearFrameVerify(CSceneRenderer* self) {
    int result = self->CSceneRenderer::ClearFrame();
    ei::verify::Check("CSceneRenderer::ClearFrame", 1, result, "%d", result);
    return result;
}
EI_VERIFY(0x00501A40, ClearFrameVerify, 0, s_unused[12]);

static int __fastcall SetTextureVerify(CSceneRenderer* self, void*, int stage, void* texture) {
    int result = self->CSceneRenderer::SetTexture(stage, texture);
    CDXSurface* expected = texture ? static_cast<CTexture*>(texture)->surface_ : nullptr;
    bool ok = result == 1 && self->device_->stageSurfaces_[stage] == expected;
    ei::verify::Check("CSceneRenderer::SetTexture", true, ok, "stage %d texture %p", stage, texture);
    return result;
}
EI_VERIFY(0x00501AF0, SetTextureVerify, 0, s_unused[13]);

static int __fastcall DrawFigureVerify(CSceneRenderer* self, void*, void* figure, const void* a, const void* b,
                                       void* material, int alpha, tagRECT* screen) {
    int result = self->CSceneRenderer::DrawFigure(figure, a, b, material, alpha, screen);
    ei::verify::Check("CSceneRenderer::DrawFigure", 1, result, "%p", figure);
    return result;
}
EI_VERIFY(0x00501D00, DrawFigureVerify, 0, s_unused[14]);

static int __fastcall DrawFigureLitVerify(CSceneRenderer* self, void*, void* figure, const void* lights, void* material,
                                          int alpha, tagRECT* screen, CTexture* textureA, CTexture* textureB) {
    int result = self->CSceneRenderer::DrawFigureLit(figure, lights, material, alpha, screen, textureA, textureB);
    ei::verify::Check("CSceneRenderer::DrawFigureLit", 1, result, "%p", figure);
    return result;
}
EI_VERIFY(0x00501D40, DrawFigureLitVerify, 0, s_unused[15]);

static void __fastcall DrawLineListVerify(CSceneRenderer* self, void*, void* lines) {
    self->CSceneRenderer::DrawLineList(lines);
    ei::verify::Check("CSceneRenderer::DrawLineList", true, true, "");
}
EI_VERIFY(0x00501CE0, DrawLineListVerify, 0, s_unused[16]);

static bool __fastcall RestoreSurfacesVerify(CSceneRenderer* self) {
    bool hadDevice = self->device_ != nullptr;
    bool result = self->RestoreSurfaces();
    ei::verify::Check("CSceneRenderer::RestoreSurfaces", true, hadDevice || !result, "device %d result %d",
                      hadDevice ? 1 : 0, result ? 1 : 0);
    return result;
}
EI_VERIFY(0x00504780, RestoreSurfacesVerify, 0, s_unused[17]);

// --- Device holder -------------------------------------------------------------------------------

static void __fastcall DeviceHolderReleaseVerify(CTextureDevice** holder) {
    CTextureDevice* device = *holder;
    DeviceHolderRelease(holder);
    ei::verify::Check("DeviceHolderRelease", static_cast<void*>(device), static_cast<void*>(*holder), "%p",
                      static_cast<void*>(device));
}
EI_VERIFY(0x00504E00, DeviceHolderReleaseVerify, 0, s_unused[18]);

static void __fastcall DeviceHolderAssignVerify(CTextureDevice** holder, void*, CTextureDevice* device) {
    int refs = device ? device->refs_ : 0;
    bool same = *holder == device;
    DeviceHolderAssign(holder, nullptr, device);
    bool ok = *holder == device && (!device || same || device->refs_ == refs + 1);
    ei::verify::Check("DeviceHolderAssign", true, ok, "%p", static_cast<void*>(device));
}
EI_VERIFY(0x00504E20, DeviceHolderAssignVerify, 0, s_unused[19]);

// --- Texture holders (ours only: the texture manager loads and creates) ------------------------------
// 00500970: a loaded texture (flags_ bit 3) gives 1. 00500A00 (interface windows, sky, weather): the
// holder has a referenced texture named `name` (compared by content).

static int __fastcall EnsureLoadedVerify(CTexture** holder) {
    bool loaded = *holder && ((*holder)->flags_ & 8);
    int result = TextureHolderEnsureLoaded(holder);
    ei::verify::Check("TextureHolderEnsureLoaded", true, !loaded || result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x00500970, EnsureLoadedVerify, 0, s_unused[20]);

static void __fastcall TextureLoadVerify(CTexture** holder, void*, int kind, const char* name) {
    TextureHolderLoad(holder, nullptr, kind, name);
    CTexture* texture = *holder;
    bool ok = texture && texture->refCount_ > 0 && texture->name_ && std::strcmp(texture->name_, name) == 0;
    ei::verify::Check("TextureHolderLoad", true, ok, "kind %d name %s", kind, name);
}
EI_VERIFY(0x00500A00, TextureLoadVerify, 0, s_unused[21]);

// 00504F60 / 00505140 / 00505260 (ours only): an empty array, this returned (the sprite draw's work
// arrays; 00561536).
using AlignedWords = lib::AlignedArray<std::uint32_t>;
static AlignedWords* __fastcall WorkArrayConstructVerify(AlignedWords* self) {
    AlignedWords* result = self->Construct();
    bool ok = result == self && !self->data && self->size == 0 && self->capacity == 0 && self->growBy == 0;
    ei::verify::Check("AlignedArray::Construct (sprites)", true, ok, "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x00504F60, WorkArrayConstructVerify, 0, s_unused[22]);
EI_VERIFY(0x00505140, WorkArrayConstructVerify, 0, s_unused[23]);
EI_VERIFY(0x00505260, WorkArrayConstructVerify, 0, s_unusedArray);
