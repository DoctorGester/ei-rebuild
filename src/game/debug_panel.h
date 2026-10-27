#pragma once

// The debug information panel (string_map.cpp; docs/notes/app.md): an STLport
// map<string, string> drawn as "key  value" lines into a panel surface of the interface. The frame
// code (renderer_scene.cpp, ui_interface_draw.cpp) sets its lines, the console shortcuts remove them.

#include "lib/stlp.h"
#include "lib/stlp_string.h"
#include "lib/stlp_tree.h"

#include <windows.h>

#include <cstdint>

class CInterface3D;
class CUiSurface;
namespace mfc::impl { class CFont; }   // lib/mfc_gdi.h

namespace strmap {

using StringMap = stlp::map<stlp::string, stlp::string>;

// The vtable has two slots and no destructor, over the abstract one.
class CDebugPanel {
public:
    // the static initialiser runs it before WinMain: the empty map.
    CDebugPanel();
    // At exit: every node destroyed and freed, then the header. Not virtual: the table has no
    // destructor.
    ~CDebugPanel();
    // Panel[key] = value (a missing key is inserted first).
    virtual void Set(const char* key, const char* value);
    // Removes `key`.
    virtual void Remove(const char* key);
    // Binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
    void Init(CInterface3D* ui);
    // Frees the panel surface and the font.
    void Release();
    // The panel's rectangle: fitted by the interface, at least 100 x 15 and at most
    // the screen; a shown panel is made again.
    void SetRect(const RECT* rect);
    // Redraws at most every 500 ms, then blits the panel surface.
    void Tick();
    // Clears the panel and draws each entry.
    void Redraw();
    // Shows the panel (its surface made when missing, drawn at once) or hides it.
    void Show(std::uint8_t show);

    StringMap entries_;          // +0x04 key -> value
    CInterface3D* ui_;           // +0x10
    CUiSurface* surface_;        // +0x14 the panel surface (0x38 bytes), while shown
    mfc::impl::CFont* font_;     // +0x18 (MFC CFont)
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

} // namespace strmap
