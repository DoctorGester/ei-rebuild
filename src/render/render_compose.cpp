// Textures the game builds from images, drawn on the GPU (render::ComposeTexture, DrawIntoTexture): the images
// decoded from their files (32-bit premultiplied, "PNT3" packed, 5:6:5) and drawn into a render target, the levels
// below made by the GPU; and the global map's clouds (render::DrawClouds, map_clouds.hlsl).
// render_textures.cpp keeps the result by holder. See docs/notes/renderer.md, section 7 ("Textures composed on the
// GPU").
#include "../platform/log.h"
#include "render_internal.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace render {

static int g_failures = 0;

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
static gpu::PipelineDesc CloudsDesc() { return Desc(gpu_shaders::map_clouds::kIndex, gpu::Format::Rgba8, false); }

static gpu::Pipeline ComposePipeline(bool over) {
    return gpu::MakePipeline(ComposeDesc(over), over ? "ei-texture-compose-over" : "ei-texture-compose-replace");
}
static gpu::Pipeline MipPipeline() { return gpu::MakePipeline(MipDesc(), "ei-texture-mip"); }
static gpu::Pipeline CloudsPipeline() { return gpu::MakePipeline(CloudsDesc(), "ei-clouds"); }

// EnsurePipelines' first try and whether it made them all.
static bool g_pipelinesTried = false, g_pipelinesMade = false;

static bool EnsurePipelines() {
    if (g_pipelinesTried) return g_pipelinesMade;
    g_pipelinesTried = true;
    g_pipelinesMade =
        ComposePipeline(true) && ComposePipeline(false) && MipPipeline() && CloudsPipeline();
    if (!g_pipelinesMade)
        ei::Log("render: the texture composition pipelines could not be made (see the gpu messages above)");
    return g_pipelinesMade;
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

// A texture of the image's pixels for one composition.
static gpu::Texture MakeStepTexture(const ComposeImage& image) {
    if (image.width <= 0 || image.height <= 0) return gpu::Texture{};
    gpu::Texture t = gpu::MakeTexture({image.width, image.height, 1, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-texture-compose-image"});
    if (t) gpu::Write(t, 0, {0, 0, image.width, image.height}, image.rgba.data(), static_cast<std::size_t>(image.width) * 4);
    CountUpload(FrameStats::Upload::Mmp, image.rgba.size());
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
        scratch = gpu::MakeTexture({width, height, shape.levels - 1, 1, gpu::Format::Rgba8, gpu::TextureUse::ColourTarget | gpu::TextureUse::Sampled,
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
    // What the steps draw from, made before the pass.
    std::vector<gpu::Texture> textures;
    textures.reserve(count);
    for (std::size_t k = 0; k < count; ++k)
        textures.push_back(steps[k].image ? MakeStepTexture(*steps[k].image) : gpu::Texture{});
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = clear ? gpu::Load::Clear : gpu::Load::Keep;
    pass.colour.clear[3] = shape.alpha == BlankAlpha::None ? 1.0f : 0.0f;
    pass.label = "ei-texture-compose";
    if (gpu::BeginPass(pass)) {
        const gpu::Sampler nearest = gpu::NearestClampSampler();
        for (std::size_t k = 0; k < count; ++k) {
            const ComposeStep& step = steps[k];
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
                                           gpu::TextureUse::ColourTarget | gpu::TextureUse::Sampled, "ei-composed-texture"});
    if (!image) {
        LogFirst(g_failures, 10, "render: a %dx%d composed texture could not be made", shape.width, shape.height);
        return gpu::Texture{};
    }
    DrawSteps(shape, image, steps, count, true);
    if (shape.firstLevel == 0) return image;
    // Shown from a level below the first: those levels copied out of the full-size image (there are no views).
    const int w = std::max(shape.width >> shape.firstLevel, 1), h = std::max(shape.height >> shape.firstLevel, 1);
    gpu::Texture shown = gpu::MakeTexture(
        {w, h, shape.levels - shape.firstLevel, 1, gpu::Format::Rgba8, gpu::TextureUse::ColourTarget | gpu::TextureUse::Sampled, "ei-composed-texture"});
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

void DrawCloudTexture(const ComposeShape& shape, const gpu::Texture& image, const MapClouds& clouds) {
    if (!image || shape.firstLevel != 0 || !EnsurePipelines()) return;
    gpu::PassDesc pass;
    pass.colour.texture = image;
    pass.colour.load = gpu::Load::DontCare;
    pass.label = "ei-map-clouds";
    if (!gpu::BeginPass(pass)) return;
    gpu::ApplyPipeline(CloudsPipeline());
    ApplyRect(Rect{0, 0, shape.width, shape.height}, shape.width, shape.height);
    gpu_shaders::MapCloudParams params{};
    params.size[0] = static_cast<float>(shape.width);
    params.size[1] = static_cast<float>(shape.height);
    params.time = clouds.time;
    params.start = static_cast<float>(clouds.start);
    params.base = static_cast<float>(clouds.width) / 255.0f;
    params.seed = static_cast<int>(clouds.seed);
    gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::map_clouds::kBlockMapCloudParams, params);
    gpu::Draw(0, 6);
    gpu::EndPass();
    if (shape.levels > 1) DrawLevels(shape, image);
}

bool ReadImageLevel(const gpu::Texture& image, int level, int width, int height, std::vector<std::uint8_t>* rgba) {
    if (!image || width <= 0 || height <= 0 || !EnsurePipelines()) return false;
    gpu::Texture target = gpu::MakeTexture({width, height, 1, 1, gpu::Format::Rgba8, gpu::TextureUse::ColourTarget, "ei-texture-level-read"});
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
