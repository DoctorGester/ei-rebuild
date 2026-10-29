// The interface's per-frame draw (CInterface3D::Draw, called by the main loop between the
// scene's BeginScene and EndScene) and the screen fade drawn over it (CScreenFade::Draw).
// See docs/notes/ui.md, section 3, and docs/notes/renderer.md, section 2.
#include "app.h"
#include "ui.h"
#include "world.h"

#include "console.h"
#include "core_maths.h"
#include "model.h"
#include "texture.h"
#include "ui_widescreen.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "render/render_state.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <iterator>

namespace {

// The bars' share of the screen height.
constexpr float kLetterboxShare = 0.217f;

constexpr float kOpaque = 255.0f;

RECT ScreenOf(const CSceneRenderer* renderer) {
    RECT screen;
    gameapi::CopyRect(&screen, &renderer->device_->screen_);
    return screen;
}

// --- The overlays ---

constexpr DWORD kFpsPeriod = 400;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFpsFont = 1;

// A new surface for the screen's picture, as the interface's backdrop. Rebirth's WideScreen makes
// the full-size one the mode's size in pixels, unscaled (addon's operands
// 1 -> 0, 600 -> height, 800 -> width).
void MakeBackdrop(CInterface3D* ui) {
    if (widescreen::Active()) {
        const widescreen::Layout& wide = widescreen::Get();
        ui->backdrop_ = new CUiSurface(ui, 0, 0, wide.width, wide.height, SurfaceBlend::Opaque, false);
    } else {
        ui->backdrop_ = new CUiSurface(ui, 0, 0, 800, 600, SurfaceBlend::Opaque, true);
    }
}

} // namespace

void CScreenFade::Draw(CSceneRenderer* renderer) {
    RECT screen = ScreenOf(renderer);
    if (letterbox_) {
        const int width = screen.right - screen.left;
        const int height = screen.bottom - screen.top;
        const int bar = static_cast<int>(static_cast<double>(height) * kLetterboxShare);
        render::g_renderState.depthTest = false;
        renderer->DrawRectangle(0, 0, width, bar, -1, 0);
        renderer->DrawRectangle(0, height - bar, width, height, -1, 0);
        render::g_renderState.depthTest = true;
    }
    if (!active_) return;
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
        active_ = false;
        return;
    }
    render::g_renderState.depthTest = false;
    screen = ScreenOf(renderer);
    renderer->DrawRectangle(0, 0, screen.right - screen.left, screen.bottom - screen.top, static_cast<int>(alpha_), 0);
    render::g_renderState.depthTest = true;
}

void CScreenFade::Start(int ms) {
    rate_ = kOpaque / static_cast<float>(ms);
    alpha_ = ms > 0 ? 0.0f : kOpaque;
    lastTime_ = gameapi::timeGetTime();
    active_ = true;
}

void CInterface3D::Draw() {
    CloseAllPending();
    if ((backdropMode_ == 2 || backdropMode_ == 4 || backdropMode_ == 3) && backdrop_) backdrop_->Blit();
    if (!stack_.empty()) stack_.back()->Draw(&g_sceneRenderer);
    fade_->Draw(&g_sceneRenderer);
}

void CInterface3D::DrawOverlays() {
    if (!stack_.empty()) {
        stack_.back()->Redraw();       // its surfaces blit themselves
        stack_.back()->RedrawTips();   // its tooltips follow the mouse and redraw
    }
    // (The original calls an empty function on the fade here; nothing to do.)
    if (statusSurface_) statusSurface_->Blit();

    if (backdropMode_ == 1) {
        // A window opens over the screen: the screen greyed behind it.
        if (!backdrop_) MakeBackdrop(this);
        backdrop_->CaptureScreen();
        if (screenCopy_) delete[] screenCopy_;
        screenCopy_ = backdrop_->Rgb565Copy(0x100, 0xC0);
        backdrop_->Greyscale();
        backdropMode_ = 2;
    } else if (backdropMode_ == 3) {
        // A window opens over a window: the screen as it is, the old backdrop put aside.
        if (backdrop_) backdrop_->SwapContents(false);
        secondBackdrop_ = backdrop_;
        MakeBackdrop(this);
        backdrop_->CaptureScreen();
        backdropMode_ = 4;
    }
    if (IsMultiplayerClient() && std::ssize(stack_) == 1) root_->Redraw();

    ++fpsFrames_;
    const DWORD now = gameapi::timeGetTime();
    const DWORD elapsed = now - fpsTime_;
    if (elapsed > kFpsPeriod) {
        if (fpsSurface_) {
            std::string rate;
            float perSecond =
                static_cast<float>(fpsFrames_) * 1000.0f / static_cast<float>(static_cast<int>(elapsed));
            rate = str::Format("%.1f", static_cast<double>(perSecond));
            fpsSurface_->Clear();
            RECT rect{0, 0, 60, 18};
            fpsSurface_->DrawTextIn("FPS:", &rect, kFpsFont, kWhite, 0, 1);
            rect = RECT{0, 0, 60, 18};
            fpsSurface_->DrawTextIn(rate, &rect, kFpsFont, kWhite, 2, 1);
        }
        fpsTime_ = now;
        fpsFrames_ = 0;
    }
    if (fpsSurface_) fpsSurface_->Blit();
    if (console_ && console_->shown_) console_->Blit();
}
