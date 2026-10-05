// Containers (CUiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Area ticket A-I").
#include "ui.h"

#include "ei/hook.h"

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
    for (int i = 0; i < self->children_.size_; ++i) {
        CUiWindow* child = self->children_.data_[i];
        if (!child->enabled_) continue;
        if (atPoint && !child->ContainsPoint(b, c)) continue;
        child = self->children_.data_[i];
        child->inputHandled_ = false;
        child->Input()->Event(slot, a, b, c);
        if (self->children_.data_[i]->inputHandled_) {
            handled_ = true;
            return;
        }
    }
}

void UiContainerInput::OnDeactivate() {
    CUiContainer* self = Container();
    for (UiInputSide** side = self->inputsBegin_; side != self->inputsEnd_; ++side) (*side)->Deactivate();
    for (int i = 0; i < self->children_.size_; ++i) self->children_.data_[i]->Input()->Deactivate();
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
    for (int i = 0; i < self->children_.size_; ++i) {
        CUiWindow* child = self->children_.data_[i];
        if (!child->enabled_ || !child->ContainsPoint(x, y)) continue;
        child = self->children_.data_[i];
        child->inputHandled_ = false;
        child->Input()->Wheel(flags, delta, x, y);
        if (self->children_.data_[i]->inputHandled_) {
            handled_ = true;
            return 1;
        }
    }
    return 1;
}
EI_REPLACE_METHOD(0x00601730, UiContainerInput::OnWheel);
