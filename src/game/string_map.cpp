// The debug information panel at 0079B930 (vtable 0073BFA4 on the abstract 0073BFAC): an STLport
// map<string, string> at +4 with a setter (slot 0) and a remover (slot 1), drawn as "key  value"
// lines (shadowed, 15 pixels apart) into a panel surface of the interface at most twice a second.
// 004447D0 returns the panel. The map's algorithms and the MFC CFont members are library code. The
// constructor (00444740) runs from a static initialiser before the hooks are installed; a start-up
// check compared what it built (verify_static_initialisers.cpp until S21d). See docs/notes/app.md.
#include "debug_panel.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "lib/stlp_string.h"
#include "lib/stlp_tree_abi.h"
#include "library_trees.h"
#include "ui.h"        // CInterface3D, CUiSurface
#include "ui_gdi.h"    // GameDC, GameFont

#include <cstdint>
#include <cstring>
#include <new>
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"

namespace strmap {

namespace {

using Node = StringMap::Node;   // 0x28 bytes
static_assert(sizeof(Node) == 0x28);

// STLport (library): the map<string, string> instantiation's pair constructor (00445170), insert
// with a hint (004450B0), pair destructor (00445040) and its scalar deleting destructor (004457F0).
using Pairs = lib_trees::StringPairs;

// The node allocator's free list of 0x28-byte nodes (0079B234), which the destructor pushes to inline.
void*& NodeFreeList() { return ei::GameData<void*>(0x0079B234); }

} // namespace

// 004447E0 (slot 0): table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void CDebugPanel::Set(const char* key, const char* value) {
    stlp::string k(key);
    StringMap& map = entries_;
    Node* it;
    lib_trees::StringPairs::LowerBound(&map, nullptr, &it, &k);
    if (it == map.end() || stlp::StringAbi::Less(&k, &it->first)) {
        stlp::string empty;
        stlp::StringElement::Default(&empty);
        alignas(4) std::uint8_t storage[sizeof(stlp::string) * 2];
        auto* pair = reinterpret_cast<Pairs::Value*>(storage);
        Pairs::MakeValue(pair, nullptr, &k, &empty);
        Node* inserted;
        Pairs::InsertHint(&map, nullptr, &inserted, it, pair);
        it = inserted;
        Pairs::DestroyValue(pair);
    }
    it->second.Assign(value);   // inlined by the original: overwrite, then cut or append
}
EI_REPLACE_METHOD(0x004447E0, CDebugPanel::Set);

// 004449A0 (slot 1): removes `key`.
void CDebugPanel::Remove(const char* key) {
    StringMap& map = entries_;
    Node* it;
    {
        stlp::string k(key);
        lib_trees::StringPairs::Find(&map, nullptr, &it, &k);
    }
    if (it != map.end()) map.Erase<stlp::TreeElement<StringMap::Value>>(it);
}
EI_REPLACE_METHOD(0x004449A0, CDebugPanel::Remove);

// 00444690: the table's destructor (at exit): every node destroyed and freed, then the header.
void CDebugPanel::Destruct() {
    StringMap& map = entries_;
    if (map.size()) {
        Node* node = map.end()->parent;
        while (node) {
            lib_trees::StringPairs::EraseSubtree(&map, nullptr, node->right);
            Node* left = node->left;
            Pairs::DeletingDestroyValue(reinterpret_cast<Pairs::Value*>(&node->first), nullptr, 0);
            stlp::deallocate(node, 40);   // 004452D0
            node = left;
        }
        map.ResetToEmpty();
    }
    if (Node* header = map.end()) {
        *reinterpret_cast<void**>(header) = NodeFreeList();
        NodeFreeList() = header;
    }
}
EI_REPLACE_METHOD(0x00444690, CDebugPanel::Destruct);

// 00444740 (A-E2; thiscall, returns the panel): the constructor. The static initialiser 00444660
// runs it on the global before WinMain and registers the destructor above with atexit. The base's
// vtable (0073BFAC, abstract) first, the empty map (its header node from the node pool), then the
// panel's vtable; the interface, surface and font are PanelInit's. The map's compare byte is an
// uninitialised stack byte in the original and is left as it is here.
CDebugPanel* CDebugPanel::Construct() {
    entries_.Construct();
    ei::SetVtable(this, 0x0073BFA4);
    return this;
}
EI_REPLACE_METHOD(0x00444740, CDebugPanel::Construct);

// ---- The panel ---------------------------------------------------------------------------------

namespace {

// The interface's panel rectangle (005F7DA0, patched by Rebirth: through its address), the panel
// surface's constructor and destructor body.

// MFC CFont's vtable (0073BFB4) and the panel's font face (007842C8).
constexpr std::uintptr_t kFontVtable = std::uintptr_t(0x0073BFB4);
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
    auto* font = static_cast<ui_gdi::GameFont*>(gamecrt::operator_new(8));
    if (font) {
        font->handle = nullptr;
        font->vtable = ei::GamePointer<void*>(kFontVtable);
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
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
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
    ui->CallScaleRectToScreen(&asked);
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
    InterfaceScaleRectFromScreenOriginal(ui_, nullptr, &shown);
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
            CUiSurface* memory = gamecrt::operator_new_as<CUiSurface>();
            surface_ = memory ? memory->ConstructOnRect(ui_, &shown_, 5) : nullptr;
        }
        lastDraw_ = gameapi::timeGetTime();
        Redraw();
        return;
    }
    if (CUiSurface* surface = surface_) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    surface_ = nullptr;
}
EI_REWRITTEN(0x00444F50, CDebugPanel::Show);

} // namespace strmap
