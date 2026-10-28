// The battle HUD's unit panel: its draw (CHudUnitPanel). Rebirth patches it for its WideScreen layout, reproduced behind
// the flag (ui_widescreen.h). It drives the Direct3D device itself; those calls are made as they are and mirrored to the
// new renderer as the hero preview's are (ui_hero_preview.cpp). See docs/notes/ui.md, "Battle HUD unit panel draw".
#include "device.h"
#include "ui.h"

#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"   // figureScale
#include "texture.h"
#include "ui_widescreen.h"
#include "unit.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/vecmath.h"
#include "render/render.h"
#include "render/render_state.h"
#include "game_singletons.h"
#include "world.h"

#include <bit>
#include <d3d.h>

#include <cstdint>
#include <cstring>

namespace {

// The constants read at run time.
constexpr float kLayoutUnit = 0.0025f;       // 1 / 400, half the layout's width
constexpr float kDepthUnit = 0.48157462f;        // the layout's half height at depth 1
constexpr float kModelDepth = 9.0f;       // how far in front of the camera the model stands
constexpr float kModelDrop = 0.25f;        // of the view's half height below the centre
constexpr float kTurnAboutZ = 0.05f;
constexpr float kTurnAboutX = 1.15f;
constexpr float kSecondTurnAboutZ = 0.4f;
constexpr float kBarHeight = 15.0f;
constexpr float kPanelHeight = 220.0f;
constexpr int kBackground = 0;        // scene objects
constexpr int kHeightBar = 8;
constexpr int kMoreMark = 9;
constexpr int kFirstDisplay = 10;     // 10..31: the bars, the body parts and the effects' pictures
constexpr int kDisplaysEnd = 32;
constexpr int kInfoPage = 2;          // bodyPart_: the page of texts, without the model
constexpr std::uint32_t kPartHighlight = 0x100;    // a part's flags_: drawn with its highlight colours
constexpr std::uint32_t kPreviewDraw = 0x800;      // the unit's flags_ while the interface draws it
constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr DWORD kClearedRects = 3;

CUiMesh* Sprite(CUiWindow* window, int index) { return window->SceneMesh(index); }
IDirect3DDevice7* D3DDevice(CTextureDevice* device) { return device->d3dDevice_; }

// The quaternion product a * b, inlined twice in the original: w = a.w b.w - a.b (the dot's sum
// (y + x) + z), the vector part (a x b + b.w a) + a.w b; the first product adds z's last two terms the
// other way round.
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
void ClearDepthRects(CTextureDevice* renderer, RECT* rects, float z) {
    DWORD flags = D3DCLEAR_ZBUFFER;
    if (uidevice::HasStencil(renderer)) flags = D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
    gameapi::EnterCriticalSection(&g_deviceLock);
    D3DRECT cleared[kClearedRects];
    for (DWORD i = 0; i < kClearedRects; ++i) cleared[i] = {rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    renderer->lastLoadResult_ = device::Clear(D3DDevice(renderer), kClearedRects, cleared, flags, 0, z, 0);
    gameapi::LeaveCriticalSection(&g_deviceLock);
    render::Rect mirrored[kClearedRects];
    for (DWORD i = 0; i < kClearedRects; ++i)
        mirrored[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, mirrored, static_cast<int>(kClearedRects));
}

} // namespace

BOOL CHudUnitPanel::DrawVanilla(CSceneRenderer* pass) {
    widescreen::EnterLeftPanel();    // addon's 100232F4
    Quaternion savedOrientation = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector3 savedPosition = {};
    Sprite(this, kHeightBar)->visible_ = 0;
    Sprite(this, kMoreMark)->visible_ = 0;
    // The background alone, before anything else.
    Sprite(this, kBackground)->visible_ = 1;
    Sprite(this, kBackground)->Draw(pass);
    Sprite(this, kBackground)->visible_ = 0;

    // The unit shown while the panel is not slid away: the hover unit, else the selected one.
    CUnit* unit = nullptr;
    const char* name = nullptr;
    float size = 0.0f;     // the original leaves both unset without a unit (never read then)
    float height = 0.0f;
    if (slide_ < 1.0f) {
        const int which = hoverUnit_ ? 1 : 0;
        unit = hoverUnit_ ? hoverUnit_ : shownUnit_;
        name = strings_[which].c_str();
        size = viewSize_[which];
        height = viewHeight_[which];
        if (hoverUnit_ == shownUnit_) size = viewSize_[0];
        if (unit) size = static_cast<float>(unit->AnimationRatio() * size);
    }

    for (int i = kFirstDisplay; i < kDisplaysEnd; ++i) Sprite(this, i)->visible_ = 0;
    UiSurfaceGetDC(surfaces_[0]);
    surfaces_[0]->Clear();

    if (unit && unit->Object3D()->attached) {
        CObject3D* model = unit->Object3D();
        const bool drawn = bodyPart_ != kInfoPage;
        if (drawn) {
            // The screen around the panel's model area (right of it, its sling strip, below it):
            // the depth buffer goes to 0 there, so the model is drawn inside the panel only.
            RECT right = {180 - offset_, 0, 400 - offset_, 220};
            RECT strip = {0, 0, 20 - offset_, 220};
            RECT below = {0, 220, 400 - offset_, 300};
            // WideScreen: without the margin.
            interface_->CallScaleRectToScreenNoMargin(&right);
            interface_->CallScaleRectToScreenNoMargin(&strip);
            interface_->CallScaleRectToScreenNoMargin(&below);
            RECT rects[kClearedRects] = {right, below, strip};
            ClearDepthRects(interface_->renderer_, rects, 0.0f);

            savedPosition = model->localPosition_;
            savedOrientation = model->localRotation_;
            unit->AdvanceTime(static_cast<float>(g_clientWorld->tick_) + g_clientWorld->tickFraction_);

            // In front of the interface camera, at the layout's x = 100 less the slide, lowered so
            // that its height (in units of its size) stands a quarter of the view below the centre.
            // WideScreen: 100 - shift.
            const float x = static_cast<float>((static_cast<float>(100 - widescreen::EdgeShift() - offset_) * kLayoutUnit - 1.0f) *
                                         kDepthUnit * kModelDepth);
            const float y = static_cast<float>(height / size - static_cast<float>(kDepthUnit * kModelDepth) * kModelDrop);
            model->localPosition_ = Vector3{x, y, kModelDepth};
            // Turned with the sling buttons' angle: about z, about x, about z again.
            const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
            const Vector3 axisX = {1.0f, 0.0f, 0.0f};
            Quaternion first, second, third;
            Quaternion* q1 = first.SetAxisAngle(axisZ, kHudQuarterTurn * kTurnAboutZ);
            Quaternion* q2 = second.SetAxisAngle(axisX, kHudQuarterTurn * kTurnAboutX);
            const Quaternion tilted = Product(*q1, *q2, false);
            Quaternion* q3 = third.SetAxisAngle(axisZ, kHudQuarterTurn * kSecondTurnAboutZ);
            model->localRotation_ = Product(tilted, *q3, true);

            const float scale = static_cast<float>(1.0f / size);
            unit->Object3D()->ClientSpecific()->figureScale = Vector3{scale, scale, scale};
            // The first page draws the model in full colour, the others at half.
            model->SetPartsFlatColour(bodyPart_ == 0 ? 0xFFFFFFFFu : 0xFF808080u);
            model->SetPartsFlatSpecular(0xFF000000u);
            model->SetPartFlags(kPartHighlight);
            unit->flags_ |= kPreviewDraw;

            // Texture stage 0's mip filter off around the model's draw.
            render::TextureStage& stage0 = render::g_renderState.stages[0];
            const render::MipFilter mipFilter = stage0.mipFilter;
            stage0.mipFilter = render::MipFilter::None;
            unit->DrawFigure(pass);
            stage0.mipFilter = mipFilter;

            unit->flags_ &= ~kPreviewDraw;
            model->ClearPartFlags(kPartHighlight);
            ClearDepthRects(interface_->renderer_, rects, 1.0f);
        }

        RECT nameRect = {5, 5, 155, 20};
        surfaces_[0]->DrawWrappedText(name, &nameRect, 2, 0, kTextColour, DT_CENTER | DT_WORDBREAK, 1);

        switch (bodyPart_) {
        case 0: {
            ShowHealth(unit);
            // The height bar (scene object 8): the model's fifteen layout units, from the panel's
            // bottom.
            const float barScale = static_cast<float>(kBarHeight / size);
            std::uint32_t bits;
            bits = std::bit_cast<std::uint32_t>(barScale);
            Sprite(this, kHeightBar)->SetScaleYBits(bits);
            const float barY = static_cast<float>(kPanelHeight - barScale * kPanelHeight);
            SceneMesh(kHeightBar)->PlaceBody(static_cast<float>(20 - offset_), barY, 4.0f);
            Sprite(this, kHeightBar)->visible_ = 1;
            break;
        }
        case 1:
            ShowParts(unit);
            ShowHealth(unit);
            break;
        case 2:
            DrawUnitInfo(unit);
            break;
        case 3:
            ShowEffects(unit);
            break;
        }

        if (bodyPart_ != kInfoPage) {
            unit->Object3D()->ClientSpecific()->figureScale = Vector3{1.0f, 1.0f, 1.0f};
            model->localPosition_ = savedPosition;
            model->localRotation_ = savedOrientation;
        }
    }

    UiSurfaceReleaseDC(surfaces_[0]);
    widescreen::LeavePanel();        // addon's 10023312, before the base draw
    DrawBase(pass);
    return 1;
}
