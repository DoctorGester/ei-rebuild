// The interface's per-frame draw (CInterface3D::Draw 005F5220, called by the main loop between the
// scene's BeginScene and EndScene) and the screen fade drawn over it (CScreenFade::Draw 005E69C0).
// See docs/notes/ui.md, section 3, and docs/notes/renderer.md, section 2.
#include "ui.h"

#include "core_maths.h"
#include "model.h"
#include "texture.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "originals_render.h"
#include "render/render_state.h"

#include <cstdint>

namespace {

// The bars' share of the screen height (00740150; read from the exe's memory).
float LetterboxShare() { return *reinterpret_cast<const float*>(0x00740150); }

constexpr float kOpaque = 255.0f;     // 0073C370
constexpr std::uint32_t kZEnable = 7;  // D3DRENDERSTATE_ZENABLE

// IDirect3DDevice7::SetRenderState (slot 20; stdcall, the interface first), recorded for the
// frame capture as the other ported draws do.
void SetRenderState(void* device, std::uint32_t state, std::uint32_t value) {
    using Fn = long(__stdcall*)(void*, std::uint32_t, std::uint32_t);
    reinterpret_cast<Fn>((*static_cast<void***>(device))[20])(device, state, value);
    render::state::Render(state, value);
}

RECT ScreenOf(const CSceneRenderer* renderer) {
    RECT screen;
    gameapi::CopyRect(&screen, &renderer->device_->screen_);
    return screen;
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
    if ((!(alpha_ >= kOpaque) && rate_ > 0.0f) || (alpha_ > 0.0f && !(rate_ >= 0.0f))) {
        const DWORD now = gameapi::timeGetTime();
        alpha_ = StoreFloat(static_cast<float>(x87::FromUnsigned(now - lastTime_) * rate_ + alpha_));
        lastTime_ = gameapi::timeGetTime();
    }
    if (alpha_ > kOpaque) {
        alpha_ = kOpaque;
    } else if (!(alpha_ >= 0.0f)) {
        active_ = 0;
        return 1;
    }
    SetRenderState(renderer->device_->d3dDevice_, kZEnable, 0);
    screen = ScreenOf(renderer);
    renderer->DrawRectangle(0, 0, screen.right - screen.left, screen.bottom - screen.top, static_cast<int>(alpha_), 0);
    SetRenderState(renderer->device_->d3dDevice_, kZEnable, 1);
    return 1;
}
EI_REPLACE_METHOD(0x005E69C0, CScreenFade::Draw);

void CScreenFade::Start(int ms) {
    rate_ = kOpaque / static_cast<float>(ms);
    alpha_ = ms > 0 ? 0.0f : kOpaque;
    lastTime_ = gameapi::timeGetTime();
    active_ = 1;
}
EI_REPLACE_METHOD(0x005E6B90, CScreenFade::Start);

int CInterface3D::Draw() {
    CloseAllPending();
    if ((backdropMode_ == 2 || backdropMode_ == 4 || backdropMode_ == 3) && backdrop_) backdrop_->Blit();
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualDraw(DisplayObject());
    fade_->Draw(SceneRenderer());
    return 1;
}
EI_REPLACE_METHOD(0x005F5220, CInterface3D::Draw);
