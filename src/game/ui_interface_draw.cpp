// The interface's per-frame draw (Interface3D::Draw, called by the main loop between the
// scene's BeginScene and EndScene) and the screen fade drawn over it (ScreenFade::Draw).
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

RECT ScreenOf(const SceneRenderer* renderer) {
    RECT screen;
    gameapi::CopyRect(&screen, &renderer->device->screen);
    return screen;
}

// --- The overlays ---

constexpr DWORD kFpsPeriod = 400;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFpsFont = 1;

// A new surface for the screen's picture, as the interface's backdrop. Rebirth's WideScreen makes
// the full-size one the mode's size in pixels, unscaled (addon's operands
// 1 -> 0, 600 -> height, 800 -> width).
void MakeBackdrop(Interface3D* ui) {
    if (widescreen::Active()) {
        const widescreen::Layout& wide = widescreen::Get();
        ui->backdrop = new UiSurface(ui, 0, 0, wide.width, wide.height, SurfaceBlend::Opaque, false);
    } else {
        ui->backdrop = new UiSurface(ui, 0, 0, 800, 600, SurfaceBlend::Opaque, true);
    }
}

} // namespace

void ScreenFade::Draw(SceneRenderer* renderer) {
    RECT screen = ScreenOf(renderer);
    if (letterbox) {
        const int width = screen.right - screen.left;
        const int height = screen.bottom - screen.top;
        const int bar = static_cast<int>(static_cast<double>(height) * kLetterboxShare);
        render::g_renderState.depthTest = false;
        renderer->DrawRectangle(0, 0, width, bar, -1, 0);
        renderer->DrawRectangle(0, height - bar, width, height, -1, 0);
        render::g_renderState.depthTest = true;
    }
    if (!active) return;
    // A step towards 255 (rate > 0) or 0 (rate < 0), by the milliseconds since the last one.
    EI_NAN_AUDIT(alpha, rate);
    if ((alpha < kOpaque && rate > 0.0f) || (alpha > 0.0f && rate < 0.0f)) {
        const DWORD now = gameapi::timeGetTime();
        alpha = static_cast<float>(x87::FromUnsigned(now - lastTime) * rate + alpha);
        lastTime = gameapi::timeGetTime();
    }
    EI_NAN_AUDIT(alpha);
    if (alpha > kOpaque) {
        alpha = kOpaque;
    } else if (alpha < 0.0f) {
        active = false;
        return;
    }
    render::g_renderState.depthTest = false;
    screen = ScreenOf(renderer);
    renderer->DrawRectangle(0, 0, screen.right - screen.left, screen.bottom - screen.top, static_cast<int>(alpha), 0);
    render::g_renderState.depthTest = true;
}

void ScreenFade::Start(int ms) {
    rate = kOpaque / static_cast<float>(ms);
    alpha = ms > 0 ? 0.0f : kOpaque;
    lastTime = gameapi::timeGetTime();
    active = true;
}

void Interface3D::Draw() {
    CloseAllPending();
    if ((backdropMode == 2 || backdropMode == 4 || backdropMode == 3) && backdrop) backdrop->Blit();
    if (!stack.empty()) stack.back()->Draw(&g_sceneRenderer);
    fade->Draw(&g_sceneRenderer);
}

void Interface3D::DrawOverlays() {
    if (!stack.empty()) {
        stack.back()->Redraw();        // its surfaces blit themselves
        stack.back()->RedrawTips();    // its tooltips follow the mouse and redraw
    }
    // (The original calls an empty function on the fade here; nothing to do.)
    if (statusSurface) statusSurface->Blit();

    if (backdropMode == 1) {
        // A window opens over the screen: the screen greyed behind it.
        if (!backdrop) MakeBackdrop(this);
        backdrop->CaptureScreen();
        screenCopy = backdrop->Rgb565Copy(0x100, 0xC0);
        backdrop->Greyscale();
        backdropMode = 2;
    } else if (backdropMode == 3) {
        // A window opens over a window: the screen as it is, the old backdrop put aside.
        if (backdrop) backdrop->SwapContents(false);
        secondBackdrop = backdrop;
        MakeBackdrop(this);
        backdrop->CaptureScreen();
        backdropMode = 4;
    }
    if (IsMultiplayerClient() && std::ssize(stack) == 1) root->Redraw();

    ++fpsFrames;
    const DWORD now = gameapi::timeGetTime();
    const DWORD elapsed = now - fpsTime;
    if (elapsed > kFpsPeriod) {
        if (fpsSurface) {
            std::string rate;
            float perSecond =
                static_cast<float>(fpsFrames) * 1000.0f / static_cast<float>(static_cast<int>(elapsed));
            rate = str::Format("%.1f", static_cast<double>(perSecond));
            fpsSurface->Clear();
            RECT rect{0, 0, 60, 18};
            fpsSurface->DrawTextIn("FPS:", &rect, kFpsFont, kWhite, 0, 1);
            rect = RECT{0, 0, 60, 18};
            fpsSurface->DrawTextIn(rate, &rect, kFpsFont, kWhite, 2, 1);
        }
        fpsTime = now;
        fpsFrames = 0;
    }
    if (fpsSurface) fpsSurface->Blit();
    if (console && console->shown) console->Blit();
}
