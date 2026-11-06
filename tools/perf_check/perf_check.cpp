// Offline check of the performance overlay (src/render/render_perf, ticket R21; docs/notes/renderer.md section 15)
// on the renderer's GPU (render/gpu.h: Direct3D 12), without the game:
//   text    the lines of known numbers, the text's size by the frame's height and the box's image (render_text.h's font).
//   draw    the overlay drawn over a cleared target (as the present draws it: BGRA8, no depth) at 720, 1080 and 2160
//           rows: every pixel against the box's image (text: its coverage over the clear colour at 40%; the backing:
//           the clear colour at 40%; outside the box: untouched).
//   time    frames with the passes the renderer marks: each pass's calls and draws, the CPU times and the whole
//           frame's GPU time (gpu::FrameGpuTime) come back.
//   dump    with --dump DIR: after the time check's frames, the overlay over a cleared target at 1080 and 2160 rows
//           (shown with real numbers) written as DIR/overlay_<width>x<height>.ppm, for before/after pictures.
// Any ei::Log line but the switch's and the device's own fails the check. Exit code 0 when all pass.
#include "render/gpu.h"
#include "render/render_internal.h"
#include "render/render_perf.h"
#include "render/render_text.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace gpu = render::gpu;
namespace perf = render::perf;

namespace {

int g_checks = 0, g_failures = 0, g_problems = 0;
std::uint64_t g_frameDraws = 0, g_frameCalls = 0;   // FrameDrawCount's and FrameCallCount's (the renderer's FrameStats in the game)

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAILED: %s\n", what.c_str());
    }
}

} // namespace

namespace ei {
void Log(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    if (std::strncmp(buffer, "render: the performance overlay o", 33) != 0) {   // the switch's own lines
        ++g_problems;
        std::printf("  log: %s\n", buffer);
    }
}
} // namespace ei

std::uint64_t render::perf::FrameDrawCount() { return g_frameDraws; }
std::uint64_t render::perf::FrameCallCount() { return g_frameCalls; }

namespace {

void CheckText() {
    using perf::Pass;
    auto slot = [](Pass p) { return static_cast<int>(p); };
    perf::Numbers n;
    n.fps = 143.6;
    n.frameMs = 6.964;
    n.worstFrameMs = 9.126;
    n.gpu = true;
    n.gpuMs = 2.1;
    n.worstGpuMs = 2.6;
    n.submitMs = 1.1;
    n.shadowCpuMs = 0.12;
    n.calls[slot(Pass::Sun)] = 12;
    n.calls[slot(Pass::Point)] = 8;
    n.calls[slot(Pass::Terrain)] = 3;
    n.calls[slot(Pass::Objects)] = 30;
    n.calls[slot(Pass::Other)] = 4;
    n.calls[slot(Pass::Ui)] = 6;
    n.calls[slot(Pass::Present)] = 1;
    n.draws[slot(Pass::Sun)] = 120;
    n.draws[slot(Pass::Point)] = 80;
    n.draws[slot(Pass::Terrain)] = 30;
    n.draws[slot(Pass::Objects)] = 300;
    n.draws[slot(Pass::Other)] = 40;
    n.draws[slot(Pass::Ui)] = 24;
    n.draws[slot(Pass::Present)] = 1;
    const std::vector<std::string> expected = {"FPS 144   FRAME 6.96 MS   WORST 9.13", "GPU 2.10 MS   WORST 2.60",
                                               "CPU SUBMIT 1.10 MS   SHADOW 0.12",
                                               "CALLS (DRAWS) WORLD 37 (370)   SUN 12 (120)   POINT 8 (80)   UI 7 (25)"};
    std::vector<std::string> lines = perf::Lines(n);
    Check(lines == expected, "the lines of known numbers");
    for (const std::string& l : lines) std::printf("  %s\n", l.c_str());
    n.gpu = false;
    lines = perf::Lines(n);
    Check(lines.size() == 4 && lines[1] == "GPU -", "the lines without GPU times");
    // The text's size follows the frame: one pixel per 60 rows, at least 10.
    Check(perf::FontPixels(480) == 10 && perf::FontPixels(720) == 12 && perf::FontPixels(1080) == 18 &&
              perf::FontPixels(2160) == 36,
          "the font's size by height");
    // The box: a border around the lines, one font height each; text where the glyphs are, the backing 0.
    const int px = perf::FontPixels(1080);
    const perf::Box box = perf::MakeBox({"1", "AB"}, px);
    render::text::TextFont* font = render::text::GetTextFont(perf::kFace, px);
    const int border = perf::Border(px);
    Check(font != nullptr && box.width > 2 * border && box.height == 2 * border + 2 * font->Metrics().height &&
              box.coverage.size() == static_cast<std::size_t>(box.width) * static_cast<std::size_t>(box.height),
          "the box's size");
    int inkTop = 0, inkBottom = 0;
    for (int y = 0; y < box.height; ++y)
        for (int x = 0; x < box.width; ++x)
            if (box.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(box.width) + static_cast<std::size_t>(x)])
                (y < box.height / 2 ? inkTop : inkBottom)++;
    Check(inkTop > 0 && inkBottom > inkTop, "ink in both lines ('AB' has more than '1')");
    Check(box.coverage.front() == 0 && box.coverage.back() == 0, "the border");
    const perf::Box small = perf::MakeBox(lines, perf::FontPixels(1080)), large = perf::MakeBox(lines, perf::FontPixels(2160));
    Check(large.width >= 2 * small.width - small.width / 10 && large.height >= 2 * small.height - small.height / 10,
          "the box doubles with the frame's height");
    Check(perf::MakeBox({}, px).coverage.empty(), "no lines, no box");
}

// A BGRA8 colour target (the swap chain's format).
constexpr gpu::Format kTargetFormat = gpu::Format::Bgra8;

gpu::Texture MakeTarget(int width, int height) {
    return gpu::MakeTexture({width, height, 1, 1, kTargetFormat, gpu::TextureUse::ColourTarget, "perf-check-target"});
}

constexpr std::uint8_t kClear[3] = {160, 90, 40};   // B, G, R

bool BeginTargetPass(const gpu::Texture& t) {
    gpu::PassDesc pass;
    pass.colour.texture = t;
    pass.colour.load = gpu::Load::Clear;
    pass.colour.clear[0] = kClear[2] / 255.0f;
    pass.colour.clear[1] = kClear[1] / 255.0f;
    pass.colour.clear[2] = kClear[0] / 255.0f;
    pass.colour.clear[3] = 1.0f;
    return gpu::BeginPass(pass);
}

bool ReadTarget(const gpu::Texture& t, std::vector<std::uint8_t>* bgra) {
    gpu::Ticket ticket = gpu::Download(t, 0, Rect{0, 0, t.width, t.height});
    gpu::Texels texels;
    const bool read = ticket && gpu::Wait(ticket) && gpu::Read(ticket, &texels);
    if (read) {
        bgra->resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4);
        for (int y = 0; y < t.height; ++y)
            std::memcpy(bgra->data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) * 4,
                        texels.Row(y), static_cast<std::size_t>(t.width) * 4);
    }
    gpu::Release(ticket);
    return read;
}

void CheckDraw(int width, int height) {
    gpu::Texture t = MakeTarget(width, height);
    Rect placed{0, 0, width, height};
    perf::Prepare(height);
    if (BeginTargetPass(t)) {
        perf::Draw(kTargetFormat, width, height, placed);
        gpu::EndPass();
    }
    gpu::Submit();
    // What it should show: the current numbers' lines (none measured yet) in the font for this height, at the corner.
    const int fontPixels = perf::FontPixels(height);
    const perf::Box text = perf::MakeBox(perf::Lines(perf::Current()), fontPixels);
    const int tw = text.width, th = text.height;
    const int x0 = perf::Margin(fontPixels), y0 = perf::Margin(fontPixels);
    std::vector<std::uint8_t> bgra;
    bool read = ReadTarget(t, &bgra);
    Check(read, "the target read back");
    int wrong = 0, textPixels = 0;
    std::string example;
    for (int y = 0; read && y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = &bgra[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            int want[3] = {kClear[0], kClear[1], kClear[2]};
            const int tx = x - x0, ty = y - y0;
            bool inside = x >= x0 && y >= y0 && tx < tw && ty < th;
            if (inside) {
                // White of coverage v over the clear colour at 40%.
                const double v = text.coverage[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tw) +
                                               static_cast<std::size_t>(tx)] / 255.0;
                for (int c = 0; c < 3; ++c) want[c] = static_cast<int>(std::lround(255.0 * v + kClear[c] * (1.0 - v) * 0.4));
                if (v > 0.0) ++textPixels;
            }
            bool ok = true;
            for (int c = 0; c < 3; ++c) ok = ok && std::abs(static_cast<int>(p[c]) - want[c]) <= (inside ? 2 : 0);
            if (!ok && wrong++ == 0) {
                char buffer[160];
                std::snprintf(buffer, sizeof buffer, " (first at %d,%d: %u %u %u, expected %d %d %d)", x, y, p[0], p[1], p[2],
                              want[0], want[1], want[2]);
                example = buffer;
            }
        }
    Check(read && wrong == 0 && textPixels > 0,
          "the overlay at " + std::to_string(width) + "x" + std::to_string(height) + ": " + std::to_string(wrong) +
              " pixels wrong" + example);
    std::printf("  %dx%d: font %d px, box %dx%d, %d text pixels\n", width, height, fontPixels, tw, th, textPixels);
    gpu::Release(t);
}

// The overlay over a cleared target as the present draws it, written as a binary PPM in `dir`.
void Dump(const std::string& dir, int width, int height) {
    gpu::Texture t = MakeTarget(width, height);
    perf::Prepare(height);
    if (BeginTargetPass(t)) {
        perf::Draw(kTargetFormat, width, height, Rect{0, 0, width, height});
        gpu::EndPass();
    }
    gpu::Submit();
    std::vector<std::uint8_t> bgra;
    const bool read = ReadTarget(t, &bgra);
    Check(read, "the dumped target read back");
    const std::string path = dir + "/overlay_" + std::to_string(width) + "x" + std::to_string(height) + ".ppm";
    std::ofstream f(path, std::ios::binary);
    Check(read && f.good(), "the file " + path);
    if (read && f) {
        f << "P6\n" << width << ' ' << height << "\n255\n";
        std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 3);
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* p = &bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4];
            for (int x = 0; x < width; ++x) {
                row[static_cast<std::size_t>(x) * 3] = p[x * 4 + 2];
                row[static_cast<std::size_t>(x) * 3 + 1] = p[x * 4 + 1];
                row[static_cast<std::size_t>(x) * 3 + 2] = p[x * 4];
            }
            f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
    }
    gpu::Release(t);
}

// `draws` draws in `pass` (a cleared target pass: GPU work).
void PassWork(const gpu::Texture& t, perf::Pass pass, int draws) {
    perf::Enter(pass);
    if (BeginTargetPass(t)) gpu::EndPass();
    g_frameDraws += static_cast<std::uint64_t>(draws);
    g_frameCalls += static_cast<std::uint64_t>((draws + 1) / 2);   // a call draws up to two batches
}

void CheckTime() {
    using perf::Pass;
    auto slot = [](Pass p) { return static_cast<int>(p); };
    gpu::Texture t = MakeTarget(640, 360);
    Rect placed{0, 0, 640, 360};
    // The milliseconds since `start`, as a render::Timer gives them.
    auto since = [](std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    for (int frame = 0; frame < 40; ++frame) {
        g_frameDraws = g_frameCalls = 0;
        perf::BeginDraws();
        PassWork(t, Pass::Ui, 2);   // the canvases' queued drawing
        const auto shadow = std::chrono::steady_clock::now();
        PassWork(t, Pass::Sun, 3);
        PassWork(t, Pass::Point, 2);
        perf::AddCpu(perf::Cpu::Shadow, since(shadow));
        const auto submit = std::chrono::steady_clock::now();
        PassWork(t, Pass::Other, 0);
        PassWork(t, Pass::Terrain, 4);
        PassWork(t, Pass::Objects, 5);
        PassWork(t, Pass::Other, 1);
        PassWork(t, Pass::Objects, 1);
        PassWork(t, Pass::Ui, 3);
        perf::Enter(Pass::Present);
        perf::Prepare(360);
        if (BeginTargetPass(t)) {
            perf::Draw(kTargetFormat, 640, 360, placed);
            gpu::EndPass();
        }
        g_frameDraws += 1;
        g_frameCalls += 1;
        perf::EndDraws();
        gpu::Submit();
        perf::AddCpu(perf::Cpu::Submit, since(submit));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        perf::FrameDone();
    }
    perf::Numbers n = perf::Current();
    std::vector<std::string> lines = perf::Lines(n);
    for (const std::string& l : lines) std::printf("  %s\n", l.c_str());
    Check(n.fps > 0.0 && n.frameMs >= 2.0 && n.worstFrameMs >= n.frameMs, "the frame times");
    Check(n.draws[slot(Pass::Sun)] == 3.0 && n.draws[slot(Pass::Point)] == 2.0 && n.draws[slot(Pass::Terrain)] == 4.0 &&
              n.draws[slot(Pass::Objects)] == 6.0 && n.draws[slot(Pass::Other)] == 1.0 && n.draws[slot(Pass::Ui)] == 5.0 &&
              n.draws[slot(Pass::Present)] == 1.0,
          "each pass's draws");
    Check(n.calls[slot(Pass::Sun)] == 2.0 && n.calls[slot(Pass::Point)] == 1.0 && n.calls[slot(Pass::Terrain)] == 2.0 &&
              n.calls[slot(Pass::Objects)] == 4.0 && n.calls[slot(Pass::Other)] == 1.0 && n.calls[slot(Pass::Ui)] == 3.0 &&
              n.calls[slot(Pass::Present)] == 1.0,
          "each pass's calls");
    Check(lines.back() == "CALLS (DRAWS) WORLD 7 (11)   SUN 2 (3)   POINT 1 (2)   UI 4 (6)", "the calls' and draws' line");
    Check(n.submitMs > 0.0 && n.shadowCpuMs >= 0.0, "the CPU times");
    Check(n.gpu && n.gpuMs > 0.0 && n.worstGpuMs >= n.gpuMs, "the whole frame's GPU time came back");
    gpu::Release(t);
}

// The device; its own log line (the driver and the adapter) is not a problem.
bool StartDevice() {
    const int problems = g_problems;
    if (!gpu::Start()) {
        std::printf("no device: %s\n", gpu::Error().c_str());
        return false;
    }
    g_problems = problems;
    std::printf("device: %s, %s\n", gpu::DriverName().c_str(), gpu::DeviceName().c_str());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const bool dump = argc == 3 && std::strcmp(argv[1], "--dump") == 0;
    if (argc > 1 && !dump) {
        std::printf("usage: ei_perf_check [--dump DIR]\n");
        return 2;
    }
    CheckText();
    if (!StartDevice()) return 1;
    perf::SetOn(true);
    CheckDraw(1280, 720);
    CheckDraw(1920, 1080);
    CheckDraw(3840, 2160);
    CheckTime();
    if (dump) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the text is made again every 250 ms
        Dump(argv[2], 1920, 1080);
        Dump(argv[2], 3840, 2160);
    }
    perf::SetOn(false);
    perf::Release();
    gpu::Stop();
    std::printf("perf_check: %d checks, %d failed, %d log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
