#pragma once

// Rebirth's WideScreen patch (starter\addon.dll v0.12.4.2, docs/notes/rebirth.md "Widescreen and
// rendering") behind the flag ei::rebirth::Patch::WideScreen.
//
// With the starter's Settings\WideScreen on (DWORD, 1 when absent) and the starter's
// EvilIslands\General Settings\VIDEORESOLUTION ("WxH") a valid mode wider than 4:3, addon widens the
// 800x600 interface layout: the 4:3 part of the screen keeps the 800x600 layout in the middle
// (width43 = H * 800 / 600 pixels) and the margins left and right of it (margin pixels each) add
// `extra` layout units on either side. addon computes these once, when it applies its patches before
// WinMain, from the registry (not from the game's screen), and so does this module (Startup, before
// the hooks are installed).
//
// Done here (ticket W1): the layout widths 00742B08 (800 -> 800 + extra) and 007428AC (695 -> 695 +
// extra), the layout-to-screen scaling 005F7CA0 / 005F7D20 (+ margin), the screen-to-layout scaling
// 005F7DA0 / 005F7E00 / 005F7E30 (- margin; addon's entries 005F7D9E / 005F7DFE in front of the
// first two run them without it; every one is without the margin when called with ECX 0), CUiRegion::SetRect / AddPoint and the sprites' Place 005E6FA0 (x +
// the panel offset). Ticket W2: the sprite shapes 005E70E0 / 005E7680 / 005E7D60 / 005E99D0 (x moved
// by the panel offset), the text extent 005F8340 (TextExtentToLayout), and the places addon scales
// without the margin (ECX 0: the text surfaces' draw 005FDE20, the edit field 0065C2E0, the
// connection list 0061D780, the save / load list 006567A0, the chat line 005E3700 and the tooltip
// 0065E580 with its right edge LayoutRight; CInterface3D::CallScale*NoMargin, ui.h). The panel
// builders that set the panel offset, the camp and hero screens, minimap and cursor rectangles are
// W3-W4 (docs/notes/rebirth.md).
//
// With the flag off, or a mode addon would refuse, everything is the original's.

#include <windows.h>

namespace widescreen {

// addon's values (its globals at 100BD094..100BD0A8), valid when `active`.
struct Layout {
    bool active;   // the flag is on and the settings give a mode wider than 4:3
    int width;     // VIDEORESOLUTION's width (100BD09C)
    int height;    // ... and height (100BD098)
    int width43;   // height * 800 / 600: the screen's 4:3 part (100BD0A4)
    int margin;    // (width - width43) / 2: pixels on either side of it (100BD094)
    int extra;     // margin in layout units, ceil((float)(margin * 800.0 / width43)) (100BD0A8)
};

// The layout (computed in Startup; on first use if something asks earlier).
const Layout& Get();
inline bool Active() { return Get().active; }

// addon's panel offset (100BD0A0): added to the x of every region rectangle and point and every
// sprite placed while one of the panel builders runs (they set it to -extra or +extra and back to
// 0; W2-W4). 0 outside them. In a Rebirth launch, until the builders are ported, addon's own builders
// set it: the hook-time startup finds addon's variable and every user here reads that one.
int PanelOffset();
void SetPanelOffset(int offset);

// The scaling with the wide layout (addon's replacements; only while Active()). Each returns what
// addon's code leaves in EAX: the last coordinate stored (y, or the bottom of a rectangle).
// `margin`: addon's code tests ECX (the interface) and adds / takes off the margin only when it is
// nonzero; its entries 005F7D9E and 005F7DFE clear ECX first, and its code at 005FDE94 (the text
// surfaces' draw 005FDE20, W2) calls 005F7CA0 with ECX 0.
int RectToScreen(RECT* rect, bool margin);
int PointToScreen(POINT* point, bool margin);
int RectFromScreen(RECT* rect, bool margin);
int PointFromScreen(POINT* point, bool margin);

// The interface's text extent (005F8340, addon's 80 bytes at 005F83CF): GetTextExtentPoint32's
// pixels to layout units, the width by the 4:3 part's width (not the screen's) and the height by the
// mode's height (IMUL / IDIV, 64-bit products).
SIZE TextExtentToLayout(SIZE pixels);

// The layout's right edge with the wide layout, 800 + extra: addon's operands at 0065E7C3 / 0065E7CA
// (the tooltip kept on the layout, 0065E580).
int LayoutRight();

// What 00742B08 and 007428AC held when Startup ran (before it wrote ours): in a Rebirth launch,
// addon's values (the verify build compares them with ours).
float FoundLayoutWidth();
float FoundPanelRight();

} // namespace widescreen
