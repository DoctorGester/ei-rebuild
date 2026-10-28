// Containers (CUiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "model.h"
#include "ui.h"

#include "ei/x87.h"

namespace {
// The scene renderer: the last frame's length in milliseconds.
std::uint32_t FrameMilliseconds() { return SceneRenderer()->FrameTime(); }

} // namespace

// --- Construction and destruction --------------------------------------------------------------

CUiContainer::CUiContainer(CInterface3D* ui) : CUiWindow(ui) {
    children_ = UiPtrArray<CUiWindow>{};
}

CUiContainer::~CUiContainer() {
    for (UiInputSide* side : inputs_) delete side;
    for (int i = 0; i < std::ssize(children_); ++i) delete children_[i];
    children_.clear();
}

extern "C" int _purecall();

// Pure: a screen that does not override CreateScreen never gets on the screen.
void CUiContainer::CreateScreen() { _purecall(); }
void CUiContainer::CloseScreen() { _purecall(); }

void CUiContainer::AddInput(UiInputSide* input) {
    if (input) inputs_.push_back(input);
}

// --- Per-frame slots ---------------------------------------------------------------------------

// The milliseconds are loaded as an unsigned qword and multiplied by the float 0.001.
double CUiContainer::FrameTime() { return x87::FromUnsigned(FrameMilliseconds()) * static_cast<double>(0.001f); }

void CUiContainer::UpdateBase() {
    for (int i = 0; i < std::ssize(children_); ++i) children_[i]->Update();
}

void CUiContainer::RedrawContainer() {
    if (!interface_->drawing_) return;
    RedrawBase();
    for (int i = 0; i < std::ssize(children_); ++i) {
        CUiWindow* child = children_[i];
        if (child->enabled_) child->Redraw();
    }
}

void CUiContainer::RedrawChildTips() {
    if (!interface_->drawing_) return;
    RedrawRegionTips();
    for (int i = 0; i < std::ssize(children_); ++i) {
        CUiWindow* child = children_[i];
        if (child->enabled_) child->RedrawTips();
    }
}

void CUiContainer::ShowChildren(int show) {
    ShowSurfaces(show);
    for (int i = 0; i < std::ssize(children_); ++i) {
        CUiWindow* child = children_[i];
        if (child->enabled_) child->Show(show);
    }
}

// --- Input dispatch ----------------------------------------------------------------------------

void CUiContainer::Dispatch(int slot, int a, int b, int c, bool atPoint) {
    CUiContainer* self = this;
    handled_ = false;
    for (auto side = self->inputs_.begin(); side != self->inputs_.end(); ++side) {
        (*side)->handled_ = false;
        (*side)->Event(slot, a, b, c);
        if ((*side)->handled_) {
            handled_ = true;
            return;
        }
    }
    for (int i = 0; i < std::ssize(self->children_); ++i) {
        CUiWindow* child = self->children_[i];
        if (!child->enabled_) continue;
        if (atPoint && !child->ContainsPoint(b, c)) continue;
        child = self->children_[i];
        child->handled_ = false;
        child->Input()->Event(slot, a, b, c);
        if (self->children_[i]->handled_) {
            handled_ = true;
            return;
        }
    }
}

void CUiContainer::Deactivate() {
    CUiContainer* self = this;
    for (auto side = self->inputs_.begin(); side != self->inputs_.end(); ++side) (*side)->Deactivate();
    for (int i = 0; i < std::ssize(self->children_); ++i) self->children_[i]->Input()->Deactivate();
}

int CUiContainer::LeftDown(int buttons, int x, int y) {
    Dispatch(kEventLeftDown, buttons, x, y, true);
    return 0;
}

int CUiContainer::LeftUp(int buttons, int x, int y) {
    Dispatch(kEventLeftUp, buttons, x, y, true);
    return 0;
}

int CUiContainer::MouseMove(int buttons, int x, int y) {
    Dispatch(kEventMouseMove, buttons, x, y, true);
    return 0;
}

int CUiContainer::RightDown(int buttons, int x, int y) {
    Dispatch(kEventRightDown, buttons, x, y, true);
    return 0;
}

int CUiContainer::RightUp(int buttons, int x, int y) {
    Dispatch(kEventRightUp, buttons, x, y, true);
    return 0;
}

int CUiContainer::LeftDouble(int buttons, int x, int y) {
    Dispatch(kEventLeftDouble, buttons, x, y, true);
    return 0;
}

int CUiContainer::RightDouble(int buttons, int x, int y) {
    Dispatch(kEventRightDouble, buttons, x, y, true);
    return 0;
}

// Key events reach every enabled child, wherever the mouse is.
int CUiContainer::KeyDown(int key, int repeat, int flags) {
    Dispatch(kEventKeyDown, key, repeat, flags, false);
    return 0;
}

int CUiContainer::Char(int key, int repeat, int flags) {
    Dispatch(kEventChar, key, repeat, flags, false);
    return 0;
}

int CUiContainer::KeyUp(int key, int repeat, int flags) {
    Dispatch(kEventKeyUp, key, repeat, flags, false);
    return 0;
}

// The wheel's four arguments go on unchanged; the children are tested at (x, y). Returns 1 (the
// interface's wheel dispatch passes it on).
int CUiContainer::Wheel(int flags, int delta, int x, int y) {
    CUiContainer* self = this;
    handled_ = false;
    for (auto side = self->inputs_.begin(); side != self->inputs_.end(); ++side) {
        (*side)->handled_ = false;
        (*side)->Wheel(flags, delta, x, y);
        if ((*side)->handled_) {
            handled_ = true;
            return 1;
        }
    }
    for (int i = 0; i < std::ssize(self->children_); ++i) {
        CUiWindow* child = self->children_[i];
        if (!child->enabled_ || !child->ContainsPoint(x, y)) continue;
        child = self->children_[i];
        child->handled_ = false;
        child->Input()->Wheel(flags, delta, x, y);
        if (self->children_[i]->handled_) {
            handled_ = true;
            return 1;
        }
    }
    return 1;
}
