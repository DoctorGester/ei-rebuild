// The global map's clouds (clouds.hlsli): two frames blended, each texel's alpha from the ramp.
#include "clouds.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float> from_frame : FS_TEXTURE(0);
SamplerState cloud_smp : FS_SAMPLER(0);
Texture2D<float> to_frame : FS_TEXTURE(1);
SamplerState to_smp : FS_SAMPLER(1);
// A frame's grey level (R8: the level / 255, read back exactly).
uint Grey(Texture2D<float> frame, int3 t) { return uint(round(frame.Load(t) * 255.0)); }
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int3 t = int3(int2(pos.xy), 0);
    uint weight = uint(clouds.blend);
    uint g = (Grey(to_frame, t) * weight + Grey(from_frame, t) * (256u - weight)) >> 8;
    int4 four = clouds.ramp[g >> 2];
    return float4(1.0, 1.0, 1.0, float(four[g & 3u]) / 255.0);
}
#endif
