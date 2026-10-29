// A region's tooltip (CUiTooltip): its constructor, which splits the text into lines and measures them, the
// destructor body, SetText and the drawing of the lines; and its showing (Rebirth-patched: its WideScreen is
// reproduced behind the flag). See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "ui_native.h"
#include "options.h"
#include "ui_widescreen.h"

#include "lib/crt.h"
#include "text_split.h"
#include "lib/gameapi.h"
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

// Showing.
constexpr int kIdle = 0;
constexpr int kHovered = 1;
constexpr int kAppearing = 2;
constexpr int kDisappearing = 3;
constexpr int kShown = 4;
constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;
constexpr int kSurfaceFormat = 5;
// The game's setting 20 (the tooltips' delay in seconds; 0 turns them off), the appearing time (0.0 in the
// exe) and the tooltip's offset from the mouse in screen pixels ((24, 24), written by a static initialiser).
constexpr float kAppearTime = 0.0f;
constexpr POINT kCursorOffset = /*007C8258*/ {24, 24};   // a static initialiser

SIZE TextExtent(CInterface3D* ui, const std::string& text, int font) {
    SIZE size;
    ui->TextExtent(&size, text, font);
    return size;
}
} // namespace

// The virtuals: drawn and redrawn only while shown; the input side's defaults.
CUiTooltip::CUiTooltip(CInterface3D* ui, CUiRegion* region, std::string_view text, int flag, int extra) : CUiWindow(ui) {
    timer_ = 0.0f;
    state_ = 0;
    region_ = region;
    // An empty text leaves the measurements and the two arguments unwritten.
    if (text.empty()) return;
    text::Split(text, &lines_, kLineSeparator);
    width_ = 0;
    height_ = 0;
    lineHeight_ = 0;
    flag_ = flag;
    extra_ = extra;
    keyName_ = nullptr;
    keyWidth_ = 0;
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        // An empty line (but the last) is as high as the sample and has no width.
        SIZE size;
        int width;
        if (lines_[i].empty() && i < static_cast<int>(lines_.size()) - 1) {
            size = TextExtent(interface_, std::string(EmptyLineSample()), kFont);
            width = 0;
        } else {
            size = TextExtent(interface_, lines_[i], kFont);
            width = size.cx;
        }
        if (width > width_) width_ = width;
        if (size.cy > 0) {
            lineHeight_ = size.cy;
            height_ += size.cy;
        }
    }
    width_ += kPaddingX;
    height_ += kPaddingY;
}

CUiTooltip::~CUiTooltip() = default;

void CUiTooltip::SetText(std::string_view text) {
    if (text.empty()) return;
    text::Split(text, &lines_, kLineSeparator);
    DrawLines();
}

void CUiTooltip::DrawLines() {
    if (!(!surfaces_.empty())) return;
    CUiSurface* surface = surfaces_[0];
    if (!surface) return;
    UiSurfaceGetDC(surface);
    surface->Fill(kBackground);
    RECT whole{0, 0, 0, 0};   // empty: the whole surface
    surface->FrameRect(&whole, kTitleColour);
    int right = width_ > keyWidth_ ? width_ : keyWidth_;
    int step = lineHeight_;
    RECT row{kTextLeft, kTextTop, right - kTextLeft, step + kTextTop};
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        COLORREF colour = i >= flag_ ? kTextColour : kTitleColour;
        if (i == 0 && keyName_) {
            std::string text = lines_[0] + KeyOpen() + keyName_ + KeyClose();
            surface->DrawTextIn(text, &row, kFont, colour, 0, 0);
        } else {
            surface->DrawTextIn(lines_[i], &row, kFont, colour, 0, 0);
        }
        gameapi::OffsetRect(&row, 0, step);
    }
    UiSurfaceReleaseDC(surface);
}

void CUiTooltip::OnMouseMove(int x, int y) {
    switch (state_) {
    case kHovered: {
        const float delay = options::g_options->toolTipTime_;
        if (lines_.empty() || delay == 0.0f) {
            state_ = kIdle;
            return;
        }
        if (timer_ < delay) {
            timer_ = static_cast<float>(CUiContainer::FrameTime() + timer_);
            return;
        }
        // Shown: the first line is measured again with the key of the tooltip's command.
        keyWidth_ = 0;
        if (extra_) {
            keyName_ = static_cast<const char*>(FindCommandByKey(extra_));
            if (keyName_) {
                keyWidth_ = TextExtent(interface_, lines_[0] + KeyOpen() + keyName_ + KeyClose(), kFont).cx + kPaddingX;
            }
        }
        state_ = kAppearing;
        timer_ = 0.0f;
        RECT rect{0, 0, width_ > keyWidth_ ? width_ : keyWidth_, height_};
        POINT offset = kCursorOffset;
        // WideScreen: without the margin; the layout's right edge 800 + extra (addon's operands).
        interface_->CallScalePointFromScreenNoMargin(&offset);
        const int layoutRight = widescreen::Active() ? widescreen::LayoutRight() : kLayoutWidth;
        gameapi::OffsetRect(&rect, offset.x, offset.y);
        gameapi::OffsetRect(&rect, x, y);
        // Kept on the layout: pushed left from the right edge, and above the mouse at the bottom.
        if (rect.right > layoutRight) {
            gameapi::OffsetRect(&rect, -(rect.right - layoutRight), 0);
            if (rect.left < 0) gameapi::OffsetRect(&rect, -rect.left, 0);
        }
        if (rect.bottom > kLayoutHeight) {
            gameapi::OffsetRect(&rect, 0, -(rect.bottom - y));
            if (rect.top < 0) gameapi::OffsetRect(&rect, 0, -rect.top);
        }
        CUiSurface* surface = new CUiSurface(interface_, &rect, kSurfaceFormat);
        ui_native::Adopt(surface, "tooltip");
        AddSurface(surface);
        return;
    }
    case kAppearing:
        // The appearing time is zero in the exe: the tooltip is fully shown on the next call.
        if (timer_ < kAppearTime) {
            DrawLines();
            timer_ = static_cast<float>(CUiContainer::FrameTime() + timer_);
        } else {
            state_ = kShown;
            timer_ = kAppearTime;
            DrawLines();
        }
        return;
    case kDisappearing:
        if (timer_ > 0.0f) {
            DrawLines();
            timer_ = static_cast<float>(timer_ - CUiContainer::FrameTime());
        } else {
            state_ = kIdle;
            ClearSceneObjects();
            ClearSurfaces();
        }
        return;
    default:
        return;
    }
}
