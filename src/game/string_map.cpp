// The debug information panel at 0079B930 (vtable 0073BFA4 on the abstract 0073BFAC): an STLport
// map<string, string> at +4 with a setter (slot 0) and a remover (slot 1), drawn as "key  value"
// lines (shadowed, 15 pixels apart) into a panel surface of the interface at most twice a second.
// 004447D0 returns the panel. The map's algorithms and the MFC CFont members are library code. The
// constructor (00444740) runs from a static initialiser before the hooks are installed; a start-up
// check compared what it built (verify_static_initialisers.cpp until S21d). See docs/notes/app.md.
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "lib/stlp_string.h"
#include "library_trees.h"
#include "ui.h"        // CInterface3D, CUiSurface
#include "ui_gdi.h"    // GameDC, GameFont

#include <cstdint>
#include <cstring>
#include <new>
#include "originals_library.h"
#include "originals_ui.h"

namespace strmap {

namespace {

using StringMap = stlp::map<stlp::string, stlp::string>;
using Node = StringMap::Node;   // 0x28 bytes
static_assert(sizeof(Node) == 0x28);

// The panel (0079B930).
class CDebugPanel {
public:
    void* vtable_;
    StringMap entries_;          // +0x04 key -> value
    CInterface3D* ui_;           // +0x10
    CUiSurface* surface_;        // +0x14 the panel surface (0x38 bytes), while shown
    ui_gdi::GameFont* font_;     // +0x18 (MFC CFont)
    RECT shown_;                 // +0x1C the rectangle on screen
    RECT asked_;                 // +0x2C the rectangle asked for
    std::uint8_t visible_;       // +0x3C
    std::uint8_t unused_3D[3];
    DWORD lastDraw_;             // +0x40 the tick of the last drawing
};
static_assert(offsetof(CDebugPanel, entries_) == 0x04 && offsetof(CDebugPanel, ui_) == 0x10 &&
              offsetof(CDebugPanel, font_) == 0x18 && offsetof(CDebugPanel, shown_) == 0x1C &&
              offsetof(CDebugPanel, asked_) == 0x2C && offsetof(CDebugPanel, visible_) == 0x3C &&
              offsetof(CDebugPanel, lastDraw_) == 0x40);

// STLport (library).
__declspec(naked) Node** __fastcall LowerBound(StringMap*, void*, Node** /*out*/, const stlp::string*) EI_ORIGINAL(0x004456E0)
__declspec(naked) Node** __fastcall Find(StringMap*, void*, Node** /*out*/, const stlp::string*) EI_ORIGINAL(0x00445620)
__declspec(naked) void* __fastcall PairConstruct(void* /*pair*/, void*, const stlp::string*, const stlp::string*) EI_ORIGINAL(0x00445170)
__declspec(naked) Node** __fastcall InsertHint(StringMap*, void*, Node** /*out*/, Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x004450B0)
__declspec(naked) void __fastcall PairDestruct(void*) EI_ORIGINAL(0x00445040)
void __fastcall StringErase(stlp::string* a0, void*, const char* a2, const char* a3) {
    StringEraseOriginal(a0, nullptr, const_cast<char*>(a2), const_cast<char*>(a3));
}
void __fastcall StringAppend(stlp::string* a0, void*, const char* a2, const char* a3, int a4) { StringAppendRangeOriginal(a0, nullptr, a2, a3, (const void*)a4); }
__declspec(naked) void __fastcall PairDelete(void*, void*, unsigned) EI_ORIGINAL(0x004457F0)
__declspec(naked) void __fastcall NodeFree(Node*, std::size_t) EI_ORIGINAL(0x004452D0)

CDebugPanel* Panel(void* self) { return static_cast<CDebugPanel*>(self); }

// The node allocator's free list of 0x28-byte nodes (0079B234), which the destructor pushes to inline.
void*& NodeFreeList() { return *reinterpret_cast<void**>(0x0079B234); }

// A key string the functions below construct and release explicitly (when the original does).
struct KeyString {
    alignas(stlp::string) std::uint8_t storage[sizeof(stlp::string)];
    stlp::string* Make(const char* text) { return new (storage) stlp::string(text, text + std::strlen(text)); }
};

} // namespace

// 004447D0.
void* __cdecl StringTable() { return reinterpret_cast<void*>(0x0079B930); }
EI_REPLACE(0x004447D0, StringTable);

// 004447E0 (slot 0): table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void __fastcall StringTableSet(void* self, void*, const char* key, const char* value) {
    KeyString keyStorage;
    stlp::string* k = keyStorage.Make(key);
    StringMap& map = Panel(self)->entries_;
    Node* it;
    LowerBound(&map, nullptr, &it, k);
    if (it == map.end() || stlp::StringAbi::Less(k, &it->first)) {
        KeyString emptyStorage;
        auto* empty = reinterpret_cast<stlp::string*>(emptyStorage.storage);
        char alloc;
        StringReserveOriginal(empty, nullptr, &alloc, 8);
        *const_cast<char*>(empty->data()) = 0;
        alignas(4) std::uint8_t pair[sizeof(stlp::string) * 2];
        PairConstruct(pair, nullptr, k, empty);
        Node* inserted;
        InsertHint(&map, nullptr, &inserted, it, pair);
        it = inserted;
        PairDestruct(pair);
        StlStringDestroy(empty);
    }
    stlp::string& s = it->second;
    std::size_t length = std::strlen(value);
    std::size_t size = s.size();
    char* start = const_cast<char*>(s.data());
    if (length <= size) {
        std::memcpy(start, value, length);
        StringErase(&s, nullptr, start + length, start + size);
    } else {
        std::memcpy(start, value, size);
        StringAppend(&s, nullptr, value + size, value + length, 0);
    }
    k->~string();
}
EI_REPLACE(0x004447E0, StringTableSet);

// 004449A0 (slot 1): removes `key`.
void __fastcall StringTableRemove(void* self, void*, const char* key) {
    KeyString keyStorage;
    stlp::string* k = keyStorage.Make(key);
    StringMap& map = Panel(self)->entries_;
    Node* it;
    Find(&map, nullptr, &it, k);
    k->~string();
    if (it != map.end()) map.Erase<stlp::TreeElement<StringMap::Value>>(it);
}
EI_REPLACE(0x004449A0, StringTableRemove);

// 00444690: the table's destructor (at exit): every node destroyed and freed, then the header.
void __fastcall StringTableDestruct(void* self) {
    StringMap& map = Panel(self)->entries_;
    if (map.size()) {
        Node* node = map.end()->parent;
        while (node) {
            lib_trees::StringPairs::EraseSubtree(&map, nullptr, node->right);
            Node* left = node->left;
            PairDelete(&node->first, nullptr, 0);
            NodeFree(node, 1);
            node = left;
        }
        map.ResetToEmpty();
    }
    if (Node* header = map.end()) {
        *reinterpret_cast<void**>(header) = NodeFreeList();
        NodeFreeList() = header;
    }
}
EI_REPLACE(0x00444690, StringTableDestruct);

// 00444740 (A-E2; thiscall, returns the panel): the constructor. The static initialiser 00444660
// runs it on the global before WinMain and registers the destructor above with atexit. The base's
// vtable (0073BFAC, abstract) first, the empty map (its header node from the node pool), then the
// panel's vtable; the interface, surface and font are PanelInit's. The map's compare byte is an
// uninitialised stack byte in the original and is left as it is here.
void* __fastcall StringTableConstruct(void* self) {
    CDebugPanel* panel = Panel(self);
    panel->entries_.Construct();
    ei::SetVtable(panel, 0x0073BFA4);
    return panel;
}
EI_REPLACE(0x00444740, StringTableConstruct);

// ---- The panel ---------------------------------------------------------------------------------

void __fastcall PanelRedraw(void*);
void __fastcall PanelShow(void*, void*, std::uint8_t);
namespace {

// The interface's panel rectangle (005F7DA0, patched by Rebirth: through its address), the panel
// surface's constructor and destructor body.

// MFC CFont's vtable (0073BFB4) and the panel's font face (007842C8).
constexpr std::uintptr_t kFontVtable = std::uintptr_t(0x0073BFB4);
const char* FontFace() { return reinterpret_cast<const char*>(0x007842C8); }

void DrawString(ui_gdi::GameDC* dc, const stlp::string& s, RECT* rect) {
    dc->DrawText(s.data(), static_cast<int>(s.size()), rect, 0x800u);
}

} // namespace

// 00444A70: binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
void __fastcall PanelInit(void* self, void*, CInterface3D* ui) {
    CDebugPanel* panel = Panel(self);
    panel->ui_ = ui;
    panel->surface_ = nullptr;
    panel->visible_ = 0;
    auto* font = static_cast<ui_gdi::GameFont*>(gamecrt::operator_new(8));
    if (font) {
        font->handle = nullptr;
        font->vtable = reinterpret_cast<void**>(kFontVtable);
    }
    panel->font_ = font;
    if (!font->CreatePointFont(0x5A, FontFace(), nullptr)) {
        if (ui_gdi::GameFont* f = panel->font_) f->Delete();
        panel->font_ = nullptr;
    }
    RECT rect = {10, 10, 0x122, 0x122};
    PanelSetRect(self, nullptr, &rect);
}
EI_REPLACE(0x00444A70, PanelInit);

// 00444BD0: frees the panel surface and the font.
void __fastcall PanelRelease(void* self) {
    CDebugPanel* panel = Panel(self);
    if (CUiSurface* surface = panel->surface_) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    panel->surface_ = nullptr;
    if (ui_gdi::GameFont* font = panel->font_) font->Delete();
    panel->font_ = nullptr;
}
EI_REPLACE(0x00444BD0, PanelRelease);

// 00444C10: the panel's rectangle: fitted by the interface, at least 100 x 15 and at most the
// screen; a shown panel is made again.
void __fastcall PanelSetRect(void* self, void*, const RECT* rect) {
    CDebugPanel* panel = Panel(self);
    CInterface3D* ui = panel->ui_;
    int width = ui->width_;
    int height = ui->height_;
    RECT& asked = panel->asked_;
    asked = *rect;
    ui->CallScaleRectToScreen(&asked);
    int dx = asked.right - asked.left;
    int dy = asked.bottom - asked.top;
    if (dx < 100) dx = 100;
    if (dx > width) dx = width;
    if (dy < 15) dy = 15;
    if (dy > height) dy = height;
    asked.right = asked.left + dx;
    asked.bottom = asked.top + dy;
    RECT& shown = panel->shown_;
    shown = asked;
    InterfaceScaleRectFromScreenOriginal(panel->ui_, nullptr, &shown);
    if (panel->visible_) {
        PanelShow(self, nullptr, 0);
        PanelShow(self, nullptr, 1);
    }
}
EI_REPLACE(0x00444C10, PanelSetRect);

// 00444CD0: redraws at most every 500 ms, then the panel surface's slot 0 (Blit).
void __fastcall PanelTick(void* self) {
    CDebugPanel* panel = Panel(self);
    DWORD now = gameapi::timeGetTime();
    if (now - panel->lastDraw_ > 500) {
        panel->lastDraw_ = now;
        PanelRedraw(self);
    }
    if (CUiSurface* surface = panel->surface_) surface->Blit();
}
EI_REPLACE(0x00444CD0, PanelTick);

// 00444D00: clears the panel and draws each entry (the key in the left half, the value in the
// right half; a dark copy one pixel down and right first) until the next line would not fit.
void __fastcall PanelRedraw(void* self) {
    CDebugPanel* panel = Panel(self);
    CUiSurface* surface = panel->surface_;
    if (!surface) return;
    ui_gdi::GameDC* dc = UiSurfaceGetDC(surface);
    RECT& asked = panel->asked_;
    dc->FillSolidArea(0, 0, asked.right - asked.left, asked.bottom - asked.top, 0);
    dc->SetBkMode(1);
    void* oldFont = dc->SelectFont(panel->font_);
    RECT key = {0, 0, (asked.right - asked.left) / 2, 20};
    RECT value = {(asked.right - asked.left) / 2, 0, asked.right - asked.left, 20};
    StringMap& map = panel->entries_;
    Node* it = map.begin();
    while (it != map.end()) {
        dc->SetTextColor(static_cast<COLORREF>(0x080808));
        RECT shadow = key;
        gameapi::OffsetRect(&shadow, 1, 1);
        RECT r = shadow;
        DrawString(dc, it->first, &r);
        shadow = value;
        gameapi::OffsetRect(&shadow, 1, 1);
        r = shadow;
        DrawString(dc, it->second, &r);
        dc->SetTextColor(static_cast<COLORREF>(0xFFFFFF));
        DrawString(dc, it->first, &key);
        DrawString(dc, it->second, &value);
        gameapi::OffsetRect(&key, 0, 15);
        gameapi::OffsetRect(&value, 0, 15);
        if (key.bottom > asked.bottom - asked.top) break;
        it = StringMap::Next(it);
    }
    dc->SelectFont(oldFont);
    UiSurfaceReleaseDC(panel->surface_);
}
EI_REPLACE(0x00444D00, PanelRedraw);

// 00444F50: shows the panel (its surface made when missing, drawn at once) or hides it (the
// surface freed).
void __fastcall PanelShow(void* self, void*, std::uint8_t show) {
    CDebugPanel* panel = Panel(self);
    panel->visible_ = show;
    if (show) {
        if (!panel->surface_) {
            void* memory = gamecrt::operator_new(0x38);
            panel->surface_ = memory ? static_cast<CUiSurface*>(memory)->ConstructOnRect(panel->ui_, &panel->shown_, 5) : nullptr;
        }
        panel->lastDraw_ = gameapi::timeGetTime();
        PanelRedraw(self);
        return;
    }
    if (CUiSurface* surface = panel->surface_) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    panel->surface_ = nullptr;
}
EI_REPLACE(0x00444F50, PanelShow);

} // namespace strmap
