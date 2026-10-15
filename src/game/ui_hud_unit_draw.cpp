// The battle HUD's unit panel: its draw (006462A0, slot 20 of CHudUnitPanel). A-E2.
// Rebirth patches it for its WideScreen layout, reproduced behind the flag (W3, ui_widescreen.h):
// the panel offset while it draws (until the window's base draw), the model's x moved left by
// EdgeShift, the depth buffer's rectangles scaled without the margin. The callees Rebirth patches
// too (the sprites' Place 005E6FA0, the layout scaling 005F7CA0) are called through their
// addresses. The function drives
// the Direct3D device itself (IDirect3DDevice7::Clear, Get / SetTextureStageState): those calls are
// made as they are, and mirrored to the new renderer as the hero preview's are (ui_hero_preview.cpp).
// See ui.h (CHudUnitPanel) and docs/notes/items_spells.md ("A-H26").
#include "device.h"   // the old device's calls
#include "ui.h"

#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"   // CObject3DClientSpecific::figureScale
#include "texture.h"             // the renderer's device object
#include "ui_widescreen.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/vecmath.h"
#include "originals_render.h"    // QuaternionSetAxisAngleOriginal, the batch flushes
#include "originals_ui.h"        // the sprite draw and Place, the surface's DC
#include "render/render.h"
#include "render/render_state.h"

#include <d3d.h>

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// The page's displays and the animation's ratio are rewritten (ui_hud_panels.cpp,
// ui_hud_unit_info.cpp, unit.cpp); the draw is their only caller, so they are called through their
// addresses and their side-by-side checks keep running.
__declspec(naked) void __fastcall ShowHealthAt(CHudUnitPanel*, void*, CUnit*) EI_ORIGINAL(0x006477E0)
__declspec(naked) void __fastcall ShowPartsAt(CHudUnitPanel*, void*, CUnit*) EI_ORIGINAL(0x00647B70)
__declspec(naked) void __fastcall ShowEffectsAt(CHudUnitPanel*, void*, CUnit*) EI_ORIGINAL(0x00646B90)
__declspec(naked) void __fastcall DrawUnitInfoAt(CHudUnitPanel*, void*, CUnit*) EI_ORIGINAL(0x00647040)
__declspec(naked) double __fastcall AnimationRatioAt(CUnit*) EI_ORIGINAL(0x00532E00)

// The constants the original reads at run time.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
float LayoutUnit() { return GameFloat(0x0074015C); }       // 0.0025: 1 / 400, half the layout's width
float One() { return GameFloat(0x0073B84C); }
float DepthUnit() { return GameFloat(0x007423FC); }        // 0.48157: the layout's half height at depth 1
float ModelDepth() { return GameFloat(0x0074242C); }       // 9: how far in front of the camera the model stands
float ModelDrop() { return GameFloat(0x0073C3B0); }        // 0.25 of the view's half height below the centre
float SlingTurn() { return GameFloat(0x007C8144); }        // the sling buttons' turn (a global angle)
float TurnAboutZ() { return GameFloat(0x0073C52C); }       // 0.05
float TurnAboutX() { return GameFloat(0x00742AE8); }       // 1.15
float SecondTurnAboutZ() { return GameFloat(0x0073B868); } // 0.4
float BarHeight() { return GameFloat(0x00742430); }        // 15
float PanelHeight() { return GameFloat(0x00742AE4); }      // 220
// The world time: whole ticks (007AF61C) plus the fraction of the current one (007AFCA8).
int WorldTick() { return *reinterpret_cast<const int*>(std::uintptr_t(0x007AF61C)); }
float WorldTickFraction() { return GameFloat(0x007AFCA8); }

constexpr int kBackground = 0;        // scene objects
constexpr int kHeightBar = 8;
constexpr int kMoreMark = 9;
constexpr int kFirstDisplay = 10;     // 10..31: the bars, the body parts and the effects' pictures
constexpr int kDisplaysEnd = 32;
constexpr int kInfoPage = 2;          // bodyPart_: the page of texts, without the model
constexpr std::uint32_t kPartHighlight = 0x100;    // a part's flags_: drawn with its +0xD4 / +0xD8 colours
constexpr std::uint32_t kPreviewDraw = 0x800;      // the unit's flags_ while the interface draws it
constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr std::uint32_t kStencilFormat = 0x4000;   // DDPF_STENCILBUFFER
constexpr DWORD kClearedRects = 3;

CUiMesh* Sprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[index]); }
IDirect3DDevice7* D3DDevice(UiRenderer* renderer) { return reinterpret_cast<IDirect3DDevice7*>(renderer->device_); }
IDirect3DDevice7* D3DDevice(CTextureDevice* device) { return static_cast<IDirect3DDevice7*>(device->d3dDevice_); }

// The quaternion product a * b as the original inlines it twice: w = a.w b.w - a.b (the dot's sum
// (y + x) + z), the vector part (a x b + b.w a) + a.w b; the first product adds z's last two terms
// the other way round.
Quaternion Product(const Quaternion& a, const Quaternion& b, bool second) {
    const Vector3 av = {a.x, a.y, a.z};
    const Vector3 bv = {b.x, b.y, b.z};
    const Vector3 cross = Cross(av, bv);
    Quaternion out;
    out.w = static_cast<float>(a.w * b.w - DotXYZ(av, bv));
    out.x = static_cast<float>((cross.x + b.w * a.x) + a.w * b.x);
    out.y = static_cast<float>((cross.y + b.w * a.y) + a.w * b.y);
    out.z = second ? static_cast<float>((cross.z + b.w * a.z) + a.w * b.z) : static_cast<float>((cross.z + a.w * b.z) + b.w * a.z);
    return out;
}

// The renderer's batches flushed, then the depth buffer (and the stencil when the device has one)
// of the three screen rectangles set to `z` under the device lock.
void ClearDepthRects(UiRenderer* renderer, RECT* rects, float z) {
    renderer->FlushBatchA();
    renderer->FlushBatchB();
    DWORD flags = D3DCLEAR_ZBUFFER;
    if ((renderer->caps_ & kStencilFormat) != 0 && renderer->stencil_ != 0) flags = D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
    gameapi::EnterCriticalSection(GameDeviceLock());
    renderer->result_ = device::Clear(D3DDevice(renderer), kClearedRects, reinterpret_cast<D3DRECT*>(rects), flags, 0, z, 0);
    gameapi::LeaveCriticalSection(GameDeviceLock());
    render::Rect mirrored[kClearedRects];
    for (DWORD i = 0; i < kClearedRects; ++i)
        mirrored[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, mirrored, static_cast<int>(kClearedRects));
}

} // namespace

BOOL CHudUnitPanel::DrawVanilla(int pass) {
    widescreen::EnterLeftPanel();    // addon's 100232F4
    Quaternion savedOrientation = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector3 savedPosition = {};
    Sprite(this, kHeightBar)->visible_ = 0;
    Sprite(this, kMoreMark)->visible_ = 0;
    // The background alone, before anything else.
    Sprite(this, kBackground)->visible_ = 1;
    SpriteDrawOriginal(sceneObjects_.data[kBackground], nullptr, pass);
    Sprite(this, kBackground)->visible_ = 0;

    // The unit shown while the panel is not slid away: the hover unit, else the selected one.
    CUnit* unit = nullptr;
    const char* name = nullptr;
    float size = 0.0f;     // the original leaves both unset without a unit (never read then)
    float height = 0.0f;
    if (slide_ < One()) {
        const int which = hoverUnit_ ? 1 : 0;
        unit = hoverUnit_ ? hoverUnit_ : shownUnit_;
        name = strings_[which];
        size = viewSize_[which];
        height = viewHeight_[which];
        if (hoverUnit_ == shownUnit_) size = viewSize_[0];
        if (unit) size = static_cast<float>(AnimationRatioAt(unit) * size);
    }

    for (int i = kFirstDisplay; i < kDisplaysEnd; ++i) Sprite(this, i)->visible_ = 0;
    SurfaceGetDC(surfaces_.data[0]);
    SurfaceClearOriginal(surfaces_.data[0]);

    if (unit && unit->Object3D()->attached) {
        CObject3D* model = unit->Object3D();
        const bool drawn = bodyPart_ != kInfoPage;
        if (drawn) {
            // The screen around the panel's model area (right of it, its sling strip, below it):
            // the depth buffer goes to 0 there, so the model is drawn inside the panel only.
            RECT right = {180 - offset_, 0, 400 - offset_, 220};
            RECT strip = {0, 0, 20 - offset_, 220};
            RECT below = {0, 220, 400 - offset_, 300};
            // WideScreen (0064644C / 0064648E / 00646499: XOR ECX,ECX): without the margin.
            interface_->CallScaleRectToScreenNoMargin(&right);
            interface_->CallScaleRectToScreenNoMargin(&strip);
            interface_->CallScaleRectToScreenNoMargin(&below);
            RECT rects[kClearedRects] = {right, below, strip};
            ClearDepthRects(interface_->renderer_, rects, 0.0f);

            savedPosition = model->position;
            savedOrientation = model->orientation;
            ObjectSlots(unit)->AdvanceTime(static_cast<float>(WorldTick()) + WorldTickFraction());

            // In front of the interface camera, at the layout's x = 100 less the slide, lowered so
            // that its height (in units of its size) stands a quarter of the view below the centre.
            // WideScreen: 100 - shift (00646608).
            const float x = static_cast<float>((static_cast<float>(100 - widescreen::EdgeShift() - offset_) * LayoutUnit() - One()) *
                                         DepthUnit() * ModelDepth());
            const float y = static_cast<float>(height / size - DepthUnit() * ModelDepth() * ModelDrop());
            model->position = Vector3{x, y, ModelDepth()};
            // Turned with the sling buttons' angle: about z, about x, about z again.
            const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
            const Vector3 axisX = {1.0f, 0.0f, 0.0f};
            Quaternion first, second, third;
            Quaternion* q1 = QuaternionSetAxisAngleOriginal(&first, nullptr, &axisZ, SlingTurn() * TurnAboutZ());
            Quaternion* q2 = QuaternionSetAxisAngleOriginal(&second, nullptr, &axisX, SlingTurn() * TurnAboutX());
            const Quaternion tilted = Product(*q1, *q2, false);
            Quaternion* q3 = QuaternionSetAxisAngleOriginal(&third, nullptr, &axisZ, SlingTurn() * SecondTurnAboutZ());
            model->orientation = Product(tilted, *q3, true);

            const float scale = static_cast<float>(One() / size);
            static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached)->figureScale = Vector3{scale, scale, scale};
            // The first page draws the model in full colour, the others at half.
            model->SetPartsFlatColour(bodyPart_ == 0 ? 0xFFFFFFFFu : 0xFF808080u);
            model->SetPartsFlatSpecular(0xFF000000u);
            model->SetPartFlags(kPartHighlight);
            unit->flags_ |= kPreviewDraw;

            // Texture stage 0's mip filter off around the model's draw.
            DWORD mipFilter = 0;
            CTextureDevice* device = RenderDeviceObject();
            device->lastLoadResult_ = device::GetTextureStageState(D3DDevice(device), 0, D3DTSS_MIPFILTER, &mipFilter);
            device->lastLoadResult_ = device::SetTextureStageState(D3DDevice(device), 0, D3DTSS_MIPFILTER, D3DTFP_NONE);
            render::state::Stage(0, D3DTSS_MIPFILTER, D3DTFP_NONE);
            ObjectSlots(unit)->DrawFigure(reinterpret_cast<void*>(static_cast<std::intptr_t>(pass)));
            device->lastLoadResult_ = device::SetTextureStageState(D3DDevice(device), 0, D3DTSS_MIPFILTER, mipFilter);
            render::state::Stage(0, D3DTSS_MIPFILTER, mipFilter);

            unit->flags_ &= ~kPreviewDraw;
            model->ClearPartFlags(kPartHighlight);
            ClearDepthRects(interface_->renderer_, rects, 1.0f);
        }

        RECT nameRect = {5, 5, 155, 20};
        surfaces_.data[0]->DrawWrappedText(name, &nameRect, 2, 0, kTextColour, DT_CENTER | DT_WORDBREAK, 1);

        switch (bodyPart_) {
        case 0: {
            ShowHealthAt(this, nullptr, unit);
            // The height bar (scene object 8): the model's fifteen layout units, from the panel's
            // bottom.
            const float barScale = static_cast<float>(BarHeight() / size);
            std::uint32_t bits;
            std::memcpy(&bits, &barScale, sizeof(bits));
            Sprite(this, kHeightBar)->SetScaleYBits(bits);
            const float barY = static_cast<float>(PanelHeight() - barScale * PanelHeight());
            SpritePlaceOriginal(sceneObjects_.data[kHeightBar], nullptr, static_cast<float>(20 - offset_), barY, 4.0f);
            Sprite(this, kHeightBar)->visible_ = 1;
            break;
        }
        case 1:
            ShowPartsAt(this, nullptr, unit);
            ShowHealthAt(this, nullptr, unit);
            break;
        case 2:
            DrawUnitInfoAt(this, nullptr, unit);
            break;
        case 3:
            ShowEffectsAt(this, nullptr, unit);
            break;
        }

        if (bodyPart_ != kInfoPage) {
            static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached)->figureScale = Vector3{1.0f, 1.0f, 1.0f};
            model->position = savedPosition;
            model->orientation = savedOrientation;
        }
    }

    SurfaceReleaseDC(surfaces_.data[0]);
    widescreen::LeavePanel();        // addon's 10023312, before the base draw
    DrawBase(pass);
    return 1;
}
EI_REPLACE_METHOD(0x006462A0, CHudUnitPanel::DrawVanilla);
EI_REBIRTH_IMPLEMENTS(0x006462A0, ei::rebirth::Patch::WideScreen);
