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

// --- Input dispatch ----------------------------------------------------------------------------

namespace {
// Offers an event to a container's input sides, then to its enabled children (those holding `at`, when it is given),
// until one handles it; to its popup alone while it has one. `call` sends it to one input side.
template <class Call>
void Offer(UiContainer* self, const Point* at, Call call) {
    self->handled = false;
    if (UiWindow* popup = self->popup) {
        popup->handled = false;
        call(popup->Input());
        self->handled = true;
        return;
    }
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

void UiContainer::LeftDown(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->LeftDown(point); });
}

void UiContainer::LeftUp(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->LeftUp(point); });
}

void UiContainer::MouseMove(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->MouseMove(point); });
}

void UiContainer::RightDown(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->RightDown(point); });
}

void UiContainer::RightUp(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->RightUp(point); });
}

void UiContainer::LeftDouble(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->LeftDouble(point); });
}

void UiContainer::RightDouble(Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->RightDouble(point); });
}

// Key events reach every enabled child, wherever the mouse is.
void UiContainer::KeyDown(platform::Key key, int scan, bool repeat) {
    Offer(this, nullptr, [&](UiInputSide* to) { to->KeyDown(key, scan, repeat); });
}

void UiContainer::Char(int key, int scan) {
    Offer(this, nullptr, [&](UiInputSide* to) { to->Char(key, scan); });
}

void UiContainer::KeyUp(platform::Key key, int scan) {
    Offer(this, nullptr, [&](UiInputSide* to) { to->KeyUp(key, scan); });
}

// The children are tested at the position.
void UiContainer::Wheel(int delta, Point point) {
    Offer(this, &point, [&](UiInputSide* to) { to->Wheel(delta, point); });
}

void UiContainer::AddChild(UiWindow* child) {
    if (!child) return;
    children.push_back(child);
}
