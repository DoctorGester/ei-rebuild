# Renderer

The game's own renderer (DirectDraw 7 / Direct3D 7) is replaced by our renderer (`src/render/`, static library
`ei_render` linked into ei-rebuild.exe) on **SDL3's GPU API** through its own thin layer, `src/render/gpu.h` (section
15c): Direct3D 12 (feature level 11_0, shader model 6.0) on Windows, Vulkan on Windows and Linux, Metal on macOS.

1. Game code that called the renderer hands its data over instead: camera and matrices, object transforms, figure
   meshes, terrain sectors, particles, textures, the interface windows' quads and glyphs, movie frames. Game code makes no
   Direct3D or DirectDraw call and includes no such header; only the gamma ramp's table is left of that interface.
2. No Direct3D 7 emulation and no compatibility shims. Neither of the original's helper DLLs is loaded (2DintMMX: its
   texture work is on the GPU; 3DfpFPU: its projections are plain C++ in `projection.cpp`, section 10). Results only need
   to be close (captures are compared with a tolerance).

The renderer draws, in the game's order, the terrain (ground and water pages), every mesh the game draws (figures, map
objects, sky, interface sprites and 3D previews), the flat geometry (selection markers, ground prints), the interface
windows' quads and glyph runs, the renderer's rectangles (rubber band, fades), the depth clears mid-frame, every pre-transformed draw
(particles and sprites, the vision fog's quads, ribbons and lightning, flying hit points), the weather and the movies
(also the players outside the frame). The escape menu's and the save game's screen copies come from its frame. The
device is made on a thread of its own, with the pipelines every game draws with (section 15c, "Pipelines made ahead"),
as soon as SDL's video is up (`render::PrepareDevice`, in `app::StartVideo`, main()'s step before GameMain) and taken
at the renderer's first use.

### Shader authoring: HLSL and SDL_shadercross

The shaders are HLSL, `src/render/shaders/<program>.hlsl`, one file a program, with shared parts in `.hlsli` files.

The build (CMakeLists.txt):
- **Toolchain.** `tools/shader_toolchain/toolchain.cmake` fetches SDL_shadercross's sources (a pinned commit),
  SPIRV-Cross (a Vulkan SDK tag) and DXC's release binaries from GitHub, each checked against its pinned SHA-256. At
  configure time it builds `shadercross` and `spirv-cross` once, about a minute. Everything goes to `EI_THIRD_PARTY`
  (cache variable or environment). Its default is `build/third_party` of the main checkout, so every worktree shares
  one copy. A pin or wrapper change builds a new copy beside the old one, and a file lock lets builds that configure
  at the same time wait for each other. When its build fails, the configure prints the end of its two logs.
- **Prebuilt.** `-DEI_PREBUILT_SHADERS=<folder>` takes `gpu_shaders.h` and `.cpp` from another build's `shaders/` for
  the same system (the target `ei_shaders` builds just them) and skips the toolchain and the compiling: the Release
  workflow's Linux container, whose glibc 2.35 DXC's release cannot run on (docs/notes/linux.md, "Release").
- **Compiling.** Each program's two stages are compiled to SPIR-V and, on Windows, DXIL by shadercross (so no compiler
  runs at the start): `vs_main` under `EI_VERTEX`, `ps_main` under `EI_FRAGMENT`. The MSL is spirv-cross's from that
  SPIR-V (`--msl --msl-version 10200 --msl-decoration-binding`): every texture, sampler and block at its SPIR-V
  binding, which is SDL_GPU's Metal slot (texture and sampler n, uniform buffer n; vertex buffers sit at 14 and up).
  shadercross's own MSL binds only the textures whose sampler the stage reads, plus those after the last of them in its
  resource list, so shadow_receiver.hlsli's `shadow_range` (read with Load, its sampler unused) landed at texture(0),
  over the stage's first texture (R43h). The output goes to `shaders/hlsl/` in the build folder.
- **Reflection.** `spirv-cross --reflect` writes each stage's interface. The generator checks each MSL entry point's
  `[[texture(n)]]`, `[[sampler(n)]]` and `[[buffer(n)]]` against it (each at its binding, no two of a kind at one).
- **The header.** `tools/gen_gpu_shaders.py` checks the rules below and writes two files into `shaders/` in the build
  folder:
  - `gpu_shaders.h`: each uniform block's struct (`MeshFsParams` and so on, Matrix4 for a matrix; its size asserted)
    and each storage buffer's element (`MeshRecord`), and per program a namespace with its index in `kPrograms` and its
    slots: `kBlock<Type>` (the uniform push slot), `kSampler<Texture>` (the texture-sampler pair's slot),
    `kStorage<Type>` (the storage buffer's slot after the stage's textures) and `kAttr<Input>` (the vertex input's
    location).
  - `gpu_shaders.cpp`: the program table, with each stage's code in every format, its entry point and its sampler and
    block counts for `SDL_CreateGPUShader`.
- **The check.** `ei_shader_check` (tools/shader_check; build_checks.py on Windows, ctest `port_shaders` on Linux) makes
  every program (its storage buffer count with it) and a pipeline for each kind of target the renderer draws it into
  (`kDraws`): on Direct3D 12 and Vulkan
  on Windows, on Vulkan on Linux (lavapipe without a GPU), on Metal on macOS. A driver without a device is skipped; with
  `--require`, as ctest runs it, that fails.

Rules (SDL_GPU's, or the generator's):
- **Spaces.** Through `bindings.hlsli`'s macros:
  - vertex stage: textures and samplers in space0 (`VS_TEXTURE`, `VS_SAMPLER`), blocks in space1 (`VS_BLOCK`);
  - fragment stage: textures and samplers in space2 (`FS_*`), blocks in space3.
- **Slots.** A texture and its sampler share a slot: SDL binds them as a pair, so a texture read with `Load` still
  has one. A stage's slots run from 0 without gaps. At most 4 blocks and 16 samplers a stage.
- **Stages.** DXC keeps every resource a file declares, used or not, in each stage's SPIR-V. So code for one stage
  sits under `EI_VERTEX` / `EI_FRAGMENT`, and the generator fails on a resource in the other stage's space.
- **Inputs and outputs.** Each varying is `[[vk::location(n)]] ... : TEXCOORDn`: the location for SPIR-V, the
  semantic for DXIL.
- **Blocks.** `ConstantBuffer<T>`, whose members are `float`, `int` or `uint` with 1 to 4 components, arrays of the
  4-component ones, and `row_major float4x4`. They are laid out as HLSL packs a constant buffer (a member that would
  cross a 16-byte register starts the next one; a matrix and each array element start one); the generator checks the
  SPIR-V's offsets against that packing and writes the C++ struct to match (a float3 is `float[3]`, a `paddingN`
  member where a register's rest is skipped, each offset asserted; `alignas` drew MSVC's C4324). The matrix's register k is the Matrix4's row k,
  as `m[k]` reads it; a Matrix4 is uploaded as it is in memory. An unsigned value goes in an `int` (`asuint` reads it
  back). No variable index into a component of a block's vector: copy the vector into a local first.
- **Storage buffers.** `StructuredBuffer<T>` at `VS_STORAGE(n)` / `FS_STORAGE(n)`: register t(textures + n) in the
  stage's texture space, as SDL_GPU numbers a stage's read-only storage buffers after its textures in SPIR-V and DXIL.
  `T`'s members are `float4`, `int4`, `uint4`, `row_major float4x4` and arrays of them (16-byte members: DXIL, SPIR-V and
  MSL then lay the element out alike); the generator writes `T` as a C++ struct like a block's. In MSL SDL binds a
  stage's storage buffers after its uniform blocks, so the generator moves spirv-cross's `[[buffer(n)]]` of a storage
  buffer to blocks + slot (untested on Metal so far: no Mac run since R37f3). The renderer binds a whole buffer
  (`gpu::BindStorageBuffers`, no offset); a draw finds its element by an index (the mesh draws: an instance-rate
  `uint` vertex input, `record`).
- **Names.** A block's type is its HLSL struct's name (`MeshFsParams`), its members named as there. Each value has its
  own member (`int on`, `float strength`, not the components of one float4), a switch or a count is an `int`, a map's
  slot -1 for none. The C++ fills a block just before it pushes it and never reads one back: what the CPU keeps
  (the scene's lights, a figure part's, the shadow map's offsets) lives in plain structs.
- **Shared parts.** `rect_vs.hlsli` and `fullscreen.hlsli` are the two vertex stages most programs share. The
  receivers' `shadow_receiver.hlsli`, `point_lights.hlsli` and `scene_lighting.hlsli` take their slots from the
  including program's `SHADOW_*_SLOT` / `POINT_*_SLOT` / `SCENE_BLOCK_SLOT`.
- **The depth copy.** The programs that write the depth (`mesh`, `terrain`, `solid`, `into_multisampled`) also
  output it at `SV_Target1` (`depth_copy.hlsli`: the fragment's `SV_Position.z`). Only a
  multisampled pass's pipelines have that target (the smooth option, and only in frames where a draw takes the copy:
  section 11); with one target it is dropped. The check's
  `kDraws` marks those draws `depthCopy`, and a second output must be one float.

A new program is its `.hlsl` file, which the build finds by itself, and a line in the check's `kDraws`.
`tools/comments.py` reads the `.hlsl` / `.hlsli` comments like the C++ ones.

## 1. No fallbacks

The game draws only through this renderer: it draws every frame into an off-screen target of the game's resolution and
presents it through its own swap chain on the game window (section 9); captures write that frame as
`frames\<name>.bmp`.

**When the renderer fails** (the user's rule: no fallbacks). When it cannot start or cannot go on, the game ends.
`render::Fatal` (render.cpp) logs "render: fatal: <reason>" to ei-rebuild.log, shows the reason in the game's
error box (`app::ShowMessageBox`; the harness ends the run on it) and exits with -1. The cases:
- no device at the start (the first use): "Can't start the renderer: <gpu::Error()>." (no Direct3D 12 or Vulkan
  device: "no Direct3D 12 or Vulkan device (...)");
- a pipeline or texture the start makes: "Can't start the renderer: the terrain pipeline / the mesh pipeline / the
  blit pipeline / the white texture / the water's ripple map could not be made.";
- a call SDL refuses on the frame's commands (a command buffer, a copy or render pass, a mapping, the swap chain's
  texture, a submission or only its present, a download's wait): "The renderer cannot draw the frame: the GPU refused
  <what>: <SDL's reason>." gpu.cpp's `Failed` logs it ("gpu: failed: ...") and calls the handler the renderer set
  (`gpu::SetFailureHandler`) at once, before any other SDL call: a lost device ends the game here ("No device-loss
  recovery", section 9);
- a pass without targets (one that could not be made): "The renderer cannot draw the frame." (the shadow map's and
  the point shadows' passes name themselves);
- no swap chain (render_window.cpp): "The renderer cannot present on the game window: <gpu::Error()>."

A draw the renderer does not take is lost, not drawn another way: `render::MeshPass`, `TlDraw` and `ScreenDraw` return
nothing, and render_mesh.cpp's `Refuse` logs each reason the first time ("render: <reason>; the draw is lost": a mesh or
pre-transformed draw with no render target size, a mesh corner past its positions, a pre-transformed draw in another
vertex format than 0x1C4 / 0xC4, too many pre-transformed vertices in one frame). An empty draw is nothing to draw, not a
refusal. `EI_RENDER` (environment, or the harness step
`render <spec>`) is a comma-separated list of options: the harness build's `checkall`; any other word is
logged as unknown.

## 2. Frame structure of the game

Per main-loop pass: BeginScene; the interface draws window by window; EndScene; the escape menu; **present**
(`device_draw.cpp`: ends the frame, `render::Present`). The battle screen's world view (`WorldClient::Render`):
- view setup (the view matrix from the camera pushed on the transform pipeline's stacks), the visible sectors (every
  sector of the map; the view has no far plane: section 10, "The view's depth");
- sky (a model following the camera, z-write and alpha test off, hazed into the fog's colour towards the horizon);
- terrain ground pages (`Terrain::DrawPages`: one `render::TerrainPass` for the drawn sectors);
- objects (CollectVisible, sort, figure parts, the shadow lists);
- terrain water pages, blended;
- effects: vision fog, particles, lightning, rain and snow; then the line lists and the flying hit points.
Then the interface window by window: sprites (`UiMesh::Draw`: the mesh draw), 3D models (`UiModel::Draw`; the hero
preview first clears depth to 0 and its rectangles to 1) and surfaces (each shown `UiSurface` draws its quads and glyph
runs into the frame, alpha-blended). The battle screen clears the depth buffer before its panels. Renderer
rectangles: translucent black with an outline (rubber band, fades).

The renderer's frame: game code hands data over while running its draw code, as a command list in the game's order
(terrain batch, mesh pass, surface blit, depth clear, ...); `render::EndFrame()` (from the present, or earlier for a
capture) draws them with depth; the swap chain's present shows it (section 9). Per-frame data is rebuilt every frame
unless kept on the GPU (section 13); textures are kept per holder.

A frame step without a present (`app::FrameStep(false)`: a frame drawn for a copy of the screen, the quick save's
picture and the windows' backdrops) is a renderer frame of its own: `render::EndUnshownFrame()` draws it into the frame
target, which keeps it for the copies taken after it (they run before the next frame's first pass), and begins a new
frame. Before R60 its draws stayed in the frame and the next step's were added to them: the shown frame's once-a-frame
passes (the shadow maps, the point shadows, the occlusion, the sky reflection) and its kept state came from the first
draw's hand-over, so a new game's autosave frame (QuickSave: the unshown frame for its picture, then "saving..." over
the scene, the first frame shown) was lit otherwise than every frame after it: the meshes warmer and lighter, 15-18%
of the world view off by more than 8 (the user's Lost in Astral start at the camp fire, 2026-11-05; vanilla's zone1
alike). Scenario render_save_frame: the save's frame and the next (the same tick) match in the world view.

Conventions taken over: Direct3D clip space (the game's projection already produces it), Direct3D 7 pixel centres (3D
draws shift their clip positions by half a pixel, `Frame::pixelShift`, because the viewport is kept in whole pixels
(gpu.h's `SetViewport` takes a `Rect`); a clip -0.5..0.5 square covers pixels 16..47 of 64, a screen-pixel mesh from 8 to 24 covers 8..23),
D3DCOLOR vertex colours (B, G, R, A bytes), vertex fog in the specular alpha (255 = no fog), specular added after
texturing. Textures have the .mmp file's own mip levels and no others (section 7).

## 3. What the renderer feeds back into game logic

- **Visibility** (the frustum test, the in-view flag 0x20 of client objects): readers are client-side only (cursor
  picking and rubber band, particle sources, the shadow lists, client animation to game time, pending figure changes,
  model LRU). **Visibility only gates client-side animation, figure changes and picking, never server state or timing.**
- **Picking** uses screen rectangles written during the object draw from projected positions (copied into node and
  figure pieces; `WorldClient::UnitAt` / `Pick` (HitTest::Parts) test the parts' rectangles only). Whatever draws the objects must still
  produce them. The rule (R37f13, `map_object_draw.cpp` `BoxRect`, units' and levers' parts only): the bounds of the
  part's box (its cube when it has no box) projected by the screen stack's top, the corners inside the depth range
  (nothing behind the camera or nearer than the near plane), clamped to the screen. No vertex is projected on the CPU;
  a part the object's draw skips (too far, hidden, not in its figure) gets an empty rectangle.
  - **Accepted difference from the original** (user's call): the original clipped a crossing mesh on the CPU and took
    the bounds of the clipped triangles' points inside every plane; an empty clip kept the last draw's rectangle. Ours is
    the part's box, a little larger than its triangles (the hit order still goes by the triangles under the pixel,
    below), and larger past a screen corner or across an edge: at the bottom edge an ogre's part picks a few 800x600
    pixels further left or right than in the original. No scenario clicks the edges on purpose (H74: their code is the
    picking every click runs, which the suite reaches elsewhere). The
    corners are under the battle screen's HUD, so no click reaches a corner's difference.
  - **The order of the hits** (R64, a deviation by the user's choice): `WorldClient::Pick` sorts the objects that pass
    the test front to back, living before dead, each by `Object3DSpecific::PickDepth`: the view depth (the screen
    stack's w) of the nearest triangle drawn under the pixel's centre, over the parts whose rectangle holds the point
    (`FigureMeshDrawer::SurfaceDepth`: a ray from the eye against the triangles the part's last draw had, its figure's
    part or its own buffers moved by its offsets, placed by the matrix the draw kept, `screenMatrix`); an object with
    no triangle there (a rectangle's empty corner, a hit by the bounds) counts at its origin's depth as drawn
    (`originDepth`), the original's key, so a unit's generous click area works as before. The original (00565E40)
    sorted every hit by the objects' origins along the view, so a big platform whose origin was nearer won over a lever
    on its deck (LiA's gz1d2 lift, `mod_lost_in_astral_elevator`; mods.md). A box-front key for the surfaceless hits
    was tried and dropped: it reordered near ties between bodies lying together, which the origins had kept
    (net_pair_quest's walk searched another body and the quest failed). One rule for every pick: UnitAt, the battle
    screen's use cursor and click, the harness's `clickunit`. CPU-side from what the draw kept (no frame read back),
    so picks stay deterministic and the same under every GPU driver.
- **One real state change in a draw path:** the terrain pass applies pending water level changes to the sectors it
  draws; the water layer at rest, with the waves worked out on it (Terrain::WaterVertex), is what height queries read.
  Terrain::DrawPages applies them before handing over (the water mesh's range follows: `WaterMoved`).
- **Pixels:** the escape menu's background and the save game's 256x192 picture are copied from the frame on the GPU, and
  only the 5:6:5 picture is read back (section 6, "Screen copies on the GPU").
- **Timing:** up to five world updates per frame; vsync only changes grouping.

## 4. Hand-over map

| Data | Hand-over point | `render::` call |
|---|---|---|
| Clip matrix of the current draw | the transform pipeline's stacks (`TransformPipeline`, pipeline.h: clip space = axis swap x projection x view x model; the screen matrix; the view stack) read at hand-over time | with each draw |
| Terrain sectors | `Terrain::DrawPages` (terrain.cpp): the drawn sectors with their quarter verdicts per pass; the zone's sectors read when built or changed: vertices (33x33 per layer, water after ground), vertex records (normal, material colour, the specular word: edge fade, on the water the crests), tile lists, edge flags (`TerrainChanged`); `HandOverSceneLights` (world_client_shadow_casters.cpp) | `TerrainPass`, `TerrainChanged`, `SceneLighting` |
| Meshes (figures, objects, sky, interface sprites and previews) | `device_mesh.cpp`'s mesh draws: positions and normals in packets of four, corners and texture coordinates (`render::VertexPacket` / `NormalPacket` / `MeshCorner` / `TexCoord`, mesh_records.h: the game's meshes hold the renderer's records), colours; matrix = the screen stack's top converted back to clip space; the distance fog switch | `BeginMesh` / `EndMesh`; `MeshLighting` for parts lit on the GPU |
| Pass state | per pass of a mesh (material preset or two textures): `render::g_renderState` (section 5) | `MeshPass`; per terrain pass at `TerrainPass` |
| Depth clears | the battle screen's panels, the hero preview, the trade grid, the hero equipment, screens 2 / 3 | `ClearDepth` |
| Shadows | `world_client_draw.cpp` (the sun; `BeginCaster` / `EndCaster` around each object's draws), `map_object_shadow.cpp` (`CastsShadow`), the casters outside the view | `ShadowSun`, `CasterMesh` (section 13) |
| Particles, ribbons, lightning, hit points, vision fog | pre-transformed draws: sprites (`renderer_sprites.cpp`), ribbons and triangles (`renderer_strip.cpp`), line strips (`renderer_lines.cpp`) | `TlDraw` with vertices after the distance fog (`FogVertices`) |
| Weather | rain streaks and snowflakes built on the screen (`weather_particles.cpp`, the field in `weather_field.cpp`) | `TlDraw` |
| Interface surfaces | `UiSurface::Blit` (`ui_surface.cpp`) | `ui::Solid`, `ui::Glyphs`, `ui::DrawPicture` |
| Rectangle fills | `renderer_marker.cpp` | `ScreenDraw` |
| Cursor | the frame's cursor image (`ui_cursor.cpp`) | `Cursor` (shown as the system's cursor, section 9) |
| Movies | `bink_video.cpp` (our Bink decoder into a CPU buffer, letterboxed) | `MovieFrame` / `MovieBlit`, `Fill` |
| Present | `device_draw.cpp` | `Present` (section 9) |
| Clear colour, target size, fog | the scene renderer's fog colour, the device object's screen, the render state's fog | EndFrame / terrain hand-over |

The game-side work stays (visibility, animation, sorting, water levels); the goldens check it.

## 5. Pass state

`src/render/render_state.h`: the render state the game draws with is one plain struct the renderer owns,
`render::g_renderState` (`render::RenderState`; not "DrawState": winuser.h makes that a macro): blending and its
factors, the alpha test, depth test / write / function, culling, specular, fog and its colour, the texture factor, and
two texture stages (each its colour and alpha operations and arguments, `StageCombiner`, its filters and addressing).
The values keep Direct3D 7's numbers in typed enums (`Blend`, `Compare`, `Cull`, `TextureOp`, `TextureArg`, `Filter`,
`MipFilter`, `Address`), which the pipelines and the mesh shader take as they are. Game code writes the members where the
original set render or stage states (device_draw, device_states, renderer_init, renderer_lines, renderer_rect,
renderer_sprites, renderer_strip, figure_part_draw, particle_draw through `SetBlend`, world_client_draw, the interface's
draws); the original's reads around a draw (saved and put back) are reads of the members. `SceneRenderer::CreateRenderer`
resets it to a new Direct3D 7 device's state (the members' initialisers) before the game's own settings. Each pass copies
it (`ReadPassState`, render_mesh.cpp; the fog once a frame, `ReadFog`).
The copy (`PassState`, render_internal.h) keeps the same enums: `ToGpu` (render_mesh.cpp, render_textures.cpp) turns
them into gpu.h's blend factors, comparisons, wraps and filters, and only the mesh uniforms take their numbers as ints.
`AddsLight` is an additive pass (blended, destination factor One): it neither casts nor receives shadows. The renderer's
keys are typed: textures by their `Surface` holder, kept meshes by `FigureMesh` and `LinkMesh`, casters and units by
`Object3D`; `MeshChanged` (figure meshes, link figures and colour arrays) and the movies keep `const void*` keys.
Vertex layouts take their offsets from the vertex structs (`offsetof`; `ColourPair` for the kept colour stream).

Not kept, because nothing in the renderer reads them: shade mode, dithering, z bias, antialiasing, perspective
correction, fog vertex mode, extents, clipping, lighting, ambient, fill mode, the texture coordinate index and transform
flags (every material stage's index was 0), and stages 2-7.

Stage-0 presets (texture.h):

| constant | colour | alpha |
|---|---|---|
| `kTerrainStage` | texture x current (MODULATE) | texture (SELECTARG1) |
| `kAlphaPassStage` | texture x diffuse | texture x diffuse |
| `kSceneStage` | texture x diffuse | diffuse (SELECTARG1) |

A material's preset (`RenderPreset`, render_helper.h / render_helper.cpp) keeps its state list: `ApplyPass(0)` sets the
combiners of its first `stages` stages (the enabled ones) and turns the next stage off; pass n > 0 sets that state's
blend factors (device_draw.cpp's multi-pass draw puts the blend back afterwards). `SetBlend` (device_draw.cpp) sets the
two blend factors (the particles' additive / normal switch). The line strip (`renderer_lines.cpp`) turns stage 0's colour
/ alpha operations off around it and goes to `render::TlDraw` (line strip, FVF 0xC4); no scenario reaches it.

## 6. Interface windows and holders

**Interface windows drawn each frame.** A window surface has no pixels of its own: each frame its Blit draws its quads and
glyph runs into the frame (`render::ui::Solid` / `Glyphs`, `render_ui.cpp`, ui-native.md section 3), and a screen copy is
a frame picture on the GPU (`CapturePicture` / `DrawPicture`). The reader: the save picture (`ReadPicture`). Each surface keeps the layout rectangle it was given and is in a registry of live surfaces,
so a resolution change places them all again (`UiSurface::RelayoutAll`, "Resolution changes").

**Holders** are our own `Surface` (surface.h: width, height, bits per pixel); no holder has pixels in memory: the
picture holders (cursor images) have only a size, the textures section 7's images. A
The screen's pixel format is `ScreenFormat` (X8R8G8B8). The cursor's mask is built from the .mmp file's
pixels (`ChannelLayout::ToNative` to the screen's format, ui_cursor.cpp); the renderer keeps the picture as RGBA8
(`render::PictureFromMmp`). Scenarios: ui_compare,
ui_saveload.

**Save / load screen's pictures** (`ui_saveload.cpp`; ui.md, "Save / load window pictures"): the selected save's picture
and the new save's (from the escape menu's copy of the screen), each a texture of the picture's own size drawn into on
the GPU (section 7). The new folder is "save<n>", n one more than the highest number after "save". Checked by ui_saveload.

**Screen copies between frames.** The quick save (F5 "saveq") and the autosaves copy the screen with CaptureScreen and
make shot.sav from it (ui.md, "Save / load window pictures"). The new game's autosave draws the interface first (a frame step without a present:
the copy comes from that frame); the quick save key copies between frames, before the next frame's first draw. The
renderer's target then still holds the last frame: the copy (`render::ui::CapturePicture`, which draws the frame so far
first, SplitFrame) reads it as it is when nothing of the frame is drawn or queued yet. A pass there would clear it first
(the save's picture was once the plain clear colour). Checked by ui_quicksave (`checksave saveq world`: both pictures
against a capture of the same frozen moment as 32x24 block averages).

**Screen copies on the GPU.** A surface the screen is copied into (the escape menu's and the camp's backdrops, the
quick save's copy) gets a frame picture of its size (`ui_copy` from the frame target, x *
source / size, as DirectDraw's Blt picked), drawn by its Blit (`ui_picture`, greyed after Greyscale). ReadPicture reads a
picture back at its size (`render::ui::ReadPicture`, RGBA8); the save's pictures are made from it on the CPU. Checked by ui_quicksave, the gallery's escmenu / map_escmenu frames, ui_compare and
`ei_ui_check`.

## 7. Textures

No texture has memory the game writes: .mmp textures have only their decoded copy; the composed ones, the blank textures
`TextureManager::NewTexture` makes (kinds 0x1E, 0x20, 0x21) and the .bmp textures only the texture the GPU drew.

**Texture formats.** A texture keeps the id of the format it is held in (`Texture::format`: 0x5650, 0x5551, 0x4444,
0x8888 or a DXT1..DXT5 code; 0 none). The list is fixed (texture.cpp `IsTextureFormat`; DXT5 must stay: Rebirth's battle00
and footprints are DXT5) and `TextureFormat(id)` gives an RGB one's `render::PixelFormat` (bits and channel masks,
render.h). An .mmp file is held in its own format, a DXT code outside the list in the
RGB format its channel bit counts name (`MmpHeader::HeldFormat`; none: the original's "Can't find texture format"
error); a .bmp texture by its kind (1:5:5:5, 4:4:4:4 or 5:6:5).

| Function | What |
|---|---|
| `Texture::CreateBlank` (`texture_load.cpp`) | texture square from the header's width (height not read): a decoded-texture holder with `render::BlankTexture`, the alpha as the format keeps it (5:6:5 opaque, 1:5:5:5 one bit, 4:4:4:4 all); the 1:5:5:5 atlases one level, the others their levels made after each draw; the byte count = bits x size x size / 8 |
| `TextureManager::NewTexture` | kinds 0x1E / 0x20 / 0x21 get the formats 5650 / 5551 / 4444; another kind gets none and CreateBlank throws |
| `Texture::CreateFromMmp` (`texture_load.cpp`) | `render::TextureFromMmp` takes the file's levels (DXT1..DXT5 as their blocks; 8888, 4444, 5551, 5650 decoded by the header's channel masks; keyed by the holder); the byte count = `MmpHeader::FilledBytes`; an undecodable format keeps the holder and shows nothing (logged once) |
| `LoadBmpFiles` (`texture_load.cpp`) | loose textures\<name>.bmp via the game's DIB reader, drawn into a blank texture (`DrawIntoTexture`; 8-bit colours); reached only by render_loose_bmp (no .bmp in the installs); a non-square .bmp's pixels past the texture are left out |
| `ReleaseObject`, `TakeFromCache`, `FindTexture` (`texture.cpp`) | the cache list and its byte count; `FindTexture` does not lower the count for a texture it takes from the cache (`TakeFromCache` does) |

**An .mmp file's levels.** The first level the texture takes is at `MipOffset` (the geometric sum), each next one right
after the last, bits per pixel x width x height / 8 bytes (sizes halved down to 0). The original's 32-bit product
overflowed from level 6 of a 1024 x 1024 DXT1 and level 5 of a 2048 x 2048 DXT3 (zone1's tree crowns showed opaque green
strips); ours does not. A DXT level under 4 x 4 gets only those bytes (the start of its block) and zeros for the rest of
the block it is uploaded as. A DXT texture is a BC1 (DXT1), BC2 (DXT2 / DXT3) or BC3 (DXT4 / DXT5) image of the file's
blocks (`BlockFormat`, `ReadMmp`); there is no CPU DXT decoder. Every texture, DXT or RGB, has the file's levels only:
no GPU API makes a block-compressed level from the one above, and the original's texture had those levels too (1035 of the
installs' 1792 DXT files stop early, 229 at level 0: the menus' backdrops, tree03, the decals). A DXT top level must be a
multiple of 4 wide and high: every DXT file is, at the levels the texture quality option starts at (0..2). The cursor's
pictures (`PictureFromMmp`) are 8888 files; a DXT one is not decoded (logged). The decoded textures do not keep the
file's bytes.

**A texture found nowhere** (its name in no archive and not composed; `LoadTexture`'s "Can't find texture" is a
`DebugLog`, which writes nothing) keeps no image and draws white, as a Direct3D stage without a texture did: a unit made by AddUnitToServer has the texture
name "" and draws white lit, pale (world.md, "A record's texture names"; the original's look, R49).

**Texture patches**: `render::TexturePatch(target, to, source, from)` records that a rectangle of one holder's texture
shows a rectangle of another's (first-level texels, one per target, gone with either holder). `ReadPassState` gives stage
0's patch to the batch (`PassState::patch`), and the mesh shader samples the source (`tex2`, stage 0's sampler;
coordinates kept half a texel inside its rectangle) where the wrapped stage-0 coordinates fall in the target rectangle.
The unit's face (the blink) is its only user; check `render_blink`.

**Textures composed on the GPU** (`render_compose.cpp`). `render::ComposeTexture(holder, size, mip, levels, images)`
makes a texture from images drawn over each other: the unit skins and wounds textures (items_spells.md, "Composed
textures"). Each image is an .mmp file's pixels after its header and extra block: 32-bit 0xAARRGGBB with premultiplied
alpha, rows from the top; "PNT3" files are packed: groups of four pixels, where a group's first pixel has alpha 0 and is
not 0 that word instead counts target bytes left as they are. The renderer decodes an image to RGBA8 for the draw
(`DecodeComposeImage`), and draws into a texture of the full size with every level the texture
uses (`ComposeShape`): cleared to transparent black, each image a rectangle drawn texel for texel
(texture_compose.hlsl) with premultiplied blending (ONE, INV_SRC_ALPHA, alpha too), then the levels below, each drawn
from the one above (`texture_mip`: the mean of four texels). A pass may not read the image it draws into, so a level is
drawn into a scratch image of half the size and copied from there. The texture's view starts at level `mip` (the texture
quality option), so its size is `size >> mip`. The work runs at once from the game's load (not inside a frame pass). The
entry keeps the image and its shape only.

**Pictures drawn into textures.** The 1:5:5:5 textures (the item screens' figure textures and the image atlases
`CopyImage` fills: portraits, buttons, item cells) are composed textures without layers: `render::BlankTexture` makes the
transparent texture (one level), and `render::DrawIntoTexture(holder, image, x, y, width, height)` draws an .mmp
picture's rectangle (or white, without the file) into it, replacing what was there: blending off, alpha cut to one bit
(at least half: opaque, else 0). The picture is the file's
pixels right after the header (the extra block not skipped, as the original read them). Colours are 8-bit.
- The save / load pictures: a blank texture of the picture's size into which its RGBA8 pixels are drawn
  (`ImageLayout::Rgba`; the 5:6:5 ones decoded on the CPU, `save_picture.h`).
- **The global map's clouds** (R66; `GlobalMapClouds`, global_map_clouds.cpp; `map_clouds.hlsl`). The map draws its
  three cloud layers (shadow, haze and glow: big planes at three heights that turn with the map, each in its flat colour
  and scrolling the texture at its own pace, ui_global_map.cpp) from one blank texture, which `render::DrawClouds`
  redraws each frame with our own cloud shader (the game's look, 256 x 256 blocky value noise that swapped frames every
  16 steps, is gone). Per texel:
  - a density from fractal gradient noise (7 octaves, each 2x finer and 0.55x as strong), its point first moved by two
    slower noises (the domain warp: billows and wisps). Every octave repeats across the texture (4 cells at the coarsest,
    a hashed lattice taken modulo its period), so the layers' scrolling wraps without a seam;
  - the drift: each octave and the warp move their own way at about a third of a coarsest cell per 16 of the game's
    clock (`MapClouds::time`: seconds x the game's speed 2, so a cell in about 25 s), so the shapes change continuously.
    The drifts are whole 1/64ths of a cell, so the clock's wrap (`kMapCloudsPeriod`, 32768) moves each by whole repeats;
  - the alpha: the game's ramp of the density, 1 - (width / 255)^max(0, 255 x density - start) (SetRamp's 0x77, 0xF8:
    start is the cover, width the softness);
  - the colour: a grey shade the layer's flat colour multiplies. Lit from a fixed sun (the screen's upper left on the
    layers): the density a little towards the sun against here (rising: the side away from the sun, darker), and the
    thick parts greyer (x 0.72 at full alpha), so the clouds show volume.
  The texture's size follows the frame (the power of two at or above its height, 512 .. 2048: about a texel a pixel
  where the layers show it), remade when the frame's height changes; its levels below the first are drawn after it.
  The pattern is one word of the client's random numbers (a new one at each visit; the old code drew 24 words at the
  start and 3 a step). Check: render_map_clouds (the map's first frames, four consecutive frames, frames a second apart
  that must differ; the captures page's R66 section).

## 8. Draws outside the frame: movies and the cursor

Interface draws (ui.md, "Container draws") set `render::g_renderState`, and their depth clears
go through `ClearUiDepth` (ui.h) to `render::ClearDepth`.

| Function | What ours does |
|---|---|
| `PlayMovie` (`app/movies.cpp`): the blocking movie player with its own message loop (Esc / Space end or skip a frame) | each frame to `render::MovieFrame`, then `render::Fill` (black) + `MovieBlit`, presented through the present; result 0 / 1 (1 with movies off) |
| `ShowLoadingMovie`: the loading movie (progres*.bik per zone) stretched over the screen; hides the cursor on every call and never shows it again | its last frame blitted and presented as above |
| The cursor | the system's cursor (section 9, "The cursor") |

The harness's input steps are not posted inside a movie (the harness empties movie.ini's lists). No suite scenario calls
PlayMovie by script (H74: the code is the loading movie's); the loading movie runs in every new game
(render_cursor_present: the cursor hidden there).

Images kept across frames (`render_blit.cpp`): a movie keeps one texture per player, written whole (`Discard`) in
place unless it was already blitted in this frame (the blits are drawn at the end of the frame; then a new one replaces
it, the old one released after the present). `render::Fill` is a solid rectangle command drawn with the depth clear's
program (`solid.hlsl`) through a pipeline that writes the colour only. Every read-back of a texture (the frame, the
pictures, the composed levels, the harness build's checks) is a gpu.h download (`Download`, `Wait`, `Read`). Harness build: counts per purpose in ei-rebuild.log (`render: images ...`), and in-place
uploads are read back and compared with the pixels written (`render: MISMATCH`; sampled: the first 64 of each kind, then
every 16th, all with `checkall`).

## 9. The present path

The game has no DirectDraw object, display mode, primary, render target, depth buffer, Direct3D object or device, and no
stand-in for any of them:
- `TextureDevice` (texture.h, device_mesh.cpp) is the screen: window, full-screen flag, screen rectangle,
  the transform pipeline, the stages' bound holders and the draws' state. Its constructor starts the renderer
  (`render::StartPresenting`). The options' driver, refresh rate and depth / stencil bits choose nothing. Where the game
  read the screen's size (`GameTargetSize`, the cursor's bounds, the interface's size, the sprites' clip and the hit
  points' screen) it reads `screen`.
- What the game read from the old device's capabilities is constant, with the values the game saw: the material presets
  take every stage of their states; the texture manager loads textures up to their size (4096 pixels) and caches released
  ones up to a tenth of 256 MB (`TextureManager::kCacheLimit`); mipmapping follows the options; the little-memory branches
  are gone; start-up's "the interface takes the input" byte is always 1 (`RunMainLoop`; app.md).
- No scene bracket, viewport, clears or texture binds on a device: `SceneRenderer::BeginScene` / `EndScene` keep the
  last frame's time, a texture bind only records the stage's holder (`stageSurfaces`, which the renderer draws with), the
  frame clear is the renderer's (its colour is the scene renderer's `fogColour`).
- The present ends the frame (`render::Present`): `EndFrame` draws the frame into the swap chain's texture on the game
  window (gpu.h's `ClaimWindow` / `AcquireSwapchain`: SDL_GPU's chain in its 8-bit SDR format, at the window's pixel
  size each frame), the frame stretched over it, and `Submit` presents it. The movie players and the loading screen
  present through it too.
- **The window** (`src/render/render_window.cpp`): the frame is drawn by a full-screen triangle (shader "ei-present") into
  the client area with its aspect kept, centred, black bars (`render::FrameInWindow`, `WindowToFrame`, `FrameToWindow`).
- **Gamma** as a shader step: `SetGamma` (`renderer_marker.cpp`) keeps the two values, builds the ramp (full scale 65280)
  and hands it to `render::SetGammaRamp`: a 256x1 lookup texture the present shader reads per channel. The ramp applies
  only to what is presented, not to the frame (captures, ReadFrame). Harness build: after a ramp change the present is
  drawn into a texture of the swap chain's size and format too (the swap chain's own is never read back), read back at
  a 17x17 grid and compared with the frame through the ramp ("render: gamma ramp check"; sampled like the uploads).
- **Full screen** (FullScreen 1) without a display mode change (`present_window.cpp`, from each present): a borderless
  window over the monitor it is on (not while minimised; again after someone else resized it), the frame scaled into it;
  a resolution below the desktop's shows in the window. Full screen keeps the settings' resolution; no run may switch
  the display mode.
- **Windowed** (FullScreen 0): a decorated, movable, resizable window centred in the primary monitor's work area, its
  client area the resolution's, made smaller when it would not fit (the frame is scaled into it until the window is
  resized). A window resized by the user renders at its new size ("Resolution changes").
- The game's mouse coordinates are always mapped to frame pixels (platform.md, "Window and events"); over the bars they
  go to the frame's nearest pixel and presses there are ignored ("Resolution changes").
- **Focus loss and return** (Alt+Tab, minimise): the game's own path (app.cpp OnActivate): inactive deactivates the
  cursor, the mouse, the sound and the network; the window stays as it is; active resumes them. Minimised the client area
  is empty: the swap chain gives no texture and nothing is presented.
- Harness steps: `fullscreen 0|1`, `focus off|on`, `windowsize <w> <h>`, `waitpresent <seconds> [gamma]` (a frame
  presented on the game window; with gamma, checked against a ramp that is not the identity). Scenario
  render_present_fullscreen (full screen at 800x600 on the desktop's monitor: menus
  and battle screen, focus lost and given back, the window resized from outside and covering the monitor again).

### No device-loss recovery

A lost device ends the game. SDL_GPU reports no loss of its own: a lost device shows as the next call SDL refuses
(a submission, whose present fails first on Direct3D 12; a pass, a mapping, the swap chain's texture), which ends the
game at once with one message (section 1, "When the renderer fails"). On Direct3D 12 the vendored SDL also keeps the
first error that found the device removed, hung or reset as the device property EI_PROP_GPU_DEVICE_LOST_STRING
(third_party/SDL/README.txt); `gpu::DeviceLost` reads it at each submission and swap chain texture, logs "gpu: the
device was lost: <SDL's error with GetDeviceRemovedReason's text>" once, and a device found lost there is a refused
call too, the message naming that reason. Nothing goes on after a refused call: D7's GPU hang
(DXGI_ERROR_DEVICE_HUNG, a dozen 4K msaa-8 harness games at once) crashed several games when a refused submission
was only logged and the next frame's first uniform push met SDL's Direct3D 12 driver without a uniform buffer (its
mapping refused; a null read at offset 8 in D3D12_INTERNAL_PushUniformData; WARP on the Windows CI runner crashed the
same way, H62f). A device lost while a frame is recorded has SDL's driver drop the pushes and binds of a uniform buffer
it could not take (a local change, third_party/SDL/README.txt) until the submission fails. The harness's
`losedevice <point>` step removes the device for real (ID3D12Device5::RemoveDevice, tests/verify/system/system_win32.cpp;
Direct3D 12 keeps one device per adapter, so D3D12CreateDevice on SDL's adapter gives SDL's) just before the renderer's
next call at one point (`gpu::LoseDeviceAt`: a new command buffer, a pass, a uniform push, the swap chain's texture, a
submission, a write, a download's wait); render_device_lost_<point> check each ends on the message (`expectcrash`).
Never reproduce a loss by hanging the real GPU: overloading it has crashed the machine's Windows. Vulkan's loss shows
only as a refused call. Why no recovery (user): a device is
lost only on a driver update or reset, a TDR or a GPU switch, rare for a player and a restart away. Making everything again needed every module to drop and remake its resources, each decoded
texture to keep its file's bytes, composed textures their recipes, and runtime
backends would need it twice. Dropping the kept file bytes took the process from about 130 MB to about 119 MB private
bytes on zone1.

### Resolution changes

The rendering resolution is the device's `screen` (`TextureDevice::screen`): the settings' `VideoResolution` at the start.
It changes while the game runs through one call, `app::SetResolution(width, height)` (app/app.cpp), between
frames; no device is made again and the zone goes on. It sets, in order:
- the device's screen and the pipeline's viewport and axis swap stacks (`SceneRenderer::ResizeScreen`: stacks 8 and 7
  and their products 0, 1, 2, 5 filled again; the projection, stack 6, is kept, so the camera's near / far / field of
  view stay), and the viewport size the projections map to;
- the interface layout (`screen_layout::Configure`: the 4:3 part, the widescreen margins, the axis and font scales, the
  two layout widths; it logs "Interface layout: ...");
- the cursor's bounds;
- the settings' `VideoResolution` (written at exit: the next start makes the window at that size);
- the interface (`Interface3D::Resize`): its size and its three fonts, every surface placed again
  (`UiSurface::RelayoutAll`: its rectangle scaled from the layout rectangle it was given; what it draws is drawn at the
  new size from the next frame, its texts laid out again in the new fonts), then each window on the stack, bottom first, `UiContainer::LayoutChanged`. The battle screen builds its HUD panels
  again (`BattleScreen::BuildPanels`, at the window's edges, `EdgeShift`), keeping the map's zoom, the top panel's
  texts, the clock's blink and dial, the selection, the portraits and the pending action, and shows the clock and the
  movement panel at once (`HudClock::Show`, `HudMovePanel::Update`); the camp and the multiplayer hero screen move
  their curtains (`screen_layout::PlaceCurtains`); a help or tutorial window builds its page again
  (`HelpWindow::ShowPage`: a zone tutorial's frames at the battle screen's panels follow them to the new edges). Last
  the screen copies behind the windows are taken again (`Interface3D::RetakeBackdrops`): frame steps without a present
  with the stack cut back to what each copy showed, as `PushWindow` took them (the bottom window greyed, with the
  escape menu's save copy; then the windows under a help window, `backdropWindows`). Every other window places its
  panels, regions and sprites in the layout and draws its texts each frame, so it needs nothing.

The renderer follows by itself: the frame target is made again at the next frame at the screen's size (`EnsureTarget`
reads `GameTargetSize`), the swap chain follows the client area, the cursor's shapes are made at the new scale. Movies
are placed from the current screen every frame (`MovieRect`: as large as fits with the aspect kept; mode 1 centred at
the movie's size).

Who calls it: a window that is not full screen and whose client area changed since the last present (dragged,
maximised, a DPI change). `present_window::Update` asks at each present (`app::RequestResolution`) and the next main
loop pass takes it (`TakeResolutionChange`), so a resize during a blocking movie or loading screen is taken after it;
meanwhile the present scales the frame. The window's first client area is not a change.

The options window's resolution (setting 21, the display group's row 9, ours): a drop-down list over the sizes of the
display's own modes (`platform::DisplaySizes`: SDL_GetFullscreenDisplayModes of the window's display, in pixels, sizes
only, each once, narrowest first, none under 800x600 or above the desktop's: `platform/display_sizes.h`, unit check
display_sizes.cpp; the harness's backend returns a fixed list of 19 common sizes, so scenarios step it the same on
every machine) with the screen's own size put in its place when the list lacks it; Enter or a click steps on, Left / Right
(on any spin row) back and on. Its label and tip are ours ("option_resolution": Russian where the game's texts are,
else English; a text the game's files have wins). OK with another size calls `app::ChooseResolution`: the change is
asked for at the next main loop pass (`RequestResolution`, between frames: the windows' LayoutChanged never runs inside
their own input handler) and, in a window that is not full screen, the client area sized to it now
(`present_window::SizeClient` -> `platform::SetClientSize`: placed as the window was made, made smaller when it would
not fit, the frame then scaled into it; logged "present: the window's client area sized to ..."), taken as the
window's own size, so the next present asks for nothing. Full screen only the resolution changes: the frame is scaled
over the monitor, the display mode never changes.

**The graphics rows** (settings 22-26 of the display group, ours; ui.md, "Options data"): row 8 the display mode (full
screen / window), 10 vertical sync, 11 antialiasing (off, 2x, 4x, 8x), 12 anisotropic filtering (off, 2x .. 16x), 13 the
performance overlay. They show the running state (the device's `fullScreen`, `render::Vsync`, `Msaa`, `Anisotropy`,
`PerfOverlay`) and OK applies every change at once, nothing needs a restart:
- The display mode first (`app::ChooseFullScreen` -> `present_window::SetFullScreen`, logged "present: full screen
  without a display mode change" / "present: a window"): `platform::SetFullScreen` makes the window borderless and
  unsizeable (macOS: SDL's desktop full screen) and the next present covers the monitor with it, or decorated and
  resizable again with its client area the screen's size (`SizeClient`). Then the resolution, so a window takes the
  chosen size. The settings' FullScreen follows.
- The renderer's switches through their setters, each from the next frame (the swap chain's present mode at the next
  present), and each changed value into `[renderer]` (VSync, Msaa, Anisotropy, PerfOverlay). The harness build's
  renderer ignores the VSync and overlay switches (sections 15, 15a): those rows are saved but not applied there.
- OK then saves the settings file (`GameOptions::Save`), not only the exit.
Scenario `ui_options_graphics` (every row changed, full screen, then 2560 x 1440; the saved file compared; captures).

**Input in a window of another shape.** Mouse coordinates are frame pixels through the picture's placement
(`render::WindowToFrame`; the cursor's through `platform::MapCursorPoints`). A point over the bars goes to the frame's
nearest pixel (`present_window::MouseToGame`), so the edge pan works at the picture's edges; a press there is not
passed on and is logged ("input: a press outside the picture ignored"), its release is. Inside the frame the picture is
`Interface3D::PictureRect`: the screen less the letterbox bars while `ScreenFade::letterbox` is set (nothing in the
game sets it). A window narrower than 4:3 has the world over its whole height.

**The cursor is confined whenever the edge pan is available** (no option; a stale `ConfineCursor` line in
ei_settings.ini is ignored). `app::UpdateCursorConfinement`, run every main loop pass and at the window's focus
changes, asks `platform::ConfineCursor` (SDL_SetWindowMouseGrab) while the game's window is the active one and
`Interface3D::EdgePanLive` says the top window has an extra input that pans at the edges (`UiInput::PansAtEdges`:
the `CameraInput` of the battle, briefing and view screens with the border scroll area above 0). The main menu, the
escape menu and the modal windows are other top windows, so the cursor is free there; it is let go too when the window
loses the focus or is minimised, and at the focus loss the input is reset (`Interface3D::ResetInput`), so an edge pan
the last mouse motion started does not go on for a cursor outside the window. Scenario `app_cursor_confine` (the
harness's `logmark` / `expectnewlog` / `expectnonewlog` look at the log after the last mark).

Scenarios: `render_options_resolution` (zone1 windowed at 800x600: 1280x720 and then 1280x1024 picked in the options
from the Esc menu), `render_resize_windows` (the Esc menu, the save window, the objectives, a zone tutorial, the camp's
inventory and trade, the global map, each resized while open; `samecapture` against the window opened at that size,
the game's clocks held between), `render_resize_menus` (the main menu, the options over it and their help, the same
way), `render_resize_zone` (zone1 windowed at 800x600, 1280x720, 1000x800 under the Esc menu, back to 800x600),
`render_resize_edgepan` (full screen at 800x600 on a 16:9 monitor: the camera pans at the picture's edges, a click over
the bar ignored), `render_cursor_present`; `ui_resize_text` (the options and the help captured at 800x600, again
after several shrinks and grows: the same frames).

### The cursor

The cursor is the system's with the game's pictures (render_cursor.cpp):
- The frame step hands the renderer the frame's cursor (`render::Cursor`, ui_cursor.cpp): the current image's current
  frame, its mask, its hot spot, and whether the game holds it (with its position). The game's own cursor logic (shapes,
  the 125 ms frame steps, the position clamped to the screen, the hold) is unchanged. The options' FpsIndependentCursor
  is still read and written but chooses nothing.
- At the frame's present (`ShowFrameCursor`) the renderer shows the shape through `platform::SetCursor` and hides it when
  the frame handed none over: the movie players, the loading screens and frames with the image hidden (the original hid
  it there too). Between frames the system moves the cursor: the frame rate no longer limits it.
- A shape is the picture's frame (RGBA, alpha 255 where the mask is zero) scaled to the window as the present scales the
  frame (bilinear on texel centres over the opaque texels, so the edges come out partly transparent as the present's
  filter blended them), then through the gamma ramp, made with `platform::MakeCursor` at its first show and kept: one per
  picture frame, so an animated cursor is a set of cached shapes. Shapes of another window scale or ramp are let go once
  another is shown, those of a picture when its holder goes (`ForgetCursor`).
- The hold (the camera turned with the right button; the global map's drag): `platform::HoldCursor` at the game's
  position (platform.md, "The cursor": relative mode with a one-pixel mouse rectangle), so the cursor stands still while
  the motion reaches the game. On macOS SDL's relative mode freezes the pointer itself
  (CGAssociateMouseAndMouseCursorPosition); two local patches of the vendored SDL's Cocoa backend keep the cursor
  visible there and skip its warp to the window's centre (third_party/SDL/README.txt; macos.md).
- What looks different from the original: the cursor moves at the display's rate whatever the frame rate; over the
  letterbox bars it is where the mouse is (the game's position is clamped to the frame); it is not in the Scroll Lock
  screenshot or the movie recorder's frames.
- Harness: the harness's platform backend makes the same SDL cursors and keeps each shape's pixels; `cursormark`,
  `cursorcheck <name>|- shown|hidden|held|animated|gamma [min]` check them against the game's image at the window's scale
  and count the presents that showed, held or hid it. Scenario `render_cursor_present` (vanilla and Rebirth): the main
  menu one to one and in a 1600x900 window, hidden during a new game's loading screens and a movie, held during a
  right-button turn, the scroll cursor's frames at the window's edge, and the gamma ramp reaching the shape.

### Screenshots and the movie recorder

`surface.cpp`'s capture gets its bitmap from `render::ReadFrame` (the last frame `EndFrame` drew, before the gamma ramp;
no cursor): a Dib of the rectangle's size in the screen's depth, rows bottom-up, the part outside the frame black. No
frame yet: no bitmap (logged once). The file names (`shot%.4d.bmp` for Scroll Lock; the recorder's `<moviepath>shot%.4d.bmp`,
the movie folder defaulting to `.\capture\`), the format and the timing are the original's. The recorder's flag is read
by WinMain and the frame function and cleared on a write error; nothing in the shipped game sets it. Harness: `key
scrolllock`, `record on|off`, `checkshot <n> <frame> [tolerance] [fraction]`; scenario `render_present_shot`. A save's
picture: `checksave <slot> <frame> [tolerance] [fraction]` (ui_quicksave, section 6).

## 10. Projections in plain C++

The original transformed, lit, clipped and fogged on the CPU (3DfpFPU) and drew pre-transformed vertices. What the game
still needs of that is `projection.cpp`, as members of `TransformPipeline` (pipeline.h) and two free functions: `ToClip`
(clip space, the translation added first in each row; the decals' TransformPoints is this with the matrix untransposed),
`ProjectClipped` / `ProjectPointsClipped` (an outcode per point, `clip.h`'s `ClipCodeOf`, and the batch's verdict,
`ClipVerdict`; on screen the points go on to `ToScreen`), `Project` / `ProjectPoints` (the screen matrix's division),
`ProjectSprites`, `ToScreen`, `FogVertices` and `ClipBounds::Fit`. The pipeline keeps the viewport's half size
(`SetViewportSize`).
Checked once bit-identical against the DLL on random inputs (with the far plane it had then).

Behaviour differences (one path for every input; none in a golden):
- A terrain quarter is culled only by its box's `ClipTest`; a quarter whose vertices are all outside one plane is drawn and
  the GPU clips it.
- Ribbon and sprite points at w exactly 0 map with 1 / 0 (infinities) where the DLL used 1e6; the GPU clips them.
- The sprites' matrix is the clip stack's top as it is (the original transposed it through the FPU first).

**Sprites clipped by the GPU** (`renderer_sprites.cpp`): every batch is two triangles per sprite and the GPU clips x and
y. A pre-transformed draw keeps z inside the depth range (the shader clamps it, as Direct3D 7 drew pre-transformed
vertices), so the depth range is the sort's test: a sprite is kept when its z (all four corners share it) lies in 0..1.
Behind the camera 1 / w is negative and z / w lies above 1; at w 0 it is infinite or NaN: both dropped. Differences:
- The original's outcodes moved the centre's clip x and y by `size` / w where the corners lie `size` clip units away (a
  factor w short), so a sprite whose centre was just past an edge was dropped although its quad reached onto the screen
  (sprites popped in and out at the screen's edges). Those sprites are drawn now.
- Sprites nearer than the near plane are dropped before the sort: the order of sprites at exactly the same depth may
  differ.
- A sprite at z exactly +0 is drawn.
- A call with more than 511 sprites lost the colour and texture coordinates of some corners of its later batches in the
  original; each batch is sized before it is filled now.

### The view's depth

The camera's projection (`ProjectionMatrix`, scene/pipeline.cpp) has no far plane: its depth z / w is 1 - near / d (d the
distance along the view, near the settings' NearClipDistance, 1), 0 at the near plane and below 1 at any distance, so
the ground and the map's scenery draw to the map's edge. The original cut everything at FarClipDistance (100 units
along the view): pitched up, the ground ended in a hard edge against the sky and the hills behind it were missing (R62,
the user's 4K frame). In the D32F depth a step at d is about d^2 / near x 6e-8 units: 0.0006 at 100, 0.0054 at 300 (near 1), as
with the far plane (whose depth differed by the factor far / (far - near) only). What follows:
- Drawn to the map's edge: the terrain's sectors and quarters (`SectorData::ClipTest`; their waves move wherever they
  are drawn), the trees, buildings and the other map objects (`CollectVisible`), the vision fog. Picking reads the matrix's x, y and w rows, which the far plane never touched.
- The figures too (units and levers, drawn with their own poses and weather), the casters out of view whose shadows
  `CollectVisible` lists (`ShadowInView`), the particle sources and the lightning: everything in the view volume, at
  any distance (P12; until then they stopped at FarClipDistance, 100 units along the view, a setting no longer read).
  The server sends a player its units only within about 100 units of the camera anyway; what the figures drawn do on
  the client (their poses, footprints, weather, the client generator's draws) goes into visuals.sav and the digests.
  What it cost (P12, quiet runs of the verify build, the Portal zone gz19h from its middle at 3840 x 2160, msaa 1,
  anisotropy 1, 240 frames a view, two runs before and three after): looking along the ground (pitch 5, 30 back) 29
  more meshes past the old reach (1463 to 1492), the main thread 2.27-2.35 to 2.35-2.46 ms a frame, the GPU's median
  2.12-2.36 to 1.96 ms; at pitch 0, 15 and 45 the same meshes and times within the runs' noise (the main thread
  0.77-2.44 ms, the GPU 1.4-2.3 ms by view).
- The distance fog (`render::DistanceFog`, render_state.h; `distance_fog.hlsli`): a world draw's point at depth d
  along the view keeps exp(-((d - start) x density)^2) of its colour past `start`, the rest is the fog's colour, a haze
  that thickens smoothly with distance (nothing up to `start`, flat at first, near full far away). `WorldClient::
  UpdateLighting` sets it by the hour: from 40 units, half at 130 (89% at 200), by day (6 to 18); from 25, half at 90,
  at night (before 2 and from 22); linear between; a cave has the day's. Per vertex, as the fog factor in the specular
  alpha (the smallest of it and the vertex's own is kept): the terrain's vertex shader, the meshes' (`MeshFog::Distance`)
  and the pre-transformed draws' (`FogVertices`, the depth from 1 / w). The world's draws after the sky take it
  (`TextureDevice::fog`), the hit points and the interface do not.
- Where the fog meets the sky: the map's edge fade on the ground (`Terrain::EdgeFade`, the vertex's own, 16 units) is
  widened in the terrain's vertex shader to 0.2 times the depth (`render::MapEdge`, terrain_stages.hlsli's edge_kept): seen
  from far at a grazing angle the 16 units were a few pixels, a hard line. The sky dome (`Sky::Draw`, `MeshFog::Horizon`)
  is hazed into the fog's colour towards the eye's level (none from a rise of 11.5 degrees up, all of it at the eye's
  level and below), so the fogged distance and the map's edge meet a sky of the same colour; pitched up, what lies
  past the map's edge was the dome's lower part, darker than the fog, and the edge drew a line against it. The original's ran linearly from 90 units by day (50 at night) to the far plane at 100 and only
  on the pre-transformed draws; on our meshes it ran on clip w (0.43 times the distance), so it began past the far
  plane and never showed; the ground had only the map's edge fade, so the middle distance had no haze at all.
- The sun's shadow map covers the ground within kShadowReach (100 units) along the view (the fit's sixth plane,
  render_shadows.cpp's ViewPlanes), so its texels are as fine as before, and its shadows fade out over the last 20
  units of distance from the camera (`ShadowParams::fadeFrom` / `fadeLength`): no line marks where they end. The
  clouds' shadows have no end.
- Cost: what is in view is drawn, so a pitched-up view across the map draws more. Zone1 at msaa 8 and anisotropy 16,
  medians of 300 frozen frames (one game at a time, two rounds, NVIDIA RTX 4070 Ti SUPER, Direct3D 12): looking across
  the map from its middle (194 ground pages and 430 meshes instead of 97 and 294) 1.19-1.28 -> 1.54-1.56 ms a frame
  at 1080p (GPU 1.04-1.07 -> 1.26-1.29) and 1.95-1.97 -> 2.25-2.29 ms at 4K (GPU 2.07-2.13 -> 2.16-2.19); from the
  corner over the shore and at the game's default pitch the same as before.
- Check: scenario `render_far_ground` (zone1 at 1920x1080, pitched up to the settings' least pitch, 30 degrees, at the
  farthest zoom, from the map's corner and middle). `groundreach on` measures each frame's ground through the camera's
  clip planes (render_shadows.cpp's MeasureGroundReach: the terrain pieces' runs, the farthest point kept and the runs
  the far side z <= w cuts or leaves out); `groundreach <least>` requires no run cut and the farthest point at least
  that far. With the far plane 71-108 of 451-519 runs were cut and the ground reached 121 units; now none, and 133-204
  units.

## 11. Verification and frame capture

- **Frames are checked within one run**, at a frozen moment: `samecapture` (frames_basic, render_scene, ...: two captures in one frozen moment must match to the pixel); `tools/frame_preview.py` writes a
  downscaled side-by-side PNG. Goldens hold no frames: renderer upgrades replace the old look.
- The water pass writes depth (z-write stays on after the sky): without it the far sea's fully fogged sector ring blended
  over the nearer water at the top of the screen.
- The terrain's pipelines come from the pass state at each page's hand-over (`ReadPassState`), as the meshes' do: ground
  under `kTerrainStage` and water under `kSceneStage`, both culled as the sky leaves it, alpha-blended (SRCALPHA /
  INVSRCALPHA) with z-write on. The terrain shader picks the alpha (texture for ground, vertex colour for water) and always
  adds the specular colour. The harness build logs each new terrain pipeline's state.
- **Terrain sampling**: the pages are sampled anisotropically (16x) with linear mips (`AnisotropicSampler`,
  render_textures.cpp) when stage 0 asks for linear filters (always; the textures keep their mip levels). The game asks for point mips, through which whole terrain triangles snapped to the next
  level and the sharpness jumped along the tiles' edges: seams moving with the camera. The user's own Rebirth setup
  (dgVoodoo, Filtering 16) did not show them; that is the look kept. Check (scenario `render_terrain_seams`):
  `terrainlevels on|point|off` (the ground pages draw one grey per level through the shipped or the point-mip sampler)
  and `levelsteps <frame> <least> <most>` (the fraction of neighbouring level-map pairs whose level jumps); `mmplevels
  <most>` (`render::CheckMmpLevels`): every held .mmp texture's levels, decoded as drawn, against its level 0, both
  box-filtered to 4 x 4.

**Render quality (the smooth option).** The world view multisampled (MSAA) and its models sampled anisotropically, each
with its own level.
- **Settings**, in ei_settings.ini (standalone.md, "Settings file"):
  ```
  [renderer]
  Msaa = 8           1 (off), 2, 4 or 8
  Anisotropy = 16    1 (off) .. 16
  ```
  Read once the renderer starts (`QualitySetting`, device_mesh.cpp); a value missing or not a number is written with its
  default. The options window's antialiasing and anisotropic filtering rows set both while the game runs ("Resolution
  changes", "The graphics rows"). `render::SetMsaa` rounds down to a power of two up to 8 (`kSmoothSamples`), and the target takes the device's
  nearest count (`SamplesFor`: the least it lists at or above the asked one, else the most below: lavapipe lists 1 and 4); `render::SetAnisotropy` clamps to 1..16. Both from the next frame; `Msaa()` / `Anisotropy()` read them.
  **Never in a harness run**: `render::QualitySettingsSupported()` is false in the harness build, so a run starts with both
  off (1) and goldens and frame checks keep that look; the harness steps `msaa <samples>` / `anisotropy <level>` set them.
- The world view (`WorldClient::Render` between `render::BeginWorld` / `EndWorld`: sky, terrain, objects, water, effects,
  flying hit points) is a range of the frame's commands (`Frame::worldRanges`). With anisotropy above 1 its models take
  `AnisotropicSampler` at that level for both stages; the interface's sprites and previews keep `StageSampler`. The terrain
  pages always sample anisotropically at 16.
- With msaa above 1 the world's runs of commands draw into a multisampled colour and depth target (fewer samples when the
  device refuses the most, none when the target cannot be made: logged), and each such pass resolves into the frame target
  at its end; the other runs draw into the frame target directly, so the interface's edges and window surfaces are
  unchanged (a 3D draw's edges run through pixel centres: multisampled, every sprite would get half-covered border pixels).
  Between runs (render_smooth.cpp): after a world run the frame target's depth is set from the resolved depth copy;
  before a world run that follows other draws the frame target's colour and depth go into every sample (a pass of its
  own). The frame target is what every reader takes, always resolved. The water's reflections copy the frame where a
  world view with water ends (render.cpp's WaterCopyPoint, render_water.cpp's CopyFrameForWater): with msaa the
  world's own resolve, without it a pass ending there. A copy before the water's first draw splits the world's
  multisampled pass and resolves it once more (about 0.2-0.25 ms at 3840x2009, msaa 8; SDL_GPU reads a multisampled
  target only through a resolve): taken so only when the last frame left no copy (R17a3; R17a1 took it every frame).
- **The depth copy.** A multisampled depth is never read back: SDL_GPU samples no multisampled texture and resolves
  only colour. So a multisampled pass has a second colour attachment, R32F (`kDepthCopyFormat`), and every pipeline
  made for more than one sample has it (`AddDepthCopy`): written (red only, no blending) when the pipeline writes the
  depth, masked off otherwise. The programs that write the depth output the fragment's depth there; it is cleared to 1
  with the depth, set by the copy into every sample, and resolved with the colour into a plain R32F texture, which
  `depth_from_resolved` writes into the frame target's depth. A pixel inside one surface gets that surface's depth at
  the pixel centre (as without multisampling); an edge pixel the mean over its samples of each one's surface depth at
  the centre. Only draws after the world that test the depth see it. Chosen over drawing the world's depth again
  without multisampling (twice the world's draw calls) and over drawing the interface multisampled (its edges half
  covered). The copy's target is never blended (`PipelineDesc::second`), so a blended draw stores its own depth there. Its
  output has four components with alpha 1 (R32F keeps the red): lavapipe blends the second target with the first's state
  whatever the pipeline says, and a one-component output has alpha 0 there (docs/notes/linux.md, "The harness").
  **Only in frames that read it** (render.cpp's `DepthCopyRead`, taken at the frame's first multisampled pass from the
  recorded commands): a draw that tests the depth (a mesh or terrain batch with the depth test on) after a world run
  before a depth clear of the whole frame, or a world run after such a frame pass (its copy into every sample takes
  the frame target's depth), or a frame not recorded to its end yet (SplitFrame: an interface picture taken in its
  middle), or the harness's `smoothorder split`. Any other frame draws its multisampled passes without the second
  target (no writes, clear or resolve) and leaves the frame target's depth undefined after the world (no
  `depth_from_resolved`): the battle screen and the container screens clear the depth (ClearUiDepth) right after the
  world, so the game's frames there go without it (the battle's selection box, drawn between the two while dragged,
  is a mesh batch with the game's depth state: with the depth test on those frames keep the copy). The pipelines take it by the pass (`MakePassPipeline`,
  `PassDepthCopy`); `ListPipelines` lists the multisampled ones both ways. Its cost was the writes: at 3840x2009,
  msaa 8, render_water's views (the R17a4 kit's frame GPU times, lowest of 6-7 runs on a shared machine) went
  from shore 2.63 / sea 2.66 / lake 3.35 / river 2.55 / river at night 3.19 / lake at night 3.31 ms to 1.92 / 1.88 /
  2.26 / 1.94 / 1.98 / 2.27, 0.6-1.2 ms less; R17a4's bound (the copy written nowhere but still resolved) 2.04-2.40,
  the target kept with its writes masked instead of left out the same as without it. **Not bit-identical**: without
  the copy's output the driver compiles the world's pipelines differently, and an alpha-tested edge sample can
  flip: render_smooth's 960x720 frame (Direct3D 12, NVIDIA) has 2 pixels 3 levels off the frame drawn with the copy
  (masking the writes instead gives the same 2; writing them with no resolve and no `depth_from_resolved` gives none).
  So the frames a check compares are drawn the same way: `smoothorder before` goes without the copy like the game's
  order, and only `split` (tolerance 8) mixes the two.
- Every pipeline drawing into the frame is made for the pass's sample count (`detail::PassSampleCount`): a pipeline's
  count must match its pass's targets. A change of the sample count remakes the targets at a frame's first pass.
- Check (scenario `render_smooth`, vanilla and Rebirth): in zone1 at one frozen moment the frame with msaa 8 and
  anisotropy 16, with both 1 (`plain`) and with them again: the two with them match; `smoothcheck plain smooth 0.002 0.2`
  requires the levels drawn and the frames to differ in 0.2% .. 20% of the pixels; the quick save's picture matches the
  smooth frame; the lower levels each against `plain`. The game never reaches the copy into every sample (its world view
  is a frame's first draw), so the harness build's `smoothorder before|split|check` draws the frozen frame's passes in
  other orders and requires the copies counted. The battle frame in the game's order and in `before` is drawn without
  the depth copy and without `depth_from_resolved` (`smoothorder check` requires it); `split` reads the copy back after
  the world's first half, so every multisampled pass writes it.
- **Edge pixels shaded wrongly** (`smoothedges <plain> <smooth> <tolerance> <most> [<rectangle>]`): a multisampled
  pixel mixes what its neighbours show at their centres, so it lies within the range of its 3 x 3 neighbourhood in the
  same frozen moment drawn without multisampling; the step counts the pixels outside it by more than the tolerance.
  Thin leaves and figures' edges leave a few (5 to 19 in 1280x720); the shore's seams left 463 (scenario
  `render_shore_seams`).

### Frame capture

Harness. `capture <name>` skips two presented frames and writes the next to `frames\<name>.bmp` (24-bit): the renderer's
last frame (`render::ReadFrame`: before the gamma ramp, no cursor). `freeze` / `unfreeze` hold the game's clocks and the
camera (the camera's update runs once per frame even with dt 0 and moves a fixed fraction per call); the main loop then
runs no world, client or camera update. `samecapture <a> <b> [tolerance] [fraction]` fails the run when two captures
differ. `tools/compare_frames.py A B [--mask M1 M2 [--mask-background]] [--exclude E1 E2 ...] [--exclude-grow N] [--diff
OUT]`: per-pixel tolerance (default 8), differing fraction (default limit 0.1%), worst regions. What moves by itself
between frames without the game's clocks (other threads) is not held.

**Bursts** (R56). `capture <name> <count> [freeze] [after <text> | on <window>]` writes `count` presented frames one
after another (`<name>_1` .. `<name>_<count>`; a single `capture` lands only every third frame): from the next present,
from the first present after the console printed a line holding `<text>` (a load's `Loaded <slot>`, a new game's or
zone entry's `Saved savea`) or from the first with `<window>` on top (`menu` after a return to the main menu); with
`freeze` the clocks freeze once the first is written, so the later frames differ from it only by what is drawn late.
The first frame after a load is the first present after it: the battle screen is made by a client tick, so a load with
the clocks already frozen presents nothing. `tools/frame_preview.py` makes the sheets.

**The first frames after a load** (R56; scenario render_load_first_frame: a new game, F8, the Load window, Esc > main
menu > new game, the main menu's load, a zone change). With the clocks frozen from the first frame, every later frame
matches it in the world view to the pixel, also at msaa 8, at night with point shadows and in the town (scratch runs
over 30-40 frames each): no shadow map, point shadow face, terrain or mesh cache entry, occlusion or light starts a
frame late. Unfrozen (also on the real clock) the step from the first frame to the second is no larger than any later
step. What does change on the second frame is the HUD's own animation: the interface's frame time
(`UiContainer::FrameTime`) is 0 on the first frame after a load and the load's length on the next, so the selected
portrait's face, eased from its straight rest towards its selected turn (`HudPortraits::Update`: at once when the step
is 0.5 s or more), snaps into the turn on frame 2, and the clock's marks step (the game's behaviour). The main menu's
first frozen frames differ in a few single pixels (its particles). Its signpost (a unit) cast no shadow until the first
hover, as it played no animation (R58; ui.md, section 4; scenario render_menu_shadow).

**The town's first frames** (R56a; scenario render_load_town, the user's save "ShadowCorrupt", tests/saves/shadow_corrupt:
the town bz1g, its camera panned off the leader, saved from the Esc menu). Frozen bursts hid what moves only when the
clocks run. Three things differed from the settled frames: the camera swung in from its stale pose and glided ~3 units
onto the leader (section 14, "Placed"), and the people with a topic lit up a tick or two late (ui.md, "Briefing / base
screen": the screen marks them on the local view itself). The poses the user saw change were those two (the swing's
other angle, the lit figures' white emissive); no animation starts from a default pose: each unit's first frame shows
its animation where it restarted. On the virtual clock the save's pause (the Esc menu's, saved with the world and
cleared when the screen is made) reaches the client after the first tick, so the first dozen 8 ms frames show the
world still (the fires too) until the unpause arrives; on the real clock the load's long passes (60-160 ms) run those
ticks before the first present. Captures: build/captures/r56a in the main checkout.

## 12. The device layer in game code

All in `src/game`:
- `device_draw.cpp`: texture binds (the stage's holder recorded), the screen bounds of projected positions (picking,
  section 3), the pass hand-over, `SetBlend`, the present.
- `device_mesh.cpp`: the device object (`TextureDevice`) and its mesh draws; the settings the renderer reads at its start
  (quality, overlay, vsync).
- `surface.cpp`: the holders, the channel split, the screenshot capture.
- `texture.cpp`: the texture formats; `texture_load.cpp`: the loads (section 7).
- `renderer_scene.cpp`: counted holders, list iterator copies, aligned blocks, the axis swap matrix (WideScreen changes
  it); `render_helper.cpp`: `RenderHelper`'s built-in presets (section 5).
- `projection.cpp`: the transform pipeline's projections (section 10).
- `figure_part_draw.cpp`: a figure part's draw; `models/link_loader.cpp`: `LinkFigureLoader` (Rebirth's ResOverride
  changes it).
- `weather_field.cpp` (the rain's and the snow's particles: where each is and how much of it shows),
  `weather_particles.cpp` (their draw), `effects_weather.cpp` (the visibility fog's draw, the rain / snow layer's).
- `renderer_lines.cpp`: `LineBatch` (points four to a packet in an aligned array, a count and a colour; `AddPoint`
  appends a point with w 1 and repeats it through the rest of its packet; `Draw` projects with the clip stack's top and
  hands the points to the line strip draw). Users: the figure parts' debug bounds and the world client's line lists; no
  scenario turns either on.

**Weather** (R59, R59a; the game's own draw was replaced). The particles are a field (`weather_field.h`) without
state: a box of 80 x 80 x 40 units that repeats over the world, 20000 raindrops or 16000 snowflakes in it, each one's
point, fall group, size, shade (and a flake's drift, spin and picture) from a hash of its index, worked out once into a
table. Its anchor at a frame is that point carried by the wind so far (`RainSnow::carry`, summed from the world's time
between frames, so a change of wind changes the speed, never the place) and by its fall over the world's time (tick +
tickFraction: it moves every frame, not every tick), taken to the copy of the box around the field's centre: 10 units
beyond the camera's focus along the view, its floor 8 under the focus. Each particle falls at one of four speeds (rain
0.85 to 1.15 of 0.5 units a tick, snow 0.6 to 1.4 of 0.1). A particle leaving the box on one side comes back on the
other, so its alpha fades to nothing over the outer fifth of the box's half side (a circle around the centre, whole at
0.98 of it) and the outer 15% of its half height, and near the eye (from 0.8 to 3 units); `density` / 64 shows that
share of them, each fading over 5% of it. A snowflake also drifts about its anchor: a sine on each horizontal axis, 0.15
to 0.5 units, 4 to 12 seconds a turn (at most 0.65 units away, `kMostDrift`), and spins once a turn of its x drift.
Nothing jumps or vanishes while it shows: the unit check `weather_field.cpp` (tests/unit) follows the particles over 600
frames of a third of a tick with the view panning and the wind changing, none moves further than its speed takes it
nor its alpha by more than 0.2 in a frame; and every particle that shows lies in its bin's boxes.

What a frame costs (R59a): the table is ordered by bins (8 x 8 columns, 4 layers, the 4 fall speeds), so a bin's
particles stay together in a 10 x 10 x 10 box (and the drift); `weather::Field` works out per frame what all particles
share (each axis's wrap, each fall group's height), so an anchor is a few sums. The draw tests each bin's boxes against
the view's seven planes (the screen's edges with a tenth's margin, the depth range, the nearest distance: Ahead and
Inside as planes in the world), then each particle's anchor (with the drift or the streak's reach as a radius), and
only then places it; the ground under it comes from `RainSnow::ground`, the terrain's heights at whole units in a
128 x 128 table that wraps over the world, kept across frames (emptied when the terrain changes), blended between the
four around the point. The snow on gz11k at 1920x1080 (`render_weather_cost`: `renderstats` gives the weather's own
time and vertices, `FrameStats` WeatherMs / WeatherVertices): of 16000 flakes the bins in view hold about 5700, 1300
are on the screen, 700 above the ground are drawn; 0.22 ms a frame. Before R59a it placed all 26000 and looked up the
ground under each (Terrain::SurfaceHeight: two HeightAt, each four SectorAt / VertexAt) and built 25 vertices a flake:
about 4 ms a frame (the user's 120 fps fell to 80).

The draw (`weather_particles.cpp`) projects each particle that shows with the clip stack's top and builds it on the
screen, so its shape in pixels is the same at any aspect: a raindrop is a streak from where it was 1.6 ticks ago to
where it is (the motion blur of its fall and the wind: aligned with its motion on the screen, longer the faster it
crosses the view), alpha 0.45 at the head and 0.1 at the tail, bluish, vertex colours over the white texture, clear at
the edges, at least 1.2 and at most 5 1080p pixels in half width; a drop whose streak is all under the ground is not
drawn. A snowflake is a square of one of the "RainSnow" texture's four flake pictures (its snow row: frames a quarter
of the width wide from v 0.5, kept half a texel inside; vanilla 128 x 128 DXT3, Rebirth 256 x 256 DXT5, the same
layout), turned by its spin, 1.7 times its radius (0.035 to 0.07 units; the pictures fill about 0.6 of their squares)
in half side, at least 4.5 and at most 16 1080p pixels; it fades out over its last 0.5 units above the terrain's ground
or water as it lands (the depth test cut landing flakes in half). Thinner or smaller ones than the least are fainter by
their share of it (so far ones fade instead of flickering). Their colour is the scene's light (`frameAmbient` + 0.6 x
`frameSun`, at least 0.12). `RainSnow::Draw` binds the white texture for rain and the "RainSnow" texture for snow (taken
at the first snow drawn). Drawn as pre-transformed triangles in batches of up to 65535 vertices, clockwise on the screen
(the effects pass culls the other way: R59's first draw showed nothing), fogged by `FogVertices`, with the effects
pass's alpha blend and depth test. The old draw's faults (R59, the user's report at 16:9): its snowflakes were triangles
offset in clip units, 1.75 times too wide at 21:9, and cut the pictures' sides off; its rain cycled every 200 ticks per
cell and wrapped within a band around the camera's height, so drops jumped while seen and when the camera climbed; its
snow was cut off at 30 units under the camera and swayed by tables read by the place modulo 50 (a jump at each wrap);
its 40-unit grid around the camera's upwind point left the sides of a wide view empty. R59 drew the flakes as soft
discs, which the user found did not look like snowflakes (R59a). Checked for the look by `render_weather` and
`render_weather_particles` (rain on zone1, snow on gz11k at 1440x1080, 1920x1080 and 2520x1080: frozen frames against
the same without the particles, bursts of four 16 ms frames, close up at 21:9 and 16:9, a night frame and 3840x2160;
rain about 2.5% of the frame, snow about 0.9%); the goldens do not see it (client-only).

The game's weather comes and goes by itself: WorldServer::RollWeather (every 64 ticks) stops rain or snow with a
chance of 1 in 31 and starts it with 1 in 101, so a minute after `SetWeather("rain")` the rain has often stopped. A
scenario that needs the weather to last asks for it again every 20 seconds (render_weather, render_weather_units).
A layer's change fades over the message's strength in ticks (`SetWeather`: 20 for rain and clear, 40 for snow); from snow
to rain the layer keeps drawing snow, its density from 0 up, and turns to rain at the end of the fade.

## 13. Data kept on the GPU, lighting and shadows

The direction (user): composite in shaders, keep data on the GPU, upload only what changed; no matching of the old
renderer's calls.

**1. Measuring.** `render::FrameStats` (render_checks.h) counts per presented frame (`counts[]` and `ms[]` by its
`Count` and `Time` enums): the hand-over (terrain pages and batches,
meshes and passes, flat geometry, pre-transformed draws, blits, depth clears, state reads, matrices), bytes appended to the
vertex / index streams, uniform bytes, passes, draws, pipeline and first-texture changes, images made per kind with their
bytes. The harness build adds the CPU time (hand-over, texture work, EndFrame; `render::Timer`, render_internal.h: a
timer begun inside another takes its time out of the outer one's) and each terrain page's / mesh's vertices
against the last frame's (same / relit / moved / new). The harness step `renderstats <label>|-` logs the averages since
the last one into the report, with the main thread's CPU and wall time per frame; scenario `render_stats` measures the
main menu, zone1, a fight of twelve creatures, the camp, the base bz1g (a town), the global map and its escape menu.
`EI_PROFILE=1` gives the game side (tools/profile_report.py).
The GPU's time (SDL_GPU has no timestamp queries): the harness step `gputiming frame|pass|off` (`gpu::SetGpuTiming`,
R37e3) polls the submissions' fences from a thread that does not sleep, at time-critical priority; a submission made
while the GPU still runs the one before is timed from that one's end. `frame` times the whole frame; `pass` submits every
pass on its own at its end (the copies waiting before a pass as `upload`), so each is timed, about 0.015 ms of GPU time
more per submission (the save below: 3.47 ms split, 3.25 whole). The next `renderstats` logs (not into the report) a
line "renderstats <label> GPU ms a frame: mean (median, p10, p90; frames, the share of submissions sent while the GPU
ran the one before); by pass: label ms, ..." largest first. Only on a quiet machine, one 4K game at a time: another
build or game makes the poller late (a run's main-thread CPU time above its quiet value says so). A frame that leaves the
GPU idle most of the time may run at lower GPU clocks, so a CPU-bound frame's passes read somewhat dearer than a
GPU-bound one's, and differ by 0.3 ms from run to run: to compare variants of one draw kind, draw it several times in
its pass (R37e10 drew the ground 8 times: the GPU becomes the limit and the variants' differences, divided by the
count, hold to about 0.02 ms), and switch the variants inside one run (a harness step), not between runs. Finer splits (a pass cut by its draws' kind, a resolve in a pass of its own, a part switched off) are
probes: the R37e3 kit shows how (build/r37e3/kit in the main checkout: probe.patch, made on main before `gputiming`,
with the main thread's time by part of the frame; its scenarios; runs.py, its worktree's path at the top; the logs;
the save in build/r37e3/save5, the captures in build/r37e3/rdoc).
RenderDoc 1.46 (E:\bin\renderdoc) captures a frame of a run: the probe loads renderdoc.dll before the device
(`RENDERDOC_GetAPI`, `SetCaptureFilePathTemplate` to a path under build/, `TriggerCapture` at a step), and
`qrenderdoc.exe --python <script>` replays it without the interface (rdoc_analyse.py in the kit: actions by target,
their durations, the command list's calls by name). Its per-event durations time each event alone, the GPU drained
around it (the save's frame: 27 ms summed against 3.3 by fences), so they rank the large draws but do not add up; the
call counts are exact. PIX (installed) also takes captures (WinPixGpuCapturer.dll, `CaptureNextFrame`), but its timing
needs the Performance Log Users group (or an administrator), which the runs lack.

**2. Where the time goes.** The main thread is the frame's limit (1.7-3.8 ms a frame in the zones and the town at 4K,
5.9-7.4 ms with a crowd; the GPU frame 1.5-2.6 ms); draws (a few hundred) and state changes are cheap, and triangles are
few (25-60 K a frame). A many-figure frame's main-thread time is mostly the game's: the server's AI path search in a
fight. So these were measured and not built, each worth well under 0.2 ms a frame: per-draw records and instancing
(the uniforms are 0.03-0.07 ms, mergeable draws about 0.06-0.09 ms); keeping interface sprites on the GPU (0.01-0.1 ms;
they are streamed, since the interface rewrites its quads in place in many places); a GPU ground
decal path (under 0.07 ms). The water's waves went to the vertex shader after all (R37e4c, item 3a: 0.15 + 0.08 ms on
the heavy save once the pages were kept).
Figure animation stays on the CPU: creatures animate by their bones (each part's matrix, which the GPU applies), the
per-vertex morph is almost unused, and its result is read by game code (projection for picking).
On SDL_GPU (R37e2, times by submission) the town at night at 4K, msaa 8 takes the GPU 2.75 ms a frame (2.8 with the
crowd) and the main thread 1.1 (2.1) ms of CPU, so there a frame without a limit waits for the GPU (every submission
found the one before still running); 2.4 ms of it is after the shadow maps (the world's pass, its resolve and the
present).

**2a. A heavy frame (R37e3).** The user's Lost in Astral save (gz1h, desert with hundreds of palms; the camera as a
load leaves it, distance 50, pitch 30; kit in build/r37e3) at 3840 x 2160, msaa 8, anisotropy 16, Direct3D 12 on the
RTX 4070 Ti SUPER, 300 frames, harness (verify) build; beside it R37e2's town bz1g at night (alone / with the crowd).
- **The limit is the main thread**: 9.0-9.4 ms of CPU a frame in the save against 3.25 ms of GPU (no submission found
  the GPU busy). The town: CPU 1.1 (2.2) ms, GPU 2.30 (2.33) ms, every submission waiting, the main thread waiting
  1.15-1.25 ms for the swap chain's texture (R37e2 measured 2.75 ms of GPU there).
- **GPU by pass** (`gputiming pass`; save / town at night, ms): the world's multisampled pass 2.03 / 1.36, of it the ground
  1.1 / 0.68, the caster models 0.48 / 0.51, the other models 0.30 / 0.12, the water 0.10 / 0.05 (the pass cut by its
  draws' kind, without the resolve), the resolve 0.14-0.18 / 0.15 (a pass of its own; RenderDoc 0.14 / 0.12); the
  ambient occlusion 0.46 / 0.41 (half-size depth 0.19 / 0.07, the search 0.17 / 0.21, the two blurs 0.10 / 0.13); the
  sun's map 0.30 / 0.15 with its range passes 0.14 / 0.15; the point lights' faces - / 0.05 (0.07 with the crowd);
  the present 0.17 / 0.13; the interface's pass 0.13 / 0.04; the copies before passes 0.17 / 0.12; the water's frame
  copy and sky 0.08 / 0.10. Corrected by R37e10 (frame-plan.md, "Ground GPU cost"): the ground's draws take 0.85-1.0
  / 0.65-0.75 ms of the world pass (the 1.1 above includes cutting the pass); measured with the ground drawn 8 times, half
  of it is writing 8 samples a pixel (a trivial shader: 0.47 ms a draw at msaa 8, 0.21 at 4, 0.085 at 1) and half shading
  (the sun's PCSS 0.26, the point-light loop 0.19); anisotropy changes nothing. RenderDoc's dearest single events: ground batches (one of 12864 indices 0.26
  ms), the occlusion search, the resolve, the 8-sample colour clear (0.11 ms).
- **The command stream** (RenderDoc, the save's frame): 5754 draws (1894 into the sun's map, 1854 into the occlusion's
  depth, 1858 into the world, 140 for the interface), 23 pipeline changes, 2045 uniform buffer binds, 262 descriptor
  tables, 1294 descriptor copies, 13 vertex and 12 index buffer binds, 38 barriers, one ExecuteCommandLists. The town:
  769 draws, 21 pipelines, 265 uniform binds. gpu.h's filter leaves little for the driver; the cost is ours per draw.
- **The main thread** (save / town crowd, ms a frame, each part without the parts inside it; probe in the kit): the game's
  update 0.55 / 0.35; the world's hand-over 6.0 / 0.9: the static objects' draws 3.1 (the game's figure draws, the
  device's CPU projection of every mesh 0.96 and the renderer's hand-over 0.98 inside), the units' 0 / 0.42, the visible
  objects' walk over the whole map (CollectVisible) 0.70 / 0.08, the old shadow lists 0.27 / 0.03, the ground's pages
  1.08 / 0.04, the water's 0.48 / 0.02, the casters outside the view 0.27 / 0.10, particles 0.004 / 0.04; the interface
  0.17 / 0.002; EndFrame 2.3 / 1.2: the world's model draws 0.93 for 1749 (0.53 us a draw, 1.33 KB of uniforms each,
  2.3 MB a frame) / 0.30 for 409, PrepareShadows 0.40 / 0.13, the sun's map 0.41 for 1894 draws / 0.11, the occlusion's
  depth 0.19 / 0.05, the point faces - / 0.05, the submission 0.15 / 0.10, uploads 0.06. The sampling profile's own
  time (EI_PROFILE, the save): the terrain pages' hand-over (`TerrainPage` 8.3%, `MakeVertex` 3%, `Build`, `BuildPage`,
  `MakeGroups`, `ChangesOf` 5%), the heap (ntdll 6.5%), the device's projection (`TransformPipeline::Project` 5%,
  `ProjectToClip`, `ClipTest`, `PushMatrix` 4.4%), the objects' sort (`object_order::DrawBefore` 4.5%), samplers and
  pipelines found by description at each draw (`MakeSampler`, `SamplerDesc` ==, `PipelineHash` 4.9%), byte compares
  (memcmp 3.1%: the uniform filter), `DrawParts` 3%, `CollectVisible` 2.7%, the driver 2.5%, the casters' sort 1.4%.
- **Top costs**, the save: the world hand-over's figure draws and the device's projection (3.1 ms), the terrain pages
  handed over every frame (1.55 ms), the model draws' per-draw state work in EndFrame (0.93 ms, plus 0.6 ms of shadow
  and occlusion caster draws), the whole-map visibility walk and the objects' sort (about 1.1 ms). The follow-ups
  (R37e4-R37e9) are filed in that order of expected gain; the GPU's ground cost is split in frame-plan.md ("Ground GPU
  cost", R37e10; fixes R37e10a-R37e10e, waiting until the GPU limits play).
- **The terrain pages after R37e4** (the same save and kit, the harness build, ms a frame; the probe split the pages'
  hand-over from the game's walk and the harness's own check): a still page costs its lookups (item 3: the quarters keep
  the sector's move count they were read at, the occlusion's neighbours are looked at once a frame); the ground's pages
  0.68 -> 0.12-0.14, the water's 0.37 -> 0.16-0.19 (12 wave pages read again in one loop of positions and specular
  words), the game's walk over the sectors 0.11 + 0.05 -> 0.09 + 0.04 (the drawn sectors gathered once a pass, not once
  a page): the hand-over 1.20 -> 0.41-0.46 ms of what the shipped build spends; with the waves (AnimateWaves, 0.35 ->
  0.15: `lib::RoundAway` for std::lround) 1.55 -> 0.56-0.61. The harness build's check of every kept page every 16th
  frame (item 3; R37e3's 1.08 + 0.48 counted it) costs another 0.43 ms before, 0.56-0.66 after (it compares the
  colours too); the shipped build has none. The town at night: pages 0.034 -> 0.016, walk 0.012 -> 0.010. Draws: the
  ground's casters into the sun's map one per run of sectors in their pool
  (the save 1894 -> 1875 map draws, the town 487 -> 480); the occlusion's ground depth by pool place whatever the
  texture (103 draws in the save, 28 in the town; the quarters out of view break the runs).
- **The zone's terrain on the GPU (R37e4b)** (the same save and kit, alone on the machine: `work.py test --quiet`; ms a
  frame over the 300 measured frames, where R37e4 averaged its run from the start; the release terms a harness build
  with EI_RENDER_VALIDATION off): the hand-over, the game's walk over the sectors and the two passes' hand-over
  (`TerrainPass`), 0.41-0.46 -> 0.12 (harness build 0.14), with the writes of what moved (the water's ranges as the waves
  move them, UploadTerrain) 0.20 (0.23); with the waves (AnimateWaves 0.15, unchanged) 0.35 (0.38) against 0.56-0.61.
  The main thread's frame: 5.7-5.9 ms in release terms, 7.0 in the harness build (R37e3's 9.0-9.4 was before R37e4 and
  the other frame tickets). Draws: the ground and the water one each in the world pass (108 ground draws before), the
  ground one in the occlusion's depth (103) and one in the sun map (1875 -> 1874 map draws). The town at night: the
  hand-over 0.008 ms.
- **The waves in the vertex shader (R37e4c)** (the same save at the user's settings, msaa 1, anisotropy 1, release terms,
  `work.py test --quiet`, before and after alternated three times; kit: build/r37e4c in the main checkout, the probe
  timing RunTick's sector walk): the walk 0.17 -> 0.012 ms a frame (AnimateWaves gone; the animated tiles are what is
  left), the terrain's vertex writes 334 KB -> 0 a frame (the moved water's, about 0.08 ms in R37e4b); the main
  thread 5.27 / 4.76 / 4.79 -> 4.26 / 4.64 / 4.77 ms (medians 4.79 -> 4.64; the runs move by 0.5 ms, so read the
  parts).

**3. The zone's terrain on the GPU** (`render_terrain.cpp`, R37e4b). The whole landscape is two meshes kept on the
GPU, the ground and the water, each one draw per pass, with the zone's page textures in one Texture2DArray (a vertex
carries its page's layer with its texture coordinates). Each place of the sector grid (`Slot`) holds a fixed range of
both meshes: the sector the game holds there, all its tiles of every page and quarter, in the game's list order, in four
vertex streams (positions; normal, loss, depth, shore and extra; the colour and specular words; u, v, the layer and the
tile's animation, item 3b) and written only when something in it changed. The indices run page by page, each page's sectors by grid rows, each
sector's by quarter: the order the game's page passes drew them in (the ground is blended, so the order is kept). A
range is built again when the grid holds another sector there or the game rebuilt it (`render::TerrainChanged(s,
Rebuilt)` from Build, `Dropped` from Clear and the destructor), its colours and the rest when its ground occlusion was
worked out again (a neighbour came); the meshes are laid out again only when a range's size changed. The game moves some
in place: the water layer's positions at rest at a new water
level (`WaterMoved`, from ApplyWaterLevels when a sector with a pending level change is drawn: its water positions
written, its boxes worked out again). A pass hands over only the sectors it
draws with their quarters' clip verdicts (`TerrainPass`: the game's culling decides what the frame's fit, the water's
mirror level and the batch's presence see; the GPU clips the rest); the page texture of a page is the game's, copied
into its layer when the texture's image changes (`TextureSerial`). Every tile is drawn at full detail at any distance.
The shadow map's fit reads a drawn quarter's vertices in runs of 72 with their boxes (`TerrainPieceGroups`; the water's
at rest). Check: `terraincache off` builds and writes the whole zone at every pass (its frames the same as kept:
render_scene, render_smooth, render_terrain_seams).

**3a. The waves (R37e4c).** Nothing moves the water per frame: its vertices stay on the GPU as built (at rest, with the
water levels) and terrain_water.hlsl's `waved` moves them. Terrain::RunTick only advances the phase (`wavePhase`) and takes the
circling's time (`waveTime`, the client world's tick and fraction times 0.05); the water pass's hand-over takes the
uniforms from them (`TakeWaves`: the wave direction, the time, per water material its turn `fmod(phase * priority, 2
pi)`, lift `waveHeight * priority`, crest colour `waveGlow * priority * 50 * waveColour` and whether it keeps the
ground's x, y; Terrain::WavesOf), and each water vertex carries the rest (its phase offset, Terrain::wavePhases, in
`loss`; its material, column and row in its specular word's colour bytes; the water has a program of its own, no longer a
`vs.water` branch of the ground's, R37e4d). The formula is the one the game's per-vertex pass had (SectorData::Animate): the sine
table's entry (512 entries, the phase rounded half away from zero) at the phase along the wave direction from the
sector's first water vertex (mod 2 pi) plus the material's turn and the vertex's offset lifts the vertex by a quarter of
the lift and gives the crest (the colour bytes rounded, mod 256, where the entry is not negative); the entries at its
column and row a quarter turn apart with the time circle it sideways by three times the lift. A material of kind 4
(keeps the ground's x, y: no map has one; the old pass moved such a vertex only where a water record's depth was
negative, which it never is) gets the crest only. Only the first 32 materials wave (`kWaveMaterials`; maps have up to
7). The game's height queries work the same waves out where asked (Terrain::WaterVertex: the vertices HeightAt's walk
reads, then the plane), with the phase and time of the last RunTick. Client side only: WorldClient::RawSurfaceHeightAt
(Terrain::SurfaceHeight) is read by the flyers' shown height (UnitParams::movementKind 2: Banshee, Bat, Beholder,
Dragon, Succub, Willowisp, Ancient Curse, Shadow; `kFlies`), particles, rain splashes, spell blasts and strikes, insect
swarms and footprints, and SurfaceClass by the shallow water's footstep sounds and prints; the ambience's water vote
reads HeightAt. The camera's terrain sample (SampleTerrain) and the sound listener's height read the water at rest
(Terrain::CalmSurfaceHeight; the user: the camera's bobbing was not intended). The server's height (the AI map's
TopHeightAt) has no waves, so the AI, the server's unit placement and combat never read them.
Against the old per-vertex pass (a temporary check, R37e4c: the old pass kept on copies of every drawn water sector of
zone1 for 2000 updates, every water vertex and 64 random points a sector compared; build/r37e4c in the main checkout,
r37e4c_check.patch): 99.92% of the vertices are bit-identical to the old formula with the phase base read from the
first vertex at rest, the rest a sine table entry apart (the sums' float order; at most 0.00018 units), the heights at
the points 99.86% the same, SurfaceClass always. The old pass read its phase base from its own vertex 0 as the last
frame's waves had moved it (a frame-late feedback: the whole sector's phase shifted by the direction times that
vertex's circling), which the closed form leaves out: against the pass as it was, 5% of the vertices are bit-identical,
moved by 0.0017 units on average (at most 0.0044; zone1's lift is 0.06), the heights by 0.0008 on average (at most
0.005, and 0.04 where a point's walk takes another branch). The old pass also moved only the drawn sectors (the others
kept the waves of their last drawn frame); now every sector waves. The digest's `water` group (scenario
`terrain_water_waves`) hashes WaterVertex's positions; the crests are the GPU's alone.

**3b. Animated tiles on the GPU (R37e4d, R37e4e).** A map lists the textures that animate (`MapAnimatedTexture`: texture,
frames) and each texture has a class whose speed decides how its tiles step (`kAnimationSpeeds`): classes 0 .. 13 step
at every animation step (every third RunTick with `LandscapeFlag::AnimatedTiles`), classes 14 and 15 (speeds 5 and 9)
are at frame 0 and start a cycle at a chance of speed / 1000 per resting step (the game drew a client random number per
resting tile and step for it; no draw is made now). Scan of every map of both installs
(`Maps/*.mpr`: 38 maps in each install, 5,008 sectors, 1,494 of them with water; a throwaway script of that ticket):
**no ground tile of any map uses an animated texture** (0 of the ground layers' tiles; 196,084 water-layer tiles do, in
1,360 sectors: 183,476 of class 6 and 12,608 of class 14); 16 of the 76 maps list no animated texture, the others list a
class 6 one (56 lists) and some also a class 14 one (16 lists; zone1's swamp, sectors 4 .. 5 x 0 .. 1, is one: 1,296
tiles of one 8-frame texture). Classes 0 .. 5 and 7 .. 13 and 15 do not occur. Both
layers keep the path all the same (mods may animate ground tiles; the cost is a compare in the vertex shader and 16
bytes of vertex stream per vertex), and every sector has the same path.
- The GPU works every animated tile's frame out, nothing steps on the CPU: the vertex carries `anim` = (the texture's slot
  in its page, the frame count, the tile's seed, the rate = speed / 1000) and, in `uv`, its coordinates within the page's
  first tile (`Terrain::uvLocal`: `uvTables` without the tile's column and row); `tile_uv` (terrain_stages.hlsli) takes
  the frame from `tile_frame`, the column and row of slot + frame in the page and sums them as the CPU did (`precise`:
  the same rounding, so the coordinates are bit for bit those of the game's tables). Static vertices (anim frames 0)
  have final coordinates. `Terrain::tileSteps` counts the animation steps (RunTick). The seed is the tile's place in the
  map (layer, row and column of the whole map's tiles: `AnimatedTile::seed`, exact in a float below 2^24 tiles), so the
  schedule does not depend on when a sector was built or loaded.
- Rate 1 (classes 0 .. 13): the frame is `(hash(seed) + steps) % frames`: every tile starts at a frame hashed from its
  place and takes every step. Rate below 1 (class 14): the steps are cut into windows of four cycles (`4 * frames`
  steps); a hash of the seed and the window number says whether the tile plays in that window (at the chance that keeps
  the mean number of cycles started per step at the game's: `window * rate / (1 + rate * frames)`, from 5/1000 per
  resting step, a cycle taking `frames` steps) and at which step of the window it starts (uniform over the steps that
  leave room for the cycle); outside the cycle it shows frame 0. Checked against the old rule in a throwaway
  simulation (frames 4, 8, 16, 20 at rate 0.005: cycles started per step 0.0049 / 0.0048 / 0.0047 / 0.0046 against
  the old rule's 0.0049 / 0.0048 / 0.0046 / 0.0046) and in zone1's swamp with a debug lift of the playing tiles (two or
  three dozen of the swamp's tiles in view lifted at a time, a different set in every capture over 1,100 frames).
  The start frame is the hash too (no client random draw in FillTiles any more), so the client generator is not drawn by
  sector building or by the tiles: client effects' draws and `visuals.sav`'s generator changed once (goldens
  re-recorded with R37e4e).
- Every tile steps from the global count, wherever it is (the sector drawn or not, in reach or not): tile frames
  are client-only visuals, and the waves already move in every sector (3a). The game stepped a tile only while its
  sector was drawn and within reach (RunTick), so a sector coming back into view showed the frame it had left at; now
  it shows the frame it would have had. No tile writes to the zone's meshes after the sector is read: `TerrainChange::Moved`,
  the uv range uploads and the RunTick sector walk are gone.
- Cost on the main thread: see the numbers below (R37e4e).
- Check: both suites with `--golden`: the same goldens except the visuals' client generator (R37e4e re-recorded them).


**4. Meshes kept on the GPU** (`render_mesh_cache.cpp`, the pool shared with the terrain in `render_buffers.cpp`:
`GpuPool`; a kept item, page or mesh, is a `KeptRanges`: its place and what is left to write, which `UploadKept`
places and writes, the pool started again with the frame's items when one does not fit). Each figure mesh drawn
through the figure part draws (either through the device or a link figure's part) is
kept under the mesh, in a slot the figure mesh names (`render::MeshSlots`, handed over in `MeshDraw`:
found without a lookup; a slot knows its owner, so a handle copied with a mesh or left after its slot was dropped finds
none and takes a new one): its corners' model-space positions and texture coordinates, the colour and specular pair (a stream of
its own) and the valid triangles; the draw binds the pool with the mesh's first index, the texture offset (animated water
and lava) a shader constant. The game says what changed (`render::MeshDraw`'s owner, link figure and moved positions;
`render::MeshChanged`): an entry is built again when an array or count it was built from differs or the mesh or link
figure was rebuilt (`Rebuilt`; the counters are kept by the object's address and never dropped, so an entry keeps
where it found them); its positions are read again when the animation's per-vertex offsets changed or a link
figure's part was morphed again (`Positions`); its colours when a draw or the game lit or filled them (`Colours`). Only the
changed range of corners is uploaded. A mesh drawn again in the same frame with other data, interface sprites and mesh
draws without a source are streamed. Entries not drawn for 600 frames are dropped. Check: `meshcache on|off|check` (off:
every mesh streamed every frame; check: every kept mesh drawn compared with a fresh build every 16th frame). Risk: a
writer of a mesh's arrays in place that sends no signal; `meshcache check` reports it, and every stale mesh is logged ("a
mesh kept on the GPU was stale") in any run of the harness build.

**5. The game's shadow lists.** The game's own shadows (64 x 64 decals) are not ported; DrawPartsShadow
(map_object_shadow.cpp) fits the shadow's sphere from the object's bounds (map_objects.md, "Shadows": the bounding sphere
swept along the light to the ground) for the shadow lists, which ShadowInView tests for objects out of view. The map's
casters out of view come from the outside walk (item 10).

**6. The device's mesh draws** (`device_mesh.cpp`, `device_draw.cpp`) project a mesh crossing the view's edges (the
visibility verdict) and one whose rectangle is asked (draw flag 1: picking, section 3; a mesh wholly in view is not
projected otherwise), override the colours (draw flag 4: the hand-over reads them) and hand every pass to the renderer. Not made: pre-transformed vertices, the fog, the z scale, the CPU clip of a crossing mesh.

**9. GPU lighting** (`render_lighting.cpp`, `shaders/scene_lighting.hlsli`, `render_terrain.cpp`, `shaders/mesh.hlsl`).
The light objects stay the game's (the sun, its colour and the ambient, the torches and point lights with their positions,
colours and ranges); what they light is done per pixel.
- **The ground.** The world's draw hands over the frame's light (`render::SceneLighting`: the sun, the ambient, the
  landscape's point lights, the camera); the terrain shader lights each pixel by the game's vertex lighting rules
  (ambient times one less the water's loss, the sun's facing over the material's colour, each point light's falloff, its
  specular part) from the vertex normals and material colours. None of the game's vertex lighting runs on the CPU: the
  landscape keeps only the table of shown point lights (`Terrain::lights`, by id: AddLight / RemoveLight), which the
  hand-over reads. `SectorData::Build` writes each vertex's edge fade into its specular word (the fog factor,
  `Terrain::EdgeFade`, rounded to nearest); on the water Animate writes the wave crests over the colour bytes. A sector's
  fade never changes, so it is written once, at Build: no relighting of the terrain, no relight countdowns. Nothing loads or touches sectors for the lighting, so the sector cache's order can differ from the
  original's.
- **The figures.** The part draw (`figure_part_draw.cpp`) hands over `render::MeshLighting`: the model matrix (the world
  stack's top with the scene's camera taken off), the sun, the ambient colour (`AmbientColour`), the material's colours
  times the objects' lighting coefficient, with the part's normals and corner words; `mesh.hlsl`'s `figure_light`
  lights each pixel as 3DfpFPU's LightMesh lit each vertex (below), the point lights in the landscape's space (the part's
  matrix turns and moves only, so its distances are the model space's). The colour arrays hold white with the material's
  alpha (`Material::GpuLitColour`). An interface figure (no landscape rows) takes no point light.
- **The point lights' grid** (R68; `render_lighting.cpp`'s PreparePointLights, `shaders/point_lights.hlsli`). No cap on
  the lights: every kept scene light goes into one storage buffer a frame (its place, 1 / range squared, colour,
  specular flag and its point shadow's map), and the CPU bins the lights whose reach is in the camera's view
  (`SphereInView`) into a grid of square cells over the landscape's x and y around their reach (8 units a cell, larger
  past 128 cells a side), each cell listing the lights whose circle of reach touches it (a second buffer of words: two
  per cell, its first light's word and count, then the lists). The grid's place is in `PointLightParams` (the block the
  point shadows had). The ground, the water, the figure parts and the ground prints look up the pixel's cell and loop
  over its lights. Keep it without a cap on the lights a frame or a part: a cap (the 16 nearest the camera, the 8
  nearest a part) left lights dark in a crowded town at night (the captures page's R68 section). The figure parts take
  the ground's lights, `Terrain::lights` (the lights shown); no CPU walk over the lights per object.
- **Shadows in the light.** On the ground a shadow takes away the sun's part: lit = the smaller of the light with the sun
  hidden (the ambient plus the material's own) and the sunlit light x (1 - hidden x strength x the normal's turn towards
  the map's sun), so never lighter than half the sunlit light; the point lights' light is darkened alike (the towns'
  lamps burn by day too, and their unshadowed light hid the units' shadows). Every other mesh is darkened by (1 - hidden x
  strength) on faces turned to the map's sun by their face normal (`shadow_facing`; not by the smoothed vertex normals,
  which can lean far from their faces). Receivers look up the map at their point moved 1.5 map texels along the normal.
  The ground casts into the map: each grid place's 33 x 33 ground vertices kept in a buffer of their own, one draw for
  the zone; the vertex shader drops the sectors that do not cast this frame (a bit per sector in a uniform, read by
  `SV_VertexID / 1089`), so the casters and the map's near plane stay the game's walk's (depth only, so in any order).
- **Units take no shadow of their own** (a low-poly creature flipped triangle by triangle between shadowed and lit). The
  meshes of a unit's draw (`render::BeginCaster(object, true)`) compare from the unit's nearest depth in the map
  (`FigureFronts`: the least over its meshes of their model-space box corners through their light matrices;
  `MeshBatch::front`), as a plane facing the light, on every face: nothing of the unit is nearer the sun than that, so it
  never darkens itself, and every caster nearer the sun shadows all of it alike. Its sun side is shaded by the lighting's
  N.L alone. Lost: two intertwined units do not shadow each other. Static objects keep the face-normal rule.
- **What stays on the CPU**: the blood prints' levels (saved), the sky's lighting (`SkyObject::Light`, small), the
  terrain's edge fade (also read by the figures' draws and the ground prints).
- **Point-light shadows** (`render_point_shadows.cpp`, `shaders/point_lights.hlsli`). Once a frame (PreparePointShadows,
  from PrepareShadows, counted as the shadow map's) the kept scene lights whose reach is in the camera's view and that may
  cast (`ScenePointLight::shadows`) are weighed by their weight on the screen (the brightest channel x the share of the
  screen their sphere of reach covers, `ScreenShare`); the `kMostPointShadows` (4) weightiest that some caster reaches
  are wanted. Each gets a cube of six 512 x 512 D32 faces (perspective, 90 degrees, near 0.05, far its range) in one
  atlas (a light a row, a face a column). A shadow fades (R68): a row's light (by its id) fades in while it is wanted
  and out while it is not, keeping its row, over 250 ms of the frame clock (`kFadeMs`; the receivers take
  `PointLightParams::shadows`' w of it); a wanted light without a row waits for a free one (a row is freed when its light
  faded out, left the view, went out or shadows nothing) and fades in from nothing: picked anew each frame, the shadows
  popped as the set changed. Casters: the sun map's (strength at
  least 0.5, cut out by alpha alike) whose bounding sphere reaches the light, drawn into each face its sphere touches. An
  object with a mesh sphere within `kHoldMargin` (0.5 units) of the light casts nothing for it (the campfire's spit, a
  lamp's post). An object's own light casts none: the light a party unit gets at night and a quest light are flagged
  `Off` by CreateLight (the unit held it only by the margin, and his own shadow popped in on fast frames). Receivers (only
  what is lit on the GPU): the ground's point loop (looked up moved along the vertex normal; the ground casts none) and the
  figure parts (`MeshBatch::landscape`; a unit's meshes compare from the unit's nearest distance, `MeshBatch::pointFront`;
  other meshes only on faces turned to the light). Filter: 8 bilinear comparisons on a 1.5-texel Vogel disc turned per
  pixel, a 1-texel depth bias, the caster pipeline's slope-scaled bias; a face's lookups stay inside it. A kept light
  names its map in its record (`PointLight::shadow`; `PointShadowFrame::rowLights`). Not done: the ground and objects
  outside the view cast no point shadows.
  The faces are kept across frames: a light keeps its row while it is mapped (`MapLights`), and a face is drawn again
  only when its row's light moved, changed its range or another took the row, or the list of its casters changed
  (`PlaceRow`). A caster is the same when its kept
  mesh and `CachedMeshVersion`, index range, texture, sampler, cut-out and texture offset are, and its model matrix is
  the same (the game's own matrix, not one taken back out from under the camera: R37f13). Only the faces drawn are cleared;
  units' idle animations redraw the faces holding them every frame. PreparePointShadows keeps its working lists across
  frames, makes one sphere per caster mesh (`BoxSphere`) only where the mesh may matter to a candidate light (its placed
  box's middle within the light's range, or `kHoldMargin`, plus a bound on the radius: `PlaceBox`, the middle and |M|
  times the half extents, widened past the rounding) and compares squared distances (`NotFurther` takes a root only
  where a test is too close to tell). The meshes' matrices into the landscape's space are the ones the game handed over
  (`MeshDraw::model`, kept in `Frame::models`, `MeshBatch::placement`) and shared with the roofs; an object's state is found by its place in
  ShadowFrame::casters (section 10), not looked up.
- **Units over a kept still layer: measured, not built** (R37a1; SDL_GPU on Direct3D 12, the town bz1g at night at 4K,
  msaa 8, alone and with a crowd of 12; times by submission on a quiet machine, section 13 item 1; the kit, with the
  tried code as a patch, is build/R37a1 in the main checkout). Tried: the faces as layers of two 2D depth arrays, the
  still casters in one (cleared by the pass's load op), each face a whole-layer GPU copy of its still layer with its
  units drawn over it, a pass per layer. Point draws fell from 135 to 97 (town) and 414 to 377 (crowd), but the point
  pass's GPU time rose from 0.044 to 0.048 ms and 0.052 to 0.057 ms (against the sun map's 0.25 ms: 0.18 -> 0.19 and
  0.21 -> 0.23), and DrawPointShadows' CPU from 0.032 to 0.040 and 0.064 to 0.073 ms; with the passes repeated 20
  times a frame the gap is plain (0.19 -> 0.44 and 0.26 -> 0.57 ms). A layer's pass, copy and barriers cost more than
  the small meshes they save drawing into a 512 x 512 face. Drawing every face every frame instead costs 0.057 / 0.061
  ms, so keeping faces saves about 0.01 ms; on SDL_GPU the point pass is about a fifth of the sun map's time (D3D11's
  timestamps had 0.55). SDL_GPU took what was tried on Direct3D 12 and Vulkan: 2D array depth textures drawn layer by
  layer and sampled as a Texture2DArray, a depth layer copied whole (Direct3D 12 copies a depth subresource only whole).
- Check: render_lighting (zone1's start, creatures on slopes, a fight, zone1 at night, the hero's light at night with a
  goblin and a wolf beside him and running, the town by day and at night with its campfire), render_shadows*,
  render_scene, render_stats.

**10. Object shadows from a sun shadow map** (`render_shadows.cpp`). The game hands over the sun (`Terrain::sun`'s
direction), the object each figure draw's meshes belong to (`BeginCaster` / `EndCaster`) and the objects whose shadow the
game draws this frame (`CastsShadow` after its option, class switch and visibility tests; a unit only once faded in to
half). An object's place in the frame's casters (`ShadowFrame::casters`, in the order first met) is kept on the object
(`render::CasterMark`: the frame and the slot, Object3DClientSpecific::casterMark) and carried by its meshes
(`MeshBatch::casterSlot`): the strength, the units' fronts, their weather boxes and the point shadows' objects are read
by the slot, with no map of objects. Once a frame before the first pass (`PrepareShadows`): the camera's matrix is the world's first ground batch's; its
inverse times a mesh's clip matrix gives the mesh in the landscape's space; a view along the sun's light (tilted to at
least 20 degrees; its x and y the landscape's where a point meets a level plane along the light, an oblique projection) is
fitted around the visible ground within kShadowReach (section 10, "The view's depth"; in steps of 8 units, the texels' corners on whole texels of the landscape at the plane's
height, the visible ground's middle in steps of 8; along the light from the nearest caster that reaches it), 4096 x 4096
D32. The ground is taken in runs of 72 of a terrain piece's
vertices: a run wholly inside the view (widened by 5% at its sides) by its box's corners, one the view's sides cross by
every second vertex inside them, tested against the planes it crosses only. The casters' model meshes (first pass of
each; alpha-tested or blended passes cut out by stage 0's alpha read from the texture's level 0: a smaller level seen
from the sun at a grazing angle averages fronds away) go into it in a depth-only pass. Receivers: the ground pages (not
water: the ground under it), the world's model meshes after the ground (not the sky, the selection markers or additive
passes) and the ground prints sample it (`shaders/shadow_receiver.hlsli`) and darken by up to half (the old decal's alpha
0x80) before fog. The map is the only shadow look.
- **Ground prints** (R52; blood, footprints, scorch marks: flat geometry, `MeshDraw::groundDecal`, `MeshBatch::decal`)
  are blended over the ground, which the map, the clouds and the point shadows darken; a print took none of them, so a
  blood splat in a tree's shadow showed sunlit (the old game drew its shadow decals over the prints). A print is
  darkened as the ground under it: mesh.hlsl's `ground_shade` works the ground's light at the pixel as ground_light
  does on dry ground (no material colour, all of the ambient light; the face normal turned up) with the shadows and
  without them, and multiplies the print by the first over the second per channel. Prints follow the time of day
  (R52a): the game's print colour is unlit (white footprints and scorch marks; blood a grey of the brightest of the
  sun's luminance and the lights reaching it, re-lit only every 10 s), so footprints and scorch marks stayed white at
  night, and blood kept the last grey for up to 10 s (dark under a lamp lit since, bright after nightfall); the
  grey also ignored the night's blue. A print is drawn in its
  colour under the daylight colours (`GroundPrint::DayColour`; `WorldClient::DaylightColours`: the light table's noon,
  a cave's hour 0, handed over in SceneLights) and the second light is at least the ground's light in the daylight
  colours (the same sun direction and normal): at noon, and all day where the table's sun is white (every zone's from
  7 to 17), a print looks as in the game; at night it is as dark against its ground as by day, at dawn it takes the
  ground's tint, under a lamp at night the lamp's light. Its light (`GroundDecalLight`, in the mesh
  shader's `PartLight`, its record's `groundPrint` 1): the scene's sun,
  ambient and the point lights of its pixel's cell (the grid, item 9), with their point shadows, and the map's light
  (`mapSun`); its positions are the landscape's (identity landscape rows), its map lookup lifted along the
  landscape's up (no vertex normals). The specular lights (spell, missile and quest lights: `PointLight::colour`'s w) count in the shade as on the ground (lit = max(other, spec)) and their light is added
  again after the print's colour (rgb x shade + spec, at most 1), as the ground adds it after its texture (R52c:
  before, a print under such a light was darker than its ground by that light times its alpha). No weather on prints. The selection marker stays unshaded (the old game drew it
  after the shadows; it is a mark of the interface). Check: render_shadows_decals (shadowcheck: every print samples
  the map; with `shadowview on` the frame with the prints matches it without them, `decals off`, while the frames
  differ; then at night under a point light and under a lightning spell's light; `printlight`: the prints'
  brightness over the ground's under them, by day, at night and under each light; the spell step's floor is 1.04 on vanilla
  and 1.015 on Rebirth, the same gain of 0.036 from the added light on both: the ratio is over every pixel that differs
  between the two frames, which holds the lightning bolt and the blood far from the light, and Rebirth's gz2g weighs
  them differently; R16j3a; the scenario runs in the Rebirth set through its `rebirth` step).
- **Soft edges** (PCSS, contact-hardening; the user wanted the old decals' softness). After the casters two small passes
  make the map's depth range per 16 x 16-texel cell (`shaders/shadow_reduce.hlsl`: nearest and farthest texel;
  `shadow_dilate.hlsl`: the same over the cells within the widest filter's reach and one cell more, plus the cell's own
  nearest; one 256 x 256 RGBA32F image). Details: "shaders/shadow_receiver.hlsli" below. Radius = the mean blocker
  distance x `kSunSize` (0.01, about twice the real sun's angular radius), 1.5 texels to `kWidestRadius` (0.6 units); 8
  (under 4 texels) or 16 bilinear comparisons on a Vogel disc across the light (drawn onto the map by `disc`), turned per
  2 x 2 quad by interleaved gradient noise, each of the quad's pixels on its own quarter of the spiral. First
  tuned on the town gate at 0.025 (its edge about 125 px at 4K), which blurred a tree's crown, 8-12 units above the
  ground, by 20-30 texels: its cut-out leaves merged into one dark mass (the user's "blocky" tree shadows, R47; measured
  in gz2g: the cut-outs are right, a hard filter shows every leaf, no cut-out casts whole quads). A creature's shadow
  stays sharp at its feet. Check: the render_shadows_* scenarios (gz2g's forest is no longer captured on its own: H74, its code runs in the
  others).
- **Casters outside the view** (shadows popped in at the screen's edge with their objects). The world's draw
  (WorldClient::Render, after its object draws; `world_client_shadow_casters.cpp`) walks the objects that can reach the
  map and hands over the meshes the game did not draw, reading the game's objects only (nothing the goldens record
  moves). A caster shadows the visible ground only if its box seen from the sun (the light-space box of its bounding
  sphere) overlaps the map's x / y area and lies on the sun's side of the ground's far depth (`render::ShadowCasterReaches`);
  the map's depth range starts at the nearest caster that passes (less 2 units), and the caster shader pancakes anything
  nearer onto the near plane. Candidates come from the object grid's 16-unit sectors over the visible ground's box swept
  back towards the sun by the highest object's top over the ground (scanned every 32 frames) over the tangent of the
  sun's elevation, widened by the widest radius (`render::ShadowCasterArea`). The game's own filters stay (its object draw
  flags, a multiplayer spectator's hidden units, CastsShadow's tests). Each part is placed as
  Object3DClientSpecific::DrawParts places it; an object the game drew this frame is left out (`render::CasterDrawn`). Cut out
  by alpha as the on-screen meshes with the same texture were, else at alpha 0.5. A part goes through the world's mesh hand-over as a
  caster alone (R37f14: `FigureMeshDrawer::HandOverCaster` within `BeginCaster` / `EndCaster`, `render::CasterMesh`: a
  batch no command draws, `MeshBatch::casterOnly`, whose pass state is the texture and its cut), kept on the GPU in the
  part's own slot as its world draw keeps it. Per frame a part costs its selection, its placement (object x part
  matrix), the kept mesh's checks and the light x model product of its instance. CPU at
  3840 x 2160 (harness build, its mesh checks included; the shadow map's CPU ms with them / without them, R24): the town
  bz1g 0.23 / 0.14 (the walk 0.05, 120 parts handed over), with the camera moved until the gate leaves the screen
  0.35-0.5 / 0.25-0.4 (the walk 0.07-0.10, 170-180 parts, 9k corners); R20 measured +0.4 and +1.0 when the corners were
  copied each frame.
- **Faces turned away from the sun** have no sun light to lose (the GPU lights them by N.L), so a receiver darkens by its
  own facing with a normal-offset bias. Check: render_shadows_mound (the user's save in tests/saves/shadow_mound).
- **Steady soft edges.** Per-cell reads of the range image made cell-sized squares and straight cuts in a penumbra that
  crawled as the sun turned (the user's shimmer). So
  the range is read bilinearly between the four cells around the pixel (`range_at`: four `texelFetch` / `Load` and a lerp, clamped
  at the map's edges; no filtered RGBA32F read, which Vulkan does not promise), and the
  dilation reaches one cell more (`ShadowFrame::cellsAround`), so each of those four cells' bounds holds for all the pixel's taps and
  their weighted mean is still a bound; the search taps read the cells' own nearest filtered too; the searched radius is
  blended in from the bound between half a cell and a cell. The widest radius is capped to what `kMostCellsAround` cells
  reach. Without a map a 1 x 1 RGBA32F stand-in is bound for the range.
- **Calm penumbra grain** (R25a). The map's axes used to turn with the sun about the landscape's origin, so at every game
  tick each soft edge's taps landed on other texels and its grain changed all at once (render_shadows_shimmer's shadow
  view, frames at 8 ms: 3.3% of the frame by more than 8 between frames across a tick on Rebirth's data, 1.7% on the
  vanilla data; 0.4% between the others, the torches' flames and the units' idle animations). Three changes: the map's
  x and y are the ground's (above), so a texel stays on the ground while the sun turns; the lighting takes the time of
  day at the drawn frame (tick + tickFraction, world_client_lights.cpp), so the sun turns a little every frame instead
  of a step per tick; and a filter wider than a few pixels averages its 2 x 2 quad's four tap sets (each neighbour's
  result moved to the pixel by its gradient, read with `ddx_fine` / `ddy_fine`; each neighbour at most 0.25 from the
  pixel's own, since a helper lane past a silhouette or a fold extrapolates its triangle's plane into the air or into
  the caster), 4 x the taps at the cost of the gradient. Now 0.6-1.0% (Rebirth) and 0.6-0.8% (vanilla) between every
  pair of frames, no tick steps; without the quad average 0.8-1.3%. A filter is at least half a pixel wide (`foot`), so
  a hard edge far away does not crawl from pixel to pixel. Not taken: reuse across frames (a history buffer and its
  reprojection for a small gain), a prefiltered map (its blur loses the contact hardening). GPU cost (the town bz1g at
  4K, msaa 8, day, night and night with a crowd of 12, four runs each interleaved with main's): within 0.01 ms of the
  frame's 2.4-2.8 ms. Harness build: `shadowview on` shows the shadow alone (with the clouds; white lit, black at full strength),
  `shadowshift <n>` moves the map's origin by n x n/2 whole texels. Check: render_shadows_shimmer (the user's save in
  tests/saves/shadow_corrupt).
- **The casters' draws** (both maps, `CasterDraws` in render_internal.h): a draw's own data (`CasterInstance`: the light x
  model matrix, the texture offsets, the alpha cut) goes into an instance-rate vertex buffer, read at the draw's first
  instance (`DrawIndexed`'s first instance; Direct3D's SV_InstanceID leaves the first instance out, an instance-step
  attribute does not), so a
  draw applies no uniforms. A depth-only map with LESS_EQUAL keeps the nearest depth whatever the order, so the draws are
  grouped by what they bind, then by their indices (R37f3): draws of the same geometry (the kept meshes that share one
  copy, the palms of one morph) become one draw of consecutive instances, and every draw of one binding is one
  `DrawIndexedIndirect` call (the commands written once a frame into an indirect buffer; one command alone is a plain
  `DrawIndexed`). The save's sun map: 1874 draws in 41 calls. No comparison sort (R37f7): `Add` finds each draw's binding
  and its group (binding, first index, count) in two open-addressed tables reset by a stamp, and `Sort` places a range's
  draws by group in one counting pass: the frame's stream's bindings, then the kept meshes' (one pipeline change), each
  binding and group in the order the range met it, a group's draws in the order added. The order of bindings differs
  from R37f3's pointer order; the depth does not depend on it, and the calls are the same.
- **The map's GPU time** (R37e2: the town bz1g at night at 4K, msaa 8, with and without a crowd of 12 fighting; times by
  submission, section 13 item 1): 0.155 ms on Direct3D 12 and 0.155-0.17 on Vulkan, the same with the crowd as without.
  Of it the casters' draws are about 0.06 (drawn twice: +0.05); the pass without casters takes 0.09, the same with the
  clear left out (so the clear costs nothing) and with an optimised clear value of 1.0 given to SDL, so it is most
  likely the map's turn from depth target to sampled texture at the pass's end (SDL's barrier, a pass over the 64 MB
  map); the range passes after it 0.12 (the reduction reads the whole map). The 0.4-0.5 ms wait inside the crowd's map
  that R37e1 measured was the D3D11 driver's: its timeline followed the batches the driver sent while the main thread
  was still recording the frame. On SDL_GPU the frame is one command buffer, submitted whole at its end (a GPU capture
  shows one command list: one clear, the casters' draws, one barrier, the two range passes), so the GPU never waits
  inside it.
- How the map's look differs from the old decals: every object whose shadow the game draws and which can reach the view
  casts; the decals of overlapping objects stacked and were laid on the terrain under objects standing higher; a decal's
  direction was the sum of the sun and the point lights in reach, the map's is the sun's alone (the point lights have
  their own maps).
  - Where shadows overlap, the decals each darkened by half (the dirt under the fight's ruin at 21% of its lit
    brightness); the map hides the sun once (43% there). The user kept the strength at 0.5 with no extra darkening
    where shadows overlap (2026-10-21: "Color-wise shadows looked about the same").
  - Near the town bz1g's campfire at night the units' shadows point away from the fire, as the decals' summed
    direction made them, from the fire's cube map (point-light shadows, item 9), beside the moon's map shadow. A unit
    carrying its own light (the hero at night) lights the ground where the fire's shadow of it falls, so that shadow
    hardly shows.
- Check: scenario render_shadows (zone1's start, a fight, creatures on a slope, the town bz1g, each frozen with the map
  twice; `shadowcheck`: map made, casters and receivers, and no object the game shadows and drew left out of the map);
  render_shadows_outside (`shadowcasters all|view|walked`: with the outside casters, without them, and with every caster
  from the walk: the last matches the first, so the walk places, selects and cuts out meshes as the game's draws do; in the
  town with the camera moved the gate's long shadow is there with them and gone without them).

**11. Cloud shadows** (`render_cloud_shadows.cpp`, `shaders/cloud_shadow.hlsli`; R17d). Soft cloud patches drift over
the land by day; client side only, nothing the game simulates reads them, always on (no option). The world's light
(WorldClient::UpdateLighting) hands over the client's weather each frame (`render::CloudShadows`): `day` (0 before
hour 4 and after 20, rising to 1 at 6, falling from 18; 0 in a cave), the rain or snow's strength as its layer fades it
(`RainSnow::density` / 64 while its mode is on), the wind (`RainSnow::wind`, units a tick) and the world's time (tick
+ tickFraction).
- **The map**: 256 x 256 R8, repeating, made once on the CPU at the first frame (no data file): gradient noise over
  five octaves (4 to 64 cells across), each texel replaced by its rank, so the values are spread evenly over 0 .. 1.
- **Two layers** of it: one repeating every 176 units, one every 109 turned by (0.8, 0.6), weighted 0.65 / 0.35. Each
  drifts with the wind (0.11 and 0.15 times the wind a tick: about 0.8 units a second at the default wind of 0.4), the
  second faster, so the patches change their shape as they go. The drift is summed from the world's time between
  frames (a step back or over 64 ticks moves nothing: a load), kept within each layer's repeat in double.
- **Cover**: the share of the ground under clouds, 0.4 clear and 0.8 in full rain or snow, followed over about 150
  ticks; the threshold the layers' sum exceeds on that share comes from the sum's spread (a trapezoid for two even
  spreads, `CoverThreshold`), with a soft edge of 0.09 either side.
- **Projection**: the shadow map's (item 10): its clip x and y are the landscape's x and y where a point meets a level
  plane along the map's light, so a receiver's cloud uv is its shadow coordinate times a scale plus a shift
  (`ShadowParams::clouds` / `cloudShift`, from the map's fit). The clouds lie on the ground, seen from the sun: a high
  point takes them from where the light came through the plane. PrepareShadows fits the map and gives the receivers
  their matrices for the clouds even when nothing casts (the map is then not drawn, `on` 0).
- **Light**: a full cloud hides 0.55 of the sun (times `day`); the receivers take it as more of the sun hidden: hidden
  = 1 - (1 - map) x (1 - cloud), through the shadow's rules (item 9: the ground goes 0.55 of the way to its light in
  the shade, at least 27% darker on faces turned to the sun; a mesh 27% darker on faces turned to it, a unit on every
  face). The water
  takes the clouds alone (`ShadowParams::on` 0: `cloudParams`). Not under clouds: the sky, the selection markers and
  additive passes, as for the map (the ground prints are, as the ground under them).
- Check: render_clouds (zone1 at noon: the frame and the shadow view at one frozen moment and five seconds later,
  `diffcapture` between the views; rain: `cloudcheck` with the cover at 0.7 .. 0.8; night and the cave gz5g:
  `cloudcheck 0 0`). `cloudcheck <least> <most> [<least cover> <most cover>]` reads ShadowInfo's `cloudLight` and
  `cloudCover`; `shadowview on` shows the clouds with the map's shadows.
- GPU cost (the town bz1g by day at 3840 x 2160, msaa 8, alone and with a crowd of 12; whole-frame times by submission,
  item 1; five rounds interleaved with the clouds switched off by a probe): none measurable, the medians 2.21-2.35 ms
  with them and 2.27-2.40 without (the machine's noise is larger than two R8 reads a pixel).

**11a. Ambient occlusion** (R17b; always on, no option). Two parts, both taken off the ambient light alone: the ground's
`ambient` in ground_light and the figures' in figure_light (so in the sun, where the game's max() takes the sun's light,
it hardly shows; in the shade, at night and in the caves it does). Nothing else (the sun, the point lights, the emissive
colour, the meshes the game lights, the water, the interface's previews) is touched.
- **The ground's own** (render_terrain.cpp, `GroundOcclusion`): per sector, once, from the height field (a cell a
  unit): per ground vertex in the grid's eight directions the highest rise within 8 cells above the vertex's own
  tangent plane (sines, the plane's taken off, faded by 1 - (d / 8)^2), the mean of the eight; the vertex keeps 1 -
  0.7 x that (`VertexRest::extra`, an attribute of the ground mesh). The heights around the sector come from the
  neighbours the landscape holds (`Terrain::CachedSector`, so nothing is loaded for it); a neighbour not held continues
  the sector's edge, and the sector's occlusion is worked out again when one comes (its range of the
  ground mesh written again). Large shapes: valleys, the foot of cliffs and walls of rock.
- **The screen's** (render_occlusion.cpp, once a frame after the shadow maps, `DrawOcclusion`): the world's ground (not
  the water) and its models after it that write the depth (cut out by alpha as the sun map's casters; not the sky, the
  flat geometry or the added light) drawn depth only into a D32F target of half the frame's size, through the casters'
  pipelines (`CasterDraws`, the batch's clip matrix as the instance's) and shadow_terrain.hlsl with the camera's matrix
  (`DrawTerrainDepth`: the ground mesh in one draw per clip matrix, its positions and normals streams); then `occlusion.hlsl` per texel (the depth taken back into the landscape's space through the
  camera's inverse; the normal from the nearer neighbour on each axis; in 4 directions on the screen, 4 steps each within
  a radius of 1.5 units, at most 40 texels, the steepest rise above the surface's plane less 0.1, faded by 1 - (d /
  1.5)^2; 1 less the mean), the directions turned and the steps shifted by a 4 x 4 pattern fixed to the screen, and
  `occlusion_blur.hlsl` across and down (9 texels, Gaussian, texels of another depth, by 1/8 of the distance, left out),
  which averages the pattern away: nothing crawls as the camera moves, no history is kept. R8 targets.
- **The receivers** (`shaders/ambient_occlusion.hlsli`): the ground of the world (`TerrainBatch::occluded`) and its
  meshes lit on the GPU after the ground (`MeshBatch::occluded`) read the 2 x 2 texels around their pixel (Gather) with
  their depths, weighted bilinearly and by how near each depth lies to the pixel's own (1 / (1 + (50 x relative
  difference)^2)); when even the nearest is 10-20% of the distance away the pixel's surface is none of theirs (a mesh
  drawn without the depth, something in front) and takes none. The ground multiplies its own occlusion by it. Depths
  compare as the clip w, from the depth by the camera's projection (`z = A + B / w`). The frame is drawn half a pixel
  over (Direct3D 7's centres), the half-size depth is not: the lookup moves back by it.
- Harness build: `occlusion on|off|view|check` (`render::SetOcclusion`; view: the receivers show the occlusion alone,
  white none; check: made, ground and meshes in the depth and taking it, sectors worked out). Check: render_occlusion
  (bz1g by day, at night, a crowd of 12 at night and by day: each on, off and alone; two captures of one moment match),
  render_occlusion_cave (the Dragon's Cave gz5g, "#sky cave"); the frames in build/r17b of the R17b worktree.
- GPU cost (R17b, RTX 4070 Ti SUPER, D3D12, 3840 x 2160 msaa 8, bz1g by day and with a crowd of 12; the depth and
  occlusion passes submitted apart and timed by their fences, on and off alternated twice, 200 frames each): the
  passes 0.30 ms a frame in both (p10-p90 0.29-0.31), the rest of the frame (about 1.9-2.0 ms) no dearer with the
  lookups; the ground's own occlusion costs only when a sector is first held.

**11b. Weather on the surfaces** (`render_weather.cpp`, `shaders/terrain_ground.hlsl`, `shaders/mesh.hlsl`; R17c; always on, no
option). Client side only: driven by the weather the world's light hands over (`render::CloudWeather`, item 11, with
`snow` when the rain / snow layer's mode is snow), nothing the game simulates reads it, and the goldens hold no frames.
- **State** (PrepareSurfaces, from PrepareShadows once a frame): the wetness and the puddles follow the settled rain,
  the snow cover the settled snow (`CloudWeather::settled`, the layer's `targetMode`: the last weather message's, not
  its fade nor its mode, which stays snow through a fade from snow to rain), each by the world's time with its own time
  constant (ticks for 1 - 1/e of the way): wet in 200 (about 11 s) and dry in 1600 (about 90 s), the puddles 450 /
  2400, the snow 1600 (about half the cover in a minute, three quarters in two) and melting in 2400, in rain 900. The
  time only goes forward: the drawn frame's tick fraction can run past the next tick while the client waits for the
  server (app/main_loop.cpp's backlog), so the time handed over steps back a little now and then. Any step back (or over 64
  ticks) used to start the state again from the weather as it was, which in play made the snow and the wetness come at
  once (R53). Only a zone's landscape loaded (`render::LandscapeLoaded`, from Terrain::LoadMap; a save's load goes
  through it too) starts again: for the first 32 ticks after it the state is the settled weather as it is (the weather
  message comes a tick or more after the zone's; a zone entered or loaded in snow is covered at once, in rain wet), then
  it follows; after a load with a side file (11c: a save's, or a zone's left before) the surfaces hold the kept state
  over those 32 ticks instead. The world's time goes on over a zone change (8 ticks), so without the landscape's call
  the ground of gz11k stayed as wet as zone1's. Every load (a save's, a revisited zone's, a multiplayer zone's) keeps
  the saved weather (R57d; world.md, "Weather across a load"), and the zone's first weather message after it has no
  fade, so the client's rain layer is at the saved weather at once. The rain falling now (the layer's density, none in snow) drives the
  rings and splashes.
- **Ground** (terrain_ground.hlsl, `ground_weather`, `weathered`, `wet_ground`; nothing under the water, by `loss`): snow on
  ground turned up: n.z against an edge falling from 1.15 to 0.55 with the cover (0.15 either side of it), broken by the
  cloud map at 1/11 a unit and a little sooner higher up; a dusting, not a blanket (the user: the first look was too
  harsh): at most 0.62 .. 0.87 of the ground by the same noise, the albedo towards a grey white (0.74, 0.76, 0.81),
  0.72 .. 1.08 of it by the texture's light, so the texture shows through. Wet
  ground: the albedo down to 0.6 and a sheen of the sun (Blinn-Phong 40, 0.12). Puddles: on flat ground (n.z 0.86 ..
  0.96), where the cloud map at 1/7 a unit and the ground's own occlusion (low ground: valleys, the foot of slopes) sum
  above an edge falling with the puddles' share; the ground under them darker again (0.55), the sky reflected (the sky
  dome as on the water, Schlick's term plus 0.15) and a sharp glint, on the normal the rain's rings turn.
- **Rings and splashes** (`rain_rings`): two layers of cells 0.8 units across, a drop in the share of the cells the rain's
  strength gives, each cell's ring at its own place and phase on the client's clock (`TerrainGroundFsParams::rain`, the client
  ticks modulo a whole number of both periods: 16 and 21 ticks), its centre anywhere in the middle half of the cell,
  growing to a quarter of a cell and fading; the slope of a Gaussian ring turns the normal, and (`rain_dimples`) the
  ring's crest and a splash (a small soft spot in the first 12% of a ring's time) take up to 0.55 / 0.5 of a lighter
  colour: the water's own towards the sky it reflects (never brighter than that sky). Seen from above, water reflects
  little of the sky whatever the slope, so by the slope alone the rings did not show, and the splashes, once a disc of
  the foam's light colour made opaque, read as isolated bright dots (R53). They fade out where a cell is under a few
  pixels across (`footprint`, the units a pixel spans). On the water (and the swamps) they add to the ripples, which
  the rain makes half again as rough; in a puddle they are blended by the puddle's share.
- **Meshes** (mesh.hlsl, `weathered_mesh`): the world's meshes that receive the shadow map (`MeshBatch::light`) get
  `MeshFsParams::weather` (wetness, snow) and the sun; from the face normal (the map's clip space's derivatives taken to
  the landscape's space, turned to the camera): snow on faces turned up (n.z from 1.1 .. 1.3 down to 0.6 .. 0.85 with the
  cover; at most 0.85 of the face, full cover from a third of it; (0.76, 0.78, 0.83), as grey as the ground's), wet faces down to 0.65 with a sheen of the sun. Without a fitted map (no shadow space) none.
- **Units** (mesh.hlsl, `weathered_unit`; R17c2): a unit's figure part lit on the GPU (`MeshBatch::figure`,
  `MeshFsParams::weather.z`) takes the weather per pixel from its interpolated vertex normal instead (a low-poly figure's
  faces are too few and large for a face normal: a face's sheen flips on and off as it turns). The vertex shader takes the
  normal into the map's clip space by the receiver's matrix (`mapNormal`), the pixel back to the landscape's
  (`map_to_landscape`, as the face normal; right for the figures' rotations and even scales). Snow where the normal turns
  up (n.z plus a noise of +-0.17 fixed to the part's model space, so it rides with the unit, against a soft edge falling
  from 0.95 .. 1.25 to 0.4 .. 0.9 with the cover; its most from half the cover): the heads, shoulders, backs and the tops
  of armour dusted, at most 0.45 .. 0.8 of the part by the noise so its texture shows through (a wolf's back reads as
  dusted, not a white blanket: the user), the meshes' snow colour times the pixel's light, 0.7 .. 1.0 of it by the
  texture's light and +-0.04 by the noise. Wet: down to 0.6, and a sheen: the sky the normal reflects (upward reflections only, so on the same tops; the
  ambient light's colour plus 0.1, (r.z)^3 x (0.3 + Schlick's term) x 0.35) and the sun's highlight (Blinn-Phong 30,
  0.4, as hidden by the map and the clouds). A unit drawn without the GPU's lighting (none known) keeps the face path,
  only darker.
- **Each unit's own weather** (render_weather.cpp, `PrepareUnitWeather`, after PrepareSurfaces; R17c3): the wetness
  and snow a unit's figure parts take (`MeshBatch::weather`) are kept per unit (the object its meshes are handed over
  with, `BeginCaster`). Out of shelter they follow the settled rain / snow (as the ground: State above) with the
  ground's rise (200 / 1600 ticks) on the ground's time, which only goes forward, so a unit in the open is exactly as
  wet as the ground while it rains; a unit dries and loses its snow by itself, faster than the ground (900 / 1800
  ticks: about 50 s and 100 s; in rain its snow melts as the ground's, 900), and under a roof the weather on it is
  scaled by its exposure (1 less its shelter). A unit first met, not met for over 64 ticks (`kUnitForgetTicks`), or
  met while the ground settles after a zone's landscape was loaded (its first 32 ticks) starts from the ground's state
  times its exposure; no step of the time back restarts it. After a save's load the units of its side file (11c) take
  their kept state at the first frame, and every unit holds its state while the ground holds the kept one. Client-side only: no random, nothing the
  game simulates reads it.
  The HUD unit panel's figure takes its unit's own value too (R53a): `HudUnitPanel::DrawSprites` hands its meshes over between
  `render::BeginInterfaceUnit(unit as a MapObject)` and `EndInterfaceUnit` (the object the world's draw gave
  `BeginCaster`), `MeshBatch::interfaceUnit` finds that unit's state (the ground's when the world has not drawn it
  lately), and the mesh shader's `weathered_figure` (`MeshFsParams::weather` z 2) applies it: the snow's and the wet's
  darkening as the world's (`unit_snow_and_wet`) by the vertex normal in model space (which stands up along z as in the
  world; the draw keeps the normals the flat colours would drop), and a fixed sheen on the faces turned up (no map, sun
  or ambient light there). The camp's and the hero screen's previews and UiModel do not call it: they stay dry.
- **The roof test**: a ray straight up on the CPU against the triangles of the frame's other drawn objects (the world's
  model meshes after the ground with a caster object that is not a unit; not the added light), a mesh's box placed in
  the landscape's space (`MeshBatch::placement`) only where a loose box around it (`PlaceBox`) may lie over a unit's points,
  then the ray taken into a mesh's model space and tested
  against its triangles (the kept mesh's corners, `CachedCorner`, or the frame's stream) only where it lies under the
  box. Five rays per unit from its box (the middle and four points a quarter of its width and depth out, at 3/4 of its
  height); the shelter is the share that meets a triangle. Chosen over a top-down depth map: the data is on the CPU
  already, the answer is needed there (a state that changes over time, a number for the harness), and a map would need
  a new pass and a read back a few frames late. Trees' canopies count as roofs (their cards with their cut-out holes:
  a unit under a big tree stays drier); the casters the game does not draw this frame do not (a roof over a drawn unit
  is in view). The ground and the other meshes take the roof map instead (Roofs, below). Cost: a few hundred box tests per unit, the five rays tested together against the triangles of the
  meshes over it; measured 0.005-0.014 ms a frame on average (render_weather, render_weather_units, the shelter scenario;
  1-5 units, 7-44 roof meshes), within shadowMs.
- **Roofs** (render_weather.cpp `PrepareRoofs` / `DrawRoofMap`, shaders/roof.hlsli `roof_open`; R17c5): the ground
  (wetness, puddles, snow), the rain's rings on the water and the world's meshes (`weathered_mesh`) under a roof take no
  weather. A depth map (2048 texels a side, D32F) seen straight down over the visible ground (the sun map's fit's box,
  2 units around, its side in steps of 8 so the texel size changes seldom, the texels on whole texels of the landscape)
  from 24 units above the highest visible ground (a higher roof is drawn at that height: the caster shader's clamp) to 2
  under the lowest. Its roofs: the units' roof meshes (the frame's drawn objects but the units, `CollectUnitsAndRoofs`)
  and the sun map's casters outside the view but the units' (the caster-only batches not `figure`, `DrawRoofMap`): zoomed in, the
  gateway's roof over the visible floor stands above the view's top edge and is not drawn, and the floor stayed wet
  without them. Drawn by the sun map's caster pipelines (`CasterDraws`, cut out by their texture's alpha), only while
  the weather shows (the wetness, puddles, snow or falling rain above 0.001; else no pass, everything in the open). A
  receiver's point (its sun-map clip position through `ShadowParams::roofRows`) is looked up 0.2 units out along its
  normal (a wall's face beside its own top; the meshes' face normal turned to the camera, the ground's normal) and
  counts as open while less than 0.2 units under the highest roof there (a roof's own top, the ground at the foot of a
  rock), over a 3 x 3 tent of comparisons 0.3 units apart (a soft edge about half a unit wide). Units keep their own
  shelter (the roof test above). Plants count as roofs: the ground under a fern or a torch's stand stays dry and bare
  of snow, as under the trees.
- Cost of the units' path (R17c2, the same machine and settings, zone1 a minute into rain with the hero and four
  creatures beside him, frozen; the units' path and the face path alternated every 150 frames by a probe, 1200 frames
  each view; the kit and the captures in build/r17c2 of the main checkout): R17c's view 2.64-2.66 ms either way, zoomed in on the units
  2.60-2.61 ms either way; nothing above the noise.
- Check: scenario `render_weather` (1440x1080, zone1 by day: dry, five seconds into rain (with the particles' share),
  after a minute, half a minute after it stopped, the sea and the beach in rain; gz11k before the snow, five seconds
  into it (at most 0.15 of the cover, the flakes' share) and two minutes (0.5 .. 0.95), then a minute of rain (the cover
  down to at most 0.35); samecapture of a frozen moment drawn twice); `render_weather_units` (3840x2009) close up on the hero with a wolf, a goblin, a boar and a
  skeleton (spawned, skinned, blocked) on zone1 dry and after a minute of rain, and on gz11k before and after two
  minutes of snow. `weathercheck <least wet> <most wet> [<least snow> <most snow>]` reads ShadowInfo's `wetness`,
  `puddles` and `snowCover`. `render_weather_shelter` (bz1g by day, 1440x1080): a minute of rain soaks the hero in the
  open (as wet as the ground), he walks into the gateway of the town's gate (under its roof: shelter 1) and dries for
  half a minute while the ground stays wet (1.0 to about 0.5), then steps out drier than he went in (the roof hides him
  from the camera inside); `unitweather leader|#id|<name> <least wet> <most wet> [<least snow> <most snow> [<least
  shelter> <most shelter>]]` reads `render::UnitWeatherState` (and logs where the unit stood); the frames of R17c3 in
  build/r17c3 of its worktree.
  `render_weather_roofs` (R17c5, bz1g by day, 1440x1080, the camera turned to look through the town gate's gateway): a
  minute of rain, then two of snow; each frozen moment captured with the roofs and without (`roofs off`, harness only:
  everything weathered as in the open); diffcapture requires the gateway's floor under the roof to differ (97-98% of its
  pixels) and the open ground before the gate not to (none); `roofs check <n>` requires the map made with at least n
  roof meshes (50 there). The frames in build/captures/r17c5 of its worktree.
  `render_weather_panel` (R53a, 1440x1080, zone1 by day): the hero dry, then a minute into rain (unitweather: wet
  0.5 .. 1); `diffcapture <a> <b> <least> <most> <tolerance> <left> <top> <right> <bottom>` on the panel's rectangle
  requires the figure to differ between the two frozen moments (a dry figure differs in none of it).
- Cost (R17c, RTX 4070 Ti SUPER, Direct3D 12, 3840x2009 msaa 8 anisotropy 16, zone1 a minute into rain, a frozen
  frame; the uniforms zeroed and set again in turn, 150 frames each): the frame's GPU time 2.59 .. 2.61 ms with the
  weather and 2.59 .. 2.63 ms without, at the start and at the shore; no difference above the noise.

**11c. What a save keeps of the picture** (src/game/client_visuals.{h,cpp}, `render::WeatherToKeep` / `RestoreWeather`
/ `UnitWeatherToKeep` / `RestoreUnitWeather`, render/kept_weather.h; R57, R57c). The user (2026-11-05): a loaded save
shows the weather and the marks it was saved with, through a side file beside the save; the original's files stay as
they are, and a save without the side file starts from the defaults. The same goes for a zone left and entered again,
and each multiplayer client writes its own.
- **What the original's save keeps** (mission.sav, WorldServer::SaveWorld): the world's tick, which the client takes
  with the zone (WorldClient::LoadZone), so every clock on the client's time goes on after a load: the day's light and
  the sun, the sky dome's turn, the rain rings' clock and the water's ripples (`clientTicks`), the animations. The
  weather (`weather`, `weatherStrength`; a load keeps the weather, which the original cleared, and sends it with no
  fade: world.md, "Weather across a load"), the wind, the night level, the clock and its speed, the last rain bolt's tick; the water levels a script
  is still changing (the countdowns); the scripts' FX sources, particles, point lights and lightning; the timed effects
  and the missiles in flight, which the client then shows again.
- **What a load loses** (without the side file):
  - The ground prints: blood, footprints and scorch marks. `GroundPrint::Save` / `Load` (and Footprint's and
    BloodPrint's) exist from the original with no caller; WorldClient::LoadZone clears the three vectors. Kept (Prints).
  - The client's rain or snow layer: not kept, but the zone's first weather message after the load carries the
    saved weather with no fade, so the layer is at it at once (R57d).
  - The landscape's wave phase (`terrain::Terrain::wavePhase`, the water vertices' waves, Terrain::WaterVertex):
    back to 2 pi at every map load; the tiles' animation frame (`Terrain::frame`) back to 1.
  - The client generator (`g_clientRandom`, R34a): seeded once per process (World::Create), so the client's draws
    after a load depended on the session before it. Kept (Generator): the draws after a load follow the save. The
    game's own ISAAC and rand() are not saved (game state, not the picture).
  - Ours: the surfaces' wetness, puddles and snow and each unit's (11b) started again from the settled weather (a
    save loaded in rain was soaked at once, one loaded in snow covered at once); the clouds' drift (a new process starts
    at 0) and their cover (from the target of the rain falling at the first frame, which after a load is none: 0.4).
  - Transient and not kept: the particles of effects in flight start again, the bird, the flying hit points.
- **The side files**: saves\<slot>\visuals.sav, written by GameFlow::SaveGame after scenario.sav (every save: the
  slots, the quick save, the autosave), and saves\current\<zone>.visuals.sav beside the zone's own <zone>.sav,
  written by GameFlow::ProcessRequest when the local client leaves a zone of a kind the game keeps (0 and 2) through a
  zone change (state 4: the server's zone change message, single player and multiplayer alike; LeaveZone writes the
  zone's file right after on a server). Each process writes its own into its own saves folder, so a multiplayer client
  (which runs no server for the zone and never saves a slot) keeps its own, and the host's slot save copies only the
  host's. CopyVisitedZones copies <zone>.visuals.sav with each <zone>.sav (into a slot and back); a zone's file goes
  with ClearSaveFolder (a new game, a load, the host's SetNetworkZone). GameFlow::LoadGame copies the slot's
  visuals.sav into its zone's place in saves\current; WorldClient::LoadZone reads saves\current\<zone>.visuals.sav
  of the zone it loads (its map database record's name, the local player's zone) into `keptVisuals` and removes it:
  one path for a save's load and a revisit, each file used once. SaveFileWrite's frame
  (magic 0x157, the save version 0x74, checksum, compressed), then sections: a u32 kind, a u32 version and a u32 size,
  then the section's bytes; kind 0 ends the list. A load takes a section only at the version it knows (and only when
  its values are numbers and its size is right) and skips any other, so a section whose contents change takes the next
  version and an older file gives the defaults for that part. Sections (version 1 each):
  - Weather (1): wetness, puddles, snow (f32), the clouds' two layers' drift (x, y in f64 each) and cover (f32).
  - Waves (2): the landscape's wave phase (f32).
  - Units (3): a u32 count, then per unit drawn lately its object id (i32), wetness and snow (f32), by ascending id.
  - Prints (4; R57a): the blood prints, the footprints and the scorch marks, each list a u32 count and its prints'
    `Save` (the original's record: place, sizes, growth, life, texture name, tile, angles, lit, alpha scale, lifetime;
    a footprint's scale, kind and ours, its visible byte; a blood print's spin, spread, kind, fresh). Always written
    (three zero counts when there are none); read back only at the restore, where the prints look their textures up.
    A list that runs past the section, or a print whose place or sizes are not numbers or whose life or lifetime is
    not above 0, drops all three lists. The alpha is not saved: Load sets it from the life left, as Tick does.
  - Generator (5; R57b): `g_clientRandom` whole: seeded, count, results and memory (256 u32 each), aa, bb, cc (2068
    bytes). Only when seeded; skipped when its size is wrong or its count above 256. A slot's file only: the
    generator goes on through the zones of a session, so a zone's file leaves it out.
  - ParticleGenerator (8; MF15a): `g_particleRandom` whole, as Generator (2068 bytes, a slot's file only). A file
    with Generator but without it (older saves) seeds the particles' generator from the kept client one at the restore
    (`InitFrom`, as World::Create does), so a save gives the same particles whatever the session before it drew.
  - Zone (6; R57c): the client world's tick when the picture was taken (i32) and the zone's map as the server named
    it (zero-terminated). Every file has it; a file without it (R57's) is taken as it is.
  - WeatherDelay (7): the server's `WorldServer::weatherDelay` (i32, at most 10; world.md, "Weather across a load"):
    not the picture's, but mission.sav does not keep it either. A slot's file only (GameFlow::SaveGame); GameFlow::LoadGame
    reads it from the slot's file before the zone loads (0 when there is none), not at the restore, which comes after
    the server's load.
- **What depends on the frames drawn**: the generator's state at a save (its draws follow frames, the camera and what
  was drawn: idle picks, particles, sounds) and the footprints' places (a unit's animation's steps). Both are
  deterministic on the harness's virtual clock and are the same draws the client units' digests (cunit) already
  follow, so the Linux CI's llvmpipe runs compare them as they compare those; a run cut by its real-time limit
  differs in both.
- **Whose picture**: the restore takes the file only when its map is the loaded zone's and it was taken no later than
  8 ticks after the zone's tick (`kTickSlack`: a slot's picture is taken a tick or two after the tick its mission.sav
  keeps), so a file left from another time or map gives the defaults. The ticks the zone ran since (the loaded tick
  less the kept one: a revisit's catch-up, RestoreZone's, about 8 ticks for a quick one; the time a multiplayer client
  spent away while the host's zone ran on) age the prints: each is ticked as the client world does, and those that
  run out on the way are dropped. The weather is not moved on by them: the zone's weather over that time is not the
  client's to know, and the kept state follows the zone's own after the 32 ticks' hold anyway.
- **The restore**: the end of WorldClient::LoadZone (after the landscape's LandscapeLoaded) restores the surfaces and
  the clouds (`RestoreWeather`: the surfaces hold the kept state while they would settle, 32 ticks; the clouds go on
  from the kept drift and cover), the wave phase, the prints (made again into the lists the load emptied; their
  ground geometry on their first draw) and the client generator (after the landscape's own draws: the animated
  tiles' start frames), and hands the units to the next frame's light
  (WorldClient::UpdateLighting, `ClientVisuals::RestoreUnits`): each kept unit present among the client's objects takes
  its kept wetness and snow (`RestoreUnitWeather`), and holds it while the ground holds. Everything kept is used once,
  so a later zone change starts from the defaults.
- **The cloud cover after a load**: it goes on from the kept cover towards the saved weather's (0.737 to 0.745 at the
  first frames in render_weather_save): the load keeps the world's weather and its first message has no fade, so the
  rain layer is at full density from it (`weatherstate` checks the server's weather and the layer).
- **Left**: a multiplayer client's file that outlives its session (a crash, a host that picks the same map later) is
  only rejected by its map and tick; the host empties its saves\current at every new map (SetNetworkZone), a client
  does not. The harness's `origin` step (a recorded part that begins at a load) still seeds both
  generators again after the load: the client's has drawn in the frames between the load and the origin, which the
  recording and its replay need not draw alike (harness.md, "Parts").
- **Checks**: zone_visuals (zone1 in rain: blood and footprints, then out to bz1g and back: the prints come back aged,
  the ground, the hero and the cover as left; zone_return's goldens hold the zone side files); net_host_death (a host
  alone: z3q2 left to the base and loaded again from saves\current, its log's "client visuals: z3q2 ... restored");
  net_pair_zone_visuals (the peer walks in the host's z3q2, goes back to its own base and in again: its footprints back
  from its own side file, over 80 against about 20 without, while the host stayed);
  render_weather_save (zone1 by day: 15 s of rain, F5, 20 s of drying, F8): `weathercheck` and `unitweather`
  read the ground and the hero exactly as saved (0.759 both; from the settled rain they would be 1) and `cloudcheck`
  the cover near the saved one. ground_prints: `groundprints` counts the blood and footprints at the save and the
  same after its load (none without the side file). Every golden with saves holds its visuals.sav decoded (tools/savefile.py's
  `read_visuals`, field by field in saves.txt), not left out of the comparison.

**12. Profiling the heaviest scenes.** The user's play settings: 3840 x 2160 windowed, msaa 8, anisotropy 16, flora
shadows on, Rebirth's draw radii, `frametime 8`; zone1, zone7 (forest), zone11 (snow forest), zone19, the town bz1g, the
camera at its default and tilted low, noon and night, the town at night with a crowd of 12 fighting. Findings: the main
thread is the limit (wall time equals CPU time; no GPU waits); shadow work is the largest share of the renderer's CPU
(the fit, the walk of outside casters, the passes' submission, point shadows); shadow draws are 50-65% of all draws.

### Loose .bmp textures

Scenario `render_loose_bmp`: `# prepare: make_loose_bmp.py` writes `textures\Zone1000..Zone1007.bmp` (24-bit, four
quadrants: red and green above, blue and yellow below) and an 8-bit `a_<name>.bmp` ramp for each into the run's overlay
(the install has no `textures\*.bmp`). The texture manager then loads zone 1's terrain pages through the .bmp path. The
pages are 5:6:5 (no alpha), so the alpha file is read and its depth checked but its bytes are unused; a texture of an
alpha kind is not loaded from a .bmp by any scenario. `hues <frame> <least> <most>` (`capture::HueShares`) needs each of
the four hues on 8%..25% of the frame.

## 14. The drawn view (smooth camera and object motion, animation blends)

The world ticks every 55 ms. The main loop (`RunPass`, src/game/app/main_loop.cpp) moves the camera once a frame by the
frame's real time (cut into whole ticks), and `RunWorlds` moves it again by a whole tick after each client tick, so half
of every pan, turn and zoom arrives in jumps of 55 ms of motion about 18 times a second (the original's behaviour, jerky on
a 120 Hz screen). The camera (`Camera`: its timing, tracking, height lag, the sound listener) stays as the game has it;
only the pose the world's draw shows changes (always on, no option; moving the camera itself changed the game state).

- **Spread:** around each client tick's camera update RunWorlds notes the pose before it and the step it made
  (`camera::NoteViewBeforeTick` / `NoteViewTick`: focus, distance, the turns as the rotations that undo them, and the
  stretch of the camera's reach from its focus, 1 - before / after). Once a pass `camera::SetViewProgress` takes the
  client's part of the next tick (its backlog over a tick, 0..1). `camera::DrawnView` is the camera's own pose less the
  step times the part not reached: all of the step just after the tick, none a tick later. The position is not spread on
  its own: the camera's offset from its focus turns back with the rotation and its length goes back by the stretch, so in
  a turn the drawn camera stays on the sphere around the drawn focus (spread along the chord, a fast spin pulled it in).
  With nothing to take back the drawn pose is the camera's own to the bit. No tick for longer (a paused or stalled
  client): the camera's own pose is drawn. A pass longer than a tick spreads less, linearly to none at two ticks (in real
  ticks: elapsed / TickLength): the harness's default pass of five ticks draws the camera's own pose, which matters to the
  goldens (picking reads the drawn view). That fade is taken once, by the pass that ran the tick, and kept for the step,
  so later passes only reach further into the step and the drawn view never moves back.
- **Placed:** a placement of the camera outside its update forgets the step (`camera::ForgetViewStep`: SetFocus,
  SetHeading, RestoreView, SetDistancePitch, LookFrom, PlaybackStart, Init, Load), so the next frame draws the placed
  pose. Until a frame draws the world after it, every tick's step is a cut: a load leaves the camera's own position
  and turn as they were (only its update places them by the loaded focus), so the first tick's step began at a pose no
  frame showed and the first frames after the user's town save swung in from there (R56a). A target given in that time
  (`Camera::SetTargetId`: the base and battle screens take the leader as they are made after a load or a zone's entry)
  is drawn as reached: the gap from the camera's own focus to where the update's drift (a fifth of the way a call) and
  height lag (a tenth) settle it is added to the drawn focus and position (`camera::SettleBy`), and the update shrinks
  it by the same parts (both are linear, so it stays exact wherever the target goes) until it is gone. The camera's own
  focus keeps the game's glide: it is saved (scenario.sav's camera) and clicks pick through it, and moving it moved 153
  goldens (savea's camera focus, units 1e-5 apart in the recorded and network scenarios). A pan or a lost target drops
  the gap; later targets keep the glide (a click on another hero).
- **Rotations of other lengths:** a camera file's key may hold a rotation that is not of unit length (Lost in Astral's
  `Camera\mainmenu.cam`, its first key 1.16 long, which the main menu places the camera by; the game draws it as it is,
  a stretched view). The rotation that undoes a step is `before * now^-1` with the inverse (the conjugate over the
  length squared): with the conjugate a camera standing still undid by a scale of 1.35, and the frames between ticks
  zoomed the menu in and out (scenario mod_lost_in_astral_menu_camera, 16 ms frames).
- **Following** (flag 8, only from a save that has it; nothing in the game sets it): the camera sits on the target and
  its focus stays where it was, so the drawn position turns about that stale focus. Not reached in play.
- **Cut:** a tick whose update moved the focus by more than 8 map units, or the camera nearer to its focus or farther
  from it by more, placed it anew: no step, shown at once. A turn, pan, tilt, zoom or lift stays within both in a tick
  however fast it goes (a test on the turn's angle once took a fast spin for a cut and made it jerk).
- **Where it is read:** `WorldClient::Render` takes the frame's pose once at its start (`camera::TakeFrameView`); what the
  world's draw places by the camera reads `camera::FrameView()`: the view matrix (`view::ViewMatrix`), the sky dome,
  particle billboards, lightning's glow, rain and snow's band, the lighting's eye (`HandOverSceneLights`). The terrain's
  visible sectors keep the camera's own position. The listener stays with the camera. Picking reads the last drawn frame's
  matrices, so a click picks what the screen shows.
- **Lag:** the drawn view trails the camera by up to a tick's step.

### Spinning

Zoomed in and tilted low, a turn swings the camera over the ground and houses behind it: `ClampAboveObjects` keeps it
above the highest terrain within 2 units and the objects within 16 (fading over 10) plus CameraMinHeight, and `ApplyView`
aims it back down at the focus, so over one turn it rises and its pitch changes (the original's behaviour, matched to the
last digit). The drawn view spreads those tick steps. The mouse turn's rate is the drag in layout pixels (the counts
scaled to 800x600): the same counts turn the camera faster at a lower resolution (the original's).

### Objects

Units already move without jumps at any frame rate: the client places each on its motion path at the frame's time
(`UnitClientSpecific::AdvanceTime` from CollectVisible: PlaceOnPath samples the path at tick + fraction, the heading
turns per frame), the original's own interpolation; particles likewise draw between their last two ticks' places. What
moves only in client ticks is drawn as the camera is: a bow's arrow (EffectArrowFigure moves its figure each tick), map
objects the server's updates move, a held unit (pathTimeShift 1, not placed on its path).
- `drawn::PoseOf` (drawn_motion.cpp) is the object's own pose less the part of its last tick's step the frame has not
  reached (`drawn::Back`: the camera's spread and progress, so none at the harness's default pass of five ticks: the
  goldens' frames draw the own poses), the position along the step, the rotation slerped. Always on, no option.
- The step: `drawn::Observe` in CollectVisible (every object of the map's grid each frame; a unit before its
  AdvanceTime): the first look after a tick takes the pose at the look after the tick before and the pose now. Kept
  only when one tick passed between the two looks, nothing placed the object at a frame's time between them
  (`drawn::NotePlaced` after a unit's AdvanceTime), it moved, and by at most 8 map units (the camera's cut). An object
  placed at the frame's time (pose no longer the step's end) is drawn where it is.
- Read by the object's matrix in CollectVisible (the clip test) and `Object3DClientSpecific::DrawParts` (so picking's part
  rectangles follow what is drawn, as with the camera) and the outside shadow casters. The own pose (localPosition /
  localRotation: digests, saves, AI, sounds, PartWorldPosition) never changes.
- **Path changes:** a unit's new client path (UnitClientSpecific::MotionChanged, from the server's update) can start a
  little off where the unit was drawn (0.25-0.35 units when the hero sets off), so its placement jumps once. The jump
  from the place last shown (`drawn::Show`) fades out over 150 ms of real time (smoothstep), times the pass's spread
  (none at the harness's default pass), unless longer than the cut.
- **Interface figures:** a unit the interface draws (the HUD unit panel's, top left; the camp's and hero screen's
  previews; MapObjectFlag::NoEdgeFade during the draw) is placed by the interface in its own view, so `drawn::PoseOf`
  gives it its own pose: the world's step and path change jump would move it there (the panel's figure jumped about
  0.16 units up and right at each set-off, fading over the 150 ms, which looked like the animation switch's doing).
- Lag: up to a tick's step; an arrow vanishes up to one step short of its target (it is removed in the tick it arrives,
  so the original never drew it there either).

### Lights

The lights an object carries move with what is drawn, not with their tick place (always on): `drawn::LightPosition`
(drawn_motion.cpp) is the place the draw shows a light this pass; `Light::localPosition` stays the game's.
- **Carried:** an object's own light (a party unit's at night, a quest light: `MapObjectClientSpecific::light`, which
  RunTick puts kLightHeight above the object at each tick, so it stepped behind the unit drawn on its path) is drawn
  kLightHeight above the object's drawn place: CollectVisible hands it over (`drawn::Carry`) right after `drawn::Show`.
- **Free:** any other light (a spell's or a missile's EffectPointLight moving by its velocity each client tick, a
  script's MovePointLight, a torch's) is drawn as an object moved in ticks: its own place less the part of its last tick's
  step not reached (`Light::drawnStep`, the first look after a tick takes the step; one tick between looks, at most the
  cut). None at the harness's default pass.
- **Readers:** the GPU's point lights (`HandOverSceneLights`: the ground's, the figure parts' and the point shadows'), so
  the scene's hand-over runs in DrawPass after the objects' placement (CollectVisible); the draws read it when the frame
  is drawn. The game's own readers keep the game's place: the shadow sphere's light direction (`FitShadowSphere`), the
  blood prints' grey, the map's light marks.
- Check: `render_drawn_lights` (vanilla, 8 ms frames, zone1 at night): the hero walks with its light traced
  (`objtrace start leader light`: drawn without a jump, own with one at each tick, and the light's largest drawn step at
  most 1.1 of the unit's: 0.036 both; the game's place stepped 0.217 a tick), then a fireball's light and its blast's
  rising one (`objtrace start spelllight`: drawn without a jump, largest step 0.097 against the own 0.667 in flight,
  0.022 against 0.150 rising). A script cast's flight light stands at height 0 and lights nothing visible.

### Animation blends

A switch from one animation to another (`FigureAnimationState::Play`, or SetTime passing into the next queued one)
blends the pose last placed into the new one's (models/animation.cpp, models/animation_bones.cpp). Always on.
- `BeginBlend` keeps each placed bone's own turn, the root's position before the origin and the mesh's offsets as the
  outgoing pose; `FigureBone::Place(origin, blend)` slerps the turn and lerps the root's position and the morph offsets
  by the incoming share (at 1 the original's computation, bit for bit); `UpdatePose` places the skeleton again at every
  draw while a blend runs.
- The share: smoothstep of the real time since the outgoing pose was placed (`placedAt`: the draw before the
  switch; platform milliseconds, so a blend running also finishes in a pause) over the length, by the incoming
  animation's kind: attack, cast and hit 100 ms (the blow's moment reads at once), death 150, idle 250, the rest
  (walk, run, cross, special, map objects' None) 200. None from or into no animation (a figure built anew, a fresh
  unit). An object not drawn for longer than the length switches at once.
- **First shown pose** (R65): a switch blends only from a pose the player saw, in a figure seen moving
  (`poseShown`: the pose last placed was drawn, `PlaceForDraw` from DrawParts and the out-of-view shadow casters;
  `seenMoving`: a placement once replaced a drawn pose). So a figure's first shown pose is its current animation's,
  with no blend. Before R65 a load's units restarted their saved animation and passed into the queued one before
  any draw (RestartAnimation's Advance), blending from the rest pose `SetUpAnimatedFigure` placed: the HUD's hero
  swung out of a T pose (the user's Lost in Astral save, saved paused, so it stayed half-way until the blend's real
  time ran out). And back in the main menu after a game, its creatures arrive in the server's state before its first
  tick (special idle) and are drawn frozen (the client world is still paused from the game's Esc menu) until the
  tick sets them walking: they all blended into the walk at once. Now they switch at once. A figure first shown in a
  paused world (a paused save) likewise takes its first switch after the unpause at once. Scenario
  mod_lost_in_astral_first_pose (`animblend count`, the HUD figure's first frames against a later one).
- Goldens: the harness's default frames are 275 ms apart, longer than any blend, so each draw shows the new pose
  alone, as before. A blend counted from the switch itself drew the outgoing pose on the switch's frame: its parts'
  clip tests then differed at the screen's edge, and with them a client unit's Static flag (all its parts lit),
  which the digests read (zone_gz15h_tornado, net_pair_quest differed).
- Client side only: what plays, its frame, the end callback, hit moments and the server's view are the game's. What
  reads the placed parts sees the blended pose, all on the client: the part rectangles picking tests (a click picks what
  the screen shows), PartWorldPosition (a hero's effect point, footprints, blood, attached particles).

### Checks

- `render_drawn_motion` (vanilla and Rebirth), at 8 ms frames: the hero walks back over a new path's start
  (`objtrace` of the leader: drawn without a jump, own with the path change's one, the same path within 1%; captures
  `moving_*`), stands until an idle plays and sets off (idle, `scross20`, `cwalk05`; its right hand "rh3" without a
  jump; captures `idle_run_*`). Vanilla only (Rebirth's weapon bar click misses, and the wolf's place is out of reach on
  foot): with a bow it shoots a wolf standing with the blends off (`animblend off`: the hand jumps at the switch) and
  a second one walking (`cwalk05` into `uattack02`, no jump; captures `walk_attack_*`), and its arrow flies
  (`objtrace arrow`: drawn without a jump, own jerky, the same path within 25%). A hero standing after a walk in zone1
  keeps its walk animation until an idle starts (a few seconds).
- `render_preview_blend` (vanilla), at 8 ms frames: the hero, standing, is sent off (a path change and an
  idle into a cross); `objtrace start leader lastdraw` follows where the unit's last draw put it each frame (the HUD
  unit panel's draw), which must stay still (`camsteadier panel walk pan 0.05`: its largest step against the walk's;
  0.159 with the path change's jump in the panel, 0 without). Captures `panel_*`.
- `camera_smooth` (vanilla and Rebirth): pan, turn (Ctrl), zoom (Page Down) and follow (Home, the hero walking).
  `camtrace` keeps each recording as the drawn view (`pan`) and the camera's own (`pan.camera`). `camsame` (3%): the drawn
  view covers what the camera does. `camsmooth`: the drawn view without a jump, the camera with (a jump is a frame whose
  step is more than twice both neighbours').
- `camera_spin`: the low keyboard spin (`camsteadier`: the drawn height's largest step at most 0.35 of the camera's) and the
  fast drag zoomed out (turn without jumps; `camsame ... reach`: the drawn view's distance from its focus changes as the
  camera's own does, within 3%).
- No scenario clicks a unit mid-pan on purpose (H74: the camera and picking code runs in the camera scenarios). What
  the drawn view feeds (the Shown flags, the
  visible and shadow lists, picking) must not reach the world's state.
- Harness: `keydown` / `keyup` (a key held over frames), `frametime <ms>`, `mousedrag <dx> <dy>`, `camtrace` / `camsame` /
  `camsteadier` / `camsmooth` (tests/verify/harness_camera.h; kinds pan, turn, zoom, height, reach); `objtrace start <who>
  [part|-] [log]` / `objtrace stop <name>` (an object's drawn and own place, or a part's place in the figure, or with
  part `lastdraw` where its last draw put it, or with part `light` its own light's, or with who `spelllight` the first
  moving point light effect's, after each frame's draw; the same checks; also after `vanilla` / `rebirth`), `objplayed
  <animation> [past <frame>]` (the traced object played that database entry in a traced frame, with `past` to a frame
  of it beyond <frame>: `mod_lost_in_astral_unarmed_search`, `mod_gltf_animation`),
  `animblend on|off`, `animblend mark` / `animblend count <least> <most>` (the blends begun in world figures, not
  the interface's previews, since the mark).

## 15. The performance overlay

- **Switch**: in ei_settings.ini (standalone.md, "Settings file"):
  ```
  [renderer]
  PerfOverlay = true
  ```
  Off when absent or false (a new file has it false). Read once the renderer starts (`PerfOverlaySetting`,
  device_mesh.cpp) into `render::SetPerfOverlay`; the options window's performance overlay row switches it live. **Never in a harness run**: `render::PerfOverlaySupported()` is false in
  the harness build.
- **What it shows** (top left of the game image, refreshed every 250 ms, averages over the last second):
  ```
  FPS 144   FRAME 6.96 MS   WORST 9.13
  GPU 2.10 MS   WORST 2.60
  CPU SUBMIT 1.10 MS   SHADOW 0.12
  CALLS (DRAWS) WORLD 37 (370)   SUN 12 (120)   POINT 8 (80)   UI 7 (25)
  ```
  `FPS` and the frame time (between presents, so vsync's wait counts) with the worst frame; `GPU` the whole frame's GPU
  time (one submission's work, the present's pass with it; the worst too: section 15c, "The frame's GPU time"). `CPU
  SUBMIT` the main thread's time in EndFrame's draws up to the present; `SHADOW` the shadow maps' fitting and passes
  (part of SUBMIT) and the world draw's walk of the casters outside the view (before it); `CALLS` the frame's API draw
  calls by pass (`perf::Pass`: the sun's map with its range passes, the point lights' faces, the world view, the
  interface), the batches they drew (the logical draws, renderstats' "draws") in brackets: a run of meshes is one call.
  Calls are `DrawCalls + ShadowCalls`, draws `Draws + ShadowDraws` of `FrameStats` (the same numbers as renderstats'
  "N draws in M calls" and "shadow maps N draws in M calls"). `GPU -` until a frame's time comes back.
- **How** (`render_perf.{h,cpp}`, `shaders/perf_overlay.hlsl`): `BeginDraws` at the first DrawPending begins the frame
  in UI, `Enter(pass)` marks each change of pass for the call and draw counts, `EndDraws` after DrawToWindow; `FrameDone` at the
  present takes the latest `gpu::FrameGpuTime` (the frame timing is on while the overlay is). The text is the
  interface's (render_text.h: Liberation Mono as "Courier New", laid out by `text::Layout`, composed white on black by
  `ComposeAgainstBlack`), `FontPixels` = one pixel per 60 rows of the game image (at least 10: 18 px at 1080, 36 at
  4K), into an R8 coverage texture (`Box`) written before the present's pass when the text or the size changed, and
  drawn as one quad, 1:1, over the frame's top left corner; the shader blends the coverage over a 60% black backing
  (the frame's other 40% shows through the letters' edges), in the present's pass after the frame.
- **Never in the frame target**: captures, frame checks, the screenshot, the movie recorder and the escape menu's / save
  game's pictures read the frame target, which never holds it.
- **Check**: `ei_perf_check` (tools/perf_check, a build check) on gpu.h's device (hardware Direct3D 12): the lines of
  known numbers, the font's size by height, the box's size and ink; the overlay over a cleared BGRA8 target at 720,
  1080 and 2160 rows against the box's coverage; frames through every pass (each pass's calls and draws, the CPU times
  and the whole frame's GPU time come back). `ei_perf_check --dump DIR` also writes the overlay over the cleared
  target at 1080 and 2160 rows (with the frames' numbers) as DIR/overlay_WxH.ppm, for pictures.

## 15a. The vertical blank switch

- **Switch**: next to PerfOverlay, same file and section:
  ```
  [renderer]
  VSync = false
  ```
  On when absent or true (`PresentMode::Vsync`); false turns it off. Read once the renderer starts (`VsyncSetting`,
  device_mesh.cpp, before the first present) into `render::SetVsync`; the options window's vertical sync row switches it
  live (the present mode changes at the next present). **Never in a harness run**:
  `render::VsyncSwitchSupported()` is false in the harness build; the harness's own `SetWaitForVsync(false)` under the
  virtual clock presents in `Mailbox` (no wait, no tearing).
- **Off**: `PresentMode::Immediate` (no wait; may tear), which gpu.h's `SetPresentMode` turns into `Mailbox` where the
  swap chain has no immediate mode, and `Mailbox` into `Immediate` where it has no mailbox (Metal), then `Vsync` (the
  harness build's presents must not wait for the display; render_window.cpp logs the mode asked for and the one it
  got: "render: presenting on the game window through the <driver> swap chain, <mode>").
- **Nothing else relies on vsync pacing**: game time is the world updates (section 3); an unlocked game runs the main
  thread flat out, which is the point of the switch.
- **Check**: `ei_gpu_check` presents four frames in each mode, with the fallbacks (section 15c).

## 15b. SDL_GPU's limits

The renderer draws with SDL3's GPU API (R43e; sokol_gfx on Direct3D 11 and OpenGL before it). The spike that compared
the two (build/reviews/r43_sdl_gpu.md) found the same pixels and close frame times, the device about 150 ms slower to
make (made in the background from GameMain's window class registration, `render::PrepareDevice`).

Rules it found:
- **Vertex buffer slots.** A pipeline's slots are packed from 0. The D3D12 backend looks a buffer's description up by
  slot number. The renderer's pipelines do this: the casters' instances are slot 1, `kInstanceBuffer`.
- **What SDL_GPU lacks:**
  - border clamp (the renderer has none: nothing in the game sets a stage's address, so `Address` has no border);
  - linear filtering of RGBA32F on every Vulkan device (the receivers read the shadow range with four loads and a lerp);
  - D24S8 on AMD Vulkan and Metal (the renderer's depth is D32F everywhere, `kDepthFormat`, and it has no stencil);
  - sampling a multisampled texture, and depth resolve (the smooth option keeps an R32F depth copy instead);
  - timestamp queries (the overlay's GPU time is the whole frame's, from the submissions' fences);
  - a software device on Windows (the offline checks run on the GPU; Linux's on lavapipe).

## 15c. gpu.h: the renderer's layer on SDL_GPU

`src/render/gpu.{h,cpp}` (namespace `render::gpu`) is what the renderer's files draw with: plain C++, no SDL type but
forward-declared handles, so the files include no SDL header. `ei_gpu_check` checks it.

**Shape.**
- **Device.** `PrepareInBackground` (the device made on a thread of its own; SDL's video must be up already, or both
  drivers refuse and `Start` makes the device again on the calling thread: R43e removed the video start from here,
  which left the background device failing until `PrepareDevice` moved to after the platform layer's window class and
  video start, R43d1) and `Start` (takes it, or makes one); `Stop` waits for the GPU and releases everything. Driver: Direct3D 12, else
  Vulkan, on Windows; Vulkan on Linux; Metal on macOS; `DeviceOptions::driver` picks one. Shaders: DXIL on Direct3D 12,
  SPIR-V on Vulkan, MSL on Metal, from `gpu_shaders::kPrograms` (made at their first pipeline). `MaxSamples`,
  `FormatSupported`. No device loss is reported (section 9).
- **Pipelines made ahead** (R43d1). `PrepareInBackground(options, lister)`: the thread that made the device calls the
  `PipelineLister` (descriptions appended to a vector; `DeviceLimits::MaxSamples` for the device-dependent ones) and
  makes each into its own cache and shader list, which nothing else touches; `Start` joins the thread and moves both
  into the globals, so no lock is needed and the cache is the main thread's from then on. An exit between
  `PrepareInBackground` and `Start` (a declined disk-space prompt, a fatal error, `std::exit`) still ends with its own
  exit code: `PrepareInBackground` registers an exit function that joins the thread and releases what it made (a joinable
  `std::thread` destroyed would call `std::terminate`). SDL_GPU's device-level
  creation calls (`SDL_CreateGPUShader`, `SDL_CreateGPUGraphicsPipeline`) are thread-safe by SDL's documentation, as
  device and resource creation are, and `ei_gpu_check` makes pipelines on the thread on Direct3D 12 and Vulkan. A
  description that cannot be made (a faulty one, a sample count the device lacks) is skipped without a message and
  `MakePipeline` tries it again at its first use, saying why then. `render.cpp`'s `ListPipelines` is the renderer's
  list: each file builds its descriptions in the function its draws also use (`MeshDesc`, `TerrainDesc`, `PassDesc`,
  `QuadDesc`, ...) and lists them in `List*Pipelines`: meshes (the eight pass states every battle and menu draws with,
  found by logging a run's pipeline creations, each as a kept mesh or a frame-stream one), the terrain's two, the
  blit, depth clear and fill, the interface quads, picture and copy, the shadow map's, the composition's, the present
  (Bgra8), and for the render quality's samples the meshes, terrain and the copies between the targets. A pass state
  the list lacks is made at its first draw as before; when the draws change, log the creations (a temporary line in
  `MakePipeline`) over a few scenarios and list what shows up. The log line "gpu: N of M pipelines made ahead in X ms
  (the start waited Y ms for them)" says what it cost: on this machine's warm driver cache 5-10 ms for the 28 of a run
  (the same as making them at first use), several hundred under four games at once or after a driver or shader change.
  Y is mostly the device itself (R43d2): `app::StartVideo` (main(): the one-instance check, SDL's video, `PrepareDevice`)
  runs before GameMain, and GameMain does the work that needs no device (sound, the databases, the map list) before
  `CreateRenderer`, but that is all of 30-40 ms on a quiet machine; the start still waits about 260 ms of the device's
  ~290 (Direct3D 12 on this machine). Hiding more needs more device-free work in front of the first use, or a faster
  `CreateDevice`.
- **Handles** are plain values: `Texture` (with its size, levels, samples and format), `Buffer`, `Sampler`, `Pipeline`,
  `Ticket`. `Release` empties a handle. A texture or buffer released while recorded commands use it lives until they
  are done (SDL's). A handle released and then bound is not: a file that replaces a buffer or texture binds the new
  one (`StreamBuffer::Append` may replace its buffer, so an offset holds only until the next append). Release after
  `Stop` does nothing (globals destroyed at exit).
- **Formats**: RGBA8, BGRA8 (the swap chain), R8, R8 / R16 uint, R32F, RGBA32F, BC1-3, D32F. No stencil. An integer
  texture is never sampled (SDL_GPU's rule; refused). Direct3D 12 copies a compressed texture's region only in whole
  4x4 blocks, also on a level smaller than one, and Vulkan only at the level's own size there: gpu.cpp rounds the
  copy's extent up on Direct3D 12 alone.
- **Samplers and pipelines are kept by description** (`MakeSampler`, `MakePipeline`: the pipeline cache). The same
  description gives the same object, made at its first use (or ahead, above) and kept until `Stop`, so callers keep no maps of their own
  beyond a cheap key if they want one. A description that could not be built is kept too (one logged problem, then
  refused without a try). A `PipelineDesc` is the program, the vertex buffers (0..n-1, no gaps: SDL's
  Direct3D 12 backend looks a layout up by its index) and attributes, primitive, cull and winding, colour format, write
  mask and blend, a second colour target's format and mask (never blended), depth format, compare, write and bias, and
  the sample count. A sampler's `maxLod` 0 samples level 0 only (D3DTFP_NONE, the options' Mipmapping 0), as the
  original did.
- **One command buffer per frame**, taken at the first command; `Submit` sends it and presents the swap chain's
  texture if one was taken. A burst of writes (64 MB in one command buffer, a load) submits early unless the swap
  chain's texture was taken.
- **Passes**: `BeginPass` (a colour target with a level, load Keep / Clear / DontCare, store, a resolve target; a
  second colour target, resolved with the first; a depth target) .. `EndPass`. A target cleared or not cared about is cycled (a free copy of it takes the pass while
  the GPU still reads the old one). Viewport and scissor in target pixels from the top-left on every driver. Bindings
  (`ApplyPipeline`, `BindVertexBuffers`, `BindIndexBuffer` 32-bit, `BindTextures` texture-sampler pairs,
  `PushUniforms`) last for the pass, across pipelines, and one set again unchanged is not sent again (a uniform block
  is compared byte for byte with the one last pushed in its slot; one its caller keeps unchanged for the pass,
`PushPassUniforms`, by its address only; `BindStorageBuffers` whole buffers, R37f3). `DeviceSerial` and `PassSerial` count the devices made and the passes begun,
for callers that keep pipelines and samplers by a key of their own (the mesh draws) or bindings for a pass. `Counts()` says what the draws sent and what the
  filter left out; in a real run (scenario `render_stats`, 272k draws) 8% of the pipeline calls, 4% of the vertex and 4%
  of the index buffer binds, 12% of the texture calls and 30% of the uniform pushes were sent. `SetStateFiltering(false)`
  turns the filter off for `ei_gpu_check --bench` alone. `Draw`, `DrawIndexed` (with a first instance),
  `DrawIndexedIndirect` (commands from a `BufferUse::Indirect` buffer, `IndexedIndirect` each; `Counts().draws` counts
  the call once, `indirectDraws` its commands).
- **Writes** (`Write` of a buffer range or a texture region, `Copy` of a texture region) go between passes, never
  inside one (refused and logged). The bytes are copied into the upload ring at once; the copies wait and are recorded
  in one copy pass before the next pass, download wait or submission, so they are in order with the draws: a draw
  before a write reads what it replaces. `Contents::Discard`: nothing else of the buffer or texture is read by later
  commands, so the write may go into a fresh copy (a stream written from its start again, `StreamBuffer`'s first
  append of a frame; `Keep` for its later appends and `GpuPool`'s writes in place).
- **The upload ring** is one 16 MB transfer buffer filled from its start each command buffer. It is mapped while writes
  go into it, unmapped when the waiting copies are recorded (SDL wants it so), and mapped again without cycling to go
  on after them. A new command buffer or a full ring maps it cycling (SDL gives a free copy while the GPU reads the
  old). A write over 4 MB has a transfer buffer of its own. A command buffer that cycled it more than twice lets it go
  at its submission (SDL keeps every copy a cycle made until the transfer buffer is released). Texture rows are 256
  bytes apart and start at multiples of 512 (Direct3D 12's copy alignments, so SDL copies nothing again).
- **Downloads**: `Download(texture, level, rect)` is recorded in order with the frame's commands and returns a ticket;
  `Done` without waiting (its command buffer submitted and finished), `Wait` (submits first when the open command
  buffer holds it), `Read` maps the texels until `Release`. Released tickets' transfer buffers are kept for the next
  downloads (four at most). Not downloadable: the swap chain's texture (Vulkan makes it without transfer source) and a
  multisampled texture; a depth texture only whole on Direct3D 12.
- **The window**: `ClaimWindow(SDL_Window*)`, `SetPresentMode` (Vsync; Immediate, falling back to Mailbox, then Vsync;
  Mailbox, falling back to Immediate, then Vsync), `SwapchainFormat`, `AcquireSwapchain` (waits while too many frames are in flight;
  an empty texture while the window is minimised or hidden, which SDL's Vulkan driver treats so). SDL follows the
  window's size by itself.
- **The frame's GPU time** (the overlay's GPU line, one whole-frame time, user 2026-11-03): SDL_GPU has no timestamp
  queries, so `SetFrameTiming(true)` starts a thread that polls the submissions' fences every 0.2 ms; a submission's
  time runs from its submission (or the end of the one before, if later) to when its fence was seen signalled.
  `FrameGpuTime` gives the latest. The thread is joined when the renderer stops (`render::Shutdown`, from
  `SceneRenderer::DestroyRenderer`) and by an `std::atexit` function for exits that skip it: a joinable `std::thread`
  destroyed at exit called `std::terminate`, and with the overlay on (Rebirth's profile) every quit crashed after the
  exit functions and waited 2-3 s for the system's crash report (PL22). `gpu::Stop` is not called at exit: the device,
  its textures and the window go with the process. The thread only queries fences: SDL's Direct3D 12 `SDL_WaitForGPUFences` holds the
  submit lock while it waits, so a waiting thread would stall the main thread's submissions.
- **Fences**: each `Submit` keeps its submission's fence until the submission is seen finished; with the frame timer
  off, `Submit` asks the older ones itself, so a finished submission's fence goes back to SDL at the next submission
  (`SubmissionsHeld` counts the kept ones). Before (H50) only the timer or a download's wait asked, so outside the
  overlay every fence was kept: thousands a minute, which lavapipe and Windows' Direct3D 12 took, but Dozen on WSL
  turned down after a few seconds of several games (`vkCreateFence` VK_ERROR_OUT_OF_DEVICE_MEMORY, then an abort).
- `Problems()` counts misused calls (each logged "gpu: ..."); the check holds it at its deliberate misuses. A call
  SDL refuses is not misuse but a lost device: `Failed` logs it, calls `SetFailureHandler`'s handler and exits
  (section 1, "When the renderer fails").

**Cost** (`ei_gpu_check --bench [--driver NAME] [--frames N]`, tools/gpu_check/gpu_bench.cpp; the numbers below are
the RTX 4070 Ti SUPER on Windows 10, two runs of 3 x 500 frames, a quiet machine; the spike's tables are in
build/reviews/r43_sdl_gpu.md, 'Numbers'). The bench replays a synthetic world frame through gpu.h's calls the way
render_mesh.cpp and render_shadows.cpp make them (155 instanced caster draws into a 4096 x 4096 depth map, 263 mesh draws
with three uniform blocks, six samplers and a record each in a storage buffer, the present; half of the draws keep the
last one's mesh, 40% its textures; the numbers below are from before R37f3's records, when the draws had five blocks),
once with the filter off and once on, and reads a frame target back:
- **The draw loop**, CPU time of recording and submitting a frame (median, ms): Direct3D 12 0.26 without the filter, 0.17
  with it (960x720 and 2560x1440 alike); Vulkan 0.29 and 0.155. The spike's replay sent every call (D3D12 level with
  D3D11, Vulkan 15-25% slower); with the filter on the call counts of a frame fall from 419 pipeline, 418 + 418 buffer,
  1208 texture and 1317 uniform calls to 19, 195 + 195, 279 and 398 (94 KB of uniforms instead of 259). The frame time
  (acquire to submit) then follows the swap chain: 0.17-0.21 ms on D3D12 and 0.32-0.36 ms on Vulkan.
- **Downloads** (RGBA8 frame target, median ms, idle / drawn then read; the spike's SDL path made a transfer buffer
  each time): 960x720 Direct3D 12 0.20 / 0.21, Vulkan 0.18 / 0.19 (spike 0.78-1.21 / 0.95-1.50); 2560x1440 Direct3D 12
  0.75 / 0.69, Vulkan 0.72 / 0.76 (spike 3.3-5.2 / 3.8-5.9; its sokol D3D11 2.7-3.1 / 2.1-2.7). The first read pays the
  buffer (0.6-0.7 ms at 960x720, 1.6 at 1440p on D3D12; 7-8 ms on Vulkan's first). A download recorded with the frame
  and read a frame later with `Done` / `Read` costs the calls only: 0.12-0.14 / 0.6 ms including the frame's own
  commands. The game's own reads (screenshots, the movie recorder, the harness's captures, a picture of the escape
  menu) want the pixels now and wait; nothing in a frame's path reads back.
- **Pipelines made ahead** (the log line, a scenario run on this machine's warm driver cache): 28 of 28 in 8 ms, and the
  start waited about 500 ms for the thread, which is the device itself.

**Check**: `ei_gpu_check` (tools/gpu_check; build_checks.py on Windows, ctest `port_gpu --require` on Linux), per driver
(Direct3D 12 and Vulkan on Windows, Vulkan on Linux: lavapipe without a GPU), in a few seconds:
- texture writes from padded rows and downloads in every format (BC ones where supported), whole levels, a second
  level in two writes, a region; a download not done before its submission, then done by polling without a wait;
- the casters' program from vertex, instance and index buffers at offsets with a first instance, and as two commands
  of one indirect call; the depth test; the
  target's top-left orientation; a discarding write and a write in place between passes, each read back after the
  passes before and after it;
- the blit program in two pipelines with the bindings and uniforms kept across them; a mip level cleared as a target
  beside the first level kept; a texture copy; a 4-sample target with depth resolved;
- 77 MB of texture writes in one command buffer (through the ring, cycled, and on their own), the burst's early
  submission, each texture's first and last rows read back;
- the pipeline and sampler caches (anisotropic, compare);
- the swap chain on a 64x48 borderless, unfocusable window at (-4000, -4000) (shown, as SDL's Vulkan driver gives a
  hidden window no texture), four frames in each present mode with the fallbacks, the textures at the window's pixel
  size; under a second;
- 300 submissions with the frame timer off, at most 2 still held once the last has finished;
- frame GPU times from the fences; the misuses refused, each logged once, and nothing else logged.

## The renderer's source files

Every file is one `namespace render` block (`render::text`, `render::perf`, `render::ui`, `render::gpu` in theirs);
what a file alone uses is `static`, what the files share is declared in render_internal.h. PrepareShadows lists the
world's ground and models once a frame (`Frame::worldGround`, `Frame::worldMeshes`: the batches after the first ground batch that have a
model, additive and repeated passes flagged); the sun's and the point lights' shadows, the ambient occlusion and the roofs
walk those lists, not the commands. Rate-limited log lines go through `LogFirst`; what the frame's commands still draw is
released with `ReleaseAfterPresent`. gpu.cpp keeps pipelines and samplers by description; the renderer asks it for them at
each draw.

The frame's state is in sight: the game's hand-over entry points (render.h) read `g_frame`, everything below them takes
the `Frame` (a function does one or the other, never both). No function keeps a local `static` (state, scratch or a
counter): a file's state and the scratch kept for its room are file statics named for what they hold. `Frame::Clear`
(with `ShadowFrame`'s and `PointShadowFrame`'s) empties the frame at its present, the one place it is reset.

### render_cursor.cpp: the cursor

The game's cursor as the system's (section 9, "The cursor"): render::Cursor keeps the frame's cursor (its picture's frame,
mask, hot spot and hold), and at the frame's present ShowFrameCursor shows the matching shape through
`platform::SetCursor`, made at its first show (`platform::MakeCursor`) from the picture's frame scaled to the window and
through the gamma ramp; shapes of another scale or ramp are let go once not shown, those of a picture with its holder
(ForgetCursor). A present without a cursor hides it; a held one is kept still (`platform::HoldCursor`).

### render_mesh.cpp: meshes

Everything the game draws through the device's mesh draws (figures and map objects, the sky, the interface's sprites and
3D previews). The game hands the mesh over whole in model space with the matrix it projects it with, a figure part's
model matrix apart (`MeshDraw::model`, the product taken once here) (BeginMesh); each pass then
(src/engine/scene/device_draw.cpp) becomes a batch with the render state of that moment (MeshPass, section 5): blend, alpha test,
depth, culling, fog, the two texture stages' operations, filters and textures. A figure mesh drawn through the figure part
draws is kept on the GPU (render_mesh_cache.cpp) and its batches draw from that pool; every other mesh is built into the
frame's stream. A figure part handed over with its lighting (MeshDraw::lighting, its model space to the landscape's) is lit in the
shader from its normals.

The draws (R37f3, `MeshDraws`): before the frame's passes `PlanMeshDraws` (render.cpp) fills each mesh command's record
(`MeshRecord`, mesh.hlsl: the matrices, the place, fog, flags; 336 bytes) and its lights (`PartLight`: the sun,
ambient and point lights a figure part or ground print is lit by, 432 bytes; one for consecutive draws lit alike, entry
0 none), together what the draw's uniform blocks were, and joins consecutive commands with the same pipeline key, geometry, textures, shadow block and
fragment block (`MeshFsParams`: the run's) into a run; no run crosses a point where a pass may end (the world's edges
with multisampling, the split, the water's copy) or the performance overlay's categories. The records go into a storage
buffer once a frame, the runs' commands into an indirect buffer: draws of the same indices in a run are one command of
consecutive instances, the instance's record index read from a per-instance buffer of 0, 1, 2, ... at the first
instance; the lights go into a second storage buffer. The fragment stage reads both where it uses a value (a copy of
the whole record into a local put its light arrays in scratch memory). A run is one `DrawIndexed` or one `DrawIndexedIndirect`, drawn at its first command. The order of the draws is
the commands' order, so the pictures are the same up to the shaders' rounding (the records read from a storage buffer:
1-3 levels on about 0.01% of the pixels). The water's sky pass draws its dome the same way.

### render_mesh_cache.cpp: meshes kept on the GPU

Section 13, item 4. The figure part draws (src/engine/models/figure_part_draw.cpp) name the figure mesh a mesh draw
draws (MeshDraw::owner); the mesh's corners are kept for that figure mesh (positions and texture coordinates in model
space, colours and triangles) in a pool on the GPU (GpuPool: geometry and colours in streams of their own). What is built
and uploaded again is what the game says changed:
- the colours, when the draw's colour pair differs from the kept one or the game lit the mesh outside its draw
  (MeshChanged Colours: the sky): read and compared with the kept ones, the changed range uploaded;
- the positions, when the game moved them (MeshChanged Positions: per-vertex offsets, FigureMesh::SetOffsets; a link
  figure's part morphed again): read and compared, the changed range uploaded;
- all of it, when the mesh's buffers changed (MeshChanged Rebuilt) or the draw hands over other arrays or counts than it
  was built from. Interface sprites (no owner) and a mesh drawn again in the same frame with other data go
  into the frame's stream. The texture offsets (an animated mesh's frame of its sheet) are the draw's, added in the
  shader. A kept mesh also keeps the box of its positions (CachedMeshBox: a caster's box for the point shadows' spheres and
  the units' fronts), taken again only when they were built or moved. The casters outside the view
  (render::CasterMesh) are kept the same way, in the part's own slot: one copy whether the part is drawn or only casts
  (a part leaving the view is relit to the caster's colour pair, 0 and 0, which the shadow maps do not read).
- One GPU copy for the same arrays (R37f3, `GpuMesh`): a kept mesh's built arrays (corners, colours, triangles) are looked
  up by a hash of their bytes and compared whole; a figure mesh whose arrays match another's takes its copy (the palms
  of one morph, lit alike), so their draws bind the same indices and merge into instances. A mesh whose arrays change
  (moved, relit) while shared takes a copy of its own first (relit, it looks for a match again); a copy no mesh uses is
  freed at the next 64-frame sweep once written. The harness logs
  the copies and shares after the meshcache line ("meshcache: N copies on the GPU, N times a mesh took another's").
  The harness
  build compares every kept mesh and box with a fresh build every 16th frame (`meshcache check`); `meshcache off`
  builds every mesh into the frame's stream.

### World objects drawn whole (R37f13)

The design that came before (retained objects: an instance buffer on the GPU per object part, written when the object
changes, the view applied in the shaders, about -650 lines) was built in a smaller form after the user's change: one
view test per object, the GPU clips the rest.
- **The verdict.** `CollectVisible` / `CollectCells` test each object's bounds once (`InView`); that is the draw's verdict. `Object3DClientSpecific::DrawParts` pushes nothing on the matrix stacks, takes no
  `ClipTest` per part and projects no vertex: every part near enough (`PartTooFar`) and not hidden goes to the renderer
  whole (`FigureMeshDrawer::DrawLit`, `TextureDevice::DrawMesh`), placed by its model matrix (the object's drawn pose
  times its extra scale times the part's) under the screen stack's top. `FigureMeshFlag::Drawn` is set for those parts,
  `MapObjectFlag::Shown` when any was drawn, `MapObjectFlag::Static` when every part near enough is neither hidden nor
  flat (the digest's client units carry it; the relight rule that cleared it, `TouchedModels`, `touchTime`,
  `kLightingPeriod`, `ClientHalfFlag::Redraw` and `FigureMeshFlag::Lit` are gone). A unit's or lever's part gets its
  rectangle from its box (`BoxRect`, section 3, "Picking").
- **The hand-over.** `MeshDraw` carries the part's model matrix (`model`, multiplied in at BeginMesh and kept in
  `Frame::models`: `MeshBatch::placement`), its lighting in that space (`MeshLighting::model`: FigureLight no longer takes
  the camera back out) and one colour / specular pair (`colour`, `specular`) in place of colour arrays: a lit part's
  `Material::GpuLitColour` and specular, a flat part's flat pair; only the sky keeps its arrays. The device's CPU mesh
  draws (MeshArgs, DrawMeshBlocks, the textured DrawMeshBlocks2 and DrawTwoTextures, BoundsOfProjected) and
  link_mesh_draw.cpp are gone.
- **The renderer reads the game's matrices.** The point shadows, the units' weather and the roofs read
  `Frame::models` (`CameraInverse` and `LandscapeModel` are gone), so a kept point-shadow face compares its casters'
  matrices exactly; the water's sky dome takes the sky's own matrix (`Sky::Draw` hands it over instead of pushing it).
  The casters' walk outside the view passes over an object the world drew (`render::CasterDrawn`; the per-part list
  `CasterPartDrawn` and the objects' `casterParts` are gone).
- **Not built**: the retained instances (an instance buffer and its upload, about 300 lines for about 0.05 ms once the
  per-part work above is gone), the kept verdicts while the camera is still (dropped by the user), `ReceiverMatrices` by model. The
  outside casters through the mesh hand-over followed in R37f14 (section 13, item 10).
- **Looks**: an object partly in view casts and is drawn whole, so its shadow no longer comes from the casters' walk
  (render_shadows_outside: the town gate). A unit's rectangle is its parts' boxes, a little larger than its triangles
  (render_load_town's colour check reads the larger rectangle).
- **Measured** (R37f5's probe on the commits before and after, release terms, 3840 x 2160, Msaa 1, Anisotropy 1, three
  quiet runs each, medians; frame-plan.md, "Measured after R37f13"): the stress save's main thread 4.74 -> 3.81 ms with
  the camera still (the objects' draws 1.93 -> 1.19), 3.48 -> 2.64 panning (the draws 1.14 -> 0.64); the sampled
  2400-frame runs 3.88 -> 2.90. The town at night is GPU-bound and stays where it
  was (0.87 / 0.85 ms; with the crowd 1.62 / 1.66). More meshes reach the GPU (parts outside the view are no longer
  left out: the save 1822 -> 2081 a frame, the town 119 -> 227), in about as many calls (56 -> 58).

### render_occlusion.cpp: the screen's ambient occlusion

Section 13, item 11a: once a frame after the shadow maps the world's depth at half the frame's size (the ground and the
models that write the depth), the occlusion found in it (occlusion.hlsl) and blurred (occlusion_blur.hlsl); the ground
and the world's meshes lit on the GPU take it off their ambient light (shaders/ambient_occlusion.hlsli). The ground's own
occlusion from the height field is render_terrain.cpp's. The water's reflected rays are marched through the same depth
(render_water.cpp, R17a3), so with the occlusion off the water reflects the sky alone.

### render_point_shadows.cpp: point-light shadows

Section 13, item 9: the few point lights in view with the most weight on the screen get a cube of six depth faces each
(fading in and out), drawn from the sun map's casters, and the receivers lit on the GPU take each such light away where
it is hidden: the ground (scene_lighting.hlsli's point loop) and the figure parts (mesh.hlsl's figure_light). The faces are drawn in one pass: their clears first (the
faces do not overlap), then each face's casters through `CasterDraws` (render_shadows.cpp), grouped within the face.

### render_shadows.cpp: object shadows through a sun shadow map

Section 13, item 10. Once a frame, before the world draws, PrepareShadows fits an orthographic view from the sun around
the visible ground and gives every receiving batch the matrix from its model space to the map; DrawShadowMap draws the
casters' meshes into the map (depth only; alpha-tested and blended passes cut out by their texture's alpha), then the
map's depth range per cell. Objects the game does not draw whose shadow can reach the visible ground cast too
(CasterMesh, tested against the map's fit made right then). PrepareShadows prepares the point lights' shadows
(render_point_shadows.cpp); the frame's draw (render.cpp's DrawPending) draws the sun map, the roof map and the point
lights' faces in that order. The per-frame lookups are lists kept from frame to frame (the drawn parts sorted once for
the walk's searches, the light matrices by the frame's matrix index); ShadowState's caster counts (`casters`, `lost`,
`offScreen`) are counted only in the harness build. Space: the meshes' matrices are clip matrices,
the terrain's the same for the landscape's space, in which the map is fitted.

### render_cloud_shadows.cpp: cloud shadows

Section 13, item 11. CloudShadows keeps the frame's weather; PrepareClouds (from PrepareShadows, outside a pass) makes the
cloud map at the first frame and moves the layers' drift and the cover on by the world's time; CloudUniforms writes
the receivers' part of ShadowParams for the map's fit; BindCloudMap binds the map (bilinear, repeating) for the terrain
and mesh programs.

### render_terrain.cpp: the terrain

The game's terrain passes (ground pages, water pages: Terrain::DrawPages, src/game/terrain/terrain.cpp) clip test the
sectors in view and hand their list over once a pass (`TerrainPass`). The zone's sectors live on the GPU as two meshes,
the ground and the water, built from the sectors' tiles (every tile's nine vertices and eight triangles, the full detail
at any distance), with the vertex records' normals and material colours, the tile texture coordinates and the page's
layer in the zone's page array; the clip matrix is the pipeline's. The GPU clips. Section 13, item 3.

### render_water.cpp: the ripple map and the reflections

The water's small ripples (R17a): a 256 x 256 RGBA8 map with its levels down to 1 x 1, made at the start (section 1) from
a fixed generator of its own (the same map on every run and system, nothing drawn from the game's): r, g the slopes of
100 waves along x and y (3 to 24 cycles across, in random directions), b the foam's pattern (the height of 24 finer
waves), each wave a whole number of cycles across the map so it repeats without a seam. With 40 (until R63) a few long
waves crossed into a regular knit that no shifting of the map hid. The terrain batches bind it with a repeating, linearly mipmapped sampler
(anisotropy 8); RippleOffsets gives its two layers' offsets at the client's time (SceneLights::clientTicks: the client
world's tick and the fraction of the next one it has had), and the swamps' a fifth as fast, so the ripples hold still
under `freeze` and move with the game's clock.

The reflections (R17a1), read by shaders/terrain_stages.hlsli's `reflected_sky` (the water's and the puddles') and
terrain_water.hlsl's `screen_reflection`:
- **The sky image** (DrawSkyReflection, once a frame before the first pass, after the ambient occlusion): the world's
  meshes before its first ground (the sky dome: WorldClient::Render draws the sky first) drawn again into a quarter-size
  image, mirrored in the plane at the mean level of the water's pieces, their faces' cull turned, no depth test, over
  1.25 times the frame's extent (a ripple turns a ray past the frame's edge); cleared to the fog's colour. Their corners
  give the dome's centre, radius across and height. The shader meets the reflected ray with the dome (an ellipsoid)
  and looks the hit up in the image through its matrix, so water at any level reads it right: the plane only keeps the
  dome in view. About 0.02 ms.
- **The rays' depth and colour** (R17a3): the rays are marched through this frame's depth before the water, the ambient
  occlusion's half-size depth (render_occlusion.cpp: the ground and the models that write the depth, no water; drawn
  before the frame's first pass, terrain_water.hlsl's `occlusion_depth`), and take the colour where they hit from the last
  frame's world view: CopyFrameForWater (shaders/scene_copy.hlsl) copies the frame target's colour at half size (per
  2 x 2 pixels the mean) where a world view with water ends, after its resolve, and keeps that frame's water clip
  matrix; the hit point is looked up through it (`sceneView`), so the camera's motion moves nothing; what moved in the
  frame shows where it was one frame before. When the last frame left no copy at this size (the first frame after a
  load or a resolution change, or with water again after frames without) the copy is taken before the water's first
  draw instead, as R17a1 did (one more resolve, that frame only), so the reflections start with the first frame
  (render_load_first_frame: its first frame against a later one). Without the occlusion (`occlusion off`) the water
  reflects the sky alone.
  Why: SDL_GPU samples no multisampled texture, so a copy of the frame before the water (R17a1's) needs the world's pass
  split there and resolved once more, about 0.2-0.25 ms at 3840x2009, msaa 8 (the colour's resolve about 0.1, the depth
  copy's 0.12; the split itself 0.03). The copy at the world's end reuses its resolve, and the depth before the water
  was already there. Not the world's depth at its end: the water writes it and its waves rise up to 0.3 units, so a ray at
  a grazing angle would pass behind the next crest and hit the water itself. The last frame's colour holds the water, the
  effects and the rain drawn after it: a ray hits only above the water, where they differ only at its edges and under
  an effect. The frames of render_water, render_water_looks and five low views (pitch 12-15 degrees, msaa 8) against
  R17a1's: a mean difference under 0.11 a channel, 0.4% of the pixels by more than 8 at most (the swamp's low view),
  the rest under 0.05%.
- **BindWaterReflections**: every terrain batch's part of TerrainFsParams (the camera's clip matrix, the sky image's,
  the dome, the depth's map to clip w: render_occlusion.cpp's DepthMap, the copied frame's clip matrix, whether the
  rays are marched) and the two images at terrain slots 9 and 10 (a white image where one is missing). The fields sit in TerrainFsParams because a stage
  has at most four uniform blocks (section 15b).

Why this way (measured at 3840x2009, msaa 8): the water lies at several levels (sea, lake, river, swamps), so drawing the
objects again mirrored in one plane is wrong for all but one level and costs the scene's draws again; the sky alone is
smooth and needs one plane only as a view. The objects come by screen-space marching from the copy instead: right at
every level, no extra mesh draws, but only what the frame shows (an object off the screen or behind another reflects
nothing; the sky shows there). A full-size copy and 16 + 4 steps cost about 0.9 ms of marching on a screen of water;
half size and 10 + 3 steps about 0.1-0.2 ms, with no visible loss under the ripples.

### shaders/terrain_water.hlsl: the water

The ground and the water are two programs (R37e4d; `terrain_ground.hlsl`, `terrain_water.hlsl`) over a shared
`terrain_common.hlsli` (the vertex layout, both blocks' first members, the slot names' use) and `terrain_stages.hlsli`
(the animated tiles' coordinates, the edge fade, clip and shadow-map coordinates, the receivers' includes, the rain's
rings and the sky's reflection). Each program names its fragment slots itself (the slots of a stage run from 0 without
gaps, which the programs' different textures need: the ground has the ambient occlusion's map and no ripples, the water
the ripple map, the frame's copy and only the occlusion's depth) and has its own blocks (`TerrainGroundVsParams` /
`TerrainWaterVsParams`, `...FsParams`); the draws are `DrawGroundBatch` and `DrawWaterBatch`, the pipelines are made
per layer. The ground keeps a `levels` flag for the harness's level map; no mode uniform is left. The shared part of the
blocks is a macro (`TERRAIN_VS_MEMBERS`, `TERRAIN_FS_MEMBERS`) so the two structs stay equal where they overlap.

The water pages keep the game's look underneath: the page texture lit by ground_light from the vertex normal with the
game's crests (the vertex waves and the crests' specular are the game's formula, worked out in `waved`: section 13,
item 3a; the water levels the game's),
alpha the material's opacity, fog as for the ground. Each water vertex carries its water's kind (VertexRest::extra,
render_terrain.cpp's WaterKind): the maps' water materials (kind 3 in the .mp file) tell water, swamps and lava apart only
by their colour, so blue is water, green (zone1's, zone2's, zone7's swamps: 0.19, 0.54, 0.36, opacity 0.7) a swamp and
red (0.8, 0, 0, lit by itself) lava; the kind is interpolated, so where a swamp meets water the looks blend. `Water`
then, per pixel:
- **Ripples**: two layers of the ripple map, one repeating every 9 landscape units and one every 5 turned 0.65 radians
  against it (at 0.45 of A's weight), drifting apart; their slopes (0.165 at the steepest texel, calmer within half a
  unit of the shore) turn the vertex normal. **No repeat shows** (R63): each layer is laid in patches (`ripple_patch`),
  the map shifted by a patch's own offset (the R2 sequence), the patch picked by the cloud map at 1/47 a unit (turned
  0.4 radians): 8 patches across its range for A, 10.6 for B with their borders between A's; across a border a narrow
  blend whose slopes are scaled back to their contrast; the same noise scales the slopes 0.75 .. 1.2 (calmer and
  rougher stretches). Before, the map's repeat showed as a grid of the same glints every 9 units (B was turned by
  (0.8, 0.6), a 3-4-5 turn, so the two layers together repeated exactly every 45 units), plainest at sunset from a low
  camera. Measured from above (a scratch model of the map and the shader): the slopes' correlation with themselves 9
  units along was 0.79-0.83 and 1.0 at (27, 36); now 0.09-0.12. Cost: four ripple reads and one noise read per water
  pixel, as before (the swamps' slower layers are read only where a swamp is, the scum's noise likewise); the frame's GPU
  time on zone1's sea at 4K, msaa 8 (whole frame, lowest of 4 interleaved runs): 2.38 / 2.24 / 2.45 / 2.39 ms before,
  2.35 / 2.28 / 2.52 / 2.44 after (noon from 45 and 18 degrees, sunset from 45 and 16). The rippled normal lights the layer up to 30% more or 25% less as it turns to the sun or away
  (kRippleShade 2, by the sun's share of the light not under a cloud).
- **Depth** (`depth`, VertexRest's: the water's depth over the ground at the vertex, from the ground record,
  interpolated): the share 1 - e^(-0.5 depth) of the depth's look: from half the material's opacity at the shore to 1.15
  times it in deep water, and up to 15% darker.
- **Reflection** (R17a1): Schlick's Fresnel term (0.02 head-on, but at least 0.12, kLeastReflection: the game's lit
  layer is far brighter than deep water, so 2% of the sky did not show) of the rippled normal towards the camera
  (SceneLightParams::camera) blends in what the reflected ray (turned up to just above the horizon where a ripple tips
  it down) meets: the sky dome (`reflected_sky`, render_water.cpp's sky image), and over it what the frame showed before
  the water where the ray passes behind it (`screen_reflection`: 10 steps over 12 landscape units from a start
  jittered per pixel, 3 halvings, a hit within 2 units behind the surface; faded towards the frame's edges and the
  ray's end): the shore, the banks, trees and cliffs. The reflected part is opaque, so the water hides more of the
  ground at a grazing view. At the game's 45-degree camera the reflections of objects are short (the ray leaves the
  frame soon); low views show them whole. In shadow the reflection loses up to 0.3 (kShadedReflection).
- **Sun glints**: a sharp (0.25) and a broad Blinn-Phong lobe of the sun's colour on the rippled normal, gone as the sun
  sets and in a shadow: the water receives the objects' shadows from the sun's map and the clouds' as the ground does
  (R17a1; before, the clouds' only, and glints showed in a tree's shadow), and its layer is lit by that shade.
- **Foam**: within half a unit of the shore, the ripple map's foam pattern above a threshold, lit by the ambient and sun
  light, at most 0.35 opaque.
- **The shore line**: the water fades in over the first 0.25 units of depth, so where its surface meets the ground there
  is no edge. Before, the water at the shore line was already half its opacity and more at a grazing view (the
  reflection), and it ended in a hard line along the triangles where the ground rose out of it (not the shore's
  seams: next item). The water layer's tiles themselves never end over ground under the water (zone1:
  of 1203 vertices on the edge of the water tiles, 3 lie under it, by at most 0.07).
- **Which layer a pixel is** comes from the batch (its own program since R37e4d; before it `TerrainFsParams::flags.x`), never
  from an interpolated attribute.
  With multisampling a pixel on a triangle's edge is shaded at its centre for each triangle covering any of its
  samples, so the attributes are carried past the triangle; the ground's `loss` (0 on dry ground, more under the
  water) fell below 0 there at the shore, the old single terrain program took the sign for the water layer, and the ground's edge
  samples got the water's look and its near-0 alpha at the shore line: the sky showed through in a dotted cyan line
  along the ground's triangle edges where the sand meets the water (R17c1; R17a2's fade of the water's own edge was
  another line). Not cracks: the vertices are shared and the frame without multisampling had none. A per-pixel test of
  an attribute against a value its vertices sit at (0, -1) goes wrong the same way. Check: scenario `render_shore_seams`
  (the user's save on zone 1's beach, msaa 8 against 1, `smoothedges`).
- **A swamp** is standing water, distinct from the sea and darker than the grass around it: the ripples at 0.2 of the
  slope, drifting at a fifth of the speed (TerrainFsParams::slowRipples); a deep green body, the game's green kept:
  0.8 of the lit layer (the game's swamp, its texture lit by the material's green) blended 0.35 towards a murk of
  (0.07, 0.12, 0.03) of the light on it (kSwampLayer, kSwampMurk, kSwampBody); a faint sheen of the sky whatever the
  angle (0.03 plus 0.6 of Schlick's term, the sky at 0.7) and a glossy lobe of the sun (Blinn-Phong 300, 0.15; the
  water's broad lobe at 0.3) in place of the sharp glint; no foam, but sparse scum: patches of dull olive (at most 0.45
  opaque) where the cloud map at 1/17 a unit and the ripple pattern sum above a threshold, a little more within 1.5
  units of the shore. Its edge goes by the distance from the shore line, not by the depth: it fades in over 0.25
  units, is 0.35 opaque there and fully opaque 1.2 units in (or half a unit deep). The swamps are shallow and their
  shores flat, so the water's fade by 0.25 of depth (R17a2's first try) faded whole pools out and left them the
  grass's green. On the ground a muddy wet rim: the ground layer's `depth` is the swamp's depth over it
  (render_terrain.cpp's SwampDepth; -1 where the record's water is not a swamp), and within 0.6 units of its shore
  line and 0.3 under it the ground turns 0.45 of the way to an earthy tint of its own luminance (kMud) and is half wet
  (ground_weather's `mud`, which counts as wet ground whatever the weather: wet_ground's sheen).
  **The shore distance** (`shore_distance`) is the depth over the ground's slope, and the slope comes from the map's
  vertex normals, interpolated: on the water layer the ground record's (render_terrain.cpp's GroundSlope, in the
  vertex's second depth value, `shore`), on the ground its own interpolated normal. Taken from the depth's pixel
  derivatives (until R17c4b) the slope was the triangle's own, so the distance jumped at every triangle edge where the
  ground bent: the swamp's fade and the mud rim stepped in straight lines and notches along the triangles (a sharp
  vertical edge and diagonal steps by the grass tongue in render_swamp_shore's swamp_near), and the rim, which ran
  under the whole swamp, mixed brown into the bed, which the game's lighting tints by the water's green: near black
  (R17c4b; swamp_view's body (23, 25, 12) at the save's hour and (37, 45, 29) at noon against the game's look
  (16, 29, 2) and (26, 48, 4); now (16, 27, 7) and (29, 48, 17); swamp_near_noon's shore, 800 x 500 at (300, 300):
  the darkest 5% under luminance 20.9, now 27.7, the game's 30.0; the step by the tongue, a column's mean jump over
  120 rows at x 878: 7.5, now 1.5, the game's 1.8). The game's look for such a comparison: Water returning `game` with
  the material's opacity for the swamps too (as for lava) and no mud.
  Where a swamp meets other water (zone1: material 1 against material 0 at (147, 87), (172, 94) and (122, 69), both
  under water; the swamp's priority 0.5 is the lower, so the shared vertices are the swamp's) the ground's `depth`
  runs from the swamp's depth to -1 across the triangle and crosses 0 under the water: a false shore line. No one
  scalar per vertex avoids that crossing (a sentinel above the swamps' range only moves it one triangle into the
  river), so the ground carries a second value, `otherWater` (VertexRest, beside `depth` in one float2 attribute: the
  terrain pipeline has all 8 of its attributes): 1 where it lies under water that is not a swamp. The rim fades out
  over its first quarter (kMudOther), so it ends near the swamp's vertices, under the swamp's own near-opaque water;
  only triangles with both a swamp vertex and ground under other water change (every other ground vertex has 0). R17c4a,
  looks_swamp_river: the false band under the river (an olive strip by the bridge, a 40 x 140 strip at (680, 540))
  differed from the frame with the rim off by 2.3 a channel on average, 745 of its 5600 pixels by more than 8; now
  0.13 and 22. The real shores (looks_shore, looks_shore_near, looks_swamp, looks_swamp_east and the swamp edge's own
  shores) are the same to the pixel.
- **Lava** keeps the game's look (its texture lit by its own colour, the material's opacity).

The depth is per vertex (one unit apart), so the foam's band follows the triangles near the shore. Cost (3840x2009,
msaa 8, frame GPU time on a quiet machine by the fence probe of section 13 item 1, frozen frames of render_water's
scenes; the water's part: frames with its draws against frames without them, medians), before R17a1 / after:
the shore 1.06 / 1.35 ms, the sea 1.18 / 1.33, the lake 0.09 / 0.40, the river 0.10 / 0.48, the river at night
0.24 / 0.44, the lake at night 0.18 / 0.45. Of the 0.15-0.38 ms added, the resolve for the copy before the water was
about 0.2-0.3 (gone with R17a3), the march 0.1-0.2 on a screen of water, the sky image 0.02, the shadow map on the
water little. R17a3, the views render_water's steps reach at 3840x2009 (three rounds alternated, medians; the kit: build/R17a3 in
the main checkout, a probe patch script whose EI_R17A3=s skips the water's draws (its render.cpp variants were written
against R17a1's copy), the scenarios r17a3_time and r17a3_low, the runner and the tables), the water's part with R17a1's copy / without the rays (`sceneOn` 0) /
R17a3's: the shore 0.61 / 0.29 / 0.32 ms, the sea 0.60 / 0.26 / 0.30, the lake 1.23 / 1.17 / 1.18, the river
0.45 / 0.16 / 0.20, the river at night 0.47 / 0.20 / 0.20, the lake at night 1.24 / 1.21 / 1.27.
Why the lake views cost more (R17a4; the kit: build/R17a4 in the main checkout, R17a3's probe adapted (EI_R17A4: the
water skipped, flat, unblended, without its depth or its depth copy; no pipeline writing the depth copy; the copy not
resolved or not stored), the scenarios r17a4_time and r17a4_over (variant o draws the water a see-through red, and
r17a4_cover.py counts the frame's share of it), the runner and the table): at 3840x2009 their water covers 81% and 86%
of the frame (render_water's 4:3 captures show the lake far smaller), the shore and sea views' 28%, the river's 13%;
per frame of water every view costs about the same, 1.2-1.5 ms. Not the shading: the water's fragment returning its
vertex colour costs the same (the lake 1.24 against 1.23 ms), without blending 1.09-1.21, without the shadow map's
lookup the same. The per-sample writes are the cost: the flat water without its depth and its depth copy 0.31-0.35 ms on
the lakes. Most of it is the depth copy (section 11, "The depth copy": an R32F target every depth-writing draw of the
world writes at each sample): with no pipeline writing it the frame is 0.55-1.2 ms cheaper in every view (three rounds,
medians: the shore 2.63 / 2.08 ms, the sea 2.67 / 2.10, the lake 3.46 / 2.42, the river 2.58 / 2.11, the river at night
3.32 / 2.15, the lake at night 3.38 / 2.41), and the lakes' water then costs 0.25 ms. Its resolve is a smaller part (without it 0.05-0.5 ms
less, a noisier round). The copy is general, not the water's: since R17a4a only frames that read it write it (section 11). R17a's look (ripples, depth, glints, foam) added nothing measurable over the game's
plain water shading. Check: scenario `render_water`
(frozen moments on zone1: the beach at the west sea, the sea, the lake in the middle, the river with stones, the river
and the lake at night; samecapture of a frozen moment drawn twice, diffcapture that the ripples move). zone1's water materials
(kind 3): 0 and 2 water (0.19, 0.32, 0.65; 0 the lake and the river, 2 the sea), 1 the swamp; the swamp's tiles lie in
x 123..193, y 3..93 (the big one around (158, 34), deepest (163, 47); the east one around (172, 70)).

### render_checks.{h,cpp}: the harness's API

Everything the harness's steps call besides the product's API, the same in every build: the switches (smoothorder,
shadowcasters, shadowview, occlusion, terraincache, meshcache, terrainlevels, ...), the state read back (SmoothState,
ShadowState, OcclusionState, the caches', CursorState, WindowPresents, CheckMmpLevels) and FrameStats with
TakeFrameStats and the game's timed work (Begin/End*Time). render_checks.cpp keeps FrameStats' counts and times (the
frame's `g_stats`, Timer, the Count* functions); a switch's or read-back's body stays in the file whose state it
turns. EI_RENDER_VALIDATION (`kRenderValidation`) gates only the checks themselves (the read-backs, the caches'
compares every 16th frame) and the times; the product never calls a switch.

### render_perf.h: the performance overlay

Section 15: frames a second, the frame time, the whole frame's GPU time, the renderer's CPU time and the draws by
pass, as a few lines of text in the frame's top left corner, drawn by the present into the swap chain's texture after
the frame (render_window.cpp, DrawToWindow), never into the frame target. Off unless the settings file switches it on;
the harness build never draws it.

### render_overlay.cpp: the tools' overlay

The tools built into ei-rebuild (src/tools: the effect viewer, effects.md "The effect viewer") draw their dear imgui
panels through `render::OverlayTriangles`, once a frame from the main loop's `drawOverlay` hook.

- **Input**: imgui's triangles, with positions in frame pixels, an RGBA8 colour per vertex and 32-bit indices, plus
  draws that name a texture (0: white) and a clip rectangle.
- **Drawing**:
  - It is one command kind, `Overlay`, in the frame's command list after the interface. The harness's captures
    therefore show the panel.
  - The vertices and indices go into stream buffers. The program is `shaders/overlay.hlsl` (rect.hlsli's
    `to_clip`, the colour times the texture), alpha blended, without depth.
  - Each draw sets the scissor to its clip rectangle.
- **Textures**: the panels' textures (imgui's font atlas) are made, written and dropped through `MakeOverlayTexture`,
  `WriteOverlayTexture` and `DropOverlayTexture`, by id (imgui's texture protocol, `RendererHasTextures`).
- Without a tool open, nothing is drawn and nothing is made.

### shaders/mesh.hlsl: figure_light

A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting), with the model-space normal `n`: per
channel the largest of the ambient colour, the sun's lit colour times the cosine to it and each point light's colour
times `pointTint` (the material's diffuse colour times the coefficient), its cosine and falloff, plus the emissive
colour, at most 1. The point lights are those of the cell of `land` (the point in the landscape's space; the normal is
taken there by the part's landscape rows), in the world only; the ambient colour times `occlusion`
(section 13, item 11a; 1 outside the world). The crown: pulled towards its average colour,
taken as the average over every direction a normal can face (the mean of max(ambient, sun x cosine) for a cosine spread
evenly over -1 .. 1; 0.6 of the way). The shadow map darkens it after (mesh_fs). A light with a point shadow adds only what
reaches `land` (the point in the landscape's space; `face`: its face normal there, turned to the camera): a unit's mesh
compares from the unit's nearest distance to the light, on every face; any other only on faces turned to the light (by
`face`), looked up moved along `face`.

### shaders/mesh.hlsl: ground_shade

A ground print's light, per channel (section 13, item 10, "Ground prints"): the ground's light at the point `p` with
the normal `n` as ground_light works it on dry ground (`m` and `mp` 0, `loss` 0, `occlusion` 1), once with `hidden`
and the point shadows and once without, the first over the larger of the second and the same ground's light in the
daylight colours (`daySunColour`, `dayAmbient`; the sun's direction of now), at most 1 (1 where that is black). The
sun and ambient light are the draw's `PartLight` (GroundDecalLight), the point lights those of the pixel's cell
(point_lights.hlsli), both in the landscape's space. The specular ones (`PointLight::colour`'s w) also go apart into `spec` (the largest per channel, darkened by the map's shade), which the print adds after its colour
as the ground adds it after its texture; the reference (`full`) takes every light without the shadows.

### shaders/scene_lighting.hlsli: ground_light

The landscape lit as the game's vertex lighting lit each vertex, at a point `p` (the landscape's space) with the unit
normal `n`: `m` the material colour the sun and ambient light start from (the water's; on the ground the water's above
it, else black), `mp` the one the point lights start from (the record's own), `loss` the light lost under the water (0
above it), `hidden` how much of the sun the shadow map hides at the point, `strength` the map's darkening
(render_shadows.cpp kStrength) and `occlusion` how much of the ambient light reaches the point (section 13, item 11a; the
ambient colour is multiplied by it, 1 on the water). In the sun max(material + facing x sun, ambient), in the shade
ambient + material, as the game lit them; but a shadow is never lighter than the sunlit light times 1 - strength (where the ambient light is nearly
the sun's, as in the towns, the game's shade alone hardly shows), on faces turned to the map's light (a face turned away
keeps its light). The point lights (those of `p`'s cell, point_lights.hlsli; each material + its light, at most 1 per channel, by its falloff and facing, dimmed
under the water by the light's vertical part) take the largest per channel, darkened by the same shade, the specular ones
apart (`spec`, added after the texture as the specular word was). A light with a point shadow adds only what reaches the
point; the ground casts none, so the point is looked up moved along its own normal.

### shaders/cloud_shadow.hlsli: cloud_hidden

The share of the sun the clouds hide at a receiver from its point in the map's clip space `s`: the two layers of the
cloud map at `s` times their scale plus their shift (the second turned by (0.8, 0.6)), 0.65 a + 0.35 b, smoothstep
around the cover's threshold, times the light a full cloud hides. `with_clouds` adds it to the map's share as a
second, independent occluder.

### shaders/shadow_receiver.hlsli: the receivers and the soft edges

How much of the sun the map hides at a pixel (0 .. 1; the receivers darken by it times the strength, or take the sun's
light away by it). `s` is the pixel's position in the map's clip space, `dsx` / `dsy` its screen derivatives (taken before
any discard), `pixel` its screen position. Every comparison follows the receiver's plane (its depth slope across the map,
clamped). A receiver weighs it by its facing to the sun: the ground by its normal, the meshes by their face normal
(`shadow_facing`). A unit's meshes compare from the unit's nearest depth (`front`) on every face. `range` holds, per
kRangeCell-texel cell of the map, the nearest and the farthest depth of the cells within the widest filter's reach and one
cell more (x, y), and the cell's own nearest (z), read bilinear by hand (`range_at`). A pixel nearer than every depth around is
lit, one farther than every depth around is shadowed. For the others the nearest depth around bounds the filter's radius:
the distance along the light to it times the sun's chosen size (soft.x), from soft.z to soft.y. When that bound is wider
than half a cell, a blocker search narrows it: kSearchTaps points on a disc of the bound and the pixel's own give the mean
distance from the receiver to the nearest caster around each that hides it, the radius that distance times soft.x (so a
shadow is sharp where its caster meets the ground); from half a cell to a cell the searched radius is blended in from the
bound. Then bilinear comparisons on a disc of the radius: kFilterTaps, or kFewTaps on a narrow one, weighted 1 - r^2,
with the result's gradient across the light from the same taps. Both discs lie across the light (the sun's own disc;
`disc` / `discInverse` map them onto the oblique map and back) and are Vogel spirals turned per 2 x 2 quad (interleaved
gradient noise), each quad pixel's taps on its own quarter of the spiral; a filter from 1.5 to 3 pixels wide blends in
the quad's mean (the neighbours' results moved to the pixel by their gradients, each at most 0.25 from the pixel's own).
The narrowest filter is the larger of soft.z and half the pixel's footprint. The search reads the small cell image, not
the map: the map's texels far apart and turned per pixel cost far more.

### game/world/world_client_shadow_casters.cpp: casters outside the view

The casters of the shadow map that the game does not draw this frame (section 13, item 10). After the world's object
draws every object standing where such a caster can stand (render::ShadowCasterArea, through the object grid's sectors)
whose bounding sphere passes the map's test (render::ShadowCasterReaches) hands its parts to the map
through the world's mesh hand-over as casters alone (render::BeginCaster, CastsShadow with the strength found,
FigureMeshDrawer::HandOverCaster: render::CasterMesh), placed as Object3DClientSpecific::DrawParts places them. An object the world's draw drew
this frame (whole: R37f13; render::CasterDrawn) is passed over after the sphere test (which still moves the map's near
plane). Everything here only reads the game's
objects, so the game's logic and its goldens are those of a run without it.
