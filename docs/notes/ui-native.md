# Native interface drawing

How the interface's window surfaces draw on the GPU renderer: every surface's pixels are a canvas on the GPU, and text
is laid out, measured and drawn in our own fonts. There is no GDI in the interface. How the screens behave is ui.md.

## 1. How the interface produces pixels

Frame order: `Interface3D::Draw` draws the backdrop blit, then the top window's sprites and models, then the fade. After
the scene, `DrawOverlays` blits every shown surface of the top window (containers recurse), then the tooltips, then the
status surface, the FPS surface, the console and the backdrop captures. The cursor comes last. So every window surface
is drawn above every mesh, in stack and child order.

| Primitive | Functions (file) | Path |
|---|---|---|
| Surface blit (plain or colour-keyed) | `UiSurface::Blit` (`ui_surface.cpp`) -> `render::ui::DrawCanvas` | the canvas drawn into the frame |
| Text | `UiSurface::DrawTextIn` / `DrawTextPixels` (shadow first in 0x080808 at +1,+1), `DrawWrappedText` (`ui_text.cpp`); extents by `TextExtent` (`ui_font.h`) | glyph runs on the canvas |
| Fill / Clear / FillRect / FrameRect | `ui_surface.cpp` | canvas commands |
| Lines and carets | `UiSurface::Line` (the 1-pixel pen's rule: end point left out): console, chat typed line, edit field, connect list, save / load list | canvas commands |
| Single pixel | `UiSurface::Dot`: the HUD map's marker | canvas command |
| Screen copy into a surface | `CaptureScreen`: escape-menu backdrop, global-map pick picture | GPU copy (`ps_copy`) |
| Greyscale, (2r+4g+b)/16 | `Greyscale`: escape-menu backdrop | GPU (`ps_grey`) |
| Save picture | `Rgb565Copy` -> `render::ui::ReadRgb565`: the escape menu's 256x192 copy, QuickSave, SaveGame | GPU, 5:6:5 read back |
| Pixel read | `PixelAt` -> `render::ui::ReadPixel`: the global-map click and FindPaths (zone = (pixel>>4)&0xF - 1) | GPU read back of one pixel |
| Swap / freeze | `SwapContents` -> `render::ui::MoveCanvas`; `UiSurface::Show` / `Hide` | the canvas moved aside and back |
| Titles' RTF text | `UiScrollingSurface::RenderText` (`render_rich_text.cpp` lays it out) | glyph runs |
| Sprites, shapes, minimap, portraits | `UiMesh::Draw` -> `render::BeginMesh`; `ui_mesh.cpp` | GPU meshes |
| 3D previews and models | `UiModel::Draw`, the hero preview, the container draws, `render::ClearDepth` | GPU meshes |
| Rectangles, fades, letterbox, rubber band, flying hit points | `render::ScreenDraw`; `ScreenFade::Draw` | GPU |
| Cursor | `render::Cursor` (`render_cursor.cpp`) | the system's cursor |
| Movies | `bink_video.cpp` -> `MovieFrame` / `MovieBlit` | GPU draw of the CPU-decoded frame |
| Textures the interface writes | `CopyImage` (portraits, HUD icons, help pictures), `ItemFigureTexture`, the save / load pictures, the global-map clouds (noise and blend in shaders) | GPU |

What is left on the CPU per frame: the text layout of what is drawn again (the unit panel every frame, tooltips), the mesh
path's projection for picking bounds, and the Bink decode.

## 2. Text

The interface draws and measures text in its own fonts (`UiFont`, ui_font.h; Liberation Serif for Times New Roman,
Liberation Mono for Courier New, both built into the executable), laid out by `render::text` (render_text.cpp; "DrawText
layout rules" below) and drawn as glyph runs on the canvases. The user's call: not pixel-exact against GDI, but wrapping
and drawing use one set of metrics.

### Fonts and metrics

- `UiFont` is a face at an em height in pixels. `PointFont(tenths, face)` is CreatePointFont's size at 96 dots an inch (the
  interface's sizes are width-relative, so the DPI cancels but for rounding); `TextFontOf` gives the atlas font;
  `TextExtent` is GetTextExtentPoint32's rule on our advances (summed, no kerning, no prefix or tab handling).
- `Interface3D::Create` makes `fonts[0..2]` as the original sized them: "Times New Roman" at width x {104, 112, 144} / 800 /
  scale, where the scale is the pixel height of a 75-point font, so the size follows the resolution. With WideScreen the
  scale's 0.01 becomes width x 0.0075 / height (`screen_layout::FontScale`). The console has its own point font.
- The text extent feeds word wrap in `DrawWrappedText`, `UiScrollText`'s tokens (sample "W" for each font's height), tooltip
  sizes, the chat (wrap at 780), the message box and the console's prompt width. `DrawTextIn`'s return value (the text's
  height scaled back) and DT_CALCRECT measurements decide layout.
- **Smoothing.** Text is smoothed unless SPI_GETFONTSMOOTHING says off (the harness reports it off); the atlas is grey
  coverage, never ClearType's colour fringes. The original's credits switched the system's font smoothing off for the
  session (and never restored it); ours changes no system setting: the titles' text is unsmoothed by its own layout and
  the header is drawn in an unsmoothed font (`UiFont::unsmoothed`).
- The fonts are byte arrays built into the executable: CMakeLists.txt runs `src/render/embed.cmake` on each at build time
  into fonts/ in the build folder (not committed, no Windows resource); `render::text::FontForFace(face)` gives the bytes.
  Every build writes `third_party_notices.txt` beside the executables (the fonts' SIL Open Font License, SDL's zlib
  licence, dr_libs' and stb's notes).

**Draw semantics.** Transparent background; the shadow first, in 0x080808 at +1,+1. The rectangle is scaled to the screen
(without the WideScreen margin), then gets +2 on right and bottom; the text clips to it. Formats the game uses: 0,
DT_CENTER, DT_RIGHT, 4 (DT_VCENTER without SINGLELINE: no effect), DT_WORDBREAK, DT_EXPANDTABS (help page), DT_NOPREFIX,
DT_CALCRECT, DT_SINGLELINE | DT_VCENTER | DT_CENTER (the global map's titles), DT_END_ELLIPSIS (the connect list's Rebirth
names); any other bit is logged once and left out. Colours come from callers; scroll text uses BEL 'C' r g b and BEL 'F'
digit tokens. Surfaces are usually keyed on black, so antialiased edges are composited against black and become opaque
(the keyed fringe of the original's look).

**render_text** (namespace `render::text`, no game code):
- `Font`: vertical metrics, glyph advance and box, pair kerning, 8-bit coverage. `LoadFont(data, pixels)` implements it with
  stb_truetype on the font's bytes. Em scale = pixels / unitsPerEm (GDI's negative lfHeight); ascent and descent from OS/2
  usWinAscent / usWinDescent; the average width is the old OS/2 weighted 'a'..'z' + space average, because Liberation's
  xAvgCharWidth is much wider than Times New Roman's and would put tab stops in the wrong place.
- `GlyphAtlas`: one per font, pixel size and smoothing state; shelf packing with 1 px padding, grows by doubling its height
  (`Generation()` changes when it grows, so upload after `Layout`). Smoothing off thresholds coverage at one half.
- `GetTextFont(face, pixels, smoothing)` is the process-wide cache (not thread-safe); a font must outlive its queued runs.
- `Layout(font, text, rect, format, out)` gives the return value, the CALCRECT rectangle, the lines, the glyph quads (in
  rectangle pixels, still to be clipped to `clip`), the underline rectangles and the clip rectangle.
- `ComposeAgainstBlack`: the CPU reference of the keyed look (colour x coverage over black, alpha 0xFF wherever the coverage
  is non-zero, underlines full).

**Checks.** `ei_text_check` (tools/text_check; run by hand, not a build check) holds the layout rules. `--gdi` feeds our layout GDI's own
advances and kerning and compares with DrawTextA: every game string from texts.res is exact in every font, format and width;
the differences left are synthetic strings with '&', which the game's texts do not contain. `--explain <text> <width>
<format> [font]` prints both layouts and an ASCII diff. `--report` compares Liberation with GDI's hinted Times New Roman
(information): average widths match, GDI is 1-3 px taller above 800x600 font 0, and about one string in ten wraps at a
different word than GDI did. `--titles` checks the titles' rich text against a real RichEdit (below).

## 3. The canvases

**A GPU canvas per surface** (`render_ui.cpp`). Surfaces are retained images: drawn when something changes and blitted every
frame; incremental (drawing without a Clear adds to what is there); some are redrawn every frame (the unit panel's name,
tooltips). So each surface's holder has a render target, not a per-frame draw list. A canvas is made black with the surface
(`UiSurface::MakeCanvas`; again after SwapContents brings a surface back without one); it holds what the surface's memory
would: each channel cut to the format's bits and widened again, so 16-bit surfaces quantise as GDI did and the colour key
compares exactly. The surface functions queue commands on it in surface pixels, after the game's own scaling (so the
WideScreen rectangles stay upstream); the blit draws it at the blit's rectangle, keyed by the stored colour.

**Commands**: `Fill`, `FrameRect`, `Line`, `Pixel` (colour quantised on the CPU), `GlyphRun(font, layout, colour)`,
`CopyFrame(source)`, `Greyscale`, `Image` (CPU pixels; only `ei_ui_check` uses it). `GlyphRun` composes colour x coverage over
what is there (max coverage between glyphs, underlines full), so over black it is `ComposeAgainstBlack`; consecutive runs on
a canvas that overlap none of each other are drawn together in one coverage pass and one compose pass (the vertex stream is
grown for the whole group before its first run is appended: a buffer replaced between them voided the earlier runs'
vertices). `Greyscale` follows the format's bits (5:6:5, 5:5:5, else 8-bit channels).

**Order.** Commands queue in call order. `DrawPending` runs `RunCanvasCommands` before its passes. A command on a canvas
whose blit is recorded and not drawn calls `SplitFrame` first (the frame so far drawn, so the blit shows the old contents).
`CopyFrame` always splits first; between frames nothing is drawn, so the copy reads the last frame. Dropping a canvas whose
blit is not drawn yet draws the frame so far first: backdrop mode 3 puts the old backdrop away after the frame blitted it and
then copies that frame, which otherwise came out black (`ui_compare` and `ei_ui_check` hold it).

**Readers** read the GPU: `PixelAt` (`ReadPixel`, packed back to the screen format; the global-map pick must stay exact),
`Rgb565Copy` (`ReadRgb565`; the escape-menu and QuickSave pictures only need to be within tolerance). In-game screenshots
come from render::ReadFrame (renderer.md).

**Lost device.** A lost device ends the game (renderer.md, "No device-loss recovery"): a canvas keeps nothing but its
render target.

**Resolution changes.** Each surface keeps the layout rectangle it was given and is in a registry of live surfaces, so a
resolution change places them all again and resizes their canvases, what they hold stretched to the nearest pixel
(`UiSurface::RelayoutAll`, `render::ui::Resize`; renderer.md, "Resolution changes").

**GDI rules the canvases follow** (measured on Windows 10; `ei_ui_check` holds them): 16-bit surfaces truncate 8-bit colours
(no rounding, no dithering) for fills, pens and pixels. FrameRect draws a 1-px border inside the rectangle. A 1-px pen leaves
out the end point. A 2-px pen (round ends) draws both end points; vertical, it covers the column left of the line too;
horizontal, the row above too, without the start column; a zero-length 2-px line is one dot. The game uses only 1 and 2 px
pens, horizontal or vertical. 16-bit mode quantises colours before the key test: 0x080808 survives, but a near-black text
colour may become transparent, as in the original.

**Checks.** `ei_ui_check` (tools/ui_check; build.cmd runs it on gpu.h's device, hardware Direct3D 12; `--verbose`
prints each check and the difference maps) plays render.cpp's part itself (its frame, `SplitFrame`, `RunCanvasesNow`).
Fills, frames, lines and pixels are compared with GDI drawing into a DIB section in 5:6:5, 5:5:5 and 8:8:8; glyph runs
(`tests/render/glyph_cases.{h,cpp}`, no platform code) with `ComposeAgainstBlack`; greying, screen copies, frame blits (keyed,
a source part, split, hoisted) and the resize too. Any `ei::Log` line (gpu.h's problems among them) fails. `ui_compare`
opens the options and the help over them twice and requires the same frames.

### The gallery

Six scenarios (`tests/scenarios/ui_gallery_*.txt`) open every window family the game reaches with menus, keys and clicks,
hold each still (`freeze`) and capture it (`frames\<name>.bmp` in the run folder). Their goldens check that the steps keep
doing the same; the frames are not in the golden: compare them with `tools/compare_frames.py`.

| Scenario | Frames | Windows |
|---|---|---|
| `ui_gallery_menus` (no game) | menu, options, help, help_tip, options_keys, bind_box, difficulty, quit_box, load_empty, heroes, hero_new, titles | main menu and its version line; options with its spins and key rows; help (the options' topic); a tooltip; message boxes (key binding, quit) and the difficulty box; save / load with an empty list; hero list; new hero (edit field with typed text and caret, voice list, statistics panel); titles |
| `ui_gallery_game` | hud, quest_notice, top_page2, hud_tip, console, objectives, help, escmenu, save, load, view | the HUD (unit panel, map marker, top / movement / weapon panels, the FPS surface); the message log with a quest line; a HUD tooltip; console window (lines, typed line, 2-px caret); objectives; help from the objectives; escape menu over the greyed backdrop; save window (list, picture, name edit) and load window with two saves; the debug view (screen 7) |
| `ui_gallery_camp` | camp_items, camp_stats, camp_levelup, camp_trade, camp_trade4, camp_spells, camp_makeitem, map, map_escmenu | camp; item strip; item description; trade panel rows and hero statistics; trader class 1's trade pages 2 and 4; class 3's spell and item makers; global map with a quest title, and the escape menu over it |
| `ui_gallery_mp` (timeout 120) | hero_stats, connect, briefing, chat_typed, chat_sent | a made hero's statistics page; the connection screen's game list; the hosted game's base screen with the player list; the chat line (typed text, caret) and a sent line |
| `ui_gallery_base` | base, base_topics, base_phrase, base_rewards | bz1g's base screen: Smith's topic list, a dialogue phrase with the shown items, the rewards |
| `ui_gallery_deals` (timeout 120) | heroes_tip, camp_bought, camp_undone, camp_repair, camp_spell_parts, camp_spell_made, camp_item_finished, camp_item_parts, map_zone_tip, map_ways | the hero list's region 0 tooltip; the trade panel after a bought school level and after undo; the empty repair grid; the spell maker with a spell and two runes, then the made container; the item maker with a finished weapon, then its parts; the global map's zone tip and the objectives window a zone click opens |

Not captured: the status surface ("saving" / "loading": drawn and gone inside one frame); the repair grid with an item in it
(no script command or key wears one; ui.md section 5); a global-map link (travels at once); the exchange window (needs a
second player; `net_pair_zone` reaches it); the global map's pick picture (never on screen; `ui_global_map_pick` reaches its
reads). Rebirth's layout is covered by the WideScreen frames (`run_all --widescreen`).

### The titles' rich text

The titles' scrolling surface (`UiScrollingSurface`, ui_scrolling_surface.cpp) reads the RTF block with
`render::text::ReadRtf` and lays it out once at the surface's width with `LayoutRichText` (src/render/render_rich_text.{h,cpp},
no Windows calls); `RenderText` fills the canvas white (the key) and draws the lines the surface shows as glyph runs (one
`GlyphRun` per font and colour), its top `offset` twips above the surface's. The speed, the offsets in twips and the end are
the original's: the whole text fitting above 1000 twips below the surface keeps the offset, and 1000 twips plus the height
past it stop the scroll; "fits" is the text's pixel bottom at most the page bottom's (`TwipsToPixels`, MulDiv's rounding),
where RichEdit's EM_FORMATRANGE said all characters fitted.

**The RTF subset** (outro.rtf, written by Word 2000; the same in vanilla's and Rebirth's outro.res): the font table (with
\fcharset: 204 decodes \'xx as Windows-1251, 0 as 1252), the colour table, \pard / \plain, \par, \line, \tab, \b, \i, \fs, \f,
\cf, \ql / \qc / \qr (\qj as left), \li / \fi / \ri, \tx / \tqr, \u / \uc and the escaped characters; \stylesheet, \info and
every \* group are skipped, other words left out. Formats are kept per group.

**RichEdit's layout, reproduced.** Fonts at MulDiv(\fs, dpi, 144) pixels in the face's stand-in; advances without kerning; a
line breaks after its last blank that fits (a word wider than the line inside it); trailing blanks are left out of the
centring, x = left + (room - width) / 2; indents and tab stops in twips at the DPI. A line is the tallest cell ascent plus the
deepest cell descent of its characters, the paragraph mark's font counted on its last line (an empty paragraph is its
mark's height). The cell ascent and descent are the font's winAscent / winDescent scaled and rounded **up**
(`FontMetrics::cellAscent` / `cellDescent`), as Windows' Times New Roman reports them (the interface's rounded ascent gave 47
px lines instead of 49 at 192 DPI). Bold widens each glyph by max(1, (pixels + 10) / 20) pixels and its advance by as much
(GDI's emboldening); italic is drawn upright (the file has none). The text is drawn without smoothing.

**Scale.** Twips and points go to pixels at a DPI taken from the window, not the system's: `TitlesDpi` = 96 * (screen width /
800) * (0.01 / `screen_layout::FontScale`), the scale the interface's fonts get. The titles grow with the window like the
rest of the interface and do not move with the system DPI. Scenarios `ui_titles_size_small` (800x600) and
`ui_titles_size_large` (1600x900) capture the titles (the same line is 400 pixels wide at 800 and 789 at 1600);
`ui_titles_scroll` scrolls them to their end. `ei_text_check --titles` streams the file into a real RichEdit and compares each
line's bottom (by EM_FORMATRANGE) and the height that fits the whole text with ours, at several widths: 0 differ.

### Justified scroll text and the help page's block

Both changed from the original on the user's playtest; layout and state are otherwise untouched.
- **Justified lines** (`UiScrollText::DrawLines`, ui_scroll_text.cpp: the briefing, the objectives, the message log). The
  original shared the room the words leave by the line's tokens less one, format tokens included, so a line holding a colour
  / font token stopped one gap short of the right edge; ours divides by the words less one. A wrapped line whose gaps would
  pass `kMaxStretch` (3) normal gaps stays left aligned (a cap of 2 left half the ordinary lines ragged). Lines still break
  where the original's did.
- **The help page's text block** (`HelpWindow::ShowPage`, ui_help_page.cpp). The texts still wrap to the original's width
  (same line breaks, same page height); the block (its widest line, a text beside a picture counted from the picture's
  place) is centred in the page, the pictures moved with it. `Interface3D::WrapText` (ui_text.cpp) is `DrawWrappedText`'s
  wrap, which the page measures with.

## 4. Risks and open questions

- Text is laid out and measured in Liberation's metrics, not GDI's hinted Times New Roman, so line breaks differ from the
  original in a few places (the objectives' help page breaks one word earlier, the objectives' zone description and the
  briefing's scroll text move one word). DT_VCENTER without SINGLELINE and the '&' prefix in formats without DT_NOPREFIX are
  the easy ones to get wrong.
- Direct-to-frame glyph drawing (no canvas for text) would look cleaner but loses the keyed fringe.
- A resolution-independent interface scale (4K) needs atlases rasterised again and changes metrics.

## DrawText layout rules (render_text.cpp)

The rules (measured against user32's DrawTextA on Windows 10; the synthetic cases in tools/text_check check each of them):
- Lines end at CR or LF; CR LF and LF CR count as one break, CR CR as two. A break at the very end still makes an (empty) last
  line; an empty text draws nothing and returns 1.
- With DT_WORDBREAK the text is cut into words: each space or tab is a word of its own, any other run of characters up to a
  space, tab, CR or LF is one word. Words are added while the width from the line start to the word's end is at most the
  rectangle's width. The first word that does not fit ends the line before it; when it is the line's first word, the line is
  that word alone (clipped, never broken inside), even if a CR/LF follows (that break then makes an empty line).
  - Left and centred text then skip one space or tab at the start of the next line; right-aligned text does not (so a
    right-aligned wrap can leave a line holding only spaces).
  - Centred and right-aligned lines that end at such a wrap lose one trailing space or tab, for drawing and for DT_CALCRECT
    alike. Left-aligned lines keep it (DT_CALCRECT counts it).
- Widths: advances summed plus the font's pair kerning (DrawText goes through Uniscribe, which kerns; GetTextExtentPoint32, and
  so TextExtent, does not). There is no kerning pair across a tab, nor between a Latin and a Cyrillic run (a space after "в"
  is Cyrillic, so "в W" does not kern the space with the W). '&' (unless DT_NOPREFIX) is dropped and makes the next character
  the prefix character; "&&" draws one '&' and a trailing '&' draws nothing.
- Tabs: with DT_EXPANDTABS a tab moves to the next multiple of 8 average widths from the line's start; without it a tab has no
  width and no ink.
- x: left, right - width, or left + (rect width - width) / 2 rounded down. Lines are the font's height apart from the
  rectangle's top; DT_VCENTER without DT_SINGLELINE changes nothing.
- DT_SINGLELINE: the whole text is one line (CR and LF are characters, no word breaks), one line high also when the text is
  empty; with DT_VCENTER its top is the rectangle's top plus half the room left (height less the line's), rounded towards
  zero, also when the room is negative. DT_VCENTER's return value and DT_CALCRECT's bottom count from the rectangle's top to
  the line's bottom.
- DT_END_ELLIPSIS: a line wider than the rectangle loses characters from its end until its width plus "..."'s (three dots'
  advances and their kerning) is at most the rectangle's; the dots are drawn after it, kerned with the last character kept
  (DrawText draws the shortened text whole, but leaves that pair out of the fit). DT_MODIFYSTRING is dropped (the game never
  reads the shortened text back).
- Drawing stops after the first line whose bottom is below the rectangle (unless DT_NOCLIP); the return value is the height
  of the lines drawn (one line more when it stopped early and the text ends with a line break). DT_CALCRECT lays out every
  line, returns their height and sets right = left + widest line, bottom = top + height; with DT_WORDBREAK and a widest line
  wider than the rectangle it lays the text out again at that width.
- The last prefix character of each tab segment is underlined: one pixel at ascent + 1 below the line's top, from the
  character's position to the next character's (its advance at the end). A prefixed expanded tab is underlined from the pen
  to its stop one pixel higher; a prefixed tab that is not expanded takes the segment's underline away.
- Not modelled (synthetic cases only): a CR or LF inside a single line at large sizes (4K font 2) moves what follows by a
  pixel or two; a wrapped first word that holds a prefix and ends in '&' sometimes keeps the following space on its line.

## Source file overviews

### render_ui.cpp

The interface's 2D canvases (render_ui.h; section 3). A canvas is an RGBA8 render target holding a surface's pixels widened
to 8 bits a channel. Its commands queue in call order (g_queue) and RunCanvasCommands draws them, outside any pass: render.cpp
calls it at the start of DrawPending, so a canvas's drawing lands before the frame's next pass, and RunCanvasesNow for a
read-back. Solid shapes are quads of a colour quantised on the CPU; a glyph run is three steps (its coverage into an R8
scratch target with MAX blending, a copy of what the canvas held there, the composition of the two into the canvas), each a
pass; copies and an Image's pixels are drawn texel for texel (`ui_move`); greying works on a copy of the canvas; the screen
copy samples the frame target. Every pixel the shaders write is quantised to the
surface format. render.cpp's part: Command::Canvas (DrawCanvasBatch inside the frame's passes), SplitFrame and
RunCanvasesNow. The offline check tools/ui_check plays that part itself.

### render_ui.h

The interface's 2D canvases (section 3): a window surface's pixels, kept only on the GPU in a render target, drawn into by
fills, frames, lines, pixels and glyph runs and by the surface pixel code (the screen copy, greying, a picture), read by the
readers (one pixel, all of them, the 5:6:5 picture), and blitted onto the frame by the surface's blit. A canvas belongs to a
surface holder (the `Surface` of a UiSurface). Commands are queued in call order and drawn on the GPU before the frame's next
pass. Drawing into a canvas already blitted in the frame and not drawn yet first draws the frame so far (a pass split), so
the blit shows the canvas as it was. Single-threaded: the thread that draws the interface.

### render_text.h

Interface text without GDI (section 2): TrueType fonts built into the executable (stb_truetype), a glyph atlas per font and
pixel size, and a layout that follows user32's DrawTextA for the formats the game uses. The same layout gives the text extent
(GetTextExtentPoint32A), so wrapping and drawing use one set of metrics. Nothing here is pixel-exact with GDI: the fonts are
metric-compatible stand-ins rasterised without hinting. What follows DrawText exactly are the rules: where lines break, how
they are aligned, tabs, the '&' prefix, clipping and the return value / DT_CALCRECT rectangle, given the font's advances.
Output: glyph quads (atlas rectangle -> destination rectangle) plus underline rectangles, for the GPU canvas to draw, and a
CPU composition "against black" for tests.
