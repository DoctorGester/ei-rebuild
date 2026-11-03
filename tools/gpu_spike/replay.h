#pragma once

// ei_gpu_spike (tools/gpu_spike/README.md): a frame dump's world passes drawn again through sokol_gfx or
// SDL_GPU, timed. replay.cpp plans the draws (the frame target's passes, the programs both paths have) and measures;
// replay_sokol.cpp and replay_sdlgpu.cpp draw them.

#include "dump.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gpu_spike {

// The programs drawn: those with an HLSL source in tools/gpu_spike/shaders.
enum class Program { Terrain, Mesh, Solid, ShadowCaster, ShadowTerrain, ShadowReduce, ShadowDilate, Other };
constexpr int kPrograms = static_cast<int>(Program::Other);
extern const char* const kProgramNames[kPrograms];

// One draw with everything it needs: the state at the draw as the recording left it.
struct PlannedDraw {
    const Pipeline* pipeline = nullptr;
    Program program = Program::Other;
    sg_bindings bindings{};
    const std::vector<std::uint8_t>* uniforms[SG_MAX_UNIFORMBLOCK_BINDSLOTS] = {};
    int viewport[4] = {};
    int scissor[4] = {};
    bool scissored = false;
    int base = 0, count = 0, instances = 1, baseVertex = 0, baseInstance = 0;
};
// A pass of the frame drawn again: its attachments (made as attachments, not from the dump) and its draws.
struct PlannedPass {
    sg_pass_action action{};
    const Image* colour = nullptr;
    const Image* depth = nullptr;
    int width = 0, height = 0;   // drawn at: the frame target's passes scaled (Plan::width), others as recorded
    bool frameTarget = false;
    std::vector<PlannedDraw> draws;
};

struct Plan {
    const Frame* frame = nullptr;
    const Image* colour = nullptr;   // the frame target (the dump's)
    const Image* depth = nullptr;
    int width = 0, height = 0;       // drawn at (viewports and scissors scaled from the dump's size)
    std::vector<PlannedPass> passes;
    std::map<std::uint32_t, const Image*> images;
    std::map<std::uint32_t, const Buffer*> buffers;
    std::map<std::uint32_t, const Sampler*> samplers;
    std::map<std::uint32_t, const View*> views;
    std::map<std::uint32_t, Program> programs;   // by shader id
    std::map<std::uint32_t, bool> attachments;   // image ids the passes draw into
    int drawCount = 0, skipped = 0;
};

struct Options {
    std::string api = "sokol";   // sokol, sdlgpu
    std::string driver;          // SDL_GPU's: direct3d12, vulkan (empty: SDL's pick)
    int width = 0, height = 0;   // the target (0: the dump's)
    int frames = 300;            // timed
    bool vsync = false;
    bool debug = false;          // SDL_GPU's debug mode
    std::string out;             // the frame as .bmp
};

using Clock = std::chrono::steady_clock;
inline double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Timings {
    std::string device;   // the backend's name for it
    double window = 0, deviceMs = 0, resourcesMs = 0, pipelinesMs = 0, firstFrameMs = 0, startupMs = 0;
    std::vector<double> frameMs;   // each timed frame, present to present
    std::vector<double> cpuMs;     // its recording and submission on the CPU
    double readbackMs = 0;         // the drawn frame to the CPU, after the GPU was idle
    double readbackFrameMs = 0;    // a frame drawn and read back at once (its drawing included)
    std::vector<std::uint8_t> rgba;   // the frame, RGBA8 rows from the top
};

// Each draws the plan into a window `frames` times and fills `t`. False with `error` when it cannot.
bool RunSokol(const Plan& plan, const Options& o, SDL_Window* window, Timings* t, std::string* error);
bool RunSdlGpu(const Plan& plan, const Options& o, SDL_Window* window, Timings* t, std::string* error);

// The rectangle of a recorded viewport or scissor in `pass` (scaled in the frame target's).
void ScaleRect(const Plan& plan, const PlannedPass& pass, const int in[4], int out[4]);

} // namespace gpu_spike
