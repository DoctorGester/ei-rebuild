// The scroll bar and the options slider (ui_scrollbar.h): the constructors, the position and layout, the thumb
// drags, the wheel and the slider's position. See docs/notes/ui.md, section 8.
#include "ui_scrollbar.h"
#include "ui.h"
#include "ui_mesh.h"
#include "ui_screens.h"
#include "lib/containers.h"
#include "lib/rect.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "screen_layout.h"

#include <cstdint>
#include <initializer_list>

namespace {
// The thumb sits slightly in front of the track (0.1) and 5 to the right of the track's left edge.
constexpr float kThumbLift = 0.1f;
constexpr float kThumbInset = 5.0f;

constexpr int kDragging = 1;
constexpr int kSliderThumbHalfWidth = 14;
constexpr int kSliderThumbHeight = 10;

// The wheel's step per unit (kWheelUnits a notch): 1/120.
constexpr float kWheelStep = 0.008333334f;

constexpr float kQuarterTurn = /*007C8210*/ 1.5707964f;   // a static initialiser: pi * 0.5

void TurnQuarter(UiMesh* sprite) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    sprite->localRotation = Quaternion::AxisAngle(axis, kQuarterTurn);
}
} // namespace

// The virtuals of the scroll bar and of the slider (the slider's create / update virtuals and the drag along
// x are its own).
UiScrollbar::UiScrollbar(Interface3D* ui, Rect rect, float newDepth, int maximum, float newSpeed) : UiWindow(ui) {
    maxPosition = maximum;
    speed = newSpeed;
    depth = newDepth;
    moved = true;
    state = 0;
    position = -1;
    LoadTexture("Scrollbar", TextureKind::Interface);
    trackSprite = AddBlankSprite();
    thumbSprite = AddBlankSprite();
    upArrow = AddBlankSprite();
    downArrow = AddBlankSprite();
    upButton = AddRegion();
    downButton = AddRegion();
    trackArea = AddRegion();
    SetRect(rect);
    UiScrollbar::SetPosition(0);
    accumulated = 0.0f;
}

void UiScrollbar::SetPosition(int newPosition) {
    if (newPosition < 0) newPosition = 0;
    int maximum = maxPosition;
    if (newPosition > maximum) newPosition = maximum;
    moved = true;
    if (position == newPosition) return;
    position = newPosition;
    // Truncated by _ftol. A zero maximum divides 0 by 0 (the position is clamped to 0);
    // the game's _ftol stores the NaN as a 64-bit integer (0x8000000000000000) and returns its low
    // half, 0, where a C++ cast would give 0x80000000.
    int offset = maximum == 0 ? 0
                              : static_cast<int>(static_cast<float>(track.Height() * newPosition) / maximum);
    thumbSprite->Place(track.left + kThumbInset, static_cast<float>(offset) + track.top, depth - kThumbLift);
    const int centre = track.top + offset;
    thumb = {track.left, centre - 14, track.left + 10, centre + 14};
}

void UiScrollbar::SetRect(const Rect& newRect) {
    rect = newRect;
    const Rect bar = rect.RightStrip(10);   // the arrows at both ends, the track between them
    track = bar.Inset(0, 26);

    // The sprites (SetRectangle and the regions' SetRect are Rebirth-patched: through their
    // addresses).
    trackSprite->SetRectangle(depth, 100.0f, bar.Inset(2, 4), 8, 4, 0x20, 0xFC);
    thumbSprite->SetRectangle(depth - kThumbLift, 100.0f, bar.TopStrip(0x1C), 0x28, 8, 0x50, 0x78);
    upArrow->SetRectangle(depth, 100.0f, bar.TopStrip(0xC), 0x28, 0xB0, 0x50, 0x80);
    downArrow->SetRectangle(depth, 100.0f, bar.BottomStrip(0xC), 0x28, 0x80, 0x50, 0xB0);

    upButton->SetRect(bar.TopStrip(0xC));
    downButton->SetRect(bar.BottomStrip(0xC));
    trackArea->SetRect(rect.Inset(0, 0, 10, 0));

    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}

// --- A-H20: dragging, the wheel, the slider's position --------------------------------------------

void UiScrollbar::MouseMove(UiEvent& event) {
    if (state == kDragging) {
        Point at = screen_layout::FromScreen(event.at);
        int along = at.y - grabY;
        SetPosition(lib::Truncate(static_cast<float>(along - track.top) * maxPosition / track.Height()));
    }
    event.handled = true;
}

void UiScrollbar::Wheel(UiEvent& event) {
    if (RegionUnder(event.at) == trackArea) {
        float step = event.delta * kWheelUnits * speed * kWheelStep;
        accumulated -= step;
        event.handled = true;
    }
}

void UiSlider::SetPosition(int newPosition) {
    if (newPosition < 0) newPosition = 0;
    int maximum = maxPosition;
    if (newPosition > maximum) newPosition = maximum;
    moved = true;
    if (position == newPosition) return;
    position = newPosition;
    // Truncated by _ftol (a zero maximum: 0 / 0, which _ftol makes 0).
    int offset = lib::Truncate(static_cast<float>(track.Width() * newPosition) / maximum);
    thumbSprite->Place(static_cast<float>(track.left + offset), track.top + kThumbInset, depth - kThumbLift);
    const int centre = track.left + offset;
    thumb = {centre - kSliderThumbHalfWidth, track.top, centre + kSliderThumbHalfWidth, track.top + kSliderThumbHeight};
}

void UiSlider::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::MouseMove: MouseMove(event); break;
    default: UiScrollbar::OnEvent(event); break;
    }
}

void UiSlider::MouseMove(UiEvent& event) {
    if (state == kDragging) {
        Point at = screen_layout::FromScreen(event.at);
        int along = at.x - grabX;
        SetPosition(lib::Truncate(static_cast<float>(along - track.left) * maxPosition / track.Width()));
    }
    event.handled = true;
}

void UiScrollbar::SetMaximum(int maximum) {
    if (maximum < 0) maximum = 0;
    maxPosition = maximum;
    moved = true;
    const bool visible = maximum != 0;
    for (UiMesh* sprite : {trackSprite, thumbSprite, upArrow, downArrow}) sprite->visible = visible;
    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}

// --- Scroll bar --------------------------------------------------------------------------------

bool UiScrollbar::TakeMoved() {
    if (!moved) return false;
    moved = false;
    return true;
}

int UiScrollbar::TakePosition() {
    moved = false;
    return position;
}

void UiScrollbar::Update() {
    if (state == 2 || state == 3) {
        // The container's FrameTime does not use the window.
        float step = UiContainer::FrameTime() * speed * 16.0f;
        if (state == 2) step = -step;
        accumulated = step + accumulated;
    }
    int steps = 0;
    if (accumulated > 0.0f) {    // zero, negative and NaN take the other branch
        if (accumulated < 1.0f) return;
        // Whole steps.
        float rest = accumulated;
        do {
            rest -= 1.0f;
            ++steps;
        } while (rest >= 1.0f);
        accumulated = rest;
    } else {
        if (accumulated > -1.0f) return;
        float rest;
        do {
            rest = accumulated + 1.0f;
            --steps;
            accumulated = rest;
        } while (rest <= -1.0f);   // ends on a NaN (the original's did not)
    }
    if (steps != 0) SetPosition(position + steps);
}

void UiScrollbar::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::Wheel: Wheel(event); break;
    default: break;
    }
}

void UiScrollbar::LeftDown(UiEvent& event) {
    // The arrows' region from the screen point (the original tested the point scaled twice: no arrow took a click at
    // any size but 800x600).
    const UiRegion* region = RegionUnder(event.at);
    Point at = screen_layout::FromScreen(event.at);
    if (thumb.Contains(at)) {
        CaptureMouse();
        state = 1;
        grabX = at.x - (thumb.left + thumb.right) / 2;
        grabY = at.y - (thumb.top + thumb.bottom) / 2;
        event.handled = true;
        return;
    }
    if (region != upButton && region != downButton) return;
    CaptureMouse();
    state = region == upButton ? 2 : 3;
    accumulated = 0.0f;
    event.handled = true;
}

void UiScrollbar::LeftUp(UiEvent& event) {
    state = 0;
    ui->mouseCapture = nullptr;
    accumulated = 0.0f;
    event.handled = true;
}

// --- The slider's layout -------------------------------------------------------------------

void UiSlider::SetRect(const Rect& newRect) {
    rect = newRect;
    const Rect bar = newRect.BottomStrip(10);   // the arrows at both ends, the track between them
    track = bar.Inset(26, 0);

    trackSprite->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 6.0f, static_cast<float>(newRect.Width() - 8), 8, 4, 0x20,
                              0xFC);
    trackSprite->Place((newRect.right + newRect.left) * 0.5f, static_cast<float>(newRect.bottom - 5), depth);
    TurnQuarter(trackSprite);

    thumbSprite->SetRectangle(depth - kThumbLift, 100.0f, 0.0f, 0.0f, 10.0f, 30.0f, 0x28, 8, 0x50, 0x78);
    TurnQuarter(thumbSprite);

    upArrow->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    upArrow->Place(static_cast<float>(newRect.right - 6), static_cast<float>(newRect.bottom - 5), depth);
    TurnQuarter(upArrow);

    downArrow->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    downArrow->Place(static_cast<float>(newRect.left + 6), static_cast<float>(newRect.bottom - 5), depth);
    TurnQuarter(downArrow);

    upButton->SetRect(bar.LeftStrip(12));
    downButton->SetRect(bar.RightStrip(12));
    trackArea->SetRect(newRect.Inset(0, 0, 0, 10));

    // The position set again (one below first, so the change virtual sees a change).
    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}

// --- The controls' destructors ---------------------------------------------------------------

UiScrollbar::~UiScrollbar() {}
