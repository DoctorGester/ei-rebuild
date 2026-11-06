// Object shadows through a sun shadow map. See docs/notes/renderer.md, "The renderer's source files".
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"
#include "render_perf.h"

#include "lib/maths.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace render {

// The map: kMapSize texels a side over the visible ground up to kShadowReach along the view (its larger side in the
// landscape's x and y along the light onto a level plane, in steps of kExtentStep world units so that the texel size
// changes seldom), the texels' corners on whole texels of the landscape at the plane's height (shadows neither swim
// while the camera moves nor crawl while the sun turns). Along the light it reaches from the nearest caster towards
// the sun (every caster whose bounds are in the map's area seen from the sun and in front of the ground's far side;
// none is too high) to kDepthMargin behind the ground.
constexpr int kMapSize = 4096;
constexpr float kExtentStep = 8.0f;
constexpr float kPlaneStep = 8.0f;   // the height at which the map's texels stay on the ground (FitMap)
constexpr float kDepthMargin = 2.0f;
// The view has no far plane (renderer.md, "The view's depth"): the map covers the ground up to kShadowReach world
// units along the view (the old far plane's distance, so the map's texels are as fine as they were), and its shadows
// fade out over the last kShadowFade of that distance from the camera, so no line marks where they end.
constexpr float kShadowReach = 100.0f;
constexpr float kShadowFade = 20.0f;
// The sun at least this far above the horizon (the old shadows' 20 degrees).
constexpr float kLeastElevation = 20.0f * kPi / 180.0f;
// How dark a fully shadowed pixel gets (the old decal: alpha 0x80 of black), the receivers' constant
// depth bias in world units, the largest depth slope taken per world unit across.
constexpr float kStrength = 0.5f;
constexpr float kDepthBias = 0.04f;
constexpr float kLargestSlope = 4.0f;
// Soft edges: the filter's radius in world units is the distance along the light from the
// receiver to the blocker times kSunSize (the tangent of the sun's chosen angular radius: about twice
// the real sun's, for a soft edge like the old blurred decals'), at least kLeastRadius texels, at most
// kWidestRadius units (also the blocker search's radius). A tree's crown stands far above the ground:
// a larger sun blurred its cut-out leaves into one dark mass.
constexpr float kSunSize = 0.01f;
constexpr float kWidestRadius = 0.6f;
constexpr float kLeastRadius = 1.5f;
// The cells of the map's depth range (shaders/shadow_reduce.hlsl, the receivers' early end): kRangeCell texels a
// side, so kRangeSize cells a side; the dilation reaches at most kMostCellsAround cells.
constexpr int kRangeCell = 16;
constexpr int kRangeSize = kMapSize / kRangeCell;
constexpr int kMostCellsAround = 8;
static_assert(kMapSize == 4096 && kRangeCell == 16,
              "the constants written in shaders/shadow_reduce.hlsl and shadow_receiver.hlsli");

// --- Matrices in double ----------------------------------------------------------------------------

// The camera's clip matrix inverted and multiplied back into the game's clip matrices (FitMap, CameraInverse,
// PrepareShadows' light matrices): their product nearly cancels, so the inverse and the products through it
// are double (a float inverse moves still objects by the camera's rounding, render_point_shadows.cpp's
// kKeptPlace); everything else is float.
struct M4 {
    double m[16] = {};
};

static M4 FromMatrix(const Matrix4& a) {
    M4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = a.m[i];
    return r;
}

static Matrix4 ToMatrix(const M4& a) {
    Matrix4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = static_cast<float>(a.m[i]);
    return r;
}

static M4 Multiply(const M4& a, const M4& b) {
    M4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

// The camera's position: the point clip space puts at w = 0 (x = y = 0), the inverse's third column. False
// when it lies at infinity.
static bool CameraEye(const M4& fromClip, Vector3* eye) {
    const double* r = fromClip.m;
    const double w = r[14];
    if (std::fabs(w) <= 1e-12) return false;
    *eye = Vector3{static_cast<float>(r[2] / w), static_cast<float>(r[6] / w), static_cast<float>(r[10] / w)};
    return true;
}

// Gauss-Jordan with partial pivoting; false when singular.
static bool Invert(const M4& a, M4* out) {
    double w[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            w[i][j] = a.m[i * 4 + j];
            w[i][4 + j] = i == j ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(w[r][c]) > std::fabs(w[pivot][c])) pivot = r;
        if (std::fabs(w[pivot][c]) < 1e-30) return false;
        if (pivot != c)
            for (int j = 0; j < 8; ++j) std::swap(w[c][j], w[pivot][j]);
        double inverse = 1.0 / w[c][c];
        for (int j = 0; j < 8; ++j) w[c][j] *= inverse;
        for (int r = 0; r < 4; ++r) {
            if (r == c || w[r][c] == 0.0) continue;
            double f = w[r][c];
            for (int j = 0; j < 8; ++j) w[r][j] -= f * w[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out->m[i * 4 + j] = w[i][4 + j];
    return true;
}

// --- The game's hand-over ---------------------------------------------------------------------------

static const Object3D* g_caster = nullptr;   // BeginCaster .. EndCaster
static int g_casterParts = 0;                // ... the parts noted as drawn (NoteCasterPart)
static std::uint32_t g_casterFrame = 1;      // CasterFrame: one more at each ShadowFrameDone
static bool g_casterFigure = false;      // ... a unit's draw
static ShadowInfo g_info, g_lastInfo;

// --- Casters outside the view --------------------------------------------------------------------

// A mesh of ShadowCasterMesh: its model matrix into the landscape's space, its corners kept on the GPU by
// part (KeepCasterMesh) and its texture offsets.
struct OutsideCaster {
    Matrix4 model;
    const CachedMesh* mesh = nullptr;
    float uOffset = 0.0f, vOffset = 0.0f;
    gpu::Texture texture;
    bool hasTexture = false;
    const Object3D* object = nullptr;
    bool unit = false;
};
static std::vector<OutsideCaster> g_outside;
// More casters than this in one frame (no present for a long time) are dropped.
constexpr std::size_t kMostOutside = 100'000;
// NoteCasterPart's parts this frame, sorted at the first CasterPartDrawn after a note (a list kept from frame to frame).
static std::vector<const FigureMesh*> g_drawnParts;
static bool g_drawnPartsSorted = true;
static bool g_outsideOn = true;                 // SetOffscreenCasters: not View
static bool g_walkedOnly = false;               // SetOffscreenCasters: Walked
static bool g_shadowView = false;               // SetShadowView
static int g_shadowShift = 0;                   // SetShadowShift: texels
// How the meshes drawn on screen with a texture cut it out (alpha test or blending, and the least alpha
// kept), by the texture: a caster outside the view with that texture cuts it out the same way;
// one whose texture was not drawn yet cuts below one half (a texture with alpha is drawn alpha-tested
// or blended; without alpha nothing is cut).
struct AlphaCut {
    bool cut = false;
    float least = 0.0f;
};
static std::unordered_map<const void*, AlphaCut> g_cuts;
constexpr std::size_t kMostCuts = 8192;
// The nearest depth along the light of a caster that reaches the map this frame (ShadowCasterReaches):
// the map's near plane. Casters the game draws outside the walked objects' area are kept in front of
// it by the caster shader (depths before the near plane drawn at it).
static float g_nearestCaster = 1e30f;

// --- GPU side ---------------------------------------------------------------------------------------

// The casters' pipelines (CasterDraws): MeshLayout's position and texture coordinates in vertex buffer 0, the
// draw's CasterInstance in kInstanceBuffer as kInstanceAttrs float4s (shaders/shadow_caster.hlsl's row0 ..
// cut). A pipeline's buffer slots run from 0 without gaps (SDL_GPU's Direct3D 12 backend reads them by number),
// so the instances take slot 1, where a kept mesh's colours would be.
constexpr int kInstanceBuffer = 1;
constexpr int kInstanceAttrs = 5;
static_assert(sizeof(CasterInstance) == 16 * kInstanceAttrs);
static CasterDraws g_casterDraws("ei-shadow-casters");   // the sun map's
static gpu::Pipeline g_casterPipelines[2];               // the frame's stream, the meshes kept on the GPU (MeshLayout)
static gpu::Pipeline g_terrainCasterPipeline;            // the ground's casters (shaders/shadow_terrain.hlsl)
static gpu::Pipeline g_reducePipeline, g_dilatePipeline;
static bool g_pipelinesTried = false;
static gpu::Texture g_map, g_dummy;
static bool g_dummyCleared = false;
static gpu::Texture g_cells, g_range;
static gpu::Texture
    g_rangeDummy;   // the range's 1 x 1 stand-in without a map (the receivers' read of the range needs a texture bound)

// A depth-only pipeline into the shadow maps (kDepthFormat, nearer or equal kept, the slope's bias 1.5).
static void DepthOnly(gpu::PipelineDesc& p) {
    p.colour = gpu::Format::None;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::LessEqual;
    p.depthWrite = true;
    p.depthBiasSlope = 1.5f;
}

static gpu::PipelineDesc CasterDesc(bool cached) {
    namespace program = gpu_shaders::shadow_caster;
    gpu::PipelineDesc p;
    p.program = program::kIndex;
    MeshLayout(p, cached, true);
    p.bufferCount = kInstanceBuffer + 1;
    p.buffers[kInstanceBuffer] = {static_cast<std::uint16_t>(sizeof(CasterInstance)), true};
    for (int k = 0; k < kInstanceAttrs; ++k)   // the light matrix's rows, then the offsets and the cut
        p.attributes[p.attributeCount++] = {static_cast<std::uint8_t>(program::kAttrRow0 + k), kInstanceBuffer,
                                            gpu::VertexFormat::Float4, static_cast<std::uint16_t>(16 * k)};
    DepthOnly(p);
    return p;
}

// The ground's casters: TerrainCasterVertex (position, normal), the light matrix in the vertex uniforms, the sun's
// direction in the fragment ones.
static gpu::PipelineDesc TerrainCasterDesc() {
    namespace program = gpu_shaders::shadow_terrain;
    gpu::PipelineDesc p;
    p.program = program::kIndex;
    p.bufferCount = 1;
    p.buffers[0].stride = sizeof(TerrainCasterVertex);
    p.attributeCount = 2;
    p.attributes[0] = {program::kAttrPos, 0, gpu::VertexFormat::Float3, offsetof(TerrainCasterVertex, position)};
    p.attributes[1] = {program::kAttrNormal, 0, gpu::VertexFormat::Float3, offsetof(TerrainCasterVertex, normal)};
    DepthOnly(p);
    return p;
}

// A full-screen pass into an RGBA32F texture (it reads its texture 0 with the point sampler).
static gpu::PipelineDesc RangeDesc(int program) {
    gpu::PipelineDesc p;
    p.program = program;
    p.colour = gpu::Format::Rgba32Float;
    return p;
}

// The pipelines of the casters and the range passes, made at the first map (and tried once).
static bool MakeShadowPipelines() {
    if (!g_pipelinesTried) {
        g_pipelinesTried = true;
        g_casterPipelines[0] = gpu::MakePipeline(CasterDesc(false), "ei-shadow-caster");
        g_casterPipelines[1] = gpu::MakePipeline(CasterDesc(true), "ei-shadow-caster");
        g_terrainCasterPipeline = gpu::MakePipeline(TerrainCasterDesc(), "ei-shadow-terrain");
        g_reducePipeline = gpu::MakePipeline(RangeDesc(gpu_shaders::shadow_reduce::kIndex), "ei-shadow-cells");
        g_dilatePipeline = gpu::MakePipeline(RangeDesc(gpu_shaders::shadow_dilate::kIndex), "ei-shadow-range");
        if (!g_casterPipelines[0] || !g_casterPipelines[1] || !g_terrainCasterPipeline || !g_reducePipeline ||
            !g_dilatePipeline)
            ei::Log("render: the shadow map's pipelines could not be created (see the gpu messages above)");
    }
    return g_casterPipelines[0] && g_casterPipelines[1] && g_terrainCasterPipeline && g_reducePipeline &&
           g_dilatePipeline;
}

static gpu::Texture MakeRange(int size, const char* label) {
    gpu::Texture t = gpu::MakeTexture({size, size, 1, 1, gpu::Format::Rgba32Float, gpu::TextureUse::Sampled | gpu::TextureUse::ColourTarget, label});
    if (!t) ei::Log("render: the %dx%d shadow texture %s could not be made", size, size, label);
    return t;
}

static void RangePass(gpu::Pipeline pipeline, const gpu::Texture& target, const gpu::Texture& source,
                      const ShadowDilateParams* dilate, const char* label) {
    static_assert(gpu_shaders::shadow_reduce::kSamplerMap == 0 && gpu_shaders::shadow_dilate::kSamplerCells == 0);
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = gpu::Load::DontCare;
    pass.label = label;
    if (!gpu::BeginPass(pass)) return;
    ++g_stats.counts[FrameStats::Count::Passes];
    gpu::ApplyPipeline(pipeline);
    const gpu::TextureBinding b{source, gpu::NearestClampSampler()};
    gpu::BindTextures(gpu::Stage::Fragment, 0, {&b, 1});
    if (dilate) gpu::PushUniforms(gpu::Stage::Fragment, gpu_shaders::shadow_dilate::kBlockShadowDilateParams, *dilate);
    gpu::Draw(0, 3);
    gpu::EndPass();
}

static void EnsureDummy() {
    if (!g_rangeDummy)
        g_rangeDummy = gpu::MakeTexture({1, 1, 1, 1, gpu::Format::Rgba32Float, gpu::TextureUse::Sampled, "ei-shadow-range-none"});
    if (!g_dummy) g_dummy = MakeShadowDepth(1, 1, "ei-shadow-none");
    if (!g_dummy) return;
    if (g_dummyCleared) return;
    gpu::PassDesc pass;
    pass.depth = {g_dummy, 0, gpu::Load::Clear, 1.0f, true};
    pass.label = "ei-shadow-none";
    if (!gpu::BeginPass(pass)) return;
    gpu::EndPass();
    g_dummyCleared = true;
}

// --- The map's fit ----------------------------------------------------------------------------------

// A half-space of the landscape's space: a x + b y + c z + d >= 0.
struct Plane {
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
    float Side(const Vector3& at) const { return a * at.x + b * at.y + c * at.z + d; }
};

// The camera's view as the fit takes it, in the landscape's space (clip space's rows combined): in front of
// the camera (w >= 0), its sides widened by 5% (|x|, |y| <= 1.05 w) and no farther along the view than
// kShadowReach (w <= kShadowReach times the w row's scale: w is the distance along the view times it).
constexpr int kViewPlanes = 6;
static void ViewPlanes(const Matrix4& camera, Plane planes[kViewPlanes]) {
    auto row = [&](int i) { return Plane{camera.m[i * 4], camera.m[i * 4 + 1], camera.m[i * 4 + 2], camera.m[i * 4 + 3]}; };
    auto sum = [](const Plane& p, float k, const Plane& q, float l) {
        return Plane{p.a * k + q.a * l, p.b * k + q.b * l, p.c * k + q.c * l, p.d * k + q.d * l};
    };
    const Plane x = row(0), y = row(1), z = row(2), w = row(3);
    planes[0] = w;
    planes[1] = sum(w, 1.05f, x, -1.0f);
    planes[2] = sum(w, 1.05f, x, 1.0f);
    planes[3] = sum(w, 1.05f, y, -1.0f);
    planes[4] = sum(w, 1.05f, y, 1.0f);
    const float scale = std::sqrt(w.a * w.a + w.b * w.b + w.c * w.c);
    planes[5] = Plane{-w.a, -w.b, -w.c, scale * kShadowReach - w.d};
}

// Of the view's planes in `asked` (bit k: plane k), those the box crosses; -1 when it is wholly outside
// one of them: its corners farthest out and farthest in along each plane's normal.
constexpr int kAllPlanes = (1 << kViewPlanes) - 1;
static int CrossedPlanes(const Box& box, const Plane planes[kViewPlanes], int asked) {
    const Vector3 &low = box.low, &high = box.high;
    int crossed = 0;
    for (int k = 0; k < kViewPlanes; ++k) {
        if (!(asked & (1 << k))) continue;
        const Plane& p = planes[k];
        const Vector3 in{p.a >= 0.0f ? high.x : low.x, p.b >= 0.0f ? high.y : low.y, p.c >= 0.0f ? high.z : low.z};
        const Vector3 out{p.a >= 0.0f ? low.x : high.x, p.b >= 0.0f ? low.y : high.y, p.c >= 0.0f ? low.z : high.z};
        if (p.Side(in) < 0.0f) return -1;
        if (p.Side(out) < 0.0f) crossed |= 1 << k;
    }
    return crossed;
}

// The map's view from the sun around the visible ground, made once a frame (FitMap): by PrepareShadows,
// or earlier by the world's draw for its casters outside the view (ShadowCasterArea); made again when
// more ground was drawn since.
struct Fit {
    std::uint32_t frame = 0xFFFFFFFFu;   // FrameNumber() it was made in
    std::size_t pieces = 0;              // the frame's terrain pieces then
    bool ok = false;
    M4 fromClip{};                       // the camera's clip space to the landscape's
    bool haveEye = false;                // the camera's position (none: at infinity)
    Vector3 eye{};
    Vector3 x{}, y{}, d{};               // the map's axes (FitMap): the ground's x and y, the light's direction (down)
    float x0 = 0.0f, y0 = 0.0f, extent = 0.0f;   // the map's area along x and y
    float groundNear = 0.0f, groundFar = 0.0f;   // the visible ground's depths along d
    Box ground;                                  // the visible ground's box in the landscape's space
    // PrepareShadows: the map's depth range (from the nearest caster) and landscape space to its clip space.
    float depth = 0.0f;
    Matrix4 light{};
};
static Fit g_fit;

static std::size_t FirstGround(const Frame& f) {
    for (const Frame::Range& r : f.worldRanges) {
        std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = r.first; k < end; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Kind::Terrain && !f.terrainBatches[static_cast<std::size_t>(c.index)].water) return k;
        }
    }
    return SIZE_MAX;
}

// Landscape space to the map's clip space: x, y -1..1 over the area, z 0..1 from `zNear` to the
// ground's far side.
static Matrix4 LightMatrix(const Fit& fit, float zNear, float* depth) {
    const float zFar = fit.groundFar + kDepthMargin;
    *depth = zFar - zNear;
    Matrix4 light{};
    const float sx = 2.0f / fit.extent, sz = 1.0f / *depth;
    const Vector3 rows[3] = {fit.x, fit.y, fit.d};
    const float scales[3] = {sx, sx, sz};
    for (int k = 0; k < 3; ++k) {
        light.m[k * 4] = rows[k].x * scales[k];
        light.m[k * 4 + 1] = rows[k].y * scales[k];
        light.m[k * 4 + 2] = rows[k].z * scales[k];
    }
    light.m[3] = -fit.x0 * sx - 1.0f;
    light.m[7] = -fit.y0 * sx - 1.0f;
    light.m[11] = -zNear * sz;
    light.m[15] = 1.0f;
    return light;
}

// The visible ground seen from the sun along the map's axes x, y and d (FitMap): the least and the greatest of its
// points' places on them, its box in the landscape's space and the points taken.
struct SeenGround {
    Vector3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
    Box box;
    int points = 0;
};
static std::vector<Vector3> g_groupPositions;   // SeeGround's, kept for its room

// The visible ground by the terrain pieces' runs of vertices (a piece wholly outside the view skipped by its box, the
// planes it lies inside not tested again): a run wholly inside the camera's view by its box's corners (a box reaches
// above the run's low ground: seen from the sun its top lies a little further along y), one the view's sides cross by
// every second vertex inside them, as the camera sees it.
static SeenGround SeeGround(const Frame& f, const Matrix4& camera, const Vector3& x, const Vector3& y,
                            const Vector3& d) {
    Plane planes[kViewPlanes];
    ViewPlanes(camera, planes);
    SeenGround seen;
    auto take = [&](const Vector3& at) {
        const Vector3 l{Dot(x, at), Dot(y, at), Dot(d, at)};
        seen.low = Min(seen.low, l);
        seen.high = Max(seen.high, l);
        seen.box.Extend(at);
        ++seen.points;
    };
    auto crossedBy = [&](const TerrainGroup& g, int asked) { return CrossedPlanes(g.box, planes, asked); };
    for (const TerrainPiece& piece : f.terrainPieces) {
        const TerrainGroup& whole = TerrainPieceWhole(piece);
        const int pieceCrossed = whole.count != 0 ? crossedBy(whole, kAllPlanes) : -1;
        if (pieceCrossed < 0) continue;
        for (const TerrainGroup& group : TerrainPieceGroups(piece)) {
            const Vector3 &low = group.box.low, &high = group.box.high;
            const int crossed = crossedBy(group, pieceCrossed);
            if (crossed < 0) continue;
            if (crossed == 0) {
                for (int i = 0; i < 8; ++i) take(Vector3{(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z});
                continue;
            }
            TerrainGroupPositions(piece, group, 2, &g_groupPositions);
            for (const Vector3& at : g_groupPositions) {
                bool inside = true;
                for (int k = 0; k < kViewPlanes && inside; ++k) inside = !(crossed & (1 << k)) || planes[k].Side(at) >= 0.0f;
                if (inside) take(at);
            }
        }
    }
    return seen;
}

// "groundreach" (render_checks.h): the ground the camera's clip space keeps, by the terrain pieces' runs as SeeGround
// takes them but through the exact clip planes (0 <= z <= w, |x|, |y| <= w): the farthest point of it from the camera,
// and the runs the far side (z <= w) cuts or leaves out while the other sides keep some of them.
static bool g_measureReach = false;
static GroundReachInfo g_reach, g_lastReach;

static void MeasureGroundReach(const Frame& f) {
    g_reach = GroundReachInfo{};
    if (!g_measureReach) return;
    const std::size_t firstGround = FirstGround(f);
    if (firstGround == SIZE_MAX) return;
    const Matrix4& camera = f.matrices[static_cast<std::size_t>(f.terrainBatches[static_cast<std::size_t>(f.commands[firstGround].index)].matrix)];
    M4 fromClip;
    Vector3 eye{};
    if (!Invert(FromMatrix(camera), &fromClip) || !CameraEye(fromClip, &eye)) return;
    auto row = [&](int i) { return Plane{camera.m[i * 4], camera.m[i * 4 + 1], camera.m[i * 4 + 2], camera.m[i * 4 + 3]}; };
    auto sum = [](const Plane& p, float k, const Plane& q, float l) {
        return Plane{p.a * k + q.a * l, p.b * k + q.b * l, p.c * k + q.c * l, p.d * k + q.d * l};
    };
    const Plane x = row(0), y = row(1), z = row(2), w = row(3);
    // The sides and the near plane, then the far side.
    const Plane planes[kViewPlanes] = {z, sum(w, 1.0f, x, -1.0f), sum(w, 1.0f, x, 1.0f), sum(w, 1.0f, y, -1.0f),
                                       sum(w, 1.0f, y, 1.0f), sum(w, 1.0f, z, -1.0f)};
    constexpr int kFar = 1 << (kViewPlanes - 1), kSides = kAllPlanes & ~kFar;
    g_reach.measured = true;
    float farthest = 0.0f;
    auto take = [&](const Vector3& at) { farthest = std::max(farthest, DistanceSquared(at, eye)); };
    for (const TerrainPiece& piece : f.terrainPieces) {
        for (const TerrainGroup& group : TerrainPieceGroups(piece)) {
            const int sides = CrossedPlanes(group.box, planes, kSides);
            if (sides < 0) continue;
            ++g_reach.runs;
            const int crossed = CrossedPlanes(group.box, planes, kAllPlanes);
            if (crossed < 0 || (crossed & kFar)) ++g_reach.cut;
            if (crossed < 0) continue;
            if (crossed == 0) {
                const Vector3 &low = group.box.low, &high = group.box.high;
                for (int i = 0; i < 8; ++i) take(Vector3{(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z});
                continue;
            }
            TerrainGroupPositions(piece, group, 1, &g_groupPositions);
            for (const Vector3& at : g_groupPositions) {
                bool inside = true;
                for (int k = 0; k < kViewPlanes && inside; ++k) inside = !(crossed & (1 << k)) || planes[k].Side(at) >= 0.0f;
                if (inside) take(at);
            }
        }
    }
    g_reach.reach = std::sqrt(farthest);
}

void SetGroundReach(bool on) { g_measureReach = on; }
GroundReachInfo GroundReachState() { return g_lastReach; }

static const Fit& FitMap(Frame& f) {
    if (g_fit.frame == FrameNumber() && g_fit.pieces == f.terrainPieces.size()) return g_fit;
    g_fit = Fit{};
    g_fit.frame = FrameNumber();
    g_fit.pieces = f.terrainPieces.size();
    const ShadowFrame& shadow = f.shadow;
    if (!shadow.haveSun || f.worldRanges.empty()) return g_fit;

    const std::size_t firstGround = FirstGround(f);
    if (firstGround == SIZE_MAX) return g_fit;
    const TerrainBatch& ground = f.terrainBatches[static_cast<std::size_t>(f.commands[firstGround].index)];
    const Matrix4& camera = f.matrices[static_cast<std::size_t>(ground.matrix)];
    M4 fromClip;
    if (!Invert(FromMatrix(camera), &fromClip)) return g_fit;
    Vector3 eye{};
    const bool haveEye = CameraEye(fromClip, &eye);

    // The sun's direction, at least kLeastElevation above the horizon, travelling down.
    const std::optional<Vector3> travel = MapLightDirection(shadow.sun);
    if (!travel) return g_fit;
    const Vector3 d = *travel;
    // The map's axes: the landscape's x and y where a point meets a level plane along the light (d.z is below
    // -sin(kLeastElevation)), z along the light. A point at the plane's height keeps its place on the map
    // wherever the sun stands, so the map's texels stay on the ground while the sun turns.
    const Vector3 x{1.0f, 0.0f, -d.x / d.z}, y{0.0f, 1.0f, -d.y / d.z};

    const SeenGround seen = SeeGround(f, camera, x, y, d);
    if (seen.points == 0) return g_fit;
    const Vector3 &lo = seen.low, &hi = seen.high;
    const Box& visible = seen.box;
    float extent = std::max(hi.x - lo.x, hi.y - lo.y) + 2.0f;
    extent = std::ceil(extent / kExtentStep) * kExtentStep;
    const float texel = extent / kMapSize;
    // The texels' corners lie on whole texels of the landscape's x and y at the plane's height: the visible
    // ground's middle height in steps of kPlaneStep (ground away from it moves on the map by its height over the
    // plane as the sun turns).
    const float plane = std::round((visible.low.z + visible.high.z) * 0.5f / kPlaneStep) * kPlaneStep;
    const float ox = x.z * plane, oy = y.z * plane;
    const float shiftX = static_cast<float>(g_shadowShift), shiftY = static_cast<float>(g_shadowShift / 2);
    const float x0 = (std::floor(((lo.x + hi.x) * 0.5f - extent * 0.5f - ox) / texel) - shiftX) * texel + ox;
    const float y0 = (std::floor(((lo.y + hi.y) * 0.5f - extent * 0.5f - oy) / texel) - shiftY) * texel + oy;

    g_fit.ok = true;
    g_fit.fromClip = fromClip;
    g_fit.haveEye = haveEye;
    g_fit.eye = eye;
    g_fit.x = x;
    g_fit.y = y;
    g_fit.d = d;
    g_fit.x0 = x0;
    g_fit.y0 = y0;
    g_fit.extent = extent;
    g_fit.groundNear = lo.z;
    g_fit.groundFar = hi.z;
    g_fit.light = LightMatrix(g_fit, lo.z - kDepthMargin, &g_fit.depth);
    g_fit.ground = visible;
    return g_fit;
}

static bool CasterArea(float highestTop, float widest, ShadowCasterRect* area) {
    Timer timer(FrameStats::Time::ShadowMs);
    const Fit& fit = FitMap(g_frame);
    if (!fit.ok) return false;
    // A caster's shadow falls on the visible ground below it along the light: the caster stands
    // above the ground by at most highestTop less the lowest ground, so back towards the sun from it by
    // at most that height over the tangent of the sun's elevation (d is never flatter than 20 degrees).
    // The ground's box swept back that far, widened by the widest object's radius.
    const float back = std::max(0.0f, highestTop - fit.ground.low.z) / std::fabs(fit.d.z);
    const float towardsX = -fit.d.x * back, towardsY = -fit.d.y * back;
    const float margin = std::max(0.0f, widest);
    area->x0 = fit.ground.low.x + std::min(0.0f, towardsX) - margin;
    area->y0 = fit.ground.low.y + std::min(0.0f, towardsY) - margin;
    area->x1 = fit.ground.high.x + std::max(0.0f, towardsX) + margin;
    area->y1 = fit.ground.high.y + std::max(0.0f, towardsY) + margin;
    return true;
}

static Timer g_walkTimer, g_walkCpu;   // the walk's (BeginShadowWalk): its FrameStats time, the overlay's

// A caster outside the view into `draws` through `light` (the landscape's space to a map's clip space), cut out as the
// meshes drawn with its texture are (g_cuts); false when its mesh is not on the GPU.
static bool AddOutsideCaster(CasterDraws& draws, const OutsideCaster& e, const Matrix4& light) {
    Geometry b;
    int first = 0;
    if (!BindCachedMesh(*e.mesh, b, &first)) return false;
    CasterInstance instance{Product(light, e.model), {e.uOffset, e.vOffset}, {}};
    if (e.hasTexture && e.texture) {
        auto cut = g_cuts.find(e.texture.texture);
        AlphaCut a = cut != g_cuts.end() ? cut->second : AlphaCut{true, 0.5f};
        instance.alpha[0] = a.cut ? 1.0f : 0.0f;
        instance.alpha[1] = a.least;
    }
    draws.Add(b, true, e.hasTexture && e.texture ? e.texture : SurfaceView(nullptr), StageSampler(StageState{}), first,
              CachedIndexCount(*e.mesh), instance);
    return true;
}

// A unit's meshes take only the shadows other objects cast. Each unit's nearest depth in the map
// (the least over its receiving meshes' boxes, MeshBatch::box, through their light matrices) is kept on
// its meshes (MeshBatch::front); their receivers compare from there (shaders/shadow_receiver.hlsli), so
// none of the unit's own depths, all at or behind it, darkens it, and every caster nearer the sun does.
static void FigureFronts(Frame& f) {
    // The units' meshes by object, each object's in the frame's order (the least kept as that order finds it), in a
    // list kept from frame to frame.
    static std::vector<std::pair<const Object3D*, std::size_t>> meshes;
    meshes.clear();
    for (std::size_t k = 0; k < f.meshBatches.size(); ++k) {
        const MeshBatch& b = f.meshBatches[k];
        if (b.figure && b.light >= 0 && b.caster) meshes.emplace_back(b.caster, k);
    }
    std::sort(meshes.begin(), meshes.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return std::less<const Object3D*>()(a.first, b.first);
        return a.second < b.second;
    });
    for (std::size_t first = 0, end = 0; first < meshes.size(); first = end) {
        float front = 0.0f;
        for (end = first; end < meshes.size() && meshes[end].first == meshes[first].first; ++end) {
            const MeshBatch& b = f.meshBatches[meshes[end].second];
            const float* row = f.shadow.lights[static_cast<std::size_t>(b.light)].m + 8;   // the map's depth
            float z = row[3];
            for (int k = 0; k < 3; ++k) z += row[k] * (row[k] < 0.0f ? b.box[3 + k] : b.box[k]);
            if (end == first || z < front) front = z;
        }
        for (std::size_t k = first; k < end; ++k) f.meshBatches[meshes[k].second].front = front;
    }
}

// --- The hand-over ----------------------------------------------------------------------------------

void ListShadowPipelines(std::vector<gpu::PipelineDesc>& out) {
    out.push_back(CasterDesc(false));
    out.push_back(CasterDesc(true));
    out.push_back(TerrainCasterDesc());
    out.push_back(RangeDesc(gpu_shaders::shadow_reduce::kIndex));
    out.push_back(RangeDesc(gpu_shaders::shadow_dilate::kIndex));
    out.push_back(PointShadowClearDesc());
}

std::optional<WorldView> WorldCamera(const Frame& f) {
    const std::size_t firstGround = FirstGround(f);
    if (firstGround == SIZE_MAX) return std::nullopt;
    const TerrainBatch& ground = f.terrainBatches[static_cast<std::size_t>(f.commands[firstGround].index)];
    WorldView view;
    view.clip = f.matrices[static_cast<std::size_t>(ground.matrix)];
    M4 fromClip;
    if (!Invert(FromMatrix(view.clip), &fromClip) || !CameraEye(fromClip, &view.eye) || !view.inverse.Invert(view.clip))
        return std::nullopt;
    return view;
}

gpu::Texture MakeShadowDepth(int width, int height, const char* label) {
    gpu::Texture t = gpu::MakeTexture({width, height, 1, 1, kDepthFormat, gpu::TextureUse::Sampled | gpu::TextureUse::DepthTarget, label});
    if (!t) ei::Log("render: the %dx%d shadow depth texture %s could not be made", width, height, label);
    return t;
}

gpu::Sampler ShadowCompareSampler() {
    gpu::SamplerDesc d;
    d.u = d.v = gpu::Wrap::Clamp;
    d.compare = gpu::Compare::LessEqual;
    return gpu::MakeSampler(d);
}

const gpu::Texture& NoShadowTexture() { return g_dummy; }

std::optional<Vector3> MapLightDirection(const Vector3& sun) {
    const float horizontal = lib::Hypot(sun.x, sun.y);
    float down = std::fabs(sun.z);
    if (horizontal + down <= 1e-9f) return std::nullopt;
    const float least = horizontal * lib::Tan(kLeastElevation);
    if (down < least) down = least;
    const float length = lib::Hypot(horizontal, down);
    return Vector3{sun.x / length, sun.y / length, -down / length};
}

bool CameraInverse::Invert(const Matrix4& clip) {
    if (known && std::memcmp(&camera, &clip, sizeof(Matrix4)) == 0) return ok;
    M4 m;
    ok = render::Invert(FromMatrix(clip), &m);
    known = true;
    camera = clip;
    if (ok) std::copy(std::begin(m.m), std::end(m.m), inverse);
    return ok;
}

Matrix4 CameraInverse::Under(const Matrix4& placed) const {
    M4 m;
    std::copy(std::begin(inverse), std::end(inverse), m.m);
    return ToMatrix(Multiply(m, FromMatrix(placed)));
}

int AddOutsideRoofs(CasterDraws& draws, const Matrix4& clip) {
    int added = 0;
    for (const OutsideCaster& e : g_outside)
        if (!e.unit && AddOutsideCaster(draws, e, clip)) ++added;
    return added;
}

std::optional<Box> VisibleGround(Frame& f) {
    const Fit& fit = FitMap(f);
    if (!fit.ok) return std::nullopt;
    return fit.ground;
}

std::optional<Matrix4> InvertMatrix(const Matrix4& m) {
    M4 r;
    if (!Invert(FromMatrix(m), &r)) return std::nullopt;
    return ToMatrix(r);
}

Matrix4 Product(const Matrix4& a, const Matrix4& b) {
    Matrix4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

void ShadowSun(const Vector3& direction) {
    ShadowFrame& s = g_frame.shadow;
    s.haveSun = true;
    s.sun = direction;
}

void BeginCaster(const Object3D* object, bool figure) {
    g_caster = object;
    g_casterFigure = figure;
    g_casterParts = 0;
}
DrawnCasterParts EndCaster() {
    g_caster = nullptr;
    g_casterFigure = false;
    return {g_casterFrame, g_casterParts};
}

std::uint32_t CasterFrame() { return g_casterFrame; }

void CastsShadow(const Object3D* object, float strength) {
    if (!object) return;
    g_frame.shadow.casters[object] = strength;
}

bool ShadowCasterArea(float highestTop, float widest, ShadowCasterRect* area) {
    if (!g_outsideOn || !area) return false;
    return CasterArea(highestTop, widest, area);
}

bool ShadowTerrainArea(float highestTop, ShadowCasterRect* area) {
    if (!area) return false;
    return CasterArea(highestTop, 0.0f, area);
}

bool ShadowCasterReaches(const ShadowCasterSphere& sphere) {
    if (!g_fit.ok || g_fit.frame != FrameNumber()) return false;
    // The sphere's box in the map's space against the map's area across the light, and in front of
    // the ground's far side along it (nothing limits it towards the sun).
    const Vector3 c{sphere.x, sphere.y, sphere.z};
    const float r = sphere.radius, lx = Dot(g_fit.x, c), ly = Dot(g_fit.y, c), lz = Dot(g_fit.d, c);
    const float rx = r * Length(g_fit.x), ry = r * Length(g_fit.y);
    if (lx + rx < g_fit.x0 || lx - rx > g_fit.x0 + g_fit.extent || ly + ry < g_fit.y0 ||
        ly - ry > g_fit.y0 + g_fit.extent || lz - r > g_fit.groundFar)
        return false;
    g_nearestCaster = std::min(g_nearestCaster, lz - r);
    return true;
}

bool CasterPartDrawn(const FigureMesh* part) {
    if (!part) return false;
    if (!g_drawnPartsSorted) {
        std::sort(g_drawnParts.begin(), g_drawnParts.end(), std::less<const FigureMesh*>());
        g_drawnPartsSorted = true;
    }
    return std::binary_search(g_drawnParts.begin(), g_drawnParts.end(), part, std::less<const FigureMesh*>());
}

float CasterStrength(const Object3D* object) {
    const auto& casters = g_frame.shadow.casters;
    auto it = casters.find(object);
    return it == casters.end() ? -1.0f : it->second;
}

void ShadowCasterMesh(const Object3D* object, const ShadowMesh& m) {
    if (!g_outsideOn || m.indexCount < 3 || g_outside.size() >= kMostOutside) return;
    const CachedMesh* kept = KeepCasterMesh(m);
    if (!kept) return;
    OutsideCaster e{};
    e.model = m.model;
    e.mesh = kept;
    e.uOffset = m.uOffset;
    e.vOffset = m.vOffset;
    e.hasTexture = m.texture != nullptr;
    if (e.hasTexture) e.texture = SurfaceView(m.texture);
    e.object = object;
    e.unit = m.unit;
    g_outside.push_back(e);
}

void BeginShadowWalk() {
    g_walkTimer.Begin(FrameStats::Time::ShadowMs);
    g_walkCpu.Begin();
}

void EndShadowWalk() {
    g_walkTimer.End();
    perf::AddCpu(perf::Cpu::Shadow, g_walkCpu.End());
}

void SetOffscreenCasters(OffscreenCasters mode) {
    const bool on = mode != OffscreenCasters::View, walked = mode == OffscreenCasters::Walked;
    if (on != g_outsideOn || walked != g_walkedOnly)
        ei::Log("render: object shadows: %s", walked ? "every caster from the walk, none from the game's draws"
                                              : on   ? "objects outside the view cast too"
                                                     : "only the objects drawn cast");
    g_outsideOn = on;
    g_walkedOnly = walked;
}

void SetShadowView(bool on) {
    if (on != g_shadowView) ei::Log("render: shadow map: %s", on ? "the shadow alone" : "the frame as drawn");
    g_shadowView = on;
}

void SetShadowShift(int texels) {
    if (texels != g_shadowShift) ei::Log("render: shadow map: origin moved by %d, %d texels", texels, texels / 2);
    g_shadowShift = texels;
}

ShadowInfo ShadowState() { return g_lastInfo; }

const Object3D* CurrentCaster() { return g_caster; }
bool CurrentCasterFigure() { return g_caster && g_casterFigure; }

void NoteCasterPart(const FigureMesh* part) {
    if (!part || g_walkedOnly) return;
    ++g_casterParts;
    g_drawnParts.push_back(part);
    g_drawnPartsSorted = false;
}

void CasterDraws::Add(const Geometry& geometry, bool kept, const gpu::Texture& texture, gpu::Sampler sampler,
                      int firstIndex, int indexCount, const CasterInstance& instance) {
    Entry e{};
    e.vertices = geometry.vertices[0];
    e.indices = geometry.indices;
    e.texture = {texture, sampler};
    e.kept = kept ? 1 : 0;
    e.firstIndex = firstIndex;
    e.indexCount = indexCount;
    e.instance = static_cast<int>(instanceData.size());
    draws.push_back(e);
    instanceData.push_back(instance);
}

void CasterDraws::Sort(std::size_t from, std::size_t to) {
    std::sort(draws.begin() + static_cast<std::ptrdiff_t>(from), draws.begin() + static_cast<std::ptrdiff_t>(to),
              [](const Entry& a, const Entry& b) {
                  const auto ka = BindingKey(a), kb = BindingKey(b);
                  return ka != kb ? ka < kb : a.instance < b.instance;
              });
}

bool CasterDraws::Upload() {
    applied = nullptr;
    appliedPipeline = -1;
    instanceOffset = -1;
    if (draws.empty() || !MakeShadowPipelines()) return false;
    instanceOffset = instances.Append(instanceData.data(), instanceData.size() * sizeof(CasterInstance));
    return instanceOffset >= 0;
}

void CasterDraws::Draw(std::size_t from, std::size_t to) {
    if (instanceOffset < 0) return;
    for (std::size_t k = from; k < to; ++k) {
        const Entry& e = draws[k];
        if (e.kept != appliedPipeline) {
            gpu::ApplyPipeline(g_casterPipelines[e.kept]);
            appliedPipeline = e.kept;
        }
        if (!applied || BindingKey(*applied) != BindingKey(e)) {
            const gpu::BufferBinding vertices[kInstanceBuffer + 1] = {
                e.vertices, {instances.Buffer(), static_cast<std::uint32_t>(instanceOffset)}};
            gpu::BindVertexBuffers(0, vertices);
            gpu::BindIndexBuffer(e.indices);
            gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::shadow_caster::kSamplerTex, {&e.texture, 1});
            applied = &e;
        }
        gpu::DrawIndexed(static_cast<std::uint32_t>(e.firstIndex), static_cast<std::uint32_t>(e.indexCount), 1,
                         static_cast<std::uint32_t>(e.instance));
    }
}

void CasterDraws::Clear() {
    draws.clear();
    instanceData.clear();
    applied = nullptr;
    appliedPipeline = -1;
    instanceOffset = -1;
}

void ShadowFrameDone() {
    g_lastReach = g_reach;
    g_lastInfo = g_info;
    g_info = ShadowInfo{};
    g_outside.clear();
    g_drawnParts.clear();
    g_drawnPartsSorted = true;
    ++g_casterFrame;
    g_nearestCaster = 1e30f;
    TerrainCastersDone();
}

// The world's ground batches and the models drawn after the first of them, listed once for this frame's walks.
static void ListWorld(Frame& f) {
    f.worldGround.clear();
    f.worldMeshes.clear();
    const CachedMesh* lastMesh = nullptr;
    int lastFirst = -1;
    for (const Frame::Range& r : f.worldRanges) {
        const std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = r.first; k < end; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Kind::Terrain) {
                if (!f.terrainBatches[static_cast<std::size_t>(c.index)].water) f.worldGround.push_back(c.index);
                continue;
            }
            if (f.worldGround.empty() || c.kind != Command::Kind::Mesh) continue;
            const MeshBatch& b = f.meshBatches[static_cast<std::size_t>(c.index)];
            if (!b.model || (b.flat && !b.decal)) continue;
            WorldMesh m{c.index, AddsLight(b.state), false};
            if (!m.additive) {
                m.repeat = b.cached == lastMesh && b.firstIndex == lastFirst;
                lastMesh = b.cached;
                lastFirst = b.firstIndex;
            }
            f.worldMeshes.push_back(m);
        }
    }
}

// Every model mesh the world draws after the ground (figures, objects; not the sky, the flat geometry but the ground
// prints, or additive effects) gets its matrix into the map (MeshBatch::light; the meshes of one frame matrix share
// it), the units their fronts.
static void ReceiverMatrices(Frame& f, const Fit& fit) {
    ShadowFrame& sh = f.shadow;
    const M4 lightFromClip = Multiply(FromMatrix(fit.light), fit.fromClip);
    g_info.receivingTerrain += static_cast<int>(f.worldGround.size());
    static std::vector<int> lightOf;   // the frame's matrix index -> light index (-1: none yet)
    lightOf.assign(f.matrices.size(), -1);
    for (const WorldMesh& w : f.worldMeshes) {
        if (w.additive) continue;
        MeshBatch& b = f.meshBatches[static_cast<std::size_t>(w.batch)];
        int& light = lightOf[static_cast<std::size_t>(b.matrix)];
        if (light < 0) {
            M4 m = Multiply(lightFromClip, FromMatrix(f.matrices[static_cast<std::size_t>(b.matrix)]));
            sh.lights.push_back(ToMatrix(m));
            light = static_cast<int>(sh.lights.size()) - 1;
        }
        b.light = light;
        if (b.decal) ++g_info.receivingDecals;
        else ++g_info.receivingMeshes;
    }
    FigureFronts(f);
}

// The filters' discs lie across the light (as the sun's own disc does) and are drawn onto the map: a unit across the
// light in uv, by two axes across it, `level` and `up` (towards the sun's azimuth); the map's v runs against its y.
// The level axis keeps its length, the other is stretched by 1 / sin(elevation): `params`' disc, its inverse and the
// stretch.
static void DiscUniforms(const Fit& fit, ShadowParams& params) {
    const float horizontal = lib::Hypot(fit.d.x, fit.d.y);
    const Vector3 level =
        horizontal < 1e-6f ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{fit.d.y, -fit.d.x, 0.0f} * (1.0f / horizontal);
    const Vector3 up = Cross(fit.d, level);
    const float disc[4] = {Dot(fit.x, level), Dot(fit.x, up), -Dot(fit.y, level), -Dot(fit.y, up)};
    const float discDet = disc[0] * disc[3] - disc[1] * disc[2];
    for (int k = 0; k < 4; ++k) params.disc[k] = disc[k];
    params.discInverse[0] = disc[3] / discDet;
    params.discInverse[1] = -disc[1] / discDet;
    params.discInverse[2] = -disc[2] / discDet;
    params.discInverse[3] = disc[0] / discDet;
    params.stretch = std::max(lib::Hypot(disc[0], disc[2]), lib::Hypot(disc[1], disc[3]));
}

// The receivers' uniforms for the map (shaders/shadow_receiver.hlsli; the clouds' and the roofs' parts are added
// after), the receivers' normal offset and the dilation of the map's depth range.
static void MapUniforms(const Fit& fit, ShadowFrame& shadow) {
    const float extent = fit.extent, depth = fit.depth;
    ShadowParams& params = shadow.params;
    params = ShadowParams{};
    params.on = 1;
    params.strength = kStrength;
    params.shadowView = g_shadowView ? 1 : 0;
    params.depthBias = kDepthBias / depth;
    params.scale[0] = extent * 0.5f;
    params.scale[1] = extent * 0.5f;
    params.scale[2] = depth;
    // The largest depth change per uv unit: kLargestSlope world units of depth per unit across.
    params.largestSlope = kLargestSlope * extent / depth;
    // 1.5 of the map's texels in world units.
    shadow.normalOffset = extent * 0.5f * 2.0f / static_cast<float>(kMapSize) * 1.5f;
    if (fit.haveEye) {
        const Vector3 camera = TransformColumn(fit.light, fit.eye);
        params.camera[0] = camera.x;
        params.camera[1] = camera.y;
        params.camera[2] = camera.z;
        params.fadeFrom = kShadowReach - kShadowFade;
        params.fadeLength = kShadowFade;
    }
    params.sun[0] = fit.d.x;
    params.sun[1] = fit.d.y;
    params.sun[2] = fit.d.z;
    DiscUniforms(fit, params);
    params.softness = depth * kSunSize / extent;
    // The widest filter, in texels across the light: kWidestRadius, at most what the dilation's cells reach on the map.
    constexpr float kWidestReach = (kMostCellsAround - 1) * kRangeCell - 1.0f;
    const float widestTexels = std::min(kWidestRadius / extent * kMapSize, kWidestReach / params.stretch);
    params.searchRadius = widestTexels / kMapSize;
    params.narrowest = kLeastRadius / kMapSize;
    params.texels = static_cast<float>(kMapSize);
    // The cells around a cell that hold every texel the search and the filter read (+1 texel for the
    // bilinear comparisons' second texel), and one cell more: the receivers read the range filtered
    // between the four cells around a pixel, each of which must hold the pixel's taps.
    const float reachTexels = widestTexels * params.stretch + 1.0f;
    shadow.cellsAround = std::min(kMostCellsAround, static_cast<int>(std::ceil(reachTexels / kRangeCell)) + 1);
}

void PrepareShadows(Frame& f) {
    ShadowFrame& sh = f.shadow;
    sh.prepared = true;
    MeasureGroundReach(f);
    ListWorld(f);
    g_info = ShadowInfo{};
    PreparePointShadows(f);
    // The map is drawn with casters; the receivers' matrices into it are made for the clouds too.
    const float cloudLight = PrepareClouds(sh);
    PrepareSurfaces(sh);
    PrepareUnitWeather(f);
    PrepareRoofs(f);
    const SurfaceLevels surfaces = SurfaceState();
    g_info.wetness = surfaces.wet;
    g_info.puddles = surfaces.puddles;
    g_info.snowCover = surfaces.snow;
    for (const MeshBatch& b : f.meshBatches) g_info.groundDecals += b.decal ? 1 : 0;
    const bool casters = !(sh.casters.empty() && g_outside.empty() && TerrainCasterCount() == 0);
    if (!sh.haveSun || f.worldRanges.empty() || (!casters && cloudLight <= 0.0f)) return;
    Timer timer(FrameStats::Time::ShadowMs);
    if (!FitMap(f).ok) return;
    // Along the light from the nearest caster that reaches the map (the ground's near side without one).
    Fit& fit = g_fit;
    fit.light = LightMatrix(fit, std::min(fit.groundNear, g_nearestCaster) - kDepthMargin, &fit.depth);
    sh.lights.push_back(fit.light);
    ReceiverMatrices(f, fit);
    MapUniforms(fit, sh);
    CloudUniforms(fit.x0, fit.y0, fit.extent, cloudLight, &sh.params);
    RoofUniforms(fit.light, &sh.params);
    sh.cloudParams = sh.params;
    sh.cloudParams.on = 0;
    sh.clouds = cloudLight > 0.0f;
    g_info.cloudLight = cloudLight;
    g_info.cloudCover = sh.clouds ? CloudCover() : 0.0f;
    if (!casters) return;
    sh.on = true;
    g_info.made = true;
    g_info.size = kMapSize;
    g_info.extent = fit.extent;
}

// The objects that cast this frame (ShadowInfo::casters, `lost` and `offScreen`), with repeats: the harness build's
// counts (render_checks.h), in a list kept from frame to frame.
#if defined(EI_RENDER_VALIDATION)
static std::vector<const Object3D*> g_counted;
static void CountCaster(const Object3D* object) { g_counted.push_back(object); }
#else
static void CountCaster(const Object3D*) {}
#endif

// The casters the game drew (the batches with a light matrix of the objects it shadows; one for all passes of a mesh)
// into g_casterDraws, each one's cut noted by its texture for the casters outside the view (g_cuts).
static void AddDrawnCasters(const Frame& f) {
    const auto& casters = f.shadow.casters;
    const CachedMesh* lastMesh = nullptr;
    int lastFirst = -1;
    for (const MeshBatch& batch : f.meshBatches) {
        if (batch.light < 0 || !batch.caster || batch.indexCount <= 0) continue;
        auto it = casters.find(batch.caster);
        if (it == casters.end() || it->second < kLeastStrength) continue;
        if (batch.cached == lastMesh && batch.firstIndex == lastFirst) continue;   // a later pass of the same mesh
        lastMesh = batch.cached;
        lastFirst = batch.firstIndex;
        Geometry geometry;
        int first = 0;
        if (!BindMeshBatch(batch, geometry, &first)) continue;
        const PassState& state = batch.state;
        const StageState& stage = state.stages[0];
        CasterInstance instance{f.shadow.lights[static_cast<std::size_t>(batch.light)], {batch.uOffset, batch.vOffset}, {}};
        CasterAlpha(state, instance.alpha);
        if (stage.hasTexture && stage.texture) {
            if (g_cuts.size() >= kMostCuts) g_cuts.clear();
            g_cuts[stage.texture.texture] = AlphaCut{instance.alpha[0] > 0.5f, instance.alpha[1]};
        }
        if (g_walkedOnly) continue;   // the walk hands this caster over instead
        g_casterDraws.Add(geometry, batch.cached != nullptr, stage.texture ? stage.texture : SurfaceView(nullptr),
                          StageSampler(stage), first, batch.indexCount, instance);
        ++g_stats.counts[FrameStats::Count::ShadowDraws];
        ++g_info.casterDraws;
        CountCaster(batch.caster);
    }
}

// The casters outside the view (ShadowCasterMesh) into g_casterDraws through the map's light matrix.
static void AddOutsideCasters(const Fit& fit) {
#if defined(EI_RENDER_VALIDATION)
    const std::size_t drawn = g_counted.size();
#endif
    const Object3D* last = nullptr;
    int corners = 0;
    for (const OutsideCaster& e : g_outside) {
        if (!AddOutsideCaster(g_casterDraws, e, fit.light)) continue;
        ++g_stats.counts[FrameStats::Count::ShadowDraws];
        ++g_info.outsideDraws;
        corners += CachedVertexCount(*e.mesh);
        if (e.object == last) continue;   // an object's meshes come in a row
        last = e.object;
        CountCaster(e.object);
    }
#if defined(EI_RENDER_VALIDATION)
    std::vector<const Object3D*> outside(g_counted.begin() + static_cast<std::ptrdiff_t>(drawn), g_counted.end());
    std::sort(outside.begin(), outside.end(), std::less<const Object3D*>());
    g_info.outsideCasters = static_cast<int>(std::unique(outside.begin(), outside.end()) - outside.begin());
#endif
    g_info.outsideCorners = corners;
}

// The map's pass (g_casterDraws, then the ground's casters), then its depth range per cell, dilated by `cellsAround`.
static void DrawMap(const Fit& fit, int cellsAround) {
    const bool drawCasters = g_casterDraws.Upload();
    gpu::PassDesc pass;
    pass.depth = {g_map, 0, gpu::Load::Clear, 1.0f, true};
    pass.label = "ei-shadow-map";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the shadow map's pass could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Count::Passes];
    if (drawCasters) g_casterDraws.Draw(0, g_casterDraws.Count());
    if (TerrainCasterCount() != 0) {
        namespace program = gpu_shaders::shadow_terrain;
        const ShadowTerrainVsParams vs{fit.light};
        const ShadowTerrainFsParams fs{{fit.d.x, fit.d.y, fit.d.z, 0.0f}};
        gpu::ApplyPipeline(g_terrainCasterPipeline);
        gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockShadowTerrainVsParams, vs);
        gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowTerrainFsParams, fs);
        int draws = DrawTerrainCasters(g_terrainCasterPipeline);
        g_stats.counts[FrameStats::Count::ShadowDraws] += static_cast<std::uint64_t>(draws);
        g_info.terrainCasters = static_cast<int>(TerrainCasterCount());
    }
    gpu::EndPass();
    RangePass(g_reducePipeline, g_cells, g_map, nullptr, "ei-shadow-cells");
    const ShadowDilateParams around{cellsAround, kRangeSize - 1};
    RangePass(g_dilatePipeline, g_range, g_cells, &around, "ei-shadow-range");
}

// The objects that cast (g_counted) and, of those the game shadows, the ones that cast nothing: not drawn at all, or
// drawn but lost here.
static void CountCasters([[maybe_unused]] const Frame& f) {
#if defined(EI_RENDER_VALIDATION)
    const std::less<const Object3D*> before;
    std::sort(g_counted.begin(), g_counted.end(), before);
    g_counted.erase(std::unique(g_counted.begin(), g_counted.end()), g_counted.end());
    g_info.casters = static_cast<int>(g_counted.size());
    std::vector<const Object3D*> handedOver;
    for (const MeshBatch& batch : f.meshBatches)
        if (batch.caster && batch.indexCount > 0) handedOver.push_back(batch.caster);
    std::sort(handedOver.begin(), handedOver.end(), before);
    for (const auto& [object, strength] : f.shadow.casters) {
        if (strength < kLeastStrength || std::binary_search(g_counted.begin(), g_counted.end(), object, before)) continue;
        if (std::binary_search(handedOver.begin(), handedOver.end(), object, before)) ++g_info.lost;
        else ++g_info.offScreen;
    }
    g_counted.clear();
#endif
}

void DrawShadowMap(Frame& f) {
    EnsureDummy();
    if (!f.shadow.on) return;
    Timer timer(FrameStats::Time::ShadowMs);
    if (!g_map) g_map = MakeShadowDepth(kMapSize, kMapSize, "ei-shadow-map");
    if (!g_cells) g_cells = MakeRange(kRangeSize, "ei-shadow-cells");
    if (!g_range) g_range = MakeRange(kRangeSize, "ei-shadow-range");
    if (!MakeShadowPipelines() || !g_map || !g_cells || !g_range) {
        f.shadow.on = false;
        g_info.made = false;
        return;
    }
    // The casters' geometry and instances are written before the pass (no writes inside one).
    UploadTerrainCasters();
    g_casterDraws.Clear();
    AddDrawnCasters(f);
    AddOutsideCasters(g_fit);
    g_casterDraws.Sort(0, g_casterDraws.Count());
    DrawMap(g_fit, f.shadow.cellsAround);
    CountCasters(f);
}

const ShadowParams& ShadowUniforms(const Frame& f, bool receives, bool clouded) {
    static const ShadowParams kNone{};
    if (receives && f.shadow.on) return f.shadow.params;
    return clouded && f.shadow.clouds ? f.shadow.cloudParams : kNone;
}

void BindShadowMap(const Frame& f, int mapSlot) {
    static_assert(gpu_shaders::mesh::kSamplerShadowRange == gpu_shaders::mesh::kSamplerShadowMap + 1 &&
                  gpu_shaders::terrain::kSamplerShadowRange == gpu_shaders::terrain::kSamplerShadowMap + 1);
    const bool on = g_map && g_range && f.shadow.on;
    // The range after the map: with no map its 1 x 1 stand-in (the shaders read neither then).
    const gpu::TextureBinding b[2] = {{on ? g_map : g_dummy, ShadowCompareSampler()},
                                      {on ? g_range : g_rangeDummy, gpu::NearestClampSampler()}};
    gpu::BindTextures(gpu::Stage::Fragment, mapSlot, b);
}

} // namespace render
