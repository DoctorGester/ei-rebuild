// Offline check of the performance overlay (src/render/render_perf, ticket R21; docs/notes/renderer.md section 15)
// on a Direct3D 11 WARP device, without the game:
//   text    the lines of known numbers, every character in the font, the image's size and a glyph's pixels.
//   draw    the overlay drawn over a cleared back buffer (as the present draws it: BGRA8, no depth) at 720 and
//           2160 rows: every pixel against the text image scaled up (text: its brightness; the box: the clear
//           colour at 40%; outside the box: untouched).
//   time    frames with the timestamps the renderer takes: the GPU times come back, the passes within the total,
//           the frame counts kept.
// Any sokol error or warning and any ei::Log line but the switch's fails the check. Exit code 0 when all pass.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include "render/render_internal.h"
#include "render/render_perf.h"

#include <cstdarg>
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

namespace render::detail {
ID3D11Device* D3DDevice() { return g_device; }
ID3D11DeviceContext* D3DContext() { return g_context; }
} // namespace render::detail

namespace {

void CheckText() {
    perf::Numbers n;
    n.fps = 143.6;
    n.frameMs = 6.964;
    n.worstFrameMs = 9.126;
    n.gpu = true;
    n.gpuMs = 2.1;
    n.worstGpuMs = 2.6;
    n.shadowMs = 0.31;
    n.worldMs = 1.2;
    n.uiMs = 0.4;
    n.presentMs = 0.19;
    n.submitMs = 1.1;
    n.shadowCpuMs = 0.12;
    n.draws = 370;
    n.shadowDraws = 120;
    const std::vector<std::string> expected = {"FPS 144   FRAME 6.96 MS   WORST 9.13", "GPU 2.10 MS   WORST 2.60",
                                               "  SHADOW 0.31   WORLD 1.20   UI 0.40   PRESENT 0.19",
                                               "CPU SUBMIT 1.10 MS   SHADOW 0.12", "DRAWS 370   SHADOW 120"};
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
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(t.image).tex2d));
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    g_context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool ok = SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    if (ok) {
        bgra->resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4);
        for (int y = 0; y < t.height; ++y)
            std::memcpy(bgra->data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) * 4,
                        static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                        static_cast<std::size_t>(t.width) * 4);
        g_context->Unmap(staging, 0);
    }
    staging->Release();
    sg_reset_state_cache();
    return ok;
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

void CheckTime() {
    Target t = MakeTarget(640, 360);
    render::Rect placed{0, 0, 640, 360};
    for (int frame = 0; frame < 40; ++frame) {
        perf::BeginGpuFrame();
        {
            perf::CpuTimer shadow(perf::Cpu::Shadow);
            perf::Stamp(perf::Mark::ShadowBegin);
            BeginTargetPass(t);
            sg_end_pass();
            perf::Stamp(perf::Mark::ShadowEnd);
        }
        perf::CpuTimer submit(perf::Cpu::Submit);
        perf::WorldCommand(true);
        perf::WorldCommand(true);
        perf::WorldCommand(false);   // the world ends here
        perf::Stamp(perf::Mark::FrameEnd);
        BeginTargetPass(t);
        perf::Draw(640, 360, placed);
        sg_end_pass();
        perf::Stamp(perf::Mark::PresentEnd);
        perf::EndGpuFrame();
        sg_commit();
        submit.Stop();
        g_context->Flush();
        Sleep(2);
        perf::FrameDone(10, 3);
    }
    perf::Numbers n = perf::Current();
    std::printf("  %s\n", perf::Lines(n)[0].c_str());
    std::printf("  %s\n", perf::Lines(n)[1].c_str());
    Check(n.fps > 0.0 && n.frameMs >= 2.0 && n.worstFrameMs >= n.frameMs, "the frame times");
    Check(n.draws == 10.0 && n.shadowDraws == 3.0, "the draws");
    Check(n.submitMs > 0.0 && n.shadowCpuMs >= 0.0, "the CPU times");
    Check(n.gpu, "the GPU times came back");
    double parts = n.shadowMs + n.worldMs + n.uiMs + n.presentMs;
    Check(n.gpu && n.gpuMs > 0.0 && parts <= n.gpuMs * 1.001 + 0.001 && n.worstGpuMs >= n.gpuMs,
          "the passes within the GPU total");
    DropTarget(t);
}

bool StartDevice(bool hardware) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION, &g_device, &got, &g_context);
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
    perf::SetOn(false);
    perf::Release();
    sg_shutdown();
    g_context->Release();
    g_device->Release();
    std::printf("perf_check: %d checks, %d failed, %d sokol / log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
