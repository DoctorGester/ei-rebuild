// MFC 4.2's CDC and GDI objects, registered at their library addresses (mfc_gdi.h).
#include "mfc_gdi.h"

#include "ei/hook.h"
#include "compiled_vtable.h"
#include "crt.h"
#include "gameapi.h"
#include "mfc_exception.h"
#include "mfc_runtime_class.h"
#include "mfc_string.h"
#include "game/game_data.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

constinit lib::StaticObject<CFixedAlloc> g_tempGdiObjectAlloc = /*007C8CF0*/ {};
constinit lib::StaticObject<CFixedAlloc> g_tempDCAlloc = /*007C8D18*/ {};

namespace {

// The fixed allocators of the temporary objects (mfc_gdi.h).
CFixedAlloc& TempDCAlloc() { return *g_tempDCAlloc; }
CFixedAlloc& TempGdiObjectAlloc() { return *g_tempGdiObjectAlloc; }

// ---- The compiled tables (C19) --------------------------------------------------------------------

// CGdiObject's and its classes' tables: 0 GetRuntimeClass (the class's record), 1 the scalar deleting destructor
// (~CGdiObject: the GDI object deleted; the classes add no destructor of their own), 2 Serialize (CObject's, empty).
template <typename Self, const CRuntimeClass* kClass>
struct GdiObjectTable : ei::CompiledTag<Self> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return kClass; }   // 0
    virtual void* DeleteScalar(unsigned flags) {                              // 1
        return this->template ObjectAs<CGdiObject>()->DeletingDestruct(flags);
    }
    virtual void Serialize(void*) {}                                          // 2: 00444B00
};
// game.exe's 007463D4 (slots 0070BFAD, 00444B70), 007463C4 (0070BFB9, 005FDDC0), 007463B4 (0070BFB3, 006E6963: ~CPen
// is 005E38F0, ~CGdiObject's body).
struct PlainGdiObjectTable final : GdiObjectTable<PlainGdiObjectTable, &kClassCGdiObject> {};
struct BrushTable final : GdiObjectTable<BrushTable, &kClassCBrush> {};
struct PenTable final : GdiObjectTable<PenTable, &kClassCPen> {};
// game.exe's 0073BFB4 (0070BFBF, 00444B10): the game's CFont, whose deleting destructor the game's code holds.
struct FontTable final : GdiObjectTable<FontTable, &kClassCFont> {};
// CTempGdiObject (game.exe's 0074645C: 0070C08D, 0070C044): its memory goes back to the fixed allocator.
struct TempGdiObjectTable final : GdiObjectTable<TempGdiObjectTable, &kClassCTempGdiObject> {
    void* DeleteScalar(unsigned flags) override {
        auto* self = ObjectAs<CGdiObject>();
        self->Destruct();   // 0070C064
        if (flags & 1) TempGdiObjectAlloc().Free(self);
        return self;
    }
};

// CDC's 29 slots (MFC 4.2's CDC virtuals in declaration order). The ones the image reaches have bodies: the
// interface calls 10 / 12 / 26 of its surfaces' CTempDCs (ui_gdi.h), CHandleMap::DeleteTemp slot 1, the destructor
// slot 5; 0, 2-4 and 6 are as short. The rest are unported (config/standalone_unreachable.csv).
template <typename Self, const CRuntimeClass* kClass>
struct DCTable : ei::CompiledTag<Self> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return kClass; }                         // 0
    virtual void* DeleteScalar(unsigned flags) = 0;                                                 // 1
    virtual void Serialize(void*) {}                                                                // 2: 00444B00
    virtual void SetAttribDC(HDC dc) { This()->SetAttribDC(dc); }                                   // 3: 00708E42
    virtual void SetOutputDC(HDC dc) { This()->SetOutputDC(dc); }                                   // 4: 00708E4C
    virtual void ReleaseAttribDC() { This()->ReleaseAttribDC(); }                                   // 5: 00708E56
    virtual void ReleaseOutputDC() { This()->ReleaseOutputDC(); }                                   // 6: 00708E5B
    virtual int SaveDC() { ei::UnportedSlot(0x00708E98); }                                          // 7
    virtual BOOL RestoreDC(int) { ei::UnportedSlot(0x00708EC9); }                                   // 8
    virtual CGdiObject* SelectStockObject(int) { ei::UnportedSlot(0x00708F1E); }                    // 9
    virtual CGdiObject* SelectObject(CGdiObject* font) { return This()->SelectObject(font); }       // 10: 00708FE6
    virtual COLORREF SetBkColor(COLORREF) { ei::UnportedSlot(0x00709093); }                         // 11
    virtual COLORREF SetTextColor(COLORREF colour) { return This()->SetTextColor(colour); }         // 12: 0070917A
    virtual int SetMapMode(int) { ei::UnportedSlot(0x007091A9); }                                   // 13
    virtual POINT SetViewportOrg(int, int) { ei::UnportedSlot(0x007091D7); }                        // 14
    virtual POINT OffsetViewportOrg(int, int) { ei::UnportedSlot(0x00709223); }                     // 15
    virtual SIZE SetViewportExt(int, int) { ei::UnportedSlot(0x0070926F); }                         // 16
    virtual SIZE ScaleViewportExt(int, int, int, int) { ei::UnportedSlot(0x007092BB); }             // 17
    virtual SIZE SetWindowExt(int, int) { ei::UnportedSlot(0x007093AB); }                           // 18
    virtual SIZE ScaleWindowExt(int, int, int, int) { ei::UnportedSlot(0x007093F7); }               // 19
    virtual int GetClipBox(RECT*) const { ei::UnportedSlot(0x0070944F); }                           // 20
    virtual BOOL PtVisible(int, int) const { ei::UnportedSlot(0x006E697F); }                        // 21
    virtual BOOL RectVisible(const RECT*) const { ei::UnportedSlot(0x006E6993); }                   // 22
    virtual BOOL TextOut(int, int, const char*, int) { ei::UnportedSlot(0x006E69A3); }              // 23
    virtual BOOL ExtTextOut(int, int, UINT, const RECT*, const char*, UINT, int*) {                 // 24
        ei::UnportedSlot(0x006E69BF);
    }
    virtual SIZE TabbedTextOut(int, int, const char*, int, int, int*, int) { ei::UnportedSlot(0x006E69E4); }   // 25
    virtual int DrawText(const char* text, int length, RECT* rect, UINT format) {                   // 26: 006E6A1C
        return This()->DrawText(text, length, rect, format);
    }
    virtual BOOL GrayString(CBrush*, GRAYSTRINGPROC, LPARAM, int, int, int, int, int) { ei::UnportedSlot(0x006E6A38); }   // 27
    virtual int Escape(int, int, const char*, void*) { ei::UnportedSlot(0x006E6A68); }              // 28

protected:
    CDC* This() { return this->template ObjectAs<CDC>(); }
};
// game.exe's 007461B0 (0: 0070BF7B, 1: 00708CEB).
struct PlainDCTable final : DCTable<PlainDCTable, &kClassCDC> {
    void* DeleteScalar(unsigned flags) override { return This()->DeletingDestruct(flags); }
};
// CTempDC (game.exe's 007463E4; 0: 0070C022, 1: 0070BFFD): ~CDC, the memory back to the fixed allocator.
struct TempDCTable final : DCTable<TempDCTable, &kClassCTempDC> {
    void* DeleteScalar(unsigned flags) override {
        This()->Destruct();
        if (flags & 1) TempDCAlloc().Free(This());
        return This();
    }
};

// A map of the image's thread, made as afxMapHDC / afxMapHGDIOBJ make it (under the critical new
// handler). The thread state's destructor frees MFC's; the image's live until the process ends.
CHandleMap* MakeMap(CHandleMap*& map, BOOL create, const CRuntimeClass* objectClass, unsigned handleOffset, int handleCount) {
    if (!map && create) {
        const gamecrt::NewHandler old = SetAfxNewHandler(CriticalNewHandlerAt());
        CHandleMap* memory = gamecrt::operator_new_as<CHandleMap>();
        map = memory ? memory->Construct(objectClass, handleOffset, handleCount) : nullptr;
        SetAfxNewHandler(old);
    }
    return map;
}

} // namespace

void* CGdiObject::Vtable() { return PlainGdiObjectTable::Vtable(); }
void* CBrush::Vtable() { return BrushTable::Vtable(); }
void* CPen::Vtable() { return PenTable::Vtable(); }
void* CFont::Vtable() { return FontTable::Vtable(); }
void* CDC::Vtable() { return PlainDCTable::Vtable(); }

// ---- The maps (image only)------------------------------------------------------------------------

CHandleMap* __stdcall MapHDC(BOOL create) {
    static thread_local CHandleMap* map = nullptr;
    return MakeMap(map, create, &kClassCTempDC, offsetof(CDC, hdc), 2);
}

CHandleMap* __stdcall MapHGDIOBJ(BOOL create) {
    static thread_local CHandleMap* map = nullptr;
    return MakeMap(map, create, &kClassCTempGdiObject, offsetof(CGdiObject, handle), 1);
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

void* __stdcall CreateCGdiObject() {
    auto* object = gamecrt::operator_new_as<CGdiObject>();
    if (!object) return nullptr;
    object->vtable = CGdiObject::Vtable();
    object->handle = nullptr;
    return object;
}

void* __stdcall CreateTempGdiObject() {
    auto* object = static_cast<CGdiObject*>(TempGdiObjectAlloc().Alloc());
    if (!object) return nullptr;
    object->handle = nullptr;
    object->vtable = TempGdiObjectTable::Vtable();
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
EI_REWRITTEN(0x00444B10, CGdiObject::DeletingDestruct);
EI_REPLACE_METHOD(0x00444B70, CGdiObject::DeletingDestruct);
EI_REPLACE_METHOD(0x005FDDC0, CGdiObject::DeletingDestruct);
EI_REWRITTEN(0x0070A060, CGdiObject::FromHandle);
EI_REWRITTEN(0x0070A075, CGdiObject::Attach);
EI_REWRITTEN(0x0070A0A2, CGdiObject::Detach);
EI_REWRITTEN(0x0070A0CC, CGdiObject::DeleteObject);
EI_REPLACE(0x0070C028, CreateTempGdiObject);
EI_REPLACE(0x0070BF93, CreateCGdiObject);
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

void CDC::Destruct() {
    vtable = Vtable();
    if (hdc) gameapi::DeleteDC(Detach());
}

CDC* CDC::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// The original releases the attribute DC through slot 5, which no class of the game overrides.
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

void* __stdcall CreateCDC() {
    auto* dc = gamecrt::operator_new_as<CDC>();
    return dc ? dc->Construct() : nullptr;
}

void* __stdcall CreateTempDC() {
    auto* dc = static_cast<CDC*>(TempDCAlloc().Alloc());
    if (!dc) return nullptr;
    dc->Construct();
    dc->vtable = TempDCTable::Vtable();
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
EI_REWRITTEN(0x00708E0C, CDC::Destruct);
EI_REWRITTEN(0x00708CEB, CDC::DeletingDestruct);
EI_REWRITTEN(0x00708DC5, CDC::Detach);
EI_REWRITTEN(0x00708E42, CDC::SetAttribDC);
EI_REWRITTEN(0x00708E4C, CDC::SetOutputDC);
EI_REWRITTEN(0x00708E56, CDC::ReleaseAttribDC);
EI_REWRITTEN(0x00708E5B, CDC::ReleaseOutputDC);
EI_REWRITTEN(0x00708D79, CDC::FromHandle);
EI_REPLACE(0x0070BFD7, CreateTempDC);
EI_REPLACE(0x0070BF64, CreateCDC);
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
