// The image blits: the movies' frames (MovieFrame / MovieBlit: an RGBA8 image updated in place, drawn as a screen
// rectangle that copies texels one to one, a stretch when the sizes differ). Also here: the
// solid rectangles, the depth clear the game does in the middle of the frame and the movie player's fill (Fill).
// (The interface windows' canvases have their own blit, render_ui.cpp.)
#include "render.h"
#include "render_internal.h"

#include "../platform/log.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

namespace {

using namespace detail;

enum PipelineKind { kBlit, kClearDepth, kFill, kPipelineKinds };

sg_shader g_blitShader{}, g_solidShader{};
sg_pipeline g_pipelines[kPipelineKinds]{};
// For the render quality's multisampled passes (render_smooth.cpp), made at their first use.
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
    solid_params_t vs{
        {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
         static_cast<float>(r.bottom)},
        {static_cast<float>(width), static_cast<float>(height), z, 0.0f},
        {((colour >> 16) & 0xFF) / 255.0f, ((colour >> 8) & 0xFF) / 255.0f, (colour & 0xFF) / 255.0f, 1.0f}};
    sg_apply_uniforms(UB_solid_params, SG_RANGE(vs));
    sg_draw(0, 6, 1);
    CountDraw(sizeof(vs));
}

std::vector<sg_image> g_frameImages;   // let go during the frame (a movie's), destroyed at the present
std::vector<sg_view> g_frameViews;

// A movie's RGBA8 image, updated in place (sg_update_image) instead of made again. Sokol allows
// one update per image and frame, and a recorded blit draws the image's contents at the end of the
// frame (DrawPending), so an image is rewritten only when no blit of it is recorded in this frame
// and it was not rewritten since the last commit.
sg_image MakeMovieImage(int w, int h) {
    sg_image_desc desc{};
    desc.width = w;
    desc.height = h;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.dynamic_update = true;
    desc.label = "ei-movie-frame";
    CountImage(ImageUse::Movie, true);
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
long g_uploads = 0;   // movie uploads so far (the read-back is sampled: ReadBackDue)
#endif

// Verify build: the movie image just uploaded read back from the GPU and compared with the pixels written (what the
// shaders then draw are exactly these texels), sampled. Nothing in the other builds.
void CheckUpload(sg_image image, const std::uint8_t* rgba, int w, int h) {
#if defined(EI_RENDER_VALIDATION)
    if (!ReadBackDue(g_uploads++)) return;
    platform::graphics::Readback copy;
    if (copy.Read(image, platform::Rect{0, 0, w, h})) {
        bool same = true;
        for (int y = 0; y < h && same; ++y)
            same = std::memcmp(copy.Row(y), rgba + static_cast<std::size_t>(y) * w * 4,
                               static_cast<std::size_t>(w) * 4) == 0;
        ++g_uploadChecks;
        if (!same && g_uploadMismatches++ < 10)
            ei::Log("render: MISMATCH: movie image %dx%d read back from the GPU differs from the pixels written", w, h);
    }
    copy.Release();
    if (g_uploadChecks % 20 == 1)
        ei::Log("render: image uploads checked back from the GPU: %ld of %ld, mismatches %ld", g_uploadChecks, g_uploads, g_uploadMismatches);
#else
    (void)image;
    (void)rgba;
    (void)w;
    (void)h;
#endif
}

void UpdateMovieImage(sg_image image, const std::uint8_t* rgba, int w, int h) {
    sg_image_data data{};
    data.mip_levels[0] = sg_range{rgba, static_cast<std::size_t>(w) * h * 4};
    CountImage(ImageUse::Movie, false);
    CountUpload(FrameStats::Movie, data.mip_levels[0].size);
    sg_update_image(image, &data);
    CheckUpload(image, rgba, w, h);
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

} // namespace

namespace detail {

bool CreateBlitPipeline() {
    g_blitShader = sg_make_shader(blit_shader_desc(sg_query_backend()));
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_NEAREST;
    sd.mag_filter = SG_FILTER_NEAREST;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sd.label = "ei-image-blit";
    g_nearest = sg_make_sampler(&sd);

    g_solidShader = sg_make_shader(solid_shader_desc(sg_query_backend()));
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
    blit_params_t fs{{static_cast<float>(b.destination.left), static_cast<float>(b.destination.top),
                      static_cast<float>(sw) / static_cast<float>(dw), static_cast<float>(sh) / static_cast<float>(dh)},
                     {static_cast<float>(b.source.left), static_cast<float>(b.source.top),
                      static_cast<float>(b.textureWidth), static_cast<float>(b.textureHeight)}};
    sg_apply_uniforms(UB_blit_params, SG_RANGE(fs));
    sg_draw(0, 6, 1);
    CountDraw(sizeof(rect_params_t) + sizeof(fs));
}

void DrawClearDepth(const ClearBatch& clear, int width, int height) {
    Rect r = clear.rect.right > clear.rect.left ? clear.rect : Rect{0, 0, width, height};
    DrawSolid(kClearDepth, r, clear.z, 0, width, height);
}

void DrawFill(const FillBatch& fill, int width, int height) { DrawSolid(kFill, fill.rect, 0.0f, fill.colour, width, height); }

} // namespace detail

namespace detail {
namespace {
long g_imageMade[static_cast<int>(ImageUse::Count)];
long g_imageUpdated[static_cast<int>(ImageUse::Count)];
long g_imageRequests[static_cast<int>(ImageUse::Count)];   // pictures asked for (movie frames)
} // namespace

void CountImage(ImageUse use, bool made) { ++(made ? g_imageMade : g_imageUpdated)[static_cast<int>(use)]; }
void CountImageRequest(ImageUse use) { ++g_imageRequests[static_cast<int>(use)]; }

void LogImageCounts(const char* when) {
    static const char* const names[] = {"movie", "texture"};
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
    platform::FloatingPointGuard fpu;
    CountImageRequest(ImageUse::Movie);
    g_movieScratch.assign(rgbx, rgbx + static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 3; i < g_movieScratch.size(); i += 4) g_movieScratch[i] = 255;
    Movie& m = g_movies[key];
    if (m.image.id && m.width == width && m.height == height && m.blitFrame != FrameNumber() &&
        m.updatedCommit != CommitNumber()) {
        UpdateMovieImage(m.image, g_movieScratch.data(), width, height);
        m.updatedCommit = CommitNumber();
        return;
    }
    sg_image image = MakeMovieImage(width, height);
    sg_view view = MakeImageView(image);
    if (!view.id) return;
    UpdateMovieImage(image, g_movieScratch.data(), width, height);
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

} // namespace render
