// A level of a texture read into a target (render_compose.cpp), sampled at each pixel's centre: with a linear filter
// from the level above a composed texture's level (twice its size: the mean of the four texels under the pixel), or
// with a nearest one texel for texel (the verify build's read of a level).
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct TextureMipParams {
    float4 level;   // x: the level read; y, z: the target's width and height
};
ConstantBuffer<TextureMipParams> params : FS_BLOCK(0);
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    return tex0.SampleLevel(smp, pos.xy / params.level.yz, params.level.x);
}
#endif
