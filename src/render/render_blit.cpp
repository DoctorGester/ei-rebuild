// The interface windows' surfaces in the new renderer. The game composes most of its interface on
// the CPU: every window (CUiSurface) owns an off-screen surface holder whose pixels (memory the new
// renderer owns) it fills with GDI text, fills and pixel code, and 005FDA80 blits it onto the frame
// (plainly or with a source colour key) in the windows' draw order, between the sprites and 3D
// previews of the same windows. The new renderer reads such a surface when its contents changed
// (render_textures.cpp) and draws it as
// a screen rectangle that copies texels one to one (a stretch when the sizes differ), keyed texels
// left out. Also here: the depth clear the old path does in the middle of the frame.
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include "../game/texture.h"   // CDXSurface

#include <cstring>
#include <unordered_map>
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
// For the smooth option's multisampled passes (render_smooth.cpp), made at their first use.
sg_pipeline g_blitMultisampled{}, g_clearMultisampled{};
int g_multisampledSamples = 0;
sg_sampler g_nearest{};

sg_pipeline MakeBlitPipeline(int samples) {
    sg_pipeline_desc p{};
    p.shader = g_blitShader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_ALWAYS;   // a blit ignores and keeps the depth buffer
    p.depth.write_enabled = false;
    p.colors[0].pixel_format = kColourFormat;
    p.colors[0].write_mask = SG_COLORMASK_RGB;
    p.sample_count = samples;
    p.label = "ei-window-blit";
    return sg_make_pipeline(&p);
}

sg_pipeline MakeClearPipeline(int samples) {
    sg_pipeline_desc q{};
    q.shader = g_clearShader;
    q.depth.pixel_format = kDepthFormat;
    q.depth.compare = SG_COMPAREFUNC_ALWAYS;
    q.depth.write_enabled = true;
    q.colors[0].pixel_format = kColourFormat;
    q.colors[0].write_mask = SG_COLORMASK_NONE;
    q.sample_count = samples;
    q.label = "ei-clear-depth";
    return sg_make_pipeline(&q);
}

void DestroyMultisampledPipelines() {
    if (g_blitMultisampled.id) sg_destroy_pipeline(g_blitMultisampled);
    if (g_clearMultisampled.id) sg_destroy_pipeline(g_clearMultisampled);
    g_blitMultisampled = g_clearMultisampled = sg_pipeline{};
    g_multisampledSamples = 0;
}

// The blit's or the depth clear's pipeline for the pass being drawn.
sg_pipeline PassPipeline(bool clear) {
    int samples = PassSampleCount();
    if (samples <= 1) return clear ? g_clear : g_blit;
    if (g_multisampledSamples != samples) {
        DestroyMultisampledPipelines();
        g_blitMultisampled = MakeBlitPipeline(samples);
        g_clearMultisampled = MakeClearPipeline(samples);
        g_multisampledSamples = samples;
    }
    return clear ? g_clearMultisampled : g_blitMultisampled;
}

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
    g_blit = MakeBlitPipeline(1);

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
    g_clear = MakeClearPipeline(1);
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
    sg_apply_pipeline(PassPipeline(false));
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
    sg_apply_pipeline(PassPipeline(true));
    VsParams vs{{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom)},
                {static_cast<float>(width), static_cast<float>(height), clear.z, 0.0f}};
    sg_range vr{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vr);
    sg_draw(0, 6, 1);
}

void BlitParts(sg_shader* shader, sg_pipeline* pipeline, sg_sampler* sampler) {
    *shader = g_blitShader;
    *pipeline = g_blit;
    *sampler = g_nearest;
}

void DestroyBlits() {
    DestroyMultisampledPipelines();
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

namespace detail {
namespace {
long g_imageMade[static_cast<int>(ImageUse::Count)];
long g_imageUpdated[static_cast<int>(ImageUse::Count)];
long g_imageRequests[static_cast<int>(ImageUse::Count)];   // pictures asked for (cursor, fill, movie frames)
} // namespace

void CountImage(ImageUse use, bool made) { ++(made ? g_imageMade : g_imageUpdated)[static_cast<int>(use)]; }
void CountImageRequest(ImageUse use) { ++g_imageRequests[static_cast<int>(use)]; }

void LogImageCounts(const char* when) {
    static const char* const names[] = {"cursor", "fill", "movie", "window surface", "texture"};
    for (int i = 0; i < static_cast<int>(ImageUse::Count); ++i)
        ei::Log("render: images %s, frame %u: %s: %ld requested, %ld made, %ld updated in place", when,
                static_cast<unsigned>(FrameNumber()), names[i], g_imageRequests[i], g_imageMade[i], g_imageUpdated[i]);
}

#if defined(EI_RENDER_VALIDATION)
namespace {
struct ImageCountLogger {
    ~ImageCountLogger() { LogImageCounts("at exit"); }
} g_imageCountLogger;
} // namespace
#endif
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

namespace {

// RGBA8 images that are updated in place (sg_update_image) instead of made again. Sokol allows
// one update per image and frame, and a recorded blit draws the image's contents at the end of the
// frame (DrawPending), so an image is rewritten only when no blit of it is recorded in this frame
// and it was not rewritten since the last commit.
sg_image MakeDynamicImage(int w, int h, const char* label, ImageUse use) {
    sg_image_desc desc{};
    desc.width = w;
    desc.height = h;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.dynamic_update = true;
    desc.label = label;
    CountImage(use, true);
    return sg_make_image(&desc);
}

// A texture view of `image`; none (and the image destroyed) when it could not be made.
sg_view MakeImageView(sg_image image) {
    if (!image.id) return sg_view{};
    sg_view view{};
    if (sg_query_image_state(image) == SG_RESOURCESTATE_VALID) {
        sg_view_desc vd{};
        vd.texture.image = image;
        view = sg_make_view(&vd);
        if (view.id && sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
            sg_destroy_view(view);
            view = sg_view{};
        }
    }
    if (!view.id) sg_destroy_image(image);
    return view;
}

#if defined(EI_RENDER_VALIDATION)
long g_uploadChecks = 0, g_uploadMismatches = 0;
// Verify build: reads the image back from the GPU and compares it with the pixels just written
// (what the blit's unchanged shader then draws is exactly these texels).
void CheckUpload(sg_image image, const std::uint8_t* rgba, int w, int h, const char* what) {
    sg_d3d11_image_info info = sg_d3d11_query_image_info(image);
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(info.tex2d));
    if (!texture) return;
    ID3D11Device* device = nullptr;
    texture->GetDevice(&device);
    if (!device) return;
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (context && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging))) {
        context->CopyResource(staging, texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            bool same = true;
            for (int y = 0; y < h && same; ++y)
                same = std::memcmp(static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                                   rgba + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4) == 0;
            context->Unmap(staging, 0);
            ++g_uploadChecks;
            if (!same && g_uploadMismatches++ < 10)
                ei::Log("render: MISMATCH: %s image %dx%d read back from the GPU differs from the pixels written", what, w, h);
        }
        staging->Release();
    }
    if (context) context->Release();
    device->Release();
    sg_reset_state_cache();
}
#endif

void UpdateImage(sg_image image, const std::uint8_t* rgba, int w, int h, ImageUse use) {
    sg_image_data data{};
    data.mip_levels[0] = sg_range{rgba, static_cast<std::size_t>(w) * h * 4};
    CountImage(use, false);
    sg_update_image(image, &data);
#if defined(EI_RENDER_VALIDATION)
    CheckUpload(image, rgba, w, h, use == ImageUse::Movie ? "movie" : "cursor/fill");
    if (g_uploadChecks % 20 == 1 && g_uploadChecks) ei::Log("render: image uploads checked back from the GPU: %ld, mismatches %ld", g_uploadChecks, g_uploadMismatches);
#endif
}

struct Movie {
    sg_image image{};
    sg_view view{};
    int width = 0, height = 0;
    std::uint32_t blitFrame = 0xFFFFFFFFu;      // FrameNumber() of the last blit recorded
    std::uint32_t updatedCommit = 0xFFFFFFFFu;  // CommitNumber() of the last update
};
std::unordered_map<const void*, Movie> g_movies;
std::vector<std::uint8_t> g_movieScratch;

// The cursor's and the fills' small images, kept across frames: one per size, rewritten only when
// the content differs (compared with a copy of what the image holds). Several images of one size
// are kept when a frame draws different contents of it.
struct CachedImage {
    sg_image image{};
    sg_view view{};
    int width = 0, height = 0;
    std::uint32_t usedFrame = 0xFFFFFFFFu;      // FrameNumber() of the last blit recorded
    std::uint32_t updatedCommit = 0xFFFFFFFFu;  // CommitNumber() of the last update
    std::uint64_t lastUse = 0;                  // for dropping the least recently used
    std::vector<std::uint8_t> pixels;
};
std::vector<CachedImage> g_cache;
std::uint64_t g_cacheClock = 0;
constexpr std::size_t kCacheMax = 64;

} // namespace

void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height) {
    if (!key || !rgbx || width <= 0 || height <= 0 || !Active() || !Started()) return;
    FpuGuard fpu;
    CountImageRequest(ImageUse::Movie);
    g_movieScratch.assign(rgbx, rgbx + static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 3; i < g_movieScratch.size(); i += 4) g_movieScratch[i] = 255;
    Movie& m = g_movies[key];
    if (m.image.id && m.width == width && m.height == height && m.blitFrame != FrameNumber() &&
        m.updatedCommit != CommitNumber()) {
        UpdateImage(m.image, g_movieScratch.data(), width, height, ImageUse::Movie);
        m.updatedCommit = CommitNumber();
        return;
    }
    sg_image image = MakeDynamicImage(width, height, "ei-movie-frame", ImageUse::Movie);
    sg_view view = MakeImageView(image);
    if (!view.id) return;
    UpdateImage(image, g_movieScratch.data(), width, height, ImageUse::Movie);
    // The previous image may still be drawn by this frame's commands: destroyed after the present.
    if (m.image.id) {
        g_frameViews.push_back(m.view);
        g_frameImages.push_back(m.image);
    }
    m = Movie{image, view, width, height, 0xFFFFFFFFu, CommitNumber()};
}

void MovieBlit(const void* key, const Rect& to) {
    if (!Active()) return;
    auto it = g_movies.find(key);
    if (it == g_movies.end() || !it->second.view.id) return;
    Movie& m = it->second;
    m.blitFrame = FrameNumber();
    Frame& f = CurrentFrame();
    f.blitBatches.push_back(BlitBatch{m.view, m.width, m.height, to, Rect{0, 0, m.width, m.height}});
    f.commands.push_back(Command{Command::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}

namespace {
// Draws `rgba` (w x h) at `to` as a blit of this frame.
void CursorBlit(const std::vector<std::uint8_t>& rgba, int w, int h, const Rect& to, ImageUse use);
} // namespace

void Fill(const Rect& to, std::uint32_t colour) {
    if (!Active() || !Started() || to.right <= to.left || to.bottom <= to.top) return;
    FpuGuard fpu;
    // One texel stretched over the rectangle (D3DCOLOR is B, G, R, A in memory).
    std::vector<std::uint8_t> rgba{static_cast<std::uint8_t>(colour >> 16), static_cast<std::uint8_t>(colour >> 8),
                                   static_cast<std::uint8_t>(colour), 255};
    CursorBlit(rgba, 1, 1, to, ImageUse::Fill);
}

void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to) {
    if (!imageHolder || !mask || !Active() || !Started()) return;
    const auto* holder = static_cast<const CDXSurface*>(imageHolder);
    int w = from.right - from.left, h = from.bottom - from.top;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) return;
    FpuGuard fpu;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    // The image loaded from its picture (004EFC60, render::PictureFromMmp): the colours from the
    // file, the mask (the holder's width, in its pixel size) from the game's memory. A picture holder
    // has no surface: without a decoded picture there is nothing to draw.
    int maskWidth = holder->width;
    int maskBytes = holder->bitsPerPixel / 8;
    // The whole picture is kept with its mask for the cursor thread (render_cursor.cpp).
    const Picture* picture = PictureOf(imageHolder);
    if (picture && picture->width == maskWidth && (maskBytes == 2 || maskBytes == 4) &&
        CursorPicturePart(imageHolder, mask, maskBytes, *picture, from, &rgba))
        CursorBlit(rgba, w, h, to, ImageUse::Cursor);
}

namespace {
// The view of a cached image holding `rgba` (w x h), or none when every cached image is in use
// this frame or one could not be made.
sg_view CachedView(const std::vector<std::uint8_t>& rgba, int w, int h, ImageUse use) {
    std::uint32_t frame = FrameNumber(), commit = CommitNumber();
    CachedImage* sameSize = nullptr;   // the least recently used free image of this size
    CachedImage* anySize = nullptr;    // the least recently used free image
    for (CachedImage& c : g_cache) {
        if (c.width == w && c.height == h && c.pixels == rgba) {   // unchanged: no upload
            c.usedFrame = frame;
            c.lastUse = ++g_cacheClock;
            return c.view;
        }
        if (c.usedFrame == frame || c.updatedCommit == commit) continue;
        if (c.width == w && c.height == h && (!sameSize || c.lastUse < sameSize->lastUse)) sameSize = &c;
        if (!anySize || c.lastUse < anySize->lastUse) anySize = &c;
    }
    CachedImage* chosen = sameSize;
    if (!chosen && g_cache.size() < kCacheMax) {
        g_cache.emplace_back();
        chosen = &g_cache.back();
    }
    if (!chosen) chosen = anySize;
    if (!chosen) return sg_view{};
    if (chosen->width != w || chosen->height != h || !chosen->image.id) {
        if (chosen->view.id) sg_destroy_view(chosen->view);
        if (chosen->image.id) sg_destroy_image(chosen->image);
        *chosen = CachedImage{};
        sg_image image = MakeDynamicImage(w, h, "ei-small-image", use);
        sg_view view = MakeImageView(image);
        if (!view.id) return sg_view{};
        chosen->image = image;
        chosen->view = view;
        chosen->width = w;
        chosen->height = h;
    }
    UpdateImage(chosen->image, rgba.data(), w, h, use);
    chosen->pixels = rgba;
    chosen->updatedCommit = commit;
    chosen->usedFrame = frame;
    chosen->lastUse = ++g_cacheClock;
    return chosen->view;
}

void CursorBlit(const std::vector<std::uint8_t>& rgba, int w, int h, const Rect& to, ImageUse use) {
    CountImageRequest(use);
    sg_view view = CachedView(rgba, w, h, use);
    if (!view.id) {
        // Every cached image is in use this frame: an image for this frame only.
        CountImage(use, true);
        sg_image_desc desc{};
        desc.width = w;
        desc.height = h;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.data.mip_levels[0] = sg_range{rgba.data(), rgba.size()};
        desc.label = "ei-cursor-frame";
        sg_image image = sg_make_image(&desc);
        view = MakeImageView(image);
        if (!view.id) return;
        g_frameImages.push_back(image);
        g_frameViews.push_back(view);
    }
    Frame& f = CurrentFrame();
    if (use == ImageUse::Cursor && PresentsItself()) {
        // Drawn by EndFrame after the frame without it is kept for the cursor thread.
        f.cursor = BlitBatch{view, w, h, to, Rect{0, 0, w, h}};
        f.haveCursor = true;
        return;
    }
    f.blitBatches.push_back(BlitBatch{view, w, h, to, Rect{0, 0, w, h}});
    f.commands.push_back(Command{Command::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}
} // namespace

namespace detail {
void DropBlits() {
    RetireFrameImages();
    for (auto& entry : g_movies) {
        if (entry.second.view.id) sg_destroy_view(entry.second.view);
        if (entry.second.image.id) sg_destroy_image(entry.second.image);
    }
    g_movies.clear();   // a movie shows again from its next frame
    for (CachedImage& c : g_cache) {
        if (c.view.id) sg_destroy_view(c.view);
        if (c.image.id) sg_destroy_image(c.image);
    }
    g_cache.clear();
    DestroyBlits();
}
} // namespace detail

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
