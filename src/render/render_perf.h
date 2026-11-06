#pragma once

// The performance overlay: drawn into the window's back buffer after the frame, never into the frame target.
// See docs/notes/renderer.md, section 15 and "The renderer's source files".

#include "lib/rect.h"
#include "render/gpu.h"

#include <cstdint>
#include <string>
#include <vector>

namespace render::perf {

bool On();
// The switch itself, ungated: tools/perf_check's (the game goes through render::SetPerfOverlay, which keeps it
// off in the harness build).
void SetOn(bool on);

// ---- The frame's draws (render.cpp, render_shadows.cpp, inside the frame's draws) -------------------------

// What the frame's draws go to: the sun's shadow map with its range passes, the point lights' shadow faces, the
// ground and water pages, the models that cast shadows (objects and units), the rest of the world view (the other
// models, effects, decals, pre-transformed draws), the interface (with its pictures' frame copies at the frame's
// start), and the present's pass (the frame onto the window, this overlay).
enum class Pass : int { Sun, Point, Terrain, Objects, Other, Ui, Present, Count };
constexpr int kPasses = static_cast<int>(Pass::Count);
// The frame's first draws (once a frame): its draws counted from here, in Ui. Nothing when off.
void BeginDraws();
// The frame's draws from here on are `pass`'s.
void Enter(Pass pass);
// After the present's draws.
void EndDraws();
// The draws made so far in the frame, every pass's (render.cpp, from FrameStats; tools/perf_check has its own): the
// batches drawn, and the API calls that drew them (a run of meshes is one call), the shadow maps' included.
std::uint64_t FrameDrawCount();
std::uint64_t FrameCallCount();

// The renderer's CPU time: the end of the frame's draws up to the present (Submit) and the shadow maps (Shadow: their
// fitting and passes in Submit, and the world draw's walk of the casters outside the view before it).
enum class Cpu : int { Submit, Shadow, Count };
// `ms` of `part`'s time in this frame (a render::Timer's). Nothing when off.
void AddCpu(Cpu part, double ms);

// The frame presented (render::Present): its interval since the last one, its CPU times and draws, and the GPU time
// of the latest frame the GPU finished (gpu::FrameGpuTime, timed while the overlay is on).
void FrameDone();

// ---- The overlay (render_window.cpp, in the present's pass on the swap chain's texture) --------------------

// Before the present's pass: the text made again when due (or for another size of frame: `frameHeight`, the game
// image's height in the window) and its image written.
void Prepare(int frameHeight);
// Inside the pass: the overlay over `placed` (the frame's rectangle in a target of `format`, `width` x `height`).
void Draw(gpu::Format format, int width, int height, const Rect& placed);
// The renderer stops: the text's image released and the frame timing off (before gpu::Stop).
void Release();

// ---- What the overlay shows (also for tools/perf_check) ----------------------------------------------------

struct Numbers {
    double fps = 0.0;                      // frames presented in the last second
    double frameMs = 0.0, worstFrameMs = 0.0;
    bool gpu = false;                      // GPU times known (frames finished in the last second)
    double gpuMs = 0.0, worstGpuMs = 0.0;  // the whole frame's (its submission's work)
    double submitMs = 0.0, shadowCpuMs = 0.0;
    double calls[kPasses] = {};            // each pass's API calls
    double draws[kPasses] = {};            // and the batches they drew
};
// The numbers over the last second as of now.
Numbers Current();
// The overlay's lines.
std::vector<std::string> Lines(const Numbers& n);

// The text: Liberation Mono (render_text.h) at FontPixels(frame height) pixels, one per kFrameRowsPerFontPixel rows of
// the game image (the interface's text grows with the window the same way), at least kLeastFontPixels.
constexpr int kLeastFontPixels = 10, kFrameRowsPerFontPixel = 60;
constexpr const char* kFace = "Courier New";
int FontPixels(int frameHeight);
// The box's border around the text and its offset from the frame's top left corner, in screen pixels.
inline int Border(int fontPixels) { return fontPixels / 3; }
inline int Margin(int fontPixels) { return fontPixels / 2; }
// The lines laid out and drawn into a one-channel image: 0 the backing, else the text's coverage.
struct Box {
    int width = 0, height = 0;
    std::vector<std::uint8_t> coverage;   // rows top down
};
Box MakeBox(const std::vector<std::string>& lines, int fontPixels);

} // namespace render::perf
