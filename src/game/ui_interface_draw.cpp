// The interface's per-frame draw (CInterface3D::Draw, called by the main loop between the
// scene's BeginScene and EndScene) and the screen fade drawn over it (CScreenFade::Draw).
// See docs/notes/ui.md, section 3, and docs/notes/renderer.md, section 2.
#include "app.h"
#include "ui.h"
#include "world.h"
#include "game_strings.h"

#include "console.h"
#include "core_maths.h"
#include "model.h"
#include "texture.h"
#include "ui_native.h"
#include "ui_widescreen.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render_state.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

namespace {

// The bars' share of the screen height.
float LetterboxShare() { return 0.217f /*00740150*/; }

constexpr float kOpaque = 255.0f;
constexpr std::uint32_t kZEnable = 7;  // D3DRENDERSTATE_ZENABLE

// IDirect3DDevice7::SetRenderState, recorded for the frame capture.
void SetRenderState(CTextureDevice* device, std::uint32_t state, std::uint32_t value) {
    uidevice::SetRenderState(device, state, value);
    render::state::Render(state, value);
}

RECT ScreenOf(const CSceneRenderer* renderer) {
    RECT screen;
    gameapi::CopyRect(&screen, &renderer->device_->screen_);
    return screen;
}

// --- The overlays ---

// The surfaces' memory totals (CUiSurface::SwapContents keeps them).
int SurfaceBytesKept() { return static_cast<int>(UiSurfaceKeptBytes()); }
int SurfaceBytesAllocated() { return static_cast<int>(UiSurfaceBytes()); }

constexpr DWORD kFpsPeriod = 400;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFpsFont = 1;

// A new surface for the screen's picture, as the interface's backdrop. Rebirth's WideScreen makes
// the full-size one the mode's size in pixels, unscaled (addon's operands
// 1 -> 0, 600 -> height, 800 -> width).
void MakeBackdrop(CInterface3D* ui) {
    if (app::g_littleOtherMemory != 0) {
        ui->backdrop_ = new CUiSurface(ui, 0, 0, 400, 300, 6, 1);
        ui->backdrop_->SetRect(0, 0, 800, 600);
    } else if (widescreen::Active()) {
        const widescreen::Layout& wide = widescreen::Get();
        ui->backdrop_ = new CUiSurface(ui, 0, 0, wide.width, wide.height, 4, 0);
    } else {
        ui->backdrop_ = new CUiSurface(ui, 0, 0, 800, 600, 4, 1);
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
        SetRenderState(renderer->device_, kZEnable, 0);
        renderer->DrawRectangle(0, 0, width, bar, -1, 0);
        renderer->DrawRectangle(0, height - bar, width, height, -1, 0);
        SetRenderState(renderer->device_, kZEnable, 1);
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
    SetRenderState(renderer->device_, kZEnable, 0);
    screen = ScreenOf(renderer);
    renderer->DrawRectangle(0, 0, screen.right - screen.left, screen.bottom - screen.top, static_cast<int>(alpha_), 0);
    SetRenderState(renderer->device_, kZEnable, 1);
    return 1;
}

void CScreenFade::Start(int ms) {
    rate_ = kOpaque / static_cast<float>(ms);
    alpha_ = ms > 0 ? 0.0f : kOpaque;
    lastTime_ = gameapi::timeGetTime();
    active_ = 1;
}

int CInterface3D::Draw() {
    CloseAllPending();
    if ((backdropMode_ == 2 || backdropMode_ == 4 || backdropMode_ == 3) && backdrop_) backdrop_->Blit();
    if (stack_.Start() != stack_.Finish()) stack_.Finish()[-1]->Draw(SceneRenderer());
    fade_->Draw(SceneRenderer());
    return 1;
}

void CInterface3D::DrawOverlays() {
    std::string text;
    strmap::CDebugPanel* panel = DebugInfoPanel();
    text = str::Format("%d", SurfaceBytesKept());
    panel->Set("surface stored", text.c_str());
    text = str::Format("%d", SurfaceBytesAllocated());
    panel->Set("surface alloc", text.c_str());
    if (stack_.Start() != stack_.Finish()) {
        stack_.Finish()[-1]->Redraw();       // its surfaces blit themselves
        stack_.Finish()[-1]->RedrawTips();   // its tooltips follow the mouse and redraw
    }
    // (The original calls an empty function on the fade here; nothing to do.)
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
    if (IsMultiplayerClient() && stack_.Finish() - stack_.Start() == 1) root_->Redraw();
    panel->Tick();

    ++fpsFrames_;
    const DWORD now = gameapi::timeGetTime();
    const DWORD elapsed = now - fpsTime_;
    if (elapsed > kFpsPeriod) {
        if (fpsSurface_) {
            std::string rate;
            float perSecond =
                static_cast<float>(fpsFrames_) * 1000.0f /*0073C34C*/ / static_cast<float>(static_cast<int>(elapsed));
            rate = str::Format("%.1f", static_cast<double>(perSecond));
            UiSurfaceGetDC(fpsSurface_);
            fpsSurface_->Clear();
            RECT rect{0, 0, 60, 18};
            fpsSurface_->DrawTextIn("FPS:", &rect, kFpsFont, kWhite, 0, 1);
            rect = RECT{0, 0, 60, 18};
            fpsSurface_->DrawTextIn(rate, &rect, kFpsFont, kWhite, 2, 1);
            UiSurfaceReleaseDC(fpsSurface_);
        }
        fpsTime_ = now;
        fpsFrames_ = 0;
    }
    if (fpsSurface_) fpsSurface_->Blit();
    if (console_ && console_->shown_) console_->Blit();
}
