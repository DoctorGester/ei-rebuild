// The plain scroll bar's constructor, position and layout (CUiScrollbar) and the options slider's constructor;
// also the thumb drags, the wheel and the slider's position. See ui.h and docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"

#include <cstdint>

namespace {

constexpr int kTextureKind = 3;

// The thumb sits slightly in front of the track (0.1) and 5 to the right of the track's left edge.
constexpr float kThumbLift = 0.1f;
constexpr float kThumbInset = 5.0f;

enum Sprite { kTrack, kThumb, kUpArrow, kDownArrow };
enum Region { kUpRegion, kDownRegion, kTrackRegion };

CUiMesh* Sprite(CUiWindow* window, int index) { return window->SceneMesh(index); }
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
CUiScrollbar::CUiScrollbar(CInterface3D* ui, RECT rect, float depth, int maximum, float speed) : CUiWindow(ui) {
    maxPosition_ = maximum;
    speed_ = speed;
    depth_ = depth;
    moved_ = true;
    state_ = 0;
    position_ = -1;
    LoadTexture("Scrollbar", kTextureKind);
    for (int i = 0; i < 4; ++i) AddBlankSprite();
    for (int i = 0; i < 3; ++i) AddRegion();
    CUiScrollbar::SetRect(&rect);
    CUiScrollbar::SetPosition(0);
    accumulated_ = 0.0f;
}

void CUiScrollbar::SetPosition(int position) {
    if (position < 0) position = 0;
    int maximum = maxPosition_;
    if (position > maximum) position = maximum;
    moved_ = true;
    if (position_ == position) return;
    position_ = position;
    // Truncated by _ftol. A zero maximum divides 0 by 0 (the position is clamped to 0);
    // the game's _ftol stores the NaN as a 64-bit integer (0x8000000000000000) and returns its low
    // half, 0, where a C++ cast would give 0x80000000.
    int offset = maximum == 0 ? 0
                              : static_cast<int>(static_cast<double>((track_.bottom - track_.top) * position) / maximum);
    Sprite(this, kThumb)->Place(static_cast<float>(track_.left + static_cast<double>(kThumbInset)),
                                static_cast<float>(static_cast<double>(offset) + track_.top), depth_ - kThumbLift);
    thumb_.left = track_.left;
    thumb_.right = track_.left + 10;
    int centre = track_.top + offset;
    thumb_.top = centre - 14;
    thumb_.bottom = centre + 14;
}

void CUiScrollbar::SetRect(const RECT* rect) {
    rect_ = *rect;
    track_.left = rect->right - 10;
    track_.right = rect->right;
    track_.top = rect->top + 26;
    track_.bottom = rect->bottom - 26;

    // The sprites (SetRectangle and the regions' SetRect are Rebirth-patched: through their
    // addresses).
    Sprite(this, kTrack)->SetRectangle(depth_, 100.0f, AsFloat(rect->right - 8), AsFloat(rect->top + 4),
                                       AsFloat(rect->right - 2), AsFloat(rect->bottom - 4), 8, 4, 0x20, 0xFC);
    Sprite(this, kThumb)->SetRectangle(depth_ - kThumbLift, 100.0f, AsFloat(rect->right - 10), AsFloat(rect->top),
                                       AsFloat(rect->right), AsFloat(rect->top + 0x1C), 0x28, 8, 0x50, 0x78);
    Sprite(this, kUpArrow)->SetRectangle(depth_, 100.0f, AsFloat(rect->right - 10), AsFloat(rect->top),
                                         AsFloat(rect->right), AsFloat(rect->top + 0xC), 0x28, 0xB0, 0x50, 0x80);
    Sprite(this, kDownArrow)->SetRectangle(depth_, 100.0f, AsFloat(rect->right - 10), AsFloat(rect->bottom - 0xC),
                                           AsFloat(rect->right), AsFloat(rect->bottom), 0x28, 0x80, 0x50, 0xB0);

    regions_[kUpRegion]->SetRect(rect->right - 10, rect->top, rect->right, rect->top + 0xC);
    regions_[kDownRegion]->SetRect(rect->right - 10, rect->bottom - 0xC, rect->right, rect->bottom);
    regions_[kTrackRegion]->SetRect(rect->left, rect->top, rect->right - 10, rect->bottom);

    int position = position_;
    position_ = position - 1;
    SetPosition(position);
}

// --- A-H20: dragging, the wheel, the slider's position --------------------------------------------

void CUiScrollbar::MouseMove(int, int x, int y) {
    if (state_ == kDragging) {
        POINT point{x, y};
        interface_->ScalePointFromScreenEntry(&point);
        int along = point.y - grabY_;
        SetPosition(Truncate(static_cast<double>(along - track_.top) * maxPosition_ / (track_.bottom - track_.top)));
    }
    handled_ = true;
}

void CUiScrollbar::Wheel(int, int delta, int x, int y) {
    if (RegionAt(x, y) == kTrackRegion) {
        double step = static_cast<double>(static_cast<short>(delta)) * speed_ * kWheelStep;
        accumulated_ = static_cast<float>(accumulated_ - step);
        handled_ = true;
    }
}

void CUiSlider::SetPosition(int position) {
    if (position < 0) position = 0;
    int maximum = maxPosition_;
    if (position > maximum) position = maximum;
    moved_ = true;
    if (position_ == position) return;
    position_ = position;
    // Truncated by _ftol (a zero maximum: 0 / 0, which _ftol makes 0).
    int offset = Truncate(static_cast<double>((track_.right - track_.left) * position) / maximum);
    Sprite(this, kThumb)->Place(static_cast<float>(track_.left + offset),
                                static_cast<float>(track_.top + static_cast<double>(kThumbInset)), depth_ - kThumbLift);
    thumb_.top = track_.top;
    thumb_.bottom = track_.top + kSliderThumbHeight;
    int centre = track_.left + offset;
    thumb_.left = centre - kSliderThumbHalfWidth;
    thumb_.right = centre + kSliderThumbHalfWidth;
}

void CUiSlider::MouseMove(int, int x, int y) {
    if (state_ == kDragging) {
        POINT point{x, y};
        interface_->ScalePointFromScreenEntry(&point);
        int along = point.x - grabX_;
        SetPosition(Truncate(static_cast<double>(along - track_.left) * maxPosition_ / (track_.right - track_.left)));
    }
    handled_ = true;
}
