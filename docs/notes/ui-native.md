# Native interface drawing

How the interface's window surfaces draw on the GPU renderer: every frame each shown surface draws its quads and glyph
runs straight into the frame at the current size, and text is laid out, measured and drawn in our own fonts. There is no
GDI in the interface and no per-window render target. How the screens behave is ui.md.

## 1. How the interface produces pixels

Frame order: `Interface3D::Draw` draws the backdrop's picture, then the top window's sprites and models, then the fade.
After the scene, `DrawOverlays` draws every shown surface of the top window (containers recurse), then the tooltips, then
the status surface, the FPS surface, the console and the backdrop captures. The cursor comes last. So every window
surface is drawn above every mesh, in stack and child order.

| Primitive | Functions (file) | Path |
|---|---|---|
| Surface draw | `UiSurface::Blit` (`ui_surface.cpp`): its frame picture, then its window's paint at its rectangle | `render::ui::Solid` / `Glyphs` into the frame |
| Text | `UiSurface::DrawTextIn(text, const Rect&, TextStyle)` / `DrawTextPixels` (shadow first in 0x080808 at +1,+1), `DrawWrappedText` (`ui/ui_interface.cpp`); extents by `TextExtent` (`ui_font.h`) | glyph runs, laid out again when the font or rectangle changes |
| Fill / Clear / FillRect / FrameRect | `ui_surface.cpp` (`render::ui::FrameRects`) | solid quads |
| Lines and carets | `UiSurface::Line` (`render::ui::LineRects`: the 1-pixel pen's rule, end point left out): console, chat typed line, edit field, connect list, save / load list | solid quads |
| Single pixel | `UiSurface::Dot`: the HUD map's marker | a solid quad |
| Screen copy into a surface | `CaptureScreen`: escape-menu backdrop, the escape menu's save copy (`screenCopy`), the quick save's copy | a frame picture (`render::ui::CapturePicture`, `ui_copy`) |
| Greyscale, (2r+4g+b)/16 | `Greyscale`: escape-menu backdrop | the picture drawn through `ui_picture`'s grey option |
| Save picture | `UiSurface::ReadPicture` -> `render::ui::ReadPicture`: the copy read back at its size, made into shot.sav on the CPU (`save_picture.h`): FillEntries, QuickSave | GPU read back, CPU |
| Show / hide | `UiSurface::Show` / `Hide` | the shown flag; its frame picture is kept |
| Titles' RTF text | `UiScrollingSurface::RenderText` (`render_rich_text.cpp` lays it out) | glyph runs |
| Sprites, shapes, minimap, portraits | `UiMesh::Draw` -> `render::BeginMesh`; `ui_mesh.cpp` | GPU meshes |
| 3D previews and models | `UiModel::Draw`, the hero preview, the container draws, `render::ClearDepth` | GPU meshes |
| Rectangles, fades, letterbox, rubber band, flying hit points | `render::ScreenDraw`; `ScreenFade::Draw` | GPU |
| Cursor | `render::Cursor` (`render_cursor.cpp`) | the system's cursor |
| Movies | `bink_video.cpp` -> `MovieFrame` / `MovieBlit` | GPU draw of the CPU-decoded frame |
| Textures the interface writes | `CopyImage` (portraits, HUD icons, help pictures), `ItemFigureTexture`, the save / load pictures, the global-map clouds (noise and blend in shaders) | GPU |

What is left on the CPU per frame: the quads of every shown surface, the text layout of every painted surface's texts,
the picking rectangles' box corners, and the Bink decode.

## 2. Text

The interface draws and measures text in its own fonts (`UiFont`, ui_font.h; Liberation Serif for Times New Roman,
Liberation Mono for Courier New, both built into the executable), laid out by `render::text` (render_text.cpp; "DrawText
layout rules" below) and drawn as glyph runs into the frame. The user's call: not pixel-exact against GDI, but wrapping
and drawing use one set of metrics.

### Fonts and metrics

- `UiFont` is a face at an em height in pixels. `PointFont(tenths, face)` is CreatePointFont's size at 96 dots an inch (the
  interface's sizes are width-relative, so the DPI cancels but for rounding); `TextFontOf` gives the atlas font;
  `TextExtent` is GetTextExtentPoint32's rule on our advances (summed, no kerning, no prefix or tab handling).
- `Interface3D::Create` makes `fonts[0..2]` as the original sized them: "Times New Roman" at width x {104, 112, 144} / 800 /
  scale, where the scale is the pixel height of a 75-point font, so the size follows the resolution. With WideScreen the
  scale's 0.01 becomes width x 0.0075 / height (`screen_layout::FontScale`). The console has its own point font, at the same scale (`ConsoleWindow::Px`).
- The text extent feeds word wrap in `DrawWrappedText`, `UiScrollText`'s tokens (sample "W" for each font's height), tooltip
  sizes, the chat (wrap at 780), the message box and the console's prompt width. `DrawTextIn`'s return value (the text's
  height scaled back) and DT_CALCRECT measurements decide layout.
- **Smoothing.** Text is always smoothed, whatever the desktop's setting and in harness runs too (the user: no option;
  our rasteriser is deterministic, so captures stay the same run to run); the atlas is grey coverage, never ClearType's
  colour fringes. No system setting changes (the original's credits switched the system's font smoothing off for the
  session): the titles' text is unsmoothed by its own layout and the header is drawn in an unsmoothed font
  (`UiFont::unsmoothed`).
- The fonts are byte arrays built into the executable: CMakeLists.txt runs `src/render/embed.cmake` on each at build time
  into fonts/ in the build folder (not committed, no Windows resource); `render::text::FontForFace(face)` gives the bytes.
  Every build writes `third_party_notices.txt` beside the executables (the fonts' SIL Open Font License, SDL's zlib
  licence, dr_libs' and stb's notes).

**Draw semantics.** Transparent background; the shadow first, in 0x080808 at +1,+1. The rectangle is scaled to the screen
(without the WideScreen margin), then gets +2 on right and bottom; the text clips to it. Formats the game uses: 0,
DT_CENTER, DT_RIGHT, 4 (DT_VCENTER without SINGLELINE: no effect), DT_WORDBREAK, DT_EXPANDTABS (help page), DT_NOPREFIX,
DT_CALCRECT, DT_SINGLELINE | DT_VCENTER | DT_CENTER (the global map's titles), DT_END_ELLIPSIS (the connect list's Rebirth
names); any other bit is logged once and left out. Colours come from callers; scroll text uses BEL 'C' r g b and BEL 'F'
digit tokens. Text is alpha-blended over what the frame holds, so on a keyed surface its antialiased edges blend over
the scene (the original's edges were composited against black and keyed: a dark fringe).

**render_text** (namespace `render::text`, no game code):
- `Font`: vertical metrics, glyph advance and box, pair kerning, 8-bit coverage. `LoadFont(data, pixels)` implements it with
  stb_truetype on the font's bytes. Em scale = pixels / unitsPerEm (GDI's negative lfHeight); ascent and descent from OS/2
  usWinAscent / usWinDescent; the average width is the old OS/2 weighted 'a'..'z' + space average, because Liberation's
  xAvgCharWidth is much wider than Times New Roman's and would put tab stops in the wrong place.
- `GlyphAtlas`: one per font, pixel size and smoothing state; shelf packing with 1 px padding, grows by doubling its height
  (`Generation()` changes when it grows, so upload after `Layout`). Smoothing off thresholds coverage at one half.
- `GetTextFont(face, pixels, smoothing)` is the process-wide cache (not thread-safe); a font must outlive its queued runs.
  The caches (render_text.cpp's fonts and parsed files, render_rich_text.cpp's bold fonts) are file statics built
  before main, so they outlive the exit functions the game registers; destroying them only frees memory.
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

## 3. The surfaces drawn each frame

**Immediate drawing.** The UI is a few hundred quads and glyphs a frame, so nothing is cached in render targets: a shown
surface draws into the frame at the point of its Blit (`render::ui::Solid` and `Glyphs`, render_ui.cpp), in screen
pixels at its rectangle, clipped to it and to the screen. One program (`ui_quad`) draws both: vertices in frame pixels,
the atlas's coverage (or full coverage for solids and underlines), alpha-blended; consecutive quads with one atlas and
one clip rectangle are one draw. A keyed surface leaves out its draws in its background colour (the key), so what is
behind it shows; an opaque one draws them. A surface draws only its frame picture and its window's paint.

**Painted surfaces.** A window paints its surface each frame: `UiSurface::PaintWith` gives the surface a paint (the
window's `Paint...` function), which Blit calls after the frame picture; the fills, frames, lines, dots and texts it makes
go straight into the frame (laid out each time, at the current size and fonts), and the surface keeps nothing. What
the paint draws is the window's state (members), so a change shows from the next frame without a redraw call; a draw
whose value sets layout (a text's height, a wrapped text's line count) is measured where it ran before (`WrapText`,
`TextExtent`), the paint draws only. A draw outside a surface's paint draws nothing (a text still returns its
value), so the surface keeps no draws. Painted: the options' spins, the main menu's version line, the FPS and status surfaces, tooltips, the item
strips' counts and prices, the HUD unit panel's texts and the HUD map's marker, the global map's zone tip and title,
the help page's three surfaces, the edit field and the connection screen's note; the console (its lines, scroll and
input), the chat overlay's lines (each line's coloured pieces, measured when it is added) and typed line, the base
screen's chat window (the players' statuses Update finds), the connection screen's list, the save / load list, the
scrolling texts (the words `PlaceWords` placed when the position moved: the briefing, the objectives, the message log),
the options' rows and the objectives' quest list and zone text; the camp's action and title lines, the hero
statistics panel, the trade panel's lists, the trade, repair and spell maker grids' counts, prices and costs, the item
description panel, the exchange window's four surfaces, the multiplayer hero screen's list, commands, voices, experience
and rename title, the message box and the difficulty question, the briefing's topics and person label and the titles'
header. What a redraw did besides drawing stays where it ran: the console's and the lists' clamps, the sprites,
tips and tints, the connection screen's text colour left for its note, the message boxes' measuring.

**Labels.** Where a window's texts come from reads that must stay where they ran (the text manager's open text, the
owner's prices, the hero's statistics, the attack parameters), the window keeps the texts it worked out as `UiLabel`s
(DrawTextIn's or DrawWrappedText's arguments: text, layout rectangle, `TextStyle` {font, colour, format, shadow}, wrap lines) in a member
vector per surface, made where the draws were (a Clear empties it), and its paint is `PaintLabels`. A wrapped label is
wrapped again at the current fonts.

The titles' scrolling surface has no paint: its Blit draws the laid-out rich text at the current offset.

**Frame pictures.** A screen copy (`CaptureScreen`: the escape menu's and the camp's backdrops, the save's copy) is state as data: what the frame looked like, which cannot be drawn again. It is a picture of
the surface's size on the GPU (`render::ui::CapturePicture`: `source` of the frame drawn so far, nearest pixel, x *
source / size; the copy runs at the next DrawPending, after the frame so far is drawn, `SplitFrame`), drawn at the
surface's Blit stretched over its rectangle and greyed after `Greyscale` (`ui_picture`). A picture still drawn in the
frame is released after it. A capture between frames reads the last frame (the quick save's key).

**Reader.** `UiSurface::ReadPicture` reads the picture back as it is (`render::ui::ReadPicture`, RGBA8 at the picture's size,
a stall): only when a save's picture is made (the save window's FillEntries, QuickSave). The escape menu keeps its own
copy of the screen for that (`Interface3D::screenCopy`, a surface never shown, captured beside the backdrop) and reads it
only when a save window opens. In-game screenshots come from render::ReadFrame (renderer.md).

**Lost device.** A lost device ends the game (renderer.md, "No device-loss recovery").

**Resolution changes.** Each surface keeps the layout rectangle it was given and is in a registry of live surfaces, so a
resolution change places them all again (`UiSurface::RelayoutAll`); their paints draw at the new size from the next
frame, texts laid out in the new fonts (sharp; scenario `ui_resize_text`; renderer.md, "Resolution changes"). The
frame pictures behind the windows are taken again at the new size (`Interface3D::RetakeBackdrops`).

**GDI rules the quads follow** (measured on Windows 10; `ei_ui_check` holds them): FrameRect draws a 1-px border inside the
rectangle (`FrameRects`). A 1-px pen leaves out the end point (`LineRects`). A 2-px pen (round ends) draws both end points;
vertical, it covers the column left of the line too; horizontal, the row above too, without the start column; a
zero-length 2-px line is one dot. The game uses only 1 and 2 px pens, horizontal or vertical.

**Checks.** `ei_ui_check` (tools/ui_check; build.cmd runs it on gpu.h's device, hardware Direct3D 12; `--verbose`
prints each check) plays render.cpp's part itself with the frame of `tests/render/glyph_cases.{h,cpp}` (no platform
code; `ei_render_check` runs the glyph cases on Vulkan or Metal). Fills, frames, lines and pixels drawn as solids are
compared with GDI drawing into an 8:8:8 DIB section exactly; glyph runs with each quad's coverage blended in turn (one
level a channel); the picture's copy, its read back and its plain and grey draws exactly. Any `ei::Log`
line (gpu.h's problems among them) fails. `ui_compare` opens the options and the help over them twice and requires the
same frames.

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
| `ui_gallery_deals` (timeout 120) | heroes_tip, camp_bought, camp_undone, camp_repair, camp_spell_parts, camp_spell_made, camp_item_finished, camp_item_parts, map_zone_tip, map_ways | the hero list's region 0 tooltip; the trade panel after a bought school level and after undo; the empty repair grid; the spell maker with a spell and two runes, then the made container; the item maker with a finished weapon, then its parts; the global map's zone tip and the objectives window a zone click opens |

Not captured: the status surface ("saving" / "loading": drawn and gone inside one frame); the repair grid with an item in it
(no script command or key wears one; ui.md section 5); a global-map link (travels at once); the exchange window (needs a
second player; `net_pair_companion` reaches it). Rebirth's layout is covered by the WideScreen frames (`run_all --widescreen`).

### The drop-down list

`UiDropList` (ui_drop_list.cpp; ours) picks one of many texts: the options' resolution row (`platform::DisplaySizes`) and the main menu's list of mods (mods.md, "The menu's list") use it, the other rows keep the original's spin and slider. It is drawn in the game's list look,
nothing new: the save list's frame art (`saveload` texels 2, 0xFE - 0xB4, 0xF9, 5 wide) and shade (flat black, denser:
0xE0000000 where the save list's is 0xA0, so what the list covers hardly shows), the
rows' highlight (flat 0xC8A06800) on the active entry and a darker one (0xC8503400: a window's
sprites draw with alpha reference 200, so a fainter alpha draws nothing) on the chosen one, font 1 in the rows'
colour, the game's scroll bar (`UiScrollbar`, speed 1: a wheel notch or 1/16 s of a held arrow is one entry), and the
scroll bar's down arrow at the right of the closed row.
- **Layout** (layout units): entries 20 high, at most 10 shown, in a frame 5 wide 2 below the row, or 2 above it when the
  entries don't fit below and the room above is larger (the options' last display row opens upwards). The list is a
  surface over the whole layout painting the row and the open entries, so its texts come after every other surface of
  the window; its sprites lie in front of the rows' controls and highlight (depth 3) at depths 2.3 to 2.6: a sprite nearer
  than the near plane (NearClipDistance, 2) is not drawn at all, so a popup's sprites stay between 2 and the controls' 3.
- **Over the owner.** Sprites draw before every surface (section 1), so the owner's children under an open list (the
  sliders and spins of the rows it covers) are disabled while it is open (`covered`) and enabled again when it closes;
  the owner's own surface draws nothing in the value column. A popup over other columns of the options would need their
  texts left out the same way.
- **Input.** While open it is its owner's `UiContainer::popup`: the container's dispatch sends every event to it alone
  (ui.md, "Container input dispatch"), and the options' own LeftDown, LeftDouble and KeyDown pass straight to the container
  then. Mouse: the entry under the pointer is active; a press on an entry chooses it, on the scroll bar scrolls, outside the
  frame (the row too) closes without a change; a double click is a press. The wheel over the entries scrolls. Keys:
  Up / Down / Home / End move the active entry (scrolled into view), Enter chooses it, Escape closes; every other key is
  taken and dropped. Deactivate closes it. Closed: a click on the row opens it; the options' Enter or a double click on
  the row's name opens it, Left / Right step the value as the spin did.
- **Size and rate.** Everything is placed in layout units from pointer positions, and the scroll bar's arrows and wheel
  count frames' time and notches, so the list behaves the same at any window size and frame rate (ui.md, "Camera mouse
  turn": no motion counts are involved). `UiScrollbar::LeftDown` tests the region on the screen point (the original
  scaled the point twice, so its arrows took clicks only at 800x600).
- **Choosing** sets the value and its changed flag, as the spin did: the options' Update copies it into the setting and
  OK's Apply asks `app::ChooseResolution` (renderer.md, "Resolution changes").
- **Checks.** Scenario `ui_options_droplist` (harness steps `droplist open | pick <n> | expect <text> | isopen <0|1>`):
  the keys, the hover, the wheel, a click outside, picks by index and OK at 800x600, then the list at 1280x720; frames
  droplist_open (4:3) and droplist_wide (widescreen), on both installs.

### The titles' rich text

The titles' scrolling surface (`UiScrollingSurface`, ui_scrolling_surface.cpp) reads the RTF block with
`render::text::ReadRtf` and lays it out once at the surface's width with `LayoutRichText` (src/render/render_rich_text.{h,cpp},
no Windows calls); its Blit draws the lines the surface shows as glyph runs into the frame (one per font and colour, none
in the background colour, white, the key), its top `offset` twips above the surface's. The speed, the offsets in twips and the end are
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
rest of the interface and do not move with the system DPI. No scenario captures them. `ei_text_check --titles` streams
the file into a real RichEdit and compares each line's bottom (by EM_FORMATRANGE) and the height that fits the whole
text with ours, at several widths: 0 differ.

### Justified scroll text and the help page's block

Both changed from the original on the user's playtest; layout and state are otherwise untouched.
- **Justified lines** (`UiScrollText::PlaceWords`, ui_scroll_text.cpp: the briefing, the objectives, the message log). The
  original shared the room the words leave by the line's tokens less one, format tokens included, so a line holding a colour
  / font token stopped one gap short of the right edge; ours divides by the words less one. A wrapped line whose gaps would
  pass `kMaxStretch` (3) normal gaps stays left aligned (a cap of 2 left half the ordinary lines ragged). Lines still break
  where the original's did.
- **The help page's text block** (`HelpWindow::ShowPage`, ui_help_window.cpp). The texts still wrap to the original's width
  (same line breaks, same page height); the block (its widest line, a text beside a picture counted from the picture's
  place) is centred in the page, the pictures moved with it. `Interface3D::WrapText` (ui/ui_interface.cpp) is `DrawWrappedText`'s
  wrap, which the page measures with.

## 4. Risks and open questions

- Text is laid out and measured in Liberation's metrics, not GDI's hinted Times New Roman, so line breaks differ from the
  original in a few places (the objectives' help page breaks one word earlier, the objectives' zone description and the
  briefing's scroll text move one word). DT_VCENTER without SINGLELINE and the '&' prefix in formats without DT_NOPREFIX are
  the easy ones to get wrong.

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

The interface drawn into the frame (render_ui.h; section 3). `Solid` and `Glyphs` append quads to the frame's
`uiVertices` and a `Command::Ui` batch (one atlas, one scissor; the last batch grows while both match); `UploadUi` puts
the vertices and the atlases on the GPU before the frame's passes and `DrawUiBatch` draws a batch with `ui_quad`. The
frame pictures: `CapturePicture` makes a render target of the picture's size and queues the frame copy (`ui_copy`), run
at the start of the next DrawPending (`RunCanvasCommands`) or of a read (`RunCanvasesNow`); `DrawPicture` records a
`Command::Picture` (`ui_picture`, nearest texel, greyed on request); `ReadPicture` reads a picture back as it is. The GDI pen and frame rules as rectangles (`LineRects`, `FrameRects`). render.cpp's
part: the two commands, `UploadUi`, `UiFrameDone` after the present; tools/ui_check plays it itself.
