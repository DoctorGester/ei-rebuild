#pragma once

// The message box's edit field.

#include "ui.h"

// The classes of typed characters a field admits (UiEditField::charClasses).
constexpr std::uint32_t kClassDigit = 1;
constexpr std::uint32_t kClassLetter = 2;      // letters and the space
constexpr std::uint32_t kClassOther = 4;
constexpr std::uint32_t kClassSeparator = 8;   // '.' and ':'

// The message box's edit field.
class UiEditField : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    // While focused. The first edit key starts editing (Backspace / Delete from an empty text, the others
    // with the caret at the end); the caret is not clamped here.
    void KeyDown(UiEvent& event);
    // The caret kept within the text (after every change, as the original's redraw did).
    void ClampCaret();
    // The surface's paint: the text (centred with mode 1), then a 1-pixel caret line in caretColour after the
    // caret's characters while it falls inside the field.
    void PaintContents(UiSurface& surface) const;
    // Every character class, font 1, at most 50 characters; focused. caret, editing and changed are left
    // as they are.
    UiEditField(Rect rect);
    ~UiEditField() override;
    // While focused and without Ctrl, a character of an allowed class: the first one starts editing from an
    // empty text; inserted while shorter than maxLength and narrower than area.
    void Char(UiEvent& event);
    // SetText: not editing, changed, the caret at its end.
    void SetLook(std::uint32_t charClasses, char mode, int font, int maxLength, std::uint32_t colour, std::uint32_t caretColour);
    void SetText(std::string_view text);
    void SetFocused(bool focused);

    Rect area;                       // the surface's layout rectangle
    std::uint32_t charClasses = kClassDigit | kClassLetter | kClassOther | kClassSeparator;  // classes Char takes (1 digits, 2 letters / space, 4 others, 8 '.' ':')
    std::uint8_t centred = 0;        // SetLook's mode
    int font = 1;                    // interface font
    int maxLength = 50;
    std::uint32_t colour = 0xFFFFFF;      // the text's colour
    std::uint32_t caretColour = 0xFF;     // SetLook's last argument (0xFF from the constructor)
    std::string text;
    int caret = 0;                   // caret position in text (bytes, at a character's start: U4)
    bool editing = false;            // an edit key was pressed (the first one sets it)
    bool focused = true;             // keys are taken only while set
    bool changed = false;
};
