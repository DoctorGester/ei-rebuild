// The screen's ambient occlusion: the world's depth at half the frame's size, the occlusion found in it and blurred,
// which the ground and the meshes lit on the GPU take off their ambient light. See docs/notes/renderer.md, "The
// renderer's source files".
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>

namespace render {

// The radius the occlusion is looked for in (world units: about a unit's height and a half), the most it darkens
// (horizons all round at the surface's normal: 1 - kStrength), the rise above a surface's plane that hides nothing
// (its facets' creases), and the widest radius in texels (a surface just before the camera).
constexpr float kRadius = 1.5f;
constexpr float kStrength = 1.0f;
constexpr float kAngleBias = 0.1f;
constexpr float kWidestTexels = 40.0f;
// The blur leaves out a texel whose depth lies 1 / kBlurSharpness of the distance nearer or further: another surface.
constexpr float kBlurSharpness = 8.0f;

static OcclusionMode g_mode = OcclusionMode::On;
static OcclusionInfo g_info, g_lastInfo;

// The half-size depth and the occlusion (raw, across, then blurred down into `g_occlusion` again).
static int g_width = 0, g_height = 0;
static gpu::Texture g_depth, g_occlusion, g_across;
static gpu::Pipeline g_findPipeline, g_blurPipeline, g_terrainPipeline;
static bool g_pipelinesTried = false;
static CasterDraws g_depthDraws("ei-occlusion-depth");

// The receivers' uniforms this frame (ambient_occlusion.hlsli's `on` and `map`).
static bool g_on = false;
static float g_params[4] = {}, g_map[4] = {};

static gpu::PipelineDesc FullScreenDesc(int program) {
    gpu::PipelineDesc p;
    p.program = program;
    p.colour = gpu::Format::R8;
    return p;
}

static bool MakePipelines() {
    if (!g_pipelinesTried) {
        g_pipelinesTried = true;
        g_findPipeline = gpu::MakePipeline(FullScreenDesc(gpu_shaders::occlusion::kIndex), "ei-occlusion");
        g_blurPipeline = gpu::MakePipeline(FullScreenDesc(gpu_shaders::occlusion_blur::kIndex), "ei-occlusion-blur");
        g_terrainPipeline = gpu::MakePipeline(TerrainDepthDesc(), "ei-occlusion-terrain");
        if (!g_findPipeline || !g_blurPipeline || !g_terrainPipeline)
            ei::Log("render: the ambient occlusion's pipelines could not be created (see the gpu messages above)");
    }
    return g_findPipeline && g_blurPipeline && g_terrainPipeline;
}

// The textures of a `width` x `height` frame's half size; false when they cannot be made.
static bool EnsureTextures(int width, int height) {
    const int w = (width + 1) / 2, h = (height + 1) / 2;
    if (w == g_width && h == g_height && g_depth && g_occlusion && g_across) return true;
    for (gpu::Texture* t : {&g_depth, &g_occlusion, &g_across}) gpu::Release(*t);
    g_width = g_height = 0;
    g_depth = gpu::MakeTexture({w, h, 1, 1, kDepthFormat, gpu::kDepthTarget | gpu::kSampled, "ei-occlusion-depth"});
    g_occlusion = gpu::MakeTexture({w, h, 1, 1, gpu::Format::R8, gpu::kColourTarget | gpu::kSampled, "ei-occlusion"});
    g_across = gpu::MakeTexture({w, h, 1, 1, gpu::Format::R8, gpu::kColourTarget | gpu::kSampled, "ei-occlusion-across"});
    if (!g_depth || !g_occlusion || !g_across) {
        ei::Log("render: the ambient occlusion's %dx%d textures could not be made", w, h);
        return false;
    }
    g_width = w;
    g_height = h;
    return true;
}

// A camera's clip depth z at the clip w is A + B / w (a perspective: its depth row is A times its w row plus B
// times (0, 0, 0, 1)); `out` gets A and B.
static void DepthMap(const Matrix4& clip, float out[2]) {
    const float* m = clip.m;
    const Vector3 depth{m[8], m[9], m[10]}, w{m[12], m[13], m[14]};
    const float ww = LengthSquared(w);
    const float a = ww > 0.0f ? Dot(depth, w) / ww : 0.0f;
    out[0] = a;
    out[1] = m[11] - a * m[15];
}

// A full-screen pass of `pipeline` into `target` reading `textures`.
static void FullScreenPass(gpu::Pipeline pipeline, const gpu::Texture& target,
                           std::span<const gpu::TextureBinding> textures, const void* params, std::uint32_t bytes,
                           const char* label) {
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = gpu::Load::DontCare;
    pass.label = label;
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the ambient occlusion's pass could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Passes];
    gpu::ApplyPipeline(pipeline);
    gpu::BindTextures(gpu::Stage::Fragment, 0, textures);
    gpu::PushUniforms(gpu::Stage::Fragment, 0, params, bytes);
    gpu::Draw(0, 3);
    gpu::EndPass();
}

void SetOcclusion(OcclusionMode mode) {
    static const char* const kNames[] = {"on", "off", "shown alone"};
    if (mode != g_mode) ei::Log("render: ambient occlusion %s", kNames[static_cast<int>(mode)]);
    g_mode = mode;
}

OcclusionInfo OcclusionState() {
    OcclusionInfo info = g_lastInfo;
    GroundOcclusionCounts(&info.groundSectors, &info.groundMade);
    return info;
}

void ListOcclusionPipelines(std::vector<gpu::PipelineDesc>& out) {
    out.push_back(FullScreenDesc(gpu_shaders::occlusion::kIndex));
    out.push_back(FullScreenDesc(gpu_shaders::occlusion_blur::kIndex));
    out.push_back(TerrainDepthDesc());
}

void DrawOcclusion(Frame& f) {
    g_on = false;
    g_info = OcclusionInfo{};
    if (g_mode == OcclusionMode::Off || f.worldRanges.empty()) return;
    Matrix4 camera, fromClip;
    Vector3 eye;
    int width = 0, height = 0;
    if (!WorldCamera(f, &camera, &eye) || !InvertMatrix(camera, &fromClip) || !FrameSize(&width, &height)) return;
    if (!MakePipelines() || !EnsureTextures(width, height)) return;

    // The depth: the world's ground (not the water) and its models after it that write the depth (not the sky,
    // the flat geometry or the light added), cut out by their alpha as the shadow map's casters; the ground and
    // the models lit on the GPU receive.
    g_depthDraws.Clear();
    const CachedMesh* lastMesh = nullptr;
    int lastFirst = -1;
    for (int index : f.worldGround) {
        f.terrainBatches[static_cast<std::size_t>(index)].occluded = true;
        ++g_info.receivingTerrain;
    }
    for (const WorldMesh& w : f.worldMeshes) {
        MeshBatch& b = f.meshBatches[static_cast<std::size_t>(w.batch)];
        if (w.additive || b.flat) continue;
        const PassState& s = b.state;
        if (b.lighting >= 0) {
            b.occluded = true;
            ++g_info.receivingMeshes;
        }
        if (!s.depthWrite || (b.cached && b.cached == lastMesh && b.firstIndex == lastFirst)) continue;
        lastMesh = b.cached;
        lastFirst = b.firstIndex;
        Geometry g;
        int first = 0;
        if (!BindMeshBatch(b, g, &first)) continue;
        const StageState& st = s.stages[0];
        CasterInstance instance{f.matrices[static_cast<std::size_t>(b.matrix)], {b.uOffset, b.vOffset}, {}};
        CasterAlpha(s, instance.alpha);
        g_depthDraws.Add(g, b.cached != nullptr, st.texture ? st.texture : SurfaceView(nullptr), StageSampler(st),
                         first, b.indexCount, instance);
    }
    g_depthDraws.Sort(0, g_depthDraws.Count());
    const bool drawMeshes = g_depthDraws.Upload();
    gpu::PassDesc pass;
    pass.depth = {g_depth, 0, gpu::Load::Clear, 1.0f, true};
    pass.label = "ei-occlusion-depth";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the ambient occlusion's pass could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Passes];
    g_info.terrainDraws = DrawTerrainDepth(f, g_terrainPipeline);
    if (drawMeshes) g_depthDraws.Draw(0, g_depthDraws.Count());
    g_info.meshDraws = static_cast<int>(drawMeshes ? g_depthDraws.Count() : 0);
    gpu::EndPass();

    float depthMap[2];
    DepthMap(camera, depthMap);
    const float* m = camera.m;
    OcclusionParams find{};
    find.fromClip = fromClip;
    find.size[0] = static_cast<float>(g_width);
    find.size[1] = static_cast<float>(g_height);
    find.size[2] = 1.0f / g_width;
    find.size[3] = 1.0f / g_height;
    find.eye[0] = eye.x;
    find.eye[1] = eye.y;
    find.eye[2] = eye.z;
    find.reach[0] = kRadius;
    find.reach[1] = 1.0f / (kRadius * kRadius);
    find.reach[2] = 0.5f * g_height * Length(Vector3{m[4], m[5], m[6]});   // the y row: the projection's scale
    find.reach[3] = kWidestTexels;
    find.depthMap[0] = depthMap[0];
    find.depthMap[1] = depthMap[1];
    find.depthMap[2] = kStrength;
    find.depthMap[3] = kAngleBias;
    const gpu::TextureBinding depth{g_depth, gpu::NearestClampSampler()};
    FullScreenPass(g_findPipeline, g_occlusion, {&depth, 1}, &find, sizeof(find), "ei-occlusion");
    OcclusionBlurParams blur{{1, 0, g_width - 1, g_height - 1}, {depthMap[0], depthMap[1], kBlurSharpness, 0.0f}};
    const gpu::TextureBinding across[2] = {{g_occlusion, gpu::NearestClampSampler()}, depth};
    FullScreenPass(g_blurPipeline, g_across, across, &blur, sizeof(blur), "ei-occlusion-across");
    blur.step[0] = 0;
    blur.step[1] = 1;
    const gpu::TextureBinding down[2] = {{g_across, gpu::NearestClampSampler()}, depth};
    FullScreenPass(g_blurPipeline, g_occlusion, down, &blur, sizeof(blur), "ei-occlusion-down");

    g_on = true;
    g_params[0] = 1.0f;
    g_params[1] = depthMap[0];
    g_params[2] = depthMap[1];
    g_params[3] = g_mode == OcclusionMode::View ? 1.0f : 0.0f;
    g_map[0] = static_cast<float>(g_width) / width;
    g_map[1] = static_cast<float>(g_height) / height;
    g_map[2] = 1.0f / g_width;
    g_map[3] = 1.0f / g_height;
    g_info.made = true;
    g_info.width = g_width;
    g_info.height = g_height;
}

void OcclusionUniforms(bool receives, float on[4], float map[4]) {
    const bool taken = receives && g_on;
    for (int k = 0; k < 4; ++k) {
        on[k] = taken ? g_params[k] : 0.0f;
        map[k] = taken ? g_map[k] : 0.0f;
    }
}

void BindOcclusion(int slot) {
    const gpu::Texture& none = WhiteView();
    const gpu::TextureBinding b[2] = {{g_on ? g_occlusion : none, gpu::NearestClampSampler()},
                                      {g_on ? g_depth : none, gpu::NearestClampSampler()}};
    gpu::BindTextures(gpu::Stage::Fragment, slot, b);
}

void OcclusionFrameDone() {
    g_lastInfo = g_info;
    g_on = false;
}

} // namespace render
