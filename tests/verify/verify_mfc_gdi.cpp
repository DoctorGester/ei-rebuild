// Checks for MFC's CDC and GDI objects (src/lib/mfc_gdi.cpp, tickets S8 and S8a). Members that draw
// or change the DC run ours only, and the DC's state afterwards is checked: the background mode and
// colour, the pen position, the text colour, the object selected. FromHandle, Attach, Detach and
// DeleteObject run ours only and check the object against the original's handle maps (the hook
// build keeps MFC's). CPen's constructor checks the pen it made; the point fonts run the original
// into a scratch CFont first and compare the two fonts' LOGFONTs; CDC's constructor runs against the
// original on a twin. The console scenario reaches the DC members (the console window's input line:
// ui_console_window.cpp); every scenario reaches the fonts (CInterface3D::Create) and the
// interface's DCs (FromHandle, SelectObject, SetTextColor, DrawText); the pens come with the hud and
// map screens.
#include "verify.h"

#include "../../src/lib/mfc_gdi.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

using ei::verify::Check;
using mfc::impl::CDC;
using mfc::impl::CFont;
using mfc::impl::CGdiObject;
using mfc::impl::CHandleMap;
using mfc::impl::CPen;
void* s_unused;

// The original's map of GDI objects (MFC's module thread state; not made here).
__declspec(naked) CHandleMap* __stdcall MapHGDIOBJAt(BOOL) EI_ORIGINAL(0x00709FEE)
void* PermanentObject(HGDIOBJ handle) {
    CHandleMap* map = MapHGDIOBJAt(FALSE);
    return map ? map->permanentMap.GetValueAt(handle) : nullptr;
}
// CFont's vtable (a scratch font for the original) and the temporary objects' vtables.
void* FontVtable() { return reinterpret_cast<void*>(0x0073BFB4); }
void* TempDCVtable() { return reinterpret_cast<void*>(0x007463E4); }
void* TempGdiObjectVtable() { return reinterpret_cast<void*>(0x0074645C); }

// The DC whose state the members leave readable (the attribute DC when there is one).
HDC StateDC(const CDC* dc) { return dc->attribDC ? dc->attribDC : dc->hdc; }

bool At(HDC dc, int x, int y) {
    POINT position;
    return !dc || (::GetCurrentPositionEx(dc, &position) && position.x == x && position.y == y);
}

// ---- S8: the members without a handle map ----------------------------------------------------------

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

// ---- S8a: the handle maps' users -------------------------------------------------------------------

// 00708CD7: MOV EAX,ECX; XOR ECX,ECX; MOV dword ptr [EAX],imm32 (10 bytes).
CDC*(__fastcall* s_construct)(CDC*);
CDC* __fastcall ConstructVerify(CDC* self) {
    CDC twin;
    std::memcpy(&twin, self, sizeof(twin));
    CDC* expected = s_construct(&twin);
    CDC* actual = self->Construct();
    Check("CDC::CDC", true, expected == &twin && actual == self && std::memcmp(&twin, self, sizeof(twin)) == 0, "");
    return actual;
}

CDC* __stdcall DCFromHandleVerify(HDC dc) {
    CDC* result = CDC::FromHandle(dc);
    Check("CDC::FromHandle", true, dc ? result && result->hdc == dc : !result, "%p: %p", dc, result);
    return result;
}

void* __stdcall CreateTempDCVerify() {
    auto* dc = static_cast<CDC*>(mfc::impl::CreateTempDC());
    Check("CTempDC::CreateObject", true, dc && dc->vtable == TempDCVtable() && !dc->hdc && !dc->attribDC && !dc->printing, "%p", dc);
    return dc;
}

CGdiObject* __stdcall GdiFromHandleVerify(HGDIOBJ handle) {
    CGdiObject* result = CGdiObject::FromHandle(handle);
    Check("CGdiObject::FromHandle", true, handle ? result && result->handle == handle : !result, "%p: %p", handle, result);
    return result;
}

void* __stdcall CreateTempGdiObjectVerify() {
    auto* object = static_cast<CGdiObject*>(mfc::impl::CreateTempGdiObject());
    Check("CTempGdiObject::CreateObject", true, object && object->vtable == TempGdiObjectVtable() && !object->handle, "%p", object);
    return object;
}

BOOL __fastcall AttachVerify(CGdiObject* self, void*, HGDIOBJ handle) {
    const BOOL result = self->Attach(handle);
    Check("CGdiObject::Attach", true, handle ? result && self->handle == handle && PermanentObject(handle) == self : !result, "%p", handle);
    return result;
}

HGDIOBJ __fastcall DetachVerify(CGdiObject* self) {
    const HGDIOBJ before = self->handle;
    const HGDIOBJ result = self->Detach();
    Check("CGdiObject::Detach", true, result == before && !self->handle && (!before || PermanentObject(before) != self), "%p", before);
    return result;
}

BOOL __fastcall DeleteObjectVerify(CGdiObject* self) {
    const HGDIOBJ before = self->handle;
    const BOOL result = self->DeleteObject();
    Check("CGdiObject::DeleteObject", true, !self->handle && (before || !result) && (!before || PermanentObject(before) != self), "%p", before);
    return result;
}

// The game's ~CGdiObject copies (ticket S16): ours only; no handle left, CGdiObject's vtable.
template <std::uintptr_t Address>
BOOL __fastcall DestructVerify(CGdiObject* self) {
    const HGDIOBJ before = self->handle;
    const BOOL result = self->Destruct();
    Check("~CGdiObject", true, !self->handle && self->vtable == CGdiObject::Vtable() && (before || !result), "%08X %p",
          static_cast<unsigned>(Address), before);
    return result;
}
// Their deleting destructors: ours only, this returned (the memory may be gone).
template <std::uintptr_t Address>
CGdiObject* __fastcall DeletingDestructVerify(CGdiObject* self, void*, unsigned flags) {
    CGdiObject* result = self->DeletingDestruct(flags);
    Check("~CGdiObject (deleting)", true, result == self, "%08X flags %u", static_cast<unsigned>(Address), flags);
    return result;
}

CPen* __fastcall PenConstructVerify(CPen* self, void*, int style, int width, COLORREF colour) {
    CPen* result = self->Construct(style, width, colour);
    LOGPEN pen{};
    const bool made = ::GetObjectA(self->handle, sizeof(pen), &pen) == sizeof(pen);
    Check("CPen::CPen", true,
          result == self && self->vtable == CPen::Vtable() && PermanentObject(self->handle) == self && made &&
              static_cast<int>(pen.lopnStyle) == style && pen.lopnColor == colour,
          "style %d, width %d, colour %06lx", style, width, colour);
    return result;
}

// The original's font, made into a scratch CFont: its LOGFONT, then the font deleted.
template <typename Make>
bool OriginalFont(LOGFONTA* font, Make make) {
    CFont scratch;
    scratch.vtable = FontVtable();
    scratch.handle = nullptr;
    const BOOL made = make(&scratch);
    const bool read = made && ::GetObjectA(scratch.handle, sizeof(*font), font) == sizeof(*font);
    scratch.DeleteObject();
    return read;
}

bool SameFont(const CFont* font, bool expectedMade, const LOGFONTA& expected) {
    if (!expectedMade) return !font->handle;
    LOGFONTA actual{};
    return font->handle && ::GetObjectA(font->handle, sizeof(actual), &actual) == sizeof(actual) &&
           std::memcmp(&expected, &actual, sizeof(actual)) == 0;
}

// 0070BBE5: PUSH EBP; MOV EBP,ESP; SUB ESP,0x3C (6 bytes).
BOOL(__fastcall* s_createPointFont)(CFont*, void*, int, const char*, CDC*);
BOOL __fastcall CreatePointFontVerify(CFont* self, void*, int pointSize, const char* faceName, CDC* dc) {
    LOGFONTA expected{};
    const bool made = OriginalFont(&expected, [&](CFont* scratch) { return s_createPointFont(scratch, nullptr, pointSize, faceName, dc); });
    const BOOL result = self->CreatePointFont(pointSize, faceName, dc);
    Check("CFont::CreatePointFont", true, (result != 0) == made && SameFont(self, made, expected), "%d, %s", pointSize, faceName);
    return result;
}

// 0070BC2A: PUSH EBP; MOV EBP,ESP; SUB ESP,0x50 (6 bytes).
BOOL(__fastcall* s_createPointFontIndirect)(CFont*, void*, const LOGFONTA*, CDC*);
BOOL __fastcall CreatePointFontIndirectVerify(CFont* self, void*, const LOGFONTA* font, CDC* dc) {
    LOGFONTA expected{};
    const bool made = OriginalFont(&expected, [&](CFont* scratch) { return s_createPointFontIndirect(scratch, nullptr, font, dc); });
    const BOOL result = self->CreatePointFontIndirect(font, dc);
    Check("CFont::CreatePointFontIndirect", true, (result != 0) == made && SameFont(self, made, expected), "%ld, %s", font->lfHeight,
          font->lfFaceName);
    return result;
}

// The object `handle` is now selected into the DC whose state the members leave readable.
bool Selected(const CDC* dc, HGDIOBJ handle) {
    const HDC state = StateDC(dc);
    return !state || !handle || ::GetCurrentObject(state, ::GetObjectType(handle)) == handle;
}

template <int Id>
struct SelectObjectVerify {
    static inline const char* name;
    static CGdiObject* __fastcall Verify(CDC* self, void*, CGdiObject* object) {
        const HGDIOBJ handle = object ? object->handle : nullptr;
        CGdiObject* result = self->SelectObject(object);
        Check(name, true, !result || Selected(self, handle), "%p", handle);
        return result;
    }
};
using SelectPenVerify = SelectObjectVerify<0>;
using SelectFontVerify = SelectObjectVerify<1>;

COLORREF __fastcall SetTextColorVerify(CDC* self, void*, COLORREF colour) {
    const COLORREF result = self->SetTextColor(colour);
    const HDC dc = StateDC(self);
    Check("CDC::SetTextColor", true, !dc || ::GetTextColor(dc) == colour, "%06lx", colour);
    return result;
}

// Draws: nothing to compare (the wrapper counts the calls).
int __fastcall DrawTextVerify(CDC* self, void*, const char* text, int length, RECT* rect, UINT format) {
    const int result = self->DrawText(text, length, rect, format);
    Check("CDC::DrawText", true, true, "");
    return result;
}

struct Names {
    Names() {
        SelectPenVerify::name = "CDC::SelectObject(CPen*)";
        SelectFontVerify::name = "CDC::SelectObject(CFont*)";
    }
} s_names;

} // namespace

EI_VERIFY(0x007090C2, SetBkModeVerify, 0, s_unused);
EI_VERIFY(0x00709613, MoveToVerify, 0, s_unused);
EI_VERIFY(0x0070965F, LineToVerify, 0, s_unused);
EI_VERIFY(0x0070BADA, FillSolidRectVerify, 0, s_unused);
EI_VERIFY(0x0070BB04, FillSolidAreaVerify, 0, s_unused);
EI_VERIFY(0x00708CD7, ConstructVerify, 10, s_construct);
EI_VERIFY(0x00708D79, DCFromHandleVerify, 0, s_unused);
EI_VERIFY(0x0070BFD7, CreateTempDCVerify, 0, s_unused);
EI_VERIFY(0x0070A060, GdiFromHandleVerify, 0, s_unused);
EI_VERIFY(0x0070C028, CreateTempGdiObjectVerify, 0, s_unused);
EI_VERIFY(0x0070A075, AttachVerify, 0, s_unused);
EI_VERIFY(0x0070A0A2, DetachVerify, 0, s_unused);
EI_VERIFY(0x0070A0CC, DeleteObjectVerify, 0, s_unused);
EI_VERIFY(0x00444B30, DestructVerify<0x00444B30>, 0, s_unused);
EI_VERIFY(0x00444B90, DestructVerify<0x00444B90>, 0, s_unused);
EI_VERIFY(0x005FDDE0, DestructVerify<0x005FDDE0>, 0, s_unused);
EI_VERIFY(0x00444B10, DeletingDestructVerify<0x00444B10>, 0, s_unused);
EI_VERIFY(0x00444B70, DeletingDestructVerify<0x00444B70>, 0, s_unused);
EI_VERIFY(0x005FDDC0, DeletingDestructVerify<0x005FDDC0>, 0, s_unused);
EI_VERIFY(0x0070A0E2, PenConstructVerify, 0, s_unused);
EI_VERIFY(0x0070BBE5, CreatePointFontVerify, 6, s_createPointFont);
EI_VERIFY(0x0070BC2A, CreatePointFontIndirectVerify, 6, s_createPointFontIndirect);
EI_VERIFY(0x00708F5A, SelectPenVerify::Verify, 0, s_unused);
EI_VERIFY(0x00708FE6, SelectFontVerify::Verify, 0, s_unused);
EI_VERIFY(0x0070917A, SetTextColorVerify, 0, s_unused);
EI_VERIFY(0x006E6A1C, DrawTextVerify, 0, s_unused);
