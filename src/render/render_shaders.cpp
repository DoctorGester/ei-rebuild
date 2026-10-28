// The renderer's shaders as bytecode (render_internal.h, ShaderBytecode): written in src/render/shaders/*.hlsl,
// compiled at build time by fxc into the headers included here (CMakeLists.txt's ei_shader,
// src/render/shaders/compile.cmake), with the flags sokol would give d3dcompiler at run time.
#include "render_internal.h"

namespace render::detail {

namespace {

#include "shaders/blit_vs.h"
#include "shaders/blit_ps.h"
#include "shaders/clear_depth_vs.h"
#include "shaders/clear_depth_ps.h"
#include "shaders/mesh_vs.h"
#include "shaders/mesh_ps.h"
#include "shaders/terrain_vs.h"
#include "shaders/terrain_ps.h"
#include "shaders/present_vs.h"
#include "shaders/present_ps.h"
#include "shaders/into_multisampled_vs.h"
#include "shaders/into_multisampled_ps.h"
#include "shaders/depth_from_multisampled_vs.h"
#include "shaders/depth_from_multisampled2_ps.h"
#include "shaders/depth_from_multisampled4_ps.h"
#include "shaders/depth_from_multisampled8_ps.h"
#include "shaders/shadow_caster_vs.h"
#include "shaders/shadow_caster_ps.h"
#include "shaders/shadow_terrain_vs.h"
#include "shaders/shadow_terrain_ps.h"
#include "shaders/shadow_range_vs.h"
#include "shaders/shadow_reduce_ps.h"
#include "shaders/shadow_dilate_ps.h"
#include "shaders/ui_rect_vs.h"
#include "shaders/ui_glyph_vs.h"
#include "shaders/ui_solid_ps.h"
#include "shaders/ui_glyph_ps.h"
#include "shaders/ui_compose_ps.h"
#include "shaders/ui_grey_ps.h"
#include "shaders/ui_copy_ps.h"
#include "shaders/ui_rgb565_ps.h"
#include "shaders/ui_blit_ps.h"
#include "shaders/perf_overlay_vs.h"
#include "shaders/perf_overlay_ps.h"
#include "shaders/texture_compose_vs.h"
#include "shaders/texture_compose_ps.h"
#include "shaders/texture_clouds_vs.h"
#include "shaders/texture_clouds_frame_ps.h"
#include "shaders/texture_clouds_ps.h"

static_assert(kSmoothSamples == 8, "a depth_from_multisampled shader for each count up to it (CMakeLists.txt)");

} // namespace

const ShaderBytecode kBlitShader{SG_RANGE(blit_vs), SG_RANGE(blit_ps)};
const ShaderBytecode kClearDepthShader{SG_RANGE(clear_depth_vs), SG_RANGE(clear_depth_ps)};
const ShaderBytecode kMeshShader{SG_RANGE(mesh_vs), SG_RANGE(mesh_ps)};
const ShaderBytecode kTerrainShader{SG_RANGE(terrain_vs), SG_RANGE(terrain_ps)};
const ShaderBytecode kPresentShader{SG_RANGE(present_vs), SG_RANGE(present_ps)};
const ShaderBytecode kIntoMultisampledShader{SG_RANGE(into_multisampled_vs), SG_RANGE(into_multisampled_ps)};
const ShaderBytecode kCasterShader{SG_RANGE(shadow_caster_vs), SG_RANGE(shadow_caster_ps)};
const ShaderBytecode kTerrainCasterShader{SG_RANGE(shadow_terrain_vs), SG_RANGE(shadow_terrain_ps)};
const ShaderBytecode kReduceShader{SG_RANGE(shadow_range_vs), SG_RANGE(shadow_reduce_ps)};
const ShaderBytecode kDilateShader{SG_RANGE(shadow_range_vs), SG_RANGE(shadow_dilate_ps)};
const ShaderBytecode kUiSolidShader{SG_RANGE(ui_rect_vs), SG_RANGE(ui_solid_ps)};
const ShaderBytecode kUiGlyphShader{SG_RANGE(ui_glyph_vs), SG_RANGE(ui_glyph_ps)};
const ShaderBytecode kUiComposeShader{SG_RANGE(ui_rect_vs), SG_RANGE(ui_compose_ps)};
const ShaderBytecode kUiGreyShader{SG_RANGE(ui_rect_vs), SG_RANGE(ui_grey_ps)};
const ShaderBytecode kUiCopyShader{SG_RANGE(ui_rect_vs), SG_RANGE(ui_copy_ps)};
const ShaderBytecode kUiRgb565Shader{SG_RANGE(ui_rect_vs), SG_RANGE(ui_rgb565_ps)};
const ShaderBytecode kUiBlitShader{SG_RANGE(ui_rect_vs), SG_RANGE(ui_blit_ps)};
const ShaderBytecode kPerfOverlayShader{SG_RANGE(perf_overlay_vs), SG_RANGE(perf_overlay_ps)};
const ShaderBytecode kTextureComposeShader{SG_RANGE(texture_compose_vs), SG_RANGE(texture_compose_ps)};
const ShaderBytecode kCloudFrameShader{SG_RANGE(texture_clouds_vs), SG_RANGE(texture_clouds_frame_ps)};
const ShaderBytecode kCloudsShader{SG_RANGE(texture_clouds_vs), SG_RANGE(texture_clouds_ps)};

ShaderBytecode DepthFromMultisampledShader(int samples) {
    switch (samples) {
    case 2: return {SG_RANGE(depth_from_multisampled_vs), SG_RANGE(depth_from_multisampled2_ps)};
    case 4: return {SG_RANGE(depth_from_multisampled_vs), SG_RANGE(depth_from_multisampled4_ps)};
    case 8: return {SG_RANGE(depth_from_multisampled_vs), SG_RANGE(depth_from_multisampled8_ps)};
    default: return {};
    }
}

void SetBytecode(sg_shader_desc& s, const ShaderBytecode& code) {
    s.vertex_func.bytecode = code.vs;
    s.fragment_func.bytecode = code.ps;
}

} // namespace render::detail
