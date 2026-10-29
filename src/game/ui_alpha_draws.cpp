// Three interface windows' draws (their draw, called with the pass) that only set alpha
// states (src/render/render_state.h) around the container draw: a window with child windows (alpha
// reference 1 around its region tips and children, when the interface's +0x30 is set), the global map
// (blending on, source alpha / inverse source alpha, alpha test off, alpha reference 1) and the help window
// (alpha reference 1 around the container draw). See docs/notes/renderer.md ("Pass state").
#include "ui.h"
#include "ui_camp.h"

#include "render/render_state.h"

#include <cstdint>

// A container's. The children, each drawn when enabled.
void UiContainer::DrawChild(SceneRenderer* pass) {
    if (!ui->drawing) return;
    render::RenderState& state = render::g_renderState;
    const std::uint8_t savedRef = state.alphaReference;
    state.alphaReference = 1;
    UpdateRegionTips();
    const std::vector<UiWindow*>& currentChildren = children;
    for (int i = 0; i < std::ssize(currentChildren); ++i) {
        UiWindow* child = currentChildren[i];
        if (!child->enabled) continue;
        child->DrawChild(pass);
    }
    state.alphaReference = savedRef;
}

void HelpWindow::Draw(SceneRenderer* pass) {
    render::RenderState& state = render::g_renderState;
    const std::uint8_t savedRef = state.alphaReference;
    state.alphaReference = 1;
    UiContainer::Draw(pass);
    state.alphaReference = savedRef;
}
