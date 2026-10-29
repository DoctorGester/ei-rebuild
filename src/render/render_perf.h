#pragma once

// The performance overlay: drawn into the window's back buffer after the frame, never into the frame target.
// See docs/notes/renderer.md, section 15 and "The renderer's source files".

#include <cstdint>
#include <string>
#include <vector>

namespace render {
struct Rect;
}

namespace render::perf {

bool On();
// The switch itself, ungated: tools/perf_check's (the game goes through render::SetPerfOverlay, which keeps it
// off in the harness build).
void SetOn(bool on);

// ---- The frame's GPU time and draws (render.cpp, render_shadows.cpp, inside the frame's draws) ------------

// What the frame's work goes to, each from its first draw up to the next one's (a timestamp at each change): the
// sun's shadow map with its range passes, the point lights' shadow faces, the ground and water pages, the models
// that cast shadows (objects and units), the rest of the world view (the other models, effects, decals,
// pre-transformed draws, the uploads, the frame passes' clears, copies and resolve), the interface (with the
// canvases' queued drawing at the frame's start), and the present's pass (the frame onto the window, the cursor,
// this overlay).
enum class Pass : int { Sun, Point, Terrain, Objects, Other, Ui, Present, Count };
constexpr int kPasses = static_cast<int>(Pass::Count);
// The frame's first draws (once a frame): its work counted from here, in Ui; a set of queries begun (the results
// of earlier frames that are ready are collected first) unless every set still waits for the GPU. Nothing when off.
void BeginGpuFrame();
// The frame's work from here on is `pass`'s.
void Enter(Pass pass);
// After the present's draws: the frame's last timestamp, the set closed (its results read at a later frame).
void EndGpuFrame();
// The draws made so far in the frame, every pass's (render.cpp, from FrameStats; tools/perf_check has its own).
std::uint64_t FrameDrawCount();

// The renderer's CPU time: the end of the frame's draws up to the present (Submit) and the shadow map (Shadow).
enum class Cpu : int { Submit, Shadow, Count };
class CpuTimer {
public:
    explicit CpuTimer(Cpu part);
    ~CpuTimer();
    void Stop();
    CpuTimer(const CpuTimer&) = delete;
    CpuTimer& operator=(const CpuTimer&) = delete;

private:
    Cpu part;
    long long start = 0;
};

// The frame presented (render::Present): its interval since the last one, its CPU times and draws.
void FrameDone();

// ---- The overlay (render_window.cpp, in the present's pass on the back buffer) -----------------------------

// Draws the overlay over `placed` (the frame's rectangle in the back buffer of `width` x `height`).
void Draw(int width, int height, const Rect& placed);
// The device is lost or the renderer stops: the queries and sokol objects released (before sg_shutdown).
void Release();

// ---- What the overlay shows (also for tools/perf_check) ----------------------------------------------------

struct Numbers {
    double fps = 0.0;                      // frames presented in the last second
    double frameMs = 0.0, worstFrameMs = 0.0;
    bool gpu = false;                      // GPU times known (timestamps of the last second)
    double gpuMs = 0.0, worstGpuMs = 0.0;  // the frame's draws and the present
    double passMs[kPasses] = {};           // of it, each pass's (by Pass)
    double submitMs = 0.0, shadowCpuMs = 0.0;
    double draws[kPasses] = {};            // each pass's draws
};
// The numbers over the last second as of now.
Numbers Current();
// The overlay's lines.
std::vector<std::string> Lines(const Numbers& n);

// The font's cell: 5 x 7 glyphs, a column and two rows apart; the box's border around the text.
constexpr int kGlyphWidth = 5, kGlyphHeight = 7, kCellWidth = 6, kCellHeight = 9, kBorder = 3;
// Brightness in the image: the box 0, letters and signs kLabel, digits and the point kValue.
constexpr std::uint8_t kLabel = 176, kValue = 255;
// `lines` drawn into a one-channel image (`width` x `height`, rows top down).
void Rasterise(const std::vector<std::string>& lines, std::vector<std::uint8_t>* pixels, int* width, int* height);
// Whether the font has `c` (lower case drawn as capitals); anything else is a blank.
bool HasGlyph(char c);
// The image's screen pixels per pixel for a frame `height` pixels high: one per 540 rows, at least one.
int Scale(int height);
// The box's offset from the frame's top left corner, in screen pixels.
inline int Margin(int scale) { return 4 * scale; }

} // namespace render::perf
