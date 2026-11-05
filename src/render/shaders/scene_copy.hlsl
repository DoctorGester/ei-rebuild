// The world view at its end (render_water.cpp's CopyFrameForWater) at half its size, the colour the next frame's water
// reflections take: per 2 x 2 pixels their mean colour.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> colour : FS_TEXTURE(0);
SamplerState colour_smp : FS_SAMPLER(0);   // unused: read with Load
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint width, height;
    colour.GetDimensions(width, height);
    int2 last = int2(width, height) - 1;
    int2 at = int2(pos.xy) * 2;
    float4 sum = 0.0;
    [unroll] for (int k = 0; k < 4; ++k) sum += colour.Load(int3(min(at + int2(k & 1, k >> 1), last), 0));
    return sum * 0.25;
}
#endif
