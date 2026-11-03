// The image blit (render_blit.cpp: the cursor, the movies, the fills): a rectangle of an image onto the frame, texel
// for pixel, cut out where the texel's alpha (the cursor's mask) is below a half.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct BlitParams {
    float4 dest;     // x, y: destination left, top; z, w: source texels per destination pixel
    float4 source;   // x, y: source left, top; z, w: texture width, height
};
ConstantBuffer<BlitParams> params : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 texel = params.source.xy + floor((pos.xy - params.dest.xy) * params.dest.zw);
    texel = clamp(texel, 0.0, params.source.zw - 1.0);
    float4 c = tex.Load(int3(int2(texel), 0));
    if (c.a < 0.5) discard;   // the colour key
    return float4(c.rgb, 1.0);
}
#endif
