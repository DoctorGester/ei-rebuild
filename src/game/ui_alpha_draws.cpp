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
void Flush(CUiWindow* w) {
    w->interface_->renderer_->FlushBatchA();
    w->interface_->renderer_->FlushBatchB();
}

// Through their original addresses (rewritten or not), as the original calls them.
__declspec(naked) void __fastcall UpdateRegionTips(CUiWindow*, void*, int) EI_ORIGINAL(0x005FEBF0)

} // namespace

// 00600F70 (a container's). The children, each drawn (slot 21) when enabled.
void __fastcall WindowDrawChildren(CUiWindow* self, void*, int pass) {
    if (!self->interface_->drawing_) return;
    Flush(self);
    std::uint32_t savedRef = GetState(self, kAlphaRef);
    SetState(self, kAlphaRef, 1);
    UpdateRegionTips(self, nullptr, pass);
    const UiPtrArray<CUiWindow>& children = static_cast<CUiContainer*>(self)->children_;
    for (int i = 0; i < children.size; ++i) {
        CUiWindow* child = children[i];
        if (!child->enabled_) continue;
        child->VirtualDrawChild(pass);
    }
    Flush(self);
    SetState(self, kAlphaRef, savedRef);
}
EI_REPLACE(0x00600F70, WindowDrawChildren);

// 0062EFC0. Returns 1.
int __fastcall WindowDrawBlended(CUiWindow* self, void*, int pass) {
    SetState(self, kAlphaBlend, 1);
    SetState(self, kAlphaTest, 0);
    SetState(self, kSourceBlend, 5);
    SetState(self, kDestBlend, 6);
    SetState(self, kAlphaRef, 1);
    ContainerDrawOriginal(self, nullptr, pass);
    return 1;
}
EI_REPLACE(0x0062EFC0, WindowDrawBlended);

// 0065A6D0. Returns 1.
int __fastcall WindowDrawAlphaRef(CUiWindow* self, void*, int pass) {
    Flush(self);
    std::uint32_t savedRef = GetState(self, kAlphaRef);
    SetState(self, kAlphaRef, 1);
    ContainerDrawOriginal(self, nullptr, pass);
    Flush(self);
    SetState(self, kAlphaRef, savedRef);
    return 1;
}
EI_REPLACE(0x0065A6D0, WindowDrawAlphaRef);
