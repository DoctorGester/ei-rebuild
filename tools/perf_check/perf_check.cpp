// Offline check of the performance overlay (src/render/render_perf, ticket R21; docs/notes/renderer.md section 15)
// on a Direct3D 11 WARP device, without the game:
//   text    the lines of known numbers, every character in the font, the image's size and a glyph's pixels.
//   draw    the overlay drawn over a cleared back buffer (as the present draws it: BGRA8, no depth) at 720 and
//           2160 rows: every pixel against the text image scaled up (text: its brightness; the box: the clear
//           colour at 40%; outside the box: untouched).
//   time    frames with the passes the renderer marks: the GPU times come back, the passes add up to the total
//           (also in a frame with more changes of pass than the frame's timestamps), each pass's draws.
//   vsync   (R23, the vertical blank switch) the rules for the chain's flags and Present's interval and flags
//           (tearing wanted or not, supported or not), and on a hidden window's flip-model chain made as the renderer
//           makes it: the flags it reports, ResizeBuffers with them, a Present with the switch's interval and flags.
// Any sokol error or warning and any ei::Log line but the switch's fails the check. Exit code 0 when all pass.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>

#include "platform/win32/graphics_win32.h"
#include "render/render_internal.h"
#include "render/render_perf.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace perf = render::perf;

namespace {

int g_checks = 0, g_failures = 0, g_problems = 0;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
std::uint64_t g_frameDraws = 0;   // FrameDrawCount's (the renderer's FrameStats in the game)

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAILED: %s\n", what.c_str());
    }
}

void SokolLog(const char* tag, std::uint32_t level, std::uint32_t item, const char* message, std::uint32_t line, const char*, void*) {
    if (level > 2 || item == SG_LOGITEM_D3D11_FEATURE_LEVEL_0_DETECTED) return;
    ++g_problems;
    std::printf("  sokol: %s level %u item %u (line %u): %s\n", tag ? tag : "sg", level, item, line, message ? message : "");
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
    n.passMs[slot(Pass::Sun)] = 0.25;
    n.passMs[slot(Pass::Point)] = 0.06;
    n.passMs[slot(Pass::Terrain)] = 0.5;
    n.passMs[slot(Pass::Objects)] = 0.4;
    n.passMs[slot(Pass::Other)] = 0.3;
    n.passMs[slot(Pass::Ui)] = 0.4;
    n.passMs[slot(Pass::Present)] = 0.19;
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
                                               "  SHADOW 0.31 (SUN 0.25  POINT 0.06)",
                                               "  WORLD 1.20 (TERRAIN 0.50  OBJECTS 0.40  OTHER 0.30)",
                                               "  UI 0.40   PRESENT 0.19", "CPU SUBMIT 1.10 MS   SHADOW 0.12",
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
    std::vector<std::uint8_t> pixels;
    int w = 0, h = 0;
    perf::Rasterise({"1", "AB"}, &pixels, &w, &h);
    Check(w == 2 * perf::kBorder + perf::kCellWidth + perf::kGlyphWidth &&
              h == 2 * perf::kBorder + perf::kCellHeight + perf::kGlyphHeight,
          "the image's size");
    // '1': its top row "..#..", its bottom row ".###." (a digit: kValue); 'A' of the second line a label.
    auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]; };
    const int b = perf::kBorder;
    Check(at(b + 2, b) == perf::kValue && at(b + 1, b) == 0 && at(b + 1, b + 6) == perf::kValue && at(b, b + 6) == 0,
          "the pixels of '1'");
    Check(at(b + 1, b + perf::kCellHeight) == perf::kLabel && at(b, b + perf::kCellHeight) == 0, "the pixels of 'A'");
    Check(at(0, 0) == 0 && at(w - 1, h - 1) == 0, "the border");
    Check(perf::Scale(480) == 1 && perf::Scale(720) == 1 && perf::Scale(1080) == 2 && perf::Scale(1440) == 3 &&
              perf::Scale(2160) == 4,
          "the scale by height");
}

// A BGRA8 render target (the back buffer's format) with its attachment view.
struct Target {
    sg_image image{};
    sg_view attachment{};
    int width = 0, height = 0;
};

Target MakeTarget(int width, int height) {
    Target t;
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = width;
    d.height = height;
    d.pixel_format = SG_PIXELFORMAT_BGRA8;
    d.sample_count = 1;
    t.image = sg_make_image(&d);
    sg_view_desc v{};
    v.color_attachment.image = t.image;
    t.attachment = sg_make_view(&v);
    t.width = width;
    t.height = height;
    return t;
}

void DropTarget(Target& t) {
    sg_destroy_view(t.attachment);
    sg_destroy_image(t.image);
}

constexpr std::uint8_t kClear[3] = {160, 90, 40};   // B, G, R

void BeginTargetPass(const Target& t) {
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = sg_color{kClear[2] / 255.0f, kClear[1] / 255.0f, kClear[0] / 255.0f, 1.0f};
    pass.attachments.colors[0] = t.attachment;
    sg_begin_pass(&pass);
}

bool ReadTarget(const Target& t, std::vector<std::uint8_t>* bgra) {
    platform::graphics::Readback copy;
    if (!copy.Read(t.image, platform::Rect{0, 0, t.width, t.height})) return false;
    bgra->resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4);
    for (int y = 0; y < t.height; ++y)
        std::memcpy(bgra->data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) * 4, copy.Row(y),
                    static_cast<std::size_t>(t.width) * 4);
    return true;
}

void CheckDraw(int width, int height) {
    Target t = MakeTarget(width, height);
    render::Rect placed{0, 0, width, height};
    BeginTargetPass(t);
    perf::Draw(width, height, placed);
    sg_end_pass();
    sg_commit();
    // What it should show: the current numbers' lines (none measured yet), scaled up from the corner.
    std::vector<std::uint8_t> text;
    int tw = 0, th = 0;
    perf::Rasterise(perf::Lines(perf::Current()), &text, &tw, &th);
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
                std::uint8_t v = text[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tw) + static_cast<std::size_t>(tx)];
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
    DropTarget(t);
}

// `draws` draws in `pass` (cleared target passes: GPU work between the stamps).
void PassWork(const Target& t, perf::Pass pass, int draws) {
    perf::Enter(pass);
    BeginTargetPass(t);
    sg_end_pass();
    g_frameDraws += static_cast<std::uint64_t>(draws);
}

void CheckTime() {
    using perf::Pass;
    auto slot = [](Pass p) { return static_cast<int>(p); };
    Target t = MakeTarget(640, 360);
    render::Rect placed{0, 0, 640, 360};
    for (int frame = 0; frame < 40; ++frame) {
        g_frameDraws = 0;
        perf::BeginGpuFrame();
        PassWork(t, Pass::Ui, 2);   // the canvases' queued drawing
        {
            perf::CpuTimer shadow(perf::Cpu::Shadow);
            PassWork(t, Pass::Sun, 3);
            PassWork(t, Pass::Point, 2);
        }
        perf::CpuTimer submit(perf::Cpu::Submit);
        PassWork(t, Pass::Other, 0);
        PassWork(t, Pass::Terrain, 4);
        PassWork(t, Pass::Objects, 5);
        PassWork(t, Pass::Other, 1);
        PassWork(t, Pass::Objects, 1);
        PassWork(t, Pass::Ui, 3);
        // Every tenth frame more changes of pass than a frame's timestamps: the rest goes to the last stamped.
        if (frame % 10 == 9)
            for (int k = 0; k < 100; ++k) perf::Enter(k % 2 ? Pass::Ui : Pass::Other);
        perf::Enter(Pass::Present);
        BeginTargetPass(t);
        perf::Draw(640, 360, placed);
        sg_end_pass();
        g_frameDraws += 1;
        perf::EndGpuFrame();
        sg_commit();
        submit.Stop();
        g_context->Flush();
        Sleep(2);
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
    Check(n.gpu, "the GPU times came back");
    double parts = 0.0;
    for (double ms : n.passMs) parts += ms;
    Check(n.gpu && n.gpuMs > 0.0 && std::abs(parts - n.gpuMs) <= n.gpuMs * 0.001 + 0.0001 && n.worstGpuMs >= n.gpuMs,
          "the passes add up to the GPU total");
    DropTarget(t);
}

bool StartDevice(bool hardware) {
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;   // the shaders are shader model 5
    D3D_FEATURE_LEVEL got{};
    HRESULT hr =
        D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION, &g_device, &got, &g_context);
    if (FAILED(hr)) {
        std::printf("D3D11CreateDevice (%s) failed: %08lX\n", hardware ? "hardware" : "WARP", static_cast<unsigned long>(hr));
        return false;
    }
    sg_desc desc{};
    desc.environment.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    desc.environment.defaults.sample_count = 1;
    desc.environment.d3d11.device = g_device;
    desc.environment.d3d11.device_context = g_context;
    desc.logger.func = &SokolLog;
    sg_setup(&desc);
    return sg_isvalid();
}

// The vertical blank switch (src/platform/win32/graphics_win32.cpp, R23): the pure rules, then real chains on a hidden
// window.
void CheckVsync() {
    const UINT tearing = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    namespace d = platform::graphics;
    Check(d::SyncIntervalFor(true) == 1 && d::SyncIntervalFor(false) == 0, "the sync interval: 1 with the wait, 0 without");
    Check(d::ChainFlagsFor(false, true) == 0 && d::ChainFlagsFor(false, false) == 0 && d::ChainFlagsFor(true, false) == 0 &&
              d::ChainFlagsFor(true, true) == tearing,
          "the chain's tearing flag only when wanted and supported");
    Check(d::PresentFlagsFor(true, 0) == 0 && d::PresentFlagsFor(true, tearing) == 0 && d::PresentFlagsFor(false, 0) == 0 &&
              d::PresentFlagsFor(false, tearing) == DXGI_PRESENT_ALLOW_TEARING,
          "Present's tearing flag only without the wait and on a chain made with the flag");

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
              SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
              SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)));
    Check(ok, "the device's DXGI factory");
    HWND window = CreateWindowExW(0, L"STATIC", L"ei_perf_check", WS_POPUP, 0, 0, 320, 240, nullptr, nullptr, nullptr, nullptr);
    Check(window != nullptr, "a hidden window");
    if (ok && window) {
        const bool supported = d::TearingSupported(factory);
        std::printf("  tearing presents %s on this device\n", supported ? "supported" : "not supported");
        for (int wanted = 0; wanted < 2; ++wanted) {
            const bool allowTearing = wanted != 0;
            const bool vsync = !allowTearing;
            DXGI_SWAP_CHAIN_DESC1 sd{};
            sd.Width = 320;
            sd.Height = 240;
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.Scaling = DXGI_SCALING_STRETCH;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            sd.Flags = d::ChainFlagsFor(allowTearing, supported);
            IDXGISwapChain1* chain = nullptr;
            const std::string name = allowTearing ? "the VSync=0 chain" : "the VSync=1 chain";
            HRESULT hr = factory->CreateSwapChainForHwnd(g_device, window, &sd, nullptr, nullptr, &chain);
            Check(SUCCEEDED(hr) && chain, name + " made");
            if (!chain) continue;
            DXGI_SWAP_CHAIN_DESC1 got{};
            chain->GetDesc1(&got);
            Check(((got.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) == (allowTearing && supported),
                  name + " reports the tearing flag as asked");
            Check(SUCCEEDED(chain->ResizeBuffers(0, 400, 300, DXGI_FORMAT_UNKNOWN, sd.Flags)), name + " resized with the same flags");
            hr = chain->Present(d::SyncIntervalFor(vsync), d::PresentFlagsFor(vsync, sd.Flags));
            Check(SUCCEEDED(hr), name + " presents with the switch's interval and flags");
            if (FAILED(hr)) std::printf("  Present returned %08lX\n", static_cast<unsigned long>(hr));
            chain->Release();
        }
    }
    if (window) DestroyWindow(window);
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
}

} // namespace

int main(int argc, char** argv) {
    bool hardware = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hardware")) hardware = true;
        else {
            std::printf("usage: ei_perf_check [--hardware]\n");
            return 2;
        }
    }
    CheckText();
    if (!StartDevice(hardware)) return 1;
    perf::SetOn(true);
    CheckDraw(1280, 720);
    CheckDraw(3840, 2160);
    CheckTime();
    CheckVsync();
    perf::SetOn(false);
    perf::Release();
    sg_shutdown();
    g_context->Release();
    g_device->Release();
    std::printf("perf_check: %d checks, %d failed, %d sokol / log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
