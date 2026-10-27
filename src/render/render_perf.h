#pragma once

// The performance overlay (docs/notes/renderer.md, section 15): frames a second, the frame time (average and
// worst over the last second), the GPU time of the frame's passes (timestamp queries, read a few frames late
// without waiting), the renderer's CPU time and the draws, as a few lines of text in the frame's top left corner.
// Drawn by the present into the game window's back buffer after the frame and the cursor (render_window.cpp,
// DrawToWindow), never into the frame target: captures, screenshots, the escape menu's and the save game's
// pictures and every frame check read the frame target, so none of them holds it. Off unless the settings file
// switches it on (render::SetPerfOverlay); the harness build never draws it.
// Its own text: a 5 x 7 pixel font of capitals, digits and a few signs, drawn into a one-channel image (0 the
// box, else the text's brightness) and scaled up by whole pixels with the game's height (Scale).

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

// ---- The frame's GPU time (render.cpp, inside the frame's draws) ------------------------------------------

// The points of a frame its timestamps are taken at, in the frame's order.
enum class Mark : int { FrameBegin, ShadowBegin, ShadowEnd, WorldEnd, FrameEnd, PresentEnd, Count };
// The frame's first draws: a set of queries begun (once a frame; the results of earlier frames that are ready
// are collected first). Nothing when off, or when every set is still waiting for the GPU.
void BeginGpuFrame();
void Stamp(Mark mark);
// Each command drawn: whether it is a world view's (the world's GPU time ends at the first command after it).
void WorldCommand(bool world);
// Before the frame's commit: the set closed (its results read at a later frame).
void EndGpuFrame();

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
    Cpu part_;
    long long start_ = 0;
};

// The frame presented (render::Present): its interval since the last one and its counts.
void FrameDone(std::uint64_t draws, std::uint64_t shadowDraws);

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
    double shadowMs = 0.0, worldMs = 0.0, uiMs = 0.0, presentMs = 0.0;
    double submitMs = 0.0, shadowCpuMs = 0.0;
    double draws = 0.0, shadowDraws = 0.0;
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
