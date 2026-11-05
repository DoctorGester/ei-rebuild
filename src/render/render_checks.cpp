// The renderer's harness API (render_checks.h): what the frames cost, the counts and CPU times of FrameStats, and the
// timers behind them, and the read-backs that need no state of the files they check (the held textures' levels, the
// movie uploads). The switches and the other read-backs are in the files that keep that state.
#include "render_checks.h"

#include "../platform/log.h"
#include "render_internal.h"
#include "render_perf.h"

#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace render {

FrameStats g_stats;   // the frame being built

// The game's timed work (BeginDeviceMeshTime and the others), at most one of each kind running.
static Timer g_deviceMeshTimer, g_decalTimer, g_flatDrawTimer, g_shadowBuildTimer, g_weatherTimer;

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
        kind = static_cast<int>(newKind);
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
    ++g_stats.counts[FrameStats::Count::PipelineApplies];
    if (pipeline.pipeline != g_lastPipeline) ++g_stats.counts[FrameStats::Count::PipelineChanges];
    g_lastPipeline = pipeline.pipeline;
}

void CountBindings(const gpu::Texture& first) {
    ++g_stats.counts[FrameStats::Count::Bindings];
    if (first.texture != g_lastTexture) ++g_stats.counts[FrameStats::Count::TextureChanges];
    g_lastTexture = first.texture;
}

void CountDraw(std::size_t uniformBytes) {
    ++g_stats.counts[FrameStats::Count::Draws];
    g_stats.counts[FrameStats::Count::UniformBytes] += uniformBytes;
}

void CountReuse([[maybe_unused]] bool terrain, [[maybe_unused]] const void* key, [[maybe_unused]] int page,
                [[maybe_unused]] const Vertex* vertices, [[maybe_unused]] std::size_t count) {
#if defined(EI_RENDER_VALIDATION)
    Timer timer(FrameStats::Time::StatsMs);
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
    FrameStats::Reuse r = last == g_lastHashes.end()            ? FrameStats::Reuse::New
                          : last->second.geometry != h.geometry ? FrameStats::Reuse::Moved
                          : last->second.colours != h.colours   ? FrameStats::Reuse::Relit
                                                                : FrameStats::Reuse::Same;
    auto& reuse = terrain ? g_stats.terrainReuse : g_stats.meshReuse;
    ++reuse[r];
    g_hashes[id] = h;
    if (!terrain && !g_frameGeometry.insert(h.geometry ^ count).second) ++g_stats.counts[FrameStats::Count::SharedMeshes];
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
    g_stats.counts[FrameStats::Count::Matrices] = matrices;
    if (width > 0) {
        g_statsSum.width = width;
        g_statsSum.height = height;
    }
    FrameStats& s = g_statsSum;
    const FrameStats& f = g_stats;
    s.frames += 1;
    for (std::size_t k = 0; k < s.counts.size(); ++k) s.counts[k] += f.counts[k];
    for (std::size_t k = 0; k < s.ms.size(); ++k) s.ms[k] += f.ms[k];
    for (std::size_t k = 0; k < s.uploads.size(); ++k) {
        s.uploads[k] += f.uploads[k];
        s.uploadBytes[k] += f.uploadBytes[k];
    }
    for (std::size_t k = 0; k < s.terrainReuse.size(); ++k) {
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

void BeginDeviceMeshTime() { g_deviceMeshTimer.Begin(FrameStats::Time::DeviceMeshMs); }

void EndDeviceMeshTime() { g_deviceMeshTimer.End(); }

void BeginDecalTime() {
    if (g_decalTimer.Running()) return;
    g_decalTimer.Begin(FrameStats::Time::DecalPrintMs);
    if (g_decalTimer.Running()) ++g_stats.counts[FrameStats::Count::DecalPrints];
}

void EndDecalTime(int vertices) {
    if (!g_decalTimer.Running()) return;
    g_decalTimer.End();
    g_stats.counts[FrameStats::Count::DecalVertices] += static_cast<std::uint64_t>(vertices);
}

void BeginFlatDrawTime(int vertices) {
    if (g_flatDrawTimer.Running()) return;
    g_flatDrawTimer.Begin(FrameStats::Time::FlatMs);
    if (!g_flatDrawTimer.Running()) return;
    ++g_stats.counts[FrameStats::Count::FlatDraws];
    g_stats.counts[FrameStats::Count::FlatDrawVertices] += static_cast<std::uint64_t>(vertices);
}

void EndFlatDrawTime() { g_flatDrawTimer.End(); }

void BeginWeatherTime() { g_weatherTimer.Begin(FrameStats::Time::WeatherMs); }

void EndWeatherTime(int vertices) {
    if (!g_weatherTimer.Running()) return;
    g_weatherTimer.End();
    g_stats.counts[FrameStats::Count::WeatherVertices] += static_cast<std::uint64_t>(vertices);
}

void BeginShadowBuildTime() {
    if (g_shadowBuildTimer.Running()) return;
    g_shadowBuildTimer.Begin(FrameStats::Time::ShadowBuildMs);
    if (g_shadowBuildTimer.Running()) ++g_stats.counts[FrameStats::Count::ShadowBuilds];
}

void EndShadowBuildTime() { g_shadowBuildTimer.End(); }

#if defined(EI_RENDER_VALIDATION)
static long g_uploadChecks = 0, g_uploadMismatches = 0;
static long g_uploads = 0;   // movie uploads so far (the read-back is sampled: ReadBackDue)

void CheckMovieUpload(const gpu::Texture& image, const std::uint8_t* rgba, int w, int h) {
    if (!ReadBackDue(g_uploads++)) return;
    gpu::Ticket copy = gpu::Download(image, 0, Rect{0, 0, w, h});
    gpu::Texels texels;
    if (gpu::Wait(copy) && gpu::Read(copy, &texels)) {
        bool same = true;
        for (int y = 0; y < h && same; ++y)
            same = std::memcmp(texels.Row(y), rgba + static_cast<std::size_t>(y) * w * 4,
                               static_cast<std::size_t>(w) * 4) == 0;
        ++g_uploadChecks;
        if (!same)
            LogFirst(g_uploadMismatches, 10,
                     "render: MISMATCH: movie image %dx%d read back from the GPU differs from the pixels written", w,
                     h);
    }
    gpu::Release(copy);
    if (g_uploadChecks % 20 == 1)
        ei::Log("render: image uploads checked back from the GPU: %ld of %ld, mismatches %ld", g_uploadChecks, g_uploads, g_uploadMismatches);
}
#endif

// The next level of an RGBA8 level `src` of `size` as a 2x2 box filter (sizes halved, at least 1).
struct HalvedLevel {
    std::vector<std::uint8_t> texels;
    Size size;
};
static HalvedLevel HalveLevel(const std::vector<std::uint8_t>& src, Size size) {
    int w = size.w, h = size.h;
    int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            int x0 = x * 2, y0 = y * 2, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
            for (int c = 0; c < 4; ++c) {
                int sum = src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] + src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                          src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] + src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                dst[(static_cast<std::size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return {std::move(dst), {nw, nh}};
}

MmpLevelCheck CheckMmpLevels() {
    MmpLevelCheck c;
    for (const HeldMmp& t : HeldMmpTextures()) {
        // The levels as the GPU reads them, down to 4 x 4 (DXT levels under 4 x 4 are only partly in the file).
        const int width = t.width, height = t.height;
        std::vector<std::vector<std::uint8_t>> levels;
        int w = width, hh = height;
        bool all = true;
        for (int l = 0; l < t.levels && w >= 4 && hh >= 4 && all; ++l) {
            levels.emplace_back();
            all = ReadImageLevel(t.image, l, w, hh, &levels.back());
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
        }
        if (!all || levels.empty()) continue;
        ++c.textures;
        // Each level and level 0 box-filtered down to 4 x 4 (or less): the file's own filtering of
        // a level (1-bit alpha kept 0 or 1, sharpened colours) changes its texels, not those averages.
        auto summary = [](std::vector<std::uint8_t> level, int w, int hh) {
            Size size{w, hh};
            while (size.w > 4 || size.h > 4) {
                HalvedLevel next = HalveLevel(level, size);
                level = std::move(next.texels);
                size = next.size;
            }
            return level;
        };
        const std::vector<std::uint8_t> reference = summary(levels[0], width, height);
        w = width;
        hh = height;
        for (std::size_t l = 1; l < levels.size(); ++l) {
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
            const std::vector<std::uint8_t> mine = summary(levels[l], w, hh);
            std::uint32_t sum = 0;
            for (std::size_t i = 0; i < reference.size(); ++i)
                sum += static_cast<std::uint32_t>(reference[i] > mine[i] ? reference[i] - mine[i] : mine[i] - reference[i]);
            double difference = static_cast<double>(sum) / static_cast<double>(reference.size());
            ++c.levels;
            if (difference > c.worst) {
                c.worst = difference;
                c.worstWidth = width;
                c.worstHeight = height;
                c.worstLevel = static_cast<int>(l);
                c.worstFormat = t.format;
            }
        }
    }
    return c;
}

std::uint64_t perf::FrameDrawCount() { return g_stats.counts[FrameStats::Count::Draws] + g_stats.counts[FrameStats::Count::ShadowDraws]; }

} // namespace render
