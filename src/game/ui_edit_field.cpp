// The message box's edit field: its key handler, constructor and character
// handler, and its drawing (Rebirth's WideScreen behind its flag). See ui.h (CUiEditField) and
// docs/notes/items_spells.md
#include "ui.h"
#include "ui_screens.h"

#include "asset_text.h"

#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstddef>
#include <cstring>
#include <new>

namespace {
constexpr int kKeyBackspace = 0x08;
constexpr int kKeyEnd = 0x23;
constexpr int kKeyHome = 0x24;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDelete = 0x2E;

constexpr int kSurfaceFormat = 5;
constexpr int kKeyTab = 0x09;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;
constexpr std::uint32_t kClassDigit = 1;
constexpr std::uint32_t kClassLetter = 2;      // letters and the space
constexpr std::uint32_t kClassOther = 4;
constexpr std::uint32_t kClassSeparator = 8;   // '.' and ':'

int TextWidth(CInterface3D* ui, const std::string& text, int font) {
    SIZE size;
    ui->TextExtent(&size, text, font);
    return size.cx;
}

// The class of typed character `key` that charClasses_ admits or not. A character past ASCII is a code point: a
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

void CUiEditField::OnKeyDown(int key, int, int) {
    if (!focused_) return;
    auto& text = text_;
    switch (key) {
    case kKeyBackspace:
    case kKeyDelete:
        // The first edit key of a field starts from an empty text.
        if (!editing_) {
            editing_ = true;
            text.clear();
            caret_ = 0;
            changed_ = true;
        }
        // By character (a UTF-8 character's bytes).
        if (key == kKeyBackspace) {
            if (caret_ > 0) {
                const int previous = text::PreviousCharacter(text, caret_);
                std::string tail = str::Mid(text, caret_);
                std::string head = str::Left(text, previous);
                text = head + tail;
                caret_ = previous;
                changed_ = true;
            }
        } else if (caret_ < static_cast<int>(text.size())) {
            std::string tail = str::Mid(text, text::NextCharacter(text, caret_));
            std::string head = str::Left(text, caret_);
            text = head + tail;
            changed_ = true;
        }
        break;
    case kKeyEnd:
    case kKeyHome:
    case kKeyLeft:
    case kKeyRight:
        // The first edit key of a field keeps the text, the caret at its end.
        if (!editing_) {
            editing_ = true;
            caret_ = text.size();
        }
        if (key == kKeyEnd)
            caret_ = text.size();
        else if (key == kKeyHome)
            caret_ = 0;
        else if (key == kKeyLeft)
            caret_ = text::PreviousCharacter(text, caret_);
        else
            caret_ = text::NextCharacter(text, caret_);
        break;
    default:
        return;
    }
    DrawContentsVanilla();
    handled_ = true;
}

// The window's slots; the input side's keys and characters.
CUiEditField::CUiEditField(CInterface3D* ui, RECT rect) : CUiWindow(ui) {
    charClasses_ = kClassDigit | kClassLetter | kClassOther | kClassSeparator;
    centred_ = 0;
    font_ = 1;
    maxLength_ = 50;
    colour_ = 0xFFFFFF;
    caretColour_ = 0xFF;
    ClearSurfaces();
    area_ = rect;
    CUiSurface* surface = new CUiSurface(interface_, &area_, kSurfaceFormat);
    AddSurface(surface);
    DrawContentsVanilla();
    focused_ = true;
}

void CUiEditField::OnChar(int key, int, int) {
    if (!focused_) return;
    if (key == kKeyBackspace || key == kKeyTab || key == kKeyEnter || key == kKeyEscape) return;
    if (interface_->ctrlDown_) return;
    if ((charClasses_ & CharClass(key)) == 0) return;
    auto& text = text_;
    // The first character of a field starts from an empty text.
    if (!editing_) {
        editing_ = true;
        text.clear();
        caret_ = 0;
        changed_ = true;
    }
    std::string tail = str::Mid(text, caret_);
    std::string head = str::Left(text, caret_);
    std::string typed = text::Typed(head, key) + tail;
    if (text::CharacterCount(text) < maxLength_ && TextWidth(interface_, typed, font_) < area_.right - area_.left) {
        text = typed;
        changed_ = true;
        caret_ += text::TypedLength(key);
    }
    DrawContentsVanilla();
    handled_ = true;
}

// --- the look, the text and the focus ------------------------------------------------------------

void CUiEditField::SetLook(std::uint32_t charClasses, char mode, int font, int maxLength, COLORREF colour,
                           COLORREF caretColour) {
    charClasses_ = charClasses;
    centred_ = static_cast<std::uint8_t>(mode);
    font_ = font;
    maxLength_ = maxLength;
    colour_ = colour;
    caretColour_ = caretColour;
    DrawContentsVanilla();
}

void CUiEditField::SetText(std::string_view text) {
    editing_ = false;
    text_ = text;
    changed_ = true;
    caret_ = text_.size();
    DrawContentsVanilla();
}

void CUiEditField::SetFocused(bool focused) { focused_ = focused; }

// --- the text and the caret drawn ----------------------------------------------------------------

void CUiEditField::DrawContentsVanilla() {
    auto& text = text_;
    if (caret_ > static_cast<int>(text.size())) caret_ = text.size();
    if (caret_ < 0) caret_ = 0;
    CUiSurface* surface = surfaces_[0];
    if (!surface->holder_) return;
    surface->Clear();
    const int width = area_.right - area_.left;
    const int height = area_.bottom - area_.top;
    RECT box{0, 0, width, height};
    surface->DrawTextIn(text, &box, font_, colour_, centred_ ? DT_NOPREFIX | DT_CENTER : DT_NOPREFIX, 1);
    // The caret stands after the first caret_ characters; a centred text starts half its width left
    // of the middle.
    int caretX = TextWidth(interface_, str::Left(text, caret_), font_);
    if (centred_) caretX += width / 2 - TextWidth(interface_, text, font_) / 2;
    if (caretX >= 0 && caretX < width) {
        POINT top{caretX, 0};
        POINT bottom{caretX, height};
        // WideScreen: without the margin.
        interface_->CallScalePointToScreenNoMargin(&top);
        interface_->CallScalePointToScreenNoMargin(&bottom);
        surface->Line(top.x, top.y, bottom.x, bottom.y, 1, caretColour_);
    }
}
