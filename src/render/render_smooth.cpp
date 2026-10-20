// The smooth option's multisampling (R12a; docs/notes/renderer.md section 11): render.cpp draws the
// world view's commands into a multisampled colour and depth target and resolves the colour into the
// frame target at the end of each such pass; the interface draws into the frame target as without the
// option. Here: the sample count the device gives and the two copies between the targets where the
// frame changes from one to the other (the frame target's colour and depth into every sample before a
// world run that follows interface draws; sample 0 of the multisampled depth into the frame target's
// depth after a world run).
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <cstdio>

namespace render {

namespace {

using namespace detail;

// A triangle covering the target; the pixel shader reads the source at its own pixel.
const char* const kIntoMultisampledSource = R"(
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
Texture2D<float4> colour : register(t0);
Texture2D<float> depth : register(t1);
float4 ps_main(float4 pos : SV_Position, out float z : SV_Depth) : SV_Target0 {
    int3 at = int3(pos.xy, 0);
    z = depth.Load(at);
    return colour.Load(at);
}
)";

// %d: the sample count (shader model 4 wants the multisampled texture's count in its type).
const char* const kDepthFromMultisampledFormat = R"(
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
Texture2DMS<float, %d> depth : register(t0);
float4 ps_main(float4 pos : SV_Position, out float z : SV_Depth) : SV_Target0 {
    z = depth.Load(int2(pos.xy), 0);
    return float4(0.0, 0.0, 0.0, 0.0);
}
)";

sg_shader g_intoShader{}, g_fromShader{};
sg_pipeline g_into{}, g_from{};
int g_copySamples = 0;   // the sample count the copies were made for
sg_sampler g_point{};

// A shader of `source` reading `views` textures (t0..), each paired with one non-filtering sampler
// (sokol wants every texture paired; the shaders only Load).
sg_shader CopyShader(const char* source, int views, bool multisampled, const char* label) {
    sg_shader_desc s{};
    s.vertex_func.source = source;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = source;
    s.fragment_func.entry = "ps_main";
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_NONFILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    for (int v = 0; v < views; ++v) {
        s.views[v].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        s.views[v].texture.image_type = SG_IMAGETYPE_2D;
        s.views[v].texture.sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT;
        s.views[v].texture.multisampled = multisampled;
        s.views[v].texture.hlsl_register_t_n = static_cast<std::uint8_t>(v);
        s.texture_sampler_pairs[v].stage = SG_SHADERSTAGE_FRAGMENT;
        s.texture_sampler_pairs[v].view_slot = static_cast<std::uint8_t>(v);
        s.texture_sampler_pairs[v].sampler_slot = 0;
    }
    s.label = label;
    return sg_make_shader(&s);
}

// Depth always written, colour as `writeColour`, for `samples`.
sg_pipeline CopyPipeline(sg_shader shader, int samples, bool writeColour, const char* label) {
    sg_pipeline_desc p{};
    p.shader = shader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = SG_COMPAREFUNC_ALWAYS;
    p.depth.write_enabled = true;
    p.colors[0].pixel_format = kColourFormat;
    p.colors[0].write_mask = writeColour ? SG_COLORMASK_RGBA : SG_COLORMASK_NONE;
    p.sample_count = samples;
    p.label = label;
    return sg_make_pipeline(&p);
}

void Draw(sg_pipeline pipeline, const sg_view* views, int count) {
    sg_apply_pipeline(pipeline);
    sg_bindings b{};
    for (int v = 0; v < count; ++v) b.views[v] = views[v];
    b.samplers[0] = g_point;
    sg_apply_bindings(&b);
    sg_draw(0, 3, 1);
}

} // namespace

namespace detail {

int SupportedSamples(ID3D11Device* device) {
    for (int samples = kSmoothSamples; samples > 1; samples /= 2) {
        UINT colour = 0, depth = 0;
        // kColourFormat (RGBA8) and kDepthFormat (sokol's DEPTH: a D32_FLOAT view of R32_TYPELESS).
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(samples), &colour)) &&
            SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, static_cast<UINT>(samples), &depth)) &&
            colour > 0 && depth > 0)
            return samples;
    }
    return 1;
}

bool PrepareSmoothCopies(int samples) {
    if (g_copySamples == samples) return true;
    DestroySmooth();
    char from[1024];
    std::snprintf(from, sizeof(from), kDepthFromMultisampledFormat, samples);
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_NEAREST;
    d.mag_filter = SG_FILTER_NEAREST;
    d.label = "ei-smooth-copy";
    g_point = sg_make_sampler(&d);
    g_intoShader = CopyShader(kIntoMultisampledSource, 2, false, "ei-into-multisampled");
    g_fromShader = CopyShader(from, 1, true, "ei-depth-from-multisampled");
    g_into = CopyPipeline(g_intoShader, samples, true, "ei-into-multisampled");
    g_from = CopyPipeline(g_fromShader, 1, false, "ei-depth-from-multisampled");
    bool ok = sg_query_pipeline_state(g_into) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(g_from) == SG_RESOURCESTATE_VALID;
    if (!ok) {
        ei::Log("render: the smooth option: the copies between the targets could not be made (see the sokol messages above)");
        DestroySmooth();
        return false;
    }
    g_copySamples = samples;
    return true;
}

void CopyIntoMultisampled(sg_view colour, sg_view depth) {
    const sg_view views[2] = {colour, depth};
    Draw(g_into, views, 2);
}

void CopyDepthFromMultisampled(sg_view depth) { Draw(g_from, &depth, 1); }

void DestroySmooth() {
    if (g_into.id) sg_destroy_pipeline(g_into);
    if (g_from.id) sg_destroy_pipeline(g_from);
    if (g_intoShader.id) sg_destroy_shader(g_intoShader);
    if (g_fromShader.id) sg_destroy_shader(g_fromShader);
    if (g_point.id) sg_destroy_sampler(g_point);
    g_into = g_from = sg_pipeline{};
    g_intoShader = g_fromShader = sg_shader{};
    g_point = sg_sampler{};
    g_copySamples = 0;
}

} // namespace detail

} // namespace render
