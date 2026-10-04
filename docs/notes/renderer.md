# Renderer: the DirectX boundary, the two plugin DLLs, and the plan for a modern backend

This note inventories everything game.exe 1.06 does with DirectDraw 7 / Direct3D 7 (and DirectInput,
Bink and the two Nival plugin DLLs), says what game logic reads back from the renderer, proposes how
to rebuild the renderer as a modern backend behind the same boundary, and describes the frame
capture the harness now has for verifying it (`capture <name>`, `tools/compare_frames.py`,
`tests/scenarios/frames_basic.txt`).

Everything here is static analysis: Ghidra (the GUI project through its HTTP server, plus headless
imports of game.exe, 3dfpfpu.dll and 2dintmmx.dll with every vtable slot made a function), and a
capstone scan of every function in `config/functions.csv` for stdcall vtable calls (`this` pushed
last, the COM convention) with the pushed arguments resolved where they are constants. No game was
run. Counts are lower bounds where an object pointer flows through a parameter or a stack slot the
scan could not follow; a runtime trace (work item R1 below) will close them.

## Summary

- The boundary is narrow and old-fashioned. game.exe does its own transform, clipping, lighting and
  fog (through 3DfpFPU.dll) and hands Direct3D 7 only **pre-transformed, pre-lit screen-space
  vertices**: FVF 0x1C4 (XYZRHW | DIFFUSE | SPECULAR | TEX1) and 0xC4 (no texture), triangle lists,
  a few strips and line strips. No D3D transform, lighting, material, clip plane or fixed-function
  vertex processing is used (`D3DRENDERSTATE_LIGHTING`/`CLIPPING` are set to 0).
- Statically: **524 DirectDraw/Direct3D call sites in 143 functions** (26 of those functions are
  already rewritten), plus 18 DirectInput sites in 5 functions, 12 Bink imports, and one
  `DirectSoundEnumerateA` (the Miles device list). Methods used (counting Release and AddRef):
  IDirectDraw7 12 (one only in dead code), IDirectDrawSurface7 17 (one dead), IDirect3D7 5 (one
  dead), IDirect3DDevice7 21 (one dead), IDirectDrawGammaControl 1 (plus QueryInterface and Release
  on the surface), IDirectDrawClipper and IDirect3DVertexBuffer7 only in dead code, IDirectInput7A
  (CreateDeviceEx) and IDirectInputDevice7A (5, the mouse only).
- **3DfpFPU.dll** (`GetTLPipeline`, a static object with a 45-slot vtable): 54 functions, **56.7 KB of
  pure x87 code** (no MMX/SSE; Intel-compiler style with branch-free min/max built from FNSTSW
  masks): projection with outcodes, homogeneous triangle clipping, viewport mapping, per-vertex
  lighting into packed colours, vertex fog into the specular alpha, bounding volumes.
  **2DintMMX.dll** (`GetGraphicsProcessor`, 13 slots): 18 functions, **9.6 KB of MMX code**: 8888 to
  565/555/1555/4444 conversion, colour blending for composed skins, alpha premultiply, 2x2 mip
  downsampling, two triangle rasterisers and a blur for the CPU-drawn unit shadows. The rest of each
  DLL (151 KB and 94 KB files) is statically linked MFC/CRT. Both DLLs are byte-identical in the
  GOG and Rebirth installs.
- **Game logic reads the renderer back**: unit picking under the cursor tests screen rectangles that
  are computed from the T&L-projected vertices during the draw; client-side animation and figure
  changes are gated by the view-frustum test; the escape menu and the save game's picture are copied
  out of the render target (the picture is stored in the save file); the frame rate (Flip with
  vsync) changes how many world updates run per frame. The T&L math therefore has to stay
  bit-exact; the rasterisation does not.
- Proposed layering: **exact layer** (game code + rewritten 3DfpFPU/2DintMMX, CPU only, verified bit
  for bit against the DLLs in process) over a **D3D7 emulation layer** (our own COM objects returned
  from an intercepted `DirectDrawCreateEx`, CPU-side DirectDraw surfaces, a D3D7 state machine) over
  an **RHI**. Recommended RHI: **sokol_gfx** (D3D11 first, GL 3.3 / Metal later), with SDL3 used only
  for window/input/audio in the later standalone exe; sokol_app is not a good fit. Rough size of the
  renderer work: 16-20 person-weeks plus 4-6 for the exact T&L/2D layer (breakdown below).
- Frame capture: `capture <name>` reads the render target in process at the present point (after
  the interface is drawn, before the software cursor and the flip) through a read-only Lock and
  writes `frames\<name>.bmp`; `freeze`/`unfreeze` hold the game's clocks so frames are stable;
  `tools/compare_frames.py` compares with a per-pixel tolerance.

## 1. The import boundary

| Import | Called from | Purpose |
|---|---|---|
| `DDRAW!DirectDrawEnumerateExA` (thunk 006E616C) | 004EE4A0 | driver list 007AEE64 (flags 5: attached secondary devices); "Can't enumerate direct draw drivers" |
| `DDRAW!DirectDrawCreateEx` (thunk 006E6172, IAT 0073B03C) | 004EE5D0 (each driver: GetCaps, keeps the first with 3D hardware), 004EE760 (display modes and devices of one driver, for the options screen), 004EEA90 (the device object's IDirectDraw7), and one site at 004EE531 | always `IID_IDirectDraw7` (00745C90) |
| `DINPUT!DirectInputCreateEx` (IAT 0073B044) | 0065EAD0 | version 0x700, `IID_IDirectInput7A`; the mouse only |
| `DSOUND!#2` (`DirectSoundEnumerateA`) | 006B6A30 | the sound device list handed to Miles |
| `binkw32` (12 functions) | 005DA3C6-005DBF7B | movies, see section 6 |
| `LoadLibrary("2DintMMX.dll")` + `GetGraphicsProcessor` | 00500540 (renderer +0xC), 004EDD70 (device object +0x4C) | same static object twice; both demand MMX (CPUID), "MMX required in this version" |
| `LoadLibrary("3DfpFPU.dll")` + `GetTLPipeline` | 004F0B90 (CTransformLibrary +0xC, created by 00500540) | the T&L pipeline |

0045E310 also loads DDRAW.DLL, DINPUT.DLL and D3D8.DLL by name and probes them with
`DirectDrawCreate`/QueryInterface chains (`IID_IDirectDraw2`, ...): the DirectX SDK's
version-check sample, informational only.

## 2. Objects in game.exe

- **Renderer** 007AF060 (vtable 0073D93C, 26 slots; `CVisualizator` in its error strings): +8 the
  CTransformLibrary (vtable 0073D778, 0x94 bytes: matrix stacks, +0x10 the DLL module, +0xC the
  3DfpFPU pipeline), +0xC the 2DintMMX graphics processor (007AF06C), +0x10 its module, +0x14 the
  clear colour, +0x20 the device object (007AF080), +0x24 the effect manager (004FCA60, vtable
  0073D840: Direct3D state blocks per material technique), +0x34.. frame-time statistics.
  00500960 returns it.
- **Device object** (0x17C bytes, vtable 0073D760; constructor 004E5E50, which first runs the
  DirectDraw base 004EDD70, vtable 0073D770). +8 the last HRESULT (every call stores it), +0xC
  IDirectDraw7, +0x10 the driver GUID, +0x14 the primary surface, +0x18 the render target, +0x1C the
  clipper (dead), +0x20 the cooperative flags (8 window, 0x11 full screen), +0x24..+0x30 the client
  rectangle, +0x34 the window, +0x38 the display-mode list, +0x3C / +0x40 the YUV FourCCs the driver
  accepts for off-screen / overlay surfaces (probed at start-up, not used further), +0x44 overlay
  support, +0x48 / +0x4C the 2DintMMX module and processor, +0x50 IDirect3D7, +0x54
  IDirect3DDevice7, +0x58 the z-buffer surface, +0x60 the ZENABLE value (forced to 1, the W-buffer
  test is overwritten), +0x7C the device GUID, +0xAC the CTransformLibrary again, +0xB4 / +0xB8 the
  z-buffer and texture format lists, +0xD8 the vertex buffer (never set), +0x12C the textures bound
  per stage, +0x14C the current material, +0x150 the override colour of mesh draws, +0x15C the screen
  bounds of the last mesh draw, +0x16C / +0x170 / +0x174 vertex, triangle and call counters, +0x178
  texture changes.
- **Surface wrapper** (`CDXDirectDrawSurface`, 0x20 bytes, vtable 0073D774, counted): +0xC the device
  object, +0x10 the IDirectDrawSurface7, +0x14 / +0x18 width / height, +0x1C bits per pixel; 007AEE94
  counts them. Constructors: 004EFA60 (CreateSurface from a DDSURFACEDESC2), 004EF7B0 (system-memory
  off-screen plain surface in the primary's format), 004EF690 (wrap an existing surface), 004F0060
  (create and fill from an 8888 picture through 2DintMMX).
- **Textures**: CTexture / CTextureManager (007AF744; `texture*.cpp`), surfaces at texture +0x5C.
- **UI surfaces**: CUiSurface (0x38 bytes, vtable 00740F28, `ui.h`), an off-screen DirectDraw
  surface per window drawn with GDI (GetDC) and pixel code, blitted onto the render target.
- **Device lock**: the critical section 007AEE78 is held around nearly every DirectDraw/Direct3D call
  because a second thread (the software cursor, 005E5690) blits too.

## 3. Start-up and display setup

1. 004EE4A0 enumerates drivers; 00500B70 picks settings +0x2C (an index) or the first driver whose
   DirectDraw caps report 3D hardware (004EE5D0: `DDCAPS_3D` and two more caps bits).
2. 004EDD70 (DirectDraw part): `DirectDrawCreateEx`; `SetCooperativeLevel(hwnd, flags | 0xC00)` with
   flags `DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE` (0x11) or `DDSCL_NORMAL` (8) per the FullScreen option;
   **0xC00 = DDSCL_MULTITHREADED | DDSCL_FPUSETUP**: FPUSETUP is why the main thread runs at 24-bit
   x87 precision (control word 007F, see docs/notes/rebirth.md), which game logic depends on; full
   screen: `SetDisplayMode(w, h, bpp, refresh, 0)` from VideoResolution / VideoColorDepthInBits /
   VideoRefreshRate; `GetCaps`; six FourCC probes (`CreateSurface` 100x100 YUY2, UYVY, ... in video
   memory and as overlays).
3. 004EEE30 creates the surfaces. Full screen: a primary with `DDSCAPS_PRIMARYSURFACE | FLIP |
   COMPLEX (| 3DDEVICE)` and one back buffer (`GetAttachedSurface(DDSCAPS_BACKBUFFER)`) as render
   target. Window: a primary (0x200) and an off-screen render target of the client size in video
   memory (`OFFSCREENPLAIN | VIDEOMEMORY | 3DDEVICE`, the primary's pixel format; "Back buffer is not
   in video memory" otherwise).
4. 004E5E50 (Direct3D part): `QueryInterface(IID_IDirect3D7)`, `EnumDevices` (004ED820 collects
   them), the **HAL** device preferred over TnLHAL (the game does its own T&L), the render depth
   checked against the device's `dwDeviceRenderBitDepth`; `CreateDevice`; `EnumTextureFormats`
   (004E6EE0) and `EnumZBufferFormats`; the z-buffer created and attached (004E6C30,
   `AddAttachedSurface`, depth from VideoZBufferDepthInBits); `SetRenderTarget`, `SetViewport`
   (client rectangle, z 0..1); four state blocks recorded with `Begin/EndStateBlock` (handles
   007AEDDC, 007AEDD8, 007AEDD4, 007AEDD0; stage 0 only):

   | Block | Colour | Alpha |
   |---|---|---|
   | 007AEDDC | TEXTURE x CURRENT | TEXTURE |
   | 007AEDD8 | TEXTURE x DIFFUSE | TEXTURE x DIFFUSE |
   | 007AEDD4 | TEXTURE x DIFFUSE | TEXTURE |
   | 007AEDD0 | TEXTURE x DIFFUSE | DIFFUSE |

5. 00500B70 (rewritten, `renderer_init.cpp`): `GetCaps` of the device and of DirectDraw, the render
   states and texture filters below, the T&L pipeline's viewport, projection and fog range.
6. Display modes: the list the options screen offers comes from `EnumDisplayModes` (callback
   004ED7F0) of each driver (004EE760, and 004EEA90 for the device object's +0x38), keeping the
   modes whose depth the chosen device renders (16, 24, 32 against `dwDeviceRenderBitDepth`); the
   game sets the stored VideoResolution / VideoColorDepthInBits as they are. In a window the client
   rectangle decides the render target's size.
7. Mode changes and lost surfaces: 00504780 (`SetCooperativeLevel`, `SetDisplayMode`,
   `RestoreAllSurfaces`, `Restore` of primary, render target and z-buffer, textures reloaded by
   00693F60); 004EEC60 / 004EE330 `RestoreDisplayMode`; FatalError (004560D0) and 00455980 call
   `FlipToGDISurface` before a message box in full screen. Gamma (00504A60): with
   `DDCAPS2_PRIMARYGAMMA`, `QueryInterface(IID_IDirectDrawGammaControl)` on the primary and
   `SetGammaRamp` from the Brightness / Contrast / Gamma options.

## 4. Interfaces and methods used

Slot numbers are vtable indices. "Sites" lists functions (rewritten ones are named).

### IDirectDraw7

| Slot | Method | Sites |
|---|---|---|
| 0 | QueryInterface (IID_IDirect3D7) | 004E5E50, 004EE760 |
| 2 | Release | 004EE330, 004EE5D0, 004EE760 |
| 4 | CreateClipper | 004EF510 (no callers: dead) |
| 6 | CreateSurface | 004EDD70 (FourCC probes), 004EF7B0, 004EFA60, 004F0060 |
| 8 | EnumDisplayModes | 004EE760, 004EEA90 |
| 10 | FlipToGDISurface | 00455980, 004560D0 |
| 11 | GetCaps | 004EDD70, 004EE5D0, 00500B70 (`renderer_init.cpp`), 00504A60 |
| 19 | RestoreDisplayMode | 004EE330, 004EEC60 |
| 20 | SetCooperativeLevel | 004EECF0 |
| 21 | SetDisplayMode | 004EDD70, 004EEBC0 |
| 23 | GetAvailableVidMem | 00456440 (three queries), 006913A0 (texture budget) |
| 25 | RestoreAllSurfaces | 004563A0, 00456440, 00504780 |

### IDirectDrawSurface7

| Slot | Method | Use |
|---|---|---|
| 0 | QueryInterface | gamma control on the primary (00504A60) |
| 1 / 2 | AddRef / Release | wrappers 004EF690, 004EFD20, 004EEE30 |
| 3 | AddAttachedSurface | z-buffer to render target (004E6C30) |
| 5 | Blt | UI surfaces onto the render target (005FDA80: `DDBLT_WAIT | DDBLT_ASYNC`, with `DDBLT_KEYSRCOVERRIDE` for colour-keyed windows), screen captures from the render target (005FD3D0 `CaptureScreen`, rewritten), stretch into a smaller copy (005FD440 `Rgb565Copy`, rewritten), colour fills (`DDBLT_COLORFILL`: 00458270, 005FCC00/005FCDD0 surface creation, 005DB9E0), movie frames to the primary (005DA5AB, 005DA880, 005DB603, 005DBD80; stretched for letterboxing), the software cursor (005E54A0, 005E5640, 005E5690, 005E6630), mip levels (00533980), one depth value from the z-buffer into a 1-pixel surface (004EAFD0, dead: see section 9) |
| 7 | BltFast | the window-mode present: render target to the primary at the client origin, `DDBLTFAST_WAIT` (004EF610) |
| 11 | Flip | the full-screen present, `Flip(backbuffer, 0)` (004EF610) |
| 12 | GetAttachedSurface | the back buffer (004EEE30), mip chains (00533980, 00690970, 00690B30, 00690D30) |
| 17 / 26 | GetDC / ReleaseDC | GDI text on UI surfaces (005FDC00 / 005FDC60) |
| 21 | GetPixelFormat | 004EEE30, 004EF7B0, 004F0060, 0068F860, 006D6D40 |
| 22 | GetSurfaceDesc | wrappers, UI surfaces, cursor, texture loading (15 functions) |
| 25 / 32 | Lock / Unlock | UI pixel work (`ui_surface_pixels.cpp`), cursor images and blits (005E4EB0, `ui_cursor.cpp` 005E5C40), texture upload (0068FBF0, 00690970-00690FC0, `texture_mmp.cpp`), item figure textures (`ui_item_view.cpp` 005EC020), global map (0062F050), save list pictures (00655550, 00655D40, 00656E10), movie frames, the movie-capture screenshot (006D6D40), the dead depth reader 004EAFD0 |
| 27 | Restore | 00504780 |
| 28 | SetClipper | 004EF510 (dead) |
| 45 | SetPriority (100) | texture surfaces (00690430 `texture_mmp.cpp`, 00691B00, 00691BF0, 00693C60) |

Lock flags: 0 (no wait; item textures), `DDLOCK_WAIT` (1; UI surfaces, cursor),
`WAIT | READONLY` (0x11; screenshots), `WAIT | WRITEONLY` (0x21; movies, picture uploads),
`WAIT | WRITEONLY | NOSYSLOCK` (0x821; texture uploads). Blt flags: `DDBLT_WAIT` (0x1000000),
`| DDBLT_ASYNC` (0x1000200), `| DDBLT_COLORFILL` (0x1000400), `| DDBLT_KEYSRCOVERRIDE` (0x1010200).
DDBLTFX is used only for the fill colour and the source colour key.

Surface kinds created: the primary (with a one-buffer flip chain in full screen), the render target
and z-buffer, off-screen plain surfaces in video memory (`0x4040`) or system memory (`0x840`) in the
primary's format (UI windows, movie frames, pictures, the cursor's save-under), and textures: a
system-memory staging surface (`TEXTURE | SYSTEMMEMORY`, a mip chain with `COMPLEX | MIPMAP` when
mipmapping is on) copied by `IDirect3DDevice7::Load` into a managed texture (`TEXTURE | ALLOCONLOAD`,
`DDSCAPS2_TEXTUREMANAGE` plus hint bits). Texture formats come from the .mmp header (0x8888, 0x4444,
0x5551, 0x5650 and DXT variants) matched against `EnumTextureFormats` (004E7DA0); 8888 falls back to
4444. UI and render target surfaces have the display format: 16-bit 5:6:5 or 5:5:5, or 32-bit
X8R8G8B8.

### IDirect3D7

| Slot | Method | Sites |
|---|---|---|
| 2 | Release | 004E68A0 |
| 3 | EnumDevices | 004E5E50, 004EE760 |
| 4 | CreateDevice | 004E5E50 |
| 5 | CreateVertexBuffer | 004E7700 (constructor reached only from 004E72A0, which has no callers: dead) |
| 6 | EnumZBufferFormats | 004E5E50 |

### IDirect3DDevice7

| Slot | Method | Sites and arguments |
|---|---|---|
| 2 | Release | 004E68A0 |
| 3 | GetCaps | 00500B70, 0060C870, 006913A0, 004FCA60 (effect manager) |
| 4 | EnumTextureFormats | 004E6EE0 |
| 5 / 6 | BeginScene / EndScene | 00503F40 (on D3DERR_SCENE_IN_SCENE: flush, EndScene, BeginScene), 005040A0 |
| 8 | SetRenderTarget | 004E5E50 |
| 10 | Clear | 00501A40 (frame: TARGET + ZBUFFER (+ STENCIL), renderer +0x14, z 1.0); hero / item / face previews clear depth (+ stencil) in a rectangle: 005EC300 (`ui_hero_preview.cpp`), 00602BF0, 00622420, 00627390, 00627C90, 00628F80, 00632A90, 00636020 (`ui_battle_setup.cpp`), 006462A0 |
| 13 | SetViewport | 004E5E50 |
| 20 / 21 | SetRenderState / GetRenderState | see below |
| 22 / 23 | BeginStateBlock / EndStateBlock | 004E5E50 (four blocks), 004FD2A0 (effect techniques) |
| 25 | DrawPrimitive | line strips, FVF 0xC4 (0049B140, 0049B4B0, 0050434F, 005045D0); a textured quad as triangle strip, FVF 0x1C4, 4 vertices (00504860) |
| 26 | DrawIndexedPrimitive | triangle lists, FVF 0x1C4: 004EB550 / 004EB370 (`device_draw.cpp`, every mesh), 00502A20 (`renderer_strip.cpp`), 00503680 (`renderer_sprites.cpp`), 00501BB0 (renderer slot 12: terrain), 004E4240, 004E4C80; FVF 0xC4: 0050434F |
| 32 | DrawIndexedPrimitiveVB | 004E7840 / 004E7A40 (flushes; the vertex buffer is never created, see above) |
| 35 | SetTexture | 004EB1D0, 004EB300, 004EB370 / 004EB550 (`device_draw.cpp`), 004EBAF0, 00501AF0, 0050434F |
| 36 / 37 | Get/SetTextureStageState | see below |
| 39 | ApplyStateBlock | 004EB550 (`device_draw.cpp`), 00569E40, 0056AFE0, 00600EF0 |
| 41 | DeleteStateBlock | 004E68A0, 004FD9D0 |
| 43 | Load | texture upload 00690430 (`texture_mmp.cpp`), 0068FBF0; shadow texture 0051CE60 (`map_object_shadow.cpp`) |

Not used: Set/GetTransform, SetLight, LightEnable, SetMaterial, MultiplyTransform, ValidateDevice,
Capture/CreateStateBlock, PreLoad, ComputeSphereVisibility, clip planes, strided draws.

Render states (constant values seen; "var" = from a variable or the effect tables):

| State | Values (sites) |
|---|---|
| 2 ANTIALIAS | 0 / 2 SORTINDEPENDENT (00500B70 from the Antialiasing option, 0060C870, 0060E6B0) |
| 4 TEXTUREPERSPECTIVE | option (00500B70) |
| 7 ZENABLE | 1 (device +0x60), 0 / 1 around 2D layers (00569E40, 005E69C0) |
| 8 FILLMODE | 3 SOLID, 2 WIREFRAME (00569970, a debug toggle) |
| 9 SHADEMODE | 2 GOURAUD, 1 FLAT around line strips (0049B140, 0049B4B0, 0050434F, 00630670) |
| 14 ZWRITEENABLE | 1 / 0 (00569E40, 0056AFE0: the scene pass) |
| 15 ALPHATESTENABLE | 1 / 0 (scene pass, interface 005F71C0 / 005F7350, HUD 0063C4F0, 0062EFC0) |
| 19 / 20 SRCBLEND / DESTBLEND | ONE / ZERO (init), SRCALPHA / INVSRCALPHA (most), SRCALPHA / ONE (additive: ribbons, sprites), material values (004EB550, 00501C60, 004FD2A0) |
| 22 CULLMODE | 2 CW (init), 1 NONE (00569E40) |
| 24 ALPHAREF | 200 (init, scene), 10 (scene), 0x80 (interface), 1 (HUD and windows) |
| 25 ALPHAFUNC | 5 GREATER |
| 26 DITHERENABLE | option, 1 (00630670) |
| 27 ALPHABLENDENABLE | 1 / 0 |
| 28 FOGENABLE | 1 (fog comes from the specular alpha the T&L writes) |
| 29 SPECULARENABLE | option; toggled around HUD, item views and terrain |
| 34 FOGCOLOR | 0, then the zone's colour (00501AB0, renderer slot) |
| 47 ZBIAS | 0 / 1 around debug lines (0050434F) |
| 136-139 CLIPPING, LIGHTING, EXTENTS, AMBIENT | 0 |
| 140 FOGVERTEXMODE | 3 LINEAR (irrelevant for pre-transformed vertices) |

Texture stage states: COLOROP / ALPHAOP 1 DISABLE and 4 MODULATE, 2 SELECTARG1; arguments 0
DIFFUSE, 1 CURRENT, 2 TEXTURE (state blocks above, line draws 0049B140 / 0049B4B0 / 0050434F /
005045D0, 004EB170); per material technique the effect manager (004FD2A0) records for each stage
COLOROP / ARG1 / ARG2 / ALPHAOP / ARG1 / ARG2 from its 0x28-byte stage records (a stage is skipped
when its required caps are missing), splits techniques with more stages than the device's
`wMaxSimultaneousTextures` into passes, and records SRCBLEND / DESTBLEND per extra pass; TEXCOORDINDEX
per stage and TEXTURETRANSFORMFLAGS = COUNT2 for generated coordinates (004EB550); MAG / MINFILTER
1 POINT or 2 LINEAR and MIPFILTER 1 NONE / 2 POINT (00500B70, from Filtering / Mipmapping);
MIPFILTER saved / forced to 1 / restored around the hero and face previews (005EC300, 006462A0).
The generic D3D7 stage combiner with up to `wMaxSimultaneousTextures` stages is therefore needed,
not just the four fixed blocks.

### Others

- **IDirectDrawGammaControl**: SetGammaRamp (00504A60).
- **IDirectDrawClipper**: SetHWnd, and the surface's SetClipper, only in 004EF510, which nothing
  calls; the window-mode present uses BltFast, which ignores clippers.
- **IDirect3DVertexBuffer7**: Unlock (004E7840 / 004E7A40) and Release (004E76B0), dead with the
  vertex buffer.
- **IDirectInput7A / IDirectInputDevice7A** (0065EAD0-0065ECB0, destructor 004578D0):
  `CreateDeviceEx(GUID_SysMouse, IID_IDirectInputDevice7A)`, `SetDataFormat(c_dfDIMouse)`,
  `SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)`, `Acquire`, `Unacquire`,
  `GetDeviceState` (DIMOUSESTATE). Keys and the cursor position come from window messages.

## 5. The frame and its presentation

The main loop (004571DC ff.) measures elapsed time with timeGetTime, runs up to five world updates
of 0078FF34 ms each, the client updates and the camera, the interface update (005F57C0), then the
frame function **00458010** once per iteration:

1. 00503F40: frame statistics, `BeginScene` (all counters reset).
2. 005F5220: the interface draws everything: the world view of the battle screen (00569E40 terrain
   and objects, 0056AFE0 the object passes, particles, ribbons, shadows), 3D interface models and
   sprites (TL triangles), and the windows' CUiSurfaces blitted onto the render target
   (005FDA80) in window order, interleaved with the 3D parts.
3. 005040A0: flushes, `EndScene`, the statistics text.
4. The escape menu's Draw (005F5320, Rebirth-patched) when requested.
5. When presenting: the cursor lock (005E65F0), the **software cursor** blitted into the render
   target (005E54A0; only with the display option's software cursor bit; otherwise Windows draws
   the cursor), **present** 004EF610 (full screen: `Flip`; window: `BltFast` of the render target
   to the primary), the cursor unlock (005E6610); with movie capturing on (0079BC54) the render
   target becomes `<dir>\sshot%4d.bmp` through 006D6D40 (CDib from a surface, read-only Lock) and
   006D5AC0. Movie capturing also replaces the elapsed time by 1000 / MovieCapturingFPS.

The software cursor's thread (005E5690, 41 ms per frame) blits straight to the primary under the
device lock when the cursor is in thread mode.

## 6. Movies (Bink)

`PlayMovie` 005DB3F0 (startup movies via 00458270 and config\movie.ini, the main menu's intro,
zone and briefing movies): `BinkOpen` (with Miles sound), an optional `DDBLT_COLORFILL` of the
primary and a Clear, one off-screen surface of the movie size, in video memory (0x4040) or, with
00784756 set, in system memory (004EF7B0), always in the primary's format; `BinkDDSurfaceType`
on that **IDirectDrawSurface7** (Bink queries its pixel format through the COM interface); then its
own PeekMessage loop: `BinkWait`, `BinkDoFrame`, `Lock(WAIT | WRITEONLY)`, `BinkCopyToBuffer(bink,
bits, pitch, height, 0, 0, type)`, `Unlock`, and a `Blt` of the frame **to the primary** (centred, or
stretched to the width and letterboxed when 0079BCBC == 2), `BinkNextFrame`; Esc / Space skip.
005DA880 is the same loop on a thread. `CBinkVideo` (005DBA50 open, 005DBD80 per frame) plays into
a surface during the interface draw; only the credits (0062B7D0) use it. Harness runs never play
movies.

## 7. The interface's 2D surfaces

Each interface window owns CUiSurfaces: off-screen plain surfaces in the display format, drawn
with GDI through the surface's DC (text: `CUiSurface::DrawWrappedText` 005FDFA0 and 005FDE20,
fonts from CreateFontIndirect), with direct pixel writes under Lock (`ui_surface_pixels.cpp`:
Greyscale, the 5:6:5 copy, contents saved and restored by `SwapContents`; the global map and the
save list's pictures) and with colour fills. Every frame each visible
surface is blitted onto the render target at its screen rectangle (005FDA80), optionally with a
source colour key (the surface's +0x34). `CaptureScreen` (005FD3D0) goes the other way: it Blts the
render target into a surface (the escape menu's greyed background, and through `Rgb565Copy` 005FD440
the 256x192 5:6:5 picture stored in the save game). The software cursor saves and restores the
pixels under itself (005E5C40 `CCursor::Blit`).

## 8. The plugin DLLs

Both are VC6-linked DLLs (Rich header: Utc12 builds 8168 / 8447, Linker 6.0, 2000-01-25) with MFC
and the CRT linked in; the Nival code is a small block at the start of `.text`. The exported factory
returns the address of a static object; its vtable is installed by a static constructor.

### 3DfpFPU.dll: the T&L pipeline (`GetTLPipeline` 1000F200 -> object 100204C0, vtable 1001F220)

Object state: +4 the sun (a counted light object; slot 2), +8 a work buffer, +0x1C..+0x24 the
ambient colour (floats), +0x2C / +0x30 the viewport half height / width, +0x34 / +0x38 the fog range
and +0x3C = 255 / (far - near), +0x40 the projection matrix (transposed). Vertex positions come in
"blocks" of four vertices (64 bytes: x x x x y y y y z z z z + pad), the layout the mesh code uses.

| Slot | Address | Bytes | RET | Role | Known callers |
|---|---|---|---|---|---|
| 0 | 1000F2B0 | 50 | 4 | deleting destructor | |
| 1 | 1000F1D0 | 35 | 8 | viewport size (w, h) -> half extents | 00500B70, 005015A0 |
| 2 | 10001F20 | 53 | 4 | set the sun light (counted) | 005A7A70 |
| 3 | 1000F1A0 | 35 | 0xC | ambient colour (r, g, b) | 00500740 |
| 4 | 1000F180 | 25 | 4 | projection matrix (64 bytes) | 00500B70, 005015A0 |
| 5 | 1000F150 | 37 | 8 | fog range (near, far) | 00500B70, 00566360, 005EDD80 |
| 6 | 10008270 | 562 | 0xC | bounding box and sphere of 16-byte points | (not found statically) |
| 7 | 10007B80 | 1764 | 0xC | bounds of vertex blocks | figure loaders 005A2500, 005A37C0, 005A3800, 005A4D60, 005A5C70, FIG loaders 004F7F00, 004F87D0 |
| 8 | 100063B0 | 2736 | 0x14 | blocks -> clip space + outcodes | 00501D90 (ribbons) |
| 9 | 10005AE0 | 2254 | 0x14 | blocks -> screen + outcodes | 004E9010, 004E9A50, 004F2650 |
| 10-12 | 10004F20.. | 2254-2994 | 0x14-0x18 | variants (two inputs, colours copied) | |
| 13 | 10003850 | 1204 | 0x10 | 16-byte points -> screen (no clipping) | 00501D90 |
| 14 | 10003220 | 1574 | 0x10 | blocks -> screen (no clipping) | 004E9010, 004E9A50, 004F2650 |
| 15-17 | 10002C20.. | 1522-1665 | 0x10-0x14 | blocks -> TL vertices (with colours) | |
| 18, 19 | 100072C0, 10006E70 | 2232, 1089 | 0x14, 0x10 | transform with / without outcodes | 004F29F0 |
| 20-23 | 100084B0.. | 1035-2254 | 0x10-0x14 | further transform variants | |
| 24 | 10009E40 | 1625 | 0x14 | sprites: points + size -> four corners + outcodes | 00502CA0 (`renderer_sprites.cpp`) |
| 25-28 | 1000A4A0.. | 859-1841 | 0x10-0x14 | transform variants | 0056C750 (26) |
| 29, 30 | 1000BD00, 1000BA00 | 1768, 765 | 0x14, 0x10 | 2D affine transform + bounding rectangle (float / integer) | 004F4680 (29) |
| 31, 32 | 1000C620, 1000C3F0 | 135 | 8 | perspective divide + viewport, 16 / 32-byte vertices | |
| 33, 34 | 1000C6B0, 1000C480 | 409 | 8 | the same for clipped vertices | 004E9010, 004E9A50, 00501D90, 006BFF40 (`terrain_draw.cpp`) (34) |
| 35 | 1000C850 | 280 | 8 | vertex fog: specular alpha = min(alpha, 255 - (z - near) * scale) | 004E4240, 004E4C80, 004E9010, 004E9A50, 00501BB0, 00502A20, 00502CA0 |
| 36 | 1000C970 | 549 | 0x14 | clip one triangle (homogeneous) | slot 37 |
| 37 | 1000CBB0 | 196 | 0x18 | clip an indexed triangle list | 004E9010, 004E9A50, 004EA4A0, 006BFF40 |
| 38 | 1000CC80 | 554 | 0x10 | interpolate two vertices (clipping) | |
| 39-44 | 1000CEB0.. | 698-2633 | 0x18-0x30 | per-vertex lighting (sun, point lights, ambient) into packed ARGB colours (39, 42 also speculars) | 005A7A70 (40) |

"Known callers" come from the static search; slots without callers may be unused by this game (the
library is generic) or called through pointers the search lost. The runtime trace (R1) settles it.

### 2DintMMX.dll: the graphics processor (`GetGraphicsProcessor` 100036E0 -> object 10012FA0, vtable 10012140)

| Slot | Address | Bytes | Role | Known callers |
|---|---|---|---|---|
| 0 | 10003700 | 50 | deleting destructor | |
| 1 | 10001000 | 542 | set a 16-bit target format (kind 0 opaque, 1 5:5:5, 2 1:5:5:5; masks, with fast paths for 5:6:5 / 5:5:5 / 1:5:5:5) | 004F0060 |
| 2 | 10001220 | 601 | convert a row of 8888 to the format (kinds 0-2, 3 = 4:4:4:4) | 004F0060, 00693180 (`texture_compose.cpp`) |
| 3, 4 | 10001490, 100015F0 | 348, 394 | blend a palette-indexed layer into 32-bit pixels with per-pixel weights (MMX, saturating) | (composition; not found statically) |
| 5 | 10001780 | 573 | alpha-over blend of 32-bit pixels, or the same over run-length (PNT3) data | 005EC020 (`ui_item_view.cpp`), 00693010, 005FFFB0 |
| 6 | 100019D0 | 220 | premultiply by alpha | 00693970 |
| 7 | 10001AB0 | 146 | 2x2 box downsample (mipmaps) | 00693180 |
| 8, 9 | 10001B50, 10001EB0 | 817, 2658 | triangle rasterisers into a 16-bit buffer (flat; with u, v) | 005A9310, 005A0E80 (`figure_rasterise.cpp`): the shadow textures |
| 10 | 10002930 | 2481 | 16-bit block operation (w, h, pitch) | 00690D30 |
| 11, 12 | 10003300, 10003400 | 249, 151 | vertical / horizontal 3-tap blur of the 4-bit alpha (x 0x5556 >> 16) | 0051CE60 (`map_object_shadow.cpp`) |

## 9. What game logic reads back from the renderer

1. **Picking**: `CObject3D` hit tests (0051C130: the model's screen rectangle at +0x100 when the
   in-view flag 0x20 is set) and the per-entry lists 00571800 / 005718B0 (a screen rectangle per
   entry, then the figure's per-piece test) use rectangles filled during the draw from the
   T&L-projected vertices (004E8900 / 004E86B0 into the device's +0x15C, copied by 005A7A70 into the
   node and figure pieces). Which unit is under the cursor, and so what a click does, depends on the
   projected screen positions.
2. **Visibility**: the frustum test 004F16A0 (game.exe code, rewritten in `model.cpp`) decides the
   in-view flag 0x20 in `CollectVisible` 0056A990, which also animates visible units and applies
   pending figure changes; the drawn flag 0x40 of meshes comes from the T&L result codes. Animation
   state on the client thus depends on the camera and the T&L outcodes.
3. **Bounds at load time**: figure loaders take their bounding boxes from T&L slot 7; the clip test
   and the picking use them.
4. **Pixels**: `CaptureScreen` / `Rgb565Copy` copy the render target into the escape menu and into
   the save game's 256x192 picture (written to the save file), and the movie-capture screenshots.
   Save files therefore contain rasteriser output.
5. **Timing**: the main loop runs up to five world updates per frame; `Flip` with vsync (dgVoodoo
   forces it) paces frames, so the renderer's speed changes how world updates group into frames
   (not their results); the frame statistics (frame time, num_verts, num_trias) go to the game's
   debug output object.
6. **Ground picking** (0056BF30 / 0056C350) inverts the view matrices in game code, not in the DLL;
   it depends on the matrix stacks, which stay in the exact layer anyway.

No game logic reads the depth buffer or the result of a Direct3D draw. A depth reader exists
(004EAFD0, a device method: Blt of one pixel from the z-buffer into a 1-pixel surface at device
+0x5C, read-only Lock, the value converted by depth width, -1.0 on failure), but nothing calls it or
points at it.

## 10. Proposed layering

```
 game code (rewritten or original)  ── calls ──>  COM: IDirectDraw7 / IDirectDrawSurface7 /
   + exact layer: T&L (3DfpFPU), 2D (2DintMMX)       IDirect3D7 / IDirect3DDevice7 (as today)
                                                          │
                     ┌────────────────────────────────────┴───────────────────┐
           original path (flag off)                                 new path (flag on)
           dgVoodoo DDraw.dll / D3DImm.dll                          D3D7 emulation layer (ours)
                                                                     │  CPU surfaces, state machine
                                                                     ▼
                                                                    RHI (sokol_gfx: D3D11 / GL / Metal)
```

1. **Exact layer (CPU, bit-exact)**: game code, plus flat rewrites of the 3DfpFPU slots the game
   uses and of 2DintMMX, installed by giving the game our objects (same vtable layout) instead of
   the DLLs' (hook 004F0B90's `GetProcAddress` result or overwrite CTransformLibrary +0xC and the
   two graphics-processor pointers after creation). Verified in process against the DLLs on the
   same inputs (both are pure apart from their small state).
2. **D3D7 emulation layer**: our implementations of the interfaces in section 4, returned when the
   hook DLL intercepts `DirectDrawCreateEx` / `DirectDrawEnumerateExA` (IAT 0073B03C / 0073B038),
   behind a flag so the dgVoodoo path stays available. It contains: surfaces as CPU memory in their
   exact DirectDraw format (Lock, GetDC through a DIB section, Blt / BltFast / colour fill / colour
   key / stretch done in software exactly like DirectDraw's documented behaviour), each with a GPU
   texture mirror uploaded when dirty; the render target and z-buffer as GPU images (CPU copies made
   only on Lock / Blt-from / capture); textures (staging surface + Load = format conversion to RGBA8
   and upload, mips); the render / texture stage state machine with state blocks; draws recorded per
   frame and replayed into the RHI; present (Flip / BltFast) to the window's swap chain; gamma as a
   final LUT pass. Bink keeps working because BinkCopyToBuffer writes into our CPU surface memory
   (our surface must answer `GetPixelFormat` / `GetSurfaceDesc` as Bink expects).
3. **RHI**: a few hundred lines on sokol_gfx: one uber-shader (TL vertex to clip space with
   perspective-correct interpolation through 1 / rhw, D3D7 pixel centres (+0.5), diffuse + specular,
   up to two (later N) texture stages with the D3D7 ops and arguments as uniforms, alpha test, fog
   from the specular alpha, colour key), a pipeline cache keyed by blend / depth / cull / primitive
   state, streaming vertex and index buffers, images for textures and surfaces, render-target
   readback.

Why not translate COM calls one-to-one into modern calls as they happen: D3D7 mixes CPU access
(Lock of the render target, GDI on surfaces, Blts between surfaces) with drawing; recording a frame
and replaying it lets the layer batch uploads before the render pass, order CPU blits correctly
against 3D draws, and capture or compare a frame as data.

## 11. What must stay exact

- All T&L outputs: projected positions, rhw, outcodes, clip results (vertex counts and order),
  bounds (slots 6, 7, 29), lighting colours and fog alphas; they feed picking, culling and the draw
  flags. This is x87 code: keep the operation order and the 24-bit precision (see rewrite-guide
  rule 7).
- The x87 control word: the new layer must leave the main thread at 007F after device creation as
  `DDSCL_FPUSETUP` does (and keep it there: save / restore around RHI and driver calls).
- The matrix stacks, the frustum test, the screen-bounds code (004E8900 / 004E86B0), the game-side
  screen clipper (0042D910, `clip.cpp`), the sprite sort: game code, already exact.
- Everything game logic sees through the COM interfaces: HRESULTs, surface descriptions (sizes,
  pitches, formats), lock pointers' contents for CPU surfaces, enumerations (driver, device,
  display modes, texture and z formats), caps bits the game tests (texture stages, filtering,
  mipmapping, gamma, render bit depths, video memory). Pick one fixed, plausible set.
- 2DintMMX output where it becomes texture or surface content: not logic, but cheap to keep exact
  (integer code), and it keeps texture caches (`textures\*.mmp`) byte-identical.

Free to differ (within tolerance): rasterisation, texture filtering, blending precision, dithering,
16-bit quantisation of the render target, gamma, MSAA. One exception to weigh: the save game's
picture is rasteriser output stored in the save file.

## 12. Backend options: SDL3 GPU vs sokol

What this game needs from an RHI: screen-space triangles (lists, strips, line strips) with
fixed-function-style colour combining; 16-bit textures and surfaces; many CPU-written 2D surfaces
uploaded per frame (UI windows, movie frames, cursor, shadow textures); alpha blend / alpha test /
vertex fog / z; reading the render target back to the CPU (captures, save picture, software cursor
save-under); running inside the original game.exe (the game owns the window, the message loop and
DirectInput) and later in a standalone exe; 32-bit MSVC builds with our CMake.

| | SDL3 (SDL_gpu + platform) | sokol (sokol_gfx + sokol_app) |
|---|---|---|
| Backends | Vulkan, Direct3D 12, Metal. No D3D11, no OpenGL, no WebGPU | Direct3D 11, OpenGL 3.3/4.x, GLES3 / WebGL2, Metal, WebGPU; Vulkan in progress |
| Fit for TL triangles, blend, alpha test, fog | fine (shader + immutable pipelines; LINESTRIP supported; wireframe fill mode available) | fine (shader + immutable pipelines; line strips supported; no wireframe fill mode, only a debug toggle needs it) |
| 16-bit textures | B5G6R5 / B5G5R5A1 / B4G4R4A4 formats exist (not on every backend / GPU); converting to RGBA8 is simpler | no packed 16-bit colour formats (5:6:5, 1:5:5:5, 4:4:4:4); convert to RGBA8 on upload (exact, cheap) |
| Per-frame CPU uploads | explicit transfer buffers + copy passes, any number per frame, but copies must sit outside render passes: record the frame first, upload, then draw (our layer records frames anyway) | `sg_update_image` once per image per frame (needs a small ring of images for surfaces blitted twice in a frame), `sg_append_buffer` for streaming vertices: maps directly onto D3D7 user-pointer draws |
| Render-target readback | portable (`SDL_DownloadFromGPUTexture` + fence) | no portable API: small per-backend code (D3D11 staging copy, glReadPixels, Metal blit) through sokol's native-handle queries |
| Shaders | per-backend bytecode (SPIR-V, DXIL, MSL): HLSL compiled offline with SDL_shadercross (DXC + SPIRV-Cross) | sokol-shdc (one prebuilt tool) turns annotated GLSL into a header with every backend's variant |
| Inside game.exe | `SDL_CreateWindowWithProperties` on the game's HWND; SDL subclasses the window procedure and wants its event pump, alongside the game's own loop and DirectInput: workable, but it is a second owner of the window | sokol_gfx takes an externally created device and swap chain: we create a D3D11 device + DXGI swap chain on the game's HWND (about 150 lines) and nothing else touches the window. sokol_app cannot be used here (it owns window and loop) |
| Standalone exe | SDL3 gives window, keyboard, relative mouse (replaces DirectInput), audio, timers, message boxes | sokol_app inverts control (init / frame / event callbacks; the game's main loop would have to become a frame callback), has a smaller feature set; sokol_audio is minimal. sokol_gfx pairs routinely with SDL3 for the platform part |
| x87 / 32-bit | both build as separate C targets (own flags, SSE inside is harmless); neither touches the FPU control word; D3D12 and Vulkan run in 32-bit processes (32-bit Vulkan drivers exist for the big vendors but are less exercised) | D3D11 and GL are the most exercised 32-bit paths |
| Comparison with dgVoodoo | dgVoodoo here outputs through D3D12 (dgVoodoo.conf `OutputAPI = d3d12_fl11_0`); both use the D3D10+ rasterisation rules | same rules; D3D11 is dgVoodoo's other usual output |
| Licence | zlib | zlib |
| Build integration | CMake project (add_subdirectory or prebuilt SDL3.dll); large | two headers and one .c file in our CMake target; trivial with MSVC x86 |
| Maturity / size | SDL_gpu new (2025) but maintained by the SDL team; large codebase | sokol_gfx stable for years, one maintainer, ~20k lines, easy to read and patch |

**Recommendation: sokol_gfx as the RHI, with its D3D11 backend first** (then GL 3.3 for Linux and
Metal or GL for macOS). It drops into our CMake/MSVC x86 build as two files, coexists with the
original game.exe without touching its window (we hand it a D3D11 device and swap chain on the
game's HWND), maps directly onto the game's immediate-mode user-pointer draws, and covers the widest
range of old hardware the players of a 2000 game have. Its gaps are small and on our side of the
fence: RGBA8 conversion for 16-bit formats (exact anyway), a once-per-frame image update rule (an
image ring), and render-target readback (one function per backend). For the standalone exe use
**SDL3 for window, input and audio only**, not sokol_app: the game's own loop and Win32 window code
map onto SDL3 directly, and SDL3 can host a sokol_gfx D3D11 or GL context. If one library for
everything is preferred, SDL3 alone is the fallback: portable readback and explicit uploads are its
strengths, but it brings Vulkan/D3D12 only, a heavier shader toolchain, and a second owner of the
game's window while we still run inside game.exe. Neither library is added yet (needs the user's
go-ahead).

## 13. Refactoring the rewritten draw code onto the backend

Where today's rewrites sit relative to the boundary (all call COM through raw vtable slots):

| File | Functions | Calls across the boundary |
|---|---|---|
| `device_draw.cpp` | 004EB550, 004EB370 | Set/GetRenderState, ApplyStateBlock, Set/GetTextureStageState, SetTexture, DrawIndexedPrimitive |
| `device_mesh.cpp` | 004E9010, 004E9A50 | T&L 14 / 9 / 37 / 34 / 35, then device_draw |
| `device_shadow.cpp` | 004EA4A0 | T&L (via 004F2650), 37 / 34, device_draw |
| `renderer_init.cpp` | 00500B70 | device and DirectDraw GetCaps, render states, stage states, T&L 1 / 4 / 5 |
| `renderer_strip.cpp` | 00501D90, 00502A20, 00502AC0, 00502B90 | T&L 13 / 8 / 34, render states, DrawIndexedPrimitive |
| `renderer_sprites.cpp` | 00502CA0, 00503680, 00503710, 005037D0 | T&L 24 / 35, game clipper, render states, DrawIndexedPrimitive |
| `terrain_draw.cpp` | 006BFF40 | T&L 37 / 34, renderer slot 12, SPECULARENABLE |
| `particle_draw.cpp`, `particle_render.cpp` | 00498940, 004976B0, ... | renderer slots only (slot 17 = 00502CA0) |
| `figure_part_draw.cpp`, `figure_part_draw_plain.cpp`, `figure_part_bounds.cpp` | 005A7A70, 005A7640, 005A7290 | frustum test, T&L 2 / 40, device mesh draws |
| `figure_rasterise.cpp`, `map_object_shadow.cpp` | 005A0E80, 0051CE60 | 2DintMMX 8 / 9 / 11 / 12, device Load, surface Lock |
| `ui_surface_pixels.cpp` | 005FD3D0, 005FD440, 005FD7A0, 005FE440 | surface Blt / Lock / Unlock / GetSurfaceDesc |
| `ui_cursor.cpp` | 005E5C40 | surface Lock / Unlock |
| `texture.cpp`, `texture_mmp.cpp`, `texture_compose.cpp` | 006922A0, 006926F0, 00690430, 00693180 | surface creation and Lock, device Load, SetPriority, 2DintMMX 2 / 7 |
| `ui_hero_preview.cpp`, `ui_item_view.cpp`, `ui_hud.cpp`, `ui_objectives.cpp`, `ui_battle_setup.cpp`, `ui_global_map.cpp` | previews, HUD, map | Clear, render states, MIPFILTER, surface Lock |

Plan:

1. **Keep them as they are.** They are behaviour-exact game code at the COM boundary; because the
   new layer implements the same interfaces, they run unchanged on both paths, and their verify
   wrappers keep working. No rewrite is wasted.
2. **Now (cheap): one typed header for the boundary.** Slot numbers and call helpers are duplicated
   with different names (`ComCall`, `VirtualCall`, `Com`, `ThisCall`, local `SetRenderState` in
   `ui_hud.cpp` and `ui_objectives.cpp`, ...). Add `src/game/d3d7.h` with named inline wrappers
   (`d3d7::SetRenderState(device, D3DRENDERSTATE_ZENABLE, 1)`, `ddraw::Lock(surface, ...)`,
   `tl::ProjectBlocks(pipeline, ...)`) and switch the files to it; this is a mechanical change with
   identical code generation (the verify build would show any slip).
3. **Standalone build (later):** the wrappers become direct calls into the emulation layer (our C++
   objects, no COM indirection); then, where it is worth it, draw code can talk to the RHI layer in
   its own terms (the TL vertex array and a draw state struct) instead of D3D7 states.
4. The T&L slot calls stay calls into the exact layer, whichever implementation (DLL or ours) is
   installed.

## 14. Verification plan

1. **Exact layer**: wrap the DLL objects' vtables in the verify build (they live in the DLLs'
   writable .data); for each call, run the DLL method on copies of the inputs and our method on the
   real ones, compare outputs bit for bit (the usual `ei::verify::Check` reports, counted in
   harness_report.txt). Run every scenario; add random-input fuzzing offline (the existing practice
   of comparing x87 sequences under an emulator at 24 and 53 bits applies).
2. **Frame comparison (implemented)**: `capture <name>` + `tools/compare_frames.py`. Uses: (a) the
   same scenario with hooks vs `--no-hooks` (rewritten game code vs original, same backend: expect
   identical frames where the scene is frozen at the same state); (b) dgVoodoo path vs new backend.
3. **Same-process A/B (tee)**: the decisive check for the backend. With both paths in one process
   the layer forwards each frame's recorded command stream to dgVoodoo's objects and replays it on
   the RHI into an off-screen target; the capture then compares the two images of the very same
   frame (same game state, same animation phase, same weather), so no freezing or determinism is
   needed. Tolerance per pixel (default 8 per channel) and a differing-fraction threshold; the worst
   regions point at the draw to inspect (the recorded stream can be bisected by draw call).
4. **Trace replay**: save a frame's command stream (with its textures and surfaces) to a file and
   replay it in a small offline tool against the RHI: regression tests without running the game.
5. **Reference configuration**: dgVoodoo as installed forces 8x MSAA, 16x anisotropic filtering,
   dithering and vsync (`dgVoodoo.conf`, [DirectX]); for reference frames use a run folder with
   its own conf set to app-driven antialiasing and filtering and dithering off (run\vanilla already
   carries its own copy of the conf; never edit the installs).

## 15. Work breakdown (one experienced developer)

| # | Item | Estimate |
|---|---|---|
| R1 | Intercept `DirectDrawCreateEx`/`EnumerateExA`; pass-through wrappers of all used interfaces around dgVoodoo's objects, with a trace (method, caller, arguments, formats) and the plugin-call profile; gives the measured inventory and the hook points for A/B | 1.5 weeks |
| R2 | CPU surface layer: formats, Lock/Unlock, GetDC via DIB sections, Blt/BltFast (fill, colour key, stretch), flip chain, descriptions and enumerations, lost-surface no-ops, Bink compatibility | 2 weeks |
| R3 | RHI layer on sokol_gfx: device and swap chain on the game's HWND, streaming buffers, uber-shader, pipeline cache, textures (Load, mips, format conversion), Clear with rectangles, viewport, state blocks, present | 3-4 weeks |
| R4 | Frame recording and replay; render target readback; interleaving of CPU blits with draws; UI composition | 1.5 weeks |
| R5 | Display modes, window / full screen (borderless), scaling, gamma LUT, alt-tab, the software cursor thread | 1 week |
| R6 | Verification: tee mode, per-draw bisection, trace replay tool, reference run folder, a set of frame scenarios (menus, zones, camp, map, spells, shadows, movies) | 2 weeks |
| R7 | Stabilisation across GPUs / drivers and both installs (Rebirth widescreen, draw radii) | 2 weeks |
| E1 | Exact 2DintMMX (13 slots, integer/MMX, C++ with exact saturation semantics) and its verify wrappers | 1-1.5 weeks |
| E2 | Exact 3DfpFPU: first the slots the game uses (about 20; x87 sequences transcribed with the operation order kept, some naked), then the rest if the trace shows them; verify wrappers and fuzzing | 3-5 weeks |
| D1 | Typed boundary header and the switch of the rewritten files (section 13) | 0.5 week |

Renderer (R1-R7): about 13-14 weeks of work plus 2-6 weeks of slack for surprises (16-20); exact
layer (E1, E2): 4-6.5 weeks; D1 can go first. Outside the renderer, for the standalone exe: GDI text
and MFC CDC on surfaces (own glyph rasteriser or pre-rendered fonts, 2-3 weeks), Bink 1 decoding
without binkw32 (FFmpeg's decoder or our own, 1-2 weeks), window/input through SDL3 (1 week),
Miles audio (separate).

## 16. Frame capture (harness, verify build)

Steps (tests/verify/harness.cpp, harness_capture.cpp):

- `capture <name>`: skips two presented frames, then writes the next one to
  `<output folder>\frames\<name>.bmp` (24-bit, bottom-up, lossless) and lists it under "frames
  captured" in harness_report.txt; fails if no frame is presented within 10 s or the surface cannot
  be read. Names: letters, digits, `_`, `-`, `.`.
- `freeze` / `unfreeze`: hold the game's timeGetTime and GetTickCount (IAT 0073B6A0 / 0073B344) at
  their current values. The main loop then sees no elapsed time: no world, client or camera update,
  and nothing the game animates from these clocks moves (interface fades, tooltips, frame timer,
  the interpolation fraction), while every loop still draws and presents. Unfreezing resumes both
  clocks from where they stopped. The hooks are installed only when the scenario contains `freeze`;
  such a scenario also gets the fixed date of deterministic mode (the main menu's picture follows the
  time of day). In deterministic mode the virtual clock simply stops.

How: the call of 005E65F0 at 004580D7 (the cursor lock in the frame function, just before the
software cursor's blit and the present) is redirected to the capture hook, which counts presented
frames and, when a capture is due, reads the render target (device object 007AF080 +0x18 -> +0x10)
under the device lock: `Lock(NULL, DDLOCK_WAIT | DDLOCK_READONLY)` and a conversion from the
surface's own RGB masks (5:6:5, 5:5:5, X8R8G8B8, ... by bit replication, so 16-bit white stays 255),
falling back to `GetDC` + `BitBlt` into a DIB section. Both work on dgVoodoo's emulated surfaces at
the game's own resolution, before dgVoodoo scales and presents. Capturing the window or the desktop
through GDI is not used: dgVoodoo presents through a DXGI swap chain, whose contents GDI reads back
as black (or stale), and in full screen the game's surfaces are not on the desktop at all. The same
hook works with `--no-hooks`, in both installs and in both window modes (render target = back
buffer in full screen, off-screen target in a window).

What makes two runs' frames comparable, and the limits:

- **Cursor**: captured before the software cursor is drawn; the hardware cursor is never in a
  surface. Hover effects (highlights, cursor-dependent UI) still depend on the mouse position:
  scenarios park the mouse (`move`) before freezing.
- **Animation time, particles, interface fades**: held by `freeze`. Within one freeze consecutive
  captures should be identical (frames_basic captures `world` and `world_again` to check);
  differences mean something moves per frame without the game's clocks (other threads, frame
  counters, the movie thread).
- **Different runs** reach a screen after a different number of world updates (real-time
  scheduling), so unit idle animations, particles, weather (random rain, lightning) and AI movement
  differ between runs even when frozen; the deterministic mode is still experimental. Cross-run
  comparisons are meaningful for static screens (menus, camp, global map, the world at a frozen
  moment with no weather) and with a tolerance; exact backend checks need the same-process A/B mode
  (section 14).
- **Settings**: both runs must use the same resolution, colour depth and options (they come from the
  registry, which harness runs read but do not write) and the same install: Rebirth widens the
  layout and the draw radii, so vanilla and Rebirth frames are not comparable with each other.
- **dgVoodoo**: forced MSAA, anisotropic filtering and dithering change the original's pixels; see
  section 14, item 5.

`tools/compare_frames.py A B` compares two BMP files or two run folders (every `frames\<name>.bmp`
present in both): a pixel differs when a channel differs by more than `--tolerance` (default 8, one
5-bit step); it prints the differing fraction, the largest and mean difference and the worst
`--block`-sized regions, writes a difference image with `--diff`, and fails above `--max-fraction`
(default 0.1 %). Standard library only.

`tests/scenarios/frames_basic.txt` captures the main menu, the zone1 world after a new game with the
hero unblocked and the camera settled (twice in one freeze), the camp (`screen 5`) and the global map
(`screen 4`), each frozen with the mouse parked; about 30 s.

Not done here: the backend itself, the runtime trace (R1), a reference run folder with its own
dgVoodoo.conf, freezing threads that animate on their own.
