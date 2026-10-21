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
#include "shaders/shadow_range_vs.h"
#include "shaders/shadow_reduce_ps.h"
#include "shaders/shadow_dilate_ps.h"

static_assert(kSmoothSamples == 8, "a depth_from_multisampled shader for each count up to it (CMakeLists.txt)");

} // namespace

const ShaderBytecode kBlitShader{SG_RANGE(blit_vs), SG_RANGE(blit_ps)};
const ShaderBytecode kClearDepthShader{SG_RANGE(clear_depth_vs), SG_RANGE(clear_depth_ps)};
const ShaderBytecode kMeshShader{SG_RANGE(mesh_vs), SG_RANGE(mesh_ps)};
const ShaderBytecode kTerrainShader{SG_RANGE(terrain_vs), SG_RANGE(terrain_ps)};
const ShaderBytecode kPresentShader{SG_RANGE(present_vs), SG_RANGE(present_ps)};
const ShaderBytecode kIntoMultisampledShader{SG_RANGE(into_multisampled_vs), SG_RANGE(into_multisampled_ps)};
const ShaderBytecode kCasterShader{SG_RANGE(shadow_caster_vs), SG_RANGE(shadow_caster_ps)};
const ShaderBytecode kReduceShader{SG_RANGE(shadow_range_vs), SG_RANGE(shadow_reduce_ps)};
const ShaderBytecode kDilateShader{SG_RANGE(shadow_range_vs), SG_RANGE(shadow_dilate_ps)};

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
