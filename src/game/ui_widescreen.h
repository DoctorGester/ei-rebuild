#pragma once

// Rebirth's WideScreen patch behind the flag ei::rebirth::Patch::WideScreen (docs/notes/rebirth.md, "Widescreen and
// rendering"). With a valid mode wider than 4:3 the 800x600 interface layout is widened: the 4:3 part of the screen keeps
// the layout in the middle (width43 = H * 800 / 600 pixels) and the margins left and right of it add `extra` layout units
// on either side. With the flag off, or a mode addon would refuse, everything is the original's. The list of what moves
// is in docs/notes/rebirth.md, "WideScreen module overview".

#include <windows.h>

class CUiWindow;

namespace widescreen {

// addon's values (its globals at 100BD094..100BD0A8), valid when `active`.
struct Layout {
    bool active;   // the flag is on and the settings give a mode wider than 4:3
    int width;     // VIDEORESOLUTION's width (100BD09C)
    int height;    // ... and height (100BD098)
    int width43;   // height * 800 / 600: the screen's 4:3 part (100BD0A4)
    int margin;    // (width - width43) / 2: pixels on either side of it (100BD094)
    int extra;     // margin in layout units, ceil((float)(margin * 800.0 / width43)) (100BD0A8)
    // Two floats addon computes when it applies its patches (53-bit FPU precision, rounded to float):
    float axisScale;   // (float)(width43 / width): written into the axis swap
    float fontScale;   // (float)(width * (double)0.0075f / height): written into the font scale
};

// The layout (computed in Startup; on first use if something asks earlier).
const Layout& Get();

// The layout widths addon writes (the game's .rdata floats 800.0 and 695.0; Startup adds `extra` with the
// wide layout). Every reader reads them from memory (rewrite-guide rule 9).
extern float g_layoutWidth;   // the layout's right edge: the clock's centre x (CHudClock::Update)
extern float g_panelRight;    // the bottom panels' right column: the quick items' x (CHudQuickItems::SetUnit)
inline bool Active() { return Get().active; }

// addon's panel offset (100BD0A0): added to the x of every region rectangle and point and every
// sprite placed or shaped while one of the battle screen's panel builders runs (they set it to
// -extra or +extra and back to 0: EnterLeftPanel / EnterRightPanel / LeavePanel). 0 outside them.
// Our builders are its only writers (addon's code wrote it nowhere else).
int PanelOffset();
void SetPanelOffset(int offset);

// The layout units the battle screen's panels move by to reach the screen's edges: extra while
// Active(), else 0. addon adds it to (right edge) or takes it off (left edge) the builders' layout
// constants (its operands at the W3 sites).
int EdgeShift();

// addon's handlers at a panel builder's entry: the panel offset -EdgeShift() for a panel at the
// screen's left edge, +EdgeShift() at its right; and at its exit (every return), 0 again, also
// when the builder ran inside another one (the outer one then goes on with 0, as with addon).
// Nothing while not Active() (addon installs them only then).
void EnterLeftPanel();
void EnterRightPanel();
void LeavePanel();

// The scaling with the wide layout (addon's replacements; only while Active()). Each returns the last
// coordinate stored (y, or the bottom of a rectangle). `margin`: addon adds / takes off the margin only when
// its interface argument is set; its two entries in front of the screen-to-layout scaling pass none, and the
// text surfaces' draw calls the layout-to-screen scaling without one.
void RectToScreen(RECT* rect, bool margin);
void PointToScreen(POINT* point, bool margin);
void RectFromScreen(RECT* rect, bool margin);
void PointFromScreen(POINT* point, bool margin);

// The interface's text extent (addon's replacement): GetTextExtentPoint32's pixels to layout units, the width
// by the 4:3 part's width (not the screen's) and the height by the mode's height (64-bit products).
SIZE TextExtentToLayout(SIZE pixels);

// The projection's x scale on the axis swap's diagonal: axisScale while Active(), else 1.
float AxisScale();
// What a 75-point font's pixel height is multiplied by for the interface fonts' scale: fontScale while
// Active(), else the original's 0.01.
float FontScale();
// addon's patch of the global map's mouse move and click, in place of the pick picture's pixel read: the
// half-size picture's x is (x / 2) - margin / 2. margin / 2 while Active(), else 0.
int PickShift();

// addon's handlers after the camp's and the multiplayer hero screen's Creates: two more sprites of `window`
// over the screen's margins, the left one the layout rectangle (-margin, 0)-(0, 600) showing texels (0xE3,
// 0x3E)-(0x100, 0xB0), the right one (800, 0)-(800 + margin, 600) the same texels mirrored (addon takes the
// margin's pixels as layout units: wider than the margins, harmless), both at depth 2, scale 100, tint 0.3,
// shown `visible` (the camp 1, the hero screen 0). Nothing while not Active() (addon installs them only then).
void AddCurtains(CUiWindow* window, int visible);
// addon's handler after the hero screen's mode function: when the window has exactly 21 sprites (the 19 of the
// hero screen and the curtains), the curtains (19, 20) are shown in every mode but the hero list (9). Nothing
// while not Active().
void ShowHeroCurtains(CUiWindow* window, int mode);

// The layout's right edge with the wide layout, 800 + extra: addon's operands (the tooltip kept on the
// layout).
int LayoutRight();

} // namespace widescreen
