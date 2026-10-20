// Object shadows through a sun shadow map (R16j; docs/notes/renderer.md section 13). The game hands
// over the sun (CWorldClient::Render), which object each mesh belongs to (BeginCaster, the object
// draws of 0056AFE0) and which objects cast this frame (CastsShadow, from their shadow draw 0051CE60:
// the options' switches and the game's own visibility of the shadow). Once a frame, before the world
// draws, PrepareShadows fits an orthographic view from the sun around the visible ground and gives
// every receiving batch the matrix from its model space to the map; DrawShadowMap draws the casters'
// meshes into the map (depth only; alpha-tested and blended passes cut out by their texture's
// alpha). The terrain and mesh shaders sample it with filtering (ShadowHlsl). With the classic
// option (render::SetClassicShadows) none of this runs and the game's decals draw as before.
//
// Space: the meshes' matrices are clip matrices (projection x view x model), the terrain's the same
// for the landscape's space; that space is the one the map is fitted in (the inverse of the
// terrain's matrix times a mesh's gives the mesh's model matrix into it).
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

namespace render {

namespace {

using namespace detail;

// The map: kMapSize texels a side over the visible ground (its larger side seen from the sun, in
// steps of kExtentStep world units so that the texel size changes seldom), the origin snapped to
// whole texels (shadows do not swim while the camera moves). Casters up to kCasterReach units
// towards the sun from the lowest ground seen cast.
constexpr int kMapSize = 4096;
constexpr double kExtentStep = 8.0;
constexpr double kCasterReach = 160.0;
// The sun at least this far above the horizon (the old shadows' 20 degrees, 0051CE60).
constexpr double kLeastElevation = 20.0 * 3.14159265358979323846 / 180.0;
// How dark a fully shadowed pixel gets (the old decal: alpha 0x80 of black), the receivers' constant
// depth bias in world units, the largest depth slope taken per world unit across.
constexpr float kStrength = 0.5f;
constexpr double kDepthBias = 0.04;
constexpr double kLargestSlope = 4.0;
constexpr float kLeastStrength = 0.5f;   // casters weaker than this (units fading in) do not cast

// --- Matrices in double ----------------------------------------------------------------------------

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

// A point or direction in double (x, y, z) and a homogeneous point.
struct D3 {
    double x, y, z;
};
struct D4 {
    double x, y, z, w;
};

double Dot(const D3& a, const D3& b) { return DotUnrounded<Sum3::XYZ, double>(a.x, a.y, a.z, b.x, b.y, b.z); }

D3 Cross(const D3& a, const D3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// a * v, row by row.
D4 Transform(const M4& a, const D4& v) {
    double r[4];
    for (int i = 0; i < 4; ++i) {
        const double* row = a.m + i * 4;
        r[i] = DotUnrounded<Sum3::XYZ, double>(row[0], row[1], row[2], v.x, v.y, v.z) + row[3] * v.w;
    }
    return {r[0], r[1], r[2], r[3]};
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
bool g_classicDecal = false;        // ClassicShadowDecal .. EndMesh
ShadowInfo g_info, g_lastInfo;

// --- GPU side ---------------------------------------------------------------------------------------

// The casters into the map: positions through the light matrix; a pass with alpha test or blending
// cut out where its stage-0 texel's alpha is below the reference.
const char* const kCasterSource = R"(
cbuffer vs_params : register(b0) { float4 row0; float4 row1; float4 row2; float4 row3; };
cbuffer fs_params : register(b0) { float4 alpha; };   // x: cut out, y: the least alpha kept
struct vs_in { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct vs_out { float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    o.uv = i.uv;
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
void ps_main(vs_out i) {
    if (alpha.x > 0.5 && tex.Sample(smp, i.uv).a < alpha.y) discard;
}
)";

// The receivers' part (included in the terrain and mesh shaders): how much of the sun the map hides
// at a pixel, times the strength. `s` is the pixel's position in the map's clip space, `dsx` / `dsy`
// its screen derivatives (taken before any discard). Surfaces turned away from the sun (their face
// normal, towards the camera) are left as the game lit them. The comparison per tap follows the
// receiver's plane (its depth slope across the map, clamped), 3 x 3 bilinear taps (PCF).
const char* const kReceiverSource = R"(
float shadow_amount(Texture2D<float> map, SamplerComparisonState smp, float3 s, float3 dsx, float3 dsy,
                    float4 on, float4 scale, float4 cam) {
    if (on.x < 0.5) return 0.0;
    float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || s.z > 1.0) return 0.0;
    float3 n = cross(dsx * scale.xyz, dsy * scale.xyz);
    float len = length(n);
    float facing = 1.0;
    if (len > 1e-20) {
        n /= len;
        if (dot(n, (cam.xyz - s) * scale.xyz) < 0.0) n = -n;
        facing = saturate(-n.z * 6.0);   // the sun's light travels along +z
    }
    if (facing <= 0.0) return 0.0;
    float2 ux = float2(dsx.x * 0.5, -dsx.y * 0.5), uy = float2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    float2 slope = 0.0;
    if (abs(det) > 1e-24) slope = float2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -scale.w, scale.w);
    float z = s.z - on.w;
    float lit = 0.0;
    [unroll] for (int j = -1; j <= 1; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
            float2 o = float2(i, j) * on.z;
            lit += map.SampleCmpLevelZero(smp, uv + o, z + dot(slope, o));
        }
    }
    return (1.0 - lit / 9.0) * facing * on.y;
}
)";

struct CasterVs {
    Matrix4 light;
};
struct CasterFs {
    float alpha[4];
};

sg_shader g_casterShader{};
sg_pipeline g_casterPipeline{};
sg_image g_map{}, g_dummy{};
sg_view g_mapAttachment{}, g_mapTexture{}, g_dummyAttachment{}, g_dummyTexture{};
sg_sampler g_compare{};
bool g_dummyCleared = false;

bool MakeCasterPipeline() {
    if (g_casterPipeline.id) return sg_query_pipeline_state(g_casterPipeline) == SG_RESOURCESTATE_VALID;
    sg_shader_desc s{};
    s.vertex_func.source = kCasterSource;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = kCasterSource;
    s.fragment_func.entry = "ps_main";
    s.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    s.attrs[0].hlsl_sem_name = "POSITION";
    s.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    s.attrs[1].hlsl_sem_name = "TEXCOORD";
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(CasterVs);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(CasterFs);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
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
    sg_pipeline_desc p{};
    p.shader = g_casterShader;
    p.layout.buffers[0].stride = sizeof(Vertex);
    p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[0].offset = 0;
    p.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[1].offset = 20;
    p.index_type = SG_INDEXTYPE_UINT32;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.cull_mode = SG_CULLMODE_NONE;
    p.colors[0].pixel_format = SG_PIXELFORMAT_NONE;   // depth only (sokol then takes no colour)
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    p.depth.write_enabled = true;
    p.depth.bias_slope_scale = 1.5f;
    p.sample_count = 1;
    p.label = "ei-shadow-caster";
    g_casterPipeline = sg_make_pipeline(&p);
    bool ok = sg_query_pipeline_state(g_casterPipeline) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the shadow casters' pipeline could not be created (see the sokol messages above)");
    return ok;
}

// A depth image of `size` with an attachment and a texture view.
bool MakeDepth(int size, const char* label, sg_image* image, sg_view* attachment, sg_view* texture) {
    sg_image_desc d{};
    d.usage.depth_stencil_attachment = true;
    d.width = size;
    d.height = size;
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
    if (!ok) ei::Log("render: the %dx%d shadow depth image could not be made", size, size);
    return ok;
}

void DestroyDepth(sg_image* image, sg_view* attachment, sg_view* texture) {
    if (attachment->id) sg_destroy_view(*attachment);
    if (texture->id) sg_destroy_view(*texture);
    if (image->id) sg_destroy_image(*image);
    *image = sg_image{};
    *attachment = *texture = sg_view{};
}

sg_sampler CompareSampler() {
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

// The 1 x 1 map bound when there is none (cleared to the far plane once).
void EnsureDummy() {
    if (!g_dummy.id && !MakeDepth(1, "ei-shadow-none", &g_dummy, &g_dummyAttachment, &g_dummyTexture)) return;
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

// Results of earlier frames that are ready go into this frame's counts.
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
        FrameStats& s = Stats();
        ++s.gpuFrames;
        s.gpuShadowMs += static_cast<double>(t[1] - t[0]) * ms;
        s.gpuFrameMs += static_cast<double>(t[2] - t[0]) * ms;
    }
}
#endif

} // namespace

// --- The hand-over ----------------------------------------------------------------------------------

void ShadowSun(const float direction[3]) {
    if (!Active() || !direction) return;
    ShadowFrame& s = CurrentFrame().shadow;
    s.haveSun = true;
    s.sun = Vector3{direction[0], direction[1], direction[2]};
}

void BeginCaster(const void* object) { g_caster = object; }
void EndCaster() { g_caster = nullptr; }

void CastsShadow(const void* object, float strength) {
    if (!Active() || !object) return;
    CurrentFrame().shadow.casters.emplace_back(object, strength);
}

void ClassicShadowDecal() { g_classicDecal = true; }

ShadowInfo ShadowState() {
    ShadowInfo info = g_lastInfo;
    info.classic = ClassicShadows();
    return info;
}

namespace detail {

const void* CurrentCaster() { return g_caster; }

bool DropClassicDecal() { return g_classicDecal && !ClassicShadows(); }

void EndClassicDecal() { g_classicDecal = false; }

void ShadowFrameDone() {
    g_lastInfo = g_info;
    g_info = ShadowInfo{};
}

const char* ShadowHlsl() { return kReceiverSource; }

void PrepareShadows(Frame& f) {
    ShadowFrame& sh = f.shadow;
    sh.prepared = true;
    sh.on = false;
    sh.lights.clear();
    g_info = ShadowInfo{};
    if (ClassicShadows() || !sh.haveSun || f.worldRanges.empty() || sh.casters.empty()) return;
    StatsTimer timer(&FrameStats::shadowMs);

    // The world's first ground batch gives the camera's matrix (landscape space to clip).
    std::size_t firstGround = SIZE_MAX;
    for (const Frame::Range& r : f.worldRanges) {
        std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = r.first; k < end && firstGround == SIZE_MAX; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Terrain && !f.terrainBatches[static_cast<std::size_t>(c.index)].water) firstGround = k;
        }
        if (firstGround != SIZE_MAX) break;
    }
    if (firstGround == SIZE_MAX) return;
    const TerrainBatch& ground = f.terrainBatches[static_cast<std::size_t>(f.commands[firstGround].index)];
    M4 camera = FromMatrix(f.matrices[static_cast<std::size_t>(ground.matrix)]);
    M4 fromClip;
    if (!Invert(camera, &fromClip)) return;
    // The camera's position: the point clip space puts at w = 0 (x = y = 0).
    D4 eye = Transform(fromClip, D4{0.0, 0.0, 1.0, 0.0});
    bool haveEye = std::fabs(eye.w) > 1e-12;

    // The sun's direction, at least kLeastElevation above the horizon, travelling down.
    D3 d{sh.sun.x, sh.sun.y, sh.sun.z};
    double horizontal = std::sqrt(LengthSquared2DUnrounded<double>(d.x, d.y));
    double down = std::fabs(d.z);
    if (!(horizontal + down > 1e-9)) return;
    double least = horizontal * std::tan(kLeastElevation);
    if (down < least) down = least;
    double length = std::sqrt(LengthSquared2DUnrounded<double>(horizontal, down));
    d = D3{d.x / length, d.y / length, -down / length};
    // The sun's view: x across, y the rest, z along the light.
    D3 x{d.y, -d.x, 0.0};   // d x up
    double across = std::sqrt(LengthSquared2DUnrounded<double>(x.x, x.y));
    x = across < 1e-6 ? D3{1.0, 0.0, 0.0} : D3{x.x / across, x.y / across, 0.0};
    D3 y = Cross(d, x);

    // The visible ground seen from the sun: every second terrain vertex inside the camera's view.
    D3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    int seen = 0;
    for (std::size_t i = 0; i < f.terrainVertices.size(); i += 2) {
        const Vector3& p = f.terrainVertices[i].position;
        D3 at{p.x, p.y, p.z};
        D4 c = Transform(camera, D4{at.x, at.y, at.z, 1.0});
        if (c.w <= 0.0 || std::fabs(c.x) > 1.05 * c.w || std::fabs(c.y) > 1.05 * c.w || c.z > c.w) continue;
        D3 l{Dot(x, at), Dot(y, at), Dot(d, at)};
        lo = D3{std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z)};
        hi = D3{std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z)};
        ++seen;
    }
    if (seen == 0) return;
    double extent = std::max(hi.x - lo.x, hi.y - lo.y) + 2.0;
    extent = std::ceil(extent / kExtentStep) * kExtentStep;
    double texel = extent / kMapSize;
    double x0 = std::floor(((lo.x + hi.x) * 0.5 - extent * 0.5) / texel) * texel;
    double y0 = std::floor(((lo.y + hi.y) * 0.5 - extent * 0.5) / texel) * texel;
    double zNear = lo.z - kCasterReach, zFar = hi.z + 2.0;
    double depth = zFar - zNear;
    // Landscape space to the map's clip space: x, y -1..1 over the extent, z 0..1 from near to far.
    M4 light{};
    double sx = 2.0 / extent, sz = 1.0 / depth;
    const D3 rows[3] = {x, y, d};
    const double scales[3] = {sx, sx, sz};
    for (int k = 0; k < 3; ++k) {
        light.m[k * 4] = rows[k].x * scales[k];
        light.m[k * 4 + 1] = rows[k].y * scales[k];
        light.m[k * 4 + 2] = rows[k].z * scales[k];
    }
    light.m[3] = -x0 * sx - 1.0;
    light.m[7] = -y0 * sx - 1.0;
    light.m[11] = -zNear * sz;
    light.m[15] = 1.0;
    sh.lights.push_back(ToMatrix(light));
    M4 lightFromClip = Multiply(light, fromClip);

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

    ShadowParams p{};
    p.on[0] = 1.0f;
    p.on[1] = kStrength;
    p.on[2] = 1.0f / static_cast<float>(kMapSize);
    p.on[3] = static_cast<float>(kDepthBias / depth);
    p.scale[0] = static_cast<float>(extent * 0.5);
    p.scale[1] = static_cast<float>(extent * 0.5);
    p.scale[2] = static_cast<float>(depth);
    // The largest depth change per uv unit: kLargestSlope world units of depth per unit across.
    p.scale[3] = static_cast<float>(kLargestSlope * extent / depth);
    if (haveEye) {
        D4 c = Transform(light, D4{eye.x / eye.w, eye.y / eye.w, eye.z / eye.w, 1.0});
        p.camera[0] = static_cast<float>(c.x);
        p.camera[1] = static_cast<float>(c.y);
        p.camera[2] = static_cast<float>(c.z);
    }
    std::memcpy(sh.params, &p, sizeof(p));
    sh.on = true;
    g_info.made = true;
    g_info.size = kMapSize;
    g_info.extent = static_cast<float>(extent);
}

void DrawShadowMap(Frame& f) {
    FpuGuard fpu;
#if defined(EI_RENDER_VALIDATION)
    // The frame's GPU time begins here (the frame's first pass follows).
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
        sg_bindings b{};
        bool ok = MakeCasterPipeline() && BindMeshGeometry(b) &&
                  (g_map.id || MakeDepth(kMapSize, "ei-shadow-map", &g_map, &g_mapAttachment, &g_mapTexture));
        if (!ok) {
            f.shadow.on = false;
            g_info.made = false;
        } else {
            sg_pass pass{};
            pass.action.depth.load_action = SG_LOADACTION_CLEAR;
            pass.action.depth.clear_value = 1.0f;
            pass.action.depth.store_action = SG_STOREACTION_STORE;
            pass.attachments.depth_stencil = g_mapAttachment;
            pass.label = "ei-shadow-map";
            sg_begin_pass(&pass);
            ++Stats().passes;
            sg_apply_pipeline(g_casterPipeline);
            std::unordered_map<const void*, bool> counted;
            int lastFirst = -1;
            for (const MeshBatch& batch : f.meshBatches) {
                if (batch.light < 0 || !batch.caster || batch.indexCount <= 0) continue;
                auto it = casters.find(batch.caster);
                if (it == casters.end() || !(it->second >= kLeastStrength)) continue;
                if (batch.firstIndex == lastFirst) continue;   // a later pass of the same mesh
                lastFirst = batch.firstIndex;
                const PassState& s = batch.state;
                const StageState& st = s.stages[0];
                CasterFs fs{};
                if ((s.alphaTest || s.blend) && st.hasTexture) {
                    fs.alpha[0] = 1.0f;
                    fs.alpha[1] = s.alphaTest ? (static_cast<float>(s.alphaReference) + 0.5f) / 255.0f : 0.5f;
                }
                b.views[0] = st.texture.id ? st.texture : SurfaceView(nullptr);
                b.samplers[0] = StageSampler(st);
                sg_apply_bindings(&b);
                CasterVs vs{f.shadow.lights[static_cast<std::size_t>(batch.light)]};
                sg_range vr{&vs, sizeof(vs)};
                sg_apply_uniforms(0, &vr);
                sg_range fr{&fs, sizeof(fs)};
                sg_apply_uniforms(1, &fr);
                sg_draw(batch.firstIndex, batch.indexCount, 1);
                ++Stats().shadowDraws;
                ++g_info.casterDraws;
                counted[batch.caster] = true;
            }
            sg_end_pass();
            g_info.casters = static_cast<int>(counted.size());
        }
    }
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
    if (receives && f.shadow.on) std::memcpy(params, f.shadow.params, sizeof(*params));
    else *params = ShadowParams{};
}

void BindShadowMap(sg_bindings& b, int viewSlot, int samplerSlot) {
    b.views[viewSlot] = g_mapTexture.id && CurrentFrame().shadow.on ? g_mapTexture : g_dummyTexture;
    b.samplers[samplerSlot] = CompareSampler();
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
}

void DestroyShadows() {
#if defined(EI_RENDER_VALIDATION)
    ReleaseQueries();
#endif
    DestroyDepth(&g_map, &g_mapAttachment, &g_mapTexture);
    DestroyDepth(&g_dummy, &g_dummyAttachment, &g_dummyTexture);
    g_dummyCleared = false;
    if (g_compare.id) sg_destroy_sampler(g_compare);
    g_compare = sg_sampler{};
    if (g_casterPipeline.id) sg_destroy_pipeline(g_casterPipeline);
    g_casterPipeline = sg_pipeline{};
    if (g_casterShader.id) sg_destroy_shader(g_casterShader);
    g_casterShader = sg_shader{};
}

} // namespace detail

} // namespace render
