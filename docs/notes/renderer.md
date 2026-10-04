# Renderer: the new sidecar renderer, the data it takes from the game, and the old boundary

The game's own renderer (DirectDraw 7 / Direct3D 7, translated by dgVoodoo in our runs) is being
replaced. The design:

1. The game code that calls the renderer is rewritten (much of it already is). Instead of talking to
   Direct3D it hands its data to a **new renderer in our own sidecar module** (`src/render/`), which
   draws the frame from data pulled from the game: camera and matrices, object transforms, figure
   meshes, terrain sectors, particles, textures, the interface windows' CPU surfaces (uploaded as
   textures at first), movie frames. The old Direct3D calls stay behind a flag until we stop hooking
   into game.exe.
2. No Direct3D 7 emulation, no bit-exact rewrite of 3DfpFPU.dll / 2DintMMX.dll, no x87 or
   compatibility shims in the new code. Results only need to be close: captures are compared with a
   tolerance. Picking, culling and the save game's picture are visual / UI (section 3 checks what the
   renderer feeds back into game logic).
3. Full determinism is out of scope.

The backend is **sokol_gfx** (`third_party/sokol`, commit 2e75443, zlib licence) on **Direct3D 11**,
on the game's own window. Code: `src/render/` (static library `ei_render`, linked into ei_hook).
Status: the new renderer draws, off-screen and shown on request, the terrain (ground and water
pages; checked in game, section 8), every mesh the old path draws (figures, map objects, the sky, and
the interface's sprites and 3D previews), and the interface windows' surfaces, in the game's order;
depth clears in the middle of the frame included. Not drawn yet: shadows and ground decals,
particles, ribbons, lightning, flying hit points, the rectangle fills (005041F0), the software
cursor, movies. The mesh and surface hand-overs have only run in the offline test so far (section
8); the first in-game run is `tests/scenarios/render_scene.txt`.

## 1. The switch (EI_RENDER)

`EI_RENDER` (environment, or the harness step `render <spec>` before the game starts) is a
comma-separated list:

| Option | Effect |
|---|---|
| `new` | the sidecar renders every frame into an off-screen target of the game's resolution; captures also write `frames\<name>.new.bmp` |
| `show` | a child window over the game's client area shows the new frame (mouse input passes through to the game: `WM_NCHITTEST` returns `HTTRANSPARENT`) |
| `split` | the child window covers the right half only: old frame left, new frame right, same pixels |
| `noterrain` | the old path stops drawing the terrain (DrawSectorPage returns after applying water levels) |
| `noobjects` | ... the meshes drawn inside the world view (figures, map objects, the sky) and the flat geometry there (004EA4A0: shadows, ground decals) |
| `nointerface` | ... the meshes drawn outside the world view (the interface's sprites and 3D previews) and the window surfaces' blits |

Without `new` nothing in `src/render` runs; the old path draws as before. The harness has the steps
`oldterrain`, `oldobjects`, `oldinterface` (on / off) for the same switches during a run. Planned:
`noparticles`, ... and finally `present` (the sidecar presents and the old path's Flip / BltFast
stops).

Why a child window and not a swap chain on the game window itself: dgVoodoo already owns a DXGI
swap chain on that HWND (a second flip-model swap chain on one window fails). A child window with
its own swap chain sits on top of it. When the old path stops presenting (and in the standalone exe)
the swap chain moves to the game window.

## 2. Frame structure

What the game draws per frame (00458010, once per main-loop iteration):

1. 00503F40 BeginScene (frame statistics).
2. 005F5220: the interface draws, window by window. The battle screen's world view is 00569E40:
   - view setup 00566360 (view matrix from the camera 0079B2A8 through 004F0EF0, pushed on the
     transform pipeline's stacks; fog range), the visible sector range around the camera focus
     (006CA260, 100 units);
   - the sky object (flag 0x1000: 004D96E0, a model following the camera, turning with time;
     z-write off, alpha test off);
   - terrain ground pages (006C6230 -> 006C6460 -> DrawSectorPage 006BFF40 per sector page);
   - objects (0056AFE0 -> CollectVisible 0056A990, sort, figure parts 005A7A70 / 005A7640, shadows
     0051CE60);
   - terrain water pages (006C6330), blended;
   - effects (flag 0x400): the vision fog layer (004E2700 -> CVisibilityFog 004E1EB0), the
     client's slot 36, particles 00498940, lightning (00468940 CLightningManager::DrawAll), rain and
     snow (004E5680);
   - 00569D60, flying hit points (005E18E0, flag 0x2000).
   Then the interface, window by window (the top window's slot 20 draws its children): a window's
   sprites (CUiMesh, 005E6D90: a matrix pushed on both stacks, the colour through renderer slot 7,
   the figure through renderer slot 14 -> 005A7640 -> the device's mesh draw 004E9010), its 3D
   models (CUiModel 005FC2F0: the model's own draw, the same mesh draws; the hero preview 005EC300
   first clears the depth buffer to 0 and its rectangles to 1), and (slot 22, 005FECC0) its
   surfaces: 005FDA80 blits each shown CUiSurface's DirectDraw surface onto the render target,
   plain or with a source colour key (DDBLT_KEYSRCOVERRIDE); no alpha blending. The surfaces are
   composed on the CPU (GDI text 005FDE20 / 005FDFA0, fills, pixel code). The battle screen clears
   the depth buffer before its panels (00636020). The translucent dark panels over the scene
   (35 % black: the top panel and the portrait panel) are most likely sprites. Renderer slot 19
   (005041F0) draws translucent black rectangles with an outline; the callers found are the
   rubber-band selection (00636020) and the fades (005E69C0).
3. 005040A0 EndScene; the escape menu (005F5320, Rebirth-patched).
4. The software cursor (only with that display option), **present** 004EF610 (rewritten,
   `src/game/device_present.cpp`: full screen Flip, window BltFast to the primary).

The sidecar's frame: game code hands data over while it runs its draw code, as a list of commands in
the game's order (terrain batch, mesh pass, surface blit, depth clear); `render::EndFrame()` (called
from the present, or earlier by a capture) draws them in one off-screen pass with depth, then the
optional child-window pass, `sg_commit`. Per-frame data is rebuilt every frame (no caching of game
geometry yet); textures and window surfaces are cached per surface holder and read again when the
DirectDraw surface's uniqueness value says its contents changed.

Conventions taken over: Direct3D clip space (the game's projection already produces it), pixel
centres of Direct3D 7 (integer screen coordinates; the viewport is moved by half a pixel), D3DCOLOR
vertex colours (B, G, R, A bytes), vertex fog in the specular alpha (255 = no fog), specular added
after texturing.

## 3. What the renderer feeds back into game logic

Checked statically (scan of every `TEST [reg+8],0x20` reader and of the draw paths' side effects):

- **Visibility (frustum test 004F16A0, in-view flag 0x20 of the client object)**: set by
  CollectVisible on client objects (CObject3DClientSpecific, CUnitClientSpecific). Its readers:
  the client object's point and rectangle hit tests (0051C130, vtable slots 0073DEEC / 0073DEF0:
  cursor picking and rubber-band selection, i.e. the interface), the particle source draw
  (00496EB0, client), and the shadow volume test (0051E300). Its other effects, all client-side:
  visible units are animated to the game time (client slot 25), pending figure changes are applied
  (0050D450), models are touched in the model cache's LRU. The server objects (CWorldServer,
  CUnitServer, CMapObjectServer) are separate instances; their action timing comes from the
  animation data (CUnitServer::AnimationDuration / AnimationMoment, docs/notes/combat.md), not from
  the client's figures. Other 0x20 tests at +8 belong to other objects (the camera's "playing"
  flag 005788B0, figure loaders). **Result: visibility only gates client-side animation, figure
  changes and picking; it does not feed server-side game state or timing.**
- **Picking**: which unit is under the cursor (and what a click orders) uses screen rectangles
  written during the object draw from projected vertices (004E8900 / 004E86B0 into the device's
  +0x15C, copied by 005A7A70 into the node and figure pieces). Visual / UI by the design, but when
  the old object draw is switched off these rectangles must still be produced: the hand-over code
  will compute them on the CPU from the projected bounds (approximate is fine).
- **One real state change in a draw path**: the terrain page draw applies pending water level
  changes to the sector's water layer (006C30E0 from 006BFF40; otherwise only when a sector is
  built). The water layer is what height queries read (VertexAt / GroundAndWaterDepth 006C8930 ->
  TerrainOwner::SurfaceHeight: camera, object placement). It stays in the game code: DrawSectorPage
  applies the levels before handing the page over or skipping the old draw.
- **Pixels**: the escape menu's background and the save game's 256x192 picture are copied from the
  render target (005FD3D0 CaptureScreen, 005FD440 Rgb565Copy). With the sidecar presenting, they
  must be taken from the sidecar's frame (one readback when the menu opens).
- **Timing**: the main loop runs up to five world updates of 0078FF34 ms per frame; vsync on the
  present only changes how updates group into frames, not their results. The sector cache ages
  sectors by frames drawn (240 frames), which affects loading, not results.

## 4. Data the sidecar needs and where it comes from

| Data | Where in the game | Handed over by | Status |
|---|---|---|---|
| Clip matrix of the current draw | the transform pipeline (renderer 007AF060 +8): stacks of 64-byte row-major matrices, out = M * (x, y, z, 1); +0x24 / +0x28 data / depth: clip space (axis swap * projection * view * model, Direct3D clip volume); +0x1C: the same times the viewport (screen pixels); +0x5C / +0x60 the view stack | read at hand-over time (the top is the current object's or the landscape's) | used by the terrain |
| Camera | CCamera 0079B2A8 (position, rotation, focus; settings near / far / fov via 0040D380) | (the matrices above are enough so far) | - |
| Fog | render states FOGENABLE / FOGCOLOR of the old device (read per frame); the zone's colour is set through renderer slot 0 (00501AB0); fog range in the transform pipeline (slot 5, from 00566360) | the terrain hand-over | first step: read from the old device |
| Clear colour | renderer +0x14 (00501A40 clears with it) | EndFrame | used |
| Render target size | device object 007AF080 -> +0x18 (CDXSurface +0x14 / +0x18) | EndFrame | used |
| Terrain sectors | SectorData (terrain_sector.h): world positions +0x17C (33x33 per layer, water layer after the ground), vertex records +0x190 (0x54 bytes: lit colour +0x11, specular / fog +0x15), tile lists per quarter and page +0x314 (9 entries per tile: record pointer, u, v), tile edge flags +0x2C4, quarter visibility +0x144 (2 = outside), level of detail +0x170, seam flags +0x174 / +0x178, owner TerrainFields +0x1C | DrawSectorPage (006BFF40) per visible page | drawn (ground and water) |
| Terrain textures | TerrainFields +0xD8: CTexture* per page, index page % (pages + 2); CTexture +0x5C -> +0x10 the managed DirectDraw surface and its attached mip levels | TerrainPage, read once (Lock), converted to RGBA8 | used; later from the .mmp files |
| Objects | CollectVisible's per-class visible and shadow lists (client world 007AF5B4); per object the model matrix +0x54 and the figure state +0x98 | through the mesh draws below | drawn |
| Meshes (figures, objects, sky, interface sprites and previews) | positions in blocks of four (morphed / animated by the game), the corners (position, colour, texture coordinate indices), colours / speculars as lit by the game (005A7A70 step 3; flag 4: the device's flat colour +0x150 and an alpha), texture coordinates and offsets; the matrix: the top of the pipeline's screen stack (+0x1C, the clip stack +0x24 times the viewport mapping, converted back to clip space for the full frame); the old device's distance fog switch +0x158 and range (transform object +0x34 / +0x3C) | DeviceDrawMesh 004E9010 / 004E9A50 -> `render::BeginMesh` | drawn |
| Pass state | per pass of a mesh (the material's technique: one state block per pass, 004EB550; or the two textures of 004EB370): blend and factors, alpha test, depth test / write / function, culling, specular, fog and its colour, texture factor, two texture stages' colour / alpha operations and arguments, filters, addressing, and the bound surface holders (device +0x12C / +0x130) | DeviceDrawIndexed 004EB550 / DeviceDrawTwoTextures 004EB370 -> `render::MeshPass`, read back from the old device (the state blocks are opaque) | drawn; later the materials from the game's data |
| Depth clears | the battle screen's before its panels (00636020), the hero preview's (005EC300: all to 0, its rectangles to 1) | `render::ClearDepth` | drawn; 00627390 (not rewritten) missing |
| Shadows | 64x64 shadow textures rasterised on the CPU (2DintMMX slots 8 / 9 / 11 / 12 into a surface), ground quad 0056C750, drawn with 004EA4A0 | 0051CE60 / 004EA4A0 (rewritten) | later (or GPU shadows) |
| Particles, ribbons, lightning | particle sources' interpolated points, sizes, colours and texture rectangles (007A9BD0 / 007A9BE0), textures; ribbon polylines | 00502CA0 sprites, 00501D90 ribbons (rewritten) | later |
| Sky | the sky object (004D96E0: a model at the camera, turning with time) | the figure path | with objects |
| Interface surfaces | CUiSurface (0x38 bytes): +0x0C flags (1 colour key, 2 whole surface stretched, 4 shown), +0x10 the surface holder, +0x24 the screen rectangle, +0x34 the colour key; the DirectDraw surface's pixels (read with a read-only Lock when its uniqueness value changed) | the blit 005FDA80 (rewritten: `ui_surface_blit.cpp`) -> `render::SurfaceBlit` | drawn (overlay) |
| Rectangle fills | renderer slot 19 (005041F0): a translucent black quad (alpha argument) and optionally its outline | - | missing (the rubber band, fades) |
| Cursor | the cursor object 007C7BD8 (shape 005E5200; hardware cursor normally; the display option's software cursor is drawn into locked surfaces by 005E5690 / 005E5C40 after the capture point) | - | later |
| Movies | Bink frames: BinkCopyToBuffer into a locked surface (005DB3F0 and 005DA880 loops, CBinkVideo 005DBA50 / 005DBD80) | the movie loops | later: copy into a CPU buffer, upload as a texture |

## 5. Order of work and estimates (one developer)

| Step | Content | Estimate |
|---|---|---|
| 1. Terrain | done and checked in game (section 8): sidecar, device on Direct3D 11, off-screen target, child-window view, ground and water pages from sector data, page textures with their own mip levels, fog from the old device, capture and mask tools. Left: terrain decals (selection circles, prints: 006CCF20 / 006CEDE0 / 006CF370), the fog colour and range from game data | 0.5 week |
| 2. Interface overlay | first version done (moved ahead of the objects at the user's request, so that captures stop differing where the old frame has the translucent interface over the scene): every mesh outside the world view, the window surfaces as textures read on change, depth clears, `nointerface`. Left: rectangle fills (005041F0), the depth clear of 00627390, the software cursor, the escape menu's and save picture's screen copy (005FD3D0) from the new frame, fixes after the first in-game run | 1 week |
| 3. Objects and figures | first version done with the interface (the same mesh draws): model-space meshes with the game's animation and lighting, the screen stack matrix, every pass with the old device's state (materials' multi-pass techniques, alpha, culling, distance fog), the sky, `noobjects`. Left: in-game check and fixes, picking rectangles computed in the hand-over once the old projection goes, the materials from the game's data instead of the old device's state | 2-3 weeks |
| 4. Shadows | the shadow textures (upload what the game rasterises, or render shadows on the GPU) and their ground quads | 1 week |
| 5. Particles and effects | sprites (sorted back to front as the game sorts them), ribbons, lightning, flying hit points, weather | 1.5-2 weeks |
| 6. Native interface | the window surfaces drawn by the new renderer instead of uploaded (estimate below) | 5-7 weeks |
| 7. Movies | Bink frames into a CPU buffer and a texture, letterboxing | 0.5-1 week |
| 8. Switch-over | the sidecar presents (child window, then the game window once the old path stops presenting), display modes and window sizes, the old path off by default | 1-1.5 weeks |

About 8-11 weeks to a complete new frame with the overlay interface (steps 1-5, 7, 8), plus the
native interface when we want it (step 6), plus verification scenarios per step (a capture scenario
with masks each, about a day per step).

### The interface: overlay or native (estimates)

What the interface's draw path is (section 2): sprites and 3D models go through the same mesh draws
as the world (so both options draw them the same way, done); everything else is CPU composition into
the windows' DirectDraw surfaces (GDI text with the game's fonts and code page, fills, lines, pixel
code: the console, chat, descriptions, objectives, item icons and faces composed by CopyImage /
Greyscale, dialogue, lobby, tooltips, the frame rate) blitted plain or colour-keyed in window
order; renderer slot 19's translucent rectangles; the cursor (hardware normally).

- **Overlay** (chosen, first version in this step): upload each window surface as a texture when its
  contents changed (the surface's uniqueness value, checked at every blit) and draw it as a
  rectangle at the blit's place in the command list, colour key as transparency, texels copied one
  to one (stretched when the game stretches). Done: about 2 days. Left: about 1 week (above). Cost:
  only changed surfaces are read; a full-screen 4K surface changing every frame would be 33 MB of
  conversion per frame (the backdrops do not change).
- **Native**: the new renderer draws text (a glyph atlas rasterised with GDI from the game's fonts,
  to keep the metrics the word wrap 005F8340 and the layouts depend on: about 1 week), every
  window's surface drawing turned into text / quad / sprite submissions (about 25 window classes'
  Draw / Update methods, many not rewritten yet: 3-4 weeks), the screen copies from the new frame
  (0.5 week), verification per screen (scenarios reaching camp, trade, spell book, map, dialogue,
  lobby: about 1 week). About 5-7 weeks: not nearly as cheap. It can come window by window later:
  a window switched to native drawing simply stops blitting its surface; the command order and the
  quads are already in place.

## 6. What happens to the rewritten draw functions

They become the hand-over points, as DrawSectorPage already is: their game-side work stays
(visibility, level of detail, animation, lighting, sorting, the water levels), the Direct3D
submission after it moves behind the flag and the data goes to `render::...` instead. Their verify
wrappers keep checking the game-side part. The Direct3D 7 code is deleted when we stop hooking
game.exe; then the 3DfpFPU / 2DintMMX calls left in game code (bounds at figure load, the terrain's
transform before clipping, the shadow rasteriser) get plain C++ replacements without exactness
requirements, or disappear when the GPU does the work.

| File | Hand-over |
|---|---|
| `terrain_draw.cpp` (006BFF40) | `render::TerrainPage` (done) |
| `device_mesh.cpp` (004E9010, 004E9A50) | `render::BeginMesh` / `EndMesh` after the visibility test and the colour override (done) |
| `device_draw.cpp` (004EB550, 004EB370) | `render::MeshPass` per pass; the old DrawIndexedPrimitive skipped with `noobjects` / `nointerface` (done) |
| `world_client_draw.cpp` (00569E40) | `render::BeginWorld` / `EndWorld`: the world's meshes from the interface's (done) |
| `ui_surface_blit.cpp` (005FDA80, new) | `render::SurfaceBlit` (done) |
| `ui_battle_setup.cpp` (00636020), `ui_hero_preview.cpp` (005EC300) | `render::ClearDepth` (done) |
| `figure_part_draw.cpp`, `figure_part_draw_plain.cpp` (005A7A70, 005A7640) | model matrices and figure data directly, once the meshes no longer go through the old projection (step 3) |
| `map_object_shadow.cpp` (0051CE60), `device_shadow.cpp` (004EA4A0), `figure_rasterise.cpp` (005A0E80) | shadows (step 4); `device_shadow.cpp` already skips its draw with `noobjects` |
| `renderer_sprites.cpp` (00502CA0), `renderer_strip.cpp` (00501D90), `particle_render.cpp`, `particle_draw.cpp` | particles and ribbons (step 5) |
| `ui_surface_pixels.cpp`, `ui_cursor.cpp`, `ui_item_view.cpp`, `ui_hud*.cpp` | the screen copies, the cursor, native windows (steps 2 and 6) |
| `texture*.cpp` | textures from the .mmp data directly |
| `renderer_init.cpp` (00500B70) | stays for the old path |
| `device_present.cpp` (004EF610) | `render::Present` (done) |

## 7. The old boundary (reference)

Kept for reference while the old path still runs; the first version of this note (commit ccaa452)
has the full per-call inventory.

- Imports: `DirectDrawEnumerateExA`, `DirectDrawCreateEx` (always `IID_IDirectDraw7`),
  `DirectInputCreateEx` (the mouse), `DirectSoundEnumerateA` (the Miles device list), 12 Bink
  functions; `2DintMMX.dll` / `3DfpFPU.dll` loaded by name (00500540, 004EDD70, 004F0B90).
- 524 DirectDraw / Direct3D call sites in 143 functions (26 rewritten). Interfaces: IDirectDraw7
  (12 methods), IDirectDrawSurface7 (17), IDirect3D7 (5), IDirect3DDevice7 (21),
  IDirectDrawGammaControl (SetGammaRamp); clipper and vertex buffer code is dead.
- The game transforms, lights, clips and fogs on the CPU (3DfpFPU) and draws pre-transformed
  vertices (FVF 0x1C4 / 0xC4) as triangle lists, a few strips and line strips; `DDSCL_FPUSETUP` sets
  the main thread's x87 precision to 24 bits (docs/notes/rebirth.md).
- Objects: renderer 007AF060 (vtable 0073D93C: slot 0 fog colour, 2 create, 4 / 5 begin / end
  frame, 8 clear, 11 set texture, 12 draw TL triangles, 16 ribbons, 17 sprites), device object
  007AF080 (0x17C bytes: +0xC IDirectDraw7, +0x14 primary, +0x18 render target, +0x54
  IDirect3DDevice7, +0xAC transform pipeline, +0x14C current material), surface wrappers
  (CDXSurface, 0x20 bytes, +0x10 the surface), device lock 007AEE78.
- **3DfpFPU.dll** (`GetTLPipeline`, a static object with 45 slots, 56.7 KB of x87 code): projection
  with outcodes, homogeneous clipping (slots 36-38), viewport mapping (31-34), vertex fog into the
  specular alpha (35), lighting into packed colours (39-44), bounds (6, 7, 29). Callers found:
  slots 1-5, 7-9, 13, 14, 18, 19, 24, 26, 29, 34, 35, 37, 40.
- **2DintMMX.dll** (`GetGraphicsProcessor`, 13 slots, 9.6 KB of MMX code): 8888 to 16-bit
  conversion (1, 2), skin compositing blends (3-5), premultiply (6), mip downsampling (7), the shadow
  triangle rasterisers (8, 9), a 16-bit block operation (10), the shadow blur (11, 12).
- Both DLLs are identical in the GOG and Rebirth installs.

## 8. Verification

- **Offline**: a stand-alone program (not committed; it points `render::game::Where()` at its own
  data) hands the module one sector page with and without a DirectDraw texture (with mip levels)
  and reads the frame back: shader compilation (d3dcompiler_47), coverage with the Direct3D 7 pixel
  centres (a clip -0.5..0.5 square covers pixels 16..47 of 64), colours, readback, the split view.
  A second case hands over a mesh with a screen-pixel matrix (a quad from 8 to 24 covers pixels
  8..23, as Direct3D 7 would), a depth clear, colour-keyed and plain window surface blits (keyed
  texels left out, one-to-one placement, a stretched blit, a partial one) and a window surface
  changed between two frames (read again through its uniqueness value). There is no old Direct3D
  device in it, so the pass state (all zero) is not exercised.
- **In game, per feature**: at one frozen moment capture the old frame with less and less of it
  drawn by the old path (`oldobjects off`, `oldterrain off`, `oldinterface off`) and the new
  renderer's frame (always all of it); masks from pairs of old frames pick the pixels of one
  feature, `--mask-background` keeps those with nothing in front, `--exclude` leaves out another
  feature's. Scenario `render_scene` (its header has the compare commands for the whole frame, the
  terrain, the objects and the interface); `render_terrain` is the terrain alone.
  `tools/frame_preview.py` writes a downscaled side-by-side PNG of captures.
- **Terrain, first run** (vanilla, 3840x2160, before the textures' own mip levels): with the plain
  mask 29.6 % of the masked pixels differed (the mask held the translucent interface panels and the
  objects' shadows, which the new frame does not have yet); with `--mask-background` (51.5 % of the
  frame is terrain with nothing in front) **1.80 %** differ by more than 8, mean 1.15 per channel,
  the average colours equal to 0.1. What is left, explained from the same captures:
  - Nearly all of it is on steep slopes in the distance (rows 640-1200 of the frame: 1.5-6.2 % of
    the pixels per 80-row band differ, at most 1.3 % and mostly under 0.7 % elsewhere) with no
    colour bias (in the worst 64x64 blocks the mean signed difference stays under 1.6 levels while
    the mean absolute one is 5-10): texture detail under strong minification (mip level choice and
    filtering). The new renderer now reads the textures' own mip
    levels (the .mmp file's, attached to the surface) instead of box-filtering level 0; the next
    run shows whether that closes it.
  - **Fog** matches: the mean signed difference per 80-row band from the horizon down stays between
    -0.16 and +0.02 levels per channel and does not grow towards the horizon, so the fog colour and
    curve (vertex fog in the specular alpha, from the sector records) are the old path's.
  - **Water near the coast looked lighter** in the downscaled frames: that is the interface, not the
    water. Two translucent dark panels (35 % black: the top panel over the whole width, y < 360,
    and the portrait panel at the left, x 128-800, y < 800) dim the old frame's scene under them to
    0.687 of its colour; old / new is 0.68-0.70 in all three channels at the water pixels sampled
    there (e.g. old 67,62,42 / new 96,90,60 BGR at 1000,150). Inside the mask, which leaves those
    panels out, the water matches like the ground.
  - The **light strip** at the top of the new frame is the far sea's edge (the clear colour in
    steps where the last water pages end); in the old frame the interface's top bar covers it.
  With the interface in the new frame (`render_scene`) these areas are compared as they are.
- **Freeze stability**: `samecapture world world_again` in frames_basic and render_terrain (two
  captures in one frozen moment must match).
- Cross-run comparisons are not meaningful (no determinism); the A/B above is always within one run.

## 9. Frame capture (harness, verify build)

- `capture <name>`: skips two presented frames, writes the next to `frames\<name>.bmp` (24-bit,
  lossless) from the old render target at the present point (after the scene and interface, before
  the software cursor and the flip; a read-only Lock of the render target, GetDC as fallback; a GDI
  capture of the window would come back black with dgVoodoo's DXGI presentation). With the new
  renderer on, its frame of the same moment goes to `frames\<name>.new.bmp` (render::EndFrame, then
  a staging copy of the off-screen target).
- `freeze` / `unfreeze`: hold timeGetTime / GetTickCount and the camera. The main loop then runs
  no world, client or camera update. The camera has to be held separately: the main loop calls
  CCamera::Update once per frame even with no elapsed time (00457595, dt 0), and parts of it move a
  fixed fraction per call (the focus drift to the tracked object, the focus height lag, the lift
  over nearby objects). That was the 8.4 % difference between two frozen captures (measured: the
  whole scene moved about one pixel between frames while the camera was still settling after the
  new game; a later run where it had settled showed none).
- `samecapture <a> <b> [tolerance] [fraction]`: fails the run when two captures differ.
- `render <spec>`, `oldterrain on|off`, `oldobjects on|off`, `oldinterface on|off`: section 1 and 8.
- `tools/compare_frames.py A B [--mask M1 M2 [--mask-background]] [--exclude E1 E2 ...]
  [--exclude-grow N] [--diff OUT]`: per-pixel tolerance (default 8), differing fraction (default
  limit 0.1 %), worst regions; standard library only.

Limits: what moves by itself between frames without the game's clocks (other threads) is not held;
the harness's own waits use real time.
