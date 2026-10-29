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
void FillArea(CUiSurface* surface, int x, int y, int width, int height, COLORREF colour) {
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
void ConsoleWindow::Show(bool shown) {
    shown_ = shown;
    if (shown) {
        if (!surface_) {
            surface_ = new CUiSurface(screen_, &rect_, SurfaceBlend::Opaque);
        }
        DrawLines();
        DrawInput();
        return;
    }
    if (CUiSurface* surface = surface_) {
        delete surface;
    }
    surface_ = nullptr;
}

// Keys while the window is shown. Shift (0x10) and Ctrl (0x11) are remembered; with
// Ctrl, PgUp/PgDn scroll by a page and Home/End go to the first/last page, otherwise PgUp/PgDn
// scroll by a line and Home/End/Left/Right move the caret (Shift keeps the selection's anchor);
// Up/Down step through the entered commands; Delete removes the selection or the next character.
void ConsoleWindow::OnKeyDown(int key, int, int) {
    switch (key) {
    case VK_SHIFT:
        shift_ = true;
        return;
    case VK_CONTROL:
        ctrl_ = true;
        return;
    case VK_PRIOR:
        scroll_ = ctrl_ ? scroll_ + visibleLines_ : scroll_ + 1;
        break;
    case VK_NEXT:
        scroll_ = ctrl_ ? scroll_ - visibleLines_ : scroll_ - 1;
        break;
    case VK_END:
        if (!ctrl_) {
            caret_ = input_.size();
            if (!shift_) anchor_ = caret_;
            DrawInput();
            return;
        }
        scroll_ = 0;
        break;
    case VK_HOME:
        if (!ctrl_) {
            caret_ = 0;
            if (!shift_) anchor_ = 0;
            DrawInput();
            return;
        }
        {
            int first = static_cast<int>(lines_.size()) - visibleLines_;
            scroll_ = first < 0 ? 0 : first;
        }
        break;
    case VK_LEFT:
    case VK_RIGHT:   // by character
        caret_ = key == VK_LEFT ? ::text::PreviousCharacter(input_, caret_) : ::text::NextCharacter(input_, caret_);
        if (!shift_) anchor_ = caret_;
        DrawInput();
        return;
    case VK_UP:
    case VK_DOWN:
        if (key == VK_UP) {
            if (commandIndex_ < static_cast<int>(commands_.size())) ++commandIndex_;
        } else if (commandIndex_ >= 0) {
            --commandIndex_;
        }
        if (commandIndex_ >= 0 && commandIndex_ < static_cast<int>(commands_.size()))
            input_ = commands_[commandIndex_];
        else
            input_.clear();
        caret_ = input_.size();
        anchor_ = caret_;
        DrawInput();
        return;
    case VK_DELETE:
        if (anchor_ == caret_) {
            if (caret_ < static_cast<int>(input_.size())) {
                std::string rest = str::Mid(input_, ::text::NextCharacter(input_, caret_));
                input_ = str::Left(input_, caret_) + rest;
                DrawInput();
            }
        } else {
            int low = anchor_ < caret_ ? anchor_ : caret_;
            int high = anchor_ > caret_ ? anchor_ : caret_;
            std::string rest = str::Mid(input_, high);
            input_ = str::Left(input_, low) + rest;
            caret_ = low;
            anchor_ = low;
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
    int low = caret_ < anchor_ ? caret_ : anchor_;
    int high = anchor_ > caret_ ? anchor_ : caret_;
    switch (key) {
    case 8: {   // Backspace
        if (anchor_ != caret_) {
            int first = anchor_ < caret_ ? anchor_ : caret_;
            int last = anchor_ > caret_ ? anchor_ : caret_;
            std::string rest = str::Mid(input_, last);
            input_ = str::Left(input_, first) + rest;
            caret_ = first;
            anchor_ = first;
        } else {
            if (caret_ == 0) return;
            const int previous = ::text::PreviousCharacter(input_, caret_);
            std::string rest = str::Mid(input_, caret_);
            input_ = str::Left(input_, previous) + rest;
            caret_ = previous;
            anchor_ = caret_;
        }
        DrawInput();
        return;
    }
    case 9:
        return;
    case 13: {   // Enter
        bool repeated = ei::rebirth::Enabled(ei::rebirth::Patch::ImproveConsole) && !commands_.empty() &&
                        str::Compare(input_, commands_[0]) == 0;
        if (!repeated) {
            commands_.insert(commands_.begin(), input_);
            if (commands_.size() > kHistoryDepth) commands_.pop_back();
        }
        commandIndex_ = -1;
        input_.clear();
        caret_ = 0;
        anchor_ = 0;
        DrawInput();
        AddText((game::consoletexts::Prompt() + commands_[0] + game::NewLineText()));
        Execute(commands_[0]);
        return;
    }
    case 27:   // Escape
        input_.clear();
        caret_ = 0;
        anchor_ = 0;
        DrawInput();
        return;
    default:
        break;
    }
    if (ctrl_ && !shift_ && ei::rebirth::Enabled(ei::rebirth::Patch::ImproveConsole)) {
        char letter = static_cast<char>(static_cast<std::uint8_t>(key) + 0x60);
        if (letter == 'a') {
            anchor_ = 0;
            caret_ = input_.size();
            DrawInput();
        } else if ((letter == 'c' || letter == 'x') && low != high) {
            const char* text = input_.c_str();
            CopyToClipboard(text + low, text + high);
            if (letter == 'x') {
                std::string rest = str::Mid(input_, high);
                std::string head = str::Left(input_, low);
                input_ = head + rest;
                caret_ = low;
                anchor_ = low;
                DrawInput();
            }
        } else if (letter == 'v') {
            std::string clip = ClipboardText();
            int cut = str::FindOneOf(clip, "\r\n");
            if (cut >= 0) clip = str::Left(clip, cut);
            std::string rest = str::Mid(input_, high);
            std::string head = str::Left(input_, low);
            input_ = (head + clip) + rest;
            int position = static_cast<int>(clip.size()) - (anchor_ != caret_ ? 1 : 0) + high;
            position = ::text::CharacterStart(input_, position);   // never inside a character
            caret_ = position;
            anchor_ = position;
            DrawInput();
        }
    }
    if (ctrl_) return;
    std::string rest = str::Mid(input_, high);
    std::string head = str::Left(input_, low);
    input_ = ::text::Typed(head, key) + rest;
    caret_ = low + ::text::TypedLength(key);
    anchor_ = caret_;
    DrawInput();
}

// Keeps the scroll position within the lines, then (with a surface and a font) fills
// the area above the input strip and draws the visible lines bottom up, 15 pixels apart, with a
// "+" at the top right when there is more above and at the bottom right when scrolled back.
void ConsoleWindow::DrawLines() {
    const int lineCount = static_cast<int>(lines_.size());
    if (scroll_ > lineCount - visibleLines_) scroll_ = lineCount - visibleLines_;
    if (scroll_ < 0) scroll_ = 0;
    if (!surface_ || !font_) return;
    CUiSurface* surface = surface_;
    FillArea(surface, 0, 0, width_, height_ - kInputHeight, kBackground);
    int line = lineCount - scroll_ - 1;
    for (int i = 0, y = 0; i < visibleLines_ && line >= 0; ++i, y += kLineHeight, --line) {
        RECT rect{8, height_ - y - 0x2E, width_ - 0x12, height_ - y - 0x1A};
        const std::string& text = lines_[line];
        surface->DrawTextPixels(font_, text, rect, kDrawLine, kLineText, false);
    }
    if (scroll_ > 0) {
        std::string mark = game::consoletexts::More();
        RECT rect{width_ - 0x12, height_ - 0x2E, width_ - 8, height_ - kInputHeight};
        surface->DrawTextPixels(font_, mark, rect, kDrawMark, kLineText, false);
    }
    if (scroll_ < lineCount - visibleLines_) {
        std::string mark = game::consoletexts::More();
        RECT rect{width_ - 0x12, 8, width_ - 8, 0x17};
        surface->DrawTextPixels(font_, mark, rect, kDrawMark, kLineText, false);
    }
}

// Keeps the caret and anchor within the input, then (with a surface and a font) fills
// the input strip, shades the selection (clipped 16 pixels before the right edge), draws "> " and
// the input, and a 2-pixel caret line when it lies left of that limit.
void ConsoleWindow::DrawInput() {
    caret_ = Clamp(caret_, input_.size());
    anchor_ = Clamp(anchor_, input_.size());
    if (!surface_ || !font_) return;
    CUiSurface* surface = surface_;
    FillArea(surface, 0, height_ - kInputHeight, width_, kInputHeight, kBackground);
    int caretX = PromptWidth(font_, input_, caret_);
    if (anchor_ != caret_) {
        int anchorX = PromptWidth(font_, input_, anchor_);
        int low = caretX < anchorX ? caretX : anchorX;
        int high = caretX > anchorX ? caretX : anchorX;
        if (high > width_ - 0x10) high = width_ - 0x10;
        if (low < high) FillArea(surface, low + 8, height_ - 0x17, high - low, 0xF, kSelection);
    }
    RECT rect{8, height_ - 0x17, width_ - 8, height_ - 3};
    std::string text = std::string(game::consoletexts::Prompt()) + input_;
    surface->DrawTextPixels(font_, text, rect, kDrawLine, kInputText, false);
    if (caretX < width_ - 0x10) surface->Line(caretX + 8, height_ - 0x17, caretX + 8, height_ - 8, 2, kInputText);
}

} // namespace console
