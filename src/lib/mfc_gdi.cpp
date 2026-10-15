// MFC 4.2's CDC members without a handle map, registered at their library addresses (mfc_gdi.h).
#include "mfc_gdi.h"

#include "ei/hook.h"
#include "gameapi.h"

namespace mfc::impl {

// Set on the output DC (when it is not the attribute DC) and on the attribute DC; the attribute
// DC's old mode is returned.
int CDC::SetBkMode(int mode) {
    int old = 0;
    if (hdc != attribDC) old = gameapi::SetBkMode(hdc, mode);
    if (attribDC) old = gameapi::SetBkMode(attribDC, mode);
    return old;
}

POINT CDC::MoveTo(int x, int y) {
    POINT old{};
    if (hdc != attribDC) gameapi::MoveToEx(hdc, x, y, &old);
    if (attribDC) gameapi::MoveToEx(attribDC, x, y, &old);
    return old;
}

// The attribute DC follows the pen position; the line is drawn on the output DC.
BOOL CDC::LineTo(int x, int y) {
    if (attribDC && hdc != attribDC) gameapi::MoveToEx(attribDC, x, y, nullptr);
    return gameapi::LineTo(hdc, x, y);
}

// An opaque ExtTextOut of no text in the colour.
void CDC::FillSolidRect(const RECT* rect, COLORREF colour) {
    gameapi::SetBkColor(hdc, colour);
    gameapi::ExtTextOutA(hdc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
}

void CDC::FillSolidArea(int x, int y, int cx, int cy, COLORREF colour) {
    gameapi::SetBkColor(hdc, colour);
    const RECT rect{x, y, x + cx, y + cy};
    gameapi::ExtTextOutA(hdc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
}

EI_REPLACE_METHOD(0x007090C2, CDC::SetBkMode);
EI_REPLACE_METHOD(0x00709613, CDC::MoveTo);
EI_REPLACE_METHOD(0x0070965F, CDC::LineTo);
EI_REPLACE_METHOD(0x0070BADA, CDC::FillSolidRect);
EI_REPLACE_METHOD(0x0070BB04, CDC::FillSolidArea);

} // namespace mfc::impl
