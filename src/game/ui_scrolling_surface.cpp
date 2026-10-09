// The titles' scrolling text surface (CUiScrollingSurface, ui_screens.h): rendering its RichEdit
// text into the surface. Its constructor (0065ED30), Load (0065EF30) and Start (0065F1C0) are not
// rewritten (ui_credits.cpp forwards them). See docs/notes/items_spells.md (A-H17).
#include "ui_screens.h"

#include "originals_ui.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/gameapi.h"

#include <richedit.h>

namespace {
constexpr int kTwipsPerInch = 1440;
constexpr int kExtraTwipsBelow = 1000;   // formatted below the visible height
} // namespace

void CUiScrollingSurface::RenderText() {
    ui_gdi::GameDC* dc = SurfaceGetDC(this);
    SurfaceClearOriginal(this);
    gameapi::SendMessageA(richEdit_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(unknown_34));   // +0x34: the colour
    int width = gameapi::MulDiv(rect_.right - rect_.left, kTwipsPerInch, gameapi::GetDeviceCaps(dc->attribDC_, LOGPIXELSX));
    int height = gameapi::MulDiv(rect_.bottom - rect_.top, kTwipsPerInch, gameapi::GetDeviceCaps(dc->attribDC_, LOGPIXELSY));
    RECT area = {0, 0, width, offset_ + kExtraTwipsBelow + height};
    gameapi::OffsetRect(&area, -offsetX_, -offset_);
    FORMATRANGE range;
    range.hdc = dc->hdc_;
    range.hdcTarget = dc->hdc_;
    range.rc = area;
    range.rcPage = area;
    range.chrg.cpMin = 0;
    range.chrg.cpMax = -1;
    LRESULT fitted = gameapi::SendMessageA(richEdit_, EM_FORMATRANGE, 1, reinterpret_cast<LPARAM>(&range));
    if (endOffset_ > 0) {
        if (offset_ > endOffset_ + kExtraTwipsBelow + height) {
            speed_ = 0.0f;
            endOffset_ = 0;
        }
    } else if (fitted >= textLength_) {
        endOffset_ = offset_;
    }
    SurfaceReleaseDC(this);
}
EI_REPLACE_METHOD(0x0065F020, CUiScrollingSurface::RenderText);
