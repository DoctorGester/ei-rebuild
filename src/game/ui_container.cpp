// Containers (CUiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "ui.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/stlp.h"
#include "originals_render.h"
#include "originals_ui.h"

namespace {
// The container's vtables (main, input side).
void** ContainerVtable() { return reinterpret_cast<void**>(0x00741008); }
void** ContainerInputVtable() { return reinterpret_cast<void**>(0x00740FD4); }

// The display object (00500960): slot 6 gives the last frame's length in milliseconds.
std::uint32_t FrameMilliseconds() {
    void* display = DisplayObject();
    using Fn = std::uint32_t(__thiscall*)(void*);
    return reinterpret_cast<Fn>((*static_cast<void***>(display))[6])(display);
}

// Slot 0 of an input side or a window: the deleting destructor (with 1: the memory is freed).
void DeleteInstance(void* object) {
    using Fn = void(__thiscall*)(void*, int);
    reinterpret_cast<Fn>((*static_cast<void***>(object))[0])(object, 1);
}
} // namespace

// --- Construction and destruction --------------------------------------------------------------

CUiContainer* CUiContainer::Construct(CInterface3D* ui) {
    CUiWindow::Construct(ui);
    children_ = UiPtrArray<CUiWindow>{};
    // The extra input sides (an empty STLport vector; the original stores the low byte of `ui` as
    // its allocator into +0x160 and overwrites it with the null capacity at once).
    inputsBegin_ = nullptr;
    inputsEnd_ = nullptr;
    inputsCapacity_ = nullptr;
    vtable_ = ContainerVtable();
    inputVtable_ = ContainerInputVtable();
    return this;
}
EI_REPLACE_METHOD(0x00600B20, CUiContainer::Construct);

void CUiContainer::DestroyContainer() {
    vtable_ = ContainerVtable();
    inputVtable_ = ContainerInputVtable();
    for (UiInputSide** side = inputsBegin_; side != inputsEnd_; ++side)
        if (*side) DeleteInstance(*side);
    for (int i = 0; i < children_.size; ++i)
        if (CUiWindow* child = children_.data[i]) DeleteInstance(child);
    children_.SetSize(0, -1);
    // The vector's storage goes back to the node pool (the pointers are left as they are).
    if (inputsBegin_)
        stlp::deallocate(inputsBegin_, static_cast<std::size_t>(inputsCapacity_ - inputsBegin_) * sizeof(UiInputSide*));
    // The children array's destructor (its storage is already freed).
    if (children_.data) FreeAligned(children_.data);
    DestroyBase();
}
EI_REPLACE_METHOD(0x00600BA0, CUiContainer::DestroyContainer);

// STLport's push_back: when full, a block of size + max(size, 1) pointers from the node pool
// (00601B70, _M_insert_overflow), the old block back to the pool.
void CUiContainer::AddInput(UiInputSide* input) {
    if (!input) return;
    if (inputsEnd_ != inputsCapacity_) {
        if (inputsEnd_) *inputsEnd_ = input;
        ++inputsEnd_;
        return;
    }
    std::size_t size = static_cast<std::size_t>(inputsEnd_ - inputsBegin_);
    std::size_t length = size + (size > 1 ? size : 1);
    auto** fresh = static_cast<UiInputSide**>(stlp::allocate(length * sizeof(UiInputSide*)));
    for (std::size_t i = 0; i < size; ++i) fresh[i] = inputsBegin_[i];
    fresh[size] = input;
    if (inputsBegin_)
        stlp::deallocate(inputsBegin_, static_cast<std::size_t>(inputsCapacity_ - inputsBegin_) * sizeof(UiInputSide*));
    inputsBegin_ = fresh;
    inputsEnd_ = fresh + size + 1;
    inputsCapacity_ = fresh + length;
}
EI_REPLACE_METHOD(0x00600CC0, CUiContainer::AddInput);

// --- Per-frame slots ---------------------------------------------------------------------------

// The milliseconds are loaded as an unsigned qword and multiplied by the float 0.001.
double CUiContainer::FrameTime() { return x87::FromUnsigned(FrameMilliseconds()) * static_cast<double>(0.001f); }
EI_REPLACE_METHOD(0x00600270, CUiContainer::FrameTime);

void CUiContainer::UpdateBase() {
    for (int i = 0; i < children_.size; ++i) children_.data[i]->VirtualUpdate();
}
EI_REPLACE_METHOD(0x006010E0, CUiContainer::UpdateBase);

void CUiContainer::RedrawContainer() {
    if (!interface_->drawing_) return;
    RedrawBase();
    for (int i = 0; i < children_.size; ++i) {
        CUiWindow* child = children_.data[i];
        if (child->enabled_) child->VirtualRedraw();
    }
}
EI_REPLACE_METHOD(0x00601040, CUiContainer::RedrawContainer);

void CUiContainer::RedrawChildTips() {
    if (!interface_->drawing_) return;
    RedrawTips();
    for (int i = 0; i < children_.size; ++i) {
        CUiWindow* child = children_.data[i];
        if (child->enabled_) child->VirtualRedrawTips();
    }
}
EI_REPLACE_METHOD(0x00601090, CUiContainer::RedrawChildTips);

void CUiContainer::ShowChildren(int show) {
    WindowShowSurfacesOriginal(this, nullptr, show);
    for (int i = 0; i < children_.size; ++i) {
        CUiWindow* child = children_.data[i];
        if (child->enabled_) child->VirtualShow(show);
    }
}
EI_REPLACE_METHOD(0x00601AC0, CUiContainer::ShowChildren);

// --- Input dispatch ----------------------------------------------------------------------------

void UiContainerInput::Dispatch(int slot, int a, int b, int c, bool atPoint) {
    CUiContainer* self = Container();
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
        child->inputHandled_ = false;
        child->Input()->Event(slot, a, b, c);
        if (self->children_.data[i]->inputHandled_) {
            handled_ = true;
            return;
        }
    }
}

void UiContainerInput::OnDeactivate() {
    CUiContainer* self = Container();
    for (UiInputSide** side = self->inputsBegin_; side != self->inputsEnd_; ++side) (*side)->Deactivate();
    for (int i = 0; i < self->children_.size; ++i) self->children_.data[i]->Input()->Deactivate();
}
EI_REPLACE_METHOD(0x00601A60, UiContainerInput::OnDeactivate);

void UiContainerInput::OnLeftDown(int buttons, int x, int y) { Dispatch(kLeftDown, buttons, x, y, true); }
EI_REPLACE_METHOD(0x00601110, UiContainerInput::OnLeftDown);

void UiContainerInput::OnLeftUp(int buttons, int x, int y) { Dispatch(kLeftUp, buttons, x, y, true); }
EI_REPLACE_METHOD(0x006011F0, UiContainerInput::OnLeftUp);

void UiContainerInput::OnMouseMove(int buttons, int x, int y) { Dispatch(kMouseMove, buttons, x, y, true); }
EI_REPLACE_METHOD(0x006012D0, UiContainerInput::OnMouseMove);

void UiContainerInput::OnRightDown(int buttons, int x, int y) { Dispatch(kRightDown, buttons, x, y, true); }
EI_REPLACE_METHOD(0x006013B0, UiContainerInput::OnRightDown);

void UiContainerInput::OnRightUp(int buttons, int x, int y) { Dispatch(kRightUp, buttons, x, y, true); }
EI_REPLACE_METHOD(0x00601490, UiContainerInput::OnRightUp);

void UiContainerInput::OnLeftDouble(int buttons, int x, int y) { Dispatch(kLeftDouble, buttons, x, y, true); }
EI_REPLACE_METHOD(0x00601570, UiContainerInput::OnLeftDouble);

void UiContainerInput::OnRightDouble(int buttons, int x, int y) { Dispatch(kRightDouble, buttons, x, y, true); }
EI_REPLACE_METHOD(0x00601650, UiContainerInput::OnRightDouble);

// Key events reach every enabled child, wherever the mouse is.
void UiContainerInput::OnKeyDown(int key, int repeat, int flags) { Dispatch(kKeyDown, key, repeat, flags, false); }
EI_REPLACE_METHOD(0x00601820, UiContainerInput::OnKeyDown);

void UiContainerInput::OnChar(int key, int repeat, int flags) { Dispatch(kChar, key, repeat, flags, false); }
EI_REPLACE_METHOD(0x006018E0, UiContainerInput::OnChar);

void UiContainerInput::OnKeyUp(int key, int repeat, int flags) { Dispatch(kKeyUp, key, repeat, flags, false); }
EI_REPLACE_METHOD(0x006019A0, UiContainerInput::OnKeyUp);

// The wheel's four arguments go on unchanged; the children are tested at (x, y). Returns 1 (the
// interface's wheel dispatch 005F7880 passes it on).
int UiContainerInput::OnWheel(int flags, int delta, int x, int y) {
    CUiContainer* self = Container();
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
        child->inputHandled_ = false;
        child->Input()->Wheel(flags, delta, x, y);
        if (self->children_.data[i]->inputHandled_) {
            handled_ = true;
            return 1;
        }
    }
    return 1;
}
EI_REPLACE_METHOD(0x00601730, UiContainerInput::OnWheel);
