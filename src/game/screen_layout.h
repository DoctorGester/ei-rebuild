#pragma once

// The interface layout for any window shape (Rebirth's WideScreen patch folded in for every aspect ratio, no
// flag; docs/notes/rebirth.md, "WideScreen"). The 800x600 layout is drawn in the window's 4:3 part: the largest 4:3
// rectangle in the window, centred. A window wider than 4:3 has margins left and right of it (`margin` pixels, `extra`
// layout units each; the battle screen's panels move out to the window's edges by `extra`, Rebirth's widescreen); a
// window narrower than 4:3 (5:4, portrait) has bands above and below it (`marginY` pixels: the 4:3 part letterboxed,
// nothing moves into the bands); at exactly 4:3 both are 0 and everything is the original's. The list of what moves
// is in docs/notes/rebirth.md, "WideScreen module overview".

#include "lib/rect.h"

class UiWindow;

namespace screen_layout {

// The layout's values (Rebirth's for a window wider than 4:3: addon's globals at 100BD094..100BD0A8).
struct Layout {
    int width;      // the window's width in pixels (the options' VideoResolution: the window is made at it)
    int height;     // ... and height
    int width43;    // the 4:3 part's width: min(width, height * 800 / 600)
    int height43;   // ... and height: min(height, width * 600 / 800)
    int margin;     // (width - width43) / 2: pixels left and right of the 4:3 part
    int marginY;    // (height - height43) / 2: pixels above and below it
    int extra;      // margin in layout units, ceil((float)(margin * 800.0 / width43))
    // The 3D view keeps the 4:3 proportions over the whole window: the projection's x and y scales.
    float axisScale;    // (float)(width43 / width)
    float axisScaleY;   // (float)(height43 / height)
    // The interface fonts' scale: width * 0.0075 / height43 (addon's), 0.01 on a 4:3 window (the original's constant).
    float fontScale;
};

// Computes the layout for the window the game makes, at the resolution it loaded from its options (GameMain, right
// after the options load), and again at each resolution change (app::SetResolution), and writes the two layout
// widths. Until the first call the layout is an 800x600 window's.
void Configure(int width, int height);

const Layout& Get();

// The layout widths (the game's .rdata floats 800.0 and 695.0 plus `extra`: Rebirth wrote them). Every reader
// reads them from memory (rewrite-guide rule 9).
extern float g_layoutWidth;   // the layout's right edge: the clock's centre x (HudClock::Update)
extern float g_panelRight;    // the bottom panels' right column: the quick items' x (HudQuickItems::SetUnit)

// The panel offset (addon's 100BD0A0): added to the x of every region rectangle and point and every sprite placed or
// shaped while one of the battle screen's panel builders runs (they set it to -extra or +extra and back to 0:
// EnterLeftPanel / EnterRightPanel / LeavePanel). 0 outside them.
int PanelOffset();
void SetPanelOffset(int offset);

// The layout units the battle screen's panels move by to reach the window's edges: `extra` (0 on a window 4:3 or
// narrower). Added to (right edge) or taken off (left edge) the builders' layout constants.
int EdgeShift();

// At a panel builder's entry: the panel offset -EdgeShift() for a panel at the window's left edge, +EdgeShift() at its
// right; and at its exit (every return), 0 again, also when the builder ran inside another one (the outer one then
// goes on with 0, as with addon).
void EnterLeftPanel();
void EnterRightPanel();
void LeavePanel();

// Layout to screen pixels and back: x by the 4:3 part's width, y by its height. `margin`: the 4:3 part's place in
// the window (its left and top margins) added / taken off; without it for sizes and offsets (the text surfaces' draw,
// the drags, the tooltip's cursor offset: Interface3D's *NoMargin scalings).
void RectToScreen(Rect* rect, bool margin);
void PointToScreen(Point* point, bool margin);
void RectFromScreen(Rect* rect, bool margin);
void PointFromScreen(Point* point, bool margin);

// The layout units one mouse count turns, tilts or pans by: the same at every window size (positions scale to the
// window, the response to the mouse's motion does not). The 800-unit layout across a 4:3 part of 2880
// pixels, which is what a 3840x2160 screen (the user's 4K monitor at 200% Windows scaling, in the game's physical
// pixels) had when the drags were scaled to the window. The original took raw counts at 800x600 (a count a unit).
constexpr float kLayoutPerCount = 800.0f / 2880.0f;

// The interface's text extent: the drawn text's pixels to layout units, by the 4:3 part's size (64-bit products).
Size TextExtentToLayout(Size pixels);

// The projection's x and y scales on the axis swap's diagonal (axisScale, axisScaleY).
float AxisScale();
float AxisScaleY();
// What a 75-point font's pixel height is multiplied by for the interface fonts' scale (fontScale).
float FontScale();
// The global map's pick picture is read at (x / 2, y / 2) less this: the 4:3 part's margins halved (addon's patch of
// the map's mouse move and click took margin / 2 off x).
Point PickShift();

// After the camp's and the multiplayer hero screen's Creates: two more sprites of `window` over the window's left and
// right margins, the left one the layout rectangle (-margin, 0)-(0, 600) showing texels (0xE3, 0x3E)-(0x100, 0xB0),
// the right one (800, 0)-(800 + margin, 600) the same texels mirrored (addon takes the margin's pixels as layout
// units: wider than the margins, harmless; with no margin both are empty), both at depth 2, scale 100, tint 0.3,
// shown `visible` (the camp 1, the hero screen 0). Returns the left one's index among the window's sprites.
int AddCurtains(UiWindow* window, int visible);
// The two curtains from sprite `first` on (AddCurtains's) over the margins the window has now (the screen took
// another size), shown as they were.
void PlaceCurtains(UiWindow* window, int first);
// The hero screen's curtains' index (its 19 sprites come first).
constexpr int kHeroCurtains = 19;
// After the hero screen's mode function: when the window has exactly 21 sprites (the 19 of the hero screen and the
// curtains), the curtains (19, 20) are shown in every mode but the hero list (9).
void ShowHeroCurtains(UiWindow* window, int mode);

// The layout's right edge, 800 + extra (the tooltip kept on the layout).
int LayoutRight();

} // namespace screen_layout
