# Native interface drawing: analysis and plan (2026-10-14)

Plan for drawing the interface's window surfaces on the GPU renderer instead of GDI into DIB sections (tickets N0-N5, P1-P8 in docs/tickets.md). Written before the work; update the status lines as tickets land.


**Summary.** The interface's art is already drawn on the GPU: sprites, minimap, 3D previews, rectangles, fades and hit points all reach `render::BeginMesh`, `ScreenDraw` or `TlDraw`. What is still CPU-only is the window surfaces (`CUiSurface`, 0x38 bytes). These are text, fills, frames, carets and lines drawn with GDI into a DIB, and today that DIB is re-uploaded (memcmp, convert, upload) whenever it changes.

The proposal is to keep each surface as a persistent GPU canvas, a render target owned by `src/render`. Drawing calls are recorded in surface pixels at the GDI boundary and replayed on the GPU. GDI stays only for metrics, so layout stays identical. The work is about 4 preparation tickets that can start now, then 7–8 porting tickets.

## 1. Inventory: how the interface produces pixels

The frame order is this. `CInterface3D::Draw` 005F5220 (inside BeginScene..EndScene) draws the backdrop blit, then the top window's slot 20 (sprites and models), then the fade. After EndScene, `DrawOverlays` 005F5320 runs the top window's slot 22 (`RedrawBase` 005FECC0, which blits every shown surface through 005FDA80; containers recurse in 00601040), then slot 23 (tooltips 005FED00), then the status surface, FPS surface, console, the debug panel and the backdrop captures. The cursor 005E54A0 comes last. So every window surface is drawn above every mesh, in stack and child order.

| Primitive | Functions (file) | Users / call sites | Path today |
|---|---|---|---|
| Surface blit (plain, colour-keyed, stretched with flag 2) | 005FDA80 `ui_surface_blit.cpp` → `render::SurfaceBlit` → `render_textures.cpp WindowSurfaceView` (memcmp of the DIB at every blit, then convert and upload) | every surface: about 45 `AddSurface` sites in 22 files, plus the console, debug panel (`string_map.cpp`), FPS / status surfaces and backdrops | GPU blit of a CPU texture |
| Text | 005FDE20 `CUiSurface::DrawTextIn` (`ui_text_draw.cpp`: GDI DrawText through the MFC CDC slot 26; with a shadow, first in 0x080808 at +1,+1); 005FDFA0 `DrawWrappedText` (`ui_text.cpp`) | about 71 `SurfaceDrawText` / `DrawWrappedText` sites in 35 files (most: saveload_list 7, exchange 7, hero_screen 6, help_page 6, stats_rows 4); 26 more through `SurfaceDraw::Text` in `ui_item_info.cpp`; direct `dc->DrawText` in the console (4) and debug panel (4, own font) | CPU (GDI) |
| Fill / Clear / FrameRect | 005FE370, 005FE2F0, 005FDCB0 (`ui_surface_frame.cpp`); `CDC::FillSolidRect` 0070BADA / 0070BB04 | about 50 Clear lines in 27 files; Fill and FrameRect only in the tooltip (`ui_tooltip.cpp:131`); FillSolidRect in the console (3) and debug panel (1) | CPU |
| Lines and carets (CPen solid, width 1 or 2) | MoveTo / LineTo / SelectPen | console 005E4A10, chat typed line 005E3700, edit field 0065C2E0, connect list 0061D780, save/load list (`ui_saveload_list.cpp:104`) | CPU |
| Single pixel | `SetPixel` | minimap marker: N0 found `CHudMap::SetupVanilla` 00641F20, about 33 pixels on a 24x18 surface (18x18 under Rebirth), drawn again at each HUD setup | CPU |
| Get / release DC (start and end of drawing) | 005FDC00 / 005FDC60 (`ui_surface_dc.cpp`: a memory DC on the DIB) | about 57 sites in 38 files | CPU |
| Screen copy into a surface | 005FD3D0 `CaptureScreen` → `render::CopyFrameTo` (staging readback to the CPU) | escape-menu backdrop (`ui_interface_draw.cpp:151/161`), global-map pick picture (`ui_global_map_places.cpp:182`), save game (`ui_saveload.cpp:591`) | GPU → CPU readback |
| Greyscale, (2r+4g+b)/16 | 005FE440 (`ui_surface_pixels.cpp`) | escape-menu backdrop (`ui_interface_draw.cpp:154`) | CPU |
| Save picture | 005FD440 `Rgb565Copy` (CPU stretch with the (2x+1)·src/(2·size) rule) | escape-menu `block5C_` (256x192), QuickSave 006570E0, SaveGame 00655550 | CPU, reads pixels |
| Pixel read | 005FD240 `PixelAt` | global-map click (`ui_global_map_input.cpp:128`) and FindPaths (`ui_global_map_paths.cpp:171`): zone = (pixel>>4)&0xF − 1 | CPU, reads pixels |
| Swap / freeze | 005FD7A0 `SwapContents`, 005FD760 `Freeze` | camp, menus, backdrop mode 3/4; N0: also every window's `ShowSurfaces` 006002C0 on each show and hide | CPU memcpy |
| RichEdit text | 0065F020 `CUiScrollingSurface::RenderText` (EM_FORMATRANGE into the DIB) | titles / credits | CPU, keep |
| Sprites, shapes, minimap, portraits | `CUiMesh::Draw` 005E6D90 → 004E9010 → `render::BeginMesh`; shape builders 005E70E0/7680/7D60/99D0; `ui_mesh_build.cpp` | every panel | GPU (done) |
| 3D previews and models | `CUiModel::Draw` 005FC2F0, hero preview 005EC300, container draws 00600EF0 / 00622420, `render::ClearDepth` | camp, hero, HUD | GPU (done) |
| Rectangles, fades, letterbox, rubber band | slot 19 005041F0 → `render::ScreenDraw`; `CScreenFade::Draw` 005E69C0 | — | GPU (done) |
| Flying hit points | 005E18E0 → 00501BB0 → `TlDraw` | — | GPU (done) |
| Cursor | 005E54A0 → `render::Cursor` (`render_blit.cpp`: CPU composite plus `sg_make_image` every frame) | — | GPU draw, CPU work per frame |
| Movies | `bink_video.cpp` → `MovieFrame` / `MovieBlit` (Bink decodes on the CPU) | — | GPU draw; the CPU decode cannot be avoided |
| CPU-written textures used by sprites | `CopyImage` 005FFFB0 (16 sites in 8 files: portraits, HUD icons, help pictures), `ItemFigureTexture` 005EC020, global-map clouds `CCloudNoise::Draw` 00499F10 (every frame) | — | CPU fill, then GPU texture |

Notes on the inventory:
- The `2DintMMX.dll` / `3DfpFPU.dll` work in the interface is limited to the picture and texture conversion (004F0060, the decoder at 007AF06C slot 5) and the mesh path's CPU projection for picking bounds. Neither does per-frame window pixel work.
- Tooltips, edit boxes, list boxes, the console and chat are all CPU surfaces.
- The minimap and global map are GPU meshes, except for the pick readback, the clouds texture and the one-pixel dot.

## 2. Text

**Changed 2026-10-14 (user):** the native path does not use GDI at all, for metrics or glyphs, and need not be pixel-exact. N2 loads its own font (TrueType rasteriser plus a font metric-compatible with Times New Roman) and its layout also serves the text extent, so wrapping uses the same metrics as drawing. The GDI notes below describe the original behaviour to approximate.

**Fonts.** `CInterface3D::Create` 005F4990 (`ui_interface_create.cpp`) makes `fonts_[0..2]` with `CFont::CreatePointFont` 0070BBE5: "Times New Roman" at width·{104, 112, 144}/800/scale. The scale is the pixel height of a 75-point font. So the font size depends on resolution; with WideScreen the scale's 0.01 becomes width·0.0075/height (addon's float at 005F4F81, `widescreen::FontScale`; W4).
- The console has its own point font (`console_window_create.cpp`); the debug panel has its own too.
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
- `Font`: the small font interface (vertical metrics, glyph advance and box, pair kerning, 8-bit coverage). `LoadFont(path, pixels)` implements it with stb_truetype 1.26 (`STB_TRUETYPE_IMPLEMENTATION` in render_text.cpp only). Em scale = pixels / unitsPerEm, which matches GDI's negative lfHeight. Ascent and descent come from OS/2 usWinAscent / usWinDescent. The average width is the old OS/2 weighted 'a'..'z' + space average, because Liberation's xAvgCharWidth is much wider than Times New Roman's, which would put tab stops in the wrong place.
- Font files: `FontFileForFace` maps "Courier New" to LiberationMono-Regular.ttf and every other face to LiberationSerif-Regular.ttf. `FontDirectory()` is the one place that decides where they live: `SetFontDirectory`, or by default `EI_FONT_DIR` (third_party/fonts, a compile definition on ei_render). A standalone exe or a DLL loading fonts from next to itself calls `SetFontDirectory` once. `PointSizeToPixels(tenths, dpi)` = dpi·tenths/720, as `CreatePointFont`.
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
- **Fallback.** A surface that any code we have not ported draws into stays CPU, uploaded as today. This covers RichEdit 0065F020, and Rebirth-patched draws that still run addon's code in a Rebirth launch (none left after W2–W4, W2a, W2b and W4a: the connect list 0061D780 and Create 0061B2C0, and the hero screen's Create 00613EA0 are ours in a Rebirth launch).
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
  capture, `compare_frames.py --mask-file`; `tests/verify/harness_ui.cpp`). **N4** `ui_gallery` scenario (every window family captured frozen; needs the virtual
  clock). **N5** cursor and per-frame images cached (`render_blit.cpp`).
- **P1** surface plumbing (canvas per native surface, blit, Get/ReleaseDC as the metrics DC, Fill / Clear / FrameRect, readback for
  PixelAt / Rgb565Copy / SwapContents, CaptureScreen as a GPU copy, Greyscale; first native surfaces: backdrop, FPS, status; needs
  N1, N3). **P2** text (005FDE20 records glyph runs, GDI on the metrics DC for the return value; 005FDFA0 unchanged; needs N2, P1).
  **P3a-d** windows by family (menus and dialogs; HUD; camp, hero, trade and item info; briefing, global map titles, help page, chat
  window), mostly native-table entries plus fixes the diff finds; **P4** console and debug panel (direct FillSolidRect / DrawText,
  own fonts, the 2-pixel pen caret); **P5** controls with lines and carets (edit field 0065C2E0, chat typed line 005E3700, connect
  list 0061D780, save/load list 006567A0); **P6** the global-map pick picture (`RefreshSelection` 00630670: frame step, GPU copy,
  readback on click; colours exact, point copy, no dithering) and the camp's backdrop modes 3/4; **P7** (optional) CPU-written
  textures as GPU sub-image uploads, clouds 00499F10 as a shader; **P8** make `uinative` the default under `present`, drop the DIB
  and memcmp for native surfaces (keep a 1x1 metrics DC), docs (renderer.md section 6, ui.md).
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
- **Not ported (keep CPU and upload):** RichEdit titles 0065F020, Bink decode.
- **Open questions:**
  - Should the default later move to direct-to-frame glyph drawing, which looks cleaner but loses the fringe and needs a looser tolerance?
  - Should a resolution-independent UI scale (4K) be done? It needs re-rasterised atlases and changes metrics, so it is out of scope for behaviour-exact work.

### Critical files for implementation
- E:\workspace\ei\src\render\render_blit.cpp (and new render_ui.cpp / render_text.cpp beside it; render.cpp `DrawPending`, render_internal.h `Command`)
- E:\workspace\ei\src\render\render_textures.cpp (`WindowSurfaceView`, surface memory, `CopyFrameTo` readback partners)
- E:\workspace\ei\src\game\ui_text_draw.cpp (005FDE20 / 005F8340; W2 conflict) and E:\workspace\ei\src\game\ui_text.cpp (005FDFA0)
- E:\workspace\ei\src\game\ui_surface_blit.cpp, E:\workspace\ei\src\game\ui_surface_dc.cpp, E:\workspace\ei\src\game\ui_surface_frame.cpp, E:\workspace\ei\src\game\ui_surface_pixels.cpp, E:\workspace\ei\src\game\ui_surface_create.cpp
- E:\workspace\ei\src\game\ui_interface_draw.cpp (frame order, backdrop / greyscale / save picture), E:\workspace\ei\src\game\ui_interface_create.cpp (fonts), E:\workspace\ei\src\game\ui_global_map_places.cpp (pick picture)
