// The interface's 2D canvases (render_ui.h). See docs/notes/ui-native.md, "Source file overviews".
#include "render_ui.h"
#include "render_internal.h"
#include "render_text.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

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

struct CanvasImage {
    sg_image image{};
    sg_view attachment{}, texture{};
};

struct Op {
    enum Kind : std::uint8_t { Make, Solid, Glyphs, Copy, Grey, Pixels };
    Kind kind = Solid;
    const void* holder = nullptr;
    unsigned canvas = 0;
    Rect rect{};                 // Solid: the rectangle; Glyphs: the run's bounds; Copy: the frame's source; Pixels: where
    Rect scissor{};              // Glyphs: the clip rectangle within the canvas
    std::uint32_t colour = 0;    // Solid: 0x00RRGGBB as the canvas holds it; Glyphs: the text colour
    const text::GlyphAtlas* atlas = nullptr;
    std::vector<float> vertices;   // Glyphs: x, y, atlas x, atlas y per corner, six corners a quad
    std::vector<std::uint32_t> pixels;   // Pixels: RGBA8, rect's rows
};
std::vector<Op> g_queue;

// A canvas's commands drawn since it was last covered whole are kept (its history) and drawn again on a new
// device; a history grown past this many is replaced by the canvas's pixels read back (Compact).
constexpr std::size_t kMaxHistory = 256;

struct CanvasState {
    unsigned id = 0;   // queued commands name the canvas they were made for
    int width = 0, height = 0;
    PixelFormat format;
    std::uint32_t bits[3] = {8, 8, 8};   // red, green, blue (at most 8)
    std::uint32_t greyMode = 2;          // ps_grey: 0 5:6:5, 1 5:5:5, 2 on 8-bit channels
    CanvasImage gpu;                     // made by RunCanvasCommands
    bool blitPending = false;            // blitted in the frame, the blit not drawn yet
    std::vector<Op> history;             // drawn on `gpu` since it was made or last covered whole
    bool copied = false;                 // a screen copy drawn: its pixels become the history (Compact)
};
std::unordered_map<const void*, CanvasState> g_canvases;
unsigned g_nextId = 1;

// Images given up, destroyed at the next RunCanvasCommands (DropCanvas comes from game code at any time).
std::vector<CanvasImage> g_retired;

Rect Intersect(const Rect& a, const Rect& b) {
    return Rect{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}
bool Empty(const Rect& r) { return r.right <= r.left || r.bottom <= r.top; }

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
    Rect r = Intersect(rect, Rect{0, 0, canvas.width, canvas.height});
    if (Empty(r)) return;
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
sg_shader g_solidShader{}, g_glyphShader{}, g_composeShader{}, g_greyShader{}, g_copyShader{}, g_rgb565Shader{}, g_blitShader{};
sg_pipeline g_solid{}, g_glyph{}, g_compose{}, g_grey{}, g_copy{}, g_rgb565{};
// The frame blit per sample count of the pass (1, or the render quality's multisampled one).
struct BlitPipeline {
    int samples;
    sg_pipeline pipeline;
};
std::vector<BlitPipeline> g_blits;
sg_sampler g_nearest{};

// Scratch targets as large as the largest canvas so far: a glyph run's coverage, a copy of a canvas; and
// the 5:6:5 picture ReadRgb565 reads back.
struct Scratch {
    sg_image image{};
    sg_view attachment{}, texture{};
    int width = 0, height = 0;
};
Scratch g_coverage, g_under, g_picture;

struct AtlasTexture {
    unsigned generation = 0;
    sg_image image{};
    sg_view view{};
};
std::unordered_map<const text::GlyphAtlas*, AtlasTexture> g_atlases;

sg_buffer g_vertices{};
std::size_t g_vertexCapacity = 0;

ID3D11DeviceContext* Context() { return static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context())); }

// A pipeline drawing into a canvas (RGBA8), the coverage scratch (R8, MAX blending) or the 5:6:5 picture
// (R16UI), no depth.
sg_pipeline MakeCanvasPipeline(sg_shader shader, sg_pixel_format format, const char* label) {
    const bool coverage = format == SG_PIXELFORMAT_R8;
    sg_pipeline_desc p{};
    p.shader = shader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.colors[0].pixel_format = format;
    if (coverage) {
        p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT4;
        p.colors[0].blend.enabled = true;
        p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.op_rgb = SG_BLENDOP_MAX;
        p.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.op_alpha = SG_BLENDOP_MAX;
    }
    p.sample_count = 1;
    p.label = label;
    return sg_make_pipeline(&p);
}

bool EnsurePipelines() {
    if (g_pipelinesTried) return g_pipelinesMade;
    g_pipelinesTried = true;
    const sg_backend backend = sg_query_backend();
    g_solidShader = sg_make_shader(ui_solid_shader_desc(backend));
    g_glyphShader = sg_make_shader(ui_glyph_shader_desc(backend));
    g_composeShader = sg_make_shader(ui_compose_shader_desc(backend));
    g_greyShader = sg_make_shader(ui_grey_shader_desc(backend));
    g_copyShader = sg_make_shader(ui_copy_shader_desc(backend));
    g_rgb565Shader = sg_make_shader(ui_rgb565_shader_desc(backend));
    g_blitShader = sg_make_shader(ui_blit_shader_desc(backend));
    g_solid = MakeCanvasPipeline(g_solidShader, SG_PIXELFORMAT_RGBA8, "ei-canvas-solid");
    g_glyph = MakeCanvasPipeline(g_glyphShader, SG_PIXELFORMAT_R8, "ei-canvas-glyph");
    g_compose = MakeCanvasPipeline(g_composeShader, SG_PIXELFORMAT_RGBA8, "ei-canvas-compose");
    g_grey = MakeCanvasPipeline(g_greyShader, SG_PIXELFORMAT_RGBA8, "ei-canvas-grey");
    g_copy = MakeCanvasPipeline(g_copyShader, SG_PIXELFORMAT_RGBA8, "ei-canvas-copy");
    g_rgb565 = MakeCanvasPipeline(g_rgb565Shader, SG_PIXELFORMAT_R16UI, "ei-canvas-rgb565");
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_NEAREST;
    sd.mag_filter = SG_FILTER_NEAREST;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sd.label = "ei-canvas";
    g_nearest = sg_make_sampler(&sd);
    g_pipelinesMade = true;
    for (sg_pipeline p : {g_solid, g_glyph, g_compose, g_grey, g_copy, g_rgb565})
        g_pipelinesMade = g_pipelinesMade && sg_query_pipeline_state(p) == SG_RESOURCESTATE_VALID;
    if (!g_pipelinesMade) ei::Log("render: the canvas pipelines could not be created (see the sokol messages above)");
    return g_pipelinesMade;
}

sg_pipeline BlitPipelineFor(int samples) {
    for (const BlitPipeline& b : g_blits)
        if (b.samples == samples) return b.pipeline;
    sg_pipeline_desc p{};
    p.shader = g_blitShader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_ALWAYS;   // as the image blit: the depth buffer ignored and kept
    p.depth.write_enabled = false;
    p.colors[0].pixel_format = kColourFormat;
    p.colors[0].write_mask = SG_COLORMASK_RGB;
    p.sample_count = samples;
    p.label = "ei-canvas-blit";
    sg_pipeline made = sg_make_pipeline(&p);
    g_blits.push_back(BlitPipeline{samples, made});
    return made;
}

void DestroyImage(CanvasImage& i) {
    if (i.attachment.id) sg_destroy_view(i.attachment);
    if (i.texture.id) sg_destroy_view(i.texture);
    if (i.image.id) sg_destroy_image(i.image);
    i = CanvasImage{};
}

// A colour target of `width` x `height` with an attachment and a texture view.
bool MakeTarget(int width, int height, sg_pixel_format format, const char* label, sg_image* image, sg_view* attachment,
                sg_view* texture) {
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = width;
    d.height = height;
    d.pixel_format = format;
    d.sample_count = 1;
    d.label = label;
    *image = sg_make_image(&d);
    sg_view_desc v{};
    v.color_attachment.image = *image;
    *attachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = *image;
    *texture = sg_make_view(&v);
    return sg_query_image_state(*image) == SG_RESOURCESTATE_VALID && sg_query_view_state(*attachment) == SG_RESOURCESTATE_VALID &&
           sg_query_view_state(*texture) == SG_RESOURCESTATE_VALID;
}

bool EnsureScratch(Scratch& s, int width, int height, sg_pixel_format format, const char* label) {
    if (s.image.id && s.width >= width && s.height >= height) return true;
    int w = std::max(width, s.width), h = std::max(height, s.height);
    if (s.attachment.id) sg_destroy_view(s.attachment);
    if (s.texture.id) sg_destroy_view(s.texture);
    if (s.image.id) sg_destroy_image(s.image);
    s = Scratch{};
    if (!MakeTarget(w, h, format, label, &s.image, &s.attachment, &s.texture)) {
        ei::Log("render: cannot create the %dx%d canvas scratch target %s", w, h, label);
        return false;
    }
    s.width = w;
    s.height = h;
    return true;
}

void BeginCanvasPass(sg_view attachment, bool clear, const char* label) {
    sg_pass pass{};
    pass.action.colors[0].load_action = clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = sg_color{0.0f, 0.0f, 0.0f, 1.0f};
    pass.attachments.colors[0] = attachment;
    pass.label = label;
    sg_begin_pass(&pass);
}

ui_params_t CanvasParams(const CanvasState& canvas) {
    ui_params_t fs{};
    for (int i = 0; i < 3; ++i) fs.bits[i] = static_cast<int>(canvas.bits[i]);
    fs.bits[3] = static_cast<int>(canvas.greyMode);
    return fs;
}

void ApplyFs(const ui_params_t& fs) { sg_apply_uniforms(UB_ui_params, SG_RANGE(fs)); }

// `r` of the canvas's image copied into g_under at (0, row).
void CopyUnder(const CanvasState& canvas, const Rect& r, int row) {
    ID3D11Texture2D* from = D3DTexture(canvas.gpu.image);
    ID3D11Texture2D* to = D3DTexture(g_under.image);
    if (!from || !to) return;
    D3D11_BOX box{static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0, static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), 1};
    Context()->CopySubresourceRegion(to, 0, 0, static_cast<UINT>(row), 0, from, 0, &box);
}

// The canvas's image of its size, all black.
bool MakeImage(CanvasState& canvas) {
    g_retired.push_back(canvas.gpu);
    canvas.gpu = CanvasImage{};
    if (!MakeTarget(canvas.width, canvas.height, SG_PIXELFORMAT_RGBA8, "ei-canvas", &canvas.gpu.image, &canvas.gpu.attachment,
                    &canvas.gpu.texture)) {
        ei::Log("render: cannot create a %dx%d canvas", canvas.width, canvas.height);
        DestroyImage(canvas.gpu);
        return false;
    }
    BeginCanvasPass(canvas.gpu.attachment, true, "ei-canvas-clear");
    sg_end_pass();
    return true;
}

// Solid ops [from, to) of the queue, all on `canvas`, in one pass.
void DrawSolids(const CanvasState& canvas, std::size_t from, std::size_t to) {
    BeginCanvasPass(canvas.gpu.attachment, false, "ei-canvas-solid");
    sg_apply_pipeline(g_solid);
    for (std::size_t k = from; k < to; ++k) {
        const Op& op = g_queue[k];
        ApplyRect(op.rect, canvas.width, canvas.height);
        ui_params_t fs = CanvasParams(canvas);
        fs.colour[0] = ((op.colour >> 16) & 0xFF) / 255.0f;
        fs.colour[1] = ((op.colour >> 8) & 0xFF) / 255.0f;
        fs.colour[2] = (op.colour & 0xFF) / 255.0f;
        fs.colour[3] = 1.0f;
        ApplyFs(fs);
        sg_draw(0, 6, 1);
    }
    sg_end_pass();
}

// The atlas as an R8 texture, made again when it changed.
sg_view AtlasView(const text::GlyphAtlas& atlas) {
    AtlasTexture& t = g_atlases[&atlas];
    if (t.image.id && t.generation == atlas.Generation()) return t.view;
    if (t.view.id) sg_destroy_view(t.view);
    if (t.image.id) sg_destroy_image(t.image);
    t = AtlasTexture{};
    if (atlas.Width() <= 0 || atlas.Height() <= 0) return sg_view{};
    sg_image_desc d{};
    d.width = atlas.Width();
    d.height = atlas.Height();
    d.pixel_format = SG_PIXELFORMAT_R8;
    d.data.mip_levels[0] = sg_range{atlas.Pixels(), static_cast<std::size_t>(atlas.Width()) * static_cast<std::size_t>(atlas.Height())};
    d.label = "ei-canvas-atlas";
    t.image = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = t.image;
    t.view = sg_make_view(&v);
    t.generation = atlas.Generation();
    return t.view;
}

// Room for `bytes` more in the stream buffer this frame: a buffer too small is replaced by a larger one (empty), so
// the offsets of vertices appended before are void; false when none can be made.
bool VertexRoom(std::size_t bytes) {
    if (!g_vertices.id || sg_query_buffer_will_overflow(g_vertices, bytes)) {
        if (g_vertices.id) sg_destroy_buffer(g_vertices);
        g_vertexCapacity = std::max<std::size_t>({g_vertexCapacity * 2, bytes * 2, 64 * 1024});
        sg_buffer_desc d{};
        d.size = g_vertexCapacity;
        d.usage.vertex_buffer = true;
        d.usage.immutable = false;
        d.usage.dynamic_update = true;
        d.label = "ei-canvas-glyphs";
        g_vertices = sg_make_buffer(&d);
        if (sg_query_buffer_state(g_vertices) != SG_RESOURCESTATE_VALID) {
            if (g_vertices.id) sg_destroy_buffer(g_vertices);
            g_vertices = sg_buffer{};
            g_vertexCapacity = 0;
            return false;
        }
    }
    return true;
}

// The vertices appended to the stream buffer: their offset, or -1 (no room: VertexRoom for all of a pass's first).
int AppendVertices(const std::vector<float>& vertices) {
    std::size_t bytes = vertices.size() * sizeof(float);
    if (!g_vertices.id || sg_query_buffer_will_overflow(g_vertices, bytes)) return -1;
    sg_range range{vertices.data(), bytes};
    int offset = sg_append_buffer(g_vertices, &range);
    return sg_query_buffer_overflow(g_vertices) ? -1 : offset;
}

// Glyph runs [from, to) of the queue, all on `canvas` and none overlapping another, drawn together: each run's
// coverage into g_coverage and what the canvas held under it into g_under, the runs stacked in rows of both (each
// with its top left corner at the left edge), then each composed into the canvas, in one coverage pass and one
// compose pass: each pass waits on the GPU for the one before it (the battle screen's six runs a frame took the
// GPU 0.07 ms drawn one by one, 0.055 ms together; docs/notes/renderer.md section 13).
void DrawGlyphs(const CanvasState& canvas, std::size_t from, std::size_t to) {
    struct Run {
        const Op* op;
        sg_view atlas;
        int offset;   // its vertices' in g_vertices
        int row;      // its top row in the scratch targets
    };
    std::vector<Run> runs;
    std::size_t bytes = 0;
    for (std::size_t k = from; k < to; ++k) bytes += g_queue[k].vertices.size() * sizeof(float);
    if (!VertexRoom(bytes)) return;
    int width = 0, height = 0;
    for (std::size_t k = from; k < to; ++k) {
        const Op& op = g_queue[k];
        const int row = height;
        width = std::max(width, op.rect.right - op.rect.left);
        height += op.rect.bottom - op.rect.top;
        if (!op.atlas) continue;
        sg_view atlas = AtlasView(*op.atlas);
        int offset = AppendVertices(op.vertices);
        if (atlas.id && offset >= 0) runs.push_back(Run{&op, atlas, offset, row});
    }
    if (runs.empty() || !EnsureScratch(g_coverage, width, height, SG_PIXELFORMAT_R8, "ei-canvas-coverage") ||
        !EnsureScratch(g_under, width, height, SG_PIXELFORMAT_RGBA8, "ei-canvas-under"))
        return;
    // The coverage: each run's canvas positions moved to its row (vs_glyph), clipped to it (its bounds within its
    // clip rectangle).
    BeginCanvasPass(g_coverage.attachment, true, "ei-canvas-coverage");
    sg_apply_pipeline(g_glyph);
    for (const Run& run : runs) {
        const Rect& r = run.op->rect;
        sg_apply_scissor_rect(0, run.row, r.right - r.left, r.bottom - r.top, true);
        sg_bindings b{};
        b.vertex_buffers[0] = g_vertices;
        b.vertex_buffer_offsets[0] = run.offset;
        b.views[0] = run.atlas;
        b.samplers[0] = g_nearest;
        sg_apply_bindings(&b);
        ApplyRect(Rect{r.left, r.top - run.row, r.right, r.bottom - run.row}, g_coverage.width, g_coverage.height);
        sg_draw(0, static_cast<int>(run.op->vertices.size() / 4), 1);
    }
    sg_end_pass();
    // What the canvas held there, then the runs composed into it.
    for (const Run& run : runs) CopyUnder(canvas, run.op->rect, run.row);
    BeginCanvasPass(canvas.gpu.attachment, false, "ei-canvas-compose");
    sg_apply_pipeline(g_compose);
    sg_bindings c{};
    c.views[0] = g_coverage.texture;
    c.views[1] = g_under.texture;
    c.samplers[0] = g_nearest;
    sg_apply_bindings(&c);
    for (const Run& run : runs) {
        const Op& op = *run.op;
        ApplyRect(op.rect, canvas.width, canvas.height);
        ui_params_t fs = CanvasParams(canvas);
        fs.colour[0] = static_cast<float>(op.colour & 0xFF);
        fs.colour[1] = static_cast<float>((op.colour >> 8) & 0xFF);
        fs.colour[2] = static_cast<float>((op.colour >> 16) & 0xFF);
        fs.source[0] = op.rect.left;
        fs.source[1] = op.rect.top - run.row;
        ApplyFs(fs);
        sg_draw(0, 6, 1);
    }
    sg_end_pass();
}

// Whether glyph run `op` can join the runs [from, to) drawn together: on the same canvas, overlapping none of
// them (each run's composition reads what the canvas held before it), the stacked rows within a target's size.
bool JoinsRuns(const Op& op, std::size_t from, std::size_t to) {
    if (op.kind != Op::Glyphs || op.holder != g_queue[from].holder || op.canvas != g_queue[from].canvas) return false;
    int height = op.rect.bottom - op.rect.top;
    for (std::size_t k = from; k < to; ++k) {
        if (!Empty(Intersect(op.rect, g_queue[k].rect))) return false;
        height += g_queue[k].rect.bottom - g_queue[k].rect.top;
    }
    return height <= kMaxCanvas;
}

void DrawCopy(const CanvasState& canvas, const Op& op) {
    sg_view frame = FrameTexture();
    int fw = 0, fh = 0;
    if (!frame.id || !FrameSize(&fw, &fh)) return;
    Rect r = Intersect(op.rect, Rect{0, 0, fw, fh});
    if (Empty(r)) return;
    BeginCanvasPass(canvas.gpu.attachment, false, "ei-canvas-copy");
    sg_apply_pipeline(g_copy);
    sg_bindings b{};
    b.views[0] = frame;
    b.samplers[0] = g_nearest;
    sg_apply_bindings(&b);
    ApplyRect(Rect{0, 0, canvas.width, canvas.height}, canvas.width, canvas.height);
    ui_params_t fs = CanvasParams(canvas);
    fs.source[0] = r.left;
    fs.source[1] = r.top;
    fs.source[2] = r.right - r.left;
    fs.source[3] = r.bottom - r.top;
    fs.dest[0] = canvas.width;
    fs.dest[1] = canvas.height;
    ApplyFs(fs);
    sg_draw(0, 6, 1);
    sg_end_pass();
}

// An Image's pixels written into the canvas's texture.
void DrawPixels(const CanvasState& canvas, const Op& op) {
    ID3D11Texture2D* texture = D3DTexture(canvas.gpu.image);
    if (!texture) return;
    const Rect& r = op.rect;
    D3D11_BOX box{static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0, static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), 1};
    Context()->UpdateSubresource(texture, 0, &box, op.pixels.data(), static_cast<UINT>(r.right - r.left) * 4u, 0);
}

void DrawGrey(const CanvasState& canvas) {
    if (!EnsureScratch(g_under, canvas.width, canvas.height, SG_PIXELFORMAT_RGBA8, "ei-canvas-under")) return;
    Rect whole{0, 0, canvas.width, canvas.height};
    CopyUnder(canvas, whole, 0);
    BeginCanvasPass(canvas.gpu.attachment, false, "ei-canvas-grey");
    sg_apply_pipeline(g_grey);
    sg_bindings b{};
    b.views[0] = g_under.texture;
    b.samplers[0] = g_nearest;
    sg_apply_bindings(&b);
    ApplyRect(whole, canvas.width, canvas.height);
    ApplyFs(CanvasParams(canvas));
    sg_draw(0, 6, 1);
    sg_end_pass();
}

CanvasState* Find(const void* holder, unsigned id) {
    auto it = g_canvases.find(holder);
    return it != g_canvases.end() && it->second.id == id ? &it->second : nullptr;
}

// Staging copies for the read-backs, kept and grown: the canvases' (RGBA8) and the 5:6:5 picture's. Mapped under the
// present's lock (GpuWorkAlone); UnmapRegion after reading.
StagingCopy g_staging, g_pictureStaging;

void UnmapRegion(StagingCopy& s) {
    s.Unmap();
    sg_reset_state_cache();
}

// `op`, just drawn on `canvas`, joins its history. One that covers the canvas whole (its making, a fill or
// picture of all of it) starts the history again. A screen copy is not kept (the frame it copied is gone on a new
// device): the canvas is marked to be read back (Compact) once the queue is drawn.
void Remember(CanvasState& canvas, Op&& op) {
    const bool whole = op.rect.left <= 0 && op.rect.top <= 0 && op.rect.right >= canvas.width && op.rect.bottom >= canvas.height;
    if (op.kind == Op::Make || op.kind == Op::Copy || ((op.kind == Op::Solid || op.kind == Op::Pixels) && whole))
        canvas.history.clear();
    if (op.kind == Op::Copy) canvas.copied = true;
    if (op.kind != Op::Make && op.kind != Op::Copy) canvas.history.push_back(std::move(op));
}

// A long or copied history replaced by the canvas's pixels read back: one picture of the whole canvas.
void Compact(const void* holder, CanvasState& canvas) {
    canvas.copied = false;
    ID3D11Texture2D* texture = D3DTexture(canvas.gpu.image);
    const UINT w = static_cast<UINT>(canvas.width), h = static_cast<UINT>(canvas.height);
    if (!g_staging.Map(Context(), texture, 0, 0, w, h)) return;
    Op op;
    op.kind = Op::Pixels;
    op.holder = holder;
    op.canvas = canvas.id;
    op.rect = Rect{0, 0, canvas.width, canvas.height};
    op.pixels.resize(static_cast<std::size_t>(w) * h);
    for (UINT y = 0; y < h; ++y)   // RGBA8, as the Pixels command holds it
        std::memcpy(op.pixels.data() + static_cast<std::size_t>(y) * w, g_staging.Row(y), w * 4u);
    UnmapRegion(g_staging);
    canvas.history.clear();
    canvas.history.push_back(std::move(op));
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
    canvas->history.clear();
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
    if (!canvas || Empty(rect)) return;
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
    if (layout.clipped) scissor = Intersect(scissor, Rect{layout.clip.left, layout.clip.top, layout.clip.right, layout.clip.bottom});
    if (Empty(scissor)) return;
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
    for (const Rect& u : layout.underlines) quad(u.left, u.top, u.right - u.left, u.bottom - u.top, -1.0f, -1.0f);
    op.rect = Intersect(bounds, scissor);
    if (Empty(op.rect)) return;
    Push(std::move(op));
}

void CopyFrame(const void* holder, const Rect& source) {
    CanvasState* canvas = Target(holder);
    if (!canvas || Empty(source)) return;
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
    if (it == g_canvases.end() || !it->second.gpu.image.id) return false;
    const CanvasState& canvas = it->second;
    FpuGuard fpu;
    GpuWorkAlone alone;
    ID3D11Texture2D* texture = D3DTexture(canvas.gpu.image);
    UINT w = static_cast<UINT>(canvas.width), h = static_cast<UINT>(canvas.height);
    if (!g_staging.Map(Context(), texture, 0, 0, w, h)) return false;
    rgb->resize(static_cast<std::size_t>(w) * h);
    for (UINT y = 0; y < h; ++y) {
        const std::uint8_t* row = g_staging.Row(y);
        std::uint32_t* out = rgb->data() + static_cast<std::size_t>(y) * w;
        for (UINT x = 0; x < w; ++x)   // RGBA8
            out[x] = (static_cast<std::uint32_t>(row[x * 4]) << 16) | (static_cast<std::uint32_t>(row[x * 4 + 1]) << 8) | row[x * 4 + 2];
    }
    UnmapRegion(g_staging);
    if (width) *width = canvas.width;
    if (height) *height = canvas.height;
    return true;
}

bool ReadPixel(const void* holder, int x, int y, std::uint32_t* rgb) {
    if (!HasCanvas(holder)) return false;
    RunCanvasesNow();
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end() || !it->second.gpu.image.id) return false;
    const CanvasState& canvas = it->second;
    if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return false;
    FpuGuard fpu;
    GpuWorkAlone alone;
    ID3D11Texture2D* texture = D3DTexture(canvas.gpu.image);
    if (!g_staging.Map(Context(), texture, static_cast<UINT>(x), static_cast<UINT>(y), 1, 1)) return false;
    const std::uint8_t* p = g_staging.Row(0);   // RGBA8
    *rgb = (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
    UnmapRegion(g_staging);
    return true;
}

bool ReadRgb565(const void* holder, int width, int height, std::uint16_t* out) {
    if (!out || !HasCanvas(holder) || width < 1 || height < 1 || width > kMaxCanvas || height > kMaxCanvas) return false;
    RunCanvasesNow();
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end() || !it->second.gpu.texture.id || !EnsurePipelines()) return false;
    const CanvasState& canvas = it->second;
    FpuGuard fpu;
    GpuWorkAlone alone;
    if (!EnsureScratch(g_picture, width, height, SG_PIXELFORMAT_R16UI, "ei-canvas-rgb565")) return false;
    BeginCanvasPass(g_picture.attachment, false, "ei-canvas-rgb565");
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_pipeline(g_rgb565);
    sg_bindings b{};
    b.views[0] = canvas.gpu.texture;
    b.samplers[0] = g_nearest;
    sg_apply_bindings(&b);
    ApplyRect(Rect{0, 0, width, height}, width, height);
    ui_params_t fs{};
    fs.source[2] = canvas.width;
    fs.source[3] = canvas.height;
    fs.dest[0] = width;
    fs.dest[1] = height;
    ApplyFs(fs);
    sg_draw(0, 6, 1);
    sg_end_pass();
    ID3D11Texture2D* texture = D3DTexture(g_picture.image);
    if (!g_pictureStaging.Map(Context(), texture, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height)))
        return false;
    for (int y = 0; y < height; ++y)
        std::memcpy(out + static_cast<std::size_t>(y) * static_cast<std::size_t>(width),
                    g_pictureStaging.Row(static_cast<unsigned>(y)), static_cast<std::size_t>(width) * sizeof(std::uint16_t));
    UnmapRegion(g_pictureStaging);
    return true;
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
    Rect r = Intersect(destination, Rect{0, 0, canvas->width, canvas->height});
    if (Empty(r)) return;
    Op op;
    op.kind = Op::Pixels;
    op.holder = holder;
    op.canvas = canvas->id;
    op.rect = r;
    const int w = r.right - r.left, h = r.bottom - r.top;
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
    for (CanvasImage& i : g_retired) DestroyImage(i);
    g_retired.clear();
    if (g_queue.empty()) return;
    if (!EnsurePipelines()) {
        g_queue.clear();
        return;
    }
    std::size_t k = 0;
    bool readBack = false;
    while (k < g_queue.size()) {
        const Op& op = g_queue[k];
        CanvasState* canvas = Find(op.holder, op.canvas);
        // A canvas without its image (made again after a lost device) starts black.
        if (canvas && op.kind != Op::Make && !canvas->gpu.image.id && !MakeImage(*canvas)) canvas = nullptr;
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
        }
        for (; k < end; ++k) Remember(*canvas, std::move(g_queue[k]));
        readBack = readBack || canvas->copied || canvas->history.size() > kMaxHistory;
    }
    g_queue.clear();
    if (readBack) {
        FpuGuard fpu;
        for (auto& [holder, canvas] : g_canvases)
            if ((canvas.copied || canvas.history.size() > kMaxHistory) && canvas.gpu.image.id) Compact(holder, canvas);
    }
}

void CanvasBlitsDrawn() {
    for (auto& entry : g_canvases) entry.second.blitPending = false;
}

void DrawCanvasBatch(const CanvasBatch& batch, int width, int height) {
    auto it = g_canvases.find(batch.holder);
    if (it == g_canvases.end() || !it->second.gpu.texture.id || !g_pipelinesMade) return;
    const CanvasState& canvas = it->second;
    int dw = batch.destination.right - batch.destination.left, dh = batch.destination.bottom - batch.destination.top;
    int sw = batch.source.right - batch.source.left, sh = batch.source.bottom - batch.source.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    sg_apply_pipeline(BlitPipelineFor(PassSampleCount()));
    sg_bindings b{};
    b.views[0] = canvas.gpu.texture;
    b.samplers[0] = g_nearest;
    sg_apply_bindings(&b);
    ApplyRect(batch.destination, width, height);
    ui_params_t fs{};
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
    sg_draw(0, 6, 1);
}

void DropCanvases() {
    // Each canvas is queued to be made again on the new device (black) with its history drawn on it, before
    // what was queued since the last frame.
    std::vector<Op> replay;
    for (auto& [holder, canvas] : g_canvases) {
        DestroyImage(canvas.gpu);
        Op make;
        make.kind = Op::Make;
        make.holder = holder;
        make.canvas = canvas.id;
        replay.push_back(std::move(make));
        for (Op& op : canvas.history) {
            op.holder = holder;   // (MoveCanvas leaves the history under the key it was drawn with)
            replay.push_back(std::move(op));
        }
        canvas.history.clear();
        canvas.copied = false;
    }
    for (Op& op : g_queue) replay.push_back(std::move(op));
    g_queue = std::move(replay);
    for (CanvasImage& i : g_retired) DestroyImage(i);
    g_retired.clear();
    for (auto& entry : g_atlases) {
        if (entry.second.view.id) sg_destroy_view(entry.second.view);
        if (entry.second.image.id) sg_destroy_image(entry.second.image);
    }
    g_atlases.clear();
    for (Scratch* s : {&g_coverage, &g_under, &g_picture}) {
        if (s->attachment.id) sg_destroy_view(s->attachment);
        if (s->texture.id) sg_destroy_view(s->texture);
        if (s->image.id) sg_destroy_image(s->image);
        *s = Scratch{};
    }
    if (g_vertices.id) sg_destroy_buffer(g_vertices);
    g_vertices = sg_buffer{};
    g_vertexCapacity = 0;
    for (const BlitPipeline& b : g_blits) sg_destroy_pipeline(b.pipeline);
    g_blits.clear();
    for (sg_pipeline* p : {&g_solid, &g_glyph, &g_compose, &g_grey, &g_copy, &g_rgb565}) {
        if (p->id) sg_destroy_pipeline(*p);
        *p = sg_pipeline{};
    }
    for (sg_shader* s : {&g_solidShader, &g_glyphShader, &g_composeShader, &g_greyShader, &g_copyShader, &g_rgb565Shader,
                         &g_blitShader}) {
        if (s->id) sg_destroy_shader(*s);
        *s = sg_shader{};
    }
    if (g_nearest.id) sg_destroy_sampler(g_nearest);
    g_nearest = sg_sampler{};
    g_pipelinesTried = g_pipelinesMade = false;
    g_staging.Release();
    g_pictureStaging.Release();
}

} // namespace detail

} // namespace render
