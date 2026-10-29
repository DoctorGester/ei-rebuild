# Native interface drawing: analysis and plan (2026-10-14)

Plan for drawing the interface's window surfaces on the GPU renderer instead of GDI into DIB sections (tickets N0-N5, P1-P8 in docs/tickets.md). Written before the work; update the status lines as tickets land.

**Status (P8, P8a, 2026-10-29): done.** Every window surface draws on its GPU canvas and nowhere else, the titles' RTF text too; the GDI path, the DIB sections, the switch words and the comparison harness are gone, and the interface measures text with our own fonts. Sections 1-3 and 5 are the plan as written; "P8 done" and "The titles' rich text" (end of section 3) and "Overview of the surface canvases" say what is.


**Summary.** The interface's art is already drawn on the GPU: sprites, minimap, 3D previews, rectangles, fades and hit points all reach `render::BeginMesh`, `ScreenDraw` or `TlDraw`. What is still CPU-only is the window surfaces (`CUiSurface`, 0x38 bytes). These are text, fills, frames, carets and lines drawn with GDI into a DIB, and today that DIB is re-uploaded (memcmp, convert, upload) whenever it changes.

The proposal is to keep each surface as a persistent GPU canvas, a render target owned by `src/render`. Drawing calls are recorded in surface pixels at the GDI boundary and replayed on the GPU. GDI stays only for metrics, so layout stays identical. The work is about 4 preparation tickets that can start now, then 7–8 porting tickets.

## 1. Inventory: how the interface produces pixels

The frame order is this. `CInterface3D::Draw` 005F5220 (inside BeginScene..EndScene) draws the backdrop blit, then the top window's slot 20 (sprites and models), then the fade. After EndScene, `DrawOverlays` 005F5320 runs the top window's slot 22 (`RedrawBase` 005FECC0, which blits every shown surface through 005FDA80; containers recurse in 00601040), then slot 23 (tooltips 005FED00), then the status surface, FPS surface, console and the backdrop captures. The cursor 005E54A0 comes last. So every window surface is drawn above every mesh, in stack and child order.

| Primitive | Functions (file) | Users / call sites | Path today |
|---|---|---|---|
| Surface blit (plain, colour-keyed, stretched with flag 2) | 005FDA80 `ui_surface_blit.cpp` → `render::SurfaceBlit` → `render_textures.cpp WindowSurfaceView` (memcmp of the DIB at every blit, then convert and upload) | every surface: about 45 `AddSurface` sites in 22 files, plus the console, FPS / status surfaces and backdrops | GPU blit of a CPU texture |
| Text | 005FDE20 `CUiSurface::DrawTextIn` (`ui_text_draw.cpp`: GDI DrawText on the surface's HDC (the original: MFC's CDC slot 26); with a shadow, first in 0x080808 at +1,+1); 005FDFA0 `DrawWrappedText` (`ui_text.cpp`) | about 71 `SurfaceDrawText` / `DrawWrappedText` sites in 35 files (most: saveload_list 7, exchange 7, hero_screen 6, help_page 6, stats_rows 4); 26 more through `SurfaceDraw::Text` in `ui_item_info.cpp`; direct `dc->DrawText` in the console (4) | CPU (GDI) |
| Fill / Clear / FrameRect | 005FE370, 005FE2F0, 005FDCB0 (`ui_surface_frame.cpp`); `gdi::FillSolidRect` (the original's `CDC::FillSolidRect` 0070BADA / 0070BB04) | about 50 Clear lines in 27 files; Fill and FrameRect only in the tooltip (`ui_tooltip.cpp:131`); FillSolidRect in the console (3) | CPU |
| Lines and carets (a solid pen, width 1 or 2: gdi::Pen) | MoveTo / LineTo / SelectPen | console 005E4A10, chat typed line 005E3700, edit field 0065C2E0, connect list 0061D780, save/load list (`ui_saveload_list.cpp:104`) | CPU |
| Single pixel | `SetPixel` | minimap marker: N0 found `CHudMap::SetupVanilla` 00641F20, about 33 pixels on a 24x18 surface (18x18 under Rebirth), drawn again at each HUD setup | CPU |
| Get / release DC (start and end of drawing) | 005FDC00 / 005FDC60 (`ui_surface_dc.cpp`: a memory DC on the DIB) | about 57 sites in 38 files | CPU |
| Screen copy into a surface | 005FD3D0 `CaptureScreen` → the surface's canvas (`ps_copy`; R28a, canvas-only in every mode) | escape-menu backdrop (`ui_interface_draw.cpp`), global-map pick picture (`ui_global_map_places.cpp`), quick save (`ui_saveload.cpp`) | GPU |
| Greyscale, (2r+4g+b)/16 | 005FE440 → `ps_grey` on the canvas (R28a) | escape-menu backdrop | GPU |
| Save picture | 005FD440 `Rgb565Copy` → `ps_rgb565` into an R16_UINT target, (2x+1)·src/(2·size), read back (R28a) | escape-menu `screenCopy_` (256x192), QuickSave 006570E0, SaveGame 00655550 | GPU, 5:6:5 read-back |
| Pixel read | 005FD240 `PixelAt` | global-map click (`ui_global_map_input.cpp:128`) and FindPaths (`ui_global_map_paths.cpp:171`): zone = (pixel>>4)&0xF − 1 | CPU, reads pixels |
| Swap / freeze | 005FD7A0 `SwapContents`, 005FD760 `Freeze` | camp, menus, backdrop mode 3/4; N0: also every window's `ShowSurfaces` 006002C0 on each show and hide | CPU memcpy |
| Titles' RTF text | 0065F020 `CUiScrollingSurface::RenderText` (the original: RichEdit EM_FORMATRANGE into the DIB) | titles / credits | GPU glyph runs (P8a) |
| Sprites, shapes, minimap, portraits | `CUiMesh::Draw` 005E6D90 → 004E9010 → `render::BeginMesh`; shape builders 005E70E0/7680/7D60/99D0; `ui_mesh_build.cpp` | every panel | GPU (done) |
| 3D previews and models | `CUiModel::Draw` 005FC2F0, hero preview 005EC300, container draws 00600EF0 / 00622420, `render::ClearDepth` | camp, hero, HUD | GPU (done) |
| Rectangles, fades, letterbox, rubber band | slot 19 005041F0 → `render::ScreenDraw`; `CScreenFade::Draw` 005E69C0 | — | GPU (done) |
| Flying hit points | 005E18E0 → 00501BB0 → `TlDraw` | — | GPU (done) |
| Cursor | 005E54A0 → `render::Cursor` (`render_blit.cpp`: CPU composite plus `sg_make_image` every frame) | — | GPU draw, CPU work per frame |
| Movies | `bink_video.cpp` → `MovieFrame` / `MovieBlit` (Bink decodes on the CPU) | — | GPU draw; the CPU decode cannot be avoided |
| CPU-written textures used by sprites | `CopyImage` 005FFFB0 (16 sites in 8 files: portraits, HUD icons, help pictures) and `ItemFigureTexture` 005EC020 draw on the GPU (R28); the save / load pictures and the global-map clouds (`CGlobalMapClouds::Draw`, every frame: noise and blend in shaders) too (R28b) | — | GPU (done) |

Notes on the inventory:
- The `3DfpFPU.dll` work in the interface is limited to the mesh path's CPU projection for picking bounds (2DintMMX's picture and texture conversion, 004F0060 and the decoder at 007AF06C slot 5, is gone since R28). Neither does per-frame window pixel work.
- Tooltips, edit boxes, list boxes, the console and chat are all CPU surfaces.
- The minimap and global map are GPU meshes, except for the pick readback, the clouds texture and the one-pixel dot.

## 2. Text

**Changed 2026-10-14 (user):** the native path does not use GDI at all, for metrics or glyphs, and need not be pixel-exact. N2 loads its own font (TrueType rasteriser plus a font metric-compatible with Times New Roman) and its layout also serves the text extent, so wrapping uses the same metrics as drawing. The GDI notes below describe the original behaviour to approximate.

**Fonts.** `CInterface3D::Create` 005F4990 (`ui_interface_create.cpp`) makes `fonts_[0..2]` with `gdi::PointFont` (the original's `CFont::CreatePointFont` 0070BBE5): "Times New Roman" at width·{104, 112, 144}/800/scale. The scale is the pixel height of a 75-point font. So the font size depends on resolution; with WideScreen the scale's 0.01 becomes width·0.0075/height (addon's float at 005F4F81, `widescreen::FontScale`; W4).
- The console has its own point font (`console_window_create.cpp`).
- All text is GDI: no bitmap fonts, quality left at its default, so the system's ClearType / smoothing setting decides the look.
- The credits switch font smoothing off for the session.

**Metrics.**
- The text extent 005F8340 calls `GetTextExtentPoint32A` on `GetDC(game window)` and scales by 800/width and 600/height. It feeds word wrap in 005FDFA0, `CUiScrollText::Tokenize` 0065D4B0 / `DrawLines` 0065D150 (sample "W" for each font's height), tooltip sizes, the chat (wrap at 780) and the message box.
- 005FDE20's return value (the DrawText height scaled back through 005F7E30) and DT_CALCRECT measurements (`ui_layouts.cpp:556/615`, format 0x411) decide layout.
- **Rule: metrics must stay GDI.** In the hook build, keep calling GDI DrawText / extent on a metrics DC (a 1x1 DIB memory DC, or an empty clip region). The pixels go away; the numbers stay bit-exact.

**Draw semantics to reproduce.**
- Transparent background. The shadow is drawn first, in 0x080808 at +1,+1.
- The rectangle is scaled through 005F7CA0 (W2 changes this to "no margin"), then gets +2 on right and bottom. DrawText clips to it.
- Formats seen: 0, DT_CENTER, DT_RIGHT, 4 (DT_VCENTER without SINGLELINE, so no effect), DT_WORDBREAK, DT_EXPANDTABS (help page), DT_NOPREFIX (0x800), DT_CALCRECT. Formats without DT_NOPREFIX turn '&' into an underline prefix.
- Colours come from callers. Scroll text uses BEL 'C' r g b and BEL 'F' digit tokens.
- Surfaces are usually keyed on black. Antialiased edges are therefore composited against black and become opaque, which is the dark fringe of today's look.

**What a GPU text path needs** (`src/render`, independent of game code):
- **Glyph atlas.** One per HFONT and smoothing state, rasterised by GDI itself: TextOut white on black into a 32-bit DIB, keeping RGB coverage so ClearType is reproduced. Advances from `GetCharABCWidths` / `GetCharWidth32`. DrawText applies no kerning.
- **Layout.** Reproduces DrawText for the flags above: line breaking, alignment, tabs (8 × average width), '&' prefix and underline, clipping, multibyte lead bytes.
- **Draw command.** Glyph quads drawn in "against black" mode: colour × coverage, and opaque wherever coverage > 0. This reproduces the keyed CPU look.
- **Validation (offline, exact).** For a corpus of all game strings × 3 fonts × 800x600 / 1024x768 / 1280x720 / 1920x1080 / 3840x2160, compare the atlas composition on the CPU against GDI DrawText into a DIB. Require the same line breaks and identical pixels, except where glyphs overhang.

### N2 done (2026-10-14): `src/render/render_text.{h,cpp}` and `tools/text_check`

**What exists** (namespace `render::text`, no GDI and no game code):
- `Font`: the small font interface (vertical metrics, glyph advance and box, pair kerning, 8-bit coverage). `LoadFont(data, pixels)` implements it with stb_truetype 1.26 on a font's bytes in memory (`STB_TRUETYPE_IMPLEMENTATION` in render_text.cpp only). Em scale = pixels / unitsPerEm, which matches GDI's negative lfHeight. Ascent and descent come from OS/2 usWinAscent / usWinDescent. The average width is the old OS/2 weighted 'a'..'z' + space average, because Liberation's xAvgCharWidth is much wider than Times New Roman's, which would put tab stops in the wrong place.
- Fonts: `FontForFace` gives Liberation Mono for "Courier New" and Liberation Serif for every other face, both built into the executable (P2a: see "P2a done"). `PointSizeToPixels(tenths, dpi)` = dpi·tenths/720, as `CreatePointFont`.
- `GlyphAtlas`: one per font, pixel size and smoothing state. It uses shelf packing with 1 px padding and grows by doubling its height; `Generation()` changes when it grows. Smoothing off thresholds coverage at one half.
- `TextFont`: font + atlas + cached advances and kerning. `GetTextFont(face, pixels, smoothing)` is the process-wide cache and is not thread-safe.
- `DecodeCp1251` (checked against MultiByteToWideChar: 0 of 256 bytes differ). There are no DBCS lead bytes; the game is 1251 only.
- `Layout(font, text, rect, format, out)`: DrawTextA-compatible for 0, DT_CENTER, DT_RIGHT, 4, DT_WORDBREAK, DT_EXPANDTABS, DT_NOCLIP, DT_NOPREFIX and DT_CALCRECT. It gives the return value, the CALCRECT rectangle, the lines, the glyph quads (atlas x/y/size, screen x/y), the underline rectangles and the clip rectangle. `TextExtent` is GetTextExtentPoint32: advances summed, no kerning, no prefix or tab handling.
- `ComposeAgainstBlack(font, layout, colour, pixels, ...)`: the CPU reference of the keyed look. Each pixel gets colour × coverage over black with alpha 0xFF wherever the coverage is non-zero, and underlines are full coverage.

**DrawTextA rules measured on Windows 10** (the code's rules comment and the tool's 156 hand-measured cases hold them all):
- **Kerning.** DrawText goes through Uniscribe and applies the font's pair kerning, also with prefixes. GetTextExtentPoint32 does not kern. This corrects "DrawText applies no kerning" above. There is no kerning pair across a tab, expanded or not. Uniscribe also does not kern across a change of script: spaces, digits and punctuation join the run before them, so "в W" does not kern the space with the W, while "a W" does. This shows from about 30 px up, where the space–W pair rounds to −1.
- **Word wrap.**
  - Every space and tab is a word of its own.
  - A word that overflows the rectangle (width > rect width) ends the line before it.
  - When it is the line's first word, it is a line on its own, even if a CR/LF follows; that break then makes an empty line.
  - Left and centre skip one white character at the next line's start; right does not.
  - Right and centre drop one trailing white character at a wrap (for CALCRECT too); left keeps it.
- **Breaks.** CR+LF and LF+CR count as one break; CR CR is two. A trailing break adds an empty line. Empty text returns 1.
- **Tabs.** Without DT_EXPANDTABS a tab has zero width and no ink. With it, stops are 8·tmAveCharWidth apart, measured from the line's own start.
- **Centre.** x = left + floor((rect width − width) / 2).
- **Early stop.** After a line, y += line height. Drawing stops when y > bottom (unless NOCLIP / CALCRECT). If it stopped early and the text ends with CR/LF, one more line height is added to the return value.
- **CALCRECT.** right = left + widest line, bottom = top + lines·height. With DT_WORDBREAK and a widest line wider than the rectangle, DrawText lays out again at that width, and the second layout's lines are the answer.
- **Prefixes.** "&&" draws '&', and a trailing '&' draws nothing. The last prefix character of a tab segment is underlined: 1 px at top + ascent + 1, from the character to the next character's kerned position. A prefixed expanded tab is underlined from the pen to its stop, one pixel higher. A prefixed non-expanded tab removes the segment's underline.

**Exactness test** (`ei_text_check --gdi`) compares our layout and composition, fed GDI's own advances and kerning through a
GDI-backed `Font` in the tool, against DrawTextA: 12101 unique strings from texts.res plus 1044 synthetic ones across 16 fonts (5
resolutions x 3 interface fonts, plus the console), 8 formats x 3 widths (return value and CALCRECT rectangle: 471 of 5,047,680
layouts differ, 0.009%), pixels non-antialiased on every 50th game string and every synthetic one (1,528 of 2,060,563 differ).
Every remaining difference is a synthetic string with '&'; the game's texts contain none, and every game string is exact in every
font, format and width. One quirk is not modelled: an overflowing first word that holds a prefix and ends in '&' / "&&" sometimes
keeps the following space on its line. `--explain <text> <width> <format> [font]` prints both layouts and an ASCII diff of the two
drawings (reads \r, \n and \t from the wide command line). The tool's GDI font takes control characters' widths from DrawText
(Uniscribe), not from GetTextExtentPoint32; the stb font gives them zero width.

**Liberation versus GDI** (`ei_text_check --report`, game strings only; information, not a gate): average width matches on all 16
fonts; height and ascent match at 800x600 font0 (13 px) and 1024x768 font0, elsewhere GDI's hinted Times New Roman is 1-3 px taller
(the console 15 vs 14). 5.8% of 4,646,784 layouts break differently (9.6% at font 0, where heights agree); single-line extents
differ on 110,834 of 148,752 (mean 5.9 px, worst 174 px; Liberation +0.67% wider, +1.5% at 13 px, because GDI uses hinted advances
and stb_truetype unhinted ones). So wrapping through our own metrics everywhere keeps text consistent with itself, but about one
string in ten wraps at a different word than today's GDI at 800x600.

**For P2 and later:**
- **The key test.** The CPU path keys on the composed colour, not on coverage. A near-black composed pixel (for example the 0x080808 shadow at low coverage) is keyed out or quantised in 16-bit mode, so the GPU path should use "composed colour != key" rather than "coverage > 0" when it must match the CPU look.
- **The shadow** (0x080808 at +1,+1) is drawn first and can land over non-black pixels of the second pass; composite both passes in order.
- **Quads.** `TextLayout::quads` are in rectangle pixels and still need clipping to `TextLayout::clip`, unless `clipped` is false.
- **Atlas.** It can grow during a layout, so upload after `Layout` and check `Generation()`.
- **Pre-existing build-check failure, not N2.** `build.cmd release` fails in check_returns on the release map, on main too: dbghelp leaves 619 names to undname, mostly from libcpmt:vector_algorithms.obj. The 200-name chunks then exceed the command-line limit (53,117 characters; WinError 206). The verify map passes.

## 3. Architecture proposal

**Model: a GPU canvas per surface.** Surfaces are retained images: they are drawn when something changes and blitted every frame. They are also incremental (drawing without a Clear adds to what is there), and some are redrawn every frame (the unit-panel name in 006462A0, tooltips). So the GPU equivalent is a render target per holder, not a per-frame draw list.

- Commands queue between GetDC and ReleaseDC and are executed into the canvas in order.
- The blit 005FDA80 draws the canvas texture at the blit's rectangle, keyed by alpha, stretched for flag 2.
- No memcmp, no upload, no list growth. The "dirty" state is just "commands pending".
- A canvas updated after it was already blitted in the same frame uses the existing pass split (`DrawPending` / `g_drawnCommands` in `render.cpp`). Updates made before a canvas's first blit of the frame are hoisted to the start of the frame.

**API: `src/render/render_ui.{h,cpp}`** (new files; plus `render_text.{h,cpp}`):
- `Canvas(holder, w, h, masks, key)`, `Fill(holder, rect, colour)` (a colour equal to the key, after quantising to the surface format, punches transparency), `FrameRect`, `Line(x0, y0, x1, y1, width, colour)` (GDI's end-point-exclusive rule), `Pixel`.
- `Text(holder, font, layout, colour, clip)`.
- `CopyFrame(holder, screenRect)`: a GPU copy of the frame drawn so far.
- `Greyscale(holder)`: a shader pass, ping-pong.
- `Image(holder, srcHolder or texture, src, dst, keyed)`: room for pictures later.
- `ReadBack(holder)`: writes the canvas into the holder's DIB in the surface's format.
- `DrawCanvas(holder, dst, src, keyed)`: a new Frame command kind.
- Blend modes needed: opaque fill, "glyph against black", alpha-keyed blit, point-sampled stretch. Scissor equals the clip rectangle intersected with the surface. A 16-bit mode quantises in the shader so key matches and colours agree.

**Game-side hand-over.** Record at the GDI boundary, in surface pixels, after the game's own scaling: 005FDE20, Fill / Clear / FrameRect, and the pen, LineTo and FillSolidRect wrappers in `ui_gdi.h`. This keeps WideScreen orthogonal: W1/W2 change the rectangles and scale functions upstream, and the 2D layer only ever sees final pixels.
- **Native table.** Whether a surface is native is decided per surface, in a side map keyed by the surface. `CUiSurface` is game-allocated at 0x38 bytes, so no new fields can be added. The flag is set from a table of window vtables in a new `ui_native.cpp`, keyed by the window recorded in `AddSurface` 005FFA90.
- **Switch.** `EI_RENDER` words:
  - `uinative[=all|list]`: native surfaces skip GDI pixels; GDI is still called on the metrics DC for return values.
  - `uiboth` (verify default): record and draw GDI, so the existing pixel wrappers (`pixel_settle.h` users) keep working, and the per-surface diff can run.
  - Default stays CPU.
- **Fallback.** A surface that any code we have not ported draws into stays CPU, uploaded as today. This covered RichEdit 0065F020 (until P8a), and Rebirth-patched draws that still run addon's code in a Rebirth launch (none left after W2–W4, W2a, W2b and W4a: the connect list 0061D780 and Create 0061B2C0, and the hero screen's Create 00613EA0 are ours in a Rebirth launch).
- **Readers.**
  - `PixelAt` and `Rgb565Copy` call `ReadBack` first, then run the original CPU code unchanged. This keeps the format round trip the global-map pick relies on.
  - `SwapContents` keeps the canvas; the 007C7E74 / 007C7E78 totals are still computed from pitch × height.
  - `CaptureScreen` becomes a GPU copy, with no staging readback unless someone reads it.

**Caching.** Canvases live until `HolderGone`. The glyph atlas is kept per HFONT and rebuilt when the smoothing state (`SPI_GETFONTSMOOTHING`) changes. The cursor image and mask become cached textures keyed by (holder, from-rect) instead of `sg_make_image` every frame.

**Verification.**
- **(a) Per-surface diff in the verify build under `uiboth`.** After each ReleaseDC, or when a canvas changes, read the canvas back and compare it with the DIB after the key (per-channel tolerance about 24 for glyph edges, exact for fills). The log carries the window vtable, surface index and differing fraction. PNG pairs are dumped on failure, and the run fails above a limit. This is precise and per window, and it needs no capture choreography.
- **(b) Frame A/B within one frozen moment.** Steps: `uipath cpu`, capture; `uipath native`, capture; then `samecapture` with a UI mask. The renderer writes `frames\<name>.uimask.bmp` from the native blit rectangles. Both representations exist under `uiboth`, so switching is instant.
- **(c) Suite.** The UI scenarios run with `new,present,uiboth` on vanilla and on `run_all --native`; `--widescreen` keeps comparing against addon.

### N1 done (2026-10-21): `src/render/render_ui.{h,cpp}`, `shaders/ui_canvas.hlsl`, `tools/ui_check`

**What exists** (namespace `render::ui`, no GDI, nothing calls it yet):
- `Canvas(holder, w, h, format)` makes an RGBA8 render target, black. The same size and format again keeps it; another one makes it
  again, black. It holds what the surface's memory would: each channel cut to the format's bits and widened again (`Expand`'s
  rule), so `ReadBack` packs it back exactly. `DropCanvas` runs from `HolderGone`; a lost device brings canvases back black
  (`DropCanvases`), so P8 needs a CPU copy or a redraw before it drops the DIBs.
- Commands, in surface pixels: `Fill`, `FrameRect`, `Line`, `Pixel` (colour quantised on the CPU), `GlyphRun(font, layout,
  colour)`, `CopyFrame(source)`, `Greyscale`. `GlyphRun` composes colour × coverage over what is there (max coverage between
  glyphs, underlines full), so over black it is `ComposeAgainstBlack`. `Greyscale` follows 005FE440 on the format's bits (5:6:5,
  5:5:5, else 8-bit channels). `CopyFrame` uses CopyFrameTo's stretch and truncation.
- `ReadPixels` (0x00RRGGBB) and `ReadBack` (into SurfaceMemory, marked written) run the queue first. `DrawCanvas(holder, dst,
  src, keyed, key)` records `Command::Canvas`; it draws like the window blit, with the key compared on the stored colour.
- **Order.** Commands queue in call order. `DrawPending` runs `RunCanvasCommands` before its passes (hoisting). A command on a
  canvas whose blit is recorded and not drawn calls `SplitFrame` first (the frame so far drawn: the blit shows the old contents).
  `CopyFrame` always splits first; between frames nothing is drawn, so the copy reads the last frame. `RunCanvasesNow` takes the
  present's lock (the cursor thread).
- **GDI rules measured** (Windows 10; `ei_ui_check` holds them): 16-bit DIBs truncate 8-bit colours (no rounding, no dithering)
  for fills, pens and SetPixel. FrameRect draws a 1-px border inside the rectangle, and odd shapes for an empty one (not
  modelled; the game never passes one). A 1-px pen leaves out the end point. A 2-px pen (round ends) draws both end points.
  Vertical, it covers the column left of the line too; horizontal, the row above too, without the start column. A zero-length
  2-px line is one dot. Wider pens and slanted lines are approximate; the game uses only 1 and 2 px, horizontal or vertical.

**Check.** `ei_ui_check` (build.cmd runs it after the vector replay; about 0.3 s on WARP; `--hardware` uses the GPU, `--verbose`
prints each check and the difference maps). It plays render.cpp's part: its frame, `SplitFrame`, `RunCanvasesNow`, and
SurfaceMemory are its own. It gates 64 checks exactly. Fills, frames, 1- and 2-px lines and pixels are compared with GDI drawing
into a DIB section in 5:6:5, 5:5:5 and 8:8:8. Glyph runs (shadow then text over fills, clipping, underlines, Cyrillic) are
compared with the CPU rule and `ComposeAgainstBlack`. Also checked: greying against 005FE440's formulas, screen copies against
CopyFrameTo's, and five frame blits (keyed, stretched, a source part, split, hoisted). Any sokol warning or `ei::Log` line fails.

**For P1:** call `Canvas` where the surface's memory is made (005FDA80's holder), record at the GDI boundary in surface pixels,
`DrawCanvas` in place of `SurfaceBlit` for native surfaces, `ReadBack` before PixelAt / Rgb565Copy / SwapContents read pixels.
The text font must outlive its queued runs (`GetTextFont`'s are process-wide). The atlas texture is remade whenever the atlas's
generation changes (every new glyph); a partial update is a later optimisation.

### N3 done (2026-10-21): the switch and the comparison harness

**The switch** (`render_ui.h`, read by render.cpp's `ParseSpec`): EI_RENDER's `uiboth` (GDI and the canvas both drawn) and
`uinative` (the canvas only), each optionally `=<family>+<family>` (`ui::Listed(name)`; P1 names the families, e.g. backdrop,
fps, status). No word: today's path. `ui::SetPath` picks what the blits show under `uiboth`; `ui::DrawsCanvas(holder)` is the
blit's test. `ui::Image(holder, rect, rgb, pitch)` writes pixels into a canvas (quantised): the fallback for drawing the canvas
cannot record (GDI text before P2, RichEdit, code not ported). `ui::Canvases` (version, the glyph runs' clip rectangles) and
`ui::DrawnCanvasRects` (the last drawn frame's canvas blits) are the harness's view. `ei_ui_check` gates them too (73 checks).

**The diff** (`tests/verify/harness_ui.{h,cpp}`, steps and settings in the header): under `uiboth`, after each frame every
canvas whose version or memory changed is read back (`ReadPixels`) and compared with its holder's memory widened the canvas's
way. Outside the glyph runs' rectangles exactly (`uidiff tolerance`), inside them by `block` averages within `text` (default 24),
each with its own fraction limit. A differing surface is logged by name (`W<window vtable>#<index>`, or backdrop, backdrop2,
fps, status, console), its pair written as `frames\uidiff<n>_<name>_gdi.bmp` / `_canvas.bmp`, and fails the run unless
`uidiffcheck <least> [most]` expected it. `uipath native|cpu` plus `capture` and `uimask <name>` (white where canvases were
blitted) give the frame A/B: `python tools\compare_frames.py <a>.bmp <b>.bmp --mask-file <name>.uimask.bmp`.

**Proof before P1** (`tests/scenarios/ui_compare.txt`): `uimirror on` feeds a canvas for every named surface from its memory;
the menu and options window's checks all match exactly, a `uifault` square is found once and the next check passes, the two
paths' captures are identical and the mask is empty (nothing calls `DrawCanvas` yet).

**For P1:** make the canvas where the surface's memory is made and record at the GDI boundary; under `uiboth` call GDI too;
for text until P2, `Image` the DrawText rectangle from the memory after GDI draws (or the diff will flag every text surface);
mirror `SwapContents` (a swapped-out holder has no memory to compare). The UI scenarios then run with `render new,present,uiboth`.
P2 tunes `uidiff text / block / textlimit` for our own fonts (whole-word shifts need wider blocks or a looser limit).

### N4: the gallery (2026-10-21): `tests/scenarios/ui_gallery_*.txt`

Six scenarios open every window family the original reaches with menus, keys and clicks, hold each still (`freeze`) and
capture it (`frames\<name>.bmp` in the run folder); N4a's `ui_gallery_deals` adds the camp's deals and the map's zone
click. The hero list's region 0 tooltip showed with `moveregion 0`, two `move`s a pixel apart a few frames later and 3 s;
N4's `moveregion` with 2.5 s showed none (which of the two differences matters is untested). Each has a vanilla golden from three agreeing runs of the original
(plain, `EI_HEAP_JITTER=1`, `2,fill`; no EI_COVER), so the suite checks that the steps keep doing the same; the frames
themselves are not in the golden. They are the coverage for N3's per-surface diff and for P1-P8: run them under `uiboth`
/ `uinative` and compare.

| Scenario | Frames | Families (census vtable) |
|---|---|---|
| `ui_gallery_menus` (no game) | menu, options, help, help_tip, options_keys, bind_box, difficulty, quit_box, load_empty, heroes, hero_new, titles | main menu and its version line (drawn before AddSurface); options 00741FAC with its spins CUiSpin 00742D5C and key rows; help 00740E28 (the options' topic); tooltip CUiTooltip 00742F64 (the help's OK button, FrameRect); message boxes (key binding, quit) and the difficulty box (BoxText, DT_CALCRECT); save / load 00741388 with an empty list; hero list 0074214C; new hero: edit field CUiEditField 00742DF4 with typed text and caret, voice list, CHeroStatsPanel 00741ABC; titles 00740C3C (RichEdit EM_FORMATRANGE) |
| `ui_gallery_game` | hud, quest_notice, top_page2, hud_tip, console, objectives, help, escmenu, save, load, view | HUD: unit panel CHudUnitPanel 007426F8, map CHudMap 00742660 (SetPixel marker), top / movement / weapon panels, the FPS surface (`console fps on`); message log CUiScrollText 00742E9C with a quest line; HUD tooltip (movement panel); console window (lines, typed line, 2-px caret); objectives; help from the objectives; escape menu over the greyed backdrop (CaptureScreen, Greyscale, Rgb565Copy); save window (list with pen lines, picture, the name edit field) and load window with two saves; the debug view (screen 7) |
| `ui_gallery_camp` | camp_items, camp_stats, camp_levelup, camp_trade, camp_trade4, camp_spells, camp_makeitem, map, map_escmenu | camp 0074091C; item strip 0074197C / 00621B50; item description CItemInfoPanel 007404DC (a spell weapon, a statistics row); trade panel rows 007417A8 and hero statistics (page 1); trader class 1's trade pages 2 and 4 (CTradeGrid); class 3's spell and item makers with filled strips; global map with a quest title, and the escape menu over it |
| `ui_gallery_mp` (timeout 120) | hero_stats, connect, briefing, chat_typed, chat_sent | a made hero's statistics page; connection screen CMpConnectScreen 00741CD0 (the game list with its pen lines); the hosted game's base screen with the player list CChatWindow 00741478; chat line CChatOverlay 007400DC (typed text, caret) and a sent line (the unowned chat line surface) |
| `ui_gallery_base` | base, base_topics, base_phrase, base_rewards | bz1g's base screen (briefing): Smith's topic list, a dialogue phrase with the shown items, the rewards |
| `ui_gallery_deals` (N4a, timeout 120) | heroes_tip, camp_bought, camp_undone, camp_repair, camp_spell_parts, camp_spell_made, camp_item_finished, camp_item_parts, map_zone_tip, map_ways | the hero list's region 0 tooltip (0x9CA5); the trade panel after its click bought a school level ("give 0 exp" with the camp open) and after the undo button (RestoreRecord); page 6's repair grid CRepairGrid (empty cells); class 3's spell maker with a spell and two runes, then the made container; the item maker with a finished weapon, then its parts (DeconstructItem); the global map's zone tip (gz2g open) and the objectives window a zone click opens (FindPaths) |

**Not captured, and why:**
- The status surface (005F75B0, "saving" / "loading" notices): it is drawn during the save inside one frame and gone by the
  next, so no capture can hold it. `ui_gallery_game`'s save reaches it (the per-surface diff sees it).
- The repair grid with an item in it: `ui_gallery_deals` captures page 6's grid drawn empty; only a worn item is offered
  for repair, and no script command or key wears one (ui.md section 5).
- A global-map link (regions 3 and up): it needs a way of kind 2 open and travels at once (no window to hold).
- The exchange window (`ui_exchange.cpp`): it needs a second player; `net_pair_zone` reaches it (a pair, no frames).
- The global map's pick picture: never shown on screen; `ui_global_map_pick` reaches its PixelAt reads.
- Rebirth launches: the gallery's goldens are vanilla only (the suite's install); Rebirth's layout is WideScreen's
  frames (`run_all --widescreen`).

### P1 done (2026-10-21): surface plumbing, `src/game/ui_native.{h,cpp}`

**Native surfaces.** `ui_native::Adopt(surface, family)` runs where a surface is made and gives its holder a canvas when
EI_RENDER lists the family (`uiboth` / `uinative`, `=backdrop+fps+status`): `backdrop` (005F5320's MakeBackdrop, both
backdrops), `fps` (ShowFps 005F5700), `status` (ShowStatusText 005F75B0). The side map is keyed by the surface (the canvas
by its holder); `Forget` runs in the constructors and ReleaseContents. No `ui` word: nothing is adopted and every hook
returns at once, so the default path is today's.

**What records.** Fill / Clear 005FE370 / 005FE2F0 (`Fill`), FrameRect 005FDCB0, CaptureScreen 005FD3D0 (`CopyFrame`),
Greyscale 005FE440, all in surface pixels after the game's scaling. Text until P2: 005FDE20 copies its DrawText rectangle
(with the shadow's pixel, the whole surface for DT_NOCLIP, nothing for DT_CALCRECT) from the memory onto the canvas with
`Image` after GDI drew. SwapContents' restore makes the new holder's canvas from the kept bytes (`Restored`). The blit
005FDA80 calls `DrawCanvas` when `DrawsCanvas(holder)`, else `SurfaceBlit`.

**The memory stays the source of truth.** GDI and the CPU code keep drawing it in both modes (a lost device brings canvases
back black). Only under `uinative` do the screen copy (no frame read-back) and greying a surface whose memory is behind go
to the canvas alone; the memory is then behind until `Sync` reads the canvas back (`ReadBack`), which PixelAt 005FD240,
Rgb565Copy 005FD440 and SwapContents' save call first (GetDC 005FDC00 no longer does since P2: see "P2 done"). Under
`uiboth` nothing reads back, so the per-surface diff compares GDI's own drawing.

**Rule from a real bug.** `DropCanvas` (HolderGone) draws the frame so far first when the canvas's blit is not drawn yet,
as the window blits' textures outlive their holders: backdrop mode 3 puts the old backdrop away (SwapContents) after the
frame blitted it and then copies that frame, which came out black (`ui_compare`'s help A/B and `ei_ui_check` hold it).

**Running the suite with the canvases.** `EI_RENDER_EXTRA=uiboth python tools/work.py test P1 <scenarios>`: the harness
adds EI_RENDER_EXTRA's words to the scenario's `render` step or to EI_RENDER (a batch keeps its EI_* variables), and a
differing surface fails the run. A native surface is also compared as it goes (`ui_native::g_goneObserver`): the status
surface lives only inside ShowStatusText's own frame. The harness's `uimirror` leaves native surfaces alone.
`ui_compare` turns the frame rate on (its canvas blitted under `uipath native`) and opens the help over the options
through each path; both paths' captures match. Result (2026-10-21): all 43 `ui_*` scenarios pass under `uiboth` with 0
differing checks (backdrop, fps, status compared in each that reaches them), and the gallery's, ui_saveload's and
ui_quicksave's 29 frames under `uinative` equal the `uiboth` ones exactly.

### P2 done (2026-10-29): text through the glyph atlas, `ui_native::Text`

**What is drawn natively.** `CUiSurface::DrawTextIn` 005FDE20 (`ui_text_draw.cpp`; `DrawLine` and `DrawWrappedText`
005FDFA0 reach it) asks `ui_native::Text` first. On a native surface (any surface `Adopt` gave a canvas, and the screen
copies) the text is laid out by `render::text::Layout` (DrawText's rules, section 2) in `TextFontOf(font)` and drawn
with `GlyphRun`: the shadow in 0x080808 at +1,+1 first, then the colour. No GDI draws it. Surfaces that are not native
draw with GDI as before; so without a `ui` word in EI_RENDER nothing changes on screen yet (P3a-d adopt the window
families, P8 makes it the default).

**The fonts: our glyphs, GDI's places** (`ui_native_fonts.cpp`). `TextFontOf(HFONT)` makes a `render::text::Font`
whose glyph shapes (coverage and box) come from our font standing in for the face (Liberation Serif for Times
New Roman, at the HFONT's -lfHeight) and whose metrics come from the GDI font: ascent, descent, average width, each
character's advance (GetTextExtentPoint32 less the overhang; control characters as DrawText spaces them) and the
kerning pairs. These are the inputs N2's `ei_text_check --gdi` fed the layout to match DrawTextA on every game string
in every font, format and width, so line breaks, alignment, DT_CALCRECT and DrawText's value are GDI's, and
`CInterface3D::TextExtent` 005F8340 (still GDI's extent) agrees with what is drawn. The layout and wrapping the game
computes are therefore unchanged in every mode; only the glyphs' shapes are ours, each drawn at GDI's pen position on
GDI's baseline. Smoothing: off for NONANTIALIASED_QUALITY (the harness's fonts) or when SPI_GETFONTSMOOTHING is off
(the credits); the atlas is grey coverage, never ClearType's colour fringes. This is the only place native text asks
GDI anything (a memory DC with its own copy of the font, metrics only, nothing drawn), and the one place to change for
metrics of our own (section 2's 2026-10-14 decision: then about one string in ten wraps elsewhere, "Liberation versus
GDI").

**The three paths** (`ui_native::TextPath`, what DrawTextIn still does):
- `Canvas` (`uinative`, and the screen copies in any mode): canvas only; DrawText's value from the layout (GDI's, see
  above). The memory lacks the text (`behind`) until a reader calls `Sync`.
- `AlsoGdi` (`uiboth`): GDI also draws the memory and its value is returned; the per-surface diff compares the two.
- `Gdi`: not native, or the format has a bit the layout does not follow (DT_SINGLELINE,
  DT_END_ELLIPSIS, ...): GDI draws and `TextDrawn` copies its rectangle onto the canvas as in P1 (the memory synced first).

**GetDC no longer reads back.** GDI text no longer lands in a native surface's memory, and nothing copies the memory
onto the canvas after GDI drew except `TextDrawn` (whose caller syncs first) and SwapContents' restore (whose save syncs
or puts the canvas aside), so GetDC 005FDC00 drops P1's `Sync`: the frame-rate surface redrawn every frame under
`uinative` costs no read-back. Rule for P3-P5: drawing a family does through the DC directly (pens, FillSolidRect)
reaches only the memory; record it on the canvas (P4, P5) or `Sync` before drawing and copy it as `TextDrawn` does.

**The comparison's text settings** (`harness_ui.h`): with GDI's placement the glyphs still differ in shape (the
frame rate's Times New Roman at 4K: 6.5% of the text pixels per pixel), so the defaults are now 8x8 block averages,
`text` 64, `textlimit` 0.05; the frame rate's checks differ in about 2.7% of their text, while missing, recoloured or
moved text still fails. `ui_compare`'s options A/B allows 0.1% of the frame (the frame rate's glyphs are 0.02%).
Result (2026-10-29): all 45 `ui_*` scenarios pass under `EI_RENDER_EXTRA=uiboth`.

**Frame differences** (2026-10-29, 3840x2160, the harness's fonts without smoothing). Under `uinative` the gallery's,
ui_saveload's, ui_quicksave's, ui_help_page's and ui_hud's frames equal the default path's exactly except where native
text is shown: the frame rate in `ui_gallery_game`'s ten frames (0.017-0.022% of each frame). An experiment that adopted
every surface (not committed: one `Adopt` in the surface's `Finish`) gave the same layout everywhere (no line wrapped
or moved: the menus' help page, options, boxes, the HUD, camp and base screens) with these shape differences:
- Our glyphs are unhinted Liberation, GDI's are hinted Times New Roman: stems and serifs differ by a pixel, ours are a
  little heavier at 4K sizes, and cap tops sit one or two rows lower (same baseline, GDI's hinting rounds cap heights
  up). Without smoothing both are 1-bit, so edges step differently.
- With smoothing on (outside the harness) the atlas is grey coverage; GDI's default quality may be ClearType's
  coloured edges. The composition over black keeps the old dark fringe (section 2).
- Not text but visible in that experiment: the titles' RichEdit (0065F020) drew nothing on its canvas (it stays CPU:
  never adopt it), and the pens' lines and carets (P5) were missing, as expected under `uinative`.

**API for P3a-d and P4** (`src/game/ui_native.h`):
- A window family goes native with `ui_native::Adopt(surface, "<family>")` where the surface is made (one line in the
  family's file); its `DrawTextIn` / `DrawLine` / `DrawWrappedText` text then follows on its own.
- Direct `dc->DrawText` users (the console: P4) call `ui_native::Text(surface, font, text, rect,
  format, colour, shadow, &height)` and draw with GDI only when it returns `Gdi` / `AlsoGdi`; any HFONT works
  (`TextFontOf` reads its face, height, weight and quality: the console's Courier New becomes Liberation Mono placed
  by Courier New's metrics; a bold or italic font would still get the regular file's shapes: the game makes none).
- Formats the layout does not follow fall back to GDI (`Gdi`); add them to `render_text.cpp` (and `ei_text_check`'s
  cases) when a family needs one. `TextFontOf(font)` also measures (`render::text::TextExtent`, `Layout` with
  DT_CALCRECT): the same numbers as GDI's extent and DrawText.
- Check a family with `EI_RENDER_EXTRA=uiboth` (the per-surface diff) and frames under `EI_RENDER_EXTRA=uinative`
  against the default path's (`tools/compare_frames.py`): only glyph shapes may differ.
- Fonts: built into the executable since P2a (`render::text::FontForFace`); there is no font file to miss.

### P3a done (2026-10-29): menus and dialogs

**Families adopted** (one `Adopt` where each surface is made, before anything draws on it):
| Family | Surface (file) |
|---|---|
| `menu` | the main menu's version line (`CMainMenu::AddVersionLine`, ui_main_menu.cpp; adopted before AddSurface, as it draws first) |
| `msgbox` | `CMessageBox::CreateScreen`'s text surface (ui_layouts.cpp): the quit box, the key-binding box, every message box |
| `difficulty` | `CDifficultyBox::CreateScreen`'s text surface (ui_layouts.cpp) |
| `options` | the options window's surface (`COptionsWindow::CreateScreen`, ui_layouts.cpp) and each spin's (`CUiSpin`, ui_spin.cpp: only the options make spins) |
| `objectives` | the objectives' three surfaces (ui_objectives.cpp's `NewSurface`) and its text column, a `CUiScrollText` adopted by the objectives right after it is made (the scroll text draws nothing before its first Draw; the HUD's message log and the briefing's text are the same class, left to P3b / P3d) |
| `credits` | the titles' header line (`CCreditsScreen::CreateScreen`, ui_layouts.cpp) |

**Still GDI, and why.** The titles' scrolling page (`CUiScrollingSurface`, RichEdit EM_FORMATRANGE): never adopt it (see "P2 done").
The message box's edit field (`CUiEditField`, flag 4): P5 (lines and carets). Tooltips (`CUiTooltip`, any window's): P3b. The help
window over the options / objectives: P3d. The escape menu has no surface of its own; its greyed backdrop and the save picture are
R28a's screen copies (GPU-only in every mode, nothing to adopt).

**Checks** (2026-10-29). `EI_RENDER_EXTRA=uiboth`: the 45 `ui_*` pass, every family above compared in the per-surface diff (the
gallery's menus: the options, spins, boxes, difficulty, titles header; ui_objectives: the three surfaces and the column), 0
differing. `ui_compare`'s options A/B now shows native glyphs in the options (0.6% of the frame; its limit is 0.8%). Under
`EI_RENDER_EXTRA=uinative` the gallery's, ui_saveload's and ui_quicksave's frames against the default path's: the same layout
everywhere (no line wrapped or moved, the objectives' justified column word for word); only glyph shapes differ, in the frames
that show these families: options 0.54%, options_keys 0.41%, difficulty 0.41%, bind_box 0.27%, quit_box 0.12%, objectives 0.51%,
the help over the objectives 0.27% (the objectives' text under it), plus the frame rate's 0.02% in `ui_gallery_game`. The menu
and titles frames are unchanged (the version string is empty in the harness's install; the titles header is blank at that
moment).

### P4 done (2026-10-29): the console

**Native now.** The console window's surface (`ui_console_window.cpp`; family `console`) is adopted where `Show` makes it.
Its fill (`FillArea`: GDI's `FillSolidRect` and `ui_native::Fill`), its
text (`DrawString`: `ui_native::Text`; the lines and "+" marks in 0xB4B4B4, the typed line in white) and the 2-pixel caret (`ui_native::Line`, new in P4: `render::ui::Line`) are
recorded on the canvas. Layout, colours and scrolling are untouched; the caret and selection positions still come from GDI's
extent on the surface's memory DC (`PromptWidth`), and the text is placed by GDI's metrics as in P2 (Courier New's metrics,
Liberation Mono's glyph shapes).

**Still GDI.** The memory DC: the extent for the caret, the fills and the pen are still drawn into the memory too (it stays the
source of truth; under `uinative` the text is not in it until `Sync`), and `Text`'s fall-back (a format the
layout does not follow) draws with GDI and copies the rectangle onto the canvas (`TextDrawn`).

**Checked.** `console`, `utf8_typing` and `ui_gallery_game` under `EI_RENDER_EXTRA=uiboth`: all pass, the console compared 3
times in the gallery (0 differing). Under `uinative` the
gallery's console frame (3840x2160) differs from the default path's in 4712 pixels, all glyph edges inside the console (the
lines, the typed line); the fill, the selection and the caret are identical.

### P3b done (2026-10-29): the HUD

**Families adopted** (one `Adopt` where each surface is made, before anything draws on it):
| Family | Surface (file) |
|---|---|
| `unitpanel` | the unit panel's surface (`CHudUnitPanel`'s setup, ui_hud_slide.cpp): the name (006462A0's `DrawWrappedText`), health and mana numbers, the info page's lines, the effects' names and times; redrawn every frame |
| `messagelog` | the battle screen's message log, a `CUiScrollText` adopted by `CBattleScreen` right after it is made (ui_battle_setup.cpp; the scroll text draws nothing before its first Draw); the top panel's pages are this log's text |
| `tooltip` | every `CUiTooltip`'s surface (ui_tooltip.cpp): the HUD's, and the menus', hero list's and global map's tips too (one class); Fill, FrameRect and text all recorded already |
| `hudmap` | the HUD map's dot (`CHudMap`'s setup, ui_hud_slide.cpp): its one SetPixel also recorded (`ui_native::Pixel`, new: `render::ui::Pixel`) |

The clock, the quick items, the top panel's frame, the movement panel and the other HUD panels have no surfaces (sprites only):
nothing to adopt. The camp's and hero screen's quick strip is P3c's (ui_panel_draws.cpp).

**Still GDI.** Nothing in these families: they draw only through DrawTextIn, Clear / Fill, FrameRect and the dot's SetPixel, all
recorded. The checks below are vanilla standalone runs; Rebirth launches were not run with the canvases.

**Checks** (2026-10-29). `EI_RENDER_EXTRA=uiboth`: the 45 `ui_*` pass, 0 differing (ui_hud: 230 checks of 11 surfaces, the unit
panel 156 of them). `ui_compare`'s options A/B shows a native tooltip over the options now (1.1% of the 1280x720 frame, 0.82% of 3840x2160's; limit 1.5%). Under
`EI_RENDER_EXTRA=uinative` the gallery's, ui_saveload's and ui_quicksave's frames against the default path's: the same layout
(no line wrapped or moved: the tooltip's four wrapped lines, the log's justified line), only glyph shapes differ: the unit
panel's name and numbers in every HUD frame (about 0.02%), the message log's quest line (quest_notice, 0.08% more), the HUD tip
0.18%, the hero list's tip 0.03%; the map dot is identical. The menus' help_tip frame shows no tooltip at its capture (the same
frame as help), so the menus' tips are covered by ui_compare only.

### P3c done (2026-10-29): camp, hero, trade and item info

**Families adopted** (one `Adopt` where each surface is made, before anything draws on it):
| Family | Surfaces (file) |
|---|---|
| `camp` | the camp's two text surfaces (`CCampPages`, ui_camp_create.cpp's `NewSurface`) and the spell maker's (`CSpellMakerPage::BuildSpellMaker`, ui_hero_panels.cpp) |
| `hero` | the hero screen's four (`ui_hero_screen_create.cpp`'s `NewSurface`: statistics, name, commands, rename) and the statistics column (`CHeroStatsPanel::Build`) |
| `trade` | the trade panel (`CTradePanel::Build`) and the trade / repair grids (`BuildCellGrid`) |
| `iteminfo` | the item description (`CItemInfoPanel::Build`) |
| `itemstrip` | the item strips' count and number surfaces (`CUiItemStrip::Setup`) |
| `exchange` | the player exchange window's four (`ui_exchange.cpp`) |

`camp` and `hero` go through `ui_native::AdoptUnlessWidescreen`: not adopted while Rebirth's WideScreen layout is active (vanilla only
for now; the other families have no wide layout of their own). None of these holds a `CUiScrollText`, so nothing is adopted after
construction here.

**Still GDI, and why.** The hero screen's and the exchange window's edit fields (`CUiEditField`, their own surfaces): P5 (carets).
Everything the families' windows draw through the surface functions (Clear, DrawLine, DrawWrappedText, DrawTextIn) is recorded; none
of these files uses a pen, FillSolidRect or SetPixel directly.

**Checked** (2026-10-29). `EI_RENDER_EXTRA=uiboth`: the 45 `ui_*` scenarios and `net_pair_zone` pass with 0 differing surfaces
(the exchange's four compared 469 times in `net_pair_zone`). Under load `net_pair_zone` runs out of its frame-wait time under
`uiboth` with all the families (the comparison costs about 35 ms of CPU a frame there: 37 s against 8 s; `uinative` and each family
listed alone pass); it is the harness comparison's cost, not the native path's. `--golden`: 121 the same, 24 without a golden.
Frames under `uinative` against the default path's (3840x2160, `ui_gallery_camp`, `ui_gallery_deals`, `ui_gallery_mp`,
`ui_gallery_base`): the same layout (no line wrapped or moved; checked by eye in the trade panel) and only glyph shapes differ: the
camp's pages 0.74-1.51% of the frame (statistics 0.74, items 1.19, spells 1.11, trade 1.51, trade page 4 1.13, level-up 1.38,
makers 1.13), the deals 0.82-1.46%, the hero list's tooltip 0.23% (the tooltip itself is P3b's, so this is the hero list under it),
the map's titles 0.24%, the hero statistics page 1.71% (`ui_gallery_mp`); the map, the connection and base screens, the chat and
the briefing are unchanged.

### P3d done (2026-10-29): briefing, global map titles, help page and chat

**Families adopted** (one `Adopt` where each surface is made, before anything draws on it):
| Family | Surfaces (file) |
|---|---|
| `briefing` | the base screen's topic rows and person's label (`CBriefingScreen`'s setup, ui_briefing_create.cpp) and its text, a `CUiScrollText` adopted right after it is made |
| `globalmap` | the zone tip with its quests (surface 0) and the allod's title and clock (ui_global_map.cpp); not the pick picture `backdrop_` (P6) |
| `help` | the help page's header, body and footer (`NewSurface`, ui_help_page.cpp): over the options, the objectives and the zone tutorials |
| `chat` | the player list (`CChatWindow`, ui_chat_window.cpp) and the chat overlay's line surfaces (ui_chat_overlay.cpp; the typed line is P5's) |

The global map's titles are drawn with DT_SINGLELINE | DT_VCENTER | DT_CENTER (0x25), which the layout did not follow (they fell
back to GDI). `render::text::Layout` now takes DT_SINGLELINE (section "DrawText layout rules"); `ei_text_check --gdi` holds it
(two more formats, four heights for a single line). Help and global map are adopted under Rebirth's WideScreen too (W4 is
done); `ui_widescreen*` pass under `uiboth`.

**ui_compare.** No surface is mirrored any more (the help page under the options is native), so `uifault` falls back to a native
canvas and writes its memory back onto it once the check has found the square (tests/verify/image/harness_ui_image.cpp;
tests/verify/harness_ui.h's line still says "mirrored": fix it when the harness next changes). The help A/B allows 3% of the
frame (the help page's native glyphs are 2%).

**Still GDI.** Nothing in these families' own drawing (DrawTextIn, Clear, DrawWrappedText only).

### P5 done (2026-10-29): edit, chat, connect and save / load lines and carets

**Families adopted**, each pen line also recorded on the canvas (`ui_native::Line`, after the game's scaling, the 1-pixel pen's
rule: end point left out):
| Family | Surface (file) | Line |
|---|---|---|
| `edit` | every `CUiEditField`'s (ui_edit_field.cpp): the message box's, new hero's, hero screen's, exchange's and connection screen's fields | the caret in `caretColour_` |
| `chat` | the chat overlay's typed line (ui_chat_overlay.cpp) | the caret in white |
| `connect` | the connection screen's game list and its "connecting" note (`NewSurface`, ui_connect_screen.cpp) | the line under the column titles (ui_connect_list.cpp) |
| `saveload` | the save / load window's list (`CSaveLoadWindow::CreateScreen`, ui_layouts.cpp) | the save name's caret (ui_saveload_list.cpp) |

The edit field has no selection (only the console has one, P4). **Still GDI:** the pens still draw into the memory too (it stays
the source of truth until P8), and the connect list's Rebirth names (`DrawEllipsis`, DT_END_ELLIPSIS) take `Text`'s GDI fall-back.

**Checks for P3d and P5** (2026-10-29). `EI_RENDER_EXTRA=uiboth`: the 45 `ui_*` pass with 0 differing surfaces (ui_describe
needs a lightly loaded machine: the comparison costs about 100 ms a frame in the camp, 47 s of its 60). `--golden`: 121 the same,
24 without a golden. Frames under `uinative` against the default path's (3840x2160): the same layout everywhere, carets and the
connect list's line identical, only glyph shapes differ: help over the options 2.02%, over the objectives 1.84%, the hero list
0.23% and new hero 0.30% (edit field), connect 0.28%, briefing 0.01%, chat typed 0.02% and sent 0.03%, base topics 0.10%,
phrase 0.09%, rewards 0.45%, save 0.13% / 0.22%, load 0.16-0.21%, the escape menu 0.10%, the global map 0.04% (the title and clock in
the same places), its zone tip 0.08% and ways 0.30%.

### P2a done (2026-10-29): the fonts inside the executable

The two Liberation files are byte arrays built into the executable (ei_render, so ei_hook.dll and the tools): CMakeLists.txt runs
`src/render/embed.cmake` on each at build time into fonts/ in the build folder (not committed, no Windows resource, so a port
keeps it); `render_fonts.cpp` includes them and `render::text::FontForFace(face)` gives the bytes (Liberation Mono for Courier New,
Liberation Serif else). `LoadFont(data, pixels)` reads memory. EI_FONT_DIR, FontDirectory / SetFontDirectory, text_check's
`--fonts` and the "font file missing" fall-back to GDI are gone (`Text` still falls back for a format the layout does not follow).
ei_hook.dll grows by 712,704 bytes (7,173,632 to 7,886,336, release); ei_standalone.exe is unchanged.

**Notices.** Every build writes `third_party_notices.txt` beside the executables (configure time, from third_party's README and
LICENSE files: the fonts' SIL Open Font License, sokol's zlib licence, stb's note); play.cmd copies it into run\play; README's
License section says so.

### P8 done (2026-10-29): the canvas is the only path

**One path** (user 2026-10-29). Every window surface gets its canvas when it is made (`CUiSurface::MakeCanvas`) and
the canvas is the surface's pixels: no DIB section, no memory for a window, no memcmp or upload at a blit, no families
to adopt, no `uinative` / `uiboth` words (an EI_RENDER word that is no longer known is logged and ignored). The
surface functions draw only on the canvas: `Clear` / `Fill` / `FillRect` / `FrameRect`, `Line` (the edit field's,
the chat's, the save list's and the console's carets, the connect list's rule: the pen's rule, end point left out),
`Dot` (the minimap marker), `DrawTextIn` / `DrawTextPixels` (glyph runs), `CaptureScreen`, `Greyscale`, `Image`. The
readers read the GPU: `PixelAt` (`ReadPixel`), `Rgb565Copy` (`ReadRgb565`); `SwapContents` moves the canvas aside
(`MoveCanvas`; a surface brought back without one gets a new black canvas). P6's parts were on the GPU already
(R28a: the pick picture, the camp's backdrop modes 3 / 4 through the screen copy, greying and `MoveCanvas`); they
now call render::ui directly.

**Lost device.** With no memory behind it a canvas must come back by itself: each canvas keeps the commands drawn on
it since a command last covered it whole (a whole fill or picture starts the list again), and `DropCanvases` queues
them again, after a Make, for the new device. A screen copy cannot be drawn again (its frame is gone), so once one is
drawn the canvas's pixels are read back as its history (one whole picture); so is a history past 256 commands.
`ei_ui_check`'s "lost" holds it (fills, frames, lines, pixels, a picture, a glyph run, a moved canvas, a copy greyed,
300 pixels); render_device_lost compares the frames before and after a loss.

**Text in our own fonts** (user 2026-10-29). `UiFont` (ui_font.h: a face at an em height in pixels) replaces the
HFONTs: `PointFont(tenths, face)` is CreatePointFont's size at 96 dots an inch (the interface's sizes are
width-relative, so the DPI cancels but for rounding), `TextFontOf` the atlas font (smoothed unless
SPI_GETFONTSMOOTHING says off), `TextExtent` GetTextExtentPoint32's rule on our advances. The interface's text extent
005F8340 (word wrap, the scroll text, tooltips, the chat, the message box) and the console's prompt width use it, so
wrapping and drawing use one set of metrics (`GdiPlacedFont`, which placed our glyphs by GDI's advances, is gone).
The layout follows every format the game uses: DT_END_ELLIPSIS is new (`kEndEllipsis`: the single line cut until it
and "..." fit; the connect list's Rebirth names), DT_MODIFYSTRING is dropped (the game never reads the shortened
text back); any other bit is logged once and left out. `ei_text_check --gdi` has the two ellipsis formats.

**Harness.** The comparison (`harness_ui`, the `uipath` / `uimask` / `uidiff` / `uidiffcheck` / `uimirror` /
`uifault` steps) and the census (`ui_census`, `uicensus`, `tools/ui_census_report.py`) are gone with the GDI path
they measured; so are the "MISSED WRITE" check and the window-surface upload counts in `renderstats`. The harness
reports font smoothing off to the image (SPI_GETFONTSMOOTHING), as it makes game.exe's fonts without smoothing.
`ui_compare` now opens the options and the help over them twice and requires the same frames (mode 3's backdrop
put away and copied again). P3e's comment (tests/verify/harness_ui.h) went with the file.

**Deleted.** ui_native.{h,cpp}, ui_native_fonts.cpp, ui_gdi.h, ui_surface_dc.cpp (GetDC / ReleaseDC 005FDC00 /
005FDC60 and every caller's bracket), lib/gdi.{h,cpp}, the surface memory's DIB section, change marks and window
textures (render_textures.cpp: `WindowSurfaceView`, `Lookup16`, `SurfaceWritten`, `MissedSurfaceWrites`), render_ui's
switch, comparison views and read-back into memory, system_text's DrawTextA / GetTextExtentPoint32A /
CreateFontIndirectA routes.

**GDI left.** None in the interface since P8a (the titles' text: next section). Outside it: the tools' references
(`ei_ui_check` draws GDI's fills and lines to compare, `ei_text_check` GDI's DrawTextA and RichEdit), and gameapi.h's
generated import declarations.

**Checks.** `--golden`: 145 runs pass, 121 goldens the same, 24 without a golden; `ei_ui_check` 102 checks. Frames
against main's default (GDI) path, 1280x720 unless said: glyph shapes everywhere (the camp's pages 1.8-3.4% of the
frame, the deals 2.0-3.2%, the base 0.5-2.6%, menus 0.2-4.3%, 4K widescreen 0.05-2.2%); line breaks moved only in
three places: the objectives' help page (its first paragraph breaks one word earlier, so the page and its window are
one line taller: 14.5% of the frame), the objectives' zone description (centred, "Однако," to the next line) and the
briefing's scroll text (a word back onto the line before; as many lines); the camp's item info, the
trade lists, the help over the options and the tooltips break as before. The scroll text's lines are one pixel
closer at 1280x720 (GDI's hinted Times New Roman is taller, section 2). At 4K the HUD tooltip at the bottom row
(`ui_widescreen_more` tooltip2, 13.5%) now opens above the mouse: the height it is placed by is the height it is drawn
at, and it no longer runs off the bottom of the screen.

### The titles' rich text (P8a, 2026-10-29)

**What draws it.** The titles' scrolling surface (`CUiScrollingSurface`, ui_scrolling_surface.cpp; 0065ED30-0065F1C0)
no longer loads Riched20.dll or makes a RichEdit control, and has no DIB page or memory DC. `LoadRtf` reads the RTF
block with `render::text::ReadRtf` and lays it out once at the surface's width with `LayoutRichText`
(src/render/render_rich_text.{h,cpp}, no Windows calls); `RenderText` fills the canvas white (the key) and draws the
lines the surface shows as glyph runs (`RichDraws`: one `GlyphRun` per font and colour), its top `offset_` twips
above the surface's. The speed, the offsets in twips and the end are the original's: the whole text fitting above
1000 twips below the surface keeps the offset, and 1000 twips plus the height past it stop the scroll; "fits" is
the text's pixel bottom at most the page bottom's (`TwipsToPixels`, MulDiv's rounding), which is where RichEdit's
EM_FORMATRANGE said all characters fitted.

**The RTF subset** (outro.rtf, 28,101 bytes, written by Word 2000; the same in vanilla's and Rebirth's outro.res):
the font table (with \fcharset: 204 decodes \'xx as Windows-1251, 0 as 1252), the colour table, \pard / \plain,
\par, \line, \tab, \b, \i, \fs, \f, \cf, \ql / \qc / \qr (\qj as left), \li / \fi / \ri, \tx / \tqr, \u / \uc
and the escaped characters; \stylesheet, \info and every \* group are skipped, other words left out. Formats
are kept per group. The file uses \qc, \fi / \li hanging indents (4245 and 4320 twips; one -284), \fs 20-32, \b,
\cf 2 / 16 and no tabs.

**RichEdit's layout, reproduced.** Fonts at MulDiv(\fs, dpi, 144) pixels in the face's stand-in (Liberation Serif);
advances without kerning; a line breaks after its last blank that fits (a word wider than the line inside it);
trailing blanks are left out of the centring, x = left + (room - width) / 2; indents and tab stops in twips at the
DPI. A line is the tallest cell ascent plus the deepest cell descent of its characters, the paragraph mark's font
counted on its last line (an empty paragraph is its mark's height). The cell ascent and descent are the font's
winAscent / winDescent scaled and rounded **up** (`FontMetrics::cellAscent` / `cellDescent`), which is what Windows'
Times New Roman reports: with the interface's rounded ascent the 16-point lines came out 47 px instead of 49 at 192
DPI. Bold has no face of its own: `BoldFont` widens each glyph by max(1, (pixels + 10) / 20) pixels and its advance by
as much (GDI's emboldening); italic is drawn upright (the file's text has none). The text is drawn without
smoothing, as RichEdit drew it with the titles' font smoothing off.

**DPI.** Twips and points go to pixels at the system DPI (`GetDpiForSystem`; the process is system DPI aware), the
number GetDeviceCaps gave RichEdit's memory DC: 192 on a 200% display, so the text is as big as before on every
machine (the interface fonts are width-relative; these are not).

**Checks.** `ei_text_check --titles` (tools/text_check/titles_check.cpp) streams the file into a real RichEdit and
measures each line's bottom by EM_FORMATRANGE (the least page height that fits one more line; RichEdit puts the first
line on any page, so that one is not measured) and the height that fits the whole text, against ours: 0 of 285 line
bottoms differ and the fit height is equal (77,142 twips at 192 DPI, 81,368 at 96 with `--dpi-unaware`) at widths
1248 (1280x720), 780 (800x600), 3744 (4K) and 600 (295 lines: wrapped hanging indents); `--picture` writes RichEdit's
page beside ours. Scenario `ui_titles_scroll` scrolls the titles to their end with captures at 2, 30, 90 and 150 s of
game time: the lines are in the same places before and after, and the main menu comes back at frame 665 in both.
The frame's median time there fell from 4.7 ms to 0.24 ms (RichEdit formatted and copied a whole page every frame).
Look: our glyphs are Liberation's (bold emboldened), RichEdit's were Times New Roman's; in the harness the old frames
also had ClearType fringes, as RichEdit asked the system's smoothing rather than the harness's answer.

## 4. Tickets

The tickets (N0-N5 preparation, P1-P8 porting) are in docs/tickets.toml (`python tools/tickets.py show N1`); this note keeps what
they depend on.
- **N0** (done): instrumentation; results in ui-census.md (no uncaught drawing; addon.dll draws only through game functions;
  SetPixel is the 24x18 HUD map marker, SwapContents runs on every window show and hide, tooltips are not every frame, the version
  line draws before AddSurface). Lesson from its multiplayer probes: every game hosts on UDP 8888 (00783E30) and ignores a failed
  bind (00412D80), so a harness game run beside another hosting game joined that game's server and lost it when that game quit (a
  "disconnect" box in ui_mp_host); the harness now gives each game a free port of its own (`tests/verify/harness_net.{h,cpp}`).
  The census's call chains keep a scanned stack address only when its call leads into the function holding the frame before it
  (method in ui-census.md "How it was measured").
- **N1** the 2D canvas layer (done, section 3 "N1 done"; `src/render/render_ui.{h,cpp}`, checked offline by `ei_ui_check`). **N2** glyph atlas and DrawText layout (done, section 2).
  **N3** comparison harness (done, section 3 "N3 done"; `uinative` / `uiboth` / `uipath`, the per-surface diff (a), UI mask
  capture, `compare_frames.py --mask-file`; `tests/verify/harness_ui.cpp`). **N4** the `ui_gallery_*` scenarios (done, every
  window family captured frozen; section 3 "N4: the gallery"). **N5** cursor and per-frame images cached (`render_blit.cpp`).
- **P1** (done, section 3 "P1 done") surface plumbing (canvas per native surface, blit, Get/ReleaseDC as the metrics DC, Fill / Clear / FrameRect, readback for
  PixelAt / Rgb565Copy / SwapContents, CaptureScreen as a GPU copy, Greyscale; first native surfaces: backdrop, FPS, status; needs
  N1, N3). **P2** (done, section 3 "P2 done") text (005FDE20 records glyph runs on native surfaces: our glyphs placed by GDI's metrics; 005FDFA0 unchanged; needs N2, P1).
  **P3a-d** (done) windows by family (menus and dialogs; HUD; camp, hero, trade and item info; briefing, global map titles, help page, chat
  window), mostly native-table entries plus fixes the diff finds; **P4** (done, section 3 "P4 done") console (direct FillSolidRect / DrawText,
  own fonts, the 2-pixel pen caret); **P5** (done, section 3 "P5 done") controls with lines and carets (edit field 0065C2E0, chat typed line 005E3700, connect
  list 0061D780, save/load list 006567A0); **P6** the global-map pick picture (`RefreshSelection` 00630670: frame step, GPU copy,
  readback on click; colours exact, point copy, no dithering) and the camp's backdrop modes 3/4; **P7** (optional) CPU-written
  textures as GPU sub-image uploads, clouds 00499F10 as a shader (done by R28 / R28b); **P8** (done, section 3 "P8 done") the
  canvas the only path: no DIB, memcmp, GDI text or metrics, switch words or comparison; **P8a** (done, section 3 "The titles' rich text") the
  titles' text laid out by us instead of RichEdit.
- After P2, P3a-d and P4 touch disjoint files; each one's only shared edit is a line in `ui_native.cpp`. W3 / W4 only decide when
  Rebirth launches can go native; vanilla is not blocked. Porting tickets add no fields (side maps) and leave `ui.h` alone.

## 5. Risks and open questions

**Things the game reads back from pixels:**
- The global-map pick (005FD240 on a frame drawn by `FrameStepOriginal`; zone = (pixel>>4)&0xF − 1) must stay exact. The read goes canvas → DIB in the surface's format, then the original PixelAt.
- The escape-menu and QuickSave pictures (005FD440, written into shot.sav) only need to be within tolerance.
- The verify wrappers that compare surface pixels need `uiboth`.
- In-game screenshots (006D6D40) in the switch-over come from render::ReadFrame (R9c3, renderer.md).

**GDI-dependent layout.** Extent and DrawText return values must stay GDI (the metrics DC). The layout library must match GDI's line breaks exactly, or text lands on different lines; N2's corpus test is the gate. DT_VCENTER without SINGLELINE and the '&' prefix in formats without DT_NOPREFIX are easy to get wrong.

**Font smoothing.**
- ClearType or greyscale depends on the system, and the credits turn smoothing off at runtime (keyed in the atlas).
- 16-bit mode quantises colours and the key test: 0x080808 survives, but near-black text colours may become transparent in the CPU path and must do so on the GPU too.

**Rebirth.**
- Patched draws ran addon's GDI code until W2–W4 ported them (005FDE20, 005F8340, 005E3700, 0065C2E0, 006567A0, 00659420, 0065E580, 006462A0, the backdrop in 005F5320 and the creation 005F4990 are ours in a Rebirth launch now). The connect list 0061D780 (W2a), the connect screen's Create 0061B2C0 (W2b) and the hero screen's Create 00613EA0 (W4a) are ours in a Rebirth launch too; the fallback is automatic only if N0 or the diff catches drawing we have not ported.
- `present` under Rebirth still crashes (R9c), so native UI is validated on vanilla and `--native` first.
- EI_Plugin's fixes (006407E0, 005ED7B0) keep original code there.

**Other risks.**
- **Performance.** Today every blit memcmps the whole DIB (1.9 MB for an 800x600 32-bit backdrop) and there is a full-frame readback per frame in `present`. Native removes the memcmp and upload. What remains on the CPU: GDI metrics calls; the global-map clouds and cursor composite, unless P7 / N5 handle them; the per-frame readback, which is R9's. Canvas updates after a blit in the same frame split the main pass; hoisting covers the common case.
- **Not ported (keep CPU and upload):** Bink decode.
- **Open questions:**
  - Should the default later move to direct-to-frame glyph drawing, which looks cleaner but loses the fringe and needs a looser tolerance?
  - Should a resolution-independent UI scale (4K) be done? It needs re-rasterised atlases and changes metrics, so it is out of scope for behaviour-exact work.

### Critical files for implementation
- E:\workspace\ei\src\render\render_blit.cpp (and new render_ui.cpp / render_text.cpp beside it; render.cpp `DrawPending`, render_internal.h `Command`)
- E:\workspace\ei\src\render\render_textures.cpp (`WindowSurfaceView`, surface memory)
- E:\workspace\ei\src\game\ui_text_draw.cpp (005FDE20 / 005F8340; W2 conflict) and E:\workspace\ei\src\game\ui_text.cpp (005FDFA0)
- E:\workspace\ei\src\game\ui_surface_blit.cpp, E:\workspace\ei\src\game\ui_surface_dc.cpp, E:\workspace\ei\src\game\ui_surface_frame.cpp, E:\workspace\ei\src\game\ui_surface_pixels.cpp, E:\workspace\ei\src\game\ui_surface_create.cpp
- E:\workspace\ei\src\game\ui_interface_draw.cpp (frame order, backdrop / greyscale / save picture), E:\workspace\ei\src\game\ui_interface_create.cpp (fonts), E:\workspace\ei\src\game\ui_global_map_places.cpp (pick picture)

## Overview of the surface canvases

Every interface window surface's pixels are its holder's canvas on the GPU (P8): made black with the surface
(`CUiSurface::MakeCanvas`, ui_surface_create.cpp; again in the surface format after SwapContents brings a surface
back without one), drawn into by the surface functions (`Clear` / `Fill` / `FillRect` / `FrameRect`, `Line` for the
carets and the connect list's rule, `Dot` for the minimap marker, `DrawTextIn` / `DrawTextPixels` as glyph runs,
`CaptureScreen`, `Greyscale`; the titles' text as glyph runs too) and blitted by `CUiSurface::Blit` (`DrawCanvas`). The readers
read the GPU: `PixelAt` (`ReadPixel`, packed back to the screen format), `Rgb565Copy` (`ReadRgb565`); `SwapContents`
moves the canvas aside under the surface's address and back (`MoveCanvas`). Text is laid out, measured and drawn in our
own fonts (ui_font.h). A lost device makes every canvas again and draws on it the commands drawn since it was last
covered whole (render_ui.cpp's history; after a screen copy or 256 commands the canvas's pixels read back instead), so
nothing comes back black (docs/notes/renderer.md section 6, "Screen copies on the GPU", and R9c1).

## DrawText layout rules (render_text.cpp)

The rules (measured against user32's DrawTextA on Windows 10 with fixed-pitch fonts; the synthetic
cases in tools/text_check check each of them):
- Lines end at CR or LF; CR LF and LF CR count as one break. A break at the very end still makes an
  (empty) last line; an empty text draws nothing and returns 1.
- With DT_WORDBREAK the text is cut into words: each space or tab is a word of its own, any other
  run of characters up to a space, tab, CR or LF is one word. Words are added while the width from
  the line start to the word's end is at most the rectangle's width. The first word that does not
  fit ends the line before it; when it is the line's first word, the line is that word alone
  (clipped, never broken inside).
  - Left and centred text then skip one space or tab at the start of the next line; right-aligned
    text does not (so a right-aligned wrap can leave a line holding only spaces).
  - Centred and right-aligned lines that end at such a wrap lose one trailing space or tab, for
    drawing and for DT_CALCRECT alike. Left-aligned lines keep it (DT_CALCRECT counts it).
- Widths: advances summed plus the font's pair kerning (DrawText on Windows 10 goes through
  Uniscribe, which kerns; GetTextExtentPoint32, and so TextExtent, does not). There is no kerning
  pair across a tab, nor between a Latin and a Cyrillic run (a space after "в" is Cyrillic, so
  "в W" does not kern the space with the W). '&' (unless DT_NOPREFIX) is dropped and makes the next character the prefix
  character; "&&" draws one '&' and a trailing '&' draws nothing.
- Tabs: with DT_EXPANDTABS a tab moves to the next multiple of 8 average widths from the line's
  start; without it a tab has no width and no ink.
- x: left, right - width, or left + (rect width - width) / 2 rounded down. Lines are the font's
  height apart from the rectangle's top; DT_VCENTER without DT_SINGLELINE changes nothing.
- DT_SINGLELINE: the whole text is one line (CR and LF are characters, no word breaks), one line high also when the text
  is empty; with DT_VCENTER its top is the rectangle's top plus half the room left (height less the line's), rounded
  towards zero, also when the room is negative. DT_VCENTER's return value and DT_CALCRECT's bottom count from the
  rectangle's top to the line's bottom.
- DT_END_ELLIPSIS (P8): a line wider than the rectangle loses characters from its end until its width plus "..."'s
  (three dots' advances and their kerning) is at most the rectangle's; the dots are drawn after it, kerned with the
  last character kept (DrawText draws the shortened text whole, but leaves that pair out of the fit). `ei_text_check
  --gdi` font 0: every layout and every game string's pixels the same as DrawText's.
- Drawing stops after the first line whose bottom is below the rectangle (unless DT_NOCLIP); the
  return value is the height of the lines drawn (one line more when it stopped early and the
  text ends with a line break). DT_CALCRECT lays out every line, returns their height and sets right = left +
  widest line, bottom = top + height; with DT_WORDBREAK and a widest line wider than the
  rectangle it lays the text out again at that width.
- The last prefix character of each tab segment is underlined: one pixel at ascent + 1 below the
  line's top, from the character's position to the next character's (its advance at the end).
  A prefixed expanded tab is underlined from the pen to its stop one pixel higher; a prefixed
  tab that is not expanded takes the segment's underline away.
- Not modelled (synthetic cases only): a CR or LF inside a single line at large sizes (4K font 2) moves what follows by
  a pixel or two; the game's single lines hold none.
- Not modelled (synthetic cases only; the game's texts have no '&'): a wrapped first word that
  holds a prefix and ends in '&' sometimes keeps the following space on its line in DrawText.

## Source file overviews

Moved from the files' headers.

### render_ui.cpp

The interface's 2D canvases (render_ui.h; ticket N1, docs/notes/ui-native.md section 3). A canvas is an RGBA8
render target holding a surface's pixels widened to 8 bits a channel. Its commands queue in call order (g_queue)
and RunCanvasCommands draws them, outside any pass: render.cpp calls it at the start of DrawPending, so a canvas's
drawing lands before the frame's next pass, and RunCanvasesNow for a read-back. Solid shapes are quads of a colour
quantised on the CPU; a glyph run is three steps (its coverage into an R8 scratch target with MAX blending, a copy
of what the canvas held there, the composition of the two into the canvas); greying works on a copy of the canvas;
the screen copy samples the frame target. Each command drawn joins its canvas's history (cleared when a command covers
the canvas whole); a screen copy or a history past 256 commands is replaced by the canvas's pixels read back
(Compact), and DropCanvases queues every canvas's history again for the new device. Every pixel the shaders write is quantised to the surface format, so a
canvas never holds a colour its surface could not. render.cpp's part: Command::Canvas (DrawCanvasBatch inside the
frame's passes), SplitFrame (the frame so far drawn before a canvas already blitted in it changes, or before a
screen copy) and RunCanvasesNow. The offline check tools/ui_check plays that part itself.

### render_ui.h

The interface's 2D canvases (docs/notes/ui-native.md, section 3 and "P8 done"): a window surface's pixels, kept only
on the GPU in a render target, drawn into by the commands GDI drew with (fills, frames, lines, pixels, glyph runs) and
by the surface pixel code (the screen copy, greying, a picture), read by the readers (one pixel, all of them, the
5:6:5 picture), and blitted onto the frame by the surface's blit. No GDI here. A canvas belongs to a surface holder (the `Surface` of
a CUiSurface) and holds what the surface's memory would: every channel cut to the format's bits and widened again
(render_textures.cpp's Expand), so 16-bit surfaces quantise as GDI does and the colour key compares exactly.
Commands are queued in call order and drawn on the GPU before the frame's next pass (drawing before a canvas's
first blit of the frame lands before that blit). Drawing into a canvas already blitted in the frame and not drawn
yet first draws the frame so far (a pass split), so the blit shows the canvas as it was. Single-threaded: the
thread that draws the interface.

### render_text.h

Interface text without GDI (docs/notes/ui-native.md, section 2; ticket N2): TrueType fonts built into the
executable (stb_truetype), a glyph atlas per font and pixel size, and a layout that follows user32's DrawTextA for the formats
the game uses. The same layout gives the text extent (GetTextExtentPoint32A), so wrapping and drawing use one set
of metrics. Nothing here is pixel-exact with GDI: the fonts are metric-compatible stand-ins (Liberation Serif for
Times New Roman, Liberation Mono for Courier New) rasterised without hinting. What follows DrawText exactly are the
rules: where lines break, how they are aligned, tabs, the '&' prefix, clipping and the return value / DT_CALCRECT
rectangle, given the font's advances. Output: glyph quads (atlas rectangle -> destination rectangle) plus underline
rectangles, for the GPU canvas (N1) to draw, and a CPU composition "against black" for tests.
