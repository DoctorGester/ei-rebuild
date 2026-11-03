#!/bin/sh
# The SDL_GPU spike's Linux build (tools/gpu_spike/README.md), the counterpart of build.cmd: SDL_shadercross
# (with DXC's Linux release) into build/third_party/shadercross-build-linux, then ei_gpu_spike into build/gpu_spike_linux
# (sokol_gfx on OpenGL 4.5 beside SDL_GPU on Vulkan; no recorder, the dumps come from Windows). Needs the packages of
# docs/notes/linux.md and sokol-shdc (EI_SHDC, else build/tools/sokol-shdc). Run tools/gpu_spike/fetch_third_party.py
# first.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TP="$ROOT/build/third_party"
SHDC="${EI_SHDC:-$ROOT/build/tools/sokol-shdc}"
cmake -S "$ROOT/tools/gpu_spike/shadercross" -B "$TP/shadercross-build-linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    "-DEI_THIRD_PARTY=$TP"
cmake --build "$TP/shadercross-build-linux" --target shadercross_tool
cmake -S "$ROOT" -B "$ROOT/build/gpu_spike_linux" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_GPU_SPIKE=ON \
    "-DEI_SHDC=$SHDC" "-DEI_SHADERCROSS=$TP/shadercross-build-linux/shadercross/shadercross"
cmake --build "$ROOT/build/gpu_spike_linux" --target ei_gpu_spike
echo "Built: $ROOT/build/gpu_spike_linux/ei_gpu_spike"
