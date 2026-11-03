// The interface's 2D canvases (render_ui.h). See docs/notes/ui-native.md, "Source file overviews".
#include "render_ui.h"
#include "render_internal.h"
#include "render_text.h"

#include "../platform/log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

namespace {

using namespace detail;

constexpr int kMaxCanvas = 8192;

// ---- Pixel formats --------------------------------------------------------------------------------------

// An 8-bit channel value cut to its top `bits` and widened back to 8.
std::uint32_t QuantiseChannel(std::uint32_t value, int bits) {
    return bits >= 8 ? value : WidenTop(value >> (8 - bits), bits);
}
// `colour` (a COLORREF) as a surface of `format` keeps it (each channel's top bits) widened back to 8 bits a
// channel, as 0x00RRGGBB: what the canvas holds and the blit shows.
std::uint32_t Quantise(std::uint32_t colour, const PixelFormat& format) {
    std::uint32_t r = colour & 0xFF, g = (colour >> 8) & 0xFF, b = (colour >> 16) & 0xFF;
    return (QuantiseChannel(r, Channel(format.redMask).bits) << 16) | (QuantiseChannel(g, Channel(format.greenMask).bits) << 8) |
           QuantiseChannel(b, Channel(format.blueMask).bits);
}
// A pixel of `format` (a colour key) widened to 0x00RRGGBB.
std::uint32_t Widen(std::uint32_t pixel, const PixelFormat& format) {
    std::uint32_t out = 0;
    const std::uint32_t masks[3] = {format.redMask, format.greenMask, format.blueMask};
    for (int i = 0; i < 3; ++i) out = (out << 8) | Channel(masks[i]).Widen(pixel);
    return out;
}

// ---- Canvases and their queue -------------------------------------------------------------------------


struct Op {
    enum Kind : std::uint8_t { Make, Solid, Glyphs, Copy, Grey, Pixels, Resize };
    Kind kind = Solid;
    const void* holder = nullptr;
    unsigned canvas = 0;
    Rect rect{};                 // Solid: the rectangle; Glyphs: the run's bounds; Copy: the frame's source; Pixels: where;
                                 // Resize: the old size
    Rect scissor{};              // Glyphs: the clip rectangle within the canvas
    std::uint32_t colour = 0;    // Solid: 0x00RRGGBB as the canvas holds it; Glyphs: the text colour
    const text::GlyphAtlas* atlas = nullptr;
    std::vector<float> vertices;   // Glyphs: x, y, atlas x, atlas y per corner, six corners a quad
    std::vector<std::uint32_t> pixels;   // Pixels: RGBA8, rect's rows
};
std::vector<Op> g_queue;

struct CanvasState {
    unsigned id = 0;   // queued commands name the canvas they were made for
    int width = 0, height = 0;
    PixelFormat format;
    std::uint32_t bits[3] = {8, 8, 8};   // red, green, blue (at most 8)
    std::uint32_t greyMode = 2;          // ps_grey: 0 5:6:5, 1 5:5:5, 2 on 8-bit channels
    gpu::Texture gpu;                    // made by RunCanvasCommands
    bool blitPending = false;            // blitted in the frame, the blit not drawn yet
};
std::unordered_map<const void*, CanvasState> g_canvases;
unsigned g_nextId = 1;

// Images given up, released at the next RunCanvasCommands (DropCanvas comes from game code at any time).
std::vector<gpu::Texture> g_retired;

// The canvas of `holder` for a new command: a blit of it the frame has not drawn yet is drawn first.
CanvasState* Target(const void* holder) {
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end()) return nullptr;
    if (it->second.blitPending) {
        SplitFrame();
        it->second.blitPending = false;
    }
    return &it->second;
}

void Push(Op&& op) { g_queue.push_back(std::move(op)); }

void PushSolid(const void* holder, CanvasState& canvas, const Rect& rect, std::uint32_t rgb) {
    Rect r = rect.Intersection(Rect{0, 0, canvas.width, canvas.height});
    if (r.IsEmpty()) return;
    Op op;
    op.kind = Op::Solid;
    op.holder = holder;
    op.canvas = canvas.id;
    op.rect = r;
    op.colour = rgb;
    Push(std::move(op));
}

std::uint32_t CanvasColour(std::uint32_t colour, const CanvasState& canvas) { return Quantise(colour, canvas.format); }

// 0x00RRGGBB quantised to the canvas's bits, as the RGBA8 texel the canvas holds (red in the low byte).
std::uint32_t CanvasTexel(std::uint32_t rgb, const CanvasState& canvas) {
    std::uint32_t r = QuantiseChannel((rgb >> 16) & 0xFF, static_cast<int>(canvas.bits[0]));
    std::uint32_t g = QuantiseChannel((rgb >> 8) & 0xFF, static_cast<int>(canvas.bits[1]));
    std::uint32_t b = QuantiseChannel(rgb & 0xFF, static_cast<int>(canvas.bits[2]));
    return r | (g << 8) | (b << 16) | 0xFF000000u;
}

// ---- GPU objects ----------------------------------------------------------------------------------------

bool g_pipelinesTried = false, g_pipelinesMade = false;

// Scratch targets as large as the largest canvas so far: a glyph run's coverage, a copy of a canvas; and
// the 5:6:5 picture ReadRgb565 reads back.
gpu::Texture g_coverage, g_under, g_picture;

struct AtlasTexture {
    unsigned generation = 0;
    gpu::Texture image;
};
std::unordered_map<const text::GlyphAtlas*, AtlasTexture> g_atlases;

StreamBuffer g_vertices(false, "ei-canvas-glyphs");
std::vector<float> g_glyphVertices;   // DrawGlyphs' runs' vertices, appended to g_vertices together

// A pipeline drawing `program` into a canvas (RGBA8), the coverage scratch (R8, MAX blending) or the 5:6:5 picture
// (R16UI), no depth.
gpu::Pipeline CanvasPipeline(int program, gpu::Format format, const char* label) {
    gpu::PipelineDesc p;
    p.program = program;
    p.colour = format;
    if (format == gpu::Format::R8) {
        p.bufferCount = 1;
        p.buffers[0].stride = 4 * sizeof(float);
        p.attributeCount = 1;
        p.attributes[0] = {gpu_shaders::ui_glyph::kAttrPosition, 0, gpu::VertexFormat::Float4, 0};
        p.blend = {true, gpu::BlendFactor::One, gpu::BlendFactor::One, gpu::BlendFactor::One, gpu::BlendFactor::One,
                   gpu::BlendOp::Max, gpu::BlendOp::Max};
    }
    return gpu::MakePipeline(p, label);
}

gpu::Pipeline SolidPipeline() { return CanvasPipeline(gpu_shaders::ui_solid::kIndex, gpu::Format::Rgba8, "ei-canvas-solid"); }
gpu::Pipeline GlyphPipeline() { return CanvasPipeline(gpu_shaders::ui_glyph::kIndex, gpu::Format::R8, "ei-canvas-glyph"); }
gpu::Pipeline ComposePipeline() { return CanvasPipeline(gpu_shaders::ui_compose::kIndex, gpu::Format::Rgba8, "ei-canvas-compose"); }
gpu::Pipeline GreyPipeline() { return CanvasPipeline(gpu_shaders::ui_grey::kIndex, gpu::Format::Rgba8, "ei-canvas-grey"); }
gpu::Pipeline CopyPipeline() { return CanvasPipeline(gpu_shaders::ui_copy::kIndex, gpu::Format::Rgba8, "ei-canvas-copy"); }
gpu::Pipeline MovePipeline() { return CanvasPipeline(gpu_shaders::ui_move::kIndex, gpu::Format::Rgba8, "ei-canvas-move"); }
gpu::Pipeline Rgb565Pipeline() { return CanvasPipeline(gpu_shaders::ui_rgb565::kIndex, gpu::Format::R16Uint, "ei-canvas-rgb565"); }

gpu::Sampler Nearest() {
    return gpu::MakeSampler({gpu::Filter::Nearest, gpu::Filter::Nearest, gpu::Filter::Nearest, gpu::Wrap::Clamp, gpu::Wrap::Clamp});
}

// One texture with the nearest sampler at slot 0 of the fragment stage (every canvas program's first).
void BindTexture(const gpu::Texture& texture) {
    const gpu::TextureBinding b{texture, Nearest()};
    gpu::BindTextures(gpu::Stage::Fragment, 0, {&b, 1});
}

bool EnsurePipelines() {
    if (g_pipelinesTried) return g_pipelinesMade;
    g_pipelinesTried = true;
    g_pipelinesMade = SolidPipeline() && GlyphPipeline() && ComposePipeline() && GreyPipeline() && CopyPipeline() &&
                      MovePipeline() && Rgb565Pipeline();
    if (!g_pipelinesMade) ei::Log("render: the canvas pipelines could not be created (see the gpu messages above)");
    return g_pipelinesMade;
}

// The frame blit for the pass's sample count (1, or the render quality's multisampled one).
gpu::Pipeline BlitPipeline(int samples) {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::ui_blit::kIndex;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::Always;   // as the image blit: the depth buffer ignored and kept
    p.depthWrite = false;
    p.colour = kColourFormat;
    p.colourMask = gpu::kWriteRed | gpu::kWriteGreen | gpu::kWriteBlue;
    p.samples = samples;
    AddDepthCopy(p);
    return gpu::MakePipeline(p, "ei-canvas-blit");
}

// A colour target of `width` x `height` that is also sampled.
gpu::Texture MakeTarget(int width, int height, gpu::Format format, const char* label) {
    return gpu::MakeTexture({width, height, 1, 1, format, gpu::kColourTarget | gpu::kSampled, label});
}

bool EnsureScratch(gpu::Texture& s, int width, int height, gpu::Format format, const char* label) {
    if (s && s.width >= width && s.height >= height) return true;
    int w = std::max(width, s.width), h = std::max(height, s.height);
    gpu::Release(s);
    // The 5:6:5 picture is read back only (an integer format is not sampled).
    s = format == gpu::Format::R16Uint ? gpu::MakeTexture({w, h, 1, 1, format, gpu::kColourTarget, label})
                                       : MakeTarget(w, h, format, label);
    if (!s) {
        ei::Log("render: cannot create the %dx%d canvas scratch target %s", w, h, label);
        return false;
    }
    return true;
}

bool BeginCanvasPass(const gpu::Texture& target, bool clear, const char* label) {
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = clear ? gpu::Load::Clear : gpu::Load::Keep;
    pass.label = label;
    return gpu::BeginPass(pass);
}

UiParams CanvasParams(const CanvasState& canvas) {
    UiParams fs{};
    for (int i = 0; i < 3; ++i) fs.bits[i] = static_cast<int>(canvas.bits[i]);
    fs.bits[3] = static_cast<int>(canvas.greyMode);
    return fs;
}

// Every canvas program's fragment block is UiParams at slot 0.
static_assert(gpu_shaders::ui_blit::kBlockUiParams == 0 && gpu_shaders::ui_compose::kBlockUiParams == 0 &&
              gpu_shaders::ui_copy::kBlockUiParams == 0 && gpu_shaders::ui_grey::kBlockUiParams == 0 &&
              gpu_shaders::ui_move::kBlockUiParams == 0 && gpu_shaders::ui_rgb565::kBlockUiParams == 0 &&
              gpu_shaders::ui_solid::kBlockUiParams == 0);
void ApplyFs(const UiParams& fs) { gpu::PushUniforms(gpu::Stage::Fragment, 0, fs); }

// In a pass on a target of `width` x `height`: `from` of `source` drawn at (left, top), texel for pixel.
void DrawMoved(const gpu::Texture& source, const Rect& from, int left, int top, int width, int height) {
    gpu::ApplyPipeline(MovePipeline());
    BindTexture(source);
    ApplyRect(Rect{left, top, left + from.Width(), top + from.Height()}, width, height);
    UiParams fs{};
    fs.source[0] = left - from.left;
    fs.source[1] = top - from.top;
    ApplyFs(fs);
    gpu::Draw(0, 6);
}

// The canvas's image of its size, all black.
bool MakeImage(CanvasState& canvas) {
    g_retired.push_back(canvas.gpu);
    canvas.gpu = MakeTarget(canvas.width, canvas.height, gpu::Format::Rgba8, "ei-canvas");
    if (!canvas.gpu) {
        ei::Log("render: cannot create a %dx%d canvas", canvas.width, canvas.height);
        return false;
    }
    if (BeginCanvasPass(canvas.gpu, true, "ei-canvas-clear")) gpu::EndPass();
    return true;
}

// Solid ops [from, to) of the queue, all on `canvas`, in one pass.
void DrawSolids(const CanvasState& canvas, std::size_t from, std::size_t to) {
    if (!BeginCanvasPass(canvas.gpu, false, "ei-canvas-solid")) return;
    gpu::ApplyPipeline(SolidPipeline());
    for (std::size_t k = from; k < to; ++k) {
        const Op& op = g_queue[k];
        ApplyRect(op.rect, canvas.width, canvas.height);
        UiParams fs = CanvasParams(canvas);
        fs.colour[0] = ((op.colour >> 16) & 0xFF) / 255.0f;
        fs.colour[1] = ((op.colour >> 8) & 0xFF) / 255.0f;
        fs.colour[2] = (op.colour & 0xFF) / 255.0f;
        fs.colour[3] = 1.0f;
        ApplyFs(fs);
        gpu::Draw(0, 6);
    }
    gpu::EndPass();
}

// The atlas as an R8 texture, made again when it changed.
gpu::Texture AtlasImage(const text::GlyphAtlas& atlas) {
    AtlasTexture& t = g_atlases[&atlas];
    if (t.image && t.generation == atlas.Generation()) return t.image;
    gpu::Release(t.image);
    t = AtlasTexture{};
    if (atlas.Width() <= 0 || atlas.Height() <= 0) return gpu::Texture{};
    t.image = gpu::MakeTexture({atlas.Width(), atlas.Height(), 1, 1, gpu::Format::R8, gpu::kSampled, "ei-canvas-atlas"});
    if (t.image)
        gpu::Write(t.image, 0, {0, 0, atlas.Width(), atlas.Height()}, atlas.Pixels(), static_cast<std::size_t>(atlas.Width()));
    t.generation = atlas.Generation();
    return t.image;
}

// Glyph runs [from, to) of the queue, all on `canvas` and none overlapping another, drawn together: each run's
// coverage into g_coverage and what the canvas held under it into g_under, the runs stacked in rows of both (each
// with its top left corner at the left edge), then each composed into the canvas, in one pass each for the
// coverage, the copy and the composition: each pass waits on the GPU for the one before it (the battle screen's six
// runs a frame took the GPU 0.07 ms drawn one by one, 0.055 ms together; docs/notes/renderer.md section 13).
void DrawGlyphs(const CanvasState& canvas, std::size_t from, std::size_t to) {
    struct Run {
        const Op* op;
        gpu::Texture atlas;
        std::uint32_t first;   // its vertices' first byte in the runs' (g_glyphVertices)
        int row;               // its top row in the scratch targets
    };
    std::vector<Run> runs;
    g_glyphVertices.clear();
    int width = 0, height = 0;
    for (std::size_t k = from; k < to; ++k) {
        const Op& op = g_queue[k];
        const int row = height;
        width = std::max(width, op.rect.Width());
        height += op.rect.Height();
        if (!op.atlas) continue;
        gpu::Texture atlas = AtlasImage(*op.atlas);
        if (!atlas) continue;
        runs.push_back(Run{&op, atlas, static_cast<std::uint32_t>(g_glyphVertices.size() * sizeof(float)), row});
        g_glyphVertices.insert(g_glyphVertices.end(), op.vertices.begin(), op.vertices.end());
    }
    // One append for all the runs (an append may replace the stream's buffer, leaving the offsets before it behind).
    const int offset = runs.empty() ? -1 : g_vertices.Append(g_glyphVertices.data(), g_glyphVertices.size() * sizeof(float));
    if (offset < 0 || !EnsureScratch(g_coverage, width, height, gpu::Format::R8, "ei-canvas-coverage") ||
        !EnsureScratch(g_under, width, height, gpu::Format::Rgba8, "ei-canvas-under"))
        return;
    // The coverage: each run's canvas positions moved to its row (vs_glyph), clipped to it (its bounds within its
    // clip rectangle).
    if (!BeginCanvasPass(g_coverage, true, "ei-canvas-coverage")) return;
    gpu::ApplyPipeline(GlyphPipeline());
    for (const Run& run : runs) {
        const Rect& r = run.op->rect;
        gpu::SetScissor({0, run.row, r.Width(), run.row + r.Height()});
        const gpu::BufferBinding vertices{g_vertices.Buffer(), static_cast<std::uint32_t>(offset) + run.first};
        gpu::BindVertexBuffers(0, {&vertices, 1});
        BindTexture(run.atlas);
        ApplyRect(Rect{r.left, r.top - run.row, r.right, r.bottom - run.row}, g_coverage.width, g_coverage.height);
        gpu::Draw(0, static_cast<std::uint32_t>(run.op->vertices.size() / 4));
    }
    gpu::EndPass();
    // What the canvas held there, then the runs composed into it.
    if (!BeginCanvasPass(g_under, false, "ei-canvas-under")) return;
    for (const Run& run : runs) DrawMoved(canvas.gpu, run.op->rect, 0, run.row, g_under.width, g_under.height);
    gpu::EndPass();
    if (!BeginCanvasPass(canvas.gpu, false, "ei-canvas-compose")) return;
    gpu::ApplyPipeline(ComposePipeline());
    const gpu::Sampler nearest = Nearest();
    const gpu::TextureBinding c[2] = {{g_coverage, nearest}, {g_under, nearest}};
    gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::ui_compose::kSamplerTex0, c);
    for (const Run& run : runs) {
        const Op& op = *run.op;
        ApplyRect(op.rect, canvas.width, canvas.height);
        UiParams fs = CanvasParams(canvas);
        fs.colour[0] = static_cast<float>(op.colour & 0xFF);
        fs.colour[1] = static_cast<float>((op.colour >> 8) & 0xFF);
        fs.colour[2] = static_cast<float>((op.colour >> 16) & 0xFF);
        fs.source[0] = op.rect.left;
        fs.source[1] = op.rect.top - run.row;
        ApplyFs(fs);
        gpu::Draw(0, 6);
    }
    gpu::EndPass();
}

// Whether glyph run `op` can join the runs [from, to) drawn together: on the same canvas, overlapping none of
// them (each run's composition reads what the canvas held before it), the stacked rows within a target's size.
bool JoinsRuns(const Op& op, std::size_t from, std::size_t to) {
    if (op.kind != Op::Glyphs || op.holder != g_queue[from].holder || op.canvas != g_queue[from].canvas) return false;
    int height = op.rect.Height();
    for (std::size_t k = from; k < to; ++k) {
        if (!op.rect.Intersection(g_queue[k].rect).IsEmpty()) return false;
        height += g_queue[k].rect.Height();
    }
    return height <= kMaxCanvas;
}

void DrawCopy(const CanvasState& canvas, const Op& op) {
    const gpu::Texture& frame = FrameTexture();
    int fw = 0, fh = 0;
    if (!frame || !FrameSize(&fw, &fh)) return;
    Rect r = op.rect.Intersection(Rect{0, 0, fw, fh});
    if (r.IsEmpty()) return;
    if (!BeginCanvasPass(canvas.gpu, false, "ei-canvas-copy")) return;
    gpu::ApplyPipeline(CopyPipeline());
    BindTexture(frame);
    ApplyRect(Rect{0, 0, canvas.width, canvas.height}, canvas.width, canvas.height);
    UiParams fs = CanvasParams(canvas);
    fs.source[0] = r.left;
    fs.source[1] = r.top;
    fs.source[2] = r.Width();
    fs.source[3] = r.Height();
    fs.dest[0] = canvas.width;
    fs.dest[1] = canvas.height;
    ApplyFs(fs);
    gpu::Draw(0, 6);
    gpu::EndPass();
}

// The canvas's image made again at its new size, the old one (op.rect: its size) stretched over it with the copy's
// nearest pixel.
void ResizeImage(CanvasState& canvas, const Op& op) {
    const gpu::Texture old = canvas.gpu;
    g_retired.push_back(old);
    canvas.gpu = MakeTarget(canvas.width, canvas.height, gpu::Format::Rgba8, "ei-canvas");
    if (!canvas.gpu) {
        ei::Log("render: cannot create a %dx%d canvas", canvas.width, canvas.height);
        return;
    }
    if (!BeginCanvasPass(canvas.gpu, true, "ei-canvas-resize")) return;
    if (old) {
        gpu::ApplyPipeline(CopyPipeline());
        BindTexture(old);
        ApplyRect(Rect{0, 0, canvas.width, canvas.height}, canvas.width, canvas.height);
        UiParams fs = CanvasParams(canvas);
        fs.source[2] = op.rect.right;
        fs.source[3] = op.rect.bottom;
        fs.dest[0] = canvas.width;
        fs.dest[1] = canvas.height;
        ApplyFs(fs);
        gpu::Draw(0, 6);
    }
    gpu::EndPass();
}

// An Image's pixels drawn into the canvas, through an image of their own.
void DrawPixels(const CanvasState& canvas, const Op& op) {
    const Rect& r = op.rect;
    const int w = r.Width(), h = r.Height();
    gpu::Texture image = gpu::MakeTexture({w, h, 1, 1, gpu::Format::Rgba8, gpu::kSampled, "ei-canvas-pixels"});
    if (image && gpu::Write(image, 0, {0, 0, w, h}, op.pixels.data(), static_cast<std::size_t>(w) * 4) &&
        BeginCanvasPass(canvas.gpu, false, "ei-canvas-pixels")) {
        DrawMoved(image, Rect{0, 0, w, h}, r.left, r.top, canvas.width, canvas.height);
        gpu::EndPass();
    }
    gpu::Release(image);
}

void DrawGrey(const CanvasState& canvas) {
    if (!EnsureScratch(g_under, canvas.width, canvas.height, gpu::Format::Rgba8, "ei-canvas-under")) return;
    Rect whole{0, 0, canvas.width, canvas.height};
    if (!BeginCanvasPass(g_under, false, "ei-canvas-under")) return;
    DrawMoved(canvas.gpu, whole, 0, 0, g_under.width, g_under.height);
    gpu::EndPass();
    if (!BeginCanvasPass(canvas.gpu, false, "ei-canvas-grey")) return;
    gpu::ApplyPipeline(GreyPipeline());
    BindTexture(g_under);
    ApplyRect(whole, canvas.width, canvas.height);
    ApplyFs(CanvasParams(canvas));
    gpu::Draw(0, 6);
    gpu::EndPass();
}

CanvasState* Find(const void* holder, unsigned id) {
    auto it = g_canvases.find(holder);
    return it != g_canvases.end() && it->second.id == id ? &it->second : nullptr;
}

// `region` of `texture` read back now (a stall), into `texels` until `ticket` is released.
bool ReadBack(const gpu::Texture& texture, const Rect& region, gpu::Ticket* ticket, gpu::Texels* texels) {
    *ticket = gpu::Download(texture, 0, region);
    return gpu::Wait(*ticket) && gpu::Read(*ticket, texels);
}

} // namespace

// ---- The public commands --------------------------------------------------------------------------------

namespace ui {

std::uint32_t Pack(std::uint32_t rgb, const PixelFormat& format) {
    std::uint32_t out = 0;
    const std::uint32_t masks[3] = {format.redMask, format.greenMask, format.blueMask};
    for (int i = 0; i < 3; ++i) {
        const Channel c(masks[i]);
        if (!c.bits) continue;
        std::uint32_t v = (rgb >> (16 - 8 * i)) & 0xFF;
        out |= (c.bits >= 8 ? v << (c.shift + c.bits - 8) : (v >> (8 - c.bits)) << c.shift) & c.mask;
    }
    return out;
}

std::uint32_t TopBits(std::uint32_t rgb, const PixelFormat& format) {
    std::uint32_t out = 0;
    const std::uint32_t masks[3] = {format.redMask, format.greenMask, format.blueMask};
    for (int i = 0; i < 3; ++i) {
        const int bits = std::min(Channel(masks[i]).bits, 8);
        const std::uint32_t v = (rgb >> (16 - 8 * i)) & 0xFF;
        out = (out << 8) | (bits ? v & (0xFFu << (8 - bits)) & 0xFF : 0);
    }
    return out;
}

bool Canvas(const void* holder, int width, int height, const PixelFormat& format) {
    if (!holder || width < 1 || height < 1 || width > kMaxCanvas || height > kMaxCanvas) return false;
    auto it = g_canvases.find(holder);
    if (it != g_canvases.end() && it->second.width == width && it->second.height == height &&
        it->second.format.bitsPerPixel == format.bitsPerPixel && it->second.format.redMask == format.redMask &&
        it->second.format.greenMask == format.greenMask && it->second.format.blueMask == format.blueMask)
        return true;
    CanvasState* canvas = it != g_canvases.end() ? Target(holder) : &g_canvases[holder];
    canvas->id = g_nextId++;
    canvas->width = width;
    canvas->height = height;
    canvas->format = format;
    const std::uint32_t masks[3] = {format.redMask, format.greenMask, format.blueMask};
    for (int i = 0; i < 3; ++i) canvas->bits[i] = static_cast<std::uint32_t>(std::min(Channel(masks[i]).bits, 8));
    bool is565 = format.bitsPerPixel == 16 && format.redMask == 0xF800 && format.greenMask == 0x7E0 && format.blueMask == 0x1F;
    bool is555 = format.bitsPerPixel == 16 && format.redMask == 0x7C00 && format.greenMask == 0x3E0 && format.blueMask == 0x1F;
    canvas->greyMode = is565 ? 0 : is555 ? 1 : 2;
    Op op;
    op.kind = Op::Make;
    op.holder = holder;
    op.canvas = canvas->id;
    Push(std::move(op));
    return true;
}

bool HasCanvas(const void* holder) { return g_canvases.count(holder) != 0; }

void DropCanvas(const void* holder) {
    // A blit of it the frame has not drawn yet is drawn first (Target): the blit reads the canvas when the frame
    // is drawn, and SwapContents puts a backdrop away after the frame blitted it.
    if (!Target(holder)) return;
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end()) return;
    g_retired.push_back(it->second.gpu);
    g_canvases.erase(it);
}

void Resize(const void* holder, int width, int height) {
    if (width < 1 || height < 1 || width > kMaxCanvas || height > kMaxCanvas || !Target(holder)) return;
    // What is queued for it was laid out at the old size.
    RunCanvasesNow();
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end()) return;
    CanvasState& canvas = it->second;
    if (canvas.width == width && canvas.height == height) return;
    Op op;
    op.kind = Op::Resize;
    op.holder = holder;
    op.canvas = canvas.id;
    op.rect = Rect{0, 0, canvas.width, canvas.height};
    canvas.width = width;
    canvas.height = height;
    Push(std::move(op));
}

void MoveCanvas(const void* from, const void* to) {
    if (from == to || !Target(from)) return;   // (Target: a blit of it not drawn yet is drawn first)
    DropCanvas(to);
    auto it = g_canvases.find(from);
    if (it == g_canvases.end()) return;
    CanvasState canvas = std::move(it->second);
    g_canvases.erase(it);
    for (Op& op : g_queue)
        if (op.holder == from && op.canvas == canvas.id) op.holder = to;
    g_canvases.emplace(to, std::move(canvas));
}

void Fill(const void* holder, const Rect& rect, std::uint32_t colour) {
    CanvasState* canvas = Target(holder);
    if (canvas) PushSolid(holder, *canvas, rect, CanvasColour(colour, *canvas));
}

void FrameRect(const void* holder, const Rect& rect, std::uint32_t colour) {
    CanvasState* canvas = Target(holder);
    if (!canvas || rect.IsEmpty()) return;
    std::uint32_t rgb = CanvasColour(colour, *canvas);
    PushSolid(holder, *canvas, Rect{rect.left, rect.top, rect.right, rect.top + 1}, rgb);
    PushSolid(holder, *canvas, Rect{rect.left, rect.bottom - 1, rect.right, rect.bottom}, rgb);
    PushSolid(holder, *canvas, Rect{rect.left, rect.top + 1, rect.left + 1, rect.bottom - 1}, rgb);
    PushSolid(holder, *canvas, Rect{rect.right - 1, rect.top + 1, rect.right, rect.bottom - 1}, rgb);
}

void Line(const void* holder, int x0, int y0, int x1, int y1, int width, std::uint32_t colour) {
    CanvasState* canvas = Target(holder);
    if (!canvas) return;
    std::uint32_t rgb = CanvasColour(colour, *canvas);
    if (width < 1) width = 1;
    if (x0 == x1 && y0 == y1) {   // nothing with a one-pixel pen; GDI's two-pixel pen leaves a dot
        if (width == 2) PushSolid(holder, *canvas, Rect{x0, y0, x0 + 1, y0 + 1}, rgb);
        return;
    }
    // A wider pen is a band of its width around the line; a one-pixel pen covers the pixels from the
    // start up to the end, not including it.
    const int before = width > 1 ? width / 2 : 0;
    const int left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
    if (width == 2 && (x0 == x1 || y0 == y1)) {
        // GDI's two-pixel pen (round ends; measured on Windows 10, the console's caret): both end points
        // drawn. Vertical: the column left of the line too. Horizontal: the row above too, without the start.
        if (x0 == x1) {
            PushSolid(holder, *canvas, Rect{x0 - 1, top, x0 + 1, bottom + 1}, rgb);
        } else {
            PushSolid(holder, *canvas, Rect{left, y0, right + 1, y0 + 1}, rgb);
            PushSolid(holder, *canvas, x1 > x0 ? Rect{x0 + 1, y0 - 1, x1 + 1, y0} : Rect{x1, y0 - 1, x0, y0}, rgb);
        }
        return;
    }
    if (y0 == y1) {
        Rect r = width > 1 ? Rect{left - before, y0 - before, right + before + 1, y0 - before + width}
                           : Rect{x0 < x1 ? x0 : x1 + 1, y0, x0 < x1 ? x1 : x0 + 1, y0 + 1};
        PushSolid(holder, *canvas, r, rgb);
        return;
    }
    if (x0 == x1) {
        Rect r = width > 1 ? Rect{x0 - before, top - before, x0 - before + width, bottom + before + 1}
                           : Rect{x0, y0 < y1 ? y0 : y1 + 1, x0 + 1, y0 < y1 ? y1 : y0 + 1};
        PushSolid(holder, *canvas, r, rgb);
        return;
    }
    // Bresenham's steps (the end point left out), a pen-wide square at each.
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy, x = x0, y = y0;
    while (x != x1 || y != y1) {
        PushSolid(holder, *canvas, Rect{x - before, y - before, x - before + width, y - before + width}, rgb);
        int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y += sy;
        }
    }
}

void Pixel(const void* holder, int x, int y, std::uint32_t colour) {
    CanvasState* canvas = Target(holder);
    if (canvas) PushSolid(holder, *canvas, Rect{x, y, x + 1, y + 1}, CanvasColour(colour, *canvas));
}

void GlyphRun(const void* holder, text::TextFont& font, const text::TextLayout& layout, std::uint32_t colour) {
    CanvasState* canvas = Target(holder);
    if (!canvas || (layout.quads.empty() && layout.underlines.empty())) return;
    Rect scissor{0, 0, canvas->width, canvas->height};
    if (layout.clipped) scissor = scissor.Intersection(layout.clip);
    if (scissor.IsEmpty()) return;
    Op op;
    op.kind = Op::Glyphs;
    op.holder = holder;
    op.canvas = canvas->id;
    op.scissor = scissor;
    op.colour = colour & 0xFFFFFF;
    op.atlas = &font.Atlas();
    op.vertices.reserve((layout.quads.size() + layout.underlines.size()) * 24);
    Rect bounds{scissor.right, scissor.bottom, scissor.left, scissor.top};
    auto quad = [&](int x, int y, int w, int h, float u, float v) {
        if (w <= 0 || h <= 0) return;
        const float x0 = static_cast<float>(x), y0 = static_cast<float>(y);
        const float x1 = static_cast<float>(x + w), y1 = static_cast<float>(y + h);
        const float u1 = u < 0.0f ? u : u + w, v1 = u < 0.0f ? v : v + h;
        const float corners[6][4] = {{x0, y0, u, v}, {x1, y0, u1, v}, {x1, y1, u1, v1}, {x0, y0, u, v}, {x1, y1, u1, v1}, {x0, y1, u, v1}};
        for (const auto& c : corners) op.vertices.insert(op.vertices.end(), c, c + 4);
        bounds = Rect{std::min(bounds.left, x), std::min(bounds.top, y), std::max(bounds.right, x + w), std::max(bounds.bottom, y + h)};
    };
    for (const text::GlyphQuad& q : layout.quads)
        quad(q.x, q.y, q.width, q.height, static_cast<float>(q.atlasX), static_cast<float>(q.atlasY));
    for (const Rect& u : layout.underlines) quad(u.left, u.top, u.Width(), u.Height(), -1.0f, -1.0f);
    op.rect = bounds.Intersection(scissor);
    if (op.rect.IsEmpty()) return;
    Push(std::move(op));
}

void CopyFrame(const void* holder, const Rect& source) {
    CanvasState* canvas = Target(holder);
    if (!canvas || source.IsEmpty()) return;
    // The frame as drawn up to now: its commands recorded so far drawn before the copy is.
    SplitFrame();
    Op op;
    op.kind = Op::Copy;
    op.holder = holder;
    op.canvas = canvas->id;
    op.rect = source;
    Push(std::move(op));
}

void Greyscale(const void* holder) {
    CanvasState* canvas = Target(holder);
    if (!canvas) return;
    Op op;
    op.kind = Op::Grey;
    op.holder = holder;
    op.canvas = canvas->id;
    Push(std::move(op));
}

bool ReadPixels(const void* holder, std::vector<std::uint32_t>* rgb, int* width, int* height) {
    if (!HasCanvas(holder)) return false;
    RunCanvasesNow();
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end() || !it->second.gpu) return false;
    const CanvasState& canvas = it->second;
    const int w = canvas.width, h = canvas.height;
    gpu::Ticket ticket;
    gpu::Texels texels;
    if (!ReadBack(canvas.gpu, Rect{0, 0, w, h}, &ticket, &texels)) {
        gpu::Release(ticket);
        return false;
    }
    rgb->resize(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = texels.Row(y);
        std::uint32_t* out = rgb->data() + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < w; ++x)   // RGBA8
            out[x] = (static_cast<std::uint32_t>(row[x * 4]) << 16) | (static_cast<std::uint32_t>(row[x * 4 + 1]) << 8) | row[x * 4 + 2];
    }
    gpu::Release(ticket);
    if (width) *width = canvas.width;
    if (height) *height = canvas.height;
    return true;
}

bool ReadPixel(const void* holder, int x, int y, std::uint32_t* rgb) {
    if (!HasCanvas(holder)) return false;
    RunCanvasesNow();
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end() || !it->second.gpu) return false;
    const CanvasState& canvas = it->second;
    if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return false;
    gpu::Ticket ticket;
    gpu::Texels texels;
    const bool read = ReadBack(canvas.gpu, Rect{x, y, x + 1, y + 1}, &ticket, &texels);
    if (read) {
        const std::uint8_t* p = texels.Row(0);   // RGBA8
        *rgb = (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
    }
    gpu::Release(ticket);
    return read;
}

bool ReadRgb565(const void* holder, int width, int height, std::uint16_t* out) {
    if (!out || !HasCanvas(holder) || width < 1 || height < 1 || width > kMaxCanvas || height > kMaxCanvas) return false;
    RunCanvasesNow();
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end() || !it->second.gpu || !EnsurePipelines()) return false;
    const CanvasState& canvas = it->second;
    if (!EnsureScratch(g_picture, width, height, gpu::Format::R16Uint, "ei-canvas-rgb565")) return false;
    if (!BeginCanvasPass(g_picture, false, "ei-canvas-rgb565")) return false;
    gpu::SetViewport({0, 0, width, height});
    gpu::ApplyPipeline(Rgb565Pipeline());
    BindTexture(canvas.gpu);
    ApplyRect(Rect{0, 0, width, height}, width, height);
    UiParams fs{};
    fs.source[2] = canvas.width;
    fs.source[3] = canvas.height;
    fs.dest[0] = width;
    fs.dest[1] = height;
    ApplyFs(fs);
    gpu::Draw(0, 6);
    gpu::EndPass();
    gpu::Ticket ticket;
    gpu::Texels texels;
    const bool read = ReadBack(g_picture, Rect{0, 0, width, height}, &ticket, &texels);
    for (int y = 0; read && y < height; ++y)
        std::memcpy(out + static_cast<std::size_t>(y) * static_cast<std::size_t>(width), texels.Row(y),
                    static_cast<std::size_t>(width) * sizeof(std::uint16_t));
    gpu::Release(ticket);
    return read;
}

void DrawCanvas(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key) {
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end()) return;
    CanvasState& canvas = it->second;
    Frame& f = g_frame;
    CanvasBatch batch{holder, destination, source ? *source : Rect{0, 0, canvas.width, canvas.height}, keyed,
                      Widen(key, canvas.format)};
    f.canvasBatches.push_back(batch);
    f.commands.push_back(Command{Command::Canvas, static_cast<int>(f.canvasBatches.size()) - 1});
    canvas.blitPending = true;
}

void Image(const void* holder, const Rect& destination, const std::uint32_t* rgb, int pitch) {
    CanvasState* canvas = Target(holder);
    if (!canvas || !rgb) return;
    Rect r = destination.Intersection(Rect{0, 0, canvas->width, canvas->height});
    if (r.IsEmpty()) return;
    Op op;
    op.kind = Op::Pixels;
    op.holder = holder;
    op.canvas = canvas->id;
    op.rect = r;
    const int w = r.Width(), h = r.Height();
    op.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const std::uint32_t* in = rgb + static_cast<std::ptrdiff_t>(y + r.top - destination.top) * pitch + (r.left - destination.left);
        std::uint32_t* out = op.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x) out[x] = CanvasTexel(in[x], *canvas);
    }
    Push(std::move(op));
}

} // namespace ui

// ---- render.cpp's side ------------------------------------------------------------------------------------

namespace detail {

void RunCanvasCommands() {
    for (gpu::Texture& t : g_retired) gpu::Release(t);
    g_retired.clear();
    if (g_queue.empty()) return;
    if (!EnsurePipelines()) {
        g_queue.clear();
        return;
    }
    std::size_t k = 0;
    while (k < g_queue.size()) {
        const Op& op = g_queue[k];
        CanvasState* canvas = Find(op.holder, op.canvas);
        // A canvas whose image could not be made takes no commands.
        if (canvas && op.kind != Op::Make && !canvas->gpu) canvas = nullptr;
        if (!canvas) {
            ++k;
            continue;
        }
        std::size_t end = k + 1;
        switch (op.kind) {
        case Op::Make: MakeImage(*canvas); break;
        case Op::Solid:
            while (end < g_queue.size() && g_queue[end].kind == Op::Solid && g_queue[end].holder == op.holder &&
                   g_queue[end].canvas == op.canvas)
                ++end;
            DrawSolids(*canvas, k, end);
            break;
        case Op::Glyphs:
            while (end < g_queue.size() && JoinsRuns(g_queue[end], k, end)) ++end;
            DrawGlyphs(*canvas, k, end);
            break;
        case Op::Copy: DrawCopy(*canvas, op); break;
        case Op::Grey: DrawGrey(*canvas); break;
        case Op::Pixels: DrawPixels(*canvas, op); break;
        case Op::Resize: ResizeImage(*canvas, op); break;
        }
        k = end;
    }
    g_queue.clear();
}

void CanvasBlitsDrawn() {
    for (auto& entry : g_canvases) entry.second.blitPending = false;
}

void DrawCanvasBatch(const CanvasBatch& batch, int width, int height) {
    auto it = g_canvases.find(batch.holder);
    if (it == g_canvases.end() || !it->second.gpu || !g_pipelinesMade) return;
    const CanvasState& canvas = it->second;
    int dw = batch.destination.Width(), dh = batch.destination.Height();
    int sw = batch.source.Width(), sh = batch.source.Height();
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    const gpu::Pipeline pipeline = BlitPipeline(PassSampleCount());
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    BindTexture(canvas.gpu);
    ApplyRect(batch.destination, width, height);
    UiParams fs{};
    fs.source[0] = batch.source.left;
    fs.source[1] = batch.source.top;
    fs.source[2] = canvas.width;
    fs.source[3] = canvas.height;
    fs.dest[0] = batch.destination.left;
    fs.dest[1] = batch.destination.top;
    fs.scale[0] = static_cast<float>(sw) / static_cast<float>(dw);
    fs.scale[1] = static_cast<float>(sh) / static_cast<float>(dh);
    fs.key[0] = static_cast<int>((batch.key >> 16) & 0xFF);
    fs.key[1] = static_cast<int>((batch.key >> 8) & 0xFF);
    fs.key[2] = static_cast<int>(batch.key & 0xFF);
    fs.key[3] = batch.keyed ? 1 : 0;
    ApplyFs(fs);
    gpu::Draw(0, 6);
}

} // namespace detail

} // namespace render
