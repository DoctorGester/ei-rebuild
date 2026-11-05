// The frame as it stands before the water's first draw (render_water.cpp's CopyFrameForWater), at half its size, into
// the images the water's reflection rays are marched through: per 2 x 2 pixels their mean colour and the nearest of
// their depths (the frame target's depth, or the multisampled passes' resolved depth copy), so a thin object stays in
// the half-size depth.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> colour : FS_TEXTURE(0);
SamplerState colour_smp : FS_SAMPLER(0);   // unused: read with Load
Texture2D<float> depth : FS_TEXTURE(1);
SamplerState depth_smp : FS_SAMPLER(1);    // unused: read with Load
struct PsOut {
    float4 colour : SV_Target0;
    float4 depth : SV_Target1;
};
PsOut ps_main(float4 pos : SV_Position) {
    uint width, height;
    colour.GetDimensions(width, height);
    int2 last = int2(width, height) - 1;
    int2 at = int2(pos.xy) * 2;
    float4 sum = 0.0;
    float nearest = 1.0;
    [unroll] for (int k = 0; k < 4; ++k) {
        int3 texel = int3(min(at + int2(k & 1, k >> 1), last), 0);
        sum += colour.Load(texel);
        nearest = min(nearest, depth.Load(texel));
    }
    PsOut o;
    o.colour = sum * 0.25;
    o.depth = float4(nearest, 0.0, 0.0, 1.0);
    return o;
}
#endif
