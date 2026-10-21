// The interface's 2D canvases (render_ui.h; ticket N1, docs/notes/ui-native.md section 3).
//
// A canvas is an RGBA8 render target holding a surface's pixels widened to 8 bits a channel. Its
// commands queue in call order (g_queue) and RunCanvasCommands draws them, outside any pass: render.cpp
// calls it at the start of DrawPending, so a canvas's drawing lands before the frame's next pass, and
// RunCanvasesNow for a read-back. Solid shapes are quads of a colour quantised on the CPU; a glyph run
// is three steps (its coverage into an R8 scratch target with MAX blending, a copy of what the canvas
// held there, the composition of the two into the canvas); greying works on a copy of the canvas; the
// screen copy samples the frame target. Every pixel the shaders write is quantised to the surface
// format, so a canvas never holds a colour its surface could not.
//
// render.cpp's part: Command::Canvas (DrawCanvasBatch inside the frame's passes), SplitFrame (the frame
// so far drawn before a canvas already blitted in it changes, or before a screen copy) and
// RunCanvasesNow. The offline check tools/ui_check plays that part itself.
#include "render_ui.h"
#include "render_internal.h"
#include "render_text.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

namespace {

using namespace detail;

constexpr int kMaxCanvas = 8192;

// ---- Pixel formats --------------------------------------------------------------------------------------

struct Channel {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;
};
Channel MakeChannel(std::uint32_t mask) {
    Channel c;
    c.mask = mask;
    if (!mask) return c;
    while (!((mask >> c.shift) & 1)) ++c.shift;
    while (c.shift + c.bits < 32 && ((mask >> (c.shift + c.bits)) & 1)) ++c.bits;
    return c;
}
// The top `bits` of a channel widened to 8 by repeating them (render_textures.cpp's Expand).
std::uint32_t WidenTop(std::uint32_t top, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return (top >> (bits - 8)) & 0xFF;
    std::uint32_t out = 0;
    for (int filled = 0; filled < 8; filled += bits) out |= (top << (8 - bits)) >> filled;
    return out & 0xFF;
}
std::uint32_t QuantiseChannel(std::uint32_t value, int bits) {
    return bits >= 8 ? value : WidenTop(value >> (8 - bits), bits);
}

// ---- Canvases and their queue -------------------------------------------------------------------------

struct CanvasImage {
    sg_image image{};
    sg_view attachment{}, texture{};
};

struct CanvasState {
    unsigned id = 0;   // queued commands name the canvas they were made for
    int width = 0, height = 0;
    ui::PixelFormat format;
    std::uint32_t bits[3] = {8, 8, 8};   // red, green, blue (at most 8)
    std::uint32_t greyMode = 2;          // ps_grey: 0 5:6:5, 1 5:5:5, 2 on 8-bit channels
    CanvasImage gpu;                     // made by RunCanvasCommands
    bool blitPending = false;            // blitted in the frame, the blit not drawn yet
};
std::unordered_map<const void*, CanvasState> g_canvases;
unsigned g_nextId = 1;

struct Op {
    enum Kind : std::uint8_t { Make, Solid, Glyphs, Copy, Grey };
    Kind kind = Solid;
    const void* holder = nullptr;
    unsigned canvas = 0;
    Rect rect{};                 // Solid: the rectangle; Glyphs: the run's bounds; Copy: the frame's source
    Rect scissor{};              // Glyphs: the clip rectangle within the canvas
    std::uint32_t colour = 0;    // Solid: 0x00RRGGBB as the canvas holds it; Glyphs: the text colour
    const text::GlyphAtlas* atlas = nullptr;
    std::vector<float> vertices;   // Glyphs: x, y, atlas x, atlas y per corner, six corners a quad
};
std::vector<Op> g_queue;

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

void PushSolid(const void* holder, const CanvasState& canvas, const Rect& rect, std::uint32_t rgb) {
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

std::uint32_t CanvasColour(ui::Colour colour, const CanvasState& canvas) { return ui::Quantise(colour, canvas.format); }

// ---- GPU objects ----------------------------------------------------------------------------------------

struct VsParams {
    float rect[4];
    float target[4];
};
struct FsParams {
    float colour[4];
    std::uint32_t bits[4];
    std::int32_t source[4];
    std::int32_t dest[4];
    float scale[4];
    std::uint32_t key[4];
};
static_assert(sizeof(FsParams) == 96);

bool g_pipelinesTried = false, g_pipelinesMade = false;
sg_shader g_solidShader{}, g_glyphShader{}, g_composeShader{}, g_greyShader{}, g_copyShader{}, g_blitShader{};
sg_pipeline g_solid{}, g_glyph{}, g_compose{}, g_grey{}, g_copy{};
// The frame blit per sample count of the pass (1, or the smooth option's multisampled one).
struct BlitPipeline {
    int samples;
    sg_pipeline pipeline;
};
std::vector<BlitPipeline> g_blits;
sg_sampler g_nearest{};

// Scratch targets as large as the largest canvas so far: a glyph run's coverage, a copy of a canvas.
struct Scratch {
    sg_image image{};
    sg_view attachment{}, texture{};
    int width = 0, height = 0;
};
Scratch g_coverage, g_under;

struct AtlasTexture {
    unsigned generation = 0;
    sg_image image{};
    sg_view view{};
};
std::unordered_map<const text::GlyphAtlas*, AtlasTexture> g_atlases;

sg_buffer g_vertices{};
std::size_t g_vertexCapacity = 0;

ID3D11DeviceContext* Context() { return static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context())); }
ID3D11Device* Device() { return static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device())); }
ID3D11Texture2D* TextureOf(sg_image image) {
    return static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));
}

// A shader of ui_canvas.hlsl: the vertex parameters, the fragment parameters, `textures` texture views
// (t0, t1) loaded directly (a non-filtering sampler paired with each, as sokol wants), and for the glyph
// shader its vertex (POSITION, float4).
sg_shader MakeShader(const ShaderBytecode& code, int textures, bool glyphVertex, const char* label) {
    sg_shader_desc s{};
    SetBytecode(s, code);
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(VsParams);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(FsParams);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    for (int t = 0; t < textures; ++t) {
        s.views[t].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        s.views[t].texture.image_type = SG_IMAGETYPE_2D;
        s.views[t].texture.sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT;
        s.views[t].texture.hlsl_register_t_n = static_cast<std::uint8_t>(t);
        s.texture_sampler_pairs[t].stage = SG_SHADERSTAGE_FRAGMENT;
        s.texture_sampler_pairs[t].view_slot = static_cast<std::uint8_t>(t);
        s.texture_sampler_pairs[t].sampler_slot = 0;
    }
    if (textures > 0) {
        s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        s.samplers[0].sampler_type = SG_SAMPLERTYPE_NONFILTERING;
        s.samplers[0].hlsl_register_s_n = 0;
    }
    if (glyphVertex) {
        s.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        s.attrs[0].hlsl_sem_name = "POSITION";
        s.attrs[0].hlsl_sem_index = 0;
    }
    s.label = label;
    return sg_make_shader(&s);
}

// A pipeline drawing into a canvas (RGBA8) or the coverage scratch (R8, MAX blending), no depth.
sg_pipeline MakeCanvasPipeline(sg_shader shader, bool coverage, const char* label) {
    sg_pipeline_desc p{};
    p.shader = shader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.colors[0].pixel_format = coverage ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
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
    g_solidShader = MakeShader(kUiSolidShader, 0, false, "ei-canvas-solid");
    g_glyphShader = MakeShader(kUiGlyphShader, 1, true, "ei-canvas-glyph");
    g_composeShader = MakeShader(kUiComposeShader, 2, false, "ei-canvas-compose");
    g_greyShader = MakeShader(kUiGreyShader, 1, false, "ei-canvas-grey");
    g_copyShader = MakeShader(kUiCopyShader, 1, false, "ei-canvas-copy");
    g_blitShader = MakeShader(kUiBlitShader, 1, false, "ei-canvas-blit");
    g_solid = MakeCanvasPipeline(g_solidShader, false, "ei-canvas-solid");
    g_glyph = MakeCanvasPipeline(g_glyphShader, true, "ei-canvas-glyph");
    g_compose = MakeCanvasPipeline(g_composeShader, false, "ei-canvas-compose");
    g_grey = MakeCanvasPipeline(g_greyShader, false, "ei-canvas-grey");
    g_copy = MakeCanvasPipeline(g_copyShader, false, "ei-canvas-copy");
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_NEAREST;
    sd.mag_filter = SG_FILTER_NEAREST;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sd.label = "ei-canvas";
    g_nearest = sg_make_sampler(&sd);
    g_pipelinesMade = true;
    for (sg_pipeline p : {g_solid, g_glyph, g_compose, g_grey, g_copy})
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
    p.depth.compare = SG_COMPAREFUNC_ALWAYS;   // as the window blit: the depth buffer ignored and kept
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

void ApplyRect(const Rect& r, int width, int height) {
    VsParams vs{{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom)},
                {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f}};
    sg_range range{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &range);
}

FsParams CanvasParams(const CanvasState& canvas) {
    FsParams fs{};
    for (int i = 0; i < 3; ++i) fs.bits[i] = canvas.bits[i];
    fs.bits[3] = canvas.greyMode;
    return fs;
}

void ApplyFs(const FsParams& fs) {
    sg_range range{&fs, sizeof(fs)};
    sg_apply_uniforms(1, &range);
}

// `r` of the canvas's image copied to the same place in g_under.
void CopyUnder(const CanvasState& canvas, const Rect& r) {
    ID3D11Texture2D* from = TextureOf(canvas.gpu.image);
    ID3D11Texture2D* to = TextureOf(g_under.image);
    if (!from || !to) return;
    D3D11_BOX box{static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0, static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), 1};
    Context()->CopySubresourceRegion(to, 0, static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0, from, 0, &box);
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
        FsParams fs = CanvasParams(canvas);
        fs.colour[0] = static_cast<float>((op.colour >> 16) & 0xFF) / 255.0f;
        fs.colour[1] = static_cast<float>((op.colour >> 8) & 0xFF) / 255.0f;
        fs.colour[2] = static_cast<float>(op.colour & 0xFF) / 255.0f;
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

// The vertices appended to the stream buffer (grown when they do not fit): their offset, or -1.
int AppendVertices(const std::vector<float>& vertices) {
    std::size_t bytes = vertices.size() * sizeof(float);
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
            return -1;
        }
    }
    sg_range range{vertices.data(), bytes};
    int offset = sg_append_buffer(g_vertices, &range);
    return sg_query_buffer_overflow(g_vertices) ? -1 : offset;
}

void DrawGlyphs(const CanvasState& canvas, const Op& op) {
    if (!op.atlas || !EnsureScratch(g_coverage, canvas.width, canvas.height, SG_PIXELFORMAT_R8, "ei-canvas-coverage") ||
        !EnsureScratch(g_under, canvas.width, canvas.height, SG_PIXELFORMAT_RGBA8, "ei-canvas-under"))
        return;
    sg_view atlas = AtlasView(*op.atlas);
    int offset = AppendVertices(op.vertices);
    if (!atlas.id || offset < 0) return;
    // The run's coverage.
    BeginCanvasPass(g_coverage.attachment, true, "ei-canvas-coverage");
    sg_apply_viewport(0, 0, canvas.width, canvas.height, true);
    sg_apply_scissor_rect(op.scissor.left, op.scissor.top, op.scissor.right - op.scissor.left, op.scissor.bottom - op.scissor.top, true);
    sg_apply_pipeline(g_glyph);
    sg_bindings b{};
    b.vertex_buffers[0] = g_vertices;
    b.vertex_buffer_offsets[0] = offset;
    b.views[0] = atlas;
    b.samplers[0] = g_nearest;
    sg_apply_bindings(&b);
    ApplyRect(Rect{0, 0, canvas.width, canvas.height}, canvas.width, canvas.height);
    ApplyFs(CanvasParams(canvas));
    sg_draw(0, static_cast<int>(op.vertices.size() / 4), 1);
    sg_end_pass();
    // What the canvas held there, then the two composed into it.
    CopyUnder(canvas, op.rect);
    BeginCanvasPass(canvas.gpu.attachment, false, "ei-canvas-compose");
    sg_apply_pipeline(g_compose);
    sg_bindings c{};
    c.views[0] = g_coverage.texture;
    c.views[1] = g_under.texture;
    c.samplers[0] = g_nearest;
    sg_apply_bindings(&c);
    ApplyRect(op.rect, canvas.width, canvas.height);
    FsParams fs = CanvasParams(canvas);
    fs.colour[0] = static_cast<float>(op.colour & 0xFF);
    fs.colour[1] = static_cast<float>((op.colour >> 8) & 0xFF);
    fs.colour[2] = static_cast<float>((op.colour >> 16) & 0xFF);
    ApplyFs(fs);
    sg_draw(0, 6, 1);
    sg_end_pass();
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
    FsParams fs = CanvasParams(canvas);
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

void DrawGrey(const CanvasState& canvas) {
    if (!EnsureScratch(g_under, canvas.width, canvas.height, SG_PIXELFORMAT_RGBA8, "ei-canvas-under")) return;
    Rect whole{0, 0, canvas.width, canvas.height};
    CopyUnder(canvas, whole);
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

// D3D11 staging texture for read-backs, kept and grown.
ID3D11Texture2D* g_staging = nullptr;
UINT g_stagingWidth = 0, g_stagingHeight = 0;

void ReleaseStaging() {
    if (g_staging) g_staging->Release();
    g_staging = nullptr;
    g_stagingWidth = g_stagingHeight = 0;
}

} // namespace

// ---- The public commands --------------------------------------------------------------------------------

namespace ui {

std::uint32_t Quantise(Colour colour, const PixelFormat& format) {
    std::uint32_t r = colour & 0xFF, g = (colour >> 8) & 0xFF, b = (colour >> 16) & 0xFF;
    return (QuantiseChannel(r, MakeChannel(format.redMask).bits) << 16) | (QuantiseChannel(g, MakeChannel(format.greenMask).bits) << 8) |
           QuantiseChannel(b, MakeChannel(format.blueMask).bits);
}

std::uint32_t Widen(std::uint32_t pixel, const PixelFormat& format) {
    std::uint32_t out = 0;
    const std::uint32_t masks[3] = {format.redMask, format.greenMask, format.blueMask};
    for (int i = 0; i < 3; ++i) {
        Channel c = MakeChannel(masks[i]);
        out = (out << 8) | WidenTop((pixel & c.mask) >> c.shift, c.bits);
    }
    return out;
}

std::uint32_t Pack(std::uint32_t rgb, const PixelFormat& format) {
    std::uint32_t out = 0;
    const std::uint32_t masks[3] = {format.redMask, format.greenMask, format.blueMask};
    for (int i = 0; i < 3; ++i) {
        Channel c = MakeChannel(masks[i]);
        if (!c.bits) continue;
        std::uint32_t v = (rgb >> (16 - 8 * i)) & 0xFF;
        out |= (c.bits >= 8 ? v << (c.shift + c.bits - 8) : (v >> (8 - c.bits)) << c.shift) & c.mask;
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
    for (int i = 0; i < 3; ++i) canvas->bits[i] = static_cast<std::uint32_t>(std::min(MakeChannel(masks[i]).bits, 8));
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
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end()) return;
    g_retired.push_back(it->second.gpu);
    g_canvases.erase(it);
}

void Fill(const void* holder, const Rect& rect, Colour colour) {
    CanvasState* canvas = Target(holder);
    if (canvas) PushSolid(holder, *canvas, rect, CanvasColour(colour, *canvas));
}

void FrameRect(const void* holder, const Rect& rect, Colour colour) {
    CanvasState* canvas = Target(holder);
    if (!canvas || Empty(rect)) return;
    std::uint32_t rgb = CanvasColour(colour, *canvas);
    PushSolid(holder, *canvas, Rect{rect.left, rect.top, rect.right, rect.top + 1}, rgb);
    PushSolid(holder, *canvas, Rect{rect.left, rect.bottom - 1, rect.right, rect.bottom}, rgb);
    PushSolid(holder, *canvas, Rect{rect.left, rect.top + 1, rect.left + 1, rect.bottom - 1}, rgb);
    PushSolid(holder, *canvas, Rect{rect.right - 1, rect.top + 1, rect.right, rect.bottom - 1}, rgb);
}

void Line(const void* holder, int x0, int y0, int x1, int y1, int width, Colour colour) {
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

void Pixel(const void* holder, int x, int y, Colour colour) {
    CanvasState* canvas = Target(holder);
    if (canvas) PushSolid(holder, *canvas, Rect{x, y, x + 1, y + 1}, CanvasColour(colour, *canvas));
}

void GlyphRun(const void* holder, text::TextFont& font, const text::TextLayout& layout, Colour colour) {
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
        const float u1 = u < 0.0f ? u : u + static_cast<float>(w), v1 = u < 0.0f ? v : v + static_cast<float>(h);
        const float corners[6][4] = {{x0, y0, u, v}, {x1, y0, u1, v}, {x1, y1, u1, v1}, {x0, y0, u, v}, {x1, y1, u1, v1}, {x0, y1, u, v1}};
        for (const auto& c : corners) op.vertices.insert(op.vertices.end(), c, c + 4);
        bounds = Rect{std::min(bounds.left, x), std::min(bounds.top, y), std::max(bounds.right, x + w), std::max(bounds.bottom, y + h)};
    };
    for (const text::GlyphQuad& q : layout.quads)
        quad(q.x, q.y, q.width, q.height, static_cast<float>(q.atlasX), static_cast<float>(q.atlasY));
    for (const text::Rect& u : layout.underlines) quad(u.left, u.top, u.right - u.left, u.bottom - u.top, -1.0f, -1.0f);
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
    ID3D11Texture2D* texture = TextureOf(canvas.gpu.image);
    if (!texture) return false;
    UINT w = static_cast<UINT>(canvas.width), h = static_cast<UINT>(canvas.height);
    if (!g_staging || g_stagingWidth < w || g_stagingHeight < h) {
        ReleaseStaging();
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(Device()->CreateTexture2D(&desc, nullptr, &g_staging))) {
            g_staging = nullptr;
            return false;
        }
        g_stagingWidth = w;
        g_stagingHeight = h;
    }
    D3D11_BOX box{0, 0, 0, w, h, 1};
    ID3D11DeviceContext* context = Context();
    context->CopySubresourceRegion(g_staging, 0, 0, 0, 0, texture, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(g_staging, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    rgb->resize(static_cast<std::size_t>(w) * h);
    for (UINT y = 0; y < h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        std::uint32_t* out = rgb->data() + static_cast<std::size_t>(y) * w;
        for (UINT x = 0; x < w; ++x)   // RGBA8
            out[x] = (static_cast<std::uint32_t>(row[x * 4]) << 16) | (static_cast<std::uint32_t>(row[x * 4 + 1]) << 8) | row[x * 4 + 2];
    }
    context->Unmap(g_staging, 0);
    sg_reset_state_cache();
    if (width) *width = canvas.width;
    if (height) *height = canvas.height;
    return true;
}

bool ReadBack(const void* holder) {
    const SurfaceMemory* memory = SurfaceMemoryOf(holder);
    if (!memory || (memory->bitsPerPixel != 16 && memory->bitsPerPixel != 32)) return false;
    std::vector<std::uint32_t> rgb;
    int w = 0, h = 0;
    if (!ReadPixels(holder, &rgb, &w, &h) || w != memory->width || h != memory->height) return false;
    PixelFormat format{memory->bitsPerPixel, memory->redMask, memory->greenMask, memory->blueMask};
    for (int y = 0; y < h; ++y) {
        auto* row = static_cast<std::uint8_t*>(memory->bits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(memory->pitch);
        const std::uint32_t* in = rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        if (memory->bitsPerPixel == 32) {
            auto* out = reinterpret_cast<std::uint32_t*>(row);
            for (int x = 0; x < w; ++x) out[x] = Pack(in[x], format) | memory->alphaMask;
        } else {
            auto* out = reinterpret_cast<std::uint16_t*>(row);
            for (int x = 0; x < w; ++x) out[x] = static_cast<std::uint16_t>(Pack(in[x], format) | memory->alphaMask);
        }
    }
    SurfaceWritten(holder, nullptr);
    return true;
}

void DrawCanvas(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key) {
    auto it = g_canvases.find(holder);
    if (it == g_canvases.end()) return;
    CanvasState& canvas = it->second;
    Frame& f = CurrentFrame();
    CanvasBatch batch{holder, destination, source ? *source : Rect{0, 0, canvas.width, canvas.height}, keyed,
                      Widen(key, canvas.format)};
    f.canvasBatches.push_back(batch);
    f.commands.push_back(Command{Command::Canvas, static_cast<int>(f.canvasBatches.size()) - 1});
    canvas.blitPending = true;
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
    while (k < g_queue.size()) {
        const Op& op = g_queue[k];
        CanvasState* canvas = Find(op.holder, op.canvas);
        // A canvas without its image (made again after a lost device) starts black.
        if (canvas && op.kind != Op::Make && !canvas->gpu.image.id && !MakeImage(*canvas)) canvas = nullptr;
        if (!canvas) {
            ++k;
            continue;
        }
        switch (op.kind) {
        case Op::Make: MakeImage(*canvas); break;
        case Op::Solid: {
            std::size_t end = k + 1;
            while (end < g_queue.size() && g_queue[end].kind == Op::Solid && g_queue[end].holder == op.holder &&
                   g_queue[end].canvas == op.canvas)
                ++end;
            DrawSolids(*canvas, k, end);
            k = end;
            continue;
        }
        case Op::Glyphs: DrawGlyphs(*canvas, op); break;
        case Op::Copy: DrawCopy(*canvas, op); break;
        case Op::Grey: DrawGrey(*canvas); break;
        }
        ++k;
    }
    g_queue.clear();
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
    FsParams fs{};
    fs.source[0] = batch.source.left;
    fs.source[1] = batch.source.top;
    fs.source[2] = canvas.width;
    fs.source[3] = canvas.height;
    fs.dest[0] = batch.destination.left;
    fs.dest[1] = batch.destination.top;
    fs.scale[0] = static_cast<float>(sw) / static_cast<float>(dw);
    fs.scale[1] = static_cast<float>(sh) / static_cast<float>(dh);
    fs.key[0] = (batch.key >> 16) & 0xFF;
    fs.key[1] = (batch.key >> 8) & 0xFF;
    fs.key[2] = batch.key & 0xFF;
    fs.key[3] = batch.keyed ? 1u : 0u;
    ApplyFs(fs);
    sg_draw(0, 6, 1);
}

void DropCanvases() {
    for (auto& entry : g_canvases) DestroyImage(entry.second.gpu);
    for (CanvasImage& i : g_retired) DestroyImage(i);
    g_retired.clear();
    for (auto& entry : g_atlases) {
        if (entry.second.view.id) sg_destroy_view(entry.second.view);
        if (entry.second.image.id) sg_destroy_image(entry.second.image);
    }
    g_atlases.clear();
    for (Scratch* s : {&g_coverage, &g_under}) {
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
    for (sg_pipeline* p : {&g_solid, &g_glyph, &g_compose, &g_grey, &g_copy}) {
        if (p->id) sg_destroy_pipeline(*p);
        *p = sg_pipeline{};
    }
    for (sg_shader* s : {&g_solidShader, &g_glyphShader, &g_composeShader, &g_greyShader, &g_copyShader, &g_blitShader}) {
        if (s->id) sg_destroy_shader(*s);
        *s = sg_shader{};
    }
    if (g_nearest.id) sg_destroy_sampler(g_nearest);
    g_nearest = sg_sampler{};
    g_pipelinesTried = g_pipelinesMade = false;
    ReleaseStaging();
}

} // namespace detail

} // namespace render
