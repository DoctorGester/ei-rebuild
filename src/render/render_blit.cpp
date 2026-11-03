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

enum PipelineKind { kBlit, kClearDepth, kFill };

// None of them tests the depth: a blit and the fill keep the depth buffer, the depth clear writes only it. Made for
// the pass's sample count (render_smooth.cpp).
gpu::Pipeline PassPipeline(PipelineKind kind) {
    gpu::PipelineDesc p;
    p.program = kind == kBlit ? gpu_shaders::blit::kIndex : gpu_shaders::solid::kIndex;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::Always;
    p.depthWrite = kind == kClearDepth;
    p.colour = kColourFormat;
    p.colourMask = kind == kClearDepth ? 0 : gpu::kWriteRed | gpu::kWriteGreen | gpu::kWriteBlue;
    p.samples = PassSampleCount();
    AddDepthCopy(p);
    return gpu::MakePipeline(p, kind == kBlit ? "ei-image-blit" : kind == kClearDepth ? "ei-clear-depth" : "ei-fill");
}

// `r` with the depth clear's or the fill's pipeline; `colour` a D3DCOLOR, its alpha ignored.
void DrawSolid(PipelineKind kind, const Rect& r, float z, std::uint32_t colour, int width, int height) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    gpu::Pipeline pipeline = PassPipeline(kind);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    const SolidParams vs{
        {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
         static_cast<float>(r.bottom)},
        {static_cast<float>(width), static_cast<float>(height), z, 0.0f},
        {((colour >> 16) & 0xFF) / 255.0f, ((colour >> 8) & 0xFF) / 255.0f, (colour & 0xFF) / 255.0f, 1.0f}};
    gpu::PushUniforms(gpu::Stage::Vertex, gpu_shaders::solid::kBlockSolidParams, vs);
    gpu::Draw(0, 6);
    CountDraw(sizeof(vs));
}

// Movie images let go during a frame (its blit commands, drawn at its end, still hold them), released once that
// frame is presented.
struct LetGo {
    gpu::Texture image;
    std::uint32_t frame;
};
std::vector<LetGo> g_letGo;

void ReleaseLetGo() {
    std::size_t kept = 0;
    for (LetGo& l : g_letGo) {
        if (l.frame != FrameNumber()) gpu::Release(l.image);
        else g_letGo[kept++] = l;
    }
    g_letGo.resize(kept);
}

// A movie's RGBA8 image, written in place instead of made again. A recorded blit draws the image's contents at the end
// of the frame (DrawPending), so an image is rewritten only when no blit of it is recorded in this frame.
gpu::Texture MakeMovieImage(int w, int h) {
    CountImage(ImageUse::Movie, true);
    return gpu::MakeTexture({w, h, 1, 1, gpu::Format::Rgba8, gpu::kSampled, "ei-movie-frame"});
}

#if defined(EI_RENDER_VALIDATION)
long g_uploadChecks = 0, g_uploadMismatches = 0;
long g_uploads = 0;   // movie uploads so far (the read-back is sampled: ReadBackDue)
#endif

// Verify build: the movie image just uploaded read back from the GPU and compared with the pixels written (what the
// shaders then draw are exactly these texels), sampled. Nothing in the other builds.
void CheckUpload(const gpu::Texture& image, const std::uint8_t* rgba, int w, int h) {
#if defined(EI_RENDER_VALIDATION)
    if (!ReadBackDue(g_uploads++)) return;
    gpu::Ticket copy = gpu::Download(image, 0, Rect{0, 0, w, h});
    gpu::Texels texels;
    if (gpu::Wait(copy) && gpu::Read(copy, &texels)) {
        bool same = true;
        for (int y = 0; y < h && same; ++y)
            same = std::memcmp(texels.Row(y), rgba + static_cast<std::size_t>(y) * w * 4,
                               static_cast<std::size_t>(w) * 4) == 0;
        ++g_uploadChecks;
        if (!same && g_uploadMismatches++ < 10)
            ei::Log("render: MISMATCH: movie image %dx%d read back from the GPU differs from the pixels written", w, h);
    }
    gpu::Release(copy);
    if (g_uploadChecks % 20 == 1)
        ei::Log("render: image uploads checked back from the GPU: %ld of %ld, mismatches %ld", g_uploadChecks, g_uploads, g_uploadMismatches);
#else
    (void)image;
    (void)rgba;
    (void)w;
    (void)h;
#endif
}

void UpdateMovieImage(const gpu::Texture& image, const std::uint8_t* rgba, int w, int h) {
    CountImage(ImageUse::Movie, false);
    CountUpload(FrameStats::Movie, static_cast<std::size_t>(w) * h * 4);
    gpu::Write(image, 0, {0, 0, w, h}, rgba, static_cast<std::size_t>(w) * 4, gpu::Contents::Discard);
    CheckUpload(image, rgba, w, h);
}

struct Movie {
    gpu::Texture image;
    int width = 0, height = 0;
    std::uint32_t blitFrame = 0xFFFFFFFFu;      // FrameNumber() of the last blit recorded
};
std::unordered_map<const void*, Movie> g_movies;
std::vector<std::uint8_t> g_movieScratch;

} // namespace

namespace detail {

bool CreateBlitPipeline() {
    bool ok = PassPipeline(kBlit) && PassPipeline(kClearDepth) && PassPipeline(kFill);
    if (!ok) ei::Log("render: the image blit, depth clear or fill pipeline could not be created (see the gpu messages above)");
    return ok;
}

void DrawBlitBatch(const Frame& /*frame*/, const BlitBatch& b, int width, int height) {
    if (!b.texture || b.textureWidth <= 0 || b.textureHeight <= 0) return;
    int dw = b.destination.Width(), dh = b.destination.Height();
    int sw = b.source.Width(), sh = b.source.Height();
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    gpu::Pipeline pipeline = PassPipeline(kBlit);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    const gpu::TextureBinding texture{
        b.texture, gpu::MakeSampler({gpu::Filter::Nearest, gpu::Filter::Nearest, gpu::Filter::Nearest, gpu::Wrap::Clamp,
                                     gpu::Wrap::Clamp})};
    gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::blit::kSamplerTex, {&texture, 1});
    CountBindings(b.texture);
    ApplyRect(b.destination, width, height);
    const BlitParams fs{{static_cast<float>(b.destination.left), static_cast<float>(b.destination.top),
                         static_cast<float>(sw) / static_cast<float>(dw), static_cast<float>(sh) / static_cast<float>(dh)},
                        {static_cast<float>(b.source.left), static_cast<float>(b.source.top),
                         static_cast<float>(b.textureWidth), static_cast<float>(b.textureHeight)}};
    gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::blit::kBlockBlitParams, fs);
    gpu::Draw(0, 6);
    CountDraw(sizeof(RectParams) + sizeof(fs));
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


void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height) {
    if (!key || !rgbx || width <= 0 || height <= 0) return;
    Start();
    CountImageRequest(ImageUse::Movie);
    g_movieScratch.assign(rgbx, rgbx + static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 3; i < g_movieScratch.size(); i += 4) g_movieScratch[i] = 255;
    ReleaseLetGo();
    Movie& m = g_movies[key];
    if (m.image && m.width == width && m.height == height && m.blitFrame != FrameNumber()) {
        UpdateMovieImage(m.image, g_movieScratch.data(), width, height);
        return;
    }
    gpu::Texture image = MakeMovieImage(width, height);
    if (!image) return;
    UpdateMovieImage(image, g_movieScratch.data(), width, height);
    // The previous image may still be drawn by this frame's commands: released after the present.
    if (m.image) g_letGo.push_back({m.image, FrameNumber()});
    m = Movie{image, width, height, 0xFFFFFFFFu};
}

void MovieBlit(const void* key, const Rect& to) {
    auto it = g_movies.find(key);
    if (it == g_movies.end() || !it->second.image) return;
    Movie& m = it->second;
    m.blitFrame = FrameNumber();
    ++g_stats.blits;
    Frame& f = g_frame;
    f.blitBatches.push_back(BlitBatch{m.image, m.width, m.height, to, Rect{0, 0, m.width, m.height}});
    f.commands.push_back(Command{Command::Blit, static_cast<int>(f.blitBatches.size()) - 1});
}

void Fill(const Rect& to, std::uint32_t colour) {
    if (to.right <= to.left || to.bottom <= to.top) return;
    g_frame.fills.push_back(FillBatch{to, colour});
    g_frame.commands.push_back(Command{Command::Fill, static_cast<int>(g_frame.fills.size()) - 1});
}

} // namespace render
