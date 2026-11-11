// The message box's edit field: its key handler, constructor and character
// handler, and its paint (Rebirth's WideScreen). See ui_edit_field.h (UiEditField) and
// docs/notes/ui.md, section 8.
#include "game/ui/ui_edit_field.h"
#include "engine/core/asset_text.h"
#include "game/ui/ui.h"
#include "game/ui/ui_screens.h"
#include "lib/containers.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"
#include "render/text_format.h"
#include "game/ui/screen_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace {

// The class of typed character `key` that charClasses admits or not. A character past ASCII is a code point: a
// letter when Windows calls it one.
std::uint32_t CharClass(int key) {
    if (key >= 0x80) return platform::IsLetter(static_cast<std::uint32_t>(key)) ? kClassLetter : kClassOther;
    const char c = static_cast<char>(key);
    if (text::IsDigit(key) != 0) return kClassDigit;
    if (text::IsAlpha(key) != 0 || c == ' ') return kClassLetter;
    if (c == '.' || c == ':') return kClassSeparator;
    return kClassOther;
}
} // namespace

void UiEditField::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::Char: Char(event); break;
    default: break;
    }
}

void UiEditField::KeyDown(UiEvent& event) {
    if (!focused) return;
    auto& currentText = text;
    switch (event.key) {
    case platform::Key::Backspace:
    case platform::Key::Delete:
        // The first edit key of a field starts from an empty text.
        if (!editing) {
            editing = true;
            currentText.clear();
            caret = 0;
            changed = true;
        }
        // By character (a UTF-8 character's bytes).
        if (event.key == platform::Key::Backspace) {
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
    case platform::Key::End:
    case platform::Key::Home:
    case platform::Key::Left:
    case platform::Key::Right:
        // The first edit key of a field keeps the text, the caret at its end.
        if (!editing) {
            editing = true;
            caret = lib::Count(currentText);
        }
        if (event.key == platform::Key::End)
            caret = lib::Count(currentText);
        else if (event.key == platform::Key::Home)
            caret = 0;
        else if (event.key == platform::Key::Left)
            caret = text::PreviousCharacter(currentText, caret);
        else
            caret = text::NextCharacter(currentText, caret);
        break;
    default:
        return;
    }
    ClampCaret();
    event.handled = true;
}

// The window's slots; its keys and characters.
UiEditField::UiEditField(Rect rect) {
    ClearSurfaces();
    area = rect;
    UiSurface* surface = AddSurface(std::make_unique<UiSurface>(&area, SurfaceBlend::Keyed));
    surface->PaintWith([this](UiSurface& out) { PaintContents(out); });
    ClampCaret();
}

void UiEditField::Char(UiEvent& event) {
    if (!focused) return;
    const int character = event.character;
    if (character == '\b' || character == '\t' || character == '\r' || character == '\x1B') return;
    if (g_interface.ctrlDown) return;
    if ((charClasses & CharClass(character)) == 0) return;
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
    std::string typed = text::Typed(head, character) + tail;
    if (text::CharacterCount(currentText) < maxLength && g_interface.TextExtent(typed, font).w < area.Width()) {
        currentText = typed;
        changed = true;
        caret += text::TypedLength(character);
    }
    ClampCaret();
    event.handled = true;
}

// --- the look, the text and the focus ------------------------------------------------------------

void UiEditField::SetLook(std::uint32_t newCharClasses, char mode, int newFont, int newMaxLength, std::uint32_t newColour,
                           std::uint32_t newCaretColour) {
    charClasses = newCharClasses;
    centred = static_cast<std::uint8_t>(mode);
    font = newFont;
    maxLength = newMaxLength;
    colour = newColour;
    caretColour = newCaretColour;
    ClampCaret();
}

void UiEditField::SetText(std::string_view newText) {
    editing = false;
    text = newText;
    changed = true;
    caret = lib::Count(text);
    ClampCaret();
}

void UiEditField::SetFocused(bool newFocused) { focused = newFocused; }

// --- the text and the caret painted ---------------------------------------------------------------

void UiEditField::ClampCaret() {
    if (caret > static_cast<int>(text.size())) caret = lib::Count(text);
    if (caret < 0) caret = 0;
}

void UiEditField::PaintContents(UiSurface& surface) const {
    const std::string& currentText = text;
    const int width = area.Width();
    const int height = area.Height();
    Rect box{0, 0, width, height};
    surface.DrawTextIn(currentText, box, TextStyle{font, colour, centred ? render::text::Format::NoPrefix | render::text::Format::Center : render::text::Format::NoPrefix});
    // The caret stands after the first caret characters; a centred text starts half its width left
    // of the middle.
    int caretX = g_interface.TextExtent(str::Left(currentText, caret), font).w;
    if (centred) caretX += width / 2 - g_interface.TextExtent(currentText, font).w / 2;
    if (caretX >= 0 && caretX < width) {
        Point top{caretX, 0};
        Point bottom{caretX, height};
        // WideScreen: without the margin.
        top = screen_layout::ToScreenNoMargin(top);
        bottom = screen_layout::ToScreenNoMargin(bottom);
        surface.Line(top, bottom, 1, caretColour);
    }
}

UiEditField::~UiEditField() {}
