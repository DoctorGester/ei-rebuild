# Renderer: the sidecar renderer, the hand-over from the game, and the switch-over

The game's own renderer (DirectDraw 7 / Direct3D 7) is replaced by a new renderer in a sidecar module, and only the
standalone image runs it (`ei_standalone.exe` + `ei_hook.dll`, docs/notes/standalone.md). game.exe runs the original
code only, on dgVoodoo: its runs are the reference (goldens, function vectors, widescreen frames), never our
renderer's.

1. Game code that called the renderer is rewritten and hands its data to a **new renderer in a sidecar
   module** (`src/render/`, static library `ei_render` linked into ei_hook): camera and matrices, object
   transforms, figure meshes, terrain sectors, particles, textures, the interface windows' CPU surfaces
   (uploaded as textures), movie frames. The Direct3D 7 calls left in game code go to two stand-in objects that draw
   nothing (section 9).
2. No Direct3D 7 emulation, no bit-exact rewrite of 3DfpFPU.dll / 2DintMMX.dll, no x87 or compatibility
   shims in the new code. Results only need to be close (captures are compared with a tolerance); full
   determinism is out of scope.

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
`frames\<name>.bmp`. When the Direct3D 11 device cannot be made at the start, 004EDD70 throws the game's error ("Can't
start the renderer (no Direct3D 11 device).") and WinMain ends the game.

`render::Active()` is the one query: the image (`ei::Standalone()`) and not failed (no device at the start, a sokol
panic, a lost device not made again: R9c1). Every `render::` entry point checks it itself, so game code does not ask.
In game.exe (the harness and the vector recorders run inside the original's process) it is false: captures there read
the old render target. `EI_RENDER` (environment, or the harness step `render <spec>` in a run of the image) is a
comma-separated list of options: `smooth` (the smooth option, R12a, section 11), `classicshadows` (the game's shadow
decals instead of the shadow map, R16j, section 13) and the harness build's `removable` (R9c5); `new`, `present` and `cpuwindows` (the old switch, still written by run_all and the scenarios) are accepted
and change nothing; any other word is logged as unknown. The original code (`--no-hooks`) never gets EI_RENDER and the harness ignores its `render`
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
- effects (flag 0x400): vision fog (004E2700 -> CVisibilityFog 004E1EB0), client slot 36, particles 00498940,
  lightning (00468940), rain and snow (004E5680); then 00569D60, flying hit points (005E18E0, flag 0x2000).
Then the interface window by window (the top window's slot 20 draws its children): sprites (CUiMesh 005E6D90: matrix
pushed on both stacks, colour through renderer slot 7, figure through slot 14 -> 005A7640 -> mesh draw 004E9010), 3D
models (CUiModel 005FC2F0; the hero preview 005EC300 first clears depth to 0 and its rectangles to 1) and (slot 22,
005FECC0) surfaces: 005FDA80 blits each shown CUiSurface's surface onto the render target, plain or colour-keyed
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
texturing. Textures use the .mmp file's own mip levels (not a box filter of level 0).

## 3. What the renderer feeds back into game logic

- **Visibility** (frustum test 004F16A0, in-view flag 0x20 of client objects): readers are client-side only (cursor
  picking and rubber band 0051C130, particle sources 00496EB0, shadow volume 0051E300; also client animation to game
  time, pending figure changes 0050D450, model LRU). **Visibility only gates client-side animation, figure changes and
  picking, never server state or timing.**
- **Picking** uses screen rectangles written during the object draw from projected vertices (004E8900 / 004E86B0 into
  the device's +0x15C, copied by 005A7A70 into node and figure pieces). Whatever draws the objects
  must still produce them (approximate is fine, from projected bounds).
- **One real state change in a draw path:** the terrain page draw applies pending water level changes (006C30E0 from
  006BFF40); the water layer is what height queries read. DrawSectorPage applies them before handing over (the
  game's own draw of the page is gone, R11a).
- **Pixels:** the escape menu's background and the save game's 256x192 picture are copied from the render target
  (005FD3D0 CaptureScreen, 005FD440 Rgb565Copy); with the sidecar presenting they come from its frame.
- **Timing:** up to five world updates of 0078FF34 ms per frame; vsync only changes grouping.

## 4. Hand-over map (game data, hand-over point, `render::` call)

| Data | Where in the game | Hand-over point | Status |
|---|---|---|---|
| Clip matrix of the current draw | transform pipeline (renderer 007AF060 +8): stacks of 64-byte row-major matrices, `+0x24/+0x28` clip space (axis swap * projection * view * model), `+0x1C` the same times the viewport, `+0x5C/+0x60` view stack | read at hand-over time | used |
| Terrain sectors | SectorData (`terrain_sector.h`): positions +0x17C (33x33 per layer, water after ground), vertex records +0x190 (0x54 bytes, lit colour +0x11, specular / fog +0x15), tile lists per quarter and page +0x314, edge flags +0x2C4, quarter visibility +0x144, LOD +0x170, seam flags +0x174 / +0x178, owner TerrainFields +0x1C; page textures TerrainFields +0xD8 | DrawSectorPage 006BFF40 (`terrain_draw.cpp`) -> `render::TerrainPage` | drawn |
| Meshes (figures, objects, sky, interface sprites and previews) | positions in blocks of four (morphed by the game), corners, colours as lit by the game (005A7A70 step 3; flag 4: device flat colour +0x150), texture coordinates; matrix = top of the screen stack converted back to clip space; distance fog switch +0x158 and range | DeviceDrawMesh 004E9010 / 004E9A50 (`device_mesh.cpp`) -> `render::BeginMesh` / `EndMesh` | drawn |
| Pass state | per pass of a mesh (material technique 004EB550, or two textures 004EB370): blend, alpha test, depth, culling, specular, fog, texture factor, two stages, filters, addressing, bound holders (device +0x12C / +0x130) | `device_draw.cpp` -> `render::MeshPass` (section 5); per terrain page (blend, depth, culling: the pipeline) at `render::TerrainPage` | drawn |
| Depth clears | 00636020, hero preview 005EC300, trade grid 00627390, hero equipment 00622420, screens 2 / 3 (00632A90 / 00602BF0) | `render::ClearDepth` | drawn |
| Shadows | the sun's shadow map (R16j, section 13): which object a mesh belongs to, which objects cast, the sun; classic: 64x64 shadow textures rasterised on the CPU (2DintMMX), ground quad 0056C750, drawn with 004EA4A0 (also selection markers, light decals) | `world_client_draw.cpp` (00569E40 `render::ShadowSun`, 0056AFE0 `BeginCaster` / `EndCaster`), `map_object_shadow.cpp` (0051CE60 `CastsShadow`, `ClassicShadowDecal`); classic: `device_shadow.cpp` -> `render::BeginMesh` without corners | drawn (map by default, decals with `classicshadows`) |
| Particles, ribbons, lightning, hit points, vision fog | pre-transformed draws: sprites 00502CA0 -> 00503680, ribbons 00501D90 -> 00502A20, triangles 00501BB0 (`renderer_triangles.cpp`), line strips 005045D0 / 0049B4B0 (`renderer_lines.cpp`) | `render::TlDraw` with vertices after the fog pass and the old device's state | drawn; later the particle data itself |
| Weather | rain / snow triangles built by 004E4240 / 004E4C80 (`weather_particles.cpp`: `BuildRain` / `BuildSnow`, then `DrawParticles`) into pre-transformed vertices and drawn with the device directly | 004E5680 rewritten (`weather_draw.cpp`): after the draw its triangles go to `TlDraw`; the old draw stays | drawn; the old draw (`DrawParticles`) still runs, into the stand-in |
| Interface surfaces | CUiSurface (0x38): +0x0C flags (1 colour key, 2 stretched, 4 shown), +0x10 holder, +0x24 rect, +0x34 key | 005FDA80 (`ui_surface_blit.cpp`) -> `render::SurfaceBlit` | drawn |
| Rectangle fills | renderer slot 19 (005041F0) | `renderer_rect.cpp` -> `render::ScreenDraw` | drawn |
| Cursor | object 007C7BD8; images from 8888 pictures loaded by 004EFC60 -> `render::PictureFromMmp`, mask from game memory | frame draw 005E54A0 (`ui_cursor_frame.cpp`) -> `render::Cursor` | drawn; the cursor thread (005E5690) presents between frames (section 9, R9c2) |
| Movies | Bink: BinkCopyToBuffer (32-bit type 4) into a CPU buffer; CBinkVideo 005DBA50 opens, 005DBD80 steps and blits into a letterboxed rectangle | `bink_video.cpp` -> `render::MovieFrame` / `MovieBlit` / `SurfaceBlit`, `render::Fill` | drawn |
| Present | 004EF610 | `device_present.cpp` -> `render::Present` (the new frame on the window's swap chain, section 9) | done |
| Clear colour, target size, fog | renderer +0x14; device object 007AF080 -> +0x18; render states FOGENABLE / FOGCOLOR of the old device (zone colour through slot 0 00501AB0) | EndFrame / terrain hand-over | fog still read from the stand-in device's state (section 9) |

Their game-side work stays (visibility, LOD, animation, lighting, sorting, water levels); the goldens check it. Not
handed over directly yet: `figure_part_draw*.cpp` (005A7A70, 005A7640: model matrices and figure data once meshes stop
going through the old projection), the particle data, materials from the game's data instead of the device state,
picking rectangles. The 3DfpFPU / 2DintMMX calls left in game code (bounds at figure load, the terrain's transform
before clipping, the shadow rasteriser) still want plain C++ replacements (section 9).

The interface is overlay, not native: each window surface is uploaded as a texture when its contents changed (checked
at every blit) and drawn at the blit's place with the colour key as transparency. Native drawing (glyph atlas from the
game's fonts to keep the metrics the word wrap 005F8340 depends on, every window class's Draw / Update as quad / text
submissions) comes window by window (docs/notes/ui-native.md, section 9): a window switched to native simply stops
blitting its surface.

## 5. Pass state

`src/render/render_state.h`: every rewritten place that sets a render state, a texture stage state or applies a state
block on the old device also calls `render::state::Render / Stage / Block` with the same values (writers in
device_draw, device_states, renderer_init (and a reset at device creation), renderer_lines, renderer_rect,
renderer_sprites, renderer_strip, terrain_draw, world_client_draw, ui_alpha_draws, ui_camp, ui_container_draws,
ui_create_screen, ui_hero_preview, ui_hud, ui_item_view, ui_objectives, ui_help_window). A pass takes its states from
this mirror. The device records only four state blocks at its creation (004E6392..004E67BD; handles 007AEDDC / D8 /
D4 / D0), each a stage-0 setup; a material's technique is a list of those handles plus blend factors and texture
records:

| handle | colour | alpha |
|---|---|---|
| 007AEDDC | texture x current (MODULATE) | texture (SELECTARG1) |
| 007AEDD8 | texture x diffuse | texture x diffuse |
| 007AEDD4 | texture x diffuse | texture (SELECTARG1) |
| 007AEDD0 | texture x diffuse | diffuse (SELECTARG1) |

A state nothing has fed is read from the stand-in device (which keeps every state the game sets; `OldRenderStates` /
`OldStageStates` in render.cpp) and logged once ("render: pass state: render state N not fed by the game code yet").
The harness build (`EI_RENDER_VALIDATION`) reads every fed state back from it and logs disagreements ("fed X but the
old device has Y": a writer not converted) and draws the device's value. States the game never sets (ZFUNC, TEXTUREFACTOR,
ADDRESSU / V, stage 1 off) start with the device's initial values.
Stage 0's mip filter, which the unit panel's draw 006462A0 turns off around the unit's picture, is fed by
`CHudUnitPanel::DrawVanilla` (ui_hud_unit_draw.cpp).

Rewritten state setters (all feed the mirror): 00501C60 (renderer slot 10, `device_states.cpp`: SRCBLEND then
DESTBLEND, failure through 004800E0; known caller the particle draw 00498940);
005045D0 (slot 20, a strip through integer screen points, no caller) and 0049B4B0 (`renderer_lines.cpp`: the strip of
the renderer's line lists, 0049B780 from slot 15 00501CE0: bounds boxes 005A7290, line lists 00569D60; flat shaded,
into a growing vertex buffer at 007A9CE0; both turn stage 0's colour / alpha ops off and back to MODULATE) go to
`render::TlDraw` (line strip, FVF 0xC4), not reached by scenarios; 005041F0 (`renderer_rect.cpp`). 0049B140
only appends a line-list point (the device calls under it belong to 0049B320, a dead two-point line draw); 0050434F
and 00504341 are not functions (inside 005041F0; a false vtable entry 00747E2C in config/vtables.csv).

## 6. Interface windows and holders on game memory (`cpuwindows`)

**Memory** (owned by the new renderer): `render::NewSurfaceMemory / SurfaceMemoryOf / FreeSurfaceMemory / HolderGone`
(render_textures.cpp): one zeroed top-down DIB section per holder (GDI draws into it through a memory DC) with pitch,
size, bit count and channel masks. Made by the holder code, used by game code (`ClientBitmap`, and `DirectPixels`, which
describes it as a `SurfacePixels`, in `dx_surface.h`) and read by the renderer after GdiFlush.
Change detection (R16a): writers mark what they wrote (`render::SurfaceWritten`): the DC release 005FDC60 marks
GDI's bounds of the DC (`SetBoundsRect` at 005FDC00, `GetBoundsRect`, 2 pixels wider), `DirectPixels` the whole
memory unless the caller only reads (`reads`: PixelAt, the copies' sources, SwapContents' save, SurfaceBytes),
`CopyFrameTo` the whole memory. At a blit only the marked rectangle is compared with the copy of what the texture
holds, and the differing part is converted and written into the texture (a Direct3D 11 default-usage texture handed
to sokol, `UpdateSubresource`); a surface already blitted in this frame gets a new texture instead. The harness
build compares the rest of the memory at every blit: a change outside the marks is logged ("MISSED WRITE") and fails
the run (`render::MissedSurfaceWrites`; its time counts as "counting"). Memory textures (section 7) use the same
marks, once a frame (R16b).

**Holders without a surface**: no holder has a DirectDraw surface (`CDXSurface::surface` is always null; R11b removed
the code for one): the holders of 004EF7B0 (windows 005FCC00 / 005FCDD0 / 005FD7A0, the cursor's background and
composition surfaces 005E60B0, the movies' decode surfaces), the picture holders of 004F0060 (cursor images via
004EFC60 from 005E4EB0; dead picture windows) and the textures (section 7) have only the renderer's memory, written
and read through `DirectPixels` (no Lock / Unlock, GetDC, Blt or colour fill). Without memory a holder is not made:
the original's DDSurface error is thrown (`ThrowSurfaceNotMade`, device +8 = DDERR_UNSUPPORTED). The screen's pixel
format comes from the display mode (`ReadScreenFormat`). If the new renderer fails later (`Active()` turns false)
nothing more is shown but nothing crashes.

| Address | Name / change |
|---|---|
| 004EF7B0 `DXSurfaceConstruct`, 004EFD20 destructor body | memory-owning holder, no CreateSurface (device +8 = 0) |
| 004F0060 `DXSurfaceLoadPicture` (RET 8) | 8888 pictures only; fills the surface (32 bits a row copy, 16 bits through 2DintMMX slots 1, 2); a system request with memory fills it directly; only live caller 004EFC60 from 005E4EB0 (flag byte `00784756` is 1 in the exe, never written); S on a scratch holder |
| 005E4EB0 `CursorImage::Construct` (`ui_cursor.cpp`) | picture, holder, frames = width / height, hot spot x cursor size (007C7C90 / 94) / 32, mask (16 bits all ones where the pixel is 0; 32 bits low 24 bits 0) |
| 005DBA50 `CBinkVideo::Open` | no BinkDDSurfaceType (the holder has no surface): 4, the CPU copy's type |
| 005FCC00 / 005FCDD0 / 005FCF80 / 005FD120 | the colour-fill Blt that clears a new window is left out (a new DIB section is zero) |
| 005FDC00 / 005FDC60 | GDI through a memory DC on the DIB |
| 005FD3D0, 005FD440, 005FE440, 005FD7A0, 005FD240 | screen copy (`render::CopyFrameTo`; no Blt from the old target, which does not hold the frame; between frames the last frame, R13), save picture stretch (CPU copy, (2x+1) * source / (2 * size)), Greyscale, SwapContents, pixel read: no Lock / GetSurfaceDesc |
| 005FDA80, 005E54A0 / 005E5690 / 005E6630, 005DBD80 / 005DB3F0 / 005DA400 | the original's Blt, composition 005E5C40, background save, Lock / BinkCopyToBuffer / Unlock are gone (R11a); the new renderer gets the blits, the cursor and the frames |

Scenarios: render_cpuwindows, render_present / render_present_cpuwindows, render_titles_present, render_movie_present.
Not reproduced: the unwinding when 004EFC60 / 004EF7B0 throw and the fallback to 004EF7B0 when 004EFA60's
video-memory holder throws (only with 00784756 0; 004EFA60, the holder from a DirectDraw description, and 004EFEA0
are not ported: config/dead_code.csv, R11b). The original quirks kept (failed Locks returning with the device
lock held, the 24-bit clear ignoring the pitch, ...) are in the functions' comments.

**Save / load screen's pictures** (`ui_saveload.cpp`): two 256x256 5:6:5 textures made by 005FF0B0 (-> 00691D80 ->
0068F6C0): objects_[1] filled by 00656E10 (ShowPicture: the selected save's shot.sav) and +0x184 (the new save's
picture) filled by 00655D40 (FillEntries, from the escape menu's 256x192 copy, interface +0x5C) and written to shot.sav
by 00655550 (SaveGame; the autosave 006570E0 writes it from a window surface). 00656464 is not a function (inside
00655D40; a false vtable entry 0074718C). Both textures have the renderer's memory from 00691D80.
The new folder is "save<n>", n one more than the highest atoi(folder.Mid(4)) (006FD9B4 is Mid). Checked by scenario ui_saveload. The SavesPath Rebirth patch
is disabled in this install and not reproduced.

**Screen copies between frames** (R13): 006570E0 (quick save F5 "saveq", autosaves) copies the screen with
CaptureScreen (005FD3D0) and stretches it to 256x192 (005FD440) for shot.sav. The new game's autosave passes DL 1 and
draws the interface first (FrameStep without a present: the copy comes from that frame); the quick save key passes DL 0
and copies between frames, before the next frame's first draw. The original's render target then still holds the
last frame, and so does the new renderer's target: `render::CopyFrameTo` reads it as it is when nothing of the frame is
drawn or queued yet (no pass, no commands). A pass there would clear it first: the save's picture was the plain clear
colour (blue-grey 0x8596) after R11a. Checked by scenario ui_quicksave (`checksave saveq world`: the picture against a
capture of the same frozen moment as 8x8 block averages; the original's: at most 22 per channel, a cleared one 145+).

## 7. Textures without DirectDraw

CPU-written textures (00691D80), shadow textures and .bmp textures have only the renderer's memory; .mmp textures only
its decoded copy. The texture fills 0068FA10 / 00690970 / 00690B30 / 00690D30 are not ported (config/dead_code.csv).

**Memory textures updated in place (R16b, `render_textures.cpp`).** A texture whose holder has the renderer's memory
(the classic shadows' 64 x 64 4:4:4:4 textures, the global map's 256 x 256, the item and save pictures) is checked at
its first bind of a frame (`SurfaceView`): read again when the game marked its memory written since
(`render::SurfaceWritten`, the marks of section 6: 0051CE60's copy into the shadow's texture goes through
`DirectPixels`, so it marks it) and the memory differs from the copy the texture was made from; also when there is
no copy (the memory was made again) or the size changed. Read means level 0 converted to RGBA8 (16-bit layouts
through a 64 K-entry table per mask layout, `Lookup16`: the per-channel mask loop cost more than the rest) and
written with `UpdateSubresource` into the holder's own Direct3D 11 texture (default usage, every level, made once and
handed to sokol), the other levels made by the GPU (`GenerateMips`). No image or view is made per change any more
(before: 4-16 new images a frame in a fight or town with the classic shadows, their levels box-filtered on the CPU,
the old ones destroyed at the present). A frame's draws happen at its end (DrawPending), after every hand-over's
update, and a texture is read at most once a frame, so a draw never sees a later write. The harness build compares
the memory of every unmarked memory texture at that check: a change is logged ("MISSED WRITE: texture holder") and
fails the run like a window surface's. `memorytextures remake` (harness) makes them again as new images with CPU
levels as before, for frame comparisons: render_scene, render_shadows (start, fight, town, classic),
render_options_shadows (each moment classic too) and ui_global_map_pick capture both at one frozen moment, 8 / 0.5%
(the GPU's levels round differently from the CPU box filter). The plan's texture array of shadow slices was not
needed for the CPU gain (each holder keeps its texture; objects that go free theirs through HolderGone); R16e can put
the shadows into one when its instanced decal draw wants them in one binding.

| Address | Name / change |
|---|---|
| 0068F6C0 `CTexture::CreateBlank` (`texture_blank.cpp`, RET 8, EAX 1) | surface square from header->width (height not read); the holder is `NewMemoryTextureHolder`, one level (the original: 004EFA60's texture with mipCount levels, caps by the kind flags); +0x28 = bits x size x size / 8 |
| 00691D80 `CTextureManager::NewTexture` (RET 0xC) | kinds 0x1E / 0x20 / 0x21 get the device's 5650 / 5551 / 4444 (missing: FatalError "Can't find texture format %d."); its texture through 0068F6C0 |
| 00692010 `NewShadowTexture`, 00692140 `ShadowRenderSurface` (`texture_shadow.cpp`) | 64x64 4:4:4:4 shadow texture and the shared render surface (manager +0xE0) |
| 00690FC0 `LockPixels`, 00691030 `UnlockPixels`; 0051CE60's copy -> `CTexture::LoadPixelsFrom` | device lock held between the two; the holder is described from its memory (`DirectPixels`); the shadow texture is filled from the render surface on the CPU |
| 00690430 `CTexture::CreateFromMmp` (`texture_mmp.cpp`) | no staging surface, fill (0068FA10), Load or priority; `render::TextureFromMmp` decodes the file's levels (DXT1 / DXT3, 8888, 4444, 5551, 5650 by the header's channel masks; keyed by the holder); +0x28 = `MmpHeader::FilledBytes`; an undecodable format keeps the holder and shows nothing (logged once) |
| 0068FBF0 `LoadBmpFiles` (`texture_bmp.cpp`, RET 0xC) | loose textures\<name>.bmp (from 00690260) via the game's DIB reader (006D4AA0 / 006D5910 / 006D4D10 / 006D4CD0); not reached (no .bmp in the installs); loops over the width then the height (a non-square .bmp overruns the surface) |
| 00691B00 `ReleaseObject` (slot 24), 00691BF0 `TakeFromCache`, 00693C60 `FindTexture` (`texture_cache.cpp`) | cache list +0xBC, bytes +0xD8; no SetPriority (no surface); 00693C60 does not lower +0xD8 for a texture it takes from the cache (00691BF0 does) |

Where an .mmp file's levels are (R12b): the first level the texture takes at `MipOffset` (006908E0's geometric sum),
each next one right after the last, bits per pixel x width x height / 8 bytes (sizes halved down to 0), as the fill
00690970 steps. 006908E0's 32-bit product overflows from level 6 of a 1024 x 1024 DXT1 and level 5 of a 2048 x 2048
DXT3: taken for every level, the decoder read zone1's tree crowns' small levels from the wrong bytes (opaque bright
green texels, drawn as thin strips where a leaf card is seen edge-on). A DXT level under 4 x 4 gets only those bytes
(the start of its block) and zeros for the rest.

Users of the CPU-written textures: 005FF0B0 (`CUiWindow::CreateTexture`, from the battle screen 00635250, options
006503C0, save / load, global map 0062D350, tutorial 00658E10, 00605DE0, 00608F90, 00640060) and 005EC020 (item screens'
figure textures); writers 005FFFB0 CopyImage, 005EC020, the global map's weather 0062F050 (00499F10 draws into the
memory) and the save / load three all use `DirectPixels`. The binders (004EB300, 004EBAF0, 00501AF0, 004EB370's inline
one) pass null to SetTexture (the old device draws nothing in the switch-over; the renderer reads the bound holder,
device +0x12C). Renderer side (`render_textures.cpp`): decoded textures (`StoreDecoded`) are keyed by
the holder; a holder with neither surface nor memory shows its decoded texture until it goes (`HolderGone` retires its
texture and window entries); `TexturePixels` ignores a holder with memory.

Texture patches (R11d): `render::TexturePatch(target, to, source, from)` records that a rectangle of one holder's
texture shows a rectangle of another's (first-level texels, one per target, gone with either holder). `ReadPassState`
gives stage 0's patch to the batch (`PassState::patch`), and the mesh shader samples the source (`tex2`, stage 0's
sampler; coordinates kept half a texel inside its rectangle) where the wrapped stage-0 coordinates fall in the target
rectangle; the target texture is not changed. The unit's face (00533980, the blink: the original blits into the skin
down the mip chain) is its only user; check `render_blink` (frames of the hero's portrait with the mouth open, closed,
open again; the original's run the same).

Scenarios: render_present*, render_movie_present, render_titles_present, ui_saveload, render_blink. Dead / not functions: 00693DE0,
0068F860 (reached only from unreferenced 00691D50),
004EAFD0 (inside 004EA4A0's range).

## 8. Draws with old-device calls, movies and the cursor outside the frame

Interface draws (`ui_container_draws.cpp`; each keeps the original's device calls in order, states go to
`render::state`, depth clears to `render::ClearDepth`; the old device's clears are left out (+8 = 0); the older clear
hand-overs 00636020, 005EC300, 00627390 still call the stand-in's Clear, which does nothing). The rewrites:
00600EF0 (CUiContainer slot 20: state block 007AEDD8, 005FEB90, children's slot 20); 00602BF0 / 00632A90 (slot 20 of
screen 3, vtable 007405AC / screen 2, 00740688: depth clear then 00600EF0; no scenario reaches them); 00622420
(CHeroEquipPanel slot 20, vtable 007418E0: highlight frames, quick items' models (005ECA10), in the camp the armour
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

Images kept across frames (`render_blit.cpp`): the cursor's composite, `render::Fill`'s 1x1 colours and the movie frames
are dynamic images updated with `sg_update_image`, not made per blit. Cursor and fills share a cache of at most 64
images (one per size, several of a size when one frame draws different contents), each keeping a copy of its pixels: a
blit with the same pixels uploads nothing, else a free image of that size (not blitted in this frame, not updated
since the last commit; sokol allows one update per image and frame, and the blits are drawn at the end of the frame)
is rewritten; only when every image is in use is a one-frame image made. A movie keeps one image per player, updated
in place unless it was already blitted or updated in this frame (then a new one replaces it after the present).
Harness build: counts per purpose in ei_hook.log (`render: images ...`, every 300 frames and at exit) and every
in-place upload is read back from the GPU and compared with the pixels written (`render: MISMATCH` on a difference).

## 9. The present path and what is left

The switch-over without DirectDraw (R9a to R9c3, R10) is done and is the only way our renderer runs. The open items
come first, then what holds for the present path (the subsection names are cited from src/ and the tickets).

### Open items

- **R9b2** (ready): WinMain's original in harness runs. The harness patches the CALL at 00457595, so WinMain 00456440's
  original runs (GetAvailableVidMem at 00456DA2 / 00456DC6 / 00456E0D, RestoreAllSurfaces at 0045715A) and reaches the
  stand-ins; `app_main.cpp`'s rewrite runs only in a launch without the harness. Let the rewrite run under the harness
  (freeze hook in app_main.cpp); the COM stand-ins can go after that, once Rebirth's patched originals (006462A0,
  0060C870) are covered.
- A desktop display mode change while the game runs is not exercised: no run may switch the display mode (R9c4
  below). R9c covers a window resized from outside, which is what such a change does to the game window.
- **Native interface** (docs/notes/ui-native.md, tickets N1, N4, P1 to P8; P7 is optional): window surfaces drawn on the
  GPU instead of GDI into DIB sections, no GDI in src/render, own font loading; P7 puts the CPU-written textures and
  the global map's clouds on the GPU.
- **DirectDraw left after R11c**: R11b removed the holders' DirectDraw branches and the 18 DirectDraw-only ports no
  code of ours called (config/dead_code.csv rows "R11b, DirectDraw only"; find_dead_code.py drops hand rows, so re-add
  them after a regeneration); R11c the surface tests (screen clear, capture, eye blink, binds), `device::Surface` and
  the DDSURFACEDESC2 layout of `DirectPixels` (now `SurfacePixels`). `CDXSurface::surface` (+0x10) stays, always null.
  Left: the COM stand-ins and device.h's wrappers (R9b2). R11d: the blink is a texture patch drawn in the shader (section 7).
- No ticket yet: the fog colour and range come from the stand-in device's state (not game data); weather's old draw
  (`CRainSnow::DrawParticles`) still runs next to the hand-over, into the stand-in; GPU shadows; picking rectangles
  from projected bounds; the 3DfpFPU / 2DintMMX calls left in game code (bounds at figure load, the terrain's transform
  before clipping, the shadow rasteriser) want plain C++ replacements; several steps (holders, saved pictures, interface
  draws, movies, textures) are checked by goldens and harness scenarios, never by a person in game.

### R9a: the game without DirectDraw

The game has no DirectDraw object, display mode, primary, render target, depth buffer, Direct3D object or device:
- 00500B70 lists and tries no DirectDraw driver (null driver id; it calls neither 004EE5D0 nor 004EEA90). 004EDD70
  starts the new renderer (`render::StartPresenting`: the Direct3D 11 device), loads 2DintMMX's converter, keeps the
  cooperative level's flags and screen rectangle, lists the one display mode, and makes the primary and the render
  target two holders without a surface of the screen's width, height and bits (`GameTargetSize`, the screen copies and
  the `if (primary_) renderTarget_->width` reads see the screen). No video FourCC probing.
- 004E5E50 takes the IDirect3DDevice7 stand-in (HAL id, no depth formats, no depth buffer, no render target set), sets
  the main thread's x87 precision to 24 bits as Direct3D 7's CreateDevice did (the game passes DDSCL_FPUSETUP), builds
  the format table from the stand-in's fixed list (5:6:5, 1:5:5:5, 4:4:4:4, 8:8:8:8, DXT1-5: what dgVoodoo listed;
  **DXT2/4/5 must stay**: Rebirth's battle00 and footprints are DXT5, and without them the texture load failed and
  `SetTexture` crashed), sets the viewport and records the four state blocks on it (handles 1..4, which
  `render::state::Block` knows by their addresses).
- `src/game/device_null.cpp`: the two stand-ins in the device object's +0x0C / +0x54. They draw nothing; their state
  (render / stage states, state blocks, bound textures, viewport, scene bracket, display mode) is a plain object,
  `SwitchOverDevice`, and the COM stand-ins log and forward to it. The IDirect3DDevice7 one keeps the states (Direct3D
  7 defaults at the start), feeds them to the mirror (`render_state.h`), records / applies / captures / deletes state
  blocks, answers GetCaps with fixed capabilities (8 stages, every texture operation, 4096 textures, no
  sort-independent antialiasing) and counts SetTexture references. The IDirectDraw7 one answers GetDisplayMode with
  the chosen mode in `device::ScreenFormat` (16 bits 5:6:5, else X8R8G8B8: what dx_surface.cpp's ReadScreenFormat
  reads; the windows' colour keys, CUiSurface +0x34, are 0 or a COLORREF set without the format, so they agree),
  GetCaps with 3D / texture / mipmap capabilities and no gamma or FourCC, GetAvailableVidMem 256 MB, and refuses
  CreateSurface (logged; no code of ours calls it since R11b). Every method
  logs its first call from each calling place (`old device: IDirect3DDevice7::SetRenderState from game.exe 0050B22E`,
  at most 400 lines); `tools/old_device_calls.py [run folders]` lists that log by calling function (default: every
  `build/verify/runs/*present*`).
- `device.h`: `device::SetRenderState(d3d, ...)` and one call per method the rewritten code uses (GetCaps,
  EnumTextureFormats, Begin / EndScene, Clear, SetViewport, SetRenderTarget, Set / GetRenderState, Set /
  GetTextureStageState, state block calls, SetTexture, DrawPrimitive, DrawIndexedPrimitive; IDirectDraw7 GetCaps,
  GetDisplayMode, EnumDisplayModes, RestoreDisplayMode, SetCooperativeLevel,
  GetAvailableVidMem, RestoreAllSurfaces, FlipToGDISurface, WaitForVerticalBlank). Each answers from `SwitchOverDevice`
  when the interface is the device object's stand-in (or null): draws, clears, mode changes and restores give D3D_OK.
  Every `->Method(` on the device in src/game goes through these; the interface's `UiD3DDevice` (ui.h) has declared
  methods (`ui_d3d_device.cpp`). The new renderer's state reads (render.cpp `OldRenderStates` / `OldStageStates`) read
  `SwitchOverDevice` through `device_switchover.h` (no Direct3D 7 headers there).
- 004EF610 ends the frame (`render::Present`) and returns 0: `EndFrame` draws the frame into a flip-model swap chain
  (FLIP_DISCARD, BGRA8, two buffers, `MakeWindowAssociation` without Alt+Enter / window changes) on the game window,
  sized to its client area (`ResizeBuffers` when it changes; the hook makes the process system DPI aware at WinMain so
  that rectangle is in physical pixels), stretched over it, `Present(1, 0)`. The movie players and the loading screen
  go through 004EF610 as before.
- Guarded for surfaceless holders: `device::SameSurface` (the same only when one holder) in
  005FDA80 (window blits: otherwise every window was skipped), the three movie players' "not the primary itself"
  checks and CUiSurface::CaptureScreen; 00504780 (restore after a lost device) returns at once; 00458270's screen
  clears with no primary do nothing (app_frame.cpp, result 0). The movie holders (`bink_video.cpp` `NewMovieHolder`):
  00784756 is written by the game start (00456E35) from GetAvailableVidMem and the stand-in's 256 MB clears it, so the
  holder is 004EF7B0's (only the renderer's memory; a video-memory request would throw into the game's error box); each
  Bink frame is copied into memory the renderer owns (`render::MovieFrame`). Pictures loaded with the video-memory flag
  (004F0060) get the renderer's memory as the system-memory ones do (dx_surface.cpp).
- Not touched (the stand-ins answer): app.cpp's FlipToGDISurface, app_main's GetAvailableVidMem / RestoreAllSurfaces,
  texture.cpp's video memory and largest texture, the camp's antialiasing GetCaps.
- The harness shows windows without activating them; without DirectDraw nothing activates the game window, so the
  game's active flag (0079BC5C) and the renderer's bit 0 stay 0 and no frame is drawn. The harness sends one WM_ACTIVATE
  (active) once the device object exists. A player's own start is in the foreground and gets the activation from
  Windows.
- Scenarios: render_present, render_movie_present, render_present_cpuwindows, render_titles_present and ui_saveload.

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
  MISMATCH gamma ramp").
- Full screen (device flags_ bit 0, registry FullScreen 1) without a display mode change (`present_window.cpp`, called
  from each present in device_present.cpp): the WS_POPUP window is moved over the monitor it is on (not while
  minimised; again after someone else resized it), and the frame is scaled into it; a window resized by the user
  scales the frame the same way; a resolution below the desktop's shows in the window.
- The game's mouse coordinates when the frame does not fill the window one to one: GetCursorPos / SetCursorPos (the
  cursor 005E52E0, the global map 0062F050 take screen coordinates as a window at 0, 0 the resolution's size) through
  replaced import slots; WM_MOUSEMOVE .. WM_MBUTTONDBLCLK (client) and WM_MOUSEWHEEL (screen) mapped at the top of the
  input dispatcher (00457970, app.cpp). The game's own wheel message (0xCC54) carries no position.
- Focus loss and return (Alt+Tab, minimise): the WM_ACTIVATE path is the game's own (app.cpp OnActivate): inactive
  deactivates the device (004EEC60, RestoreDisplayMode on the stand-in), the sound and the network and minimises;
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
  "display mode" settings (COptionsWindow +0x410) are the shadow switches: OK (Apply 006524A0) with one changed
  drops the shadows the switches turned off (0056D7A0); switched on again they are rasterised and uploaded anew.
- Scenario `render_options_shadows` (vanilla and Rebirth, golden from the original): in zone1 the three switches
  stepped and applied through the Esc menu's options and OK, then back; the captures show the ruins' shadows gone and
  back. A desktop mode change is left out (no run may change the display mode).

### PF3 (R9b1): no DirectDraw probes at startup

0045E310 (`app_dxversion.cpp` GetDxVersion) probes nothing in the image and returns 0x800 (what the probe gives on
current Windows); "app: native run: DirectX 7 taken as given, the version probe (0045E310) skipped" in ei_hook.log.
The probe used to fail now and then with several games on the machine ("DirectX 7 or later" message). Returning 0x800
without skipping the probe's side effects once hung sound init (DirectMusic through CoCreateInstance and DDRAW.DLL left
loaded mattered to the sound code); the full skip did not hang. A timed-out harness run logs the main thread's
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
  not reset to the identity when the present pipeline is made), window surfaces and memory textures (read again from
  the game's memory). A movie shows again from its next frame. No new device: the new renderer is off (`Active()`
  false) and nothing more is shown; the game goes on.
- The decoded textures (`owned`: no memory) keep what they were decoded from (`TextureSource`, render_textures.cpp):
  the header and the file's bytes of the levels decoded (DXT stays compressed), or the RGBA8 level of `TexturePixels`;
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
- render::Cursor keeps the cursor aside (`Frame::cursor`); EndFrame draws the frame, copies it (`KeepFrameForCursor`)
  and then draws the cursor over it, so frames, captures and ReadFrame are as before; the cursor thread draws onto a
  copy of the cursor-less frame and presents that. `CursorBetweenFrames` presents only when nothing was presented on
  the game window for 40 ms (real time: the harness's virtual clock does not reach the renderer).
- Harness build: every frame the cursor thread composes is read back and compared with the frame without its cursor and
  the cursor's texels ("render: cursor between frames: N composed frames checked, M differed" / "render: MISMATCH
  cursor between frames").

Harness: `mouseglide <ms> <x0> <y0> <x1> <y1>` (the harness's mouse, what GetCursorPos reads, moved by a thread of its
own in 10 ms steps), `stall <ms>` (the main thread away: one long frame), `cursormark`, `cursorcheck <name>|- <min>
[max]` (presents by the cursor thread since the mark within min..max; with a name the last composed frame goes to
frames\<name>.bmp, the checks must have found no difference and its rectangle must sit at the mouse less the hot spot
within 24 pixels), and the starter step `fpscursor 0|1`. Scenario `render_cursor_present` (on `realtime`: on the
virtual clock other threads see the main thread's clock plus the real time since it last moved and never go back, so
after a movie or a load the cursor thread's clock stands still until the main thread's frames catch up).

### R9c3: the Scroll Lock screenshot and the movie recorder from the new frame

`surface_capture.cpp` (006D6D40, which 006D6FC0 and both users call): the holder (the render target or the primary)
gets its bitmap from `render::ReadFrame` (the last frame `EndFrame` drew,
before the gamma ramp and the present's cursor): a CDib of the rectangle's size in the holder's depth with the channel
masks `device::ScreenFormat` gives, rows bottom-up, the part outside the frame black. No frame yet: no bitmap (logged
once). The callers are unchanged, so the file names (`shot%.4d.bmp` from counter 007C7E24 for Scroll Lock; the
recorder's `<moviepath>shot%.4d.bmp` from 0079BC58, the movie folder defaulting to `.\capture\`), the format
(CDib::Write) and the timing are the original's. The recorder wrote through a null bitmap when there was none
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
  wrappers (CDXSurface, 0x20 bytes, +0x10 the surface), device lock 007AEE78.
- **3DfpFPU.dll** (`GetTLPipeline`, 45 slots): projection with outcodes, homogeneous clipping (slots 36-38), viewport
  mapping (31-34), vertex fog into the specular alpha (35), lighting into packed colours (39-44), bounds (6, 7, 29).
  **2DintMMX.dll** (`GetGraphicsProcessor`, 13 slots): 8888 to 16-bit conversion (1, 2), skin compositing blends (3-5),
  premultiply (6), mip downsampling (7), shadow triangle rasterisers (8, 9), a 16-bit block operation (10), shadow blur
  (11, 12). Both DLLs are identical in the GOG and Rebirth installs.

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
  fraction of neighbouring level-map pairs whose level jumps). Two frozen views of zone1, vanilla: 0.004% with the
  shipped sampler (limit 0.05%), 0.32% / 0.38% through point mips (at least 0.1%, so the check sees seams); Rebirth's
  wide view, further and steeper: 0.10% / 0.15% (limit 0.4%; a triangle's level still jumps where its slopes do, as
  under dgVoodoo's 16x) against 1.2% / 1.5% (at least 0.6%). The original's run captures its frames and skips both
  steps.

- Check (R12b, same scenario): `mmplevels <most>` (`render::CheckMmpLevels`, verify build): every held .mmp texture's
  levels, decoded as drawn, against its level 0, both box-filtered to 4 x 4: the largest mean difference per
  channel. The file's own mips reach 52 (1-bit alpha small levels); the overflowing offsets gave 128 / 149 (vanilla /
  Rebirth); limit 80. Against the harness original's `far` frame, the tree crowns (2750, 550)-(3250, 950) at
  3840 x 2160: 1.30% of the pixels differed (tolerance 8, shift 1; largest 150) before, 0.24% after (leaf edges).

- **The smooth option (R12a)**, user's decision 2026-10-20: off by default; on, the image draws as the user's
  Rebirth dgVoodoo setup (starter\Engine\dgVoodoo.conf: Filtering 16, Antialiasing 8x) except for the interface. One
  switch: `smooth` in EI_RENDER (read once, at the first use: the harness's `render` step or the environment), or
  `render::SetSmooth` (from the next frame; ST2's options screen can call it, ST1 keep it in the settings file);
  `render::Smooth()` reads it. Goldens and frame checks run with it off.
  - The world view (`CWorldClient::Render` 00569E40 between `render::BeginWorld` / `EndWorld`: sky, terrain,
    objects, shadows, water, effects, flying hit points) is a range of the frame's commands (`Frame::worldRanges`).
    Its models (`MeshBatch::model`: BeginMesh draws, not the pre-transformed ones) take `AnisotropicSampler` for both
    stages (point filters stay point, Mipmapping 0 keeps level 0); the interface's sprites and previews keep
    `StageSampler`.
  - The world's runs of commands draw into a multisampled colour and depth target of 8 samples (`kSmoothSamples`;
    fewer when `CheckMultisampleQualityLevels` refuses 8 for RGBA8 or D32, none when the target cannot be made: logged),
    and each such pass resolves into the frame target (sokol's resolve attachment) at its end; the other runs draw
    into the frame target as without the option, so the interface's edges and window surfaces are unchanged (a 3D
    draw's edges run through pixel centres, D3D7's convention: multisampled, every sprite would get half-covered
    border pixels). Between runs (render_smooth.cpp): after a world run the frame target's depth is set from sample 0
    of the multisampled depth; before a world run that follows other draws the frame target's colour and depth go
    into every sample (a pass of its own: the resolve's destination cannot be read in the pass that resolves into
    it). The frame target is what every reader takes (the present, the cursor thread's copy, `ReadFrame`,
    `CopyFrameTo`, the gamma ramp check), always resolved: a pass ends with the resolve, and a copy between frames
    (R13) finds the last frame there.
  - Every pipeline drawing into the frame is made for the pass's sample count (`detail::PassSampleCount`; mesh and
    terrain pipelines keyed by it, the blit and depth clear made for it at first use): sokol's validation skips a
    draw whose pipeline's count differs from the pass's. A change of the option remakes the targets at a frame's
    first pass.
  - Check (scenario `render_smooth`, image only, vanilla and Rebirth): on from the start; in zone1 at one frozen
    moment the frame with the option, without it (`smooth off`) and with it again (`smooth on`): the two with it
    match (samecapture); `smoothcheck plain smooth 0.002 0.2` requires the option on, the world multisampled with the
    device's samples (8 here) in the last frame, anisotropic world models, and the frames to differ in 0.2% .. 20% of
    the pixels (a black or stale frame fails); the quick save's picture (`checksave`, R13) matches the smooth frame.

### Frame capture

Harness. `capture <name>` skips two presented frames and writes the next to `frames\<name>.bmp` (24-bit). In the image
it is the new renderer's last frame (`render::ReadFrame`: before the gamma ramp and the software cursor). In a run of
the original (`--no-hooks`, dgVoodoo) it is the old render target at the present point (after scene and interface,
before the software cursor and the flip; a read-only Lock of the render target, GetDC as fallback: a GDI capture of
the window comes back black with dgVoodoo). `freeze` / `unfreeze` hold timeGetTime / GetTickCount and the camera
(CCamera::Update at 00457595 runs once per frame even with dt 0 and moves a fixed fraction per call); the main loop
then runs no world, client or camera update. `samecapture <a> <b> [tolerance] [fraction]` fails the run when two
captures differ. `tools/compare_frames.py A B [--mask M1 M2 [--mask-background]] [--exclude E1 E2 ...]
[--exclude-grow N] [--diff OUT]`: per-pixel tolerance (default 8), differing fraction (default limit 0.1%), worst
regions. Limits: what moves by itself between frames without the game's clocks (other threads) is not held; the
harness's own waits use real time.

### A-E1: line lists (line_batch.cpp)

`LineBatch` (renderer_lines.h, 0x20 bytes): points four to a packet (x[4], y[4], z[4], w[4]) in an aligned array, a
count and a colour. `AddPoint` (0049B140) appends a point with w 1 and repeats it through the rest of its packet,
growing the array by one packet when full. `Draw` (0049B780) resizes the two work arrays in the exe's data (the
projected points 007A9CB8 without keeping the contents, the outcodes 007A9CC8), projects with the top of the pipeline's
clip stack through the transform object's `ProjectClipped` and, when that returns 0, hands the points to
`DeviceDrawLineStrip` (0049B4B0); it never touches the Direct3D device itself. Returns 1. Users: the figure parts'
debug bounds (005A7290) and the world client's line lists (00569D60); no scenario turns either on, so nothing reaches
it.

## 12. A-S11: what is left of the old renderer's range (0x004C0000-0x00500000)

A-S11 classified the 173 game functions of the range that were not rewritten; the plain game code of the range (text
manager, script loader, `CSysInfo`, the fog manager) is in script.md (A-S11). All direct DirectDraw / Direct3D 7 calls
(class c) are rewritten (A-S12 / A-S13) through the typed interfaces (`<ddraw.h>`, `<d3d.h>`; casts in `device.h`),
as the original makes them; the COM objects are the boundary the new renderer replaces. What is not rewritten:
static initialisers and what only they reach, one MFC TRY/CATCH, a few renderer-interface functions, library rows
(`config/library.csv`, noted "A-S11"), dead rows (`config/dead_code.csv`: 004DABC0, 004E7FB0, 004EE760, and 004F0A20 /
004F0A90 / 004F0AE0 called only by 004EE760; the display mode callback 004ED7F0 too), and functions under 5 bytes
(cannot be hooked; 004F0B00 is the DirectDraw error reporter, a bare RET). 004EE2F8 is not a function (the common exit
of 004EDD70); 0049F7B1 is the tail of `IsaacRandom::Seed`.

File map of the device layer (all in `src/game`; the files' headers name the addresses):
- `device_binds.cpp` (texture binds 004EB300 / 004EBAF0 / 004EB1D0: SetTexture when the stage's holder changes, the
  batch flushed first; batch flushes 004E7840 / 004E7A40: no vertex buffer is ever locked, so only their first test
  runs); `directdraw_holder.cpp` (the DirectDraw base 004EDD70, 004EECF0, 004EEC60, destructor body 004EE330; the
  driver list 004EE4A0 / 004ED610 / 004EE5D0, 004EEA90, 004EEBC0, 004EEE30 are not ported: dead_code.csv, R11b);
  `dx_errors.h` throws the game's error object through `_CxxThrowException` 006E7819); `device_object.cpp` (constructor
  004E5E50, 004E6EE0 format table, destructor body 004E68A0, the four state blocks of stage 0 at 007AEDD0..DC; the
  depth buffer 004E6C30 / 004E6BC0 and the device callback 004ED820 are not ported); `pixel_formats.cpp` (004EDAA0 /
  004EDC00 / 004EDC70 and the format lookups 004E7DA0 / 004E7C40); `device_helpers.cpp` (counted holders, list iterator copies, aligned blocks, deleting destructors, the
  axis swap matrix 004F1330, which Rebirth WideScreen patches: vanilla launch only); `render_helper.cpp` (`CRenderHelper`
  slots and built-in presets, `CTransformPipeline` constructor 004F0B90 / destructor 004F0E10 / matrix stacks 004F0DB0,
  `DriverAt` 004EE6D0); `link_mesh_draw.cpp` (CLinkMesh slots 7-14, 004F3870..004F4850); `weather_particles.cpp` (rain
  004E4240 and snow 004E4C80); `effects_weather_draw.cpp` (visibility fog draw 004E1EB0, rain / snow buffers 004E3C20);
  `device_bounds.cpp` (screen bounds 004E86B0 / 004E8900); `link_loader_create.cpp` (`CLinkFigureLoader` constructor
  004F68C0, vanilla launch only: ResOverride patches it).
- Weather: 81 x 81 cells around the point the wind carries a particle to the camera from; a cell's hash and the
  world's time give the particle's cycle, fall and texture frame; corners in packets through `ProjectClipped` (stack
  2), the ones on screen through the transform object's slot 32 (`ToScreen`) and, with the device's fog flag, slot 35
  (`FogVertices`), then DrawIndexedPrimitive. Snow sways by five tables of cubics (007ABB98 .. 007AE398, built by the
  static initialiser 004E3B20 through 004E3980; evaluated with 004E5660).

Verification: the goldens (the weather draws' scenes: rain in many scenarios, snow in rebirth_flags via `SetWeather`)
and the function vectors; these functions run in the image like the rest.

Original details kept: the T&L HAL device is never used; the rain's
and snow's vertex colours and packet w are never written (the buffer's old bytes are drawn, the fog pass only lowers
the specular's alpha); the rain's height uses the snow's rise (007AEDA0, both 40); the helper's destructor walks on from
a record it has just erased (reads freed nodes, so its walk over more than one record differs in ours); slot 1 tests a
texture operation against the capability bits with `&` (operation number, not its bit); 004E7E00 appends indices and
copies one index more than asked. Different in ours: a throw from the constructors does not free what was built; the
driver record's unset GUID and monitor bytes are zeroed; fields the original leaves as stack contents are zero.

## 13. R16: what a frame costs, and the plan to draw from GPU-resident data

User's direction (2026-10-20): stop matching the old renderer's calls; composite in shaders, keep data on the GPU,
re-upload only what changed. R11d's blink (`render::TexturePatch`: a shader patch instead of a texel copy) is the
pattern.

**Measuring.** `render::FrameStats` (render.h) counts per presented frame: the hand-over (terrain pages and batches,
meshes and passes, flat geometry, pre-transformed draws, blits, depth clears, state reads, matrices), bytes appended to
the vertex / index streams, uniform bytes, passes, draws, pipeline and first-texture changes, images made per kind
(.mmp, CPU pixels, memory textures, window surfaces, movie, small) with their bytes, and the renderer memory compared
with snapshots. The harness build adds the CPU time (hand-over, texture work, EndFrame; a nested timer's time counts
only for its own field) and each terrain page's / mesh's vertices against the last frame's (same; only colours
changed = relit; positions or texture coordinates changed = moved; new), plus the meshes whose geometry equals
another's in the frame. The harness step `renderstats <label>|-` logs the averages since the last one into the report;
scenario `render_stats` (image only) measures the main menu, zone1, a fight of twelve creatures (GodMode hero), the
camp, the base bz1g (a town), the global map and its escape menu. It also logs the mesh draws whose positions an
animation's per-vertex offsets moved ("morphed meshes", R16h) and the main thread's CPU and wall time per frame since
the last `renderstats` (its cycles, in milliseconds by the thread's CPU time, as the TIMING lines): the game's work and
the renderer's together, what a many-figure frame costs. `EI_PROFILE=1` gives the game side
(tools/profile_report.py). RenderDoc was not needed: the counters see the hand-over, which a capture does not.

Per frame, vanilla, 3840 x 2160 (the harness's desktop size; Rebirth's runs are within a few percent; times from a run
without the profiler):

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
   render_options_shadows, ui_global_map_pick capture in place and `memorytextures remake` at one frozen moment, 8 /
   0.5% (largest channel difference 2); no texture change without a mark (the harness build's check) in the suite.
3. **R16c Terrain pages resident on the GPU (done).** `render_terrain.cpp`. Each sector page at a level of detail and
   seam variant is built once, all four quarters, into a pool of default-usage buffers written with UpdateSubresource
   (sokol has no partial update): positions and texture coordinates (20 bytes a vertex), the lit colours (8 bytes, a
   stream of their own) and absolute indices. A frame keeps the visible quarters of each page handed over
   (`TerrainPiece`); ranges that follow each other in the pool are one draw. A page is built again when its tile
   lists, records or edge flags moved (pointers, sizes) or the game rebuilt the sector (`render::TerrainChanged(s,
   true)` from Build and Clear). When the game only moves vertices or texture coordinates (Animate's waves and
   animated tiles, ApplyWaterLevels: `TerrainChanged(s, false)`) the page reads them again through its vertices'
   records and tile entries and uploads the changed range; the visible quarters' colours are compared with the
   records every frame, the changed range uploaded. The pool (at least 128 K vertices) starts again with the frame's
   pages when full (made larger when they need more than half of it); pages not drawn for 600 frames are dropped.
   The shadow map's fit reads the pieces' positions (`TerrainPositions`, the same vertices in the same order).
   Measured (render_stats, vanilla 4K, per frame): terrain upload before / after menu 268 / 51 KB, zone1 808 / 229,
   fight 808 / 211, town 372 / 74 (indices only when a page is built: none in these scenes); terrain CPU (`renderstats`
   "terrain": hand-over and uploads) zone1 0.44-0.61 ms with every page built and uploaded every frame / 0.24 kept,
   town 0.15 kept (Rebirth's wider view: zone1 1046 / 289 KB, 0.67 / 0.44-0.56 ms; the times are noisy). What zone1 still uploads is mostly its water pages
   (the waves move every water vertex each tick, Animate on the CPU) and relit colours; waves in the vertex shader
   would take the rest (R16c2). Check: the harness's `terraincache off` builds and uploads every page every frame as
   before; render_scene, render_terrain_seams and render_smooth capture the frozen frame both ways (0 pixels differ),
   and the harness build compares every kept page with a fresh build every 16th frame (`terraincache check`, 0 stale
   in the render_* scenarios and render_stats).
4. **R16d Meshes resident on the GPU (done).** `render_mesh_cache.cpp`, the pool shared with the terrain in
   `render_buffers.cpp` (`GpuPool`: default-usage buffers written with UpdateSubresource, a geometry stream, a colour
   stream and absolute indices; placed in order, started again with the frame's entries when full). Each figure mesh
   drawn through the figure part draws (DrawLit 005A7A70 and DrawPlain 005A7640, either through the device or a link
   figure's part) is kept under the mesh: its corners' model-space positions and texture coordinates (20 bytes), the
   colour and specular pair (8 bytes, a stream of its own) and the valid triangles; the draw binds the pool with the
   mesh's first index, the texture offset (`textureOffsetU_` / `V_`, animated water and lava) a shader constant. The
   game says what changed (`render::SetMeshSource` around the device's draw; `render::MeshChanged`): an entry is built
   again when an array or count it was built from differs or the mesh or link figure was rebuilt (loaded, cleared,
   mirrored, copied, a part added or removed: `MeshChange::Rebuilt`); its positions are read again when the
   animation's per-vertex offsets changed (`SetOffsets` 005A0B10 compares them) or a link figure's part was morphed
   again (004F3800) (`Positions`); its colours when the draw lit or filled them (Light, FlatColours, DrawPlain's fill,
   the device's colour override, flag 4; also when that draw was streamed or never reached a pass), the sky lit itself
   (CSkyObject::Light, `Colours` on the sky) or a link figure lit or filled a part (004F3870 / 004F3AD0, `Colours` on
   the colour array: the original code calls them outside the figure part draws too, the meshes keeping flag 0x10,
   "lit"). Only the changed range of corners is uploaded. A mesh drawn again in the same frame with other data, interface sprites (CUiMesh::Draw
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
5. **R16e Ground decals on the GPU.** Footprints, blood, object shadows, selection markers, light pools and spell marks
   are projected on the CPU (ProjectDecal 0056C750) into flat meshes every frame (12-32 a frame, up to 344 KB in the
   town); instead a box per decal projects onto the depth of the ground and platforms in a shader (the game keeps its
   lists, lifetimes and textures; the shadow textures from R16b). Gain: the CPU projection and clipping and the flat
   uploads; all decals in one instanced draw. Risk: the look changes at steep slopes, platform edges and under water;
   draw order (after ground and platforms, before figures). Check: ground_prints, render_scene, effects frames at
   16 / 2% plus a side-by-side (frame_preview.py) for the user.
6. **R16f The present without a frame copy.** Every frame is copied whole for the cursor thread (33 MB of GPU traffic
   at 4K) before the cursor is drawn into it; drawing the cursor and the gamma ramp in the present pass keeps the frame
   clean for the cursor thread. Gain: GPU bandwidth only. Risk: low. Check: render_cursor_present, render_present,
   render_present_shot (cursorcheck's own comparisons, 0 differences).
7. **R16g Per-draw records and instancing.** After R16c / R16d the per-frame data is matrices, colours and pass state:
   one ring buffer of per-draw records (matrix, fog, stage operations) read by index replaces apply_uniforms per draw;
   meshes with the same geometry and state (132 of 350 in the fight: creatures of one kind; trees) draw instanced,
   their textures from texture arrays where sizes match; materials read from the game's data instead of the stand-in
   device's state. Particles: the scenes measured have 0-3 pre-transformed draws, so they follow only if a spell-heavy
   scene (spells_cast) shows otherwise. Gain: small now (draws are few), grows with unit counts and wider views. Check:
   render_scene, units at 8 / 0.1%.
8. **R16h Figure animation on the GPU (measured, not built: nothing to gain).** The plan assumed the game morphs the
   figure parts' positions on the CPU every frame. Creatures animate by their bones: FigureBone::Place (0050C230) gives
   each part a position and rotation, i.e. its matrix, which the GPU already applies. The per-vertex morph
   (AnimationTrack::Evaluate 0050ACD0 blends a track's key-frame vertex sets; Place hands them to the part through
   CFigureMesh::SetOffsets 005A0B10; the part draws add them into a copy of the positions) is almost unused: per frame
   0.6 of the fight's 331 mesh draws (12 corners), none in zone1, the town or the menu (renderstats' "morphed
   meshes"), and a profile of render_stats has no sample in Evaluate or the copies. Its result is also read by game
   code, so it would stay on the CPU: the device's projection of the moved positions (DrawMesh 004E9010: the
   visibility verdict and, with draw flag 1, the part's screen rectangle for picking) and the object shadow's
   (CFigureMesh::DrawOffset 005A0CD0, CLinkMesh::ProjectPart 004F4680: the raster whose bounds feed the shadow
   lists). What a GPU path could take is the renderer's reading and upload of the few moved corners (R16d already
   uploads only the changed range). The "moved" meshes in the table above were the classic shadows' decals (R16e).
   Where a many-figure frame's main-thread time goes instead (render_stats, vanilla 4K, harness build; main thread
   CPU / wall ms per frame, renderstats): fight 18.6 / 19.0, mostly the server's AI path search (LocalSearch,
   CAIMap::FindPath); town 8.8 / 9.1: by profile figure lighting in 3DfpFPU (17% of the samples; R16i), the x87
   control word set around every libcrt math call (the CRT's ctrlfp 9%: Hypot in the camera's ClampAboveObjects;
   SV10i), the ground decals' projection and clipping (DecalBuilder; R16e) and the device's per-corner work for meshes
   the new renderer draws from the hand-over anyway (FillCorners, the clipper, fog: R16k). The renderer's own CPU
   (terrain, hand-over, textures, end of frame, shadow map) is 2.0 ms of the fight's frame and 1.0 of the town's; GPU frame
   1.2 / 1.4 ms.
9. **R16i GPU lighting and shadows (later: it changes the look and waits for the user).** Sun and point lights per pixel
   and shadow maps (or GPU-drawn shadow textures) instead of the terrain lit per vertex (about 2 ms a frame on the
   CPU), figures lit per draw (3DfpFPU slots 39-44) and 64 x 64 CPU-rasterised object shadows; the game's vertex
   colours stay as the fallback behind an option. Check: the user's review of side-by-side frames; with the option off
   the frames match today's at 8 / 0.1%.

10. **R16j Object shadows from a sun shadow map (done; user 2026-10-20).** `render_shadows.cpp`. The game hands over the
    sun (00569E40: `Terrain::sun`'s direction), the object each figure draw's meshes belong to (0056AFE0's object
    draws between `BeginCaster` / `EndCaster`) and the objects whose shadow the game draws this frame (0051CE60
    `CastsShadow` after its option, class switch and visibility tests; a unit only once faded in to half). Once a
    frame before the first pass (`PrepareShadows`): the camera's matrix is the world's first ground batch's; its
    inverse times a mesh's clip matrix gives the mesh in the landscape's space; an orthographic view from the sun
    (tilted to at least 20 degrees as 0051CE60's) is fitted around every second terrain vertex inside the view (x, y
    in steps of 8 units, origin on whole texels; 160 units of caster room towards the sun), 4096 x 4096 D32. The
    casters' model meshes (first pass of each; alpha-tested or blended passes cut out by stage 0's alpha, read from
    the texture's level 0: a smaller level seen from the sun at a grazing angle averages fronds away;
    render_options_shadows' pine casts a denser shadow so, 0.9% of that frame) go into it in a depth-only pass. Receivers: the ground pages (not water: the ground under it) and the world's model meshes
    after the ground (not the sky, the flat geometry or additive passes) sample it in their shaders (`shaders/shadow_receiver.hlsli`:
    soft edges below, receiver-plane depth bias, face normal from screen derivatives so surfaces turned
    away from the sun stay as the game lit them) and darken by up to half (the old decal's alpha 0x80) before fog.
    **Soft edges (R16j2; the user: the map's shadows were much harder than the old ones, the town gate's).** PCSS,
    contact-hardening: after the casters two small passes make the map's depth range per 16 x 16-texel cell
    (`shaders/shadow_reduce.hlsl`: nearest and farthest texel, Gather, shader model 4.1; `shadow_dilate.hlsl`: the same over the cells
    within the widest filter's reach, 3 cells at 56-72 units, plus the cell's own nearest; one 256 x 256 RGBA32F
    image). A receiver nearer than every depth around is lit, farther than every one shadowed (most pixels end
    there); near an edge the nearest depth around bounds the radius, and when that bound is wider than a cell a
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
    The classic shadows' decal (the flat mesh after `ClassicShadowDecal`) is dropped; the game's own shadow building
    (`RenderShadow`, the CPU raster) still runs: its decal's bounds feed the shadow lists. The classic option
    (`classicshadows`, `render::SetClassicShadows`, the harness's `shadows classic|map`) makes no map, samples none
    and draws the decals as before (frames as before R16j: the shaders' shadow branch is off).
    Cost (render_stats, vanilla 4K, per frame, map / classic): CPU hand-over + textures + end of frame + map in
    zone1 1.48 / 1.88 ms, fight 3.24 / 5.82, town 1.78 / 5.84 (no shadow textures uploaded: 4-15 a frame classic); the
    map's own CPU time 0.3-0.6 ms (the fit over the terrain vertices); GPU (timestamp queries, harness build) frame
    1.10 / 0.76 ms zone1, 1.16 / 1.07 fight, 0.95 / 0.70 town, of it the map's pass 0.06-0.27 ms.
    Check: scenario render_shadows (image only): zone1's start, a fight, creatures on a slope, the town bz1g, each
    frozen with the map, classic and the map again (samecapture, diffcapture, `shadowcheck`: map made, casters and
    receivers, and no object the game shadows and drew left out of the map: "drawn but not cast" 0; "not drawn"
    counts the ones off screen, 1-3 in those frames).
    Classic against the map (R16j1; the user's ruin block in the fight and fern in the town): every object whose
    shadow the game draws and which is on screen casts. The two looks differ because (1) the classic decals of
    overlapping objects stack, each darkening by half (the dirt below the fight's ruin is at 21% of its lit
    brightness classic, 43% with the map), and a decal is a blurred 64 x 64 silhouette laid on the terrain, also
    under an object standing higher (the ruin's platform), so it shows wider and darker; the ruin block's own map
    shadow falls on its sun side, on the platform and the dirt beside it; (2) a decal's direction is the sum of the
    sun and the point lights in reach (0051CE60's LightDirection: the town's campfire turns the gate's decal
    down-left, over the fern), the map's the sun's alone; the fern itself casts in neither (the game draws no
    shadow for it). To tell which object a shadow belongs to: a temporary filter in DrawShadow (0051CE60) by object
    id, classic and map captured in one run, with kStrength 1 (map shadows black); a dump of the map (a staging copy
    of its D32 image) showed that adding casters only ever lowers its depths. R16j3 holds what is left (darkness
    where shadows overlap, point lights). Open: casters the game does not draw (off screen) cast nothing, so a shadow can appear at the
    screen's edge with its object; terrain does not cast (R16i); point lights do not cast. `run_all --widescreen`
    runs with `classicshadows` (addon's frames have the decals).

Not worth a ticket now: pipeline and texture-binding changes (2-13 / 4-66 a frame) and the state reads per pass (part
of the 0.5-1.6 ms hand-over).
