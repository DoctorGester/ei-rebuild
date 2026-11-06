// ei_gpu_check --bench: the draw loop's and the downloads' costs on gpu.h, per driver (docs/notes/renderer.md, "gpu.h").
//
// The draw loop: a synthetic world frame of the renderer's shape, replayed through gpu.h's calls the way render_mesh.cpp
// and render_shadows.cpp make them: a shadow pass of 155 instanced caster draws into a 4096x4096 depth map, a world pass
// of 263 mesh draws (the mesh program's three uniform blocks, six samplers and each draw's record, written before the
// pass) into a colour and depth target of the window's size, then the present, one call a draw. About half of the draws
// keep the mesh and half keep the textures of the draw before, the fragment block changes rarely. Each case runs with the state filter on
// and off (gpu::SetStateFiltering), the window presenting immediate, and reports the frame time, the time recording a
// frame, and the calls sent and left out.
//
// The downloads: a 32-bit frame target read back to the CPU, idle and after a frame drawn into it, waited for as the
// harness's captures do, and polled a frame later without a wait.
#include "render/gpu.h"

#include "shaders/gpu_shaders.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace render_bench {

namespace {

namespace g = render::gpu;
namespace sh = gpu_shaders;

double NowMs() { return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()); }

struct Stats {
    double mean = 0.0, median = 0.0;
};
Stats Summary(std::vector<double> v) {
    if (v.empty()) return {};
    double sum = 0.0;
    for (double x : v) sum += x;
    std::sort(v.begin(), v.end());
    return {sum / static_cast<double>(v.size()), v[v.size() / 2]};
}

std::uint32_t g_seed = 12345;
std::uint32_t Next(std::uint32_t modulo) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (g_seed >> 8) % modulo;
}

struct MeshVertex {
    float position[3];
    std::uint8_t colour[4], specular[4];
    float uv[2];
    float normal[3];
};
static_assert(sizeof(MeshVertex) == 40);
struct CasterVertex {
    float position[3];
    float uv[2];
};
struct CasterInstance {
    float rows[4][4];
    float cut[4];
};

constexpr int kMeshBuffers = 10, kMeshesPerBuffer = 4, kMeshes = kMeshBuffers * kMeshesPerBuffer;
constexpr int kVerticesPerMesh = 300, kIndicesPerMesh = 900;
constexpr int kTextures = 38;
constexpr int kCasterDraws = 155, kWorldDraws = 263;

struct Draw {
    int mesh, texture[3], pipeline;
    bool fsChanges;
};

struct World {
    g::Buffer vertices[kMeshBuffers], casterVertices[kMeshBuffers], indices[kMeshBuffers], instances;
    g::Buffer records, lights, recordIndices;   // the world draws' MeshRecords, a PartLight, 0, 1, 2, ... per instance
    g::Texture textures[kTextures], range;
    g::Sampler linear, nearest;
    g::Pipeline caster, mesh[2], blit;
    g::Format swapFormat = g::Format::None;
    std::vector<Draw> casters, world;
};

g::PipelineDesc CasterDesc() {
    namespace sc = sh::shadow_caster;
    g::PipelineDesc d;
    d.program = sc::kIndex;
    d.bufferCount = 2;
    d.buffers[0] = {sizeof(CasterVertex), false};
    d.buffers[1] = {sizeof(CasterInstance), true};
    d.attributeCount = 7;
    d.attributes[0] = {sc::kAttrPos, 0, g::VertexFormat::Float3, 0};
    d.attributes[1] = {sc::kAttrUv, 0, g::VertexFormat::Float2, 12};
    d.attributes[2] = {sc::kAttrRow0, 1, g::VertexFormat::Float4, 0};
    d.attributes[3] = {sc::kAttrRow1, 1, g::VertexFormat::Float4, 16};
    d.attributes[4] = {sc::kAttrRow2, 1, g::VertexFormat::Float4, 32};
    d.attributes[5] = {sc::kAttrRow3, 1, g::VertexFormat::Float4, 48};
    d.attributes[6] = {sc::kAttrCut, 1, g::VertexFormat::Float4, 64};
    d.colour = g::Format::None;
    d.depth = g::Format::Depth32Float;
    d.depthCompare = g::Compare::LessEqual;
    d.depthWrite = true;
    return d;
}

g::PipelineDesc MeshDesc(bool blend) {
    namespace m = sh::mesh;
    g::PipelineDesc d;
    d.program = m::kIndex;
    d.bufferCount = 2;
    d.buffers[0] = {sizeof(MeshVertex), false};
    d.buffers[1] = {sizeof(std::uint32_t), true};
    d.attributeCount = 6;
    d.attributes[5] = {m::kAttrRecord, 1, g::VertexFormat::Uint, 0};
    d.attributes[0] = {m::kAttrPos, 0, g::VertexFormat::Float3, 0};
    d.attributes[1] = {m::kAttrCol, 0, g::VertexFormat::UByte4Norm, 12};
    d.attributes[2] = {m::kAttrSpec, 0, g::VertexFormat::UByte4Norm, 16};
    d.attributes[3] = {m::kAttrUv, 0, g::VertexFormat::Float2, 20};
    d.attributes[4] = {m::kAttrNormal, 0, g::VertexFormat::Float3, 28};
    d.cull = g::CullFace::Back;
    d.colour = g::Format::Rgba8;
    d.depth = g::Format::Depth32Float;
    d.depthCompare = g::Compare::LessEqual;
    d.depthWrite = !blend;
    if (blend) {
        d.blend.on = true;
        d.blend.sourceColour = g::BlendFactor::SourceAlpha;
        d.blend.destColour = g::BlendFactor::OneMinusSourceAlpha;
    }
    return d;
}

// `count` draws in the renderer's order of change: runs of one mesh, runs of one texture.
std::vector<Draw> Sequence(int count, int meshes, int textures) {
    std::vector<Draw> out;
    Draw d{};
    for (int i = 0; i < count; ++i) {
        if (i == 0 || Next(2) == 0) d.mesh = static_cast<int>(Next(static_cast<std::uint32_t>(meshes)));
        for (int t = 0; t < 3; ++t)
            if (i == 0 || Next(5) < 2) d.texture[t] = static_cast<int>(Next(static_cast<std::uint32_t>(textures)));
        if (i == 0 || Next(8) == 0) d.pipeline = static_cast<int>(Next(2));
        d.fsChanges = i == 0 || Next(10) < 3;
        out.push_back(d);
    }
    return out;
}

bool MakeWorld(World* w) {
    std::vector<MeshVertex> vertices(kVerticesPerMesh * kMeshesPerBuffer);
    for (int b = 0; b < kMeshBuffers; ++b) {
        // Triangles of a few pixels, so the GPU has little to do and the CPU sets the frame time: three vertices near one place.
        float centre[3] = {};
        int j = 0;
        for (MeshVertex& v : vertices) {
            v = {};
            if (j++ % 3 == 0)
                for (float& c : centre) c = static_cast<float>(Next(1800)) / 1000.0f - 0.9f;
            for (int k = 0; k < 3; ++k) v.position[k] = centre[k] + static_cast<float>(Next(8)) / 1000.0f - 0.004f;
            v.position[2] = 0.1f + 0.8f * (centre[2] + 0.9f) / 1.8f;
            std::memset(v.colour, 255, 4);
            v.uv[0] = static_cast<float>(Next(1000)) / 1000.0f;
            v.uv[1] = static_cast<float>(Next(1000)) / 1000.0f;
            v.normal[2] = 1.0f;
        }
        std::vector<CasterVertex> positions(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            std::memcpy(positions[i].position, vertices[i].position, sizeof(positions[i].position));
            std::memcpy(positions[i].uv, vertices[i].uv, sizeof(positions[i].uv));
        }
        w->casterVertices[b] = g::MakeBuffer(g::BufferUse::Vertex, static_cast<std::uint32_t>(positions.size() * sizeof(CasterVertex)), "bench caster vertices");
        g::Write(w->casterVertices[b], 0, positions.data(), static_cast<std::uint32_t>(positions.size() * sizeof(CasterVertex)));
        w->vertices[b] = g::MakeBuffer(g::BufferUse::Vertex, static_cast<std::uint32_t>(vertices.size() * sizeof(MeshVertex)), "bench vertices");
        g::Write(w->vertices[b], 0, vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(MeshVertex)));
        std::vector<std::uint32_t> indices(kIndicesPerMesh * kMeshesPerBuffer);
        for (int m = 0; m < kMeshesPerBuffer; ++m)
            for (int i = 0; i < kIndicesPerMesh; ++i)
                indices[static_cast<std::size_t>(m * kIndicesPerMesh + i)] = static_cast<std::uint32_t>(i % kVerticesPerMesh);
        w->indices[b] = g::MakeBuffer(g::BufferUse::Index, static_cast<std::uint32_t>(indices.size() * 4), "bench indices");
        g::Write(w->indices[b], 0, indices.data(), static_cast<std::uint32_t>(indices.size() * 4));
        if (!w->vertices[b] || !w->casterVertices[b] || !w->indices[b]) return false;
    }
    CasterInstance instance[8] = {};
    for (CasterInstance& c : instance) {
        c.rows[0][0] = c.rows[1][1] = c.rows[3][3] = 1.0f;
    }
    w->instances = g::MakeBuffer(g::BufferUse::Vertex, sizeof(instance), "bench instances");
    g::Write(w->instances, 0, instance, sizeof(instance));
    std::vector<sh::MeshRecord> records(kWorldDraws);
    std::vector<std::uint32_t> recordIndices(kWorldDraws);
    for (std::size_t i = 0; i < records.size(); ++i) {
        float* matrix = reinterpret_cast<float*>(&records[i].matrix);
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
        matrix[12] = 1e-5f * static_cast<float>(i);   // the matrix changes every draw
        recordIndices[i] = static_cast<std::uint32_t>(i);
    }
    w->records = g::MakeBuffer(g::BufferUse::Storage, static_cast<std::uint32_t>(records.size() * sizeof(sh::MeshRecord)), "bench records");
    g::Write(w->records, 0, records.data(), w->records.size);
    w->recordIndices = g::MakeBuffer(g::BufferUse::Vertex, static_cast<std::uint32_t>(recordIndices.size() * 4), "bench record indices");
    g::Write(w->recordIndices, 0, recordIndices.data(), w->recordIndices.size);
    const sh::PartLight light{};
    w->lights = g::MakeBuffer(g::BufferUse::Storage, sizeof(light), "bench lights");
    g::Write(w->lights, 0, &light, sizeof(light));
    if (!w->records || !w->lights || !w->recordIndices) return false;
    std::vector<std::uint32_t> texels(64 * 64, 0xFF808080u);
    for (g::Texture& t : w->textures) {
        t = g::MakeTexture({64, 64, 1, 1, g::Format::Rgba8, g::TextureUse::Sampled, "bench texture"});
        if (!t) return false;
        g::Write(t, 0, {0, 0, 64, 64}, texels.data(), 64 * 4);
    }
    w->range = g::MakeTexture({256, 256, 1, 1, g::Format::Rgba32Float, g::TextureUse::Sampled, "bench range"});
    const std::vector<float> rangeTexels(256 * 256 * 4, 0.5f);
    g::Write(w->range, 0, {0, 0, 256, 256}, rangeTexels.data(), 256 * 16);
    w->linear = g::MakeSampler({});
    w->nearest = g::MakeSampler({g::Filter::Nearest, g::Filter::Nearest});
    w->caster = g::MakePipeline(CasterDesc(), "bench caster");
    w->mesh[0] = g::MakePipeline(MeshDesc(false), "bench mesh");
    w->mesh[1] = g::MakePipeline(MeshDesc(true), "bench mesh blended");
    g::PipelineDesc blit;
    blit.program = sh::blit::kIndex;
    blit.colour = w->swapFormat;
    w->blit = g::MakePipeline(blit, "bench blit");
    w->casters = Sequence(kCasterDraws, kMeshes, kTextures);
    w->world = Sequence(kWorldDraws, kMeshes, kTextures);
    return w->range && w->linear && w->nearest && w->caster && w->mesh[0] && w->mesh[1] && w->blit;
}

void ReleaseWorld(World* w) {
    for (g::Buffer& b : w->vertices) g::Release(b);
    for (g::Buffer& b : w->casterVertices) g::Release(b);
    for (g::Buffer& b : w->indices) g::Release(b);
    g::Release(w->instances);
    g::Release(w->records);
    g::Release(w->lights);
    g::Release(w->recordIndices);
    for (g::Texture& t : w->textures) g::Release(t);
    g::Release(w->range);
}

struct Targets {
    g::Texture shadow, colour, depth;
};

// The frame's commands (not its acquire or its submission).
void RecordFrame(const World& w, const Targets& t, const g::Texture& swap) {
    {
        g::PassDesc p;
        p.depth = {t.shadow, 0, g::Load::Clear, 1.0f, true};
        g::BeginPass(p);
        for (const Draw& d : w.casters) {
            g::ApplyPipeline(w.caster);
            const g::BufferBinding buffers[2] = {{w.casterVertices[d.mesh / kMeshesPerBuffer],
                                                  static_cast<std::uint32_t>(d.mesh % kMeshesPerBuffer * kVerticesPerMesh * sizeof(CasterVertex))},
                                                 {w.instances, 0}};
            g::BindVertexBuffers(0, buffers);
            g::BindIndexBuffer({w.indices[d.mesh / kMeshesPerBuffer],
                                static_cast<std::uint32_t>(d.mesh % kMeshesPerBuffer * kIndicesPerMesh * 4)});
            const g::TextureBinding tex[1] = {{w.textures[d.texture[0]], w.nearest}};
            g::BindTextures(g::Stage::Fragment, sh::shadow_caster::kSamplerTex, tex);
            g::DrawIndexed(0, kIndicesPerMesh, 1, 0);
        }
        g::EndPass();
    }
    {
        g::PassDesc p;
        p.colour.texture = t.colour;
        p.colour.load = g::Load::Clear;
        p.depth = {t.depth, 0, g::Load::Clear, 1.0f, true};
        g::BeginPass(p);
        sh::MeshFsParams fs{};
        sh::ShadowParams shadow{};
        sh::PointShadowParams point{};
        float fsStep = 0.0f;
        std::uint32_t n = 0;
        for (const Draw& d : w.world) {
            g::ApplyPipeline(w.mesh[d.pipeline]);
            const g::BufferBinding vertices[2] = {{w.vertices[d.mesh / kMeshesPerBuffer],
                                                   static_cast<std::uint32_t>(d.mesh % kMeshesPerBuffer * kVerticesPerMesh * sizeof(MeshVertex))},
                                                  {w.recordIndices, 0}};
            g::BindVertexBuffers(0, vertices);
            g::BindIndexBuffer({w.indices[d.mesh / kMeshesPerBuffer],
                                static_cast<std::uint32_t>(d.mesh % kMeshesPerBuffer * kIndicesPerMesh * 4)});
            g::TextureBinding tex[3];
            for (int k = 0; k < 3; ++k) tex[k] = {w.textures[d.texture[k]], w.linear};
            g::BindTextures(g::Stage::Fragment, sh::mesh::kSamplerTex0, tex);
            const g::TextureBinding shadows[1] = {{t.shadow, w.nearest}};
            g::BindTextures(g::Stage::Fragment, sh::mesh::kSamplerShadowMap, shadows);
            const g::TextureBinding ranges[1] = {{w.range, w.nearest}};
            g::BindTextures(g::Stage::Fragment, sh::mesh::kSamplerShadowRange, ranges);
            g::BindTextures(g::Stage::Fragment, sh::mesh::kSamplerPointShadowMap, ranges);
            const g::Buffer storage[2] = {w.records, w.lights};
            g::BindStorageBuffers(g::Stage::Vertex, sh::mesh::kStorageMeshRecord, {storage, 1});
            g::BindStorageBuffers(g::Stage::Fragment, sh::mesh::kStorageMeshRecord, storage);
            if (d.fsChanges) fs.fogColour[0] = (fsStep += 0.001f);
            g::PushUniforms(g::Stage::Fragment, sh::mesh::kBlockMeshFsParams, fs);
            g::PushUniforms(g::Stage::Fragment, sh::mesh::kBlockShadowParams, shadow);
            g::PushUniforms(g::Stage::Fragment, sh::mesh::kBlockPointShadowParams, point);
            g::DrawIndexed(0, kIndicesPerMesh, 1, n++);
        }
        g::EndPass();
    }
    g::PassDesc p;
    p.colour.texture = swap;
    p.colour.load = g::Load::DontCare;
    g::BeginPass(p);
    g::ApplyPipeline(w.blit);
    const g::TextureBinding tex[1] = {{t.colour, w.nearest}};
    g::BindTextures(g::Stage::Fragment, sh::blit::kSamplerTex, tex);
    const sh::RectParams rect{{0, 0, static_cast<float>(swap.width), static_cast<float>(swap.height)},
                              {static_cast<float>(swap.width), static_cast<float>(swap.height)}};
    const sh::BlitParams blit{{0, 0}, {1, 1}, {0, 0}, {static_cast<float>(swap.width), static_cast<float>(swap.height)}};
    g::PushUniforms(g::Stage::Vertex, sh::blit::kBlockRectParams, rect);
    g::PushUniforms(g::Stage::Fragment, sh::blit::kBlockBlitParams, blit);
    g::Draw(0, 6);
    g::EndPass();
}

struct Result {
    std::vector<double> frames, records;        // every round's
    std::vector<double> frameMedians, recordMedians;   // each round's
    g::DrawCounts counts;
    std::size_t frameCount = 0;
};

// One round of `count` frames after 60 unmeasured ones; false when the swap chain fails.
bool Round(World& world, const Targets& t, int count, Result* out) {
    std::vector<double> frames, records;
    for (int k = -60; k < count; ++k) {
        SDL_PumpEvents();
        if (k == 0) g::ResetCounts();
        const double start = NowMs();
        g::Texture swap;
        if (!g::AcquireSwapchain(&swap)) return false;
        if (swap) {
            const double recordStart = NowMs();
            RecordFrame(world, t, swap);
            g::Submit();
            if (k >= 0) records.push_back(NowMs() - recordStart);
        } else {
            g::Submit();
        }
        if (k >= 0) frames.push_back(NowMs() - start);
    }
    const g::DrawCounts c = g::Counts();
    out->counts.draws += c.draws;
    out->counts.pipelinesBound += c.pipelinesBound, out->counts.pipelinesSkipped += c.pipelinesSkipped;
    out->counts.vertexBound += c.vertexBound, out->counts.vertexSkipped += c.vertexSkipped;
    out->counts.indexBound += c.indexBound, out->counts.indexSkipped += c.indexSkipped;
    out->counts.texturesBound += c.texturesBound, out->counts.texturesSkipped += c.texturesSkipped;
    out->counts.pushes += c.pushes, out->counts.pushesSkipped += c.pushesSkipped, out->counts.pushedBytes += c.pushedBytes;
    out->frameCount += frames.size();
    out->frameMedians.push_back(Summary(frames).median);
    out->recordMedians.push_back(Summary(records).median);
    out->frames.insert(out->frames.end(), frames.begin(), frames.end());
    out->records.insert(out->records.end(), records.begin(), records.end());
    return true;
}

std::pair<double, double> Range(const std::vector<double>& v) {
    return {*std::min_element(v.begin(), v.end()), *std::max_element(v.begin(), v.end())};
}

// Three rounds with the filter off and on, alternating; each case's means and medians over the rounds and the range of
// the rounds' medians.
void Frames(const char* driver, int width, int height, World& world, int count) {
    Targets t;
    t.shadow = g::MakeTexture({4096, 4096, 1, 1, g::Format::Depth32Float, g::TextureUse::DepthTarget | g::TextureUse::Sampled, "bench shadow"});
    t.colour = g::MakeTexture({width, height, 1, 1, g::Format::Rgba8, g::TextureUse::ColourTarget | g::TextureUse::Sampled, "bench colour"});
    t.depth = g::MakeTexture({width, height, 1, 1, g::Format::Depth32Float, g::TextureUse::DepthTarget, "bench depth"});
    if (!t.shadow || !t.colour || !t.depth) {
        std::printf("gpu_bench: %s: no targets for %dx%d\n", driver, width, height);
        return;
    }
    Result results[2];   // filter off, on
    for (int round = 0; round < 3; ++round) {
        for (int on = 0; on < 2; ++on) {
            g::SetStateFiltering(on != 0);
            if (!Round(world, t, count, &results[on])) std::printf("gpu_bench: %s: the swap chain failed: %s\n", driver, g::Error());
        }
    }
    g::SetStateFiltering(true);
    for (int on = 0; on < 2; ++on) {
        const Result& r = results[on];
        if (r.frames.empty()) continue;
        const Stats f = Summary(r.frames), rec = Summary(r.records);
        const auto [fLow, fHigh] = Range(r.frameMedians);
        const auto [rLow, rHigh] = Range(r.recordMedians);
        const double n = static_cast<double>(r.frameCount);
        const g::DrawCounts& c = r.counts;
        std::printf("gpu_bench: %s %dx%d filter %-3s: frame mean %.3f median %.3f (rounds %.3f-%.3f) ms; recording mean %.3f median %.3f "
                    "(rounds %.3f-%.3f) ms; per frame %.0f draws, pipelines %.0f of %.0f, vertex binds %.0f of %.0f, index %.0f of %.0f, "
                    "texture calls %.0f of %.0f, pushes %.0f of %.0f (%.1f KB)\n",
                    driver, width, height, on ? "on" : "off", f.mean, f.median, fLow, fHigh, rec.mean, rec.median, rLow, rHigh,
                    static_cast<double>(c.draws) / n, static_cast<double>(c.pipelinesBound) / n,
                    static_cast<double>(c.pipelinesBound + c.pipelinesSkipped) / n, static_cast<double>(c.vertexBound) / n,
                    static_cast<double>(c.vertexBound + c.vertexSkipped) / n, static_cast<double>(c.indexBound) / n,
                    static_cast<double>(c.indexBound + c.indexSkipped) / n, static_cast<double>(c.texturesBound) / n,
                    static_cast<double>(c.texturesBound + c.texturesSkipped) / n, static_cast<double>(c.pushes) / n,
                    static_cast<double>(c.pushes + c.pushesSkipped) / n, static_cast<double>(c.pushedBytes) / n / 1024.0);
    }
    g::Release(t.shadow);
    g::Release(t.colour);
    g::Release(t.depth);
}

// A frame target of width x height drawn and read back as a capture does: `idle` (nothing drawn since the last
// read), drawn (a pass clearing it, then the read), and polled (the download recorded with a frame and read a frame
// later without a wait: the cost of the calls).
void Readbacks(const char* driver, int width, int height, int count) {
    g::Texture target = g::MakeTexture({width, height, 1, 1, g::Format::Rgba8, g::TextureUse::ColourTarget | g::TextureUse::Sampled, "bench frame"});
    if (!target) return;
    auto draw = [&](float shade) {
        g::PassDesc p;
        p.colour.texture = target;
        p.colour.load = g::Load::Clear;
        p.colour.clear[0] = shade;
        g::BeginPass(p);
        g::EndPass();
    };
    auto read = [&]() {
        g::Ticket ticket = g::Download(target, 0, {0, 0, width, height});
        g::Texels texels;
        const bool ok = g::Wait(ticket) && g::Read(ticket, &texels);
        volatile std::uint8_t sink = ok ? texels.Row(height - 1)[0] : 0;
        (void)sink;
        g::Release(ticket);
        return ok;
    };
    draw(0.25f);
    g::Submit();
    double firstStart = NowMs();
    read();
    const double first = NowMs() - firstStart;
    std::vector<double> idle, drawn, drawnSplit;
    for (int k = 0; k < count; ++k) {
        const double a = NowMs();
        read();
        idle.push_back(NowMs() - a);
    }
    for (int k = 0; k < count; ++k) {
        const double a = NowMs();
        draw(static_cast<float>(k % 7) / 7.0f);
        read();
        drawn.push_back(NowMs() - a);
    }
    // Polled: the frame's commands, the download after them, the submission, and the read when the fence has passed
    // (the next frame, or later).
    std::vector<double> calls;
    g::Ticket pending;
    int polls = 0;
    for (int k = 0; k < count; ++k) {
        const double a = NowMs();
        draw(static_cast<float>(k % 5) / 5.0f);
        g::Ticket ticket = g::Download(target, 0, {0, 0, width, height});
        g::Submit();
        if (pending) {
            ++polls;
            g::Texels texels;
            if (g::Done(pending) && g::Read(pending, &texels)) g::Release(pending);
            else g::Wait(pending), g::Release(pending);
        }
        pending = ticket;
        calls.push_back(NowMs() - a);
    }
    if (pending) {
        g::Wait(pending);
        g::Release(pending);
    }
    const Stats i = Summary(idle), d = Summary(drawn), c = Summary(calls);
    std::printf("gpu_bench: %s %dx%d readback (mean / median ms of %d): first %.2f, idle %.2f / %.2f, drawn then read %.2f / %.2f, "
                "drawn and read a frame later without a wait %.2f / %.2f\n",
                driver, width, height, count, first, i.mean, i.median, d.mean, d.median, c.mean, c.median);
    g::Release(target);
}

} // namespace

// Every case on `driver`; false when it made no device.
bool Run(const char* driver, int frames) {
    g::DeviceOptions options;
    options.driver = driver;
    if (!g::Start(options)) {
        std::printf("gpu_bench: %s: no device (%s)\n", driver, g::Error());
        return false;
    }
    std::printf("gpu_bench: %s (%s)\n", driver, g::DeviceName());
    for (const int width : {960, 2560}) Readbacks(driver, width, width == 960 ? 720 : 1440, std::max(20, frames / 5));
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "ei_gpu_bench");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, 40);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 40);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 960);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 720);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, true);
    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window && g::ClaimWindow(window)) {
        g::SetPresentMode(g::PresentMode::Immediate);
        World world;
        world.swapFormat = g::SwapchainFormat();
        if (MakeWorld(&world)) {
            for (const int width : {960, 2560}) {
                const int height = width == 960 ? 720 : 1440;
                SDL_SetWindowSize(window, width, height);
                SDL_SyncWindow(window);
                Frames(driver, width, height, world, frames);
            }
        } else {
            std::printf("gpu_bench: %s: the world could not be made (%s)\n", driver, g::Error());
        }
        ReleaseWorld(&world);
        g::ReleaseWindow();
    } else {
        std::printf("gpu_bench: %s: no window: %s\n", driver, SDL_GetError());
    }
    if (window) SDL_DestroyWindow(window);
    g::Stop();
    return true;
}

} // namespace render_bench
