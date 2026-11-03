// The render quality's multisampling (docs/notes/renderer.md section 11): render.cpp draws the world view's
// commands into a multisampled colour and depth target and resolves the colour into the frame target at the
// end of each such pass; the interface draws into the frame target as without the option. A multisampled depth
// cannot be read back (SDL_GPU neither samples a multisampled texture nor resolves a depth), so every pipeline of
// such a pass also writes each fragment's depth into a second, R32F, colour target, resolved with the colour.
// Here: that target on the pipelines, and the two copies between the targets where the frame changes from one
// to the other (the frame target's colour and depth into every sample before a world run that follows interface
// draws; the resolved depth copy into the frame target's depth after a world run).
#include "render.h"
#include "render_internal.h"

#include "../platform/log.h"

namespace render {

namespace {

using namespace detail;

gpu::Pipeline g_into, g_from;
int g_copySamples = 0;   // the sample count the copies were made for

// Depth always written, colour as `writeColour`, for `samples`.
gpu::Pipeline CopyPipeline(int program, int samples, bool writeColour, const char* label) {
    gpu::PipelineDesc p;
    p.program = program;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::Always;
    p.depthWrite = true;
    p.colour = kColourFormat;
    p.colourMask = writeColour ? gpu::kWriteAll : 0;
    p.samples = samples;
    AddDepthCopy(p);
    return gpu::MakePipeline(p, label);
}

void Draw(gpu::Pipeline pipeline, const gpu::Texture* textures, int count) {
    gpu::ApplyPipeline(pipeline);
    const gpu::Sampler point = gpu::MakeSampler({gpu::Filter::Nearest, gpu::Filter::Nearest});
    gpu::TextureBinding b[2];
    for (int k = 0; k < count; ++k) b[k] = {textures[k], point};
    gpu::BindTextures(gpu::Stage::Fragment, 0, {b, static_cast<std::size_t>(count)});
    gpu::Draw(0, 3);
}

} // namespace

namespace detail {

void AddDepthCopy(gpu::PipelineDesc& p) {
    if (p.samples <= 1) return;
    p.second = kDepthCopyFormat;
    p.secondMask = p.depthWrite ? gpu::kWriteRed : 0;
}

bool PrepareSmoothCopies(int samples) {
    if (g_copySamples == samples) return true;
    g_copySamples = 0;
    g_into = CopyPipeline(gpu_shaders::into_multisampled::kIndex, samples, true, "ei-into-multisampled");
    g_from = CopyPipeline(gpu_shaders::depth_from_resolved::kIndex, 1, false, "ei-depth-from-resolved");
    if (!g_into || !g_from) {
        ei::Log("render: msaa: the copies between the targets could not be made (see the gpu messages above)");
        return false;
    }
    g_copySamples = samples;
    return true;
}

static_assert(gpu_shaders::into_multisampled::kSamplerColour == 0 && gpu_shaders::into_multisampled::kSamplerDepth == 1 &&
              gpu_shaders::depth_from_resolved::kSamplerDepthCopy == 0);

void CopyIntoMultisampled(const gpu::Texture& colour, const gpu::Texture& depth) {
    const gpu::Texture textures[2] = {colour, depth};
    Draw(g_into, textures, 2);
}

void CopyDepthFromResolved(const gpu::Texture& depth) { Draw(g_from, &depth, 1); }

} // namespace detail

} // namespace render
