// The message box's edit field: its key handler, constructor and character
// handler, and its drawing (Rebirth's WideScreen behind its flag). See ui.h (UiEditField) and
// docs/notes/items_spells.md
#include "ui.h"
#include "ui_screens.h"

#include "asset_text.h"

#include <cstdint>
#include <string>
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstddef>
#include <cstring>
#include <new>
#include "lib/win_user.h"

namespace {
constexpr int kKeyBackspace = 0x08;
constexpr int kKeyEnd = 0x23;
constexpr int kKeyHome = 0x24;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDelete = 0x2E;

constexpr int kKeyTab = 0x09;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;
constexpr std::uint32_t kClassDigit = 1;
constexpr std::uint32_t kClassLetter = 2;      // letters and the space
constexpr std::uint32_t kClassOther = 4;
constexpr std::uint32_t kClassSeparator = 8;   // '.' and ':'

// The class of typed character `key` that charClasses admits or not. A character past ASCII is a code point: a
// letter when Windows calls it one.
std::uint32_t CharClass(int key) {
    if (key >= 0x80) return key <= 0xFFFF && IsCharAlphaW(static_cast<WCHAR>(key)) ? kClassLetter : kClassOther;
    const char c = static_cast<char>(key);
    if (text::IsDigit(key) != 0) return kClassDigit;
    if (text::IsAlpha(key) != 0 || c == ' ') return kClassLetter;
    if (c == '.' || c == ':') return kClassSeparator;
    return kClassOther;
}

} // namespace

void UiEditField::KeyDown(int key, int, int) {
    if (!focused) return;
    auto& currentText = text;
    switch (key) {
    case kKeyBackspace:
    case kKeyDelete:
        // The first edit key of a field starts from an empty text.
        if (!editing) {
            editing = true;
            currentText.clear();
            caret = 0;
            changed = true;
        }
        // By character (a UTF-8 character's bytes).
        if (key == kKeyBackspace) {
            if (caret > 0) {
                const int previous = text::PreviousCharacter(currentText, caret);
                std::string tail = str::Mid(currentText, caret);
                std::string head = str::Left(currentText, previous);
                currentText = head + tail;
                caret = previous;
                changed = true;
            }
        } else if (caret < static_cast<int>(currentText.size())) {
            std::string tail = str::Mid(currentText, text::NextCharacter(currentText, caret));
            std::string head = str::Left(currentText, caret);
            currentText = head + tail;
            changed = true;
        }
        break;
    case kKeyEnd:
    case kKeyHome:
    case kKeyLeft:
    case kKeyRight:
        // The first edit key of a field keeps the text, the caret at its end.
        if (!editing) {
            editing = true;
            caret = currentText.size();
        }
        if (key == kKeyEnd)
            caret = currentText.size();
        else if (key == kKeyHome)
            caret = 0;
        else if (key == kKeyLeft)
            caret = text::PreviousCharacter(currentText, caret);
        else
            caret = text::NextCharacter(currentText, caret);
        break;
    default:
        return;
    }
    DrawContentsVanilla();
    handled = true;
}

// The window's slots; the input side's keys and characters.
UiEditField::UiEditField(Interface3D* ownerUi, RECT rect) : UiWindow(ownerUi) {
    charClasses = kClassDigit | kClassLetter | kClassOther | kClassSeparator;
    centred = 0;
    font = 1;
    maxLength = 50;
    colour = 0xFFFFFF;
    caretColour = 0xFF;
    ClearSurfaces();
    area = rect;
    UiSurface* surface = new UiSurface(ui, &area, SurfaceBlend::Keyed);
    AddSurface(surface);
    DrawContentsVanilla();
    focused = true;
}

void UiEditField::Char(int key, int, int) {
    if (!focused) return;
    if (key == kKeyBackspace || key == kKeyTab || key == kKeyEnter || key == kKeyEscape) return;
    if (ui->ctrlDown) return;
    if ((charClasses & CharClass(key)) == 0) return;
    auto& currentText = text;
    // The first character of a field starts from an empty text.
    if (!editing) {
        editing = true;
        currentText.clear();
        caret = 0;
        changed = true;
    }
    std::string tail = str::Mid(currentText, caret);
    std::string head = str::Left(currentText, caret);
    std::string typed = text::Typed(head, key) + tail;
    if (text::CharacterCount(currentText) < maxLength && ui->TextExtent(typed, font).cx < area.right - area.left) {
        currentText = typed;
        changed = true;
        caret += text::TypedLength(key);
    }
    DrawContentsVanilla();
    handled = true;
}

// --- the look, the text and the focus ------------------------------------------------------------

void UiEditField::SetLook(std::uint32_t newCharClasses, char mode, int newFont, int newMaxLength, COLORREF newColour,
                           COLORREF newCaretColour) {
    charClasses = newCharClasses;
    centred = static_cast<std::uint8_t>(mode);
    font = newFont;
    maxLength = newMaxLength;
    colour = newColour;
    caretColour = newCaretColour;
    DrawContentsVanilla();
}

void UiEditField::SetText(std::string_view newText) {
    editing = false;
    text = newText;
    changed = true;
    caret = text.size();
    DrawContentsVanilla();
}

void UiEditField::SetFocused(bool newFocused) { focused = newFocused; }

// --- the text and the caret drawn ----------------------------------------------------------------

void UiEditField::DrawContentsVanilla() {
    auto& currentText = text;
    if (caret > static_cast<int>(currentText.size())) caret = currentText.size();
    if (caret < 0) caret = 0;
    UiSurface* surface = surfaces[0];
    if (!surface->holder) return;
    surface->Clear();
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    RECT box{0, 0, width, height};
    surface->DrawTextIn(currentText, &box, font, colour, centred ? DT_NOPREFIX | DT_CENTER : DT_NOPREFIX, 1);
    // The caret stands after the first caret characters; a centred text starts half its width left
    // of the middle.
    int caretX = ui->TextExtent(str::Left(currentText, caret), font).cx;
    if (centred) caretX += width / 2 - ui->TextExtent(currentText, font).cx / 2;
    if (caretX >= 0 && caretX < width) {
        POINT top{caretX, 0};
        POINT bottom{caretX, height};
        // WideScreen: without the margin.
        ui->CallScalePointToScreenNoMargin(&top);
        ui->CallScalePointToScreenNoMargin(&bottom);
        surface->Line(top.x, top.y, bottom.x, bottom.y, 1, caretColour);
    }
}
