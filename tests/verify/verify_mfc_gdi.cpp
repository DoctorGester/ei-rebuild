// Checks for MFC's CDC members without a handle map (src/lib/mfc_gdi.cpp, ticket S8). They draw or
// change the DC, so they run ours only, and the DC's state afterwards is checked: the background
// mode and colour, the pen position. The console scenario reaches them (the console window's input
// line: ui_console_window.cpp).
#include "verify.h"

#include "../../src/lib/mfc_gdi.h"

namespace {

using ei::verify::Check;
using mfc::impl::CDC;
void* s_unused;

// The DC whose state the members leave readable (the attribute DC when there is one).
HDC StateDC(const CDC* dc) { return dc->attribDC ? dc->attribDC : dc->hdc; }

bool At(HDC dc, int x, int y) {
    POINT position;
    return !dc || (::GetCurrentPositionEx(dc, &position) && position.x == x && position.y == y);
}

int __fastcall SetBkModeVerify(CDC* self, void*, int mode) {
    const int result = self->SetBkMode(mode);
    const HDC dc = StateDC(self);
    Check("CDC::SetBkMode", true, !dc || ::GetBkMode(dc) == mode, "%d", mode);
    return result;
}

POINT* __fastcall MoveToVerify(CDC* self, void*, POINT* result, int x, int y) {
    *result = self->MoveTo(x, y);
    Check("CDC::MoveTo", true, At(self->hdc, x, y) && At(self->attribDC, x, y), "%d, %d", x, y);
    return result;
}

BOOL __fastcall LineToVerify(CDC* self, void*, int x, int y) {
    const BOOL result = self->LineTo(x, y);
    Check("CDC::LineTo", true, !result || (At(self->hdc, x, y) && At(self->attribDC, x, y)), "%d, %d", x, y);
    return result;
}

void __fastcall FillSolidRectVerify(CDC* self, void*, const RECT* rect, COLORREF colour) {
    self->FillSolidRect(rect, colour);
    Check("CDC::FillSolidRect(const RECT*)", true, !self->hdc || ::GetBkColor(self->hdc) == colour, "%06lx", colour);
}

void __fastcall FillSolidAreaVerify(CDC* self, void*, int x, int y, int cx, int cy, COLORREF colour) {
    self->FillSolidArea(x, y, cx, cy, colour);
    Check("CDC::FillSolidRect(x, y, cx, cy)", true, !self->hdc || ::GetBkColor(self->hdc) == colour, "%d, %d, %d, %d: %06lx", x, y, cx, cy,
          colour);
}

} // namespace

EI_VERIFY(0x007090C2, SetBkModeVerify, 0, s_unused);
EI_VERIFY(0x00709613, MoveToVerify, 0, s_unused);
EI_VERIFY(0x0070965F, LineToVerify, 0, s_unused);
EI_VERIFY(0x0070BADA, FillSolidRectVerify, 0, s_unused);
EI_VERIFY(0x0070BB04, FillSolidAreaVerify, 0, s_unused);
