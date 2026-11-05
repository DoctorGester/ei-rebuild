SDL3 (https://libsdl.org, https://github.com/libsdl-org/SDL), zlib licence (LICENSE.txt).
SDL 3.4.18 (tag release-3.4.18, commit 829a65d769d935c4852f8159e964312c0957260a) from its source release
SDL3-3.4.18.tar.gz: CMakeLists.txt, cmake/, include/, src/, wayland-protocols/, LICENSE.txt and .git-hash;
its tests, examples, documents and IDE projects left out. Built statically into ei-rebuild.exe (CMakeLists.txt).
Used by the platform layer (src/platform/sdl, src/audio/audio.cpp; docs/notes/platform.md).

Local changes from that release:
- src/video/cocoa/SDL_cocoamouse.m and SDL_cocoawindow.m honor SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE: keep the
  selected cursor visible without warping it to the window centre on capture. The game holds its rotation cursor
  at the right-button press point while reading relative motion.
- src/gpu/metal/SDL_gpu_metal.m: METAL_UploadToTexture reads the source's pixels_per_row / rows_per_layer (it took
  the region's width, so gpu.cpp's 256-byte padded rows went up skewed on Metal; port_gpu's read-back checks), as
  METAL_DownloadFromTexture and the other drivers do. Upstream's fix is commit 32f9c098d5 on main (2026-11-02),
  not in release-3.4.x as of 3.4.20: drop this change once the vendored release includes it.
- src/gpu/d3d12/SDL_gpu_d3d12.c: the hint (so the environment variable) EI_GPU_D3D12_WARP=1 picks the WARP software
  adapter (IDXGIFactory4::EnumWarpAdapter) instead of the high-performance one, in the driver's support check and in
  the device. SDL has no such switch; the Windows CI job (no GPU) and local checks of that path set it
  (docs/notes/harness.md, "The Windows CI job").
