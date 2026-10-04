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
weather (after its builders draw). Movies (Bink) are handed over; the trade grid's depth clears too (00627390 rewritten).
The switch-over mode `present` exists (section 5, "Switch-over").

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
| `present` | the switch-over: everything handed over is off in the old path (`noterrain,noobjects,nointerface`, movie blits too) and at the old present (004EF610) the new frame is copied into the game's render target, which the old Flip / BltFast then shows; captures write the new frame as `frames\<name>.bmp`. Scenario `render_present` (frames_basic in this mode). A first version showed the frame in the child window instead: under dgVoodoo's full screen the second swap chain made the game window inactive (WM_ACTIVATE inactive: the game's frame function then draws nothing and the window minimises itself), so no frame was drawn at all. `show` / `split` have the same problem in full screen; use them windowed |

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
| Depth clears | the battle screen's before its panels (00636020), the hero preview's (005EC300: all to 0, its rectangles to 1) | `render::ClearDepth` | drawn; 00627390 (not rewritten) missing |
| Shadows | 64x64 shadow textures rasterised on the CPU (2DintMMX slots 8 / 9 / 11 / 12 into a surface), ground quad 0056C750, drawn with 004EA4A0 | 004EA4A0 (rewritten) -> `render::BeginMesh` without corners (per-vertex positions, one colour pair, depth scale); the texture from the render surface right after 0051CE60 copies it (`render::TextureChanged`) | drawn |
| Particles, ribbons, lightning, hit points, vision fog | the renderer's pre-transformed draws: sprites 00502CA0 -> 00503680 (particles, the vision fog's quads), ribbons 00501D90 -> 00502A20 (lightning), triangles 00501BB0 (rewritten, `renderer_triangles.cpp`: flying hit points) | `render::TlDraw` at the three draws (vertices after the fog pass, the old device's state) | drawn; later: the particle data itself instead of its screen quads |
| Weather | rain and snow quads built by 004E4240 / 004E4C80 (2.6 KB each, not rewritten; procedural placement) into pre-transformed vertices (+0x70, count +0x74, indices +0x84) and drawn with the device directly | the weather draw 004E5680 rewritten (`weather_draw.cpp`): after the rain / snow returns, its triangles go to `render::TlDraw` with the device state of that moment | drawn; the old draw stays (the two builders are not split yet) |
| Sky | the sky object (004D96E0: a model at the camera, turning with time) | the figure path | with objects |
| Interface surfaces | CUiSurface (0x38 bytes): +0x0C flags (1 colour key, 2 whole surface stretched, 4 shown), +0x10 the surface holder, +0x24 the screen rectangle, +0x34 the colour key; the DirectDraw surface's pixels (read with a read-only Lock when its uniqueness value changed) | the blit 005FDA80 (rewritten: `ui_surface_blit.cpp`) -> `render::SurfaceBlit` | drawn (overlay) |
| Rectangle fills | renderer slot 19 (005041F0): a translucent black quad (alpha argument) and optionally its outline | 005041F0 rewritten (`renderer_rect.cpp`) -> `render::ScreenDraw` (pre-transformed triangles, line strip) | drawn |
| Cursor | the cursor object 007C7BD8 (shape 005E5200; hardware cursor normally; the display option's software cursor is drawn into locked surfaces by 005E5690 / 005E5C40 after the capture point) | the frame draw 005E54A0 rewritten (`ui_cursor_frame.cpp`) -> `render::Cursor` (image and mask; texels with a mask left out) | drawn; the cursor thread's draws between frames are not (they go to the primary) |
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
| 7. Movies | done: the decoded frame's surface as a window-surface blit. Later: BinkCopyToBuffer into a CPU buffer (no DirectDraw surface) | (done) |
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
   surface already). Left: those, then the DirectDraw texture objects themselves.
2. **The interface is composed on DirectDraw surfaces** with GDI (GetDC / ReleaseDC, text, fills)
   and pixel code (locks). First step done behind `EI_RENDER=...,cpuwindows` (off by default until
   proven in game): the surface holder's constructor 004EF7B0 (used for the windows, the software
   cursor and the movies' surfaces; system memory, the screen's format) is rewritten and, with the
   option, puts the surface's pixels in a DIB section the game code owns (DirectDraw client
   memory: DDSD_LPSURFACE / DDSD_PITCH; refused -> a plain surface, logged "cpuwindows: ..."); the
   destructor body 004EFD20 frees it. The window surface's DC helpers 005FDC00 / 005FDC60 are
   rewritten: with client memory GDI draws through a memory DC on the DIB section, not
   DirectDraw's GetDC (an empty Lock / Unlock afterwards tells DirectDraw the memory changed, for
   the old path's Blt). Lock users (the console, the description panels, Greyscale ...) work on
   the same memory unchanged. The new renderer reads such a surface's memory directly
   (`render::ClientSurface`; GdiFlush first), not through a Lock. Checked offline: Windows'
   DirectDraw takes a DIB section as a system-memory surface's memory, and GDI through a memory DC
   on the DIB and through the surface's DC both land in it (dgVoodoo not checked: the fallback
   covers a refusal; if it caches client memory and misses GDI writes, the old path's windows
   would go stale, which the empty Lock / Unlock is there to prevent). Left: the change detection
   still asks DirectDraw (the uniqueness value; a dirty flag from ReleaseDC and the lock users
   next), the old path's Blt of windows (goes with the old path), and with the verify build's
   side-by-side text checks under cpuwindows the GDI batch must be flushed before they read
   pixels (not done: expect false mismatches there with the option on).
3. **Pass state** (decided: fed by the game code). `src/render/render_state.h`: every rewritten
   place that sets a render state, a texture stage state or applies a state block on the old device
   also calls `render::state::Render / Stage / Block` with the same values (13 files so far:
   device_draw, renderer_init (and a reset at device creation), renderer_rect, renderer_sprites,
   renderer_strip, terrain_draw, world_client_draw, ui_camp, ui_create_screen, ui_hero_preview,
   ui_hud, ui_item_view, ui_objectives). The device records only four state blocks, at its creation
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
   not converted, e.g. the renderer's state presets 00501C60 that particles use); the device's
   value is drawn then. The first runs' list (render_*, frames_basic, ui_camp, both installs) is
   handled: the fog colour (renderer slot 0, 00501AB0), the material setter (004EB170: stage 1 off
   without a material) and three window draws that set alpha states (00600F70, 0062EFC0, 0065A6D0)
   are rewritten and feed the mirror; the states the game never sets (ZFUNC, TEXTUREFACTOR,
   ADDRESSU / V, stage 1 off) start with the device's initial values (checked by the verify build
   like fed ones). One documented exception, not logged: stage 0's mip filter, which the unit
   panel's draw 006462A0 turns off around the unit's picture and back (Rebirth replaces that whole
   function with its wide-layout version, so it cannot feed the mirror until it is rewritten with
   that patch); the device's value is drawn in the verify build, the mirror's (linear mips) in the
   release build. Known writers not reached by the scenarios: the tutorial screen's create
   00658E10 (alpha reference 1), the particle presets 0049B140 / 0049B4B0 and the renderer's
   005045D0 (stage 0 colour operation around their draws).
4. **CPU paths into surfaces**: the shadow rasteriser now draws into a CPU buffer (the surface's
   layout), which goes to the new renderer (`render::TexturePixels`) and is then copied into the
   render surface for the old path's texture load. Bink decodes each frame into a CPU buffer too
   (BinkCopyToBuffer's 32-bit R, G, B, X type 4, checked offline against the 5:6:5 decode of a game
   movie) for the new renderer (`render::MovieFrame / MovieBlit`); with `present` the video's
   DirectDraw surface is not filled at all. Left: the software cursor (its image comes from a
   texture loaded by 005E4EB0 into a surface, not rewritten; read back each frame) and the screen
   copies (written into DirectDraw surfaces the interface draws from: with point 2).
5. **Draws the old path still makes**: the weather's (004E4240 / 004E4C80 build and draw in one
   function; the new renderer takes the triangles after they return). Everything in the world view
   and the interface is handed over; with `present` the old back buffer is overwritten by the new
   frame.
8. **Presenting through the old path** costs a full-frame readback and conversion per frame (33 MB
   at 3840x2160): a staging copy kept between frames, 32-bit BGRX written directly. Fine for the
   switch-over; the final form presents from the new renderer's own swap chain once dgVoodoo's is
   gone (point 6).
6. **Old device calls still made**: 524 call sites in 143 functions; with no device every one needs
   a guard or a rewrite. Creating the device (00500B70, `renderer_init.cpp`) can only be skipped
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
