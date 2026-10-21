// Offline check of the interface canvases (src/render/render_ui, ticket N1; docs/notes/ui-native.md).
//
//   ei_ui_check [--hardware] [--verbose]
//
// Runs render_ui's commands on a Direct3D 11 WARP device (--hardware: the default adapter) without the
// game, playing render.cpp's part itself (the frame, its target, SplitFrame, RunCanvasesNow), and
// compares every canvas pixel exactly:
//   gdi     fills, frames, lines (1 and 2 px, horizontal and vertical) and pixels against GDI drawing the
//           same into a DIB section of each surface format (5:6:5, 5:5:5, 8:8:8); diagonal lines are
//           reported, not gated (Bresenham's steps, not GDI's).
//   glyphs  glyph runs (render_text layouts, underlines, clipping, over a coloured background, the shadow
//           pass under the text) against the CPU rule, and over black against text::ComposeAgainstBlack.
//   grey    greying against 005FE440's formulas on the packed pixels.
//   copy    screen copies of a frame texture against CopyFrameTo's stretch and packing.
//   image   pixels written into canvases (clipped, quantised) exactly; the switch's words and paths; the
//           comparison's view (versions, the glyph runs' rectangles).
//   frame   canvases blitted in the frame (keyed, stretched, a source part) against the window blit's
//           rule, and the pass split: drawing into a canvas blitted and not drawn yet draws the frame
//           first, so the blit shows the canvas as it was; the blits' rectangles kept for the UI mask.
// Any sokol error or warning and any ei::Log line fails the check. Exit code 0 when all pass.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include "render/render_internal.h"
#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using render::Rect;
using render::SurfaceMemory;
namespace ui = render::ui;
namespace text = render::text;
namespace detail = render::detail;

namespace {

bool g_verbose = false;
int g_problems = 0;   // sokol messages and ei::Log lines

// ---- The host: what render.cpp gives render_ui.cpp -----------------------------------------------------

detail::Frame g_frame;
std::unordered_map<const void*, SurfaceMemory> g_memories;
int g_written = 0;
sg_view g_frameTexture{};   // CopyFrame's source
int g_frameWidth = 0, g_frameHeight = 0;
int g_splits = 0;

// The frame's target for the blits (with depth: the canvas blit's pipeline is the frame pass's).
sg_image g_target{}, g_targetDepth{};
sg_view g_targetColour{}, g_targetDepthView{};
int g_targetWidth = 160, g_targetHeight = 120;
std::size_t g_drawnCommands = 0;
int g_passes = 0;
const sg_color kTargetClear{40.0f / 255.0f, 50.0f / 255.0f, 60.0f / 255.0f, 1.0f};
constexpr std::uint32_t kTargetClearRgb = 0x28323C;

// render.cpp's DrawPending: the canvases' queue first, then the frame's commands not drawn yet.
void DrawTestFrame() {
    detail::RunCanvasCommands();
    if (g_passes > 0 && g_drawnCommands == g_frame.commands.size()) return;
    sg_pass pass{};
    pass.action.colors[0].load_action = g_passes == 0 ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = kTargetClear;
    pass.action.depth.load_action = g_passes == 0 ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = g_targetColour;
    pass.attachments.depth_stencil = g_targetDepthView;
    sg_begin_pass(&pass);
    for (std::size_t k = g_drawnCommands; k < g_frame.commands.size(); ++k) {
        const detail::Command& c = g_frame.commands[k];
        if (c.kind == detail::Command::Canvas)
            detail::DrawCanvasBatch(g_frame.canvasBatches[static_cast<std::size_t>(c.index)], g_targetWidth, g_targetHeight);
    }
    sg_end_pass();
    ++g_passes;
    g_drawnCommands = g_frame.commands.size();
    detail::CanvasBlitsDrawn();
}

void LogLine(const char* prefix, const char* text) {
    ++g_problems;
    std::printf("  %s%s\n", prefix, text);
}

void SokolLog(const char* tag, std::uint32_t level, std::uint32_t item, const char* message, std::uint32_t line, const char*, void*) {
    char buffer[1024];
    std::snprintf(buffer, sizeof buffer, "%s level %u item %u (line %u): %s", tag ? tag : "sg", level, item, line, message ? message : "");
    // sokol's note that a feature level 11.0 device has eight UAV slots (none are used here) is not a problem.
    if (level <= 2 && item != SG_LOGITEM_D3D11_FEATURE_LEVEL_0_DETECTED) LogLine("sokol: ", buffer);
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
const SurfaceMemory* SurfaceMemoryOf(const void* holder) {
    auto it = g_memories.find(holder);
    return it == g_memories.end() ? nullptr : &it->second;
}
void SurfaceWritten(const void*, const Rect*) { ++g_written; }
namespace detail {
Frame& CurrentFrame() { return g_frame; }
sg_view FrameTexture() { return g_frameTexture; }
bool FrameSize(int* width, int* height) {
    *width = g_frameWidth;
    *height = g_frameHeight;
    return g_frameTexture.id != 0;
}
int PassSampleCount() { return 1; }
void SplitFrame() {
    ++g_splits;
    if (g_target.id) DrawTestFrame();   // before CheckFrame there is no frame: "between frames"
}
void RunCanvasesNow() { RunCanvasCommands(); }
FpuGuard::FpuGuard() : saved_(0) {}
FpuGuard::~FpuGuard() {}
} // namespace detail
} // namespace render

namespace {

// ---- Checks -------------------------------------------------------------------------------------------------

struct Format {
    const char* name;
    ui::PixelFormat format;
};
const Format kFormats[] = {
    {"5:6:5", {16, 0xF800, 0x7E0, 0x1F}},
    {"5:5:5", {16, 0x7C00, 0x3E0, 0x1F}},
    {"8:8:8", {32, 0xFF0000, 0xFF00, 0xFF}},
};

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

// A surface's memory for ReadBack, as NewSurfaceMemory makes it (top-down, rows 4-byte aligned).
struct Memory {
    std::vector<std::uint8_t> bytes;
    SurfaceMemory memory;
};
std::unordered_map<const void*, Memory> g_memoryStore;

void AttachMemory(const void* holder, int width, int height, const ui::PixelFormat& f) {
    Memory& m = g_memoryStore[holder];
    int pitch = (width * f.bitsPerPixel / 8 + 3) & ~3;
    m.bytes.assign(static_cast<std::size_t>(pitch) * static_cast<std::size_t>(height), 0);
    m.memory = SurfaceMemory{m.bytes.data(), nullptr, pitch, width, height, f.bitsPerPixel, f.redMask, f.greenMask, f.blueMask, 0};
    g_memories[holder] = m.memory;
}

std::uint32_t PixelOf(const SurfaceMemory& m, int x, int y) {
    const auto* row = static_cast<const std::uint8_t*>(m.bits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(m.pitch);
    if (m.bitsPerPixel == 16) return reinterpret_cast<const std::uint16_t*>(row)[x];
    return reinterpret_cast<const std::uint32_t*>(row)[x];
}

// The canvas read back packed into its memory.
const SurfaceMemory* ReadPacked(const void* holder) {
    if (!ui::ReadBack(holder)) return nullptr;
    return &g_memories[holder];
}

// The test's own quantising (independent of render_ui's): top bits, repeated.
std::uint32_t RefChannel(std::uint32_t v, int bits) {
    if (bits >= 8) return v;
    std::uint32_t top = v >> (8 - bits), out = 0;
    for (int filled = 0; filled < 8; filled += bits) out |= (top << (8 - bits)) >> filled;
    return out & 0xFF;
}
struct Bits {
    int r, g, b;
};
Bits BitsOf(const ui::PixelFormat& f) {
    if (f.bitsPerPixel == 32) return {8, 8, 8};
    return f.greenMask == 0x7E0 ? Bits{5, 6, 5} : Bits{5, 5, 5};
}
std::uint32_t RefQuantise(std::uint32_t rgb, const ui::PixelFormat& f) {
    Bits b = BitsOf(f);
    return (RefChannel((rgb >> 16) & 0xFF, b.r) << 16) | (RefChannel((rgb >> 8) & 0xFF, b.g) << 8) | RefChannel(rgb & 0xFF, b.b);
}
std::uint32_t RgbOfColorref(COLORREF c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }

// ---- gdi: fills, frames, lines, pixels against GDI ------------------------------------------------------------

struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    SurfaceMemory memory{};
    Dib(int width, int height, const ui::PixelFormat& f) {
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
        void* bits = nullptr;
        bitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        old = SelectObject(dc, bitmap);
        memory = SurfaceMemory{bits, bitmap, (width * f.bitsPerPixel / 8 + 3) & ~3, width, height, f.bitsPerPixel, f.redMask, f.greenMask, f.blueMask, 0};
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
    AttachMemory(holder, w, h, format.format);
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
    const SurfaceMemory* ours = ReadPacked(holder);
    if (!ours) return -1;
    int differ = 0;
    if (g_verbose && count == 1) {   // where they differ: G drawn by GDI only, C by the canvas only, X both, differently
        for (int y = 0; y < h; ++y) {
            std::string row;
            for (int x = 0; x < w; ++x) {
                std::uint32_t a = PixelOf(dib.memory, x, y), b = PixelOf(*ours, x, y);
                row += a == b ? (a ? '#' : '.') : !b ? 'G' : !a ? 'C' : 'X';
            }
            if (row.find_first_not_of('.') != std::string::npos) std::printf("    %2d %s\n", y, row.c_str());
        }
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint32_t a = PixelOf(dib.memory, x, y), b = PixelOf(*ours, x, y);
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
    sg_commit();
}

// ---- glyphs: glyph runs against the CPU rule -----------------------------------------------------------------

// The run drawn onto `pixels` (0x00RRGGBB) by the canvas's rule.
void RefGlyphRun(std::vector<std::uint32_t>& pixels, int width, int height, text::TextFont& font, const text::TextLayout& layout,
                 COLORREF colour, const ui::PixelFormat& format) {
    int x0 = 0, y0 = 0, x1 = width, y1 = height;
    if (layout.clipped) {
        x0 = std::max(x0, layout.clip.left);
        y0 = std::max(y0, layout.clip.top);
        x1 = std::min(x1, layout.clip.right);
        y1 = std::min(y1, layout.clip.bottom);
    }
    if (x1 <= x0 || y1 <= y0) return;
    std::vector<std::uint8_t> cover(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    const text::GlyphAtlas& atlas = font.Atlas();
    for (const text::GlyphQuad& q : layout.quads)
        for (int y = std::max(y0, q.y); y < std::min(y1, q.y + q.height); ++y)
            for (int x = std::max(x0, q.x); x < std::min(x1, q.x + q.width); ++x) {
                std::uint8_t a = atlas.Pixels()[static_cast<std::size_t>(q.atlasY + y - q.y) * static_cast<std::size_t>(atlas.Width()) +
                                                static_cast<std::size_t>(q.atlasX + x - q.x)];
                std::uint8_t& c = cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                c = std::max(c, a);
            }
    for (const text::Rect& u : layout.underlines)
        for (int y = std::max(y0, u.top); y < std::min(y1, u.bottom); ++y)
            for (int x = std::max(x0, u.left); x < std::min(x1, u.right); ++x)
                cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 255;
    std::uint32_t text = RgbOfColorref(colour);
    for (std::size_t i = 0; i < cover.size(); ++i) {
        unsigned c = cover[i];
        if (!c) continue;
        std::uint32_t out = 0;
        for (int shift : {16, 8, 0}) {
            unsigned t = (text >> shift) & 0xFF, d = (pixels[i] >> shift) & 0xFF;
            out |= ((t * c + d * (255 - c) + 127) / 255) << shift;
        }
        pixels[i] = RefQuantise(out, format);
    }
}

int Differ(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int width, std::string* example) {
    int differ = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i] && ++differ == 1 && example) {
            char buffer[160];
            std::snprintf(buffer, sizeof buffer, "first at (%d, %d): expected %06X, canvas %06X", static_cast<int>(i % static_cast<std::size_t>(width)),
                          static_cast<int>(i / static_cast<std::size_t>(width)), a[i], b[i]);
            *example = buffer;
        }
    return differ + (a.size() == b.size() ? 0 : 1000000);
}

void CheckGlyphs() {
    if (g_verbose) std::printf("glyphs:\n");
    const int w = 180, h = 72;
    struct Case {
        const char* text;
        unsigned format;
        text::Rect rect;
        bool smoothing;
        int pixels;
    };
    const Case cases[] = {
        {"Hello, &world: AV Wa fi", 0, {4, 4, 176, 40}, true, 16},
        {"\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0! \xD1\xEB\xEE\xE2\xE0 \xED\xE0 \xED\xE5\xF1\xEA\xEE\xEB\xFC\xEA\xEE \xF1\xF2\xF0\xEE\xEA", text::kWordBreak | text::kCenter,
         {10, 2, 120, 70}, true, 19},
        {"Clipped text that runs past the right edge of its rectangle", text::kNoPrefix, {-6, 30, 90, 44}, true, 13},
        {"Right &aligned\r\nsecond line", text::kRight, {0, 20, 175, 71}, false, 16},
    };
    int textAreasWrong = 0;
    for (const Format& f : kFormats) {
        int caseIndex = 0;
        for (const Case& c : cases) {
            const void* holder = &c;
            text::TextFont* font = text::GetTextFont("Times New Roman", c.pixels, c.smoothing);
            if (!font) {
                Check(false, std::string("font Times New Roman ") + std::to_string(c.pixels) + " px");
                continue;
            }
            ui::Canvas(holder, w, h, f.format);
            // A background of fills (the text blends over it), the shadow then the text.
            std::vector<std::uint32_t> expected(static_cast<std::size_t>(w) * h, 0);
            const COLORREF fills[3] = {RGB(200, 30, 90), RGB(10, 140, 250), RGB(255, 255, 255)};
            for (int i = 0; i < 3; ++i) {
                Rect r{i * 60, i * 10, i * 60 + 70, h - i * 5};
                ui::Fill(holder, r, fills[i]);
                for (int y = r.top; y < r.bottom; ++y)
                    for (int x = r.left; x < std::min(r.right, w); ++x)
                        expected[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = RefQuantise(RgbOfColorref(fills[i]), f.format);
            }
            std::u32string decoded = text::DecodeCp1251(c.text);
            text::TextLayout shadow, layout;
            text::Rect shifted{c.rect.left + 1, c.rect.top + 1, c.rect.right + 1, c.rect.bottom + 1};
            bool laid = text::Layout(*font, decoded, shifted, c.format, shadow) && text::Layout(*font, decoded, c.rect, c.format, layout);
            Check(laid, std::string("layout of case ") + std::to_string(caseIndex));
            ui::GlyphRun(holder, *font, shadow, RGB(8, 8, 8));
            RefGlyphRun(expected, w, h, *font, shadow, RGB(8, 8, 8), f.format);
            const COLORREF colour = RGB(250, 220, 120 + caseIndex * 30);
            ui::GlyphRun(holder, *font, layout, colour);
            RefGlyphRun(expected, w, h, *font, layout, colour, f.format);
            // The comparison's text areas: the two runs' clip rectangles within the canvas.
            std::vector<ui::CanvasInfo> canvases;
            ui::Canvases(&canvases);
            const Rect clip{std::max(0, layout.clip.left), std::max(0, layout.clip.top), std::min(w, layout.clip.right), std::min(h, layout.clip.bottom)};
            textAreasWrong += !(canvases.size() == 1 && canvases[0].text.size() == 2 && layout.clipped &&
                                canvases[0].text[1].left == clip.left && canvases[0].text[1].top == clip.top &&
                                canvases[0].text[1].right == clip.right && canvases[0].text[1].bottom == clip.bottom);
            std::vector<std::uint32_t> got;
            std::string example;
            int differ = ui::ReadPixels(holder, &got) ? Differ(expected, got, w, &example) : -1;
            Check(differ == 0, std::string(f.name) + " glyph run, case " + std::to_string(caseIndex) + " (" + std::to_string(layout.quads.size()) +
                                   " glyphs, " + std::to_string(layout.underlines.size()) + " underlines): " + std::to_string(differ) +
                                   " pixels differ" + (example.empty() ? "" : " (" + example + ")"));
            ui::DropCanvas(holder);
            ++caseIndex;
        }
    }
    Check(textAreasWrong == 0, "glyph runs' text areas (ui::Canvases): " + std::to_string(textAreasWrong) + " cases wrong");
    // Over black in 8:8:8 the run is ComposeAgainstBlack (render_text's CPU reference).
    {
        const void* holder = &w;
        text::TextFont* font = text::GetTextFont("Times New Roman", 21, true);
        text::TextLayout layout;
        if (font && text::Layout(*font, text::DecodeCp1251("&Against black: Ww Qq \xC6\xE6"), text::Rect{3, 3, 170, 60}, text::kWordBreak, layout)) {
            ui::Canvas(holder, w, h, kFormats[2].format);
            ui::GlyphRun(holder, *font, layout, RGB(255, 200, 40));
            std::vector<std::uint32_t> composed(static_cast<std::size_t>(w) * h, 0), got;
            text::ComposeAgainstBlack(*font, layout, RGB(255, 200, 40), composed.data(), w, h, w);
            for (std::uint32_t& p : composed) p &= 0xFFFFFF;
            std::string example;
            int differ = ui::ReadPixels(holder, &got) ? Differ(composed, got, w, &example) : -1;
            Check(differ == 0, "8:8:8 glyph run over black against ComposeAgainstBlack: " + std::to_string(differ) + " pixels differ" +
                                   (example.empty() ? "" : " (" + example + ")"));
            ui::DropCanvas(holder);
        } else {
            Check(false, "font or layout for the ComposeAgainstBlack case");
        }
    }
    sg_commit();
}

// ---- grey: 005FE440 -------------------------------------------------------------------------------------------

std::uint32_t GreyPacked(std::uint32_t pixel, const ui::PixelFormat& f) {
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
        AttachMemory(holder, w, h, f.format);
        std::mt19937 random(99);
        for (int i = 0; i < 80; ++i) {
            int x = static_cast<int>(random() % w), y = static_cast<int>(random() % h);
            ui::Fill(holder, Rect{x, y, x + 1 + static_cast<int>(random() % 20), y + 1 + static_cast<int>(random() % 15)},
                     static_cast<COLORREF>(random() & 0xFFFFFF));
        }
        const SurfaceMemory* before = ReadPacked(holder);
        std::vector<std::uint32_t> expected;
        for (int y = 0; y < h && before; ++y)
            for (int x = 0; x < w; ++x) expected.push_back(GreyPacked(PixelOf(*before, x, y), f.format));
        ui::Greyscale(holder);
        const SurfaceMemory* after = ReadPacked(holder);
        int differ = 0;
        for (int y = 0; y < h && after; ++y)
            for (int x = 0; x < w; ++x) differ += PixelOf(*after, x, y) != expected[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
        Check(before && after && differ == 0, std::string(f.name) + " greyscale: " + std::to_string(differ) + " pixels differ from 005FE440's");
        ui::DropCanvas(holder);
    }
    sg_commit();
}

// ---- image: Image, the switch, the comparison's view -------------------------------------------------------

void CheckImage() {
    if (g_verbose) std::printf("image:\n");
    for (const Format& f : kFormats) {
        const int w = 37, h = 29;
        const void* holder = &f.name;
        ui::Canvas(holder, w, h, f.format);
        std::vector<ui::CanvasInfo> canvases;
        ui::Canvases(&canvases);
        const unsigned made = canvases.size() == 1 ? canvases[0].version : 0;
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
        ui::Canvases(&canvases);
        Check(canvases.size() == 1 && canvases[0].version > made && canvases[0].text.empty() && canvases[0].width == w,
              std::string(f.name) + " canvas listed, its version moved by the commands");
        ui::DropCanvas(holder);
    }
    // The switch: EI_RENDER's words, the path, which blits draw the canvas.
    int a = 0;
    ui::Canvas(&a, 4, 4, kFormats[2].format);
    ui::ResetOptions();
    bool cpu = ui::CurrentMode() == ui::Mode::Cpu && !ui::Listed("fps") && !ui::DrawsCanvas(&a);
    bool both = ui::ParseOption("uiboth") && ui::CurrentMode() == ui::Mode::Both && ui::Listed("fps") && !ui::DrawsCanvas(&a);
    ui::SetPath(ui::Path::Native);
    both = both && ui::DrawsCanvas(&a) && !ui::DrawsCanvas(&cpu);
    ui::SetPath(ui::Path::Cpu);
    bool listed = ui::ParseOption("uinative=fps+status") && ui::CurrentMode() == ui::Mode::Native && ui::Listed("fps") &&
                  ui::Listed("status") && !ui::Listed("backdrop") && ui::DrawsCanvas(&a);
    bool other = !ui::ParseOption("uinatives") && !ui::ParseOption("smooth");
    ui::ResetOptions();
    Check(cpu && both && listed && other && ui::CurrentMode() == ui::Mode::Cpu,
          "the switch: no word, uiboth with both paths, uinative=fps+status, other words left alone");
    ui::DropCanvas(&a);
    sg_commit();
}

// ---- copy: CopyFrameTo's screen copy ----------------------------------------------------------------------------

std::vector<std::uint8_t> g_frameRgba;
sg_image g_frameImage{};

void MakeFrameTexture() {
    g_frameWidth = 200;
    g_frameHeight = 150;
    std::mt19937 random(4242);
    g_frameRgba.resize(static_cast<std::size_t>(g_frameWidth) * g_frameHeight * 4);
    for (std::size_t i = 0; i < g_frameRgba.size(); ++i) g_frameRgba[i] = static_cast<std::uint8_t>(i % 4 == 3 ? 255 : random() & 0xFF);
    sg_image_desc d{};
    d.width = g_frameWidth;
    d.height = g_frameHeight;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = sg_range{g_frameRgba.data(), g_frameRgba.size()};
    g_frameImage = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = g_frameImage;
    g_frameTexture = sg_make_view(&v);
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
            AttachMemory(holder, c.width, c.height, f.format);
            ui::CopyFrame(holder, c.source);
            const SurfaceMemory* got = ReadPacked(holder);
            Rect r{std::max(c.source.left, 0), std::max(c.source.top, 0), std::min(c.source.right, g_frameWidth), std::min(c.source.bottom, g_frameHeight)};
            int sw = r.right - r.left, sh = r.bottom - r.top, differ = 0;
            for (int y = 0; y < c.height && got; ++y)
                for (int x = 0; x < c.width; ++x) {
                    int sx = r.left + x * sw / c.width, sy = r.top + y * sh / c.height;
                    const std::uint8_t* p = &g_frameRgba[(static_cast<std::size_t>(sy) * g_frameWidth + static_cast<std::size_t>(sx)) * 4];
                    std::uint32_t rgb = (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
                    differ += PixelOf(*got, x, y) != ui::Pack(rgb, f.format);
                }
            Check(got && differ == 0, std::string(f.name) + " copy of (" + std::to_string(c.source.left) + ", " + std::to_string(c.source.top) + ")-(" +
                                          std::to_string(c.source.right) + ", " + std::to_string(c.source.bottom) + ") into " + std::to_string(c.width) +
                                          "x" + std::to_string(c.height) + ": " + std::to_string(differ) + " pixels differ");
            ui::DropCanvas(holder);
        }
    }
    // CopyFrame draws the frame so far first (SplitFrame).
    Check(g_splits == 12, "every copy drew the frame so far first: " + std::to_string(g_splits) + " splits for 12 copies");
    sg_commit();
}

// ---- frame: canvases in the frame, the pass split ---------------------------------------------------------------

bool ReadTarget(std::vector<std::uint32_t>* rgb) {
    auto* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    auto* context = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(g_target).tex2d));
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool ok = SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    if (ok) {
        rgb->resize(static_cast<std::size_t>(desc.Width) * desc.Height);
        for (UINT y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < desc.Width; ++x)
                (*rgb)[static_cast<std::size_t>(y) * desc.Width + x] =
                    (static_cast<std::uint32_t>(row[x * 4]) << 16) | (static_cast<std::uint32_t>(row[x * 4 + 1]) << 8) | row[x * 4 + 2];
        }
        context->Unmap(staging, 0);
    }
    staging->Release();
    sg_reset_state_cache();
    return ok;
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
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = g_targetWidth;
    d.height = g_targetHeight;
    d.pixel_format = detail::kColourFormat;
    g_target = sg_make_image(&d);
    d.usage.color_attachment = false;
    d.usage.depth_stencil_attachment = true;
    d.pixel_format = detail::kDepthFormat;
    g_targetDepth = sg_make_image(&d);
    sg_view_desc v{};
    v.color_attachment.image = g_target;
    g_targetColour = sg_make_view(&v);
    v = sg_view_desc{};
    v.depth_stencil_attachment.image = g_targetDepth;
    g_targetDepthView = sg_make_view(&v);

    std::vector<std::uint32_t> expected(static_cast<std::size_t>(g_targetWidth) * g_targetHeight, kTargetClearRgb);
    const ui::PixelFormat f565 = kFormats[0].format;
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
    const ui::PixelFormat f888 = kFormats[2].format;
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
    std::vector<Rect> drawn;
    ui::DrawnCanvasRects(&drawn);
    Check(drawn.size() == 5 && drawn[0].left == 5 && drawn[4].right == 140 && drawn[4].bottom == 90, "the frame's five canvas blit rectangles kept");
    // A canvas dropped with commands queued, and its blit, leave nothing behind.
    ui::Fill(second, Rect{0, 0, 5, 5}, RGB(9, 9, 9));
    ui::DropCanvas(second);
    ui::DropCanvas(first);
    detail::RunCanvasCommands();
    Check(!ui::HasCanvas(first) && !ui::HasCanvas(second), "dropped canvases are gone");
    sg_commit();
}

bool StartDevice(bool hardware) {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   levels, 3, D3D11_SDK_VERSION, &device, &got, &context);
    if (FAILED(hr)) {
        std::printf("D3D11CreateDevice (%s) failed: %08lX\n", hardware ? "hardware" : "WARP", static_cast<unsigned long>(hr));
        return false;
    }
    sg_desc desc{};
    desc.environment.defaults.color_format = detail::kColourFormat;
    desc.environment.defaults.depth_format = detail::kDepthFormat;
    desc.environment.defaults.sample_count = 1;
    desc.environment.d3d11.device = device;
    desc.environment.d3d11.device_context = context;
    desc.logger.func = &SokolLog;
    sg_setup(&desc);
    std::printf("device: %s, feature level %X\n", hardware ? "hardware" : "WARP", static_cast<unsigned>(got));
    return sg_isvalid();
}

} // namespace

int main(int argc, char** argv) {
    bool hardware = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hardware")) hardware = true;
        else if (!std::strcmp(argv[i], "--verbose")) g_verbose = true;
        else {
            std::printf("usage: ei_ui_check [--hardware] [--verbose]\n");
            return 2;
        }
    }
    if (!StartDevice(hardware)) return 1;
    CheckGdi();
    CheckGlyphs();
    CheckGrey();
    CheckImage();
    CheckCopy();
    CheckFrame();
    detail::DropCanvases();
    sg_shutdown();
    std::printf("ui_check: %d checks, %d failed, %d sokol / log problems\n", g_checks, g_failures, g_problems);
    return g_failures == 0 && g_problems == 0 ? 0 : 1;
}
