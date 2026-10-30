// The build supplies sokol descriptors for each compiled shader, independent of the renderer's backend.
#include "native_shaders/all_shaders.h"
#include "render_internal.h"

namespace render::detail {
sg_shader_desc ShaderDescription(ShaderProgram program) {
    switch (program) {
    case ShaderProgram::Blit:
        return *blit_generated::blit_shader_desc(sg_query_backend());
    case ShaderProgram::Solid:
        return *solid_generated::solid_shader_desc(sg_query_backend());
    case ShaderProgram::Mesh:
        return *mesh_generated::mesh_shader_desc(sg_query_backend());
    case ShaderProgram::Terrain:
        return *terrain_generated::terrain_shader_desc(sg_query_backend());
    case ShaderProgram::Present:
        return *present_generated::present_shader_desc(sg_query_backend());
    case ShaderProgram::IntoMultisampled:
        return *into_multisampled_generated::into_multisampled_shader_desc(sg_query_backend());
    case ShaderProgram::Depth2:
        return *depth_from_multisampled2_generated::depth_from_multisampled2_shader_desc(sg_query_backend());
    case ShaderProgram::Depth4:
        return *depth_from_multisampled4_generated::depth_from_multisampled4_shader_desc(sg_query_backend());
    case ShaderProgram::Depth8:
        return *depth_from_multisampled8_generated::depth_from_multisampled8_shader_desc(sg_query_backend());
    case ShaderProgram::Caster:
        return *shadow_caster_generated::shadow_caster_shader_desc(sg_query_backend());
    case ShaderProgram::TerrainCaster:
        return *shadow_terrain_generated::shadow_terrain_shader_desc(sg_query_backend());
    case ShaderProgram::Reduce:
        return *shadow_reduce_generated::shadow_reduce_shader_desc(sg_query_backend());
    case ShaderProgram::Dilate:
        return *shadow_dilate_generated::shadow_dilate_shader_desc(sg_query_backend());
    case ShaderProgram::PerfOverlay:
        return *perf_overlay_generated::perf_overlay_shader_desc(sg_query_backend());
    case ShaderProgram::TextureCompose:
        return *texture_compose_generated::texture_compose_shader_desc(sg_query_backend());
    case ShaderProgram::CloudFrame:
        return *texture_clouds_frame_generated::texture_clouds_frame_shader_desc(sg_query_backend());
    case ShaderProgram::Clouds:
        return *texture_clouds_generated::texture_clouds_shader_desc(sg_query_backend());
    case ShaderProgram::UiSolid:
        return *ui_solid_generated::ui_solid_shader_desc(sg_query_backend());
    case ShaderProgram::UiGlyph:
        return *ui_glyph_generated::ui_glyph_shader_desc(sg_query_backend());
    case ShaderProgram::UiCompose:
        return *ui_compose_generated::ui_compose_shader_desc(sg_query_backend());
    case ShaderProgram::UiGrey:
        return *ui_grey_generated::ui_grey_shader_desc(sg_query_backend());
    case ShaderProgram::UiCopy:
        return *ui_copy_generated::ui_copy_shader_desc(sg_query_backend());
    case ShaderProgram::UiRgb565:
        return *ui_rgb565_generated::ui_rgb565_shader_desc(sg_query_backend());
    case ShaderProgram::UiBlit:
        return *ui_blit_generated::ui_blit_shader_desc(sg_query_backend());

    default: return {};
    }
}
}
