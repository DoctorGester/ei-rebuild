// Offline check of the interface's drawing into the frame (src/render/render_ui; docs/notes/ui-native.md).
//
//   ei_ui_check [--verbose]
//
// Runs render_ui's draws on the renderer's GPU (render/gpu.h: Direct3D 12) without the game, playing render.cpp's
// part itself (the frame, its target, SplitFrame, RunCanvasesNow, the commits), and compares the frame's pixels:
//   gdi      fills, frames (FrameRects), lines (LineRects, 1 and 2 px, horizontal and vertical) and pixels drawn as
//            solids against GDI drawing the same into an 8:8:8 DIB section, exactly; diagonal lines are reported,
//            not gated (Bresenham's steps, not GDI's).
//   glyphs   glyph runs (render_text layouts, underlines, clipping, over fills, the shadow under the text) against
//            each quad's coverage blended over what is there in turn, within one level a channel.
//   picture  a frame picture: the copy of a part of the frame (x * source / size), its 5:6:5 reading
//            ((2 x + 1) * source / (2 size)), one-pixel reads, and its draw onto the frame, plain and greyed
//            ((2 r + 4 g + b) / 16), exactly.
// Any ei::Log line but the device's own (gpu.h's misuses among them) fails the check. Exit code 0 when all pass.
#define NOMINMAX
#include "render/glyph_cases.h"
#include "render/gpu.h"
#include "render/render_internal.h"
#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <windows.h>

namespace ui = render::ui;
namespace text = render::text;
namespace detail = render::detail;
namespace gpu = render::gpu;

namespace {

bool g_verbose = false;
int g_problems = 0;   // ei::Log lines

// ---- The host: render.cpp's part, played by glyph_cases' frame ------------------------------------------------

int g_splits = 0;

void LogLine(const char* prefix, const char* text) {
    ++g_problems;
    std::printf("  %s%s\n", prefix, text);
}

} // namespace

namespace ei {
void Log(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    LogLine("log: ", buffer);
}
} // namespace ei

namespace render {
namespace detail {
Frame g_frame;
FrameStats g_stats;   // render_buffers.cpp's counts (the quads' vertex stream)
const gpu::Texture& FrameTexture() { return glyph_cases::Target(); }
bool FrameSize(int* width, int* height) {
    *width = glyph_cases::TargetSize().w;
    *height = glyph_cases::TargetSize().h;
    return static_cast<bool>(glyph_cases::Target());
}
int PassSampleCount() { return 1; }
std::uint32_t CommitNumber() { return glyph_cases::Commits(); }
std::uint32_t FrameNumber() { return glyph_cases::Commits(); }
void SplitFrame() {
    ++g_splits;
    glyph_cases::DrawFrame();
}
void RunCanvasesNow() { RunCanvasCommands(); }
} // namespace detail
} // namespace render

namespace {

using glyph_cases::Differ;
using glyph_cases::EndFrame;
using glyph_cases::NewFrame;
using glyph_cases::Outcome;
using glyph_cases::ReadFrame;
using glyph_cases::RgbOfColorref;

// ---- Checks -------------------------------------------------------------------------------------------------

int g_checks = 0, g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) {
        if (g_verbose) std::printf("  ok   %s\n", what.c_str());
        return;
    }
    ++g_failures;
    std::printf("  FAIL %s\n", what.c_str());
}

// ---- gdi: fills, frames, lines, pixels against GDI ------------------------------------------------------------

struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    const std::uint8_t* bits = nullptr;
    int pitch = 0;
    Dib(int width, int height) {   // 8:8:8, the top byte unused
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* memory = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &memory, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        old = SelectObject(dc, bitmap);
        bits = static_cast<const std::uint8_t*>(memory);
        pitch = width * 4;
    }
    std::uint32_t At(int x, int y) const {
        const std::uint8_t* row = bits + static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch);
        return reinterpret_cast<const std::uint32_t*>(row)[x] & 0xFFFFFF;
    }
    ~Dib() {
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(bitmap);
    }
};

enum class Shape { Fill, Frame, Line1, Line2, Pixel, Diagonal };
const char* ShapeName(Shape s) {
    switch (s) {
    case Shape::Fill: return "fills";
    case Shape::Frame: return "frames";
    case Shape::Line1: return "1-px lines";
    case Shape::Line2: return "2-px lines";
    case Shape::Pixel: return "pixels";
    case Shape::Diagonal: return "diagonal 1-px lines";
    }
    return "?";
}

constexpr int kGdiWidth = 61, kGdiHeight = 47;

// `count` random shapes drawn by GDI and into the frame: the pixels that differ.
int CompareWithGdi(Shape shape, int count, unsigned seed, std::string* example) {
    const int w = kGdiWidth, h = kGdiHeight;
    const Rect all{0, 0, w, h};
    NewFrame(w, h);
    Dib dib(w, h);
    std::mt19937 random(seed);
    auto coord = [&](int size) { return static_cast<int>(random() % static_cast<unsigned>(size + 8)) - 4; };
    auto colour = [&]() { return static_cast<COLORREF>(random() & 0xFFFFFF); };
    auto solids = [&](const std::vector<Rect>& rects, COLORREF c) {
        for (const Rect& r : rects) ui::Solid(r, c, all);
    };
    for (int i = 0; i < count; ++i) {
        COLORREF c = colour();
        switch (shape) {
        case Shape::Fill:
        case Shape::Frame: {
            int x0 = coord(w), x1 = coord(w), y0 = coord(h), y1 = coord(h);
            RECT r{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
            // GDI's FrameRect draws odd shapes for an empty rectangle (not the game's; FrameRects gives none).
            if (shape == Shape::Frame && (r.right == r.left || r.bottom == r.top)) ++r.right, ++r.bottom;
            const Rect ours{r.left, r.top, r.right, r.bottom};
            if (shape == Shape::Fill) {
                SetBkColor(dib.dc, c);
                ExtTextOutA(dib.dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);   // CDC::FillSolidRect
                ui::Solid(ours, c, all);
            } else {
                HBRUSH brush = CreateSolidBrush(c);
                FrameRect(dib.dc, &r, brush);
                DeleteObject(brush);
                solids(ui::FrameRects(ours), c);
            }
            break;
        }
        case Shape::Line1:
        case Shape::Line2:
        case Shape::Diagonal: {
            int width = shape == Shape::Line2 ? 2 : 1;
            int x0 = coord(w), y0 = coord(h), x1 = coord(w), y1 = coord(h);
            if (shape != Shape::Diagonal) {
                if (random() & 1) x1 = x0;
                else y1 = y0;
            }
            if (g_verbose && count == 1) std::printf("    line %d px (%d, %d)-(%d, %d)\n", width, x0, y0, x1, y1);
            HPEN pen = CreatePen(PS_SOLID, width, c);
            HGDIOBJ old = SelectObject(dib.dc, pen);
            MoveToEx(dib.dc, x0, y0, nullptr);
            LineTo(dib.dc, x1, y1);
            SelectObject(dib.dc, old);
            DeleteObject(pen);
            solids(ui::LineRects({x0, y0}, {x1, y1}, width), c);
            break;
        }
        case Shape::Pixel: {
            int x = coord(w), y = coord(h);
            SetPixel(dib.dc, x, y, c);
            ui::Solid(Rect::Sized({x, y}, {1, 1}), c, all);
            break;
        }
        }
    }
    GdiFlush();
    std::vector<std::uint32_t> ours;
    const bool read = ReadFrame(&ours);
    EndFrame();
    if (!read) return -1;
    std::vector<std::uint32_t> gdi(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) gdi[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = dib.At(x, y);
    return Differ(gdi, ours, w, example);
}

void CheckGdi() {
    if (g_verbose) std::printf("gdi:\n");
    for (Shape s : {Shape::Fill, Shape::Frame, Shape::Line1, Shape::Line2, Shape::Pixel}) {
        // One shape at a time (a difference shows which), and many in one frame.
        int single = 0;
        std::string example;
        for (unsigned seed = 1; seed <= 60 && single == 0; ++seed) single = CompareWithGdi(s, 1, seed * 7919u, &example);
        int many = single == 0 ? CompareWithGdi(s, 300, 12345u, &example) : 0;
        Check(single == 0 && many == 0, std::string(ShapeName(s)) + " against GDI: " + Outcome(single + many, example));
    }
    std::string example;
    int diagonal = CompareWithGdi(Shape::Diagonal, 100, 777u, &example);
    std::printf("  report %s: %d of %d pixels differ from GDI (not gated)\n", ShapeName(Shape::Diagonal), diagonal,
                kGdiWidth * kGdiHeight);
}

// ---- picture: frame copies --------------------------------------------------------------------------------------

std::uint32_t GreyOf(std::uint32_t rgb) {
    const std::uint32_t grey = (2 * ((rgb >> 16) & 0xFF) + 4 * ((rgb >> 8) & 0xFF) + (rgb & 0xFF)) >> 4;
    return grey << 16 | grey << 8 | grey;
}

std::uint16_t Rgb565Of(std::uint32_t rgb) {
    return static_cast<std::uint16_t>((((rgb >> 16) & 0xFF) >> 3) << 11 | (((rgb >> 8) & 0xFF) >> 2) << 5 | ((rgb & 0xFF) >> 3));
}

void CheckPicture() {
    if (g_verbose) std::printf("picture:\n");
    const int w = 96, h = 64;
    const Rect all{0, 0, w, h};
    int key = 0;
    NewFrame(w, h);
    // A frame of random blocks, copied in part to a picture of another size.
    std::vector<std::uint32_t> frame(static_cast<std::size_t>(w) * h, 0);
    std::mt19937 random(4242u);
    for (int i = 0; i < 60; ++i) {
        const int x = static_cast<int>(random() % w), y = static_cast<int>(random() % h);
        const Rect r = Rect{x, y, x + 1 + static_cast<int>(random() % 12), y + 1 + static_cast<int>(random() % 9)}.Intersection(all);
        const COLORREF c = random() & 0xFFFFFF;
        ui::Solid(r, c, all);
        for (int yy = r.top; yy < r.bottom; ++yy)
            for (int xx = r.left; xx < r.right; ++xx) frame[static_cast<std::size_t>(yy) * w + static_cast<std::size_t>(xx)] = RgbOfColorref(c);
    }
    const Rect source{10, 6, 90, 60};
    const int pw = 40, ph = 27;
    const int splits = g_splits;
    ui::CapturePicture(&key, source, pw, ph);
    Check(g_splits == splits + 1 && ui::HasPicture(&key), "a capture draws the frame so far first");
    std::vector<std::uint32_t> picture(static_cast<std::size_t>(pw) * ph);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
            picture[static_cast<std::size_t>(y) * pw + static_cast<std::size_t>(x)] =
                frame[static_cast<std::size_t>(source.top + y * source.Height() / ph) * w + static_cast<std::size_t>(source.left + x * source.Width() / pw)];
    // One-pixel reads.
    int wrong = 0;
    for (Point at : {Point{0, 0}, Point{39, 26}, Point{17, 9}, Point{3, 22}}) {
        std::uint32_t rgb = 0;
        if (!ui::ReadPicturePixel(&key, at, &rgb) || rgb != picture[static_cast<std::size_t>(at.y) * pw + static_cast<std::size_t>(at.x)]) ++wrong;
    }
    std::uint32_t outside = 0;
    Check(wrong == 0 && !ui::ReadPicturePixel(&key, {pw, 0}, &outside), "one-pixel reads of the picture: " + std::to_string(wrong) + " wrong");
    // The 5:6:5 picture at its own size and at about half of it.
    for (Size size : {Size{pw, ph}, Size{19, 13}}) {
        std::vector<std::uint16_t> got(static_cast<std::size_t>(size.w) * size.h), want(got.size());
        for (int y = 0; y < size.h; ++y)
            for (int x = 0; x < size.w; ++x) {
                const int tx = ((2 * x + 1) * pw) / (2 * size.w), ty = ((2 * y + 1) * ph) / (2 * size.h);
                want[static_cast<std::size_t>(y) * size.w + static_cast<std::size_t>(x)] = Rgb565Of(picture[static_cast<std::size_t>(ty) * pw + static_cast<std::size_t>(tx)]);
            }
        const bool read = ui::ReadPictureRgb565(&key, size.w, size.h, got.data());
        Check(read && got == want, "the 5:6:5 picture at " + std::to_string(size.w) + "x" + std::to_string(size.h));
    }
    EndFrame();
    // Drawn onto the next frame at twice its size, plain and greyed.
    NewFrame(w * 2, h * 2);
    std::vector<std::uint32_t> expected(static_cast<std::size_t>(w) * 4 * h, 0);
    const Rect plain = Rect::Sized({4, 6}, {pw * 2, ph * 2}), grey = Rect::Sized({100, 60}, {pw * 2, ph * 2});
    ui::DrawPicture(&key, plain, false);
    ui::DrawPicture(&key, grey, true);
    for (const Rect& r : {plain, grey})
        for (int y = r.top; y < r.bottom; ++y)
            for (int x = r.left; x < r.right; ++x) {
                const std::uint32_t c = picture[static_cast<std::size_t>((y - r.top) / 2) * pw + static_cast<std::size_t>((x - r.left) / 2)];
                expected[static_cast<std::size_t>(y) * (w * 2) + static_cast<std::size_t>(x)] = r == grey ? GreyOf(c) : c;
            }
    std::vector<std::uint32_t> got;
    std::string example;
    const int differ = ReadFrame(&got) ? Differ(expected, got, w * 2, &example) : -1;
    Check(differ == 0, "the picture drawn plain and greyed: " + Outcome(differ, example));
    // Captured again while the frame draws it: the draw keeps the old picture.
    ui::CapturePicture(&key, source, pw, ph);
    ui::DropPicture(&key);
    Check(!ui::HasPicture(&key), "a dropped picture is gone");
    EndFrame();
}

// The device; its own log line (the driver and the adapter) is not a problem.
bool StartDevice() {
    const int problems = g_problems;
    const bool started = gpu::Start();
    if (!started) {
        std::printf("no device: %s\n", gpu::Error());
        return false;
    }
    g_problems = problems;
    std::printf("device: %s, %s\n", gpu::DriverName(), gpu::DeviceName());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--verbose")) g_verbose = true;
        else {
            std::printf("usage: ei_ui_check [--verbose]\n");
            return 2;
        }
    }
    if (!StartDevice()) return 1;
    CheckGdi();
    if (g_verbose) std::printf("glyphs:\n");
    glyph_cases::Run(Check);
    CheckPicture();
    glyph_cases::ReleaseFrame();
    gpu::Stop();
    std::printf("ui_check: %d checks, %d failed, %d log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
