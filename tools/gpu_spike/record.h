#pragma once

// The SDL_GPU spike's frame recorder (tools/gpu_spike/README.md), in the spike build only (EI_GPU_SPIKE):
// sokol_gfx's trace hooks keep every description and the calls since the last commit; a requested dump writes the
// frame that commits next, with each buffer and image it uses read back on Direct3D 11, to
// %EI_GPU_SPIKE_DUMP%\<name>.eigpu.
namespace gpu_spike {

// After sg_setup.
void InstallRecorder();
// The frame that commits next is written as `name` (when EI_GPU_SPIKE_DUMP names a folder).
void RequestDump(const char* name);

} // namespace gpu_spike
