# The SDL_GPU spike (R43)

This draws one recorded game frame through SDL3's GPU API (Direct3D 12 or Vulkan) and through sokol_gfx (Direct3D 11
or OpenGL 4.5), then times both. It is a tool, not the renderer. The game's default build has none of it: the
CMake option `EI_GPU_SPIKE` (off by default) adds the recorder to the renderer and the `ei_gpu_spike` target. The
plan for the full port is in build/reviews/r43_sdl_gpu.md, and its short version is in docs/notes/renderer.md,
"SDL_GPU".

## Parts

- `record.cpp`, `dump.cpp`: the recorder. It uses sokol's trace hooks to keep every resource's description and the
  frame's calls. When the harness captures a frame (tests/verify/harness_capture.cpp, `RequestDump`), the next
  `sg_commit` writes the frame to `%EI_GPU_SPIKE_DUMP%\<capture>.eigpu`. The file holds every image and buffer the
  frame uses, read back from the GPU, plus the frame's calls.
- `replay.cpp`: plans a dump. The frame target is the colour image that most terrain and mesh draws write. Passes
  with draws of the ported programs are kept, along with the frame target's passes. It also times the run and
  compares the drawn frame with the dump's own.
- `replay_sokol.cpp`: replays the frame through sokol_gfx and platform/graphics.h with the game's own GLSL programs.
- `replay_sdlgpu.cpp`: replays the frame through SDL_GPU. `kBindings` maps sokol's binding slots to SDL's per-stage
  slots.
- `shaders/*.hlsl`: the HLSL for the ported programs: terrain, mesh, solid, shadow_caster, shadow_terrain,
  shadow_reduce and shadow_dilate. They were restored from commit a861650b3, the last before MAC3 moved the shaders
  to sokol-shdc GLSL, and brought up to the current GLSL. SDL_shadercross compiles them at build time to DXIL, SPIR-V
  and MSL.
- `shadercross/`: a CMake wrapper that builds SDL_shadercross's command-line tool from the downloaded sources and our
  third_party/SDL.
- `fetch_third_party.py`: downloads SDL_shadercross, SPIRV-Cross and DXC (Windows and Linux) into build/third_party.
  It pins each one's URL and SHA-256.

## Build and run

Windows:

```
python tools\gpu_spike\fetch_third_party.py
tools\gpu_spike\build.cmd
```

This makes two builds:
- build\gpu_spike: the verify image with the recorder, plus ei_gpu_spike. sokol's validation is on in this build.
- build\gpu_spike_release: ei_gpu_spike without the harness, which is the build to take timings from.

To record dumps, for example from the shadow scenes:

```
set EI_BUILD_DIR=<worktree>\build\gpu_spike
set EI_GPU_SPIKE_DUMP=<worktree>\build\gpu_spike_dumps
python tools\work.py test <ID> render_shadows
```

To replay one:

```
build\gpu_spike_release\ei_gpu_spike.exe build\gpu_spike_dumps\fight_map.eigpu --api sdlgpu --driver vulkan
```

The options are:
- `--api sokol|sdlgpu`
- `--driver direct3d12|vulkan`
- `--size WxH`: draws the frame target at another size.
- `--frames N`: default 300. Every run stops after 40 s.
- `--vsync`
- `--out x.bmp`
- `--reference ref.bmp`: writes the dump's own frame.
- `--debug`: lists the passes and compares every attachment with the dump's (SDL_GPU only).

Linux (WSL included): first install the packages in docs/notes/linux.md, then run `sh tools/gpu_spike/build.sh`
after the fetch. This builds shadercross with DXC's Linux release, then ei_gpu_spike with sokol on OpenGL and
SDL_GPU on Vulkan. Dumps are recorded on Windows only. On WSLg, `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`
picks lavapipe.

## What SDL_GPU needed

- Binding spaces in HLSL:
  - Vertex shader: textures and samplers in space0, uniform blocks in space1.
  - Fragment shader: textures and samplers in space2, uniform blocks in space3.
  - Vertex inputs are `TEXCOORD<location>` with `[[vk::location]]`.
- shadercross keeps the bindings of every stage, so code for only one stage sits under `EI_VERTEX` / `EI_FRAGMENT`.
- A pipeline's vertex buffer slots must be packed from 0. The D3D12 backend looks each buffer's description up by
  slot number, so with slots 0 and 2 the instance rows lost their per-instance step.
- SDL_GPU has no border clamp. `SG_WRAP_CLAMP_TO_BORDER` becomes clamp-to-edge here.
