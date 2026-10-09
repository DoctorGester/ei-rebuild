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
| `present` | the switch-over: everything handed over is off in the old path, `cpuwindows` on, and at the old present (004EF610) the new frame is copied into the game's render target which the old Flip / BltFast shows; captures write the new frame as `frames\<name>.bmp` |

Without `new` nothing in `src/render` runs. Harness steps `oldterrain`, `oldobjects`, `oldinterface` (on / off)
switch the old path's parts during a run. `present` copies into the old render target and `show` / `split` use a
child window because dgVoodoo owns a flip-model DXGI swap chain on the game HWND (a second one on that window fails),
and under dgVoodoo's full screen a child window's swap chain made the game window inactive (WM_ACTIVATE inactive: the
frame function then draws nothing and the window minimises itself), so `show` / `split` work windowed only.
Presenting from our own swap chain needs dgVoodoo gone (section 9).

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
| Weather | rain / snow quads built by 004E4240 / 004E4C80 (2.6 KB each, not rewritten) into pre-transformed vertices and drawn with the device directly | 004E5680 rewritten (`weather_draw.cpp`): after the draw its triangles go to `TlDraw`; the old draw stays | drawn; builders not split |
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
the game code yet"), with a count every 20000 passes. The verify build also reads every fed state back and logs
disagreements ("fed X but the old device has Y": a writer not converted); the device's value is drawn then. States the
game never sets (ZFUNC, TEXTUREFACTOR, ADDRESSU / V, stage 1 off) start with the device's initial values.
**Documented exception:** stage 0's mip filter, which the unit panel's draw 006462A0 turns off around the unit's
picture (Rebirth replaces that whole function, so it cannot feed the mirror until rewritten with that patch): the
device's value is drawn in the verify build, the mirror's (linear mips) in the release build.

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
00655D40, which ends at 0065663C; a false vtable entry 0074718C). With cpuwindows Build (00653DD0) and FillEntries give
each texture's holder memory (`GivePictureMemory` -> `AttachTextureMemory`, CTexture +0x3C); outside the switch-over the
DirectDraw texture stays and the fills still Lock it and copy the 0x18000-byte block into the memory before Unlock.
Verify: ShowPicture and FillEntries run the original first on the real window then ours (safe twice); SaveGame ours
only. Scenario ui_saveload (`new,present`). Kept: the 0x18000 bytes are one block (pitch taken as 512); Locks use flags
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
temporary DirectDraw texture lent to the holder and taken back (00691D80's wrapper lends every new texture while
005EC020 / 00655D40's original runs). If DirectDraw refuses it ("verify: no DirectDraw texture ... ours only") 005FFFB0
/ 00656E10 run ours only and 005EC020 / 00655D40 would crash in their original (not expected). Scenarios:
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

## 9. What is left

The old device still exists in the switch-over: the DirectDraw object (004EDD70), primary surface, render target and
depth buffer (004E6C30), the Direct3D device with its four state blocks and viewport (004E5E50), its texture format
list (004E6EE0, read by FindFormat 004E7DA0 and ParseMmpHeader 004E7C40) and dgVoodoo's swap chain under them; the old
Flip / BltFast still presents. To present from our own swap chain on the game window (dgVoodoo gone, no DirectDraw
device at all):
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

Other open items: the fog colour and range come from the old device state (not game data yet); weather's two builders
(004E4240 / 004E4C80) are not split from their draws; GPU shadows; several steps (holders, saved pictures, interface
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
timeGetTime / GetTickCount and the camera (CCamera::Update runs once per frame even with dt 0, 00457595, and parts of it
move a fixed fraction per call; that was an 8.4% difference between two frozen captures); the main loop then runs no
world, client or camera update. `samecapture <a> <b> [tolerance] [fraction]` fails the run when two captures differ
(frames_basic and render_terrain: two captures in one frozen moment must match). `tools/compare_frames.py A B [--mask M1
M2 [--mask-background]] [--exclude E1 E2 ...] [--exclude-grow N] [--diff OUT]`: per-pixel tolerance (default 8),
differing fraction (default limit 0.1%), worst regions. Limits: what moves by itself between frames without the game's
clocks (other threads) is not held; the harness's own waits use real time.
