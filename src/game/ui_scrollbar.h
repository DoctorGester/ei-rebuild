#pragma once

// The scroll bar and the options window's slider.

#include "ui.h"

// A scroll bar (a scrolling text has one).
class UiScrollbar : public UiWindow {
public:
    int Position() const { return position; }
    // Whether the position moved since the last call (the flag cleared).
    bool TakeMoved();
    // The position, the moved flag cleared unread.
    int TakePosition();
    // `speed` in lines per second; position 0.
    UiScrollbar(Interface3D* ui, Rect rect, float depth, int maximum, float speed);
    ~UiScrollbar() override;
    // Scrolls while an arrow is held: whole steps of the accumulated amount move the position.
    void Update() override;
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);   // thumb drag or an arrow
    void LeftUp(UiEvent& event);
    // Drags the thumb (state 1). The event is used either way.
    void MouseMove(UiEvent& event);
    // Over the track: delta (low 16 bits) * speed / 120 into the accumulated amount.
    void Wheel(UiEvent& event);
    // Clamped to 0..maximum, moved set.
    virtual void SetPosition(int position);
    // The track is the right 10 pixels less 26 at either end. The position is set again (one below first,
    // so it counts as a change). The slider has its own (UiSlider::SetRect).
    void SetRect(const Rect* rect);
    // Negative: 0. The sprites show only for a maximum above 0; the position is set again as SetRect does.
    void SetMaximum(int maximum);
    int maxPosition = 0;
    float speed = 0.0f;              // lines per second while an arrow is held (/16)
    int position = 0;
    // Layout coordinates (the options slider: left + 26 .. right - 26 over the bottom 10 pixels).
    Rect track;
    Rect thumb;                      // layout coordinates
    int grabX = 0;                   // mouse offset from the thumb centre while dragging
    int grabY = 0;
    float depth = 0.0f;              // the sprites' depth
    bool moved = false;
    int state = 0;                   // 0 idle, 1 dragging the thumb, 2 up, 3 down
    float accumulated = 0.0f;        // scrolled amount not yet applied

    // The regions, in their creation order: the arrows (on the slider: left, right) and the track.
    UiRegion* upButton = nullptr;
    UiRegion* downButton = nullptr;
    UiRegion* trackArea = nullptr;
    // The sprites, in their drawing order (the slider turns them a quarter: its up arrow on the right).
    UiMesh* trackSprite = nullptr;
    UiMesh* thumbSprite = nullptr;
    UiMesh* upArrow = nullptr;
    UiMesh* downArrow = nullptr;
};

// The options window's horizontal slider, laid out along the bottom of its rectangle.
class UiSlider : public UiScrollbar {
public:
    UiSlider(Interface3D* ui, Rect rect, float depth, int maximum, float speed)
        : UiScrollbar(ui, rect, depth, maximum, speed) {}
    // Along the track's width; the thumb 28 wide and 10 high.
    void SetPosition(int position) override;
    void OnEvent(UiEvent& event) override;
    // The scroll bar's drag along x.
    void MouseMove(UiEvent& event);
    // Along the bottom 10 pixels, the sprites turned a quarter; the position set again as the scroll bar's does.
    void SetRect(const Rect* rect);
};
