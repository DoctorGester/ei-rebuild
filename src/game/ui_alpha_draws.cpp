// Three interface windows' draws (slot 20, thiscall with the pass, RET 4) that only set alpha
// states around the container draw, rewritten so the new renderer's state mirror
// (src/render/render_state.h) gets the values: 00600F70 (a window with child windows: alpha
// reference 1 around its region tips and children, when the interface's +0x30 is set), 0062EFC0
// (blending on, source alpha / inverse source alpha, alpha test off, alpha reference 1) and
// 0065A6D0 (alpha reference 1 around the container draw). See docs/notes/renderer.md ("Pass
// state").
#include "ui.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "originals_ui.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kAlphaBlend = 0x1B, kAlphaTest = 0x0F, kSourceBlend = 0x13, kDestBlend = 0x14, kAlphaRef = 0x18;

// The interface's Direct3D device.
UiD3DDevice* Device(CUiWindow* w) { return w->interface_->renderer_->device_; }

void SetState(CUiWindow* w, std::uint32_t state, std::uint32_t value) {
    Device(w)->SetRenderState(state, value);
    render::state::Render(state, value);
}
std::uint32_t GetState(CUiWindow* w, std::uint32_t state) {
    std::uint32_t value = 0;
    Device(w)->GetRenderState(state, &value);
    return value;
}

} // namespace

// 00600F70 (a container's). The children, each drawn (slot 21) when enabled.
void CUiContainer::DrawChildrenImpl(int pass) {
    if (!interface_->drawing_) return;
    std::uint32_t savedRef = GetState(this, kAlphaRef);
    SetState(this, kAlphaRef, 1);
    UpdateRegionTips(pass);
    const UiPtrArray<CUiWindow>& children = children_;
    for (int i = 0; i < children.size; ++i) {
        CUiWindow* child = children[i];
        if (!child->enabled_) continue;
        child->VirtualDrawChild(pass);
    }
    SetState(this, kAlphaRef, savedRef);
}
EI_REPLACE_METHOD(0x00600F70, CUiContainer::DrawChildrenImpl);

// 0062EFC0. Returns 1.
int CUiContainer::DrawBlendedImpl(int pass) {
    SetState(this, kAlphaBlend, 1);
    SetState(this, kAlphaTest, 0);
    SetState(this, kSourceBlend, 5);
    SetState(this, kDestBlend, 6);
    SetState(this, kAlphaRef, 1);
    DrawContainerImpl(pass);
    return 1;
}
EI_REPLACE_METHOD(0x0062EFC0, CUiContainer::DrawBlendedImpl);

// 0065A6D0. Returns 1.
int CUiContainer::DrawAlphaRefImpl(int pass) {
    std::uint32_t savedRef = GetState(this, kAlphaRef);
    SetState(this, kAlphaRef, 1);
    DrawContainerImpl(pass);
    SetState(this, kAlphaRef, savedRef);
    return 1;
}
EI_REPLACE_METHOD(0x0065A6D0, CUiContainer::DrawAlphaRefImpl);
