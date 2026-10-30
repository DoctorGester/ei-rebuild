// Containers (UiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "game_singletons.h"
#include "model.h"
#include "ui.h"

// --- Construction and destruction --------------------------------------------------------------

UiContainer::~UiContainer() {
    for (UiInputSide* side : inputs) delete side;
    for (int i = 0; i < std::ssize(children); ++i) delete children[i];
    children.clear();
}

extern "C" int _purecall();

// Pure: a screen that does not override CreateScreen never gets on the screen.
void UiContainer::CreateScreen() { _purecall(); }
void UiContainer::CloseScreen() { _purecall(); }

void UiContainer::AddInput(UiInputSide* input) {
    if (input) inputs.push_back(input);
}

// --- Per-frame slots ---------------------------------------------------------------------------

float UiContainer::FrameTime() { return g_sceneRenderer.FrameTime() * 0.001f; }

int UiContainer::FrameTicks() { return static_cast<int>(g_sceneRenderer.FrameTime()); }

void UiContainer::Update() {
    for (int i = 0; i < std::ssize(children); ++i) children[i]->Update();
}

void UiContainer::Redraw() {
    if (!ui->drawing) return;
    UiWindow::Redraw();
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i];
        if (child->enabled) child->Redraw();
    }
}

void UiContainer::RedrawTips() {
    if (!ui->drawing) return;
    UiWindow::RedrawTips();
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i];
        if (child->enabled) child->RedrawTips();
    }
}

void UiContainer::Show(bool show) {
    UiWindow::Show(show);
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i];
        if (child->enabled) child->Show(show);
    }
}

// --- Input dispatch ----------------------------------------------------------------------------

void UiContainer::Dispatch(int slot, int first, int second, int third, bool atPoint) {
    UiContainer* self = this;
    handled = false;
    for (auto& side : self->inputs) {
        side->handled = false;
        side->Event(slot, first, second, third);
        if (side->handled) {
            handled = true;
            return;
        }
    }
    for (int i = 0; i < std::ssize(self->children); ++i) {
        UiWindow* child = self->children[i];
        if (!child->enabled) continue;
        if (atPoint && !child->ContainsPoint(second, third)) continue;
        child = self->children[i];
        child->handled = false;
        child->Input()->Event(slot, first, second, third);
        if (self->children[i]->handled) {
            handled = true;
            return;
        }
    }
}

void UiContainer::Deactivate() {
    UiContainer* self = this;
    for (auto& side : self->inputs) side->Deactivate();
    for (int i = 0; i < std::ssize(self->children); ++i) self->children[i]->Input()->Deactivate();
}

void UiContainer::LeftDown(int buttons, int x, int y) {
    Dispatch(kEventLeftDown, buttons, x, y, true);
}

void UiContainer::LeftUp(int buttons, int x, int y) {
    Dispatch(kEventLeftUp, buttons, x, y, true);
}

void UiContainer::MouseMove(int buttons, int x, int y) {
    Dispatch(kEventMouseMove, buttons, x, y, true);
}

void UiContainer::RightDown(int buttons, int x, int y) {
    Dispatch(kEventRightDown, buttons, x, y, true);
}

void UiContainer::RightUp(int buttons, int x, int y) {
    Dispatch(kEventRightUp, buttons, x, y, true);
}

void UiContainer::LeftDouble(int buttons, int x, int y) {
    Dispatch(kEventLeftDouble, buttons, x, y, true);
}

void UiContainer::RightDouble(int buttons, int x, int y) {
    Dispatch(kEventRightDouble, buttons, x, y, true);
}

// Key events reach every enabled child, wherever the mouse is.
void UiContainer::KeyDown(int key, int repeat, int modifiers) {
    Dispatch(kEventKeyDown, key, repeat, modifiers, false);
}

void UiContainer::Char(int key, int repeat, int modifiers) {
    Dispatch(kEventChar, key, repeat, modifiers, false);
}

void UiContainer::KeyUp(int key, int repeat, int modifiers) {
    Dispatch(kEventKeyUp, key, repeat, modifiers, false);
}

// The wheel's four arguments go on unchanged; the children are tested at (x, y).
void UiContainer::Wheel(int modifiers, int delta, int x, int y) {
    UiContainer* self = this;
    handled = false;
    for (auto& side : self->inputs) {
        side->handled = false;
        side->Wheel(modifiers, delta, x, y);
        if (side->handled) {
            handled = true;
            return;
        }
    }
    for (int i = 0; i < std::ssize(self->children); ++i) {
        UiWindow* child = self->children[i];
        if (!child->enabled || !child->ContainsPoint(x, y)) continue;
        child = self->children[i];
        child->handled = false;
        child->Input()->Wheel(modifiers, delta, x, y);
        if (self->children[i]->handled) {
            handled = true;
            return;
        }
    }
}
