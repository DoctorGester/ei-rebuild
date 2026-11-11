#pragma once

// The interface layout for any window shape (Rebirth's WideScreen patch folded in for every aspect ratio, no
// flag; docs/notes/rebirth.md, "WideScreen"). The 800x600 layout is drawn in the window's 4:3 part: the largest 4:3
// rectangle in the window, centred. A window wider than 4:3 has margins left and right of it (`margin` pixels, `extra`
// layout units each; the battle screen's panels move out to the window's edges by `extra`, Rebirth's widescreen); a
// window narrower than 4:3 (5:4, portrait) has bands above and below it (`marginY` pixels: the 4:3 part letterboxed,
// nothing moves into the bands); at exactly 4:3 both are 0 and everything is the original's. The list of what moves
// is in docs/notes/rebirth.md, "WideScreen module overview".

#include "lib/rect.h"

class UiMesh;
class UiWindow;

namespace screen_layout {

// The layout's values (Rebirth's for a window wider than 4:3: addon's globals at 100BD094..100BD0A8).
struct Layout {
    int width = 0;  // the window's width in pixels (the options' VideoResolution: the window is made at it)
    int height = 0;  // ... and height
    int width43 = 0;  // the 4:3 part's width: min(width, height * 800 / 600)
    int height43 = 0;  // ... and height: min(height, width * 600 / 800)
    int margin = 0;  // (width - width43) / 2: pixels left and right of the 4:3 part
    int marginY = 0;  // (height - height43) / 2: pixels above and below it
    int extra = 0;  // margin in layout units, ceil((float)(margin * 800.0 / width43))
    // The interface fonts' scale: width * 0.0075 / height43 (addon's), 0.01 on a 4:3 window (the original's constant).
    float fontScale = 0.0f;
};

// Computes the layout for the window the game makes, at the resolution it loaded from its options (GameMain, right
// after the options load), and again at each resolution change (app::SetResolution), and writes the two layout
// widths and the scene renderer's axis scales (the 3D view keeps the 4:3 proportions over the whole window). Until the
// first call the layout is an 800x600 window's.
void Configure(int width, int height);

const Layout& Get();

// The layout widths (the game's .rdata floats 800.0 and 695.0 plus `extra`: Rebirth wrote them). Every reader
// reads them from memory (rewrite-guide rule 9).
extern float g_layoutWidth;   // the layout's right edge: the clock's centre x (HudClock::Update)
extern float g_panelRight;    // the bottom panels' right column: the quick items' x (HudQuickItems::SetUnit)

// The layout units the battle screen's panels move by to reach the window's edges: `extra` (0 on a window 4:3 or
// narrower). A panel at the left edge has its origin (UiWindow::originX) at -EdgeShift(), one at the right edge at
// +EdgeShift().
int EdgeShift();

// Layout (800x600) to screen pixels and back: x by the 4:3 part's width, y by its height, and the 4:3 part's place in
// the window (its left and top margins) added / taken off. A rectangle's corners go each on its own.
Point ToScreen(Point point);
Rect ToScreen(const Rect& rect);
Point FromScreen(Point point);
Rect FromScreen(const Rect& rect);
// The same without the margins, for sizes and offsets: the text surfaces' draw and text height, the edit field, the
// connection list, the save / load list, the chat line, the tooltip's cursor offset, the drags.
Point ToScreenNoMargin(Point point);
Rect ToScreenNoMargin(const Rect& rect);
Point FromScreenNoMargin(Point point);

// The layout units one mouse count turns, tilts or pans by: the same at every window size (positions scale to the
// window, the response to the mouse's motion does not). The 800-unit layout across a 4:3 part of 2880
// pixels, which is what a 3840x2160 screen (the user's 4K monitor at 200% Windows scaling, in the game's physical
// pixels) had when the drags were scaled to the window. The original took raw counts at 800x600 (a count a unit).
constexpr float kLayoutPerCount = 800.0f / 2880.0f;

// The interface's text extent: the drawn text's pixels to layout units, by the 4:3 part's size (64-bit products).
Size TextExtentToLayout(Size pixels);

// What a 75-point font's pixel height is multiplied by for the interface fonts' scale (fontScale).
float FontScale();

// After the camp's and the multiplayer hero screen's Creates: two more sprites of `window` over the window's left and
// right margins, the left one the layout rectangle (-margin, 0)-(0, 600) showing texels (0xE3, 0x3E)-(0x100, 0xB0),
// the right one (800, 0)-(800 + margin, 600) the same texels mirrored (addon takes the margin's pixels as layout
// units: wider than the margins, harmless; with no margin both are empty), both at depth 2, scale 100, tint 0.3,
// shown `visible` (the camp 1, the hero screen 0); their window keeps them.
struct Curtains {
    UiMesh* left = nullptr;
    UiMesh* right = nullptr;
};
Curtains AddCurtains(UiWindow* window, int visible);
// The two curtains over the margins the window has now (the screen took another size), shown as they were; nothing
// before AddCurtains.
void PlaceCurtains(const Curtains& curtains);
// After the hero screen's mode function: the curtains shown in every mode but the hero list (9); nothing before
// AddCurtains (the mode is first set while the screen is built).
void ShowHeroCurtains(const Curtains& curtains, int mode);

// The layout's right edge, 800 + extra (the tooltip kept on the layout).
int LayoutRight();

} // namespace screen_layout
