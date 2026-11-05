// The help window's sprite draw: alpha reference 1 around the container draw (src/render/render_state.h).
// See docs/notes/renderer.md ("Pass state").
#include "ui.h"
#include "ui_camp.h"

#include "render/render_state.h"

#include <cstdint>

void HelpWindow::DrawSprites(SceneRenderer* pass) {
    render::RenderState& state = render::g_renderState;
    const std::uint8_t savedRef = state.alphaReference;
    state.alphaReference = 1;
    UiContainer::DrawSprites(pass);
    state.alphaReference = savedRef;
}
