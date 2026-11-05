// The renderer's harness API (render_checks.h): what the frames cost, the counts and CPU times of FrameStats, and the
// timers behind them. The switches and read-backs of the renderer's state are in the files that keep that state.
#include "render_checks.h"

#include "render_internal.h"
#include "render_perf.h"

#include <unordered_map>
#include <unordered_set>

namespace render {

FrameStats g_stats;   // the frame being built

// The game's timed work (BeginDeviceMeshTime and the others), at most one of each kind running.
static Timer g_deviceMeshTimer, g_decalTimer, g_flatDrawTimer, g_shadowBuildTimer;

static FrameStats g_statsSum;                  // the presented frames since the last TakeFrameStats
static const void* g_lastPipeline = nullptr;   // applied last in this frame
static const void* g_lastTexture = nullptr;
static int g_timedKind = -1;   // the kind of the innermost Timer with one running, -1 none
#if defined(EI_RENDER_VALIDATION)
// CountReuse: per key the hashes of the vertices' geometry (positions, texture coordinates) and
// colours, this frame's and the last; this frame's mesh geometry hashes.
struct VertexHashes {
    std::uint64_t geometry = 0, colours = 0;
};
static std::unordered_map<std::uint64_t, VertexHashes> g_hashes, g_lastHashes;
static std::unordered_set<std::uint64_t> g_frameGeometry;

static std::uint64_t Fnv(std::uint64_t h, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i + 4 <= bytes; i += 4) {
        const std::uint32_t word = p[i] | static_cast<std::uint32_t>(p[i + 1]) << 8 |
                                   static_cast<std::uint32_t>(p[i + 2]) << 16 | static_cast<std::uint32_t>(p[i + 3]) << 24;
        h = (h ^ word) * 0x100000001B3ull;
    }
    return h;
}
#endif

void Timer::Begin() {
    if (!start) start = Ticks();
}

void Timer::Begin(FrameStats::Time newKind) {
    if constexpr (kRenderValidation) {
        if (start) return;
        kind = newKind;
        outer = g_timedKind;
        g_timedKind = kind;
        start = Ticks();
    }
}

double Timer::End() {
    if (!start) return 0.0;
    const double ms = static_cast<double>(Ticks() - start) * kMsPerTick;
    start = 0;
    if (kind >= 0) {
        g_stats.ms[kind] += ms;
        if (outer >= 0) g_stats.ms[outer] -= ms;
        g_timedKind = outer;
        kind = outer = -1;
    }
    return ms;
}

void CountPipeline(gpu::Pipeline pipeline) {
    ++g_stats.counts[FrameStats::PipelineApplies];
    if (pipeline.pipeline != g_lastPipeline) ++g_stats.counts[FrameStats::PipelineChanges];
    g_lastPipeline = pipeline.pipeline;
}

void CountBindings(const gpu::Texture& first) {
    ++g_stats.counts[FrameStats::Bindings];
    if (first.texture != g_lastTexture) ++g_stats.counts[FrameStats::TextureChanges];
    g_lastTexture = first.texture;
}

void CountDraw(std::size_t uniformBytes) {
    ++g_stats.counts[FrameStats::Draws];
    g_stats.counts[FrameStats::UniformBytes] += uniformBytes;
}

void CountReuse([[maybe_unused]] bool terrain, [[maybe_unused]] const void* key, [[maybe_unused]] int page,
                [[maybe_unused]] const Vertex* vertices, [[maybe_unused]] std::size_t count) {
#if defined(EI_RENDER_VALIDATION)
    Timer timer(FrameStats::StatsMs);
    VertexHashes h{0xCBF29CE484222325ull, 0xCBF29CE484222325ull};
    for (std::size_t i = 0; i < count; ++i) {
        const Vertex& v = vertices[i];
        h.geometry = Fnv(h.geometry, &v.position, sizeof(v.position));
        h.geometry = Fnv(h.geometry, &v.u, 2 * sizeof(float));
        h.colours = Fnv(h.colours, &v.colour, 2 * sizeof(std::uint32_t));
    }
    std::uint64_t id = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key)) << 8) ^
                       static_cast<std::uint64_t>(page) ^ (terrain ? 1ull << 63 : 0);
    auto last = g_lastHashes.find(id);
    FrameStats::Reuse r = last == g_lastHashes.end()            ? FrameStats::New
                          : last->second.geometry != h.geometry ? FrameStats::Moved
                          : last->second.colours != h.colours   ? FrameStats::Relit
                                                                : FrameStats::Same;
    auto& reuse = terrain ? g_stats.terrainReuse : g_stats.meshReuse;
    ++reuse[r];
    g_hashes[id] = h;
    if (!terrain && !g_frameGeometry.insert(h.geometry ^ count).second) ++g_stats.counts[FrameStats::SharedMeshes];
#endif
}

void CountUpload(FrameStats::Upload kind, std::size_t bytes) {
    ++g_stats.uploads[kind];
    g_stats.uploadBytes[kind] += bytes;
}

FrameStats TakeFrameStats() {
    FrameStats s = g_statsSum;
    g_statsSum = FrameStats{};
    return s;
}

void AddFrameStats(std::size_t matrices, int width, int height) {
    g_stats.counts[FrameStats::Matrices] = matrices;
    if (width > 0) {
        g_statsSum.width = width;
        g_statsSum.height = height;
    }
    FrameStats& s = g_statsSum;
    const FrameStats& f = g_stats;
    s.frames += 1;
    for (int k = 0; k < FrameStats::CountKinds; ++k) s.counts[k] += f.counts[k];
    for (int k = 0; k < FrameStats::TimeKinds; ++k) s.ms[k] += f.ms[k];
    for (int k = 0; k < FrameStats::UploadKinds; ++k) {
        s.uploads[k] += f.uploads[k];
        s.uploadBytes[k] += f.uploadBytes[k];
    }
    for (int k = 0; k < FrameStats::ReuseKinds; ++k) {
        s.terrainReuse[k] += f.terrainReuse[k];
        s.meshReuse[k] += f.meshReuse[k];
    }
    g_stats = FrameStats{};
    g_lastPipeline = g_lastTexture = nullptr;
#if defined(EI_RENDER_VALIDATION)
    g_lastHashes.swap(g_hashes);
    g_hashes.clear();
    g_frameGeometry.clear();
#endif
}

void BeginDeviceMeshTime() { g_deviceMeshTimer.Begin(FrameStats::DeviceMeshMs); }

void EndDeviceMeshTime() { g_deviceMeshTimer.End(); }

void BeginDecalTime() {
    if (g_decalTimer.Running()) return;
    g_decalTimer.Begin(FrameStats::DecalPrintMs);
    if (g_decalTimer.Running()) ++g_stats.counts[FrameStats::DecalPrints];
}

void EndDecalTime(int vertices) {
    if (!g_decalTimer.Running()) return;
    g_decalTimer.End();
    g_stats.counts[FrameStats::DecalVertices] += static_cast<std::uint64_t>(vertices);
}

void BeginFlatDrawTime(int vertices) {
    if (g_flatDrawTimer.Running()) return;
    g_flatDrawTimer.Begin(FrameStats::FlatMs);
    if (!g_flatDrawTimer.Running()) return;
    ++g_stats.counts[FrameStats::FlatDraws];
    g_stats.counts[FrameStats::FlatDrawVertices] += static_cast<std::uint64_t>(vertices);
}

void EndFlatDrawTime() { g_flatDrawTimer.End(); }

void BeginShadowBuildTime() {
    if (g_shadowBuildTimer.Running()) return;
    g_shadowBuildTimer.Begin(FrameStats::ShadowBuildMs);
    if (g_shadowBuildTimer.Running()) ++g_stats.counts[FrameStats::ShadowBuilds];
}

void EndShadowBuildTime() { g_shadowBuildTimer.End(); }

std::uint64_t perf::FrameDrawCount() { return g_stats.counts[FrameStats::Draws] + g_stats.counts[FrameStats::ShadowDraws]; }

} // namespace render
