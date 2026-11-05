// Containers (UiContainer, ui.h): the input dispatch, which passes an
// event on to the extra inputs and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "game_singletons.h"
#include "model.h"
#include "ui.h"

// --- Per-frame slots ---------------------------------------------------------------------------

float UiContainer::FrameTime() { return g_sceneRenderer.FrameTime() * 0.001f; }

int UiContainer::FrameTicks() { return static_cast<int>(g_sceneRenderer.FrameTime()); }

void UiContainer::Update() {
    for (int i = 0; i < std::ssize(children); ++i) children[i]->Update();
}

void UiContainer::DrawSurfaces() {
    if (!ui->drawing) return;
    UiWindow::DrawSurfaces();
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i].get();
        if (child->enabled) child->DrawSurfaces();
    }
}

void UiContainer::DrawTips() {
    if (!ui->drawing) return;
    UiWindow::DrawTips();
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i].get();
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
