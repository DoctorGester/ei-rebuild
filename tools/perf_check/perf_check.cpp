// Offline check of the performance overlay (src/render/render_perf, ticket R21; docs/notes/renderer.md section 15)
// on the renderer's GPU (render/gpu.h: Direct3D 12), without the game:
//   text    the lines of known numbers, every character in the font, the image's size and a glyph's pixels.
//   draw    the overlay drawn over a cleared target (as the present draws it: BGRA8, no depth) at 720 and 2160 rows:
//           every pixel against the text image scaled up (text: its brightness; the box: the clear colour at 40%;
//           outside the box: untouched).
//   time    frames with the passes the renderer marks: each pass's draws, the CPU times and the whole frame's GPU
//           time (gpu::FrameGpuTime) come back.
// Any ei::Log line but the switch's and the device's own fails the check. Exit code 0 when all pass.
#include "render/gpu.h"
#include "render/render_internal.h"
#include "render/render_perf.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace gpu = render::gpu;
namespace perf = render::perf;

namespace {

int g_checks = 0, g_failures = 0, g_problems = 0;
std::uint64_t g_frameDraws = 0;   // FrameDrawCount's (the renderer's FrameStats in the game)

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
    n.draws[slot(Pass::Sun)] = 120;
    n.draws[slot(Pass::Point)] = 80;
    n.draws[slot(Pass::Terrain)] = 30;
    n.draws[slot(Pass::Objects)] = 300;
    n.draws[slot(Pass::Other)] = 40;
    n.draws[slot(Pass::Ui)] = 24;
    n.draws[slot(Pass::Present)] = 1;
    const std::vector<std::string> expected = {"FPS 144   FRAME 6.96 MS   WORST 9.13", "GPU 2.10 MS   WORST 2.60",
                                               "CPU SUBMIT 1.10 MS   SHADOW 0.12",
                                               "DRAWS WORLD 370   SUN 120   POINT 80   UI 25"};
    std::vector<std::string> lines = perf::Lines(n);
    Check(lines == expected, "the lines of known numbers");
    for (const std::string& l : lines) std::printf("  %s\n", l.c_str());
    n.gpu = false;
    lines = perf::Lines(n);
    Check(lines.size() == 4 && lines[1] == "GPU -", "the lines without GPU times");
    for (const std::string& l : perf::Lines(perf::Numbers{}))
        for (char c : l) Check(c == ' ' || perf::HasGlyph(c), std::string("a glyph for '") + c + "'");
    for (char c : std::string("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:/-+%()"))
        Check(perf::HasGlyph(c), std::string("a glyph for '") + c + "'");
    const perf::TextImage image = perf::Rasterise({"1", "AB"});
    const int w = image.width, h = image.height;
    Check(w == 2 * perf::kBorder + perf::kCellWidth + perf::kGlyphWidth &&
              h == 2 * perf::kBorder + perf::kCellHeight + perf::kGlyphHeight,
          "the image's size");
    // '1': its top row "..#..", its bottom row ".###." (a digit: kValue); 'A' of the second line a label.
    auto at = [&](int x, int y) {
        return image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
    };
    const int b = perf::kBorder;
    Check(at(b + 2, b) == perf::kValue && at(b + 1, b) == 0 && at(b + 1, b + 6) == perf::kValue && at(b, b + 6) == 0,
          "the pixels of '1'");
    Check(at(b + 1, b + perf::kCellHeight) == perf::kLabel && at(b, b + perf::kCellHeight) == 0, "the pixels of 'A'");
    Check(at(0, 0) == 0 && at(w - 1, h - 1) == 0, "the border");
    Check(perf::Scale(480) == 1 && perf::Scale(720) == 1 && perf::Scale(1080) == 2 && perf::Scale(1440) == 3 &&
              perf::Scale(2160) == 4,
          "the scale by height");
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
    perf::Prepare();
    if (BeginTargetPass(t)) {
        perf::Draw(kTargetFormat, width, height, placed);
        gpu::EndPass();
    }
    gpu::Submit();
    // What it should show: the current numbers' lines (none measured yet), scaled up from the corner.
    const perf::TextImage text = perf::Rasterise(perf::Lines(perf::Current()));
    const int tw = text.width, th = text.height;
    int scale = perf::Scale(height), x0 = perf::Margin(scale), y0 = perf::Margin(scale);
    std::vector<std::uint8_t> bgra;
    bool read = ReadTarget(t, &bgra);
    Check(read, "the target read back");
    int wrong = 0, textPixels = 0;
    std::string example;
    for (int y = 0; read && y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = &bgra[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            int want[3] = {kClear[0], kClear[1], kClear[2]};
            int tx = (x - x0) / scale, ty = (y - y0) / scale;
            bool inside = x >= x0 && y >= y0 && tx < tw && ty < th;
            if (inside) {
                std::uint8_t v = text.pixels[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tw) +
                                             static_cast<std::size_t>(tx)];
                for (int c = 0; c < 3; ++c) want[c] = v ? v : (kClear[c] * 4 + 5) / 10;
                if (v) ++textPixels;
            }
            bool ok = true;
            for (int c = 0; c < 3; ++c) ok = ok && std::abs(static_cast<int>(p[c]) - want[c]) <= (inside ? 1 : 0);
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
    std::printf("  %dx%d: scale %d, box %dx%d, %d text pixels\n", width, height, scale, tw * scale, th * scale, textPixels);
    gpu::Release(t);
}

// `draws` draws in `pass` (a cleared target pass: GPU work).
void PassWork(const gpu::Texture& t, perf::Pass pass, int draws) {
    perf::Enter(pass);
    if (BeginTargetPass(t)) gpu::EndPass();
    g_frameDraws += static_cast<std::uint64_t>(draws);
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
        g_frameDraws = 0;
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
        perf::Prepare();
        if (BeginTargetPass(t)) {
            perf::Draw(kTargetFormat, 640, 360, placed);
            gpu::EndPass();
        }
        g_frameDraws += 1;
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
    Check(lines.back() == "DRAWS WORLD 11   SUN 3   POINT 2   UI 6", "the draws' line");
    Check(n.submitMs > 0.0 && n.shadowCpuMs >= 0.0, "the CPU times");
    Check(n.gpu && n.gpuMs > 0.0 && n.worstGpuMs >= n.gpuMs, "the whole frame's GPU time came back");
    gpu::Release(t);
}

// The device; its own log line (the driver and the adapter) is not a problem.
bool StartDevice() {
    const int problems = g_problems;
    if (!gpu::Start()) {
        std::printf("no device: %s\n", gpu::Error());
        return false;
    }
    g_problems = problems;
    std::printf("device: %s, %s\n", gpu::DriverName(), gpu::DeviceName());
    return true;
}

} // namespace

int main(int argc, char**) {
    if (argc > 1) {
        std::printf("usage: ei_perf_check\n");
        return 2;
    }
    CheckText();
    if (!StartDevice()) return 1;
    perf::SetOn(true);
    CheckDraw(1280, 720);
    CheckDraw(3840, 2160);
    CheckTime();
    perf::SetOn(false);
    perf::Release();
    gpu::Stop();
    std::printf("perf_check: %d checks, %d failed, %d log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
