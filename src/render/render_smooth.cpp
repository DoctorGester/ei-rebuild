// The render quality's multisampling (docs/notes/renderer.md section 11): render.cpp draws the world view's
// commands into a multisampled colour and depth target and resolves the colour into the frame target at the
// end of each such pass; the interface draws into the frame target as without the option. Here: the sample
// count the device gives and the two copies between the targets where the frame changes from one to the other
// (the frame target's colour and depth into every sample before a world run that follows interface draws;
// sample 0 of the multisampled depth into the frame target's depth after a world run).
#include "../hook/log.h"
#include "platform/graphics.h"
#include "render.h"
#include "render_internal.h"

namespace render {

namespace {

using namespace detail;

sg_shader g_intoShader{}, g_fromShader{};
sg_pipeline g_into{}, g_from{};
int g_copySamples = 0;   // the sample count the copies were made for
sg_sampler g_point{};

sg_shader CopyShader(ShaderProgram code, const char* label) {
    sg_shader_desc s = ShaderDescription(code);
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


bool PrepareSmoothCopies(int samples) {
    if (g_copySamples == samples) return true;
    DestroySmooth();
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_NEAREST;
    d.mag_filter = SG_FILTER_NEAREST;
    d.label = "ei-smooth-copy";
    g_point = sg_make_sampler(&d);
    g_intoShader = CopyShader(kIntoMultisampledShader, "ei-into-multisampled");
    g_fromShader = CopyShader(kDepthFromMultisampledShader, "ei-depth-from-multisampled");
    g_into = CopyPipeline(g_intoShader, samples, true, "ei-into-multisampled");
    g_from = CopyPipeline(g_fromShader, 1, false, "ei-depth-from-multisampled");
    bool ok = sg_query_pipeline_state(g_into) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(g_from) == SG_RESOURCESTATE_VALID;
    if (!ok) {
        ei::Log("render: msaa: the copies between the targets could not be made (see the sokol messages above)");
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
