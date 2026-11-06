// The performance overlay (render_perf.h, docs/notes/renderer.md section 15): what a frame costs, measured
// while the game runs and drawn over the presented frame. Off unless switched on (render::SetPerfOverlay); when
// off every entry point returns at once.
#include "render_perf.h"

#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"
#include "render_text.h"

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

// The frames: their interval since the last present, the CPU times, each pass's draws (kFrameDraws + pass) and API
// calls (kFrameCalls + pass).
constexpr int kInterval = 0;
constexpr int kSubmitMs = 1;
constexpr int kShadowCpuMs = 2;
constexpr int kFrameDraws = 3;
constexpr int kFrameCalls = kFrameDraws + kPasses;
constexpr int kFrameValues = kFrameCalls + kPasses;
static std::deque<Sample<kFrameValues>> g_frames;
static long long g_lastPresent = 0;
static double g_cpu[static_cast<int>(Cpu::Count)] = {};   // this frame's
static double g_draws[kPasses] = {};                      // this frame's
static double g_calls[kPasses] = {};
static std::uint64_t g_drawsCounted = 0;                  // FrameDrawCount when last given to a pass
static std::uint64_t g_callsCounted = 0;                  // FrameCallCount

// The GPU: the whole frame's time (gpu::FrameGpuTime), one per frame the GPU finished.
static std::deque<Sample<1>> g_gpu;
static bool g_timing = false;   // gpu::SetFrameTiming on

// The frame's pass (between BeginDraws and EndDraws).
static bool g_inFrame = false;
static Pass g_pass = Pass::Ui;

// The draws and calls since the last change, to the pass they were made in.
static void CountDraws() {
    const std::uint64_t draws = FrameDrawCount(), calls = FrameCallCount();
    if (draws >= g_drawsCounted) g_draws[static_cast<int>(g_pass)] += static_cast<double>(draws - g_drawsCounted);
    if (calls >= g_callsCounted) g_calls[static_cast<int>(g_pass)] += static_cast<double>(calls - g_callsCounted);
    g_drawsCounted = draws;
    g_callsCounted = calls;
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

// ---- Drawing --------------------------------------------------------------------------------------------------

constexpr double kRefreshMs = 250.0;   // the text is made again this often

static gpu::Texture g_image;
static bool g_imageReady = false;            // Prepare wrote it for this frame's Draw
static Box g_box;                            // the text's image
static bool g_boxChanged = false;
static long long g_lastText = 0;
static int g_fontPixels = 0;
static std::vector<std::string> g_lines;

int FontPixels(int frameHeight) { return std::max(kLeastFontPixels, frameHeight / kFrameRowsPerFontPixel); }

Box MakeBox(const std::vector<std::string>& lines, int fontPixels) {
    text::TextFont* font = text::GetTextFont(kFace, fontPixels);
    if (!font || lines.empty()) return {};
    std::u32string all;
    int widest = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::u32string line = text::DecodeCp1251(lines[i].c_str());
        widest = std::max(widest, text::TextExtent(*font, line).w);
        if (i) all += U'\n';
        all += line;
    }
    const int border = Border(fontPixels), rows = static_cast<int>(lines.size());
    Box box;
    box.width = widest + 2 * border;
    box.height = rows * font->Metrics().height + 2 * border;
    const Rect inside{border, border, border + widest, box.height - border};
    text::TextLayout layout;
    text::Layout(*font, all, inside, text::Format::NoPrefix | text::Format::NoClip, layout);
    // White on black: the blue channel is the coverage.
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(box.width) * static_cast<std::size_t>(box.height), 0);
    text::ComposeAgainstBlack(*font, layout, 0x00FFFFFF, pixels.data(), box.width, box.height, box.width);
    box.coverage.resize(pixels.size());
    for (std::size_t i = 0; i < pixels.size(); ++i) box.coverage[i] = static_cast<std::uint8_t>(pixels[i] & 0xFF);
    return box;
}

// The box's image on the GPU, made again when its size changed.
static bool UploadBox() {
    if (g_box.coverage.empty()) return false;
    if (g_image.width != g_box.width || g_image.height != g_box.height) {
        gpu::Release(g_image);
        g_image = gpu::MakeTexture({g_box.width, g_box.height, 1, 1, gpu::Format::R8, gpu::TextureUse::Sampled, "ei-perf-overlay"});
        if (!g_image) return false;
        g_boxChanged = true;
    }
    if (g_boxChanged) {
        if (!gpu::Write(g_image, 0, {0, 0, g_box.width, g_box.height}, g_box.coverage.data(),
                        static_cast<std::size_t>(g_box.width), gpu::Contents::Discard))
            return false;
        g_boxChanged = false;
    }
    return true;
}

// The text made again from the last second's numbers (every kRefreshMs: readable, and cheap) or at once for another
// size.
static void RefreshText(int fontPixels) {
    long long now = Ticks();
    const bool sameSize = fontPixels == g_fontPixels;
    if (sameSize && !g_box.coverage.empty() && static_cast<double>(now - g_lastText) * kMsPerTick < kRefreshMs) return;
    g_lastText = now;
    std::vector<std::string> lines = Lines(Current());
    if (sameSize && lines == g_lines && !g_box.coverage.empty()) return;
    g_lines = std::move(lines);
    g_fontPixels = fontPixels;
    g_box = MakeBox(g_lines, fontPixels);
    g_boxChanged = true;
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
    g_callsCounted = FrameCallCount();
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
        Sample<kFrameValues> s;
        s.when = now;
        s.values[kInterval] = static_cast<double>(now - g_lastPresent) * kMsPerTick;
        s.values[kSubmitMs] = g_cpu[static_cast<int>(Cpu::Submit)];
        s.values[kShadowCpuMs] = g_cpu[static_cast<int>(Cpu::Shadow)];
        for (int p = 0; p < kPasses; ++p) {
            s.values[kFrameDraws + p] = g_draws[p];
            s.values[kFrameCalls + p] = g_calls[p];
        }
        Keep(g_frames, s);
    }
    g_lastPresent = now;
    for (double& c : g_cpu) c = 0.0;
    for (double& d : g_draws) d = 0.0;
    for (double& c : g_calls) c = 0.0;
}

void Prepare(int frameHeight) {
    g_imageReady = false;
    if (!g_on) return;
    RefreshText(FontPixels(frameHeight));
    g_imageReady = UploadBox();
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
    const int margin = Margin(FontPixels(placed.Height()));
    float left = static_cast<float>(placed.left + margin), top = static_cast<float>(placed.top + margin);
    float right = left + static_cast<float>(g_image.width), bottom = top + static_cast<float>(g_image.height);
    gpu::SetViewport({0, 0, width, height});
    gpu::ApplyPipeline(pipeline);
    const gpu::TextureBinding text{g_image, gpu::NearestClampSampler()};
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerTex0, {&text, 1});
    const RectParams vs{{left, top, right, bottom}, {static_cast<float>(width), static_cast<float>(height)}};
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockRectParams, vs);
    const PerfOverlayParams fs{{left, top}};
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockPerfOverlayParams, fs);
    gpu::Draw(0, 6);
}

void Release() {
    g_inFrame = false;
    if (g_timing && gpu::Started()) gpu::SetFrameTiming(false);
    g_timing = false;
    gpu::Release(g_image);
    g_imageReady = false;
    g_boxChanged = true;
}

Numbers Current() {
    Numbers n;
    if (!g_frames.empty()) {
        double sum[kFrameValues] = {};
        for (const auto& s : g_frames) {
            for (int k = 0; k < kFrameValues; ++k) sum[k] += s.values[k];
            n.worstFrameMs = std::max(n.worstFrameMs, s.values[kInterval]);
        }
        float count = static_cast<float>(static_cast<int>(g_frames.size()));
        n.frameMs = sum[kInterval] / count;
        n.fps = n.frameMs > 0.0 ? 1000.0 / n.frameMs : 0.0;
        n.submitMs = sum[kSubmitMs] / count;
        n.shadowCpuMs = sum[kShadowCpuMs] / count;
        for (int p = 0; p < kPasses; ++p) {
            n.draws[p] = sum[kFrameDraws + p] / count;
            n.calls[p] = sum[kFrameCalls + p] / count;
        }
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
    char line[192];
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
    auto calls = [&](Pass p) { return n.calls[static_cast<int>(p)]; };
    const double world = calls(Pass::Terrain) + calls(Pass::Objects) + calls(Pass::Other);
    const double worldDraws = draws(Pass::Terrain) + draws(Pass::Objects) + draws(Pass::Other);
    const double ui = calls(Pass::Ui) + calls(Pass::Present), uiDraws = draws(Pass::Ui) + draws(Pass::Present);
    std::snprintf(line, sizeof(line), "CALLS (DRAWS) WORLD %.0f (%.0f)   SUN %.0f (%.0f)   POINT %.0f (%.0f)   UI %.0f (%.0f)", world,
                  worldDraws, calls(Pass::Sun), draws(Pass::Sun), calls(Pass::Point), draws(Pass::Point), ui, uiDraws);
    lines.emplace_back(line);
    return lines;
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
