// MFC 4.2's CDC and GDI objects, registered at their library addresses (mfc_gdi.h).
#include "mfc_gdi.h"

#include "ei/hook.h"
#include "crt.h"
#include "gameapi.h"
#include "mfc_exception.h"
#include "mfc_string.h"
#include "game/game_data.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

namespace {

// The runtime classes of the temporary objects, their fixed allocators and CTempDC's vtable.
CRuntimeClass* TempDCClass() { return ei::GamePointer<CRuntimeClass>(0x00745FB0); }
CRuntimeClass* TempGdiObjectClass() { return ei::GamePointer<CRuntimeClass>(0x00745F88); }
CFixedAlloc& TempDCAlloc() { return ei::GameData<CFixedAlloc>(0x007C8D18); }
CFixedAlloc& TempGdiObjectAlloc() { return ei::GameData<CFixedAlloc>(0x007C8CF0); }
void* TempDCVtable() { return ei::GamePointer<void>(0x007463E4); }
void* TempGdiObjectVtable() { return ei::GamePointer<void>(0x0074645C); }

// A map of the image's thread, made as afxMapHDC / afxMapHGDIOBJ make it (under the critical new
// handler). The thread state's destructor frees MFC's; the image's live until the process ends.
CHandleMap* MakeMap(CHandleMap*& map, BOOL create, CRuntimeClass* objectClass, unsigned handleOffset, int handleCount) {
    if (!map && create) {
        const gamecrt::NewHandler old = SetAfxNewHandler(CriticalNewHandlerAt());
        void* memory = gamecrt::operator_new(sizeof(CHandleMap));
        map = memory ? static_cast<CHandleMap*>(memory)->Construct(objectClass, handleOffset, handleCount) : nullptr;
        SetAfxNewHandler(old);
    }
    return map;
}

} // namespace

// ---- The maps (image only) ------------------------------------------------------------------------

CHandleMap* __stdcall MapHDC(BOOL create) {
    static thread_local CHandleMap* map = nullptr;
    return MakeMap(map, create, TempDCClass(), offsetof(CDC, hdc), 2);
}

CHandleMap* __stdcall MapHGDIOBJ(BOOL create) {
    static thread_local CHandleMap* map = nullptr;
    return MakeMap(map, create, TempGdiObjectClass(), offsetof(CGdiObject, handle), 1);
}

EI_REWRITTEN(0x00708D07, MapHDC);
EI_REWRITTEN(0x00709FEE, MapHGDIOBJ);
// ---- CGdiObject, CPen, CFont ----------------------------------------------------------------------

CGdiObject* __stdcall CGdiObject::FromHandle(HGDIOBJ object) {
    return static_cast<CGdiObject*>(mfc::impl::MapHGDIOBJ(TRUE)->FromHandle(object));
}

BOOL CGdiObject::Attach(HGDIOBJ object) {
    if (!object) return FALSE;
    CHandleMap* map = mfc::impl::MapHGDIOBJ(TRUE);
    handle = object;
    *map->permanentMap.At(object) = this;
    return TRUE;
}

HGDIOBJ CGdiObject::Detach() {
    const HGDIOBJ object = handle;
    if (object) {
        if (CHandleMap* map = mfc::impl::MapHGDIOBJ(FALSE)) map->permanentMap.RemoveKey(handle);
    }
    handle = nullptr;
    return object;
}

BOOL CGdiObject::DeleteObject() {
    if (!handle) return FALSE;
    return gameapi::DeleteObject(Detach());
}

void* __stdcall CreateTempGdiObject() {
    auto* object = static_cast<CGdiObject*>(TempGdiObjectAlloc().Alloc());
    if (!object) return nullptr;
    object->handle = nullptr;
    object->vtable = TempGdiObjectVtable();
    return object;
}

CPen* CPen::Construct(int style, int width, COLORREF colour) {
    handle = nullptr;
    vtable = Vtable();
    if (!Attach(gameapi::CreatePen(style, width, colour))) {
        vtable = CGdiObject::Vtable();   // the unwind's ~CGdiObject (its DeleteObject finds no handle)
        ThrowResourceException();
    }
    return this;
}

BOOL CFont::CreatePointFont(int pointSize, const char* faceName, CDC* dc) {
    LOGFONTA font;
    std::memset(&font, 0, sizeof(font));
    font.lfHeight = pointSize;
    font.lfCharSet = DEFAULT_CHARSET;
    gameapi::lstrcpynA(font.lfFaceName, faceName, LF_FACESIZE);
    return CreatePointFontIndirect(&font, dc);
}

// The height: device pixels from LOGPIXELSY (tenths of a point, 720 per inch), then the distance
// from the origin in logical units, negative (a character height).
BOOL CFont::CreatePointFontIndirect(const LOGFONTA* source, CDC* dc) {
    const HDC hdc = dc ? dc->attribDC : gameapi::GetDC(nullptr);
    LOGFONTA font = *source;
    POINT height{0, gameapi::GetDeviceCaps(hdc, LOGPIXELSY) * font.lfHeight / 720};
    gameapi::DPtoLP(hdc, &height, 1);
    POINT origin{0, 0};
    gameapi::DPtoLP(hdc, &origin, 1);
    font.lfHeight = -std::abs(height.y - origin.y);
    if (!dc) gameapi::ReleaseDC(nullptr, hdc);
    return Attach(gameapi::CreateFontIndirectA(&font));
}

void CGdiObject::Destruct() {
    vtable = Vtable();
    DeleteObject();
}

CGdiObject* CGdiObject::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

EI_REWRITTEN(0x00444B30, CGdiObject::Destruct);
EI_REWRITTEN(0x00444B90, CGdiObject::Destruct);
EI_REWRITTEN(0x005FDDE0, CGdiObject::Destruct);
EI_REPLACE_METHOD(0x00444B10, CGdiObject::DeletingDestruct);
EI_REPLACE_METHOD(0x00444B70, CGdiObject::DeletingDestruct);
EI_REPLACE_METHOD(0x005FDDC0, CGdiObject::DeletingDestruct);
EI_REWRITTEN(0x0070A060, CGdiObject::FromHandle);
EI_REWRITTEN(0x0070A075, CGdiObject::Attach);
EI_REWRITTEN(0x0070A0A2, CGdiObject::Detach);
EI_REWRITTEN(0x0070A0CC, CGdiObject::DeleteObject);
EI_REPLACE(0x0070C028, CreateTempGdiObject);
EI_REWRITTEN(0x0070A0E2, CPen::Construct);
EI_REWRITTEN(0x0070BBE5, CFont::CreatePointFont);
EI_REWRITTEN(0x0070BC2A, CFont::CreatePointFontIndirect);

// ---- CDC ------------------------------------------------------------------------------------------

CDC* CDC::Construct() {
    vtable = Vtable();
    hdc = nullptr;
    attribDC = nullptr;
    printing = FALSE;
    return this;
}

CDC* __stdcall CDC::FromHandle(HDC dc) {
    return static_cast<CDC*>(mfc::impl::MapHDC(TRUE)->FromHandle(dc));
}

void* __stdcall CreateTempDC() {
    auto* dc = static_cast<CDC*>(TempDCAlloc().Alloc());
    if (!dc) return nullptr;
    dc->Construct();
    dc->vtable = TempDCVtable();
    return dc;
}

CGdiObject* CDC::SelectObject(CGdiObject* object) {
    const HGDIOBJ selected = object ? object->handle : nullptr;
    HGDIOBJ old = nullptr;
    if (hdc != attribDC) old = gameapi::SelectObject(hdc, selected);
    if (attribDC) old = gameapi::SelectObject(attribDC, selected);
    return CGdiObject::FromHandle(old);
}

COLORREF CDC::SetTextColor(COLORREF colour) {
    COLORREF old = CLR_INVALID;
    if (hdc != attribDC) old = gameapi::SetTextColor(hdc, colour);
    if (attribDC) old = gameapi::SetTextColor(attribDC, colour);
    return old;
}

int CDC::DrawText(const char* text, int length, RECT* rect, UINT format) {
    return gameapi::DrawTextA(hdc, text, length, rect, format);
}

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

EI_REWRITTEN(0x00708CD7, CDC::Construct);
EI_REWRITTEN(0x00708D79, CDC::FromHandle);
EI_REPLACE(0x0070BFD7, CreateTempDC);
EI_REWRITTEN(0x00708F5A, CDC::SelectObject);
EI_REPLACE_METHOD(0x00708FE6, CDC::SelectObject);
EI_REPLACE_METHOD(0x0070917A, CDC::SetTextColor);
EI_REPLACE_METHOD(0x006E6A1C, CDC::DrawText);
EI_REWRITTEN(0x007090C2, CDC::SetBkMode);
EI_REWRITTEN(0x00709613, CDC::MoveTo);
EI_REWRITTEN(0x0070965F, CDC::LineTo);
EI_REWRITTEN(0x0070BADA, CDC::FillSolidRect);
EI_REWRITTEN(0x0070BB04, CDC::FillSolidArea);

} // namespace mfc::impl
