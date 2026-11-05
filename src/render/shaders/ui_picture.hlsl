// A frame picture onto the frame (render_ui.cpp's DrawPicture), stretched by its nearest texel, greyed as
// (2 r + 4 g + b) / 16 when asked (ui_params.hlsli).
#include "ui_params.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 texel = floor((pos.xy - float2(ui.destAt)) * ui.scale);
    texel = clamp(texel, 0.0, float2(ui.sourceSize) - 1.0);
    float4 c = tex0.Load(int3(int2(texel), 0));
    if (ui.grey != 0) {
        uint3 v = Bytes(c.rgb);
        uint grey = (2u * v.r + 4u * v.g + v.b) >> 4;
        return FromBytes(uint3(grey, grey, grey));
    }
    return float4(c.rgb, 1.0);
}
#endif
