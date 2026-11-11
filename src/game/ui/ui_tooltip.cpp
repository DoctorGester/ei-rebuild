// A region's tooltip (UiTooltip): its constructor, which splits the text into lines and measures them, SetText
// and the painting of the lines; and its showing (Rebirth-patched: its WideScreen is
// reproduced behind the flag). See ui.h and docs/notes/ui.md, section 2.
#include "game/ui/ui.h"
#include "engine/data/options.h"
#include "game/ui/screen_layout.h"

#include "lib/crt.h"
#include "engine/core/text_split.h"
#include "lib/rect.h"
#include <string>
#include "lib/vecmath.h"
#include "game/app/key_bindings.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr int kFont = 0;
constexpr char kLineSeparator = '\n';
constexpr int kPaddingX = 12;               // added to the widest line
constexpr int kPaddingY = 8;                // added to the lines' heights
constexpr int kTextLeft = 6;
constexpr int kTextTop = 4;
constexpr std::uint32_t kBackground = 0x1C2B;
constexpr std::uint32_t kTitleColour = 0x31B3FF;   // also the frame

// The exe's texts: "w" (an empty line's height is measured on it), "  ( " and " )" around the key.
constexpr const char* kEmptyLineSample = "w";
constexpr const char* kKeyOpen = "  ( ";
constexpr const char* kKeyClose = " )";

constexpr int kLayoutHeight = 600;
// The game's setting 20 (the tooltips' delay in seconds; 0 turns them off), the appearing time (0.0 in the
// exe) and the tooltip's offset from the mouse in screen pixels ((24, 24), written by a static initialiser).
constexpr float kAppearTime = 0.0f;
constexpr Point kCursorOffset = /*007C8258*/ {24, 24};   // a static initialiser

} // namespace

UiTooltip::UiTooltip(UiRegion* newRegion, std::string_view text, int command) : region(newRegion) {
    // An empty text leaves the measurements and the two arguments unwritten.
    if (text.empty()) return;
    lines = text::Split(text, kLineSeparator);
    extra = command;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        // An empty line (but the last) is as high as the sample and has no width.
        Size size;
        int textWidth;
        if (lines[i].empty() && i < static_cast<int>(lines.size()) - 1) {
            size = g_interface.TextExtent(std::string(kEmptyLineSample), kFont);
            textWidth = 0;
        } else {
            size = g_interface.TextExtent(lines[i], kFont);
            textWidth = size.w;
        }
        if (textWidth > width) width = textWidth;
        if (size.h > 0) {
            lineHeight = size.h;
            height += size.h;
        }
    }
    width += kPaddingX;
    height += kPaddingY;
}

void UiTooltip::SetText(std::string_view text) {
    if (text.empty()) return;
    lines = text::Split(text, kLineSeparator);
}

void UiTooltip::PaintLines(UiSurface& out) const {
    out.Fill(kBackground);
    Rect whole{0, 0, 0, 0};   // empty: the whole surface
    out.FrameRect(whole, kTitleColour);
    int right = width > keyWidth ? width : keyWidth;
    int step = lineHeight;
    Rect row{kTextLeft, kTextTop, right - kTextLeft, step + kTextTop};
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::uint32_t colour = i > 0 ? kUiTextColour : kTitleColour;
        if (i == 0 && keyName) {
            std::string text = lines[0] + kKeyOpen + keyName + kKeyClose;
            out.DrawTextIn(text, row, TextStyle{kFont, colour, render::text::Format::Left, false});
        } else {
            out.DrawTextIn(lines[i], row, TextStyle{kFont, colour, render::text::Format::Left, false});
        }
        row.Offset(0, step);
    }
}

void UiTooltip::OnMouseMove(Point point) {
    switch (state) {
    case State::Hovered: {
        // Left before the delay ran out: back to Idle, the wait starting again on the next hover. A tooltip left
        // waiting would show at the mouse over another region, where that region's own appears next.
        if (!region->HitTest(point)) {
            state = State::Idle;
            timer = 0.0f;
            return;
        }
        const float delay = options::g_options.toolTipTime;
        if (lines.empty() || delay == 0.0f) {
            state = State::Idle;
            return;
        }
        if (timer < delay) {
            timer = UiContainer::FrameTime() + timer;
            return;
        }
        // Shown: the first line is measured again with the key of the tooltip's command.
        keyWidth = 0;
        if (extra) {
            keyName = FindCommandByKey(extra);
            if (keyName) {
                keyWidth = g_interface.TextExtent(lines[0] + kKeyOpen + keyName + kKeyClose, kFont).w + kPaddingX;
            }
        }
        state = State::Appearing;
        timer = 0.0f;
        Rect area{0, 0, width > keyWidth ? width : keyWidth, height};
        Point offset = kCursorOffset;
        // WideScreen: without the margin; the layout's right edge 800 + extra (addon's operands).
        offset = screen_layout::FromScreenNoMargin(offset);
        const int layoutRight = screen_layout::LayoutRight();
        area.Offset(offset);
        area.Offset(point);
        // Kept on the layout: pushed left from the right edge, and above the mouse at the bottom.
        if (area.right > layoutRight) {
            area.Offset(-(area.right - layoutRight), 0);
            if (area.left < 0) area.Offset(-area.left, 0);
        }
        if (area.bottom > kLayoutHeight) {
            area.Offset(0, -(area.bottom - point.y));
            if (area.top < 0) area.Offset(0, -area.top);
        }
        surface = std::make_unique<UiSurface>(&area, SurfaceBlend::Keyed);
        surface->PaintWith([this](UiSurface& out) { PaintLines(out); });
        return;
    }
    case State::Appearing:
        // The appearing time is zero in the exe: the tooltip is Shown on the next call.
        if (timer < kAppearTime) {
            timer = UiContainer::FrameTime() + timer;
        } else {
            state = State::Shown;
            timer = kAppearTime;
        }
        return;
    case State::Disappearing:
        if (timer > 0.0f) {
            timer -= UiContainer::FrameTime();
        } else {
            state = State::Idle;
            surface.reset();
        }
        return;
    default:
        return;
    }
}

void UiTooltip::Draw() {
    if (Shown() && surface && surface->Shown()) surface->Blit();
}
