// A picture (tex0) stretched over an R16_UINT target as 5:6:5 pixels, each channel's top bits: the save's picture
// (ui_params.hlsli). Each pixel takes the picture's pixel under its centre, (2 x + 1) * picture size / (2 * target size).
#include "ui_params.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
uint ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint2 x = uint2(pos.xy);
    uint2 t = ((2u * x + 1u) * uint2(ui.source.zw)) / (2u * uint2(ui.dest.xy));
    uint3 v = Bytes(tex0.Load(int3(int2(t), 0)).rgb);
    return ((v.r >> 3) << 11) | ((v.g >> 2) << 5) | (v.b >> 3);
}
#endif
