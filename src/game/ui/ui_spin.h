#pragma once

// The options window's spin control.

#include "game/ui/ui.h"

// A spin control of the options window: a window stepping through its texts (the on / off settings).
class UiSpin : public UiWindow {
public:
    UiSpin(Rect rect, const char* text);
    ~UiSpin() override;
    void AddText(std::string_view text);
    int Value() const { return value; }
    // Whether it changed since the last call (the flag cleared).
    bool TakeChanged();
    // The value, the changed flag cleared unread.
    int TakeValue();
    // 0 when not below the text count (a negative one is kept).
    void SetValue(int value);
    // The value `by` steps on, wrapping both ways.
    void Step(int by);
    void OnEvent(UiEvent& event) override;
    // The next value, wrapping.
    void LeftDown(UiEvent& event);

    std::vector<std::string> texts;
    int value = 0;
    bool changed = false;
    Rect layout;                     // the constructor's rectangle
};
