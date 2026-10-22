// The debug information panel at 0079B930 (game.exe's table 0073BFA4, on the abstract 0073BFAC): an STLport map<string, string> at +4 with a setter (slot 0) and a remover
// (slot 1), drawn as "key  value" lines (shadowed, 15 pixels apart) into a panel surface of the
// interface at most twice a second.
// 004447D0 returns the panel. The map's algorithms and the MFC CFont members are library code. The
// constructor (00444740) runs from a static initialiser before the hooks are installed; a start-up
// check compared what it built (verify_static_initialisers.cpp until S21d). See docs/notes/app.md.
#include "debug_panel.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "lib/stlp_string.h"
#include "lib/static_object.h"
#include "lib/stlp_tree_abi.h"
#include "library_trees.h"
#include "ui.h"        // CInterface3D, CUiSurface
#include "ui_gdi.h"    // GameDC, GameFont

#include <cstdint>
#include <cstring>
#include <new>
#include "game_data.h"

namespace strmap {

namespace {

using Node = StringMap::Node;   // 0x28 bytes
static_assert(sizeof(Node) == 0x28);

// STLport (library): the map<string, string> instantiation's pair constructor (00445170), insert
// with a hint (004450B0), pair destructor (00445040) and its scalar deleting destructor (004457F0),
// as the map's element policy runs them (lib_trees::StringPairs registers them).
using Pair = StringMap::Element;

} // namespace

// 004447E0 (slot 0): table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void CDebugPanel::Set(const char* key, const char* value) {
    stlp::string k(key);
    StringMap& map = entries_;
    Node* it = map.lower_bound(k);
    if (it == map.end() || stlp::StringAbi::Less(&k, &it->first)) {
        stlp::string empty;
        stlp::StringElement::Default(&empty);
        lib::InPlace<StringMap::Value> pair;
        Pair::Make(pair.Get(), k, empty);
        it = map.InsertUnique<Pair>(it, *pair.Get());
        Pair::Destroy(pair.Get());
    }
    it->second.Assign(value);   // inlined by the original: overwrite, then cut or append
}
EI_REWRITTEN(0x004447E0, CDebugPanel::Set);

// 004449A0 (slot 1): removes `key`.
void CDebugPanel::Remove(const char* key) {
    StringMap& map = entries_;
    Node* it;
    {
        stlp::string k(key);
        it = map.find(k);
    }
    if (it != map.end()) map.Erase<Pair>(it);
}
EI_REWRITTEN(0x004449A0, CDebugPanel::Remove);

// 00444690: the destructor (at exit): every node destroyed and freed (the original inlines _M_erase's loop: the
// right subtree, the pair's deleting destructor with 0, the node back to the pool 004452D0), then the header pushed
// on the 0x28-byte nodes' free list (0079B234) as the pool's deallocate does.
CDebugPanel::~CDebugPanel() { entries_.Destroy<Pair>(); }
EI_REWRITTEN_SPECIAL(0x00444690, CDebugPanel::~CDebugPanel);

// 00444740 (A-E2): the constructor. The static initialiser 00444660 runs it on the global before WinMain and
// registers the destructor above with atexit. The empty map (its header node from the node pool); the interface,
// surface and font are Init's. The map's compare byte is an uninitialised stack byte in the original and is left as
// it is here.
CDebugPanel::CDebugPanel() { entries_.Construct(); }
EI_REWRITTEN_SPECIAL(0x00444740, CDebugPanel::CDebugPanel);

// The panel, zero at the start: built by the static initialiser 00444660 in the list's order and destroyed by the exit
// function it registers (00444680); both ported on the name and registered at their addresses
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CDebugPanel> g_debugPanel = /*0079B930*/ {};
static_assert(sizeof(CDebugPanel) == 0x44);

namespace {
void __cdecl DestroyDebugPanel() { g_debugPanel.Destroy(); }   // 00444680
void __cdecl ConstructDebugPanel() {                           // 00444660
    g_debugPanel.Construct();
    libcrt::AtExit(DestroyDebugPanel);
}
} // namespace

EI_REPLACE(0x00444660, ConstructDebugPanel);
EI_REPLACE(0x00444680, DestroyDebugPanel);

// ---- The panel ---------------------------------------------------------------------------------

namespace {

// The interface's panel rectangle (005F7DA0, patched by Rebirth: through its address), the panel
// surface's constructor and destructor body.

// The panel's font face (007842C8).
const char* FontFace() { return "Courier New"; }

void DrawString(ui_gdi::GameDC* dc, const stlp::string& s, RECT* rect) {
    dc->DrawText(s.data(), static_cast<int>(s.size()), rect, 0x800u);
}

} // namespace

// 00444A70: binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
void CDebugPanel::Init(CInterface3D* ui) {
    ui_ = ui;
    surface_ = nullptr;
    visible_ = 0;
    auto* font = gamecrt::operator_new_as<ui_gdi::GameFont>();
    if (font) {
        font->handle = nullptr;
        font->vtable = mfc::impl::CFont::Vtable();
    }
    font_ = font;
    if (!font->CreatePointFont(0x5A, FontFace(), nullptr)) {
        if (ui_gdi::GameFont* f = font_) f->Delete();
        font_ = nullptr;
    }
    RECT rect = {10, 10, 0x122, 0x122};
    SetRect(&rect);
}
EI_REWRITTEN(0x00444A70, CDebugPanel::Init);

// 00444BD0: frees the panel surface and the font.
void CDebugPanel::Release() {
    if (CUiSurface* surface = surface_) {
        delete surface;
    }
    surface_ = nullptr;
    if (ui_gdi::GameFont* font = font_) font->Delete();
    font_ = nullptr;
}
EI_REWRITTEN(0x00444BD0, CDebugPanel::Release);

// 00444C10: the panel's rectangle: fitted by the interface, at least 100 x 15 and at most the
// screen; a shown panel is made again.
void CDebugPanel::SetRect(const RECT* rect) {
    CInterface3D* ui = ui_;
    int width = ui->width_;
    int height = ui->height_;
    RECT& asked = asked_;
    asked = *rect;
    ui->ScaleRectToScreenEntry(&asked);
    int dx = asked.right - asked.left;
    int dy = asked.bottom - asked.top;
    if (dx < 100) dx = 100;
    if (dx > width) dx = width;
    if (dy < 15) dy = 15;
    if (dy > height) dy = height;
    asked.right = asked.left + dx;
    asked.bottom = asked.top + dy;
    RECT& shown = shown_;
    shown = asked;
    ui_->ScaleRectFromScreenEntry(&shown);
    if (visible_) {
        Show(0);
        Show(1);
    }
}
EI_REWRITTEN(0x00444C10, CDebugPanel::SetRect);

// 00444CD0: redraws at most every 500 ms, then the panel surface's slot 0 (Blit).
void CDebugPanel::Tick() {
    DWORD now = gameapi::timeGetTime();
    if (now - lastDraw_ > 500) {
        lastDraw_ = now;
        Redraw();
    }
    if (CUiSurface* surface = surface_) surface->Blit();
}
EI_REWRITTEN(0x00444CD0, CDebugPanel::Tick);

// 00444D00: clears the panel and draws each entry (the key in the left half, the value in the
// right half; a dark copy one pixel down and right first) until the next line would not fit.
void CDebugPanel::Redraw() {
    CUiSurface* surface = surface_;
    if (!surface) return;
    ui_gdi::GameDC* dc = UiSurfaceGetDC(surface);
    RECT& asked = asked_;
    dc->FillSolidArea(0, 0, asked.right - asked.left, asked.bottom - asked.top, 0);
    dc->SetBkMode(1);
    void* oldFont = dc->SelectFont(font_);
    RECT key = {0, 0, (asked.right - asked.left) / 2, 20};
    RECT value = {(asked.right - asked.left) / 2, 0, asked.right - asked.left, 20};
    StringMap& map = entries_;
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
    UiSurfaceReleaseDC(surface_);
}
EI_REWRITTEN(0x00444D00, CDebugPanel::Redraw);

// 00444F50: shows the panel (its surface made when missing, drawn at once) or hides it (the
// surface freed).
void CDebugPanel::Show(std::uint8_t show) {
    visible_ = show;
    if (show) {
        if (!surface_) {
            surface_ = new CUiSurface(ui_, &shown_, 5);
        }
        lastDraw_ = gameapi::timeGetTime();
        Redraw();
        return;
    }
    if (CUiSurface* surface = surface_) {
        delete surface;
    }
    surface_ = nullptr;
}
EI_REWRITTEN(0x00444F50, CDebugPanel::Show);

} // namespace strmap

strmap::CDebugPanel* DebugInfoPanel() { return &*strmap::g_debugPanel; }
