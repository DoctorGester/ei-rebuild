// MFC 4.2's CDC and GDI objects (mfc_gdi.h).
#include "mfc_gdi.h"

#include "crt.h"
#include "gameapi.h"
#include "mfc_exception.h"
#include "mfc_runtime_class.h"
#include "mfc_string.h"
#include "game/game_data.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace mfc::impl {

constinit lib::StaticObject<CFixedAlloc> g_tempGdiObjectAlloc = /*007C8CF0*/ {};
constinit lib::StaticObject<CFixedAlloc> g_tempDCAlloc = /*007C8D18*/ {};

namespace {

// The fixed allocators of the temporary objects (mfc_gdi.h).
CFixedAlloc& TempDCAlloc() { return *g_tempDCAlloc; }
CFixedAlloc& TempGdiObjectAlloc() { return *g_tempGdiObjectAlloc; }

// CTempGdiObject: its memory goes back to the fixed allocator.
class CTempGdiObject final : public CGdiObject {
public:
    // ~CGdiObject.
    const CRuntimeClass* GetRuntimeClass() const override { return &kClassCTempGdiObject; }
    static void* operator new(std::size_t) noexcept { return TempGdiObjectAlloc().Alloc(); }
    static void operator delete(void* block) { TempGdiObjectAlloc().Free(block); }
};
// CTempDC: ~CDC, the memory back to the fixed allocator.
class CTempDC final : public CDC {
public:
    const CRuntimeClass* GetRuntimeClass() const override { return &kClassCTempDC; }
    static void* operator new(std::size_t) noexcept { return TempDCAlloc().Alloc(); }
    static void operator delete(void* block) { TempDCAlloc().Free(block); }
};

// A map of the image's thread, made as afxMapHDC / afxMapHGDIOBJ make it (under the critical new
// handler). The thread state's destructor frees MFC's; the image's live until the process ends.
CHandleMap* MakeMap(CHandleMap*& map, BOOL create, const CRuntimeClass* objectClass, unsigned handleOffset, int handleCount) {
    if (!map && create) {
        const gamecrt::NewHandler old = SetAfxNewHandler(CriticalNewHandler);
        map = new CHandleMap(objectClass, handleOffset, handleCount);
        SetAfxNewHandler(old);
    }
    return map;
}

} // namespace

const CRuntimeClass* CGdiObject::GetRuntimeClass() const { return &kClassCGdiObject; }
const CRuntimeClass* CBrush::GetRuntimeClass() const { return &kClassCBrush; }
const CRuntimeClass* CPen::GetRuntimeClass() const { return &kClassCPen; }
const CRuntimeClass* CFont::GetRuntimeClass() const { return &kClassCFont; }
const CRuntimeClass* CDC::GetRuntimeClass() const { return &kClassCDC; }

// ---- The maps (image only)------------------------------------------------------------------------

CHandleMap* __stdcall MapHDC(BOOL create) {
    static thread_local CHandleMap* map = nullptr;
    return MakeMap(map, create, &kClassCTempDC, offsetof(CDC, hdc), 2);
}

CHandleMap* __stdcall MapHGDIOBJ(BOOL create) {
    static thread_local CHandleMap* map = nullptr;
    return MakeMap(map, create, &kClassCTempGdiObject, offsetof(CGdiObject, handle), 1);
}

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

void* __stdcall CreateCGdiObject() { return new CGdiObject(); }

void* __stdcall CreateTempGdiObject() { return new CTempGdiObject(); }

// A failure unwinds through ~CGdiObject (its DeleteObject finds no handle).
CPen::CPen(int style, int width, COLORREF colour) {
    if (!Attach(gameapi::CreatePen(style, width, colour))) ThrowResourceException();
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

CGdiObject::~CGdiObject() { DeleteObject(); }

CDC::~CDC() {
    if (hdc) gameapi::DeleteDC(Detach());
}

// The attribute DC is released through slot 5, which no class of the game overrides.
HDC CDC::Detach() {
    const HDC dc = hdc;
    if (dc) {
        if (CHandleMap* map = mfc::impl::MapHDC(FALSE)) map->permanentMap.RemoveKey(dc);
    }
    ReleaseAttribDC();
    hdc = nullptr;
    return dc;
}

void CDC::SetAttribDC(HDC dc) { attribDC = dc; }
void CDC::SetOutputDC(HDC dc) { hdc = dc; }
void CDC::ReleaseAttribDC() { attribDC = nullptr; }
void CDC::ReleaseOutputDC() { hdc = nullptr; }

CDC* __stdcall CDC::FromHandle(HDC dc) {
    return static_cast<CDC*>(mfc::impl::MapHDC(TRUE)->FromHandle(dc));
}

void* __stdcall CreateCDC() { return new CDC(); }

void* __stdcall CreateTempDC() { return new CTempDC(); }

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

} // namespace mfc::impl
