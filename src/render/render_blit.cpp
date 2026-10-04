// The interface windows' surfaces in the new renderer. The game composes most of its interface on
// the CPU: every window (CUiSurface) owns an off-screen DirectDraw surface that it fills with GDI
// text, fills and pixel code, and 005FDA80 blits it onto the frame (plainly or with a source colour
// key) in the windows' draw order, between the sprites and 3D previews of the same windows. The
// new renderer reads such a surface when its contents changed (render_textures.cpp) and draws it as
// a screen rectangle that copies texels one to one (a stretch when the sizes differ), keyed texels
// left out. Also here: the depth clear the old path does in the middle of the frame.
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ddraw.h>

#include <cstring>
#include <vector>

namespace render {

namespace {

using namespace detail;

const char* const kBlitSource = R"(
cbuffer vs_params : register(b0) {
    float4 rect;     // destination left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height
};
cbuffer fs_params : register(b0) {
    float4 dest;     // x, y: destination left, top; z, w: source texels per destination pixel
    float4 source;   // x, y: source left, top; z, w: texture width, height
};
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}
Texture2D<float4> tex : register(t0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 texel = source.xy + floor((pos.xy - dest.xy) * dest.zw);
    texel = clamp(texel, float2(0.0, 0.0), source.zw - 1.0);
    float4 c = tex.Load(int3(texel, 0));
    if (c.a < 0.5) discard;   // the colour key
    return float4(c.rgb, 1.0);
}
)";

const char* const kClearDepthSource = R"(
cbuffer vs_params : register(b0) {
    float4 rect;     // left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height; z: the depth
};
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, target.z, 1.0);
}
float4 ps_main(float4 pos : SV_Position) : SV_Target0 { return float4(0.0, 0.0, 0.0, 0.0); }
)";

struct VsParams {
    float rect[4];
    float target[4];
};
struct FsParams {
    float dest[4];
    float source[4];
};

sg_shader g_blitShader{}, g_clearShader{};
sg_pipeline g_blit{}, g_clear{};
sg_sampler g_nearest{};

} // namespace

namespace detail {

bool CreateBlitPipeline() {
    sg_shader_desc s{};
    s.vertex_func.source = kBlitSource;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = kBlitSource;
    s.fragment_func.entry = "ps_main";
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(VsParams);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(FsParams);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    // The shader loads texels directly; sokol still wants a sampler paired with the texture.
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_NONFILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    s.label = "ei-window-blit";
    g_blitShader = sg_make_shader(&s);
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_NEAREST;
    sd.mag_filter = SG_FILTER_NEAREST;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sd.label = "ei-window-blit";
    g_nearest = sg_make_sampler(&sd);
    sg_pipeline_desc p{};
    p.shader = g_blitShader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_ALWAYS;   // a blit ignores and keeps the depth buffer
    p.depth.write_enabled = false;
    p.colors[0].pixel_format = kColourFormat;
    p.colors[0].write_mask = SG_COLORMASK_RGB;
    p.sample_count = 1;
    p.label = "ei-window-blit";
    g_blit = sg_make_pipeline(&p);

    sg_shader_desc c{};
    c.vertex_func.source = kClearDepthSource;
    c.vertex_func.entry = "vs_main";
    c.fragment_func.source = kClearDepthSource;
    c.fragment_func.entry = "ps_main";
    c.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    c.uniform_blocks[0].size = sizeof(VsParams);
    c.uniform_blocks[0].hlsl_register_b_n = 0;
    c.label = "ei-clear-depth";
    g_clearShader = sg_make_shader(&c);
    sg_pipeline_desc q{};
    q.shader = g_clearShader;
    q.depth.pixel_format = kDepthFormat;
    q.depth.compare = SG_COMPAREFUNC_ALWAYS;
    q.depth.write_enabled = true;
    q.colors[0].pixel_format = kColourFormat;
    q.colors[0].write_mask = SG_COLORMASK_NONE;
    q.sample_count = 1;
    q.label = "ei-clear-depth";
    g_clear = sg_make_pipeline(&q);
    bool ok = sg_query_pipeline_state(g_blit) == SG_RESOURCESTATE_VALID &&
              sg_query_pipeline_state(g_clear) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the window blit or depth clear pipeline could not be created (see the sokol messages above)");
    return ok;
}

void DrawBlitBatch(const Frame& /*frame*/, const BlitBatch& b, int width, int height) {
    if (!b.texture.id || b.textureWidth <= 0 || b.textureHeight <= 0) return;
    int dw = b.destination.right - b.destination.left, dh = b.destination.bottom - b.destination.top;
    int sw = b.source.right - b.source.left, sh = b.source.bottom - b.source.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    sg_apply_pipeline(g_blit);
    sg_bindings bind{};
    bind.views[0] = b.texture;
    bind.samplers[0] = g_nearest;
    sg_apply_bindings(&bind);
    VsParams vs{{static_cast<float>(b.destination.left), static_cast<float>(b.destination.top),
                 static_cast<float>(b.destination.right), static_cast<float>(b.destination.bottom)},
                {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f}};
    FsParams fs{{static_cast<float>(b.destination.left), static_cast<float>(b.destination.top),
                 static_cast<float>(sw) / static_cast<float>(dw), static_cast<float>(sh) / static_cast<float>(dh)},
                {static_cast<float>(b.source.left), static_cast<float>(b.source.top), static_cast<float>(b.textureWidth),
                 static_cast<float>(b.textureHeight)}};
    sg_range vr{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vr);
    sg_range fr{&fs, sizeof(fs)};
    sg_apply_uniforms(1, &fr);
    sg_draw(0, 6, 1);
}

void DrawClearDepth(const ClearBatch& clear, int width, int height) {
    if (!g_clear.id) return;
    Rect r = clear.rect.right > clear.rect.left ? clear.rect : Rect{0, 0, width, height};
    if (r.bottom <= r.top) return;
    sg_apply_pipeline(g_clear);
    VsParams vs{{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom)},
                {static_cast<float>(width), static_cast<float>(height), clear.z, 0.0f}};
    sg_range vr{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vr);
    sg_draw(0, 6, 1);
}

void DestroyBlits() {
    if (g_blit.id) sg_destroy_pipeline(g_blit);
    if (g_clear.id) sg_destroy_pipeline(g_clear);
    if (g_blitShader.id) sg_destroy_shader(g_blitShader);
    if (g_clearShader.id) sg_destroy_shader(g_clearShader);
    if (g_nearest.id) sg_destroy_sampler(g_nearest);
    g_blit = g_clear = sg_pipeline{};
    g_blitShader = g_clearShader = sg_shader{};
    g_nearest = sg_sampler{};
}

} // namespace detail

namespace {
std::vector<sg_image> g_frameImages;   // made for one frame (the cursor), destroyed at the present
std::vector<sg_view> g_frameViews;
} // namespace

namespace detail {
void RetireFrameImages() {
    for (sg_view v : g_frameViews) sg_destroy_view(v);
    for (sg_image i : g_frameImages) sg_destroy_image(i);
    g_frameViews.clear();
    g_frameImages.clear();
}
} // namespace detail

void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to) {
    if (!imageHolder || !mask || !Active() || !Started()) return;
    auto* dd = *reinterpret_cast<IDirectDrawSurface7* const*>(static_cast<const char*>(imageHolder) + 0x10);
    int w = from.right - from.left, h = from.bottom - from.top;
    if (!dd || w <= 0 || h <= 0 || w > 256 || h > 256) return;
    FpuGuard fpu;
    DDSURFACEDESC2 d{};
    d.dwSize = sizeof(d);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    LockGameDevice();
    HRESULT hr = dd->Lock(nullptr, &d, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    const DDPIXELFORMAT& pf = d.ddpfPixelFormat;
    int bytes = static_cast<int>(pf.dwRGBBitCount / 8);
    bool ok = SUCCEEDED(hr) && d.lpSurface && (pf.dwFlags & DDPF_RGB) && (bytes == 2 || bytes == 4) && from.left >= 0 &&
              from.top >= 0 && from.right <= static_cast<int>(d.dwWidth) && from.bottom <= static_cast<int>(d.dwHeight);
    if (ok) {
        auto expand = [](std::uint32_t pixel, std::uint32_t m) -> std::uint8_t {
            if (!m) return 0;
            int shift = 0, bits = 0;
            while (!((m >> shift) & 1)) ++shift;
            while (shift + bits < 32 && ((m >> (shift + bits)) & 1)) ++bits;
            std::uint32_t v = (pixel & m) >> shift;
            return static_cast<std::uint8_t>(bits >= 8 ? v >> (bits - 8) : (v << (8 - bits)) | (v >> (2 * bits - 8 > 0 ? 2 * bits - 8 : 0)));
        };
        std::uint32_t colourBits = pf.dwRBitMask | pf.dwGBitMask | pf.dwBBitMask;
        for (int y = 0; y < h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(d.lpSurface) + static_cast<std::size_t>(from.top + y) * d.lPitch;
            const auto* mrow = static_cast<const std::uint8_t*>(mask) +
                               (static_cast<std::size_t>(from.top + y) * d.dwWidth + from.left) * bytes;
            for (int x = 0; x < w; ++x) {
                std::uint32_t pixel = 0, m = 0;
                std::memcpy(&pixel, row + static_cast<std::size_t>(from.left + x) * bytes, static_cast<std::size_t>(bytes));
                std::memcpy(&m, mrow + static_cast<std::size_t>(x) * bytes, static_cast<std::size_t>(bytes));
                std::uint8_t* o = &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
                o[0] = expand(pixel, pf.dwRBitMask);
                o[1] = expand(pixel, pf.dwGBitMask);
                o[2] = expand(pixel, pf.dwBBitMask);
                o[3] = (m & colourBits) == 0 ? 255 : 0;
            }
        }
    }
    if (SUCCEEDED(hr)) dd->Unlock(nullptr);
    UnlockGameDevice();
    if (!ok) return;
    sg_image_desc desc{};
    desc.width = w;
    desc.height = h;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = sg_range{rgba.data(), rgba.size()};
    desc.label = "ei-cursor";
    sg_image image = sg_make_image(&desc);
    sg_view_desc vd{};
    vd.texture.image = image;
    sg_view view = sg_make_view(&vd);
    g_frameImages.push_back(image);
    g_frameViews.push_back(view);
    Frame& f = CurrentFrame();
    f.blitBatches.push_back(BlitBatch{view, w, h, to, Rect{0, 0, w, h}});
    f.commands.push_back(Command{Command::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}

void SurfaceBlit(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key) {
    if (!holder || !Active() || !Started()) return;
    FpuGuard fpu;
    int width = 0, height = 0;
    sg_view view = WindowSurfaceView(holder, keyed, key, &width, &height);
    if (!view.id) return;
    Frame& f = CurrentFrame();
    BlitBatch b{view, width, height, destination, source ? *source : Rect{0, 0, width, height}};
    f.blitBatches.push_back(b);
    f.commands.push_back(Command{Command::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}

} // namespace render
