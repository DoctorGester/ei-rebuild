#pragma once

// The debug information panel (string_map.cpp; docs/notes/app.md): an STLport
// map<string, string> drawn as "key  value" lines into a panel surface of the interface. The frame
// code (renderer_scene.cpp, ui_interface_draw.cpp) sets its lines, the console shortcuts remove them.

#include <map>

#include <windows.h>

#include "lib/gdi.h"

#include <cstdint>
#include <string>

class CInterface3D;
class CUiSurface;

namespace strmap {

using StringMap = std::map<std::string, std::string>;

// The vtable has two slots and no destructor, over the abstract one.
class CDebugPanel {
public:
    // the static initialiser runs it before WinMain: the empty map.
    CDebugPanel();
    // At exit: every node destroyed and freed, then the header. Not virtual: the table has no destructor.
    ~CDebugPanel();
    virtual void Set(const char* key, const char* value);
    virtual void Remove(const char* key);
    // Binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
    void Init(CInterface3D* ui);
    void Release();
    // The panel's rectangle: fitted by the interface, at least 100 x 15 and at most
    // the screen; a shown panel is made again.
    void SetRect(const RECT* rect);
    // Redraws at most every 500 ms, then blits the panel surface.
    void Tick();
    void Redraw();
    // Shows the panel (its surface made when missing, drawn at once) or hides it.
    void Show(std::uint8_t show);

    StringMap entries_;          // key -> value
    CInterface3D* ui_;
    CUiSurface* surface_;        // the panel surface (0x38 bytes), while shown
    gdi::Font font_;
    RECT shown_;                 // the rectangle on screen
    RECT asked_;                 // the rectangle asked for
    std::uint8_t visible_;
    std::uint8_t unused_3D[3];
    DWORD lastDraw_;             // the tick of the last drawing
};

} // namespace strmap
