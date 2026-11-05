// Textures the game builds from images (render_compose.cpp): a rectangle of an image drawn texel for texel into a
// rectangle of the texture's first level, over what is there (premultiplied alpha, the pipeline's blending) or
// replacing it, or the rectangle filled with one colour; then the levels below (texture_mip.hlsl).
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct TextureComposeParams {
    int2 offset;   // the image texel drawn at target texel (0, 0)
    int fill;      // with `colour`
    int alpha;     // the alpha kept (render::BlankAlpha: 0 none, opaque; 1 only its top bit, a 1:5:5:5 texture's; 2 all)
    float4 colour;
};
ConstantBuffer<TextureComposeParams> params : FS_BLOCK(0);
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    if (params.fill != 0) return params.colour;
    float4 c = tex0.Load(int3(int2(pos.xy) + params.offset, 0));
    if (params.alpha == 0) c.a = 1.0;
    else if (params.alpha == 1) c.a = c.a >= 0.5 ? 1.0 : 0.0;
    return c;
}
#endif
