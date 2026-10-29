// The image blits: the software cursor's picture (render::Cursor) and the movies' frames (MovieFrame / MovieBlit),
// each an RGBA8 image drawn as a screen rectangle that copies texels one to one (a stretch when the sizes differ),
// texels with alpha under a half left out; the images are kept across frames and updated in place. Also here: the
// solid rectangles, the depth clear the game does in the middle of the frame and the movie player's fill (Fill).
// (The interface windows' canvases have their own blit, render_ui.cpp.)
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include "../game/texture.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

namespace {

using namespace detail;

struct FsParams {
    float dest[4];
    float source[4];
};
struct SolidParams {
    float rect[4];
    float target[4];   // z: the depth clear's depth
    float colour[4];   // the fill's colour
};

enum PipelineKind { kBlit, kClearDepth, kFill, kPipelineKinds };

sg_shader g_blitShader{}, g_solidShader{};
sg_pipeline g_pipelines[kPipelineKinds]{};
// For the smooth option's multisampled passes (render_smooth.cpp), made at their first use.
sg_pipeline g_multisampled[kPipelineKinds]{};
int g_multisampledSamples = 0;
sg_sampler g_nearest{};

// None of them tests the depth: a blit and the fill keep the depth buffer, the depth clear writes only it.
sg_pipeline MakePipeline(PipelineKind kind, int samples) {
    sg_pipeline_desc p{};
    p.shader = kind == kBlit ? g_blitShader : g_solidShader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_ALWAYS;
    p.depth.write_enabled = kind == kClearDepth;
    p.colors[0].pixel_format = kColourFormat;
    p.colors[0].write_mask = kind == kClearDepth ? SG_COLORMASK_NONE : SG_COLORMASK_RGB;
    p.sample_count = samples;
    p.label = kind == kBlit ? "ei-image-blit" : kind == kClearDepth ? "ei-clear-depth" : "ei-fill";
    return sg_make_pipeline(&p);
}

void DestroyMultisampledPipelines() {
    for (sg_pipeline& p : g_multisampled) {
        if (p.id) sg_destroy_pipeline(p);
        p = sg_pipeline{};
    }
    g_multisampledSamples = 0;
}

sg_pipeline PassPipeline(PipelineKind kind) {
    int samples = PassSampleCount();
    if (samples <= 1) return g_pipelines[kind];
    if (g_multisampledSamples != samples) {
        DestroyMultisampledPipelines();
        for (int k = 0; k < kPipelineKinds; ++k) g_multisampled[k] = MakePipeline(static_cast<PipelineKind>(k), samples);
        g_multisampledSamples = samples;
    }
    return g_multisampled[kind];
}

// `r` with the depth clear's or the fill's pipeline; `colour` a D3DCOLOR, its alpha ignored.
void DrawSolid(PipelineKind kind, const Rect& r, float z, std::uint32_t colour, int width, int height) {
    if (r.right <= r.left || r.bottom <= r.top || !g_pipelines[kind].id) return;
    sg_pipeline pipeline = PassPipeline(kind);
    sg_apply_pipeline(pipeline);
    CountPipeline(pipeline);
    SolidParams vs{{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom)},
                   {static_cast<float>(width), static_cast<float>(height), z, 0.0f},
                   {static_cast<float>((colour >> 16) & 0xFF) / 255.0f, static_cast<float>((colour >> 8) & 0xFF) / 255.0f,
                    static_cast<float>(colour & 0xFF) / 255.0f, 1.0f}};
    sg_range vr{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vr);
    sg_draw(0, 6, 1);
    CountDraw(sizeof(vs));
}

std::vector<sg_image> g_frameImages;   // made for one frame (the cursor), destroyed at the present
std::vector<sg_view> g_frameViews;

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
long g_uploads[2] = {0, 0};   // uploads so far, cursor and movie (the read-back is sampled: ReadBackDue)
// Verify build: reads the image back from the GPU and compares it with the pixels just written
// (what the blit's unchanged shader then draws is exactly these texels).
void CheckUpload(sg_image image, const std::uint8_t* rgba, int w, int h, const char* what) {
    StagingCopy copy;
    if (copy.Map(D3DContext(), D3DTexture(image), 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h))) {
        bool same = true;
        for (int y = 0; y < h && same; ++y)
            same = std::memcmp(copy.Row(static_cast<unsigned>(y)), rgba + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4) == 0;
        ++g_uploadChecks;
        if (!same && g_uploadMismatches++ < 10)
            ei::Log("render: MISMATCH: %s image %dx%d read back from the GPU differs from the pixels written", what, w, h);
    }
    copy.Release();
    sg_reset_state_cache();
}
#endif

void UpdateImage(sg_image image, const std::uint8_t* rgba, int w, int h, ImageUse use) {
    sg_image_data data{};
    data.mip_levels[0] = sg_range{rgba, static_cast<std::size_t>(w) * h * 4};
    CountImage(use, false);
    CountUpload(use == ImageUse::Movie ? FrameStats::Movie : FrameStats::Small, data.mip_levels[0].size);
    sg_update_image(image, &data);
#if defined(EI_RENDER_VALIDATION)
    long& uploads = g_uploads[use == ImageUse::Movie ? 1 : 0];
    if (ReadBackDue(uploads++)) {
        CheckUpload(image, rgba, w, h, use == ImageUse::Movie ? "movie" : "cursor");
        if (g_uploadChecks % 20 == 1 && g_uploadChecks)
            ei::Log("render: image uploads checked back from the GPU: %ld of %ld, mismatches %ld", g_uploadChecks, g_uploads[0] + g_uploads[1], g_uploadMismatches);
    }
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

// The cursor's small images, kept across frames: one per size, rewritten only when
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

// The view of a cached image holding `rgba` (w x h), or none when every cached image is in use
// this frame or one could not be made.
sg_view CachedView(const std::vector<std::uint8_t>& rgba, int w, int h) {
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
        sg_image image = MakeDynamicImage(w, h, "ei-small-image", ImageUse::Cursor);
        sg_view view = MakeImageView(image);
        if (!view.id) return sg_view{};
        chosen->image = image;
        chosen->view = view;
        chosen->width = w;
        chosen->height = h;
    }
    UpdateImage(chosen->image, rgba.data(), w, h, ImageUse::Cursor);
    chosen->pixels = rgba;
    chosen->updatedCommit = commit;
    chosen->usedFrame = frame;
    chosen->lastUse = ++g_cacheClock;
    return chosen->view;
}

// `rgba` (w x h) at `to`: drawn by the present (DrawToWindow) over the frame, which stays without it for the
// cursor thread.
void CursorBlit(const std::vector<std::uint8_t>& rgba, int w, int h, const Rect& to) {
    CountImageRequest(ImageUse::Cursor);
    sg_view view = CachedView(rgba, w, h);
    if (!view.id) {
        // Every cached image is in use this frame: an image for this frame only.
        CountImage(ImageUse::Cursor, true);
        sg_image_desc desc{};
        desc.width = w;
        desc.height = h;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.data.mip_levels[0] = sg_range{rgba.data(), rgba.size()};
        desc.label = "ei-cursor-frame";
        CountUpload(FrameStats::Small, rgba.size());
        sg_image image = sg_make_image(&desc);
        view = MakeImageView(image);
        if (!view.id) return;
        g_frameImages.push_back(image);
        g_frameViews.push_back(view);
    }
    g_frame.cursor = BlitBatch{view, w, h, to, Rect{0, 0, w, h}};
    g_frame.haveCursor = true;
}

} // namespace

namespace detail {

bool CreateBlitPipeline() {
    sg_shader_desc s{};
    SetBytecode(s, kBlitShader);
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(RectParams);
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
    s.label = "ei-image-blit";
    g_blitShader = sg_make_shader(&s);
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_NEAREST;
    sd.mag_filter = SG_FILTER_NEAREST;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sd.label = "ei-image-blit";
    g_nearest = sg_make_sampler(&sd);

    sg_shader_desc c{};
    SetBytecode(c, kSolidShader);
    c.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    c.uniform_blocks[0].size = sizeof(SolidParams);
    c.uniform_blocks[0].hlsl_register_b_n = 0;
    c.label = "ei-solid";
    g_solidShader = sg_make_shader(&c);
    bool ok = true;
    for (int k = 0; k < kPipelineKinds; ++k) {
        g_pipelines[k] = MakePipeline(static_cast<PipelineKind>(k), 1);
        ok = ok && sg_query_pipeline_state(g_pipelines[k]) == SG_RESOURCESTATE_VALID;
    }
    if (!ok) ei::Log("render: the image blit, depth clear or fill pipeline could not be created (see the sokol messages above)");
    return ok;
}

void DrawBlitBatch(const Frame& /*frame*/, const BlitBatch& b, int width, int height) {
    if (!b.texture.id || b.textureWidth <= 0 || b.textureHeight <= 0) return;
    int dw = b.destination.right - b.destination.left, dh = b.destination.bottom - b.destination.top;
    int sw = b.source.right - b.source.left, sh = b.source.bottom - b.source.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    sg_pipeline pipeline = PassPipeline(kBlit);
    sg_apply_pipeline(pipeline);
    CountPipeline(pipeline);
    sg_bindings bind{};
    bind.views[0] = b.texture;
    bind.samplers[0] = g_nearest;
    sg_apply_bindings(&bind);
    CountBindings(bind);
    ApplyRect(b.destination, width, height);
    FsParams fs{{static_cast<float>(b.destination.left), static_cast<float>(b.destination.top),
                 static_cast<float>(sw) / static_cast<float>(dw), static_cast<float>(sh) / static_cast<float>(dh)},
                {static_cast<float>(b.source.left), static_cast<float>(b.source.top), static_cast<float>(b.textureWidth),
                 static_cast<float>(b.textureHeight)}};
    sg_range fr{&fs, sizeof(fs)};
    sg_apply_uniforms(1, &fr);
    sg_draw(0, 6, 1);
    CountDraw(sizeof(RectParams) + sizeof(fs));
}

void DrawClearDepth(const ClearBatch& clear, int width, int height) {
    Rect r = clear.rect.right > clear.rect.left ? clear.rect : Rect{0, 0, width, height};
    DrawSolid(kClearDepth, r, clear.z, 0, width, height);
}

void DrawFill(const FillBatch& fill, int width, int height) { DrawSolid(kFill, fill.rect, 0.0f, fill.colour, width, height); }

void DestroyBlits() {
    DestroyMultisampledPipelines();
    for (sg_pipeline& p : g_pipelines) {
        if (p.id) sg_destroy_pipeline(p);
        p = sg_pipeline{};
    }
    if (g_blitShader.id) sg_destroy_shader(g_blitShader);
    if (g_solidShader.id) sg_destroy_shader(g_solidShader);
    if (g_nearest.id) sg_destroy_sampler(g_nearest);
    g_blitShader = g_solidShader = sg_shader{};
    g_nearest = sg_sampler{};
}

} // namespace detail

namespace detail {
namespace {
long g_imageMade[static_cast<int>(ImageUse::Count)];
long g_imageUpdated[static_cast<int>(ImageUse::Count)];
long g_imageRequests[static_cast<int>(ImageUse::Count)];   // pictures asked for (cursor, movie frames)
} // namespace

void CountImage(ImageUse use, bool made) { ++(made ? g_imageMade : g_imageUpdated)[static_cast<int>(use)]; }
void CountImageRequest(ImageUse use) { ++g_imageRequests[static_cast<int>(use)]; }

void LogImageCounts(const char* when) {
    static const char* const names[] = {"cursor", "movie", "texture"};
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

namespace detail {
void RetireFrameImages() {
    for (sg_view v : g_frameViews) sg_destroy_view(v);
    for (sg_image i : g_frameImages) sg_destroy_image(i);
    g_frameViews.clear();
    g_frameImages.clear();
}
} // namespace detail

void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height) {
    if (!key || !rgbx || width <= 0 || height <= 0) return;
    Start();
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
    auto it = g_movies.find(key);
    if (it == g_movies.end() || !it->second.view.id) return;
    Movie& m = it->second;
    m.blitFrame = FrameNumber();
    ++g_stats.blits;
    Frame& f = g_frame;
    f.blitBatches.push_back(BlitBatch{m.view, m.width, m.height, to, Rect{0, 0, m.width, m.height}});
    f.commands.push_back(Command{Command::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}

void Fill(const Rect& to, std::uint32_t colour) {
    if (to.right <= to.left || to.bottom <= to.top) return;
    g_frame.fills.push_back(FillBatch{to, colour});
    g_frame.commands.push_back(Command{Command::Fill, static_cast<int>(g_frame.fills.size()) - 1});
}

void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to) {
    if (!imageHolder || !mask) return;
    Start();
    const auto* holder = static_cast<const Surface*>(imageHolder);
    int w = from.right - from.left, h = from.bottom - from.top;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) return;
    FpuGuard fpu;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    // The image loaded from its picture (render::PictureFromMmp): the colours from the
    // file, the mask (the holder's width, in its pixel size) the cursor image built from the file. A
    // picture holder has no surface: without a decoded picture there is nothing to draw.
    int maskWidth = holder->width;
    int maskBytes = holder->bitsPerPixel / 8;
    // The whole picture is kept with its mask for the cursor thread (render_cursor.cpp).
    const Picture* picture = PictureOf(imageHolder);
    if (picture && picture->width == maskWidth && (maskBytes == 2 || maskBytes == 4) &&
        CursorPicturePart(imageHolder, mask, maskBytes, *picture, from, &rgba))
        CursorBlit(rgba, w, h, to);
}

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

} // namespace render
