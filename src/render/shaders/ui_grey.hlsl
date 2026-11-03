// The greying (tex0: a copy of the canvas; ui_canvas.hlsli).
#include "ui_canvas.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint3 v = Bytes(tex0.Load(int3(int2(pos.xy), 0)).rgb);
    uint grey = (2u * v.r + 4u * v.g + v.b) >> 4;
    return FromBytes(uint3(grey, grey, grey));
}
#endif
