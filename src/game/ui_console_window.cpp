// The console window's surface, paint and key input. The text
// side (lines, history depth, placement) is in console_window.cpp. See docs/notes/ui.md
// ("Console window").
#include "console.h"
#include "game_strings.h"
#include "ui.h"
#include "asset_text.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include "lib/str.h"

#include <algorithm>
#include <cstring>
#include <string>
#include "platform/platform.h"

namespace console {

namespace {


// Colours and the strings the window draws (read from the exe: "> " before the input, "+" for
// the scroll marks).
constexpr std::uint32_t kBackground = 0x282828;
constexpr std::uint32_t kSelection = 0x505050;
constexpr std::uint32_t kLineText = 0xB4B4B4;
constexpr std::uint32_t kInputText = 0xFFFFFF;
// Layout pixels (ConsoleWindow::Px scales them): the line pitch and the input strip at the bottom.
constexpr int kLineHeight = 15;
constexpr int kInputHeight = 0x1F;
constexpr std::uint32_t kDrawLine = 0x800;      // DT_NOPREFIX
constexpr std::uint32_t kDrawMark = 0x802;      // DT_NOPREFIX | DT_RIGHT

int Clamp(int value, int high) {
    if (value > high) value = high;
    if (value < 0) value = 0;
    return value;
}

// Width in pixels of the prompt and the first `count` characters of the input in `font`.
int PromptWidth(const UiFont& font, const std::string& input, int count) {
    return TextExtent(font, std::string(game::consoletexts::Prompt()) + str::Left(input, count)).w;
}

constexpr int kHistoryDepth = 30;

// CDC::FillSolidRect of `width` x `height` at (x, y).
void FillArea(UiSurface& surface, int x, int y, int width, int height, std::uint32_t colour) {
    surface.FillRect(Rect{x, y, x + width, y + height}, colour);
}

} // namespace

// A shown window gets a surface over its rectangle (format 4) if it has none, painted from the lines and the
// input; hiding releases the surface.
void ConsoleWindow::Show(bool newShown) {
    shown = newShown;
    if (newShown) {
        if (!surface) {
            surface = new UiSurface(screen, &rect, SurfaceBlend::Opaque);
            surface->PaintWith([this](UiSurface& out) {
                PaintLines(out);
                PaintInput(out);
            });
        }
        ClampScroll();
        ClampCaret();
        return;
    }
    if (UiSurface* currentSurface = surface) {
        delete currentSurface;
    }
    surface = nullptr;
}

// Keys while the window is shown. Shift (0x10) and Ctrl (0x11) are remembered; with
// Ctrl, PgUp/PgDn scroll by a page and Home/End go to the first/last page, otherwise PgUp/PgDn
// scroll by a line and Home/End/Left/Right move the caret (Shift keeps the selection's anchor);
// Up/Down step through the entered commands; Delete removes the selection or the next character.
void ConsoleWindow::OnKeyDown(platform::Key key, int, bool) {
    switch (key) {
    case platform::Key::Shift:
        shift = true;
        return;
    case platform::Key::Control:
        ctrl = true;
        return;
    case platform::Key::PageUp:
        scroll = ctrl ? scroll + visibleLines : scroll + 1;
        break;
    case platform::Key::PageDown:
        scroll = ctrl ? scroll - visibleLines : scroll - 1;
        break;
    case platform::Key::End:
        if (!ctrl) {
            caret = lib::Count(input);
            if (!shift) anchor = caret;
            ClampCaret();
            return;
        }
        scroll = 0;
        break;
    case platform::Key::Home:
        if (!ctrl) {
            caret = 0;
            if (!shift) anchor = 0;
            ClampCaret();
            return;
        }
        {
            int first = static_cast<int>(lines.size()) - visibleLines;
            scroll = first < 0 ? 0 : first;
        }
        break;
    case platform::Key::Left:
    case platform::Key::Right:   // by character
        caret = key == platform::Key::Left ? ::text::PreviousCharacter(input, caret) : ::text::NextCharacter(input, caret);
        if (!shift) anchor = caret;
        ClampCaret();
        return;
    case platform::Key::Up:
    case platform::Key::Down:
        if (key == platform::Key::Up) {
            if (commandIndex < static_cast<int>(commands.size())) ++commandIndex;
        } else if (commandIndex >= 0) {
            --commandIndex;
        }
        if (commandIndex >= 0 && commandIndex < static_cast<int>(commands.size()))
            input = commands[commandIndex];
        else
            input.clear();
        caret = lib::Count(input);
        anchor = caret;
        ClampCaret();
        return;
    case platform::Key::Delete:
        if (anchor == caret) {
            if (caret < static_cast<int>(input.size())) {
                std::string rest = str::Mid(input, ::text::NextCharacter(input, caret));
                input = str::Left(input, caret) + rest;
                ClampCaret();
            }
        } else {
            int low = anchor < caret ? anchor : caret;
            int high = anchor > caret ? anchor : caret;
            std::string rest = str::Mid(input, high);
            input = str::Left(input, low) + rest;
            caret = low;
            anchor = low;
            ClampCaret();
        }
        return;
    default:
        return;
    }
    ClampScroll();
}

// ---- Character input ------------------------------------------------------------------

// Typed characters (code points, stored as UTF-8; the caret is a byte offset at a
// character's start and moves by character). Backspace deletes the selection or the character before the caret,
// Enter enters the line (into the history, at most 30, echoed with "> ", then executed), Escape
// clears the line, Tab does nothing; other characters replace the selection, except while Ctrl is
// held. Rebirth's ImproveConsole skips the history for a line equal to the last one and adds
// Ctrl+A / C / X / V (select all, copy, cut, paste: the first line of the clipboard).
void ConsoleWindow::OnChar(int key, int) {
    int low = caret < anchor ? caret : anchor;
    int high = anchor > caret ? anchor : caret;
    switch (key) {
    case 8: {   // Backspace
        if (anchor != caret) {
            int first = anchor < caret ? anchor : caret;
            int last = anchor > caret ? anchor : caret;
            std::string rest = str::Mid(input, last);
            input = str::Left(input, first) + rest;
            caret = first;
            anchor = first;
        } else {
            if (caret == 0) return;
            const int previous = ::text::PreviousCharacter(input, caret);
            std::string rest = str::Mid(input, caret);
            input = str::Left(input, previous) + rest;
            caret = previous;
            anchor = caret;
        }
        ClampCaret();
        return;
    }
    case 9:
        return;
    case 13: {   // Enter
        // A line equal to the last one is not added to the history again (Rebirth's ImproveConsole).
        bool repeated = !commands.empty() && str::Compare(input, commands[0]) == 0;
        if (!repeated) {
            commands.insert(commands.begin(), input);
            if (commands.size() > kHistoryDepth) commands.pop_back();
        }
        commandIndex = -1;
        input.clear();
        caret = 0;
        anchor = 0;
        ClampCaret();
        AddText((game::consoletexts::Prompt() + commands[0] + game::NewLineText()));
        Execute(commands[0]);
        return;
    }
    case 27:   // Escape
        input.clear();
        caret = 0;
        anchor = 0;
        ClampCaret();
        return;
    default:
        break;
    }
    if (ctrl && !shift) {   // Ctrl+A / C / X / V: select all, copy, cut, paste (Rebirth's ImproveConsole)
        char letter = static_cast<char>(static_cast<std::uint8_t>(key) + 0x60);
        if (letter == 'a') {
            anchor = 0;
            caret = lib::Count(input);
            ClampCaret();
        } else if ((letter == 'c' || letter == 'x') && low != high) {
            const char* text = input.c_str();
            platform::CopyText(std::string_view(text + low, static_cast<std::size_t>(high - low)));   // Rebirth's clipboard (ImproveConsole)
            if (letter == 'x') {
                std::string rest = str::Mid(input, high);
                std::string head = str::Left(input, low);
                input = head + rest;
                caret = low;
                anchor = low;
                ClampCaret();
            }
        } else if (letter == 'v') {
            std::string clip = platform::ClipboardText();
            int cut = str::FindOneOf(clip, "\r\n");
            if (cut >= 0) clip = str::Left(clip, cut);
            std::string rest = str::Mid(input, high);
            std::string head = str::Left(input, low);
            input = (head + clip) + rest;
            int position = static_cast<int>(clip.size()) - (anchor != caret ? 1 : 0) + high;
            position = ::text::CharacterStart(input, position);    // never inside a character
            caret = position;
            anchor = position;
            ClampCaret();
        }
    }
    if (ctrl) return;
    std::string rest = str::Mid(input, high);
    std::string head = str::Left(input, low);
    input = ::text::Typed(head, key) + rest;
    caret = low + ::text::TypedLength(key);
    anchor = caret;
    ClampCaret();
}

void ConsoleWindow::ClampScroll() {
    const int lineCount = static_cast<int>(lines.size());
    if (scroll > lineCount - visibleLines) scroll = lineCount - visibleLines;
    if (scroll < 0) scroll = 0;
}

void ConsoleWindow::ClampCaret() {
    caret = Clamp(caret, lib::Count(input));
    anchor = Clamp(anchor, lib::Count(input));
}

// The area above the input strip filled, the visible lines bottom up, 15 layout pixels apart, with a "+" at the top
// right when there is more above and at the bottom right when scrolled back.
void ConsoleWindow::PaintLines(UiSurface& out) const {
    if (!font) return;
    const int lineCount = static_cast<int>(lines.size());
    const int inputHeight = Px(kInputHeight);
    FillArea(out, 0, 0, width, height - inputHeight, kBackground);
    int line = lineCount - scroll - 1;
    for (int i = 0, y = 0; i < visibleLines && line >= 0; ++i, y += Px(kLineHeight), --line) {
        Rect area{Px(8), height - y - Px(0x2E), width - Px(0x12), height - y - Px(0x1A)};
        out.DrawTextPixels(font, lines[line], area, kDrawLine, kLineText, false);
    }
    if (scroll > 0) {
        Rect area{width - Px(0x12), height - Px(0x2E), width - Px(8), height - inputHeight};
        out.DrawTextPixels(font, game::consoletexts::More(), area, kDrawMark, kLineText, false);
    }
    if (scroll < lineCount - visibleLines) {
        Rect area{width - Px(0x12), Px(8), width - Px(8), Px(0x17)};
        out.DrawTextPixels(font, game::consoletexts::More(), area, kDrawMark, kLineText, false);
    }
}

// The input strip filled, the selection shaded (clipped 16 layout pixels before the right edge), "> " and the input, and
// a 2-layout-pixel caret line when it lies left of that limit.
void ConsoleWindow::PaintInput(UiSurface& out) const {
    if (!font) return;
    const int inputHeight = Px(kInputHeight);
    const int margin = Px(8);
    const int right = width - Px(0x10);
    const int top = height - Px(0x17);
    FillArea(out, 0, height - inputHeight, width, inputHeight, kBackground);
    int caretX = PromptWidth(font, input, caret);
    if (anchor != caret) {
        int anchorX = PromptWidth(font, input, anchor);
        int low = caretX < anchorX ? caretX : anchorX;
        int high = caretX > anchorX ? caretX : anchorX;
        if (high > right) high = right;
        if (low < high) FillArea(out, low + margin, top, high - low, Px(0xF), kSelection);
    }
    Rect area{margin, top, width - margin, height - Px(3)};
    out.DrawTextPixels(font, std::string(game::consoletexts::Prompt()) + input, area, kDrawLine, kInputText, false);
    if (caretX < right) out.Line({caretX + margin, top}, {caretX + margin, height - margin}, std::max(Px(2), 1), kInputText);
}

} // namespace console

void console::ConsoleWindow::Blit() {
    if (surface) surface->Blit();
}
