// Containers (UiContainer, ui.h): the default input handlers of their input side, which pass an
// event on to the extra input sides and the child windows. See docs/notes/ui.md ("Container input dispatch").
#include "game_singletons.h"
#include "model.h"
#include "ui.h"

#include <cstdlib>

// --- Construction and destruction --------------------------------------------------------------

UiContainer::~UiContainer() {
    for (UiInputSide* side : inputs) delete side;
    for (int i = 0; i < std::ssize(children); ++i) delete children[i];
    children.clear();
}

// Pure: a screen that does not override CreateScreen never gets on the screen.
void UiContainer::CreateScreen() { std::abort(); }
void UiContainer::CloseScreen() { std::abort(); }

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

namespace {
// Offers an event to a container's input sides, then to its enabled children (those holding `at`, when it is given),
// until one handles it. `call` sends it to one input side.
template <class Call>
void Offer(UiContainer* self, const Point* at, Call call) {
    self->handled = false;
    for (auto& side : self->inputs) {
        side->handled = false;
        call(side);
        if (side->handled) {
            self->handled = true;
            return;
        }
    }
    for (int i = 0; i < std::ssize(self->children); ++i) {
        UiWindow* child = self->children[i];
        if (!child->enabled || (at && !child->ContainsPoint(*at))) continue;
        child = self->children[i];
        child->handled = false;
        call(child->Input());
        if (self->children[i]->handled) {
            self->handled = true;
            return;
        }
    }
}
} // namespace

void UiContainer::Deactivate() {
    UiContainer* self = this;
    for (auto& side : self->inputs) side->Deactivate();
    for (int i = 0; i < std::ssize(self->children); ++i) self->children[i]->Input()->Deactivate();
}

void UiContainer::LeftDown(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->LeftDown(buttons, point); });
}

void UiContainer::LeftUp(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->LeftUp(buttons, point); });
}

void UiContainer::MouseMove(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->MouseMove(buttons, point); });
}

void UiContainer::RightDown(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->RightDown(buttons, point); });
}

void UiContainer::RightUp(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->RightUp(buttons, point); });
}

void UiContainer::LeftDouble(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->LeftDouble(buttons, point); });
}

void UiContainer::RightDouble(int buttons, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->RightDouble(buttons, point); });
}

// Key events reach every enabled child, wherever the mouse is.
void UiContainer::KeyDown(int key, int repeat, int modifiers) {
    Offer(this, nullptr, [&](UiInputSide* to) { to->KeyDown(key, repeat, modifiers); });
}

void UiContainer::Char(int key, int repeat, int modifiers) {
    Offer(this, nullptr, [&](UiInputSide* to) { to->Char(key, repeat, modifiers); });
}

void UiContainer::KeyUp(int key, int repeat, int modifiers) {
    Offer(this, nullptr, [&](UiInputSide* to) { to->KeyUp(key, repeat, modifiers); });
}

// The children are tested at the position.
void UiContainer::Wheel(int modifiers, int delta, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->Wheel(modifiers, delta, point); });
}

void UiContainer::AddChild(UiWindow* child) {
    if (!child) return;
    children.push_back(child);
}
