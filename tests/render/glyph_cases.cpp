#include "render/glyph_cases.h"

#include "render/gpu.h"
#include "render/render_internal.h"
#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace glyph_cases {

namespace ui = render::ui;
namespace text = render::text;
namespace gpu = render::gpu;

namespace {
gpu::Texture g_target, g_targetDepth;
int g_targetWidth = 0, g_targetHeight = 0;
std::size_t g_drawnCommands = 0;
int g_passes = 0;
std::uint32_t g_commits = 0;

} // namespace

void NewFrame(int width, int height) {
    gpu::Release(g_target);
    gpu::Release(g_targetDepth);
    g_targetWidth = width;
    g_targetHeight = height;
    g_target = gpu::MakeTexture(
        {width, height, 1, 1, render::kColourFormat, gpu::TextureUse::ColourTarget | gpu::TextureUse::Sampled, "ui-check-target"});
    g_targetDepth = gpu::MakeTexture({width, height, 1, 1, render::kDepthFormat, gpu::TextureUse::DepthTarget, "ui-check-depth"});
}

void DrawFrame() {
    render::RunCanvasCommands();
    render::UploadUi(render::g_frame);
    if (g_passes > 0 && g_drawnCommands == render::g_frame.commands.size()) return;
    gpu::PassDesc pass;
    pass.colour.texture = g_target;
    pass.colour.load = g_passes == 0 ? gpu::Load::Clear : gpu::Load::Keep;
    pass.depth = {g_targetDepth, 0, g_passes == 0 ? gpu::Load::Clear : gpu::Load::Keep, 1.0f, true};
    if (!gpu::BeginPass(pass)) return;
    for (std::size_t k = g_drawnCommands; k < render::g_frame.commands.size(); ++k) {
        const render::Command& c = render::g_frame.commands[k];
        const auto i = static_cast<std::size_t>(c.index);
        if (c.kind == render::Command::Kind::Ui)
            render::DrawUiBatch(render::g_frame.uiBatches[i], g_targetWidth, g_targetHeight);
        if (c.kind == render::Command::Kind::Picture)
            render::DrawPictureBatch(render::g_frame.pictureBatches[i], g_targetWidth, g_targetHeight);
    }
    gpu::EndPass();
    ++g_passes;
    g_drawnCommands = render::g_frame.commands.size();
}

void EndFrame() {
    render::g_frame.Clear();
    render::UiFrameDone();
    render::ReleaseRetired();
    g_drawnCommands = 0;
    g_passes = 0;
    gpu::Submit();
    ++g_commits;
}

bool ReadFrame(std::vector<std::uint32_t>* rgb) {
    DrawFrame();
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
                                                              (static_cast<std::uint32_t>(row[x * 4 + 1]) << 8) | row[x * 4 + 2];
        }
    }
    gpu::Release(ticket);
    return read;
}

const gpu::Texture& Target() { return g_target; }
Size TargetSize() { return {g_targetWidth, g_targetHeight}; }
std::uint32_t Commits() { return g_commits; }
void ReleaseFrame() {
    gpu::Release(g_target);
    gpu::Release(g_targetDepth);
}

std::uint32_t RgbOfColorref(std::uint32_t c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }

int Differ(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int width, std::string* example, int tolerance,
           const std::vector<std::uint8_t>* blends) {
    int differ = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const int allowed = blends && i < blends->size() ? std::max<int>(tolerance, (*blends)[i]) : tolerance;
        bool off = false;
        for (int shift : {16, 8, 0})
            off = off || std::abs(static_cast<int>((a[i] >> shift) & 0xFF) - static_cast<int>((b[i] >> shift) & 0xFF)) > allowed;
        if (off && ++differ == 1 && example) {
            char buffer[160];
            std::snprintf(buffer, sizeof buffer, "first at (%d, %d): expected %06X, drawn %06X", static_cast<int>(i % static_cast<std::size_t>(width)),
                          static_cast<int>(i / static_cast<std::size_t>(width)), a[i], b[i]);
            *example = buffer;
        }
    }
    return differ + (a.size() == b.size() ? 0 : 1000000);
}

std::string Outcome(int differ, const std::string& example) {
    return std::to_string(differ) + " pixels differ" + (example.empty() ? "" : " (" + example + ")");
}

namespace {
constexpr std::uint32_t Colourref(unsigned r, unsigned g, unsigned b) { return r | (g << 8) | (b << 16); }

// The run drawn onto `pixels` (0x00RRGGBB, `width` x `height`) at `origin`: each quad's coverage blended over what
// is there in turn (underlines fully covered), clipped to `clip` and the layout's clip; `blends` counts the blends
// at each pixel.
void RefGlyphs(std::vector<std::uint32_t>& pixels, std::vector<std::uint8_t>& blends, int width, Rect clip, text::TextFont& font,
               const text::TextLayout& layout, Point origin, std::uint32_t colour) {
    if (layout.clipped) clip = clip.Intersection(layout.clip.Offsetted(origin));
    const std::uint32_t t = RgbOfColorref(colour);
    auto blend = [&](int x, int y, unsigned c) {
        if (!c || !clip.Contains({x, y})) return;
        const std::size_t at = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
        std::uint32_t& p = pixels[at];
        ++blends[at];
        std::uint32_t out = 0;
        for (int shift : {16, 8, 0}) {
            unsigned a = (t >> shift) & 0xFF, d = (p >> shift) & 0xFF;
            out |= ((a * c + d * (255 - c) + 127) / 255) << shift;
        }
        p = out;
    };
    const text::GlyphAtlas& atlas = font.Atlas();
    for (const text::GlyphQuad& q : layout.quads)
        for (int y = 0; y < q.height; ++y)
            for (int x = 0; x < q.width; ++x)
                blend(origin.x + q.x + x, origin.y + q.y + y,
                      atlas.Pixels()[static_cast<std::size_t>(q.atlasY + y) * static_cast<std::size_t>(atlas.Width()) +
                                     static_cast<std::size_t>(q.atlasX + x)]);
    for (const Rect& u : layout.underlines)
        for (int y = u.top; y < u.bottom; ++y)
            for (int x = u.left; x < u.right; ++x) blend(origin.x + x, origin.y + y, 255);
}

} // namespace

void Run(CheckFn check) {
    const int w = 180, h = 72;
    const Rect all{0, 0, w, h};
    struct Case {
        const char* text;
        text::Format format;
        Rect rect;
        bool smoothing;
        int pixels;
    };
    const Case cases[] = {
        {"Hello, &world: AV Wa fi", text::Format::Left, {4, 4, 176, 40}, true, 16},
        {"\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0! \xD1\xEB\xEE\xE2\xE0 \xED\xE0 \xED\xE5\xF1\xEA\xEE\xEB\xFC\xEA\xEE "
         "\xF1\xF2\xF0\xEE\xEA",
         text::Format::WordBreak | text::Format::Center,
         {10, 2, 120, 70},
         true,
         19},
        {"Clipped text that runs past the right edge of its rectangle", text::Format::NoPrefix, {-6, 30, 90, 44}, true, 13},
        {"Right &aligned\r\nsecond line", text::Format::Right, {0, 20, 175, 71}, false, 16},
    };
    int caseIndex = 0;
    for (const Case& c : cases) {
        text::TextFont* font = text::GetTextFont("Times New Roman", c.pixels, c.smoothing);
        text::TextLayout layout;
        if (!font) {
            check(false, "font of case " + std::to_string(caseIndex));
            continue;
        }
        text::Layout(*font, text::DecodeCp1251(c.text), c.rect, c.format, layout);
        NewFrame(w, h);
        // A background of fills (the text blends over it), the shadow one pixel right and down, then the text,
        // drawn at an origin inside the frame and clipped to a part of it.
        std::vector<std::uint32_t> expected(static_cast<std::size_t>(w) * h, 0);
        std::vector<std::uint8_t> blends(expected.size(), 0);
        const std::uint32_t fills[3] = {Colourref(200, 30, 90), Colourref(10, 140, 250), Colourref(255, 255, 255)};
        for (int i = 0; i < 3; ++i) {
            Rect r = Rect{i * 60, i * 10, i * 60 + 70, h - i * 5}.Intersection(all);
            ui::Solid(r, fills[i], all);
            for (int y = r.top; y < r.bottom; ++y)
                for (int x = r.left; x < r.right; ++x) expected[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = RgbOfColorref(fills[i]);
        }
        const Rect clip{0, 0, w - 7, h};
        const Point origin{2, 1};
        ui::Glyphs(*font, layout, origin + Point{1, 1}, Colourref(8, 8, 8), clip);
        RefGlyphs(expected, blends, w, clip, *font, layout, origin + Point{1, 1}, Colourref(8, 8, 8));
        const std::uint32_t colour = Colourref(250, 220, 120 + caseIndex * 30);
        ui::Glyphs(*font, layout, origin, colour, clip);
        RefGlyphs(expected, blends, w, clip, *font, layout, origin, colour);
        std::vector<std::uint32_t> got;
        std::string example;
        const int differ = ReadFrame(&got) ? Differ(expected, got, w, &example, 1, &blends) : -1;
        EndFrame();
        check(differ == 0, "glyph run, case " + std::to_string(caseIndex) + " (" + std::to_string(layout.quads.size()) + " glyphs, " +
                               std::to_string(layout.underlines.size()) + " underlines): " + Outcome(differ, example));
        ++caseIndex;
    }
    // Many runs in one frame (about 2,000 glyphs, more than the stream buffer's first size), in several colours.
    const int bw = 960, bh = 340;
    text::TextFont* font = text::GetTextFont("Times New Roman", 14, true);
    if (!font) {
        check(false, "font Times New Roman 14 px");
        return;
    }
    NewFrame(bw, bh);
    std::vector<std::uint32_t> expected(static_cast<std::size_t>(bw) * bh, 0);
    std::vector<std::uint8_t> blends(expected.size(), 0);
    const char* line = "The quick brown fox jumps over the lazy dog; pack my box with five dozen liquor jugs. "
                       "Sphinx of black quartz, judge my vow: 0123456789 and again 9876543210.";
    int runs = 0;
    for (int i = 0; i < 13; ++i) {
        text::TextLayout layout;
        text::Layout(*font, text::DecodeCp1251(line), Rect{2, 2 + i * 26, bw - 2, 2 + i * 26 + 24}, text::Format::NoPrefix, layout);
        ui::Glyphs(*font, layout, {0, 0}, Colourref(220, 230, 140 + i * 8), Rect{0, 0, bw, bh});
        RefGlyphs(expected, blends, bw, Rect{0, 0, bw, bh}, *font, layout, {0, 0}, Colourref(220, 230, 140 + i * 8));
        ++runs;
    }
    std::vector<std::uint32_t> got;
    std::string example;
    const int differ = ReadFrame(&got) ? Differ(expected, got, bw, &example, 1, &blends) : -1;
    EndFrame();
    check(runs == 13 && differ == 0, "13 glyph runs in one frame (" + std::to_string(runs) + " laid out): " + Outcome(differ, example));
}


}   // namespace glyph_cases
