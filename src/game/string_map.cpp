// The debug information panel (on an abstract base): a map<string, string> at +4 with a setter and a
// remover, drawn as "key  value" lines (shadowed, 15 pixels apart) into a panel surface of the interface at most
// twice a second. The map's algorithms and the CFont members are library code. The constructor runs from a
// static initialiser. See docs/notes/app.md.
#include "debug_panel.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/static_object.h"
#include "ui.h"        // CInterface3D, CUiSurface
#include "ui_gdi.h"
#include "ui_native.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace strmap {

namespace {

// The panel's font face.
const char* FontFace() { return "Courier New"; }

constexpr UINT kDrawFormat = 0x800;   // DT_NOPREFIX
constexpr COLORREF kShadowColour = 0x080808;
constexpr COLORREF kTextColour = 0xFFFFFF;

// `s` in `rect`, in `colour`, with the shadow one pixel lower and to the right first: through the glyph atlas on
// a native surface, else GDI with the panel font selected in `dc` (a native surface GDI drew on has that part
// copied onto its canvas).
void DrawString(CUiSurface* surface, HDC dc, HFONT font, const std::string& s, const RECT& rect) {
    const ui_native::TextPath path = ui_native::Text(surface, font, s, rect, kDrawFormat, kTextColour, true, nullptr);
    if (path == ui_native::TextPath::Canvas) return;
    for (const bool shadow : {true, false}) {
        RECT drawn = rect;
        if (shadow) gameapi::OffsetRect(&drawn, 1, 1);
        gameapi::SetTextColor(dc, shadow ? kShadowColour : kTextColour);
        gameapi::DrawTextA(dc, s.data(), static_cast<int>(s.size()), &drawn, kDrawFormat);
    }
    if (path == ui_native::TextPath::Gdi) {
        RECT inked = rect;
        ++inked.right;
        ++inked.bottom;
        ui_native::TextDrawn(surface, inked, true);
    }
}

} // namespace

// Table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void CDebugPanel::Set(std::string_view key, std::string_view value) {
    entries_[std::string(key)].assign(value);
}

// Removes `key`.
void CDebugPanel::Remove(std::string_view key) {
    entries_.erase(std::string(key));
}

// The destructor runs at exit (the exit function the static initialiser registers).
CDebugPanel::~CDebugPanel() = default;

// The constructor. A static initialiser runs it on the global before WinMain: the empty map; the interface,
// surface and font are Init's.
CDebugPanel::CDebugPanel() {}

// The panel, zero at the start: built by a static initialiser in the list's order and destroyed by the exit
// function
// it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CDebugPanel> g_debugPanel = /*0079B930*/ {};

// A second block: it uses g_debugPanel (defined above).
namespace {
void DestroyDebugPanel() { g_debugPanel.Destroy(); }
} // namespace

void ConstructDebugPanel() {
    g_debugPanel.Construct();
    std::atexit(DestroyDebugPanel);
}

// ---- The panel ---------------------------------------------------------------------------------

// Binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
void CDebugPanel::Init(CInterface3D* ui) {
    ui_ = ui;
    surface_ = nullptr;
    visible_ = 0;
    font_ = gdi::PointFont(0x5A, FontFace());
    RECT rect = {10, 10, 0x122, 0x122};
    SetRect(&rect);
}

// Frees the panel surface and the font.
void CDebugPanel::Release() {
    if (CUiSurface* surface = surface_) {
        delete surface;
    }
    surface_ = nullptr;
    font_.reset();
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
// right half; a dark copy one pixel down and right first, through the glyph atlas on a native surface) until the next line would not fit.
void CDebugPanel::Redraw() {
    CUiSurface* surface = surface_;
    if (!surface) return;
    HDC dc = UiSurfaceGetDC(surface);
    RECT& asked = asked_;
    gdi::FillSolidRect(dc, 0, 0, asked.right - asked.left, asked.bottom - asked.top, 0);
    ui_native::Fill(surface, RECT{0, 0, asked.right - asked.left, asked.bottom - asked.top}, 0);
    gameapi::SetBkMode(dc, 1);
    HGDIOBJ oldFont = gameapi::SelectObject(dc, font_.get());
    RECT key = {0, 0, (asked.right - asked.left) / 2, 20};
    RECT value = {(asked.right - asked.left) / 2, 0, asked.right - asked.left, 20};
    for (const auto& [name, text] : entries_) {
        DrawString(surface, dc, font_.get(), name, key);
        DrawString(surface, dc, font_.get(), text, value);
        gameapi::OffsetRect(&key, 0, 15);
        gameapi::OffsetRect(&value, 0, 15);
        if (key.bottom > asked.bottom - asked.top) break;
    }
    gameapi::SelectObject(dc, oldFont);
    UiSurfaceReleaseDC(surface_);
}

// Shows the panel (its surface made when missing, drawn at once) or hides it (the
// surface freed).
void CDebugPanel::Show(std::uint8_t show) {
    visible_ = show;
    if (show) {
        if (!surface_) {
            surface_ = new CUiSurface(ui_, &shown_, 5);
            ui_native::Adopt(surface_, "debug");
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

