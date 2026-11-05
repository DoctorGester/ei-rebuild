// The performance overlay (render_perf.h, docs/notes/renderer.md section 15): what a frame costs, measured
// while the game runs and drawn over the presented frame. Off unless switched on (render::SetPerfOverlay); when
// off every entry point returns at once.
#include "render_perf.h"

#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>

namespace render {

static_assert(std::chrono::steady_clock::period::num == 1 && std::chrono::steady_clock::period::den == 1'000'000'000,
              "kMsPerTick is for a nanosecond clock");
long long Ticks() { return std::chrono::steady_clock::now().time_since_epoch().count(); }

namespace perf {

static bool g_on = false;

// One second of samples: each with the time it was taken (Ticks).
template <int N>
struct Sample {
    long long when = 0;
    double values[N] = {};
};

template <int N>
static void Keep(std::deque<Sample<N>>& samples, const Sample<N>& s) {
    samples.push_back(s);
    constexpr long long second = static_cast<long long>(1000.0 / kMsPerTick);
    while (!samples.empty() && s.when - samples.front().when > second) samples.pop_front();
}

// The frames: their interval since the last present, the CPU times, each pass's draws (FrameDraws + pass).
enum FrameValue { Interval, SubmitMs, ShadowCpuMs, FrameDraws, FrameValues = FrameDraws + kPasses };
static std::deque<Sample<FrameValues>> g_frames;
static long long g_lastPresent = 0;
static double g_cpu[static_cast<int>(Cpu::Count)] = {};   // this frame's
static double g_draws[kPasses] = {};                      // this frame's
static std::uint64_t g_drawsCounted = 0;                  // FrameDrawCount when last given to a pass

// The GPU: the whole frame's time (gpu::FrameGpuTime), one per frame the GPU finished.
static std::deque<Sample<1>> g_gpu;
static bool g_timing = false;   // gpu::SetFrameTiming on

// The frame's pass (between BeginDraws and EndDraws).
static bool g_inFrame = false;
static Pass g_pass = Pass::Ui;

// The draws since the last change, to the pass they were made in.
static void CountDraws() {
    const std::uint64_t now = FrameDrawCount();
    if (now >= g_drawsCounted) g_draws[static_cast<int>(g_pass)] += static_cast<double>(now - g_drawsCounted);
    g_drawsCounted = now;
}

// The latest frame the GPU finished, into the samples (never waiting); the frame timing on while the overlay is.
static void Collect() {
    if (g_timing != g_on && gpu::Started()) {
        gpu::SetFrameTiming(g_on);
        g_timing = g_on;
    }
    double ms = 0.0;
    if (!g_timing || !gpu::FrameGpuTime(&ms)) return;
    Sample<1> s;
    s.when = Ticks();
    s.values[0] = ms;
    Keep(g_gpu, s);
}

// ---- The font ------------------------------------------------------------------------------------------------

struct Glyph {
    char c = 0;
    const char* rows[kGlyphHeight] = {};
};
// clang-format off
static const Glyph kFont[] = {
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
};
// clang-format on

static const Glyph* FindGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const Glyph& g : kFont)
        if (g.c == c) return &g;
    return nullptr;
}

// ---- Drawing --------------------------------------------------------------------------------------------------

constexpr double kRefreshMs = 250.0;   // the text is made again this often

static gpu::Texture g_image;
static bool g_imageReady = false;            // Prepare wrote it for this frame's Draw
static TextImage g_text;   // the text's image
static bool g_textChanged = false;
static long long g_lastText = 0;
static std::vector<std::string> g_lines;

// The text's image on the GPU, made again when its size changed.
static bool UploadText() {
    if (g_text.pixels.empty()) return false;
    if (g_image.width != g_text.width || g_image.height != g_text.height) {
        gpu::Release(g_image);
        g_image =
            gpu::MakeTexture({g_text.width, g_text.height, 1, 1, gpu::Format::R8, gpu::kSampled, "ei-perf-overlay"});
        if (!g_image) return false;
        g_textChanged = true;
    }
    if (g_textChanged) {
        if (!gpu::Write(g_image, 0, {0, 0, g_text.width, g_text.height}, g_text.pixels.data(),
                        static_cast<std::size_t>(g_text.width), gpu::Contents::Discard))
            return false;
        g_textChanged = false;
    }
    return true;
}

// The text made again from the last second's numbers (every kRefreshMs: readable, and cheap).
static void RefreshText() {
    long long now = Ticks();
    if (!g_text.pixels.empty() && static_cast<double>(now - g_lastText) * kMsPerTick < kRefreshMs) return;
    g_lastText = now;
    std::vector<std::string> lines = Lines(Current());
    if (lines == g_lines && !g_text.pixels.empty()) return;
    g_lines = std::move(lines);
    g_text = Rasterise(g_lines);
    g_textChanged = true;
}

bool On() { return g_on; }

void SetOn(bool on) {
    if (on != g_on) ei::Log("render: the performance overlay %s", on ? "on" : "off");
    g_on = on;
}

void BeginDraws() {
    if (!g_on || g_inFrame) return;
    g_inFrame = true;
    g_pass = Pass::Ui;
    g_drawsCounted = FrameDrawCount();
}

void Enter(Pass pass) {
    if (!g_inFrame || pass == g_pass) return;
    CountDraws();
    g_pass = pass;
}

void EndDraws() {
    if (!g_inFrame) return;
    CountDraws();
    g_inFrame = false;
}

void AddCpu(Cpu part, double ms) {
    if (g_on) g_cpu[static_cast<int>(part)] += ms;
}

void FrameDone() {
    if (!g_on && !g_timing) return;
    Collect();
    if (!g_on) return;
    long long now = Ticks();
    if (g_lastPresent) {
        Sample<FrameValues> s;
        s.when = now;
        s.values[Interval] = static_cast<double>(now - g_lastPresent) * kMsPerTick;
        s.values[SubmitMs] = g_cpu[static_cast<int>(Cpu::Submit)];
        s.values[ShadowCpuMs] = g_cpu[static_cast<int>(Cpu::Shadow)];
        for (int p = 0; p < kPasses; ++p) s.values[FrameDraws + p] = g_draws[p];
        Keep(g_frames, s);
    }
    g_lastPresent = now;
    for (double& c : g_cpu) c = 0.0;
    for (double& d : g_draws) d = 0.0;
}

void Prepare() {
    g_imageReady = false;
    if (!g_on) return;
    RefreshText();
    g_imageReady = UploadText();
}

void Draw(gpu::Format format, int width, int height, const Rect& placed) {
    if (!g_on || !g_imageReady || width <= 0 || height <= 0) return;
    namespace program = gpu_shaders::perf_overlay;
    gpu::PipelineDesc d;
    d.program = program::kIndex;
    d.colour = format;
    d.blend = {true, gpu::BlendFactor::SourceAlpha, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendFactor::One,
               gpu::BlendFactor::OneMinusSourceAlpha};
    const gpu::Pipeline pipeline = gpu::MakePipeline(d, "ei-perf-overlay");
    if (!pipeline) return;
    int scale = Scale(placed.Height());
    float left = static_cast<float>(placed.left + Margin(scale)), top = static_cast<float>(placed.top + Margin(scale));
    float right = left + static_cast<float>(g_image.width * scale), bottom = top + static_cast<float>(g_image.height * scale);
    gpu::SetViewport({0, 0, width, height});
    gpu::ApplyPipeline(pipeline);
    const gpu::TextureBinding text{g_image, gpu::NearestClampSampler()};
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerTex0, {&text, 1});
    const RectParams vs{{left, top, right, bottom}, {static_cast<float>(width), static_cast<float>(height)}};
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockRectParams, vs);
    const PerfOverlayParams fs{{left, top}, static_cast<float>(scale)};
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockPerfOverlayParams, fs);
    gpu::Draw(0, 6);
}

void Release() {
    g_inFrame = false;
    if (g_timing && gpu::Started()) gpu::SetFrameTiming(false);
    g_timing = false;
    gpu::Release(g_image);
    g_imageReady = false;
    g_textChanged = true;
}

Numbers Current() {
    Numbers n;
    if (!g_frames.empty()) {
        double sum[FrameValues] = {};
        for (const auto& s : g_frames) {
            for (int k = 0; k < FrameValues; ++k) sum[k] += s.values[k];
            n.worstFrameMs = std::max(n.worstFrameMs, s.values[Interval]);
        }
        float count = static_cast<float>(static_cast<int>(g_frames.size()));
        n.frameMs = sum[Interval] / count;
        n.fps = n.frameMs > 0.0 ? 1000.0 / n.frameMs : 0.0;
        n.submitMs = sum[SubmitMs] / count;
        n.shadowCpuMs = sum[ShadowCpuMs] / count;
        for (int p = 0; p < kPasses; ++p) n.draws[p] = sum[FrameDraws + p] / count;
    }
    if (!g_gpu.empty()) {
        double sum = 0.0;
        for (const auto& s : g_gpu) {
            sum += s.values[0];
            n.worstGpuMs = std::max(n.worstGpuMs, s.values[0]);
        }
        n.gpu = true;
        n.gpuMs = sum / static_cast<double>(static_cast<int>(g_gpu.size()));
    }
    return n;
}

std::vector<std::string> Lines(const Numbers& n) {
    auto draws = [&](Pass p) { return n.draws[static_cast<int>(p)]; };
    char line[128];
    std::vector<std::string> lines;
    std::snprintf(line, sizeof(line), "FPS %.0f   FRAME %.2f MS   WORST %.2f", n.fps, n.frameMs, n.worstFrameMs);
    lines.emplace_back(line);
    if (n.gpu) {
        std::snprintf(line, sizeof(line), "GPU %.2f MS   WORST %.2f", n.gpuMs, n.worstGpuMs);
        lines.emplace_back(line);
    } else {
        lines.emplace_back("GPU -");
    }
    std::snprintf(line, sizeof(line), "CPU SUBMIT %.2f MS   SHADOW %.2f", n.submitMs, n.shadowCpuMs);
    lines.emplace_back(line);
    std::snprintf(line, sizeof(line), "DRAWS WORLD %.0f   SUN %.0f   POINT %.0f   UI %.0f",
                  draws(Pass::Terrain) + draws(Pass::Objects) + draws(Pass::Other), draws(Pass::Sun), draws(Pass::Point),
                  draws(Pass::Ui) + draws(Pass::Present));
    lines.emplace_back(line);
    return lines;
}

TextImage Rasterise(const std::vector<std::string>& lines) {
    std::size_t columns = 1;
    for (const std::string& l : lines) columns = std::max(columns, l.size());
    int rows = static_cast<int>(lines.size());
    int w = 2 * kBorder + static_cast<int>(columns) * kCellWidth - (kCellWidth - kGlyphWidth);
    int h = 2 * kBorder + (rows > 0 ? rows * kCellHeight - (kCellHeight - kGlyphHeight) : 0);
    TextImage image{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0)};
    for (int r = 0; r < rows; ++r) {
        const std::string& l = lines[static_cast<std::size_t>(r)];
        for (std::size_t c = 0; c < l.size(); ++c) {
            const Glyph* g = FindGlyph(l[c]);
            if (!g) continue;
            std::uint8_t value = (g->c >= '0' && g->c <= '9') || g->c == '.' ? kValue : kLabel;
            int x0 = kBorder + static_cast<int>(c) * kCellWidth, y0 = kBorder + r * kCellHeight;
            for (int y = 0; y < kGlyphHeight; ++y)
                for (int x = 0; x < kGlyphWidth; ++x)
                    if (g->rows[y][x] == '#')
                        image.pixels[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(w) +
                                     static_cast<std::size_t>(x0 + x)] = value;
        }
    }
    return image;
}

bool HasGlyph(char c) { return FindGlyph(c) != nullptr; }

int Scale(int height) {
    int scale = (height + 270) / 540;
    return scale < 1 ? 1 : scale;
}

} // namespace perf

bool PerfOverlaySupported() {
#if defined(EI_RENDER_VALIDATION)
    return false;   // the harness build: no run, capture or frame check may hold it
#else
    return true;
#endif
}

void SetPerfOverlay(bool on) { perf::SetOn(on && PerfOverlaySupported()); }

bool PerfOverlay() { return perf::On(); }

} // namespace render
