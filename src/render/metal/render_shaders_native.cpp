#include "native_shaders/all_shaders.h"
#include "render/render_internal.h"
namespace render::detail {
#define EI_NATIVE_SHADER(symbol, name)                                                                                 \
    const ShaderBytecode symbol { {}, {}, name##_generated::name##_shader_desc }
EI_NATIVE_SHADER(kBlitShader, blit);
EI_NATIVE_SHADER(kSolidShader, solid);
EI_NATIVE_SHADER(kMeshShader, mesh);
EI_NATIVE_SHADER(kTerrainShader, terrain);
EI_NATIVE_SHADER(kPresentShader, present);
EI_NATIVE_SHADER(kIntoMultisampledShader, into_multisampled);
EI_NATIVE_SHADER(kCasterShader, shadow_caster);
EI_NATIVE_SHADER(kTerrainCasterShader, shadow_terrain);
EI_NATIVE_SHADER(kReduceShader, shadow_reduce);
EI_NATIVE_SHADER(kDilateShader, shadow_dilate);
EI_NATIVE_SHADER(kPerfOverlayShader, perf_overlay);
EI_NATIVE_SHADER(kTextureComposeShader, texture_compose);
EI_NATIVE_SHADER(kCloudFrameShader, texture_clouds_frame);
EI_NATIVE_SHADER(kCloudsShader, texture_clouds);
EI_NATIVE_SHADER(kUiSolidShader, ui_solid);
EI_NATIVE_SHADER(kUiGlyphShader, ui_glyph);
EI_NATIVE_SHADER(kUiComposeShader, ui_compose);
EI_NATIVE_SHADER(kUiGreyShader, ui_grey);
EI_NATIVE_SHADER(kUiCopyShader, ui_copy);
EI_NATIVE_SHADER(kUiRgb565Shader, ui_rgb565);
EI_NATIVE_SHADER(kUiBlitShader, ui_blit);
#undef EI_NATIVE_SHADER
ShaderBytecode DepthFromMultisampledShader(int samples) {
    switch (samples) {
    case 2:
        return {{}, {}, depth_from_multisampled2_generated::depth_from_multisampled2_shader_desc};
    case 4:
        return {{}, {}, depth_from_multisampled4_generated::depth_from_multisampled4_shader_desc};
    case 8:
        return {{}, {}, depth_from_multisampled8_generated::depth_from_multisampled8_shader_desc};
    default:
        return {};
    }
}
void SetBytecode(sg_shader_desc& shader, const ShaderBytecode& code) {
    if (code.descriptor) shader = *code.descriptor(sg_query_backend());
}
}
