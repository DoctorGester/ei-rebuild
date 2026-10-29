// The console window's surface, drawing and key input. The text
// side (lines, history depth, placement) is in console_window.cpp. See docs/notes/ui.md
// ("Console window").
#include "console.h"
#include "game_strings.h"
#include "ui.h"
#include "system_text.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/rebirth.h"
#include "lib/str.h"

#include <cstring>
#include <string>

namespace console {

namespace {


// Colours and the strings the window draws (read from the exe: "> " before the input, "+" for
// the scroll marks).
constexpr COLORREF kBackground = 0x282828;
constexpr COLORREF kSelection = 0x505050;
constexpr COLORREF kLineText = 0xB4B4B4;
constexpr COLORREF kInputText = 0xFFFFFF;
constexpr int kLineHeight = 15;
constexpr int kInputHeight = 0x1F;     // the input strip at the bottom
constexpr UINT kDrawLine = 0x800;      // DT_NOPREFIX
constexpr UINT kDrawMark = 0x802;      // DT_NOPREFIX | DT_RIGHT

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
void FillArea(UiSurface* surface, int x, int y, int width, int height, COLORREF colour) {
    surface->FillRect(RECT{x, y, x + width, y + height}, colour);
}

// Rebirth's clipboard (ImproveConsole): the text as CF_UNICODETEXT, converted from UTF-8 (addon's helpers convert
// with the ANSI code page).
void CopyToClipboard(const char* first, const char* last) {
    int count = static_cast<int>(last - first);
    std::wstring wide;
    if (count > 0) wide = ::text::Wide(std::string_view(first, static_cast<std::size_t>(count)));
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (wide.size() + 1) * 2);
    if (!memory) return;
    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(locked, wide.c_str(), (wide.size() + 1) * 2);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
}

std::string ClipboardText() {
    std::wstring wide;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                wide = text;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    return ::text::Narrow(wide);
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
    case VK_SHIFT:
        shift = true;
        return;
    case VK_CONTROL:
        ctrl = true;
        return;
    case VK_PRIOR:
        scroll = ctrl ? scroll + visibleLines : scroll + 1;
        break;
    case VK_NEXT:
        scroll = ctrl ? scroll - visibleLines : scroll - 1;
        break;
    case VK_END:
        if (!ctrl) {
            caret = input.size();
            if (!shift) anchor = caret;
            DrawInput();
            return;
        }
        scroll = 0;
        break;
    case VK_HOME:
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
    case VK_LEFT:
    case VK_RIGHT:   // by character
        caret = key == VK_LEFT ? ::text::PreviousCharacter(input, caret) : ::text::NextCharacter(input, caret);
        if (!shift) anchor = caret;
        DrawInput();
        return;
    case VK_UP:
    case VK_DOWN:
        if (key == VK_UP) {
            if (commandIndex < static_cast<int>(commands.size())) ++commandIndex;
        } else if (commandIndex >= 0) {
            --commandIndex;
        }
        if (commandIndex >= 0 && commandIndex < static_cast<int>(commands.size()))
            input = commands[commandIndex];
        else
            input.clear();
        caret = input.size();
        anchor = caret;
        DrawInput();
        return;
    case VK_DELETE:
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
            caret = input.size();
            DrawInput();
        } else if ((letter == 'c' || letter == 'x') && low != high) {
            const char* text = input.c_str();
            CopyToClipboard(text + low, text + high);
            if (letter == 'x') {
                std::string rest = str::Mid(input, high);
                std::string head = str::Left(input, low);
                input = head + rest;
                caret = low;
                anchor = low;
                DrawInput();
            }
        } else if (letter == 'v') {
            std::string clip = ClipboardText();
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
        RECT area{8, height - y - 0x2E, width - 0x12, height - y - 0x1A};
        const std::string& text = lines[line];
        currentSurface->DrawTextPixels(font, text, area, kDrawLine, kLineText, false);
    }
    if (scroll > 0) {
        std::string mark = game::consoletexts::More();
        RECT area{width - 0x12, height - 0x2E, width - 8, height - kInputHeight};
        currentSurface->DrawTextPixels(font, mark, area, kDrawMark, kLineText, false);
    }
    if (scroll < lineCount - visibleLines) {
        std::string mark = game::consoletexts::More();
        RECT area{width - 0x12, 8, width - 8, 0x17};
        currentSurface->DrawTextPixels(font, mark, area, kDrawMark, kLineText, false);
    }
}

// Keeps the caret and anchor within the input, then (with a surface and a font) fills
// the input strip, shades the selection (clipped 16 pixels before the right edge), draws "> " and
// the input, and a 2-pixel caret line when it lies left of that limit.
void ConsoleWindow::DrawInput() {
    caret = Clamp(caret, input.size());
    anchor = Clamp(anchor, input.size());
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
    RECT area{8, height - 0x17, width - 8, height - 3};
    std::string text = std::string(game::consoletexts::Prompt()) + input;
    currentSurface->DrawTextPixels(font, text, area, kDrawLine, kInputText, false);
    if (caretX < width - 0x10) currentSurface->Line(caretX + 8, height - 0x17, caretX + 8, height - 8, 2, kInputText);
}

} // namespace console

void console::ConsoleWindow::Blit() {
    if (surface) surface->Blit();
}
