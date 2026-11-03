// Offline check of the interface canvases (src/render/render_ui, ticket N1; docs/notes/ui-native.md).
//
//   ei_ui_check [--verbose]
//
// Runs render_ui's commands on the renderer's GPU (render/gpu.h: Direct3D 12) without the game, playing
// render.cpp's part itself (the frame, its target, SplitFrame, RunCanvasesNow, the commits), and
// compares every canvas pixel exactly:
//   gdi     fills, frames, lines (1 and 2 px, horizontal and vertical) and pixels against GDI drawing the
//           same into a DIB section of each surface format (5:6:5, 5:5:5, 8:8:8); diagonal lines are
//           reported, not gated (Bresenham's steps, not GDI's).
//   glyphs  glyph runs (render_text layouts, underlines, clipping, over a coloured background, the shadow
//           pass under the text) against the CPU rule, and over black against text::ComposeAgainstBlack.
//   grey    greying against 005FE440's formulas on the packed pixels.
//   copy    screen copies of a frame texture against the original's stretch (x * source / size) and packing.
//   picture the save's 5:6:5 picture (ReadRgb565) against the original's stretch ((2 x + 1) * source / (2 size))
//           and packing; one-pixel reads (ReadPixel) and their top bits (TopBits); a canvas moved to another key with its queued commands
//           (MoveCanvas).
//   image   pixels written into canvases (clipped, quantised) exactly.
//   frame   canvases blitted in the frame (keyed, stretched, a source part) against the window blit's
//           rule, and the pass split: drawing into a canvas blitted and not drawn yet draws the frame
//           first, so the blit shows the canvas as it was.
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
using glyph_cases::Differ;
using glyph_cases::Format;
using glyph_cases::kFormats;
using glyph_cases::RefQuantise;
using glyph_cases::RefTopBits;

namespace {

bool g_verbose = false;
int g_problems = 0;   // ei::Log lines

// ---- The host: what render.cpp gives render_ui.cpp -----------------------------------------------------

gpu::Texture g_frameTexture;   // CopyFrame's source
int g_frameWidth = 0, g_frameHeight = 0;
int g_splits = 0;
std::uint32_t g_commits = 0;

// The frame's target for the blits (with depth: the canvas blit's pipeline is the frame pass's).
gpu::Texture g_target, g_targetDepth;
int g_targetWidth = 160, g_targetHeight = 120;
std::size_t g_drawnCommands = 0;
int g_passes = 0;
constexpr std::uint32_t kTargetClearRgb = 0x28323C;

// render.cpp's EndFrame: the commands sent (the stream buffers are written from their start again).
void Commit() {
    gpu::Submit();
    ++g_commits;
}

// render.cpp's DrawPending: the canvases' queue first, then the frame's commands not drawn yet.
void DrawTestFrame() {
    detail::RunCanvasCommands();
    if (g_passes > 0 && g_drawnCommands == detail::g_frame.commands.size()) return;
    gpu::PassDesc pass;
    pass.colour.texture = g_target;
    pass.colour.load = g_passes == 0 ? gpu::Load::Clear : gpu::Load::Keep;
    const float clear[4] = {40.0f / 255.0f, 50.0f / 255.0f, 60.0f / 255.0f, 1.0f};
    std::copy(std::begin(clear), std::end(clear), pass.colour.clear);
    pass.depth = {g_targetDepth, 0, g_passes == 0 ? gpu::Load::Clear : gpu::Load::Keep, 1.0f, true};
    if (!gpu::BeginPass(pass)) return;
    for (std::size_t k = g_drawnCommands; k < detail::g_frame.commands.size(); ++k) {
        const detail::Command& c = detail::g_frame.commands[k];
        if (c.kind == detail::Command::Canvas)
            detail::DrawCanvasBatch(detail::g_frame.canvasBatches[static_cast<std::size_t>(c.index)], g_targetWidth, g_targetHeight);
    }
    gpu::EndPass();
    ++g_passes;
    g_drawnCommands = detail::g_frame.commands.size();
    detail::CanvasBlitsDrawn();
}

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
FrameStats g_stats;   // render_buffers.cpp's counts (the canvases' vertex stream)
const gpu::Texture& FrameTexture() { return g_frameTexture; }
bool FrameSize(int* width, int* height) {
    *width = g_frameWidth;
    *height = g_frameHeight;
    return static_cast<bool>(g_frameTexture);
}
int PassSampleCount() { return 1; }
std::uint32_t CommitNumber() { return g_commits; }
std::uint32_t FrameNumber() { return g_commits; }
void SplitFrame() {
    ++g_splits;
    if (g_target) DrawTestFrame();   // before CheckFrame there is no frame: "between frames"
}
void RunCanvasesNow() { RunCanvasCommands(); }
} // namespace detail
} // namespace render

namespace {

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

// A canvas read back, each pixel packed in the surface format (empty when it cannot be read).
std::vector<std::uint32_t> ReadPacked(const void* holder, const render::PixelFormat& f) {
    std::vector<std::uint32_t> pixels;
    if (!ui::ReadPixels(holder, &pixels)) return {};
    for (std::uint32_t& p : pixels) p = ui::Pack(p, f);
    return pixels;
}

// ---- gdi: fills, frames, lines, pixels against GDI ------------------------------------------------------------

struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    const std::uint8_t* bits = nullptr;
    int pitch = 0, bitsPerPixel = 0;
    Dib(int width, int height, const render::PixelFormat& f) {
        struct {
            BITMAPINFOHEADER header;
            DWORD masks[3];
        } info{};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = 1;
        info.header.biBitCount = static_cast<WORD>(f.bitsPerPixel);
        info.header.biCompression = BI_BITFIELDS;
        info.masks[0] = f.redMask;
        info.masks[1] = f.greenMask;
        info.masks[2] = f.blueMask;
        void* memory = nullptr;
        bitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &memory, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        old = SelectObject(dc, bitmap);
        bits = static_cast<const std::uint8_t*>(memory);
        pitch = (width * f.bitsPerPixel / 8 + 3) & ~3;
        bitsPerPixel = f.bitsPerPixel;
    }
    std::uint32_t At(int x, int y) const {
        const std::uint8_t* row = bits + static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch);
        if (bitsPerPixel == 16) return reinterpret_cast<const std::uint16_t*>(row)[x];
        return reinterpret_cast<const std::uint32_t*>(row)[x];
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

// `count` random shapes drawn by GDI and by the canvas: the pixels that differ.
int CompareWithGdi(const Format& format, Shape shape, int count, unsigned seed, std::string* example) {
    const int w = 61, h = 47;
    const void* holder = &format;   // any address
    ui::Canvas(holder, w, h, format.format);
    Dib dib(w, h, format.format);
    std::mt19937 random(seed);
    auto coord = [&](int size) { return static_cast<int>(random() % static_cast<unsigned>(size + 8)) - 4; };
    auto colour = [&]() { return static_cast<COLORREF>(random() & 0xFFFFFF); };
    for (int i = 0; i < count; ++i) {
        COLORREF c = colour();
        switch (shape) {
        case Shape::Fill:
        case Shape::Frame: {
            int x0 = coord(w), x1 = coord(w), y0 = coord(h), y1 = coord(h);
            RECT r{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
            // GDI's FrameRect draws odd shapes for an empty rectangle (not the game's; the canvas draws none).
            if (shape == Shape::Frame && (r.right == r.left || r.bottom == r.top)) ++r.right, ++r.bottom;
            if (shape == Shape::Fill) {
                SetBkColor(dib.dc, c);
                ExtTextOutA(dib.dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);   // CDC::FillSolidRect
                ui::Fill(holder, Rect{r.left, r.top, r.right, r.bottom}, c);
            } else {
                if (g_verbose) std::printf("    frame (%ld, %ld)-(%ld, %ld)\n", r.left, r.top, r.right, r.bottom);
                HBRUSH brush = CreateSolidBrush(c);
                FrameRect(dib.dc, &r, brush);
                DeleteObject(brush);
                ui::FrameRect(holder, Rect{r.left, r.top, r.right, r.bottom}, c);
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
            if (g_verbose) std::printf("    line %d px (%d, %d)-(%d, %d)\n", width, x0, y0, x1, y1);
            HPEN pen = CreatePen(PS_SOLID, width, c);
            HGDIOBJ old = SelectObject(dib.dc, pen);
            MoveToEx(dib.dc, x0, y0, nullptr);
            LineTo(dib.dc, x1, y1);
            SelectObject(dib.dc, old);
            DeleteObject(pen);
            ui::Line(holder, x0, y0, x1, y1, width, c);
            break;
        }
        case Shape::Pixel: {
            int x = coord(w), y = coord(h);
            SetPixel(dib.dc, x, y, c);
            ui::Pixel(holder, x, y, c);
            break;
        }
        }
    }
    GdiFlush();
    const std::vector<std::uint32_t> ours = ReadPacked(holder, format.format);
    if (ours.empty()) return -1;
    auto canvasAt = [&](int x, int y) { return ours[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)]; };
    int differ = 0;
    if (g_verbose && count == 1) {   // where they differ: G drawn by GDI only, C by the canvas only, X both, differently
        for (int y = 0; y < h; ++y) {
            std::string row;
            for (int x = 0; x < w; ++x) {
                std::uint32_t a = dib.At(x, y), b = canvasAt(x, y);
                row += a == b ? (a ? '#' : '.') : !b ? 'G' : !a ? 'C' : 'X';
            }
            if (row.find_first_not_of('.') != std::string::npos) std::printf("    %2d %s\n", y, row.c_str());
        }
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint32_t a = dib.At(x, y), b = canvasAt(x, y);
            if (a != b && ++differ == 1 && example) {
                char buffer[160];
                std::snprintf(buffer, sizeof buffer, "first at (%d, %d): GDI %08X, canvas %08X", x, y, a, b);
                *example = buffer;
            }
        }
    ui::DropCanvas(holder);
    return differ;
}

void CheckGdi() {
    if (g_verbose) std::printf("gdi:\n");
    for (const Format& f : kFormats) {
        for (Shape s : {Shape::Fill, Shape::Frame, Shape::Line1, Shape::Line2, Shape::Pixel}) {
            // One shape at a time (a difference shows which), and many on one canvas.
            int single = 0;
            std::string example;
            for (unsigned seed = 1; seed <= 60 && single == 0; ++seed) single = CompareWithGdi(f, s, 1, seed * 7919u, &example);
            int many = single == 0 ? CompareWithGdi(f, s, 300, 12345u, &example) : 0;
            Check(single == 0 && many == 0,
                  std::string(f.name) + " " + ShapeName(s) + ": " + std::to_string(single + many) + " pixels differ from GDI" +
                      (example.empty() ? "" : " (" + example + ")"));
        }
        std::string example;
        int diagonal = CompareWithGdi(f, Shape::Diagonal, 100, 777u, &example);
        std::printf("  report %s %s: %d of %d pixels differ from GDI (not gated)\n", f.name, ShapeName(Shape::Diagonal), diagonal, 61 * 47);
    }
    Commit();
}

// ---- grey: 005FE440 -------------------------------------------------------------------------------------------

std::uint32_t GreyPacked(std::uint32_t pixel, const render::PixelFormat& f) {
    if (f.bitsPerPixel == 16 && f.greenMask == 0x7E0) {
        std::uint32_t grey = (((pixel >> 4) & 0x7E) + ((pixel >> 10) & 0x3E) + (pixel & 0x1F)) >> 4;
        return ((grey << 5 | grey) << 6) | grey;
    }
    if (f.bitsPerPixel == 16) {
        std::uint32_t grey = (((pixel >> 3) & 0x7C) + ((pixel >> 9) & 0x3E) + (pixel & 0x1F)) >> 4;
        return ((grey << 5 | grey) << 5) | grey;
    }
    std::uint32_t grey = (((pixel >> 6) & 0x3FC) + ((pixel >> 15) & 0x1FE) + (pixel & 0xFF)) >> 4;
    return ((grey << 8 | grey) << 8) | grey;
}

void CheckGrey() {
    if (g_verbose) std::printf("grey:\n");
    for (const Format& f : kFormats) {
        const int w = 70, h = 50;
        const void* holder = &f.format;
        ui::Canvas(holder, w, h, f.format);
        std::mt19937 random(99);
        for (int i = 0; i < 80; ++i) {
            int x = static_cast<int>(random() % w), y = static_cast<int>(random() % h);
            ui::Fill(holder, Rect{x, y, x + 1 + static_cast<int>(random() % 20), y + 1 + static_cast<int>(random() % 15)},
                     static_cast<COLORREF>(random() & 0xFFFFFF));
        }
        const std::vector<std::uint32_t> before = ReadPacked(holder, f.format);
        std::vector<std::uint32_t> expected;
        for (std::uint32_t pixel : before) expected.push_back(GreyPacked(pixel, f.format));
        ui::Greyscale(holder);
        const std::vector<std::uint32_t> after = ReadPacked(holder, f.format);
        int differ = 0;
        for (std::size_t i = 0; i < after.size() && i < expected.size(); ++i) differ += after[i] != expected[i];
        Check(!before.empty() && after.size() == before.size() && differ == 0, std::string(f.name) + " greyscale: " + std::to_string(differ) + " pixels differ from 005FE440's");
        ui::DropCanvas(holder);
    }
    Commit();
}

// ---- image: Image ----------------------------------------------------------------------------------------------

void CheckImage() {
    if (g_verbose) std::printf("image:\n");
    for (const Format& f : kFormats) {
        const int w = 37, h = 29;
        const void* holder = &f.name;
        ui::Canvas(holder, w, h, f.format);
        ui::Fill(holder, Rect{0, 0, w, h}, RGB(30, 60, 90));
        // A picture larger than the canvas, placed partly outside it: only the part inside is written.
        const int pw = 30, ph = 20;
        std::mt19937 random(7);
        std::vector<std::uint32_t> picture(static_cast<std::size_t>(pw) * ph);
        for (std::uint32_t& p : picture) p = random() & 0xFFFFFF;
        const Rect at{20, -5, 20 + pw, -5 + ph};
        ui::Image(holder, at, picture.data(), pw);
        std::vector<std::uint32_t> expected(static_cast<std::size_t>(w) * h, RefQuantise(0x1E3C5A, f.format));
        for (int y = std::max(0, at.top); y < std::min(h, at.bottom); ++y)
            for (int x = std::max(0, at.left); x < std::min(w, at.right); ++x)
                expected[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                    RefQuantise(picture[static_cast<std::size_t>(y - at.top) * pw + static_cast<std::size_t>(x - at.left)], f.format);
        std::vector<std::uint32_t> got;
        std::string example;
        int differ = ui::ReadPixels(holder, &got) ? Differ(expected, got, w, &example) : -1;
        Check(differ == 0, std::string(f.name) + " image, clipped: " + std::to_string(differ) + " pixels differ" + (example.empty() ? "" : " (" + example + ")"));
        ui::DropCanvas(holder);
    }
    Commit();
}

// ---- copy: CopyFrameTo's screen copy ----------------------------------------------------------------------------

std::vector<std::uint8_t> g_frameRgba;

void MakeFrameTexture() {
    g_frameWidth = 200;
    g_frameHeight = 150;
    std::mt19937 random(4242);
    g_frameRgba.resize(static_cast<std::size_t>(g_frameWidth) * g_frameHeight * 4);
    for (std::size_t i = 0; i < g_frameRgba.size(); ++i) g_frameRgba[i] = static_cast<std::uint8_t>(i % 4 == 3 ? 255 : random() & 0xFF);
    g_frameTexture = gpu::MakeTexture({g_frameWidth, g_frameHeight, 1, 1, gpu::Format::Rgba8, gpu::kSampled, "ui-check-frame"});
    gpu::Write(g_frameTexture, 0, Rect{0, 0, g_frameWidth, g_frameHeight}, g_frameRgba.data(),
               static_cast<std::size_t>(g_frameWidth) * 4, gpu::Contents::Discard);
}

void CheckCopy() {
    if (g_verbose) std::printf("copy:\n");
    MakeFrameTexture();
    struct Case {
        Rect source;
        int width, height;
    };
    const Case cases[] = {{{0, 0, 200, 150}, 200, 150}, {{10, 20, 110, 95}, 256, 192}, {{150, 100, 260, 190}, 64, 48}, {{-20, -10, 37, 41}, 33, 77}};
    for (const Format& f : kFormats) {
        for (const Case& c : cases) {
            const void* holder = &c;
            ui::Canvas(holder, c.width, c.height, f.format);
            ui::CopyFrame(holder, c.source);
            std::vector<std::uint32_t> pixels;
            const bool got = ui::ReadPixels(holder, &pixels);
            Rect r{std::max(c.source.left, 0), std::max(c.source.top, 0), std::min(c.source.right, g_frameWidth), std::min(c.source.bottom, g_frameHeight)};
            int sw = r.right - r.left, sh = r.bottom - r.top, differ = 0;
            for (int y = 0; y < c.height && got; ++y)
                for (int x = 0; x < c.width; ++x) {
                    int sx = r.left + x * sw / c.width, sy = r.top + y * sh / c.height;
                    const std::uint8_t* p = &g_frameRgba[(static_cast<std::size_t>(sy) * g_frameWidth + static_cast<std::size_t>(sx)) * 4];
                    std::uint32_t rgb = (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
                    differ += ui::Pack(pixels[static_cast<std::size_t>(y) * c.width + static_cast<std::size_t>(x)], f.format) !=
                              ui::Pack(rgb, f.format);
                }
            Check(got && differ == 0, std::string(f.name) + " copy of (" + std::to_string(c.source.left) + ", " + std::to_string(c.source.top) + ")-(" +
                                          std::to_string(c.source.right) + ", " + std::to_string(c.source.bottom) + ") into " + std::to_string(c.width) +
                                          "x" + std::to_string(c.height) + ": " + std::to_string(differ) + " pixels differ");
            ui::DropCanvas(holder);
        }
    }
    // CopyFrame draws the frame so far first (SplitFrame).
    Check(g_splits == 12, "every copy drew the frame so far first: " + std::to_string(g_splits) + " splits for 12 copies");
    Commit();
}

// ---- picture: the save's 5:6:5 picture, one-pixel reads, moved canvases -----------------------------------------

void CheckPicture() {
    if (g_verbose) std::printf("picture:\n");
    struct Case {
        int canvasWidth, canvasHeight, width, height;
    };
    const Case cases[] = {{800, 600, 256, 192}, {1920, 1080, 256, 192}, {37, 29, 256, 192}, {200, 150, 64, 48}};
    for (const Format& f : kFormats) {
        for (const Case& c : cases) {
            const void* holder = &c;
            ui::Canvas(holder, c.canvasWidth, c.canvasHeight, f.format);
            std::mt19937 random(99);
            std::vector<std::uint32_t> picture(static_cast<std::size_t>(c.canvasWidth) * c.canvasHeight);
            for (std::uint32_t& px : picture) px = random() & 0xFFFFFF;
            ui::Image(holder, Rect{0, 0, c.canvasWidth, c.canvasHeight}, picture.data(), c.canvasWidth);
            std::vector<std::uint32_t> held;
            std::vector<std::uint16_t> got(static_cast<std::size_t>(c.width) * c.height, 0xFFFF);
            bool read = ui::ReadPixels(holder, &held) && ui::ReadRgb565(holder, c.width, c.height, got.data());
            int differ = read ? 0 : -1;
            for (int y = 0; y < c.height && read; ++y)
                for (int x = 0; x < c.width; ++x) {
                    int sx = (2 * x + 1) * c.canvasWidth / (2 * c.width), sy = (2 * y + 1) * c.canvasHeight / (2 * c.height);
                    std::uint32_t rgb = held[static_cast<std::size_t>(sy) * c.canvasWidth + static_cast<std::size_t>(sx)];
                    std::uint32_t packed = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x7E0) | ((rgb >> 3) & 0x1F);
                    differ += got[static_cast<std::size_t>(y) * c.width + static_cast<std::size_t>(x)] != packed;
                }
            Check(differ == 0, std::string(f.name) + " 5:6:5 picture of a " + std::to_string(c.canvasWidth) + "x" + std::to_string(c.canvasHeight) +
                                   " canvas at " + std::to_string(c.width) + "x" + std::to_string(c.height) + ": " + std::to_string(differ) + " pixels differ");
            int wrong = read ? 0 : -1;
            const int points[][2] = {{0, 0}, {c.canvasWidth - 1, c.canvasHeight - 1}, {c.canvasWidth / 3, c.canvasHeight / 2}};
            for (const auto& pt : points) {
                std::uint32_t one = 0;
                wrong += !read || !ui::ReadPixel(holder, pt[0], pt[1], &one) ||
                         one != held[static_cast<std::size_t>(pt[1]) * c.canvasWidth + static_cast<std::size_t>(pt[0])] ||
                         ui::TopBits(one, f.format) != RefTopBits(one, f.format);
            }
            std::uint32_t outside = 0;
            Check(wrong == 0 && !ui::ReadPixel(holder, c.canvasWidth, 0, &outside),
                  std::string(f.name) + " one-pixel reads of the " + std::to_string(c.canvasWidth) + "x" + std::to_string(c.canvasHeight) + " canvas");
            ui::DropCanvas(holder);
        }
    }
    // A canvas moved with a fill still queued: the fill lands on the canvas under its new key.
    int from = 0, to = 0;
    ui::Canvas(&from, 16, 8, kFormats[2].format);
    ui::Fill(&from, Rect{0, 0, 16, 8}, RGB(10, 20, 30));
    ui::MoveCanvas(&from, &to);
    ui::Fill(&to, Rect{0, 0, 8, 8}, RGB(200, 100, 50));
    std::vector<std::uint32_t> moved;
    bool ok = !ui::HasCanvas(&from) && ui::ReadPixels(&to, &moved) && moved.size() == 128 && moved[0] == 0xC86432 && moved[15] == 0x0A141E;
    Check(ok, "a canvas moved with its queued fill");
    ui::DropCanvas(&to);
    Commit();
}

// ---- resize: a surface's canvas placed again for another screen size --------------------------------------------

// A canvas with a picture and a fill still queued, resized (larger, smaller, another shape): each pixel the one under
// it at the old size (x * old width / new width, CopyFrame's rule), the fill drawn at the old size first; then a
// fill at the new size lands where it is asked to.
void CheckResize() {
    if (g_verbose) std::printf("resize:\n");
    struct Case {
        int width, height, newWidth, newHeight;
    };
    const Case cases[] = {{80, 60, 128, 72}, {128, 72, 100, 80}, {37, 29, 74, 58}, {200, 150, 50, 75}};
    for (const Format& f : kFormats) {
        for (const Case& c : cases) {
            const void* holder = &c;
            ui::Canvas(holder, c.width, c.height, f.format);
            std::mt19937 random(11);
            std::vector<std::uint32_t> picture(static_cast<std::size_t>(c.width) * c.height);
            for (std::uint32_t& px : picture) px = random() & 0xFFFFFF;
            ui::Image(holder, Rect{0, 0, c.width, c.height}, picture.data(), c.width);
            ui::Fill(holder, Rect{0, 0, c.width / 2, c.height / 2}, RGB(10, 200, 30));
            std::vector<std::uint32_t> before;
            const bool readBefore = ui::ReadPixels(holder, &before);
            // Queued when the canvas is resized: drawn at the old size first.
            ui::Fill(holder, Rect{c.width / 2, 0, c.width, c.height}, RGB(250, 40, 90));
            for (int y = 0; y < c.height && readBefore; ++y)
                for (int x = c.width / 2; x < c.width; ++x)
                    before[static_cast<std::size_t>(y) * c.width + static_cast<std::size_t>(x)] = RefQuantise(0xFA285A, f.format);
            ui::Resize(holder, c.newWidth, c.newHeight);
            std::vector<std::uint32_t> after;
            int width = 0, height = 0;
            const bool read = readBefore && ui::ReadPixels(holder, &after, &width, &height) && width == c.newWidth && height == c.newHeight;
            int differ = read ? 0 : -1;
            for (int y = 0; y < c.newHeight && read; ++y)
                for (int x = 0; x < c.newWidth; ++x) {
                    const int sx = x * c.width / c.newWidth, sy = y * c.height / c.newHeight;
                    differ += after[static_cast<std::size_t>(y) * c.newWidth + static_cast<std::size_t>(x)] !=
                              before[static_cast<std::size_t>(sy) * c.width + static_cast<std::size_t>(sx)];
                }
            ui::Fill(holder, Rect{c.newWidth - 1, c.newHeight - 1, c.newWidth, c.newHeight}, RGB(255, 255, 255));
            std::uint32_t corner = 0;
            const bool filled = ui::ReadPixel(holder, c.newWidth - 1, c.newHeight - 1, &corner) && corner == 0xFFFFFF;
            Check(differ == 0 && filled, std::string(f.name) + " " + std::to_string(c.width) + "x" + std::to_string(c.height) + " canvas resized to " +
                                             std::to_string(c.newWidth) + "x" + std::to_string(c.newHeight) + ": " + std::to_string(differ) +
                                             " pixels differ" + (filled ? "" : ", the fill at the new size missing"));
            ui::DropCanvas(holder);
        }
    }
    Commit();
}

// ---- frame: canvases in the frame, the pass split ---------------------------------------------------------------

bool ReadTarget(std::vector<std::uint32_t>* rgb) {
    const int w = g_targetWidth, h = g_targetHeight;
    gpu::Ticket ticket = gpu::Download(g_target, 0, Rect{0, 0, w, h});
    gpu::Texels texels;
    const bool read = ticket && gpu::Wait(ticket) && gpu::Read(ticket, &texels);
    if (read) {
        rgb->resize(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* row = texels.Row(y);
            for (int x = 0; x < w; ++x)
                (*rgb)[static_cast<std::size_t>(y) * w + x] = (static_cast<std::uint32_t>(row[x * 4]) << 16) |
                                                              (static_cast<std::uint32_t>(row[x * 4 + 1]) << 8) |
                                                              row[x * 4 + 2];
        }
    }
    gpu::Release(ticket);
    return read;
}

// The blit of `canvas` (its pixels, `cw` wide) onto `target` as ps_blit draws it (exact ratios only).
void RefBlit(std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& canvas, int cw, int ch, const Rect& dst, const Rect& src,
             bool keyed, std::uint32_t keyRgb) {
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top, sw = src.right - src.left, sh = src.bottom - src.top;
    for (int y = std::max(dst.top, 0); y < std::min(dst.bottom, g_targetHeight); ++y)
        for (int x = std::max(dst.left, 0); x < std::min(dst.right, g_targetWidth); ++x) {
            int tx = src.left + ((2 * (x - dst.left) + 1) * sw) / (2 * dw), ty = src.top + ((2 * (y - dst.top) + 1) * sh) / (2 * dh);
            tx = std::clamp(tx, 0, cw - 1);
            ty = std::clamp(ty, 0, ch - 1);
            std::uint32_t c = canvas[static_cast<std::size_t>(ty) * cw + static_cast<std::size_t>(tx)];
            if (keyed && c == keyRgb) continue;
            target[static_cast<std::size_t>(y) * g_targetWidth + static_cast<std::size_t>(x)] = c;
        }
}

void CheckFrame() {
    if (g_verbose) std::printf("frame:\n");
    g_target = gpu::MakeTexture({g_targetWidth, g_targetHeight, 1, 1, detail::kColourFormat, gpu::kColourTarget, "ui-check-target"});
    g_targetDepth = gpu::MakeTexture({g_targetWidth, g_targetHeight, 1, 1, detail::kDepthFormat, gpu::kDepthTarget, "ui-check-depth"});

    std::vector<std::uint32_t> expected(static_cast<std::size_t>(g_targetWidth) * g_targetHeight, kTargetClearRgb);
    const render::PixelFormat f565 = kFormats[0].format;
    int a = 0, b = 0;
    const void* first = &a;
    const void* second = &b;
    // A 5:6:5 canvas: black (the key) with a red block and a blue frame.
    ui::Canvas(first, 32, 24, f565);
    ui::Fill(first, Rect{4, 4, 20, 16}, RGB(250, 10, 10));
    ui::FrameRect(first, Rect{0, 0, 32, 24}, RGB(10, 10, 250));
    std::vector<std::uint32_t> firstPixels;
    Check(ui::ReadPixels(first, &firstPixels), "first canvas read");
    int splitsBefore = g_splits;
    // Blitted keyed at 1:1, then plain from a part at 2:1, then drawn into: the frame so far is drawn
    // first, so both blits show it as it was.
    ui::DrawCanvas(first, Rect{5, 5, 37, 29}, nullptr, true, 0);
    RefBlit(expected, firstPixels, 32, 24, Rect{5, 5, 37, 29}, Rect{0, 0, 32, 24}, true, 0);
    ui::DrawCanvas(first, Rect{40, 2, 72, 26}, nullptr, false, 0);
    RefBlit(expected, firstPixels, 32, 24, Rect{40, 2, 72, 26}, Rect{0, 0, 32, 24}, false, 0);
    const Rect part{0, 0, 16, 16};
    ui::DrawCanvas(first, Rect{80, 0, 112, 32}, &part, false, 0);
    RefBlit(expected, firstPixels, 32, 24, Rect{80, 0, 112, 32}, Rect{0, 0, 16, 16}, false, 0);
    Check(g_splits == splitsBefore, "blits recorded without a split");
    ui::Fill(first, Rect{0, 0, 32, 24}, RGB(0, 200, 0));
    Check(g_splits == splitsBefore + 1, "drawing into a canvas blitted in the frame splits the frame once");
    ui::Fill(first, Rect{0, 0, 8, 8}, RGB(0, 0, 0));
    Check(g_splits == splitsBefore + 1, "and only once until it is blitted again");
    // Drawn again after the change, keyed (the key a 5:6:5 pixel: black), stretched 1:2.
    std::vector<std::uint32_t> changed;
    Check(ui::ReadPixels(first, &changed), "first canvas read again");
    ui::DrawCanvas(first, Rect{10, 40, 74, 88}, nullptr, true, 0);
    RefBlit(expected, changed, 32, 24, Rect{10, 40, 74, 88}, Rect{0, 0, 32, 24}, true, 0);
    // A second canvas made and drawn in the same frame, before its blit (hoisted, no split), keyed on
    // its fill colour (punched out).
    const render::PixelFormat f888 = kFormats[2].format;
    ui::Canvas(second, 40, 30, f888);
    ui::Fill(second, Rect{0, 0, 40, 30}, RGB(1, 2, 3));
    ui::Fill(second, Rect{10, 10, 30, 20}, RGB(200, 200, 200));
    std::vector<std::uint32_t> secondPixels(40 * 30, 0x010203);
    for (int y = 10; y < 20; ++y)
        for (int x = 10; x < 30; ++x) secondPixels[static_cast<std::size_t>(y) * 40 + static_cast<std::size_t>(x)] = 0xC8C8C8;
    int splits = g_splits;
    ui::DrawCanvas(second, Rect{100, 60, 140, 90}, nullptr, true, 0x010203);
    RefBlit(expected, secondPixels, 40, 30, Rect{100, 60, 140, 90}, Rect{0, 0, 40, 30}, true, 0x010203);
    Check(g_splits == splits, "a canvas drawn before its first blit needs no split");
    DrawTestFrame();
    std::vector<std::uint32_t> got;
    std::string example;
    int differ = ReadTarget(&got) ? Differ(expected, got, g_targetWidth, &example) : -1;
    Check(differ == 0, "frame with five canvas blits: " + std::to_string(differ) + " pixels differ" + (example.empty() ? "" : " (" + example + ")"));
    // A canvas dropped with commands queued, and its blit, leave nothing behind.
    ui::Fill(second, Rect{0, 0, 5, 5}, RGB(9, 9, 9));
    ui::DropCanvas(second);
    ui::DropCanvas(first);
    detail::RunCanvasCommands();
    Check(!ui::HasCanvas(first) && !ui::HasCanvas(second), "dropped canvases are gone");
    // A canvas dropped while its blit is not drawn yet (SwapContents putting away a backdrop the frame blitted):
    // the frame so far is drawn first, so the blit shows it.
    ui::Canvas(first, 16, 16, f888);
    ui::Fill(first, Rect{0, 0, 16, 16}, RGB(30, 60, 90));
    ui::DrawCanvas(first, Rect{120, 96, 136, 112}, nullptr, false, 0);
    RefBlit(expected, std::vector<std::uint32_t>(16 * 16, 0x1E3C5A), 16, 16, Rect{120, 96, 136, 112}, Rect{0, 0, 16, 16}, false, 0);
    splits = g_splits;
    ui::DropCanvas(first);
    Check(g_splits == splits + 1 && !ui::HasCanvas(first), "dropping a canvas with its blit pending splits the frame first");
    differ = ReadTarget(&got) ? Differ(expected, got, g_targetWidth, &example) : -1;
    Check(differ == 0, "and the dropped canvas's blit is drawn: " + std::to_string(differ) + " pixels differ" + (example.empty() ? "" : " (" + example + ")"));
    Commit();
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
    Commit();
    CheckGrey();
    CheckImage();
    CheckCopy();
    CheckPicture();
    CheckResize();
    CheckFrame();
    gpu::Release(g_frameTexture);
    gpu::Release(g_target);
    gpu::Release(g_targetDepth);
    gpu::Stop();
    std::printf("ui_check: %d checks, %d failed, %d log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
