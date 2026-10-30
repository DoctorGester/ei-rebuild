// The console window's surface, drawing and key input. The text
// side (lines, history depth, placement) is in console_window.cpp. See docs/notes/ui.md
// ("Console window").
#include "console.h"
#include "game_strings.h"
#include "ui.h"
#include "asset_text.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include "ei/rebirth.h"
#include "lib/str.h"

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
constexpr int kLineHeight = 15;
constexpr int kInputHeight = 0x1F;     // the input strip at the bottom
constexpr std::uint32_t kDrawLine = 0x800;      // DT_NOPREFIX
constexpr std::uint32_t kDrawMark = 0x802;      // DT_NOPREFIX | DT_RIGHT

int Clamp(int value, int high) {
    if (value > high) value = high;
    if (value < 0) value = 0;
    return value;
}

// Width in pixels of the prompt and the first `count` characters of the input in `font`.
int PromptWidth(const UiFont& font, const std::string& input, int count) {
    return TextExtent(font, std::string(game::consoletexts::Prompt()) + str::Left(input, count)).cx;
}

constexpr int kHistoryDepth = 30;

// CDC::FillSolidRect of `width` x `height` at (x, y).
void FillArea(UiSurface* surface, int x, int y, int width, int height, std::uint32_t colour) {
    surface->FillRect(Rect{x, y, x + width, y + height}, colour);
}

} // namespace

// A shown window gets a surface over its rectangle (format 4) if it has none and is
// drawn; hiding releases the surface.
void ConsoleWindow::Show(bool newShown) {
    shown = newShown;
    if (newShown) {
        if (!surface) {
            surface = new UiSurface(screen, &rect, SurfaceBlend::Opaque);
        }
        DrawLines();
        DrawInput();
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
void ConsoleWindow::OnKeyDown(int key, int, int) {
    switch (key) {
    case platform::kKeyShift:
        shift = true;
        return;
    case platform::kKeyControl:
        ctrl = true;
        return;
    case platform::kKeyPageUp:
        scroll = ctrl ? scroll + visibleLines : scroll + 1;
        break;
    case platform::kKeyPageDown:
        scroll = ctrl ? scroll - visibleLines : scroll - 1;
        break;
    case platform::kKeyEnd:
        if (!ctrl) {
            caret = lib::Count(input);
            if (!shift) anchor = caret;
            DrawInput();
            return;
        }
        scroll = 0;
        break;
    case platform::kKeyHome:
        if (!ctrl) {
            caret = 0;
            if (!shift) anchor = 0;
            DrawInput();
            return;
        }
        {
            int first = static_cast<int>(lines.size()) - visibleLines;
            scroll = first < 0 ? 0 : first;
        }
        break;
    case platform::kKeyLeft:
    case platform::kKeyRight:   // by character
        caret = key == platform::kKeyLeft ? ::text::PreviousCharacter(input, caret) : ::text::NextCharacter(input, caret);
        if (!shift) anchor = caret;
        DrawInput();
        return;
    case platform::kKeyUp:
    case platform::kKeyDown:
        if (key == platform::kKeyUp) {
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
        DrawInput();
        return;
    case platform::kKeyDelete:
        if (anchor == caret) {
            if (caret < static_cast<int>(input.size())) {
                std::string rest = str::Mid(input, ::text::NextCharacter(input, caret));
                input = str::Left(input, caret) + rest;
                DrawInput();
            }
        } else {
            int low = anchor < caret ? anchor : caret;
            int high = anchor > caret ? anchor : caret;
            std::string rest = str::Mid(input, high);
            input = str::Left(input, low) + rest;
            caret = low;
            anchor = low;
            DrawInput();
        }
        return;
    default:
        return;
    }
    DrawLines();
}

// ---- Character input ------------------------------------------------------------------

// Typed characters (code points, stored as UTF-8; the caret is a byte offset at a
// character's start and moves by character). Backspace deletes the selection or the character before the caret,
// Enter enters the line (into the history, at most 30, echoed with "> ", then executed), Escape
// clears the line, Tab does nothing; other characters replace the selection, except while Ctrl is
// held. Rebirth's ImproveConsole skips the history for a line equal to the last one and adds
// Ctrl+A / C / X / V (select all, copy, cut, paste: the first line of the clipboard).
void ConsoleWindow::OnChar(int key, int, int) {
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
        DrawInput();
        return;
    }
    case 9:
        return;
    case 13: {   // Enter
        bool repeated = ei::rebirth::Enabled(ei::rebirth::Patch::ImproveConsole) && !commands.empty() &&
                        str::Compare(input, commands[0]) == 0;
        if (!repeated) {
            commands.insert(commands.begin(), input);
            if (commands.size() > kHistoryDepth) commands.pop_back();
        }
        commandIndex = -1;
        input.clear();
        caret = 0;
        anchor = 0;
        DrawInput();
        AddText((game::consoletexts::Prompt() + commands[0] + game::NewLineText()));
        Execute(commands[0]);
        return;
    }
    case 27:   // Escape
        input.clear();
        caret = 0;
        anchor = 0;
        DrawInput();
        return;
    default:
        break;
    }
    if (ctrl && !shift && ei::rebirth::Enabled(ei::rebirth::Patch::ImproveConsole)) {
        char letter = static_cast<char>(static_cast<std::uint8_t>(key) + 0x60);
        if (letter == 'a') {
            anchor = 0;
            caret = lib::Count(input);
            DrawInput();
        } else if ((letter == 'c' || letter == 'x') && low != high) {
            const char* text = input.c_str();
            platform::CopyText(std::string_view(text + low, static_cast<std::size_t>(high - low)));   // Rebirth's clipboard (ImproveConsole)
            if (letter == 'x') {
                std::string rest = str::Mid(input, high);
                std::string head = str::Left(input, low);
                input = head + rest;
                caret = low;
                anchor = low;
                DrawInput();
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
            DrawInput();
        }
    }
    if (ctrl) return;
    std::string rest = str::Mid(input, high);
    std::string head = str::Left(input, low);
    input = ::text::Typed(head, key) + rest;
    caret = low + ::text::TypedLength(key);
    anchor = caret;
    DrawInput();
}

// Keeps the scroll position within the lines, then (with a surface and a font) fills
// the area above the input strip and draws the visible lines bottom up, 15 pixels apart, with a
// "+" at the top right when there is more above and at the bottom right when scrolled back.
void ConsoleWindow::DrawLines() {
    const int lineCount = static_cast<int>(lines.size());
    if (scroll > lineCount - visibleLines) scroll = lineCount - visibleLines;
    if (scroll < 0) scroll = 0;
    if (!surface || !font) return;
    UiSurface* currentSurface = surface;
    FillArea(currentSurface, 0, 0, width, height - kInputHeight, kBackground);
    int line = lineCount - scroll - 1;
    for (int i = 0, y = 0; i < visibleLines && line >= 0; ++i, y += kLineHeight, --line) {
        Rect area{8, height - y - 0x2E, width - 0x12, height - y - 0x1A};
        const std::string& text = lines[line];
        currentSurface->DrawTextPixels(font, text, area, kDrawLine, kLineText, false);
    }
    if (scroll > 0) {
        std::string mark = game::consoletexts::More();
        Rect area{width - 0x12, height - 0x2E, width - 8, height - kInputHeight};
        currentSurface->DrawTextPixels(font, mark, area, kDrawMark, kLineText, false);
    }
    if (scroll < lineCount - visibleLines) {
        std::string mark = game::consoletexts::More();
        Rect area{width - 0x12, 8, width - 8, 0x17};
        currentSurface->DrawTextPixels(font, mark, area, kDrawMark, kLineText, false);
    }
}

// Keeps the caret and anchor within the input, then (with a surface and a font) fills
// the input strip, shades the selection (clipped 16 pixels before the right edge), draws "> " and
// the input, and a 2-pixel caret line when it lies left of that limit.
void ConsoleWindow::DrawInput() {
    caret = Clamp(caret, lib::Count(input));
    anchor = Clamp(anchor, lib::Count(input));
    if (!surface || !font) return;
    UiSurface* currentSurface = surface;
    FillArea(currentSurface, 0, height - kInputHeight, width, kInputHeight, kBackground);
    int caretX = PromptWidth(font, input, caret);
    if (anchor != caret) {
        int anchorX = PromptWidth(font, input, anchor);
        int low = caretX < anchorX ? caretX : anchorX;
        int high = caretX > anchorX ? caretX : anchorX;
        if (high > width - 0x10) high = width - 0x10;
        if (low < high) FillArea(currentSurface, low + 8, height - 0x17, high - low, 0xF, kSelection);
    }
    Rect area{8, height - 0x17, width - 8, height - 3};
    std::string text = std::string(game::consoletexts::Prompt()) + input;
    currentSurface->DrawTextPixels(font, text, area, kDrawLine, kInputText, false);
    if (caretX < width - 0x10) currentSurface->Line(caretX + 8, height - 0x17, caretX + 8, height - 8, 2, kInputText);
}

} // namespace console

void console::ConsoleWindow::Blit() {
    if (surface) surface->Blit();
}
