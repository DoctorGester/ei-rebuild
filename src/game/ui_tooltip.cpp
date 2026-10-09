// A region's tooltip (CUiTooltip, 0065E320-0065EA50; A-H20): its constructor, which splits the text
// into lines and measures them, the destructor body, SetText and the drawing of the lines. Showing it
// (0065E580) is Rebirth-patched and stays original. See ui.h and docs/notes/items_spells.md.
#include "ui.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
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
    unknown_17C = 0;
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
