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
Status: terrain (ground and water pages) drawn by the new renderer off-screen, shown on request;
everything else still drawn by the old path. Not yet run in the game (an offline test drives it with
fake game data, see section 8).

## 1. The switch (EI_RENDER)

`EI_RENDER` (environment, or the harness step `render <spec>` before the game starts) is a
comma-separated list:

| Option | Effect |
|---|---|
| `new` | the sidecar renders every frame into an off-screen target of the game's resolution; captures also write `frames\<name>.new.bmp` |
| `show` | a child window over the game's client area shows the new frame (mouse input passes through to the game: `WM_NCHITTEST` returns `HTTRANSPARENT`) |
| `split` | the child window covers the right half only: old frame left, new frame right, same pixels |
| `noterrain` | the old path stops drawing the terrain (DrawSectorPage returns after applying water levels) |

Without `new` nothing in `src/render` runs; the old path draws as before. Planned options follow the
order of work: `noobjects`, `noparticles`, `nointerface`, ... and finally `present` (the sidecar
presents and the old path's Flip / BltFast stops).

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
   Then the windows' CPU surfaces are blitted onto the render target (005FDA80), interleaved with 3D
   interface models (previews) and sprites.
3. 005040A0 EndScene; the escape menu (005F5320, Rebirth-patched).
4. The software cursor (only with that display option), **present** 004EF610 (rewritten,
   `src/game/device_present.cpp`: full screen Flip, window BltFast to the primary).

The sidecar's frame: game code hands data over while it runs its draw code; `render::EndFrame()`
(called from the present, or earlier by a capture) draws it: an off-screen pass with depth (3D scene:
so far terrain ground, then water), later a 2D composition pass (interface, cursor), then the
optional child-window pass, `sg_commit`. Per-frame data is rebuilt every frame (no caching of game
geometry yet); textures are cached per game texture object.

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
| Objects | CollectVisible's per-class visible and shadow lists (client world 007AF5B4); per object the model matrix +0x54 and the figure state +0x98 | the figure part draws 005A7A70 / 005A7640 (rewritten) | next |
| Figure meshes | CFigureMesh: positions in blocks of four (morphed / animated by the game), colours +0x138 / speculars +0x148 (lit by the game: 005A7A70 step 3), texture coordinates, offsets +0xCC / +0xD0, flat colour +0xD4; material +0x14C of the device (blend, technique stages, textures by name through 00693C60) | DeviceDrawMesh 004E9010 / 004E9A50, DeviceDrawIndexed 004EB550 (rewritten) | next |
| Shadows | 64x64 shadow textures rasterised on the CPU (2DintMMX slots 8 / 9 / 11 / 12 into a surface), ground quad 0056C750, drawn with 004EA4A0 | 0051CE60 / 004EA4A0 (rewritten) | later (or GPU shadows) |
| Particles, ribbons, lightning | particle sources' interpolated points, sizes, colours and texture rectangles (007A9BD0 / 007A9BE0), textures; ribbon polylines | 00502CA0 sprites, 00501D90 ribbons (rewritten) | later |
| Sky | the sky object (004D96E0: a model at the camera, turning with time) | the figure path | with objects |
| Interface | CUiSurface CPU surfaces (DirectDraw off-screen surfaces drawn with GDI and pixel code), their screen rectangles and colour keys (005FDA80); 3D previews (005EC300, 005ECA10, ...) and sprites / meshes (CUiMesh, TL triangles) | the surface blit 005FDA80, the mesh / sprite draws | later: surfaces uploaded as textures each frame they change |
| Cursor | the cursor object 007C7BD8 (shape 005E5200; hardware cursor normally) | - | later |
| Movies | Bink frames: BinkCopyToBuffer into a locked surface (005DB3F0 and 005DA880 loops, CBinkVideo 005DBA50 / 005DBD80) | the movie loops | later: copy into a CPU buffer, upload as a texture |

## 5. Order of work and estimates (one developer)

| Step | Content | Estimate |
|---|---|---|
| 1. Terrain | done: sidecar, device on Direct3D 11, off-screen target, child-window view, ground and water pages from sector data, page textures, fog from the old device, capture and mask tools. Left: check in game, the fog colour and range from game data, terrain decals (selection circles, prints: 006CCF20 / 006CEDE0 / 006CF370), the old-path seams at quarter borders | 1 week |
| 2. Objects and figures | hand-over from the figure part draws (meshes with the game's animation and lighting, model matrices, materials and their multi-pass techniques, alpha), the sky, picking rectangles computed in the hand-over, `noobjects` | 3-4 weeks |
| 3. Shadows | the shadow textures (upload what the game rasterises, or render shadows on the GPU) and their ground quads | 1 week |
| 4. Particles and effects | sprites (sorted back to front as the game sorts them), ribbons, lightning, flying hit points, weather | 1.5-2 weeks |
| 5. Interface | window surfaces as textures (dirty tracking through the surface writes the game does), composition in window order with colour keys, 3D previews, sprites / meshes, minimap, escape menu and save picture from the new frame, cursor | 2-3 weeks |
| 6. Movies | Bink frames into a CPU buffer and a texture, letterboxing | 0.5-1 week |
| 7. Switch-over | the sidecar presents (child window, then the game window once the old path stops presenting), display modes and window sizes, the old path off by default | 1-1.5 weeks |

About 10-13 weeks, plus verification scenarios per step (a capture scenario with a mask each, about
a day per step).

## 6. What happens to the rewritten draw functions

They become the hand-over points, as DrawSectorPage already is: their game-side work stays
(visibility, level of detail, animation, lighting, sorting, the water levels), the Direct3D
submission after it moves behind the flag and the data goes to `render::...` instead. Their verify
wrappers keep checking the game-side part. The Direct3D 7 code is deleted when we stop hooking
game.exe; then the 3DfpFPU / 2DintMMX calls left in game code (bounds at figure load, the terrain's
transform before clipping, the shadow rasteriser) get plain C++ replacements without exactness
requirements, or disappear when the GPU does the work.

| File | Hand-over (planned) |
|---|---|
| `terrain_draw.cpp` (006BFF40) | `render::TerrainPage` (done) |
| `figure_part_draw.cpp`, `figure_part_draw_plain.cpp` (005A7A70, 005A7640), `device_mesh.cpp` (004E9010, 004E9A50), `device_draw.cpp` (004EB550, 004EB370) | meshes, model matrices, materials (step 2) |
| `map_object_shadow.cpp` (0051CE60), `device_shadow.cpp` (004EA4A0), `figure_rasterise.cpp` (005A0E80) | shadows (step 3) |
| `renderer_sprites.cpp` (00502CA0), `renderer_strip.cpp` (00501D90), `particle_render.cpp`, `particle_draw.cpp` | particles and ribbons (step 4) |
| `ui_surface_pixels.cpp`, `ui_cursor.cpp`, `ui_hero_preview.cpp`, `ui_item_view.cpp`, `ui_hud*.cpp` | interface (step 5) |
| `texture*.cpp` | textures from the .mmp data directly (with step 2) |
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
  data) hands the module one sector page with and without a DirectDraw texture and reads the frame
  back: shader compilation (d3dcompiler_47), coverage with the Direct3D 7 pixel centres (a clip
  -0.5..0.5 square covers pixels 16..47 of 64), colours, readback, the split view.
- **In game, per feature**: at one frozen moment capture the old frame (A), the old frame without
  the feature (`oldterrain off`, later `oldobjects off`, ...: B), and the new renderer's frame (N);
  compare N with A only where A and B differ and B shows the clear colour (nothing in front of the
  feature: no translucent interface panel, object or shadow):
  `python tools\compare_frames.py N A --mask A B --mask-background` (scenario `render_terrain`).
  `toolsrame_preview.py` writes a downscaled side-by-side PNG of captures.
- **Terrain, first run** (vanilla, 3840x2160): with the plain mask 29.6 % of the masked pixels
  differed (the mask held the translucent interface panels and the objects' shadows, which the new
  frame does not have yet); with `--mask-background` (51.5 % of the frame is terrain with nothing in
  front) **1.80 %** differ by more than 8, mean 1.15 per channel, the average colours equal to 0.1.
  The rest is on steep rock slopes: texture detail under strong minification. The new renderer now
  reads the textures' own mip levels (the .mmp file's, attached to the surface) instead of
  box-filtering level 0. The light strip at the top of the new frame is the far fogged sea and the
  clear colour between the last water pages; the old frame draws the sky object there (and the
  interface's top bar covers it).
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
- `render <spec>`, `oldterrain on|off`: section 1 and 8.
- `tools/compare_frames.py A B [--mask M1 M2] [--diff OUT]`: per-pixel tolerance (default 8),
  differing fraction (default limit 0.1 %), worst regions; standard library only.

Limits: what moves by itself between frames without the game's clocks (other threads) is not held;
the harness's own waits use real time.
