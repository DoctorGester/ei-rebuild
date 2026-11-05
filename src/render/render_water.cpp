// The water's small ripples (the map the water's look samples, shaders/terrain.hlsl's Water, and its two layers'
// motion) and its reflections (the sky dome mirrored in the water's plane, the frame before the water's first draw).
// See docs/notes/renderer.md, "The renderer's source files".
#include "render_internal.h"

#include "lib/maths.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace render {

// The map: kRippleSize texels a side, repeating, with its levels down to 1 x 1.
constexpr int kRippleSize = 256;
constexpr int kRippleLevels = 9;
static_assert(1 << (kRippleLevels - 1) == kRippleSize);
constexpr float kTwoPi = 6.28318531f;

static gpu::Texture g_ripples;

// The map is the same on every run and draws nothing from the game's generators.
struct MapRandom {
    std::uint32_t state = 0x2545F491u;
    float Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

// A wave of the map: whole cycles across it in x and y (so the map repeats without a seam), its height and phase.
struct Wave {
    int kx = 0, ky = 0;
    float amplitude = 0.0f, cosPhase = 0.0f, sinPhase = 0.0f;
};

// `count` waves in random directions, from `longest` to `shortest` cycles across the map, the heights falling as
// the cycles to the power `falloff`.
static std::vector<Wave> Waves(MapRandom& random, int count, float longest, float shortest, float falloff) {
    std::vector<Wave> waves;
    while (static_cast<int>(waves.size()) < count) {
        const float angle = random.Next() * kTwoPi;
        const float cycles = longest + (shortest - longest) * random.Next();
        const float phase = random.Next() * kTwoPi;
        const int kx = static_cast<int>(std::lround(cycles * lib::Cos(angle)));
        const int ky = static_cast<int>(std::lround(cycles * lib::Sin(angle)));
        if (kx == 0 && ky == 0) continue;
        const float length = lib::Hypot(static_cast<float>(kx), static_cast<float>(ky));
        waves.push_back(Wave{kx, ky, lib::Pow(length, -falloff), lib::Cos(phase), lib::Sin(phase)});
    }
    return waves;
}

static std::uint8_t Signed(float v) { return static_cast<std::uint8_t>(std::lround(127.5f + 127.0f * v)); }
static std::uint8_t Unsigned(float v) { return static_cast<std::uint8_t>(std::lround(255.0f * v)); }

// Level 0: r, g the slopes of a sum of waves along x and y (-1 .. 1 as 0 .. 255, the steepest at the ends), b the
// foam's pattern (a finer sum's height, 0 .. 1), a 255.
static std::vector<std::uint8_t> FirstLevel() {
    MapRandom random;
    const std::vector<Wave> surface = Waves(random, 40, 3.0f, 22.0f, 1.5f);
    const std::vector<Wave> foam = Waves(random, 24, 10.0f, 40.0f, 0.8f);
    // A wave's angle at a texel is a whole number of kRippleSize-th turns: its cosine and sine from these.
    std::vector<float> cosines(kRippleSize), sines(kRippleSize);
    for (int k = 0; k < kRippleSize; ++k) {
        cosines[k] = lib::Cos(kTwoPi * static_cast<float>(k) / kRippleSize);
        sines[k] = lib::Sin(kTwoPi * static_cast<float>(k) / kRippleSize);
    }
    constexpr int kMask = kRippleSize - 1;
    const std::size_t texels = static_cast<std::size_t>(kRippleSize) * kRippleSize;
    std::vector<float> sx(texels), sy(texels), pattern(texels);
    for (const Wave& w : surface)
        for (int y = 0; y < kRippleSize; ++y) {
            const int rowTurns = w.ky * y;
            for (int x = 0; x < kRippleSize; ++x) {
                const int i = (rowTurns + w.kx * x) & kMask;
                const float c = cosines[i] * w.cosPhase - sines[i] * w.sinPhase;   // cos(the angle + the phase)
                const std::size_t at = static_cast<std::size_t>(y) * kRippleSize + x;
                sx[at] += w.amplitude * static_cast<float>(w.kx) * c;
                sy[at] += w.amplitude * static_cast<float>(w.ky) * c;
            }
        }
    for (const Wave& w : foam)
        for (int y = 0; y < kRippleSize; ++y) {
            const int rowTurns = w.ky * y;
            for (int x = 0; x < kRippleSize; ++x) {
                const int i = (rowTurns + w.kx * x) & kMask;
                pattern[static_cast<std::size_t>(y) * kRippleSize + x] +=
                    w.amplitude * (sines[i] * w.cosPhase + cosines[i] * w.sinPhase);   // sin(the angle + the phase)
            }
        }
    float steepest = 1e-9f, low = 1e30f, high = -1e30f;
    for (std::size_t at = 0; at < texels; ++at) {
        steepest = std::fmax(steepest, std::fmax(std::fabs(sx[at]), std::fabs(sy[at])));
        low = std::fmin(low, pattern[at]);
        high = std::fmax(high, pattern[at]);
    }
    std::vector<std::uint8_t> level(texels * 4);
    for (std::size_t at = 0; at < texels; ++at) {
        level[4 * at] = Signed(sx[at] / steepest);
        level[4 * at + 1] = Signed(sy[at] / steepest);
        level[4 * at + 2] = Unsigned((pattern[at] - low) / (high - low + 1e-9f));
        level[4 * at + 3] = 255;
    }
    return level;
}

// The level after `level` (`size` texels a side): each texel the mean of four.
static std::vector<std::uint8_t> NextLevel(const std::vector<std::uint8_t>& level, int size) {
    const int half = size / 2;
    std::vector<std::uint8_t> next(static_cast<std::size_t>(half) * half * 4);
    for (int y = 0; y < half; ++y)
        for (int x = 0; x < half; ++x)
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int dx, int dy) {
                    return level[(static_cast<std::size_t>(2 * y + dy) * size + 2 * x + dx) * 4 + c];
                };
                next[(static_cast<std::size_t>(y) * half + x) * 4 + c] =
                    static_cast<std::uint8_t>((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
            }
    return next;
}

bool MakeRippleMap() {
    if (g_ripples) return true;
    g_ripples = gpu::MakeTexture({kRippleSize, kRippleSize, kRippleLevels, 1, gpu::Format::Rgba8, gpu::kSampled,
                                  "ei-water-ripples"});
    if (!g_ripples) return false;
    std::vector<std::uint8_t> level = FirstLevel();
    for (int k = 0, size = kRippleSize; k < kRippleLevels; ++k, size /= 2) {
        if (k > 0) level = NextLevel(level, size * 2);
        if (!gpu::Write(g_ripples, k, {0, 0, size, size}, level.data(), static_cast<std::size_t>(size) * 4)) {
            gpu::Release(g_ripples);
            return false;
        }
    }
    return true;
}

gpu::TextureBinding RippleMap() {
    gpu::SamplerDesc d;
    d.mip = gpu::Filter::Linear;
    d.anisotropy = 8;
    return gpu::TextureBinding{g_ripples, gpu::MakeSampler(d)};
}

// The layers drift apart in two directions, in map widths per client tick (about 55 ms): A about 0.27 landscape
// units a second at its scale, B about 0.2 (terrain.hlsl's kRippleScaleA / B); a swamp's a fifth as fast.
void RippleOffsets(float water[4], float swamp[4]) {
    constexpr double kDrift[4] = {0.0016, 0.0010, -0.0012, 0.0019};
    constexpr double kSwampDrift = 0.2;
    const double ticks = KeptSceneLight().clientTicks;
    for (int k = 0; k < 4; ++k) {
        const double moved = ticks * kDrift[k], slow = moved * kSwampDrift;
        water[k] = static_cast<float>(moved - std::floor(moved));
        swamp[k] = static_cast<float>(slow - std::floor(slow));
    }
}

// --- The reflections ---------------------------------------------------------------------------------

// The sky image: the dome drawn mirrored in the water's plane at a kSkyScale-th of the frame's size (the dome is
// smooth), over kSkyMargin times the frame's extent (a ripple turns a ray past the frame's edge).
constexpr int kSkyScale = 4;
constexpr float kSkyMargin = 1.25f;

static gpu::Texture g_skyImage, g_skyDepth;
// The frame before the water's first draw at half its size (CopyFrameForWater, shaders/scene_copy.hlsl).
static gpu::Texture g_sceneColour, g_sceneDepth;
static gpu::Pipeline g_copyPipeline;
static bool g_copyTried = false;
static int g_copyFailures = 0;
static Matrix4 g_skyView{};   // the landscape's space to the sky image's clip space
// The dome as the shader takes it: its centre in the landscape's space, its radius across and its height above the
// centre. Without a dome, far around the camera (the sky image is then the fog's colour alone).
static Vector3 g_domeCentre{};
static float g_domeRadius = 1000.0f, g_domeHeight = 1000.0f;
static std::uint32_t g_copyFrame = ~0u;   // the frame (FrameNumber) CopyFrameForWater last copied

static gpu::PipelineDesc CopyDesc() {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::scene_copy::kIndex;
    p.colour = kColourFormat;
    p.second = kDepthCopyFormat;
    return p;
}

void ListWaterPipelines(std::vector<gpu::PipelineDesc>& out) { out.push_back(CopyDesc()); }

// `t` remade at `width` x `height` (of `format`, for `use`) when it has another size.
static bool Ensure(gpu::Texture& t, int width, int height, gpu::Format format, std::uint8_t use, const char* label) {
    if (t && t.width == width && t.height == height) return true;
    gpu::Release(t);
    t = gpu::MakeTexture({width, height, 1, 1, format, use, label});
    return static_cast<bool>(t);
}

// The dome's extent around `centre` from the corners of `batch` placed by `model` (its model space to the landscape's).
static void ExtendDome(const Frame& f, const MeshBatch& batch, const Matrix4& model, const Vector3& centre) {
    auto extend = [&](const Vector3& corner) {
        const Vector3 q = TransformColumn(model, corner) - centre;
        g_domeRadius = std::max(g_domeRadius, lib::Hypot(q.x, q.y));
        g_domeHeight = std::max(g_domeHeight, q.z);
    };
    for (int k = 0; k < batch.indexCount; ++k) {
        if (batch.cached) {
            extend(CachedCorner(*batch.cached, batch.firstIndex + k));
        } else {
            const auto at = static_cast<std::size_t>(batch.firstIndex + k);
            if (at < f.meshIndices.size() && f.meshIndices[at] < f.meshVertices.size())
                extend(f.meshVertices[f.meshIndices[at]].position);
        }
    }
}

void DrawSkyReflection(Frame& f) {
    const std::optional<WorldView> view = WorldCamera(f);
    const std::optional<Size> size = FrameSize();
    if (!view || !size || f.worldRanges.empty()) return;
    const int width = std::max(1, size->w / kSkyScale), height = std::max(1, size->h / kSkyScale);
    if (!Ensure(g_skyImage, width, height, kColourFormat, gpu::kColourTarget | gpu::kSampled, "ei-water-sky") ||
        !Ensure(g_skyDepth, width, height, kDepthFormat, gpu::kDepthTarget, "ei-water-sky-depth"))
        Fatal("Can't draw the frame: the water's sky image could not be made.");
    // The mirror's plane: the mean level of the water drawn (the image is read where it shows a point of the dome, so
    // water at another level reads it right too; the plane only keeps the dome in view).
    double level = 0.0;
    int pieces = 0;
    for (const TerrainBatch& b : f.terrainBatches) {
        if (!b.water) continue;
        for (int k = b.firstPiece; k < b.firstPiece + b.pieceCount; ++k) {
            const Box& box = TerrainPieceWhole(f.terrainPieces[static_cast<std::size_t>(k)]).box;
            level += 0.5 * (box.low.z + box.high.z);
            ++pieces;
        }
    }
    const float h = pieces > 0 ? static_cast<float>(level / pieces) : 0.0f;
    const Matrix4 mirror{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 2.0f * h, 0, 0, 0, 1}};
    const float s = 1.0f / kSkyMargin;
    const Matrix4 margin{{s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    g_skyView = Product(margin, Product(view->clip, mirror));
    g_domeCentre = view->eye;
    g_domeRadius = g_domeHeight = 1000.0f;

    gpu::PassDesc pass;
    pass.colour.texture = g_skyImage;
    pass.colour.load = gpu::Load::Clear;
    UnpackColour(f.fogColour, pass.colour.clear);
    pass.colour.clear[3] = 1.0f;
    pass.depth = {g_skyDepth, 0, gpu::Load::Clear, 1.0f, false};
    pass.label = "ei-water-sky";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the water's sky pass could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Passes];
    // The dome: the meshes the world draws before its first ground (WorldClient::Render draws the sky first), mirrored,
    // so their faces turn the other way; the image keeps no depth.
    const Frame::Range& world = f.worldRanges.front();
    bool first = true;
    for (std::size_t k = world.first; k < std::min(world.end, f.commands.size()); ++k) {
        const Command& c = f.commands[k];
        if (c.kind == Command::Terrain) break;
        if (c.kind != Command::Mesh) continue;
        MeshBatch batch = f.meshBatches[static_cast<std::size_t>(c.index)];
        if (!batch.model) continue;
        const Matrix4 model = view->inverse.Under(f.matrices[static_cast<std::size_t>(batch.matrix)]);
        if (first) {
            g_domeCentre = TransformColumn(model, Vector3{});
            g_domeRadius = g_domeHeight = 0.0f;
            first = false;
        }
        ExtendDome(f, batch, model, g_domeCentre);
        f.matrices.push_back(Product(g_skyView, model));
        batch.matrix = static_cast<int>(f.matrices.size()) - 1;
        batch.state.cull = batch.state.cull == Cull::Clockwise          ? Cull::CounterClockwise
                           : batch.state.cull == Cull::CounterClockwise ? Cull::Clockwise
                                                                        : Cull::None;
        batch.state.depthTest = batch.state.depthWrite = false;
        batch.unclipped = true;
        DrawMeshBatch(f, batch, 1);
    }
    gpu::EndPass();
    g_domeRadius = std::max(g_domeRadius, 1.0f);
    g_domeHeight = std::max(g_domeHeight, 1.0f);
}

void CopyFrameForWater(const gpu::Texture& colour, const gpu::Texture& depth) {
    if (!g_copyTried) {
        g_copyTried = true;
        g_copyPipeline = gpu::MakePipeline(CopyDesc(), "ei-water-scene-copy");
    }
    if (!g_copyPipeline) {
        LogFirst(g_copyFailures, 1, "render: the water's frame copy pipeline could not be made: no reflections of the frame");
        return;
    }
    const int width = (colour.width + 1) / 2, height = (colour.height + 1) / 2;
    if (!Ensure(g_sceneColour, width, height, kColourFormat, gpu::kColourTarget | gpu::kSampled,
                "ei-water-scene") ||
        !Ensure(g_sceneDepth, width, height, kDepthCopyFormat, gpu::kColourTarget | gpu::kSampled,
                "ei-water-scene-depth"))
        Fatal("Can't draw the frame: the water's copy of the frame could not be made.");
    gpu::PassDesc pass;
    pass.colour.texture = g_sceneColour;
    pass.colour.load = gpu::Load::DontCare;
    pass.second.texture = g_sceneDepth;
    pass.second.load = gpu::Load::DontCare;
    pass.label = "ei-water-scene-copy";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the water's frame copy could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Passes];
    gpu::ApplyPipeline(g_copyPipeline);
    const gpu::TextureBinding sources[2] = {{colour, gpu::NearestClampSampler()}, {depth, gpu::NearestClampSampler()}};
    static_assert(gpu_shaders::scene_copy::kSamplerColour == 0 && gpu_shaders::scene_copy::kSamplerDepth == 1);
    gpu::BindTextures(gpu::Stage::Fragment, 0, sources);
    gpu::Draw(0, 3);
    gpu::EndPass();
    g_copyFrame = FrameNumber();
}

bool WaterCopyTaken() { return g_copyFrame == FrameNumber(); }

void BindWaterReflections(const Matrix4& view, TerrainFsParams* fs) {
    namespace program = gpu_shaders::terrain;
    fs->view = view;
    fs->skyView = g_skyView;
    fs->dome[0] = g_domeCentre.x;
    fs->dome[1] = g_domeCentre.y;
    fs->dome[2] = g_domeCentre.z;
    fs->domeRadius = g_domeRadius;
    fs->domeHeight = g_domeHeight;
    float depthMap[2];
    DepthMap(view, depthMap);
    fs->sceneDepthOffset = depthMap[0];
    fs->sceneDepthScale = depthMap[1];
    const bool copied = WaterCopyTaken() && g_sceneColour;
    fs->sceneOn = copied ? 1 : 0;
    fs->sceneSize[0] = static_cast<float>(g_sceneColour.width);
    fs->sceneSize[1] = static_cast<float>(g_sceneColour.height);
    gpu::SamplerDesc clamped;
    clamped.u = clamped.v = gpu::Wrap::Clamp;
    const gpu::Sampler linear = gpu::MakeSampler(clamped);
    const gpu::TextureBinding textures[3] = {
        {g_skyImage ? g_skyImage : WhiteView(), linear},
        {copied ? g_sceneColour : WhiteView(), linear},
        {copied ? g_sceneDepth : WhiteView(), gpu::NearestClampSampler()}};
    static_assert(program::kSamplerSceneColour == program::kSamplerSkyImage + 1 &&
                  program::kSamplerSceneDepth == program::kSamplerSkyImage + 2);
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerSkyImage, textures);
}

}   // namespace render
