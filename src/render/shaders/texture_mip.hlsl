// A level of a composed texture (texture_compose.hlsl) from the one above it (tex0, a view of that level only),
// sampled with a linear filter at each pixel's centre: the mean of the four texels under the pixel where the level
// above is twice the size.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint width, height, levels;
    tex0.GetDimensions(0, width, height, levels);
    float2 size = float2(max(uint2(width, height) >> 1, 1));
    return tex0.SampleLevel(smp, pos.xy / size, 0.0);
}
#endif
