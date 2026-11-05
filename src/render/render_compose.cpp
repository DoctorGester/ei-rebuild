// Textures the game builds from images, drawn on the GPU (render::ComposeTexture, DrawIntoTexture): the images
// decoded from their files (32-bit premultiplied, "PNT3" packed, 5:6:5) and drawn into a render target, the levels
// below made by the GPU; and the global map's clouds (render::DrawClouds), their noise frames built by the GPU too.
// render_textures.cpp keeps the result by holder. See docs/notes/renderer.md, section 7 ("Textures composed on the
// GPU").
#include "../platform/log.h"
#include "render_internal.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace render {

static int g_failures = 0;

constexpr int kCloudSize = 256;   // a clouds frame's width and height
// The clouds' frames the GPU built, by their words: the two a draw blends and the next ones (a few).
struct CloudFrameTexture {
    CloudFrame frame{};
    gpu::Texture image;
    std::uint32_t used = 0;   // the composition that last used it
};
static std::vector<CloudFrameTexture> g_cloudFrames;
static std::uint32_t g_compositions = 0;
constexpr std::size_t kCloudFramesKept = 4;

// The programs' rectangle into a target of `format`, no depth; `over`: premultiplied blending (the image plus what is
// there times one less the image's alpha, alpha too).
static gpu::PipelineDesc Desc(int program, gpu::Format format, bool over) {
    gpu::PipelineDesc p;
    p.program = program;
    p.colour = format;
    if (over)
        p.blend = {true, gpu::BlendFactor::One, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendFactor::One,
                   gpu::BlendFactor::OneMinusSourceAlpha};
    return p;
}

static gpu::PipelineDesc ComposeDesc(bool over) {
    return Desc(gpu_shaders::texture_compose::kIndex, gpu::Format::Rgba8, over);
}
static gpu::PipelineDesc MipDesc() { return Desc(gpu_shaders::texture_mip::kIndex, gpu::Format::Rgba8, false); }
static gpu::PipelineDesc CloudFrameDesc() {
    return Desc(gpu_shaders::texture_clouds_frame::kIndex, gpu::Format::R8, false);
}
static gpu::PipelineDesc CloudsDesc() { return Desc(gpu_shaders::texture_clouds::kIndex, gpu::Format::Rgba8, false); }

static gpu::Pipeline ComposePipeline(bool over) {
    return gpu::MakePipeline(ComposeDesc(over), over ? "ei-texture-compose-over" : "ei-texture-compose-replace");
}
static gpu::Pipeline MipPipeline() { return gpu::MakePipeline(MipDesc(), "ei-texture-mip"); }
static gpu::Pipeline CloudFramePipeline() { return gpu::MakePipeline(CloudFrameDesc(), "ei-cloud-frame"); }
static gpu::Pipeline CloudsPipeline() { return gpu::MakePipeline(CloudsDesc(), "ei-clouds"); }

static bool EnsurePipelines() {
    static bool tried = false, made = false;
    if (tried) return made;
    tried = true;
    made = ComposePipeline(true) && ComposePipeline(false) && MipPipeline() && CloudFramePipeline() && CloudsPipeline();
    if (!made) ei::Log("render: the texture composition pipelines could not be made (see the gpu messages above)");
    return made;
}

static gpu::Sampler Linear() {
    return gpu::MakeSampler({gpu::Filter::Linear, gpu::Filter::Linear, gpu::Filter::Nearest, gpu::Wrap::Clamp, gpu::Wrap::Clamp});
}

// One 0xAARRGGBB pixel (bytes B, G, R, A) as R, G, B, A.
static void PutPixel(std::uint8_t* out, const std::uint8_t* in) {
    out[0] = in[2];
    out[1] = in[1];
    out[2] = in[0];
    out[3] = in[3];
}

// The image's pixels as RGBA8 rows from the top; texels the packed data leaves out (or the bytes do not reach) are
// transparent black (what drawing them over anything leaves as it was).
static void Decode(const TextureImage& image, ComposeImage& out) {
    out.width = image.width;
    out.height = image.height;
    const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    out.rgba.assign(count * 4, 0);
    const auto* p = static_cast<const std::uint8_t*>(image.pixels);
    std::size_t left = image.bytes;
    if (image.layout == ImageLayout::Rgba) {
        std::memcpy(out.rgba.data(), p, std::min(left, count * 4));
        return;
    }
    if (image.layout == ImageLayout::Argb) {
        for (std::size_t i = 0; i < count && left >= 4; ++i, p += 4, left -= 4) PutPixel(&out.rgba[i * 4], p);
        return;
    }
    // Groups of four pixels; a group whose first pixel has alpha 0 is instead a word counting the target's bytes
    // to leave (unless the word is 0: a group whose first pixel is transparent black).
    std::size_t at = 0;
    while (at < count && left >= 4) {
        const std::uint32_t word = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        if (p[3] == 0 && word != 0) {
            at += word / 4;
            p += 4;
            left -= 4;
            continue;
        }
        for (int k = 0; k < 4 && at < count && left >= 4; ++k, ++at, p += 4, left -= 4) PutPixel(&out.rgba[at * 4], p);
    }
}

// The clouds frame of `frame`'s words as a texture (R8): one the GPU built for an earlier composition, else built now
// (a pass of its own, so not inside another). Empty when it cannot be made.
static gpu::Texture CloudFrameImage(const CloudFrame& frame) {
    for (CloudFrameTexture& f : g_cloudFrames) {
        if (std::memcmp(&f.frame, &frame, sizeof(frame)) != 0) continue;
        f.used = g_compositions;
        return f.image;
    }
    if (g_cloudFrames.size() >= kCloudFramesKept) {
        // The one unused longest goes.
        auto oldest = g_cloudFrames.begin();
        for (auto it = g_cloudFrames.begin(); it != g_cloudFrames.end(); ++it)
            if (it->used < oldest->used) oldest = it;
        gpu::Release(oldest->image);
        g_cloudFrames.erase(oldest);
    }
    CloudFrameTexture f;
    f.frame = frame;
    f.used = g_compositions;
    f.image = gpu::MakeTexture({kCloudSize, kCloudSize, 1, 1, gpu::Format::R8, gpu::kColourTarget | gpu::kSampled, "ei-cloud-frame"});
    gpu::PassDesc pass;
    pass.colour.texture = f.image;
    pass.colour.load = gpu::Load::DontCare;
    pass.label = "ei-cloud-frame";
    if (!f.image || !gpu::BeginPass(pass)) {
        LogFirst(g_failures, 10, "render: a clouds frame could not be made");
        gpu::Release(f.image);
        return gpu::Texture{};
    }
    gpu::ApplyPipeline(CloudFramePipeline());
    ApplyRect(Rect{0, 0, kCloudSize, kCloudSize}, kCloudSize, kCloudSize);
    CloudParams params{};
    static_assert(sizeof(params.words) == sizeof(frame.words));
    std::memcpy(params.words, frame.words, sizeof(params.words));   // the words' bits, as the shader reads them
    gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::texture_clouds_frame::kBlockCloudParams, params);
    gpu::Draw(0, 6);
    gpu::EndPass();
    g_cloudFrames.push_back(f);
    return f.image;
}

// A texture of the image's pixels for one composition.
static gpu::Texture MakeStepTexture(const ComposeImage& image) {
    if (image.width <= 0 || image.height <= 0) return gpu::Texture{};
    gpu::Texture t = gpu::MakeTexture({image.width, image.height, 1, 1, gpu::Format::Rgba8, gpu::kSampled, "ei-texture-compose-image"});
    if (t) gpu::Write(t, 0, {0, 0, image.width, image.height}, image.rgba.data(), static_cast<std::size_t>(image.width) * 4);
    CountUpload(FrameStats::Mmp, image.rgba.size());
    return t;
}

// In a pass of its own: level `targetLevel` of `target` (`width` x `height`) drawn from level `level` of `source`,
// filtered down from it (texture_mip with the linear sampler: `source`'s level twice the size) or texel for texel
// (the nearest one: the same size).
static void DrawLevel(const gpu::Texture& source, int level, gpu::Sampler sampler, const gpu::Texture& target,
                      int targetLevel, int width, int height) {
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.level = targetLevel;
    pass.colour.load = gpu::Load::DontCare;
    pass.label = "ei-texture-level";
    if (!gpu::BeginPass(pass)) return;
    gpu::ApplyPipeline(MipPipeline());
    const gpu::TextureBinding b{source, sampler};
    gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::texture_mip::kSamplerTex0, {&b, 1});
    ApplyRect(Rect{0, 0, width, height}, width, height);
    const gpu_shaders::TextureMipParams fs{static_cast<float>(level), {static_cast<float>(width), static_cast<float>(height)}};
    gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::texture_mip::kBlockTextureMipParams, fs);
    gpu::Draw(0, 6);
    gpu::EndPass();
}

// The scratch texture the levels of a composed texture are drawn into (a pass may not read the texture it draws into):
// half the texture's size, kept for the next of the same shape (the global map's clouds are drawn every frame).
static gpu::Texture g_levelScratch;

// The levels below the first, each from the one above: drawn into the scratch's level, then copied into the texture's.
static void DrawLevels(const ComposeShape& shape, const gpu::Texture& target) {
    gpu::Texture& scratch = g_levelScratch;
    const int width = std::max(shape.width / 2, 1), height = std::max(shape.height / 2, 1);
    if (scratch.width != width || scratch.height != height || scratch.levels != shape.levels - 1) {
        gpu::Release(scratch);
        scratch = gpu::MakeTexture({width, height, shape.levels - 1, 1, gpu::Format::Rgba8, gpu::kColourTarget | gpu::kSampled,
                                    "ei-texture-levels"});
        if (!scratch) return;
    }
    for (int level = 1; level < shape.levels; ++level) {
        const int w = std::max(shape.width >> level, 1), h = std::max(shape.height >> level, 1);
        DrawLevel(target, level - 1, Linear(), scratch, level - 1, w, h);
        gpu::Copy(scratch, level - 1, {0, 0, w, h}, target, level, 0, 0);
    }
}

static void DrawSteps(const ComposeShape& shape, const gpu::Texture& target, const ComposeStep* steps,
                      std::size_t count, bool clear) {
    // What the steps draw from, made before the pass (a clouds frame is built in a pass of its own).
    ++g_compositions;
    std::vector<gpu::Texture> textures;
    textures.reserve(count);
    std::vector<std::pair<gpu::Texture, gpu::Texture>> cloudFrames(count);
    for (std::size_t k = 0; k < count; ++k) {
        textures.push_back(steps[k].image ? MakeStepTexture(*steps[k].image) : gpu::Texture{});
        if (steps[k].clouds)
            cloudFrames[k] = {CloudFrameImage(steps[k].clouds->from), CloudFrameImage(steps[k].clouds->to)};
    }
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = clear ? gpu::Load::Clear : gpu::Load::Keep;
    pass.colour.clear[3] = shape.alpha == BlankAlpha::None ? 1.0f : 0.0f;
    pass.label = "ei-texture-compose";
    if (gpu::BeginPass(pass)) {
        const gpu::Sampler nearest = gpu::NearestClampSampler();
        for (std::size_t k = 0; k < count; ++k) {
            const ComposeStep& step = steps[k];
            if (step.clouds) {
                if (!cloudFrames[k].first || !cloudFrames[k].second) continue;
                gpu::ApplyPipeline(CloudsPipeline());
                const gpu::TextureBinding b[2] = {{cloudFrames[k].first, nearest}, {cloudFrames[k].second, nearest}};
                gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::texture_clouds::kSamplerFromFrame, b);
                ApplyRect(step.to, shape.width, shape.height);
                CloudParams params{};
                params.blend = static_cast<int>(step.clouds->blend);
                for (int g = 0; g < 256; ++g) params.ramp[g / 4][g % 4] = step.clouds->ramp[g];
                gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::texture_clouds::kBlockCloudParams, params);
                gpu::Draw(0, 6);
                continue;
            }
            const bool fill = !step.image;
            if (!fill && !textures[k]) continue;
            gpu::ApplyPipeline(ComposePipeline(step.over && !fill));
            const gpu::TextureBinding b{fill ? WhiteView() : textures[k], nearest};
            gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::texture_compose::kSamplerTex0, {&b, 1});
            ApplyRect(step.to, shape.width, shape.height);
            const TextureComposeParams fs{{step.fromX - step.to.left, step.fromY - step.to.top}, fill ? 1 : 0,
                                          static_cast<int>(shape.alpha), {1.0f, 1.0f, 1.0f, 1.0f}};
            gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::texture_compose::kBlockTextureComposeParams, fs);
            gpu::Draw(0, 6);
        }
        gpu::EndPass();
    }
    for (gpu::Texture& t : textures) gpu::Release(t);
    if (shape.levels > 1) DrawLevels(shape, target);
}

void ListComposePipelines(std::vector<gpu::PipelineDesc>& out) {
    out.push_back(ComposeDesc(true));
    out.push_back(ComposeDesc(false));
    out.push_back(MipDesc());
    out.push_back(CloudFrameDesc());
    out.push_back(CloudsDesc());
}

bool DecodeComposeImage(const TextureImage& image, ComposeImage* decoded) {
    if (!image.pixels || image.width <= 0 || image.height <= 0 || image.width > 4096 || image.height > 4096) return false;
    Decode(image, *decoded);
    return true;
}

gpu::Texture MakeComposed(const ComposeShape& shape, const ComposeStep* steps, std::size_t count) {
    if (!EnsurePipelines() || shape.width <= 0 || shape.height <= 0 || shape.levels < 1 || shape.firstLevel >= shape.levels)
        return gpu::Texture{};
    gpu::Texture image = gpu::MakeTexture({shape.width, shape.height, shape.levels, 1, gpu::Format::Rgba8,
                                           gpu::kColourTarget | gpu::kSampled, "ei-composed-texture"});
    if (!image) {
        LogFirst(g_failures, 10, "render: a %dx%d composed texture could not be made", shape.width, shape.height);
        return gpu::Texture{};
    }
    DrawSteps(shape, image, steps, count, true);
    if (shape.firstLevel == 0) return image;
    // Shown from a level below the first: those levels copied out of the full-size image (there are no views).
    const int w = std::max(shape.width >> shape.firstLevel, 1), h = std::max(shape.height >> shape.firstLevel, 1);
    gpu::Texture shown = gpu::MakeTexture(
        {w, h, shape.levels - shape.firstLevel, 1, gpu::Format::Rgba8, gpu::kColourTarget | gpu::kSampled, "ei-composed-texture"});
    for (int level = shape.firstLevel; shown && level < shape.levels; ++level)
        gpu::Copy(image, level, {0, 0, std::max(shape.width >> level, 1), std::max(shape.height >> level, 1)}, shown,
                  level - shape.firstLevel, 0, 0);
    gpu::Release(image);
    return shown;
}

void DrawComposed(const ComposeShape& shape, const gpu::Texture& image, const ComposeStep* steps, std::size_t count) {
    if (!image || !count || shape.firstLevel != 0 || !EnsurePipelines()) return;
    DrawSteps(shape, image, steps, count, false);
}

bool ReadImageLevel(const gpu::Texture& image, int level, int width, int height, std::vector<std::uint8_t>* rgba) {
    if (!image || width <= 0 || height <= 0 || !EnsurePipelines()) return false;
    gpu::Texture target = gpu::MakeTexture({width, height, 1, 1, gpu::Format::Rgba8, gpu::kColourTarget, "ei-texture-level-read"});
    bool read = false;
    if (target) {
        DrawLevel(image, level, gpu::NearestClampSampler(), target, 0, width, height);
        gpu::Ticket copy = gpu::Download(target, 0, Rect{0, 0, width, height});
        gpu::Texels texels;
        if (gpu::Wait(copy) && gpu::Read(copy, &texels)) {
            rgba->resize(static_cast<std::size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y)
                std::memcpy(rgba->data() + static_cast<std::size_t>(y) * width * 4, texels.Row(y),
                            static_cast<std::size_t>(width) * 4);
            read = true;
        }
        gpu::Release(copy);
    }
    gpu::Release(target);
    return read;
}

}   // namespace render
