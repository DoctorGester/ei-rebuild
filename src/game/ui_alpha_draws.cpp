// Three interface windows' draws (slot 20, thiscall with the pass, RET 4) that only set alpha
// states around the container draw, rewritten so the new renderer's state mirror
// (src/render/render_state.h) gets the values: a window with child windows: alpha
// reference 1 around its region tips and children, when the interface's +0x30 is set),
// (blending on, source alpha / inverse source alpha, alpha test off, alpha reference 1) and
// (alpha reference 1 around the container draw). See docs/notes/renderer.md ("Pass
// state").
#include "ui.h"
#include "ui_camp.h"

#include "render/render_state.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kAlphaBlend = 0x1B, kAlphaTest = 0x0F, kSourceBlend = 0x13, kDestBlend = 0x14, kAlphaRef = 0x18;

// The interface's Direct3D device.
CTextureDevice* Device(CUiWindow* w) { return w->interface_->renderer_; }

void SetState(CUiWindow* w, std::uint32_t state, std::uint32_t value) {
    uidevice::SetRenderState(Device(w), state, value);
    render::state::Render(state, value);
}
std::uint32_t GetState(CUiWindow* w, std::uint32_t state) {
    std::uint32_t value = 0;
    uidevice::GetRenderState(Device(w), state, &value);
    return value;
}

} // namespace

// A container's. The children, each drawn when enabled.
void CUiContainer::DrawChild(CSceneRenderer* pass) {
    if (!interface_->drawing_) return;
    std::uint32_t savedRef = GetState(this, kAlphaRef);
    SetState(this, kAlphaRef, 1);
    UpdateRegionTips(pass);
    const UiPtrArray<CUiWindow>& children = children_;
    for (int i = 0; i < std::ssize(children); ++i) {
        CUiWindow* child = children[i];
        if (!child->enabled_) continue;
        child->DrawChild(pass);
    }
    SetState(this, kAlphaRef, savedRef);
}

BOOL CGlobalMap::Draw(CSceneRenderer* pass) {
    SetState(this, kAlphaBlend, 1);
    SetState(this, kAlphaTest, 0);
    SetState(this, kSourceBlend, 5);
    SetState(this, kDestBlend, 6);
    SetState(this, kAlphaRef, 1);
    CUiContainer::Draw(pass);
    return 1;
}

BOOL CHelpWindow::Draw(CSceneRenderer* pass) {
    std::uint32_t savedRef = GetState(this, kAlphaRef);
    SetState(this, kAlphaRef, 1);
    CUiContainer::Draw(pass);
    SetState(this, kAlphaRef, savedRef);
    return 1;
}
