# Renderer: the sidecar renderer, the hand-over from the game, and the switch-over

The game's own renderer (DirectDraw 7 / Direct3D 7) is replaced by a new renderer in a sidecar module, and only the
standalone image runs it (`ei_standalone.exe` + `ei_hook.dll`, docs/notes/standalone.md). game.exe runs the original
code only, on dgVoodoo: its runs are the reference (goldens, function vectors, widescreen frames), never our
renderer's.

1. Game code that called the renderer is rewritten and hands its data to a **new renderer in a sidecar
   module** (`src/render/`, static library `ei_render` linked into ei_hook): camera and matrices, object
   transforms, figure meshes, terrain sectors, particles, textures, the interface windows' CPU surfaces
   (uploaded as textures), movie frames. Game code makes no Direct3D 7 or DirectDraw call and includes no DirectDraw /
   Direct3D 7 header (R27c; section 9); only the gamma ramp's DDGAMMARAMP is left (renderer_gamma.cpp, R27d).
2. No Direct3D 7 emulation and no x87 or compatibility shims in the new renderer. Neither library DLL is loaded:
   2DintMMX's uses went in R28, and 3DfpFPU's projections are plain C++ in game code (`projection.cpp`, R31, section
   10). Results only need to be close (captures are compared with a tolerance); full determinism is out of scope.

Backend: **sokol_gfx** (`third_party/sokol`, commit 2e75443, zlib) on **Direct3D 11**. The new renderer draws, in
the game's order, the terrain (ground and water pages), every mesh the game's own draws (figures, map objects, sky,
interface sprites and 3D previews), the flat geometry (object shadows, selection markers, light decals), the
interface windows' surfaces, the renderer's rectangles (rubber band, fades), the software cursor, the depth clears
mid-frame, every pre-transformed draw (particles and sprites, the vision fog's quads, ribbons and lightning, flying
hit points), the weather (after its builders draw) and the movies (also the players outside the frame); the escape
menu's and the save game's screen copy comes from the new frame. The game has no DirectDraw or Direct3D 7 device and no
texture or surface holder of it is a DirectDraw object (section 9).

Shaders (SV10b): written in `src/render/shaders/*.hlsl` (`shadow_receiver.hlsli` is included by the terrain's and
the meshes'), compiled to bytecode at build time by the Windows SDK's fxc (CMakeLists.txt's `ei_shader`,
`shaders/compile.cmake`: column-major matrices, `/O3`, the flags sokol would give d3dcompiler; fxc's warnings fail
the build) into headers that `render_shaders.cpp` turns into `ShaderBytecode` (render_internal.h); the shader
descriptions take it with `SetBytecode`. No compiler runs at start (it took 0.35 s of every run's start-up); the
bytecode is what d3dcompiler_47 made at run time to the byte but the recorded compile flags and the checksum. A new
shader: its `.hlsl`, an `ei_shader` line per function, its arrays in render_shaders.cpp. The Direct3D 11 device is
made on a thread of its own from the image's start (`render::PrepareDevice`, called by standalone.cpp before
WinMain; not with "removable") and taken at the renderer's first use.

## 1. Always on in the image (no DirectDraw fallback)

The standalone image always draws with the new renderer (R11a, user's decision 2026-10-16: the game code's DirectDraw
fallback is gone): the game has no DirectDraw or Direct3D 7 device, the new renderer draws every frame into an
off-screen target of the game's resolution and presents it through its own flip-model swap chain on the game window
(`render::StartPresenting`, 004EDD70; section 9, R9a), the surface holders of 004EF7B0 / 004F0060 and the textures
keep their pixels in memory the new renderer owns (sections 6, 7), and captures write the new frame as
`frames\<name>.bmp`.

**When the renderer fails (R26, the user's rule 2026-10-28: no fallbacks).** The game cannot run without the renderer,
and no code path draws any other way: when it cannot start or cannot go on, the game ends. `render::detail::Fatal`
(render.cpp) logs "render: fatal: <reason>" to ei_hook.log, shows the reason in the game's error box
(`app::ShowMessageBox`, MB_ICONHAND; the harness ends the run on it) and exits with -1, also with Rebirth's
DisableExitOnCrashReport (which makes `game::FatalError` carry on; the renderer's failure must not). The cases:
- no device at the start (`Start`, at the first use: 004EDD70's `render::StartPresenting` in a normal start): "Can't
  start the renderer: no Direct3D 11 device (D3D11CreateDevice failed, <hr>)." or "...: sokol_gfx could not be set up
  on the Direct3D 11 device.";
- a shader or pipeline the start makes: "Can't start the renderer: the terrain shader / the mesh shader / the blit
  pipeline could not be made.";
- a sokol panic (SokolLog, level 0; sokol would abort after it): "The renderer stopped: sokol_gfx panic <item>
  (<message>).";
- a lost device that cannot be made again (RecreateDevice, five tries 200 ms apart): "The Direct3D 11 device was lost
  and no new one could be made: <reason>." (or its pipelines: "The Direct3D 11 device was lost and made again: the
  ... could not be made.");
- no swap chain on a working device (render_window.cpp `NoChain`): "The renderer cannot present on the game window:
  no swap chain / the swap chain cannot be resized / the swap chain cannot be made again (<w>x<h>)." A swap chain that
  fails on a lost device waits for the new device instead.
A draw the renderer does not take is lost, not drawn another way: `render::MeshPass`, `TlDraw` and `ScreenDraw`
return nothing (R26b), and render_mesh.cpp's `Refuse` logs each reason the first time ("render: <reason>; the draw is
lost": a mesh or pre-transformed draw with no render target size, a mesh corner past its positions, a pre-transformed
draw in another vertex format than 0x1C4 / 0xC4, too many pre-transformed vertices in one frame). An empty draw (no
vertices) is nothing to draw, not a refusal. The device layer has no DrawPrimitive / DrawIndexedPrimitive.
A lost device that is made again (R9c1) is recovery, not a fallback, and stays. There is no `render::Active()` query:
every entry point draws. Only the image runs it: in game.exe (the harness and the vector recorders run inside the
original's process) nothing of ours draws, and the harness asks `ei::Standalone()` (R26a folded HarnessRenderActive into it; a renderer failure ends the game, so
there is no "failed" state to poll): captures there read the old render target. `EI_RENDER` (environment, or the harness step `render <spec>` in a run of the image) is a
comma-separated list of options: the harness build's `removable` (R9c5) and `checkall`; any other word is logged as unknown (R16i removed `classicshadows`: the shadow map is the only shadow look). The original code (`--no-hooks`) never gets EI_RENDER and the harness ignores its `render`
step (logged). There is no mode with the old path drawing beside the new renderer (R11), and the renderer reads no
DirectDraw surface (no holder has one).

## 2. Frame structure of the game

Per main-loop iteration (00458010): 00503F40 BeginScene; 005F5220 the interface draws window by window; 005040A0
EndScene; the escape menu (005F5320, Rebirth-patched); the software cursor (display option only); **present**
004EF610 (rewritten, `device_present.cpp`: ends the sidecar frame, `render::Present`). The battle screen's
world view is 00569E40:
- view setup 00566360 (view matrix from the camera 0079B2A8 through 004F0EF0 pushed on the transform pipeline's
  stacks; fog range), visible sector range 006CA260 (100 units);
- sky (flag 0x1000: 004D96E0, a model following the camera, z-write and alpha test off);
- terrain ground pages (006C6230 -> 006C6460 -> DrawSectorPage 006BFF40 per page);
- objects (0056AFE0 -> CollectVisible 0056A990, sort, figure parts 005A7A70 / 005A7640, shadows 0051CE60);
- terrain water pages (006C6330), blended;
- effects (flag 0x400): vision fog (004E2700 -> VisibilityFog 004E1EB0), client slot 36, particles 00498940,
  lightning (00468940), rain and snow (004E5680); then 00569D60, flying hit points (005E18E0, flag 0x2000).
Then the interface window by window (the top window's slot 20 draws its children): sprites (UiMesh 005E6D90: matrix
pushed on both stacks, colour through renderer slot 7, figure through slot 14 -> 005A7640 -> mesh draw 004E9010), 3D
models (UiModel 005FC2F0; the hero preview 005EC300 first clears depth to 0 and its rectangles to 1) and (slot 22,
005FECC0) surfaces: 005FDA80 blits each shown UiSurface's surface onto the render target, plain or colour-keyed
(DDBLT_KEYSRCOVERRIDE), no alpha blending. Surfaces are composed on the CPU (GDI text 005FDE20 / 005FDFA0, fills,
pixel code). The battle screen clears the depth buffer before its panels (00636020). Renderer slot 19 (005041F0)
draws translucent black rectangles with an outline (rubber band 00636020, fades 005E69C0).

The sidecar's frame: game code hands data over while running its draw code, as a command list in the game's order
(terrain batch, mesh pass, surface blit, depth clear, ...); `render::EndFrame()` (from the present, or earlier for a
capture) draws them in one off-screen pass with depth; the swap chain's present shows it (section 9). Per-frame data is rebuilt
every frame; textures and window surfaces are cached per holder.

Conventions taken over: Direct3D clip space (the game's projection already produces it), Direct3D 7 pixel centres
(3D draws shift their clip positions by half a pixel, `Frame::pixelShift`, because sokol truncates its float viewport
to whole pixels; a clip -0.5..0.5 square covers pixels 16..47 of 64, a screen-pixel mesh from 8 to 24 covers 8..23),
D3DCOLOR vertex colours (B, G, R, A bytes), vertex fog in the specular alpha (255 = no fog), specular added after
texturing. Textures use the .mmp file's own mip levels and no others (no box filter of level 0): the texture has the levels
its file has, DXT or RGB, as the original's (section 7, R28c, R28d).

## 3. What the renderer feeds back into game logic

- **Visibility** (frustum test 004F16A0, in-view flag 0x20 of client objects): readers are client-side only (cursor
  picking and rubber band 0051C130, particle sources 00496EB0, shadow volume 0051E300; also client animation to game
  time, pending figure changes 0050D450, model LRU). **Visibility only gates client-side animation, figure changes and
  picking, never server state or timing.**
- **Picking** uses screen rectangles written during the object draw from projected vertices (004E8900 / 004E86B0 into
  the device's +0x15C, copied by 005A7A70 into node and figure pieces; `WorldClient::UnitAt` / `PickAt2` test the
  parts' rectangles only). Whatever draws the objects must still produce them. The rule (`device_bounds.cpp`
  `BoundsOfProjected`, one path for every mesh): the bounds of the projected positions, clamped to the screen. The
  projection leaves screen positions when the whole mesh is on screen and clip-space ones when it crosses the view's
  edges; those count when inside the depth range (nothing behind the camera or past the far plane), mapped as
  ToScreen maps them. A crossing mesh always hands its passes over (the GPU clips them); a part with no position on
  screen gets an empty rectangle.
  - **Accepted difference from the original** (user, 2026-10-29): the original clipped a crossing mesh on the CPU
    (3DfpFPU slots 36 / 37, ToScreenClipped 34) and took the bounds of the clipped triangles' points that came out
    inside every plane; an empty clip kept the last draw's rectangle and drew nothing. Ours is larger past a screen
    corner, and across one edge it can be wider along that edge (points beyond the edge stretch the other axis): at the
    bottom edge an ogre's part picks a few 800x600 pixels further left or right than in the original. On screen it is
    the original's rectangle. `render_pick_edges` (golden from the original: left and bottom edge clicks where both
    agree) and `render_pick_edges_rule` (the image's golden: a bottom edge click in the strip only ours picks) check it.
    The corners are under the battle screen's HUD, so no click reaches a corner's difference.
- **One real state change in a draw path:** the terrain page draw applies pending water level changes (006C30E0 from
  006BFF40); the water layer is what height queries read. DrawSectorPage applies them before handing over (the
  game's own draw of the page is gone, R11a).
- **Pixels:** the escape menu's background and the save game's 256x192 picture are copied from the render target
  (005FD3D0 CaptureScreen, 005FD440 Rgb565Copy); in the image they are made from the new renderer's frame on the GPU
  and only the 5:6:5 picture is read back (section 6, "Screen copies on the GPU").
- **Timing:** up to five world updates of 0078FF34 ms per frame; vsync only changes grouping.

## 4. Hand-over map (game data, hand-over point, `render::` call)

| Data | Where in the game | Hand-over point | Status |
|---|---|---|---|
| Clip matrix of the current draw | transform pipeline (renderer 007AF060 +8): stacks of 64-byte row-major matrices, `+0x24/+0x28` clip space (axis swap * projection * view * model), `+0x1C` the same times the viewport, `+0x5C/+0x60` view stack | read at hand-over time | used |
| Terrain sectors | SectorData (`terrain_sector.h`): `vertices` (33x33 per layer, water after ground), `records` (SectorVertex: normal, material colour and byte, the specular word: edge fade, on the water the crests), tile lists per quarter and page `tiles`, edge flags `tileFlags`, quarter verdicts `quarterMode`, `detail`, `seamBefore` / `seamAfter`, `owner`; page textures `Terrain::pageTextures`; the lights `Terrain::lights` | DrawSectorPage 006BFF40 (`SectorData::DrawPage`, terrain_passes.cpp) -> `render::TerrainPage`; `HandOverSceneLights` (world_client_shadow_casters.cpp) -> `render::SceneLighting` | drawn |
| Meshes (figures, objects, sky, interface sprites and previews) | positions in blocks of four (morphed by the game), corners, colours as lit by the game (005A7A70 step 3; flag 4: device flat colour +0x150), texture coordinates; matrix = top of the screen stack converted back to clip space; distance fog switch +0x158 and range | DeviceDrawMesh 004E9010 / 004E9A50 (`device_mesh.cpp`) -> `render::BeginMesh` / `EndMesh` | drawn |
| Pass state | per pass of a mesh (material technique 004EB550, or two textures 004EB370): blend, alpha test, depth, culling, specular, fog, texture factor, two stages, filters, addressing, bound holders (TextureDevice::stageSurfaces) | `device_draw.cpp` -> `render::MeshPass` (section 5); per terrain page (blend, depth, culling: the pipeline) at `render::TerrainPage` | drawn |
| Depth clears | 00636020, hero preview 005EC300, trade grid 00627390, hero equipment 00622420, screens 2 / 3 (00632A90 / 00602BF0) | `render::ClearDepth` | drawn |
| Shadows | the sun's shadow map (R16j, section 13): which object a mesh belongs to, which objects cast, the sun; the game's 64x64 shadow textures (2DintMMX) and decals are not ported (R26), their projection (the shadow's sphere) still runs | `world_client_draw.cpp` (00569E40 `render::ShadowSun`, 0056AFE0 `BeginCaster` / `EndCaster`), `map_object_shadow.cpp` (0051CE60 `CastsShadow`) | drawn (the map only) |
| Particles, ribbons, lightning, hit points, vision fog | pre-transformed draws: sprites 00502CA0 -> 00503680, ribbons 00501D90 -> 00502A20, triangles 00501BB0 (`TextureDevice::DrawTlTriangles`, `renderer_strip.cpp`), line strips 005045D0 / 0049B4B0 (`renderer_lines.cpp`) | `render::TlDraw` with vertices after the fog pass and the old device's state | drawn; later the particle data itself |
| Weather | rain / snow triangles built by 004E4240 / 004E4C80 (`weather_particles.cpp`: `BuildRain` / `BuildSnow`, then `DrawParticles`) into pre-transformed vertices | `DrawParticles` (after the fog pass) -> `render::TlDraw` | drawn |
| Interface surfaces | UiSurface (0x38): +0x0C flags (1 colour key, 2 stretched, 4 shown), +0x10 holder, +0x24 rect, +0x34 key | 005FDA80 (`ui_surface_blit.cpp`) -> `render::SurfaceBlit` | drawn |
| Rectangle fills | renderer slot 19 (005041F0) | `renderer_rect.cpp` -> `render::ScreenDraw` | drawn |
| Cursor | object 007C7BD8; images from 8888 pictures loaded by 004EFC60 -> `render::PictureFromMmp`, mask built from the file's pixels (R30b) | frame draw 005E54A0 (`ui_cursor_frame.cpp`) -> `render::Cursor` | drawn; the cursor thread (005E5690) presents between frames (section 9, R9c2) |
| Movies | Bink: BinkCopyToBuffer (32-bit type 4) into a CPU buffer; BinkVideo 005DBA50 opens, 005DBD80 steps and blits into a letterboxed rectangle | `bink_video.cpp` -> `render::MovieFrame` / `MovieBlit` / `SurfaceBlit`, `render::Fill` | drawn |
| Present | 004EF610 | `device_present.cpp` -> `render::Present` (the new frame on the window's swap chain, section 9) | done |
| Clear colour, target size, fog | renderer +0x14; device object 007AF080 -> +0x18; the render state's fog switch and colour (section 5; zone colour through slot 0 00501AB0) | EndFrame / terrain hand-over | done |

Their game-side work stays (visibility, LOD, animation, lighting, sorting, water levels); the goldens check it. Not
handed over directly yet: `figure_part_draw*.cpp` (005A7A70, 005A7640: model matrices and figure data once meshes stop
going through the old projection), the particle data, materials from the game's data instead of the device state,
picking rectangles. Neither DLL has a caller: 2DintMMX since R28, 3DfpFPU since R31 (section 10).

The interface is overlay, not native: each window surface is uploaded as a texture when its contents changed (checked
at every blit) and drawn at the blit's place with the colour key as transparency. Native drawing (glyph atlas from the
game's fonts to keep the metrics the word wrap 005F8340 depends on, every window class's Draw / Update as quad / text
submissions) comes window by window (docs/notes/ui-native.md, section 9): a window switched to native simply stops
blitting its surface.

## 5. Pass state

`src/render/render_state.h` (R27a): the render state the game draws with is one plain struct the renderer owns,
`render::g_renderState` (`render::RenderState`; not "DrawState": winuser.h makes that a macro): blending and its
factors, the alpha test, depth test / write / function, culling, specular, fog and its colour, the texture factor,
and two texture stages (each its colour and alpha operations and arguments, `StageCombiner`, its filters and
addressing). The values keep Direct3D 7's numbers in typed enums (`Blend`, `Compare`, `Cull`, `TextureOp`,
`TextureArg`, `Filter`, `MipFilter`, `Address`), which the pipelines and the mesh shader take as they are. The game
code writes the members where the original called SetRenderState / SetTextureStageState (device_draw,
device_states, renderer_init, renderer_lines, renderer_rect, renderer_sprites, renderer_strip, link_mesh_draw,
particle_draw through `SetBlend`, world_client_draw, ui_alpha_draws, ui_container_draws, ui_create_screen,
ui_help_window, ui_hero_preview, ui_hud, ui_hud_unit_draw, ui_interface_draw, ui_item_view, ui_objectives); the
original's GetRenderState / GetTextureStageState around a draw (saved and put back) are reads of the members.
`SceneRenderer::CreateRenderer` resets it to a new Direct3D 7 device's state (the members' initialisers) before the
game's own settings. Each pass copies it (`ReadPassState`, render_mesh.cpp; the fog once a frame, `ReadFog`). There
is no other copy: there is no Direct3D device (section 9).

Not kept, because nothing in the renderer reads them: shade mode, dithering, z bias, antialiasing (the camp's switch
is gone with it), perspective correction, fog vertex mode, extents, clipping, lighting, ambient, fill mode, the
texture coordinate index and transform flags (every material stage's index was 0), and stages 2-7.

The device recorded four stage-0 state blocks at its creation (004E6392..004E67BD; handles 007AEDDC / D8 / D4 / D0).
They are constants in texture.h; the 007AEDD4 one (texture x diffuse, alpha texture) had no user and is gone:

| original | constant | colour | alpha |
|---|---|---|---|
| 007AEDDC | `kTerrainStage` | texture x current (MODULATE) | texture (SELECTARG1) |
| 007AEDD8 | `kAlphaPassStage` | texture x diffuse | texture x diffuse |
| 007AEDD0 | `kSceneStage` | texture x diffuse | diffuse (SELECTARG1) |

A material's preset (`RenderPreset`, model.h; render_helper.cpp; the original's 004FD2A0 recorded state blocks)
keeps its state list: `ApplyPass(0)` sets the combiners of its first `stages` stages (the enabled ones) and turns
the next stage off; pass n > 0 sets that state's blend factors (device_draw.cpp's multi-pass draw puts the blend
back afterwards).

Rewritten state setters: 00501C60 (renderer slot 10, `device_states.cpp` `SetBlend`: the two blend factors; known
caller the particle draw 00498940); 0049B4B0 (`renderer_lines.cpp`: the strip of the renderer's line lists, 0049B780
from slot 15 00501CE0: bounds boxes 005A7290, line lists 00569D60; stage 0's colour / alpha ops off and back to
MODULATE around it) goes to `render::TlDraw` (line strip, FVF 0xC4), not reached by scenarios; 005041F0
(`renderer_rect.cpp`). 0049B140 only appends a line-list point; 0050434F and 00504341 are not functions (inside
005041F0; a false vtable entry 00747E2C in config/vtables.csv).

## 6. Interface windows and holders

**Interface windows on GPU canvases (P8).** A window surface's pixels live only in its interface canvas
(`render_ui.cpp`, docs/notes/ui-native.md, "P8 done"): an RGBA8 render target per holder, made with the surface
(`UiSurface::MakeCanvas`), drawn into by the surface functions (fills, frames, lines, pixels, glyph runs in our own
fonts, the screen copy, greying) and blitted by 005FDA80 (`render::ui::DrawCanvas`). There is no
memory, no DIB section, no GDI, no comparison or upload at a blit. The readers get pixels from the GPU: PixelAt
(`ReadPixel`), the save picture (`ReadRgb565`); SwapContents moves the canvas aside (`MoveCanvas`). A lost device
brings every canvas back from the commands drawn on it since it was last covered whole (a screen copy's and a long
history's pixels read back instead; R9c1).

**No memory** (R30b): no holder has pixels in memory. The cursor's pictures were the last (004F0060 filled memory in
the screen's format, 005E4EB0 read it back for the mask): the mask is built from the .mmp file's pixels
(`ChannelLayout::ToNative` to the screen's format, ui_cursor.cpp), checked against the memory path at 16 and 32 bits
for all 25 cursor pictures before it went; the renderer keeps the picture as RGBA8 (`render::PictureFromMmp`).

**Holders without a surface**: the holder is our own `Surface` (surface.h: device, width, height, bits per pixel; R27b
replaced `DXSurface` and its always-null DirectDraw surface pointer): the holders of 004EF7B0 (windows 005FCC00 /
005FCDD0 / 005FD7A0: a canvas, no memory; the cursor's background and composition surfaces 005E60B0 made are gone, R30a), the picture holders
of 004F0060 (cursor images via 004EFC60 from 005E4EB0: size only, no memory; dead picture windows) and the textures
(section 7); no Lock / Unlock, GetDC, Blt or colour fill. A window of no size is not made: the original's DDSurface
error is thrown (`ThrowSurfaceNotMade`). The screen's pixel format is `ScreenFormat` of the device object's colour depth
(`bitsPerPixel`: 16 bits 5:6:5, else X8R8G8B8): a canvas quantises to it. If the renderer fails later the game ends
(section 1, "When the renderer fails").

| Address | Name / change |
|---|---|
| 004EF7B0 `DXSurfaceConstruct`, 004EFD20 destructor body | holder without memory, no CreateSurface |
| 004F0060 `DXSurfaceLoadPicture` (RET 8) | 8888 pictures only; the holder takes the picture's size and the screen's bit count, nothing is filled (the original: 32 bits a row copy, 16 bits each pixel cut to the screen format's channels, as 2DintMMX slots 1, 2 did; R30b); only live caller 004EFC60 from 005E4EB0 (flag byte `00784756` is 1 in the exe, never written) |
| 005E4EB0 `CursorImage::Construct` (`ui_cursor.cpp`) | picture, holder, frames = width / height, hot spot x cursor size (007C7C90 / 94) / 32, mask from the .mmp pixels in the screen's format (16 bits all ones where the 5:6:5 pixel is 0; 32 bits low 24 bits 0) |
| 005DBA50 `BinkVideo::Open` | no BinkDDSurfaceType (the holder has no surface): 4, the CPU copy's type |
| 005FCC00 / 005FCDD0 / 005FCF80 / 005FD120 | the colour-fill Blt that clears a new window is left out (a new canvas is black) |
| 005FDC00 / 005FDC60 | gone (P8): no DC; the callers' GetDC / ReleaseDC brackets are left out |
| 005FD3D0, 005FD440, 005FE440, 005FD7A0, 005FD240 | screen copy, save picture, Greyscale on the GPU (R28a, "Screen copies on the GPU" below); SwapContents (the canvas put aside on the GPU), pixel read (from the GPU): no Lock / GetSurfaceDesc |
| 005FDA80, 005E54A0 / 005E5690 / 005E6630, 005DBD80 / 005DB3F0 / 005DA400 | the original's Blt, composition 005E5C40, background save, Lock / BinkCopyToBuffer / Unlock are gone (R11a); the new renderer gets the blits, the cursor and the frames |

Scenarios: render_present, render_titles_present, render_movie_present, ui_compare, ui_saveload, render_device_lost.
Not reproduced: the unwinding when 004EFC60 / 004EF7B0 throw and the fallback to 004EF7B0 when 004EFA60's
video-memory holder throws (only with 00784756 0; 004EFA60, the holder from a DirectDraw description, and 004EFEA0
are not ported: config/dead_code.csv, R11b). The original quirks kept (failed Locks returning with the device
lock held, the 24-bit clear ignoring the pitch, ...) are in the functions' comments.

**Save / load screen's pictures** (`ui_saveload.cpp`): two 256x256 5:6:5 textures made by 005FF0B0 (-> 00691D80 ->
0068F6C0): objects[1] filled by 00656E10 (ShowPicture: the selected save's shot.sav) and +0x184 (the new save's
picture) filled by 00655D40 (FillEntries, from the escape menu's 256x192 copy, interface +0x5C) and written to shot.sav
by 00655550 (SaveGame; the autosave 006570E0 writes it from a window surface). 00656464 is not a function (inside
00655D40; a false vtable entry 0074718C). Both are drawn into on the GPU (section 7, R28b); SaveGame writes the copy the window kept.
The new folder is "save<n>", n one more than the highest atoi(folder.Mid(4)) (006FD9B4 is Mid). Checked by scenario ui_saveload. The SavesPath Rebirth patch
is disabled in this install and not reproduced.

**Screen copies between frames** (R13): 006570E0 (quick save F5 "saveq", autosaves) copies the screen with
CaptureScreen (005FD3D0) and stretches it to 256x192 (005FD440) for shot.sav. The new game's autosave passes DL 1 and
draws the interface first (FrameStep without a present: the copy comes from that frame); the quick save key passes DL 0
and copies between frames, before the next frame's first draw. The original's render target then still holds the
last frame, and so does the new renderer's target: the copy (`render::ui::CopyFrame`, which draws the frame so far
first, SplitFrame) reads it as it is when nothing of the frame is drawn or queued yet (no pass, no commands). A pass
there would clear it first: the save's picture was the plain clear colour (blue-grey 0x8596) after R11a. Checked by
scenario ui_quicksave (`checksave saveq world`: the picture against a capture of the same frozen moment as 8x8 block
averages; the original's: at most 22 per channel, a cleared one 145+).

**Screen copies on the GPU (R28a).** A surface the screen is copied into (CaptureScreen 005FD3D0: the escape menu's
and the camp's backdrops 005F5320, the global map's pick picture 00630670, the quick save's copy 006570E0) keeps its
pixels in its interface canvas only (`render_ui.cpp`, docs/notes/ui-native.md section 3; since P8 every window surface
does): the copy is the canvas's `ps_copy` from the frame
target (x * source / size, as DirectDraw's Blt and the old CPU copy picked), the greying 005FE440 its `ps_grey` (the
format's own bits, 005FE440's formulas), and the blit draws the canvas. Rgb565Copy 005FD440 draws the canvas into an
R16_UINT target of the picture's size (`ps_rgb565`: the pixel under each centre, (2x+1) * source / (2 * size), each
channel's top bits) and reads only that back (`render::ui::ReadRgb565`, a row copy): the escape menu's 256x192
`screenCopy` and the quick save's shot.sav keep their 5:6:5 format. PixelAt 005FD240 on such a surface reads its
one pixel from the GPU (`ReadPixel`, each channel cut to the screen format's top bits by `render::ui::TopBits`: the
pick picture's zone colours exact), and
SwapContents 005FD7A0 (the camp's backdrop put aside in mode 3) moves the canvas aside under the surface's address
and back (`MoveCanvas`) instead of keeping the bytes (the CPU loops and `render::CopyFrameTo` are gone). Once a
copy is drawn the canvas's pixels are read back as its history (P8), so a lost device brings it back as it was.
Checked by ui_quicksave (checksave), ui_global_map_pick (the zone under the click), the gallery's escmenu /
map_escmenu frames, ui_compare and `ei_ui_check` ("picture", "lost").

## 7. Textures without DirectDraw

No texture has memory the game writes (R28b): .mmp textures have only their decoded copy; the composed ones, the
textures 00691D80 makes (kinds 0x1E, 0x20, 0x21) and the .bmp textures only the texture the GPU drew. The texture fills 0068FA10 / 00690970 / 00690B30 / 00690D30 are not ported (config/dead_code.csv).

**Texture formats (R27b).** A texture keeps the id of the format it is held in (`CTexture::format`: 0x5650, 0x5551,
0x4444, 0x8888 or a DXT1..DXT5 code; 0 none). The list is fixed (texture.cpp `IsTextureFormat`, what the old device
offered; DXT5 must stay: Rebirth's battle00 and footprints are DXT5) and `TextureFormat(id)` gives an RGB one's
`render::PixelFormat` (bits and channel masks, render.h; the interface canvases take the same type). An .mmp file
is held in its
own format, a DXT code outside the list in the RGB format its channel bit counts name (`MmpHeader::HeldFormat`; none:
the original's "Can't find texture format" error); a .bmp texture by its kind (1:5:5:5, 4:4:4:4 or 5:6:5). The file
formats (MMP, BMP, shot.sav's 5:6:5 picture) are read and written as before; the renderer uploads a DXT .mmp file's
blocks as they are (BC1 / BC2 / BC3, R28c) and decodes the others to RGBA8;
no game code writes a texture's pixels (R28b).

**No memory textures (R28b).** The textures in the renderer's memory (R16b: read again at their first bind of a frame
when marked written and changed, written in place with `UpdateSubresource` and `GenerateMips`, the harness's
`memorytextures remake` and the texture "MISSED WRITE" check) are gone with their last users: `SurfaceView` returns a
holder's decoded or GPU-drawn texture only (white for none). The shadow textures had gone with R26.

| Address | Name / change |
|---|---|
| 0068F6C0 `CTexture::CreateBlank` (`texture_blank.cpp`, RET 8, EAX 1) | texture square from header->width (height not read): `TakeBlankHolder`, a `NewDecodedTextureHolder` with `render::BlankTexture` (R28, R28b), the alpha as the format keeps it (5:6:5 opaque, 1:5:5:5 one bit, 4:4:4:4 all); the 1:5:5:5 atlases one level, the others their levels made after each draw (as the memory textures had; the original: 004EFA60's texture with mipCount levels, caps by the kind flags); +0x28 = bits x size x size / 8 |
| 00691D80 `CTextureManager::NewTexture` (RET 0xC) | kinds 0x1E / 0x20 / 0x21 get the texture formats 5650 / 5551 / 4444 (always there: the original's "Can't find texture format %d." is gone); another kind gets none and 0068F6C0 throws; its texture through 0068F6C0 |
| 00692010 `NewShadowTexture`, 00692140 `ShadowRenderSurface`, 00690FC0 `LockPixels`, 00691030 `UnlockPixels` | not ported (R26): only the 64x64 shadow texture used them, which the shadow map replaced (section 13) |
| 00690430 `CTexture::CreateFromMmp` (`texture_mmp.cpp`) | no staging surface, fill (0068FA10), Load or priority; `render::TextureFromMmp` takes the file's levels (DXT1..DXT5 as their blocks; 8888, 4444, 5551, 5650 decoded by the header's channel masks; keyed by the holder); +0x28 = `MmpHeader::FilledBytes`; an undecodable format keeps the holder and shows nothing (logged once) |
| 0068FBF0 `LoadBmpFiles` (`texture_bmp.cpp`, RET 0xC) | loose textures\<name>.bmp (from 00690260) via the game's DIB reader (006D4AA0 / 006D5910 / 006D4D10 / 006D4CD0), drawn into a blank texture (`TakeBlankHolder`, `DrawIntoTexture`; R28b: 8-bit colours instead of the format's 16 bits); reached only by render_loose_bmp (no .bmp in the installs); loops over the width then the height (a non-square .bmp overran the surface; the pixels past it are left out) |
| 00691B00 `ReleaseObject` (slot 24), 00691BF0 `TakeFromCache`, 00693C60 `FindTexture` (`texture_cache.cpp`) | cache list +0xBC, bytes +0xD8; no SetPriority (no surface); 00693C60 does not lower +0xD8 for a texture it takes from the cache (00691BF0 does) |

Where an .mmp file's levels are (R12b): the first level the texture takes at `MipOffset` (006908E0's geometric sum),
each next one right after the last, bits per pixel x width x height / 8 bytes (sizes halved down to 0), as the fill
00690970 steps. 006908E0's 32-bit product overflows from level 6 of a 1024 x 1024 DXT1 and level 5 of a 2048 x 2048
DXT3: taken for every level, the decoder read zone1's tree crowns' small levels from the wrong bytes (opaque bright
green texels, drawn as thin strips where a leaf card is seen edge-on). A DXT level under 4 x 4 gets only those bytes
(the start of its block) and zeros for the rest of the block it is uploaded as.

**DXT textures as blocks (R28c).** A DXT .mmp texture is a BC1 (DXT1), BC2 (DXT2 / DXT3) or BC3 (DXT4 / DXT5) image of
the file's blocks (`BlockFormat`, `ReadMmp`); there is no CPU DXT decoder. It has the file's levels only: Direct3D 11
cannot box-filter a block-compressed level, and the original's texture had those levels too (1035 of the installs'
1792 DXT files stop early, 229 of them at level 0: the menus' backdrops, tree03, the decals). Its top level must be a
multiple of 4 wide and high (Direct3D 11's rule): every DXT file is, at the levels the texture quality option starts
at (0..2). The software cursor's pictures (`PictureFromMmp`) are 8888 files; a DXT one is not decoded (logged). Measured
(verify build, the log's `render: .mmp textures at exit` line; vanilla, zone1, 60-68 textures): uploaded 176 / 28 MB,
held at most 129 / 19.6 MB (6.6x less), CPU time making them 296-344 / 110-116 ms (before / after). Frames against the
CPU decoder (render_scene, units, models, render_device_lost, render_terrain_seams; 4K): 0.13% of the pixels over 8, at
most 79, all on interface sprites drawn smaller than their DXT texture that has one level (the portrait's health and
mana bars, the minimap's frame: sharper now, the CPU path had box-filtered levels the original lacked); the main
menu 0.031%; the world the same; the camp frame the same pixel for pixel.

RGB .mmp textures (8888, 4444, 5551, 5650) hold the file's levels only too (R28d; the CPU box filter that added the
missing ones, `MakeImage`'s halving and `HalveLevel`, is gone; `HalveLevel` stays only in `CheckMmpLevels`'s own
comparison, verify builds). Frames before / after (render_scene, units, models, render_device_lost's menu, camp and
world; 4K): identical pixel for pixel, the `.mmp textures at exit` line the same (60 made, 28457 KB uploaded, 19588 KB
held): every RGB file those scenes draw has its whole chain.

Users of the CPU-written textures: 005FF0B0 (`UiWindow::CreateTexture`, from the battle screen 00635250, options
006503C0, save / load, global map 0062D350, tutorial 00658E10, 00605DE0, 00608F90, 00640060) and 005EC020 (item screens'
figure textures); writers 005FFFB0 CopyImage and 005EC020 draw on the GPU (R28, below), the save / load pictures and
the global map's weather 0062F050 too (R28b, below). The binders (004EB300, 004EBAF0, 00501AF0, 004EB370's inline
one) pass null to SetTexture (the old device draws nothing in the switch-over; the renderer reads the bound holder,
TextureDevice::stageSurfaces). Renderer side (`render_textures.cpp`): decoded textures (`StoreDecoded`) are keyed by
the holder; a holder with neither surface nor memory shows its decoded texture until it goes (`HolderGone` retires its
texture and window entries).

Texture patches (R11d): `render::TexturePatch(target, to, source, from)` records that a rectangle of one holder's
texture shows a rectangle of another's (first-level texels, one per target, gone with either holder). `ReadPassState`
gives stage 0's patch to the batch (`PassState::patch`), and the mesh shader samples the source (`tex2`, stage 0's
sampler; coordinates kept half a texel inside its rectangle) where the wrapped stage-0 coordinates fall in the target
rectangle; the target texture is not changed. The unit's face (00533980, the blink: the original blits into the skin
down the mip chain) is its only user; check `render_blink` (frames of the hero's portrait with the mouth open, closed,
open again; the original's run the same).

**Textures composed on the GPU (R28, `render_compose.cpp`).** `render::ComposeTexture(holder, size, mip, levels,
images)` makes a texture from images drawn over each other: the unit skins and wounds textures
(CTextureManager::Compose, items_spells.md "Composed textures"). Each image is an .mmp file's pixels after its header and
extra block: 32-bit 0xAARRGGBB with premultiplied alpha, rows from the top; "PNT3" files are packed: groups of four
pixels, where a group's first pixel has alpha 0 and is not 0 that word instead counts target bytes left as they are
(2DintMMX slot 5's reading; its plain path took pixel 1's alpha for pixel 0 of each group, not kept). The renderer
decodes an image to RGBA8 once (shared by name while a texture holds it: `DecodeComposeImage`), and draws into a
Direct3D 11 texture of the full size with every level the texture uses (`ComposeRecipe`): cleared to transparent
black, each image a rectangle drawn texel for texel (shader texture_compose.hlsl) with premultiplied blending (ONE,
INV_SRC_ALPHA, alpha too), then `GenerateMips`; the texture's view starts at level `mip` (the texture quality option),
so its size is `size >> mip` (HolderSize, texture patches: the blink patches the hero's composed skin). The work runs at
once from the game's load (not inside a frame pass), with the present's lock held against the cursor thread
(`GpuWorkAlone`). The entry is owned and keeps its recipe (the decoded images, shared): a new device draws every
composed texture again right after it is made (`RemakeComposed`; render_device_lost's camp and world frames match
before and after the loss). Frames against the CPU composition (render_blink, units, models, render_device_lost: at
most 0.012% of a 4K frame over 8, on the hero's and wounded units' skins; ui_gallery_camp unchanged): the 8-bit
colours instead of 1:5:5:5 / 4:4:4:4.

**Item pictures drawn on the GPU (R28).** The 1:5:5:5 textures (00691D80 kind 0x20: the item screens' figure textures
005EC020 and the image atlases 005FFFB0 CopyImage fills: the interface's portraits, buttons and item cells) are
composed textures without layers: `render::BlankTexture` makes the transparent texture (one level), and
`render::DrawIntoTexture(holder, image, x, y, width, height)` draws an .mmp picture's rectangle (or white, without
the file) into it, replacing what was there: the same shader with blending off, alpha cut to one bit (at least
half: opaque, else 0) as the 1:5:5:5 store did. The step joins the recipe (a step it covers whole leaves it), so a
new device draws the texture again. The picture is the file's pixels right after the header (the extra block not
skipped, as the original read them). No 2DintMMX: the original unpacked the picture with slot 5 and packed it to 1:5:5:5
by hand under the device lock. Colours are 8-bit now (the picture's own instead of 5 bits a channel).

**Save / load pictures and the global map's clouds on the GPU (R28b).** The textures 00691D80 makes for them (kinds
0x1E, 5:6:5, and 0x21, 4:4:4:4) are blank textures like the item pictures. The save / load pictures: a 256x192 5:6:5
picture (shot.sav's bytes as stored, or the escape menu's copy `screenCopy`) is drawn into the texture's top with
`DrawIntoTexture` (`ImageLayout::Rgb565`, decoded to RGBA8 for the draw; an image without a name is not shared); the
save window keeps the copy (`SaveLoadWindow::newPicture`) and writes shot.sav from it, so the file's bytes are as
before. The clouds (`GlobalMapClouds`, global_map_clouds.cpp): the CPU keeps only the three random words of each of
a frame's four layers (`render::CloudFrame`, taken at build steps 1-4 as 00499F10's layers took them: the game's
random numbers are drawn as before), and `render::DrawClouds` each frame draws the texture from two frames, the blend
and the 256-entry alpha ramp (texture_clouds.hlsl `ps_clouds`: white, alpha ramp[(to x blend + from x (256 - blend))
>> 8], 8-bit alpha now instead of the ramp's top 4 bits). A frame's 256 x 256 grey levels are built once by the GPU
from its words (`ps_frame`, into an R8UI texture, kept by words in render_compose.cpp: at most 4): the layers' hash,
the 3 x 3 smoothing in the 1024 cells' order, the octaves' bilinear sums and the byte store, integer for integer as
the CPU code did. The step joins the texture's recipe (it covers the whole texture, so it replaces the last), so a new
device draws the clouds again. Frames against the memory textures (4K, at one frozen moment): ui_saveload's `save` /
`load` / `load2`, ui_quicksave's and render_loose_bmp's `world` the same pixel for pixel; ui_global_map_pick's `map`
0.005% of the pixels over 8, at most 10 (the clouds' 8-bit alpha): the noise is the CPU code's.

Scenarios: render_present*, render_movie_present, render_titles_present, ui_saveload, render_blink. Dead / not functions: 00693DE0,
0068F860 (reached only from unreferenced 00691D50),
004EAFD0 (inside 004EA4A0's range).

## 8. Draws with old-device calls, movies and the cursor outside the frame

Interface draws (`ui_container_draws.cpp`; each keeps the original's device calls in order, states go to
`render::g_renderState`, depth clears to `ClearUiDepth` (ui.h), which hands them to `render::ClearDepth`; so do the
clears of 00636020, 005EC300, 00627390 and the other interface draws). The rewrites:
00600EF0 (UiContainer slot 20: stage 0 `kAlphaPassStage`, 005FEB90, children's slot 20); 00602BF0 / 00632A90 (slot 20 of
screen 3, vtable 007405AC / screen 2, 00740688: depth clear then 00600EF0; no scenario reaches them); 00622420
(HeroEquipPanel slot 20, vtable 007418E0: highlight frames, quick items' models (005ECA10), in the camp the armour
models in seven slots between three depth clears, each after a flush of the two batches); 0064D970 (slot 26 of vtable
007411BC: alpha reference = the window's +0x1EC).

Movies and software cursor (the original draws them to the primary; its device calls are gone, R11a):

| Address | The original | Ours |
|---|---|---|
| 005DB3F0 `PlayMovie` (`bink_video.cpp`; fastcall name, frame time, 5 stack arguments (the fifth never read), RET 0x14) | blocking player (callers 00458270, 0057A060, 004B89B8, 00602190, 00632940, 00634D20, 00634E60): own message loop (Esc / Space end or skip a frame), letterboxed blit to the primary | each frame to `render::MovieFrame`, then `render::Fill` (black) + `MovieBlit`, presented through 004EF610; result 0 / 1 (1 with movies off), 00784751 set again |
| 005DB9E0 `HolderFill`, 005E5640 `HolderBlt`, 005E5C40 composition | colour fill, Blt between holders, cursor blend, under the device lock | not ported: no caller of ours (config/dead_code.csv, R11b) |
| 005E6630 `CursorSaveBackground` | background copy under the device lock | does nothing |
| 005DA400 `ShowLoadingMovie` (fastcall progress) | loading movie (progres*.bik per zone by 00595080, path 007AF460 + "Movies\" + name; state 007C5550 / holder 007C551C / type 007C5554 / rect 007C5520) stretched over the primary; hides the cursor (005E5270 0) on every call and never shows it again | last frame blitted and presented as above |
| 005E5690 `CursorThread` (started through 005E52A0) | every 41 ms with the software cursor bit: restore background, save, compose (005E5C40), blit to the primary | the last frame presented again with the cursor moved (R9c2) |

005DA880 is dead (a thread procedure started only by 005DAD50, which has no caller); 005DA5AB / 005DB603 are catch
blocks (fallback to 004EF7B0, only with 00784756 0). The harness's input steps wait for PeekMessageA calls recognised
by return address; the rewritten player's call no longer returns to 005DB788, so input steps are not posted inside a
movie (the harness empties movie.ini's lists). Scenario render_movie_present (PlayMovie("crdtfin.bik") after a new
game); 005DA400 runs in every new game; 005E5690 presents only with FpsIndependentCursor (render_cursor_present
sets it).

Shared helpers (render_internal.h): every read-back of a texture (the frame, the canvases, the composed levels, the
harness build's upload / ramp / cursor checks, also the cursor thread's) goes through `StagingCopy`
(render_staging.cpp: a staging texture kept and grown, copied into and mapped; no sokol state touched, so a main-thread
read resets sokol's state cache after it); `D3DTexture` is a sokol image's texture, `Channel` a pixel format's channel
(its 8-bit value: `Widen`), `RectParams` / `ApplyRect` the rectangle shaders' vertex block. The .mmp header is
texture.h's `MmpHeader` on both sides.

Images kept across frames (`render_blit.cpp`). The cursor (R32): each cursor picture (all its frames side by side)
goes to the GPU once, at its first draw, with its mask as alpha (255 where the mask is zero), as an immutable image
per holder; it is made again only when the holder's mask or size changes, let go when the holder goes (HolderGone)
and dropped with the device. A frame's cursor is that image with `from` as the source rectangle: the present shader
reads the part (`cursor_params.cursorSize`: its size and top left texel) and the cursor thread draws the same
texture (its shader view referenced under the sheet lock), so no frame composes or uploads cursor pixels. Before
R32 the part was cut out on the CPU every frame and uploaded whenever it differed (render_stats: 1 upload, 4 KB, a
frame in every scene; render_cursor_present: 31 requests, 31 uploads); now render_stats' small uploads are 0 after a
picture's first frame. A movie keeps one dynamic image per player, updated with `sg_update_image` in place unless it
was already blitted or updated in this frame (sokol allows one update per image and frame, and the blits are drawn
at the end of the frame; then a new one replaces it after the present).
`render::Fill` (the movie player's black) is a solid rectangle command, drawn with the depth clear's shader
(`solid.hlsl`) through a pipeline that writes the colour only (R30c).
Harness build: counts per purpose in ei_hook.log (`render: images ...`, every 300 frames and at exit) and every
in-place upload is read back from the GPU and compared with the pixels written (`render: MISMATCH` on a difference;
sampled like the gamma ramp check: the first 64 of each kind, then every 16th, all with `checkall`).

## 9. The present path and what is left

The switch-over without DirectDraw (R9a to R9c3, R10) is done and is the only way our renderer runs. The open items
come first, then what holds for the present path (the subsection names are cited from src/ and the tickets).

### Open items

- A desktop display mode change while the game runs is not exercised: no run may switch the display mode (R9c4
  below). R9c covers a window resized from outside, which is what such a change does to the game window.
- **Native interface** (docs/notes/ui-native.md, tickets N1, N4, P1 to P8): window surfaces drawn on the
  GPU instead of GDI into DIB sections, no GDI in src/render, own font loading. R28 (which took P7 in) put the composed
  skins and the item pictures on the GPU, R28a the screen copies (the menu backdrop, its greying, the save's picture,
  the pick picture); the global map's clouds and the save / load pictures are still written on the CPU (R28b).
- **DirectDraw left after R11c**: R11b removed the holders' DirectDraw branches and the 18 DirectDraw-only ports no
  code of ours called (config/dead_code.csv rows "R11b, DirectDraw only"; find_dead_code.py drops hand rows, so re-add
  them after a regeneration); R11c the surface tests (screen clear, capture, eye blink, binds), `device::Surface` and
  the DDSURFACEDESC2 layout of `DirectPixels` (now `SurfacePixels`). R27b: the holder is `Surface` (no surface pointer),
  pixel formats are `render::PixelFormat` (bits and channel masks, render.h; R30g: the canvases' too) and the texture
  formats one fixed list
  (`TextureFormat`, section 7); no format enumeration, display-mode format read or depth format. R27c: no device
  layer at all (R9a below): device.h, the switch-over device and its tokens are gone. R27d: the gamma ramp is one uint16 table.
  R27e: app_dxversion.cpp's version probe of the system's DDRAW.DLL and the DirectDraw / Direct3D rows of dx_errors.cpp's
  message table are gone; no DirectDraw or Direct3D 7 name is left in game code.
  The EI_RENDER_VALIDATION code (harness build) was read through in R27e: every check and switch left (upload and
  cursor / ramp read-backs, kept mesh / terrain page against a fresh build, missed surface writes, level map, timers,
  removable device) validates or measures the renderer itself and has a scenario; the A/B against the old device draws
  went with R26 (meshcorners), so none was removed.
  R11d: the blink is a texture patch drawn in the shader (section 7).
- No ticket yet: weather's old draw
  (`RainSnow::DrawParticles`) still runs next to the hand-over, into the stand-in; GPU shadows; several steps (holders, saved pictures, interface
  draws, movies, textures) are checked by goldens and harness scenarios, never by a person in game.

### R9a: the game without DirectDraw

The game has no DirectDraw object, display mode, primary, render target, depth buffer, Direct3D object or device, and
no stand-in for any of them (R27c):
- `TextureDevice` (texture.h, device_object.cpp; the original's CDXDirectDraw base 004EDD70 / 004EE330 and the device
  object 004E5E50 / 004E68A0 folded into one class) is the screen: window, full-screen flag, screen rectangle, colour
  depth, the transform pipeline, the stages' bound holders and the draws' state. Its constructor (R28: no MMX check
  and no 2DintMMX.dll any more, its last uses gone) starts the new renderer (`render::StartPresenting`: the Direct3D 11
  device) and sets the main thread's x87 precision to 24 bits as Direct3D 7's CreateDevice did (the game passed DDSCL_FPUSETUP). The
  options' driver, refresh rate and depth / stencil bits choose nothing. No display mode list (nothing read it), no
  video FourCC probing, no format table (R27b: texture.cpp's fixed list, section 7). Where the game read the primary's
  or the render target's size (`GameTargetSize`, the cursor's bounds, the interface's size, the sprites' clip and the
  hit points' screen) it reads `screen` (R27f); the screenshot and the movie recorder copy the renderer's last frame
  at the screen's size and depth (`CaptureScreen`, surface_capture.cpp).
- What the game read from the old device's capabilities is constant now, with the values the game saw on dgVoodoo:
  the material presets take every stage of their states (`RenderHelper::Record`: eight stages, every texture
  operation); the texture manager loads textures up to their size (`limitTo512` 0: 4096-pixel textures) and caches
  released ones up to a tenth of 256 MB (`CTextureManager::kCacheLimit`); mipmapping stays as the options say (the
  device had mipmaps); the little-memory branches (the interface backdrop at 400x300, the item screen's caches of 20)
  are gone (256 MB was never little); start-up's "the interface takes the input" byte, which held the top byte of the
  free non-local video memory without sound, is 1 there (`GameMain`).
- No scene bracket, viewport, clears or texture binds on a device: `SceneRenderer::BeginScene` / `EndScene` keep the
  last frame's time, a texture bind only records the stage's holder (`stageSurfaces`, which
  the renderer draws with), the interface's depth clears go to `render::ClearDepth`
  (`ClearUiDepth`, ui.h), and the frame clear is the renderer's (its clear colour is the scene renderer's `fogColour`).
  The deactivation's RestoreDisplayMode, the activation's RestoreAllSurfaces, the message box's FlipToGDISurface and
  the movie list's screen clears are gone. The harness's RestoreAfterMinimise calls game.exe's own IDirectDraw7 in
  the original's runs; in the image `image::RestoreSurfaces` has nothing to restore. The vertex-buffer batches are gone
  (R9b3: the batch members, 004E7820 / 004E7E00 and the flush bodies with their DrawIndexedPrimitiveVB; R9b4 took out
  the empty `FlushBatch` / `FlushBatch2` ports 004E7840 / 004E7A40 and their ~35 callers).
- 004EF610 ends the frame (`render::Present`) and returns 0: `EndFrame` draws the frame into a flip-model swap chain
  (FLIP_DISCARD, BGRA8, two buffers, `MakeWindowAssociation` without Alt+Enter / window changes) on the game window,
  sized to its client area (`ResizeBuffers` when it changes; the hook makes the process system DPI aware at WinMain so
  that rectangle is in physical pixels), stretched over it, `Present(1, 0)`. The movie players and the loading screen
  go through 004EF610 as before.
- The original's "is this holder the primary / render target" checks (005FDA80's window blits, the three movie
  players, UiSurface::CaptureScreen) are gone: no holder stands for the screen. 00504780 (restore after a lost
  device) returns at once. Each Bink frame is copied into memory the renderer owns (`render::MovieFrame`); the
  original's 00784756, written by the game start (00456E35) from the options' driver index, only chose the movie
  holder's memory kind and is gone. Pictures loaded with the video-memory flag
  (004F0060) get the renderer's memory as the system-memory ones do (surface.cpp).
- The harness shows windows without activating them; without DirectDraw nothing activates the game window, so the
  game's active flag (0079BC5C) and the renderer's bit 0 stay 0 and no frame is drawn. The harness sends one WM_ACTIVATE
  (active) once the device object exists. A player's own start is in the foreground and gets the activation from
  Windows.
- Scenarios: render_present, render_movie_present, render_titles_present and ui_saveload.

### R9c: everything the player sees through the new renderer's present

- `src/render/render_window.cpp` (render.cpp keeps `EndFrame` and gives the module its device, context and frame
  through `render_internal.h`): the game window's swap chain. The frame is drawn by a full-screen triangle (shader
  "ei-present") into the client area with its aspect kept, centred, black bars (`render::FrameInWindow`,
  `WindowToFrame`, `FrameToWindow`). A failed `ResizeBuffers` (other than a lost device) makes the swap chain again
  once. A lost device is logged with `GetDeviceRemovedReason` and handled by R9c1.
- Gamma as a shader step: 00504A60 (`renderer_gamma.cpp` SetGamma) keeps the three values, builds the ramp as before
  (BuildRamp, full scale 65280) and hands it to `render::SetGammaRamp`: a 256x1 lookup texture the present shader reads
  per channel (each entry v*255/65280, rounded). Result true, as with a primary that has DDCAPS2_PRIMARYGAMMA. The ramp
  applies only to what is presented, not to the frame (captures, ReadFrame), as the hardware ramp did not change
  surface reads. Harness build: after a ramp change the presented back buffer is read back at a 17x17 grid
  (one-to-one placement only) and compared with the frame through the ramp ("render: gamma ramp check" / "render:
  MISMATCH gamma ramp"). Both read-backs (this and the image uploads below) stall on the GPU, so they are sampled (SV10e,
  `ReadBackDue`): the first 64 of each kind (cursor / fill uploads, movie uploads, ramps), then every 16th; the
  render_* scenarios (not render_stats, which measures) say `checkall` in their `render` line for every one.
- Full screen (device flags bit 0, registry FullScreen 1) without a display mode change (`present_window.cpp`, called
  from each present in device_present.cpp): the WS_POPUP window is moved over the monitor it is on (not while
  minimised; again after someone else resized it), and the frame is scaled into it; a window resized by the user
  scales the frame the same way; a resolution below the desktop's shows in the window.
- The game's mouse coordinates when the frame does not fill the window one to one: GetCursorPos / SetCursorPos (the
  cursor 005E52E0, the global map 0062F050 take screen coordinates as a window at 0, 0 the resolution's size) through
  replaced import slots; WM_MOUSEMOVE .. WM_MBUTTONDBLCLK (client) and WM_MOUSEWHEEL (screen) mapped at the top of the
  input dispatcher (00457970, app.cpp). The game's own wheel message (0xCC54) carries no position.
- Focus loss and return (Alt+Tab, minimise): the WM_ACTIVATE path is the game's own (app.cpp OnActivate): inactive
  deactivates the cursor, the mouse, the sound and the network and minimises (004EEC60's RestoreDisplayMode is gone);
  active restores, and in full screen 00504780 (returns at once) and the interface reset 005DAE70 (cursor rebuilt).
  Minimised the client area is empty and nothing is presented; on return the swap chain is resized.
  DXGI_STATUS_OCCLUDED counts as success.
- Harness steps: `fullscreen 0|1` (starter: the registry's FullScreen), `focus off|on` (WM_ACTIVATE inactive through
  the game's window procedure and SW_SHOWMINNOACTIVE; SW_SHOWNOACTIVATE and WA_CLICKACTIVE), `windowsize <w> <h>`,
  `waitpresent <seconds> [gamma]` (a frame presented on the game window; with gamma, checked against a ramp that is
  not the identity). The mouse steps aim at frame pixels mapped into the window. Scenarios render_present_window
  (windowed 800x600: the gamma slider held, the ramp checked; the window resized to 1600x900 and the options reached
  through the mapped clicks) and render_present_fullscreen (full screen at 800x600 on the desktop's monitor: menus and
  battle screen through the mapping, focus lost and given back, the window resized from outside and covering the
  monitor again). Both set a resolution; they run beside others (our renderer changes no display mode).

### R9c4: no resolution change inside the game

The game never changes its resolution or makes its device again while running, so the new renderer's frame target
keeps the size it was made with and only the swap chain follows the window (R9c):
- The resolution is the registry's `VideoResolution` (with `FullScreen`, `VideoColorDepthInBits`), read once by the
  options load (0045B210, `options.cpp`; 0045C560 writes it back) before WinMain makes the window at that size (the
  other two functions naming the strings, 00459100 and 0045A380, have no caller); WinMain creates the renderer
  once (00500B70 through slot 2 of 0073D93C; its only caller) and ShutDown destroys it (slot 3). 004EDD70 (the device
  object) is called only from 004E5E50, which only 00500B70 calls; 004EEBC0 (SetDisplayMode, not ported) has its only
  caller in 00504780, the restore after focus returns in full screen, which in ours returns at once.
- The options window has no resolution: its 26 settings (table 00787430) are volumes, keyboard and mouse speeds,
  border scroll, rubber band, three shadow switches (7-9: units, buildings, flora; globals 0079BD14 / 18 / 1C), marks,
  footprints, selection type, path, brightness / contrast / gamma (applied at once through 00504A60, R9c), flying HP,
  tutorial, autosave, tooltip time, filters, camera axes, stereo and difficulty. What our code once called the
  "display mode" settings (OptionsWindow +0x410) are the shadow switches: OK (Apply 006524A0) with one changed
  drops the shadows the switches turned off (0056D7A0); switched on again they are rasterised and uploaded anew.
- Scenario `render_options_shadows` (vanilla and Rebirth, golden from the original): in zone1 the three switches
  stepped and applied through the Esc menu's options and OK, then back; the captures show the ruins' shadows gone and
  back. A desktop mode change is left out (no run may change the display mode).

### PF3 (R9b1): no DirectDraw probes at startup

R27e: the version probe (0045E310, `GetDxVersion`) is gone, with WinMain's "This Game requires DX7" check on its answer
(line 0 of res\sysmsg.txt, now unused; the other lines keep their places): the game loads no DDRAW.DLL and starts on a
system without DirectDraw. The probe used to fail now and then with several games on the machine ("DirectX 7 or later"
message), and left DirectMusic loaded through CoCreateInstance, which once mattered to the sound code (a run that
skipped only part of the probe's side effects hung sound init); the full skip did not hang. A timed-out harness run logs the main thread's
instruction pointer and return addresses (`LogMainThreadStack` in harness.cpp); a hang seen once in `AIL_waveOutOpen`
is what to read it for.

### R9c1: a lost Direct3D 11 device made again

- Losses are seen from the swap chain's Present / ResizeBuffers (DXGI_ERROR_DEVICE_REMOVED / RESET / HUNG /
  DRIVER_INTERNAL_ERROR, `render_window.cpp`) and by `GetDeviceRemovedReason` at the end of every presented frame (a
  loss nothing reported, e.g. minimised). Nothing more is drawn in that frame; at the end of `render::Present` (the
  frame's commands cleared, so nothing holds a sokol id), `RecreateDevice` (render.cpp): each module destroys what it
  made (`DropWindow`, the frame target and readback copy, `DestroyTerrain`, `DestroyMeshes`,
  `DropBlits`, `DropTextures`), then `sg_shutdown`, the context cleared and released, the device released (the log
  gives the references left: 0 in the runs), a new device (up to five tries 200 ms apart), `sg_setup` and the
  pipelines. The next frames make the frame target, the swap chain (the game's gamma ramp is kept: the lookup table is
  not reset to the identity when the present pipeline is made), the interface canvases (`DropCanvases`: each made
  again and the commands drawn on it since it was last covered whole drawn again, P8) and the composed and drawn-into
  textures (their recipes drawn again, `RemakeComposed`). A movie shows again from its next frame. No new device: the game ends (section 1, "When the
  renderer fails").
- The decoded textures keep what they were decoded from (`TextureSource`, render_textures.cpp):
  the header and the file's bytes of the levels it holds;
  `SurfaceView` makes the image again from it at
  its next use (`Remake`, once a frame). game.exe is not large address aware: this is address space the switch-over
  freed by dropping the DirectDraw textures.
- Test: `render::LoseDevice` (harness step `losedevice [seconds]`) makes the next swap chain Present report
  DXGI_ERROR_DEVICE_REMOVED without calling it; the rest is the real path. The step waits until the device is made
  again and a frame presented on it. Scenario render_device_lost (vanilla and Rebirth): at one frozen moment in the
  main menu, in zone1 and in the camp (textures loaded after the first loss), the frame before and after the loss match
  (samecapture: 0 pixels over 8). A real removal: R9c5 below.

### R9c5: a real removal of the device (harness build only)

- `removable` in EI_RENDER (the scenario's `render new,present,removable`; known only to the harness build,
  `EI_RENDER_VALIDATION`) makes the Direct3D 11 device through `D3D11On12CreateDevice` on a Direct3D 12 device of the
  default adapter and a direct queue (d3d12.dll loaded at run time from System32; nothing installed). The swap chain,
  the gamma ramp check, the cursor's deferred context and every frame work on it as on the hardware device.
- `render::RemoveDevice` (harness step `removedevice [seconds]`) asks for a removal; it happens right before the
  next swap chain Present (`RemoveIfAsked`; without a swap chain at the end of the frame): the GPU is let finish
  (an event query, at most 2 s), then `ID3D12Device5::RemoveDevice` removes the Direct3D 12 device, so Present and
  `GetDeviceRemovedReason` report DXGI_ERROR_DEVICE_REMOVED for real, in this process only (no driver reset, no TDR;
  other processes and the desktop are not touched). R9c1's path then releases everything (0 references left; the
  Direct3D 12 device and queue too, so the next `D3D12CreateDevice` gives a new device and not the removed one:
  Direct3D 12 devices are one per adapter while one lives) and makes a new removable device.
- The step checks that the loss seen was that removal (not simulated, `GetDeviceRemovedReason` 887A0005) and that the
  new device is removable again; `WindowPresents()` gives the last loss's result and reason.
- Rule from a real bug: removed with GPU work in flight, the old device's teardown crashed in a thread of
  d3d11on12.dll (a null fence in its command list manager) or of DXGI / the driver's user-mode part (nvwgf2um.dll), also
  in a small program that presented on its own (cloaked) window and released everything in order (10 runs of 10); with
  the GPU idle first it never did (the game's runs, that program 10 of 10). So the removal waits for the GPU; a real TDR goes through the kernel
  and not through this teardown.
- Scenario `render_device_removed` (image golden, vanilla; also runs on Rebirth): render_device_lost's three moments
  (main menu, zone1, the camp) with `removedevice`: the frame before and after each removal match (0 pixels over 8).

### R9c2: the software cursor between frames

The original's cursor: the renderer's +4 bit 0 is the game's active flag (WinMain's activation, app_main.cpp), and with
the options' FpsIndependentCursor (registry FPSIndependentCursor, options +0x80; the test machine's value is 0) the
cursor runs in mode 2 with a thread (005E5690, started through 005E52A0, mode set by 005DAEA0) that wakes every 41 ms
and, when the image is shown (+0x0C) and 005E52E0 says the cursor moved or its frame stepped, restores the old place's
background and blits the composed cursor at the new place: about 24 Hz whatever the frame rate. It draws nothing while
the image is hidden: every movie player (005DB3F0, 005DBA50, the titles 0062B7D0) and every loading screen step
(005DA400) call 005E5270 0 first and show it again only at their end, so during movies and loading there is no cursor.

Ours is a present from the cursor thread, not a hardware cursor (that would move with the Windows cursor while the game
holds its own still (005E67B0, the camera turned with the right button), is not scaled with the frame in a larger
window, needs a new HCURSOR per animated frame, ignores the gamma ramp and is invisible to captures). The design is in
the header of `render_cursor.cpp`; the rules worth knowing:
- The cursor thread makes no sokol call (a deferred Direct3D 11 context with the raw objects of sokol's pipelines,
  executed with RestoreContextState so sokol's state cache stays right; ID3D11Multithread protection on).
- The swap chain is shared: the main thread's window present and the cursor thread's take turns under one lock
  (`BeginWindowPresent` .. `EndWindowPresent`); a lost device drops the cursor's references under it first
  (`DropCursorPresents`, R9c1). Lock order: the cursor's own lock (005E65F0, held around the frame draw and the present
  in mode 2) before the present lock, in both threads.
- render::Cursor keeps the cursor aside (`Frame::cursor`); the frame target never holds it. The present shader
  (`present.hlsl`) draws the cursor over the frame as it goes through the gamma ramp into the back buffer (its texels
  with alpha of a half or more replace the frame's pixel; where the cursor touches the bilinear footprint the filter
  is done by hand over the composed pixels, so a scaled window filters the cursor with the frame as before). The cursor
  thread runs the same draw (the same raw pipeline) with its own cursor position over the frame target: no copy of the
  frame (R16f; there was a whole-frame copy a frame, 33 MB of GPU traffic at 4K). So that the target is whole when it
  reads it, everything that draws into the frame target runs under the present lock (`EndFrame`, and `DrawPendingAlone`
  for the passes of a copy in the middle of a frame, SplitFrame) and `BeginWindowPresent` drops what the cursor thread
  draws with until the frame's own present: during a frame that drew a pass early (a copy in its middle: quick save),
  the cursor does not move between frames. `CursorBetweenFrames` presents only when nothing was presented on
  the game window for 40 ms (real time: the harness's virtual clock does not reach the renderer).
- The cursor's texels are the part `from` of its picture's image (section 8, R32): the cursor thread takes the
  picture the last frame drew (`SetCursorSheet`, its shader view referenced) and presents only when the call names
  that picture; DropCursorPresents and HolderGone let it go.
- Harness build: every frame the cursor thread presents is read back from the back buffer and compared with the
  frame target, the cursor's texels (read back from the picture's image) and the ramp as uploaded (one to one placement; "render: cursor between frames: N
  presented frames checked, M differed" / "render: MISMATCH cursor between frames"). The ramp check of the main
  thread's present (`CheckRamp`) leaves out the points around the cursor.

Harness: `mouseglide <ms> <x0> <y0> <x1> <y1>` (the harness's mouse, what GetCursorPos reads, moved by a thread of its
own in 10 ms steps), `stall <ms>` (the main thread away: one long frame), `cursormark`, `cursorcheck <name>|- <min>
[max]` (presents by the cursor thread since the mark within min..max; with a name the last presented frame (after the ramp) goes to
frames\<name>.bmp, the checks must have found no difference and its rectangle must sit at the mouse less the hot spot
within 24 pixels), and the starter step `fpscursor 0|1`. Scenario `render_cursor_present` (on `realtime`: on the
virtual clock other threads see the main thread's clock plus the real time since it last moved and never go back, so
after a movie or a load the cursor thread's clock stands still until the main thread's frames catch up).

### R9c3: the Scroll Lock screenshot and the movie recorder from the new frame

`surface_capture.cpp` (006D6D40, which 006D6FC0 and both users call): the holder (the render target or the primary)
gets its bitmap from `render::ReadFrame` (the last frame `EndFrame` drew,
before the gamma ramp and the present's cursor): a Dib of the rectangle's size in the holder's depth with the channel
masks `device::ScreenFormat` gives, rows bottom-up, the part outside the frame black. No frame yet: no bitmap (logged
once). The callers are unchanged, so the file names (`shot%.4d.bmp` from counter 007C7E24 for Scroll Lock; the
recorder's `<moviepath>shot%.4d.bmp` from 0079BC58, the movie folder defaulting to `.\capture\`), the format
(Dib::Write) and the timing are the original's. The recorder wrote through a null bitmap when there was none
(`bitmap->Write` before its check): checked now. The recorder's flag (0079BC54) is read by WinMain and the frame
function and cleared on a write error; nothing in the shipped game's code sets it, so it is unreachable for a player.
The pixels are checked by the harness: `key scrolllock` (scan 0x46), `record on|off` (sets 0079BC54), `checkshot <n>
<frame> [tolerance] [fraction]` (reads `shot<n>.bmp` from the saves overlay and compares it with frames\<frame>.bmp as
samecapture does). Scenario `render_present_shot`: Scroll Lock in the main menu and in the game at a frozen moment,
then the recorder for a few frozen frames. A save's picture: `checksave <slot> <frame> [tolerance] [fraction]`
(`<slot>\shot.sav` against the frame as 8x8 block averages; scenario ui_quicksave, section 6).

### R10: runs of our code on our renderer, dgVoodoo for the original code only

User's decision (2026-10-15): test runs of our code draw with our renderer, mainly because dgVoodoo runs that set a
resolution must have the machine to themselves and serialised the suites.
- The default suite is the standalone image's (`our_code()` in tools/run_all.py: installs `standalone` and
  `standalone-rebirth`), always `EI_RENDER=new,present`. dgVoodoo draws only the original code (`--no-hooks`,
  `--widescreen --record`'s rebirth-nohooks). A run of the original code never gets EI_RENDER (run_once drops it).
- Alone (`runs_alone`): only a dgVoodoo run with a `resolution` step. Plans never hold a run that would switch the real
  display (main's `switches_display`: `resolution WxH` with `fullscreen 1`): such a scenario runs only on our renderer
  (a `# no original: <install>` line leaves its `--no-hooks` runs out).
- Reference cache (`reference_key`, `store_reference`, `reference_from_cache`): a passed run of the original code is
  kept in `<main checkout>/build/reference/<scenario>-<install>/<key>/` (the run folder without harness_saves, plus
  cached.json with run()'s results), the key being the scenario's last commit (`work.scenario_revs`), the install's
  files (run/<install>: names, sizes, times; dgVoodoo.conf by content), the tools of its `# prepare:` lines, the clock
  override and the game's EI_* settings. A scenario with uncommitted edits is never cached. `run()` copies a cached run
  into the run folder (a note "reference cache: <key> ..."; no time recorded); work.py test gives such a run alone = 0
  and expected 1 s. Three keys kept per scenario and install. `--fresh` (run_all, compare.py; compare.py always with
  `--runs` > 1) plays it again. Not in the key: the harness itself (the harness build): after a harness change that
  alters what a reference run captures, `--fresh`.
- `run_all --widescreen` compares the image's frames with addon's recorded ones (docs/notes/rebirth.md).
  compare_frames.py runs 8 at once (`COMPARE_JOBS`); the widescreen scenarios' 4K captures take 45-60 s beside other
  games (`timeout 90` in ui_widescreen_hud / _more / _screens).

## 10. The old boundary (reference)

- Imports: `DirectDrawCreateEx` (always `IID_IDirectDraw7`), `DirectInputCreateEx` (the mouse), 12 Bink functions;
  `2DintMMX.dll` / `3DfpFPU.dll` loaded by name (00500540, 004EDD70, 004F0B90). The game transforms, lights, clips and
  fogs on the CPU (3DfpFPU) and draws pre-transformed vertices (FVF 0x1C4 / 0xC4) as triangle lists, a few strips and
  line strips; `DDSCL_FPUSETUP` sets the main thread's x87 precision to 24 bits (rebirth.md). Clipper and vertex buffer
  code is dead.
- Objects: renderer 007AF060 (vtable 0073D93C: slot 0 fog colour, 2 create, 4 / 5 begin / end frame, 8 clear, 11 set
  texture, 12 draw TL triangles, 16 ribbons, 17 sprites), device object 007AF080 (0x17C bytes: +0xC IDirectDraw7, +0x14
  primary, +0x18 render target, +0x54 IDirect3DDevice7, +0xAC transform pipeline, +0x14C current material), surface
  wrappers (DXSurface, 0x20 bytes, +0x10 the surface), device lock 007AEE78.
- **3DfpFPU.dll** (`GetTLPipeline`, 45 slots): projection with outcodes, homogeneous clipping (slots 36-38), viewport
  mapping (31-34), vertex fog into the specular alpha (35), lighting into packed colours (39-44), bounds (6, 7, 29).
  **2DintMMX.dll** (`GetGraphicsProcessor`, 13 slots): 8888 to 16-bit conversion (1, 2), skin compositing blends (3-5),
  premultiply (6), mip downsampling (7), shadow triangle rasterisers (8, 9), a 16-bit block operation (10), shadow blur
  (11, 12). Both DLLs are identical in the GOG and Rebirth installs. The rebuild loads neither: 2DintMMX since R28,
  which took its last uses (composition, picture unpacking, 16-bit packing), 3DfpFPU since R31 (below).

### R31: the projections in plain C++

`projection.cpp` holds what the game still called through 3DfpFPU's transform object, as members of
`TransformPipeline` (model.h) and two free functions: `ToClip` (clip space, the translation added first in each row;
the decals' TransformPoints, slot 26, is this with the matrix untransposed), `ProjectClipped` / `ProjectPointsClipped`
(slots 9 / 8: an outcode per point, `clip.h`'s `ClipCodeOf`, and the batch's verdict, `ClipVerdict`; on screen the
points go on to `ToScreen`), `Project` / `ProjectPoints` (14 / 13, the screen matrix's division), `ProjectSprites`
(24), `ToScreen` (32; ToScreenClipped 34 is the same call now), `FogVertices` (35), `ClipBounds::Fit` (7) and
`ProjectFlat` (29, the shadows' flat projection). The pipeline keeps the viewport's half size, the fog's range and the
projection matrix itself (`SetViewportSize`, `SetDepthRange`, `projection`). Gone with the DLL: the loader (004F0B90,
CPUID, the "FPU Transform release library loaded." debug line and the "can't load" errors), the calls nothing read
back (`AddSun` with its counted hold on the sun, `SetAmbient`; LightMesh had no caller left), and the terrain's own
projection of every sector vertex (18 / 19) before its quarters were tested.

Verification: a stand-alone program built with the game's flags calls the DLL and the new code on the same random
inputs at 24-bit precision (3000 rounds: verdicts 0, 1 and 2 all reached, every slot above): bit-identical results.
The plain float sums in the DLL's order are the same x87 code at that precision.

Behaviour differences (one path for every input; none of them is in a golden):
- A terrain quarter is culled only by its box's `ClipTest`. The DLL also projected the sector's vertices and culled a
  quarter whose vertices were all outside one plane; now such a quarter is drawn and the GPU clips it (the box test
  already culls most of them; the quarter mode only tells the renderer whether the page crosses the view).
- Ribbon and sprite points at w exactly 0 map with 1 / 0 (infinities) where the DLL's clipped screen mapping used
  1e6; the GPU clips them either way.
- The sprites' matrix is the clip stack's top as it is; the original transposed it through the FPU first, which
  quieted a signalling NaN (no matrix holds one).

### R31a: sprites clipped by the GPU

The sprite draw (`renderer_sprites.cpp`, 00502CA0) was the last draw clipped on the CPU: ProjectSprites wrote an
outcode per corner and a crossing batch's triangles went through the screen clipper (clip.cpp, 0042D910..0042E4D0)
before the TL draw. Now every batch is two triangles per sprite and the GPU clips x and y. A TL draw keeps z inside
the depth range (the shader clamps it, as Direct3D 7 drew pre-transformed vertices), so the depth range is the
sort's test: a sprite is kept when its z (all four corners share it) lies in 0..1, which is what the clipper's two z
planes left of a sprite. Behind the camera 1 / w is negative and z / w lies above 1 (the projection's z row is
Q (z - n) with Q = f / (f - n) above 1), at w 0 it is infinite or NaN: both dropped.

Behaviour differences (one path for every sprite):
- The original's outcodes moved the centre's clip x and y by `size` / w where the corners lie `size` clip units
  away (a factor w short), so a sprite whose centre was just past an edge counted as outside although its quad
  reached onto the screen: the clipper dropped it (sprites popped in and out at the screen's edges), and a batch whose
  points all lay past one edge was not drawn at all. Those sprites are drawn now. Measured with a temporary A/B
  switch in one frozen moment (ten fire and smoke sources around the hero, vanilla): 15 crossing batches, 180
  on-screen sprites the outcodes dropped; the frames differ on 4.3% of the pixels (0.12% by more than 8, largest
  36), along the top and bottom edges where smoke and fire cross them.
- Sprites past the far plane are dropped before the sort, not by the clipper after it: the merge sort's order of
  sprites at exactly the same depth may differ in such a batch.
- A sprite at z exactly +0 is drawn (the clipper's near plane kept z above 0 out when the batch crossed).
- A call with more than 511 sprites lost the colour and texture coordinates of some corners of its second and later
  batches (the corner vector was resized after they were written, zeroing what lay past the last batch's kept
  sprites): each batch is sized before it is filled now.

## 11. Verification and frame capture

- **Frames are checked within one run**, at a frozen moment: `samecapture` (frames_basic, render_scene, render_present,
  render_device_lost, ...: two captures in one frozen moment must match; on the image they match to the pixel, PF3a),
  `tools/frame_preview.py` writes a downscaled side-by-side PNG. Cross-run comparisons are not meaningful (no
  determinism); A/B is always within one run.
- The water pass writes depth as the old device does (ZWRITEENABLE stays 1 after the sky): without it the far sea's
  last, fully fogged sector ring blended over the nearer water at the top of the screen (a light strip with a straight
  lower edge, seen right of the top panel in WideScreen; R10c).
- The terrain's pipelines come from the pass state at each page's hand-over (`ReadPassState`, R10d), as the meshes'
  do: ground under the terrain block 007AEDDC and water under the scene block 007AEDD0, both with CULLMODE 2 (what the
  sky leaves), ALPHABLENDENABLE 1 (SRCALPHA / INVSRCALPHA) and ZWRITEENABLE 1. The terrain shader still picks the
  alpha (texture for ground, vertex colour for water) and always adds the specular colour (DrawSectorPage turns
  SPECULARENABLE on around each of its draws, after the hand-over). The harness build logs each new terrain pipeline's
  state ("render: terrain pipeline (first for a ground page): ..."). `--widescreen` matches addon's frames with it.
- **Terrain sampling (R12)**: the pages are sampled anisotropically (16x) with linear mips (`AnisotropicSampler`,
  render_textures.cpp; logged once per sampler) when stage 0 asks for linear filters (the options' Filtering 1 / 2; point filters
  stay point, Mipmapping 0 keeps level 0). The game asks for D3DTFP_POINT mips: through them whole terrain
  triangles snapped to the next level (a triangle's level follows its texture-coordinate slopes, which change from
  tile to tile) and the sharpness jumped along the tiles' edges: the seams the user saw moving with the camera. The
  harness's original (run\vanilla's dgVoodoo, Filtering appdriven) shows the same steps; the user's own original
  (Rebirth's starter\Engine\dgVoodoo.conf: Filtering 16, Antialiasing 8x) does not, and that is the look kept. So the
  image's terrain differs from the harness original's where a level changes (about 0.5% of a zoomed-out frame).
- Check (scenario `render_terrain_seams`, verify build): `terrainlevels on|point|off` (`render::TerrainLevels`: the
  ground pages draw a texture of the page's size and levels with one grey per level, marked green / blue, through the
  shipped sampler or the old point-mip one) and `levelsteps <frame> <least> <most>` (`capture::LevelSteps`: the
  fraction of neighbouring level-map pairs whose level jumps). Two frozen views of zone1 in the image's 1280x720
  frame (H31; the steps are lines, so the fraction grows about tenfold from 3840x2160's), vanilla: 0.04% / 0.06% with
  the shipped sampler (limit 0.3%), 2.9% / 3.5% through point mips (at least 1%, so the check sees seams); Rebirth's
  wide view, further and steeper: 0.96% / 0.66% (limit 1.6%; a triangle's level still jumps where its slopes do, as
  under dgVoodoo's 16x) against 3.8% / 3.7% (at least 2.4%): only four times apart at this size (ten at 3840x2160).
  The original's run captures its frames and skips both steps.

- Check (R12b, same scenario): `mmplevels <most>` (`render::CheckMmpLevels`, verify build): every held .mmp texture's
  levels, decoded as drawn (a DXT texture's read back from the GPU: `ReadImageLevel`, render_compose.cpp), against its level 0, both box-filtered to 4 x 4: the largest mean difference per
  channel. The file's own mips reach 52 (1-bit alpha small levels); the overflowing offsets gave 128 / 149 (vanilla /
  Rebirth); limit 80. Against the harness original's `far` frame, the tree crowns (2750, 550)-(3250, 950) at
  3840 x 2160: 1.30% of the pixels differed (tolerance 8, shift 1; largest 150) before, 0.24% after (leaf edges).

- **Render quality (R12a's smooth option; R33 split it into two levels)**: the world view multisampled (MSAA) and its
  models sampled anisotropically, each with its own level, as the user's Rebirth dgVoodoo setup draws at the most
  (starter\Engine\dgVoodoo.conf: Filtering 16, Antialiasing 8x) except for the interface.
  - **Settings** (user 2026-10-29: from the file, separately, with levels): in ei_settings.ini next to
    ei_standalone.exe (play.cmd's is run\play\ei_settings.ini), the key of the overlay and vsync (section 15):
    ```
    [Software\Nival Interactive\EvilIslands\Standalone]
    "Msaa"=dword:00000008          1 (off), 2, 4 or 8
    "Anisotropy"=dword:00000010    1 (off) .. 16
    ```
    Read once the renderer starts (`QualitySetting`, device_object.cpp, after `render::StartPresenting`); a value
    missing or not a DWORD is written with its default (8 and 16: the look play.cmd's `smooth` gave before R33), so
    the file always shows both (a new file has them already, with "VSync" 1: settings_file::DefaultDocument, S24). `render::SetMsaa` rounds down to a power of two up to 8 (`kSmoothSamples`), and the
    target takes at most the device's samples; `render::SetAnisotropy` clamps to 1..16 (`kMostAnisotropy`). Both from
    the next frame (ST2's options screen can call them); `Msaa()` / `Anisotropy()` read them. **Never in a harness
    run**: `render::QualitySettingsSupported()` is false in the harness build (the pinned settings file lacks the
    values, a read of a missing value fails a run), so a run starts with both off (1) and goldens and frame checks
    keep that look; the harness steps `msaa <samples>` / `anisotropy <level>` set them. EI_RENDER's `smooth` is gone.
  - The world view (`WorldClient::Render` 00569E40 between `render::BeginWorld` / `EndWorld`: sky, terrain,
    objects, shadows, water, effects, flying hit points) is a range of the frame's commands (`Frame::worldRanges`).
    With anisotropy above 1 its models (`MeshBatch::model`: BeginMesh draws, not the pre-transformed ones) take
    `AnisotropicSampler` at that level for both stages (point filters stay point, Mipmapping 0 keeps level 0); the
    interface's sprites and previews keep `StageSampler`. The terrain pages always sample anisotropically at 16,
    whatever the setting (their seams, above).
  - With msaa above 1 the world's runs of commands draw into a multisampled colour and depth target of that many samples
    (fewer when `CheckMultisampleQualityLevels` refuses the device's most for RGBA8 or D32, none when the target cannot be made: logged),
    and each such pass resolves into the frame target (sokol's resolve attachment) at its end; the other runs draw
    into the frame target as without the option, so the interface's edges and window surfaces are unchanged (a 3D
    draw's edges run through pixel centres, D3D7's convention: multisampled, every sprite would get half-covered
    border pixels). Between runs (render_smooth.cpp): after a world run the frame target's depth is set from sample 0
    of the multisampled depth; before a world run that follows other draws the frame target's colour and depth go
    into every sample (a pass of its own: the resolve's destination cannot be read in the pass that resolves into
    it). The frame target is what every reader takes (the present, the cursor thread's copy, `ReadFrame`,
    the canvases' screen copy, the gamma ramp check), always resolved: a pass ends with the resolve, and a copy between frames
    (R13) finds the last frame there.
  - Every pipeline drawing into the frame is made for the pass's sample count (`detail::PassSampleCount`; mesh and
    terrain pipelines keyed by it, the blit and depth clear made for it at first use): sokol's validation skips a
    draw whose pipeline's count differs from the pass's. A change of the sample count remakes the targets at a frame's
    first pass.
  - Check (scenario `render_smooth`, image only, vanilla and Rebirth): msaa 8 and anisotropy 16 from the start; in
    zone1 at one frozen moment the frame with them, with both 1 (`plain`) and with them again: the two with them
    match (samecapture); `smoothcheck plain smooth 0.002 0.2` requires the levels drawn (the world multisampled with
    as many samples as asked, at most the device's, 8 here; anisotropic world models only when anisotropy is above 1)
    and the frames to differ in 0.2% .. 20% of the pixels (a black or stale frame fails); the quick save's picture
    (`checksave`, R13) matches the smooth frame. R33's levels, each against `plain`: msaa 4 with anisotropy 4, msaa 2
    alone, anisotropy 8 alone (1280 x 720: 3.24% of the pixels differ at 8 / 16, 3.18% at 4 / 4, 0.70% at msaa 2
    alone, 2.56% at anisotropy 8 alone; each required 0.2% .. 20%, anisotropy alone 0.5%). `renderstats` lines end with
    the levels and the samples drawn.
  - The copy into every sample (R12a1): the game never reaches it (its world view is a frame's first draw), so the
    harness build's `smoothorder before|split` (`render::SetSmoothOrder`) draws the frozen frame's passes in other
    orders: an empty frame pass before the world (copies the cleared target in), or the world's first run cut at
    its middle command with an empty frame pass between (depth back from sample 0, then the resolved first half and
    that depth into every sample); `smoothorder check` requires the copies counted (`SmoothInfo::copiesInto`,
    `depthCopiesBack`). Before matches the game's order to 1 (the noise between two captures of the same frame);
    split differs in 0.02% of the pixels (> 8), only along edges where both halves meet (water against the
    terrain's edge: those pixels' samples get sample 0's depth and the resolved colour, by design). A copy shifted
    by 3 pixels gave 10%, the depth not copied (1.0) 37%.

### Frame capture

Harness. `capture <name>` skips two presented frames and writes the next to `frames\<name>.bmp` (24-bit). In the image
it is the new renderer's last frame (`render::ReadFrame`: before the gamma ramp and the software cursor, which the present draws). In a run of
the original (`--no-hooks`, dgVoodoo) it is the old render target at the present point (after scene and interface,
before the software cursor and the flip; a read-only Lock of the render target, GetDC as fallback: a GDI capture of
the window comes back black with dgVoodoo). `freeze` / `unfreeze` hold timeGetTime / GetTickCount and the camera
(Camera::Update at 00457595 runs once per frame even with dt 0 and moves a fixed fraction per call); the main loop
then runs no world, client or camera update. `samecapture <a> <b> [tolerance] [fraction]` fails the run when two
captures differ. `tools/compare_frames.py A B [--mask M1 M2 [--mask-background]] [--exclude E1 E2 ...]
[--exclude-grow N] [--diff OUT]`: per-pixel tolerance (default 8), differing fraction (default limit 0.1%), worst
regions. Limits: what moves by itself between frames without the game's clocks (other threads) is not held; the
harness's own waits use real time.

### A-E1: line lists (line_batch.cpp)

`LineBatch` (renderer_lines.h, 0x20 bytes): points four to a packet (x[4], y[4], z[4], w[4]) in an aligned array, a
count and a colour. `AddPoint` (0049B140) appends a point with w 1 and repeats it through the rest of its packet,
growing the array by one packet when full. `Draw` (0049B780) resizes the two work arrays (the original's
projected points 007A9CB8 without keeping the contents, the outcodes 007A9CC8; plain globals of ours), projects with the top of the pipeline's
clip stack through the pipeline's `ProjectClipped` and, when that returns 0, hands the points to
`DeviceDrawLineStrip` (0049B4B0); it never touches the Direct3D device itself. Returns 1. Users: the figure parts'
debug bounds (005A7290) and the world client's line lists (00569D60); no scenario turns either on, so nothing reaches
it.

## 12. A-S11: what is left of the old renderer's range (0x004C0000-0x00500000)

A-S11 classified the 173 game functions of the range that were not rewritten; the plain game code of the range (text
manager, script loader, `CSysInfo`, the fog manager) is in script.md (A-S11). All direct DirectDraw / Direct3D 7 calls
(class c) were rewritten (A-S12 / A-S13) through the typed interfaces and are gone since R27c (no device, section 9);
the COM objects were the boundary the new renderer replaced. What is not rewritten:
static initialisers and what only they reach, one MFC TRY/CATCH, a few renderer-interface functions, library rows
(`config/library.csv`, noted "A-S11"), dead rows (`config/dead_code.csv`: 004DABC0, 004E7FB0, 004EE760, and 004F0A20 /
004F0A90 / 004F0AE0 called only by 004EE760; the display mode callback 004ED7F0 too), and functions under 5 bytes
(cannot be hooked; 004F0B00 is the DirectDraw error reporter, a bare RET). 004EE2F8 is not a function (the common exit
of 004EDD70); 0049F7B1 is the tail of `IsaacRandom::Seed`.

File map of the device layer (all in `src/game`; the files' headers name the addresses):
- `device_binds.cpp` (texture binds 004EB300 / 004EBAF0 / 004EB1D0: SetTexture when the stage's holder changes, the
  batch flushed first; batch flushes 004E7840 / 004E7A40: empty, nothing is batched); `device_object.cpp` (the
  device object: the DirectDraw base 004EDD70 / destructor body 004EE330 and the device's constructor 004E5E50 /
  destructor body 004E68A0 as one class (the four stage-0 state blocks at 007AEDD0..DC are section 5's constants);
  004EECF0 / 004EEC60 (cooperative level, display mode restore), the driver list 004EE4A0 / 004ED610 / 004EE5D0,
  004EEA90, 004EEBC0, 004EEE30, the depth buffer 004E6C30 / 004E6BC0, the device callback 004ED820 and the format
  table 004E6EE0 are not ported; `dx_errors.h` throws the game's error object through `_CxxThrowException` 006E7819);
  `surface.cpp` (the holder 004EF7B0 / 004EFC60 / 004F0060 / 004EFD20, the channel split 004EDAA0 / 004EDC00 /
  004EDC70); `texture.cpp` (the texture formats in place of the lookups 004E7DA0 / 004E7C40); `device_helpers.cpp` (counted holders, list iterator copies, aligned blocks, deleting destructors, the
  axis swap matrix 004F1330, which Rebirth WideScreen patches: vanilla launch only); `render_helper.cpp` (`RenderHelper`'s
  built-in presets); `projection.cpp` (the transform pipeline's projections, R31, section 10); `link_mesh_draw.cpp` (LinkMesh slots 7-14, 004F3870..004F4850); `weather_particles.cpp` (rain
  004E4240 and snow 004E4C80); `effects_weather_draw.cpp` (visibility fog draw 004E1EB0, rain / snow buffers 004E3C20);
  `device_bounds.cpp` (screen bounds 004E86B0 / 004E8900); `link_loader_create.cpp` (`LinkFigureLoader` constructor
  004F68C0, vanilla launch only: ResOverride patches it).
- Weather: 81 x 81 cells around the point the wind carries a particle to the camera from; a cell's hash and the
  world's time give the particle's cycle, fall and texture frame; corners in packets through `ProjectClipped` (stack
  2), the ones on screen through the pipeline's `ToScreen` and, with the device's fog flag, `FogVertices`, then `render::TlDraw`. Snow sways by five tables of cubics (007ABB98 .. 007AE398, built by the
  static initialiser 004E3B20 through 004E3980; evaluated with 004E5660).

Verification: the goldens (the weather draws' scenes: rain in many scenarios, snow in rebirth_flags via `SetWeather`)
and the function vectors; these functions run in the image like the rest.

Original details kept: the T&L HAL device is never used; the rain's
and snow's vertex colours and packet w are never written (the buffer's old bytes are drawn, the fog pass only lowers
the specular's alpha); the rain's height uses the snow's rise (007AEDA0, both 40); the helper's destructor walks on from
a record it has just erased (reads freed nodes, so its walk over more than one record differs in ours); slot 1 tested a
texture operation against the capability bits with `&` (operation number, not its bit; gone with the caps, R27c). Different in ours: a throw from the constructors does not free what was built; the
driver record's unset GUID and monitor bytes are zeroed; fields the original leaves as stack contents are zero.

## 13. R16: what a frame costs, and the plan to draw from GPU-resident data

User's direction (2026-10-20): stop matching the old renderer's calls; composite in shaders, keep data on the GPU,
re-upload only what changed. R11d's blink (`render::TexturePatch`: a shader patch instead of a texel copy) is the
pattern.

**Measuring.** `render::FrameStats` (render.h) counts per presented frame: the hand-over (terrain pages and batches,
meshes and passes, flat geometry, pre-transformed draws, blits, depth clears, state reads, matrices), bytes appended to
the vertex / index streams, uniform bytes, passes, draws, pipeline and first-texture changes, images made per kind
(.mmp, window surfaces, movie, small) with their bytes, and the renderer memory compared
with snapshots. The harness build adds the CPU time (hand-over, texture work, EndFrame; a nested timer's time counts
only for its own field) and each terrain page's / mesh's vertices against the last frame's (same; only colours
changed = relit; positions or texture coordinates changed = moved; new), plus the meshes whose geometry equals
another's in the frame. The harness step `renderstats <label>|-` logs the averages since the last one into the report;
scenario `render_stats` (image only) measures the main menu, zone1, a fight of twelve creatures (GodMode hero), the
camp, the base bz1g (a town), the global map and its escape menu. It also logs the mesh draws whose positions an
animation's per-vertex offsets moved ("morphed meshes", R16h) and the main thread's CPU and wall time per frame since
the last `renderstats` (its cycles, in milliseconds by the thread's CPU time, as the TIMING lines): the game's work and
the renderer's together, what a many-figure frame costs; R16k added "device mesh draws", the old device path's own
time in its mesh draws 004E9010 / 004E9A50 (projection, clipping, passes; the hand-over in them left out). `EI_PROFILE=1` gives the game side
(tools/profile_report.py). RenderDoc was not needed: the counters see the hand-over, which a capture does not.

R16's starting point (2026-10-20, before R16a: the old shadows, the CPU lighting and the window surfaces' uploads
still ran), per frame, vanilla, 3840 x 2160 (the harness's desktop size; Rebirth's runs are within a few percent;
times from a run without the profiler):

| Scene | terrain pages: same / relit / moved | meshes (flat) | draws | pipeline / texture changes | upload KB: terrain / meshes | images made again | memory compared | CPU ms: hand-over / textures / EndFrame |
|---|---|---|---|---|---|---|---|---|
| menu (3D scene) | 15: 8 / 4 / 2 | 166 (12) | 174 | 8 / 32 | 268 / 546 | 2.3 shadows | 5.6 MB | 0.46 / 0.87 / 0.46 |
| zone1 | 54: 28 / 15 / 5 | 145 (13) | 165 | 12 / 50 | 808 / 277 | 3.9 shadows | 4.7 MB | 0.61 / 0.93 / 0.51 |
| fight | 54: 28 / 15 / 5 | 350 (32) | 370 | 12 / 57 | 808 / 500 | 11.2 shadows | 4.7 MB | 0.67 / 2.48 / 0.83 |
| town (bz1g) | 49: 28 / 13 / 3 | 135 (27, 344 KB) | 151 | 8 / 59 | 372 / 2310 | 14.5 shadows | 0.2 MB | 1.64 / 2.47 / 0.93 |
| camp | 0 | 95 | 118 | 6 / 27 | 0 / 76 | a 4 MB window surface every 15 frames | 12.3 MB | 0.12 / 2.36 / 0.42 |
| global map | 0 | 23 | 25 | 2 / 7 | 0 / 181 | its 341 KB map texture every frame | 0.6 MB | 0.16 / 1.16 / 0.22 |
| escape menu | 0 | 12 | 14 | 3 / 4 | 0 / 105 | none | 32.4 MB | 0.05 / 2.94 / 0.12 |

Meshes against the last frame: zone1 112 same / 22 relit / 11 moved, fight 131 / 199 / 19 (132 share another mesh's
geometry), town 52 / 60 / 23 (the moved ones are the classic shadows' decals, projected again every frame; with the
shadow map zone1 1, fight 2, town 1). A shadow texture is 64 x 64 with mips (21 KB). Game side (profile, zone1, about 13 ms a
harness frame): terrain lighting (`SectorData::Light` / `ShadeSun`) about 16%, object shadows (`DrawShadow`: CPU raster
and decal projection) 10-17%, the renderer's `MemoryChanged` 5%.

What the numbers say: draws (14-372) and state changes (2-13 pipelines, 4-66 textures) are cheap; the cost is CPU work
around the GPU: comparing and converting the renderer's memory (window surfaces compared at every blit: up to 32 MB a
frame at 4K), shadow textures made again as new images (4-15 a frame), and every vertex built and streamed again each
frame though most of it did not change (0.5-2.7 MB a frame).

**Plan**, by value for effort (tickets R16a-R16i, renderer group). Frame checks compare A/B frames of one frozen moment
in one run (a switch between the old and the new path; `samecapture`, tools/compare_frames.py) with a tolerance, never
across runs; the goldens check the game state.

1. **R16a Window surfaces by change marks, partial uploads (done).** How it works: section 6, "Change detection".
   Measured with render_stats (vanilla, 4K; `renderstats` "window surfaces" ms, the window part of "textures"), before
   / after: menu 0.5-0.8 / 0.004 ms, zone1 0.4-0.6 / 0.2, fight 0.7 / 0.2, camp 1.6 / 0.6, escape menu 2.3-2.8 /
   0.006; memory compared menu 5.6 MB / 0.1, zone1 4.7 / 2.9, camp 12.3 / 7.9, escape menu 32.4 / 0 (Rebirth alike).
   What is still compared is the surfaces the game redraws every frame (the HUD unit panel, the item strip, the camp
   page: GDI's bounds cover them whole, and 91-99% of the redraws change nothing, ui-census.md); the native interface
   (ui-native.md, P1-P8) removes those. Checked by the ui_* and render_* scenarios (captures, 0 missed writes).
2. **R16b Shadow and memory textures updated in place (done).** How it works: section 7, "Memory textures updated in
   place" (each holder's own texture written with UpdateSubresource, levels by GenerateMips, read again only when
   marked written; no texture array: not needed for the gain). Measured with render_stats (vanilla, 4K; per frame;
   "textures" CPU ms, of it window surfaces in brackets; the machine shared with other workers' runs, so noisy):
   main's last run before / after / the same run with `memorytextures remake` (new images with CPU levels as before,
   but already with the 16-bit table): fight classic 20.2 (1.4; an outlier: Rebirth's run 3.2) / 0.59 (0.25) / 1.04
   (0.23), town classic 2.27 / 0.32 / 0.71, global map 1.00 / 0.08 / 0.22. The renderer's CPU per frame (terrain,
   hand-over, textures, end of frame and the shadow map's), before / after: fight classic 23.1 (Rebirth's run 4.9) /
   2.53, town classic 3.68 / 1.42, global map 1.24 / 0.28; with the shadow map (no memory textures drawn; the
   differences are the machine's load) fight 5.27 / 2.51, town 1.42 / 1.34. GPU frame ms before / after: fight
   classic 0.81 / 0.78, town classic 0.63 / 0.72, fight 1.28 / 1.25, town 0.97 / 0.95, map 0.78 / 1.56 (noisy;
   GenerateMips of 12-17 small textures is not measurable). Images made a frame: 12-17 classic shadows to none (after
   the first use of each holder), the map's 341 KB to none (renderstats' "memory textures" count stays: it counts
   uploads, made or in place; ei_hook.log's `render: images` lines tell them apart). In place with CPU levels (UpdateSubresource per level)
   was tried: 0.76-0.89 of remake's time against GenerateMips' 0.33-0.45. Check: render_scene, render_shadows,
   render_options_shadows, ui_global_map_pick captured in place and `memorytextures remake` at one frozen moment, 8 /
   0.5% (largest channel difference 2). The memory textures are gone since R28b (section 7).
3. **R16c Terrain pages resident on the GPU (done).** `render_terrain.cpp`. Each sector page at a level of detail and
   seam variant is built once, all four quarters, into a pool of default-usage buffers written with UpdateSubresource
   (sokol has no partial update): positions and texture coordinates (20 bytes a vertex), the lit colours (8 bytes, a
   stream of their own) and absolute indices. A frame keeps the visible quarters of each page handed over
   (`TerrainPiece`); ranges that follow each other in the pool are one draw. A page is built again when its tile
   lists, records, edge flags or animated tiles moved (pointers, sizes) or the game rebuilt the sector
   (`render::TerrainChanged(s, Rebuilt)` from Build and Clear). When the game only moves texture coordinates
   (Animate's animated tiles: `Moved`; only the pages holding one of the sector's animated tiles) or the water
   layer's positions (Animate's waves, ApplyWaterLevels: `WaterMoved`; only the pages reading a water record) the
   page reads them again through its vertices' records and tile entries and uploads the changed range; the visible
   quarters' colours are compared with the records every frame, the changed range uploaded. The pool (at least 128 K vertices) starts again with the frame's
   pages when full (made larger when they need more than half of it); pages not drawn for 600 frames are dropped.
   The shadow map's fit reads the pieces' positions (`TerrainPositions`, the same vertices in the same order).
   Measured (render_stats, vanilla 4K, per frame): terrain upload before / after menu 268 / 51 KB, zone1 808 / 229,
   fight 808 / 211, town 372 / 74 (indices only when a page is built: none in these scenes); terrain CPU (`renderstats`
   "terrain": hand-over and uploads) zone1 0.44-0.61 ms with every page built and uploaded every frame / 0.24 kept,
   town 0.15 kept (Rebirth's wider view: zone1 1046 / 289 KB, 0.67 / 0.44-0.56 ms; the times are noisy). What zone1
   still uploads is its water pages' texture coordinates (R16c2 below) and relit colours. Check: the harness's
   `terraincache off` builds and uploads every page every frame as before (since R16c2 also the pages the game
   moved, which it read again instead before); render_scene, render_terrain_seams and render_smooth capture the frozen frame both ways (0 pixels differ),
   and the harness build compares every kept page with a fresh build every 16th frame (`terraincache check`, 0 stale
   in the render_* scenarios and render_stats).
   **R16c2 Water waves in the vertex shader (measured, not built: about 0.03 ms a frame to gain).** The water
   option is off in the harness's settings (`EnableWaterWaves` 0, config/harness/registry.reg; WorldClient clears
   landscape flag 1 then, and Animate 006C3E80 skips the waves), so in render_stats' scenes the water pages move
   by their animated tiles' texture coordinates (the animated water textures, a step every third tick), not by
   waves. Measured with temporary timers (harness build, 4K, per frame, the 60 frames of each scene; vanilla /
   Rebirth): zone1 6 water pages (9.3 K vertices in view), 174 / 221 KB of their texture coordinates uploaded,
   writes 0.03 / 0.04 ms; the page re-reads 0.11 / 0.13 ms, because `TerrainChanged(s, false)` was per sector and
   the ground pages of every sector with animated tiles read their vertices again too (34 / 43 pages, 21.9 / 26.7
   K vertices, nothing changed in the ground ones); Animate itself 0.03 ms. With the waves option on (a temporary
   harness step setting it before newgame): Animate 1.0 / 1.2 ms in zone1 and 0.35 ms in the town (33 / 25
   sectors: three sine lookups, three conversions and the specular per water vertex), uploads 332 / 377 KB, the
   water pages' writes 0.05-0.06 ms. GPU waves would leave Animate on the CPU (the game reads the water heights:
   HeightAt through VertexAt's layer 1, the digest) and would still re-read and upload the same water pages for
   their animated texture coordinates, so what they could take is the waves' share of the writes and the crests'
   specular: about 0.02-0.03 ms. Built instead (R16c2): the changes say which layer moved (`TerrainChange`), and a
   page reads its vertices again only for its own kind (pages with animated tiles, pages reading water records):
   zone1 34 -> 4.7 pages a frame, 21.9 -> 8.9 K vertices read, 0.11 -> 0.05 ms (Rebirth 43 -> 5.7, 0.13 -> 0.06;
   town 25 -> 2.7 pages, 0.046 -> 0.016 ms); uploads unchanged. Check: `terraincache check` 0 stale in render_stats
   and the render_* scenarios, and with the waves on in zone1 and the town (721 / 872 checks); render_scene's
   world_fresh 0 pixels differ; goldens. What is left with the waves on is Animate on the CPU (R16c3).
   **R16c3 Animate's circling waves once per call (done).** Their sines depend only on the column or the row and
   the world time: 33 values per call (`CircleSines`, the same expressions) instead of the time read and two
   lookups per vertex. Measured A/B in one run (alternate world updates, harness build, vanilla): zone1 0.21-0.27 ->
   0.10-0.14 ms per world update (a harness frame is five), the town 0.10-0.23 -> 0.05-0.10 (noisy). Check: the scenario
   `terrain_water_waves` (`waterwaves 1` before newgame; the digest's `water` group, a scenario's `digest water`
   line: per cached sector hashes of the water layer's positions and speculars, every record) and its golden of the
   original, the same before and after.
   **R16c4 FISTP inlined (done).** `x87::fistp` / `RoundToInt` (include/ei/x87.h) are `__forceinline` functions
   with an `__asm` FLD / FISTP instead of naked calls (the same instructions and control word; every one of the ~220
   sites inlines it: no call relocation to them is left in any object), and Animate's helpers `Sine`, `Byte`,
   `SignClear` are `inline` (the build's `/Ob1` inlines only functions marked so). Measured A/B/C in one run
   (alternate world updates, harness build, vanilla, `waterwaves 1`; ms per world update): zone1 calls 0.091-0.095 /
   helpers inlined 0.087-0.089 / FISTP inlined too 0.066-0.070, the town 0.036 / 0.029 / 0.024: about 0.12 ms a
   harness frame in zone1. Check: terrain_water_waves' goldens (vanilla, and Rebirth's, recorded in R16c4; in the
   Rebirth smoke set) and every golden the same, ei_vectors 0.
   **R16c5 /Ob2 (done).** The build inlines at /Ob2 (CMakeLists' `EI_INLINE`; 1 builds as before, for A/B runs with
   `EI_BUILD_DIR`), so file-local helpers inline without `inline`. Measured: render_stats vanilla, the /Ob1 and /Ob2
   verify builds run at the same time, three rounds (main thread CPU ms a frame, /Ob1 -> /Ob2): fight 17.1 / 18.0 /
   26.7 -> 16.0 / 17.1 / 23.5, town 5.3 / 5.5 / 5.8 -> 4.6 / 5.0 / 5.0, zone1 5.3 / 8.8 / 5.3 -> 4.4 / 7.5 / 4.3
   (escape menu, map, camp within the noise). Inlined constant helpers folded five products the original rounds to
   24 bits (check_fp_fold, removed in C25d; written as the float now). Check: goldens both installs, ei_vectors 0, the build checks.
   /Ob3 against /Ob2 (three rounds the same way): no gain beyond the noise (fight 18.3 / 18.7, town 6.9 / 7.0, zone1
   5.8 / 5.3 ms means), and one more fold (`SetTimeSpeedImpl` 0058EEE0's 1 / 15), so the build stays at /Ob2.
4. **R16d Meshes resident on the GPU (done).** `render_mesh_cache.cpp`, the pool shared with the terrain in
   `render_buffers.cpp` (`GpuPool`: default-usage buffers written with UpdateSubresource, a geometry stream, a colour
   stream and absolute indices; placed in order, started again with the frame's entries when full). Each figure mesh
   drawn through the figure part draws (DrawLit 005A7A70 and DrawPlain 005A7640, either through the device or a link
   figure's part) is kept under the mesh: its corners' model-space positions and texture coordinates (20 bytes), the
   colour and specular pair (8 bytes, a stream of its own) and the valid triangles; the draw binds the pool with the
   mesh's first index, the texture offset (`textureOffsetU` / `V_`, animated water and lava) a shader constant. The
   game says what changed (`render::SetMeshSource` around the device's draw; `render::MeshChanged`): an entry is built
   again when an array or count it was built from differs or the mesh or link figure was rebuilt (loaded, cleared,
   mirrored, copied, a part added or removed: `MeshChange::Rebuilt`); its positions are read again when the
   animation's per-vertex offsets changed (`SetOffsets` 005A0B10 compares them) or a link figure's part was morphed
   again (004F3800) (`Positions`); its colours when the draw lit or filled them (Light, FlatColours, DrawPlain's fill,
   the device's colour override, flag 4; also when that draw was streamed or never reached a pass), the sky lit itself
   (SkyObject::Light, `Colours` on the sky) or a link figure lit or filled a part (004F3870 / 004F3AD0, `Colours` on
   the colour array: the original code calls them outside the figure part draws too, the meshes keeping flag 0x10,
   "lit"). Only the changed range of corners is uploaded. A mesh drawn again in the same frame with other data, interface sprites (UiMesh::Draw
   005E6D90: the interface rewrites its quads in place in many places, the original's and Rebirth's code among them;
   `render::SetInterfaceDraw`) and mesh draws without a source are streamed as before. Entries not drawn for 600
   frames are dropped. Measured (render_stats, vanilla 4K, per frame; before = the same scene with `meshcache off`,
   as before R16d): mesh upload KB (vertices + indices) menu 324 + 139 / 35 + 2, zone1 114 + 51 / 42 + 17, fight
   240 + 131 / 64 + 17, town 1648 + 286 / 339 + 6, camp 50 + 25 / 31 + 15, escape menu 73 + 32 / 0, the global map
   106 + 75 unchanged (all interface sprites); with classic shadows (the flat decals still streamed) zone1 208 + 64 /
   135 + 30, fight 350 + 147 / 185 + 34, town 1997 + 334 / 685 + 56. Hand-over CPU ms (kept / streamed in the same run, two
   runs, noisy): zone1 0.12-0.25 / 0.11-0.17, fight 0.27-0.28 / 0.26-0.37, town 0.24-0.26 / 0.46-0.61 (the colour
   comparisons cost about what the copies saved in the small scenes). What is still uploaded is relit colours (the town's 68 relit meshes a frame: its figures are lit
   again every frame, about 40 K corners), moved figures and the streamed sprites; colours lit on the GPU (R16i) and
   GPU animation (R16h) take the rest. Check: the harness's `meshcache on|off|check` (off: every mesh built and
   streamed every frame); render_scene, render_blink, models and units capture the frozen frame both ways (0 pixels
   differ at 8), and the harness build compares every kept mesh drawn with a fresh build every 16th frame (0 stale in
   those and render_stats, 4352 checks; no stale mesh logged in any scenario of the image's suite). Risk: a writer of
   a mesh's arrays in place that sends no signal (found and signalled: the sky's lighting, the link figures' lighting
   outside the part draws; streamed: the interface's quads); `meshcache check` reports it, and every stale mesh is
   logged ("a mesh kept on the GPU was stale") in any run of the harness build.
   **R16d2 Interface sprites kept on the GPU (measured, not built: about 0.1 ms a frame at most to gain).** What the
   streamed sprites cost, measured with a temporary counter around their build into the frame's stream (render_stats,
   vanilla 4K, the harness build, 4 games at once; per frame): zone1's HUD 54 draws, 655 corners, 17.9 KB vertices +
   6.1 KB indices, 0.010 ms; the fight 52 draws, 18.6 + 6.1 KB, 0.016 ms; the camp 78 draws, 30.5 + 14.5 KB, 0.042 ms;
   the global map 23 draws, 3869 corners, 105.8 + 74.9 KB, 0.08-0.10 ms; the town and the escape menu none. Their copy
   into the stream's ring buffer at the end of the frame (one append for the frame) is about 0.02 ms for the map's
   181 KB. Keeping them would still read and compare each sprite's source corners per draw (the same reads as the
   build), so the gain is a part of 0.03 ms (HUD) to 0.12 ms (map) of a 3.4 ms (map) to 17-27 ms (zone1, fight)
   main-thread frame, and nothing on the GPU (the same draws; 181 KB a frame is about 11 MB/s at 60 frames, 170 MB/s
   for 16 games). Not worth a signal at every writer (the original's and Rebirth's shape code among them) or a check
   per draw; SetInterfaceDraw stays. Revisit if an interface draws far more sprites (the native interface, ui-native.md).
5. **R16e Ground decals on the GPU (measured, not built: under 0.07 ms a frame to gain with the shadow map).** The
   plan: a box per decal projecting onto the depth of the ground and platforms in a shader instead of ProjectDecal
   0056C750 (CPU projection and clipping) and the device's flat draws 004EA4A0. Measured with `renderstats`' decal
   timers (harness build: "decals projected" by ProjectDecal for object shadows and ground prints, "flat draws" of
   the shadows' decals and the rest (prints, selection markers), "shadow builds": RenderShadow in 0051CE60 around
   its projection: the parts' points, the 64 x 64 raster, its blur, the texture's copy). render_stats, vanilla 4K,
   ms a frame (count a frame), the machine shared with other workers' runs:

   | Scene | shadow projections | print projections | flat draws: shadows | flat draws: others | shadow builds | main thread CPU |
   |---|---|---|---|---|---|---|
   | zone1 | 0.34 (11) | 0 | 0.065 (12) | 0.000 (1) | 0.31 | 6.2 |
   | fight | 0.60 (18) | 0.004 (0.2) | 0.072 (19) | 0.005 (7) | 0.73 | 18.6 |
   | town | 0.48-0.63 (23-25) | 0.004-0.013 (1) | 0.17-0.21 (27-29) | 0.015-0.06 (45-210, more as prints pile up) | 1.15-1.30 | 6.6-7.5 |

   Classic shadows add the decals' hand-over: 13 / 20 / 28 flat meshes, 94 / 126 / 288 KB, hand-over CPU +0.02 /
   +0.03 / +0.06 ms. EI_PROFILE=1 agrees (the town with the map, 7.3 ms frames): DrawShadow 30% of the samples,
   RenderShadow 25% (ProjectDecal 7.7: Terrain::AddDecalTriangles 7.2, DecalBuilder::ClipStage 6.3,
   AddTerrainTriangle 5.4; LinkMesh::RasterisePartImpl 6.8), DrawFlatBlocks 5.4; zone1 9.5 / 9.2 / 4.2, fight
   7.0 / 6.7 / 3.5. A ground print is projected once (GroundPrint::Draw's `built`), so what a GPU decal path takes
   is the prints' and markers' flat draws: under 0.07 ms. The object shadows' projection
   (0.34-0.63 ms) stays either way: its mesh's bounds are the shadow's sphere (`Object3DClientSpecific::sphere`,
   FitBounds in RenderShadow), which ShadowInView 0051E300 tests for objects out of view (listed for shadows,
   their pose updated: CollectVisible 0056A990), so the game reads the projection whatever draws the decal.
   What the numbers showed: with the shadow map the whole decal build ran every frame for the moving objects though
   only that sphere is read: projection, build and the decal's flat draw (which BeginMesh
   drops) are 0.72 ms of zone1's frame, 1.40 of the fight's and 2.1 of the town's.
   **R16e1 (done): with the map the classic texture and the decal's draw are left out; R26 deleted their code.**
   DrawShadow 0051CE60 (map_object_shadow.cpp): RenderShadow still projects the parts' points (bounds, quad), reads
   whether a part is keyed (`KeyedParts`, which the original read in the raster's loop) and projects the decal (its
   sphere); the texture (MakeTexture: the render surface's lock, RasterisePart / FigureMesh::Rasterise, BlurShadow /
   BlurShadow2, the copy, LoadPixelsFrom, `render::TexturePixels`; NewShadowTexture / ShadowRenderSurface, the
   texture pixels 007AF1E0 / 007AEEB8 / 007C2E00 and their exit functions) and, after SetTexture and EdgeFade, the
   decal's DrawFlatBlocks are gone. The half's `shadowMade` stands for the original's "texture made" test: the
   first draw after Create / ClearShadow builds the shadow. 2DintMMX's RasteriseTriangle / RasteriseKeyedTriangle
   (slots 8 and 9) and BlurShadow / BlurShadow2 (slots 11 / 12) are not called (R27e; R28 removed the converter's
   interface and the DLL's load). (Until R16i a switch to the classic option needed the textures again: a table
   of the shadows whose texture was made, with a generation the harness's `shadows rebuild` moved, rebuilt the
   static ones the game would not rebuild; R16i removed the option, the table and those harness words.) Measured (render_stats, 4K,
   per frame, the `_shadowfull` stretch with them made against `_shadowskip` right after; shadow builds ms + the
   shadows' flat draws ms): vanilla zone1 0.30 + 0.063 / 0.08 + 0, fight 0.81 + 0.092 / 0.22 + 0, town 1.10 +
   0.19 / 0.35 + 0; Rebirth 0.61 + 0.084 / 0.11, 0.96 + 0.115 / 0.28, 1.19 + 0.32 / 0.34 (what stays is the
   points' projection; the decal projections, 0.3-0.9 ms, are counted apart and stay). Check: render_shadows (at
   the start and in the town the map's frame with and without them, 0 pixels differ; the classic frame after the
   switch against the one after `shadows rebuild`, 8 / 0.01%: 0 differ, and 0.12% without the switch's rebuild;
   R16i removed those comparisons with the option), render_options_shadows, goldens. R16e2 (the user's call) would take a sphere around the
   decal's box instead of the projected mesh, dropping the projection too, but lists more objects out of view for
   shadows. Facts for a later GPU decal path: the CPU volume is a prism along the direction through the quad's u, v
   in [0, 1], cut by one plane at the point minus depth * 0.33 along the direction and open beyond it (a print
   lands on the ground under a bridge too, unless `skipOnPlatform` and the point is on a platform); receivers are
   the terrain and, with `objects`, the platform objects' link-figure parts (an object shadow with keyed parts:
   the terrain only); DrawPass 0056AFE0 draws all objects and figures, then the prints, the shadows and the
   selection markers, then water and particles come after; the frame's depth is D32 without stencil (kDepthFormat),
   so keeping decals off figures and other objects needs a stencil format or a receiver mask, and reading depth
   needs a pass break (and the multisampled depth with the smooth option). Revisit if decals become many more.
6. **R16f The present without a frame copy (done).** The frame was copied whole every frame for the cursor thread
   (33 MB of GPU traffic at 4K) before the cursor was drawn into it. Now the present shader draws the cursor over the
   frame as it applies the gamma ramp (section 9, R9c2), the frame target never holds the cursor and the cursor thread
   draws the frame target itself: the copy and the cursor's blit pass (one pass and one draw less a frame with the
   cursor) are gone. Measured in render_stats (4K, 16 games share the GPU, so the GPU ms are noise): passes 2.0 -> 1.0 a
   frame. Check: render_cursor_present, render_present, render_present_shot (0 differences, the main thread's cursor
   checked in the back buffer too: "cursor in the presented frame").
7. **R16g Per-draw records and instancing (measured, not built: under 0.2 ms a frame to gain).** The plan: one ring
   buffer of per-draw records (matrix, fog, stage operations) read by index instead of two apply_uniforms per mesh
   draw; meshes with the same geometry and state drawn instanced. Measured with a temporary timer around the parts
   of `DrawMeshBatch` (QueryPerformanceCounter; render_stats, vanilla 4K, the harness build with sokol's validation
   layer off, as in the product; per frame, main thread):

   | Scene | mesh draws (kept on the GPU) | all of DrawMeshBatch ms | uniforms (both blocks built and applied) | pipeline + bindings | sg_draw | same geometry and state as the draw before / as any earlier draw of the frame |
   |---|---|---|---|---|---|---|
   | menu | 159 (158) | 0.25 | 0.085 | 0.14 | 0.008 | 3 / 7 |
   | zone1 | 137 (68) | 0.17 | 0.032 | 0.12 | 0.007 | 4 / 5 |
   | fight | 245-330 (163-242) | 0.30-0.40 | 0.055-0.074 | 0.21-0.28 | 0.012-0.016 | 4 / 47-77 |
   | town | 129 (118) | 0.15 | 0.027 | 0.11 | 0.006 | 11 / 11 |
   | camp | 108 | 0.14 | 0.025 | 0.10 | 0.005 | 5 / 5 |

   ("Same geometry and state": the kept mesh's vertices and triangles hash equal, and the pipeline, both stages'
   textures and operations, the patch, alpha test, fog, specular, texture factor, shadow sampling and distance fog
   are the same.) So a ring of records could save at most the uniforms' 0.03-0.07 ms (less its own building and
   upload), and instancing at most the draws it merges: in the fight 47-77 of them could join an earlier one (only
   4 the one just before; the rest only by reordering the frame's draws), about 1.2 us each, 0.06-0.09 ms; together
   under 0.2 ms of the fight's 19 ms main-thread frame, and nothing on the GPU (the same vertices; the GPU frame is
   1.2 ms). The whole mesh draw path is 0.4 ms of it: the main thread's time is the game's (item 8). The largest
   part, the pipeline lookup and the bindings (0.8 us a draw: the pipeline map, the samplers per stage,
   sg_apply_bindings setting every view and sampler), is also under 0.3 ms. EI_PROFILE=1 (the fight) agrees: no
   renderer draw function among the top samples, d3d11.dll and the driver 1.4% each. Revisit with many more draws
   (a much wider view or hundreds of units); materials from the game's data and particles (0-3 pre-transformed
   draws a frame) stay with it.
8. **R16h Figure animation on the GPU (measured, not built: nothing to gain).** The plan assumed the game morphs the
   figure parts' positions on the CPU every frame. Creatures animate by their bones: FigureBone::Place (0050C230) gives
   each part a position and rotation, i.e. its matrix, which the GPU already applies. The per-vertex morph
   (AnimationTrack::Evaluate 0050ACD0 blends a track's key-frame vertex sets; Place hands them to the part through
   FigureMesh::SetOffsets 005A0B10; the part draws add them into a copy of the positions) is almost unused: per frame
   0.6 of the fight's 331 mesh draws (12 corners), none in zone1, the town or the menu (renderstats' "morphed
   meshes"), and a profile of render_stats has no sample in Evaluate or the copies. Its result is also read by game
   code, so it would stay on the CPU: the device's projection of the moved positions (DrawMesh 004E9010: the
   visibility verdict and, with draw flag 1, the part's screen rectangle for picking) and the object shadow's
   (FigureMesh::DrawOffset 005A0CD0, LinkMesh::ProjectPart 004F4680: the raster whose bounds feed the shadow
   lists). What a GPU path could take is the renderer's reading and upload of the few moved corners (R16d already
   uploads only the changed range). The "moved" meshes in the table above were the classic shadows' decals (R16e).
   Where a many-figure frame's main-thread time goes instead (render_stats, vanilla 4K, harness build; main thread
   CPU / wall ms per frame, renderstats): fight 18.6 / 19.0, mostly the server's AI path search (LocalSearch,
   AIMap::FindPath); town 8.8 / 9.1: by profile figure lighting in 3DfpFPU (17% of the samples; R16i), the x87
   control word set around every libcrt math call (the CRT's ctrlfp 9%: Hypot in the camera's ClampAboveObjects;
   gone with SV10i, Hypot 1.6% after), the ground decals' projection and clipping (DecalBuilder; R16e: the object shadows' build, item 5) and the device's per-corner work for meshes
   the new renderer draws from the hand-over anyway (FillCorners, the clipper, fog: R16k, measured smaller than the
   profile suggested: 0.1-0.5 ms a frame for the whole device mesh path, item 11). The renderer's own CPU
   (terrain, hand-over, textures, end of frame, shadow map) is 2.0 ms of the fight's frame and 1.0 of the town's; GPU frame
   1.2 / 1.4 ms.
9. **R16i GPU lighting (done; no option: the GPU path is the only one).** `render_lighting.cpp`,
   `shaders/scene_lighting.hlsli`, `render_terrain.cpp`, `shaders/mesh.hlsl`. The light objects stay the game's (the
   sun, its colour and the ambient, the torches and point lights with their positions, colours and ranges); what
   they light is done per pixel.
   - **The ground.** The world's draw hands over the frame's light (`render::SceneLighting`: the sun, the ambient,
     the landscape's point lights, the camera); the terrain shader lights each pixel by the vertex lighting's own
     rules (ShadeSun, LightVertex: ambient times one less the water's loss, the sun's facing over the material's
     colour, each point light's falloff, its specular part into the specular word) from the vertex normals and
     material colours. None of the game's vertex lighting runs on the CPU (R26, R34): ShadeSun 006C36B0,
     LightVertex 006C31C0, CLight::AddDirectional 0068D310, the sun's ray march SunReaches 006C92C0 and the
     landscape's per-vertex light lists (their 16-byte node pool at 007C8840) and light areas are not ported. The
     landscape keeps only the table of shown point lights (`Terrain::lights`, by id, held: AddLight / RemoveLight),
     which the hand-over reads. `SectorData::Build` writes each vertex's edge fade into its specular word (the fog
     factor, `Terrain::EdgeFade` truncated); on the water Animate writes the wave crests over the colour bytes. The
     water's specular lights are the shader's (`spec`, the larger of them and the crests: the original's crests
     replaced them where the waves ran).
     **No relighting (R34a).** A sector's fade never changes, so it is written once, at Build: there is no Lit stage,
     no relight countdowns (SetSun only keeps the sun and the ambient) and the LightingFreq option is read by nothing.
     The original dropped a sector's lighting on its countdowns and skipped the tick's tile and wave animation then;
     its animated tiles drew from the game's ISAAC, so that changed the game's draws. Now the terrain's draws (and
     every other client effect's) come from `g_clientRandom` (random.h) in both builds, and the harness keeps the
     original's sectors lit from Build on (harness_client_effects.h, standalone-verification.md section 5).
     Behaviour that differs from the original (R34; the goldens the same): nothing loads or touches sectors for the
     lighting (the ray march loaded the sectors it crossed; the light areas and relights looked sectors up), so the
     sector cache's order and evictions could differ; a specular light whose square reached a vertex outside its
     range added the water's material colour there (AddPoint's base), and adds nothing now.
   - **The figures.** 3DfpFPU's LightMesh (1000D910, disassembled) lit a part per draw: per channel the larger of the
     ambient and the sun's colour times N.L in the model's space, the larger of that and each point light's colour
     times N.L times its falloff, plus the emissive, at most 1, and for a crown part a pull towards the mean over
     the sphere. The part draw (`figure_part_draw.cpp`) now hands that over (`render::MeshLighting`: the model
     matrix (the world stack's top with the scene's camera taken off, `worlddata::SceneCamera`: a static part
     already lit is not pushed on the object stack, which only the CPU lighting read), the sun, the ambient colour `SceneRenderer::SetColour` set last (`AmbientColour`), the
     material's colours times the objects' lighting coefficient, the draw's lights in reach) with the part's normals
     and corner words; `mesh.hlsl` lights each pixel the same way (the crown's mean analytic: 0.6 of the way). The
     colour arrays hold white with the material's alpha (`Material::GpuLitColour`); a mesh kept on the GPU
     (R16d) is no longer relit each frame. The speculars and flag 0x10 (moved / static) are as before.
   - **Shadows in the light.** On the ground a shadow takes away the sun's part: lit = the smaller of the light
     with the sun hidden (the ambient plus the material's own, as the game's ray march left a vertex) and the sunlit
     light x (1 - hidden x strength x the normal's turn towards the map's sun), so never lighter than half the
     sunlit light; the point lights' light is darkened alike (the towns' lamps burn by day too, and their
     unshadowed light hid the units' shadows on the ground: the user's review). Every mesh, lit here or by the
     game, is darkened as before R16i: its colour (lamps and ambient light included) x (1 - hidden x strength) on
     faces turned to the map's sun by their face normal (`shadow_facing`). Not by the vertex normals: the figure
     parts' smoothed normals can lean far from their faces (the shop's plank table in bz1g read sideways, and
     stayed lit under the roof at night). Receivers look up the map at their point moved 1.5 map texels along
     the normal (normal-offset bias). The ground casts into the map (its faces turned away from the sun). R22's
     caster ids are gone: a receiver darkens by its own facing like any other (the ids' R16UI image, the
     harness's `shadowfacing`). Checked against main in bz1g with four units by noon and at night: the mean
     shadowed / lit brightness under the shadow view's mask is within 0.01 of main's (units, the shop, the frame).
   - **Units take no shadow of their own (R16i2; the user: a pig in town with hard dark triangles across its
     back).** By face normal a low-poly creature flipped triangle by triangle between shadowed and lit, and it
     shadowed itself in the map (its own depths, R22's ids gone), also at night under the moon's map (the hero's
     and the townsfolk's chests and shoulders). Now the meshes of a unit's draw (0056AFE0's unit lists,
     `kDrawUnit`: `render::BeginCaster(object, true)`) compare from the unit's nearest depth in the map
     (`FigureFronts` in PrepareShadows: the least over its receiving meshes of their corners' model-space box,
     `MeshBatch::box` taken at the hand-over, through their light matrices; `MeshBatch::front`), as a plane
     facing the light (no receiver slope, no normal offset), on every face: nothing of the unit is nearer the
     sun than that, so it never darkens itself, and every caster nearer the sun shadows all of it alike (a
     unit inside the gate's shadow is evenly dark, its faces turned away no longer lit). Its sun side is
     shaded by the lighting's smooth N.L alone (`figure_light`). What is lost: a caster whose nearest surface
     lies within the unit's own depth (two units intertwined) does not shadow it; a part of a drawn unit
     that only the R20 walk casts (the game's part view test left it out) could still shadow the rest when
     nearer the sun than the drawn parts' box. Static objects (`kDraw`) keep the face-normal rule (bz1g's
     shop and its plank table unchanged). Cost: a box over each unit part's positions at the hand-over and
     one depth per unit part a frame. Side by side (before | after): build\R16i2\crop_*.png on E: (bz1g noon
     and night: creatures, the hero, a townsman, the gate's shadow, the shop; zone1). The harness's spawned
     creatures draw untextured (white), the town's people textured. The shadow view writes the mesh's own
     alpha, so blended parts show flat grey triangles there that are not shadow.
   - **What stays on the CPU, and why.** The blood prints' levels (GroundPrint's `colour` is saved:
     GroundPrint::Save); the sky's lighting (SkyObject::Light, small; a candidate); the sun's reference in the
     part's light list; the terrain's edge fade (the fog factor, also read by the
     figures' draws and the ground prints). The CLight fields only the device read (world position, local
     direction, lit colours) are no longer written.
   - **Looks that changed (the user's review).** Point lights cast no shadow (until R16i1, below): the town's fires
     light the inside of the gate's shadow (render_shadows_outside's floor lowered to 0.3%); a point light per pixel
     shows a bright spot where the vertices smeared it (the hero's light on stone floors); creatures are lit per
     pixel (N.L across a face, not per vertex).
   - **Point-light shadows (R16i1).** `render_point_shadows.cpp`, `shaders/point_shadow.hlsli`. Once a frame
     (PreparePointShadows, from PrepareShadows; drawn from DrawShadowMap, so its CPU time, draws and GPU time count
     as the shadow map's in render_stats) the kept scene lights (render::SceneLighting's nearest 16) but an object's
     own light whose reach is in the camera's view are taken strongest first (brightest channel x range), at most `kMostPointShadows`
     (4), and each one that some caster reaches gets a cube of six 512 x 512 D32 faces (perspective, 90 degrees,
     near 0.05, far its range) in one atlas (a light a row, a face a column: 3072 x 2048). Casters: the meshes the
     sun's map takes (objects CastsShadow named this frame, strength at least 0.5, one pass a mesh; cut out by
     alpha alike) whose bounding sphere (the mesh's model-space box, `MeshBatch::box`, now taken for every
     caster's mesh, through its model matrix into the landscape's space) reaches the light, drawn into each face
     its sphere touches. An object with a mesh sphere within `kHoldMargin` (0.5 units) of the light casts
     nothing for it: the campfire's spit, a lamp's post. An object's own light casts none (R36, the user's
     playtest): the light a party unit gets at night (UnitClientSpecific::PlayerChanged ->
     MapObjectClientSpecific::CreateLight, radius `heroLight`, 2 units above the unit's position, moved with it
     by the companion's RunTick) and a quest light (light mode 1, the same CreateLight). It held the unit by the
     margin test alone: measured in zone1 (a log of the hero's smallest mesh-sphere gap to his light), 0.13 units
     standing, 0.15-0.27 running at 8 ms frames and 0.35-0.41 at the harness's default frame steps; where a
     frame's pose and the light's trail put it past the 0.5 margin, the unit stopped holding his light for that
     frame and his own shadow from it popped in (the user's playtest; the harness's runs of Zak stayed under). CreateLight flags such a light `Off`
     (with Moving), which the game's own shadows skip too; HandOverSceneLights hands `ScenePointLight::shadows`
     = not Off over, and PreparePointShadows takes no light without it (`SceneLightCastsShadows`). Everything
     else keeps its point shadows: the torches (TorchObjectClientSpec: the fires, the lamps), the map's
     lights and the spell effects' (EffectPointLight, Off only at a negative range). Receivers, only what
     is lit on the GPU: the ground's point loop (`ground_light`: the light's slot from `LightParams::shadowSlots`;
     looked up moved 1.5 texels of its distance along the vertex normal; the ground casts none) and the figure
     parts' (`figure_light`: the mesh's light list carries the scene light's index from the hand-over,
     `SceneLightIndex`, mapped to its slot at the draw; the point in the landscape's space from the mesh's model
     matrix, `MeshBatch::landscape`). A unit's meshes compare from the unit's nearest distance to the light
     (`MeshBatch::pointFront`, its meshes' spheres; R16i2's rule: no shadow of its own, every face); any other
     mesh only on faces turned to the light by its face normal, moved 1.5 texels along it. Filter: 8 bilinear
     comparisons on a 1.5-texel Vogel disc turned per pixel, a 1-texel depth bias, the slope-scaled bias of the
     caster pipeline; a face's lookups are kept inside it (no filtering across face edges). The light's part is
     scaled by what reaches (`seen`) before the game's max rules. Not done: the ground and objects outside the
     view do not cast point shadows (R20's walk is the sun's); lights beyond the 16 kept cast none; the set of
     mapped lights changes as the camera moves (a fifth light's shadows come and go). Frames: build\R16i1 on
     E: in the worktree of R16i1 (before = main's render_lighting run, after = this branch; light_town_night: the
     hero's shadow away from the campfire, the soldier's and the spit's legs' long shadows, the rack's posts;
     zone1 at night unchanged: the hero holds his light). Cost (render_stats, vanilla 4K, per frame, the same
     build with the point shadows off / on, two runs each, alternating; the shadow map's figures include the
     point pass): shadow draws zone1 69 / 84-100, fight 226 / 386-404, town 183 / 351-387, town at night 162 /
     316-358; the shadow map's CPU ms 0.42 / 0.41-0.49, 0.56 / 0.79-0.90, 0.97-1.02 / 1.19-1.25, 0.79-0.87 /
     0.99-1.06; its GPU pass town at night 0.45-0.49 / 0.49-0.56 ms (the 3072 x 2048 atlas' clear and the faces'
     draws), the frame's GPU time and the main thread's CPU within the runs' noise (town at night GPU frame
     1.67-1.94 / 1.91-2.74, main CPU 4.3-4.7 / 4.4-4.7). The candidates are the lights in view at all, so by day
     the town's lamps cast too (their shadows faint: the sun's light is the larger).
   Check: render_lighting (frames for review: zone1's start, creatures on slopes, a fight, zone1 at night, the hero's
   light at night with a goblin and a wolf beside him (light_night_carrier) and running (light_run_1..6, unfrozen;
   R36: no point shadows from his own light), the town
   bz1g by day and at night with its campfire), render_shadows*, render_scene, render_stats; the goldens the same
   (the digest's water hash, saves). Cost (render_stats, vanilla 4K, per frame; before = this branch's base,
   main of 2026-10-22, one run; after = two runs alone, the machine shared with other workers' games): main
   thread CPU zone1 4.46 / 2.96-3.92 ms, fight 15.42 / 12.93-12.97, town 9.56 / 5.25-5.33; meshes relit a frame
   22 / 0, 188 / 0, 58 / 0 and terrain pages relit 15 / 0, 14 / 0, 13 / 0; mesh uploads 42 / 54, 93 / 61, 305 /
   42 KB; terrain uploads 228 / 312 KB in zone1 (its moving water pages' vertices carry the normal now: 36 bytes
   against 20); GPU frame zone1 0.91 / 0.96-0.99, fight 1.89 / 1.66-1.87, town 2.13 / 3.46-3.75 in the first
   stretch but 2.84 / 1.51-2.27 in the later one (noisy; per-pixel point lights over the ground at 4K are the
   likely cost). Side by side (before | after): build\R16i\compare_*.png on E: (start, slope, fight, night,
   town, town_night).

10. **R16j Object shadows from a sun shadow map (done; user 2026-10-20).** `render_shadows.cpp`. The game hands over the
    sun (00569E40: `Terrain::sun`'s direction), the object each figure draw's meshes belong to (0056AFE0's object
    draws between `BeginCaster` / `EndCaster`) and the objects whose shadow the game draws this frame (0051CE60
    `CastsShadow` after its option, class switch and visibility tests; a unit only once faded in to half). Once a
    frame before the first pass (`PrepareShadows`): the camera's matrix is the world's first ground batch's; its
    inverse times a mesh's clip matrix gives the mesh in the landscape's space; an orthographic view from the sun
    (tilted to at least 20 degrees as 0051CE60's) is fitted around every second terrain vertex inside the view (x, y
    in steps of 8 units, origin on whole texels; along the light from the nearest caster that reaches it, below),
    4096 x 4096 D32. The
    casters' model meshes (first pass of each; alpha-tested or blended passes cut out by stage 0's alpha, read from
    the texture's level 0: a smaller level seen from the sun at a grazing angle averages fronds away;
    render_options_shadows' pine casts a denser shadow so, 0.9% of that frame) go into it in a depth-only pass. Receivers: the ground pages (not water: the ground under it) and the world's model meshes
    after the ground (not the sky, the flat geometry or additive passes) sample it in their shaders (`shaders/shadow_receiver.hlsli`:
    soft edges below, receiver-plane depth bias; a receiver's own object shadows only its faces turned to
    the sun, any other object every face: R22 below) and darken by up to half (the old decal's alpha 0x80) before fog.
    **Soft edges (R16j2; the user: the map's shadows were much harder than the old ones, the town gate's).** PCSS,
    contact-hardening: after the casters two small passes make the map's depth range per 16 x 16-texel cell
    (`shaders/shadow_reduce.hlsl`: nearest and farthest texel, Gather, shader model 4.1; `shadow_dilate.hlsl`: the same over the cells
    within the widest filter's reach and one cell more, 4 cells at 56-72 units, plus the cell's own nearest; one
    256 x 256 RGBA32F image, read filtered: R25 below). A receiver nearer than every depth around is lit, farther than every one shadowed (most pixels end
    there); near an edge the nearest depth around bounds the radius, and when that bound is wider than half a cell a
    blocker search over 8 cells (Vogel disc) and its own gives the mean distance along the light to the casters
    hiding it; radius = that distance x `kSunSize` (0.025, the tangent of a chosen sun radius far larger than the
    real sun's), 1.5 texels to `kWidestRadius` (0.6 units); then 8 (under 4 texels) or 16 bilinear comparisons on
    a Vogel disc, both discs turned per pixel by interleaved gradient noise (no grain visible at 4K 1:1). Reading
    the map itself for the search (16 texels on a disc, turned per pixel) cost 0.5-1.7 ms at 4K: the scattered
    reads; the cell image fixes that. Tuned on the town gate: its shadow's edge (10-90%, a row across the far
    end at 4K) is about 125 px with the map as with the classic decal (1-2 px before); a creature's shadow stays
    sharp at its feet. Cost (render_stats, vanilla 4K, GPU timestamps, the machine shared with other workers'
    runs, so the least of four runs): frame map / classic zone1 0.77 / 0.54 ms, fight 0.89 / 0.56, town 1.00 /
    0.63 (before, main's run: 0.65 / 0.73, 1.01 / 0.68, 0.76 / -); the map's pass with the range passes 0.12-0.16
    ms (0.03-0.06 before; the reduction reads the whole 64 MB map). Side by side (classic | map before | map
    after): build\R16j2\compare_*.png on E:.
    The game's own shadows (64 x 64 decals, R16e1) are not ported (R26); its shadow building (`RenderShadow`, the
    decal's projection) still runs: the decal's bounds feed the shadow lists. The map is the only shadow look (no
    option: goldens hold no frames).
    Cost when R16j landed (render_stats, vanilla 4K, per frame, map / the old decals): CPU hand-over + textures + end
    of frame + map in zone1 1.48 / 1.88 ms, fight 3.24 / 5.82, town 1.78 / 5.84; the map's own CPU time 0.3-0.6 ms
    (the fit over the terrain vertices); GPU (timestamp queries, harness build) frame 1.10 / 0.76 ms zone1, 1.16 /
    1.07 fight, 0.95 / 0.70 town, of it the map's pass 0.06-0.27 ms.
    Check: scenario render_shadows (image only): zone1's start, a fight, creatures on a slope, the town bz1g, each
    frozen with the map twice (samecapture; `shadowcheck`: map made, casters and receivers, and no object the game
    shadows and drew left out of the map: "drawn but not cast" 0; "not drawn" counts the ones off screen, 1-3 in
    those frames).
    How the map's look differs from the old decals (R16j1; the user's ruin block in the fight and fern in the town):
    every object whose shadow the game draws and which is on screen casts. The decals of overlapping objects
    stacked, each darkening by half, and a decal was a blurred silhouette laid on the terrain, also under an object
    standing higher, so it showed wider and darker; a decal's direction was the sum of the sun and the point lights
    in reach (0051CE60's LightDirection: the town's campfire turned the gate's decal over the fern), the map's is the
    sun's alone (the point lights have their own maps, R16i1, item 9); the fern casts in neither (the game draws no
    shadow for it). Adding casters only ever lowers the map's depths. R16j3 holds what is left (darkness where
    shadows overlap). `run_all --widescreen`'s world view behind the interface differs from addon's (GPU lighting and
    the map).
    **Casters outside the view (R20; the user: shadows popped in at the screen's edge with their objects).** The
    game draws only what it sees, so an object just off screen (or a part of a drawn one that the part's own view
    test left out) cast nothing onto the visible ground. Now the world's draw (WorldClient::Render, after its
    object draws; `world_client_shadow_casters.cpp`) walks the objects that can reach the map and hands over the
    meshes the game did not draw, reading the game's objects only (no flag, pose, list or heap block of the game
    changes; nothing the goldens record moves). Which casters: exact up to bounding-box conservativeness, with no
    distance rule. A caster shadows the visible ground only if its box seen from the sun (the light-space box of
    its bounding sphere: object bounds through its placement and extra scale) overlaps the map's x / y area (fitted
    around the visible ground) and lies on the sun's side of the ground's far depth; towards the sun nothing
    limits it (the receivers fix the far side), so `render::ShadowCasterReaches` tests exactly that and the map's
    depth range starts at the nearest caster that passes (less 2 units), not at a fixed room before the ground
    (the 160 units R16j kept are gone); the caster shader pancakes anything nearer onto the near plane. The
    candidates come from the object grid's 16-unit sectors over the visible ground's world box swept back
    towards the sun by the highest object's top over the ground (scanned every 32 frames) divided by the
    tangent of the sun's elevation, widened by the widest radius (`render::ShadowCasterArea`); each then takes the
    box test. The game's own filters stay: its object draw flags (0x40 objects, 0x80 units), a multiplayer
    spectator's hidden units, CastsShadow's option, class switch, hidden and invisible tests (the strength
    CastsShadow gave this frame when it ran for the object, a unit from half faded in). Each part is placed as
    Object3DClientSpecific::Draw (0051B7E0) places it (object placement x extra scale x part placement; parts
    flagged hidden 0x80 or farther than 0073DE58 skipped; a link figure's part through its ranges, a plain one
    through its buffers, per-vertex offsets added), with its part set's texture; a part the game drew as a
    caster's mesh this frame is left out (`render::CasterPartDrawn`). Cut out by alpha as the on-screen meshes with
    the same texture were (remembered per texture), else at alpha 0.5. Check: scenario render_shadows_outside
    (image only): zone1's start, the town bz1g, and the town with the camera moved towards the viewer until the
    gate is above the screen; each frozen with the outside casters (`shadowcasters all`, the default), without them
    (`view`, as before R20) and with every caster taken from the walk and none from the game's draws (`walked`: the
    frame matches the default at 8 / 0.01%, so the walk places, selects and cuts out meshes as the game's draws
    do). At the start and in the town nothing outside reaches the visible ground (the sun is behind the scene,
    the shadows fall towards the camera: frames the same at 8 / 0.01%); in the moved town the gate's long shadow
    on the road is there with them and gone without them (1.8% of the frame). shadowcheck's second number:
    the least casters outside the view. Cost (render_stats, vanilla 4K, per frame, run alone; outside / view):
    the map's CPU time (fit, walk, hand-over, pass) zone1 start 0.40 / 0.36 ms (7 objects, 18 draws, 704 corners
    outside), town 0.65-0.72 / 0.25-0.29 (26, 44, 35k), moved town 1.2-1.4 / 0.32-0.40 (32, 111, 83k); the map's
    GPU pass within the runs' noise (0.21-0.69 ms either way). The corners are copied every frame (no cache
    across frames yet).
    **Faces turned away from the sun (R22, then R16i; the user: on a mound beside a pine the shadow covered some of
    the mound's faces and stopped at their edges).** With the game's lighting a face turned away from the sun kept
    its light inside another object's shadow (a lit patch with straight edges); R22 tagged the map with caster ids to
    darken it. Since R16i such a face has no sun light to lose (the GPU lights it by N.L), so the ids are gone and a
    receiver darkens by its own facing with a normal-offset bias. Check: scenario render_shadows_mound (the user's
    save in tests/saves/shadow_mound, prepared by tools/copy_save.py; flora shadows switched on in the options
    window): the map's frame three times, the same.
    **Steady soft edges (R25; the user: at the camera distance of the save "ShadowCorrupt", in the town
    bz1g, the shadow under the hide-on-frame sign, whose tip reaches a soldier, shimmered; one zoom step in
    it stopped).** Cause: the receivers read the range image per cell (Load of the pixel's cell, and of each
    search tap's), so the early ends, the bound on the radius (with no search under a cell the radius *is*
    the bound) and the search stepped at the cells' edges: cell-sized squares and straight cuts in a
    penumbra (the soldier's feet in the sign's shadow, in the shadow view below a lighter square block with a
    straight cut beside him). The sun turns every frame (logged per frame: its direction moved by about 0.07
    degrees a harness frame at the save's hour), the map's texel grid turns with it across the ground, and the cells'
    edges crawl over the screen: the squares came and went from frame to frame. The texel snapping, the
    extent (72 units there, steps of 8) and the depth range (nearest caster) were steady; they were not the
    cause. Which radius regime a pixel is in depends on the extent, hence the zoom. Fix
    (`shadow_receiver.hlsli`): the range is sampled with a bilinear sampler (`RangeSampler`, clamp) between
    the four cells around the pixel, and the dilation reaches one cell more (`g_cellsAround`), so each of
    those four cells' bounds holds for all the pixel's taps and their weighted mean is still a bound; the
    search taps read the cells' own nearest filtered too; the searched radius is blended in from the bound
    between half a cell and a cell (no step where the search starts; a search that finds nothing ends lit
    only from a cell on). The widest radius is capped to what kMostCellsAround cells reach (only below a
    22-unit extent). Without a map a 1 x 1 RGBA32F stand-in is bound for the range. Measured in the
    user's frame (Rebirth, 4K, shadow view): the per-cell squares and cuts are gone; the grain of the
    turned taps still changes as the sun turns (frames at 8 ms: 4-9% of the soldier's region changes by
    more than 8 of 255 in the shadow view between frames, about the same before and after; in the real
    frame by more than 3 on 2%), which more taps or reuse across frames would calm (not done). The frame
    otherwise changes little (0.07% of the pixels by more than 8). GPU within the runs' noise (the machine
    shared; render_stats town 1.26-1.36 ms after, 1.70-2.64 before in the run after it; the dilation's
    81 reads a cell against 49 is about 2M more reads of a 1 MB image). Harness build: `shadowview on`
    shows the shadow alone (white lit, black at full strength), `shadowshift <n>` moves the map's origin by n
    x n/2 whole texels (the same texels on the world, other cells). Check: scenario render_shadows_shimmer
    (the user's save in tests/saves/shadow_corrupt; flora shadows on): frozen, the shadow view against the
    same with `shadowshift 7`, more than 64 of 255 apart on at most 0.03% (Rebirth 0.013%, vanilla 0.006%;
    with the per-cell reads 0.051% and 0.016%: the check catches the old reads on Rebirth's data only),
    and four frames at 8 ms for the eye.

11. **R16k, R26c The device's mesh draws without the work nothing reads (done).** `device_mesh.cpp`, `device_shadow.cpp`,
    `device_draw.cpp`. The mesh draws 004E9010 / 004E9A50 and the flat draw 004EA4A0 project (the visibility verdict),
    override the colours (flag 4: the hand-over reads them), take the screen rectangle for draw flag 1 (picking:
    BoundsOfProjected of the screen or clip-space positions, section 3) and hand every pass to the device (`DrawIndexed()` / `DrawTwoTextures(first, second)`: the pass state the new renderer reads).
    Not made: TL vertices, the fog, the z scale, and since R30f the CPU clip of a crossing mesh (ClipTriangles,
    ToScreenClipped; its readers were the rectangle, now the bounds of the projected positions, and the empty result's
    "no pass": a crossing mesh's passes always go over, the GPU clips them; the flat draw hands a crossing shadow's
    pass over unclipped). R26c took out the arguments of the Direct3D 7 draws and
    the work only they needed; until then a harness verb `meshcorners full|read` switched back to all the old work
    for an A/B check, 0 pixels different; R26d dropped the verb.
    R30f measured (render_stats, vanilla 4K, harness build, one run each, main's run of the same morning as before):
    "device mesh draws" ms a frame menu 0.249 -> 0.205, zone1 0.194 -> 0.165, fight 0.352 -> 0.286, town 0.747 ->
    0.394, town at night 0.586 -> 0.332; flat draws unchanged (0.01-0.1 ms); main-thread CPU within its noise (town
    11.28 -> 9.43, fight 20.11 -> 17.79). The goldens (picking reads the rectangles) stayed the same.
    Measured (render_stats, vanilla 4K, harness build, two runs; ms a frame; "device mesh draws" read / full in the
    same run, the full stretch between two read ones): zone1 0.094-0.104 / 0.111-0.115, fight 0.23-0.26 / 0.27-0.29,
    town 0.31-0.34 / 0.49-0.51, so 0.02 / 0.04 / 0.18 ms saved. Main-thread CPU (read, full, read again; the
    machine shared with other workers' runs): zone1 6.65, 5.87, 6.70 and 6.15, 5.71, 6.28; fight 18.80, 19.96,
    19.54 and 19.95, 19.00, 18.97; town 8.11, 8.48, 8.17 and 7.67, 8.90, 8.21 (main's last run before R16k, under
    heavier load: zone1 13.62, fight 21.98, town 15.46). The gain is within the main thread's noise: the device
    path was never the cost; what is left of it (0.1-0.3 ms) is the projection, the clipper and the passes' state,
    which game code or the hand-over read. Check: render_scene, models and units capture the frozen frame with
    `meshcorners full` and compare it with the default (0 pixels differ; R16k, the switch is gone); the goldens (picking reads the
    rectangles; the digest) are the same in the image's suite.

Not worth a ticket now: pipeline and texture-binding changes (2-13 / 4-66 a frame) and the state reads per pass (part
of the 0.5-1.6 ms hand-over).

12. **R37 Profile of the heaviest scenes (2026-10-29; follow-ups R37a-R37i).** The user's play settings: 3840 x 2160
    (`resolution desktop`, windowed), msaa 8, anisotropy 16, flora shadows on (switched on in the options window as
    render_shadows_mound does), Rebirth's draw radii, `frametime 8`; zone1, zone7 (forest), zone11 (snow forest),
    zone19 (16 x 16 sectors), the town bz1g, the camera at its default, at pitch 30 (the settings' least: the lowest
    the player can tilt toward the horizon) and 15, distance 50; noon and 23:00; the town at night with render_stats'
    crowd of 12 fighting. One game alone; 120 frames a scene. How: the harness build made release-like for the run
    (sokol's validation off, `ReadBackDue` false, the cache checks off) with a temporary probe (not committed): a
    timestamp query at each change of category of the frame's work (sun map, range passes, point map, terrain,
    water, objects, units, other models, flat, pre-transformed, interface, passes, present), the QPC time per
    category inside EndFrame, `sg_query_stats` counts per category (draws, pipelines, bindings, uniforms, the D3D11
    calls) after `sg_enable_stats`, triangles from a count in `sg_draw`; casters per light from DrawShadowMap /
    DrawPointShadows; a harness step setting `Camera::pitch` (radians: degrees x CAM_DEGREE; 15 looks at the horizon,
    60-75 top down) and `distance`. CPU hot spots: `EI_PROFILE=1` (self samples only; the totals' stack scan is not
    reliable). Results (Rebirth; vanilla within 10-20%):
    - **The main thread is the limit**: 1.7-3.8 ms a frame in the zones and the town, 5.9-7.4 ms with the crowd; the
      GPU frame 1.5-2.6 ms (msaa 1: about 0.5 ms less). Wall time equals CPU time (no GPU waits).
    - **Shadows are 33-57% of the main thread**: the renderer's shadow CPU (`shadowMs`: fit, the walk of outside
      casters, the passes' submission, point shadows) 0.5-1.9 ms, plus the game's object-shadow projection kept for
      its bounds (`decalShadowMs` + `shadowBuildMs`) 0.3-1.2 ms. In the crowd: PreparePointShadows 0.44 ms, sun map
      submission 0.32 ms (573 draws), point map submission 0.23 ms (453 draws), the fit and walk in the world's draw
      about 0.4 ms. Self samples over all scenes: driver and runtime 18% (d3d11 7%, ntdll 8%), the map's fit / walk /
      caster boxes 13% (FitMap 4.3, PositionBox 3.6, ShadowCasterMesh 3.1), the game's frustum test 12.7% (ClipCode
      7.8%, 13.7% in the forests), the game's decal projection 10.8%.
    - **Draws**: shadow draws are 50-65% of all (town crowd: 571 world + 573 sun + 453 point). Each draw is one
      bindings call (2 SRVs, 2 samplers) and two uniform blocks (two constant buffer updates); triangles are few
      (25-60 K a frame). Tilting low raises world draws 1.5-1.7x in zone1 and the town; the map's extent stays
      104-136 units (zones), 72-88 (town).
    - **GPU per pass** (ms): terrain 0.35-0.7, objects 0.16-0.6, msaa 8's resolve and copies 0.3-0.42, sun map
      0.05-0.35 plus the range passes 0.09, point map up to 0.19 (crowd), present 0.15; on the battle screen 0.27-0.38
      at the frame's start on the interface canvases' 13 draws, possibly a gap between stamps (R37h).
    - **Casters per light**: sun 83-290 objects, 190-570 caster draws (in view + outside); the town's 4 mapped point
      lights 4.25 faces each, 44 draws a light (crowd 113, most 397), redrawn every frame though lamps and buildings
      never move (R37a).
    Follow-ups by expected gain: R37f (the game's projection replaced by a sphere, the user's call: 0.3-1.2 ms), R37a /
    R37b (point shadows kept across frames, their preparation: up to 0.6 ms in the crowd), R37g (the frustum test:
    0.2-0.5 ms in forests), R37c (caster boxes and outside casters kept: 0.2-0.4), R37e (shadow casters' calls: 0.3),
    R37d (the fit: 0.1-0.25), R37h (the canvases' GPU), R37i (the overlay split per pass, for visibility).

### Loose .bmp textures (A-I23a)

- Scenario `render_loose_bmp` (vanilla golden from three agreeing original runs): `# prepare: make_loose_bmp.py`
  writes `textures\Zone1000..Zone1007.bmp` (24-bit, four quadrants: red and green above, blue and yellow below) and
  an 8-bit `a_<name>.bmp` ramp for each into the run's overlay (the install has no `textures\*.bmp`; the harness's
  FindFirstFile / CreateFile redirection finds them). `CTextureManager::LoadTexture` 006922A0 then loads zone 1's
  terrain pages through CTexture::LoadBmp 00690260 -> LoadBmpFiles 0068FBF0 -> Dib Construct / Read / Size /
  MakePalette. The pages are format 5:6:5 (no alpha), so the alpha file is read and its depth checked but its bytes
  are unused; a texture of an alpha kind (types 1-4, 8, 6, 13) is not loaded from a .bmp by any scenario yet.
- Pixels: `hues <frame> <least> <most>` (`capture::HueShares`; skipped in the original's run) needs each of the four
  hues on 8%..25% of the 4K frame (measured: red 16.4%, green 14.7%, blue 13.6%, yellow 16.0%). A one-time
  comparison of the original's frame (dgVoodoo) with the image's, `compare_frames.py` at WideScreen's settings: 3.5%
  of the pixels differ with the .bmp pages against 2.6% without them (the renderers' own differences: the hero, the
  shadows, filtering), so the quadrants sit where the original draws them (a flipped or channel-swapped page would
  differ over the 60% of the frame the pages cover).

## 14. The drawn view (smooth camera motion)

User 2026-10-21: at 1x speed on a 120 Hz screen the battle camera moved jerkily. Why: the world ticks every 55 ms
(0078FF34). The main loop (`RunPass`, src/game/app_main.cpp) moves the camera once a frame by the frame's real time
(cut into whole ticks), and `RunWorlds` moves it again by a whole tick after each client tick, so half of every pan,
turn and zoom arrives in jumps of 55 ms of motion about 18 times a second. The original does the same.

The camera (`Camera`: its timing, tracking, height lag, the sound listener) stays as the game has it; only the pose
the world's draw shows changes (always on, no option). User 2026-10-29 played it at 120 Hz and kept it. (R18's
option moved the camera itself once a frame by time and changed the game state: with it all 121 goldens differed
from boot tick 20. A second variant drew between the poses of the last two ticks and lagged 5 frames. Both are gone.)

- **Spread:** around each client tick's camera update RunWorlds notes the pose before it and the step it made
  (`camera::NoteViewBeforeTick` / `NoteViewTick`: position, focus, distance; the turns as the rotations that undo
  them). Once a pass `camera::SetViewProgress` takes the client's part of the next tick (its backlog over a tick,
  0..1). `camera::DrawnView` is the camera's own pose less the step times the part not reached: all of the step just
  after the tick, none a tick later. No tick for longer (a paused or stalled client): the backlog passes a tick and
  the camera's own pose is drawn. A pass longer than a tick (fewer frames than ticks, where each frame sees whole
  ticks anyway) spreads less, linearly to none at two ticks: the harness's default pass of five ticks draws the
  camera's own pose. That matters to the goldens: picking reads the drawn view, and with the last step held back on
  such frames (R35a's first try) 7 of 121 differed (a click or a client unit a step off).
- **Cut:** a tick whose update moved the focus by more than 8 map units, or the camera nearer to its focus or
  farther from it by more, placed it anew (a map change, a jump of the focus): no step, shown at once. A turn, pan,
  tilt, zoom or lift stays within both in a tick however fast it goes. (R35's test, more than 8 units of travel or about 0.4 rad of
  turn in a tick, took a fast spin for a cut: see "Spinning".)
- **Where it is read:** `WorldClient::Render` takes the frame's pose once at its start (`camera::TakeFrameView`);
  what the world's draw places by the camera reads `camera::FrameView()`: the view matrix (`view::ViewMatrix`), the
  sky dome, particle billboards (particle_draw.cpp), lightning's glow (lightning_draw.cpp), rain and snow's band
  (weather_particles.cpp), the lighting's eye (`HandOverSceneLights`) and the old shadows' axis. The terrain's
  visible sectors (`SetVisibleSectors`) and their level of detail (terrain_passes.cpp `ChooseDetail`) keep the
  camera's own position (within a sector of the drawn one; keeping them on the camera is one less thing that could
  reach the game state). The listener stays with the camera. Picking reads the last drawn frame's matrices (the
  view stack `Render` pushes, and the objects' `dirtyRect`s drawn by them), so a click picks what the screen shows.
- **Lag:** the drawn view trails the camera by up to a tick's step (on `camera_smooth`, R35: the path fits the
  camera's 2 frames (16 ms) later; up to 30 px apart on a pan, 71 px on a turn at 1280x720).

### Spinning (user 2026-10-29: "spinning the camera sometimes shakes a lot")

Measured on `camera_spin` (8 ms frames; harness `camtrace start log` writes each frame's own and drawn pose,
`mousedrag` turns the camera by a right-button drag; the same steps run on game.exe with the camera read through
exe_objects.h):

- **The original's part:** zoomed in (distance 10) and tilted low (30 degrees), a turn swings the camera over the
  ground and houses behind it. `ClampAboveObjects` (0040E4C0) keeps it above the highest terrain within 2 units and
  the objects within 16 (fading over 10) plus CameraMinHeight, and `ApplyView` then aims it back down at the focus:
  over one turn it rose from 18.98 to 27.39 and its pitch went from -30 to -53 degrees and back. At 120 Hz each
  tick's turn arrives at once, so the lift came in steps of up to 1.3 units and 5 degrees 18 times a second (the
  camera's height: largest second difference 1.29-1.45 per frame^2 over four spins). game.exe's camera and ours were
  the same to the last digit over all 1760 frames of the four spins (keyboard): the bobbing and its tick steps are
  the original's. The focus does not move in a spin (its height stays), so the ground under the look point plays no
  part.
- **What the drawn view did:** it spreads those tick steps (largest second difference 0.23-0.27 in height, 0.016
  rad in pitch against 0.098), and adds nothing at keyboard speed (the same travel, fewer reversals). But its cut
  test took any tick that turned the camera more than about 0.4 rad or moved it more than 8 units for a cut and
  drew that tick's step at once. A brisk drag zoomed out crosses it (drag 160 counts a frame at 1280x720: 0.43 rad
  a tick): the drawn view then jumped at every tick (45 jumps in 340 frames, as many as the camera's own), and with a
  real mouse's uneven counts some ticks cross it and some do not, so the view alternates between smooth and jerky:
  the shake. Ours; fixed by the cut test above (now 0 jumps for the same drag).
- The mouse turn's rate is the drag in layout pixels (the counts scaled from the screen to 800x600): the same counts
  turn the camera 3 times faster at 1280x720 than at 3840x2160 (the original's runs here). The original's.

### Checks

- `camera_smooth` (image only, vanilla and Rebirth): pan (an arrow key, 110 frames, then 60 to rest), turn (Ctrl),
  zoom (Page Down) and follow (Home, the hero walking). `camtrace` keeps each recording as the drawn view (`pan`)
  and the camera's own (`pan.camera`). `camsame` (3%): the drawn view covers what the camera does (pan 13.93 / 13.91,
  turn and zoom the same). `camsmooth`: the drawn view without a jump, the camera with (pan 23, turn 20, zoom 15: a
  jump is a frame whose step is more than twice both neighbours').
- `camera_spin` (image only): the low keyboard spin (turn without jumps; `camsteadier`: the drawn height's largest
  step at most 0.35 of the camera's, measured about 0.26; what is left are the game's own changes within a frame)
  and the fast drag zoomed out (turn without jumps, the camera's own jerky).
- Harness for it: `keydown` / `keyup` (a key held over frames; the navigation keys extended as from a keyboard, which
  the game's key table needs), `frametime <ms>`, `mousedrag <dx> <dy>`, `camtrace` / `camsame` / `camsteadier` /
  `camsmooth` (tests/verify/harness_camera.h). camtrace also runs on game.exe (no check there needs it).

## 15. The performance overlay (R21)

User 2026-10-22: a small performance overlay in the standalone, switched on from the settings file, no UI.

- **Switch**: in ei_settings.ini next to ei_standalone.exe (docs/notes/standalone.md, "Settings file"; play.cmd's is
  run\play\ei_settings.ini):
  ```
  [Software\Nival Interactive\EvilIslands\Standalone]
  "PerfOverlay"=dword:00000001
  ```
  Off when absent or 0. Read once the renderer starts (`PerfOverlaySetting`, device_object.cpp, after
  `render::StartPresenting`) into `render::SetPerfOverlay`. **Never in a harness run**: the harness build
  (EI_RENDER_VALIDATION) has `render::PerfOverlaySupported()` false, so the value is not even read (the pinned
  settings file lacks it, and a read of a missing value fails a run) and SetPerfOverlay keeps it off.
- **What it shows** (top left of the game image, refreshed every 250 ms, averages over the last second):
  `FPS` and the frame time (between presents, so vsync's wait counts) with the worst frame; `GPU` the frame's draws
  and the present (worst too), split into `SHADOW` (the shadow map's passes), `WORLD` (the frame's draws up to the
  end of the world view, the shadow map left out; the interface canvases' and uploads' work queued before it counts
  here), `UI` (the rest of the frame: the interface) and `PRESENT` (the window's pass: the frame through the gamma
  ramp, the cursor, the overlay); `CPU SUBMIT` the main thread's time in EndFrame's draws up to the present (the
  present's wait left out; draws already made by a split earlier in the frame are not in it) and of it `SHADOW` the
  shadow map's fitting and pass; `DRAWS` FrameStats::draws and the shadow map's draws. `GPU -` until timestamps
  come back (or when the device has none).
- **How** (`render_perf.{h,cpp}`, `shaders/perf_overlay.hlsl`): six timestamps a frame (frame begin at the first
  DrawPending, around DrawShadowMap, after the last world command, before and after DrawToWindow) in a disjoint
  query; five sets in flight, read with `DONOTFLUSH` when a later frame begins (never waiting; a set not ready waits
  for a later frame, a disjoint one is dropped). A mark not reached (no shadow map, no world) takes the one before
  it. The text is a built-in 5 x 7 font of capitals, digits and `.:/-+%()` drawn into an R8 image (digits brighter
  than labels), uploaded when the text changed, and drawn as one quad by whole-pixel scale (`Scale`: one per 540
  rows of the game image, so 4 at 2160) over a 60% black box, in the present's pass after the frame and the
  cursor (the cursor passes under it).
- **Never in the frame target**: captures, frame checks, the Scroll Lock screenshot, the movie recorder and the
  escape menu's / save game's pictures read the frame target, which never holds it. The cursor thread's presents
  between frames (a frame later than 40 ms, R9c2) show the last frame without it.
- **Cost**: off, a flag test at each entry point (the commands' world test only when on). On: six query Ends and a
  few non-blocking GetData calls a frame, one 6-vertex draw in the present's pass, a CPU text image of about
  250 x 50 bytes made and uploaded at most four times a second.
- **Check**: `ei_perf_check` (tools/perf_check, run by build.cmd) on a WARP device: the lines of known numbers, the
  font, the image's pixels; the overlay over a cleared BGRA8 target at 720 and 2160 rows against the image scaled up
  (every pixel); 40 frames of timestamps (the GPU times come back, the passes within the total). No harness
  scenario: the overlay is never on in a run.

## 15a. The vertical blank switch (R23)

User 2026-10-22: a way to disable vsync, from the settings file, no UI.

- **Switch**: next to PerfOverlay (section 15), same file and key:
  ```
  [Software\Nival Interactive\EvilIslands\Standalone]
  "VSync"=dword:00000000
  ```
  On when absent or any value but 0 (the default: `Present(1, 0)`); 0 turns it off. Read once the renderer starts
  (`VsyncSetting`, device_object.cpp, after `render::StartPresenting` and before the first present) into
  `render::SetVsync`. **Never in a harness run**: `render::VsyncSwitchSupported()` is false in the harness build
  (EI_RENDER_VALIDATION), so the value is not read (the pinned settings file lacks it) and SetVsync does nothing; the
  harness's own `SetWaitForVsync(false)` under the virtual clock stays interval 0 *without* tearing flags (its chains
  are made as before).
- **Off**: sync interval 0. Where the system supports tearing (`IDXGIFactory5::CheckFeatureSupport`
  `DXGI_FEATURE_PRESENT_ALLOW_TEARING`, `TearingSupported` in render_window.cpp, asked of the factory at each chain
  creation) the flip-model chain is made with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` and every present carries
  `DXGI_PRESENT_ALLOW_TEARING`: without it a windowed flip-model chain stays composed and capped at the display's rate
  even at interval 0. Without support: interval 0 and no flags (it may still be capped by the compositor). `ResizeBuffers`
  takes the flags the chain was made with (`g_chainFlags`), and Present's flag follows them (the flag is only valid on a
  chain made with it). Both present paths use the same values: render_window.cpp's `PresentWindow` and the cursor
  thread's between-frames present (render_cursor.cpp; `WindowParts` carries the sync interval and flags, no longer a
  bool). The rules are `ChainFlagsFor` / `SyncIntervalFor` / `PresentFlagsFor`.
- **Nothing else relies on vsync pacing**: game time is the world updates (up to five of 0078FF34 ms per frame, see
  the timing line in section 3: vsync only changes how they group into frames); the cursor thread presents only after
  40 ms without a main-thread present, so unlocked frames never reach it; the overlay's frame time simply stops including
  the wait. An unlocked game runs the main thread flat out (a core at 100%, the GPU as fast as it goes): that is the
  point of the switch, not a bug.
- **Check**: `ei_perf_check` (tools/perf_check, run by build.cmd), the `vsync` part: the rules for every combination
  (tearing wanted / supported, vsync on / off), and on a hidden window's flip-model chain on the WARP device made with
  them: the flags it reports, `ResizeBuffers` with them, a `Present` with the switch's interval and flags succeeds
  (the chain made with tearing only where the device supports it; the check prints which). No harness scenario (the
  harness never has the switch).

## Rain and snow draws

The rain and snow draws of the world's rain / snow layer: two functions,
thiscall with the scene renderer, returning 1; the layer's draw
(weather_draw.cpp) calls the one of the mode after the fade's step and the texture's bind.
Both walk the 81 x 81 cells (half a unit apart) around the point the wind carries a particle to
the camera from. A cell's place gives a 16-bit number; with the world's time it gives the cycle
the cell's particle is in (a rain drop falls for 200 ticks, a snowflake for 1000) and how far it
has fallen. `density` of 64 cycles show a particle; the cycle also shifts it within the cell and
picks one of the texture's four frames. The corners go to the pipeline's projection as packets of
four, the ones on screen become pre-transformed triangles drawn with the device's current state.
Kept as the game has it: the vertices' colour and specular are never written (what the
buffer held stays, the distance fog only lowers the specular's alpha), the packets' w neither;
the rain's height uses the snow's rise (kSnowRise; both are 40); a height that is not a number
would loop forever in the rain's wrap. See docs/notes/renderer.md, section 12.

## The renderer's source files

What each file does at large, moved from the files' headers.

### render_cursor.cpp: the cursor between frames

The software cursor between frames in the switch-over without DirectDraw (docs/notes/renderer.md, section 9). The
game's cursor thread (every 41 ms) drew the cursor straight onto the primary surface when it moved, so it kept
moving while the main thread drew no frame (a long frame, a blocking load). Without DirectDraw the cursor thread
hands the move here instead (render::CursorBetweenFrames): the last frame presented on the game window, which holds
no cursor (the present draws it over the frame), is presented again with the cursor at its new place. The main
thread keeps using sokol alone. The cursor thread makes no sokol call: it records its present on a Direct3D 11
deferred context with the raw objects of sokol's present pipeline (the frame target and the cursor through the
gamma ramp into the back buffer: the draw the main thread's present makes) and runs it as one command list on the
immediate context (ExecuteCommandList, which puts the context's state back, so sokol's state cache stays right),
with the device's multithread protection on. The swap chain and the frame target are shared: the main thread's
frame draw and present on the game window and the cursor thread's present take turns under one lock, and the main
thread takes the references the cursor thread draws with at the end of each of its presents (and drops them when it
begins the next frame's draws: the frame target is partly drawn until then).

### render_mesh.cpp: meshes

Meshes in the new renderer: everything the old path draws through the device's mesh draws (figures and map objects,
the sky, the interface's sprites (UiMesh) and 3D previews). The game hands the mesh over in model space with the
matrix it projects it with (BeginMesh); each pass the old path then draws (src/game/device_draw.cpp) becomes a
batch with the render state of that moment (MeshPass, section 5): blend, alpha test, depth, culling, fog, the two
texture stages' operations, filters and textures. A figure mesh drawn through the figure part draws is kept on the GPU
(render_mesh_cache.cpp) and its batches draw from that pool; every other mesh is built into the frame's stream. A
figure part handed over with its lighting (MeshSource::lighting) is lit in the shader from its normals.

### render_mesh_cache.cpp: meshes kept on the GPU

Meshes kept on the GPU (docs/notes/renderer.md section 13, item 4). The figure part draws
(src/game/figure_part_draw*.cpp) name the figure mesh a mesh draw draws (render::SetMeshSource); the mesh's corners
are kept for that figure mesh (positions and texture coordinates in model space, colours and triangles) in a pool
on the GPU (GpuPool: geometry and colours in streams of their own). A frame hands over the mesh as kept; what is
built and uploaded again is what the game says changed:
- the colours, when the draw wrote the colour arrays or the game lit the mesh outside its draw (MeshChanged
  Colours: the sky; a link figure's LightPart / FlatColours) or a draw that wrote them was not kept (NoteMeshDraw):
  read and compared with the kept ones, the changed range uploaded;
- the positions, when the game moved them (MeshChanged Positions: per-vertex offsets, FigureMesh::SetOffsets; a
  link figure's part morphed again): read and compared, the changed range uploaded;
- all of it, when the mesh's buffers changed (MeshChanged Rebuilt) or the draw hands over other arrays or counts
  than it was built from. Interface sprites (render::SetInterfaceDraw) and a mesh drawn again in the same frame
  with other data go into the frame's stream. The texture offsets (an animated mesh's frame of its sheet) are the
  draw's, added in the shader. The harness build compares every kept mesh with a fresh build every 16th frame
  (`meshcache check`); `meshcache off` builds every mesh into the frame's stream.

### render_point_shadows.cpp: point-light shadows

Point-light shadows (docs/notes/renderer.md section 13, item 9). The sun's map (render_shadows.cpp) shadows the sun
alone; here the few strongest point lights in view (the campfire, the lamps; an object's own light, the one a
party unit carries at night, casts none: ScenePointLight::shadows) get a cube of six depth faces
each, drawn from the same casters (the objects the game shadows, by their meshes drawn this frame), and the
receivers lit on the GPU take each such light away where it is hidden: the ground (shaders/scene_lighting.hlsli's
point loop) and the figure parts (shaders/mesh.hlsl's figure_light). The receivers' rules follow the sun's
(shaders/point_shadow.hlsli):
- an object never shadows itself from a light inside it or just beside it (the campfire's spit, a lamp's
  post): an object with a mesh whose bounding sphere, grown by kHoldMargin, holds the
  light casts nothing for it;
- a unit's meshes compare from the unit's nearest distance to the light (as the sun map's unit front): no shadow of
  its own, every other caster's on every face;
- other meshes are shadowed only on faces turned to the light (by their face normal), looked up a little along it
  (the normal offset). The lights' cubes share one depth atlas: a light a row of kPointFaceTexels-texel faces, one
  per column. What is left out: the ground casts no point shadow, nor objects outside the view (that walk is the
  sun map's only); lights beyond the kept scene lights (render::SceneLighting's nearest kMostSceneLights) cast
  none.

The faces are kept across frames (R37a). A light keeps its row while it stays where it is with the same range
(`GiveRows`; the strongest-first order may reshuffle, the rows do not), and a face is drawn again only when its
light or the list of its casters changed. A caster is the same when its kept mesh and `CachedMeshVersion` (bumped
whenever its corners change), index range, texture, sampler, cut-out and texture offset are, and its model matrix
is within `kKeptPlace` / `kKeptTurn`: the matrix into the landscape's space is the camera's inverse times the game's
camera-space matrix, so a building moves by up to ~0.0015 units of rounding from frame to frame (measured in the
town); a mesh of the frame's stream is never the same. Only the faces drawn are cleared (a solid-shader rectangle
at depth 1); a face known empty stays clear whatever its light. The units' idle animations move them every frame,
so a face holding a unit is drawn again whole every frame: in the town at night (Rebirth, 4K) 4 of the 17 faces,
139 of 174 draws (the crowd of 12: 417 of 450); the point map's CPU and GPU time stayed within the machine's
noise (alternating runs kept / all drawn, set against the sun map's time in the same frames). Tried and dropped:
two layers a face (the still casters in an image of their own, copied into the atlas with SV_Depth, the units
drawn over it): 101 / 378 draws, but the copies' GPU time outweighed the 40 draws (the point map's GPU time
against the sun map's 0.55 -> 0.8). PreparePointShadows (R37b) keeps its working lists across frames, makes one sphere per mesh
(BoxSphere: one root of the largest corner square) and compares squared distances, taking a root only where a
test is too close to tell (`NotFurther`: the image's CRT root is a call with control-word work); its results are
bit-equal to the plain distances'.

### render_shadows.cpp: object shadows through a sun shadow map

Object shadows through a sun shadow map (docs/notes/renderer.md section 13). The game hands over the sun, which
object each mesh belongs to (BeginCaster) and which objects cast this frame (CastsShadow). Once a frame, before the
world draws, PrepareShadows fits an orthographic view from the sun around the visible ground and gives every
receiving batch the matrix from its model space to the map; DrawShadowMap draws the casters' meshes into the map
(depth only; alpha-tested and blended passes cut out by their texture's alpha), then the map's depth range per
cell. Objects the game does not draw whose shadow can reach the visible ground cast too (ShadowCasterMesh, tested
against the map's fit made right then). The receivers lit on the GPU take the sun away by their normals' facing,
the others are shadowed on their faces turned to the sun; both look the map up a little along their normal. A
unit's meshes compare from the unit's nearest depth in the map (FigureFronts): no shadow of its own, every other
caster's on every face, no normal offset. The point lights' shadows (render_point_shadows.cpp) are prepared and
drawn from here, inside the shadow map's time. Space: the meshes' matrices are clip matrices, the terrain's the
same for the landscape's space, in which the map is fitted.

### render_terrain.cpp: the terrain

The terrain in the new renderer. The game's terrain passes (ground pages, water pages) call DrawSectorPage
(SectorData::DrawPage, src/game/terrain_passes.cpp) for every visible sector page; it hands the page here. The page's tiles are turned into
world-space triangles at the sector's level of detail (the same tile entries and index patterns as the old path),
with the vertex colours the game's lighting wrote into the sector's vertex records, the tile texture coordinates
and the page's texture; the clip matrix is the one the old path transforms the same vertices with (the pipeline's
stack at +0x24). The GPU clips; no software clipping or projection here. The pages stay on the GPU
(docs/notes/renderer.md section 13): each sector page at a level of detail and seam variant is built once into a
pool of vertex and index buffers (positions and texture coordinates in one stream, the lit colours in another) and
built again only when what it was built from changes (the sector's tile lists and records: SectorData::Build,
render:: TerrainChanged). When the game only moved vertices or texture coordinates (the animated tiles: the pages
holding one; the waves and the water levels, which move only the water layer: the pages reading it) the page reads
them again and uploads those that changed. The colours of the page's visible quarters are compared with the records
every frame and only the changed ones uploaded. A frame keeps the visible quarters of each page handed over
(TerrainPiece).

### render_perf.h: the performance overlay

The performance overlay (docs/notes/renderer.md, section 15): frames a second, the frame time (average and worst
over the last second), the GPU time of the frame's passes (timestamp queries, read a few frames late without
waiting), the renderer's CPU time and the draws, as a few lines of text in the frame's top left corner. Drawn by
the present into the game window's back buffer after the frame and the cursor (render_window.cpp, DrawToWindow),
never into the frame target: captures, screenshots, the escape menu's and the save game's pictures and every frame
check read the frame target, so none of them holds it. Off unless the settings file switches it on
(render::SetPerfOverlay); the harness build never draws it. Its own text: a 5 x 7 pixel font of capitals, digits
and a few signs, drawn into a one-channel image (0 the box, else the text's brightness) and scaled up by whole
pixels with the game's height (Scale).

### shaders/mesh.hlsl: figure_light

A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting), at the model-space point `p` with
the normal `n` (as the mesh holds it): per channel the largest of the ambient colour, the sun's lit colour times
the cosine to it and each light's lit colour times its cosine and falloff, plus the emissive colour, at most 1. The
crown: pulled towards its average colour, taken as the average over every direction a normal can face (the mean of
max(ambient, sun x cosine) for a cosine spread evenly over -1 .. 1). The shadow map darkens it after (ps_main), as
it darkened the CPU-lit colours before. A light with a point shadow (point_shadow.hlsli) adds only what reaches
`land` (the point in the landscape's space; `face`: its face normal there, turned to the camera): a unit's mesh
compares from the unit's nearest distance to the light, on every face; any other only on faces turned to the light
(by `face`: the smoothed normals lean far from their faces), looked up moved along `face`.

### shaders/scene_lighting.hlsli: ground_light

The landscape lit as the game's vertex lighting lit each vertex (ShadeSun 006C36B0, then LightVertex 006C31C0; not
ported), at a point `p` (the landscape's space) with the unit normal `n`: `m` the material colour the sun and ambient
light start from (the water's; on the ground the water's above it, else black), `mp` the one the point lights start
from (the record's own), `loss` the light lost under the water (0 above it), `hidden` how much of the sun the shadow
map hides at the point (the game's ray march gave 0 or 1) and `strength` the map's darkening
(render_shadows.cpp kStrength). Ambient and direct light apart: in the sun max(material + facing x sun, ambient),
in the shade ambient + material, as the game lit them; but a shadow is never lighter than the sunlit light times 1
- strength (the old decals' and the map's look before: where the ambient light is nearly the sun's, as in the
towns, the game's shade alone hardly shows), on faces turned to the map's light (by their facing to it: the map's
sun, at least 20 degrees high, is what casts; a face turned away keeps its light, its own object's shadow there is
no acne). The point lights (each material + its light, at most 1 per channel, by its falloff and facing, dimmed
under the water by the light's vertical part) take the largest per channel, darkened by the same shade (the towns'
lamps light the ground by day too: the units' shadows would vanish under them; the point lights cast no shadow of
their own), the specular ones (shaded alike) apart (`spec`, added after the texture as the specular word was). A
light with a point shadow (`shadows`: point_shadow.hlsli) adds only what reaches the point; the ground casts none,
so the point is looked up moved along its own normal. The colour word's light is returned.

### shaders/shadow_receiver.hlsli: the receivers and the soft edges

The shadow map's receivers' part (render_shadows.cpp; included in the terrain and mesh shaders): how much of the
sun the map hides at a pixel (0 .. 1; the receivers darken by it times the strength, on.y, or take the sun's light
away by it). `s` is the pixel's position in the map's clip space, `dsx` / `dsy` its screen derivatives (taken
before any discard), `pixel` its screen position. Every comparison follows the receiver's plane (its depth slope
across the map, clamped). A receiver weighs it by its facing to the sun, so a face turned away is not shadowed (its
own object's shadow there would be acne): the ground by its normal, the meshes by their face normal
(`shadow_facing`, the derivatives). A unit's meshes instead compare from the unit's nearest depth (`front`): no
shadow of its own (that flipped face by face on a low-poly creature), every other caster's on every face, its sun
side shaded by its lighting's N.L alone. Soft edges, contact-hardening as PCSS. `range` holds, per kRangeCell-texel
cell of the map, the nearest and the farthest depth of the cells within the widest filter's reach and one cell more
(x, y), and the cell's own nearest (z). It is read filtered (`range_smp`: bilinear between the four cells around
the pixel). Each of those four cells' bounds holds for every texel the pixel's taps read, so their weighted mean
does too, and the bounds change smoothly across the map instead of stepping at the cells' edges (read per cell,
they made cell-sized squares in a penumbra, which crawled while the sun turned the map's texels across the ground:
the user's shimmer). A pixel nearer than every depth around is lit, one farther than every depth around is
shadowed. For the others (near an edge) the nearest depth around bounds the filter's radius: the distance along the
light to it times the sun's chosen size (soft.x), from soft.z to soft.y. When that bound is wider than half a cell,
a blocker search narrows it: kSearchTaps points on a disc of the bound and the pixel's own give the mean distance
from the receiver to the nearest caster around each (the cells' own nearest, filtered) that hides it, the radius
that distance times soft.x (so a shadow is sharp where its caster meets the ground). From half a cell to a cell the
searched radius is blended in from the bound, so the radius does not step where the search starts. Then bilinear
comparisons on a disc of the radius: kFilterTaps, or kFewTaps on a narrow one. Both discs are Vogel spirals turned
per pixel (interleaved gradient noise of the screen position): the few taps make a fine grain instead of bands. The
search reads the small cell image, not the map: the map's texels far apart and turned per pixel cost far more.

### game/world_client_shadow_casters.cpp: casters outside the view

The casters of the new renderer's shadow map that the game does not draw this frame. The map
(src/render/render_shadows.cpp, docs/notes/renderer.md section 13 item 10) takes the meshes of the objects the game
draws; an object outside the view, or a part of a drawn one that the part's own view test left out, can still throw
its shadow onto the visible ground. After the world's object draws (WorldClient::Render) every object standing
where such a caster can stand (render::ShadowCasterArea, through the object grid's sectors) whose bounding sphere
passes the map's test (render::ShadowCasterReaches: inside the map's area seen from the sun, on the sun's side of
the ground it covers) hands its undrawn parts to the map (render::ShadowCasterMesh), placed as
Object3DClientSpecific::Draw places them. Everything here only reads the game's objects: no flag, pose, colour,
list or heap block of the game changes, so the game's logic and its goldens are those of a run without it.
