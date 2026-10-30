// A region's tooltip (UiTooltip): its constructor, which splits the text into lines and measures them, the
// destructor body, SetText and the drawing of the lines; and its showing (Rebirth-patched: its WideScreen is
// reproduced behind the flag). See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "options.h"
#include "ui_widescreen.h"

#include "lib/crt.h"
#include "text_split.h"
#include "lib/rect.h"
#include <string>
#include "lib/vecmath.h"
#include "object_tables.h"       // FindCommandByKey

#include <cstdint>
#include <cstring>

namespace {
constexpr int kFont = 0;
constexpr char kLineSeparator = '\n';
constexpr int kPaddingX = 12;               // added to the widest line
constexpr int kPaddingY = 8;                // added to the lines' heights
constexpr int kTextLeft = 6;
constexpr int kTextTop = 4;
constexpr COLORREF kBackground = 0x1C2B;
constexpr COLORREF kTitleColour = 0x31B3FF;   // also the frame
constexpr COLORREF kTextColour = 0xA7D7E4;

// The exe's texts: "w" (an empty line's height is measured on it), "  ( " and " )" around the key.
const char* EmptyLineSample() { return "w"; }
const char* KeyOpen() { return "  ( "; }
const char* KeyClose() { return " )"; }

constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;
// The game's setting 20 (the tooltips' delay in seconds; 0 turns them off), the appearing time (0.0 in the
// exe) and the tooltip's offset from the mouse in screen pixels ((24, 24), written by a static initialiser).
constexpr float kAppearTime = 0.0f;
constexpr POINT kCursorOffset = /*007C8258*/ {24, 24};   // a static initialiser

} // namespace

// The virtuals: drawn and redrawn only while shown; the input side's defaults.
UiTooltip::UiTooltip(Interface3D* ownerUi, UiRegion* newRegion, std::string_view text, int command) : UiWindow(ownerUi) {
    timer = 0.0f;
    region = newRegion;
    // An empty text leaves the measurements and the two arguments unwritten.
    if (text.empty()) return;
    lines = text::Split(text, kLineSeparator);
    width = 0;
    height = 0;
    lineHeight = 0;
    extra = command;
    keyName = nullptr;
    keyWidth = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        // An empty line (but the last) is as high as the sample and has no width.
        SIZE size;
        int textWidth;
        if (lines[i].empty() && i < static_cast<int>(lines.size()) - 1) {
            size = ui->TextExtent(std::string(EmptyLineSample()), kFont);
            textWidth = 0;
        } else {
            size = ui->TextExtent(lines[i], kFont);
            textWidth = size.cx;
        }
        if (textWidth > width) width = textWidth;
        if (size.cy > 0) {
            lineHeight = size.cy;
            height += size.cy;
        }
    }
    width += kPaddingX;
    height += kPaddingY;
}

UiTooltip::~UiTooltip() = default;

void UiTooltip::SetText(std::string_view text) {
    if (text.empty()) return;
    lines = text::Split(text, kLineSeparator);
    DrawLines();
}

void UiTooltip::DrawLines() {
    if (!(!surfaces.empty())) return;
    UiSurface* surface = surfaces[0];
    if (!surface) return;
    surface->Fill(kBackground);
    RECT whole{0, 0, 0, 0};   // empty: the whole surface
    surface->FrameRect(&whole, kTitleColour);
    int right = width > keyWidth ? width : keyWidth;
    int step = lineHeight;
    RECT row{kTextLeft, kTextTop, right - kTextLeft, step + kTextTop};
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        COLORREF colour = i > 0 ? kTextColour : kTitleColour;
        if (i == 0 && keyName) {
            std::string text = lines[0] + KeyOpen() + keyName + KeyClose();
            surface->DrawTextIn(text, &row, kFont, colour, 0, 0);
        } else {
            surface->DrawTextIn(lines[i], &row, kFont, colour, 0, 0);
        }
        rects::Offset(&row, 0, step);
    }
}

void UiTooltip::OnMouseMove(int x, int y) {
    switch (state) {
    case State::Hovered: {
        const float delay = options::g_options.toolTipTime;
        if (lines.empty() || delay == 0.0f) {
            state = State::Idle;
            return;
        }
        if (timer < delay) {
            timer = static_cast<float>(UiContainer::FrameTime() + timer);
            return;
        }
        // Shown: the first line is measured again with the key of the tooltip's command.
        keyWidth = 0;
        if (extra) {
            keyName = static_cast<const char*>(FindCommandByKey(extra));
            if (keyName) {
                keyWidth = ui->TextExtent(lines[0] + KeyOpen() + keyName + KeyClose(), kFont).cx + kPaddingX;
            }
        }
        state = State::Appearing;
        timer = 0.0f;
        RECT area{0, 0, width > keyWidth ? width : keyWidth, height};
        POINT offset = kCursorOffset;
        // WideScreen: without the margin; the layout's right edge 800 + extra (addon's operands).
        ui->CallScalePointFromScreenNoMargin(&offset);
        const int layoutRight = widescreen::Active() ? widescreen::LayoutRight() : kLayoutWidth;
        rects::Offset(&area, offset.x, offset.y);
        rects::Offset(&area, x, y);
        // Kept on the layout: pushed left from the right edge, and above the mouse at the bottom.
        if (area.right > layoutRight) {
            rects::Offset(&area, -(area.right - layoutRight), 0);
            if (area.left < 0) rects::Offset(&area, -area.left, 0);
        }
        if (area.bottom > kLayoutHeight) {
            rects::Offset(&area, 0, -(area.bottom - y));
            if (area.top < 0) rects::Offset(&area, 0, -area.top);
        }
        UiSurface* surface = new UiSurface(ui, &area, SurfaceBlend::Keyed);
        AddSurface(surface);
        return;
    }
    case State::Appearing:
        // The appearing time is zero in the exe: the tooltip is fully shown on the next call.
        if (timer < kAppearTime) {
            DrawLines();
            timer = static_cast<float>(UiContainer::FrameTime() + timer);
        } else {
            state = State::Shown;
            timer = kAppearTime;
            DrawLines();
        }
        return;
    case State::Disappearing:
        if (timer > 0.0f) {
            DrawLines();
            timer = static_cast<float>(timer - UiContainer::FrameTime());
        } else {
            state = State::Idle;
            ClearSceneObjects();
            ClearSurfaces();
        }
        return;
    default:
        return;
    }
}
