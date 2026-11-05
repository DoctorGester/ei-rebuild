// The image blits: the movies' frames (MovieFrame / MovieBlit: an RGBA8 image updated in place, drawn as a screen
// rectangle that copies texels one to one, a stretch when the sizes differ). Also here: the
// solid rectangles, the depth clear the game does in the middle of the frame and the movie player's fill (Fill).
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

enum class PipelineKind : int { Blit, ClearDepth, Fill };

// None of them tests the depth: a blit and the fill keep the depth buffer, the depth clear writes only it. Made for
// the pass's sample count (render_smooth.cpp).
static gpu::PipelineDesc PassDesc(PipelineKind kind, int samples) {
    gpu::PipelineDesc p;
    p.program = kind == PipelineKind::Blit ? gpu_shaders::blit::kIndex : gpu_shaders::solid::kIndex;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::Always;
    p.depthWrite = kind == PipelineKind::ClearDepth;
    p.colour = kColourFormat;
    p.colourMask = kind == PipelineKind::ClearDepth ? gpu::ColourMask::None : gpu::ColourMask::Red | gpu::ColourMask::Green | gpu::ColourMask::Blue;
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

static gpu::Pipeline PassPipeline(PipelineKind kind) {
    return MakePassPipeline(PassDesc(kind, PassSampleCount()),
                             kind == PipelineKind::Blit ? "ei-image-blit" : kind == PipelineKind::ClearDepth ? "ei-clear-depth" : "ei-fill");
}

// `r` with the depth clear's or the fill's pipeline; `colour` a D3DCOLOR, its alpha ignored.
static void DrawSolid(PipelineKind kind, const Rect& r, float z, std::uint32_t colour, int width, int height) {
    if (r.IsEmpty()) return;
    gpu::Pipeline pipeline = PassPipeline(kind);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    SolidParams vs{{}, {0.0f, 0.0f, 0.0f, 1.0f}, {static_cast<float>(width), static_cast<float>(height)}, z};
    UnpackColour(colour, vs.colour);
    StoreRect(r, vs.rect);
    gpu::PushUniforms(gpu::Stage::Vertex, gpu_shaders::solid::kBlockSolidParams, vs);
    gpu::Draw(0, 6);
    CountDraw(sizeof(vs));
}

// A movie's RGBA8 image, written in place instead of made again. A recorded blit draws the image's contents at the end
// of the frame (DrawPending), so an image is rewritten only when no blit of it is recorded in this frame.
static gpu::Texture MakeMovieImage(int w, int h) {
    return gpu::MakeTexture({w, h, 1, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-movie-frame"});
}

static void UpdateMovieImage(const gpu::Texture& image, const std::uint8_t* rgba, int w, int h) {
    CountUpload(FrameStats::Upload::Movie, static_cast<std::size_t>(w) * h * 4);
    gpu::Write(image, 0, {0, 0, w, h}, rgba, static_cast<std::size_t>(w) * 4, gpu::Contents::Discard);
#if defined(EI_RENDER_VALIDATION)
    CheckMovieUpload(image, rgba, w, h);
#endif
}

struct Movie {
    gpu::Texture image;
    int width = 0, height = 0;
    std::uint32_t blitFrame = 0xFFFFFFFFu;      // FrameNumber() of the last blit recorded
};
static std::unordered_map<const void*, Movie> g_movies;
static std::vector<std::uint8_t> g_movieScratch;

void ListBlitPipelines(std::vector<gpu::PipelineDesc>& out, int samples) {
    for (PipelineKind kind : {PipelineKind::Blit, PipelineKind::ClearDepth, PipelineKind::Fill}) out.push_back(PassDesc(kind, samples));
}

bool CreateBlitPipeline() {
    bool ok = PassPipeline(PipelineKind::Blit) && PassPipeline(PipelineKind::ClearDepth) && PassPipeline(PipelineKind::Fill);
    if (!ok) ei::Log("render: the image blit, depth clear or fill pipeline could not be created (see the gpu messages above)");
    return ok;
}

void DrawBlitBatch(const BlitBatch& b, int width, int height) {
    if (!b.texture || b.textureWidth <= 0 || b.textureHeight <= 0) return;
    int dw = b.destination.Width(), dh = b.destination.Height();
    int sw = b.source.Width(), sh = b.source.Height();
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    gpu::Pipeline pipeline = PassPipeline(PipelineKind::Blit);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    const gpu::TextureBinding texture{b.texture, gpu::NearestClampSampler()};
    gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::blit::kSamplerTex, {&texture, 1});
    CountBindings(b.texture);
    ApplyRect(b.destination, width, height);
    const BlitParams fs{{static_cast<float>(b.destination.left), static_cast<float>(b.destination.top)},
                        {static_cast<float>(sw) / static_cast<float>(dw), static_cast<float>(sh) / static_cast<float>(dh)},
                        {static_cast<float>(b.source.left), static_cast<float>(b.source.top)},
                        {static_cast<float>(b.textureWidth), static_cast<float>(b.textureHeight)}};
    gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::blit::kBlockBlitParams, fs);
    gpu::Draw(0, 6);
    CountDraw(sizeof(RectParams) + sizeof(fs));
}

void DrawClearDepth(const ClearBatch& clear, int width, int height) {
    Rect r = clear.rect.right > clear.rect.left ? clear.rect : Rect{0, 0, width, height};
    DrawSolid(PipelineKind::ClearDepth, r, clear.z, 0, width, height);
}

void DrawFill(const FillBatch& fill, int width, int height) { DrawSolid(PipelineKind::Fill, fill.rect, 0.0f, fill.colour, width, height); }

void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height) {
    if (!key || !rgbx || width <= 0 || height <= 0) return;
    Start();
    g_movieScratch.assign(rgbx, rgbx + static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 3; i < g_movieScratch.size(); i += 4) g_movieScratch[i] = 255;
    Movie& m = g_movies[key];
    if (m.image && m.width == width && m.height == height && m.blitFrame != FrameNumber()) {
        UpdateMovieImage(m.image, g_movieScratch.data(), width, height);
        return;
    }
    gpu::Texture image = MakeMovieImage(width, height);
    if (!image) return;
    UpdateMovieImage(image, g_movieScratch.data(), width, height);
    // The previous image may still be drawn by this frame's commands.
    ReleaseAfterPresent(m.image);
    m = Movie{image, width, height, 0xFFFFFFFFu};
}

void MovieBlit(const void* key, const Rect& to) {
    auto it = g_movies.find(key);
    if (it == g_movies.end() || !it->second.image) return;
    Movie& m = it->second;
    m.blitFrame = FrameNumber();
    ++g_stats.counts[FrameStats::Count::Blits];
    Frame& f = g_frame;
    f.blitBatches.push_back(BlitBatch{m.image, m.width, m.height, to, Rect{0, 0, m.width, m.height}});
    f.commands.push_back(Command{Command::Kind::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}

void Fill(const Rect& to, std::uint32_t colour) {
    if (to.right <= to.left || to.bottom <= to.top) return;
    g_frame.fills.push_back(FillBatch{to, colour});
    g_frame.commands.push_back(Command{Command::Kind::Fill, static_cast<int>(g_frame.fills.size()) - 1});
}

} // namespace render
