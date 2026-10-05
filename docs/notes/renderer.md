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
Status: the new renderer draws, off-screen and shown on request, in the game's order: the terrain
(ground and water pages), every mesh the old path draws (figures, map objects, the sky, the
interface's sprites and 3D previews), the flat geometry (object shadows, selection markers, light
decals), the interface windows' surfaces, the renderer's rectangles (rubber band, fades), the
software cursor, and the depth clears in the middle of the frame; the escape menu's and save
game's screen copy comes from the new frame. Checked in game: terrain, meshes, interface
(`render_scene`, section 8). Also handed over: every pre-transformed draw of the renderer
(particles and sprites, the vision fog's quads, ribbons and lightning, flying hit points) and the
weather (after its builders draw). Movies (Bink) are handed over, the players outside the frame too (section 13); the trade grid's depth clears too (00627390 rewritten).
The switch-over mode `present` exists (section 5, "Switch-over"); in it the interface windows', the
cursor's and the movies' surface holders have no DirectDraw surface, only memory the new renderer
owns (section 14), and so do the texture manager's CPU-written textures (the save / load pictures,
the global map's weather, the item pictures: section 15) and the shadow and .bmp textures, while the
.mmp textures are only the new renderer's decoded copy (section 16): no texture is a DirectDraw
texture there.

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
| `cpuwindows` | the surfaces of 004EF7B0 (interface windows, software cursor, movies) and the system-memory picture surfaces of 004F0060 (the cursor's images, picture windows) keep their pixels in memory the new renderer owns (DIB sections, `render::SurfaceMemory`) and reads directly; on with `present`, `nocpuwindows` turns it off. With `present` these holders have no DirectDraw surface at all (section 14), nor do the texture manager's textures (00691D80, the shadow and .bmp textures: memory; the .mmp textures: the new renderer's decoded copy; sections 15, 16) |
| `present` | the switch-over: everything handed over is off in the old path (`noterrain,noobjects,nointerface`, movie blits too; `cpuwindows` on) and at the old present (004EF610) the new frame is copied into the game's render target, which the old Flip / BltFast then shows; captures write the new frame as `frames\<name>.bmp`. Scenario `render_present` (frames_basic in this mode). A first version showed the frame in the child window instead: under dgVoodoo's full screen the second swap chain made the game window inactive (WM_ACTIVATE inactive: the game's frame function then draws nothing and the window minimises itself), so no frame was drawn at all. `show` / `split` have the same problem in full screen; use them windowed |

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
centres of Direct3D 7 (integer screen coordinates; the 3D draws move their clip positions by half a
pixel, `Frame::pixelShift`: sokol's float viewport is truncated to whole pixels), D3DCOLOR
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
| Depth clears | the battle screen's before its panels (00636020), the hero preview's (005EC300: all to 0, its rectangles to 1), the trade grid's (00627390), the hero equipment panel's (00622420: all to 0, the seven body slots to 1, all to 1), screens 2 / 3 (00632A90 / 00602BF0) | `render::ClearDepth` | drawn |
| Shadows | 64x64 shadow textures rasterised on the CPU (2DintMMX slots 8 / 9 / 11 / 12 into a surface), ground quad 0056C750, drawn with 004EA4A0 | 004EA4A0 (rewritten) -> `render::BeginMesh` without corners (per-vertex positions, one colour pair, depth scale); the texture from the render surface right after 0051CE60 copies it (`render::TextureChanged`) | drawn |
| Particles, ribbons, lightning, hit points, vision fog | the renderer's pre-transformed draws: sprites 00502CA0 -> 00503680 (particles, the vision fog's quads), ribbons 00501D90 -> 00502A20 (lightning), triangles 00501BB0 (rewritten, `renderer_triangles.cpp`: flying hit points); the line strips 005045D0 / 0049B4B0 (`renderer_lines.cpp`: debug lines) | `render::TlDraw` at the draws (vertices after the fog pass, the old device's state) | drawn; later: the particle data itself instead of its screen quads |
| Weather | rain and snow quads built by 004E4240 / 004E4C80 (2.6 KB each, not rewritten; procedural placement) into pre-transformed vertices (+0x70, count +0x74, indices +0x84) and drawn with the device directly | the weather draw 004E5680 rewritten (`weather_draw.cpp`): after the rain / snow returns, its triangles go to `render::TlDraw` with the device state of that moment | drawn; the old draw stays (the two builders are not split yet) |
| Sky | the sky object (004D96E0: a model at the camera, turning with time) | the figure path | with objects |
| Interface surfaces | CUiSurface (0x38 bytes): +0x0C flags (1 colour key, 2 whole surface stretched, 4 shown), +0x10 the surface holder, +0x24 the screen rectangle, +0x34 the colour key; the DirectDraw surface's pixels (read with a read-only Lock when its uniqueness value changed) | the blit 005FDA80 (rewritten: `ui_surface_blit.cpp`) -> `render::SurfaceBlit` | drawn (overlay) |
| Rectangle fills | renderer slot 19 (005041F0): a translucent black quad (alpha argument) and optionally its outline | 005041F0 rewritten (`renderer_rect.cpp`) -> `render::ScreenDraw` (pre-transformed triangles, line strip) | drawn |
| Cursor | the cursor object 007C7BD8 (shape 005E5200; hardware cursor normally; the display option's software cursor is drawn into locked surfaces by 005E5690 / 005E5C40 after the capture point) | the frame draw 005E54A0 rewritten (`ui_cursor_frame.cpp`) -> `render::Cursor` (image from its picture's file data, 004EFC60 -> `render::PictureFromMmp`, and mask; texels with a mask left out) | drawn; the cursor thread's draws between frames go to the primary (005E5690 rewritten); with `present` they, the background saves and the frame draw's old blits are left out (section 13) |
| Movies | Bink frames: BinkCopyToBuffer into the video's locked surface (CBinkVideo: 005DBA50 opens, 005DBD80 steps a frame and blits it into its letterboxed rectangle of the render target or the primary; called by the movie screen's draw 0062BD80 inside the frame) | 005DBD80 rewritten (`bink_video.cpp`) -> `render::SurfaceBlit` (the surface read when the decode changed it) | drawn; with `present` the old blit is skipped |

## 5. Order of work and estimates (one developer)

| Step | Content | Estimate |
|---|---|---|
| 1. Terrain | done and checked in game (section 8): sidecar, device on Direct3D 11, off-screen target, child-window view, ground and water pages from sector data, page textures with their own mip levels, fog from the old device, capture and mask tools. Left: terrain decals (selection circles, prints: 006CCF20 / 006CEDE0 / 006CF370), the fog colour and range from game data | 0.5 week |
| 2. Interface overlay | first version done (moved ahead of the objects at the user's request, so that captures stop differing where the old frame has the translucent interface over the scene): every mesh outside the world view, the window surfaces as textures read on change, depth clears, `nointerface`. Second round done: rectangle fills (005041F0), the software cursor (005E54A0), the screen copy (005FD3D0) from the new frame (a frame can now be drawn in several passes; `render::CopyFrameTo`), the half-pixel fix. Left: the depth clear of 00627390 (the camp's trade grid, 1421 bytes, not rewritten), fixes after the next in-game run | 2 days |
| 3. Objects and figures | first version done with the interface (the same mesh draws): model-space meshes with the game's animation and lighting, the screen stack matrix, every pass with the old device's state (materials' multi-pass techniques, alpha, culling, distance fog), the sky, `noobjects`. Left: in-game check and fixes, picking rectangles computed in the hand-over once the old projection goes, the materials from the game's data instead of the old device's state | 2-3 weeks |
| 4. Shadows | done: the CPU-rasterised shadow textures and their ground geometry through the flat draw, with the selection markers and light decals. Later: GPU shadows | (done) |
| 5. Particles and effects | sprites (sorted back to front as the game sorts them), ribbons, lightning, flying hit points, weather | 1.5-2 weeks |
| 6. Native interface | the window surfaces drawn by the new renderer instead of uploaded (estimate below) | 5-7 weeks |
| 7. Movies | done: BinkCopyToBuffer into a CPU buffer for the new renderer; in the switch-over the movies' holders have no DirectDraw surface (R7) | (done) |
| 8. Switch-over | first step done: `present` (below). Left: the blockers below | see below |

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

### Switch-over: what `present` does and what blocks a full switch

`present` makes the new renderer the one the player sees: it draws every frame from the handed-over
data, shows it in the child window over the whole client area, and the old path no longer draws
what is handed over and never flips or copies its back buffer. The old Direct3D 7 device (dgVoodoo
in our runs) is still created and still used, because:

1. **Textures are DirectDraw surfaces.** Done for the .mmp textures: 00690430 hands the file data
   over at load and the new renderer decodes the levels the game loads (`render::TextureFromMmp`:
   DXT1 / DXT3 blocks, 8888, 4444, 5551, 5650 by the header's channel masks; textures.res holds
   704 DXT1, 70 8888, 39 4444, 39 5551, 38 5650, 21 DXT3). A texture is read from its surface only
   when something writes to it later (its uniqueness value changes) or it does not come from an
   .mmp: loose .bmp overrides (00690260), the item pictures ("PNT3", 973 in textures.res, decoded
   by the game into textures it writes itself), the shadow textures (taken from their render
   surface already). Since R8 (sections 15, 16) no texture is a DirectDraw texture in the switch-over:
   the .mmp textures are only the new renderer's decoded copy (keyed by the holder), the CPU-written,
   shadow and .bmp textures only its memory.
2. **The interface is composed on DirectDraw surfaces** with GDI (GetDC / ReleaseDC, text, fills)
   and pixel code (locks). First step done behind `EI_RENDER=...,cpuwindows` (off by default; on with
   `present` since render_present_cpuwindows passed on both installs, `nocpuwindows` opts out): the surface holder's constructor 004EF7B0 (used for the windows, the software
   cursor and the movies' surfaces; system memory, the screen's format) is rewritten and, with the
   option, puts the surface's pixels in a DIB section the game code owns (DirectDraw client
   memory: DDSD_LPSURFACE / DDSD_PITCH; refused -> a plain surface, logged "cpuwindows: ..."); the
   destructor body 004EFD20 frees it. The window surface's DC helpers 005FDC00 / 005FDC60 are
   rewritten: with client memory GDI draws through a memory DC on the DIB section, not
   DirectDraw's GetDC (an empty Lock / Unlock afterwards tells DirectDraw the memory changed, for
   the old path's Blt). Lock users (the console, the description panels, Greyscale ...) work on
   the same memory unchanged. The new renderer reads such a surface's memory directly
   (`render::SurfaceMemoryOf`, since R7 the renderer's own memory; GdiFlush first), not through a Lock. Checked offline: Windows'
   DirectDraw takes a DIB section as a system-memory surface's memory, and GDI through a memory DC
   on the DIB and through the surface's DC both land in it (dgVoodoo not checked: the fallback
   covers a refusal; if it caches client memory and misses GDI writes, the old path's windows
   would go stale, which the empty Lock / Unlock is there to prevent; render_cpuwindows passes on
   both installs with no refusal). Change detection for these surfaces no longer asks DirectDraw:
   the new renderer keeps a copy of the memory each window texture was made from and converts the
   window again only when the memory differs (a memcmp at each blit; it sees every writer: GDI,
   the lock users, the old path's Blt, with no flag to set in each of them).
   In the switch-over (`present` with `cpuwindows`) the window code works on that memory without
   DirectDraw (`DirectPixels`, dx_surface.h: a Lock-like description of the memory after GdiFlush):
   the escape menu's screen copy 005FD3D0 no longer Blts from the old render target (it does not
   hold the frame being drawn) and the new renderer writes its frame straight into the memory
   (`render::CopyFrameTo`); the save picture's stretch 005FD440 is a CPU copy (pixel centres,
   (2 x + 1) * source / (2 * size): Windows' DirectDraw gives the same at whole-number ratios,
   checked offline; at 1280x720 -> 256x192 rows may pick a neighbour, so the verify build does not
   compare it in that mode); Greyscale 005FE440, SwapContents 005FD7A0 and the pixel read 005FD240
   (rewritten: a window's colour at a point) skip Lock / Unlock and GetSurfaceDesc. The window
   constructors 005FCC00 / 005FCDD0 are rewritten (`ui_surface_create.cpp`): with game-owned
   memory the colour-fill Blt that clears a new window is left out (a new DIB section is zero)
   in either mode. The picture window's constructor 005FCF80 and the destructor body 005FD120 are
   rewritten there too and take the surface's description (for the surface-bytes total) from the
   memory in the switch-over. With that the windows' DirectDraw use in the switch-over is down to
   creating and releasing the surface itself (004EF7B0 / 004EFD20, and 004F0060 for the picture
   holders, which get game-owned memory too since R2b, below). The save / load screen's picture
   textures (locked by 00655550 / 00655D40 / 00656E10) get game-owned memory since R3 (section 11). Scenario: render_present_cpuwindows (render_present plus the escape menu
   over its greyed screen copy). Since R7 (section 14) the holders of 004EF7B0 and the
   system-memory pictures of 004F0060 have no DirectDraw surface at all in the switch-over (the
   memory is the new renderer's, `render::SurfaceMemory`). Since R8 (section 15) the texture
   manager's CPU-written textures (00691D80: the save / load pictures, the global map's weather
   texture, the item pictures) have no DirectDraw texture there either. Left: with the verify build's
   side-by-side text checks under cpuwindows the GDI batch must be flushed before they read
   pixels (not done: expect false mismatches there with the option on).
3. **Pass state** (decided: fed by the game code). `src/render/render_state.h`: every rewritten
   place that sets a render state, a texture stage state or applies a state block on the old device
   also calls `render::state::Render / Stage / Block` with the same values (17 files so far:
   device_draw, device_states, renderer_init (and a reset at device creation), renderer_lines,
   renderer_rect, renderer_sprites, renderer_strip, terrain_draw, world_client_draw, ui_alpha_draws,
   ui_camp, ui_container_draws, ui_create_screen, ui_hero_preview, ui_hud, ui_item_view,
   ui_objectives). The device records only four state blocks, at its creation
   (004E6392..004E67BD, handles in 007AEDDC / D8 / D4 / D0), each a stage-0 setup; a material's
   technique is a list of those handles plus its blend factors and texture records:
   | handle | colour | alpha |
   |---|---|---|
   | 007AEDDC | texture x current (MODULATE) | texture (SELECTARG1) |
   | 007AEDD8 | texture x diffuse | texture x diffuse |
   | 007AEDD4 | texture x diffuse | texture (SELECTARG1) |
   | 007AEDD0 | texture x diffuse | diffuse (SELECTARG1) |
   A pass takes its states from this mirror; a state nothing has fed yet is read from the old device
   and logged once ("render: pass state: render state N not fed by the game code yet"), with a count
   every 20000 passes of the passes that still needed the old device. The verify build also reads
   every fed state back and logs disagreements ("fed X but the old device has Y": a writer in code
   not converted, as the renderer's blend setter 00501C60 that particles use was); the device's
   value is drawn then. The first runs' list (render_*, frames_basic, ui_camp, both installs) is
   handled: the fog colour (renderer slot 0, 00501AB0), the material setter (004EB170: stage 1 off
   without a material) and three window draws that set alpha states (00600F70, 0062EFC0, 0065A6D0)
   are rewritten and feed the mirror; the states the game never sets (ZFUNC, TEXTUREFACTOR,
   ADDRESSU / V, stage 1 off) start with the device's initial values (checked by the verify build
   like fed ones). One documented exception, not logged: stage 0's mip filter, which the unit
   panel's draw 006462A0 turns off around the unit's picture and back (Rebirth replaces that whole
   function with its wide-layout version, so it cannot feed the mirror until it is rewritten with
   that patch); the device's value is drawn in the verify build, the mirror's (linear mips) in the
   release build. Known writer not reached by the scenarios: the tutorial screen's create
   00658E10 (alpha reference 1).
   The "state presets" of the first list, rewritten (2026-10-05, ticket R4):
   - 00501C60 (renderer slot 10, `device_states.cpp`) sets SRCBLEND then DESTBLEND (a failure
     reported through 004800E0 "Can't set source / destination alpha blend mode."). Not a general
     preset: the caller known is the particle draw 00498940 (5, 2 before additive sources, 5, 6
     after), so it is what left the particles' blend factors out of the mirror. Verify:
     differential on the real device (original, factors read back, ours, read back, compared).
   - 005045D0 (renderer slot 20) and 0049B4B0 (`renderer_lines.cpp`) are line-strip draws, not
     particle setups: 005045D0 a strip through integer screen points (no caller found);
     0049B4B0 the strip of the renderer's line lists (0049B780 from slot 15 00501CE0: the figure
     parts' debug bounds boxes 005A7290, the world client's line lists 00569D60), flat shaded,
     into a growing vertex buffer at 007A9CE0. Both turn stage 0's colour / alpha operations off
     and back to MODULATE (0049B4B0 also SHADEMODE flat / Gouraud), now fed; their strips go to
     `render::TlDraw` (line strip, FVF 0xC4) and the old draw is skipped with the old path's
     meshes off. Verify: ours only (the original would draw twice): result, the device object's
     counters, the states read back, 0049B4B0's vertex buffer. Neither is reached by the
     scenarios (debug drawing).
   - 0049B140 sets no state: it appends a point to a line list. The device calls listed under it
     belong to 0049B320 inside the same functions.csv range, a two-point line draw with no caller
     and no pointer to it (dead; left alone).
   - 0050434F is not a function: it lies inside an instruction of the rectangle 005041F0 (a
     false vtable entry, 00747E2C in config/vtables.csv, made it a functions.csv entry, as is
     00504341). 005041F0 is rewritten (`renderer_rect.cpp`) and already feeds every state it
     sets.
4. **CPU paths into surfaces**: the shadow rasteriser now draws into a CPU buffer (the surface's
   layout), which goes to the new renderer (`render::TexturePixels`) and is then copied into the
   render surface for the old path's texture load. Bink decodes each frame into a CPU buffer too
   (BinkCopyToBuffer's 32-bit R, G, B, X type 4, checked offline against the 5:6:5 decode of a game
   movie) for the new renderer (`render::MovieFrame / MovieBlit`); with `present` the video's
   DirectDraw surface is not filled at all (and with `cpuwindows`, since R7, not even made). The software cursor's images come from 8888 pictures
   loaded into surfaces by 004EFC60 (rewritten, `dx_surface.cpp`; 004F0060 makes and fills the
   surface): the new renderer decodes the picture from the file data (`render::PictureFromMmp`)
   and `render::Cursor` takes its colours from there, with the mask (005E4EB0: all ones where the
   image is black) from game memory; no Lock per frame. The same holder type serves windows made
   from a picture (005FCF80, dead code); with `cpuwindows` these holders keep their pixels in
   game-owned memory too (004F0060, R2b below). The screen copies: with point 2
   (in the switch-over they go into the windows' memory directly).
5. **Draws the old path still makes**: the weather's (004E4240 / 004E4C80 build and draw in one
   function; the new renderer takes the triangles after they return). Everything in the world view
   and the interface is handed over; with `present` the old back buffer is overwritten by the new
   frame.
8. **Presenting through the old path** costs a full-frame readback and conversion per frame (33 MB
   at 3840x2160): a staging copy kept between frames, 32-bit BGRX written directly. Fine for the
   switch-over; the final form presents from the new renderer's own swap chain once dgVoodoo's is
   gone (point 6).
6. **Old device calls still made**: 524 call sites in 143 functions; with no device every one needs
   a guard or a rewrite. 248 of the sites are in functions rewritten by now (most of them already
   skip the device call when the new renderer takes the draw); 83 functions with DirectDraw /
   Direct3D calls are not rewritten yet (the "state presets" are done, point 3; the dead line draw
   0049B320 is left): the device setup and texture management (004E5E50,
   004E68A0, 004EE*, 0068F860..00693C60; 0068F6C0 and 00691D80 are done, section 15), the holder helpers
   (004EFA60, 004EFEA0; 004F0060 is rewritten; the save / load screen's picture locks are done,
   section 11) and a few interface draws (0060C870, 00627C90, 00628F80, 00630670, 006462A0,
   00658E10; 00600EF0, 00602BF0, 00622420, 00632A90 and 0064D970 are done, section 12; 0060C870 is
   the camp's 6.5 KB Build, whose only device use is GetCaps and, with sort-independent
   antialiasing, SetRenderState(ANTIALIAS, 2) at its end: left for a rewrite of the whole Build). Creating the device (00500B70, `renderer_init.cpp`) can only be skipped
   after 1-4; until then dgVoodoo keeps creating its device and swap chain (only never presented).
7. **Window and display**: the child window rides on the game window; dgVoodoo's own swap chain
   stays under it. With dgVoodoo in exclusive full screen a child window may not show (windowed
   and dgVoodoo's borderless "full screen" are fine); a swap chain on the game window itself needs
   dgVoodoo's swap chain gone (point 6), or running without dgVoodoo. Resolution changes go through
   the game's device re-creation (the new target follows the render target's size).

Estimates: 1 and 4 about 1 week together, 2 (DIB sections) about 1 week, 3 about 1-2 weeks,
5 is step 5 (1.5-2 weeks), 6 and 7 about 1-2 weeks after the others.

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
| `map_object_shadow.cpp` (0051CE60), `device_shadow.cpp` (004EA4A0), `figure_rasterise.cpp` (005A0E80) | shadows (step 4, done): 004EA4A0 hands over (`render::BeginMesh`), 0051CE60 tells which texture changed |
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
- **render_scene, first run** (vanilla, 3840x2160; whole frame 11.58 % differing, mean 2.9):
  terrain 2.15 %, objects 39.3 %, interface 9.70 %. Explained:
  - **Half a pixel**: the new frame was the old one shifted by half a pixel (new(x, y) is the
    average of old(x, y) and old(x + 1, y + 1): mean difference on an interface icon 4.98 as
    captured, 1.48 against the shifted old frame). sokol truncates `sg_apply_viewportf` to whole
    pixels, so the -0.5 viewport had no effect: pixel coverage of integer edges was right (the
    offline test checked only that), the sampling position of colours and texture coordinates was
    not. Fixed in the vertex shaders. Comparing the new frame with the old one resampled half a
    pixel down-right predicts: terrain 2.15 % -> 0.93 %, interface 9.70 % -> 3.70 %, objects
    39.3 % -> 31.4 %, whole frame 11.58 % -> 7.71 % (the resampling blurs, so these are upper
    bounds).
  - **Objects**: the rest is the missing shadows (they fall on objects too: the "flatter" look) and
    the missing selection marker; both go through the flat draw 004EA4A0, now handed over.
  - **Interface**: the rest is in textured sprites' detail (icons, portrait, minimap), after the
    shift; no blending or animation differences found (the panels' translucent bodies match).
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

## 10. Picture holders on game memory (ticket R2b, 2026-10-05)

**004F0060** (`dx_surface.cpp`, `DXSurfaceLoadPicture`; thiscall on the holder, RET 8: picture,
flag) is rewritten. What it does: rejects a null picture or one not in 8888 (fatal error report,
its result returned); for a holder without a surface reads the primary's pixel format, creates an
off-screen surface of the picture's size (flags CAPS | WIDTH | HEIGHT: the screen's format by
default; caps 0x840 system memory, or 0x4040 video memory when the flag's low byte is set),
describes it (004EFEA0) and sets +0x14 / +0x18 / +0x1C to the width, height and the primary's bit
count; then reads the surface's pixel format, locks it (WAIT | WRITEONLY | NOSYSLOCK) and fills it:
32 bits a row copy, 16 bits through the device's converter (device +0x4C, 2DintMMX: slot 1 the
target 0 5:6:5 / 1 5:5:5 / 2 1:5:5:5 / -1 with the four masks from 004EDAA0's channel split, slot
2 one row: out, in, target, count), 24 bits a report and a clear; unlock; 1.

With `cpuwindows` a system-memory request gets a DIB section as the surface's memory, as 004EF7B0
does for windows (DDSD_LPSURFACE | DDSD_PITCH, and DDSD_PIXELFORMAT with the primary's format, the
one DirectDraw picks without it; refused -> the original's plain surface and the log line
"cpuwindows: client-memory surface WxH refused (hr) for a picture; a plain one instead"; the memory
is the new renderer's since R7, `render::NewSurfaceMemory`; the destructor body 004EFD20 frees it). Outside the
switch-over the fill still goes through Lock / Unlock, which on a client-memory surface returns that
memory (the verify build reads the DIB itself, not the lock, so it checks the pixels landed there).
In the switch-over (`present`) the fill writes straight into the memory: no GetPixelFormat on the
surface, no Lock / Unlock and no GetSurfaceDesc (004EFEA0) after the creation: size and format come
from the memory (`DirectPixels`); the device lock is still taken around the fill. A video-memory
request (flag low byte set) keeps the original's surface: client memory must be system memory. The
only live caller passes `(byte 00784756 == 0)`: 004EFC60 from the cursor's sprite loader 005E4EB0
(005DAEA0 -> 005E63F0, 49 images; 00784756 is 1 in the exe's data and never written by address,
so the flag is 0 and the images come out in system memory, on game memory with the option).

The picture windows: 005FCF80 has no caller in game.exe, EI_Plugin, addon.dll or mp3dec
(config/dead_code.csv), and 004EFF30 / 004EFFF0 (the other two callers of 004F0060, loading a
picture from a file into an existing holder) have no callers either. So in the scenarios
004F0060 is reached only through the cursor's images (25 calls per run in render_present,
render_present_cpuwindows, ui_camp and ui_screens). 005FCF80 / 005FD120 needed no code change:
their `Describe` already takes the description from `DirectPixels`, which now covers picture
holders too (comments updated).

The new renderer: a picture holder with game memory is a window surface like the others (read
from memory, converted when the memory changed); the cursor still takes its colours from the
picture's file data (`render::PictureFromMmp`).

Original details kept: a holder that already has a surface (dead callers only) must hold the
picture's width in both +0x14 and +0x18 (the original compares the width with the height field
too); a failed Lock whose report says stop returns with the device lock still held; the 24-bit
clear ignores the pitch; without a device the original reads the primary through a null pointer
(004EFC60 always passes the device object) and its "no DXDD" branch and the release of an earlier
surface on the creating path are unreachable (not reproduced).

Forwarders added: 004EDAA0 (`ChannelLayout::Split`, the pixel-format channel split; the same as
`PixelConverter::Init` in ui_surface_pixels.cpp), 00480080 as a printf-like `FatalError`. 004EFC60
still calls 004F0060 through its address, so the verify wrapper runs.

Verify (`verify_dx_surface.cpp`, `DXSurfaceLoadPictureVerify`, 6 stolen bytes: SUB ESP,0xD8): on
the creating path (a holder without a surface, a device, an 8888 picture: every live call) the
original runs first on a scratch copy of the holder, then ours on the real one; compared: the
result, the holder's +8 / +0x14 / +0x18 / +0x1C, every row of pixels (the original's surface through
a read-only Lock; ours from its DIB section when it has one, else a read-only Lock), and with
`cpuwindows` a system-memory request must have got game-owned memory; the scratch surface is
released. Other calls (error paths, an existing surface) ours only. Scenarios: render_present and
render_present_cpuwindows (the switch-over, direct fill), ui_camp and ui_screens (plain DirectDraw
path, the comparison of the fill itself); render_cpuwindows has the option without the switch-over
(the fill through Lock into the DIB).

## 11. The save / load screen's pictures on game memory (ticket R3, 2026-10-05)

The save / load window shows two 256x256 5:6:5 textures made by 005FF0B0 (texture manager
00691D80 -> 0068F6C0: a managed DirectDraw texture in a holder made by 004EFA60, CTexture +0x5C):
objects_[1], into which **00656E10** (ShowPicture) reads the selected save's shot.sav, and +0x184
(named `dialog_` in ui.h from an earlier reading; it is the new save's picture), which **00655D40**
(FillEntries) fills from the escape menu's 256x192 copy of the screen (interface +0x5C) and
**00655550** (SaveGame) writes to shot.sav. All three are rewritten (`ui_saveload.cpp`).
**00656464 is not a function**: it starts inside an instruction of 00655D40 (a false vtable entry,
0074718C) and is that function's tail (the failed-Lock exit and the scroll bar part); 00655D40
really ends at 0065663C. The autosave 006570E0 writes shot.sav from a window surface, not a texture.

With `cpuwindows`: the window's Build (00653DD0) and FillEntries give each texture's holder
game-owned memory (`CSaveLoadWindow::GivePictureMemory` -> `AttachTextureMemory`, dx_surface.cpp: a
DIB section of the holder's size in the texture's pixel format, CTexture +0x3C; freed by 004EFD20;
since R7 made by the new renderer, `render::NewSurfaceMemory`). The DirectDraw texture stays (a managed texture cannot take
client memory). Outside the switch-over the fills still Lock the texture (the old path draws it) and
the 0x18000-byte block is copied into the memory before the Unlock; in the switch-over the fills and
SaveGame's read use the memory only (`DirectPixels`: no Lock / Unlock). The new renderer draws these
textures through mesh passes (the sprite +0x280), so the texture path (`SurfaceView`) now reads a
holder with game-owned memory from that memory, again when it differs from a snapshot (a memcmp
once a frame; separate snapshots from the window path), with box-filtered mips like a surface read.
Without the option everything is the original's.

Original details kept: the 0x18000 bytes are one block from the start of the pixels (pitch taken as
512); the Locks use flags 0; a failed Lock whose report says stop leaves the device lock held (and
SaveGame the busy cursor, FillEntries the scroll bar untouched); the entries' temporaries leave +8 /
+0x0C unset (stack bytes) where info.sav does not give them; the new folder is "save<n>" with n one
more than the highest atoi(folder.Mid(4)) (006FD9B4 is Mid, not Right as library.csv says). The
SavesPath Rebirth patch (call sites in all three) is disabled in this install and not reproduced.

Forwarders added (file-local in ui_saveload.cpp): 00455A90 free space, 0057BC30 needed space,
00581610 save thunk, 005E5200 cursor shape, 005F75B0 notice, 00634650 message box, 005F7350,
00578B50 / 00578A90 player variable / zone, 0045E850 FindMap, 0065AFE0 scroll range, 006FE508
CTime, 006E80D8 atoi, 0070133F CFile::Write, 006FE925, the stream header / string helpers 004A5340 /
004A52D0 / 00461190 / 004E5840, the SaveEntry and vector helpers 00657C60 / 00656640 / 006538E0 /
00656720 / 00653A50 / 00657D50 / 006579C0 / 006578F0 / 00657DF0 / 00657F10, CFileFind 006E6653 /
006E661A / 006E6722 / 006E684B / 006E63E9 (MatchesMask through slot 14). Callers in rewritten code
(Build, OnMouseDown, OnDialogResult) go through the original addresses (`Call*`) so the wrappers run.

Verify (`verify_ui_saveload.cpp`): ShowPicture (6 stolen bytes) and FillEntries (7) run the original
first on the real window and ours over it (both are safe to run twice, as the game does); compared:
the sprite's shown flag and texture, and the picture block (the original's texture through a
read-only Lock, scribbled over before ours runs; ours from the memory when it has some, which it
must with `cpuwindows`, and from the texture outside the switch-over); for FillEntries every entry's
strings, broken flag, game time (4 ULPs) and time (not where the original leaves them unset), the
scroll bar's range / position / moved flag, the list position, the object count and the new save's
picture. SaveGame ours only: a closing save has info.sav (header 0x111, 3) and shot.sav equal to
the picture; otherwise the pending value is untouched. Scenario `ui_saveload` (render new,present:
autosave, Save window, a save, Load window with captures); ui_hud reaches FillEntries / ShowPicture
with the option off. Not checked in game yet (runs are central).

## 12. Interface draws with old-device calls, first group (ticket R5, 2026-10-05)

Rewritten in `src/game/ui_container_draws.cpp` (each keeps the original's device calls in the same
order; states go to `render::state`, depth clears to `render::ClearDepth`):

| Address | What | Device use | Hand-over |
|---|---|---|---|
| 00600EF0 | CUiContainer slot 20, the container windows' base draw (escape menu, save / load, options, battle screen, camp ...): with the interface's +0x30 set, state block 007AEDD8 (texture x diffuse), the window's sprites and models (005FEB90), every enabled child's slot 20 | ApplyStateBlock (result to the device object's +8) | `render::state::Block` |
| 00602BF0 | slot 20 of screen 3 (the base / briefing screen, vtable 007405AC): 00578910 on 007AFEA5, a mouse move (0, x, y from 007C7C50 / 54) to the input side's slot 4, the depth clear, 00600EF0 | Clear (depth, stencil when present) to 1 | `render::ClearDepth` |
| 00632A90 | slot 20 of screen 2 (vtable 00740688): the same without the mouse move | Clear to 1 | `render::ClearDepth` |
| 00622420 | CHeroEquipPanel slot 20 (vtable 007418E0): the two highlight frames shown / placed (the owner's slot 51 gives the hero's lists: +0x730 / 34 quick items, +0x740 / 44 the right part, +0x750 / 54 armour, +0x774 the selected quick slot), the quick items' models (005ECA10, cells 100 apart at y 50, depth 12), and with the body (the camp) the armour models in the seven slots (centres 00741EC4 / 00741EE0, 70 x 70 layout rectangles scaled by 005F7CA0, depth 6) between three depth clears, each after a flush of the two batches | Clear to 0, Clear of the 7 rectangles to 1, Clear to 1 | `render::ClearDepth` (the models already hand over through 005ECA10 and the mesh draws) |
| 0064D970 | slot 26 of vtable 007411BC: the alpha reference set to the window's +0x1EC; returns SetRenderState's HRESULT (not stored in +8) | SetRenderState(ALPHAREF) | `render::state::Render` |

In the switch-over (`present`) the old device's clears are left out (+8 set to 0): nothing the
old path still draws there reads its depth buffer. The older clear hand-overs (00636020, 005EC300,
00627390) still clear the old device too; they could take the same guard. The state calls stay in
both modes (the verify build reads the states back to check the mirror). The original's 64-bit FILD
of the unsigned quick slot * 100 + 50 is `x87::FromUnsigned`.

Verify (`verify_ui_container_draws.cpp`): ours only for the four draws (the original would draw
everything twice): 00600EF0 result 1 and the children array unchanged (every ui_* / render_*
scenario); 00622420 result 1, the highlight frames shown exactly when the original's conditions
hold, and with the body the last clear's result a success (ui_camp, frames_basic, render_present,
render_present_cpuwindows); 00602BF0 / 00632A90 result 1 (no scenario reaches screens 2 / 3: a
"brief" zone or the server's screen 2 is needed). 0064D970 differential on the real device (the
original through the trampoline, 6 stolen bytes, the alpha reference read back; then ours; results
and states compared); its caller (slot 26 of 007411BC) is not identified, so it may not be reached.
Not checked in game yet (runs are central).

## 13. Movies and the software cursor outside the frame (ticket R6, 2026-10-05)

Rewritten (old device calls kept, behind `render::Presenting()`):

| Address | What | With the new renderer |
|---|---|---|
| 005DB3F0 (`bink_video.cpp`, `PlayMovie`; fastcall name, frame time, 5 stack arguments, RET 0x14) | the blocking movie player: intro, titles, the script's PlayMovie (callers 00458270, 0057A060, 004B89B8, 00602190, 00632940, 00634D20, 00634E60, the main menu). Its own message loop (PeekMessageA, formerly returning to 005DB788: Esc / Space end it or skip a frame, WM_PAINT dispatched), frames timed by BinkWait or the fixed frame time, decoded into the movie's surface and blitted into the letterboxed rectangle of the primary; the screen filled black first (005DB9E0) and the renderer's clear (00501A40) when asked | each decoded frame to `render::MovieFrame` (BinkCopyToBuffer's 32-bit type 4), then the frame `render::Fill` (black, with the fill flag) + `render::MovieBlit` and presented: with `present` through the old present 004EF610 (the new frame copied into the render target, then Flip / BltFast); otherwise `render::Present` ends the sidecar's frame and the old path draws as before. With `present` no Lock / Unlock of the movie's surface, no fill or clear, no Blt to the primary |
| 005DB9E0 (`HolderFill`, thiscall, RET 8) | a colour fill (DDBLT_COLORFILL) of a holder's surface under the device lock | only called outside `present` |
| 005DA400 (`ShowLoadingMovie`, fastcall progress, plain RET) | the loading screen's movie (00791EE0's file: progres*.bik, chosen per zone by 00595080), opened once into 007C5550 / holder 007C551C / type 007C5554 / screen rectangle 007C5520, decoded up to the progress frame (BinkGoto) and stretched over the primary | the last decoded frame (`render::MovieFrame`, key 007C551C) blitted over the screen and presented as above; with `present` the surface is not filled and nothing goes to the primary |
| 005E5640 (`HolderBlt`, `ui_cursor_frame.cpp`, thiscall, RET 0x14) | Blt between two holders under the device lock | (unchanged; its callers skip it with `present`) |
| 005E5690 (`CursorThread`, thiscall, plain RET; started through 005E52A0) | the cursor thread: every 41 ms with the software cursor bit and an image, when the cursor moved, restores the old background, saves the new one, composes (005E5C40) and blits to the primary (one union blit when the rectangles overlap), WaitForVerticalBlank before showing | with `present` the thread draws nothing (neither its first background save nor the per-tick redraws): the new renderer draws the cursor in each frame (005E54A0 -> `render::Cursor`); between frames (loading, movies) the cursor does not move |
| 005E6630 (`CursorSaveBackground`, thiscall, RET 4) | the drawn rectangle (+0x20) inside +0x50 copied from the primary (1) or the render target (0) into the background surface +0x60 (the movie players after their blits) | nothing with `present` (nobody draws from the background then) |
| 005E54A0 (`CursorDrawFrame`, changed) | | with `present` its old blits (background from the render target, the composition 005E5C40, back into the render target, mode 2's save from the primary) are left out: the new frame overwrites the render target at the present anyway; `render::Cursor` as before |

Not rewritten: **005DA880** is dead (a thread procedure started only by 005DAD50, which has no caller;
config/dead_code.csv); 005DA5AB / 005DB603 are not functions but the catch blocks of 005DA400 /
005DB3F0 (the fallback to 004EF7B0 when the video-memory surface of 004EFA60 throws). That branch
runs only with 00784756 0, which the exe never sets (it is 1, R2b), so the rewrites keep the
video-memory branch without the catch.

New in the renderer: `render::Fill(rect, colour)` (a stretched one-texel blit of the frame).
Forwarders moved into the shared headers (rule 5): 004EF7B0 `DXSurfaceConstructOriginal`, 004EFA60
`DXSurfaceFromDescOriginal` (texture_mmp.cpp uses it), 00501A40 `RendererClearOriginal`, 004EF610
`DevicePresentOriginal` (app_frame.cpp uses it) in originals_render.h; 005E5270 `CursorShowOriginal`
(console_addon.cpp, ui_layouts.cpp), 005E65F0 / 005E6610 `CursorLockOriginal` / `CursorUnlockOriginal`
(app_frame.cpp), 006B71C0 `LoadingStepOriginal` (ui_layouts.cpp, world_game.cpp), 005E6630
`CursorSaveBackgroundOriginal` in originals_ui.h. 00455F10 (sets the byte 00784751 from CL) and
004E6BA0 (a holder's release) are written inline; 00694120 is an empty function and not called.

Original details kept: a failed Lock whose error report says go on skips the copy and leaves the
device lock held (both players); a failed Unlock whose report says stop ends the movie (005DB3F0) or
returns before the blit and the cursor calls (005DA400); 005DA400 hides the cursor (005E5270 0) on
every call and never shows it again (the caller does); the fifth stack argument of 005DB3F0 is
never read; mode 2's height is trunc(screen width * (height / width)); the progress movie's path is
the CString 007AF460 + "Movies\" + the name.

The harness: its input steps wait for PeekMessageA calls from the main loop or the movie player,
recognised by return address; the rewritten player's call no longer returns to 005DB788, so input
steps (Esc to end a movie) are not posted inside it. No scenario sends input to a movie (the harness
empties movie.ini's lists); harness.cpp is unchanged.

Verify: 005DB3F0 ours only (result 0 / 1, 1 with movies off, 00784751 set again); 005DB9E0
differential (6 stolen bytes, Blt results); 005DA400 ours only (the movie open has its holder, the
frame reached or the movie at its end); 005E5640 differential (8 stolen bytes, results); 005E5690
ours only (returns only when asked to stop); 005E6630 ours only (+0x20 / +0x50 unchanged).
Scenarios: render_movie (`render new`: PlayMovie("crdtfin.bik") after a new game, the old path and
the new frames; 005DB9E0 there) and render_movie_present (`new,present`); 005DA400 and 005E6630
in every scenario with a new game; 005E5690 runs in every scenario (it draws only with the
software cursor display option, which no scenario sets; 005E5640 likewise). Not checked in game
yet (runs are central).

Left of R6-R8: R7 holders with no DirectDraw surface at all (done for the windows, the cursor and
the movies: section 14); R8 device setup and texture management (first part done: the CPU-written
textures, section 15), and with it the presentation itself (the old Flip / BltFast that `present`
still goes through, and the cursor between frames once there is no primary).

## 14. Holders with no DirectDraw surface (ticket R7, 2026-10-05)

In the switch-over with `cpuwindows` (`present`, which turns it on unless `nocpuwindows`) the
holders 004EF7B0 makes (the interface windows: 005FCC00 / 005FCDD0 / 005FD7A0; the software
cursor's background and composition surfaces: 005E60B0; the movies' decode surfaces: 005DA400,
005DB3F0, 005DBA50) and the system-memory picture holders of 004F0060 (the cursor's images through
004EFC60 from 005E4EB0; the dead picture windows) have no DirectDraw surface at all:
`CDXSurface::surface` stays null and the pixels are only in memory the new renderer owns. Without
`present`, or with `nocpuwindows`, every holder gets its DirectDraw surface as before (the old path
is kept behind the flag).

**The memory moved into the new renderer**: `render::NewSurfaceMemory / SurfaceMemoryOf /
FreeSurfaceMemory / HolderGone` (render.h, render_textures.cpp): one zeroed top-down DIB section per
holder (GDI draws into it through a memory DC) with its pitch, size, bit count and channel masks
(`render::SurfaceMemory`). It replaces both dx_surface.cpp's own map of DIB sections and their
registration with the renderer (`ClientSurface` / `ClientSurfaceGone` are gone): one record per
holder, made by the holder code, used by the game code (`ClientBitmap`, `ClientPixels`,
`DirectPixels`) and read by the renderer (window blits, textures, the screen copies). Outside the
switch-over the same memory is still given to DirectDraw as the surface's client memory.

| Address / file | Change |
|---|---|
| 004EF7B0 `DXSurfaceConstruct`, 004F0060 `DXSurfaceLoadPicture` (dx_surface.cpp) | in the switch-over, once the memory is made: no CreateSurface (the device object's +8 set to 0, as after a successful creation), the holder's +0x14 / +0x18 / +0x1C from the request and the screen's pixel format (still GetPixelFormat of the primary: the windows keep the screen's format because the game computes their colour keys in it); no memory -> a DirectDraw surface as before. Logged once: "cpuwindows: surface holders in the switch-over get no DirectDraw surface, only the new renderer's memory". The file now uses CDXSurface / CTextureDevice members instead of offsets (CTextureDevice +0x4C `converter_` added, texture.h) |
| `DirectPixels` (dx_surface.h) | also true outside the switch-over for a holder without a surface, so the pixel code never asks DirectDraw for one (see the failure case below) |
| 005E4EB0 `CursorImage::Construct` (ui_cursor.cpp, new) | a cursor image: the picture from the texture manager (slots 19 / 20 as the declared virtuals GetFile / CloseFile), the holder (004EFC60 through its address), frames = the picture's width / height, a frame's width = width / frames, the hot spot * the cursor's size (007C7C90 / 94) / 32 (signed, towards zero), and the mask (16 bits: all ones where the pixel is 0; 32 bits: where its low 24 bits are 0) built under the device lock from GetSurfaceDesc + Lock / Unlock, or with `DirectPixels` from the memory. CursorImage's +0x0C..+0x18 named (frames, frame, hot spot) |
| 005DBA50 `CBinkVideo::Open` (bink_video.cpp, new) | the frame player's open (the titles' Titles.bik): the previous Bink closed (its handle left in +0x18 when movies are off), movies off -> 0, the loading step, Miles, BinkOpen (failure: "Can't open (or bad format) video file %s.", 0), the cursor hidden, the holder (NewMovieHolder), BinkDDSurfaceType only for a holder with a surface (else 4, the CPU copy's type), the rectangle (mode 1 centred at the movie's size; else the screen's width, height trunc(h / w * screen width), centred vertically) |
| 005DBD80, 005DB3F0, 005DA400 (bink_video.cpp) | `OldPathDraws(holder)`: Lock / BinkCopyToBuffer / Unlock and the Blt only outside the switch-over and with a surface; `SurfaceType` instead of BinkDDSurfaceType on a null surface |
| 005E54A0, 005E5690, 005E6630 (ui_cursor_frame.cpp) | `OldPathDraws(cursor)`: the old blits only outside the switch-over and with the cursor's surfaces; the composition 005E5C40 only with the image's surface |
| 005FDA80 (ui_surface_blit.cpp), 005FDC60 (ui_surface_dc.cpp), 005FD3D0 / 005FD440 (ui_surface_pixels.cpp) | the old Blt, the empty Lock / Unlock after GDI and the screen / stretch Blts only with DirectDraw surfaces |
| the new renderer | `WindowSurfaceView`, `SurfaceView`, `CopyFrameTo` and `Cursor` work for a holder with memory and no surface (the cursor's colours come from the picture's file data as before; a surface is only needed for the fallback read) |

The rest of the window code (005FCC00 / 005FCDD0 / 005FCF80 / 005FD120 / 005FD7A0 / 005FD240 /
005FE440, GDI through 005FDC00 / 005FDC60) needed no change: it already works on the memory in the
switch-over (sections 5 and 10). A scan of game.exe for surface calls through a holder's +0x10 and
for every function with both Lock and Unlock (or GetDC and ReleaseDC) found no other user of these
holders' surfaces: 005E4EB0 and 005DBA50 were the only ones not rewritten. The others are textures
(0062F050's weather texture, 005EC020, 005FFFB0, the save / load pictures, the texture manager
0068F6C0..00693C60), the device's own surfaces (004E6C30, 004EEE30, 004EF610, 006D6D40), the shadow
render surface (004EA4A0) and dead code (005DA880, 004EF510; 0052EF90 is a false hit, a unit's
network state).

Left (then): the texture holders (004EFA60 through the texture manager): the save / load pictures
kept a DirectDraw texture beside their memory (R3), the other CPU-written textures (0062F050,
005EC020, 005FFFB0) had only the texture. Done for those in R8's first part (section 15). The
screen's pixel format is still read from the primary at every holder creation (the primary goes
with R8 too).

If the new renderer fails after holders were made without a surface (a sokol panic, or no Direct3D
11 device at the first frame: `Presenting()` turns false and the old path takes over), the old path
cannot draw those holders: its blits and copies skip them (the guards above), so the interface,
cursor and movies made before then stay missing, but nothing crashes, and the game code keeps
working on the memory (`DirectPixels`).

Original details kept: 005E4EB0's GetSurfaceDesc and Lock failures are reported and ignored (it
reads on); its mask buffer is bits * height * width / 8 bytes; the frame and hot spot fields as
above. 005DBA50 leaves a closed Bink handle in +0x18 when movies are off; the two zero offsets of
its centring are left out. Not reproduced: both functions' unwinding when 004EFC60 / 004EF7B0 throw
(the throw ends the game), 005DBA50's catch that falls back to 004EF7B0 when the video-memory holder
of 004EFA60 throws (only with 00784756 0, never set; as R6).

Forwarders: 004EFC60 moved into originals_render.h (`DXSurfaceFromPictureOriginal`, used by
ui_surface_create.cpp and ui_cursor.cpp). No new ones.

Verify:
- 004EF7B0 / 004EFC60 (ours only): as before, and the holder's pixels: a surface, or in the
  switch-over the memory of the holder's size and bit count (never both there).
- 004F0060 (against the original on a scratch holder, unchanged): additionally no surface beside the
  memory in the switch-over; no comparison for a holder that already has memory.
- 004EFD20 (ours only): additionally no memory left.
- 005FCC00 / 005FCDD0 / 005FD120 / 005FCF80 (ours only): the description from the memory for a
  holder without a surface.
- 005E4EB0 (new, ours only: the original would make a second holder and cannot read one without a
  surface): the result, a holder with pixels, frames / a frame's width / the height from the
  picture's description, frame 0, the hot spot, and the whole mask against the pixels (the memory,
  or a read-only Lock). Every scenario (the cursor's shapes are loaded at startup, 005DAEA0 ->
  005E63F0).
- 005DBA50 (new, ours only: it opens a file): the result 0 / 1 (0 with movies off); when it plays a
  holder of the movie's size with pixels (only memory in the switch-over) and the rectangle (mode 1
  the movie's size, else the screen's width). ui_main_menu (region 4, the old path) and
  render_titles_present.
- 005FE440 Greyscale / 005FD240 PixelAt (the originals lock the surface): for a holder without one
  ours only; Greyscale then checks every pixel grey in the 5:6:5, 5:5:5 and 8:8:8 layouts, PixelAt
  that the result is a 0x00RRGGBB colour. 005FD440 Rgb565Copy ours only then (as already in the
  switch-over); 005FD7A0 SwapContents reads the window's bytes from the memory.

Scenarios: render_present and render_present_cpuwindows (the windows, the cursor's holders and
images, the escape menu's screen copy / grey / stretch, the camp), render_movie_present (005DB3F0
and 005DA400 with movie holders without surfaces), ui_saveload (`new,present`: the save / load
windows; their textures are unchanged), render_titles_present (new: the main menu's titles, i.e.
005DBA50 / 005DBD80 in the switch-over); render_cpuwindows (the option outside the switch-over: the
memory is DirectDraw's client memory, as before) and render_movie (the old path). Not checked in
game yet (runs are central).

## 15. CPU-written textures without a DirectDraw texture (ticket R8, first part, 2026-10-05)

Why this part first, not presenting from the new renderer's own swap chain: that needs dgVoodoo's
swap chain gone (a second flip-model swap chain on the game window fails, and a child window's made
the game window inactive in full screen: section 1), i.e. no old device at all, and the device can
only go once textures no longer live in DirectDraw (section 5, points 1 and 6). Taking textures off
DirectDraw is the step that unblocks the rest; the swap chain comes after.

In the switch-over (`present` with `cpuwindows`) the textures the texture manager makes for the game
to fill on the CPU (00691D80) have no DirectDraw texture: their holder (`CDXSurface::surface` null)
has only the new renderer's memory (`render::SurfaceMemory`, a DIB section in the texture's pixel
format; CreateDIBSection takes the 4:4:4:4 and 1:5:5:5 masks too, checked offline), which the
writers fill directly and the new renderer reads (`SurfaceView`, as for R3's save pictures). Outside
the switch-over, or with `nocpuwindows`, they are the original's managed textures.

Rewritten (`texture_blank.cpp`):

| Address | What |
|---|---|
| 0068F6C0 `CTexture::CreateBlank` (thiscall, RET 8, EAX 1) | frees the texture's aligned block +0x60; a texture surface of header->width square (the height is not read) with header->mipCount levels in the texture's format_ (+0x3C): caps TEXTURE (MIPMAP / COMPLEX with mips), with flag 0x400 in system memory, else TEXTUREMANAGE; caps2 OPAQUE for flag 0x40, HINTSTATIC for 0x80, HINTDYNAMIC for 0x100 / 0x200 / 0x400; the holder from 004EFA60 (counted, the old one released); +0x28 = 0 (see below). Callers: 00691D80, the shadow textures 00692010 (flag 0x200) and 00692140 (0x400, the render surface) |
| 00691D80 `CTextureManager::NewTexture` (thiscall: width, height, kind; RET 0xC) | CreateObject(kind, -1) (slot 23; flag 0x40), width_ / height_, for kind 0x1E / 0x20 / 0x21 the device's format 5650 / 5551 / 4444 (FindFormat; missing: FatalError "Can't find texture format %d." and its result returned as the texture) and an .mmp-like header with those channels, then CreateBlank's body; in the switch-over the holder comes from `NewMemoryTextureHolder` (dx_surface.cpp: the holder 004EFA60 would make, vtable, device, size, bits, holder count, but no CreateSurface; no memory -> the DirectDraw texture as before, logged). A failed CreateBlank (never: it returns 1) releases the texture (slot 4), null |

The users of these textures: 005FF0B0 (`CUiWindow::CreateTexture`, from the battle screen 00635250,
the options 006503C0, the save / load window 00653DD0 / 00655D40, the global map 0062D350, the
tutorial 00658E10, 00605DE0, 00608F90, 00640060) and 005EC020 (the item screens' figure textures).
Their writers, all rewritten: 005FFFB0 CopyImage and 005EC020 (`ui_item_view.cpp`, `LockTexture` /
`UnlockTexture`: `DirectPixels` instead of Lock / Unlock), the global map's weather 0062F050
(`ui_global_map.cpp`, the weather drawer 00499F10 draws into the memory), the save / load window's
00655550 / 00655D40 / 00656E10 (`DirectPixels` since R3; `AttachTextureMemory` finds the memory
there). Nothing else of the texture manager touches their surface: the binders (004EB300,
004EBAF0, 00501AF0, 004EB370's inline one) pass the holder's null surface to SetTexture (D3D7 takes
it: no texture on the old device, which draws nothing in the switch-over; the new renderer reads the
bound holder, device +0x12C); SetPriority is only called on loaded textures (flag 8: the release
00691B00 keeps them in its cache, 00691BF0 / 00693C60 take them back from it), which these are
not; the reload of every texture (00692BF0 -> 006922A0) finds no file for their empty name and does
nothing, as before; the release (slot 3 00691A70, destructor 00691950) releases the holder, and
004EFD20 frees the memory.

Not memory-only yet: the shadow textures (0068F6C0 from 00692010 / 00692140: 0051CE60 locks the
render surface through 00690FC0 / 00691030 and copies it into the texture with the device's Load);
the .mmp textures (00690430) and the loose .bmp ones (00690260 / 0068FBF0).

Original details kept: 0068F6C0 makes the surface square from the header's width; its byte size
(+0x28) is meant to be bits * width * height / 8 but takes the bits from the first field of
004EDAA0's output, which that function zeroes and never writes, so it is always 0; 00691D80 writes
the header's channel fields, which nothing reads, and leaves a texture whose format is missing
registered with the manager; another kind keeps a zero format. Not reproduced: the unwinding when
004EFA60 throws (the throw ends the game).

Forwarders: 004EDAA0 is declared once in texture.h (`ChannelLayout::Split`, defined in texture.cpp);
dx_surface.cpp's own copy is gone (ui_surface_pixels.cpp still forwards it as `PixelConverter::Init`,
on the duplicates list already). New accessor `game::CantFindTextureFormatText` (00795D74,
game_strings.h); MmpHeader's channel fields and CTexture +0x60 / +0x64 named (texture.h).

Verify (`verify_texture_blank.cpp`):
- 0068F6C0 ours only (it makes a DirectDraw texture): result 1, a holder with a DirectDraw surface
  of header->width square in the format's bit count, +0x28 0, no block. Reached through the shadow
  textures (render_scene, render_present, frames_basic: object shadows).
- 00691D80 ours only (it registers a texture): for the three kinds the size, format_ equal to the
  device's format for the kind, a holder of `width` square in it, the holder count one higher,
  +0x28 0; in the switch-over with `cpuwindows` only memory of that size and those channel masks
  (no DirectDraw texture), otherwise a DirectDraw texture and no memory.
- The writers' wrappers in the switch-over (`tests/verify/memory_texture.h`): an original that locks
  such a texture runs on a temporary DirectDraw texture lent to the holder (system memory, the
  memory's size and format, filled from the memory), taken back afterwards. 005FFFB0 and 00656E10
  lend the texture they are given and read the original's pixels from the lent texture (the memory
  keeps the pixels from before, ours then writes it); 005EC020 and 00655D40 make their texture
  themselves, so while their original runs 00691D80's wrapper lends every new texture
  (`LendNewTextures`) and moves its pixels into the memory afterwards; the comparisons read memory
  where a holder has no surface. If DirectDraw refuses the temporary texture ("verify: no
  DirectDraw texture ... ours only" in ei_hook.log) 005FFFB0 / 00656E10 run ours only; 005EC020 /
  00655D40 would then crash in their original (not expected: the same kind of system-memory texture
  00692140 makes). 0062F050 (ours only) and 00655550 (ours only, reads the memory) are unchanged.

Scenarios: ui_saveload (`new,present`: the save / load pictures through FillEntries, SaveGame and
ShowPicture with memory-only textures, both against the original), render_present and
render_present_cpuwindows (the battle screen's textures through CopyImage, the camp's item and face
pictures through 005EC020, in render_present the global map's weather), render_movie_present and
render_titles_present (the switch-over with a new game / the main menu); outside the switch-over
ui_camp, ui_screens and ui_hud (DirectDraw textures: the writers against the originals as before) and
render_scene (0068F6C0 for the shadows). ei_hook.log says once "cpuwindows: the texture manager's
CPU-written textures in the switch-over get no DirectDraw texture". Not checked in game yet (runs
are central).

If the new renderer fails after such textures were made (`Presenting()` turns false), the old path
binds no texture for them (they stay missing) but nothing crashes, and the writers keep writing the
memory (`DirectPixels` is true for a holder without a surface).

Left of R8: (1) the shadow textures memory-only (00692010 / 00692140, the locks 00690FC0 / 00691030,
0051CE60's copy, 004EA4A0) and (2) the .mmp textures without DirectDraw (00690430: the new renderer
already decodes them from the file data but keys them by the surface, `StoreDecoded`; the texture
manager's SetPriority on loaded textures 00691B00 / 00691BF0 / 00693C60 needs a guard; the .bmp
overrides 00690260 / 0068FBF0 into memory; 0068FA10's fills skipped) are done (section 16); (3) the device setup
(004E5E50, 004E68A0, 004E6C30, 004EE*, 00500B70: no primary, render target, depth buffer or Direct3D
device; the screen's pixel format for the window holders from elsewhere); (4) presenting from the
new renderer's own swap chain on the game window instead of the old Flip / BltFast, and the cursor
between frames.

## 16. Every texture without DirectDraw in the switch-over (ticket R8, second part, 2026-10-05)

With this part, in the switch-over (`present` with `cpuwindows`) no texture of the game is a
DirectDraw texture any more: besides the CPU-written ones (section 15), the shadow textures and
their render surface have only the new renderer's memory, the .mmp textures only the new renderer's
decoded copy, the loose .bmp textures only memory; the texture cache leaves out the surface
priority for them. The holders' screen format no longer comes from the primary surface. Outside
the switch-over, or with `nocpuwindows`, everything is as the original (the old path is kept behind
the flag).

Rewritten:

| Address / file | What | In the switch-over |
|---|---|---|
| 00692010 `CTextureManager::NewShadowTexture` (`texture_shadow.cpp`, thiscall, plain RET) | an object's 64x64 shadow texture: CreateObject(0x25, -1), size, the device's 4:4:4:4 format (missing: FatalError "Can't find texture format %d." with 0x21, its result returned as the texture), the kind flags (& ~0x1C0) \| 0x200, an .mmp-like 4:4:4:4 header, CreateBlank's body (the texture released and null when it fails, never) | MakeBlank with `memoryOnly`: only the new renderer's memory (a 4:4:4:4 DIB section) |
| 00692140 `CTextureManager::ShadowRenderSurface` (plain RET) | the shared render surface the shadows are rasterised into, kept counted in the manager's +0xE0 (made once as 00692010's, kind flags (& ~0x3C0) \| 0x400: system memory) | the same, memory only |
| 00690FC0 `CTexture::LockPixels` (RET 4) | 0 without a holder; the device lock entered (held until the unlock), the description zeroed, Lock (WAIT \| WRITEONLY \| NOSYSLOCK), the result in the holder; a failure: 004800E0's result (the lock held) | a holder without a DirectDraw surface is described from its memory (`ClientPixels`), result 0 in the holder |
| 00691030 `CTexture::UnlockPixels` (plain RET) | the device lock left, Unlock, a failure reported | no Unlock for a holder without a surface |
| 0051CE60's copy (`map_object_shadow.cpp` -> `CTexture::LoadPixelsFrom`, texture_shadow.cpp) | the device's Load of the render surface into the shadow texture under the device lock, the result in the device's +8 | both in memory: the rows copied on the CPU (result 0); a mixed pair (only when the memory for one could not be made) is not copied, logged once |
| 00690430 `CTexture::CreateFromMmp` (`texture_mmp.cpp`, changed) | (section 5) | no staging surface, fill (0068FA10), Load or priority: a holder without surface and memory (`NewDecodedTextureHolder`, dx_surface.cpp: vtable, device, the level's size, the format's bits, the holder count) for which the new renderer decodes the levels from the file (`render::TextureFromMmp`, now keyed by the holder); +0x28 is what the fill would have returned (`MmpHeader::FilledBytes`); a format the new renderer does not decode gets the DirectDraw texture (logged once) |
| 0068FBF0 `CTexture::LoadBmpFiles` (`texture_bmp.cpp`, thiscall: device, path, alpha path; RET 0xC) | a loose textures\\<name>.bmp (from 00690260; its alpha a_<name>.bmp beside it when 8-bit): the game's DIB reader (006D4AA0 / 006D5910 / 006D4D10 / 006D4CD0, forwarded), a staging surface and a managed texture of the .bmp's size, the 24-bit colours (and alpha, else 255) converted with 004EDC00's conversion (`ChannelLayout::ToNative`, plain C++) into the staging surface, Load; flag 8 | the texture is `NewMemoryTextureHolder`'s memory and the pixels go there (no staging, Lock, Load) |
| 00691B00 `CTextureManager::ReleaseObject` (`texture_cache.cpp`, vtable slot 24, RET 4, through a shim) | a texture without references: registered and flagged 0x38 -> unregistered, appended to the cache list (+0xBC, a CPtrList), +0xD8 += its byte size, SetPriority(0); else destroyed (slot 3), no owner, a spare (RemoveObject) | SetPriority only for a holder with a DirectDraw surface |
| 00691BF0 `CTextureManager::TakeFromCache` (RET 8: type, name) | the cached texture of that type and name: out of the list, +0xD8 -= its byte size, registered (RegisterInstant), SetPriority(100); null | as above |
| 00693C60 `CTextureManager::FindTexture` (RET 4: name) | a registered texture of that name (the object list), else a cached one taken back (no +0xD8 change), null | as above |
| 004EF7B0 / 004F0060's screen format (`dx_surface.cpp`, `ReadScreenFormat`) | GetPixelFormat of the primary surface | the display mode's format (IDirectDraw7::GetDisplayMode: the primary is in the display mode's format, windowed or full screen); the primary only if that fails |

The new renderer (`render_textures.cpp`): decoded textures (`StoreDecoded`: .mmp levels, the CPU
pixels of `render::TexturePixels`) are keyed by the holder and no longer need a DirectDraw surface.
A holder with neither a surface nor memory shows its decoded texture, which is not dropped after
600 unused frames (it is the only copy) but when the holder goes: `render::HolderGone` (004EFD20)
now also retires the holder's texture and window entries, so a later holder at the same address
starts afresh. `TexturePixels` ignores a holder with memory (it is read from its memory, as the
shadow textures in the switch-over are). `render::TextureFromMmp` returns whether it decoded.

Not functions / dead: the "shadow render surface (004EA4A0)" of section 14's scan is a false hit:
the Lock / Unlock it found are in 004EAFD0 (a depth read through the render target and the depth
surface), which lies inside 004EA4A0's functions.csv range and has no reference at all; 004EA4A0
itself touches no surface (its texture is bound by 0051CE60 and read by the new renderer from the
bound holder), so it needed no change. 00693DE0 (a cache take-back by name, SetPriority 100) lies
inside 00693C60's range and has no reference; 0068F860 is reached only from 00691D50, which has none
(config/dead_code.csv). The rest of the scan's SetPriority-slot (+0xB4) and Load-slot (+0xAC) calls
in game.exe are other classes' virtual slots, not DirectDraw (checked with capstone).

Original details kept: 00692010 / 00692140 do not check CreateObject's result; 00692140 releases the
previous render surface (there is none) after keeping the new one; 00690FC0 returns with the device
lock held when its Lock fails; 00693C60 does not lower +0xD8 for a texture it takes from the cache
(00691BF0 does); 0068FBF0 runs its loop over the width, then the height (a non-square .bmp
overruns the surface: kept for the DirectDraw path, the memory path leaves out what is outside its
memory), reads the colours on without the .bmp's row padding and in its row order, logs nothing
when the alpha file is missing, writes through a null description when a failed Lock's report says
go on and keeps the device lock when it says stop. 00691B00's EAX on the cache path (the priority
call's result or a list node) is not reproduced: it returns 0 (CEngineObject::Release, its only
caller, passes it on to callers that do not read it). In the switch-over 0068FA10's "Can't find
decode format." (a DXT file for an RGB format_ without one of its layouts, which ParseMmpHeader
never picks) is not reported.

Forwarders: 006D4AA0 / 006D4CD0 / 006D5910 / 006D4D10 (the game's DIB reader, `GameBitmap` in
texture_bmp.cpp, one file). 004EDC00 is not hooked (the scripts area's range) and not forwarded
from src (ui_surface_pixels.cpp keeps its forwarder): `ChannelLayout::ToNative` is the same
conversion in C++, checked against it in the verify build. map_object_shadow.cpp keeps its
forwarders to 00692010 / 00692140 / 00690FC0 / 00691030 (through the game addresses, so their
wrappers run). New: `CTextureManager` +0xBC `cache_` (mfc::CTypedPtrList<CTexture>, vtable
00743D18), +0xD8 `cachedBytes_`, +0xE0 `shadowRenderSurface_`; `MmpHeader::FilledBytes`.

Verify (all ours only: they make, lock or register textures):
- 00692010 / 00692140 (`verify_texture_shadow.cpp`): 64x64 in the device's 4:4:4:4 format, the
  kind flag (0x200 / 0x400, none of the others), a holder of its size (holder count + 1), byte size
  0, no block; in the switch-over only memory of that size and those masks, otherwise a DirectDraw
  texture (`memory_texture::BlankTextureProblem`, shared with 00691D80's wrapper); 00692140's later
  calls return the kept texture and make no holder.
- 00690FC0: 1, the device lock held once more by this thread, a description of the holder's size
  and bits with pixels and a row's pitch (for a holder without a surface: its memory); 0 and no lock
  without a holder. 00691030: the lock left once, the holder's result 0.
- 0051CE60 (`verify_map_object_figures.cpp`, added): in the switch-over, when the call rendered a
  shadow (the render surface's memory changed), the texture's memory equals the render surface's.
- 00690430 (`verify_texture_mmp.cpp`, added): with a DirectDraw texture the byte size the
  original's fill returned equals `MmpHeader::FilledBytes` (a differential check of the formula the
  switch-over uses); in the switch-over the holder has no memory, the texture's size and that byte
  size. Also `ChannelLayout::ToNative` against 004EDC00, 64 colours for every RGB texture format
  loaded.
- 0068FBF0 (`verify_texture_bmp.cpp`): result, flag 8, a holder of the .bmp's size in the format;
  memory only in the switch-over. Not reached: the installs' textures folders have no .bmp.
- 00691B00 / 00691BF0 / 00693C60 (`verify_texture_cache.cpp`): the cache list, +0xD8 and the
  registration as described above (released: at the tail and unregistered; taken back: out of the
  list and registered; FindTexture: registered first, the cached one otherwise).
- 004EF7B0 (`verify_dx_surface.cpp`, added): a memory-only holder's channel masks and bits equal the
  primary surface's GetPixelFormat (the original's source) while there is a primary.

Scenarios: render_present and render_present_cpuwindows (the switch-over with a new game: object
shadows, the camp, the global map), render_movie_present and render_titles_present (the
switch-over at the main menu and with movies), ui_saveload (`new,present`); outside it
frames_basic, render_scene (the shadows and every .mmp texture with DirectDraw: the FilledBytes
and ToNative checks), render_cpuwindows (the option outside the switch-over: DirectDraw textures).
The cache functions run in every scenario with a new game (figures' and materials' textures by
name: 00500A00 -> 00692D30 -> 00691BF0, 004EB550 -> 00693C60; releases at zone changes and when
windows close). ei_hook.log says once "cpuwindows: the texture manager's .mmp textures in the
switch-over get no DirectDraw texture, only the new renderer's decoded copy". Not checked in game
yet (runs are central).

If the new renderer fails after such textures were made (`Presenting()` turns false: a sokol panic,
no Direct3D 11 device), the old path has no texture for them (untextured), the shadows are not
copied any more, nothing crashes.

### What is left before presenting from the new renderer's own swap chain (item 3 started)

In the switch-over the DirectDraw / Direct3D 7 objects left are the device's own: the DirectDraw
object (004EDD70), the primary surface, the render target and the depth buffer (004E6C30), the
Direct3D device with its four state blocks and viewport (004E5E50), its texture format list
(004E6EE0, which FindFormat 004E7DA0 and ParseMmpHeader 004E7C40 read) and dgVoodoo's swap chain
under them; the holders no longer read the primary (above). For a swap chain of our own on the
game window, dgVoodoo's must go, i.e. no DirectDraw device at all:
1. Device setup in the switch-over (004EDD70, 004E5E50, 004E68A0, 004E6C30, 004EE*, 00500B70 is
   rewritten): no DirectDraw object, display mode, primary, render target, depth buffer or Direct3D
   device; the device object filled for the game (the screen rectangle +0x24, the render target
   holder's width / height / bits at +0x18 without a surface: `GameTargetSize` and the screen copies
   read them; 2DintMMX's converter +0x4C still loaded), the texture formats FindFormat answers
   (5650, 5551, 4444, 8888, DXT1 / DXT3: a fixed table instead of 004E6EE0's enumeration), the four
   state block handles (007AEDDC..D0) as tokens the mirror knows (section 5, point 3). Most of these
   are in 0x004E0000-0x00500000 (the scripts area's range): coordinate before rewriting them.
2. The screen format without DirectDraw: from the chosen colour depth (16: 5:6:5, 32: X8R8G8B8). To
   check first: where the windows' colour keys (CUiSurface +0x34) are computed, so they agree.
3. Every old-device call left (section 5, point 6: 524 sites, 83 functions not rewritten) guarded
   for a device object without a device: the rewritten draws still set states, bind textures
   (004EB300 / 004EBAF0 / 00501AF0) and call 004EB550 for the hand-over even when the old draw is
   skipped; the pass state then only from the mirror (the documented exception: 006462A0's mip
   filter, Rebirth-patched).
4. `render::Present` presents a flip-model swap chain on the game window instead of 004EF610's copy
   into the render target and Flip / BltFast; the movie players and the loading screen (section 13)
   present through it too; full screen / borderless and resolution changes (the game's device
   re-creation) resize it; the gamma ramp (IDirectDrawGammaControl) becomes a shader step; lost
   device / WM_ACTIVATE handling in 004EF610's place.
5. The cursor between frames: the hardware cursor needs nothing; the software cursor option draws
   only inside frames in the switch-over (section 13), so it does not move during movies and
   loading.
