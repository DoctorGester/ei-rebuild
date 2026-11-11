// The tools' overlay (render.h's OverlayTriangles): a tool's panel drawn into the frame among its commands, with the
// tool's own textures. See docs/notes/renderer.md, "render_overlay.cpp: the tools' overlay".
#include "render_internal.h"

#include <cstddef>
#include <unordered_map>

namespace render {

static StreamBuffer g_overlayVertices(gpu::BufferUse::Vertex, "ei-overlay-vertices");
static StreamBuffer g_overlayIndices(gpu::BufferUse::Index, "ei-overlay-indices");
static int g_vertexOffset = -1;   // this frame's triangles on the GPU (UploadOverlay), -1: none
static int g_indexOffset = -1;
static std::size_t g_uploadedIndices = 0;
static std::unordered_map<std::uint32_t, gpu::Texture> g_overlayTextures;
static std::uint32_t g_lastOverlayTexture = 0;   // the last id given

// The triangles' pipeline: alpha-blended over the frame (the frame's alpha kept), no depth test or write.
static gpu::PipelineDesc OverlayDesc(int samples) {
    namespace program = gpu_shaders::overlay;
    gpu::PipelineDesc p;
    p.program = program::kIndex;
    p.bufferCount = 1;
    p.buffers[0].stride = sizeof(OverlayVertex);
    p.attributeCount = 3;
    p.attributes[0] = {program::kAttrPosition, 0, gpu::VertexFormat::Float2, offsetof(OverlayVertex, x)};
    p.attributes[1] = {program::kAttrUv, 0, gpu::VertexFormat::Float2, offsetof(OverlayVertex, u)};
    p.attributes[2] = {program::kAttrColour, 0, gpu::VertexFormat::UByte4Norm, offsetof(OverlayVertex, colour)};
    p.depth = kDepthFormat;
    p.colour = kColourFormat;
    p.colourMask = gpu::ColourMask::Red | gpu::ColourMask::Green | gpu::ColourMask::Blue;
    p.blend = {true, gpu::BlendFactor::SourceAlpha, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendFactor::One,
               gpu::BlendFactor::Zero, gpu::BlendOp::Add, gpu::BlendOp::Add};
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

void OverlayTriangles(std::span<const OverlayVertex> vertices, std::span<const std::uint32_t> indices,
                      std::span<const OverlayDraw> draws) {
    Frame& f = g_frame;
    const auto base = static_cast<std::uint32_t>(f.overlayVertices.size());
    const auto first = static_cast<std::uint32_t>(f.overlayIndices.size());
    f.overlayVertices.insert(f.overlayVertices.end(), vertices.begin(), vertices.end());
    for (std::uint32_t index : indices) f.overlayIndices.push_back(index < vertices.size() ? base + index : base);
    for (const OverlayDraw& draw : draws) {
        if (draw.indexCount == 0 || draw.firstIndex >= indices.size()) continue;
        const std::uint32_t count = draw.indexCount < indices.size() - draw.firstIndex
                                        ? draw.indexCount
                                        : static_cast<std::uint32_t>(indices.size() - draw.firstIndex);
        f.overlayBatches.push_back(OverlayBatch{first + draw.firstIndex, count, draw.texture, draw.clip});
        f.commands.push_back(Command{Command::Kind::Overlay, static_cast<int>(f.overlayBatches.size()) - 1});
    }
}

std::uint32_t MakeOverlayTexture(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    gpu::Texture image = gpu::MakeTexture({width, height, 1, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-overlay"});
    if (!image) return 0;
    g_overlayTextures[++g_lastOverlayTexture] = image;
    return g_lastOverlayTexture;
}

void WriteOverlayTexture(std::uint32_t texture, const Rect& region, const void* rows, std::size_t pitch) {
    auto found = g_overlayTextures.find(texture);
    if (found == g_overlayTextures.end() || region.Width() <= 0 || region.Height() <= 0) return;
    gpu::Write(found->second, 0, region, rows, pitch);
}

void DropOverlayTexture(std::uint32_t texture) {
    auto found = g_overlayTextures.find(texture);
    if (found == g_overlayTextures.end()) return;
    gpu::Release(found->second);
    g_overlayTextures.erase(found);
}

void UploadOverlay(const Frame& f) {
    if (f.overlayIndices.empty() || f.overlayIndices.size() == g_uploadedIndices) return;
    g_vertexOffset = g_overlayVertices.Append(f.overlayVertices.data(), f.overlayVertices.size() * sizeof(OverlayVertex));
    g_indexOffset = g_overlayIndices.Append(f.overlayIndices.data(), f.overlayIndices.size() * sizeof(std::uint32_t));
    g_uploadedIndices = f.overlayIndices.size();
}

void OverlayFrameDone() {
    g_vertexOffset = -1;
    g_indexOffset = -1;
    g_uploadedIndices = 0;
}

void DrawOverlayBatch(const OverlayBatch& batch, int width, int height) {
    if (batch.count == 0 || g_vertexOffset < 0 || g_indexOffset < 0) return;
    const Rect clip = batch.clip.Intersection(Rect{0, 0, width, height});
    if (clip.Width() <= 0 || clip.Height() <= 0) return;
    const gpu::Pipeline pipeline = MakePassPipeline(OverlayDesc(PassSampleCount()), "ei-overlay");
    if (!pipeline) return;
    auto found = g_overlayTextures.find(batch.texture);
    const gpu::Texture& image = found != g_overlayTextures.end() ? found->second : WhiteView();
    gpu::ApplyPipeline(pipeline);
    const gpu::BufferBinding vertices{g_overlayVertices.Buffer(), static_cast<std::uint32_t>(g_vertexOffset)};
    gpu::BindVertexBuffers(0, {&vertices, 1});
    gpu::BindIndexBuffer({g_overlayIndices.Buffer(), static_cast<std::uint32_t>(g_indexOffset)});
    const gpu::TextureBinding texture{
        image, gpu::MakeSampler({gpu::Filter::Linear, gpu::Filter::Linear, gpu::Filter::Nearest, gpu::Wrap::Clamp, gpu::Wrap::Clamp})};
    gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::overlay::kSamplerTex0, {&texture, 1});
    const RectParams vs{{}, {static_cast<float>(width), static_cast<float>(height)}};
    gpu::PushUniforms(gpu::Stage::Vertex, gpu_shaders::overlay::kBlockRectParams, vs);
    gpu::SetScissor(clip);
    gpu::DrawIndexed(batch.first, batch.count);
    gpu::SetScissor(Rect{0, 0, width, height});
}

} // namespace render
