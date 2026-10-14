// A region's tooltip (CUiTooltip, 0065E320-0065EA50; A-H20): its constructor, which splits the text
// into lines and measures them, the destructor body, SetText and the drawing of the lines; and its
// showing (0065E580, A-H26: Rebirth-patched; its WideScreen is reproduced behind the flag, W2,
// and CUiRegion::OnMouseMove keeps calling it through its address). See ui.h and
// docs/notes/items_spells.md.
#include "ui.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "object_tables.h"       // FindCommandByKey
#include "originals_library.h"
#include "originals_ui.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {
constexpr std::uintptr_t kTooltipVtable = std::uintptr_t(0x00742F64);
constexpr std::uintptr_t kTooltipInputVtable = std::uintptr_t(0x00742F30);
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
const char* EmptyLineSample() { return reinterpret_cast<const char*>(std::uintptr_t(0x00794B60)); }
const char* KeyOpen() { return reinterpret_cast<const char*>(std::uintptr_t(0x00794B68)); }
const char* KeyClose() { return reinterpret_cast<const char*>(std::uintptr_t(0x00794B64)); }

// Showing (0065E580).
constexpr int kIdle = 0;
constexpr int kHovered = 1;
constexpr int kAppearing = 2;
constexpr int kDisappearing = 3;
constexpr int kShown = 4;
constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;
constexpr int kSurfaceFormat = 5;
// The game's setting 20 (the tooltips' delay in seconds; 0 turns them off), the appearing time
// (0.0 in the exe) and the tooltip's offset from the mouse in screen pixels ((24, 24), written by
// the static initialiser 0065E310).
float ShowDelay() { return *reinterpret_cast<const float*>(std::uintptr_t(0x0079BE80)); }
float AppearTime() { return *reinterpret_cast<const float*>(std::uintptr_t(0x00742F2C)); }
POINT CursorOffset() { return *reinterpret_cast<const POINT*>(std::uintptr_t(0x007C8258)); }

// The extent of `text` in interface font `font` (005F8340, Rebirth-patched: through its address; it
// takes the CString by value and releases it, so a copy's data is handed over).
SIZE TextExtent(CInterface3D* ui, const mfc::CString& text, int font) {
    mfc::CString copy(text);
    char* data;
    std::memcpy(&data, &copy, sizeof(data));
    mfc::CString empty;
    std::memcpy(&copy, &empty, sizeof(data));
    SIZE size;
    InterfaceTextExtentOriginal(ui, nullptr, &size, data, font);
    return size;
}
} // namespace

CUiTooltip* CUiTooltip::Construct(CInterface3D* ui, CUiRegion* region, const char* text, int flag, int extra) {
    CUiWindow::Construct(ui);
    lines_.Construct();
    vtable_ = reinterpret_cast<void**>(kTooltipVtable);
    inputVtable_ = reinterpret_cast<void**>(kTooltipInputVtable);
    timer_ = 0.0f;
    state_ = 0;
    region_ = region;
    // An empty text leaves the measurements and the two arguments unwritten.
    if (!text || !*text) return this;
    lines_.SetSize(0, -1);
    StringSplit(text, &lines_, kLineSeparator);
    width_ = 0;
    height_ = 0;
    lineHeight_ = 0;
    flag_ = flag;
    extra_ = extra;
    keyName_ = nullptr;
    keyWidth_ = 0;
    for (int i = 0; i < lines_.size; ++i) {
        // An empty line (but the last) is as high as the sample and has no width.
        SIZE size;
        int width;
        if (lines_[i].GetLength() == 0 && i < lines_.size - 1) {
            size = TextExtent(interface_, mfc::CString(EmptyLineSample()), kFont);
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
    return this;
}
EI_REPLACE_METHOD(0x0065E320, CUiTooltip::Construct);

void CUiTooltip::DestroyBody() {
    lines_.Destruct();
    DestroyBase();
}
EI_REPLACE_METHOD(0x0065E4F0, CUiTooltip::DestroyBody);

void CUiTooltip::SetText(const char* text) {
    if (!text || !*text) return;
    lines_.SetSize(0, -1);
    StringSplit(text, &lines_, kLineSeparator);
    DrawLines();
}
EI_REPLACE_METHOD(0x0065E540, CUiTooltip::SetText);

void CUiTooltip::DrawLines() {
    if (!(surfaces_.size > 0)) return;
    CUiSurface* surface = surfaces_.data[0];
    if (!surface) return;
    SurfaceGetDC(surface);
    surface->Fill(kBackground);
    RECT whole{0, 0, 0, 0};   // empty: the whole surface
    surface->FrameRect(&whole, kTitleColour);
    int right = width_ > keyWidth_ ? width_ : keyWidth_;
    int step = lineHeight_;
    RECT row{kTextLeft, kTextTop, right - kTextLeft, step + kTextTop};
    for (int i = 0; i < lines_.size; ++i) {
        COLORREF colour = i >= flag_ ? kTextColour : kTitleColour;
        if (i == 0 && keyName_) {
            mfc::CString text = lines_[0] + KeyOpen() + keyName_ + KeyClose();
            SurfaceDrawText(surface, nullptr, text, &row, kFont, colour, 0, 0);
        } else {
            SurfaceDrawText(surface, nullptr, lines_[i], &row, kFont, colour, 0, 0);
        }
        gameapi::OffsetRect(&row, 0, step);
    }
    SurfaceReleaseDC(surface);
}
EI_REPLACE_METHOD(0x0065E880, CUiTooltip::DrawLines);

void CUiTooltip::OnMouseMoveVanilla(int x, int y) {
    switch (state_) {
    case kHovered: {
        const float delay = ShowDelay();
        if (lines_.size == 0 || delay == 0.0f) {
            state_ = kIdle;
            return;
        }
        if (timer_ < delay) {
            timer_ = RoundToFloat(WindowFrameTimeOriginal(this) + timer_);
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
        POINT offset = CursorOffset();
        // WideScreen (0065E77E: XOR ECX,ECX): without the margin; the layout's right edge 800 + extra
        // (addon's operands at 0065E7C3 / 0065E7CA).
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
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        AddSurface(memory ? SurfaceConstructOnRectOriginal(memory, nullptr, interface_, &rect, kSurfaceFormat) : nullptr);
        return;
    }
    case kAppearing:
        // The appearing time is zero in the exe: the tooltip is fully shown on the next call.
        if (timer_ < AppearTime()) {
            DrawLines();
            timer_ = RoundToFloat(WindowFrameTimeOriginal(this) + timer_);
        } else {
            state_ = kShown;
            timer_ = AppearTime();
            DrawLines();
        }
        return;
    case kDisappearing:
        if (timer_ > 0.0f) {
            DrawLines();
            timer_ = RoundToFloat(timer_ - WindowFrameTimeOriginal(this));
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
EI_REPLACE_METHOD(0x0065E580, CUiTooltip::OnMouseMoveVanilla);
EI_REBIRTH_IMPLEMENTS(0x0065E580, ei::rebirth::Patch::WideScreen);
