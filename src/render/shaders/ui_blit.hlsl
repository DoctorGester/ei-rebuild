// A canvas onto the frame as blit.hlsl draws a window surface, the colour key compared on the stored colour
// (ui_canvas.hlsli).
#include "ui_canvas.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 texel = float2(ui.source.xy) + floor((pos.xy - float2(ui.dest.xy)) * ui.scale.xy);
    texel = clamp(texel, 0.0, float2(ui.source.zw) - 1.0);
    float4 c = tex0.Load(int3(int2(texel), 0));
    if (ui.key.w != 0 && all(Bytes(c.rgb) == uint3(ui.key.rgb))) discard;
    return float4(c.rgb, 1.0);
}
#endif
