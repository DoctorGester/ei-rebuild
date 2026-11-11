// Containers (UiContainer, ui.h): the draw, and the input dispatch, which passes an
// event on to the extra inputs and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "game/app/singletons.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"
#include "game/world/world.h"
#include "render/render_state.h"

// --- Draws ------------------------------------------------------------------------------------

// Every state set goes to render::g_renderState (docs/notes/ui.md, "Container draws").

void UiContainer::DrawSprites(SceneRenderer* pass) {
    if (!g_interface.drawing) return;
    render::g_renderState.stages[0].combiner = kAlphaPassStage;
    DrawBase(pass);
    // By index: a child's draw may change the list.
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i].get();
        if (child->enabled) child->DrawSprites(pass);
    }
}

void DrawScreen(UiContainer* self, SceneRenderer* pass, bool mouseMove) {
    g_clientWorld.Render();
    if (mouseMove) {
        UiEvent move{.kind = UiEventKind::MouseMove, .at = g_cursor.position};
        self->OnEvent(move);
    }
    ClearUiDepth(1.0f);
    self->UiContainer::DrawSprites(pass);
}

// --- Per-frame slots ---------------------------------------------------------------------------

float UiContainer::FrameTime() { return g_sceneRenderer.FrameTime() * 0.001f; }

int UiContainer::FrameTicks() { return static_cast<int>(g_sceneRenderer.FrameTime()); }

void UiContainer::Update() {
    for (int i = 0; i < std::ssize(children); ++i) children[i]->Update();
}

void UiContainer::DrawSurfaces() {
    if (!g_interface.drawing) return;
    UiWindow::DrawSurfaces();
    for (const auto& child : children) {
        if (child->enabled) child->DrawSurfaces();
    }
}

void UiContainer::DrawTips() {
    if (!g_interface.drawing) return;
    UiWindow::DrawTips();
    for (const auto& child : children) {
        if (child->enabled) child->DrawTips();
    }
}

// --- Input dispatch ----------------------------------------------------------------------------

void UiContainer::Deactivate() {
    for (const auto& input : inputs) input->Deactivate();
    for (int i = 0; i < std::ssize(children); ++i) children[i]->Deactivate();
}

void UiContainer::OnEvent(UiEvent& event) {
    event.handled = false;
    if (popup) {
        popup->OnEvent(event);
        event.handled = true;
        return;
    }
    for (const auto& input : inputs) {
        input->OnEvent(event);
        if (event.handled) return;
    }
    // By index: a handler may add children.
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i].get();
        if (!child->enabled || (event.AtMouse() && !child->ContainsPoint(event.at))) continue;
        child->OnEvent(event);
        if (event.handled) return;
    }
}
