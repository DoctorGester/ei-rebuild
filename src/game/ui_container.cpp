// Containers (CUiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "game_singletons.h"
#include "model.h"
#include "ui.h"

#include "ei/x87.h"

// --- Construction and destruction --------------------------------------------------------------

CUiContainer::~CUiContainer() {
    for (UiInputSide* side : inputs) delete side;
    for (int i = 0; i < std::ssize(children); ++i) delete children[i];
    children.clear();
}

extern "C" int _purecall();

// Pure: a screen that does not override CreateScreen never gets on the screen.
void CUiContainer::CreateScreen() { _purecall(); }
void CUiContainer::CloseScreen() { _purecall(); }

void CUiContainer::AddInput(UiInputSide* input) {
    if (input) inputs.push_back(input);
}

// --- Per-frame slots ---------------------------------------------------------------------------

// The scene renderer's last frame's milliseconds are loaded as an unsigned qword and multiplied by the float 0.001.
double CUiContainer::FrameTime() { return x87::FromUnsigned(g_sceneRenderer.FrameTime()) * static_cast<double>(0.001f); }

void CUiContainer::Update() {
    for (int i = 0; i < std::ssize(children); ++i) children[i]->Update();
}

void CUiContainer::Redraw() {
    if (!ui->drawing) return;
    CUiWindow::Redraw();
    for (int i = 0; i < std::ssize(children); ++i) {
        CUiWindow* child = children[i];
        if (child->enabled) child->Redraw();
    }
}

void CUiContainer::RedrawTips() {
    if (!ui->drawing) return;
    CUiWindow::RedrawTips();
    for (int i = 0; i < std::ssize(children); ++i) {
        CUiWindow* child = children[i];
        if (child->enabled) child->RedrawTips();
    }
}

void CUiContainer::Show(bool show) {
    CUiWindow::Show(show);
    for (int i = 0; i < std::ssize(children); ++i) {
        CUiWindow* child = children[i];
        if (child->enabled) child->Show(show);
    }
}

// --- Input dispatch ----------------------------------------------------------------------------

void CUiContainer::Dispatch(int slot, int a, int b, int c, bool atPoint) {
    CUiContainer* self = this;
    handled = false;
    for (auto& side : self->inputs) {
        side->handled = false;
        side->Event(slot, a, b, c);
        if (side->handled) {
            handled = true;
            return;
        }
    }
    for (int i = 0; i < std::ssize(self->children); ++i) {
        CUiWindow* child = self->children[i];
        if (!child->enabled) continue;
        if (atPoint && !child->ContainsPoint(b, c)) continue;
        child = self->children[i];
        child->handled = false;
        child->Input()->Event(slot, a, b, c);
        if (self->children[i]->handled) {
            handled = true;
            return;
        }
    }
}

void CUiContainer::Deactivate() {
    CUiContainer* self = this;
    for (auto& side : self->inputs) side->Deactivate();
    for (int i = 0; i < std::ssize(self->children); ++i) self->children[i]->Input()->Deactivate();
}

void CUiContainer::LeftDown(int buttons, int x, int y) {
    Dispatch(kEventLeftDown, buttons, x, y, true);
}

void CUiContainer::LeftUp(int buttons, int x, int y) {
    Dispatch(kEventLeftUp, buttons, x, y, true);
}

void CUiContainer::MouseMove(int buttons, int x, int y) {
    Dispatch(kEventMouseMove, buttons, x, y, true);
}

void CUiContainer::RightDown(int buttons, int x, int y) {
    Dispatch(kEventRightDown, buttons, x, y, true);
}

void CUiContainer::RightUp(int buttons, int x, int y) {
    Dispatch(kEventRightUp, buttons, x, y, true);
}

void CUiContainer::LeftDouble(int buttons, int x, int y) {
    Dispatch(kEventLeftDouble, buttons, x, y, true);
}

void CUiContainer::RightDouble(int buttons, int x, int y) {
    Dispatch(kEventRightDouble, buttons, x, y, true);
}

// Key events reach every enabled child, wherever the mouse is.
void CUiContainer::KeyDown(int key, int repeat, int modifiers) {
    Dispatch(kEventKeyDown, key, repeat, modifiers, false);
}

void CUiContainer::Char(int key, int repeat, int modifiers) {
    Dispatch(kEventChar, key, repeat, modifiers, false);
}

void CUiContainer::KeyUp(int key, int repeat, int modifiers) {
    Dispatch(kEventKeyUp, key, repeat, modifiers, false);
}

// The wheel's four arguments go on unchanged; the children are tested at (x, y).
void CUiContainer::Wheel(int modifiers, int delta, int x, int y) {
    CUiContainer* self = this;
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
        CUiWindow* child = self->children[i];
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
