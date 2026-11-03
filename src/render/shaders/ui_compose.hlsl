// The run's colour over what the canvas held (tex1, a copy) by its coverage (tex0), rounded as
// render::text::ComposeAgainstBlack does over black (ui_canvas.hlsli). Both hold the run in its row (source.xy as in
// ui_glyph.hlsl).
#include "ui_canvas.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
Texture2D<float4> tex1 : FS_TEXTURE(1);
SamplerState smp1 : FS_SAMPLER(1);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int3 p = int3(int2(pos.xy) - ui.source.xy, 0);
    uint cover = uint(tex0.Load(p).r * 255.0 + 0.5);
    if (cover == 0u) discard;
    uint3 under = Bytes(tex1.Load(p).rgb);
    uint3 text = uint3(ui.colour.rgb);
    return FromBytes((text * cover + under * (255u - cover) + 127u) / 255u);
}
#endif
