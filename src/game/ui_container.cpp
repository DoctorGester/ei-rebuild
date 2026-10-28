// Containers (CUiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "model.h"
#include "ui.h"

#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/stlp.h"
#include "game_data.h"

namespace {
// The scene renderer: the last frame's length in milliseconds.
std::uint32_t FrameMilliseconds() { return SceneRenderer()->FrameTime(); }

} // namespace

// --- Construction and destruction --------------------------------------------------------------

CUiContainer::CUiContainer(CInterface3D* ui) : CUiWindow(ui) {
    children_ = UiPtrArray<CUiWindow>{};
    // The extra input sides (an empty STLport vector; the low byte of `ui` is stored as its allocator and
    // overwritten with the null capacity at once).
    inputsBegin_ = nullptr;
    inputsEnd_ = nullptr;
    inputsCapacity_ = nullptr;
}

CUiContainer::~CUiContainer() {
    for (UiInputSide** side = inputsBegin_; side != inputsEnd_; ++side) delete *side;
    for (int i = 0; i < children_.size; ++i) delete children_.data[i];
    children_.SetSize(0, -1);
    // The vector's storage goes back to the node pool (the pointers are left as they are).
    if (inputsBegin_)
        stlp::deallocate(inputsBegin_, static_cast<std::size_t>(inputsCapacity_ - inputsBegin_) * sizeof(UiInputSide*));
    // The children array's destructor (its storage is already freed).
    if (children_.data) lib::FreeAligned32(children_.data);
}

extern "C" int __cdecl _purecall();

// Pure: a screen that does not override CreateScreen never gets on the screen.
void CUiContainer::CreateScreen() { _purecall(); }
void CUiContainer::CloseScreen() { _purecall(); }

// STLport's push_back: when full, a block of size + max(size, 1) pointers from the node pool
// (_M_insert_overflow), the old block back to the pool.
void CUiContainer::AddInput(UiInputSide* input) {
    if (!input) return;
    if (inputsEnd_ != inputsCapacity_) {
        if (inputsEnd_) *inputsEnd_ = input;
        ++inputsEnd_;
        return;
    }
    std::size_t size = static_cast<std::size_t>(inputsEnd_ - inputsBegin_);
    std::size_t length = size + (size > 1 ? size : 1);
    auto** fresh = stlp::allocate_as<UiInputSide*>(length);
    for (std::size_t i = 0; i < size; ++i) fresh[i] = inputsBegin_[i];
    fresh[size] = input;
    if (inputsBegin_)
        stlp::deallocate(inputsBegin_, static_cast<std::size_t>(inputsCapacity_ - inputsBegin_) * sizeof(UiInputSide*));
    inputsBegin_ = fresh;
    inputsEnd_ = fresh + size + 1;
    inputsCapacity_ = fresh + length;
}

// --- Per-frame slots ---------------------------------------------------------------------------

// The milliseconds are loaded as an unsigned qword and multiplied by the float 0.001.
double CUiContainer::FrameTime() { return x87::FromUnsigned(FrameMilliseconds()) * static_cast<double>(0.001f); }

void CUiContainer::UpdateBase() {
    for (int i = 0; i < children_.size; ++i) children_.data[i]->Update();
}

void CUiContainer::RedrawContainer() {
    if (!interface_->drawing_) return;
    RedrawBase();
    for (int i = 0; i < children_.size; ++i) {
        CUiWindow* child = children_.data[i];
        if (child->enabled_) child->Redraw();
    }
}

void CUiContainer::RedrawChildTips() {
    if (!interface_->drawing_) return;
    RedrawRegionTips();
    for (int i = 0; i < children_.size; ++i) {
        CUiWindow* child = children_.data[i];
        if (child->enabled_) child->RedrawTips();
    }
}

void CUiContainer::ShowChildren(int show) {
    ShowSurfaces(show);
    for (int i = 0; i < children_.size; ++i) {
        CUiWindow* child = children_.data[i];
        if (child->enabled_) child->Show(show);
    }
}

// --- Input dispatch ----------------------------------------------------------------------------

void CUiContainer::Dispatch(int slot, int a, int b, int c, bool atPoint) {
    CUiContainer* self = this;
    handled_ = false;
    for (UiInputSide** side = self->inputsBegin_; side != self->inputsEnd_; ++side) {
        (*side)->handled_ = false;
        (*side)->Event(slot, a, b, c);
        if ((*side)->handled_) {
            handled_ = true;
            return;
        }
    }
    for (int i = 0; i < self->children_.size; ++i) {
        CUiWindow* child = self->children_.data[i];
        if (!child->enabled_) continue;
        if (atPoint && !child->ContainsPoint(b, c)) continue;
        child = self->children_.data[i];
        child->handled_ = false;
        child->Input()->Event(slot, a, b, c);
        if (self->children_.data[i]->handled_) {
            handled_ = true;
            return;
        }
    }
}

void CUiContainer::Deactivate() {
    CUiContainer* self = this;
    for (UiInputSide** side = self->inputsBegin_; side != self->inputsEnd_; ++side) (*side)->Deactivate();
    for (int i = 0; i < self->children_.size; ++i) self->children_.data[i]->Input()->Deactivate();
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
    for (UiInputSide** side = self->inputsBegin_; side != self->inputsEnd_; ++side) {
        (*side)->handled_ = false;
        (*side)->Wheel(flags, delta, x, y);
        if ((*side)->handled_) {
            handled_ = true;
            return 1;
        }
    }
    for (int i = 0; i < self->children_.size; ++i) {
        CUiWindow* child = self->children_.data[i];
        if (!child->enabled_ || !child->ContainsPoint(x, y)) continue;
        child = self->children_.data[i];
        child->handled_ = false;
        child->Input()->Wheel(flags, delta, x, y);
        if (self->children_.data[i]->handled_) {
            handled_ = true;
            return 1;
        }
    }
    return 1;
}
