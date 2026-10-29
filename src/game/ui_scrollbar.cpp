// The plain scroll bar's constructor, position and layout (CUiScrollbar) and the options slider's constructor;
// also the thumb drags, the wheel and the slider's position. See ui.h and docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"

#include <cstdint>

namespace {


// The thumb sits slightly in front of the track (0.1) and 5 to the right of the track's left edge.
constexpr float kThumbLift = 0.1f;
constexpr float kThumbInset = 5.0f;

enum Sprite { kTrack, kThumb, kUpArrow, kDownArrow };
enum Region { kUpRegion, kDownRegion, kTrackRegion };

float AsFloat(int value) { return static_cast<float>(value); }

constexpr int kDragging = 1;
constexpr int kSliderThumbHalfWidth = 14;
constexpr int kSliderThumbHeight = 10;

// The game's _ftol: the low half of a 64-bit truncation (NaN and overflow give 0, where a C++ cast
// to int gives 0x80000000).
int Truncate(double value) { return static_cast<int>(static_cast<std::int64_t>(value)); }
// The wheel's step per unit of delta: 1/120.
constexpr float kWheelStep = 0.008333334f;

} // namespace

// The virtuals of the scroll bar and of the slider (the slider's create / update virtuals and the drag along
// x are its own).
CUiScrollbar::CUiScrollbar(CInterface3D* ui, RECT rect, float newDepth, int maximum, float newSpeed) : CUiWindow(ui) {
    maxPosition = maximum;
    speed = newSpeed;
    depth = newDepth;
    moved = true;
    state = 0;
    position = -1;
    LoadTexture("Scrollbar", TextureKind::Interface);
    for (int i = 0; i < 4; ++i) AddBlankSprite();
    for (int i = 0; i < 3; ++i) AddRegion();
    CUiScrollbar::SetRect(&rect);
    CUiScrollbar::SetPosition(0);
    accumulated = 0.0f;
}

void CUiScrollbar::SetPosition(int newPosition) {
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
                              : static_cast<int>(static_cast<double>((track.bottom - track.top) * newPosition) / maximum);
    SceneMesh(kThumb)->Place(static_cast<float>(track.left + static_cast<double>(kThumbInset)),
                                static_cast<float>(static_cast<double>(offset) + track.top), depth - kThumbLift);
    thumb.left = track.left;
    thumb.right = track.left + 10;
    int centre = track.top + offset;
    thumb.top = centre - 14;
    thumb.bottom = centre + 14;
}

void CUiScrollbar::SetRect(const RECT* newRect) {
    rect = *newRect;
    track.left = newRect->right - 10;
    track.right = newRect->right;
    track.top = newRect->top + 26;
    track.bottom = newRect->bottom - 26;

    // The sprites (SetRectangle and the regions' SetRect are Rebirth-patched: through their
    // addresses).
    SceneMesh(kTrack)->SetRectangle(depth, 100.0f, AsFloat(newRect->right - 8), AsFloat(newRect->top + 4),
                                       AsFloat(newRect->right - 2), AsFloat(newRect->bottom - 4), 8, 4, 0x20, 0xFC);
    SceneMesh(kThumb)->SetRectangle(depth - kThumbLift, 100.0f, AsFloat(newRect->right - 10), AsFloat(newRect->top),
                                       AsFloat(newRect->right), AsFloat(newRect->top + 0x1C), 0x28, 8, 0x50, 0x78);
    SceneMesh(kUpArrow)->SetRectangle(depth, 100.0f, AsFloat(newRect->right - 10), AsFloat(newRect->top),
                                         AsFloat(newRect->right), AsFloat(newRect->top + 0xC), 0x28, 0xB0, 0x50, 0x80);
    SceneMesh(kDownArrow)->SetRectangle(depth, 100.0f, AsFloat(newRect->right - 10), AsFloat(newRect->bottom - 0xC),
                                           AsFloat(newRect->right), AsFloat(newRect->bottom), 0x28, 0x80, 0x50, 0xB0);

    regions[kUpRegion]->SetRect(newRect->right - 10, newRect->top, newRect->right, newRect->top + 0xC);
    regions[kDownRegion]->SetRect(newRect->right - 10, newRect->bottom - 0xC, newRect->right, newRect->bottom);
    regions[kTrackRegion]->SetRect(newRect->left, newRect->top, newRect->right - 10, newRect->bottom);

    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}

// --- A-H20: dragging, the wheel, the slider's position --------------------------------------------

void CUiScrollbar::MouseMove(int, int x, int y) {
    if (state == kDragging) {
        POINT point{x, y};
        ui->ScalePointFromScreenEntry(&point);
        int along = point.y - grabY;
        SetPosition(Truncate(static_cast<double>(along - track.top) * maxPosition / (track.bottom - track.top)));
    }
    handled = true;
}

void CUiScrollbar::Wheel(int, int delta, int x, int y) {
    if (RegionAt(x, y) == kTrackRegion) {
        double step = static_cast<double>(static_cast<short>(delta)) * speed * kWheelStep;
        accumulated = static_cast<float>(accumulated - step);
        handled = true;
    }
}

void CUiSlider::SetPosition(int newPosition) {
    if (newPosition < 0) newPosition = 0;
    int maximum = maxPosition;
    if (newPosition > maximum) newPosition = maximum;
    moved = true;
    if (position == newPosition) return;
    position = newPosition;
    // Truncated by _ftol (a zero maximum: 0 / 0, which _ftol makes 0).
    int offset = Truncate(static_cast<double>((track.right - track.left) * newPosition) / maximum);
    SceneMesh(kThumb)->Place(static_cast<float>(track.left + offset),
                                static_cast<float>(track.top + static_cast<double>(kThumbInset)), depth - kThumbLift);
    thumb.top = track.top;
    thumb.bottom = track.top + kSliderThumbHeight;
    int centre = track.left + offset;
    thumb.left = centre - kSliderThumbHalfWidth;
    thumb.right = centre + kSliderThumbHalfWidth;
}

void CUiSlider::MouseMove(int, int x, int y) {
    if (state == kDragging) {
        POINT point{x, y};
        ui->ScalePointFromScreenEntry(&point);
        int along = point.x - grabX;
        SetPosition(Truncate(static_cast<double>(along - track.left) * maxPosition / (track.right - track.left)));
    }
    handled = true;
}
