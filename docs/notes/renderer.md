# Renderer: the sidecar renderer, the hand-over from the game, and the switch-over

The game's own renderer (DirectDraw 7 / Direct3D 7, translated by dgVoodoo in our runs) is being replaced:

1. Game code that called the renderer is rewritten and hands its data to a **new renderer in a sidecar
   module** (`src/render/`, static library `ei_render` linked into ei_hook): camera and matrices, object
   transforms, figure meshes, terrain sectors, particles, textures, the interface windows' CPU surfaces
   (uploaded as textures), movie frames. The old Direct3D calls stay behind flags until we stop hooking
   game.exe.
2. No Direct3D 7 emulation, no bit-exact rewrite of 3DfpFPU.dll / 2DintMMX.dll, no x87 or compatibility
   shims in the new code. Results only need to be close (captures are compared with a tolerance); full
   determinism is out of scope.

Backend: **sokol_gfx** (`third_party/sokol`, commit 2e75443, zlib) on **Direct3D 11**. The new renderer draws, in
the game's order, the terrain (ground and water pages), every mesh the old path draws (figures, map objects, sky,
interface sprites and 3D previews), the flat geometry (object shadows, selection markers, light decals), the
interface windows' surfaces, the renderer's rectangles (rubber band, fades), the software cursor, the depth clears
mid-frame, every pre-transformed draw (particles and sprites, the vision fog's quads, ribbons and lightning, flying
hit points), the weather (after its builders draw) and the movies (also the players outside the frame); the escape
menu's and the save game's screen copy comes from the new frame. In the switch-over mode `present` no texture or
surface holder of the game is a DirectDraw object any more; what remains of the old path is the device itself
(section 9).

## 1. Switch (EI_RENDER)

`EI_RENDER` (environment, or the harness step `render <spec>` before the game starts) is a comma-separated list;
the full text is the header of `src/render/render.h`.

| Option | Effect |
|---|---|
| `new` | the sidecar renders every frame into an off-screen target of the game's resolution; captures also write `frames\<name>.new.bmp` |
| `show` / `split` | a child window over the client area shows the new frame (mouse passes through: `HTTRANSPARENT`); `split` covers the right half only |
| `noterrain`, `noobjects`, `nointerface` | the old path stops drawing the terrain (DrawSectorPage returns after applying water levels) / the world's meshes and flat geometry (004EA4A0) / the interface's meshes and window blits |
| `cpuwindows` / `nocpuwindows` | surface holders of 004EF7B0 (windows, software cursor, movies) and the system-memory picture holders of 004F0060 keep their pixels in memory the new renderer owns; on with `present` |
| `present` | the switch-over: everything handed over is off in the old path, `cpuwindows` on; captures write the new frame as `frames\<name>.bmp`. On when the device object is built (`render::StartPresenting`, 004EDD70): the game has no DirectDraw or Direct3D 7 device and the new renderer presents through its own flip-model swap chain on the game window (section 9, R9a). Turned on later (a DirectDraw device exists): at the old present (004EF610) the new frame is copied into the game's render target which the old Flip / BltFast shows |

Without `new` nothing in `src/render` runs. The harness step `render <spec>` adds the words of `EI_RENDER` to its
own, so a run with `EI_RENDER=new,present` keeps the switch-over in every scenario; once the game is built without
DirectDraw, `new,present,cpuwindows` stay on whatever a later spec says (logged). Harness steps `oldterrain`,
`oldobjects`, `oldinterface` (on / off) switch the old path's parts during a run. Under dgVoodoo, dgVoodoo owns a
flip-model DXGI swap chain on the game HWND (a second one fails), hence `present`'s copy into the render target there
and the child window of `show` / `split`, which work windowed only (under full screen the child's swap chain
deactivates the game window, which then draws nothing and minimises).

Runs without dgVoodoo: `tools\setup_native_run.cmd` makes `run\native` (run\vanilla's files without DDraw.dll,
D3DImm.dll, D3D8.dll, D3D9.dll and dgVoodoo.conf; a worktree without `run\` gets a junction to the main checkout's);
`python tools\run_all.py --native <scenarios>` runs on it with `EI_RENDER=new,present`.

## 2. Frame structure of the game

Per main-loop iteration (00458010): 00503F40 BeginScene; 005F5220 the interface draws window by window; 005040A0
EndScene; the escape menu (005F5320, Rebirth-patched); the software cursor (display option only); **present**
004EF610 (rewritten, `device_present.cpp`: full screen Flip, window BltFast to the primary). The battle screen's
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
capture) draws them in one off-screen pass with depth, then the optional child-window pass. Per-frame data is rebuilt
every frame; textures and window surfaces are cached per holder.

Conventions taken over: Direct3D clip space (the game's projection already produces it), Direct3D 7 pixel centres
(3D draws shift their clip positions by half a pixel, `Frame::pixelShift`, because sokol truncates its float viewport
to whole pixels), D3DCOLOR vertex colours (B, G, R, A bytes), vertex fog in the specular alpha (255 = no fog),
specular added after texturing. Textures use the .mmp file's own mip levels (not a box filter of level 0).

## 3. What the renderer feeds back into game logic

- **Visibility** (frustum test 004F16A0, in-view flag 0x20 of client objects): readers are client-side only (cursor
  picking and rubber band 0051C130, particle sources 00496EB0, shadow volume 0051E300; also client animation to game
  time, pending figure changes 0050D450, model LRU). **Visibility only gates client-side animation, figure changes and
  picking, never server state or timing.**
- **Picking** uses screen rectangles written during the object draw from projected vertices (004E8900 / 004E86B0 into
  the device's +0x15C, copied by 005A7A70 into node and figure pieces). When the old object draw goes, the hand-over
  must still produce them (approximate is fine, from projected bounds).
- **One real state change in a draw path:** the terrain page draw applies pending water level changes (006C30E0 from
  006BFF40); the water layer is what height queries read. DrawSectorPage applies them before handing over or skipping
  the old draw.
- **Pixels:** the escape menu's background and the save game's 256x192 picture are copied from the render target
  (005FD3D0 CaptureScreen, 005FD440 Rgb565Copy); with the sidecar presenting they come from its frame.
- **Timing:** up to five world updates of 0078FF34 ms per frame; vsync only changes grouping.

## 4. Hand-over map (game data, hand-over point, `render::` call)

| Data | Where in the game | Hand-over point | Status |
|---|---|---|---|
| Clip matrix of the current draw | transform pipeline (renderer 007AF060 +8): stacks of 64-byte row-major matrices, `+0x24/+0x28` clip space (axis swap * projection * view * model), `+0x1C` the same times the viewport, `+0x5C/+0x60` view stack | read at hand-over time | used |
| Terrain sectors | SectorData (`terrain_sector.h`): positions +0x17C (33x33 per layer, water after ground), vertex records +0x190 (0x54 bytes, lit colour +0x11, specular / fog +0x15), tile lists per quarter and page +0x314, edge flags +0x2C4, quarter visibility +0x144, LOD +0x170, seam flags +0x174 / +0x178, owner TerrainFields +0x1C; page textures TerrainFields +0xD8 | DrawSectorPage 006BFF40 (`terrain_draw.cpp`) -> `render::TerrainPage` | drawn |
| Meshes (figures, objects, sky, interface sprites and previews) | positions in blocks of four (morphed by the game), corners, colours as lit by the game (005A7A70 step 3; flag 4: device flat colour +0x150), texture coordinates; matrix = top of the screen stack converted back to clip space; distance fog switch +0x158 and range | DeviceDrawMesh 004E9010 / 004E9A50 (`device_mesh.cpp`) -> `render::BeginMesh` / `EndMesh`; `world_client_draw.cpp` (00569E40) -> `BeginWorld` / `EndWorld` | drawn |
| Pass state | per pass of a mesh (material technique 004EB550, or two textures 004EB370): blend, alpha test, depth, culling, specular, fog, texture factor, two stages, filters, addressing, bound holders (device +0x12C / +0x130) | `device_draw.cpp` -> `render::MeshPass` (section 5) | drawn |
| Depth clears | 00636020, hero preview 005EC300, trade grid 00627390, hero equipment 00622420, screens 2 / 3 (00632A90 / 00602BF0) | `render::ClearDepth` | drawn |
| Shadows | 64x64 shadow textures rasterised on the CPU (2DintMMX), ground quad 0056C750, drawn with 004EA4A0 (also selection markers, light decals) | `device_shadow.cpp` -> `render::BeginMesh` without corners; `map_object_shadow.cpp` (0051CE60) tells which texture changed (`TextureChanged`) | drawn; later GPU shadows |
| Particles, ribbons, lightning, hit points, vision fog | pre-transformed draws: sprites 00502CA0 -> 00503680, ribbons 00501D90 -> 00502A20, triangles 00501BB0 (`renderer_triangles.cpp`), line strips 005045D0 / 0049B4B0 (`renderer_lines.cpp`) | `render::TlDraw` with vertices after the fog pass and the old device's state | drawn; later the particle data itself |
| Weather | rain / snow triangles built by 004E4240 / 004E4C80 (`weather_particles.cpp`, A-S13: `BuildRain` / `BuildSnow`, then `DrawParticles`) into pre-transformed vertices and drawn with the device directly | 004E5680 rewritten (`weather_draw.cpp`): after the draw its triangles go to `TlDraw`; the old draw stays | drawn; the old draw (`DrawParticles`) still runs |
| Interface surfaces | CUiSurface (0x38): +0x0C flags (1 colour key, 2 stretched, 4 shown), +0x10 holder, +0x24 rect, +0x34 key | 005FDA80 (`ui_surface_blit.cpp`) -> `render::SurfaceBlit` | drawn |
| Rectangle fills | renderer slot 19 (005041F0) | `renderer_rect.cpp` -> `render::ScreenDraw` | drawn |
| Cursor | object 007C7BD8; images from 8888 pictures loaded by 004EFC60 -> `render::PictureFromMmp`, mask from game memory | frame draw 005E54A0 (`ui_cursor_frame.cpp`) -> `render::Cursor` | drawn; cursor thread (005E5690) and background saves left out with `present` |
| Movies | Bink: BinkCopyToBuffer (32-bit type 4) into a CPU buffer; CBinkVideo 005DBA50 opens, 005DBD80 steps and blits into a letterboxed rectangle | `bink_video.cpp` -> `render::MovieFrame` / `MovieBlit` / `SurfaceBlit`, `render::Fill` | drawn |
| Present | 004EF610 | `device_present.cpp` -> `render::Present` (with `present`: new frame copied into the render target; a full-frame readback per frame) | done |
| Clear colour, target size, fog | renderer +0x14; device object 007AF080 -> +0x18; render states FOGENABLE / FOGCOLOR of the old device (zone colour through slot 0 00501AB0) | EndFrame / terrain hand-over | fog still read from the old device state |

Their game-side work stays (visibility, LOD, animation, lighting, sorting, water levels); the verify wrappers keep
checking it. Not handed over directly yet: `figure_part_draw*.cpp` (005A7A70, 005A7640: model matrices and figure data
once meshes stop going through the old projection), the particle data, materials from the game's data instead of the
old device's state, picking rectangles. The Direct3D 7 code is deleted when we stop hooking game.exe; the 3DfpFPU /
2DintMMX calls left in game code (bounds at figure load, the terrain's transform before clipping, the shadow
rasteriser) then get plain C++ replacements.

The interface is overlay, not native: each window surface is uploaded as a texture when its contents changed (checked
at every blit) and drawn at the blit's place with the colour key as transparency. Native drawing (glyph atlas from the
game's fonts to keep the metrics the word wrap 005F8340 depends on, every window class's Draw / Update as quad / text
submissions) could come window by window: a window switched to native simply stops blitting its surface.

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

A state nothing has fed is read from the old device and logged once ("render: pass state: render state N not fed by
the game code yet"). The verify build reads every fed state back and logs disagreements ("fed X but the old device
has Y": a writer not converted) and draws the device's value. States the
game never sets (ZFUNC, TEXTUREFACTOR, ADDRESSU / V, stage 1 off) start with the device's initial values.
**Documented exception:** stage 0's mip filter, which the unit panel's draw 006462A0 turns off around the unit's
picture: the vanilla body is ported and feeds the mirror (A-E2), but Rebirth replaces that whole function, so
in a Rebirth launch the device's value is drawn in the verify build, the mirror's (linear mips) in the release build.

Rewritten state setters (all feed the mirror): 00501C60 (renderer slot 10, `device_states.cpp`: SRCBLEND then
DESTBLEND, failure through 004800E0; its known caller is the particle draw 00498940; differential on the real device);
005045D0 (slot 20, a strip through integer screen points, no caller) and 0049B4B0 (`renderer_lines.cpp`: the strip of
the renderer's line lists, 0049B780 from slot 15 00501CE0: bounds boxes 005A7290, line lists 00569D60; flat shaded,
into a growing vertex buffer at 007A9CE0; both turn stage 0's colour / alpha ops off and back to MODULATE) go to
`render::TlDraw` (line strip, FVF 0xC4), ours only, not reached by scenarios; 005041F0 (`renderer_rect.cpp`). 0049B140
only appends a line-list point (the device calls under it belong to 0049B320, a dead two-point line draw); 0050434F
and 00504341 are not functions (inside 005041F0; a false vtable entry 00747E2C in config/vtables.csv).

## 6. Interface windows and holders on game memory (`cpuwindows`)

**Memory** (owned by the new renderer): `render::NewSurfaceMemory / SurfaceMemoryOf / FreeSurfaceMemory / HolderGone`
(render_textures.cpp): one zeroed top-down DIB section per holder (GDI draws into it through a memory DC) with pitch,
size, bit count and channel masks. Made by the holder code, used by game code (`ClientBitmap`, `ClientPixels`,
`DirectPixels` in `dx_surface.h`) and read by the renderer after GdiFlush. Outside the switch-over the same memory is
given to DirectDraw as the surface's client memory (DDSD_LPSURFACE | DDSD_PITCH; refused -> a plain surface, logged
"cpuwindows: ..."). Change detection: the renderer keeps a copy of the memory each window texture was made from and
converts again only when it differs (a memcmp at each blit).

**Switch-over** (`present` with `cpuwindows`): the holders of 004EF7B0 (windows 005FCC00 / 005FCDD0 / 005FD7A0, the
cursor's background and composition surfaces 005E60B0, the movies' decode surfaces) and the system-memory picture
holders of 004F0060 (cursor images via 004EFC60 from 005E4EB0; dead picture windows) have no DirectDraw surface:
`CDXSurface::surface` stays null, the pixels are only the renderer's memory. The screen's pixel format comes from the
display mode (`ReadScreenFormat`; the primary only if that fails; the verify build checks a holder's masks and bits
against the primary's). If the new renderer fails later (`Presenting()` turns false: a sokol panic, no Direct3D 11
device) the old path cannot draw those holders (its blits and copies skip them), so the interface, cursor and movies
stay missing but nothing crashes.

| Address | Name / change (ours only unless noted) |
|---|---|
| 004EF7B0 `DXSurfaceConstruct`, 004EFD20 destructor body | memory-owning holder; in the switch-over no CreateSurface (device +8 = 0) |
| 004F0060 `DXSurfaceLoadPicture` (RET 8) | 8888 pictures only; fills the surface (32 bits a row copy, 16 bits through 2DintMMX slots 1, 2); a system request with memory fills it directly; only live caller 004EFC60 from 005E4EB0 (flag byte `00784756` is 1 in the exe, never written, so the flag passed is 0); S on a scratch holder |
| 005E4EB0 `CursorImage::Construct` (`ui_cursor.cpp`) | picture, holder, frames = width / height, hot spot x cursor size (007C7C90 / 94) / 32, mask (16 bits all ones where the pixel is 0; 32 bits low 24 bits 0) |
| 005DBA50 `CBinkVideo::Open` | BinkDDSurfaceType only for a holder with a surface (else 4, the CPU copy's type) |
| 005FCC00 / 005FCDD0 / 005FCF80 / 005FD120 | the colour-fill Blt that clears a new window is left out (a new DIB section is zero); description from the memory |
| 005FDC00 / 005FDC60 | GDI through a memory DC on the DIB; an empty Lock / Unlock afterwards tells DirectDraw the memory changed (only with a surface) |
| 005FD3D0, 005FD440, 005FE440, 005FD7A0, 005FD240 | screen copy (`render::CopyFrameTo`; no Blt from the old target, which does not hold the frame), save picture stretch (CPU copy, (2x+1) * source / (2 * size); not compared at 1280x720 -> 256x192), Greyscale, SwapContents, pixel read: no Lock / GetSurfaceDesc |
| 005FDA80, 005E54A0 / 005E5690 / 005E6630, 005DBD80 / 005DB3F0 / 005DA400 | old Blt, composition 005E5C40, background save, Lock / BinkCopyToBuffer / Unlock only outside the switch-over and with a surface (`OldPathDraws`) |

Scenarios: render_cpuwindows, render_present / render_present_cpuwindows (cursor images, the escape menu over its
greyed copy, the camp), render_titles_present (005DBA50 / 005DBD80), render_movie_present; the DirectDraw path:
ui_camp, ui_screens, ui_main_menu. **Open:** under cpuwindows the verify build's side-by-side text checks must flush
the GDI batch before reading pixels (not done: false mismatches expected there with the option on).

Kept: a holder that already has a surface (dead callers only) must hold the picture's width in both +0x14 and +0x18; a
failed Lock whose report says stop returns with the device lock held; the 24-bit clear ignores the pitch; 005E4EB0
ignores GetSurfaceDesc / Lock failures after reporting; 005DBA50 leaves a closed Bink handle in +0x18 when movies are
off. Not reproduced: the unwinding when 004EFC60 / 004EF7B0 throw and the fallback to 004EF7B0 when 004EFA60's
video-memory holder throws (only with 00784756 0).

**Save / load screen's pictures** (`ui_saveload.cpp`): two 256x256 5:6:5 textures made by 005FF0B0 (-> 00691D80 ->
0068F6C0): objects_[1] filled by 00656E10 (ShowPicture: the selected save's shot.sav) and +0x184 (the new save's
picture) filled by 00655D40 (FillEntries, from the escape menu's 256x192 copy, interface +0x5C) and written to shot.sav
by 00655550 (SaveGame; the autosave 006570E0 writes it from a window surface). 00656464 is not a function (inside
00655D40; a false vtable entry 0074718C). With cpuwindows Build (00653DD0) and FillEntries give each texture's holder
memory (`GivePictureMemory` -> `AttachTextureMemory`, CTexture +0x3C); outside the switch-over the DirectDraw texture
stays and the fills still Lock it and copy the 0x18000-byte block into the memory. Verify: ShowPicture / FillEntries
original then ours, SaveGame ours only; scenario ui_saveload (`new,present`). Kept: the 0x18000 bytes are one block (pitch taken as 512); Locks use flags
0; a failed Lock whose report says stop leaves the device lock held; the entries' temporaries leave +8 / +0x0C unset
where info.sav does not give them; the new folder is "save<n>", n one more than the highest atoi(folder.Mid(4))
(006FD9B4 is Mid). The SavesPath Rebirth patch is disabled in this install and not reproduced.

## 7. Textures without DirectDraw in the switch-over

In `present` with `cpuwindows` CPU-written textures (00691D80), shadow textures and .bmp textures have only the
renderer's memory; .mmp textures only its decoded copy. Outside it everything is the original's. All verified ours only
(they make, lock or register textures).

| Address | Name / change |
|---|---|
| 0068F6C0 `CTexture::CreateBlank` (`texture_blank.cpp`, RET 8, EAX 1) | surface square from header->width (height not read), mipCount levels, caps by the kind flags (0x400 system memory else TEXTUREMANAGE; 0x40 OPAQUE, 0x80 HINTSTATIC, 0x100 / 0x200 / 0x400 HINTDYNAMIC); holder from 004EFA60; +0x28 = 0 |
| 00691D80 `CTextureManager::NewTexture` (RET 0xC) | kinds 0x1E / 0x20 / 0x21 get the device's 5650 / 5551 / 4444 (missing: FatalError "Can't find texture format %d."); in the switch-over the holder is `NewMemoryTextureHolder` (no CreateSurface; no memory -> DirectDraw texture, logged) |
| 00692010 `NewShadowTexture`, 00692140 `ShadowRenderSurface` (`texture_shadow.cpp`) | 64x64 4:4:4:4 shadow texture and the shared render surface (manager +0xE0) |
| 00690FC0 `LockPixels`, 00691030 `UnlockPixels`; 0051CE60's copy -> `CTexture::LoadPixelsFrom` | device lock held between the two; a holder without a surface is described from its memory (`ClientPixels`); the shadow texture is filled from the render surface on the CPU |
| 00690430 `CTexture::CreateFromMmp` (`texture_mmp.cpp`) | switch-over: no staging surface, fill (0068FA10), Load or priority; `render::TextureFromMmp` decodes the file's levels (DXT1 / DXT3, 8888, 4444, 5551, 5650 by the header's channel masks; keyed by the holder); +0x28 = `MmpHeader::FilledBytes`; an undecodable format gets the DirectDraw texture (logged once) |
| 0068FBF0 `LoadBmpFiles` (`texture_bmp.cpp`, RET 0xC) | loose textures\<name>.bmp (from 00690260) via the game's DIB reader (006D4AA0 / 006D5910 / 006D4D10 / 006D4CD0); not reached (no .bmp in the installs) |
| 00691B00 `ReleaseObject` (slot 24), 00691BF0 `TakeFromCache`, 00693C60 `FindTexture` (`texture_cache.cpp`) | cache list +0xBC, bytes +0xD8; SetPriority only for a holder with a surface |

Users of the CPU-written textures: 005FF0B0 (`CUiWindow::CreateTexture`, from the battle screen 00635250, options
006503C0, save / load, global map 0062D350, tutorial 00658E10, 00605DE0, 00608F90, 00640060) and 005EC020 (item screens'
figure textures); writers 005FFFB0 CopyImage, 005EC020, the global map's weather 0062F050 (00499F10 draws into the
memory) and the save / load three all use `DirectPixels`. The binders (004EB300, 004EBAF0, 00501AF0, 004EB370's inline
one) pass the holder's null surface to SetTexture (the old device draws nothing in the switch-over; the renderer reads
the bound holder, device +0x12C). Renderer side (`render_textures.cpp`): decoded textures (`StoreDecoded`) are keyed by
the holder; a holder with neither surface nor memory shows its decoded texture until it goes (`HolderGone` retires its
texture and window entries); `TexturePixels` ignores a holder with memory.

Verify wrappers in the switch-over (`tests/verify/memory_texture.h`): an original that locks such a texture runs on a
temporary DirectDraw texture lent to the holder (00691D80's wrapper lends every new texture while 005EC020 / 00655D40's
original runs); refused ("verify: no DirectDraw texture ... ours only"): 005FFFB0 / 00656E10 run ours only, 005EC020 /
00655D40's original would crash (not expected). Scenarios:
render_present*, render_movie_present, render_titles_present, ui_saveload; outside the switch-over ui_camp, ui_screens,
ui_hud, frames_basic, render_scene.

Kept: 0068F6C0's byte size (+0x28) is always 0 (it takes the bits from the first field of 004EDAA0's output, which that
function zeroes and never writes) and its surface is square from the header's width; 00691D80 leaves a texture with a
missing format registered; 00692010 / 00692140 do not check CreateObject's result; 00690FC0 returns with the device lock
held when its Lock fails; 00693C60 does not lower +0xD8 for a texture it takes from the cache (00691BF0 does); 0068FBF0
loops over the width then the height (a non-square .bmp overruns the surface) and reads colours without row padding;
00691B00's EAX on the cache path is not reproduced (returns 0; its only caller ignores it); 0068FA10's "Can't find
decode format." is not reported in the switch-over. Dead / not functions: 00693DE0, 0068F860 (reached only from
unreferenced 00691D50), 004EAFD0 (inside 004EA4A0's range).

## 8. Draws with old-device calls, movies and the cursor outside the frame

Interface draws (`ui_container_draws.cpp`; each keeps the original's device calls in order, states go to
`render::state`, depth clears to `render::ClearDepth`; in the switch-over the old device's clears are left out (+8 = 0)
since nothing the old path still draws reads its depth buffer; the older clear hand-overs 00636020, 005EC300, 00627390
still clear the old device too and could take the same guard). Verified ours only (the original would draw twice):
00600EF0 (CUiContainer slot 20: state block 007AEDD8, 005FEB90, children's slot 20); 00602BF0 / 00632A90 (slot 20 of
screen 3, vtable 007405AC / screen 2, 00740688: depth clear then 00600EF0; screen 3 first runs 00578910 on 007AFEA5 and a
mouse move to the input side's slot 4; no scenario reaches them); 00622420 (CHeroEquipPanel slot 20, vtable 007418E0:
highlight frames, quick items' models (005ECA10), in the camp the armour models in seven slots (centres 00741EC4 /
00741EE0) between three depth clears, each after a flush of the two batches); 0064D970 (slot 26 of vtable 007411BC:
alpha reference = the window's +0x1EC; returns SetRenderState's HRESULT; differential on the real device; caller not
identified).

Movies and software cursor (old device calls kept, behind `render::Presenting()`; with `present`; all ours only):

| Address | What | With `present` |
|---|---|---|
| 005DB3F0 `PlayMovie` (`bink_video.cpp`; fastcall name, frame time, 5 stack arguments, RET 0x14) | blocking player (callers 00458270, 0057A060, 004B89B8, 00602190, 00632940, 00634D20, 00634E60): own message loop (Esc / Space end or skip a frame), letterboxed blit to the primary | each frame to `render::MovieFrame`, then `render::Fill` (black) + `MovieBlit`, presented through 004EF610 (else `render::Present` ends the sidecar frame); result 0 / 1 (1 with movies off), 00784751 set again |
| 005DB9E0 `HolderFill`, 005E5640 `HolderBlt`, 005E6630 `CursorSaveBackground` | colour fill, Blt between holders, background copy under the device lock | not called |
| 005DA400 `ShowLoadingMovie` (fastcall progress) | loading movie (progres*.bik per zone by 00595080; state 007C5550 / holder 007C551C / type 007C5554 / rect 007C5520) stretched over the primary | last frame blitted and presented as above |
| 005E5690 `CursorThread` (started through 005E52A0) | every 41 ms with the software cursor bit: restore background, save, compose (005E5C40), blit to the primary | draws nothing (the renderer draws the cursor in each frame, so it does not move between frames: loading, movies) |

005DA880 is dead (a thread procedure started only by 005DAD50, which has no caller); 005DA5AB / 005DB603 are catch
blocks (fallback to 004EF7B0, only with 00784756 0). Kept: a failed Lock whose report says go on skips the copy and
leaves the device lock held; a failed Unlock whose report says stop ends the movie (005DB3F0) or returns before the blit
and cursor calls (005DA400); 005DA400 hides the cursor (005E5270 0) on every call and never shows it again; the fifth
stack argument of 005DB3F0 is never read; the progress movie's path is 007AF460 + "Movies\" + the name. The harness's
input steps wait for PeekMessageA calls recognised by return address; the rewritten player's call no longer returns to
005DB788, so input steps are not posted inside a movie (the harness empties movie.ini's lists). Scenarios render_movie
(`new`: PlayMovie("crdtfin.bik") after a new game) and render_movie_present; 005DA400 and 005E6630 run in every new
game; 005E5690 and 005E5640 draw only with the software cursor option, which no scenario sets.

Images kept across frames (N5, `render_blit.cpp`): the cursor's composite and `render::Fill`'s 1x1 colours were an
`sg_make_image` + view per blit and the movie frames one per frame; now they are dynamic images updated with
`sg_update_image`. Cursor and fills share a cache of at most 64 images (one per size, several of a size when one
frame draws different contents), each keeping a copy of its pixels: a blit with the same pixels uploads nothing, else
a free image of that size (not blitted in this frame, not updated since the last commit; sokol allows one update per
image and frame, and the blits are drawn at the end of the frame) is rewritten; only when every image is in use is a
one-frame image made. A movie keeps one image per player, updated in place unless it was already blitted or updated
in this frame (then a new one replaces it after the present). Verify build: counts per purpose in ei_hook.log
(`render: images ...`: requested / made / updated in place, every 300 frames and at exit) and every in-place upload
is read back from the GPU and compared with the pixels written (`render: MISMATCH` on a difference). Window surfaces
(16-28 per scenario) and textures are re-made only when their contents change and are not touched.
Per scenario (render_present, _cpuwindows, render_movie_present, render_titles_present; vanilla and native): cursor
and fill images made per frame 40-135 before, 1-2 after; movie frames 17-50 made before, 1-2 after.

## 9. What is left

### R9a: the switch-over without DirectDraw (done)

With `present` on when the renderer object is created (00500B70; `EI_RENDER`, or a scenario's `render` step), the
game has no DirectDraw object, display mode, primary, render target, depth buffer, Direct3D object or device:
- 00500B70 lists and tries no DirectDraw driver (null driver id). 004EDD70 starts the new renderer
  (`render::StartPresenting`: the Direct3D 11 device; if that fails the DirectDraw device is built as before), loads
  2DintMMX's converter, keeps the cooperative level's flags and screen rectangle, lists the one display mode, and
  makes the primary and the render target two holders without a surface of the screen's width, height and bits
  (counted like other holders; `GameTargetSize`, the screen copies and the `if (primary_) renderTarget_->width` reads
  see the screen). No video FourCC probing.
- 004E5E50 takes the IDirect3DDevice7 stand-in (HAL id, no depth formats, no depth buffer, no render target set),
  sets the main thread's x87 precision to 24 bits as Direct3D 7's CreateDevice did (the game passes
  DDSCL_FPUSETUP), builds the format table from the stand-in's fixed list (5:6:5, 1:5:5:5, 4:4:4:4, 8:8:8:8, DXT1,
  DXT3: what dgVoodoo listed, minus DXT2/4/5; R9c added those back), sets the viewport and records the four state blocks on it (handles
  1..4, which `render::state::Block` knows by their addresses as before).
- `src/game/device_null.cpp`: the two stand-ins in the device object's +0x0C / +0x54. They draw nothing; the
  IDirect3DDevice7 one keeps the render and stage states (Direct3D 7 defaults at the start), feeds them to the mirror
  (`render_state.h`; a read of a state the rewritten code never fed gets what the game really set), records,
  applies, captures and deletes state blocks, answers GetCaps with fixed capabilities (8 stages, every texture
  operation, 4096 textures, no sort-independent antialiasing) and counts SetTexture references. The IDirectDraw7 one
  answers GetDisplayMode with the chosen mode in `device::ScreenFormat` (16 bits 5:6:5, else X8R8G8B8: what
  dx_surface.cpp's ReadScreenFormat reads; the windows' colour keys, CUiSurface +0x34, are 0 or a COLORREF set
  without the format, so they agree), GetCaps with 3D / texture / mipmap capabilities and no gamma or FourCC,
  GetAvailableVidMem 256 MB (dgVoodoo's card), and refuses CreateSurface (logged: a holder made through 004EFA60 then
  throws, as a failed DirectDraw call does). Every method logs its first call from each calling place
  (`old device: IDirect3DDevice7::SetRenderState from game.exe 0050B22E`, or `ei_hook.dll+offset`, at most 400
  lines): that log is R9b's list.
- 004EF610 ends the frame (`render::Present`) and returns 0: `EndFrame` draws the frame into a flip-model swap chain
  (FLIP_DISCARD, BGRA8, two buffers, `MakeWindowAssociation` without Alt+Enter / window changes) on the game window,
  sized to its client area (`ResizeBuffers` when it changes), stretched over it, `Present(1, 0)` (with the vertical
  blank). The movie players and the loading screen go through 004EF610 as before.
- Guarded for surfaceless holders: `device::SameSurface` (both without a surface: the same only when one holder) in
  005FDA80 (window blits: otherwise every window was skipped), the three movie players' "not the primary itself"
  checks and CUiSurface::CaptureScreen; 00504780 (restore after a lost device) returns at once; the surface capture
  006D6D40 (the Scroll Lock screenshot, the movie recorder) gives no bitmap for a holder without a surface (logged).
- Not touched (the stand-ins answer): app.cpp's FlipToGDISurface, app_main's GetAvailableVidMem / RestoreAllSurfaces,
  texture.cpp's video memory and largest texture, the cursor's WaitForVerticalBlank, 00504A60's gamma (no
  DDCAPS2_PRIMARYGAMMA: returns false, no ramp), the camp's antialiasing GetCaps.

What R9a leaves (R9c): full screen without a display mode change (the WS_POPUP window keeps the resolution's size at
0, 0; a resolution below the desktop's shows in the corner), the gamma ramp, lost Direct3D 11 devices (DXGI errors
are logged, nothing is recreated), the game's device re-creation on a resolution change (not exercised), Alt+Tab, the
Scroll Lock screenshot from the new frame, the movie recorder. When R9a was written the native config had not been
run (the worker could not run the game): Rebirth's patches (EI_Plugin) may call DirectDraw themselves, and the
stand-ins' log is the first thing to read.

### R9a2: the switch-over's scenarios run (done)

R9a had never run a game. What the five `present` scenarios (render_present, render_movie_present,
render_present_cpuwindows, render_titles_present, ui_saveload) hit, on the default suite and on `run_all --native`:
- 00458270's screen clears with no primary surface (app_frame.cpp: nothing to fill, result 0).
- The movie holders (bink_video.cpp `NewMovieHolder`, the players 005DB3F0 / 005DA400 / 005DBA50): 00784756 is
  written by the game start (00456E35) from GetAvailableVidMem, and the stand-in's 256 MB clears it, so the
  holders asked 004EFA60 for a video-memory surface, which the stand-in refused; the throw (no catch in the
  rewrite) ended in the game's error box (hang at step 0). Without DirectDraw the holder is 004EF7B0's (only the
  new renderer's memory); each Bink frame is copied into memory the renderer owns (`render::MovieFrame`) and
  presented through 004EF610 -> `render::Present` on the window's swap chain.
- Pictures loaded with the video-memory flag (004F0060, e.g. a 768x32 one at the game start) get the new
  renderer's memory as the system-memory ones do (dx_surface.cpp).
- The harness: windows are shown and made without activating them; dgVoodoo's exclusive cooperative level
  activated the game window anyway, without DirectDraw nothing did, so the game's active flag (0079BC5C) and the
  renderer's bit 0 stayed 0 and no frame was drawn ("no frame presented within 10 s"). The harness now sends one
  WM_ACTIVATE (active) once the device object exists, only without DirectDraw (harness.cpp
  `ActivateWithoutDirectDraw`). A player's own start is in the foreground and gets the activation from Windows.
- Verify wrappers whose originals need DirectDraw run ours only without it: 004F0060 (its original's
  CreateSurface is refused and it crashed), 005EC020 and 00655D40 (they write a texture lent through
  `memory_texture::Lend`, which now refuses at once: `memory_texture::CanLend`, logged once), and through Lend's
  refusal 005FFFB0 and 00656E10 as before.
- The swap chain sizes itself to the client rectangle each frame; the hook makes the process system DPI aware at
  WinMain (09ead405), so that rectangle is in physical pixels.

Captures (compare_frames, tolerance 8): the new frames show the same scenes as the old path's (frames_basic,
render_movie); the differences are what moves by itself (water, the global map's clouds, the clock, rain streaks:
weather is not in the new frame), about 6% for after_movie against render_movie's own new frame and 20% between
two `present` runs (vanilla / native: the water's phase). The old-device log of these runs (73 calling places,
no refusals left) is R9b's list: state setting, state blocks, BeginScene / EndScene / Clear / SetTexture from the
rewritten draws, the capability, display mode and memory queries, 0045715A's RestoreAllSurfaces.

Still R9c's: the loading screen and the movie players still reach the present through 004EF610 (fine: it only
ends the frame); full screen without a display mode change, gamma, lost Direct3D 11 devices, resolution changes,
Alt+Tab, the Scroll Lock screenshot and the movie recorder from the new frame.

### R9b: the rewritten code's calls into the old device guarded (done)

- `device_null.cpp`: the stand-ins' state (render / stage states, state blocks, bound textures, viewport, scene
  bracket, display mode) is a plain object, `SwitchOverDevice`; the two COM stand-ins only log and forward to it.
- `device.h`: `device::SetRenderState(d3d, ...)` and one call per method the rewritten code uses (GetCaps,
  EnumTextureFormats, Begin / EndScene, Clear, SetViewport, SetRenderTarget, Set / GetRenderState, Set /
  GetTextureStageState, Begin / End / Apply / DeleteStateBlock, SetTexture, DrawPrimitive, DrawIndexedPrimitive;
  IDirectDraw7 GetCaps, CreateSurface, GetDisplayMode, EnumDisplayModes, SetDisplayMode, RestoreDisplayMode,
  SetCooperativeLevel, GetAvailableVidMem, RestoreAllSurfaces, FlipToGDISurface, WaitForVerticalBlank). With a
  DirectDraw device each calls the interface given, as before. Without one, when the interface is the device
  object's stand-in (or null), it answers from `SwitchOverDevice` with the stand-in's results (states kept and
  fed to the mirror, blocks recorded and applied, fixed capabilities / formats / mode / 256 MB, CreateSurface
  refused and logged, draws / clears / mode changes / restores D3D_OK). Any other interface is still called: the
  verify wrapper of CRenderHelper (`verify_render_helper.cpp`) builds helpers on a recording device.
- Converted: every `->Method(` on the device in src/game (app, app_main, device_binds, device_draw, device_object,
  device_states, directdraw_holder, dx_surface, link_mesh_draw, render_helper, renderer_gamma / init / lifetime /
  lines / rect / scene / sprites / strip / triangles, texture, ui_camp, ui_camp_create, ui_cursor_frame,
  ui_hud_unit_draw, weather_particles, world_client_draw) and the vtable-slot helpers of the interface code: the
  interface's `UiD3DDevice` (ui.h) is now declared methods (`ui_d3d_device.cpp`: Clear, Get / Set render and stage
  states, ApplyStateBlock) used by ui_container_draws, ui_global_map_places, ui_help_window, ui_hud, ui_interface_draw,
  ui_objectives, ui_panel_draws, ui_trade_grid_draw (the per-file slot calls are gone). The new renderer's state
  reads (render.cpp `OldRenderStates` / `OldStageStates`) read `SwitchOverDevice` through `device_switchover.h`
  (no Direct3D 7 headers there).
- Verify wrappers whose originals ask the old device run ours only without DirectDraw: 004E6EE0 (BuildFormatTable:
  formats and ids not empty), 00504A60 (SetGamma: false, the three values unchanged), 006913A0 (SetDevice: device
  stored, a tenth of 256 MB); the state readers of verify_renderer_lines / state_setters / ui_hud / ui_hud_unit_draw
  go through the calls above.
- 0045E310 (`GetDxVersion`) still probes in `present` mode (its own DirectDraw object and primary surface, not the
  device object's). Tried and dropped: returning 0x800 without probing hung every native run in sound init (after
  `CSoundManager::Close`, before `Open`): the probe's side effects (DirectMusic through CoCreateInstance, DDRAW.DLL
  left loaded) matter to the sound code there. The probe fails now and then in native runs with several games on the
  machine, ending the run at the "DirectX 7 or later" message; seen before R9b too (follow-up R9b1). Since PF3
  (below) native runs skip it; the sound hang did not come back.
- `tools/old_device_calls.py [run folders]` lists the stand-ins' log by calling function (default: every
  `build/verify/runs/*present*`). The four present scenarios, vanilla and native: 73 calling places in 46 functions
  before, 4 in 1 after: WinMain's original (00456440: GetAvailableVidMem at 00456DA2 / 00456DC6 / 00456E0D,
  RestoreAllSurfaces at 0045715A). The harness patches WinMain (the `CALL` at 00457595 for `freeze`), so the
  installer leaves the original in place in every harness run; the rewrite (`app_main.cpp`, converted) runs only
  in a launch without the harness.
- Not reached by these scenarios but converted: the draws (DrawPrimitive / DrawIndexedPrimitive of renderer_lines,
  rect, scene, sprites, strip, triangles, device_draw, weather_particles; skipped already when the old path is off),
  the mode changes and restores, the cursor thread's WaitForVerticalBlank.
- Left on the stand-ins: code of game.exe that is not rewritten or not installed (WinMain in harness runs; in a
  Rebirth launch the Rebirth-patched originals the rewrites leave in place, e.g. 006462A0, 0060C870).

### R9c: everything the player sees through the new renderer's present (done)

- Rebirth's crash in `present` (NullDirect3DDevice7::SetTexture with surface 0x7463 from CUiMesh::Draw) and the
  CBattleScreenSetup::Create verify mismatch (2 objects of 3) had one cause: the stand-in's texture formats left out
  DXT2/4/5, and Rebirth's battle00 (1536x1536) and footprints are DXT5. ParseMmpHeader found no format, the texture
  load failed, Rebirth's DisableExitOnCrashReport made FatalError carry on, and the battle screen kept a garbage
  texture pointer. The list (`device_null.cpp` kTextureFormats) now has DXT1-5 as dgVoodoo did. Nothing for R9b2.
- `src/render/render_window.cpp` (moved out of render.cpp; render.cpp keeps `EndFrame` and gives the module its
  device, context and frame through `render_internal.h`): the game window's swap chain. The frame is drawn by a
  full-screen triangle (shader "ei-present") into the client area with its aspect kept, centred, black bars
  (`render::FrameInWindow`, `WindowToFrame`, `FrameToWindow`). A failed `ResizeBuffers` (other than a lost device)
  makes the swap chain again once. A lost device (removed, reset, hung, driver error from Present or ResizeBuffers)
  is logged with `GetDeviceRemovedReason` and the window shows nothing more; nothing is recreated (follow-up).
- Gamma as a shader step: 00504A60 (`renderer_gamma.cpp` SetGamma) without DirectDraw keeps the three values,
  builds the ramp as before (BuildRamp, full scale 65280) and hands it to `render::SetGammaRamp`: a 256x1 lookup
  texture the present shader reads per channel (each entry v*255/65280, rounded). Result true, as with a primary that
  has DDCAPS2_PRIMARYGAMMA. The ramp applies only to what is presented, not to the frame (captures, ReadFrame), as
  the hardware ramp did not change surface reads. Verify build: after a ramp change the presented back buffer is read
  back at a 17x17 grid (one-to-one placement only) and compared with the frame through the ramp ("render: gamma ramp
  check: N points ..." / "render: MISMATCH gamma ramp"). The verify wrapper (`verify_renderer_gamma.cpp`) without
  DirectDraw: result true, the values kept, the ramp never decreasing, the identity for 0, 0, 0.
- Full screen (device flags_ bit 0, registry FullScreen 1) without a display mode change (`present_window.cpp`,
  called from each present in device_present.cpp): the WS_POPUP window is moved over the monitor it is on (not while
  minimised; again after someone else resized it, e.g. a display mode change), and the frame is scaled into it.
  Windowed, a window resized by the user scales the frame the same way.
- The game's mouse coordinates when the frame does not fill the window one to one: GetCursorPos / SetCursorPos (the
  cursor 005E52E0, the global map 0062F050 take screen coordinates as a window at 0, 0 the resolution's size) through
  replaced import slots; WM_MOUSEMOVE .. WM_MBUTTONDBLCLK (client) and WM_MOUSEWHEEL (screen) mapped at the top of the
  input dispatcher (00457970, app.cpp). The game's own wheel message (0xCC54) carries no position. One to one at 0, 0
  nothing changes.
- Focus loss and return (Alt+Tab, minimise): the WM_ACTIVATE path is the game's own (app.cpp OnActivate): inactive
  deactivates the device (004EEC60, RestoreDisplayMode on the stand-in), the sound and the network and minimises;
  active restores, and in full screen 00504780 (returns at once without DirectDraw) and the interface reset 005DAE70
  (cursor rebuilt). Minimised the client area is empty and nothing is presented; on return the swap chain is resized
  to the restored window. DXGI_STATUS_OCCLUDED counts as success.
- Movies and the loading screen already reach `render::Present` through 004EF610 (R9a2). The software cursor:
  the cursor thread (005E5690) draws nothing without DirectDraw; the renderer draws the cursor in each frame, and the
  movie players and 005DA400 hide it, so it does not move during movies and loading. Drawing it between frames needs
  a present from another thread (follow-up).
- The harness: `fullscreen 0|1` (starter: the registry's FullScreen), `focus off|on` (WM_ACTIVATE inactive through
  the game's window procedure and SW_SHOWMINNOACTIVE; SW_SHOWNOACTIVATE and WA_CLICKACTIVE), `windowsize <w> <h>`,
  `waitpresent <seconds> [gamma]` (a frame presented on the game window; with gamma, checked against a ramp that is
  not the identity). The mouse steps aim at frame pixels mapped into the window.
- Scenarios: render_present_window (windowed 800x600: the options' gamma slider held, the ramp checked; the window
  resized to 1600x900 and the options reached through the mapped clicks) and render_present_fullscreen (full screen
  at 800x600 on the desktop's monitor: the menus and the battle screen through the mapping, focus lost and given
  back, the window resized from outside and covering the monitor again). Both set a resolution, so run_all runs
  them alone.

### PF3: no DirectDraw probes at startup in native runs (R9b1)

User's decision (2026-10-14): only native runs (EI_RENDER "present" on the system's DirectDraw, run/native) and the
standalone exe change; vanilla and Rebirth runs keep the original's probes and verify against them.
- 0045E310 (`app_dxversion.cpp` GetDxVersion): in a native run (`render::Presenting()` and the DDRAW.DLL the game
  imports is not in game.exe's folder, i.e. no dgVoodoo) it probes nothing and returns 0x800 (what the probe gives on
  current Windows); "app: native run: DirectX 7 taken as given, the version probe (0045E310) skipped" in ei_hook.log.
  Elsewhere it probes as before. The verify wrapper (ours only) accepts 0x800.
- The driver choice needs nothing: in `present` mode RendererCreate (00500B70) already lists no drivers and calls
  neither 004EE5D0 (FirstHardwareDirectDrawDriver) nor 004EEA90 (CreateDirectDraw), so a native run now makes no
  DirectDraw object at all. Under dgVoodoo (vanilla, Rebirth) all four DirectDrawCreateEx stay (~0.6 s).
- Time (EI_PROFILE=1, ui_main_menu, native): WinMain to AIL_startup 562 ms before, 57-68 ms after (three runs); the
  menu shows about 1.5 s after WinMain. The probe's own DirectDrawCreate alone was ~140 ms, its DirectMusic part ~50 ms.
- R9b1: the baseline native suite (77 runs, 4 at once) ended 8+ runs at the "DirectX 7 or later" box; with the
  skip none. R9b's "every native run hangs in sound init without the probe" did not reproduce: three native suites
  without the probe, one with 6 timeouts at step 0 in AIL_waveOutOpen (four scenarios at the same time, two of them
  passing on retry; the machine was shared with other workers' games), two with none. A timed-out harness run now
  logs the main thread's instruction pointer and return addresses (module+offset, `LogMainThreadStack` in
  harness.cpp), so the next such hang shows where it waits.

### Before R9a (reference)

The old device existed in the switch-over: the DirectDraw object (004EDD70), primary surface, render target and
depth buffer (004E6C30), the Direct3D device with its four state blocks and viewport (004E5E50), its texture format
list (004E6EE0, read by FindFormat 004E7DA0 and ParseMmpHeader 004E7C40) and dgVoodoo's swap chain under them; the old
Flip / BltFast presented. The plan to present from our own swap chain on the game window (1, 2 and 4 are R9a's,
above; 3 is R9b):
1. Device setup in the switch-over (004EDD70, 004E5E50, 004E68A0, 004E6C30, 004EE*; 00500B70 is rewritten): no
   DirectDraw object, display mode, primary, render target, depth buffer or Direct3D device; the device object filled
   for the game (screen rectangle +0x24, the render target holder's width / height / bits at +0x18 without a surface:
   `GameTargetSize` and the screen copies read them; 2DintMMX's converter +0x4C still loaded), the texture formats
   FindFormat answers (5650, 5551, 4444, 8888, DXT1 / DXT3 as a fixed table instead of 004E6EE0), the four state block
   handles as tokens the mirror knows. Most of these are in 0x004E0000-0x00500000: coordinate before rewriting.
2. The screen format without DirectDraw: from the chosen colour depth (16: 5:6:5, 32: X8R8G8B8). Check first where the
   windows' colour keys (CUiSurface +0x34) are computed so they agree.
3. Every old-device call left guarded for a device object without a device. At the last count 524 call sites in 143
   functions, 83 functions with DirectDraw / Direct3D calls not rewritten (the dead line draw 0049B320 left): the
   device setup and texture manager (004E5E50, 004E68A0, 004EE*, 0068F860..00693C60 minus the rewritten ones; 00690260
   and 0068FA10 remain), the holder helpers 004EFA60 / 004EFEA0 (R7's texture holders: save / load pictures, weather
   texture, item pictures), and interface draws 0060C870 (camp Build, 6.5 KB, Rebirth-patched: its only device use is
   GetCaps and, with sort-independent antialiasing, SetRenderState(ANTIALIAS, 2) at its end), 00627C90, 00628F80,
   00630670, 006462A0 (Rebirth-patched), 00658E10. The rewritten draws still set states, bind textures (004EB300 /
   004EBAF0 / 00501AF0) and call 004EB550 for the hand-over even when the old draw is skipped; the pass state then only
   comes from the mirror (the documented exception above).
4. `render::Present` presents a flip-model swap chain on the game window instead of 004EF610's copy and Flip / BltFast;
   the movie players and loading screen present through it; full screen / borderless and resolution changes (the game's
   device re-creation) resize it; the gamma ramp (IDirectDrawGammaControl) becomes a shader step; lost-device /
   WM_ACTIVATE handling in 004EF610's place. The present's full-frame readback (33 MB at 3840x2160, staging copy kept
   between frames) is fine for the switch-over only.
5. The cursor between frames: the hardware cursor needs nothing; the software cursor option draws only inside frames in
   the switch-over, so it does not move during movies and loading.

Other open items: the fog colour and range come from the old device state (not game data yet); weather's old draw
(`CRainSnow::DrawParticles`, split from the builders by A-S13) still runs next to the hand-over; GPU shadows; several steps (holders, saved pictures, interface
draws, movies, textures) are verified by wrappers and harness scenarios, never by a person in game.

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

- **In game, per feature:** at one frozen moment capture the old frame with less and less of it drawn by the old path
  (`oldobjects off`, `oldterrain off`, `oldinterface off`) and the new frame (always all of it); masks from pairs of old
  frames pick the pixels of one feature (`--mask-background` keeps those with nothing in front, `--exclude` leaves out
  another feature's). Scenarios `render_scene` (its header has the compare commands for the whole frame, terrain,
  objects, interface) and `render_terrain`; `tools/frame_preview.py` writes a downscaled side-by-side PNG. Known
  differences: translucent dark panels (35% black: top and portrait panel) dim the old scene under them, so compare
  masked pixels only; the light strip at the top of the new frame is the far sea's edge (clear colour), covered by the
  interface's top bar in the old frame; terrain still differs on steep distant slopes (mip choice under minification).
  Fog matches the old path. Cross-run comparisons are not meaningful (no determinism); A/B is always within one run.
  Direct3D 7 pixel centres: a clip -0.5..0.5 square covers pixels 16..47 of 64; a screen-pixel mesh from 8 to 24 covers
  8..23.

### Frame capture

Harness, verify build. `capture <name>` skips two presented frames and writes the next to `frames\<name>.bmp` (24-bit)
from the old render target at the present point (after scene and interface, before the software cursor and the flip;
a read-only Lock of the render target, GetDC as fallback: a GDI capture of the window comes back black with dgVoodoo);
with the new renderer on, its frame of the same moment goes to `frames\<name>.new.bmp`. `freeze` / `unfreeze` hold
timeGetTime / GetTickCount and the camera (CCamera::Update at 00457595 runs once per frame even with dt 0 and moves a
fixed fraction per call); the main loop then runs no world, client or camera update. `samecapture <a> <b> [tolerance] [fraction]` fails the run when two captures differ
(frames_basic and render_terrain: two captures in one frozen moment must match). `tools/compare_frames.py A B [--mask M1
M2 [--mask-background]] [--exclude E1 E2 ...] [--exclude-grow N] [--diff OUT]`: per-pixel tolerance (default 8),
differing fraction (default limit 0.1%), worst regions. Limits: what moves by itself between frames without the game's
clocks (other threads) is not held; the harness's own waits use real time.

### A-E1: line lists (line_batch.cpp)

`LineBatch` (renderer_lines.h, 0x20 bytes): points four to a packet (x[4], y[4], z[4], w[4]) in an aligned
array, a count and a colour. `AddPoint` (0049B140) appends a point with w 1 and repeats it through the rest of
its packet, growing the array by one packet when full. `Draw` (0049B780) resizes the two work arrays in the
exe's data (the projected points 007A9CB8 without keeping the contents, the outcodes 007A9CC8), projects with
the top of the pipeline's clip stack through the transform object's `ProjectClipped` and, when that returns 0,
hands the points to `DeviceDrawLineStrip` (0049B4B0); it never touches the Direct3D device itself. Returns 1.
Users: the figure parts' debug bounds (005A7290) and the world client's line lists (00569D60); no scenario
turns either on, so neither wrapper fires (AddPoint side by side on a copy, Draw ours only).

## 12. A-S11: what is left of the old renderer's range (0x004C0000-0x00500000)

173 game functions of the range were not rewritten when A-S11 started. Every one is classified below; this is
the list the renderer work starts from. The plain game code of the range (text manager, script loader,
`CSysInfo`, the fog manager) is in script.md (A-S11).

| Class | Count | Rewritten | Left |
| --- | --- | --- | --- |
| (a) plain game code | 39 | 18 | 21 (static initialisers and what only they reach, one MFC TRY/CATCH) |
| (b) renderer code through the renderer's own interfaces, or pure | 67 | 52 | 15 |
| (c) calls DirectDraw / Direct3D 7 interfaces directly | 28 | 0 | 28 (forwarded on purpose) |
| (d) library / compiler-generated (`config/library.csv`) | 25 | - | - |
| (e) dead (`config/dead_code.csv`) | 6 | - | - |
| under 5 bytes (cannot be hooked) | 8 | - | 8 |

The counts are A-S11's. A-S12 then rewrote 13 more of class (b) and 6 of class (c) (below); the tables list
what is still left.

### (c) Direct calls into DirectDraw / Direct3D 7: left forwarded

The interface names are read off the vtable offsets of the indirect calls (and the imports); the object behind
each call was not traced for the larger functions, so treat a method name there as "this slot of the interface
the function plainly works on", to be confirmed when the function is replaced.

| Address | Bytes | What | Interface calls |
| --- | --- | --- | --- |
| 004E4240 | 2623 | rain draw (from 004E5680) | IDirect3DDevice7::SetTexture, DrawIndexedPrimitive, DrawIndexedPrimitiveVB; the device lock |
| 004E4C80 | 2525 | snow draw (from 004E5680) | the same |
| 004E5E50 | 2530 | the device object's constructor (renderer creation 00500B70) | IDirect3D7 (device enumeration with callback 004ED820, CreateDevice), IDirect3DDevice7::SetViewport, BeginStateBlock / EndStateBlock (4 blocks), SetTextureStageState (24) |
| 004E68A0 | 767 | the device object's destructor body | IDirect3DDevice7::DeleteStateBlock (4), Release |
| 004E6C30 | 673 | the depth buffer (format from `FindDepthFormat`, surface from 004EFA60) | IDirectDrawSurface7::AddAttachedSurface |
| 004E6EE0 | 952 | the texture format table | IDirect3DDevice7::EnumTextureFormats |
| 004E7840 | 506 | the vertex-buffer batch flushed, variant 1 (8 callers) | IDirect3DVertexBuffer7::Unlock, IDirect3DDevice7::DrawIndexedPrimitiveVB; the device lock |
| 004E7A40 | 506 | the same, variant 2 (6 callers) | the same |
| 004EB1D0 | 301 | texture bound by name (from 006CEDE0) | IDirect3DDevice7::SetTexture |
| 004EB300 | 99 | texture bound to a stage | IDirect3DDevice7::SetTexture |
| 004EBAF0 | 180 | texture pair bound (from 004EB550) | IDirect3DDevice7::SetTexture |
| 004EDD70 | 1380 | the DirectDraw holder's constructor (`2DintMMX.dll`, `GetGraphicsProcessor`; its tail is 004EE2F8) | IDirectDraw7 slots 0x08 (Release), 0x18, 0x2C, 0x54; LoadLibraryA / GetProcAddress |
| 004EE330 | 353 | the DirectDraw holder's destructor body | IDirectDraw7::RestoreDisplayMode, Release; FreeLibrary |
| 004EE4A0 | 105 | the driver list filled | DirectDrawEnumerateExA (callback 004ED610) |
| 004EE5D0 | 253 | a driver's capabilities | DirectDrawCreateEx, IDirectDraw7::GetCaps, Release |
| 004EEA90 | 298 | a driver's display modes | DirectDrawCreateEx, IDirectDraw7::EnumDisplayModes |
| 004EEBC0 | 155 | display mode set (from 00504780) | IDirectDraw7::SetDisplayMode |
| 004EEC60 | 140 | display mode restored (from 00457D40) | IDirectDraw7::RestoreDisplayMode |
| 004EECF0 | 310 | cooperative level | IDirectDraw7::SetCooperativeLevel |
| 004EEE30 | 1756 | the primary and back surfaces | IDirectDraw7::CreateSurface (through 004EFA60), IDirectDrawSurface7::GetAttachedSurface, GetSurfaceDesc |
| 004EFA60 | 504 | a surface created (7 callers, the interface among them) | IDirectDraw7::CreateSurface, IDirectDrawSurface7::GetSurfaceDesc |
| 004EFEA0 | 137 | a surface's description | IDirectDrawSurface7::GetSurfaceDesc |

### (c) below 004F0000: rewritten by A-S13

The 22 rows of the table above below 004F0000 and the two callbacks of the (b) table (004ED610, 004ED820) are
rewritten. DirectDraw 7 / Direct3D 7 are called as the original calls them, through the typed interfaces
(`<ddraw.h>`, `<d3d.h>`; the casts are in `device.h`); each method was read off its vtable offset. The COM objects
are the boundary the new renderer replaces. Left of class (c): the six rows from 004F3C60 on (A-S12).

- `device_binds.cpp`: the texture binds 004EB300 / 004EBAF0 / 004EB1D0 (SetTexture when the stage's holder
  changes, the batch flushed first) and the batch flushes 004E7840 / 004E7A40 (Unlock,
  DrawIndexedPrimitiveVB, the buffer moved to the used list).
- `directdraw_holder.cpp`: the driver list (004EE4A0 with DirectDrawEnumerateExA's callback 004ED610,
  004EE5D0 the first hardware driver) and the device object's DirectDraw base: constructor 004EDD70 (CPUID
  into 007CAED0, `2DintMMX.dll`, the parts below), 004EEA90 (DirectDrawCreateEx, the display modes),
  004EECF0, 004EEBC0, 004EEC60, 004EEE30 (primary surface, clipper or back buffers, the video formats probed),
  destructor body 004EE330. `dx_surface.cpp`: a holder from a description 004EFA60 and its size 004EFEA0.
  `dx_errors.h`: the game's error object thrown through the game's `_CxxThrowException` (006E7819).
- `device_object.cpp`: constructor 004E5E50 (the base, IDirect3D7 from the DirectDraw object, EnumDevices with
  004ED820, CreateDevice for the HAL device only, the format table, EnumZBufferFormats, the depth buffer, render
  target and viewport, the four state blocks of stage 0 at 007AEDD0..DC), 004E6EE0 (EnumTextureFormats into
  the list and the id table), 004E6C30 (the depth format by 004E6BC0, the depth holder attached to the render
  target, a second one of one pixel), destructor body 004E68A0.
- `weather_particles.cpp`: the rain draw 004E4240 and the snow draw 004E4C80 (`CRainSnow`, effects.h; its
  work arrays are named now). 81 x 81 cells around the point the wind carries a particle to the camera from;
  a cell's hash and the world's time give the particle's cycle, fall and texture frame; corners in packets
  through `ProjectClipped` (stack 2), the ones on screen through the transform object's slot 32 (`ToScreen`:
  the perspective divide and viewport, read from `3DfpFPU.dll`) and, with the device's fog flag, slot 35
  (`FogVertices`), then DrawIndexedPrimitive. Snow sways by five tables of cubics (007ABB98 .. 007AE398, built
  by the static initialiser 004E3B20 through 004E3980; evaluated with 004E5660).

Verification (`verify_device_binds.cpp`, `verify_directdraw_holder.cpp`, `verify_device_object.cpp`,
`verify_weather_particles.cpp`; every scenario starts the device, 152 runs with 0 mismatches):

- Side by side: 004EB300, 004EB1D0 (ground prints), 004EBAF0 (no scenario reaches it: only the multi-pass
  material draw's stage whose texture is still to be found by name), 004EE4A0, 004ED610, 004EE5D0, 004EEA90
  (the original into a scratch object), 004EFEA0, 004EFA60 (a second surface into a scratch holder for the first
  48 descriptions), 004ED820, 004E6EE0, and the two weather draws: on every other call ours builds the
  vertices, then the original builds them again and draws; corners, frames, outcodes and vertices compared
  bit for bit (rain in 69 runs, as the world's weather starts it; snow in rebirth_flags, `SetWeather`).
- Ours only, with the state left checked: 004E7840, 004E7A40 (no vertex buffer is ever locked, so only their
  first test runs), 004EDD70, 004EECF0, 004EEE30, 004EE330, 004EEBC0 (a full-screen mode change after a switch
  back: no scenario), 004EEC60, 004E5E50, 004E6C30, 004E68A0, and the draw half of the weather draws (the other
  calls). The original cannot run next to ours there (one cooperative level, primary surface, Direct3D device,
  depth buffer). For those the device object after start-up was compared between a run with ours and a run
  with the 17 originals (their addresses added to `build/verify/disabled.txt` for that run): the 39
  `device-state:` lines of ei_hook.log (`verify_device_state.cpp`: flags, holders, display mode and driver
  lists, formats, the id table, batches, state blocks) were the same.
- The constructors call their parts through the addresses (`originals_render.h`: 004EDD70, 004EEA90, 004EECF0,
  004EEE30, 004E6EE0, 004E6C30, 004EFEA0, 004EFA60), so each wrapper stays in the path; 004E5E50 passes the
  enumeration callback as 004ED820 for the same reason. The standalone build calls the members directly.

Original details kept: GetAttachedSurface's reference to the back buffer is never released; 004EFA60 describes
the surface twice; the depth buffer's W-buffer choice is overwritten with 1; the T&L HAL device is never used;
the rain's and snow's vertex colours are never written (the buffer's old bytes are drawn, the fog pass only
lowers the specular's alpha) and neither are the packets' w; the rain's height uses the snow's rise (007AEDA0,
both 40). Different in ours: a throw from the constructors does not free what was built (the game ends in
WinMain either way); the driver record's unset GUID and monitor bytes are zeroed.

### (b) Through the renderer's own interfaces or pure: left

| Address | Bytes | What | Why left |
| --- | --- | --- | --- |
| 004ED610 | 358 | DirectDrawEnumerateExA's callback: a driver appended to the list | goes with 004EE4A0 |
| 004ED820 | 517 | the device enumeration callback (from 004E5E50) | goes with 004E5E50 |

### Rewritten by A-S11 (class b)

- `pixel_formats.cpp`: the channel layout of a pixel format (004EDAA0 `Split`, 004EDC00 `ToNative`, 004EDC70
  `FromNative`) and the device's format lookups (004E7DA0 by layout, 004E7C40 from an MMP header, 004E6BC0 the
  depth format). All side by side (`verify_pixel_formats.cpp`; the two per-pixel converters sampled); every
  texture load reaches them.
- `device_helpers.cpp` (30): counted holders (004E6BA0, 004EBBB0, 004EBBD0, 004EBDF0, 004EBE10, 004FAED0,
  004FAEF0, 004FDF70 release; 004EBE30, 004EBE60, 004EBE80 assign; 004C17E0, 004D9E60, 004D9E70 engine object
  pointers; 004FDED0 a COM pointer), list iterator copies (004EBCA0, 004EC1A0, 004F0510, 004F0590, 004F05A0),
  aligned blocks (004ECB50, 004ECB70, 004EC790 `AlignedArray<TLVertex>::SetSize`, 004D9EB0), the device's
  vertex-buffer batch (004E7820 returns null: the batch is never locked; 004E7E00 appends indices and keeps the
  original's loop that copies one index more than asked), deleting destructors (004EF790 `CDXSurface`, 004E7800
  the counted base 0073D768, 004F6A00 the loader base) and the axis swap matrix 004F1330 (Rebirth WideScreen
  patches it: vanilla launch only; `renderer_init.cpp` keeps its forwarder). Side by side: 004E7820, 004F1330;
  the rest ours only (`verify_device_helpers.cpp` names the scenarios: any scene).
- `render_helper.cpp` (16): `CRenderHelper`'s table slots 2, 4, 5, 6, 7 (004FDB00, 004FDB50, 004FDBA0, 004FDBC0,
  004FDD80; `records_` is a hash_map from the preset to {its states, its state block}, `presets_` the nine
  built-in ones), `CRenderPreset`'s destructor (004FD8B0, 004FD880), the deleting destructors of the device
  object (004E6880), the DirectDraw holder (004EE310), the helper (004FCC20), the two abstract bases (004FCD30,
  004FD900) and the pipeline (004F0DF0), `CTransformPipeline`'s destructor body 004F0E10 (`library_` at +0x10
  is `3DfpFPU.dll`'s module: FreeLibrary, then the 16 stacks' blocks from the last to the first) and its matrix
  stack constructor 004F0DB0 (16 matrices, aligned), and the driver list's lookup 004EE6D0 (`DriverAt`: a
  negative index walks backwards through the end node as the original's loop does; the end node is the fatal
  error "Can't find driver by index."). Side by side: 004FDB50, 004FDBA0, 004FDBC0 (into a scratch vector),
  004EE6D0 for an index inside the list; the rest ours only (`verify_render_helper.cpp`).

### Rewritten by A-S12

Direct3D calls are ported as they are (`<d3d.h>` interfaces). All side by side unless said otherwise.

- `link_mesh_draw.cpp`: CLinkMesh slots 7-14 (004F3870, 004F3AD0, 004F3C60, 004F3FA0, 004F4150, 004F42D0,
  004F4680, 004F4850): flat colours, lighting, the four draws, the shadow's projection and rasteriser
  (`verify_link_mesh_draw.cpp`; models, units, map_objects). No scenario reaches the two textured draws
  004F4150 and 004F42D0 (their wrappers are in place); 004F3C60 only in models.
- `render_helper.cpp`: `CRenderHelper`'s built-in state lists 004FCE90, constructor 004FCA60, destructor
  004FCD50, slot 1 004FD2A0 (state blocks recorded) and slot 3 004FD9D0 (released). Compared on a recording
  fake `IDirect3DDevice7` in three capability configurations at every start; on the real device ours only (a
  state block cannot be recorded twice). `CTransformPipeline`'s constructor 004F0B90: the original builds a
  second pipeline in a scratch block (`GetTLPipeline` returns the library's one object). Its failure throws the
  game's `GameError*` (`GameThrowException`, originals_engine.h) after doing the original's unwinding by hand.
- `effects_weather_draw.cpp`: the visibility fog's draw 004E1EB0 (static work arrays at 007ABA30 / 007ABA40,
  their exit handlers stay in the exe; spells_hero reaches it with cells) and the rain / snow buffers 004E3C20.
- `device_bounds.cpp`: the mesh draws' screen bounds 004E86B0 and 004E8900 (the second works in blocks of
  four); every mesh drawn with flag 1, both with and without outcodes.
- `link_loader_create.cpp`: `CLinkFigureLoader`'s constructor 004F68C0, the vanilla body, installed in the
  vanilla launch only (ResOverride patches it); the original's loader is built, summed up and destroyed first.

Original bugs kept: the helper's destructor walks on from a record it has just erased (it reads freed nodes,
so its walk over more than one record is ours only); slot 1 tests a texture operation against the
capability bits with `&` (operation number, not its bit). Not reproduced: fields the original leaves as stack
contents (unset entries of a state list are zero here; the containers' padding bytes in the figure loader).

Forwarders kept so the verify wrappers run: 004F0B90 (`renderer_base_create.cpp`), 004E86B0 / 004E8900
(`device_mesh.cpp`), 004E1EB0 (`vis_fog_manager.cpp`), 004E3C20 (`world_client_maps.cpp`), 004FCA60
(`renderer_init.cpp`), 004F68C0 (`world_client_setup.cpp`, Rebirth-patched). Could be unified later: the rain
/ snow work arrays' element types (bytes in effects.h, typed views in `effects_weather_draw.cpp`) and
`device_draw.cpp`'s `Technique` with `CRenderPreset`.

### (d), (e) and the rest

- Library rows (25 in the range, all noted "A-S11"): 004D3650, 004D3670, 004D6310, 004E2B60, 004E2E10, 004E77D0,
  004EBBF0, 004EBD30, 004EBEB0, 004EBF10, 004EC390, 004EC8A0, 004ECF60, 004ED780, 004EE2F8 (not a function: the
  common exit of 004EDD70), 004F04C0, 004F06A0, 004F0A60, 004F69F0, 004F7A80, 004FB680, 004FCC40, 004FD8A0,
  004FD920, 004FDFE0. One more outside the range: 0049F7B1 (not a function: the tail of `IsaacRandom::Seed`).
- Dead rows (6): 004DABC0, 004E7FB0 and 004EE760 have no reference anywhere (rel32 and dword scans of the exe
  and of Rebirth's binaries); 004F0A20, 004F0A90 and 004F0AE0 are called only by 004EE760. The display mode
  callback 004ED7F0 is named only by 004EE760 too (Ghidra made no function of it).
- Under 5 bytes: 004C17F0, 004D6B20, 004D6B30, 004D6B40, 004F0B00 (the DirectDraw error reporter: a bare RET),
  004F6A20, 004FD870, 004FE940.
