// Object shadows through a sun shadow map. See docs/notes/renderer.md, "The renderer's source files".
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <optional>
#include <unordered_set>

namespace render {

namespace {

using namespace detail;

// The map: kMapSize texels a side over the visible ground (its larger side seen from the sun, in
// steps of kExtentStep world units so that the texel size changes seldom), the origin snapped to
// whole texels (shadows do not swim while the camera moves). Along the light it reaches from the
// nearest caster towards the sun (every caster whose bounds are in the map's area seen from the
// sun and in front of the ground's far side; none is too high) to kDepthMargin behind the ground.
constexpr int kMapSize = 4096;
constexpr float kExtentStep = 8.0f;
constexpr float kDepthMargin = 2.0f;
// The sun at least this far above the horizon (the old shadows' 20 degrees).
constexpr float kLeastElevation = 20.0f * kPi / 180.0f;
// How dark a fully shadowed pixel gets (the old decal: alpha 0x80 of black), the receivers' constant
// depth bias in world units, the largest depth slope taken per world unit across.
constexpr float kStrength = 0.5f;
constexpr float kDepthBias = 0.04f;
constexpr float kLargestSlope = 4.0f;
constexpr float kLeastStrength = 0.5f;   // casters weaker than this (units fading in) do not cast
// Soft edges: the filter's radius in world units is the distance along the light from the
// receiver to the blocker times kSunSize (the tangent of the sun's chosen angular radius: far larger
// than the real sun's, for the old blurred decals' look), at least kLeastRadius texels, at most
// kWidestRadius units (also the blocker search's radius).
constexpr float kSunSize = 0.025f;
constexpr float kWidestRadius = 0.6f;
constexpr float kLeastRadius = 1.5f;
// The cells of the map's depth range (shaders/shadow_reduce.hlsl, the receivers' early end): kRangeCell texels a
// side, so kRangeSize cells a side; the dilation reaches at most kMostCellsAround cells.
constexpr int kRangeCell = 16;
constexpr int kRangeSize = kMapSize / kRangeCell;
constexpr int kMostCellsAround = 8;
static_assert(kMapSize == 4096 && kRangeCell == 16, "the constants written in shaders/shadow_reduce.hlsl and shadow_receiver.hlsli");

// --- Matrices in double ----------------------------------------------------------------------------

// The camera's clip matrix inverted and multiplied back into the game's clip matrices (FitMap, UnderCamera,
// PrepareShadows' light matrices): their product nearly cancels, so the inverse and the products through it
// are double (a float inverse moves still objects by the camera's rounding, render_point_shadows.cpp's
// kKeptPlace); everything else is float.
struct M4 {
    double m[16];
};

M4 FromMatrix(const Matrix4& a) {
    M4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = a.m[i];
    return r;
}

Matrix4 ToMatrix(const M4& a) {
    Matrix4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = static_cast<float>(a.m[i]);
    return r;
}

M4 Multiply(const M4& a, const M4& b) {
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
bool CameraEye(const M4& fromClip, Vector3* eye) {
    const double* r = fromClip.m;
    const double w = r[14];
    if (std::fabs(w) <= 1e-12) return false;
    *eye = Vector3{static_cast<float>(r[2] / w), static_cast<float>(r[6] / w), static_cast<float>(r[10] / w)};
    return true;
}

// Gauss-Jordan with partial pivoting; false when singular.
bool Invert(const M4& a, M4* out) {
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

const void* g_caster = nullptr;     // BeginCaster .. EndCaster
bool g_casterFigure = false;        // ... a unit's draw
ShadowInfo g_info, g_lastInfo;

// --- Casters outside the view --------------------------------------------------------------------

// A mesh of ShadowCasterMesh: its model matrix into the landscape's space, its corners kept on the GPU by
// part (KeepCasterMesh) and its texture offsets.
struct OutsideCaster {
    Matrix4 model;
    const CachedMesh* mesh;
    float uOffset, vOffset;
    sg_view texture;
    bool hasTexture;
    const void* object;
};
std::vector<OutsideCaster> g_outside;
// More casters than this in one frame (no present for a long time) are dropped.
constexpr std::size_t kMostOutside = 100'000;
std::unordered_set<const void*> g_drawnParts;   // NoteCasterPart, this frame
// CastsShadow's strengths by object, made at the first CasterStrength of a frame.
std::unordered_map<const void*, float> g_strengths;
std::uint32_t g_strengthsFrame = 0xFFFFFFFFu;
std::size_t g_strengthsCount = 0;
bool g_outsideOn = true;   // SetOffscreenCasters: not View
bool g_walkedOnly = false; // SetOffscreenCasters: Walked
bool g_shadowView = false; // SetShadowView
int g_shadowShift = 0;     // SetShadowShift: texels
// How the meshes drawn on screen with a texture cut it out (alpha test or blending, and the least alpha
// kept), by the texture's view: a caster outside the view with that texture cuts it out the same way;
// one whose texture was not drawn yet cuts below one half (a texture with alpha is drawn alpha-tested
// or blended; without alpha nothing is cut).
struct AlphaCut {
    bool cut;
    float least;
};
std::unordered_map<std::uint32_t, AlphaCut> g_cuts;
constexpr std::size_t kMostCuts = 8192;
// The nearest depth along the light of a caster that reaches the map this frame (ShadowCasterReaches):
// the map's near plane. Casters the game draws outside the walked objects' area are kept in front of
// it by the caster shader (depths before the near plane drawn at it).
float g_nearestCaster = 1e30f;

// --- GPU side ---------------------------------------------------------------------------------------

// The casters' pipelines (CasterDraws): MeshLayout's position and texture coordinates in vertex buffers 0 and 1,
// the draw's CasterInstance in kInstanceBuffer as kInstanceAttrs float4s (shaders/shadow_caster.hlsl's TEXCOORD1-5).
constexpr int kInstanceBuffer = 2;
constexpr int kInstanceAttrs = 5;
static_assert(sizeof(CasterInstance) == 16 * kInstanceAttrs);
CasterDraws g_casterDraws("ei-shadow-casters");   // the sun map's
sg_shader g_casterShader{};
sg_pipeline g_casterPipelines[2]{};   // the frame's stream, the meshes kept on the GPU (MeshLayout)
sg_shader g_terrainCasterShader{};    // the ground's casters (shaders/shadow_terrain.hlsl)
sg_pipeline g_terrainCasterPipeline{};
sg_image g_map{}, g_dummy{};
sg_view g_mapAttachment{}, g_mapTexture{}, g_dummyAttachment{}, g_dummyTexture{};
sg_sampler g_compare{};
bool g_dummyCleared = false;
sg_shader g_reduceShader{}, g_dilateShader{};
sg_pipeline g_reducePipeline{}, g_dilatePipeline{};
sg_image g_cells{}, g_range{};
sg_view g_cellsAttachment{}, g_cellsTexture{}, g_rangeAttachment{}, g_rangeTexture{};
sg_sampler g_point{};
sg_sampler g_rangeLinear{};   // the receivers read the range filtered
sg_image g_rangeDummy{};      // the range's 1 x 1 stand-in without a map (an image the filtered read accepts)
sg_view g_rangeDummyAttachment{}, g_rangeDummyTexture{};
int g_cellsAround = 1;   // the dilation of this frame's map (PrepareShadows)

bool MakeCasterPipelines() {
    if (g_casterShader.id) return sg_query_pipeline_state(g_casterPipelines[1]) == SG_RESOURCESTATE_VALID;
    sg_shader_desc s{};
    SetBytecode(s, kCasterShader);
    s.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    s.attrs[0].hlsl_sem_name = "POSITION";
    for (int k = 1; k <= kInstanceAttrs + 1; ++k) {   // the texture coordinates, then the instance's
        s.attrs[k].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        s.attrs[k].hlsl_sem_name = "TEXCOORD";
        s.attrs[k].hlsl_sem_index = static_cast<std::uint8_t>(k - 1);
    }
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    s.label = "ei-shadow-caster";
    g_casterShader = sg_make_shader(&s);
    if (sg_query_shader_state(g_casterShader) != SG_RESOURCESTATE_VALID) {
        ei::Log("render: the shadow casters' shader could not be created (see the sokol messages above)");
        return false;
    }
    bool ok = true;
    for (int cached = 0; cached < 2; ++cached) {   // the frame's stream, the meshes kept on the GPU
        sg_pipeline_desc p{};
        p.shader = g_casterShader;
        MeshLayout(p, cached != 0, true);
        p.layout.buffers[kInstanceBuffer].stride = sizeof(CasterInstance);
        p.layout.buffers[kInstanceBuffer].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        for (int k = 0; k < kInstanceAttrs; ++k) {   // the light matrix's rows, then the offsets and the cut
            sg_vertex_attr_state& a = p.layout.attrs[2 + k];
            a.buffer_index = kInstanceBuffer;
            a.offset = 16 * k;
            a.format = SG_VERTEXFORMAT_FLOAT4;
        }
        p.index_type = SG_INDEXTYPE_UINT32;
        p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        p.cull_mode = SG_CULLMODE_NONE;
        p.colors[0].pixel_format = SG_PIXELFORMAT_NONE;   // depth only
        p.depth.pixel_format = kDepthFormat;
        p.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        p.depth.write_enabled = true;
        p.depth.bias_slope_scale = 1.5f;
        p.sample_count = 1;
        p.label = "ei-shadow-caster";
        g_casterPipelines[cached] = sg_make_pipeline(&p);
        ok = ok && sg_query_pipeline_state(g_casterPipelines[cached]) == SG_RESOURCESTATE_VALID;
    }
    if (!ok) ei::Log("render: the shadow casters' pipeline could not be created (see the sokol messages above)");
    return ok;
}

// The ground's casters: TerrainCasterVertex (position, normal), the light matrix in the vertex
// uniforms, the sun's direction in the fragment ones.
bool MakeTerrainCasterPipeline() {
    if (g_terrainCasterShader.id) return sg_query_pipeline_state(g_terrainCasterPipeline) == SG_RESOURCESTATE_VALID;
    sg_shader_desc s{};
    SetBytecode(s, kTerrainCasterShader);
    s.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    s.attrs[0].hlsl_sem_name = "POSITION";
    s.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    s.attrs[1].hlsl_sem_name = "NORMAL";
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(Matrix4);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = 16;
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    s.label = "ei-shadow-terrain";
    g_terrainCasterShader = sg_make_shader(&s);
    sg_pipeline_desc p{};
    p.shader = g_terrainCasterShader;
    p.layout.buffers[0].stride = sizeof(TerrainCasterVertex);
    p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[0].offset = 0;
    p.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[1].offset = 12;
    p.index_type = SG_INDEXTYPE_UINT32;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.cull_mode = SG_CULLMODE_NONE;
    p.colors[0].pixel_format = SG_PIXELFORMAT_NONE;   // depth only
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    p.depth.write_enabled = true;
    p.depth.bias_slope_scale = 1.5f;
    p.sample_count = 1;
    p.label = "ei-shadow-terrain";
    g_terrainCasterPipeline = sg_make_pipeline(&p);
    bool ok = sg_query_shader_state(g_terrainCasterShader) == SG_RESOURCESTATE_VALID &&
              sg_query_pipeline_state(g_terrainCasterPipeline) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the ground casters' pipeline could not be created (see the sokol messages above)");
    return ok;
}

bool MakeColour(int size, sg_pixel_format format, const char* label, sg_image* image, sg_view* attachment,
                sg_view* texture) {
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = size;
    d.height = size;
    d.pixel_format = format;
    d.sample_count = 1;
    d.label = label;
    *image = sg_make_image(&d);
    sg_view_desc v{};
    v.color_attachment.image = *image;
    *attachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = *image;
    *texture = sg_make_view(&v);
    bool ok = sg_query_image_state(*image) == SG_RESOURCESTATE_VALID &&
              sg_query_view_state(*attachment) == SG_RESOURCESTATE_VALID &&
              sg_query_view_state(*texture) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the %dx%d shadow image %s could not be made", size, size, label);
    return ok;
}

void DestroyTarget(sg_image* image, sg_view* attachment, sg_view* texture) {
    if (attachment->id) sg_destroy_view(*attachment);
    if (texture->id) sg_destroy_view(*texture);
    if (image->id) sg_destroy_image(*image);
    *image = sg_image{};
    *attachment = *texture = sg_view{};
}

sg_sampler PointSampler() {
    if (g_point.id) return g_point;
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_NEAREST;
    d.mag_filter = SG_FILTER_NEAREST;
    d.mipmap_filter = SG_FILTER_NEAREST;
    d.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    d.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    d.label = "ei-shadow-point";
    g_point = sg_make_sampler(&d);
    return g_point;
}

// The receivers' read of the range: bilinear between the four cells around the point, clamped at
// the map's edges (shaders/shadow_receiver.hlsli).
sg_sampler RangeSampler() {
    if (g_rangeLinear.id) return g_rangeLinear;
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_LINEAR;
    d.mag_filter = SG_FILTER_LINEAR;
    d.mipmap_filter = SG_FILTER_NEAREST;
    d.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    d.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    d.label = "ei-shadow-range-linear";
    g_rangeLinear = sg_make_sampler(&d);
    return g_rangeLinear;
}

// A full-screen pass's shader and pipeline into an RGBA32F image: `fragment` reads view 0 (of
// `sampleType`, with the point sampler) and, with `uniforms`, 16 bytes of fragment uniforms.
bool MakeRangePipeline(const ShaderBytecode& code, sg_image_sample_type sampleType, bool uniforms, const char* label,
                       sg_shader* shader, sg_pipeline* pipeline) {
    if (shader->id) return sg_query_pipeline_state(*pipeline) == SG_RESOURCESTATE_VALID;
    sg_shader_desc s{};
    SetBytecode(s, code);
    if (uniforms) {
        s.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
        s.uniform_blocks[0].size = 16;
        s.uniform_blocks[0].hlsl_register_b_n = 0;
    }
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = sampleType;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_NONFILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    s.label = label;
    *shader = sg_make_shader(&s);
    sg_pipeline_desc p{};
    p.shader = *shader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.colors[0].pixel_format = SG_PIXELFORMAT_RGBA32F;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    p.label = label;
    *pipeline = sg_make_pipeline(&p);
    bool ok = sg_query_shader_state(*shader) == SG_RESOURCESTATE_VALID &&
              sg_query_pipeline_state(*pipeline) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the shadow map's %s pass could not be created (see the sokol messages above)", label);
    return ok;
}

bool MakeRange(int size, const char* label, sg_image* image, sg_view* attachment, sg_view* texture) {
    return MakeColour(size, SG_PIXELFORMAT_RGBA32F, label, image, attachment, texture);
}

void RangePass(sg_pipeline pipeline, sg_view attachment, sg_view source, const int* uniforms, const char* label) {
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.attachments.colors[0] = attachment;
    pass.label = label;
    sg_begin_pass(&pass);
    ++g_stats.passes;
    sg_apply_pipeline(pipeline);
    sg_bindings b{};
    b.views[0] = source;
    b.samplers[0] = PointSampler();
    sg_apply_bindings(&b);
    if (uniforms) {
        sg_range r{uniforms, 16};
        sg_apply_uniforms(0, &r);
    }
    sg_draw(0, 3, 1);
    sg_end_pass();
}

void EnsureDummy() {
    if (!g_rangeDummy.id)
        MakeRange(1, "ei-shadow-range-none", &g_rangeDummy, &g_rangeDummyAttachment, &g_rangeDummyTexture);
    if (!g_dummy.id && !MakeShadowDepth(1, 1, "ei-shadow-none", &g_dummy, &g_dummyAttachment, &g_dummyTexture)) return;
    if (g_dummyCleared) return;
    sg_pass pass{};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.attachments.depth_stencil = g_dummyAttachment;
    pass.label = "ei-shadow-none";
    sg_begin_pass(&pass);
    sg_end_pass();
    g_dummyCleared = true;
}

// --- GPU time (harness build): timestamp queries around the frame's passes and the shadow pass ------

#if defined(EI_RENDER_VALIDATION)
struct GpuQueries {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* stamps[3] = {};
    bool pending = false;
};
GpuQueries g_queries[4];
int g_querySlot = -1;   // the slot of the frame being drawn, -1: none

void ReleaseQueries() {
    for (GpuQueries& q : g_queries) {
        if (q.disjoint) q.disjoint->Release();
        for (ID3D11Query*& s : q.stamps) {
            if (s) s->Release();
            s = nullptr;
        }
        q.disjoint = nullptr;
        q.pending = false;
    }
    g_querySlot = -1;
}

void CollectQueries() {
    ID3D11DeviceContext* context = D3DContext();
    for (GpuQueries& q : g_queries) {
        if (!q.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
        if (context->GetData(q.disjoint, &d, sizeof(d), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 t[3] = {};
        bool ready = true;
        for (int k = 0; k < 3 && ready; ++k)
            ready = context->GetData(q.stamps[k], &t[k], sizeof(t[k]), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!ready) continue;
        q.pending = false;
        if (d.Disjoint || d.Frequency == 0) continue;
        double ms = 1000.0 / static_cast<double>(d.Frequency);
        FrameStats& s = g_stats;
        ++s.gpuFrames;
        s.gpuShadowMs += static_cast<double>(t[1] - t[0]) * ms;
        s.gpuFrameMs += static_cast<double>(t[2] - t[0]) * ms;
    }
}
#endif

// --- The map's fit ----------------------------------------------------------------------------------

// A half-space of the landscape's space: a x + b y + c z + d >= 0.
struct Plane {
    float a, b, c, d;
    float Side(const Vector3& at) const { return a * at.x + b * at.y + c * at.z + d; }
};

// The camera's view as the fit takes it, in the landscape's space (clip space's rows combined): in front of
// the camera (w >= 0), its sides widened by 5% (|x|, |y| <= 1.05 w) and before the far plane (z <= w).
constexpr int kViewPlanes = 6;
void ViewPlanes(const Matrix4& camera, Plane planes[kViewPlanes]) {
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
    planes[5] = sum(w, 1.0f, z, -1.0f);
}

// Of the view's planes in `asked` (bit k: plane k), those the box low..high crosses; -1 when it is wholly outside
// one of them: its corners farthest out and farthest in along each plane's normal.
constexpr int kAllPlanes = (1 << kViewPlanes) - 1;
int CrossedPlanes(const Vector3& low, const Vector3& high, const Plane planes[kViewPlanes], int asked) {
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
    std::size_t firstGround = SIZE_MAX;  // the world's first ground batch (its command)
    M4 fromClip{};                       // the camera's clip space to the landscape's
    bool haveEye = false;                // the camera's position (none: at infinity)
    Vector3 eye{};
    Vector3 x{}, y{}, d{};               // the map's axes: across, the rest, the light's direction (down)
    float x0 = 0.0f, y0 = 0.0f, extent = 0.0f;   // the map's area along x and y
    float groundNear = 0.0f, groundFar = 0.0f;   // the visible ground's depths along d
    Vector3 lowGround{}, highGround{};   // the visible ground's box in the landscape's space
    // PrepareShadows: the map's depth range (from the nearest caster) and landscape space to its clip space.
    float depth = 0.0f;
    Matrix4 light{};
};
Fit g_fit;

std::size_t FirstGround(const Frame& f) {
    for (const Frame::Range& r : f.worldRanges) {
        std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = r.first; k < end; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Terrain && !f.terrainBatches[static_cast<std::size_t>(c.index)].water) return k;
        }
    }
    return SIZE_MAX;
}

// Landscape space to the map's clip space: x, y -1..1 over the area, z 0..1 from `zNear` to the
// ground's far side.
Matrix4 LightMatrix(const Fit& fit, float zNear, float* depth) {
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

const Fit& FitMap(Frame& f) {
    if (g_fit.frame == FrameNumber() && g_fit.pieces == f.terrainPieces.size()) return g_fit;
    g_fit = Fit{};
    g_fit.frame = FrameNumber();
    g_fit.pieces = f.terrainPieces.size();
    ShadowFrame& sh = f.shadow;
    if (!sh.haveSun || f.worldRanges.empty()) return g_fit;

    const std::size_t firstGround = FirstGround(f);
    if (firstGround == SIZE_MAX) return g_fit;
    const TerrainBatch& ground = f.terrainBatches[static_cast<std::size_t>(f.commands[firstGround].index)];
    const Matrix4& camera = f.matrices[static_cast<std::size_t>(ground.matrix)];
    M4 fromClip;
    if (!Invert(FromMatrix(camera), &fromClip)) return g_fit;
    Vector3 eye{};
    const bool haveEye = CameraEye(fromClip, &eye);

    // The sun's direction, at least kLeastElevation above the horizon, travelling down.
    Vector3 travel;
    if (!MapLightDirection(sh.sun, &travel)) return g_fit;
    const Vector3 d = travel;
    // The sun's view: x across, y the rest, z along the light.
    Vector3 x{d.y, -d.x, 0.0f};
    const float across = std::hypot(x.x, x.y);
    x = across < 1e-6f ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{x.x / across, x.y / across, 0.0f};
    const Vector3 y = Cross(d, x);

    // The visible ground seen from the sun, by the terrain pieces' runs of vertices (a piece wholly outside the
    // view skipped by its box, the planes it lies inside not tested again): a run wholly inside the camera's view
    // by its box's corners (a box reaches above the run's low ground: seen from the sun its top lies
    // a little further along y), one the view's sides cross by every second vertex inside them, as the camera
    // sees it.
    Plane planes[kViewPlanes];
    ViewPlanes(camera, planes);
    Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    Vector3 groundLo{1e30f, 1e30f, 1e30f}, groundHi{-1e30f, -1e30f, -1e30f};
    int seen = 0;
    auto take = [&](const Vector3& at) {
        const Vector3 l{vecmath::Dot<Sum3::XYZ>(x, at), vecmath::Dot<Sum3::XYZ>(y, at), vecmath::Dot<Sum3::XYZ>(d, at)};
        lo = Vector3{std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z)};
        hi = Vector3{std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z)};
        groundLo = Vector3{std::min(groundLo.x, at.x), std::min(groundLo.y, at.y), std::min(groundLo.z, at.z)};
        groundHi = Vector3{std::max(groundHi.x, at.x), std::max(groundHi.y, at.y), std::max(groundHi.z, at.z)};
        ++seen;
    };
    static std::vector<Vector3> positions;
    auto crossedBy = [&](const TerrainGroup& g, int asked) {
        return CrossedPlanes(g.low, g.high, planes, asked);
    };
    for (const TerrainPiece& piece : f.terrainPieces) {
        const TerrainGroup& whole = TerrainPieceWhole(piece);
        const int pieceCrossed = whole.count != 0 ? crossedBy(whole, kAllPlanes) : -1;
        if (pieceCrossed < 0) continue;
        for (const TerrainGroup& group : TerrainPieceGroups(piece)) {
            const Vector3 &low = group.low, &high = group.high;
            const int crossed = crossedBy(group, pieceCrossed);
            if (crossed < 0) continue;
            if (crossed == 0) {
                for (int i = 0; i < 8; ++i) take(Vector3{(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z});
                continue;
            }
            TerrainGroupPositions(piece, group, 2, &positions);
            for (const Vector3& at : positions) {
                bool inside = true;
                for (int k = 0; k < kViewPlanes && inside; ++k) inside = !(crossed & (1 << k)) || planes[k].Side(at) >= 0.0f;
                if (inside) take(at);
            }
        }
    }
    if (seen == 0) return g_fit;
    float extent = std::max(hi.x - lo.x, hi.y - lo.y) + 2.0f;
    extent = std::ceil(extent / kExtentStep) * kExtentStep;
    const float texel = extent / kMapSize;
    const float x0 = (std::floor(((lo.x + hi.x) * 0.5f - extent * 0.5f) / texel) - static_cast<float>(g_shadowShift)) * texel;
    const float y0 = (std::floor(((lo.y + hi.y) * 0.5f - extent * 0.5f) / texel) - static_cast<float>(g_shadowShift / 2)) * texel;

    g_fit.ok = true;
    g_fit.firstGround = firstGround;
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
    g_fit.lowGround = groundLo;
    g_fit.highGround = groundHi;
    return g_fit;
}

bool CasterArea(float highestTop, float widest, ShadowCasterRect* area) {
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::shadowMs);
    const Fit& fit = FitMap(g_frame);
    if (!fit.ok) return false;
    // A caster's shadow falls on the visible ground below it along the light: the caster stands
    // above the ground by at most highestTop less the lowest ground, so back towards the sun from it by
    // at most that height over the tangent of the sun's elevation (d is never flatter than 20 degrees).
    // The ground's box swept back that far, widened by the widest object's radius.
    const float back = std::max(0.0f, highestTop - fit.lowGround.z) / std::fabs(fit.d.z);
    const float towardsX = -fit.d.x * back, towardsY = -fit.d.y * back;
    const float margin = std::max(0.0f, widest);
    area->x0 = fit.lowGround.x + std::min(0.0f, towardsX) - margin;
    area->y0 = fit.lowGround.y + std::min(0.0f, towardsY) - margin;
    area->x1 = fit.highGround.x + std::max(0.0f, towardsX) + margin;
    area->y1 = fit.highGround.y + std::max(0.0f, towardsY) + margin;
    return true;
}

std::optional<StatsTimer> g_walkTimer;

// A unit's meshes take only the shadows other objects cast. Each unit's nearest depth in the map
// (the least over its receiving meshes' boxes, MeshBatch::box, through their light matrices) is kept on
// its meshes (MeshBatch::front); their receivers compare from there (shaders/shadow_receiver.hlsli), so
// none of the unit's own depths, all at or behind it, darkens it, and every caster nearer the sun does.
void FigureFronts(Frame& f) {
    std::unordered_map<const void*, float> fronts;
    for (const MeshBatch& b : f.meshBatches) {
        if (!b.figure || b.light < 0 || !b.caster) continue;
        const float* row = f.shadow.lights[static_cast<std::size_t>(b.light)].m + 8;   // the map's depth
        float z = row[3];
        for (int k = 0; k < 3; ++k) z += row[k] * (row[k] < 0.0f ? b.box[3 + k] : b.box[k]);
        auto [it, added] = fronts.emplace(b.caster, z);
        if (!added && z < it->second) it->second = z;
    }
    if (fronts.empty()) return;
    for (MeshBatch& b : f.meshBatches)
        if (b.figure && b.light >= 0 && b.caster) b.front = fronts[b.caster];
}

} // namespace

// --- The hand-over ----------------------------------------------------------------------------------

namespace detail {

bool WorldCamera(const Frame& f, Matrix4* clip, Vector3* eye) {
    const std::size_t firstGround = FirstGround(f);
    if (firstGround == SIZE_MAX) return false;
    const TerrainBatch& ground = f.terrainBatches[static_cast<std::size_t>(f.commands[firstGround].index)];
    *clip = f.matrices[static_cast<std::size_t>(ground.matrix)];
    M4 fromClip;
    return Invert(FromMatrix(*clip), &fromClip) && CameraEye(fromClip, eye);
}

bool MakeShadowDepth(int width, int height, const char* label, sg_image* image, sg_view* attachment, sg_view* texture) {
    sg_image_desc d{};
    d.usage.depth_stencil_attachment = true;
    d.width = width;
    d.height = height;
    d.pixel_format = kDepthFormat;
    d.sample_count = 1;
    d.label = label;
    *image = sg_make_image(&d);
    sg_view_desc v{};
    v.depth_stencil_attachment.image = *image;
    *attachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = *image;
    *texture = sg_make_view(&v);
    bool ok = sg_query_image_state(*image) == SG_RESOURCESTATE_VALID &&
              sg_query_view_state(*attachment) == SG_RESOURCESTATE_VALID &&
              sg_query_view_state(*texture) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the %dx%d shadow depth image %s could not be made", width, height, label);
    return ok;
}

sg_sampler ShadowCompareSampler() {
    if (g_compare.id) return g_compare;
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_LINEAR;
    d.mag_filter = SG_FILTER_LINEAR;
    d.mipmap_filter = SG_FILTER_NEAREST;
    d.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    d.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    d.compare = SG_COMPAREFUNC_LESS_EQUAL;
    d.label = "ei-shadow-compare";
    g_compare = sg_make_sampler(&d);
    return g_compare;
}

sg_view NoShadowTexture() { return g_dummyTexture; }

bool MapLightDirection(const Vector3& sun, Vector3* travel) {
    const float horizontal = std::hypot(sun.x, sun.y);
    float down = std::fabs(sun.z);
    if (horizontal + down <= 1e-9f) return false;
    const float least = horizontal * std::tan(kLeastElevation);
    if (down < least) down = least;
    const float length = std::hypot(horizontal, down);
    *travel = Vector3{sun.x / length, sun.y / length, -down / length};
    return true;
}

bool UnderCamera(const Matrix4& camera, const Matrix4& placed, Matrix4* model) {
    // The inverse is kept for the camera last asked (one per frame in the world's draw).
    static Matrix4 lastCamera{};
    static M4 lastInverse{};
    static bool haveLast = false;
    if (!haveLast || std::memcmp(&lastCamera, &camera, sizeof(Matrix4)) != 0) {
        haveLast = Invert(FromMatrix(camera), &lastInverse);
        lastCamera = camera;
        if (!haveLast) return false;
    }
    *model = ToMatrix(Multiply(lastInverse, FromMatrix(placed)));
    return true;
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

} // namespace detail

void ShadowSun(const float direction[3]) {
    if (!direction) return;
    ShadowFrame& s = g_frame.shadow;
    s.haveSun = true;
    s.sun = Vector3{direction[0], direction[1], direction[2]};
}

void BeginCaster(const void* object, bool figure) {
    g_caster = object;
    g_casterFigure = figure;
}
void EndCaster() {
    g_caster = nullptr;
    g_casterFigure = false;
}

void CastsShadow(const void* object, float strength) {
    if (!object) return;
    g_frame.shadow.casters.emplace_back(object, strength);
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
    FpuGuard fpu;
    // The sphere's box in the map's space against the map's area across the light, and in front of
    // the ground's far side along it (nothing limits it towards the sun).
    const Vector3 c{sphere.x, sphere.y, sphere.z};
    const float r = sphere.radius, lx = vecmath::Dot<Sum3::XYZ>(g_fit.x, c), ly = vecmath::Dot<Sum3::XYZ>(g_fit.y, c),
                lz = vecmath::Dot<Sum3::XYZ>(g_fit.d, c);
    if (lx + r < g_fit.x0 || lx - r > g_fit.x0 + g_fit.extent || ly + r < g_fit.y0 || ly - r > g_fit.y0 + g_fit.extent ||
        lz - r > g_fit.groundFar)
        return false;
    g_nearestCaster = std::min(g_nearestCaster, lz - r);
    return true;
}

bool CasterPartDrawn(const void* part) { return part && g_drawnParts.count(part) != 0; }

float CasterStrength(const void* object) {
    const auto& casters = g_frame.shadow.casters;
    if (g_strengthsFrame != FrameNumber() || g_strengthsCount != casters.size()) {
        g_strengths.clear();
        for (const auto& c : casters) g_strengths[c.first] = c.second;
        g_strengthsFrame = FrameNumber();
        g_strengthsCount = casters.size();
    }
    auto it = g_strengths.find(object);
    return it == g_strengths.end() ? -1.0f : it->second;
}

void ShadowCasterMesh(const void* object, const ShadowMesh& m) {
    if (!g_outsideOn || m.indexCount < 3 || g_outside.size() >= kMostOutside) return;
    StatsTimer timer(&FrameStats::shadowMs);
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
    g_outside.push_back(e);
}

void BeginShadowWalk() {
    if (!g_walkTimer) g_walkTimer.emplace(&FrameStats::shadowMs);
}

void EndShadowWalk() { g_walkTimer.reset(); }

void SetOffscreenCasters(OffscreenCasters mode) {
#if defined(EI_RENDER_VALIDATION)
    const bool on = mode != OffscreenCasters::View, walked = mode == OffscreenCasters::Walked;
    if (on != g_outsideOn || walked != g_walkedOnly)
        ei::Log("render: object shadows: %s", walked ? "every caster from the walk, none from the game's draws"
                                              : on   ? "objects outside the view cast too"
                                                     : "only the objects drawn cast");
    g_outsideOn = on;
    g_walkedOnly = walked;
#else
    (void)mode;
#endif
}

void SetShadowView(bool on) {
#if defined(EI_RENDER_VALIDATION)
    if (on != g_shadowView) ei::Log("render: shadow map: %s", on ? "the shadow alone" : "the frame as drawn");
    g_shadowView = on;
#else
    (void)on;
#endif
}

void SetShadowShift(int texels) {
#if defined(EI_RENDER_VALIDATION)
    if (texels != g_shadowShift) ei::Log("render: shadow map: origin moved by %d, %d texels", texels, texels / 2);
    g_shadowShift = texels;
#else
    (void)texels;
#endif
}

ShadowInfo ShadowState() { return g_lastInfo; }

namespace detail {

const void* CurrentCaster() { return g_caster; }
bool CurrentCasterFigure() { return g_caster && g_casterFigure; }

void NoteCasterPart(const void* part) {
    if (part && !g_walkedOnly) g_drawnParts.insert(part);
}

void CasterDraws::Add(const sg_bindings& geometry, bool kept, sg_view texture, sg_sampler sampler, int firstIndex,
                      int indexCount, const CasterInstance& instance) {
    Entry e{};
    for (int k = 0; k < 2; ++k) {
        e.vertices[k] = geometry.vertex_buffers[k];
        e.vertexOffsets[k] = geometry.vertex_buffer_offsets[k];
    }
    e.indices = geometry.index_buffer;
    e.indexOffset = geometry.index_buffer_offset;
    e.texture = texture;
    e.sampler = sampler;
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
    if (draws.empty() || !MakeCasterPipelines()) return false;
    instanceOffset = instances.Append(instanceData.data(), instanceData.size() * sizeof(CasterInstance));
    return instanceOffset >= 0;
}

void CasterDraws::Draw(std::size_t from, std::size_t to) {
    if (instanceOffset < 0) return;
    for (std::size_t k = from; k < to; ++k) {
        const Entry& e = draws[k];
        if (e.kept != appliedPipeline) {
            sg_apply_pipeline(g_casterPipelines[e.kept]);
            appliedPipeline = e.kept;
            applied = nullptr;   // sokol takes the bindings again after a pipeline
        }
        if (!applied || BindingKey(*applied) != BindingKey(e)) {
            sg_bindings b{};
            for (int v = 0; v < 2; ++v) {
                b.vertex_buffers[v] = e.vertices[v];
                b.vertex_buffer_offsets[v] = e.vertexOffsets[v];
            }
            b.vertex_buffers[kInstanceBuffer] = instances.Buffer();
            b.vertex_buffer_offsets[kInstanceBuffer] = instanceOffset;
            b.index_buffer = e.indices;
            b.index_buffer_offset = e.indexOffset;
            b.views[0] = e.texture;
            b.samplers[0] = e.sampler;
            sg_apply_bindings(&b);
            applied = &e;
        }
        sg_draw_ex(e.firstIndex, e.indexCount, 1, 0, e.instance);
    }
}

void CasterDraws::Clear() {
    draws.clear();
    instanceData.clear();
    applied = nullptr;
    appliedPipeline = -1;
    instanceOffset = -1;
}

void CasterDraws::Destroy() {
    Clear();
    instances.Destroy();
}

void ShadowFrameDone() {
    g_lastInfo = g_info;
    g_info = ShadowInfo{};
    g_outside.clear();
    g_drawnParts.clear();
    g_nearestCaster = 1e30f;
    TerrainCastersDone();
}

void PrepareShadows(Frame& f) {
    ShadowFrame& sh = f.shadow;
    sh.prepared = true;
    sh.on = false;
    sh.lights.clear();
    g_info = ShadowInfo{};
    PreparePointShadows(f);
    if (!sh.haveSun || f.worldRanges.empty() || (sh.casters.empty() && g_outside.empty() && TerrainCasterCount() == 0))
        return;
    StatsTimer timer(&FrameStats::shadowMs);
    if (!FitMap(f).ok) return;
    // Along the light from the nearest caster that reaches the map (the ground's near side without one).
    Fit& fit = g_fit;
    fit.light = LightMatrix(fit, std::min(fit.groundNear, g_nearestCaster) - kDepthMargin, &fit.depth);
    const std::size_t firstGround = fit.firstGround;
    const Matrix4& light = fit.light;
    const float extent = fit.extent, depth = fit.depth;
    sh.lights.push_back(light);
    const M4 lightFromClip = Multiply(FromMatrix(light), fit.fromClip);

    // Every model mesh the world draws after the ground (figures, objects; not the sky, the flat
    // geometry or additive effects) gets its matrix into the map.
    std::unordered_map<std::uint32_t, int> reused;   // the frame's matrix index -> light index
    for (const Frame::Range& r : f.worldRanges) {
        std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = std::max(r.first, firstGround); k < end; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Terrain) {
                if (!f.terrainBatches[static_cast<std::size_t>(c.index)].water) ++g_info.receivingTerrain;
                continue;
            }
            if (c.kind != Command::Mesh) continue;
            MeshBatch& b = f.meshBatches[static_cast<std::size_t>(c.index)];
            if (!b.model || b.flat) continue;
            if (b.state.blend && b.state.destBlend == 2) continue;   // D3DBLEND_ONE: light added
            auto it = reused.find(static_cast<std::uint32_t>(b.matrix));
            if (it == reused.end()) {
                M4 m = Multiply(lightFromClip, FromMatrix(f.matrices[static_cast<std::size_t>(b.matrix)]));
                sh.lights.push_back(ToMatrix(m));
                it = reused.emplace(static_cast<std::uint32_t>(b.matrix), static_cast<int>(sh.lights.size()) - 1).first;
            }
            b.light = it->second;
            ++g_info.receivingMeshes;
        }
    }
    FigureFronts(f);

    ShadowParams p{};
    p.on[0] = 1.0f;
    p.on[1] = kStrength;
    p.on[2] = g_shadowView ? 1.0f : 0.0f;
    p.on[3] = kDepthBias / depth;
    p.scale[0] = extent * 0.5f;
    p.scale[1] = extent * 0.5f;
    p.scale[2] = depth;
    // The largest depth change per uv unit: kLargestSlope world units of depth per unit across.
    p.scale[3] = kLargestSlope * extent / depth;
    if (fit.haveEye) {
        const Vector3 c = TransformColumn<Sum3::XYZ>(light, fit.eye);
        p.camera[0] = c.x;
        p.camera[1] = c.y;
        p.camera[2] = c.z;
    }
    p.soft[0] = depth * kSunSize / extent;
    // The widest filter, in texels: kWidestRadius, at most what the dilation's cells reach.
    constexpr float kWidestReach = (kMostCellsAround - 1) * kRangeCell - 1.0f;
    const float widestTexels = std::min(kWidestRadius / extent * kMapSize, kWidestReach);
    p.soft[1] = widestTexels / kMapSize;
    p.soft[2] = kLeastRadius / kMapSize;
    p.soft[3] = static_cast<float>(kMapSize);
    // The cells around a cell that hold every texel the search and the filter read (+1 texel for the
    // bilinear comparisons' second texel), and one cell more: the receivers read the range filtered
    // between the four cells around a pixel, each of which must hold the pixel's taps.
    const float reachTexels = widestTexels + 1.0f;
    g_cellsAround = std::min(kMostCellsAround, static_cast<int>(std::ceil(reachTexels / kRangeCell)) + 1);
    sh.params = p;
    sh.on = true;
    g_info.made = true;
    g_info.size = kMapSize;
    g_info.extent = extent;
}

void DrawShadowMap(Frame& f) {
    FpuGuard fpu;
#if defined(EI_RENDER_VALIDATION)
    {
        ID3D11Device* device = D3DDevice();
        ID3D11DeviceContext* context = D3DContext();
        CollectQueries();
        g_querySlot = -1;
        for (int k = 0; k < 4; ++k) {
            GpuQueries& q = g_queries[k];
            if (q.pending) continue;
            bool ok = true;
            if (!q.disjoint) {
                D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
                ok = SUCCEEDED(device->CreateQuery(&desc, &q.disjoint));
                desc.Query = D3D11_QUERY_TIMESTAMP;
                for (ID3D11Query*& s : q.stamps) ok = ok && SUCCEEDED(device->CreateQuery(&desc, &s));
            }
            if (ok) g_querySlot = k;
            break;
        }
        if (g_querySlot >= 0) {
            context->Begin(g_queries[g_querySlot].disjoint);
            context->End(g_queries[g_querySlot].stamps[0]);
        }
    }
#endif
    EnsureDummy();
    if (f.shadow.on) {
        StatsTimer timer(&FrameStats::shadowMs);
        std::unordered_map<const void*, float> casters;
        for (const auto& c : f.shadow.casters) casters[c.first] = c.second;
        bool ok = MakeCasterPipelines() && MakeTerrainCasterPipeline() &&
                  (g_map.id || MakeShadowDepth(kMapSize, kMapSize, "ei-shadow-map", &g_map, &g_mapAttachment, &g_mapTexture)) &&
                  MakeRangePipeline(kReduceShader, SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT, false, "ei-shadow-cells",
                                    &g_reduceShader, &g_reducePipeline) &&
                  MakeRangePipeline(kDilateShader, SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT, true, "ei-shadow-range",
                                    &g_dilateShader, &g_dilatePipeline) &&
                  (g_cells.id || MakeRange(kRangeSize, "ei-shadow-cells", &g_cells, &g_cellsAttachment, &g_cellsTexture)) &&
                  (g_range.id || MakeRange(kRangeSize, "ei-shadow-range", &g_range, &g_rangeAttachment, &g_rangeTexture));
        if (!ok) {
            f.shadow.on = false;
            g_info.made = false;
        } else {
            UploadTerrainCasters();
            sg_pass pass{};
            pass.action.depth.load_action = SG_LOADACTION_CLEAR;
            pass.action.depth.clear_value = 1.0f;
            pass.action.depth.store_action = SG_STOREACTION_STORE;
            pass.attachments.depth_stencil = g_mapAttachment;
            pass.label = "ei-shadow-map";
            sg_begin_pass(&pass);
            ++g_stats.passes;
            std::unordered_map<const void*, bool> counted;
            g_casterDraws.Clear();
            const CachedMesh* lastMesh = nullptr;
            int lastFirst = -1;
            for (const MeshBatch& batch : f.meshBatches) {
                if (batch.light < 0 || !batch.caster || batch.indexCount <= 0) continue;
                auto it = casters.find(batch.caster);
                if (it == casters.end() || it->second < kLeastStrength) continue;
                if (batch.cached == lastMesh && batch.firstIndex == lastFirst) continue;   // a later pass of the same mesh
                lastMesh = batch.cached;
                lastFirst = batch.firstIndex;
                sg_bindings b{};
                int first = 0;
                if (!BindMeshBatch(batch, b, &first)) continue;
                const PassState& s = batch.state;
                const StageState& st = s.stages[0];
                CasterInstance instance{f.shadow.lights[static_cast<std::size_t>(batch.light)], {batch.uOffset, batch.vOffset}, {}};
                if ((s.alphaTest || s.blend) && st.hasTexture) {
                    instance.alpha[0] = 1.0f;
                    instance.alpha[1] = s.alphaTest ? (s.alphaReference + 0.5f) / 255.0f : 0.5f;
                }
                if (st.hasTexture && st.texture.id) {
                    if (g_cuts.size() >= kMostCuts) g_cuts.clear();
                    g_cuts[st.texture.id] = AlphaCut{instance.alpha[0] > 0.5f, instance.alpha[1]};
                }
                if (g_walkedOnly) continue;   // the walk hands this caster over instead
                g_casterDraws.Add(b, batch.cached != nullptr, st.texture.id ? st.texture : SurfaceView(nullptr),
                                  StageSampler(st), first, batch.indexCount, instance);
                ++g_stats.shadowDraws;
                ++g_info.casterDraws;
                counted[batch.caster] = true;
            }
            if (!g_outside.empty()) {
                std::unordered_map<const void*, bool> outside;
                int corners = 0;
                const sg_sampler sampler = StageSampler(StageState{});
                for (const OutsideCaster& e : g_outside) {
                    sg_bindings b{};
                    int first = 0;
                    if (!BindCachedMesh(*e.mesh, b, &first)) continue;
                    CasterInstance instance{Product(g_fit.light, e.model), {e.uOffset, e.vOffset}, {}};
                    if (e.hasTexture && e.texture.id) {
                        auto cut = g_cuts.find(e.texture.id);
                        AlphaCut a = cut != g_cuts.end() ? cut->second : AlphaCut{true, 0.5f};
                        instance.alpha[0] = a.cut ? 1.0f : 0.0f;
                        instance.alpha[1] = a.least;
                    }
                    g_casterDraws.Add(b, true, e.hasTexture && e.texture.id ? e.texture : SurfaceView(nullptr), sampler,
                                      first, CachedIndexCount(*e.mesh), instance);
                    ++g_stats.shadowDraws;
                    ++g_info.outsideDraws;
                    corners += CachedVertexCount(*e.mesh);
                    outside[e.object] = true;
                    counted[e.object] = true;
                }
                g_info.outsideCasters = static_cast<int>(outside.size());
                g_info.outsideCorners = corners;
            }
            g_casterDraws.Sort(0, g_casterDraws.Count());
            if (g_casterDraws.Upload()) g_casterDraws.Draw(0, g_casterDraws.Count());
            if (TerrainCasterCount() != 0) {
                Matrix4 light = g_fit.light;
                sg_range vr{&light, sizeof(light)};
                const float fs[4] = {g_fit.d.x, g_fit.d.y, g_fit.d.z, 0.0f};
                sg_apply_pipeline(g_terrainCasterPipeline);
                sg_apply_uniforms(0, &vr);
                sg_range fr{fs, sizeof(fs)};
                sg_apply_uniforms(1, &fr);
                int draws = DrawTerrainCasters(g_terrainCasterPipeline);
                g_stats.shadowDraws += static_cast<std::uint64_t>(draws);
                g_info.terrainCasters = draws;
            }
            sg_end_pass();
            RangePass(g_reducePipeline, g_cellsAttachment, g_mapTexture, nullptr, "ei-shadow-cells");
            const int around[4] = {g_cellsAround, kRangeSize - 1, 0, 0};
            RangePass(g_dilatePipeline, g_rangeAttachment, g_cellsTexture, around, "ei-shadow-range");
            g_info.casters = static_cast<int>(counted.size());
            // The objects the game shadows that cast nothing: not drawn at all, or drawn but lost here.
            std::unordered_map<const void*, bool> handedOver;
            for (const MeshBatch& batch : f.meshBatches)
                if (batch.caster && batch.indexCount > 0) handedOver[batch.caster] = true;
            for (const auto& c : casters) {
                if (c.second < kLeastStrength || counted.count(c.first)) continue;
                if (handedOver.count(c.first)) ++g_info.lost;
                else ++g_info.offScreen;
            }
        }
    }
    DrawPointShadows(f);
#if defined(EI_RENDER_VALIDATION)
    if (g_querySlot >= 0) D3DContext()->End(g_queries[g_querySlot].stamps[1]);
#endif
}

void ShadowFrameEnd() {
#if defined(EI_RENDER_VALIDATION)
    if (g_querySlot < 0) return;
    GpuQueries& q = g_queries[g_querySlot];
    D3DContext()->End(q.stamps[2]);
    D3DContext()->End(q.disjoint);
    q.pending = true;
    g_querySlot = -1;
#endif
}

void ShadowUniforms(const Frame& f, bool receives, ShadowParams* params) {
    if (receives && f.shadow.on) {
        *params = f.shadow.params;
    } else {
        *params = ShadowParams{};
    }
}

void BindShadowMap(sg_bindings& b, int viewSlot, int samplerSlot) {
    bool on = g_mapTexture.id && g_rangeTexture.id && g_frame.shadow.on;
    b.views[viewSlot] = on ? g_mapTexture : g_dummyTexture;
    b.samplers[samplerSlot] = ShadowCompareSampler();
    // The range, read filtered: with no map its 1 x 1 stand-in (the shaders read neither then).
    b.views[viewSlot + 1] = on ? g_rangeTexture : g_rangeDummyTexture;
    b.samplers[samplerSlot + 1] = RangeSampler();
}

void DescribeShadowMap(sg_shader_desc& s, int viewSlot, int samplerSlot) {
    s.views[viewSlot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[viewSlot].texture.image_type = SG_IMAGETYPE_2D;
    s.views[viewSlot].texture.sample_type = SG_IMAGESAMPLETYPE_DEPTH;
    s.views[viewSlot].texture.hlsl_register_t_n = static_cast<std::uint8_t>(viewSlot);
    s.samplers[samplerSlot].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[samplerSlot].sampler_type = SG_SAMPLERTYPE_COMPARISON;
    s.samplers[samplerSlot].hlsl_register_s_n = static_cast<std::uint8_t>(samplerSlot);
    s.texture_sampler_pairs[viewSlot].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[viewSlot].view_slot = static_cast<std::uint8_t>(viewSlot);
    s.texture_sampler_pairs[viewSlot].sampler_slot = static_cast<std::uint8_t>(samplerSlot);
    int rangeView = viewSlot + 1, rangeSampler = samplerSlot + 1;
    s.views[rangeView].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[rangeView].texture.image_type = SG_IMAGETYPE_2D;
    s.views[rangeView].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[rangeView].texture.hlsl_register_t_n = static_cast<std::uint8_t>(rangeView);
    s.samplers[rangeSampler].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[rangeSampler].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[rangeSampler].hlsl_register_s_n = static_cast<std::uint8_t>(rangeSampler);
    s.texture_sampler_pairs[rangeView].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[rangeView].view_slot = static_cast<std::uint8_t>(rangeView);
    s.texture_sampler_pairs[rangeView].sampler_slot = static_cast<std::uint8_t>(rangeSampler);
}

void DestroyShadows() {
#if defined(EI_RENDER_VALIDATION)
    ReleaseQueries();
#endif
    DestroyPointShadows();
    g_casterDraws.Destroy();
    DestroyTarget(&g_map, &g_mapAttachment, &g_mapTexture);
    DestroyTarget(&g_rangeDummy, &g_rangeDummyAttachment, &g_rangeDummyTexture);
    DestroyTarget(&g_dummy, &g_dummyAttachment, &g_dummyTexture);
    g_dummyCleared = false;
    DestroyTarget(&g_cells, &g_cellsAttachment, &g_cellsTexture);
    DestroyTarget(&g_range, &g_rangeAttachment, &g_rangeTexture);
    for (sg_sampler* s : {&g_point, &g_rangeLinear}) {
        if (s->id) sg_destroy_sampler(*s);
        *s = sg_sampler{};
    }
    for (sg_pipeline* p : {&g_reducePipeline, &g_dilatePipeline}) {
        if (p->id) sg_destroy_pipeline(*p);
        *p = sg_pipeline{};
    }
    for (sg_shader* sh : {&g_reduceShader, &g_dilateShader}) {
        if (sh->id) sg_destroy_shader(*sh);
        *sh = sg_shader{};
    }
    if (g_compare.id) sg_destroy_sampler(g_compare);
    g_compare = sg_sampler{};
    for (sg_pipeline& p : g_casterPipelines) {
        if (p.id) sg_destroy_pipeline(p);
        p = sg_pipeline{};
    }
    if (g_terrainCasterPipeline.id) sg_destroy_pipeline(g_terrainCasterPipeline);
    g_terrainCasterPipeline = sg_pipeline{};
    if (g_terrainCasterShader.id) sg_destroy_shader(g_terrainCasterShader);
    g_terrainCasterShader = sg_shader{};
    if (g_casterShader.id) sg_destroy_shader(g_casterShader);
    g_casterShader = sg_shader{};
}

} // namespace detail

} // namespace render
