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
  - `gpu_shaders.h`: each uniform block's struct (`MeshVsParams` and so on, Matrix4 for a matrix; its size asserted),
    and per program a namespace with its index in `kPrograms` and its slots: `kBlock<Type>` (the uniform push slot),
    `kSampler<Texture>` (the texture-sampler pair's slot) and `kAttr<Input>` (the vertex input's location).
  - `gpu_shaders.cpp`: the program table, with each stage's code in every format, its entry point and its sampler and
    block counts for `SDL_CreateGPUShader`.
- **The check.** `ei_shader_check` (tools/shader_check; build_checks.py on Windows, ctest `port_shaders` on Linux) makes
  every program and a pipeline for each kind of target the renderer draws it into (`kDraws`): on Direct3D 12 and Vulkan
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
- **Blocks.** `ConstantBuffer<T>`, whose members are `float4`, `int4` or arrays of them, and `row_major float4x4`. The
  matrix's register k is the Matrix4's row k, as `m[k]` reads it; a Matrix4 is uploaded as it is in memory. An
  unsigned value goes in an `int` (`asuint` reads it back). No variable index into a component of a block's vector:
  copy the vector into a local first.
- **Names.** A block's type is its HLSL struct's name (`MeshVsParams`), its members named as there.
- **Shared parts.** `rect_vs.hlsli` and `fullscreen.hlsli` are the two vertex stages most programs share. The
  receivers' `shadow_receiver.hlsli`, `point_shadow.hlsli` and `scene_lighting.hlsli` take their slots from the
  including program's `SHADOW_*_SLOT` / `POINT_*_SLOT` / `SCENE_BLOCK_SLOT`.
- **The depth copy.** The programs that write the depth (`mesh`, `terrain`, `solid`, `into_multisampled`) also
  output it at `SV_Target1` (`depth_copy.hlsli`: the fragment's `SV_Position.z`). Only a
  multisampled pass's pipelines have that target (the smooth option); with one target it is dropped. The check's
  `kDraws` marks those draws `depthCopy`, and a second output must be one float.

A new program is its `.hlsl` file, which the build finds by itself, and a line in the check's `kDraws`.
`tools/comments.py` reads the `.hlsl` / `.hlsli` comments like the C++ ones.

## 1. No fallbacks

The game draws only through this renderer: it draws every frame into an off-screen target of the game's resolution and
presents it through its own swap chain on the game window (section 9); captures write that frame as
`frames\<name>.bmp`.

**When the renderer fails** (the user's rule: no fallbacks). When it cannot start or cannot go on, the game ends.
`render::detail::Fatal` (render.cpp) logs "render: fatal: <reason>" to ei-rebuild.log, shows the reason in the game's
error box (`app::ShowMessageBox`; the harness ends the run on it) and exits with -1, also with Rebirth's
DisableExitOnCrashReport. The cases:
- no device at the start (the first use): "Can't start the renderer: <gpu::Error()>." (no Direct3D 12 or Vulkan
  device: "no Direct3D 12 or Vulkan device (...)");
- a pipeline or texture the start makes: "Can't start the renderer: the terrain pipeline / the mesh pipeline / the
  blit pipeline / the white texture / the water's ripple map could not be made.";
- a pass that cannot begin: "The renderer cannot draw the frame." (the shadow map's and the point shadows' passes name
  themselves);
- no swap chain, or a swap chain texture SDL refuses (render_window.cpp): "The renderer cannot present on the game
  window: <gpu::Error()>." A lost device ends the game here ("No device-loss recovery", section 9).

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
- view setup (the view matrix from the camera pushed on the transform pipeline's stacks; fog range), the visible sector
  range (100 units);
- sky (a model following the camera, z-write and alpha test off);
- terrain ground pages (`SectorData::DrawPage` per page);
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
  figure pieces; `WorldClient::UnitAt` / `PickAt2` test the parts' rectangles only). Whatever draws the objects must still
  produce them. The rule (`device_draw.cpp` `BoundsOfProjected`, one path for every mesh): the bounds of the projected
  positions, clamped to the screen. The projection leaves screen positions when the whole mesh is on screen and
  clip-space ones when it crosses the view's edges; those count when inside the depth range (nothing behind the camera or
  past the far plane), mapped as ToScreen maps them. A crossing mesh always hands its passes over (the GPU clips them); a
  part with no position on screen gets an empty rectangle.
  - **Accepted difference from the original** (user's call): the original clipped a crossing mesh on the CPU and took
    the bounds of the clipped triangles' points inside every plane; an empty clip kept the last draw's rectangle. Ours is
    larger past a screen corner, and across one edge it can be wider along that edge: at the bottom edge an ogre's part
    picks a few 800x600 pixels further left or right than in the original. `render_pick_edges` (left and bottom edge
    clicks where both agree) and `render_pick_edges_rule` (a bottom edge click in the strip only ours picks) check it. The
    corners are under the battle screen's HUD, so no click reaches a corner's difference.
- **One real state change in a draw path:** the terrain page draw applies pending water level changes; the water layer
  is what height queries read. DrawSectorPage applies them before handing over.
- **Pixels:** the escape menu's background and the save game's 256x192 picture are copied from the frame on the GPU, and
  only the 5:6:5 picture is read back (section 6, "Screen copies on the GPU").
- **Timing:** up to five world updates per frame; vsync only changes grouping.

## 4. Hand-over map

| Data | Hand-over point | `render::` call |
|---|---|---|
| Clip matrix of the current draw | the transform pipeline's stacks (`TransformPipeline`, model.h: clip space = axis swap x projection x view x model; the screen matrix; the view stack) read at hand-over time | with each draw |
| Terrain sectors | `SectorData::DrawPage` (terrain.cpp): vertices (33x33 per layer, water after ground), vertex records (normal, material colour, the specular word: edge fade, on the water the crests), tile lists, edge flags, quarter verdicts; `HandOverSceneLights` (world_client_shadow_casters.cpp) | `TerrainPage`, `SceneLighting` |
| Meshes (figures, objects, sky, interface sprites and previews) | `device_mesh.cpp`'s mesh draws: positions and normals in packets of four, corners and texture coordinates (`render::VertexPacket` / `NormalPacket` / `MeshCorner` / `TexCoord`, mesh_records.h: the game's meshes hold the renderer's records), colours; matrix = the screen stack's top converted back to clip space; distance fog switch and range | `BeginMesh` / `EndMesh`; `MeshLighting` for parts lit on the GPU |
| Pass state | per pass of a mesh (material preset or two textures): `render::g_renderState` (section 5) | `MeshPass`; per terrain page at `TerrainPage` |
| Depth clears | the battle screen's panels, the hero preview, the trade grid, the hero equipment, screens 2 / 3 | `ClearDepth` |
| Shadows | `world_client_draw.cpp` (the sun; `BeginCaster` / `EndCaster` around each object's draws), `map_object_shadow.cpp` (`CastsShadow`), the casters outside the view | `ShadowSun`, `ShadowCasterMesh` (section 13) |
| Particles, ribbons, lightning, hit points, vision fog | pre-transformed draws: sprites (`renderer_sprites.cpp`), ribbons and triangles (`renderer_strip.cpp`), line strips (`renderer_lines.cpp`) | `TlDraw` with vertices after the fog pass |
| Weather | rain / snow triangles (`weather_particles.cpp`) | `TlDraw` |
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
renderer_sprites, renderer_strip, link_mesh_draw, particle_draw through `SetBlend`, world_client_draw, the interface's
draws); the original's reads around a draw (saved and put back) are reads of the members. `SceneRenderer::CreateRenderer`
resets it to a new Direct3D 7 device's state (the members' initialisers) before the game's own settings. Each pass copies
it (`ReadPassState`, render_mesh.cpp; the fog once a frame, `ReadFog`).

Not kept, because nothing in the renderer reads them: shade mode, dithering, z bias, antialiasing, perspective
correction, fog vertex mode, extents, clipping, lighting, ambient, fill mode, the texture coordinate index and transform
flags (every material stage's index was 0), and stages 2-7.

Stage-0 presets (texture.h):

| constant | colour | alpha |
|---|---|---|
| `kTerrainStage` | texture x current (MODULATE) | texture (SELECTARG1) |
| `kAlphaPassStage` | texture x diffuse | texture x diffuse |
| `kSceneStage` | texture x diffuse | diffuse (SELECTARG1) |

A material's preset (`RenderPreset`, model.h; render_helper.cpp) keeps its state list: `ApplyPass(0)` sets the
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
(`render::PictureFromMmp`). Scenarios: render_present, render_titles_present, render_movie_present, ui_compare,
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
- The global map's clouds (`GlobalMapClouds`, global_map_clouds.cpp): the CPU keeps only the three random words of each
  of a frame's four layers (`render::CloudFrame`: the game's random numbers are drawn as before), and `render::DrawClouds`
  each frame draws the 4:4:4:4 texture from two frames, the blend and the 256-entry alpha ramp (texture_clouds.hlsl:
  white, alpha ramp[(to x blend + from x (256 - blend)) >> 8], 8-bit alpha). A frame's 256 x 256 grey levels are built
  once by the GPU from its words (`texture_clouds_frame.hlsl`, into an R8 texture, at most 4 kept): the layers' hash, the
  3 x 3 smoothing in the cells' order, the octaves' bilinear sums and the byte store, integer for integer as the CPU code
  did.

## 8. Draws outside the frame: movies and the cursor

Interface draws (`ui_container_draws.cpp`; ui.md, "Container draws") set `render::g_renderState`, and their depth clears
go through `ClearUiDepth` (ui.h) to `render::ClearDepth`.

| Function | What ours does |
|---|---|
| `PlayMovie` (`bink_video.cpp`): the blocking movie player with its own message loop (Esc / Space end or skip a frame) | each frame to `render::MovieFrame`, then `render::Fill` (black) + `MovieBlit`, presented through the present; result 0 / 1 (1 with movies off) |
| `ShowLoadingMovie`: the loading movie (progres*.bik per zone) stretched over the screen; hides the cursor on every call and never shows it again | its last frame blitted and presented as above |
| The cursor | the system's cursor (section 9, "The cursor") |

The harness's input steps are not posted inside a movie (the harness empties movie.ini's lists). Scenario
render_movie_present (PlayMovie("crdtfin.bik") after a new game); the loading movie runs in every new game
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
  presented on the game window; with gamma, checked against a ramp that is not the identity). Scenarios
  render_present_window (windowed 800x600: the gamma slider held, the ramp checked; resized to 1600x900, the options
  reached through the mapped clicks) and render_present_fullscreen (full screen at 800x600 on the desktop's monitor: menus
  and battle screen, focus lost and given back, the window resized from outside and covering the monitor again).

### No device-loss recovery

A lost device ends the game. SDL_GPU reports no loss of its own: a lost device shows as a swap chain texture SDL
refuses (render_window.cpp), a fatal error (section 1, "When the renderer fails"). Why no recovery (user): a device is
lost only on a driver update or reset, a TDR or a GPU switch, rare for a player and a restart away. Making everything again needed every module to drop and remake its resources, each decoded
texture to keep its file's bytes, composed textures their recipes, and runtime
backends would need it twice. Dropping the kept file bytes took the process from about 130 MB to about 119 MB private
bytes on zone1.

### Resolution changes

The rendering resolution is the device's `screen` (`TextureDevice::screen`): the settings' `VideoResolution` at the start.
It changes while the game runs through one call, `app::SetResolution(width, height)` (app_resolution.cpp), between
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

The options window's resolution (setting 21, the display group's last row, ours): a spin control over the sizes of the
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

**Input in a window of another shape.** Mouse coordinates are frame pixels through the picture's placement
(`render::WindowToFrame`; the cursor's through `platform::MapCursorPoints`). A point over the bars goes to the frame's
nearest pixel (`present_window::MouseToGame`), so the edge pan works at the picture's edges; a press there is not
passed on and is logged ("input: a press outside the picture ignored"), its release is. Inside the frame the picture is
`Interface3D::PictureRect`: the screen less the letterbox bars while `ScreenFade::letterbox` is set (nothing in the
game sets it). A window narrower than 4:3 has the world over its whole height.

**The cursor is confined whenever the edge pan is available** (no option; a stale `ConfineCursor` line in
ei_settings.ini is ignored). `app::UpdateCursorConfinement`, run every main loop pass and at the window's focus
changes, asks `platform::ConfineCursor` (SDL_SetWindowMouseGrab) while the game's window is the active one and
`Interface3D::EdgePanLive` says the top window has an input side that pans at the edges (`UiInputSide::PansAtEdges`:
the `CameraInput` of the battle, briefing and view screens with the border scroll area above 0). The main menu, the
escape menu and the modal windows are other top windows, so the cursor is free there; it is let go too when the window
loses the focus or is minimised, and at the focus loss the input is reset (`Interface3D::ResetInput`), so an edge pan
the last mouse motion started does not go on for a cursor outside the window. Scenario `app_cursor_confine` (the
harness's `logmark` / `expectnewlog` / `expectnonewlog` look at the log after the last mark).

Scenarios: `render_options_resolution` (zone1 windowed at 800x600: 1280x720 and then 1280x1024 picked in the options
from the Esc menu), `render_resize_windows` (the Esc menu, the save window, the objectives, a zone tutorial, the camp's
inventory and trade, the global map, each resized while open; `samecapture` against the window opened at that size,
the game's clocks held between), `render_resize_menus` (the main menu, the options over it and their help, the same
way), `render_resize_dialogue` (a base's topics and phrases resized: captures),
`render_resize_zone` (zone1 windowed at 800x600, 1280x720, 1000x800 under the Esc menu, back to 800x600),
`render_resize_edgepan` (full screen at 800x600 on a 16:9 monitor: the camera pans at the picture's edges, a click over
the bar ignored), `render_present_window`, `render_cursor_present`; `ui_resize_text` (the options and the help captured at 800x600, again
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
still needs of that is `projection.cpp`, as members of `TransformPipeline` (model.h) and two free functions: `ToClip`
(clip space, the translation added first in each row; the decals' TransformPoints is this with the matrix untransposed),
`ProjectClipped` / `ProjectPointsClipped` (an outcode per point, `clip.h`'s `ClipCodeOf`, and the batch's verdict,
`ClipVerdict`; on screen the points go on to `ToScreen`), `Project` / `ProjectPoints` (the screen matrix's division),
`ProjectSprites`, `ToScreen`, `FogVertices` and `ClipBounds::Fit`. The pipeline keeps the viewport's half size, the fog's
range and the projection matrix itself (`SetViewportSize`, `SetDepthRange`, `projection`). Checked once bit-identical
against the DLL on random inputs.

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
- Sprites past the far plane are dropped before the sort: the order of sprites at exactly the same depth may differ.
- A sprite at z exactly +0 is drawn.
- A call with more than 511 sprites lost the colour and texture coordinates of some corners of its later batches in the
  original; each batch is sized before it is filled now.

## 11. Verification and frame capture

- **Frames are checked within one run**, at a frozen moment: `samecapture` (frames_basic, render_scene, render_present, ...: two captures in one frozen moment must match to the pixel); `tools/frame_preview.py` writes a
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
  default. `render::SetMsaa` rounds down to a power of two up to 8 (`kSmoothSamples`), and the target takes the device's
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
  own). The frame target is what every reader takes, always resolved.
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
- Every pipeline drawing into the frame is made for the pass's sample count (`detail::PassSampleCount`): a pipeline's
  count must match its pass's targets. A change of the sample count remakes the targets at a frame's first pass.
- Check (scenario `render_smooth`, vanilla and Rebirth): in zone1 at one frozen moment the frame with msaa 8 and
  anisotropy 16, with both 1 (`plain`) and with them again: the two with them match; `smoothcheck plain smooth 0.002 0.2`
  requires the levels drawn and the frames to differ in 0.2% .. 20% of the pixels; the quick save's picture matches the
  smooth frame; the lower levels each against `plain`. The game never reaches the copy into every sample (its world view
  is a frame's first draw), so the harness build's `smoothorder before|split|check` draws the frozen frame's passes in
  other orders and requires the copies counted.

### Frame capture

Harness. `capture <name>` skips two presented frames and writes the next to `frames\<name>.bmp` (24-bit): the renderer's
last frame (`render::ReadFrame`: before the gamma ramp, no cursor). `freeze` / `unfreeze` hold the game's clocks and the
camera (the camera's update runs once per frame even with dt 0 and moves a fixed fraction per call); the main loop then
runs no world, client or camera update. `samecapture <a> <b> [tolerance] [fraction]` fails the run when two captures
differ. `tools/compare_frames.py A B [--mask M1 M2 [--mask-background]] [--exclude E1 E2 ...] [--exclude-grow N] [--diff
OUT]`: per-pixel tolerance (default 8), differing fraction (default limit 0.1%), worst regions. What moves by itself
between frames without the game's clocks (other threads) is not held.

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
- `link_mesh_draw.cpp`: LinkMesh's draws; `link_loader_create.cpp`: `LinkFigureLoader` (Rebirth's ResOverride changes it).
- `weather_particles.cpp` (rain and snow), `effects_weather.cpp` (the visibility fog's draw, the rain / snow buffers).
- `renderer_lines.cpp`: `LineBatch` (points four to a packet in an aligned array, a count and a colour; `AddPoint`
  appends a point with w 1 and repeats it through the rest of its packet; `Draw` projects with the clip stack's top and
  hands the points to the line strip draw). Users: the figure parts' debug bounds and the world client's line lists; no
  scenario turns either on.

**Weather**: 81 x 81 cells (half a unit apart) around the point the wind carries a particle to the camera from. A cell's
place gives a 16-bit number; with the world's time it gives the cycle the cell's particle is in (a rain drop falls for 200
ticks, a snowflake for 1000) and how far it has fallen. `density` of 64 cycles show a particle; the cycle also shifts it
within the cell and picks one of the texture's four frames. Corners go to `ProjectClipped` in packets of four, the ones on
screen through `ToScreen` and, with the device's fog flag, `FogVertices`, then `render::TlDraw`. Snow sways by five tables
of cubics built at start. Checked by the goldens (rain in many scenarios, snow in rebirth_flags via `SetWeather`).

Original details kept: the rain's and snow's vertex colours and packet w are never written (what the buffer held stays,
the fog pass only lowers the specular's alpha); the rain's height uses the snow's rise (`kSnowRise`, both 40); a height
that is not a number would loop forever in the rain's wrap. Different in ours: a throw from the constructors does not free
what was built; fields the original left as stack contents are zero.

## 13. Data kept on the GPU, lighting and shadows

The direction (user): composite in shaders, keep data on the GPU, upload only what changed; no matching of the old
renderer's calls.

**1. Measuring.** `render::FrameStats` (render.h) counts per presented frame: the hand-over (terrain pages and batches,
meshes and passes, flat geometry, pre-transformed draws, blits, depth clears, state reads, matrices), bytes appended to the
vertex / index streams, uniform bytes, passes, draws, pipeline and first-texture changes, images made per kind with their
bytes. The harness build adds the CPU time (hand-over, texture work, EndFrame) and each terrain page's / mesh's vertices
against the last frame's (same / relit / moved / new). The harness step `renderstats <label>|-` logs the averages since
the last one into the report, with the main thread's CPU and wall time per frame; scenario `render_stats` measures the
main menu, zone1, a fight of twelve creatures, the camp, the base bz1g (a town), the global map and its escape menu.
`EI_PROFILE=1` gives the game side (tools/profile_report.py).
A part of the frame's GPU time (SDL_GPU has no timestamp queries): submit the command buffer before and after the part
and time each submission by its fence, from a thread that polls without sleeping at time-critical priority; a submission
made while the GPU still runs the one before is timed from that one's end. Only on a quiet machine: another build or
game makes the poller late, and its times mean nothing then (a run's main-thread CPU time above its quiet value says
so). The kit (a probe patch, its scenario and runner, the logs): build/R37e2 in the main checkout. PIX (installed) takes
a GPU capture from a run (the probe loads WinPixGpuCapturer.dll before the device and calls its `CaptureNextFrame`), but
its timing needs the Performance Log Users group (or an administrator), which the runs lack; its event list shows the
command stream.

**2. Where the time goes.** The main thread is the frame's limit (1.7-3.8 ms a frame in the zones and the town at 4K,
5.9-7.4 ms with a crowd; the GPU frame 1.5-2.6 ms); draws (a few hundred) and state changes are cheap, and triangles are
few (25-60 K a frame). A many-figure frame's main-thread time is mostly the game's: the server's AI path search in a
fight. So these were measured and not built, each worth well under 0.2 ms a frame: per-draw records and instancing
(the uniforms are 0.03-0.07 ms, mergeable draws about 0.06-0.09 ms); keeping interface sprites on the GPU (0.01-0.1 ms;
`render::SetInterfaceDraw` streams them, since the interface rewrites its quads in place in many places); water waves in
the vertex shader (about 0.03 ms; the game reads the water heights anyway); a GPU ground decal path (under 0.07 ms).
Figure animation stays on the CPU: creatures animate by their bones (each part's matrix, which the GPU applies), the
per-vertex morph is almost unused, and its result is read by game code (projection for picking).
On SDL_GPU (R37e2, times by submission) the town at night at 4K, msaa 8 takes the GPU 2.75 ms a frame (2.8 with the
crowd) and the main thread 1.1 (2.1) ms of CPU, so there a frame without a limit waits for the GPU (every submission
found the one before still running); 2.4 ms of it is after the shadow maps (the world's pass, its resolve and the
present).

**3. Terrain pages kept on the GPU** (`render_terrain.cpp`). Each sector page is built once, all four quarters, into a
pool of buffers written in place, only the changed ranges (gpu.h's `Write` with `Keep`): positions, texture
coordinates and normals in one stream, the lit colours in another, and absolute indices. A frame keeps the visible
quarters of each page handed over (`TerrainPiece`); ranges that follow each other in the pool are one draw. A page is
built again when its tile lists, records or animated tiles moved or the game rebuilt the sector
(`render::TerrainChanged(s, Rebuilt)` from Build and Clear). When the game only moves texture coordinates (animated tiles:
`Moved`) or the water layer's positions (waves, water levels: `WaterMoved`), only the pages of that kind read their
vertices again and upload the changed range; the visible quarters' colours are compared with the records every frame.
The pool (at least 128 K vertices) starts again with the frame's pages when full (made larger when they need more than
half of it); pages not drawn for 600 frames are dropped. Every tile is drawn at full detail at any distance. The shadow
map's fit reads a piece's vertices in runs of 72 with their boxes (`TerrainPieceGroups`). The circling water waves' sines
are computed once per Animate call (`CircleSines`: 33 values) instead of per vertex. Check: `terraincache off` builds and
uploads every page every frame; `terraincache check` compares every kept page with a fresh build every 16th frame (0
stale); scenario `terrain_water_waves` (the waves are always on; the digest's `water` group).

**4. Meshes kept on the GPU** (`render_mesh_cache.cpp`, the pool shared with the terrain in `render_buffers.cpp`:
`GpuPool`). Each figure mesh drawn through the figure part draws (either through the device or a link figure's part) is
kept under the mesh: its corners' model-space positions and texture coordinates, the colour and specular pair (a stream of
its own) and the valid triangles; the draw binds the pool with the mesh's first index, the texture offset (animated water
and lava) a shader constant. The game says what changed (`render::SetMeshSource` around the device's draw;
`render::MeshChanged`): an entry is built again when an array or count it was built from differs or the mesh or link
figure was rebuilt (`Rebuilt`); its positions are read again when the animation's per-vertex offsets changed or a link
figure's part was morphed again (`Positions`); its colours when a draw or the game lit or filled them (`Colours`). Only the
changed range of corners is uploaded. A mesh drawn again in the same frame with other data, interface sprites and mesh
draws without a source are streamed. Entries not drawn for 600 frames are dropped. Check: `meshcache on|off|check` (off:
every mesh streamed every frame; check: every kept mesh drawn compared with a fresh build every 16th frame). Risk: a
writer of a mesh's arrays in place that sends no signal; `meshcache check` reports it, and every stale mesh is logged ("a
mesh kept on the GPU was stale") in any run of the harness build.

**5. The game's shadow lists.** The game's own shadows (64 x 64 decals) are not ported; DrawShadow
(map_object_shadow.cpp) fits the shadow's sphere from the object's bounds (map_objects.md, "Shadows": the bounding sphere
swept along the light to the ground) for the shadow lists, which ShadowInView tests for objects out of view. The map's
casters out of view come from the outside walk (item 10).

**6. The device's mesh draws** (`device_mesh.cpp`, `device_draw.cpp`) project (the visibility verdict), override the
colours (draw flag 4: the hand-over reads them), take the screen rectangle for draw flag 1 (picking, section 3) and hand
every pass to the renderer. Not made: pre-transformed vertices, the fog, the z scale, the CPU clip of a crossing mesh.

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
  times the objects' lighting coefficient, the draw's lights in reach, with the part's normals and corner words;
  `mesh.hlsl`'s `figure_light` lights each pixel as 3DfpFPU's LightMesh lit each vertex (below). The colour arrays hold
  white with the material's alpha (`Material::GpuLitColour`).
- **Shadows in the light.** On the ground a shadow takes away the sun's part: lit = the smaller of the light with the sun
  hidden (the ambient plus the material's own) and the sunlit light x (1 - hidden x strength x the normal's turn towards
  the map's sun), so never lighter than half the sunlit light; the point lights' light is darkened alike (the towns'
  lamps burn by day too, and their unshadowed light hid the units' shadows). Every other mesh is darkened by (1 - hidden x
  strength) on faces turned to the map's sun by their face normal (`shadow_facing`; not by the smoothed vertex normals,
  which can lean far from their faces). Receivers look up the map at their point moved 1.5 map texels along the normal.
  The ground casts into the map.
- **Units take no shadow of their own** (a low-poly creature flipped triangle by triangle between shadowed and lit). The
  meshes of a unit's draw (`render::BeginCaster(object, true)`) compare from the unit's nearest depth in the map
  (`FigureFronts`: the least over its meshes of their model-space box corners through their light matrices;
  `MeshBatch::front`), as a plane facing the light, on every face: nothing of the unit is nearer the sun than that, so it
  never darkens itself, and every caster nearer the sun shadows all of it alike. Its sun side is shaded by the lighting's
  N.L alone. Lost: two intertwined units do not shadow each other. Static objects keep the face-normal rule.
- **What stays on the CPU**: the blood prints' levels (saved), the sky's lighting (`SkyObject::Light`, small), the
  terrain's edge fade (also read by the figures' draws and the ground prints).
- **Point-light shadows** (`render_point_shadows.cpp`, `shaders/point_shadow.hlsli`). Once a frame (PreparePointShadows,
  from PrepareShadows, counted as the shadow map's) the kept scene lights (`SceneLighting`'s nearest 16) whose reach is in
  the camera's view and that may cast (`ScenePointLight::shadows`) are taken strongest first (brightest channel x range), at
  most `kMostPointShadows` (4); each one that some caster reaches gets a cube of six 512 x 512 D32 faces (perspective, 90
  degrees, near 0.05, far its range) in one atlas (a light a row, a face a column). Casters: the sun map's (strength at
  least 0.5, cut out by alpha alike) whose bounding sphere reaches the light, drawn into each face its sphere touches. An
  object with a mesh sphere within `kHoldMargin` (0.5 units) of the light casts nothing for it (the campfire's spit, a
  lamp's post). An object's own light casts none: the light a party unit gets at night and a quest light are flagged
  `Off` by CreateLight (the unit held it only by the margin, and his own shadow popped in on fast frames). Receivers (only
  what is lit on the GPU): the ground's point loop (looked up moved along the vertex normal; the ground casts none) and the
  figure parts (`MeshBatch::landscape`; a unit's meshes compare from the unit's nearest distance, `MeshBatch::pointFront`;
  other meshes only on faces turned to the light). Filter: 8 bilinear comparisons on a 1.5-texel Vogel disc turned per
  pixel, a 1-texel depth bias, the caster pipeline's slope-scaled bias; a face's lookups stay inside it. Not done: the
  ground and objects outside the view cast no point shadows; lights beyond the 16 kept cast none; the set of mapped lights
  changes as the camera moves.
  The faces are kept across frames: a light keeps its row while it stays where it is with the same range (`GiveRows`),
  and a face is drawn again only when its light or the list of its casters changed. A caster is the same when its kept
  mesh and `CachedMeshVersion`, index range, texture, sampler, cut-out and texture offset are, and its model matrix is
  within `kKeptPlace` / `kKeptTurn` (a building moves by rounding from frame to frame). Only the faces drawn are cleared;
  units' idle animations redraw the faces holding them every frame. PreparePointShadows keeps its working lists across
  frames, makes one sphere per mesh (`BoxSphere`) and compares squared distances (`NotFurther` takes a root only where a
  test is too close to tell).
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
half). Once a frame before the first pass (`PrepareShadows`): the camera's matrix is the world's first ground batch's; its
inverse times a mesh's clip matrix gives the mesh in the landscape's space; a view along the sun's light (tilted to at
least 20 degrees; its x and y the landscape's where a point meets a level plane along the light, an oblique projection) is
fitted around the visible ground (in steps of 8 units, the texels' corners on whole texels of the landscape at the plane's
height, the visible ground's middle in steps of 8; along the light from the nearest caster that reaches it), 4096 x 4096
D32. The ground is taken in runs of 72 of a terrain piece's
vertices: a run wholly inside the view (widened by 5% at its sides) by its box's corners, one the view's sides cross by
every second vertex inside them, tested against the planes it crosses only. The casters' model meshes (first pass of
each; alpha-tested or blended passes cut out by stage 0's alpha read from the texture's level 0: a smaller level seen
from the sun at a grazing angle averages fronds away) go into it in a depth-only pass. Receivers: the ground pages (not
water: the ground under it) and the world's model meshes after the ground (not the sky, the flat geometry or additive
passes) sample it (`shaders/shadow_receiver.hlsli`) and darken by up to half (the old decal's alpha 0x80) before fog. The
map is the only shadow look.
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
  stays sharp at its feet. Check: render_shadows_trees (gz2g's forest at the default and the far zoom, the gate,
  a point light under the trees at night; flora shadows on; `EI_RESOLUTION=3840x2160` for the user's view).
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
  Object3DClientSpecific::Draw places it; a part the game drew this frame is left out (`render::CasterPartDrawn`). Cut out
  by alpha as the on-screen meshes with the same texture were, else at alpha 0.5. A part's corners are kept on the GPU in
  the pool (`KeepCasterMesh`) and read again only when the game says the part moved or was rebuilt.
- **Faces turned away from the sun** have no sun light to lose (the GPU lights them by N.L), so a receiver darkens by its
  own facing with a normal-offset bias. Check: render_shadows_mound (the user's save in tests/saves/shadow_mound).
- **Steady soft edges.** Per-cell reads of the range image made cell-sized squares and straight cuts in a penumbra that
  crawled as the sun turned (the user's shimmer). So
  the range is read bilinearly between the four cells around the pixel (`range_at`: four `texelFetch` / `Load` and a lerp, clamped
  at the map's edges; no filtered RGBA32F read, which Vulkan does not promise), and the
  dilation reaches one cell more (`g_cellsAround`), so each of those four cells' bounds holds for all the pixel's taps and
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
  draw applies no uniforms. The draws are sorted by what they bind, and the pipeline and bindings are applied only when
  these change: a depth-only map with LESS_EQUAL keeps the nearest depth whatever the order.
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
  takes the clouds alone (`ShadowParams::on` 0: `cloudParams`). Not under clouds: the sky, flat geometry (ground prints,
  markers) and additive passes, as for the map.
- Check: render_clouds (zone1 at noon: the frame and the shadow view at one frozen moment and five seconds later,
  `diffcapture` between the views; rain: `cloudcheck` with the cover at 0.7 .. 0.8; night and the cave gz5g:
  `cloudcheck 0 0`). `cloudcheck <least> <most> [<least cover> <most cover>]` reads ShadowInfo's `cloudLight` and
  `cloudCover`; `shadowview on` shows the clouds with the map's shadows.
- GPU cost (the town bz1g by day at 3840 x 2160, msaa 8, alone and with a crowd of 12; whole-frame times by submission,
  item 1; five rounds interleaved with the clouds switched off by a probe): none measurable, the medians 2.21-2.35 ms
  with them and 2.27-2.40 without (the machine's noise is larger than two R8 reads a pixel).

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

The world ticks every 55 ms. The main loop (`RunPass`, src/game/app_main.cpp) moves the camera once a frame by the
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
  pose. Inside a tick the update then notes its own step.
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
- Read by the object's matrix in CollectVisible (the clip test) and `Object3DClientSpecific::Draw` (so picking's part
  rectangles follow what is drawn, as with the camera) and the outside shadow casters. The own pose (localPosition /
  localRotation: digests, saves, AI, sounds, PartWorldPosition) never changes.
- **Path changes:** a unit's new client path (UnitClientSpecific::MotionChanged, from the server's update) can start a
  little off where the unit was drawn (0.25-0.35 units when the hero sets off), so its placement jumps once. The jump
  from the place last shown (`drawn::Show`) fades out over 150 ms of real time (smoothstep), times the pass's spread
  (none at the harness's default pass), unless longer than the cut.
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
- **Readers:** the GPU's point lights (`HandOverSceneLights`, the ground's and the point shadows') and the figure parts'
  lights (`PartLighting`), which must agree to the bit: a mesh's light finds its point shadow by the scene light's place
  (`SceneLightIndex`). So the scene's hand-over runs in DrawPass after the objects' placement (CollectVisible) and before
  any mesh draw; the ground's draws read it when the frame is drawn. The game's own readers keep the game's place: which
  lights reach an object (`InReach`, and with it the Static flag the digests read), the shadow sphere's light direction
  (`FitShadowSphere`), the blood prints' grey, the map's light marks.
- Check: `render_drawn_lights` (vanilla, 8 ms frames, zone1 at night): the hero walks with its light traced
  (`objtrace start leader light`: drawn without a jump, own with one at each tick, and the light's largest drawn step at
  most 1.1 of the unit's: 0.036 both; the game's place stepped 0.217 a tick), then a fireball's light and its blast's
  rising one (`objtrace start spelllight`: drawn without a jump, largest step 0.097 against the own 0.667 in flight,
  0.022 against 0.150 rising). A script cast's flight light stands at height 0 and lights nothing visible.

### Animation blends

A switch from one animation to another (`FigureAnimationState::Play`, or SetTime passing into the next queued one)
blends the pose last placed into the new one's (map_object_animation.cpp, figure_bones.cpp). Always on.
- `BeginBlend` keeps each placed bone's own turn, the root's position before the origin and the mesh's offsets as the
  outgoing pose; `FigureBone::Place(origin, blend)` slerps the turn and lerps the root's position and the morph offsets
  by the incoming share (at 1 the original's computation, bit for bit); `UpdatePose` places the skeleton again at every
  draw while a blend runs.
- The share: smoothstep of the real time since the outgoing pose was placed (`placedAt`: the draw before the
  switch; platform milliseconds, so a blend running also finishes in a pause) over the length, by the incoming
  animation's kind: attack, cast and hit 100 ms (the blow's moment reads at once), death 150, idle 250, the rest
  (walk, run, cross, special, map objects' None) 200. None from or into no animation (a figure built anew, a fresh
  unit). An object not drawn for longer than the length switches at once.
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
- `camera_smooth` (vanilla and Rebirth): pan, turn (Ctrl), zoom (Page Down) and follow (Home, the hero walking).
  `camtrace` keeps each recording as the drawn view (`pan`) and the camera's own (`pan.camera`). `camsame` (3%): the drawn
  view covers what the camera does. `camsmooth`: the drawn view without a jump, the camera with (a jump is a frame whose
  step is more than twice both neighbours').
- `camera_spin`: the low keyboard spin (`camsteadier`: the drawn height's largest step at most 0.35 of the camera's) and the
  fast drag zoomed out (turn without jumps; `camsame ... reach`: the drawn view's distance from its focus changes as the
  camera's own does, within 3%).
- `camera_drawn` (golden, vanilla and Rebirth): the hero walks while the camera pans and turns, and clickunit clicks a
  wolf mid-pan and mid-turn where the drawn frame's picking finds it. What the drawn view feeds (the Shown flags, the
  visible and shadow lists, picking) must not reach the world's state.
- Harness: `keydown` / `keyup` (a key held over frames), `frametime <ms>`, `mousedrag <dx> <dy>`, `camtrace` / `camsame` /
  `camsteadier` / `camsmooth` (tests/verify/harness_camera.h; kinds pan, turn, zoom, height, reach); `objtrace start <who>
  [part|-] [log]` / `objtrace stop <name>` (an object's drawn and own place, or a part's place in the figure, or with
  part `light` its own light's, or with who `spelllight` the first moving point light effect's, after each frame's
  draw; the same checks), `animblend on|off`.

## 15. The performance overlay

- **Switch**: in ei_settings.ini (standalone.md, "Settings file"):
  ```
  [renderer]
  PerfOverlay = true
  ```
  Off when absent or false (a new file has it false). Read once the renderer starts (`PerfOverlaySetting`,
  device_mesh.cpp) into `render::SetPerfOverlay`. **Never in a harness run**: `render::PerfOverlaySupported()` is false in
  the harness build.
- **What it shows** (top left of the game image, refreshed every 250 ms, averages over the last second):
  ```
  FPS 144   FRAME 6.96 MS   WORST 9.13
  GPU 2.10 MS   WORST 2.60
  CPU SUBMIT 1.10 MS   SHADOW 0.12
  DRAWS WORLD 370   SUN 120   POINT 80   UI 25
  ```
  `FPS` and the frame time (between presents, so vsync's wait counts) with the worst frame; `GPU` the whole frame's GPU
  time (one submission's work, the present's pass with it; the worst too: section 15c, "The frame's GPU time"). `CPU
  SUBMIT` the main thread's time in EndFrame's draws up to the present, and of it `SHADOW` the shadow map's fitting and
  pass; `DRAWS` the frame's draws by pass (`perf::Pass`: the sun's map with its range passes, the point lights' faces,
  the world view, the interface). `GPU -` until a frame's time comes back.
- **How** (`render_perf.{h,cpp}`, `shaders/perf_overlay.hlsl`): `BeginDraws` at the first DrawPending begins the frame
  in UI, `Enter(pass)` marks each change of pass for the draw counts, `EndDraws` after DrawToWindow; `FrameDone` at the
  present takes the latest `gpu::FrameGpuTime` (the frame timing is on while the overlay is). The text is a built-in
  5 x 7 font of capitals, digits and `.:/-+%()` drawn into an R8 texture, written before the present's pass when the
  text changed, and drawn as one quad by whole-pixel scale (`Scale`: one per 540 rows of the game image) over a 60%
  black box, in the present's pass after the frame.
- **Never in the frame target**: captures, frame checks, the screenshot, the movie recorder and the escape menu's / save
  game's pictures read the frame target, which never holds it.
- **Check**: `ei_perf_check` (tools/perf_check, a build check) on gpu.h's device (hardware Direct3D 12): the lines of
  known numbers, the font, the image's pixels; the overlay over a cleared BGRA8 target at 720 and 2160 rows against the
  image scaled up; frames through every pass (each pass's draws, the CPU times and the whole frame's GPU time come
  back).

## 15a. The vertical blank switch

- **Switch**: next to PerfOverlay, same file and section:
  ```
  [renderer]
  VSync = false
  ```
  On when absent or true (`PresentMode::Vsync`); false turns it off. Read once the renderer starts (`VsyncSetting`,
  device_mesh.cpp, before the first present) into `render::SetVsync`. **Never in a harness run**:
  `render::VsyncSwitchSupported()` is false in the harness build; the harness's own `SetWaitForVsync(false)` under the
  virtual clock presents in `Mailbox` (no wait, no tearing).
- **Off**: `PresentMode::Immediate` (no wait; may tear), which gpu.h's `SetPresentMode` turns into `Mailbox` where the
  swap chain has no immediate mode, and `Mailbox` into `Vsync` where it has neither (render_window.cpp logs the mode it
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
  beyond a cheap key if they want one. A `PipelineDesc` is the program, the vertex buffers (0..n-1, no gaps: SDL's
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
  is compared byte for byte with the one last pushed in its slot). `Counts()` says what the draws sent and what the
  filter left out; in a real run (scenario `render_stats`, 272k draws) 8% of the pipeline calls, 4% of the vertex and 4%
  of the index buffer binds, 12% of the texture calls and 30% of the uniform pushes were sent. `SetStateFiltering(false)`
  turns the filter off for `ei_gpu_check --bench` alone. `Draw`, `DrawIndexed` (with a first instance).
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
  Mailbox, falling back to Vsync), `SwapchainFormat`, `AcquireSwapchain` (waits while too many frames are in flight;
  an empty texture while the window is minimised or hidden, which SDL's Vulkan driver treats so). SDL follows the
  window's size by itself.
- **The frame's GPU time** (the overlay's GPU line, one whole-frame time, user 2026-11-03): SDL_GPU has no timestamp
  queries, so `SetFrameTiming(true)` starts a thread that polls the submissions' fences every 0.2 ms; a submission's
  time runs from its submission (or the end of the one before, if later) to when its fence was seen signalled.
  `FrameGpuTime` gives the latest. The thread only queries fences: SDL's Direct3D 12 `SDL_WaitForGPUFences` holds the
  submit lock while it waits, so a waiting thread would stall the main thread's submissions.
- **Fences**: each `Submit` keeps its submission's fence until the submission is seen finished; with the frame timer
  off, `Submit` asks the older ones itself, so a finished submission's fence goes back to SDL at the next submission
  (`SubmissionsHeld` counts the kept ones). Before (H50) only the timer or a download's wait asked, so outside the
  overlay every fence was kept: thousands a minute, which lavapipe and Windows' Direct3D 12 took, but Dozen on WSL
  turned down after a few seconds of several games (`vkCreateFence` VK_ERROR_OUT_OF_DEVICE_MEMORY, then an abort).
- `Problems()` counts misused calls (each logged "gpu: ..."); the check holds it at its deliberate misuses.

**Cost** (`ei_gpu_check --bench [--driver NAME] [--frames N]`, tools/gpu_check/gpu_bench.cpp; the numbers below are
the RTX 4070 Ti SUPER on Windows 10, two runs of 3 x 500 frames, a quiet machine; the spike's tables are in
build/reviews/r43_sdl_gpu.md, 'Numbers'). The bench replays a synthetic world frame through gpu.h's calls the way
render_mesh.cpp and render_shadows.cpp make them (155 instanced caster draws into a 4096 x 4096 depth map, 263 mesh draws
with five uniform blocks and six samplers, the present; half of the draws keep the last one's mesh, 40% its textures),
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
- the casters' program from vertex, instance and index buffers at offsets with a first instance; the depth test; the
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

### render_cursor.cpp: the cursor

The game's cursor as the system's (section 9, "The cursor"): render::Cursor keeps the frame's cursor (its picture's frame,
mask, hot spot and hold), and at the frame's present ShowFrameCursor shows the matching shape through
`platform::SetCursor`, made at its first show (`platform::MakeCursor`) from the picture's frame scaled to the window and
through the gamma ramp; shapes of another scale or ramp are let go once not shown, those of a picture with its holder
(ForgetCursor). A present without a cursor hides it; a held one is kept still (`platform::HoldCursor`).

### render_mesh.cpp: meshes

Everything the game draws through the device's mesh draws (figures and map objects, the sky, the interface's sprites and
3D previews). The game hands the mesh over in model space with the matrix it projects it with (BeginMesh); each pass then
(src/game/device_draw.cpp) becomes a batch with the render state of that moment (MeshPass, section 5): blend, alpha test,
depth, culling, fog, the two texture stages' operations, filters and textures. A figure mesh drawn through the figure part
draws is kept on the GPU (render_mesh_cache.cpp) and its batches draw from that pool; every other mesh is built into the
frame's stream. A figure part handed over with its lighting (MeshSource::lighting) is lit in the shader from its normals.

### render_mesh_cache.cpp: meshes kept on the GPU

Section 13, item 4. The figure part draws (src/game/figure_part_draw*.cpp) name the figure mesh a mesh draw draws
(render::SetMeshSource); the mesh's corners are kept for that figure mesh (positions and texture coordinates in model
space, colours and triangles) in a pool on the GPU (GpuPool: geometry and colours in streams of their own). What is built
and uploaded again is what the game says changed:
- the colours, when the draw wrote the colour arrays or the game lit the mesh outside its draw (MeshChanged Colours: the
  sky; a link figure's LightPart / FlatColours) or a draw that wrote them was not kept (NoteMeshDraw): read and compared
  with the kept ones, the changed range uploaded;
- the positions, when the game moved them (MeshChanged Positions: per-vertex offsets, FigureMesh::SetOffsets; a link
  figure's part morphed again): read and compared, the changed range uploaded;
- all of it, when the mesh's buffers changed (MeshChanged Rebuilt) or the draw hands over other arrays or counts than it
  was built from. Interface sprites (render::SetInterfaceDraw) and a mesh drawn again in the same frame with other data go
  into the frame's stream. The texture offsets (an animated mesh's frame of its sheet) are the draw's, added in the
  shader. A kept mesh also keeps the box of its positions (CachedMeshBox: a caster's box for the point shadows' spheres and
  the units' fronts), taken again only when they were built or moved. The casters outside the view
  (render::ShadowCasterMesh) are kept the same way by part (KeepCasterMesh: a map of their own, positions and texture
  coordinates only; read at most once a frame), drawn from the pool with the part's matrix and texture offsets. The harness
  build compares every kept mesh, box and caster with a fresh build every 16th frame (`meshcache check`); `meshcache off`
  builds every mesh into the frame's stream (and every caster again each frame).

### render_point_shadows.cpp: point-light shadows

Section 13, item 9: the few strongest point lights in view get a cube of six depth faces each, drawn from the sun map's
casters, and the receivers lit on the GPU take each such light away where it is hidden: the ground (scene_lighting.hlsli's
point loop) and the figure parts (mesh.hlsl's figure_light). The faces are drawn in one pass: their clears first (the
faces do not overlap), then each face's casters through `CasterDraws` (render_shadows.cpp), sorted within the face.

### render_shadows.cpp: object shadows through a sun shadow map

Section 13, item 10. Once a frame, before the world draws, PrepareShadows fits an orthographic view from the sun around
the visible ground and gives every receiving batch the matrix from its model space to the map; DrawShadowMap draws the
casters' meshes into the map (depth only; alpha-tested and blended passes cut out by their texture's alpha), then the
map's depth range per cell. Objects the game does not draw whose shadow can reach the visible ground cast too
(ShadowCasterMesh, tested against the map's fit made right then). The point lights' shadows (render_point_shadows.cpp) are
prepared and drawn from here, inside the shadow map's time. Space: the meshes' matrices are clip matrices, the terrain's
the same for the landscape's space, in which the map is fitted.

### render_cloud_shadows.cpp: cloud shadows

Section 13, item 11. CloudShadows keeps the frame's weather; PrepareClouds (from PrepareShadows, outside a pass) makes the
cloud map at the first frame and moves the layers' drift and the cover on by the world's time; CloudUniforms writes
the receivers' part of ShadowParams for the map's fit; BindCloudMap binds the map (bilinear, repeating) for the terrain
and mesh programs.

### render_terrain.cpp: the terrain

The game's terrain passes (ground pages, water pages) call DrawSectorPage (SectorData::DrawPage, src/game/terrain.cpp) for
every visible sector page; it hands the page here. The page's tiles are turned into world-space triangles (every tile's
nine vertices and eight triangles, the full detail at any distance), with the vertex records' normals and material
colours, the tile texture coordinates and the page's texture; the clip matrix is the pipeline's. The GPU clips. The pages
stay on the GPU (section 13, item 3).

### render_water.cpp: the ripple map

The water's small ripples (R17a): a 256 x 256 RGBA8 map with its levels down to 1 x 1, made at the start (section 1) from
a fixed generator of its own (the same map on every run and system, nothing drawn from the game's): r, g the slopes of
40 waves along x and y, b the foam's pattern (the height of 24 finer waves), each wave a whole number of cycles across
the map so it repeats without a seam. The terrain batches bind it with a repeating, linearly mipmapped sampler
(anisotropy 8); RippleOffsets gives its two layers' offsets at the client's time (SceneLights::clientTicks: the client
world's tick and the fraction of the next one it has had), so the ripples hold still under `freeze` and move with the
game's clock.

### shaders/terrain.hlsl: the water

The water pages keep the game's look underneath: the page texture lit by ground_light from the vertex normal with the
game's crests (the vertex waves, the water levels and the crests' specular stay the game's: SectorData::AnimateWaves),
alpha the material's opacity, fog as for the ground. `Water` then, per pixel:
- **Ripples**: two layers of the ripple map, one repeating every 9 landscape units and one every 5 turned against it,
  drifting apart; their slopes (calmer within half a unit of the shore) turn the vertex normal. The rippled normal lights
  the layer up to 30% more or 25% less as it turns to the sun or away (kRippleShade, by the sun's share of the light).
- **Depth** (`depth`, PageVertex's: the water's depth over the ground at the vertex, from the ground record,
  interpolated): the share 1 - e^(-0.5 depth) of the depth's look: from half the material's opacity at the shore to 1.15
  times it in deep water, and up to 15% darker.
- **Reflection**: Schlick's Fresnel term (0.02 head-on) of the rippled normal towards the camera (SceneLightParams::camera)
  blends in the sky, taken as the fog's colour at the horizon and 0.75 of it overhead (no sky texture or screen-space
  reflection yet); the reflected part is opaque, so the water hides more of the ground at a grazing view.
- **Sun glints**: a sharp and a broad Blinn-Phong lobe of the sun's colour on the rippled normal, gone as the sun sets.
- **Foam**: within half a unit of the shore, the ripple map's foam pattern above a threshold, lit by the ambient and sun
  light, at most 0.4 opaque.

The depth is per vertex (one unit apart), so the foam's band follows the triangles near the shore. The water still
receives no sun shadows (the ground under it does), so glints show in a shadow too. Cost (R17a, 3840x2009, msaa 8,
frame GPU time on a quiet machine, frozen frames of render_water's scenes): the water pass takes 0.07-0.95 ms (river,
sea shore, lake by day, lake at night: frames without its draws against with them), and the look above adds nothing
measurable to it (within 0.03 ms of the game's plain water shading). Check: scenario `render_water`
(frozen moments on zone1: the beach at the west sea, the sea, the lake in the middle, the river with stones, the river
and the lake at night; samecapture of a frozen moment drawn twice, diffcapture that the ripples move).

### render_perf.h: the performance overlay

Section 15: frames a second, the frame time, the whole frame's GPU time, the renderer's CPU time and the draws by
pass, as a few lines of text in the frame's top left corner, drawn by the present into the swap chain's texture after
the frame (render_window.cpp, DrawToWindow), never into the frame target. Off unless the settings file switches it on;
the harness build never draws it.

### shaders/mesh.hlsl: figure_light

A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting), at the model-space point `p` with the
normal `n`: per channel the largest of the ambient colour, the sun's lit colour times the cosine to it and each light's
lit colour times its cosine and falloff, plus the emissive colour, at most 1. The crown: pulled towards its average colour,
taken as the average over every direction a normal can face (the mean of max(ambient, sun x cosine) for a cosine spread
evenly over -1 .. 1; 0.6 of the way). The shadow map darkens it after (mesh_fs). A light with a point shadow adds only what
reaches `land` (the point in the landscape's space; `face`: its face normal there, turned to the camera): a unit's mesh
compares from the unit's nearest distance to the light, on every face; any other only on faces turned to the light (by
`face`), looked up moved along `face`.

### shaders/scene_lighting.hlsli: ground_light

The landscape lit as the game's vertex lighting lit each vertex, at a point `p` (the landscape's space) with the unit
normal `n`: `m` the material colour the sun and ambient light start from (the water's; on the ground the water's above
it, else black), `mp` the one the point lights start from (the record's own), `loss` the light lost under the water (0
above it), `hidden` how much of the sun the shadow map hides at the point and `strength` the map's darkening
(render_shadows.cpp kStrength). In the sun max(material + facing x sun, ambient), in the shade ambient + material, as the
game lit them; but a shadow is never lighter than the sunlit light times 1 - strength (where the ambient light is nearly
the sun's, as in the towns, the game's shade alone hardly shows), on faces turned to the map's light (a face turned away
keeps its light). The point lights (each material + its light, at most 1 per channel, by its falloff and facing, dimmed
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

### game/world_client_shadow_casters.cpp: casters outside the view

The casters of the shadow map that the game does not draw this frame (section 13, item 10). After the world's object
draws every object standing where such a caster can stand (render::ShadowCasterArea, through the object grid's sectors)
whose bounding sphere passes the map's test (render::ShadowCasterReaches) hands its undrawn parts to the map
(render::ShadowCasterMesh), placed as Object3DClientSpecific::Draw places them. Everything here only reads the game's
objects, so the game's logic and its goldens are those of a run without it.
