// The plain scroll bar's constructor, position and layout (UiScrollbar) and the options slider's constructor;
// also the thumb drags, the wheel and the slider's position. See ui.h and docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"

#include <cstdint>
#include "lib/rounding.h"

namespace {
constexpr int kScrollbarSprites = 4;


// The thumb sits slightly in front of the track (0.1) and 5 to the right of the track's left edge.
constexpr float kThumbLift = 0.1f;
constexpr float kThumbInset = 5.0f;

enum Sprite { kTrack, kThumb, kUpArrow, kDownArrow };
enum Region { kUpRegion, kDownRegion, kTrackRegion };

constexpr int kDragging = 1;
constexpr int kSliderThumbHalfWidth = 14;
constexpr int kSliderThumbHeight = 10;

// The wheel's step per unit (kWheelUnits a notch): 1/120.
constexpr float kWheelStep = 0.008333334f;

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
    for (int i = 0; i < 4; ++i) AddBlankSprite();
    for (int i = 0; i < 3; ++i) AddRegion();
    UiScrollbar::SetRect(&rect);
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
    sceneObjects[kThumb]->Place(track.left + kThumbInset,
                                static_cast<float>(offset) + track.top, depth - kThumbLift);
    const int centre = track.top + offset;
    thumb = {track.left, centre - 14, track.left + 10, centre + 14};
}

void UiScrollbar::SetRect(const Rect* newRect) {
    rect = *newRect;
    const Rect bar = rect.RightStrip(10);   // the arrows at both ends, the track between them
    track = bar.Inset(0, 26);

    // The sprites (SetRectangle and the regions' SetRect are Rebirth-patched: through their
    // addresses).
    sceneObjects[kTrack]->SetRectangle(depth, 100.0f, bar.Inset(2, 4), 8, 4, 0x20, 0xFC);
    sceneObjects[kThumb]->SetRectangle(depth - kThumbLift, 100.0f, bar.TopStrip(0x1C), 0x28, 8, 0x50, 0x78);
    sceneObjects[kUpArrow]->SetRectangle(depth, 100.0f, bar.TopStrip(0xC), 0x28, 0xB0, 0x50, 0x80);
    sceneObjects[kDownArrow]->SetRectangle(depth, 100.0f, bar.BottomStrip(0xC), 0x28, 0x80, 0x50, 0xB0);

    regions[kUpRegion]->SetRect(bar.TopStrip(0xC));
    regions[kDownRegion]->SetRect(bar.BottomStrip(0xC));
    regions[kTrackRegion]->SetRect(rect.Inset(0, 0, 10, 0));

    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}

// --- A-H20: dragging, the wheel, the slider's position --------------------------------------------

void UiScrollbar::MouseMove(Point point) {
    if (state == kDragging) {
        ui->ScalePointFromScreen(&point);
        int along = point.y - grabY;
        SetPosition(lib::Truncate(static_cast<float>(along - track.top) * maxPosition / track.Height()));
    }
    handled = true;
}

void UiScrollbar::Wheel(float notches, Point point) {
    if (RegionAt(point) == kTrackRegion) {
        float step = notches * kWheelUnits * speed * kWheelStep;
        accumulated -= step;
        handled = true;
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
    sceneObjects[kThumb]->Place(static_cast<float>(track.left + offset),
                                track.top + kThumbInset, depth - kThumbLift);
    const int centre = track.left + offset;
    thumb = {centre - kSliderThumbHalfWidth, track.top, centre + kSliderThumbHalfWidth, track.top + kSliderThumbHeight};
}

void UiSlider::MouseMove(Point point) {
    if (state == kDragging) {
        ui->ScalePointFromScreen(&point);
        int along = point.x - grabX;
        SetPosition(lib::Truncate(static_cast<float>(along - track.left) * maxPosition / track.Width()));
    }
    handled = true;
}

void UiScrollbar::SetMaximum(int maximum) {
    if (maximum < 0) maximum = 0;
    maxPosition = maximum;
    moved = true;
    int visible = maximum != 0 ? 1 : 0;
    for (int i = 0; i < kScrollbarSprites; ++i) sceneObjects[i]->visible = visible;
    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}
