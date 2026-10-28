// The debug information panel (on an abstract base): a map<string, string> at +4 with a setter and a
// remover, drawn as "key  value" lines (shadowed, 15 pixels apart) into a panel surface of the interface at most
// twice a second. The map's algorithms and the MFC CFont members are library code. The constructor runs from a
// static initialiser. See docs/notes/app.md.
#include "debug_panel.h"

#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "lib/stlp_tree_abi.h"
#include "library_trees.h"
#include "ui.h"        // CInterface3D, CUiSurface
#include "ui_gdi.h"    // CDC, CFont

#include <cstdint>
#include <cstring>
#include <new>
#include "game_data.h"

namespace strmap {

namespace {

using Node = StringMap::Node;

// STLport (library): the map<string, string> instantiation's pair constructor, insert
// with a hint, pair destructor and its scalar deleting destructor,
// as the map's element policy runs them (lib_trees::StringPairs registers them).
using Pair = StringMap::Element;

} // namespace

// Table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void CDebugPanel::Set(const char* key, const char* value) {
    std::string k(key);
    StringMap& map = entries_;
    Node* it = map.lower_bound(k);
    if (it == map.end() || k < it->first) {
        std::string empty;
        lib::InPlace<StringMap::Value> pair;
        Pair::Make(pair.Get(), k, empty);
        it = map.InsertUnique<Pair>(it, *pair.Get());
        Pair::Destroy(pair.Get());
    }
    it->second.assign(value);
}

// Removes `key`.
void CDebugPanel::Remove(const char* key) {
    StringMap& map = entries_;
    Node* it;
    {
        std::string k(key);
        it = map.find(k);
    }
    if (it != map.end()) map.Erase<Pair>(it);
}

// The destructor (at exit): every node destroyed and freed (_M_erase's loop: the right subtree, the pair's
// deleting destructor with 0, the node back to the pool), then the header back to the pool.
CDebugPanel::~CDebugPanel() { entries_.Destroy<Pair>(); }

// The constructor. A static initialiser runs it on the global before WinMain and registers the destructor
// above with atexit. The empty map (its header node from the node pool); the interface, surface and font
// are Init's. The map's compare byte is an uninitialised stack byte and is left as it is.
CDebugPanel::CDebugPanel() { entries_.Construct(); }

// The panel, zero at the start: built by a static initialiser in the list's order and destroyed by the exit
// function
// it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CDebugPanel> g_debugPanel = /*0079B930*/ {};
static_assert(sizeof(CDebugPanel) == 0x44);

namespace {
void DestroyDebugPanel() { g_debugPanel.Destroy(); }
} // namespace

void ConstructDebugPanel() {
    g_debugPanel.Construct();
    libcrt::AtExit(DestroyDebugPanel);
}

// ---- The panel ---------------------------------------------------------------------------------

namespace {

// The interface's panel rectangle (Rebirth-patched), the panel surface's constructor and destructor body.

// The panel's font face.
const char* FontFace() { return "Courier New"; }

void DrawString(mfc::impl::CDC* dc, const std::string& s, RECT* rect) {
    dc->DrawText(s.data(), static_cast<int>(s.size()), rect, 0x800u);
}

} // namespace

// Binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
void CDebugPanel::Init(CInterface3D* ui) {
    ui_ = ui;
    surface_ = nullptr;
    visible_ = 0;
    auto* font = new mfc::impl::CFont();   // null without memory
    font_ = font;
    if (!font->CreatePointFont(0x5A, FontFace(), nullptr)) {
        delete font_;
        font_ = nullptr;
    }
    RECT rect = {10, 10, 0x122, 0x122};
    SetRect(&rect);
}

// Frees the panel surface and the font.
void CDebugPanel::Release() {
    if (CUiSurface* surface = surface_) {
        delete surface;
    }
    surface_ = nullptr;
    delete font_;
    font_ = nullptr;
}

// The panel's rectangle: fitted by the interface, at least 100 x 15 and at most the
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

// Redraws at most every 500 ms, then the panel surface's Blit.
void CDebugPanel::Tick() {
    DWORD now = gameapi::timeGetTime();
    if (now - lastDraw_ > 500) {
        lastDraw_ = now;
        Redraw();
    }
    if (CUiSurface* surface = surface_) surface->Blit();
}

// Clears the panel and draws each entry (the key in the left half, the value in the
// right half; a dark copy one pixel down and right first) until the next line would not fit.
void CDebugPanel::Redraw() {
    CUiSurface* surface = surface_;
    if (!surface) return;
    mfc::impl::CDC* dc = UiSurfaceGetDC(surface);
    RECT& asked = asked_;
    dc->FillSolidArea(0, 0, asked.right - asked.left, asked.bottom - asked.top, 0);
    dc->SetBkMode(1);
    mfc::impl::CGdiObject* oldFont = dc->SelectFont(font_);
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

// Shows the panel (its surface made when missing, drawn at once) or hides it (the
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

} // namespace strmap

strmap::CDebugPanel* DebugInfoPanel() { return &*strmap::g_debugPanel; }
