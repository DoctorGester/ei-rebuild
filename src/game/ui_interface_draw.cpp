// The interface's per-frame draw (CInterface3D::Draw 005F5220, called by the main loop between the
// scene's BeginScene and EndScene) and the screen fade drawn over it (CScreenFade::Draw 005E69C0).
// See docs/notes/ui.md, section 3, and docs/notes/renderer.md, section 2.
#include "ui.h"
#include "world.h"
#include "game_strings.h"

#include "console.h"
#include "core_maths.h"
#include "model.h"
#include "texture.h"
#include "ui_native.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "render/render_state.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstdint>

namespace {

// The bars' share of the screen height (00740150; read from the exe's memory).
float LetterboxShare() { return 0.217f /*00740150*/; }

constexpr float kOpaque = 255.0f;     // 0073C370
constexpr std::uint32_t kZEnable = 7;  // D3DRENDERSTATE_ZENABLE

// IDirect3DDevice7::SetRenderState, recorded for the frame capture as the other ported draws do.
void SetRenderState(void* device, std::uint32_t state, std::uint32_t value) {
    static_cast<UiD3DDevice*>(device)->SetRenderState(state, value);
    render::state::Render(state, value);
}

RECT ScreenOf(const CSceneRenderer* renderer) {
    RECT screen;
    gameapi::CopyRect(&screen, &renderer->device_->screen_);
    return screen;
}

// --- The overlays (005F5320) ---

// The surfaces' memory totals (CUiSurface::SwapContents keeps them).
int SurfaceBytesKept() { return ei::GameData<const volatile int>(0x007C7E74); }
int SurfaceBytesAllocated() { return ei::GameData<const volatile int>(0x007C7E78); }
// The option byte 00784753: backdrops of a quarter of the layout, stretched over it.
bool SmallBackdrops() { return ei::GameData<const volatile std::uint8_t>(0x00784753) != 0; }

constexpr int kRedrawSlot = 22;   // a window's surfaces blit themselves
constexpr int kTipsSlot = 23;     // its tooltips follow the mouse and redraw
constexpr DWORD kFpsPeriod = 400;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFpsFont = 1;

void CallSlot(void* window, int slot) {
    using Fn = void(__thiscall*)(void*);
    ei::ObjectSlot<Fn>(window, slot)(window);
}

// A new surface for the screen's picture, as the interface's backdrop. Rebirth's WideScreen makes
// the full-size one the mode's size in pixels, unscaled (addon's operands at 005F546A..005F5473
// and 005F5568..005F5571: 1 -> 0, 600 -> height, 800 -> width).
void MakeBackdrop(CInterface3D* ui) {
    auto* surface = gamecrt::operator_new_as<CUiSurface>();
    if (SmallBackdrops()) {
        ui->backdrop_ = surface ? surface->Construct(ui, 0, 0, 400, 300, 6, 1) : nullptr;
        ui->backdrop_->SetRect(0, 0, 800, 600);
    } else if (widescreen::Active()) {
        const widescreen::Layout& wide = widescreen::Get();
        ui->backdrop_ = surface ? surface->Construct(ui, 0, 0, wide.width, wide.height, 4, 0) : nullptr;
    } else {
        ui->backdrop_ = surface ? surface->Construct(ui, 0, 0, 800, 600, 4, 1) : nullptr;
    }
    ui_native::Adopt(ui->backdrop_, "backdrop");
}

} // namespace

int CScreenFade::Draw(CSceneRenderer* renderer) {
    RECT screen = ScreenOf(renderer);
    if (letterbox_) {
        const int width = screen.right - screen.left;
        const int height = screen.bottom - screen.top;
        const int bar = static_cast<int>(static_cast<double>(height) * LetterboxShare());
        SetRenderState(renderer->device_->d3dDevice_, kZEnable, 0);
        renderer->DrawRectangle(0, 0, width, bar, -1, 0);
        renderer->DrawRectangle(0, height - bar, width, height, -1, 0);
        SetRenderState(renderer->device_->d3dDevice_, kZEnable, 1);
    }
    if (!active_) return 1;
    // A step towards 255 (rate > 0) or 0 (rate < 0), by the milliseconds since the last one.
    EI_NAN_AUDIT(alpha_, rate_);
    if ((alpha_ < kOpaque && rate_ > 0.0f) || (alpha_ > 0.0f && rate_ < 0.0f)) {
        const DWORD now = gameapi::timeGetTime();
        alpha_ = static_cast<float>(x87::FromUnsigned(now - lastTime_) * rate_ + alpha_);
        lastTime_ = gameapi::timeGetTime();
    }
    EI_NAN_AUDIT(alpha_);
    if (alpha_ > kOpaque) {
        alpha_ = kOpaque;
    } else if (alpha_ < 0.0f) {
        active_ = 0;
        return 1;
    }
    SetRenderState(renderer->device_->d3dDevice_, kZEnable, 0);
    screen = ScreenOf(renderer);
    renderer->DrawRectangle(0, 0, screen.right - screen.left, screen.bottom - screen.top, static_cast<int>(alpha_), 0);
    SetRenderState(renderer->device_->d3dDevice_, kZEnable, 1);
    return 1;
}
EI_REWRITTEN(0x005E69C0, CScreenFade::Draw);

void CScreenFade::Start(int ms) {
    rate_ = kOpaque / static_cast<float>(ms);
    alpha_ = ms > 0 ? 0.0f : kOpaque;
    lastTime_ = gameapi::timeGetTime();
    active_ = 1;
}
EI_REWRITTEN(0x005E6B90, CScreenFade::Start);

int CInterface3D::Draw() {
    CloseAllPending();
    if ((backdropMode_ == 2 || backdropMode_ == 4 || backdropMode_ == 3) && backdrop_) backdrop_->Blit();
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualDraw(SceneRenderer());
    fade_->Draw(SceneRenderer());
    return 1;
}
EI_REWRITTEN(0x005F5220, CInterface3D::Draw);

void CInterface3D::DrawOverlays() {
    mfc::CString text;
    strmap::CDebugPanel* panel = DebugInfoPanel();
    text.Format("%d", SurfaceBytesKept());
    panel->Set("surface stored", text);
    text.Format("%d", SurfaceBytesAllocated());
    panel->Set("surface alloc", text);
    if (stackBegin_ != stackEnd_) {
        CallSlot(stackEnd_[-1], kRedrawSlot);
        CallSlot(stackEnd_[-1], kTipsSlot);
    }
    // (The original calls 005E6B80 on the fade here: an empty function.)
    if (statusSurface_) statusSurface_->Blit();

    if (backdropMode_ == 1) {
        // A window opens over the screen: the screen greyed behind it.
        if (!backdrop_) MakeBackdrop(this);
        backdrop_->CaptureScreen();
        if (block5C_) gamecrt::operator_delete(block5C_);
        block5C_ = backdrop_->Rgb565Copy(0x100, 0xC0);
        backdrop_->Greyscale();
        backdropMode_ = 2;
    } else if (backdropMode_ == 3) {
        // A window opens over a window: the screen as it is, the old backdrop put aside.
        if (backdrop_) backdrop_->CallSwapContents(0);
        secondBackdrop_ = backdrop_;
        MakeBackdrop(this);
        backdrop_->CaptureScreen();
        backdropMode_ = 4;
    }
    if (IsMultiplayerClient() && stackEnd_ - stackBegin_ == 1) CallSlot(root_, kRedrawSlot);
    panel->Tick();

    ++fpsFrames_;
    const DWORD now = gameapi::timeGetTime();
    const DWORD elapsed = now - fpsTime_;
    if (elapsed > kFpsPeriod) {
        if (fpsSurface_) {
            mfc::CString rate;
            float perSecond =
                static_cast<float>(fpsFrames_) * 1000.0f /*0073C34C*/ / static_cast<float>(static_cast<int>(elapsed));
            rate.Format("%.1f", static_cast<double>(perSecond));
            UiSurfaceGetDC(fpsSurface_);
            fpsSurface_->Clear();
            RECT rect{0, 0, 60, 18};
            SurfaceDrawText(fpsSurface_, nullptr, "FPS:", &rect, kFpsFont, kWhite, 0, 1);
            rect = RECT{0, 0, 60, 18};
            SurfaceDrawText(fpsSurface_, nullptr, rate, &rect, kFpsFont, kWhite, 2, 1);
            UiSurfaceReleaseDC(fpsSurface_);
        }
        fpsTime_ = now;
        fpsFrames_ = 0;
    }
    if (fpsSurface_) fpsSurface_->Blit();
    if (console_ && console_->shown_) console_->Blit();
}
EI_REPLACE_METHOD(0x005F5320, CInterface3D::DrawOverlays);
