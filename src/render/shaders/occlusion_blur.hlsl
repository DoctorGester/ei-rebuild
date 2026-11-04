// The ambient occlusion's blur (render_occlusion.cpp), two passes (across, then down) after occlusion.hlsl: a Gaussian
// over kTaps texels each side that leaves out the texels of other surfaces (by their depth against the texel's own,
// relative to its distance), so the 4 x 4 pattern averages away and no surface's occlusion spills onto another's.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
struct OcclusionBlurParams {
    int4 step;          // xy: the direction in texels, zw: the last texel
    float4 depthMap;    // a depth z at the clip w = y / (z - x); z: the weight lost per relative depth difference
};
ConstantBuffer<OcclusionBlurParams> params : FS_BLOCK(0);
Texture2D<float> occlusion : FS_TEXTURE(0);
SamplerState point_smp : FS_SAMPLER(0);   // unused: both are read with Load
Texture2D<float> depth : FS_TEXTURE(1);
SamplerState depth_smp : FS_SAMPLER(1);

static const int kTaps = 4;

float ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 c = int2(pos.xy);
    float z = depth.Load(int3(c, 0));
    if (z >= 1.0) return 1.0;
    float w = params.depthMap.y / (z - params.depthMap.x);
    float sum = occlusion.Load(int3(c, 0)), weights = 1.0;
    [unroll] for (int i = -kTaps; i <= kTaps; ++i) {
        if (i == 0) continue;
        int2 t = clamp(c + params.step.xy * i, int2(0, 0), params.step.zw);
        float zt = depth.Load(int3(t, 0));
        if (zt >= 1.0) continue;
        float wt = params.depthMap.y / (zt - params.depthMap.x);
        float weight = exp(-0.125 * i * i) * saturate(1.0 - abs(wt - w) / w * params.depthMap.z);
        sum += occlusion.Load(int3(t, 0)) * weight;
        weights += weight;
    }
    return sum / weights;
}
#endif
