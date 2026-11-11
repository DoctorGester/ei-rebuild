// The console window: its text side (lines, history depth, placement), its surface, paint and key input.
// See docs/notes/console.md and docs/notes/ui.md ("Console window").
#include "game/console/console.h"

#include "engine/core/asset_text.h"
#include "engine/core/text_split.h"
#include "game/ui/screen_layout.h"
#include "game/ui/ui.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <string>

namespace console {

namespace {
constexpr int kFontSize = 90;            // tenths of a point at the 800x600 layout's size
constexpr const char* kFontFace = "Courier New";
constexpr int kMinWidth = 0x4C;          // layout pixels, like every size the window draws with
constexpr int kMinHeight = 0x45;
constexpr int kTopMargin = 0x18;
constexpr int kLineHeight = 15;
constexpr int kLayoutHeight = 600;
constexpr Rect kInitialRect = {0, 0, 800, 150};

// Colours and the strings the window draws (read from the exe: "> " before the input, "+" for
// the scroll marks).
constexpr std::uint32_t kBackground = 0x282828;
constexpr std::uint32_t kSelection = 0x505050;
constexpr std::uint32_t kLineText = 0xB4B4B4;
constexpr std::uint32_t kInputText = 0xFFFFFF;
// Layout pixels (ConsoleWindow::Px scales them): the input strip at the bottom (the line pitch: kLineHeight).
constexpr int kInputHeight = 0x1F;
constexpr render::text::Format kDrawLine = render::text::Format::NoPrefix;
constexpr render::text::Format kDrawMark = render::text::Format::NoPrefix | render::text::Format::Right;

int Clamp(int value, int high) {
    if (value > high) value = high;
    if (value < 0) value = 0;
    return value;
}

// Width in pixels of the prompt and the first `count` characters of the input in `font`.
int PromptWidth(const UiFont& font, const std::string& input, int count) {
    return TextExtent(font, std::string("> ") + str::Left(input, count)).w;
}

constexpr int kHistoryDepth = 30;

// CDC::FillSolidRect of `width` x `height` at (x, y).
void FillArea(UiSurface& surface, int x, int y, int width, int height, std::uint32_t colour) {
    surface.FillRect(Rect{x, y, x + width, y + height}, colour);
}

} // namespace

int ConsoleWindow::Px(int layoutPixels) const {
    return static_cast<int>(std::lround(static_cast<float>(layoutPixels) * scale));
}

// The text and the metrics follow the window like the rest of the interface: one pixel of the 800x600 layout is
// height43 / 600 screen pixels (the window's 4:3 part), whatever the system's DPI or display scale (a Retina window
// is as many pixels as it has physical ones).
void ConsoleWindow::SetScale() {
    scale = static_cast<float>(screen_layout::Get().height43) / kLayoutHeight;
    font = PointFont(Px(kFontSize), kFontFace);
}

// The rectangle in pixels, clamped to at least 76x69 layout pixels (scaled) and at most the screen; the number of lines
// that fit below the 24-pixel margin, less one; the clamped rectangle back in 800x600 coordinates. A shown window gets
// a new surface. The requested rectangle is kept, so a resolution change places it again (OnResize).
void ConsoleWindow::SetRect(const Rect& newRect) {
    int screenWidth = g_interface.width;
    int screenHeight = g_interface.height;
    requestedRect = newRect;
    SetScale();
    // The 3D interface's 800x600 layout to pixels.
    screenRect = screen_layout::ToScreen(newRect);
    width = screenRect.Width();
    height = screenRect.Height();
    if (width < Px(kMinWidth)) width = Px(kMinWidth);
    if (width > screenWidth) width = screenWidth;
    if (height < Px(kMinHeight)) height = Px(kMinHeight);
    if (height > screenHeight) height = screenHeight;
    visibleLines = (height - Px(kTopMargin)) / Px(kLineHeight) - 1;
    ei::Log("Console window: %dx%d pixels, font %d px, lines %d px, %d visible", width, height, font.pixels,
            Px(kLineHeight), visibleLines);
    screenRect.right = screenRect.left + width;
    screenRect.bottom = screenRect.top + height;
    rect = screen_layout::FromScreen(screenRect);
    if (shown) {
        Show(false);
        Show(true);
    }
}

void ConsoleWindow::OnResize() { SetRect(requestedRect); }

void ConsoleWindow::SetHistory(int newLines) {
    history = newLines;
    DropOldLines(newLines);
    ClampScroll();
}

void ConsoleWindow::DropOldLines(int count) {
    if (static_cast<int>(lines.size()) > count) lines.erase(lines.begin(), lines.end() - count);
}

// The first piece continues the last line, each '\n' starts a new one. A window scrolled
// back stays on the same lines. Keeps the last history lines.
void ConsoleWindow::AddText(std::string_view text) {
    std::vector<std::string> pieces = text::Split(text, '\n');   // at least one piece
    if (!lines.empty())
        lines.back() += pieces[0];
    else
        lines.push_back(pieces[0]);
    lines.insert(lines.end(), pieces.begin() + 1, pieces.end());
    if (scroll != 0) scroll += static_cast<int>(pieces.size());
    DropOldLines(history);
    ClampScroll();
}

// --- Create --------------------------------------------------------------------------------------

// The console window's constructor. See console.h and docs/notes/console.md.
ConsoleWindow::ConsoleWindow() {
    Rect area = kInitialRect;
    SetRect(area);
    DropOldLines(kHistory);
    ClampScroll();
}

// --- Surface, paint and key input ----------------------------------------------------------------

// A shown window gets a surface over its rectangle (format 4) if it has none, painted from the lines and the
// input; hiding releases the surface.
void ConsoleWindow::Show(bool newShown) {
    shown = newShown;
    if (newShown) {
        if (!surface) {
            surface = new UiSurface(&rect, SurfaceBlend::Opaque);
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
        AddText(("> " + commands[0] + "\n"));
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
            platform::CopyText(std::string_view(input).substr(static_cast<std::size_t>(low), static_cast<std::size_t>(high - low)));   // Rebirth's clipboard (ImproveConsole)
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
        out.DrawTextPixels(font, "+", area, kDrawMark, kLineText, false);
    }
    if (scroll < lineCount - visibleLines) {
        Rect area{width - Px(0x12), Px(8), width - Px(8), Px(0x17)};
        out.DrawTextPixels(font, "+", area, kDrawMark, kLineText, false);
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
    out.DrawTextPixels(font, std::string("> ") + input, area, kDrawLine, kInputText, false);
    if (caretX < right) out.Line({caretX + margin, top}, {caretX + margin, height - margin}, std::max(Px(2), 1), kInputText);
}

} // namespace console

void console::ConsoleWindow::Blit() {
    if (surface) surface->Blit();
}
